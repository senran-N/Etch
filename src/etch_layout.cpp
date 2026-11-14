// Declarative Clay layout adapter, UTF-8 + stb_truetype FontAtlas, and Prim batch lowering.
// Compiled via src/etch_unity.cpp.

#include <cmath>

namespace etch {

namespace {

constexpr Clay_ElementId make_clay_id(uint32_t element_id) noexcept {
    return Clay_ElementId{
        .id = element_id,
        .offset = 0u,
        .baseId = element_id,
        .stringId = Clay_String{ .isStaticallyAllocated = true, .length = 0, .chars = "" },
    };
}

inline Clay_Color to_clay_color(Color c) noexcept {
    return Clay_Color{
        static_cast<float>(c.r),
        static_cast<float>(c.g),
        static_cast<float>(c.b),
        static_cast<float>(c.a),
    };
}

constexpr Clay_SizingAxis to_clay_sizing(const Sizing& s) noexcept {
    Clay_SizingAxis out = {};
    switch (s.kind) {
        case SizingKind::Fit:
            out.type = CLAY__SIZING_TYPE_FIT;
            out.size.minMax = Clay_SizingMinMax{ s.min, s.max };
            break;
        case SizingKind::Grow:
            out.type = CLAY__SIZING_TYPE_GROW;
            out.size.minMax = Clay_SizingMinMax{ s.min, s.max };
            break;
        case SizingKind::Fixed:
            out.type = CLAY__SIZING_TYPE_FIXED;
            out.size.minMax = Clay_SizingMinMax{ s.value, s.value };
            break;
        case SizingKind::Percent:
            out.type = CLAY__SIZING_TYPE_PERCENT;
            out.size.percent = (s.value < 0.0f) ? 0.0f : ((s.value > 1.0f) ? 1.0f : s.value);
            break;
    }
    return out;
}

constexpr Clay_LayoutDirection to_clay_axis(Axis axis) noexcept {
    return (axis == Axis::Col) ? CLAY_TOP_TO_BOTTOM : CLAY_LEFT_TO_RIGHT;
}

constexpr Clay_LayoutAlignmentX to_clay_align_x(AlignX ax) noexcept {
    switch (ax) {
        case AlignX::Left:   return CLAY_ALIGN_X_LEFT;
        case AlignX::Center: return CLAY_ALIGN_X_CENTER;
        case AlignX::Right:  return CLAY_ALIGN_X_RIGHT;
    }
    return CLAY_ALIGN_X_LEFT;
}

constexpr Clay_LayoutAlignmentY to_clay_align_y(AlignY ay) noexcept {
    switch (ay) {
        case AlignY::Top:    return CLAY_ALIGN_Y_TOP;
        case AlignY::Center: return CLAY_ALIGN_Y_CENTER;
        case AlignY::Bottom: return CLAY_ALIGN_Y_BOTTOM;
    }
    return CLAY_ALIGN_Y_TOP;
}

inline uint16_t to_u16_round(float v) noexcept {
    if (v <= 0.0f) {
        return 0u;
    }
    if (v >= 65535.0f) {
        return 65535u;
    }
    return static_cast<uint16_t>(v + 0.5f);
}

inline Clay_CornerRadius to_clay_corners(const Corners& c) noexcept {
    // Note: Clay_CornerRadius puts bottomLeft before bottomRight.
    return Clay_CornerRadius{
        .topLeft = c.tl,
        .topRight = c.tr,
        .bottomLeft = c.bl,
        .bottomRight = c.br,
    };
}

constexpr Rect rect_intersect(Rect a, Rect b) noexcept {
    const float x0 = (a.x > b.x) ? a.x : b.x;
    const float y0 = (a.y > b.y) ? a.y : b.y;
    const float ax1 = a.x + a.w;
    const float ay1 = a.y + a.h;
    const float bx1 = b.x + b.w;
    const float by1 = b.y + b.h;
    const float x1 = (ax1 < bx1) ? ax1 : bx1;
    const float y1 = (ay1 < by1) ? ay1 : by1;
    return Rect{
        .x = x0,
        .y = y0,
        .w = (x1 > x0) ? (x1 - x0) : 0.0f,
        .h = (y1 > y0) ? (y1 - y0) : 0.0f,
    };
}

constexpr float min4f(float a, float b, float c, float d) noexcept {
    const float ab = (a < b) ? a : b;
    const float cd = (c < d) ? c : d;
    return (ab < cd) ? ab : cd;
}

constexpr float clamp_range(float v, float lo, float hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

constexpr Clay_FloatingAttachPointType to_clay_attach_point(Anchor a) noexcept {
    switch (a) {
        case Anchor::TopLeft:      return CLAY_ATTACH_POINT_LEFT_TOP;
        case Anchor::TopCenter:    return CLAY_ATTACH_POINT_CENTER_TOP;
        case Anchor::TopRight:     return CLAY_ATTACH_POINT_RIGHT_TOP;
        case Anchor::CenterLeft:   return CLAY_ATTACH_POINT_LEFT_CENTER;
        case Anchor::Center:       return CLAY_ATTACH_POINT_CENTER_CENTER;
        case Anchor::CenterRight:  return CLAY_ATTACH_POINT_RIGHT_CENTER;
        case Anchor::BottomLeft:   return CLAY_ATTACH_POINT_LEFT_BOTTOM;
        case Anchor::BottomCenter: return CLAY_ATTACH_POINT_CENTER_BOTTOM;
        case Anchor::BottomRight:  return CLAY_ATTACH_POINT_RIGHT_BOTTOM;
    }
    return CLAY_ATTACH_POINT_LEFT_TOP;
}

constexpr Clay_FloatingAttachToElement to_clay_attach_to(AttachTo at) noexcept {
    switch (at) {
        case AttachTo::None:    return CLAY_ATTACH_TO_NONE;
        case AttachTo::Parent:  return CLAY_ATTACH_TO_PARENT;
        case AttachTo::Element: return CLAY_ATTACH_TO_ELEMENT_WITH_ID;
        case AttachTo::Root:    return CLAY_ATTACH_TO_ROOT;
    }
    return CLAY_ATTACH_TO_NONE;
}

constexpr SDL_SystemCursor to_sdl_system_cursor(Cursor c) noexcept {
    switch (c) {
        case Cursor::Arrow:      return SDL_SYSTEM_CURSOR_DEFAULT;
        case Cursor::Hand:       return SDL_SYSTEM_CURSOR_POINTER;
        case Cursor::IBeam:      return SDL_SYSTEM_CURSOR_TEXT;
        case Cursor::ResizeEW:   return SDL_SYSTEM_CURSOR_EW_RESIZE;
        case Cursor::ResizeNS:   return SDL_SYSTEM_CURSOR_NS_RESIZE;
        case Cursor::ResizeNWSE: return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
        case Cursor::ResizeNESW: return SDL_SYSTEM_CURSOR_NESW_RESIZE;
        case Cursor::Move:       return SDL_SYSTEM_CURSOR_MOVE;
        case Cursor::NotAllowed: return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    }
    return SDL_SYSTEM_CURSOR_DEFAULT;
}

inline constexpr uint32_t kScrollbarYTag = 0x53435259u;
inline constexpr float    kSbMargin      = 4.0f;
inline constexpr float    kSbHitWidth    = 12.0f;
inline constexpr float    kSbThumbWidth  = 6.0f;
inline constexpr float    kSbMinThumbH   = 24.0f;

struct ScrollbarGeom {
    bool  visible = false;
    Rect  hit_track = {};
    Rect  track = {};
    Rect  thumb = {};
    float travel_h = 0.0f;
};

ScrollbarGeom compute_vscrollbar_geom(const WidgetState* ws) noexcept {
    ScrollbarGeom g = {};
    if (!ws || !ws->is_scroll_y || ws->scroll_max.y <= 0.5f || ws->content_dim.y <= 0.5f) {
        return g;
    }
    const Rect& b = ws->last_box_logical;
    const float track_h = b.h - 2.0f * kSbMargin;
    if (b.w <= kSbHitWidth + 4.0f || track_h <= kSbMinThumbH) {
        return g;
    }
    const float visible_ratio = (ws->viewport_dim.y < ws->content_dim.y)
        ? (ws->viewport_dim.y / ws->content_dim.y)
        : 1.0f;
    float thumb_h = track_h * visible_ratio;
    if (thumb_h < kSbMinThumbH) thumb_h = kSbMinThumbH;
    if (thumb_h > track_h) thumb_h = track_h;
    const float travel = track_h - thumb_h;
    const float ratio = clamp_range(ws->scroll_pos.y / ws->scroll_max.y, 0.0f, 1.0f);
    const float thumb_x = b.x + b.w - kSbMargin - kSbThumbWidth;
    const float track_y = b.y + kSbMargin;

    g.visible = true;
    g.hit_track = Rect{ b.x + b.w - kSbMargin - kSbHitWidth, track_y, kSbHitWidth + kSbMargin, track_h };
    g.track = Rect{ thumb_x, track_y, kSbThumbWidth, track_h };
    g.thumb = Rect{ thumb_x, track_y + travel * ratio, kSbThumbWidth, thumb_h };
    g.travel_h = travel;
    return g;
}

bool step_scroll_axis(float* pos, float* vel, float max_val, float decay, float step_factor) noexcept {
    if (!pos || !vel || std::fabs(*vel) <= 1.0f) {
        if (vel) *vel = 0.0f;
        return false;
    }
    *pos += (*vel) * step_factor;
    *vel *= decay;
    if (*pos <= 0.0f) {
        *pos = 0.0f;
        *vel = 0.0f;
        return false;
    }
    if (*pos >= max_val) {
        *pos = max_val;
        *vel = 0.0f;
        return false;
    }
    if (std::fabs(*vel) <= 1.0f) {
        *vel = 0.0f;
        return false;
    }
    return true;
}

inline uint32_t next_utf8_codepoint(const char*& p, const char* end) noexcept {
    if (p >= end) {
        return 0u;
    }
    const uint8_t b0 = static_cast<uint8_t>(*p++);
    if (b0 < 0x80u) {
        return b0;
    }
    if ((b0 & 0xE0u) == 0xC0u) {
        if (p < end) {
            const uint8_t b1 = static_cast<uint8_t>(*p);
            if ((b1 & 0xC0u) == 0x80u) {
                ++p;
                const uint32_t cp = (uint32_t(b0 & 0x1Fu) << 6) | uint32_t(b1 & 0x3Fu);
                return (cp >= 0x80u) ? cp : 0xFFFDu;
            }
        }
        return 0xFFFDu;
    }
    if ((b0 & 0xF0u) == 0xE0u) {
        if ((end - p) >= 2) {
            const uint8_t b1 = static_cast<uint8_t>(p[0]);
            const uint8_t b2 = static_cast<uint8_t>(p[1]);
            if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u) {
                p += 2;
                const uint32_t cp =
                    (uint32_t(b0 & 0x0Fu) << 12) | (uint32_t(b1 & 0x3Fu) << 6) | uint32_t(b2 & 0x3Fu);
                return (cp >= 0x800u && (cp < 0xD800u || cp > 0xDFFFu)) ? cp : 0xFFFDu;
            }
        }
        return 0xFFFDu;
    }
    if ((b0 & 0xF8u) == 0xF0u) {
        if ((end - p) >= 3) {
            const uint8_t b1 = static_cast<uint8_t>(p[0]);
            const uint8_t b2 = static_cast<uint8_t>(p[1]);
            const uint8_t b3 = static_cast<uint8_t>(p[2]);
            if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u && (b3 & 0xC0u) == 0x80u) {
                p += 3;
                const uint32_t cp = (uint32_t(b0 & 0x07u) << 18) | (uint32_t(b1 & 0x3Fu) << 12) |
                                    (uint32_t(b2 & 0x3Fu) << 6) | uint32_t(b3 & 0x3Fu);
                return (cp >= 0x10000u && cp <= 0x10FFFFu) ? cp : 0xFFFDu;
            }
        }
        return 0xFFFDu;
    }
    return 0xFFFDu;
}

constexpr uint32_t hash_u64_to_32(uint64_t x) noexcept {
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x = x ^ (x >> 31);
    return static_cast<uint32_t>(x);
}

bool try_load_font_file(FontAtlas* atlas, const char* path) noexcept {
    if (!atlas || !path || path[0] == '\0') {
        return false;
    }
    size_t sz = 0;
    void* raw = SDL_LoadFile(path, &sz);
    if (!raw || sz < 16u) {
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
    atlas->font_file_data = static_cast<uint8_t*>(raw);
    atlas->font_file_size = sz;
    stbtt_GetFontVMetrics(&atlas->font_info, &atlas->font_ascent, &atlas->font_descent, &atlas->font_line_gap);
    atlas->font_valid = true;
    return true;
}

bool font_atlas_init(FontAtlas* atlas, const char* preferred_font_path) noexcept {
    if (!atlas) {
        return false;
    }
    *atlas = {};
    atlas->glyphs = static_cast<GlyphEntry*>(std::calloc(kGlyphTableCapacity, sizeof(GlyphEntry)));
    atlas->pixels = static_cast<uint8_t*>(std::calloc(size_t(kAtlasSize) * kAtlasSize * 4u, 1u));
    atlas->raster_scratch = static_cast<uint8_t*>(std::calloc(256u * 256u, 1u));
    if (!atlas->glyphs || !atlas->pixels || !atlas->raster_scratch) {
        return false;
    }

    // Reserve top-left 4x4 texels as opaque white (255, 255, 255, 255) for slot-0 fallback.
    for (uint32_t y = 0; y < 4u; ++y) {
        for (uint32_t x = 0; x < 4u; ++x) {
            uint8_t* dst = atlas->pixels + (y * kAtlasSize + x) * 4u;
            dst[0] = 255u;
            dst[1] = 255u;
            dst[2] = 255u;
            dst[3] = 255u;
        }
    }
    atlas->shelf_x = 6u;
    atlas->shelf_y = 0u;
    atlas->shelf_h = 6u;
    atlas->dirty_y0 = 0u;
    atlas->dirty_y1 = 4u;
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
    if (atlas->font_file_data) {
        SDL_free(atlas->font_file_data);
    }
    std::free(atlas->raster_scratch);
    std::free(atlas->pixels);
    std::free(atlas->glyphs);
    *atlas = {};
}

const GlyphEntry* font_atlas_get_glyph(FontAtlas* atlas, uint32_t codepoint, float phys_px_size) noexcept {
    static const GlyphEntry kEmptyGlyph = {};
    if (!atlas || !atlas->font_valid || !atlas->glyphs) {
        return &kEmptyGlyph;
    }
    const float clamped_px = (phys_px_size < 6.0f) ? 6.0f : ((phys_px_size > 128.0f) ? 128.0f : phys_px_size);
    const uint16_t size_q2 = static_cast<uint16_t>(clamped_px * 4.0f + 0.5f);
    const float quant_px = static_cast<float>(size_q2) * 0.25f;
    const uint64_t key = (static_cast<uint64_t>(codepoint) << 16) | static_cast<uint64_t>(size_q2);

    const uint32_t mask = kGlyphTableCapacity - 1u;
    uint32_t idx = hash_u64_to_32(key) & mask;
    uint32_t probes = 0u;
    while (probes < kGlyphTableCapacity) {
        GlyphEntry& slot = atlas->glyphs[idx];
        if (slot.key == key) {
            return &slot;
        }
        if (slot.key == 0ull) {
            break;
        }
        idx = (idx + 1u) & mask;
        ++probes;
    }
    if (probes >= kGlyphTableCapacity) {
        return &kEmptyGlyph;
    }

    int glyph_index = stbtt_FindGlyphIndex(&atlas->font_info, static_cast<int>(codepoint));
    if (glyph_index == 0 && codepoint != 0u && codepoint != 0x20u && codepoint != 0x09u) {
        // Fallback to '?' when a Unicode glyph is missing in a Latin-only fallback font.
        const int q_idx = stbtt_FindGlyphIndex(&atlas->font_info, '?');
        if (q_idx != 0) {
            glyph_index = q_idx;
        }
    }

    const float scale = stbtt_ScaleForPixelHeight(&atlas->font_info, quant_px);
    int adv_unscaled = 0;
    int lsb_unscaled = 0;
    stbtt_GetGlyphHMetrics(&atlas->font_info, glyph_index, &adv_unscaled, &lsb_unscaled);
    const float advance = static_cast<float>(adv_unscaled) * scale;

    int ix0 = 0;
    int iy0 = 0;
    int ix1 = 0;
    int iy1 = 0;
    stbtt_GetGlyphBitmapBox(&atlas->font_info, glyph_index, scale, scale, &ix0, &iy0, &ix1, &iy1);
    const int gw = ix1 - ix0;
    const int gh = iy1 - iy0;

    GlyphEntry entry = {
        .key = key,
        .xoff = static_cast<float>(ix0),
        .yoff = static_cast<float>(iy0),
        .width = static_cast<float>(gw),
        .height = static_cast<float>(gh),
        .advance = advance,
        .visible = false,
    };

    if (gw > 0 && gh > 0 && gw <= 254 && gh <= 254 && codepoint != 0x20u && codepoint != 0x09u) {
        constexpr uint32_t kPad = 2u;
        const uint32_t uw = static_cast<uint32_t>(gw);
        const uint32_t uh = static_cast<uint32_t>(gh);

        if (atlas->shelf_x + uw + kPad > kAtlasSize) {
            atlas->shelf_x = 0u;
            atlas->shelf_y += atlas->shelf_h + kPad;
            atlas->shelf_h = 0u;
        }
        if (atlas->shelf_y + uh + kPad <= kAtlasSize) {
            const uint32_t ax = atlas->shelf_x;
            const uint32_t ay = atlas->shelf_y;

            std::memset(atlas->raster_scratch, 0, size_t(gw) * size_t(gh));
            stbtt_MakeGlyphBitmap(
                &atlas->font_info,
                atlas->raster_scratch,
                gw,
                gh,
                gw,
                scale,
                scale,
                glyph_index);

            for (uint32_t row = 0; row < uh; ++row) {
                const uint8_t* src = atlas->raster_scratch + row * uw;
                uint8_t* dst = atlas->pixels + ((ay + row) * kAtlasSize + ax) * 4u;
                for (uint32_t col = 0; col < uw; ++col) {
                    const uint8_t cov = src[col];
                    dst[col * 4u + 0u] = 255u;
                    dst[col * 4u + 1u] = 255u;
                    dst[col * 4u + 2u] = 255u;
                    dst[col * 4u + 3u] = cov;
                }
            }

            const float inv_atlas = 1.0f / static_cast<float>(kAtlasSize);
            entry.u0 = static_cast<float>(ax) * inv_atlas;
            entry.v0 = static_cast<float>(ay) * inv_atlas;
            entry.u1 = static_cast<float>(ax + uw) * inv_atlas;
            entry.v1 = static_cast<float>(ay + uh) * inv_atlas;
            entry.visible = true;

            atlas->shelf_x += uw + kPad;
            if (uh > atlas->shelf_h) {
                atlas->shelf_h = uh;
            }
            if (!atlas->dirty) {
                atlas->dirty = true;
                atlas->dirty_y0 = ay;
                atlas->dirty_y1 = ay + uh;
            } else {
                if (ay < atlas->dirty_y0) {
                    atlas->dirty_y0 = ay;
                }
                if (ay + uh > atlas->dirty_y1) {
                    atlas->dirty_y1 = ay + uh;
                }
            }
        }
    }

    atlas->glyphs[idx] = entry;
    return &atlas->glyphs[idx];
}

Clay_Dimensions etch_measure_text(
    Clay_StringSlice text,
    Clay_TextElementConfig* config,
    void* user_data) noexcept
{
    Context* ctx = static_cast<Context*>(user_data);
    const float logical_font_px = (config && config->fontSize > 0u) ? static_cast<float>(config->fontSize) : 16.0f;
    const float letter_spacing = config ? static_cast<float>(config->letterSpacing) : 0.0f;
    const float dpi = (ctx && ctx->dpi_scale > 0.0f) ? ctx->dpi_scale : 1.0f;

    if (!ctx || !ctx->atlas.font_valid) {
        const float est_w = static_cast<float>(text.length) * logical_font_px * 0.55f;
        return Clay_Dimensions{ est_w, logical_font_px };
    }

    const float phys_px = logical_font_px * dpi;
    const char* p = text.chars;
    const char* end = (text.chars && text.length > 0) ? (text.chars + text.length) : text.chars;
    float width_phys = 0.0f;
    uint32_t glyph_count = 0u;
    while (p < end) {
        const uint32_t cp = next_utf8_codepoint(p, end);
        const GlyphEntry* g = font_atlas_get_glyph(&ctx->atlas, cp, phys_px);
        width_phys += g->advance;
        if (glyph_count > 0u) {
            width_phys += letter_spacing * dpi;
        }
        ++glyph_count;
    }

    return Clay_Dimensions{
        .width = width_phys / dpi,
        .height = logical_font_px,
    };
}

void etch_clay_error_handler(Clay_ErrorData error) noexcept {
    SDL_LogError(
        SDL_LOG_CATEGORY_APPLICATION,
        "Etch Clay error (%d): %.*s",
        static_cast<int>(error.errorType),
        error.errorText.length,
        error.errorText.chars ? error.errorText.chars : "");
}

void push_prim(
    Context* ctx,
    const Prim& prim,
    SDL_GPUTexture* ext_tex = nullptr,
    float cull_pad_px = 2.0f) noexcept
{
    if (!ctx || !ctx->prims || ctx->prim_count >= ctx->max_prims) {
        return;
    }
    // CPU-side offscreen clip culling: drop primitives completely outside their active clip rect.
    if (prim.clip.z <= prim.clip.x || prim.clip.w <= prim.clip.y ||
        prim.bounds.z <= 0.0f || prim.bounds.w <= 0.0f)
    {
        return;
    }
    if ((prim.bounds.x + prim.bounds.z + cull_pad_px <= prim.clip.x) ||
        (prim.bounds.x - cull_pad_px >= prim.clip.z) ||
        (prim.bounds.y + prim.bounds.w + cull_pad_px <= prim.clip.y) ||
        (prim.bounds.y - cull_pad_px >= prim.clip.w))
    {
        return;
    }
    if (ctx->batch_count == 0u) {
        ctx->batches[0] = DrawBatch{ .first_prim = 0u, .prim_count = 0u, .ext_textures = {}, .ext_count = 0u };
        ctx->batch_count = 1u;
    }

    DrawBatch* cur = &ctx->batches[ctx->batch_count - 1u];
    uint16_t tex_slot = 0u;

    if (ext_tex != nullptr) {
        bool found = false;
        for (uint32_t i = 0u; i < cur->ext_count; ++i) {
            if (cur->ext_textures[i] == ext_tex) {
                tex_slot = static_cast<uint16_t>(i + 1u);
                found = true;
                break;
            }
        }
        if (!found) {
            if (cur->ext_count < 3u) {
                cur->ext_textures[cur->ext_count] = ext_tex;
                tex_slot = static_cast<uint16_t>(cur->ext_count + 1u);
                ++cur->ext_count;
            } else {
                if (ctx->batch_count >= kMaxBatches) {
                    return;
                }
                cur = &ctx->batches[ctx->batch_count++];
                *cur = DrawBatch{
                    .first_prim = ctx->prim_count,
                    .prim_count = 0u,
                    .ext_textures = {ext_tex, nullptr, nullptr},
                    .ext_count = 1u,
                };
                tex_slot = 1u;
            }
        }
    }

    Prim final_p = prim;
    final_p.style.x = (final_p.style.x & ~(0x3FFu << 10)) | ((static_cast<uint32_t>(tex_slot) & 0x3FFu) << 10);
    ctx->prims[ctx->prim_count++] = final_p;
    ++cur->prim_count;
}

inline Corners scale_corners(Corners c, float dpi) noexcept {
    return Corners{ c.tl * dpi, c.tr * dpi, c.br * dpi, c.bl * dpi };
}

void emit_box_background_and_shadow(
    Context* ctx,
    const BoxExtra* extra,
    Rect box_px,
    const ClipEntry& clip,
    float dpi) noexcept
{
    if (!extra) {
        return;
    }
    const BoxDesc& d = extra->desc;
    const Corners rad_px = scale_corners(d.radius, dpi);

    if (d.shadow_color.a > 0u && d.shadow_blur > 0.0f) {
        const float blur_px = d.shadow_blur * dpi;
        const Rect shadow_rect = {
            .x = box_px.x + d.shadow_offset.x * dpi,
            .y = box_px.y + d.shadow_offset.y * dpi,
            .w = box_px.w,
            .h = box_px.h,
        };
        push_prim(
            ctx,
            Prim::make_shadow(
                shadow_rect,
                clip.rect,
                d.shadow_color,
                rad_px,
                blur_px,
                clip.radius),
            nullptr,
            2.0f * blur_px + 2.0f);
    }

    const bool has_grad = (d.background_end.a > 0u) || d.radial_gradient;
    const bool has_fill = (d.background.a > 0u) || has_grad;
    const bool any_clip = d.clip_children || d.scroll_x || d.scroll_y;
    const bool draw_border_here = !any_clip && d.border_width > 0.0f && d.border_color.a > 0u;
    if (!has_fill && !draw_border_here) {
        return;
    }

    const Color border_col = draw_border_here ? d.border_color : Color{};
    const float border_w = draw_border_here ? (d.border_width * dpi) : 0.0f;

    if (has_grad) {
        push_prim(
            ctx,
            Prim::make_gradient_rect(
                box_px,
                clip.rect,
                d.background,
                d.background_end,
                d.gradient_dir,
                rad_px,
                border_col,
                border_w,
                clip.radius,
                d.radial_gradient));
    } else {
        push_prim(
            ctx,
            Prim::make_rect(
                box_px,
                clip.rect,
                d.background,
                rad_px,
                border_col,
                border_w,
                1.0f,
                clip.radius));
    }
}

void emit_text_command(
    Context* ctx,
    const Clay_RenderCommand* cmd,
    const ClipEntry& clip,
    float dpi) noexcept
{
    const Clay_TextRenderData& td = cmd->renderData.text;
    const char* chars = td.stringContents.chars;
    const int32_t len = td.stringContents.length;
    if (!chars || len <= 0 || !ctx->atlas.font_valid) {
        return;
    }

    const float logical_font_px = (td.fontSize > 0u) ? static_cast<float>(td.fontSize) : 16.0f;
    const float phys_px = logical_font_px * dpi;
    const float scale = stbtt_ScaleForPixelHeight(&ctx->atlas.font_info, phys_px);
    const float ascender_px = static_cast<float>(ctx->atlas.font_ascent) * scale;
    const float line_top_px = cmd->boundingBox.y * dpi;
    const float baseline_y = line_top_px + ascender_px;
    const float letter_spacing_px = static_cast<float>(td.letterSpacing) * dpi;

    const Color tint = Color::rgba8(
        static_cast<uint8_t>(td.textColor.r + 0.5f),
        static_cast<uint8_t>(td.textColor.g + 0.5f),
        static_cast<uint8_t>(td.textColor.b + 0.5f),
        static_cast<uint8_t>(td.textColor.a + 0.5f));

    float pen_x = cmd->boundingBox.x * dpi;
    const char* p = chars;
    const char* end = chars + len;
    while (p < end) {
        const uint32_t cp = next_utf8_codepoint(p, end);
        const GlyphEntry* g = font_atlas_get_glyph(&ctx->atlas, cp, phys_px);
        if (g->visible) {
            const float gx = std::floor(pen_x + g->xoff + 0.5f);
            const float gy = std::floor(baseline_y + g->yoff + 0.5f);
            const Rect g_rect = { gx, gy, g->width, g->height };
            push_prim(
                ctx,
                Prim::make_glyph(
                    g_rect,
                    clip.rect,
                    tint,
                    Vec2{g->u0, g->v0},
                    Vec2{g->u1, g->v1},
                    0u,
                    clip.radius,
                    PrimFlag_StemDarken));
        }
        pen_x += g->advance + letter_spacing_px;
    }
}

WidgetState* find_scrollbar_owner(Context* ctx, uint32_t thumb_active_id) noexcept {
    if (!ctx || !ctx->states.entries || thumb_active_id == 0u) {
        return nullptr;
    }
    for (uint32_t i = 0u; i < ctx->states.capacity; ++i) {
        WidgetState& ws = ctx->states.entries[i];
        if (ws.key != 0u && ws.is_scroll_y && id_index(ws.key, kScrollbarYTag) == thumb_active_id) {
            return &ws;
        }
    }
    return nullptr;
}

void apply_scrollbar_drag(WidgetState* ws, float pointer_y, float grab_offset) noexcept {
    if (!ws) {
        return;
    }
    const ScrollbarGeom sb = compute_vscrollbar_geom(ws);
    if (sb.visible && sb.travel_h > 0.0f) {
        const float rel = (pointer_y - grab_offset) - sb.track.y;
        const float ratio = clamp_range(rel / sb.travel_h, 0.0f, 1.0f);
        ws->scroll_pos.y = ratio * ws->scroll_max.y;
        ws->scroll_vel.y = 0.0f;
    }
}

Vec2 window_to_logical_pos(const Context* ctx, float wx, float wy) noexcept {
    if (!ctx || ctx->headless || !ctx->window || ctx->dpi_scale <= 0.0f) {
        return Vec2{wx, wy};
    }
    float density = SDL_GetWindowPixelDensity(ctx->window);
    if (density <= 0.0f) {
        density = 1.0f;
    }
    const float scale = density / ctx->dpi_scale;
    return Vec2{wx * scale, wy * scale};
}

float logical_to_window_scale(const Context* ctx) noexcept {
    if (!ctx || ctx->headless || !ctx->window || ctx->dpi_scale <= 0.0f) {
        return 1.0f;
    }
    float density = SDL_GetWindowPixelDensity(ctx->window);
    if (density <= 0.0f) {
        density = 1.0f;
    }
    return ctx->dpi_scale / density;
}

uint32_t utf8_clamp_boundary(const char* s, uint32_t len, uint32_t pos) noexcept {
    if (!s || len == 0u) {
        return 0u;
    }
    if (pos > len) {
        pos = len;
    }
    while (pos > 0u && pos < len && (static_cast<uint8_t>(s[pos]) & 0xC0u) == 0x80u) {
        --pos;
    }
    return pos;
}

uint32_t utf8_prev_boundary(const char* s, uint32_t len, uint32_t pos) noexcept {
    pos = utf8_clamp_boundary(s, len, pos);
    if (pos == 0u) {
        return 0u;
    }
    --pos;
    while (pos > 0u && (static_cast<uint8_t>(s[pos]) & 0xC0u) == 0x80u) {
        --pos;
    }
    return pos;
}

uint32_t utf8_next_boundary(const char* s, uint32_t len, uint32_t pos) noexcept {
    pos = utf8_clamp_boundary(s, len, pos);
    if (!s || pos >= len) {
        return len;
    }
    const char* p = s + pos;
    (void)next_utf8_codepoint(p, s + len);
    return static_cast<uint32_t>(p - s);
}

uint32_t utf8_decode_at(const char* s, uint32_t len, uint32_t pos) noexcept {
    if (!s || pos >= len) {
        return 0u;
    }
    const char* p = s + pos;
    return next_utf8_codepoint(p, s + len);
}

uint32_t utf8_codepoint_to_byte_offset(const char* s, uint32_t len, int32_t cp_count) noexcept {
    if (!s || len == 0u || cp_count <= 0) {
        return 0u;
    }
    const char* p = s;
    const char* end = s + len;
    int32_t count = 0;
    while (p < end && count < cp_count) {
        (void)next_utf8_codepoint(p, end);
        ++count;
    }
    return static_cast<uint32_t>(p - s);
}

uint8_t utf8_char_class(uint32_t cp) noexcept {
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') {
        return 0u;
    }
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') || cp == '_') {
        return 1u;
    }
    if (cp >= 0x80u) {
        return 2u;
    }
    return 3u;
}

uint32_t utf8_word_left(const char* s, uint32_t len, uint32_t pos) noexcept {
    pos = utf8_clamp_boundary(s, len, pos);
    if (pos == 0u) {
        return 0u;
    }
    uint32_t cur = pos;
    while (cur > 0u) {
        const uint32_t prev = utf8_prev_boundary(s, len, cur);
        if (utf8_char_class(utf8_decode_at(s, len, prev)) != 0u) {
            break;
        }
        cur = prev;
    }
    if (cur == 0u) {
        return 0u;
    }
    const uint32_t first_prev = utf8_prev_boundary(s, len, cur);
    const uint8_t target_cls = utf8_char_class(utf8_decode_at(s, len, first_prev));
    cur = first_prev;
    while (cur > 0u) {
        const uint32_t prev = utf8_prev_boundary(s, len, cur);
        if (utf8_char_class(utf8_decode_at(s, len, prev)) != target_cls) {
            break;
        }
        cur = prev;
    }
    return cur;
}

uint32_t utf8_word_right(const char* s, uint32_t len, uint32_t pos) noexcept {
    pos = utf8_clamp_boundary(s, len, pos);
    if (pos >= len) {
        return len;
    }
    uint32_t cur = pos;
    while (cur < len && utf8_char_class(utf8_decode_at(s, len, cur)) == 0u) {
        cur = utf8_next_boundary(s, len, cur);
    }
    if (cur >= len) {
        return len;
    }
    const uint8_t target_cls = utf8_char_class(utf8_decode_at(s, len, cur));
    while (cur < len && utf8_char_class(utf8_decode_at(s, len, cur)) == target_cls) {
        cur = utf8_next_boundary(s, len, cur);
    }
    return cur;
}

void utf8_word_bounds_at(const char* s, uint32_t len, uint32_t pos, uint32_t* out_start, uint32_t* out_end) noexcept {
    if (!out_start || !out_end) {
        return;
    }
    if (!s || len == 0u) {
        *out_start = 0u;
        *out_end = 0u;
        return;
    }
    pos = utf8_clamp_boundary(s, len, pos);
    if (pos >= len && pos > 0u) {
        pos = utf8_prev_boundary(s, len, pos);
    }
    const uint32_t cp = utf8_decode_at(s, len, pos);
    if (cp == '\n') {
        *out_start = pos;
        *out_end = utf8_next_boundary(s, len, pos);
        return;
    }
    const uint8_t cls = utf8_char_class(cp);
    uint32_t a = pos;
    while (a > 0u) {
        const uint32_t prev = utf8_prev_boundary(s, len, a);
        const uint32_t pcp = utf8_decode_at(s, len, prev);
        if (pcp == '\n' || utf8_char_class(pcp) != cls) {
            break;
        }
        a = prev;
    }
    uint32_t b = utf8_next_boundary(s, len, pos);
    while (b < len) {
        const uint32_t ncp = utf8_decode_at(s, len, b);
        if (ncp == '\n' || utf8_char_class(ncp) != cls) {
            break;
        }
        b = utf8_next_boundary(s, len, b);
    }
    *out_start = a;
    *out_end = b;
}

void line_bounds_at(const char* s, uint32_t len, uint32_t pos, uint32_t* out_start, uint32_t* out_end) noexcept {
    if (!out_start || !out_end) {
        return;
    }
    if (!s || len == 0u) {
        *out_start = 0u;
        *out_end = 0u;
        return;
    }
    pos = utf8_clamp_boundary(s, len, pos);
    uint32_t a = pos;
    while (a > 0u && s[a - 1u] != '\n') {
        --a;
    }
    uint32_t b = pos;
    while (b < len && s[b] != '\n') {
        ++b;
    }
    *out_start = a;
    *out_end = b;
}

uint32_t erase_text_range(char* buf, uint32_t* io_len, uint32_t a, uint32_t b) noexcept {
    if (!buf || !io_len) {
        return 0u;
    }
    uint32_t len = *io_len;
    const uint32_t lo = utf8_clamp_boundary(buf, len, (a < b) ? a : b);
    const uint32_t hi = utf8_clamp_boundary(buf, len, (a < b) ? b : a);
    if (hi > lo) {
        std::memmove(buf + lo, buf + hi, size_t(len - hi));
        len -= (hi - lo);
        buf[len] = '\0';
        *io_len = len;
    }
    return lo;
}

uint32_t insert_utf8_filtered(
    char* buf,
    uint32_t cap,
    uint32_t* io_len,
    uint32_t sel_a,
    uint32_t sel_b,
    Str ins,
    bool multiline) noexcept
{
    if (!buf || cap == 0u || !io_len) {
        return 0u;
    }
    const uint32_t pos = erase_text_range(buf, io_len, sel_a, sel_b);
    uint32_t cur_len = *io_len;
    if (cur_len >= cap) {
        cur_len = cap - 1u;
        buf[cur_len] = '\0';
        *io_len = cur_len;
    }
    if (!ins.chars || ins.length == 0u) {
        return pos;
    }

    const uint32_t avail = (cap > cur_len + 1u) ? (cap - 1u - cur_len) : 0u;
    if (avail == 0u) {
        return pos;
    }

    // Pass 1: count filtered bytes respecting UTF-8 codepoint boundaries within avail.
    const char* p = ins.chars;
    const char* end = ins.chars + ins.length;
    uint32_t write_bytes = 0u;
    while (p < end) {
        const char* cp_start = p;
        const uint32_t cp = next_utf8_codepoint(p, end);
        if (cp == '\r' || cp == 0x7Fu) {
            continue;
        }
        if (cp < 0x20u && cp != '\n' && cp != '\t') {
            continue;
        }
        const uint32_t seq_bytes = (!multiline && (cp == '\n' || cp == '\t'))
            ? 1u
            : static_cast<uint32_t>(p - cp_start);
        if (write_bytes + seq_bytes > avail) {
            break;
        }
        write_bytes += seq_bytes;
    }
    if (write_bytes == 0u) {
        return pos;
    }

    const uint32_t tail_len = cur_len - pos;
    if (tail_len > 0u) {
        std::memmove(buf + pos + write_bytes, buf + pos, tail_len);
    }

    // Pass 2: materialize filtered bytes directly into target slice.
    p = ins.chars;
    uint32_t dst_off = 0u;
    while (p < end && dst_off < write_bytes) {
        const char* cp_start = p;
        const uint32_t cp = next_utf8_codepoint(p, end);
        if (cp == '\r' || cp == 0x7Fu) {
            continue;
        }
        if (cp < 0x20u && cp != '\n' && cp != '\t') {
            continue;
        }
        if (!multiline && (cp == '\n' || cp == '\t')) {
            buf[pos + dst_off++] = ' ';
        } else {
            const uint32_t seq_bytes = static_cast<uint32_t>(p - cp_start);
            if (dst_off + seq_bytes > write_bytes) {
                break;
            }
            std::memcpy(buf + pos + dst_off, cp_start, seq_bytes);
            dst_off += seq_bytes;
        }
    }

    cur_len += write_bytes;
    buf[cur_len] = '\0';
    *io_len = cur_len;
    return pos + write_bytes;
}

void push_pending_text_op(Context* ctx, PendingTextOp op, Str utf8) noexcept {
    if (!ctx || ctx->pending_text_op_count >= kMaxTextOpsPerFrame) {
        return;
    }
    if (utf8.chars && utf8.length > 0u) {
        const uint32_t avail = kMaxPendingTextBytes - ctx->pending_text_utf8_len;
        const uint32_t max_copy = (utf8.length < avail) ? utf8.length : avail;
        const uint32_t copy_len = utf8_clamp_boundary(utf8.chars, utf8.length, max_copy);
        if (copy_len == 0u) {
            return;
        }
        op.text_offset = static_cast<uint16_t>(ctx->pending_text_utf8_len);
        op.text_len = static_cast<uint16_t>(copy_len);
        std::memcpy(ctx->pending_text_utf8 + ctx->pending_text_utf8_len, utf8.chars, copy_len);
        ctx->pending_text_utf8_len += copy_len;
    }
    ctx->pending_text_ops[ctx->pending_text_op_count++] = op;
}

void clear_ime_composition(Context* ctx) noexcept {
    if (!ctx) {
        return;
    }
    ctx->ime_comp_len = 0u;
    ctx->ime_comp_utf8[0] = '\0';
    ctx->ime_comp_cursor_byte = 0u;
    ctx->ime_comp_sel_len_byte = 0u;
}

struct TextInnerGeom {
    Rect  clip_logical = {};
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    float line_h = 14.0f;
    float line_adv = 14.0f;
};

TextInnerGeom compute_text_inner_geom(Rect box_logical, const TextInputDesc& desc) noexcept {
    const float base_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f;
    const float line_adv = desc.multiline ? std::floor(base_px * 1.35f + 0.5f) : base_px;
    const float ix = box_logical.x + desc.padding.left;
    const float iy = box_logical.y + desc.padding.top;
    const float iw = (box_logical.w > desc.padding.left + desc.padding.right)
        ? (box_logical.w - desc.padding.left - desc.padding.right) : 0.0f;
    const float ih = (box_logical.h > desc.padding.top + desc.padding.bottom)
        ? (box_logical.h - desc.padding.top - desc.padding.bottom) : 0.0f;
    const float oy = desc.multiline
        ? iy
        : (iy + 0.5f * ((ih > base_px) ? (ih - base_px) : 0.0f));
    const float clip_pad_top = (desc.padding.top > 2.0f) ? 2.0f : desc.padding.top;
    const float clip_pad_bot = (desc.padding.bottom > 3.0f) ? 3.0f : desc.padding.bottom;
    return TextInnerGeom{
        .clip_logical = Rect{ ix, iy - clip_pad_top, iw, ih + clip_pad_top + clip_pad_bot },
        .origin_x = ix,
        .origin_y = oy,
        .line_h = base_px,
        .line_adv = line_adv,
    };
}

float measure_cp_logical_adv(Context* ctx, uint32_t cp, float font_size_px, float letter_spacing) noexcept {
    const float base_px = (font_size_px > 0.0f) ? font_size_px : 14.0f;
    if (!ctx || !ctx->atlas.font_valid) {
        return (cp == '\t' ? 4.0f : 1.0f) * (base_px * 0.55f) + letter_spacing;
    }
    const float dpi = (ctx->dpi_scale > 0.0f) ? ctx->dpi_scale : 1.0f;
    const float phys_px = base_px * dpi;
    if (cp == '\t') {
        const GlyphEntry* sp = font_atlas_get_glyph(&ctx->atlas, ' ', phys_px);
        return 4.0f * (sp->advance / dpi) + letter_spacing;
    }
    const GlyphEntry* g = font_atlas_get_glyph(&ctx->atlas, cp, phys_px);
    return (g->advance / dpi) + letter_spacing;
}

struct TextVisualPos {
    float    x = 0.0f;
    float    y = 0.0f;
    uint32_t line_idx = 0u;
};

TextVisualPos text_byte_to_visual(Context* ctx, const TextInputDesc& desc, Str text, uint32_t byte_pos) noexcept {
    const float base_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f;
    const float line_adv = desc.multiline ? std::floor(base_px * 1.35f + 0.5f) : base_px;
    TextVisualPos out = {};
    if (!text.chars || text.length == 0u) {
        return out;
    }
    const uint32_t clamped = utf8_clamp_boundary(text.chars, text.length, byte_pos);
    const char* p = text.chars;
    const char* end = text.chars + clamped;
    while (p < end) {
        const uint32_t cp = next_utf8_codepoint(p, end);
        if (cp == '\n' && desc.multiline) {
            out.x = 0.0f;
            out.y += line_adv;
            ++out.line_idx;
            continue;
        }
        const uint32_t vis_cp = desc.password ? 0x2022u : cp;
        out.x += measure_cp_logical_adv(ctx, vis_cp, base_px, desc.letter_spacing);
    }
    return out;
}

float text_slice_width(Context* ctx, const TextInputDesc& desc, const char* s, uint32_t byte_len, bool apply_password_mask) noexcept {
    if (!s || byte_len == 0u) {
        return 0.0f;
    }
    const float base_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f;
    const char* p = s;
    const char* end = s + byte_len;
    float w = 0.0f;
    while (p < end) {
        const uint32_t cp = next_utf8_codepoint(p, end);
        if (cp == '\n') {
            continue;
        }
        const uint32_t vis_cp = apply_password_mask ? 0x2022u : cp;
        w += measure_cp_logical_adv(ctx, vis_cp, base_px, desc.letter_spacing);
    }
    return w;
}

uint32_t visual_point_to_byte(Context* ctx, const TextInputDesc& desc, Str text, float local_x, float local_y) noexcept {
    if (!text.chars || text.length == 0u) {
        return 0u;
    }
    const float base_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f;
    const float line_adv = desc.multiline ? std::floor(base_px * 1.35f + 0.5f) : base_px;
    const uint32_t target_line = desc.multiline
        ? static_cast<uint32_t>((local_y > 0.0f) ? std::floor(local_y / line_adv) : 0.0f)
        : 0u;

    const char* p = text.chars;
    const char* end = text.chars + text.length;
    uint32_t cur_line = 0u;
    float pen_x = 0.0f;
    while (p < end) {
        const char* cp_start = p;
        const uint32_t cp = next_utf8_codepoint(p, end);
        if (cp == '\n' && desc.multiline) {
            if (cur_line == target_line) {
                return static_cast<uint32_t>(cp_start - text.chars);
            }
            ++cur_line;
            pen_x = 0.0f;
            continue;
        }
        if (cur_line == target_line) {
            const uint32_t vis_cp = desc.password ? 0x2022u : cp;
            const float adv = measure_cp_logical_adv(ctx, vis_cp, base_px, desc.letter_spacing);
            if (local_x < pen_x + 0.5f * adv) {
                return static_cast<uint32_t>(cp_start - text.chars);
            }
            pen_x += adv;
        }
    }
    return text.length;
}

void move_caret_vertical(
    Context* ctx,
    const TextInputDesc& desc,
    Str text,
    WidgetState* ws,
    int32_t delta,
    bool extend_selection) noexcept
{
    if (!ws || !desc.multiline) {
        return;
    }
    const TextVisualPos cur = text_byte_to_visual(ctx, desc, text, ws->edit_cursor);
    if (ws->edit_preferred_x < 0.0f) {
        ws->edit_preferred_x = cur.x;
    }
    const float base_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f;
    const float line_adv = std::floor(base_px * 1.35f + 0.5f);
    const TextVisualPos total_end = text_byte_to_visual(ctx, desc, text, text.length);

    if (delta < 0) {
        if (cur.line_idx == 0u) {
            ws->edit_cursor = 0u;
        } else {
            const uint32_t target_line = cur.line_idx - 1u;
            ws->edit_cursor = visual_point_to_byte(
                ctx,
                desc,
                text,
                ws->edit_preferred_x,
                (static_cast<float>(target_line) + 0.5f) * line_adv);
        }
    } else if (delta > 0) {
        if (cur.line_idx >= total_end.line_idx) {
            ws->edit_cursor = text.length;
        } else {
            const uint32_t target_line = cur.line_idx + 1u;
            ws->edit_cursor = visual_point_to_byte(
                ctx,
                desc,
                text,
                ws->edit_preferred_x,
                (static_cast<float>(target_line) + 0.5f) * line_adv);
        }
    }
    if (!extend_selection) {
        ws->edit_sel_anchor = ws->edit_cursor;
    }
}

void measure_text_content_and_ime(
    Context* ctx,
    const TextInputDesc& desc,
    Str committed,
    uint32_t cursor_byte,
    bool composing,
    float* out_total_w,
    float* out_total_h,
    float* out_comp_x,
    float* out_comp_y,
    float* out_comp_w,
    float* out_caret_x,
    float* out_caret_y) noexcept
{
    const float base_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f;
    const float line_adv = desc.multiline ? std::floor(base_px * 1.35f + 0.5f) : base_px;
    const char* s = committed.chars ? committed.chars : "";
    const uint32_t len = committed.length;
    const uint32_t c_byte = utf8_clamp_boundary(s, len, cursor_byte);

    float pen_x = 0.0f;
    float pen_y = 0.0f;
    float max_w = 0.0f;

    const auto walk_slice = [&](const char* beg, uint32_t slice_len, bool mask) noexcept {
        const char* p = beg;
        const char* end = beg + slice_len;
        while (p < end) {
            const uint32_t cp = next_utf8_codepoint(p, end);
            if (cp == '\n' && desc.multiline) {
                if (pen_x > max_w) {
                    max_w = pen_x;
                }
                pen_x = 0.0f;
                pen_y += line_adv;
                continue;
            }
            const uint32_t vis_cp = mask ? 0x2022u : cp;
            pen_x += measure_cp_logical_adv(ctx, vis_cp, base_px, desc.letter_spacing);
        }
        if (pen_x > max_w) {
            max_w = pen_x;
        }
    };

    walk_slice(s, c_byte, desc.password);
    const float cx = pen_x;
    const float cy = pen_y;
    float cw = 0.0f;
    float caret_dx = 0.0f;
    if (composing && ctx && ctx->ime_comp_len > 0u) {
        cw = text_slice_width(ctx, desc, ctx->ime_comp_utf8, ctx->ime_comp_len, false);
        caret_dx = text_slice_width(ctx, desc, ctx->ime_comp_utf8, ctx->ime_comp_cursor_byte, false);
        pen_x += cw;
        if (pen_x > max_w) {
            max_w = pen_x;
        }
    }
    if (c_byte < len) {
        walk_slice(s + c_byte, len - c_byte, desc.password);
    }

    if (out_total_w) *out_total_w = max_w;
    if (out_total_h) *out_total_h = pen_y + base_px;
    if (out_comp_x) *out_comp_x = cx;
    if (out_comp_y) *out_comp_y = cy;
    if (out_comp_w) *out_comp_w = cw;
    if (out_caret_x) *out_caret_x = cx + caret_dx;
    if (out_caret_y) *out_caret_y = cy;
}

void update_text_scroll_and_ime(
    Context* ctx,
    WidgetState* ws,
    const TextInputDesc& desc,
    const TextInnerGeom& geom,
    Str committed,
    uint32_t cursor_byte,
    bool is_focused,
    bool composing,
    float* out_scroll_x,
    float* out_scroll_y) noexcept
{
    float total_w = 0.0f;
    float total_h = 0.0f;
    float comp_x = 0.0f;
    float comp_y = 0.0f;
    float comp_w = 0.0f;
    float caret_x = 0.0f;
    float caret_y = 0.0f;
    measure_text_content_and_ime(
        ctx, desc, committed, cursor_byte, composing,
        &total_w, &total_h, &comp_x, &comp_y, &comp_w, &caret_x, &caret_y);

    const float view_w = geom.clip_logical.w;
    const float view_h = geom.clip_logical.h;
    float sx = ws ? ws->edit_scroll_x : 0.0f;
    float sy = ws ? ws->edit_scroll_y : 0.0f;

    if (view_w > 0.0f) {
        const float max_sx = (total_w + 6.0f > view_w) ? (total_w + 6.0f - view_w) : 0.0f;
        if (is_focused) {
            if (caret_x + 4.0f > sx + view_w) {
                sx = (caret_x + 4.0f) - view_w;
            }
            if (caret_x - 4.0f < sx) {
                sx = (caret_x > 4.0f) ? (caret_x - 4.0f) : 0.0f;
            }
        }
        sx = clamp_range(sx, 0.0f, max_sx);
    } else {
        sx = 0.0f;
    }

    if (desc.multiline && view_h > 0.0f) {
        const float max_sy = (total_h > view_h) ? (total_h - view_h) : 0.0f;
        if (is_focused) {
            if (caret_y + geom.line_h > sy + view_h) {
                sy = (caret_y + geom.line_h) - view_h;
            }
            if (caret_y < sy) {
                sy = caret_y;
            }
        }
        sy = clamp_range(sy, 0.0f, max_sy);
    } else {
        sy = 0.0f;
    }

    if (ws) {
        ws->edit_scroll_x = sx;
        ws->edit_scroll_y = sy;
    }
    if (out_scroll_x) *out_scroll_x = sx;
    if (out_scroll_y) *out_scroll_y = sy;

    if (ctx && is_focused && !desc.read_only && !desc.password) {
        const float win_scale = logical_to_window_scale(ctx);
        if (composing && comp_w > 0.0f) {
            ctx->ime_cursor_logical = Rect{
                .x = geom.origin_x + comp_x - sx,
                .y = geom.origin_y + comp_y - sy,
                .w = (comp_w > 2.0f) ? comp_w : 2.0f,
                .h = geom.line_h,
            };
            ctx->ime_cursor_offset_win = static_cast<int32_t>(std::floor((caret_x - comp_x) * win_scale + 0.5f));
        } else {
            ctx->ime_cursor_logical = Rect{
                .x = geom.origin_x + caret_x - sx,
                .y = geom.origin_y + caret_y - sy,
                .w = 2.0f,
                .h = geom.line_h,
            };
            ctx->ime_cursor_offset_win = 0;
        }
    }
}

void emit_utf8_glyph_run(
    Context* ctx,
    const char* s,
    uint32_t len,
    float font_size_px,
    float letter_spacing,
    float ascender_px,
    float line_adv_logical,
    Color tint,
    float origin_x,
    float origin_y,
    float scroll_x,
    float scroll_y,
    const ClipEntry& clip_px,
    float dpi,
    bool multiline,
    bool mask_bullets,
    float* io_pen_x,
    float* io_pen_y) noexcept
{
    if (!ctx || !ctx->atlas.font_valid || !s || len == 0u || !io_pen_x || !io_pen_y) {
        return;
    }
    const float base_px = (font_size_px > 0.0f) ? font_size_px : 14.0f;
    const float phys_px = base_px * dpi;
    const char* p = s;
    const char* end = s + len;
    while (p < end) {
        const uint32_t cp = next_utf8_codepoint(p, end);
        if (cp == '\n' && multiline) {
            *io_pen_x = 0.0f;
            *io_pen_y += line_adv_logical;
            continue;
        }
        const uint32_t vis_cp = mask_bullets ? 0x2022u : ((cp == '\t') ? ' ' : cp);
        const GlyphEntry* g = font_atlas_get_glyph(&ctx->atlas, vis_cp, phys_px);
        const float adv = measure_cp_logical_adv(ctx, vis_cp, base_px, letter_spacing);
        if (cp == '\t') {
            *io_pen_x += measure_cp_logical_adv(ctx, '\t', base_px, letter_spacing);
            continue;
        }
        if (g->visible) {
            const float gx = std::floor((origin_x + *io_pen_x - scroll_x) * dpi + g->xoff + 0.5f);
            const float gy = std::floor((origin_y + *io_pen_y - scroll_y) * dpi + ascender_px + g->yoff + 0.5f);
            push_prim(
                ctx,
                Prim::make_glyph(
                    Rect{ gx, gy, g->width, g->height },
                    clip_px.rect,
                    tint,
                    Vec2{ g->u0, g->v0 },
                    Vec2{ g->u1, g->v1 },
                    0u,
                    clip_px.radius,
                    PrimFlag_StemDarken));
        }
        *io_pen_x += adv;
    }
}

void emit_text_input_command(
    Context* ctx,
    const TextInputExtra* ti,
    Clay_BoundingBox bounds_logical,
    Rect box_px,
    const ClipEntry& outer_clip,
    float dpi) noexcept
{
    if (!ctx || !ti) {
        return;
    }
    const TextInputDesc& desc = ti->desc;
    WidgetState* ws = (desc.id != 0u) ? state_map_find(&ctx->states, desc.id) : nullptr;
    const Rect box_logical = { bounds_logical.x, bounds_logical.y, bounds_logical.width, bounds_logical.height };
    if (ws) {
        ws->last_box_logical = box_logical;
    }

    const TextInnerGeom geom = compute_text_inner_geom(box_logical, desc);
    const Corners rad_px = scale_corners(desc.radius, dpi);
    const bool is_focused = ti->is_focused;
    const bool composing = is_focused && !desc.read_only && !desc.password && ctx->ime_comp_len > 0u;

    if (is_focused && desc.focus_border_color.a > 0u) {
        const Color glow_col = Color::rgba8(desc.focus_border_color.r, desc.focus_border_color.g, desc.focus_border_color.b, 80u);
        push_prim(
            ctx,
            Prim::make_shadow(box_px, outer_clip.rect, glow_col, rad_px, 10.0f * dpi, outer_clip.radius),
            nullptr,
            20.0f * dpi + 2.0f);
    }

    const Color border_col = is_focused ? desc.focus_border_color : desc.border_color;
    const float border_w = (is_focused ? desc.focus_border_width : desc.border_width) * dpi;
    push_prim(
        ctx,
        Prim::make_rect(box_px, outer_clip.rect, desc.background, rad_px, border_col, border_w, 1.0f, outer_clip.radius));

    float scroll_x = 0.0f;
    float scroll_y = 0.0f;
    update_text_scroll_and_ime(
        ctx, ws, desc, geom, ti->committed_text, ti->cursor_byte, is_focused, composing, &scroll_x, &scroll_y);

    const Rect inner_px = {
        geom.clip_logical.x * dpi,
        geom.clip_logical.y * dpi,
        geom.clip_logical.w * dpi,
        geom.clip_logical.h * dpi,
    };
    const ClipEntry inner_clip = {
        .rect = rect_intersect(outer_clip.rect, inner_px),
        .radius = 0.0f,
    };

    const float base_px = geom.line_h;
    const float phys_px = base_px * dpi;
    const float scale = ctx->atlas.font_valid ? stbtt_ScaleForPixelHeight(&ctx->atlas.font_info, phys_px) : 0.0f;
    const float ascender_px = ctx->atlas.font_valid ? (static_cast<float>(ctx->atlas.font_ascent) * scale) : (phys_px * 0.8f);

    const char* s = ti->committed_text.chars ? ti->committed_text.chars : "";
    const uint32_t len = ti->committed_text.length;
    const uint32_t c_byte = utf8_clamp_boundary(s, len, ti->cursor_byte);
    const uint32_t a_byte = utf8_clamp_boundary(s, len, ti->sel_anchor_byte);
    const uint32_t sel_min = (c_byte < a_byte) ? c_byte : a_byte;
    const uint32_t sel_max = (c_byte < a_byte) ? a_byte : c_byte;

    // 1. Emit per-line selection highlight quads
    if (sel_max > sel_min && desc.selection_color.a > 0u) {
        const Color sel_col = is_focused
            ? desc.selection_color
            : Color::rgba8(desc.selection_color.r, desc.selection_color.g, desc.selection_color.b, static_cast<uint8_t>(desc.selection_color.a * 45u / 100u));
        uint32_t pos = 0u;
        float line_y = 0.0f;
        while (pos <= len) {
            uint32_t ls = 0u;
            uint32_t le = 0u;
            if (desc.multiline) {
                line_bounds_at(s, len, pos, &ls, &le);
            } else {
                ls = 0u;
                le = len;
            }
            const uint32_t hs = (sel_min > ls) ? sel_min : ls;
            const uint32_t he = (sel_max < le) ? sel_max : le;
            const bool spans_nl = desc.multiline && sel_max > le && sel_min <= le;
            if (he > hs || spans_nl) {
                const float x0 = text_slice_width(ctx, desc, s + ls, hs - ls, desc.password);
                float x1 = text_slice_width(ctx, desc, s + ls, he - ls, desc.password);
                if (spans_nl) {
                    x1 += 6.0f;
                }
                const Rect sel_px = {
                    (geom.origin_x + x0 - scroll_x) * dpi,
                    (geom.origin_y + line_y - scroll_y - 1.0f) * dpi,
                    ((x1 > x0) ? (x1 - x0) : 2.0f) * dpi,
                    (geom.line_h + 2.0f) * dpi,
                };
                push_prim(ctx, Prim::make_rect(sel_px, inner_clip.rect, sel_col, Corners::all(2.0f * dpi), {}, 0.0f, 1.0f, 0.0f));
            }
            if (!desc.multiline || le >= len) {
                break;
            }
            pos = le + 1u;
            line_y += geom.line_adv;
        }
    }

    // 2. Emit placeholder or committed text + inline IME preedit composition
    float caret_vx = 0.0f;
    float caret_vy = 0.0f;
    if (len == 0u && !composing) {
        if (desc.placeholder.chars && desc.placeholder.length > 0u) {
            float px = 0.0f;
            float py = 0.0f;
            emit_utf8_glyph_run(
                ctx, desc.placeholder.chars, desc.placeholder.length,
                base_px, desc.letter_spacing, ascender_px, geom.line_adv,
                desc.placeholder_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
                inner_clip, dpi, desc.multiline, false, &px, &py);
        }
    } else if (composing) {
        float pen_x = 0.0f;
        float pen_y = 0.0f;
        emit_utf8_glyph_run(
            ctx, s, c_byte, base_px, desc.letter_spacing, ascender_px, geom.line_adv,
            desc.text_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
            inner_clip, dpi, desc.multiline, desc.password, &pen_x, &pen_y);

        const float comp_start_x = pen_x;
        const float comp_start_y = pen_y;
        const float comp_w = text_slice_width(ctx, desc, ctx->ime_comp_utf8, ctx->ime_comp_len, false);
        const float caret_dx = text_slice_width(ctx, desc, ctx->ime_comp_utf8, ctx->ime_comp_cursor_byte, false);
        caret_vx = comp_start_x + caret_dx;
        caret_vy = comp_start_y;

        if (ctx->ime_comp_sel_len_byte > 0u) {
            const float cl_w = text_slice_width(
                ctx, desc, ctx->ime_comp_utf8 + ctx->ime_comp_cursor_byte, ctx->ime_comp_sel_len_byte, false);
            if (cl_w > 0.0f) {
                const Rect clause_bg = {
                    (geom.origin_x + comp_start_x + caret_dx - scroll_x) * dpi,
                    (geom.origin_y + comp_start_y - scroll_y - 1.0f) * dpi,
                    cl_w * dpi,
                    (geom.line_h + 2.0f) * dpi,
                };
                const Color clause_col = Color::rgba8(
                    desc.composition_color.r, desc.composition_color.g, desc.composition_color.b, 48u);
                push_prim(ctx, Prim::make_rect(clause_bg, inner_clip.rect, clause_col, Corners::all(2.0f * dpi)));
            }
        }

        emit_utf8_glyph_run(
            ctx, ctx->ime_comp_utf8, ctx->ime_comp_len, base_px, desc.letter_spacing, ascender_px, geom.line_adv,
            desc.composition_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
            inner_clip, dpi, false, false, &pen_x, &pen_y);

        if (comp_w > 0.0f) {
            const Rect uline = {
                (geom.origin_x + comp_start_x - scroll_x) * dpi,
                (geom.origin_y + comp_start_y + geom.line_h - scroll_y + 0.5f) * dpi,
                comp_w * dpi,
                (1.5f * dpi > 1.0f) ? (1.5f * dpi) : 1.0f,
            };
            push_prim(ctx, Prim::make_rect(uline, inner_clip.rect, desc.composition_color));
        }

        if (c_byte < len) {
            emit_utf8_glyph_run(
                ctx, s + c_byte, len - c_byte, base_px, desc.letter_spacing, ascender_px, geom.line_adv,
                desc.text_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
                inner_clip, dpi, desc.multiline, desc.password, &pen_x, &pen_y);
        }
    } else {
        float pen_x = 0.0f;
        float pen_y = 0.0f;
        emit_utf8_glyph_run(
            ctx, s, len, base_px, desc.letter_spacing, ascender_px, geom.line_adv,
            desc.text_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
            inner_clip, dpi, desc.multiline, desc.password, &pen_x, &pen_y);
        const TextVisualPos cpos = text_byte_to_visual(ctx, desc, ti->committed_text, c_byte);
        caret_vx = cpos.x;
        caret_vy = cpos.y;
    }

    // 3. Emit SDF caret
    if (is_focused && ctx->caret_blink_on && desc.caret_color.a > 0u) {
        const float cw_px = (2.0f * dpi > 1.5f) ? (2.0f * dpi) : 1.5f;
        const Rect caret_px = {
            std::floor((geom.origin_x + caret_vx - scroll_x) * dpi + 0.5f),
            (geom.origin_y + caret_vy - scroll_y - 1.0f) * dpi,
            cw_px,
            (geom.line_h + 2.0f) * dpi,
        };
        push_prim(ctx, Prim::make_rect(caret_px, inner_clip.rect, desc.caret_color, Corners::all(0.5f * cw_px)));
    }
}

} // namespace

bool needs_redraw(const Context* ctx) noexcept {
    return ctx != nullptr && (ctx->redraw_budget > 0u || ctx->active_anim_count > 0u);
}

void request_redraw(Context* ctx_arg) noexcept {
    Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (ctx) {
        ctx->redraw_budget = 2u;
    }
}

void wake_up(Context* ctx_arg) noexcept {
    Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (!ctx) {
        return;
    }
    if (ctx->wake_event_type != 0u) {
        SDL_Event ev = {};
        ev.type = ctx->wake_event_type;
        SDL_PushEvent(&ev);
    } else {
        ctx->redraw_budget = 2u;
    }
}

bool wait_events(Context* ctx, SDL_Event* first_event_out, int32_t timeout_ms) noexcept {
    if (!first_event_out) {
        return false;
    }
    if (needs_redraw(ctx)) {
        return SDL_PollEvent(first_event_out);
    }
    if (ctx && ctx->ime_focused_id != 0u && ctx->caret_blink_cycles < 20u && (timeout_ms < 0 || timeout_ms > 500)) {
        if (SDL_WaitEventTimeout(first_event_out, 500)) {
            return true;
        }
        ctx->caret_blink_on = !ctx->caret_blink_on;
        ++ctx->caret_blink_cycles;
        if (ctx->caret_blink_cycles >= 20u) {
            ctx->caret_blink_on = true;
        }
        request_redraw(ctx);
        return false;
    }
    if (timeout_ms < 0) {
        return SDL_WaitEvent(first_event_out);
    }
    return SDL_WaitEventTimeout(first_event_out, timeout_ms);
}

void process_event(Context* ctx, const SDL_Event& event) noexcept {
    if (!ctx) {
        return;
    }
    if (ctx->wake_event_type != 0u && event.type == ctx->wake_event_type) {
        request_redraw(ctx);
        return;
    }

    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
            ctx->event_pointer_pos = window_to_logical_pos(ctx, event.motion.x, event.motion.y);
            ctx->has_event_pointer = true;
            request_redraw(ctx);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                ctx->event_pointer_pos = window_to_logical_pos(ctx, event.button.x, event.button.y);
                ctx->event_pointer_down = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                ctx->has_event_pointer = true;
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    ctx->last_mouse_clicks = (event.button.clicks > 0u) ? event.button.clicks : 1u;
                    ctx->caret_blink_on = true;
                    ctx->caret_blink_cycles = 0u;
                }
                request_redraw(ctx);
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL: {
            float wx = event.wheel.x;
            float wy = event.wheel.y;
            if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                wx = -wx;
                wy = -wy;
            }
            ctx->event_scroll_delta.x += wx;
            ctx->event_scroll_delta.y += wy;
            if (event.wheel.mouse_x >= 0.0f && event.wheel.mouse_y >= 0.0f) {
                ctx->event_pointer_pos = window_to_logical_pos(ctx, event.wheel.mouse_x, event.wheel.mouse_y);
                ctx->has_event_pointer = true;
            }
            request_redraw(ctx);
            break;
        }
        case SDL_EVENT_TEXT_EDITING: {
            const char* src = event.edit.text ? event.edit.text : "";
            const uint32_t raw_len = static_cast<uint32_t>(std::strlen(src));
            const uint32_t max_copy = (raw_len < (kMaxImeCompBytes - 1u)) ? raw_len : (kMaxImeCompBytes - 1u);
            const uint32_t clamped = utf8_clamp_boundary(src, raw_len, max_copy);
            if (clamped > 0u) {
                std::memcpy(ctx->ime_comp_utf8, src, clamped);
            }
            ctx->ime_comp_utf8[clamped] = '\0';
            ctx->ime_comp_len = clamped;
            ctx->ime_comp_cursor_byte = (event.edit.start < 0)
                ? clamped
                : utf8_codepoint_to_byte_offset(ctx->ime_comp_utf8, clamped, event.edit.start);
            ctx->ime_comp_sel_len_byte = (event.edit.length <= 0)
                ? 0u
                : utf8_codepoint_to_byte_offset(
                    ctx->ime_comp_utf8 + ctx->ime_comp_cursor_byte,
                    clamped - ctx->ime_comp_cursor_byte,
                    event.edit.length);
            ctx->caret_blink_on = true;
            ctx->caret_blink_cycles = 0u;
            request_redraw(ctx);
            break;
        }
        case SDL_EVENT_TEXT_INPUT: {
            clear_ime_composition(ctx);
            if (event.text.text && event.text.text[0] != '\0') {
                push_pending_text_op(
                    ctx,
                    PendingTextOp{ .kind = TextEditOpKind::InsertText },
                    str_cstr(event.text.text));
            }
            ctx->caret_blink_on = true;
            ctx->caret_blink_cycles = 0u;
            request_redraw(ctx);
            break;
        }
        case SDL_EVENT_KEY_DOWN:
            ctx->caret_blink_on = true;
            ctx->caret_blink_cycles = 0u;
            if (ctx->ime_comp_len > 0u) {
                if (event.key.key == SDLK_ESCAPE) {
                    clear_ime_composition(ctx);
                    if (!ctx->headless && ctx->window) {
                        SDL_ClearComposition(ctx->window);
                    }
                }
            } else {
                if (event.key.key == SDLK_TAB) {
                    ctx->pending_tab_delta += ((event.key.mod & SDL_KMOD_SHIFT) != 0u) ? -1 : 1;
                } else {
                    push_pending_text_op(
                        ctx,
                        PendingTextOp{
                            .kind = TextEditOpKind::KeyPress,
                            .mod = event.key.mod,
                            .key = event.key.key,
                        },
                        {});
                }
            }
            request_redraw(ctx);
            break;
        case SDL_EVENT_KEY_UP:
            request_redraw(ctx);
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            if (!ctx->mouse_captured) {
                ctx->event_pointer_pos = Vec2{-1.0f, -1.0f};
                ctx->has_event_pointer = false;
            }
            request_redraw(ctx);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            ctx->event_pointer_down = false;
            ctx->active_id = 0u;
            clear_ime_composition(ctx);
            if (ctx->mouse_captured) {
                SDL_CaptureMouse(false);
                ctx->mouse_captured = false;
            }
            request_redraw(ctx);
            break;
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            request_redraw(ctx);
            break;
        default:
            break;
    }
}

void begin_frame(Context* ctx, const FrameDesc& desc) noexcept {
    if (!ctx || !ctx->clay_ctx) {
        return;
    }
    set_current_context(ctx);
    ctx->in_frame = true;
    ctx->layout_start_ticks = SDL_GetPerformanceCounter();
    ++ctx->frame_index;
    ctx->elapsed_seconds += desc.dt_seconds;

    if (ctx->redraw_budget > 0u) {
        --ctx->redraw_budget;
    }
    ctx->sim_dt = (desc.dt_seconds <= 0.0f)
        ? 0.0f
        : ((desc.dt_seconds > 0.1f) ? (1.0f / 60.0f) : desc.dt_seconds);
    ctx->active_anim_count = 0u;
    ctx->desired_cursor = Cursor::Arrow;

    ctx->prev_ime_focused_id = ctx->ime_focused_id;
    ctx->ime_focused_id = 0u;
    ctx->ime_focused_password = false;
    ctx->frame_text_op_count = ctx->pending_text_op_count;
    if (ctx->frame_text_op_count > 0u) {
        std::memcpy(ctx->frame_text_ops, ctx->pending_text_ops, size_t(ctx->frame_text_op_count) * sizeof(PendingTextOp));
    }
    if (ctx->pending_text_utf8_len > 0u) {
        std::memcpy(ctx->frame_text_utf8, ctx->pending_text_utf8, ctx->pending_text_utf8_len);
    }
    ctx->pending_text_op_count = 0u;
    ctx->pending_text_utf8_len = 0u;

    ctx->prev_focus_count = ctx->focus_count;
    if (ctx->prev_focus_count > 0u) {
        std::memcpy(ctx->prev_focus_order, ctx->focus_order, size_t(ctx->prev_focus_count) * sizeof(uint32_t));
    }
    ctx->focus_count = 0u;

    if (ctx->pending_tab_delta != 0 && ctx->prev_focus_count > 0u) {
        int32_t cur_idx = -1;
        for (uint32_t i = 0u; i < ctx->prev_focus_count; ++i) {
            if (ctx->prev_focus_order[i] == ctx->focus_id) {
                cur_idx = static_cast<int32_t>(i);
                break;
            }
        }
        const int32_t n = static_cast<int32_t>(ctx->prev_focus_count);
        int32_t next_idx = (cur_idx < 0)
            ? ((ctx->pending_tab_delta > 0) ? 0 : (n - 1))
            : ((cur_idx + ctx->pending_tab_delta) % n);
        if (next_idx < 0) {
            next_idx += n;
        }
        const uint32_t next_focus = ctx->prev_focus_order[next_idx];
        if (next_focus != ctx->focus_id) {
            clear_ime_composition(ctx);
        }
        ctx->focus_id = next_focus;
        ctx->pending_tab_delta = 0;
        ctx->caret_blink_on = true;
        ctx->caret_blink_cycles = 0u;
        request_redraw(ctx);
    }

    ctx->frame_arena.reset();
    ctx->prim_count = 0u;
    ctx->batch_count = 0u;

    const float dpi = (desc.dpi_scale > 0.0f) ? desc.dpi_scale : 1.0f;
    ctx->viewport_w = (desc.viewport_w > 0u) ? desc.viewport_w : 1u;
    ctx->viewport_h = (desc.viewport_h > 0u) ? desc.viewport_h : 1u;
    ctx->dpi_scale = dpi;

    const Clay_Dimensions logical_dims = {
        .width = static_cast<float>(ctx->viewport_w) / dpi,
        .height = static_cast<float>(ctx->viewport_h) / dpi,
    };
    Clay_SetLayoutDimensions(logical_dims);

    Vec2 pointer_pos = desc.pointer_pos;
    if (pointer_pos.x < 0.0f && pointer_pos.y < 0.0f && ctx->has_event_pointer) {
        pointer_pos = ctx->event_pointer_pos;
    }
    ctx->pointer_pos = pointer_pos;

    const bool pointer_down = desc.pointer_down || ctx->event_pointer_down;
    const bool pressed_this_frame = pointer_down && !ctx->prev_pointer_down;
    const bool released_this_frame = !pointer_down && ctx->prev_pointer_down;
    ctx->prev_pointer_down = pointer_down;

    const Vec2 scroll = (desc.scroll_delta.x != 0.0f || desc.scroll_delta.y != 0.0f)
        ? desc.scroll_delta
        : ctx->event_scroll_delta;
    ctx->event_scroll_delta = {};

    // Update Clay hit-test results against the previous frame's tree BEFORE Clay_BeginLayout() resets it.
    Clay_SetPointerState(Clay_Vector2{pointer_pos.x, pointer_pos.y}, pointer_down);
    Clay_UpdateScrollContainers(false, Clay_Vector2{0.0f, 0.0f}, 0.0f);

    state_map_clear_flags(&ctx->states, true, false);
    const Clay_ElementIdArray over = Clay_GetPointerOverIds();

    if (pressed_this_frame) {
        state_map_clear_flags(&ctx->states, false, true);
        bool handled_scrollbar = false;
        for (int32_t i = over.length - 1; i >= 0; --i) {
            WidgetState* ws = state_map_find(&ctx->states, over.internalArray[i].id);
            if (!ws || !ws->is_user_element || !ws->is_scroll_y) {
                continue;
            }
            const ScrollbarGeom sb = compute_vscrollbar_geom(ws);
            if (sb.visible && sb.hit_track.contains(pointer_pos)) {
                ctx->active_id = id_index(ws->key, kScrollbarYTag);
                ws->scroll_vel.y = 0.0f;
                if (pointer_pos.y >= sb.thumb.y && pointer_pos.y <= (sb.thumb.y + sb.thumb.h)) {
                    ctx->scrollbar_drag_grab_offset = pointer_pos.y - sb.thumb.y;
                } else {
                    ctx->scrollbar_drag_grab_offset = 0.5f * sb.thumb.h;
                    apply_scrollbar_drag(ws, pointer_pos.y, ctx->scrollbar_drag_grab_offset);
                }
                request_redraw(ctx);
                handled_scrollbar = true;
                break;
            }
        }
        if (!handled_scrollbar) {
            uint32_t top_hit = 0u;
            uint32_t top_focus = 0u;
            for (int32_t i = over.length - 1; i >= 0; --i) {
                const uint32_t oid = over.internalArray[i].id;
                WidgetState* ws = state_map_find(&ctx->states, oid);
                if (ws && ws->is_user_element) {
                    if (top_hit == 0u) {
                        top_hit = oid;
                        ws->pressed_down = true;
                        ws->pressed_this_frame = true;
                    }
                    if (top_focus == 0u && ws->is_focusable) {
                        top_focus = oid;
                    }
                }
            }
            if (top_focus != ctx->focus_id) {
                clear_ime_composition(ctx);
            }
            ctx->active_id = top_hit;
            ctx->focus_id = top_focus;
        }
    }

    if (pointer_down && ctx->active_id != 0u) {
        if (!ctx->headless && ctx->window && !ctx->mouse_captured) {
            if (SDL_CaptureMouse(true)) {
                ctx->mouse_captured = true;
            }
        }
        if (WidgetState* sb_ws = find_scrollbar_owner(ctx, ctx->active_id)) {
            apply_scrollbar_drag(sb_ws, pointer_pos.y, ctx->scrollbar_drag_grab_offset);
            ctx->desired_cursor = Cursor::Hand;
        }
    } else if (released_this_frame) {
        if (ctx->mouse_captured) {
            SDL_CaptureMouse(false);
            ctx->mouse_captured = false;
        }
        if (ctx->active_id != 0u) {
            if (WidgetState* ws = state_map_find(&ctx->states, ctx->active_id)) {
                if (ws->is_user_element && Clay_PointerOver(make_clay_id(ctx->active_id))) {
                    ws->clicked_this_frame = true;
                }
            }
        }
        state_map_clear_flags(&ctx->states, false, true);
        ctx->active_id = 0u;
    }

    // Apply scroll wheel impulses to the deepest hovered scroll container that has remaining room
    if (scroll.x != 0.0f || scroll.y != 0.0f) {
        WidgetState* target_scroll = nullptr;
        WidgetState* fallback_scroll = nullptr;
        for (int32_t i = over.length - 1; i >= 0; --i) {
            WidgetState* ws = state_map_find(&ctx->states, over.internalArray[i].id);
            if (!ws || !ws->is_user_element || (!ws->is_scroll_x && !ws->is_scroll_y)) {
                continue;
            }
            if (!fallback_scroll) {
                fallback_scroll = ws;
            }
            const bool can_y = ws->is_scroll_y && ws->scroll_max.y > 0.5f &&
                ((scroll.y < 0.0f && ws->scroll_pos.y < ws->scroll_max.y - 0.5f) ||
                 (scroll.y > 0.0f && ws->scroll_pos.y > 0.5f));
            const bool can_x = ws->is_scroll_x && ws->scroll_max.x > 0.5f &&
                ((scroll.x < 0.0f && ws->scroll_pos.x < ws->scroll_max.x - 0.5f) ||
                 (scroll.x > 0.0f && ws->scroll_pos.x > 0.5f));
            if (can_y || can_x) {
                target_scroll = ws;
                break;
            }
        }
        WidgetState* sws = target_scroll ? target_scroll : fallback_scroll;
        if (sws) {
            if (ctx->sim_dt <= 0.0f) {
                if (sws->is_scroll_x) {
                    sws->scroll_pos.x = clamp_range(sws->scroll_pos.x - scroll.x * 48.0f, 0.0f, sws->scroll_max.x);
                }
                if (sws->is_scroll_y) {
                    sws->scroll_pos.y = clamp_range(sws->scroll_pos.y - scroll.y * 48.0f, 0.0f, sws->scroll_max.y);
                }
            } else {
                if (sws->is_scroll_x) {
                    sws->scroll_vel.x += (-scroll.x) * 560.0f;
                }
                if (sws->is_scroll_y) {
                    sws->scroll_vel.y += (-scroll.y) * 560.0f;
                }
            }
            request_redraw(ctx);
        }
    }

    // Integrate frame-rate-independent inertial scroll damping
    if (ctx->sim_dt > 0.0f && ctx->states.entries) {
        constexpr float kScrollFriction = 10.5f;
        const float decay = std::exp(-kScrollFriction * ctx->sim_dt);
        const float step_factor = (1.0f - decay) / kScrollFriction;
        for (uint32_t i = 0u; i < ctx->states.capacity; ++i) {
            WidgetState& ws = ctx->states.entries[i];
            if (ws.key == 0u || (!ws.is_scroll_x && !ws.is_scroll_y)) {
                continue;
            }
            bool moving = false;
            if (ws.is_scroll_x) {
                moving |= step_scroll_axis(&ws.scroll_pos.x, &ws.scroll_vel.x, ws.scroll_max.x, decay, step_factor);
            }
            if (ws.is_scroll_y) {
                moving |= step_scroll_axis(&ws.scroll_pos.y, &ws.scroll_vel.y, ws.scroll_max.y, decay, step_factor);
            }
            if (moving) {
                ++ctx->active_anim_count;
            }
        }
    }

    Clay_BeginLayout();
}

void begin_box(const BoxDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame) {
        return;
    }

    WidgetState* box_ws = nullptr;
    if (desc.id != 0u) {
        box_ws = state_map_get_or_insert(&ctx->states, desc.id, ctx->frame_index);
        if (box_ws) {
            box_ws->is_user_element = true;
            box_ws->is_focusable = desc.focusable;
            box_ws->is_scroll_x = desc.scroll_x;
            box_ws->is_scroll_y = desc.scroll_y;
        }
        if (desc.focusable && ctx->focus_count < kMaxFocusablePerFrame) {
            ctx->focus_order[ctx->focus_count++] = desc.id;
        }
        Clay__OpenElementWithId(make_clay_id(desc.id));
    } else {
        Clay__OpenElement();
    }

    const bool is_scroll = desc.scroll_x || desc.scroll_y;
    const bool any_clip = desc.clip_children || is_scroll;
    const bool has_border = (desc.border_width > 0.0f && desc.border_color.a > 0u);
    const bool has_scrollbar = is_scroll && desc.show_scrollbar;
    const bool has_visual =
        desc.background.a > 0u ||
        desc.background_end.a > 0u ||
        desc.radial_gradient ||
        (desc.shadow_color.a > 0u && desc.shadow_blur > 0.0f) ||
        has_border;

    BoxExtra* extra = nullptr;
    if (has_visual || any_clip) {
        extra = ctx->frame_arena.alloc<BoxExtra>();
        if (extra) {
            extra->desc = desc;
        }
    }

    Clay_ElementDeclaration decl = {};
    decl.layout.sizing.width = to_clay_sizing(desc.width);
    decl.layout.sizing.height = to_clay_sizing(desc.height);
    decl.layout.padding = Clay_Padding{
        .left = to_u16_round(desc.padding.left),
        .right = to_u16_round(desc.padding.right),
        .top = to_u16_round(desc.padding.top),
        .bottom = to_u16_round(desc.padding.bottom),
    };
    decl.layout.childGap = to_u16_round(desc.gap);
    decl.layout.childAlignment = Clay_ChildAlignment{
        .x = to_clay_align_x(desc.align_x),
        .y = to_clay_align_y(desc.align_y),
    };
    decl.layout.layoutDirection = to_clay_axis(desc.axis);
    decl.cornerRadius = to_clay_corners(desc.radius);
    decl.userData = extra;

    if (has_visual && extra) {
        // Emit via CUSTOM command so shadows + gradients + borders render in unified SDF geometry.
        decl.custom.customData = extra;
    }
    if (desc.attach_to != AttachTo::None) {
        decl.floating.attachTo = to_clay_attach_to(desc.attach_to);
        decl.floating.parentId = desc.attach_element_id;
        decl.floating.attachPoints = Clay_FloatingAttachPoints{
            .element = to_clay_attach_point(desc.anchor_element),
            .parent = to_clay_attach_point(desc.anchor_parent),
        };
        decl.floating.offset = Clay_Vector2{ desc.floating_offset.x, desc.floating_offset.y };
        decl.floating.zIndex = desc.z_index;
        decl.floating.pointerCaptureMode = desc.pointer_passthrough
            ? CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH
            : CLAY_POINTER_CAPTURE_MODE_CAPTURE;
        decl.floating.clipTo = desc.clip_to_parent
            ? CLAY_CLIP_TO_ATTACHED_PARENT
            : CLAY_CLIP_TO_NONE;
    }
    if (any_clip) {
        decl.clip.horizontal = desc.clip_children || desc.scroll_x;
        decl.clip.vertical = desc.clip_children || desc.scroll_y;
        if (is_scroll && box_ws) {
            decl.clip.childOffset = Clay_Vector2{ -box_ws->scroll_pos.x, -box_ws->scroll_pos.y };
        }
        if (has_border || has_scrollbar) {
            // Ask Clay to emit CLAY_RENDER_COMMAND_TYPE_BORDER after children for scrollbar + crisp outer rim.
            decl.border.color = Clay_Color{255.0f, 255.0f, 255.0f, 1.0f};
            decl.border.width = Clay_BorderWidth{1u, 1u, 1u, 1u, 0u};
        }
    }

    Clay__ConfigureOpenElement(decl);
}

void end_box() noexcept {
    if (!current_context() || !current_context()->in_frame) {
        return;
    }
    Clay__CloseElement();
}

void box(const BoxDesc& desc) noexcept {
    begin_box(desc);
    end_box();
}

void text(Str content, const TextDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || !content.chars || content.length == 0u) {
        return;
    }

    Clay_String clay_str = {
        .isStaticallyAllocated = content.is_static,
        .length = static_cast<int32_t>(content.length),
        .chars = content.chars,
    };
    if (!content.is_static) {
        // Copy non-static transient strings into the frame arena so Clay holds valid pointers until end_frame().
        const Str copied = ctx->frame_arena.push_str(content.chars, content.length);
        if (!copied.chars) {
            return;
        }
        clay_str.chars = copied.chars;
        clay_str.length = static_cast<int32_t>(copied.length);
    }

    Clay_TextElementConfig cfg = {};
    cfg.textColor = to_clay_color(desc.color);
    cfg.fontId = desc.font_id;
    cfg.fontSize = to_u16_round((desc.font_size_px > 0.0f) ? desc.font_size_px : 16.0f);
    cfg.letterSpacing = to_u16_round(desc.letter_spacing);
    cfg.lineHeight = cfg.fontSize;
    cfg.wrapMode = desc.wrap ? CLAY_TEXT_WRAP_WORDS : CLAY_TEXT_WRAP_NONE;

    Clay__OpenTextElement(clay_str, cfg);
}

void image(const ImageDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame) {
        return;
    }

    ImageExtra* extra = ctx->frame_arena.alloc<ImageExtra>();
    if (!extra) {
        return;
    }
    extra->desc = desc;

    if (desc.id != 0u) {
        Clay__OpenElementWithId(make_clay_id(desc.id));
    } else {
        Clay__OpenElement();
    }

    Clay_ElementDeclaration decl = {};
    decl.layout.sizing.width = to_clay_sizing(desc.width);
    decl.layout.sizing.height = to_clay_sizing(desc.height);
    decl.cornerRadius = to_clay_corners(desc.radius);
    decl.image.imageData = extra;
    decl.userData = extra;

    Clay__ConfigureOpenElement(decl);
    Clay__CloseElement();
}

bool set_clipboard_text(const char* utf8_text) noexcept {
    const char* src = utf8_text ? utf8_text : "";
    Context* ctx = current_context();
    bool fallback_ok = true;
    if (ctx) {
        const size_t len = std::strlen(src);
        const uint32_t need = static_cast<uint32_t>(len + 1u);
        if (need > ctx->clipboard_fallback_cap) {
            uint32_t new_cap = (ctx->clipboard_fallback_cap > 0u) ? ctx->clipboard_fallback_cap : 128u;
            while (new_cap < need) {
                new_cap *= 2u;
            }
            char* new_buf = static_cast<char*>(std::realloc(ctx->clipboard_fallback, new_cap));
            if (new_buf) {
                ctx->clipboard_fallback = new_buf;
                ctx->clipboard_fallback_cap = new_cap;
            } else {
                fallback_ok = false;
            }
        }
        if (ctx->clipboard_fallback && ctx->clipboard_fallback_cap >= need) {
            if (len > 0u) {
                std::memcpy(ctx->clipboard_fallback, src, len);
            }
            ctx->clipboard_fallback[len] = '\0';
        }
    }
    if (!ctx || !ctx->headless) {
        return SDL_SetClipboardText(src) && fallback_ok;
    }
    return fallback_ok;
}

Str get_clipboard_text(Arena* arena) noexcept {
    Context* ctx = current_context();
    Arena* dst_arena = arena ? arena : (ctx ? &ctx->frame_arena : nullptr);
    if (!dst_arena) {
        return Str{};
    }
    if (ctx && ctx->headless) {
        if (!ctx->clipboard_fallback || ctx->clipboard_fallback[0] == '\0') {
            return Str{};
        }
        return dst_arena->push_str(ctx->clipboard_fallback, std::strlen(ctx->clipboard_fallback));
    }
    char* sdl_clip = SDL_GetClipboardText();
    if (sdl_clip) {
        const size_t len = std::strlen(sdl_clip);
        const Str out = (len > 0u) ? dst_arena->push_str(sdl_clip, len) : Str{};
        SDL_free(sdl_clip);
        return out;
    }
    if (ctx && ctx->clipboard_fallback && ctx->clipboard_fallback[0] != '\0') {
        return dst_arena->push_str(ctx->clipboard_fallback, std::strlen(ctx->clipboard_fallback));
    }
    return Str{};
}

void set_text_selection(uint32_t element_id, uint32_t anchor_byte, uint32_t cursor_byte) noexcept {
    Context* ctx = current_context();
    if (!ctx || element_id == 0u) {
        return;
    }
    WidgetState* ws = state_map_get_or_insert(&ctx->states, element_id, ctx->frame_index);
    if (!ws) {
        return;
    }
    ws->edit_initialized = true;
    ws->edit_sel_anchor = anchor_byte;
    ws->edit_cursor = cursor_byte;
    ws->edit_preferred_x = -1.0f;
    request_redraw(ctx);
}

Rect ime_cursor_rect(const Context* ctx_arg) noexcept {
    const Context* ctx = ctx_arg ? ctx_arg : current_context();
    return ctx ? ctx->ime_cursor_logical : Rect{};
}

TextInputResult text_input(const TextInputDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame) {
        return TextInputResult{};
    }

    WidgetState* ws = nullptr;
    if (desc.id != 0u) {
        ws = state_map_get_or_insert(&ctx->states, desc.id, ctx->frame_index);
        if (ws) {
            ws->is_user_element = true;
            ws->is_focusable = true;
            ws->is_scroll_x = false;
            ws->is_scroll_y = false;
        }
        if (ctx->focus_count < kMaxFocusablePerFrame) {
            ctx->focus_order[ctx->focus_count++] = desc.id;
        }
    }

    char* buf = desc.buffer;
    const uint32_t cap = desc.capacity;
    uint32_t len = 0u;
    const bool writable = (buf != nullptr && cap > 1u && !desc.read_only);
    if (buf && cap > 0u) {
        while (len + 1u < cap && buf[len] != '\0') {
            ++len;
        }
        buf[len] = '\0';
    }

    uint32_t cursor = len;
    uint32_t anchor = len;
    if (ws) {
        if (!ws->edit_initialized) {
            ws->edit_initialized = true;
            ws->edit_cursor = len;
            ws->edit_sel_anchor = len;
            ws->edit_preferred_x = -1.0f;
        }
        cursor = utf8_clamp_boundary(buf, len, ws->edit_cursor);
        anchor = utf8_clamp_boundary(buf, len, ws->edit_sel_anchor);
    }

    const bool is_focused = (desc.id != 0u && ctx->focus_id == desc.id);
    const bool is_hovered = (desc.id != 0u && Clay_PointerOver(make_clay_id(desc.id)));
    if (is_hovered || (desc.id != 0u && ctx->active_id == desc.id)) {
        set_cursor(Cursor::IBeam);
    }

    if (is_focused && !desc.read_only) {
        ctx->ime_focused_id = desc.id;
        ctx->ime_focused_password = desc.password;
        if (desc.password && ctx->ime_comp_len > 0u) {
            clear_ime_composition(ctx);
        }
    }

    // Pointer click / double-word / triple-line / drag selection
    if (ws && ws->last_box_logical.w > 0.0f && desc.id != 0u) {
        const TextInnerGeom prev_geom = compute_text_inner_geom(ws->last_box_logical, desc);
        const float local_x = ctx->pointer_pos.x - prev_geom.origin_x + ws->edit_scroll_x;
        const float local_y = ctx->pointer_pos.y - prev_geom.origin_y + ws->edit_scroll_y;
        const Str cur_text = { buf ? buf : "", len };

        if (ws->pressed_this_frame) {
            ctx->caret_blink_on = true;
            ctx->caret_blink_cycles = 0u;
            const uint32_t hit_byte = visual_point_to_byte(ctx, desc, cur_text, local_x, local_y);
            if (ctx->last_mouse_clicks >= 3u) {
                if (desc.multiline) {
                    line_bounds_at(buf, len, hit_byte, &anchor, &cursor);
                } else {
                    anchor = 0u;
                    cursor = len;
                }
            } else if (ctx->last_mouse_clicks == 2u) {
                utf8_word_bounds_at(buf, len, hit_byte, &anchor, &cursor);
            } else {
                const bool shift_held = (!ctx->headless) && ((SDL_GetModState() & SDL_KMOD_SHIFT) != 0u);
                if (!shift_held) {
                    anchor = hit_byte;
                }
                cursor = hit_byte;
            }
            ws->edit_preferred_x = -1.0f;
        } else if (ctx->active_id == desc.id && ws->pressed_down) {
            cursor = visual_point_to_byte(ctx, desc, cur_text, local_x, local_y);
            ws->edit_preferred_x = -1.0f;
            request_redraw(ctx);
        }
    }

    bool changed = false;
    bool submitted = false;
    if (is_focused) {
        for (uint32_t oi = 0u; oi < ctx->frame_text_op_count; ++oi) {
            const PendingTextOp& op = ctx->frame_text_ops[oi];
            const uint32_t sel_min = (cursor < anchor) ? cursor : anchor;
            const uint32_t sel_max = (cursor < anchor) ? anchor : cursor;
            const bool has_sel = (sel_max > sel_min);

            if (op.kind == TextEditOpKind::InsertText) {
                if (writable && op.text_len > 0u) {
                    const Str ins = { ctx->frame_text_utf8 + op.text_offset, op.text_len };
                    const uint32_t prev_len = len;
                    const uint32_t new_pos = insert_utf8_filtered(buf, cap, &len, anchor, cursor, ins, desc.multiline);
                    if (has_sel || len != prev_len) {
                        changed = true;
                    }
                    cursor = new_pos;
                    anchor = new_pos;
                    if (ws) {
                        ws->edit_preferred_x = -1.0f;
                    }
                }
                continue;
            }

            if (op.kind != TextEditOpKind::KeyPress) {
                continue;
            }

            const bool shift = (op.mod & SDL_KMOD_SHIFT) != 0u;
            const bool primary = (op.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0u;
            const bool alt = (op.mod & SDL_KMOD_ALT) != 0u;
            const bool word_mod = ((op.mod & SDL_KMOD_CTRL) != 0u) || ((op.mod & SDL_KMOD_ALT) != 0u);

            if (primary && !alt) {
                if (op.key == SDLK_A) {
                    anchor = 0u;
                    cursor = len;
                    if (ws) {
                        ws->edit_preferred_x = -1.0f;
                    }
                    continue;
                }
                if (op.key == SDLK_C || op.key == SDLK_X) {
                    if (!desc.password && has_sel && buf) {
                        const uint32_t s_len = sel_max - sel_min;
                        const Str slice = ctx->frame_arena.push_str(buf + sel_min, s_len);
                        if (slice.chars) {
                            (void)set_clipboard_text(slice.chars);
                        }
                    }
                    if (op.key == SDLK_X && writable && has_sel) {
                        cursor = erase_text_range(buf, &len, anchor, cursor);
                        anchor = cursor;
                        changed = true;
                        if (ws) {
                            ws->edit_preferred_x = -1.0f;
                        }
                    }
                    continue;
                }
                if (op.key == SDLK_V) {
                    if (writable) {
                        const Str clip = get_clipboard_text(&ctx->frame_arena);
                        if (clip.chars && clip.length > 0u) {
                            const uint32_t prev_len = len;
                            const uint32_t new_pos = insert_utf8_filtered(
                                buf, cap, &len, anchor, cursor, clip, desc.multiline);
                            if (has_sel || len != prev_len) {
                                changed = true;
                            }
                            cursor = new_pos;
                            anchor = new_pos;
                            if (ws) {
                                ws->edit_preferred_x = -1.0f;
                            }
                        }
                    }
                    continue;
                }
            }

            switch (op.key) {
                case SDLK_LEFT:
                    if (has_sel && !shift && !word_mod) {
                        cursor = sel_min;
                        anchor = sel_min;
                    } else {
                        cursor = word_mod
                            ? utf8_word_left(buf, len, cursor)
                            : utf8_prev_boundary(buf, len, cursor);
                        if (!shift) {
                            anchor = cursor;
                        }
                    }
                    if (ws) {
                        ws->edit_preferred_x = -1.0f;
                    }
                    break;
                case SDLK_RIGHT:
                    if (has_sel && !shift && !word_mod) {
                        cursor = sel_max;
                        anchor = sel_max;
                    } else {
                        cursor = word_mod
                            ? utf8_word_right(buf, len, cursor)
                            : utf8_next_boundary(buf, len, cursor);
                        if (!shift) {
                            anchor = cursor;
                        }
                    }
                    if (ws) {
                        ws->edit_preferred_x = -1.0f;
                    }
                    break;
                case SDLK_HOME: {
                    uint32_t ls = 0u;
                    uint32_t le = 0u;
                    if (desc.multiline && !primary) {
                        line_bounds_at(buf, len, cursor, &ls, &le);
                        cursor = ls;
                    } else {
                        cursor = 0u;
                    }
                    if (!shift) {
                        anchor = cursor;
                    }
                    if (ws) {
                        ws->edit_preferred_x = -1.0f;
                    }
                    break;
                }
                case SDLK_END: {
                    uint32_t ls = 0u;
                    uint32_t le = 0u;
                    if (desc.multiline && !primary) {
                        line_bounds_at(buf, len, cursor, &ls, &le);
                        cursor = le;
                    } else {
                        cursor = len;
                    }
                    if (!shift) {
                        anchor = cursor;
                    }
                    if (ws) {
                        ws->edit_preferred_x = -1.0f;
                    }
                    break;
                }
                case SDLK_UP:
                case SDLK_DOWN:
                    if (desc.multiline && ws) {
                        ws->edit_cursor = cursor;
                        ws->edit_sel_anchor = anchor;
                        move_caret_vertical(
                            ctx, desc, Str{ buf ? buf : "", len }, ws, (op.key == SDLK_UP) ? -1 : 1, shift);
                        cursor = ws->edit_cursor;
                        anchor = ws->edit_sel_anchor;
                    }
                    break;
                case SDLK_BACKSPACE:
                    if (writable) {
                        if (has_sel) {
                            cursor = erase_text_range(buf, &len, anchor, cursor);
                            anchor = cursor;
                            changed = true;
                        } else if (cursor > 0u) {
                            const uint32_t del_from = word_mod
                                ? utf8_word_left(buf, len, cursor)
                                : utf8_prev_boundary(buf, len, cursor);
                            if (del_from < cursor) {
                                cursor = erase_text_range(buf, &len, del_from, cursor);
                                anchor = cursor;
                                changed = true;
                            }
                        }
                        if (ws) {
                            ws->edit_preferred_x = -1.0f;
                        }
                    }
                    break;
                case SDLK_DELETE:
                    if (writable) {
                        if (has_sel) {
                            cursor = erase_text_range(buf, &len, anchor, cursor);
                            anchor = cursor;
                            changed = true;
                        } else if (cursor < len) {
                            const uint32_t del_to = word_mod
                                ? utf8_word_right(buf, len, cursor)
                                : utf8_next_boundary(buf, len, cursor);
                            if (del_to > cursor) {
                                cursor = erase_text_range(buf, &len, cursor, del_to);
                                anchor = cursor;
                                changed = true;
                            }
                        }
                        if (ws) {
                            ws->edit_preferred_x = -1.0f;
                        }
                    }
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    if (desc.multiline && !primary) {
                        if (writable) {
                            const uint32_t prev_len = len;
                            const uint32_t new_pos = insert_utf8_filtered(
                                buf, cap, &len, anchor, cursor, str_lit("\n"), true);
                            if (has_sel || len != prev_len) {
                                changed = true;
                            }
                            cursor = new_pos;
                            anchor = new_pos;
                            if (ws) {
                                ws->edit_preferred_x = -1.0f;
                            }
                        }
                    } else {
                        submitted = true;
                    }
                    break;
                case SDLK_ESCAPE:
                    if (has_sel) {
                        anchor = cursor;
                    }
                    break;
                default:
                    break;
            }
        }
    }

    cursor = utf8_clamp_boundary(buf, len, cursor);
    anchor = utf8_clamp_boundary(buf, len, anchor);
    if (ws) {
        ws->edit_cursor = cursor;
        ws->edit_sel_anchor = anchor;
    }

    const Str committed_slice = (buf && len > 0u) ? ctx->frame_arena.push_str(buf, len) : Str{};
    const bool composing = is_focused && !desc.read_only && !desc.password && ctx->ime_comp_len > 0u;
    if (is_focused && ws && ws->last_box_logical.w > 0.0f && !desc.read_only && !desc.password) {
        const TextInnerGeom pre_geom = compute_text_inner_geom(ws->last_box_logical, desc);
        update_text_scroll_and_ime(
            ctx, ws, desc, pre_geom, committed_slice, cursor, true, composing, nullptr, nullptr);
    }

    TextInputExtra* ti_extra = ctx->frame_arena.alloc<TextInputExtra>();
    BoxExtra* extra = ctx->frame_arena.alloc<BoxExtra>();
    if (ti_extra && extra) {
        ti_extra->desc = desc;
        if (desc.placeholder.chars && desc.placeholder.length > 0u && !desc.placeholder.is_static) {
            ti_extra->desc.placeholder = ctx->frame_arena.push_str(desc.placeholder.chars, desc.placeholder.length);
        }
        ti_extra->committed_text = committed_slice;
        ti_extra->cursor_byte = cursor;
        ti_extra->sel_anchor_byte = anchor;
        ti_extra->is_focused = is_focused;
        extra->text_input = ti_extra;
    }

    if (desc.id != 0u) {
        Clay__OpenElementWithId(make_clay_id(desc.id));
    } else {
        Clay__OpenElement();
    }

    Clay_ElementDeclaration decl = {};
    decl.layout.sizing.width = to_clay_sizing(desc.width);
    decl.layout.sizing.height = to_clay_sizing(desc.height);
    decl.layout.padding = Clay_Padding{
        .left = to_u16_round(desc.padding.left),
        .right = to_u16_round(desc.padding.right),
        .top = to_u16_round(desc.padding.top),
        .bottom = to_u16_round(desc.padding.bottom),
    };
    decl.cornerRadius = to_clay_corners(desc.radius);
    if (extra && ti_extra) {
        decl.custom.customData = extra;
        decl.userData = extra;
    }

    Clay__ConfigureOpenElement(decl);
    Clay__CloseElement();

    return TextInputResult{
        .changed = changed,
        .submitted = submitted,
        .focused = is_focused,
        .composing = composing,
        .byte_length = len,
        .cursor_byte = cursor,
        .selection_anchor_byte = anchor,
        .ime_cursor_rect = (is_focused && !desc.read_only && !desc.password) ? ctx->ime_cursor_logical : Rect{},
    };
}

bool hovered(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (element_id == 0u || !ctx || !ctx->clay_ctx) {
        return false;
    }
    if (ctx->active_id != 0u && ctx->active_id != element_id) {
        return false;
    }
    Clay_SetCurrentContext(ctx->clay_ctx);
    return Clay_PointerOver(make_clay_id(element_id));
}

bool pressed(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || element_id == 0u) {
        return false;
    }
    const WidgetState* ws = state_map_find(&ctx->states, element_id);
    return ws != nullptr && ws->pressed_down;
}

bool clicked(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || element_id == 0u) {
        return false;
    }
    const WidgetState* ws = state_map_find(&ctx->states, element_id);
    return ws != nullptr && ws->clicked_this_frame;
}

bool active(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    return ctx != nullptr && element_id != 0u && ctx->active_id == element_id;
}

uint32_t active_id() noexcept {
    Context* ctx = current_context();
    return ctx ? ctx->active_id : 0u;
}

void set_active(uint32_t element_id) noexcept {
    if (Context* ctx = current_context()) {
        ctx->active_id = element_id;
        request_redraw(ctx);
    }
}

bool focused(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    return ctx != nullptr && element_id != 0u && ctx->focus_id == element_id;
}

uint32_t focused_id() noexcept {
    Context* ctx = current_context();
    return ctx ? ctx->focus_id : 0u;
}

void request_focus(uint32_t element_id) noexcept {
    if (Context* ctx = current_context()) {
        if (element_id != ctx->focus_id) {
            clear_ime_composition(ctx);
        }
        ctx->focus_id = element_id;
        ctx->caret_blink_on = true;
        ctx->caret_blink_cycles = 0u;
        request_redraw(ctx);
    }
}

void set_cursor(Cursor cursor) noexcept {
    if (Context* ctx = current_context()) {
        ctx->desired_cursor = cursor;
    }
}

Rect element_bounds(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->clay_ctx || element_id == 0u) {
        return {};
    }
    Clay_SetCurrentContext(ctx->clay_ctx);
    const Clay_ElementData ed = Clay_GetElementData(make_clay_id(element_id));
    if (!ed.found) {
        return {};
    }
    return Rect{
        ed.boundingBox.x,
        ed.boundingBox.y,
        ed.boundingBox.width,
        ed.boundingBox.height,
    };
}

float spring(uint32_t anim_id, float target, SpringSpec spec) noexcept {
    Context* ctx = current_context();
    if (!ctx || anim_id == 0u) {
        return target;
    }
    WidgetState* ws = state_map_get_or_insert(&ctx->states, anim_id, ctx->frame_index);
    if (!ws) {
        return target;
    }
    if (!ws->spring_initialized) {
        ws->spring_pos = target;
        ws->spring_vel = 0.0f;
        ws->spring_initialized = true;
        return target;
    }

    const float x0 = ws->spring_pos - target;
    const float v0 = ws->spring_vel;
    if (std::fabs(x0) <= 0.0005f && std::fabs(v0) <= 0.005f) {
        ws->spring_pos = target;
        ws->spring_vel = 0.0f;
        return target;
    }

    const float dt = ctx->sim_dt;
    if (dt <= 0.0f) {
        ++ctx->active_anim_count;
        return ws->spring_pos;
    }

    const float response = (spec.response < 0.01f) ? 0.01f : spec.response;
    const float zeta = (spec.damping_ratio < 0.05f) ? 0.05f : spec.damping_ratio;
    const float omega = 6.28318530718f / response;

    float x1 = 0.0f;
    float v1 = 0.0f;
    if (std::fabs(zeta - 1.0f) < 1e-3f) {
        const float e = std::exp(-omega * dt);
        const float c1 = x0;
        const float c2 = v0 + omega * x0;
        const float linear = c1 + c2 * dt;
        x1 = linear * e;
        v1 = (c2 - omega * linear) * e;
    } else if (zeta < 1.0f) {
        const float omega_d = omega * std::sqrt(1.0f - zeta * zeta);
        const float e = std::exp(-zeta * omega * dt);
        const float c1 = x0;
        const float c2 = (v0 + zeta * omega * x0) / omega_d;
        const float s = std::sin(omega_d * dt);
        const float c = std::cos(omega_d * dt);
        x1 = e * (c1 * c + c2 * s);
        v1 = e * ((c2 * omega_d - c1 * zeta * omega) * c - (c1 * omega_d + c2 * zeta * omega) * s);
    } else {
        const float sq = std::sqrt(zeta * zeta - 1.0f);
        const float r1 = -omega * (zeta - sq);
        const float r2 = -omega * (zeta + sq);
        const float e1 = std::exp(r1 * dt);
        const float e2 = std::exp(r2 * dt);
        const float c2 = (v0 - r1 * x0) / (r2 - r1);
        const float c1 = x0 - c2;
        x1 = c1 * e1 + c2 * e2;
        v1 = c1 * r1 * e1 + c2 * r2 * e2;
    }

    if (std::fabs(x1) <= 0.0005f && std::fabs(v1) <= 0.005f) {
        ws->spring_pos = target;
        ws->spring_vel = 0.0f;
        return target;
    }

    ws->spring_pos = target + x1;
    ws->spring_vel = v1;
    ++ctx->active_anim_count;
    return ws->spring_pos;
}

Vec2 spring_vec2(uint32_t anim_id, Vec2 target, SpringSpec spec) noexcept {
    if (anim_id == 0u) {
        return target;
    }
    return Vec2{
        .x = spring(id_index(anim_id, 0x58u), target.x, spec),
        .y = spring(id_index(anim_id, 0x59u), target.y, spec),
    };
}

Vec2 scroll_offset(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || element_id == 0u) {
        return {};
    }
    const WidgetState* ws = state_map_find(&ctx->states, element_id);
    return ws ? ws->scroll_pos : Vec2{};
}

void set_scroll_offset(uint32_t element_id, Vec2 offset, bool stop_momentum) noexcept {
    Context* ctx = current_context();
    if (!ctx || element_id == 0u) {
        return;
    }
    if (WidgetState* ws = state_map_get_or_insert(&ctx->states, element_id, ctx->frame_index)) {
        ws->is_user_element = true;
        ws->scroll_pos = Vec2{
            (ws->scroll_max.x > 0.0f) ? clamp_range(offset.x, 0.0f, ws->scroll_max.x) : ((offset.x > 0.0f) ? offset.x : 0.0f),
            (ws->scroll_max.y > 0.0f) ? clamp_range(offset.y, 0.0f, ws->scroll_max.y) : ((offset.y > 0.0f) ? offset.y : 0.0f),
        };
        if (stop_momentum) {
            ws->scroll_vel = {};
        }
        request_redraw(ctx);
    }
}

void end_frame(Context* ctx_arg) noexcept {
    Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (!ctx || !ctx->in_frame || !ctx->clay_ctx) {
        return;
    }
    Clay_SetCurrentContext(ctx->clay_ctx);

    const float dpi = ctx->dpi_scale;
    const Rect root_clip = {
        0.0f,
        0.0f,
        static_cast<float>(ctx->viewport_w),
        static_cast<float>(ctx->viewport_h),
    };

    ClipStack clip_stack = {};
    clip_stack.entries[0] = ClipEntry{ .rect = root_clip, .radius = 0.0f };
    clip_stack.depth = 1u;

    Clay_RenderCommandArray cmds = Clay_EndLayout(0.0f);
    for (int32_t i = 0; i < cmds.length; ++i) {
        const Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(&cmds, i);
        if (!cmd) {
            continue;
        }
        const ClipEntry& cur_clip = clip_stack.entries[clip_stack.depth - 1u];
        const Rect box_px = {
            .x = cmd->boundingBox.x * dpi,
            .y = cmd->boundingBox.y * dpi,
            .w = cmd->boundingBox.width * dpi,
            .h = cmd->boundingBox.height * dpi,
        };

        switch (cmd->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
                const BoxExtra* extra = static_cast<const BoxExtra*>(cmd->renderData.custom.customData);
                if (extra && extra->text_input) {
                    emit_text_input_command(ctx, extra->text_input, cmd->boundingBox, box_px, cur_clip, dpi);
                } else {
                    emit_box_background_and_shadow(ctx, extra, box_px, cur_clip, dpi);
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                const BoxExtra* extra = static_cast<const BoxExtra*>(cmd->userData);
                if (extra && extra->desc.id != 0u && (extra->desc.scroll_x || extra->desc.scroll_y)) {
                    if (WidgetState* ws = state_map_find(&ctx->states, extra->desc.id)) {
                        const Clay_ScrollContainerData sc = Clay_GetScrollContainerData(make_clay_id(extra->desc.id));
                        if (sc.found) {
                            ws->viewport_dim = Vec2{ sc.scrollContainerDimensions.width, sc.scrollContainerDimensions.height };
                            ws->content_dim = Vec2{ sc.contentDimensions.width, sc.contentDimensions.height };
                            ws->scroll_max = Vec2{
                                (sc.contentDimensions.width > sc.scrollContainerDimensions.width)
                                    ? (sc.contentDimensions.width - sc.scrollContainerDimensions.width) : 0.0f,
                                (sc.contentDimensions.height > sc.scrollContainerDimensions.height)
                                    ? (sc.contentDimensions.height - sc.scrollContainerDimensions.height) : 0.0f,
                            };
                            ws->scroll_pos.x = clamp_range(ws->scroll_pos.x, 0.0f, ws->scroll_max.x);
                            ws->scroll_pos.y = clamp_range(ws->scroll_pos.y, 0.0f, ws->scroll_max.y);
                        } else {
                            ws->viewport_dim = Vec2{ cmd->boundingBox.width, cmd->boundingBox.height };
                            ws->content_dim = ws->viewport_dim;
                            ws->scroll_max = {};
                        }
                        ws->last_box_logical = Rect{
                            cmd->boundingBox.x,
                            cmd->boundingBox.y,
                            cmd->boundingBox.width,
                            cmd->boundingBox.height,
                        };
                    }
                }
                if (clip_stack.depth < kMaxClipDepth) {
                    const Rect clipped_rect = rect_intersect(cur_clip.rect, box_px);
                    float next_radius = 0.0f;
                    if (extra) {
                        const Corners& c = extra->desc.radius;
                        next_radius = min4f(c.tl, c.tr, c.br, c.bl) * dpi;
                    }
                    if (next_radius <= 0.0f &&
                        clipped_rect.x == cur_clip.rect.x &&
                        clipped_rect.y == cur_clip.rect.y &&
                        clipped_rect.w == cur_clip.rect.w &&
                        clipped_rect.h == cur_clip.rect.h)
                    {
                        next_radius = cur_clip.radius;
                    }
                    clip_stack.entries[clip_stack.depth++] = ClipEntry{
                        .rect = clipped_rect,
                        .radius = next_radius,
                    };
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
                if (clip_stack.depth > 1u) {
                    --clip_stack.depth;
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                const BoxExtra* extra = static_cast<const BoxExtra*>(cmd->userData);
                if (!extra) {
                    break;
                }
                const BoxDesc& d = extra->desc;
                const bool is_scroll = d.scroll_x || d.scroll_y;
                const bool any_clip = d.clip_children || is_scroll;

                if (d.scroll_y && d.show_scrollbar && d.id != 0u) {
                    if (const WidgetState* ws = state_map_find(&ctx->states, d.id)) {
                        const ScrollbarGeom sb = compute_vscrollbar_geom(ws);
                        if (sb.visible) {
                            const uint32_t thumb_id = id_index(d.id, kScrollbarYTag);
                            const bool sb_active = (ctx->active_id == thumb_id);
                            const bool sb_hover = (ctx->active_id == 0u) && sb.hit_track.contains(ctx->pointer_pos) &&
                                Clay_PointerOver(make_clay_id(d.id));
                            const Rect track_px = { sb.track.x * dpi, sb.track.y * dpi, sb.track.w * dpi, sb.track.h * dpi };
                            const Rect thumb_px = { sb.thumb.x * dpi, sb.thumb.y * dpi, sb.thumb.w * dpi, sb.thumb.h * dpi };
                            const Color track_col = Color::rgba8(255, 255, 255, 18);
                            const Color thumb_col = sb_active
                                ? Color::rgba8(96, 165, 250, 230)
                                : (sb_hover ? Color::rgba8(148, 163, 184, 195) : Color::rgba8(148, 163, 184, 125));
                            push_prim(
                                ctx,
                                Prim::make_rect(
                                    track_px,
                                    cur_clip.rect,
                                    track_col,
                                    Corners::all(0.5f * track_px.w),
                                    {},
                                    0.0f,
                                    1.0f,
                                    cur_clip.radius));
                            push_prim(
                                ctx,
                                Prim::make_rect(
                                    thumb_px,
                                    cur_clip.rect,
                                    thumb_col,
                                    Corners::all(0.5f * thumb_px.w),
                                    {},
                                    0.0f,
                                    1.0f,
                                    cur_clip.radius));
                        }
                    }
                }

                if (any_clip && d.border_width > 0.0f && d.border_color.a > 0u) {
                    // Clip against parent container (below our own SCISSOR_START) so border AA edge isn't halved.
                    const ClipEntry& parent_clip = (clip_stack.depth > 1u)
                        ? clip_stack.entries[clip_stack.depth - 2u]
                        : clip_stack.entries[0];
                    push_prim(
                        ctx,
                        Prim::make_rect(
                            box_px,
                            parent_clip.rect,
                            Color::transparent(),
                            scale_corners(d.radius, dpi),
                            d.border_color,
                            d.border_width * dpi,
                            1.0f,
                            parent_clip.radius));
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                emit_text_command(ctx, cmd, cur_clip, dpi);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
                const ImageExtra* extra = static_cast<const ImageExtra*>(cmd->renderData.image.imageData);
                if (extra && extra->desc.texture != nullptr) {
                    const ImageDesc& idesc = extra->desc;
                    push_prim(
                        ctx,
                        Prim::make_texture(
                            box_px,
                            cur_clip.rect,
                            idesc.uv_min,
                            idesc.uv_max,
                            0u,
                            scale_corners(idesc.radius, dpi),
                            idesc.tint,
                            idesc.border_color,
                            idesc.border_width * dpi,
                            PrimFlag_None,
                            cur_clip.radius),
                        idesc.texture);
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                // Fallback only if a raw Clay rectangle is ever emitted.
                const Clay_RectangleRenderData& rd = cmd->renderData.rectangle;
                const Color fill = Color::rgba8(
                    static_cast<uint8_t>(rd.backgroundColor.r + 0.5f),
                    static_cast<uint8_t>(rd.backgroundColor.g + 0.5f),
                    static_cast<uint8_t>(rd.backgroundColor.b + 0.5f),
                    static_cast<uint8_t>(rd.backgroundColor.a + 0.5f));
                const Corners cr = {
                    rd.cornerRadius.topLeft * dpi,
                    rd.cornerRadius.topRight * dpi,
                    rd.cornerRadius.bottomRight * dpi,
                    rd.cornerRadius.bottomLeft * dpi,
                };
                push_prim(ctx, Prim::make_rect(box_px, cur_clip.rect, fill, cr, {}, 0.0f, 1.0f, cur_clip.radius));
                break;
            }
            default:
                break;
        }
    }

    ctx->frame_text_op_count = 0u;
    ctx->last_mouse_clicks = 1u;

    if (ctx->ime_focused_id == 0u && ctx->ime_comp_len > 0u) {
        clear_ime_composition(ctx);
    }

    if (!ctx->headless && ctx->window) {
        if (ctx->ime_focused_id != 0u) {
            if (!ctx->ime_input_started || ctx->ime_focused_password != ctx->applied_ime_password) {
                if (ctx->ime_input_started) {
                    SDL_ClearComposition(ctx->window);
                    SDL_StopTextInput(ctx->window);
                    ctx->ime_input_started = false;
                }
                bool started = false;
                if (ctx->ime_focused_password) {
                    const SDL_PropertiesID props = SDL_CreateProperties();
                    if (props != 0u) {
                        SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER, SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_HIDDEN);
                        started = SDL_StartTextInputWithProperties(ctx->window, props);
                        SDL_DestroyProperties(props);
                    }
                } else {
                    started = SDL_StartTextInput(ctx->window);
                }
                if (started) {
                    ctx->ime_input_started = true;
                    ctx->applied_ime_password = ctx->ime_focused_password;
                }
            } else if (ctx->prev_ime_focused_id != 0u && ctx->prev_ime_focused_id != ctx->ime_focused_id) {
                SDL_ClearComposition(ctx->window);
            }

            const float wscale = logical_to_window_scale(ctx);
            const SDL_Rect ime_rect = {
                static_cast<int>(std::floor(ctx->ime_cursor_logical.x * wscale + 0.5f)),
                static_cast<int>(std::floor(ctx->ime_cursor_logical.y * wscale + 0.5f)),
                static_cast<int>(std::floor(ctx->ime_cursor_logical.w * wscale + 0.5f)),
                static_cast<int>(std::floor(ctx->ime_cursor_logical.h * wscale + 0.5f)),
            };
            const int ime_cursor = static_cast<int>(ctx->ime_cursor_offset_win);
            if (ime_rect.w > 0 && ime_rect.h > 0 &&
                (ime_rect.x != ctx->applied_ime_rect.x ||
                 ime_rect.y != ctx->applied_ime_rect.y ||
                 ime_rect.w != ctx->applied_ime_rect.w ||
                 ime_rect.h != ctx->applied_ime_rect.h ||
                 ime_cursor != ctx->applied_ime_cursor))
            {
                SDL_SetTextInputArea(ctx->window, &ime_rect, ime_cursor);
                ctx->applied_ime_rect = ime_rect;
                ctx->applied_ime_cursor = ime_cursor;
            }
        } else if (ctx->ime_input_started) {
            SDL_ClearComposition(ctx->window);
            SDL_StopTextInput(ctx->window);
            ctx->ime_input_started = false;
            ctx->applied_ime_password = false;
            ctx->applied_ime_rect = SDL_Rect{-1, -1, 0, 0};
            ctx->applied_ime_cursor = -1;
        }
    }

    if (!ctx->headless && ctx->window && ctx->desired_cursor != ctx->applied_cursor) {
        const uint32_t cidx = static_cast<uint32_t>(ctx->desired_cursor);
        if (cidx < kCursorCount) {
            if (!ctx->system_cursors[cidx]) {
                ctx->system_cursors[cidx] = SDL_CreateSystemCursor(to_sdl_system_cursor(ctx->desired_cursor));
            }
            if (ctx->system_cursors[cidx]) {
                SDL_SetCursor(ctx->system_cursors[cidx]);
                ctx->applied_cursor = ctx->desired_cursor;
            }
        }
    }

    if (ctx->active_anim_count > 0u) {
        ctx->redraw_budget = 2u;
    }

    const uint64_t end_ticks = SDL_GetPerformanceCounter();
    const uint64_t freq = SDL_GetPerformanceFrequency();
    ctx->in_frame = false;
    ctx->stats.prim_count = ctx->prim_count;
    ctx->stats.draw_call_count = ctx->batch_count;
    ctx->stats.arena_used_bytes = ctx->frame_arena.used();
    ctx->stats.arena_peak_bytes = ctx->frame_arena.peak_offset;
    ctx->stats.layout_ms = (freq > 0u)
        ? (static_cast<double>(end_ticks - ctx->layout_start_ticks) * 1000.0 / static_cast<double>(freq))
        : 0.0;
    ctx->stats.active_animations = ctx->active_anim_count;
    ctx->stats.idle_sleeping = !needs_redraw(ctx);
}

} // namespace etch
