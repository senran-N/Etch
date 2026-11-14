// HarfBuzz OpenType text shaping & Slug quadratic Bezier outline/band extraction.
// Compiled via src/etch_unity.cpp.

namespace etch {

namespace {

inline float unpack_f16_cpu(uint16_t h) noexcept {
    const uint32_t sign_bit = (static_cast<uint32_t>(h) & 0x8000u) << 16;
    const uint32_t exp_bits = (static_cast<uint32_t>(h) >> 10) & 0x1Fu;
    const uint32_t mant_bits = static_cast<uint32_t>(h) & 0x03FFu;
    uint32_t bits = 0u;
    if (exp_bits == 0u) {
        if (mant_bits == 0u) {
            bits = sign_bit;
        } else {
            const float val = static_cast<float>(mant_bits) * (1.0f / 16777216.0f);
            return (sign_bit != 0u) ? -val : val;
        }
    } else if (exp_bits == 31u) {
        bits = sign_bit | 0x7F800000u | (mant_bits << 13);
    } else {
        bits = sign_bit | ((exp_bits + 112u) << 23) | (mant_bits << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

template <typename T>
bool ensure_array_cap(T** ptr, uint32_t* cap, uint32_t needed) noexcept {
    if (needed <= *cap) {
        return true;
    }
    uint32_t new_cap = (*cap > 0u) ? *cap : 256u;
    while (new_cap < needed) {
        if (new_cap > (0x7FFFFFFFu / 2u)) {
            new_cap = needed;
            break;
        }
        new_cap *= 2u;
    }
    T* grown = static_cast<T*>(std::realloc(*ptr, size_t(new_cap) * sizeof(T)));
    if (!grown) {
        return false;
    }
    *ptr = grown;
    *cap = new_cap;
    return true;
}

bool try_load_font_file(FontAtlas* atlas, const char* path) noexcept {
    if (!atlas || !path || path[0] == '\0') {
        return false;
    }
    size_t sz = 0;
    void* raw = SDL_LoadFile(path, &sz);
    if (!raw || sz < 16u || sz > 0x7FFFFFFFu) {
        if (raw) {
            SDL_free(raw);
        }
        return false;
    }
    const unsigned char* bytes = static_cast<const unsigned char*>(raw);
    const int offset = stbtt_GetFontOffsetForIndex(bytes, 0);
    if (offset < 0 || !stbtt_InitFont(&atlas->font_info, bytes, offset)) {
        SDL_free(raw);
        return false;
    }

    hb_blob_t* blob = hb_blob_create(
        reinterpret_cast<const char*>(raw),
        static_cast<unsigned int>(sz),
        HB_MEMORY_MODE_READONLY,
        nullptr,
        nullptr);
    hb_face_t* face = blob ? hb_face_create(blob, 0u) : nullptr;
    hb_font_t* font = face ? hb_font_create(face) : nullptr;
    const unsigned int upem = face ? hb_face_get_upem(face) : 0u;
    if (!blob || !face || !font || upem == 0u) {
        if (font) hb_font_destroy(font);
        if (face) hb_face_destroy(face);
        if (blob) hb_blob_destroy(blob);
        SDL_free(raw);
        return false;
    }

    hb_ot_font_set_funcs(font);
    hb_font_set_scale(font, static_cast<int>(upem), static_cast<int>(upem));

    atlas->font_file_data = static_cast<uint8_t*>(raw);
    atlas->font_file_size = sz;
    stbtt_GetFontVMetrics(&atlas->font_info, &atlas->font_ascent, &atlas->font_descent, &atlas->font_line_gap);
    atlas->hb_blob = blob;
    atlas->hb_face = face;
    atlas->hb_font = font;
    atlas->font_valid = true;

    hb_codepoint_t gid = 0u;
    if (!hb_font_get_nominal_glyph(font, 0x2022u, &gid)) {
        hb_font_get_nominal_glyph(font, static_cast<hb_codepoint_t>('*'), &gid);
    }
    atlas->bullet_glyph_id = gid;
    atlas->bullet_advance_em = static_cast<float>(hb_font_get_glyph_h_advance(font, gid));

    hb_codepoint_t sp_gid = 0u;
    if (hb_font_get_nominal_glyph(font, static_cast<hb_codepoint_t>(' '), &sp_gid)) {
        atlas->space_advance_em = static_cast<float>(hb_font_get_glyph_h_advance(font, sp_gid));
    } else {
        atlas->space_advance_em = static_cast<float>(upem) * 0.35f;
    }
    return true;
}

bool font_atlas_init(FontAtlas* atlas, const char* preferred_font_path) noexcept {
    if (!atlas) {
        return false;
    }
    *atlas = {};
    atlas->slug_glyph_cap = kSlugGlyphTableCapacity;
    atlas->slug_glyphs = static_cast<SlugGlyphEntry*>(std::calloc(atlas->slug_glyph_cap, sizeof(SlugGlyphEntry)));
    atlas->slug_curves = static_cast<SlugCurveGpu*>(std::malloc(size_t(kInitialSlugCurveCap) * sizeof(SlugCurveGpu)));
    atlas->slug_curve_cap = atlas->slug_curves ? kInitialSlugCurveCap : 0u;
    atlas->slug_bands = static_cast<SlugBandEntryGpu*>(std::malloc(size_t(kInitialSlugBandCap) * sizeof(SlugBandEntryGpu)));
    atlas->slug_band_cap = atlas->slug_bands ? kInitialSlugBandCap : 0u;
    atlas->pixels = static_cast<uint8_t*>(std::malloc(size_t(kAtlasSize) * kAtlasSize * 4u));
    atlas->hb_buf = hb_buffer_create();

    if (!atlas->slug_glyphs || !atlas->slug_curves || !atlas->slug_bands || !atlas->pixels || !atlas->hb_buf) {
        return false;
    }

    // Solid white 16x16 fallback texture on sampler slot 0.
    std::memset(atlas->pixels, 255, size_t(kAtlasSize) * kAtlasSize * 4u);
    atlas->dirty = true;

    const char* fallbacks[] = {
        preferred_font_path,
#if defined(_WIN32)
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/Helvetica.ttc",
#else
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
#endif
    };
    for (const char* path : fallbacks) {
        if (path && try_load_font_file(atlas, path)) {
            break;
        }
    }
    return true;
}

void font_atlas_destroy(FontAtlas* atlas) noexcept {
    if (!atlas) {
        return;
    }
    if (atlas->hb_buf) hb_buffer_destroy(atlas->hb_buf);
    if (atlas->hb_font) hb_font_destroy(atlas->hb_font);
    if (atlas->hb_face) hb_face_destroy(atlas->hb_face);
    if (atlas->hb_blob) hb_blob_destroy(atlas->hb_blob);
    if (atlas->font_file_data) SDL_free(atlas->font_file_data);
    std::free(atlas->shape_scratch);
    std::free(atlas->pixels);
    std::free(atlas->slug_bands);
    std::free(atlas->slug_curves);
    std::free(atlas->slug_glyphs);
    *atlas = {};
}

bool slug_glyph_rehash(FontAtlas* atlas) noexcept {
    const uint32_t new_cap = (atlas->slug_glyph_cap > 0u) ? (atlas->slug_glyph_cap * 2u) : kSlugGlyphTableCapacity;
    SlugGlyphEntry* next = static_cast<SlugGlyphEntry*>(std::calloc(new_cap, sizeof(SlugGlyphEntry)));
    if (!next) {
        return false;
    }
    const uint32_t mask = new_cap - 1u;
    for (uint32_t i = 0u; i < atlas->slug_glyph_cap; ++i) {
        const SlugGlyphEntry& e = atlas->slug_glyphs[i];
        if (e.key == 0u) {
            continue;
        }
        uint32_t idx = hash_u32(e.key) & mask;
        while (next[idx].key != 0u) {
            idx = (idx + 1u) & mask;
        }
        next[idx] = e;
    }
    std::free(atlas->slug_glyphs);
    atlas->slug_glyphs = next;
    atlas->slug_glyph_cap = new_cap;
    return true;
}

struct SlugRawCurve {
    float p1x, p1y;
    float p2x, p2y;
    float p3x, p3y;
};

struct SlugCurveSortItem {
    uint32_t curve_idx;
    float    max_coord;
};

void sort_band_desc(SlugCurveSortItem* items, uint32_t count) noexcept {
    for (uint32_t i = 1u; i < count; ++i) {
        const SlugCurveSortItem key = items[i];
        uint32_t j = i;
        while (j > 0u && items[j - 1u].max_coord < key.max_coord) {
            items[j] = items[j - 1u];
            --j;
        }
        items[j] = key;
    }
}

SlugGlyphEntry slug_get_glyph(FontAtlas* atlas, uint32_t glyph_id) noexcept {
    if (!atlas || !atlas->font_valid || !atlas->slug_glyphs || atlas->slug_glyph_cap == 0u) {
        return {};
    }

    const uint32_t key = glyph_id + 1u;
    const uint32_t mask = atlas->slug_glyph_cap - 1u;
    uint32_t idx = hash_u32(key) & mask;
    while (atlas->slug_glyphs[idx].key != 0u) {
        if (atlas->slug_glyphs[idx].key == key) {
            return atlas->slug_glyphs[idx];
        }
        idx = (idx + 1u) & mask;
    }

    SlugGlyphEntry entry = { .key = key };
    stbtt_vertex* verts = nullptr;
    const int nverts = stbtt_GetGlyphShape(&atlas->font_info, static_cast<int>(glyph_id), &verts);
    if (nverts <= 0 || !verts) {
        if (verts) stbtt_FreeShape(&atlas->font_info, verts);
        if ((atlas->slug_glyph_count + 1u) * 4u >= atlas->slug_glyph_cap * 3u && slug_glyph_rehash(atlas)) {
            const uint32_t nmask = atlas->slug_glyph_cap - 1u;
            idx = hash_u32(key) & nmask;
            while (atlas->slug_glyphs[idx].key != 0u) idx = (idx + 1u) & nmask;
        }
        atlas->slug_glyphs[idx] = entry;
        ++atlas->slug_glyph_count;
        return entry;
    }

    constexpr uint32_t kStackRawCap = 512u;
    SlugRawCurve stack_raw[kStackRawCap];
    SlugRawCurve* raw = stack_raw;
    uint32_t raw_cap = kStackRawCap;
    uint32_t raw_count = 0u;
    SlugRawCurve* heap_raw = nullptr;

    auto push_raw = [&](SlugRawCurve c) noexcept -> bool {
        if (raw_count >= raw_cap) {
            const uint32_t new_cap = raw_cap * 2u;
            SlugRawCurve* next = static_cast<SlugRawCurve*>(std::malloc(size_t(new_cap) * sizeof(SlugRawCurve)));
            if (!next) {
                return false;
            }
            std::memcpy(next, raw, size_t(raw_count) * sizeof(SlugRawCurve));
            std::free(heap_raw);
            heap_raw = next;
            raw = heap_raw;
            raw_cap = new_cap;
        }
        raw[raw_count++] = c;
        return true;
    };

    float cur_x = 0.0f;
    float cur_y = 0.0f;
    for (int i = 0; i < nverts; ++i) {
        const stbtt_vertex& v = verts[i];
        const float vx = static_cast<float>(v.x);
        const float vy = static_cast<float>(v.y);
        if (v.type == STBTT_vmove) {
            cur_x = vx;
            cur_y = vy;
        } else if (v.type == STBTT_vline) {
            push_raw(SlugRawCurve{
                cur_x, cur_y,
                0.5f * (cur_x + vx), 0.5f * (cur_y + vy),
                vx, vy,
            });
            cur_x = vx;
            cur_y = vy;
        } else if (v.type == STBTT_vcurve) {
            push_raw(SlugRawCurve{
                cur_x, cur_y,
                static_cast<float>(v.cx), static_cast<float>(v.cy),
                vx, vy,
            });
            cur_x = vx;
            cur_y = vy;
        } else if (v.type == STBTT_vcubic) {
            const float c1x = static_cast<float>(v.cx);
            const float c1y = static_cast<float>(v.cy);
            const float c2x = static_cast<float>(v.cx1);
            const float c2y = static_cast<float>(v.cy1);
            const float l1x = 0.5f * (cur_x + c1x), l1y = 0.5f * (cur_y + c1y);
            const float hx  = 0.5f * (c1x + c2x),   hy  = 0.5f * (c1y + c2y);
            const float l2x = 0.5f * (l1x + hx),    l2y = 0.5f * (l1y + hy);
            const float r2x = 0.5f * (c2x + vx),    r2y = 0.5f * (c2y + vy);
            const float r1x = 0.5f * (hx + r2x),    r1y = 0.5f * (hy + r2y);
            const float mx  = 0.5f * (l2x + r1x),   my  = 0.5f * (l2y + r1y);
            push_raw(SlugRawCurve{
                cur_x, cur_y,
                0.75f * (l1x + l2x) - 0.25f * (cur_x + mx),
                0.75f * (l1y + l2y) - 0.25f * (cur_y + my),
                mx, my,
            });
            push_raw(SlugRawCurve{
                mx, my,
                0.75f * (r1x + r2x) - 0.25f * (mx + vx),
                0.75f * (r1y + r2y) - 0.25f * (my + vy),
                vx, vy,
            });
            cur_x = vx;
            cur_y = vy;
        }
    }
    stbtt_FreeShape(&atlas->font_info, verts);

    float min_x = 0.0f, min_y = 0.0f, max_x = 0.0f, max_y = 0.0f;
    if (raw_count > 0u) {
        min_x = max_x = raw[0].p1x;
        min_y = max_y = raw[0].p1y;
        auto accum_pt = [&](float x, float y) noexcept {
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        };
        for (uint32_t i = 0u; i < raw_count; ++i) {
            accum_pt(raw[i].p1x, raw[i].p1y);
            accum_pt(raw[i].p2x, raw[i].p2y);
            accum_pt(raw[i].p3x, raw[i].p3y);
        }
    }

    const float span_x = max_x - min_x;
    const float span_y = max_y - min_y;
    if (raw_count > 0u && span_x >= 1e-3f && span_y >= 1e-3f) {
        const float inv_w = 1.0f / span_x;
        const float inv_h = 1.0f / span_y;

        struct DecodedCurve {
            float p1x, p1y, p2x, p2y, p3x, p3y;
            float min_x, min_y, max_x, max_y;
            uint32_t p1_xy, p2_xy, p3_xy;
        };
        DecodedCurve stack_dec[kStackRawCap];
        DecodedCurve* dec = (raw_count <= kStackRawCap)
            ? stack_dec
            : static_cast<DecodedCurve*>(std::malloc(size_t(raw_count) * sizeof(DecodedCurve)));
        uint32_t kept = 0u;

        if (dec) {
            for (uint32_t i = 0u; i < raw_count; ++i) {
                const SlugRawCurve& c = raw[i];
                const uint16_t h1x = pack_f16((c.p1x - min_x) * inv_w);
                const uint16_t h1y = pack_f16((c.p1y - min_y) * inv_h);
                const uint16_t h2x = pack_f16((c.p2x - min_x) * inv_w);
                const uint16_t h2y = pack_f16((c.p2y - min_y) * inv_h);
                const uint16_t h3x = pack_f16((c.p3x - min_x) * inv_w);
                const uint16_t h3y = pack_f16((c.p3y - min_y) * inv_h);

                // Decode quantized f16 back on CPU so band classification and sorting match GPU bit-for-bit.
                const float x1 = unpack_f16_cpu(h1x), y1 = unpack_f16_cpu(h1y);
                const float x2 = unpack_f16_cpu(h2x), y2 = unpack_f16_cpu(h2y);
                const float x3 = unpack_f16_cpu(h3x), y3 = unpack_f16_cpu(h3y);

                if (x1 == x2 && x2 == x3 && y1 == y2 && y2 == y3) {
                    continue;
                }
                const float lo_x = (x1 < x2) ? ((x1 < x3) ? x1 : x3) : ((x2 < x3) ? x2 : x3);
                const float hi_x = (x1 > x2) ? ((x1 > x3) ? x1 : x3) : ((x2 > x3) ? x2 : x3);
                const float lo_y = (y1 < y2) ? ((y1 < y3) ? y1 : y3) : ((y2 < y3) ? y2 : y3);
                const float hi_y = (y1 > y2) ? ((y1 > y3) ? y1 : y3) : ((y2 > y3) ? y2 : y3);

                dec[kept++] = DecodedCurve{
                    x1, y1, x2, y2, x3, y3,
                    lo_x, lo_y, hi_x, hi_y,
                    static_cast<uint32_t>(h1x) | (static_cast<uint32_t>(h1y) << 16),
                    static_cast<uint32_t>(h2x) | (static_cast<uint32_t>(h2y) << 16),
                    static_cast<uint32_t>(h3x) | (static_cast<uint32_t>(h3y) << 16),
                };
            }

            if (kept > 0u && ensure_array_cap(&atlas->slug_curves, &atlas->slug_curve_cap, atlas->slug_curve_count + kept)) {
                const uint32_t base_curve_idx = atlas->slug_curve_count;
                for (uint32_t i = 0u; i < kept; ++i) {
                    atlas->slug_curves[atlas->slug_curve_count++] = SlugCurveGpu{
                        .p1_xy = dec[i].p1_xy,
                        .p2_xy = dec[i].p2_xy,
                        .p3_xy = dec[i].p3_xy,
                        .pad0 = 0u,
                    };
                }

                uint16_t nb = static_cast<uint16_t>((kept + 3u) / 4u);
                if (nb < 1u) nb = 1u;
                if (nb > kMaxSlugBands) nb = static_cast<uint16_t>(kMaxSlugBands);
                const uint32_t num_hb = nb;
                const uint32_t num_vb = nb;

                SlugCurveSortItem stack_sort[kStackRawCap];
                SlugCurveSortItem* sort_buf = (kept <= kStackRawCap)
                    ? stack_sort
                    : static_cast<SlugCurveSortItem*>(std::malloc(size_t(kept) * sizeof(SlugCurveSortItem)));

                // Conservative worst-case band storage: headers + (nb + nb) * kept
                const uint32_t worst_bands = num_hb + num_vb + (num_hb + num_vb) * kept;
                if (sort_buf && ensure_array_cap(&atlas->slug_bands, &atlas->slug_band_cap, atlas->slug_band_count + worst_bands)) {
                    const uint32_t band_offset = atlas->slug_band_count;
                    atlas->slug_band_count += num_hb + num_vb;
                    constexpr float kBandEps = 1e-4f;

                    for (uint32_t b = 0u; b < num_hb; ++b) {
                        const float y0 = (b == 0u) ? -1.0f : (static_cast<float>(b) / static_cast<float>(num_hb) - kBandEps);
                        const float y1 = (b + 1u == num_hb) ? 2.0f : (static_cast<float>(b + 1u) / static_cast<float>(num_hb) + kBandEps);
                        uint32_t cnt = 0u;
                        for (uint32_t k = 0u; k < kept; ++k) {
                            const DecodedCurve& dc = dec[k];
                            if (dc.p1y == dc.p2y && dc.p2y == dc.p3y) {
                                continue;
                            }
                            if (dc.max_y >= y0 && dc.min_y <= y1) {
                                sort_buf[cnt++] = SlugCurveSortItem{ base_curve_idx + k, dc.max_x };
                            }
                        }
                        sort_band_desc(sort_buf, cnt);
                        const uint32_t rel_off = atlas->slug_band_count - band_offset;
                        atlas->slug_bands[band_offset + b] = SlugBandEntryGpu{ cnt, rel_off };
                        for (uint32_t m = 0u; m < cnt; ++m) {
                            atlas->slug_bands[atlas->slug_band_count++] = SlugBandEntryGpu{ sort_buf[m].curve_idx, 0u };
                        }
                    }

                    for (uint32_t b = 0u; b < num_vb; ++b) {
                        const float x0 = (b == 0u) ? -1.0f : (static_cast<float>(b) / static_cast<float>(num_vb) - kBandEps);
                        const float x1 = (b + 1u == num_vb) ? 2.0f : (static_cast<float>(b + 1u) / static_cast<float>(num_vb) + kBandEps);
                        uint32_t cnt = 0u;
                        for (uint32_t k = 0u; k < kept; ++k) {
                            const DecodedCurve& dc = dec[k];
                            if (dc.p1x == dc.p2x && dc.p2x == dc.p3x) {
                                continue;
                            }
                            if (dc.max_x >= x0 && dc.min_x <= x1) {
                                sort_buf[cnt++] = SlugCurveSortItem{ base_curve_idx + k, dc.max_y };
                            }
                        }
                        sort_band_desc(sort_buf, cnt);
                        const uint32_t rel_off = atlas->slug_band_count - band_offset;
                        atlas->slug_bands[band_offset + num_hb + b] = SlugBandEntryGpu{ cnt, rel_off };
                        for (uint32_t m = 0u; m < cnt; ++m) {
                            atlas->slug_bands[atlas->slug_band_count++] = SlugBandEntryGpu{ sort_buf[m].curve_idx, 0u };
                        }
                    }

                    entry.min_x_em = min_x;
                    entry.min_y_em = min_y;
                    entry.max_x_em = max_x;
                    entry.max_y_em = max_y;
                    entry.band_offset = band_offset;
                    entry.num_hbands = static_cast<uint16_t>(num_hb);
                    entry.num_vbands = static_cast<uint16_t>(num_vb);
                    entry.has_outline = true;
                }
                if (sort_buf != stack_sort) {
                    std::free(sort_buf);
                }
            }
            if (dec != stack_dec) {
                std::free(dec);
            }
        }
    }
    std::free(heap_raw);

    if ((atlas->slug_glyph_count + 1u) * 4u >= atlas->slug_glyph_cap * 3u && slug_glyph_rehash(atlas)) {
        const uint32_t nmask = atlas->slug_glyph_cap - 1u;
        idx = hash_u32(key) & nmask;
        while (atlas->slug_glyphs[idx].key != 0u) {
            idx = (idx + 1u) & nmask;
        }
    }
    atlas->slug_glyphs[idx] = entry;
    ++atlas->slug_glyph_count;
    return entry;
}

std::span<const ShapedGlyph> shape_utf8_run(Context* ctx, const char* utf8, uint32_t byte_len) noexcept {
    if (!ctx || !ctx->atlas.font_valid || !ctx->atlas.hb_font || !ctx->atlas.hb_buf || !utf8 || byte_len == 0u) {
        return {};
    }

    hb_buffer_t* buf = ctx->atlas.hb_buf;
    hb_buffer_clear_contents(buf);
    hb_buffer_set_cluster_level(buf, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
    hb_buffer_add_utf8(buf, utf8, static_cast<int>(byte_len), 0u, static_cast<int>(byte_len));
    hb_buffer_guess_segment_properties(buf);

    static constexpr hb_feature_t kFeatures[3] = {
        { HB_TAG('k', 'e', 'r', 'n'), 1u, 0u, static_cast<unsigned int>(-1) },
        { HB_TAG('l', 'i', 'g', 'a'), 1u, 0u, static_cast<unsigned int>(-1) },
        { HB_TAG('c', 'a', 'l', 't'), 1u, 0u, static_cast<unsigned int>(-1) },
    };
    hb_shape(ctx->atlas.hb_font, buf, kFeatures, 3u);

    unsigned int count = 0u;
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buf, &count);
    const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &count);
    if (count == 0u || !infos || !pos) {
        return {};
    }

    ShapedGlyph* out = ctx->frame_arena.alloc_uninit<ShapedGlyph>(count);
    if (!out) {
        if (!ensure_array_cap(&ctx->atlas.shape_scratch, &ctx->atlas.shape_scratch_cap, count)) {
            return {};
        }
        out = ctx->atlas.shape_scratch;
    }

    for (unsigned int i = 0u; i < count; ++i) {
        out[i] = ShapedGlyph{
            .glyph_id = infos[i].codepoint,
            .cluster = infos[i].cluster,
            .x_advance_em = static_cast<float>(pos[i].x_advance),
            .x_offset_em = static_cast<float>(pos[i].x_offset),
            .y_offset_em = static_cast<float>(pos[i].y_offset),
        };
    }
    return std::span<const ShapedGlyph>(out, count);
}

} // namespace

} // namespace etch
