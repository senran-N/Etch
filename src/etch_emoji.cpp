// OpenType bitmap strikes; all offsets are validated before forming pointers.
namespace etch {
namespace {
struct BitmapSource {
    const uint8_t* png = nullptr;
    uint32_t len = 0, ppem = 0;
    float x = 0, top = 0;
    bool sbix = false;
};
bool sbix_source(const FontSlot& s, uint32_t strike, uint32_t glyph, BitmapSource* out) noexcept {
    const auto* d = s.sbix_data;
    const uint32_t off = ot_u32(d + 8 + strike * 4);
    if (uint64_t(off) + 4 + (uint64_t(s.num_glyphs) + 1) * 4 > s.sbix_len)
        return false;
    const auto* base = d + off;
    const uint32_t len = s.sbix_len - off;
    const uint32_t ppem = ot_u16(base);
    if (!ppem)
        return false;
    for (uint32_t depth = 0; depth <= 4; ++depth) {
        if (glyph >= s.num_glyphs)
            return false;
        const uint32_t a = ot_u32(base + 4 + glyph * 4), b = ot_u32(base + 8 + glyph * 4);
        if (a > b || b > len || b - a < 8)
            return false;
        const auto* p = base + a;
        const uint32_t tag = ot_u32(p + 4);
        if (tag == HB_TAG('d', 'u', 'p', 'e')) {
            if (b - a < 10)
                return false;
            glyph = ot_u16(p + 8);
            continue;
        }
        if (tag != HB_TAG('p', 'n', 'g', ' '))
            return false;
        *out = {p + 8, b - a - 8, ppem, float(int16_t(ot_u16(p))), float(int16_t(ot_u16(p + 2))), true};
        return true;
    }
    return false;
}
bool cbdt_source(const FontSlot& s, uint32_t strike, uint32_t glyph, BitmapSource* out) noexcept {
    const auto* d = s.cblc_data;
    const auto* size = d + 8 + strike * 48;
    const uint32_t list = ot_u32(size), count = ot_u32(size + 8), ppem = size[45];
    if (!ppem || glyph < ot_u16(size + 40) || glyph > ot_u16(size + 42) || list > s.cblc_len ||
        count > (s.cblc_len - list) / 8)
        return false;
    for (uint32_t i = 0; i < count; ++i) {
        const auto* r = d + list + i * 8;
        const uint32_t first = ot_u16(r), last = ot_u16(r + 2);
        if (glyph < first || glyph > last)
            continue;
        const uint32_t rel = ot_u32(r + 4);
        if (rel > s.cblc_len - list || s.cblc_len - list - rel < 8)
            return false;
        const uint32_t sub = list + rel;
        const auto* p = d + sub;
        const uint32_t format = ot_u16(p), image_format = ot_u16(p + 2), image_base = ot_u32(p + 4);
        uint32_t start = 0, end = 0;
        const uint8_t* metrics = nullptr;
        if (format == 1 || format == 3) {
            const uint32_t unit = format == 1 ? 4u : 2u;
            if (uint64_t(sub) + 8 + (uint64_t(last - first) + 2) * unit > s.cblc_len)
                return false;
            const auto* offsets = p + 8 + (glyph - first) * unit;
            start = unit == 4 ? ot_u32(offsets) : ot_u16(offsets);
            end = unit == 4 ? ot_u32(offsets + unit) : ot_u16(offsets + unit);
        } else if (format == 2) {
            if (s.cblc_len - sub < 20)
                return false;
            const uint32_t bytes = ot_u32(p + 8);
            const uint64_t pos = uint64_t(glyph - first) * bytes;
            if (pos + bytes > UINT32_MAX)
                return false;
            start = uint32_t(pos);
            end = start + bytes;
            metrics = p + 12;
        } else
            return false;
        if (start >= end || image_base > s.cbdt_len || end > s.cbdt_len - image_base)
            return false;
        const auto* image = s.cbdt_data + image_base + start;
        const uint32_t len = end - start;
        uint32_t header = 0;
        if (image_format == 17) {
            header = 5;
            metrics = image;
        } else if (image_format == 18) {
            header = 8;
            metrics = image;
        } else if (image_format != 19 || !metrics)
            return false;
        if (len < header + 4)
            return false;
        const uint32_t bytes = ot_u32(image + header);
        if (!bytes || bytes > len - header - 4)
            return false;
        *out = {image + header + 4, bytes, ppem, float(int8_t(metrics[2])), float(int8_t(metrics[3])), false};
        return true;
    }
    return false;
}
bool emoji_rehash(FontAtlas* a) noexcept {
    const uint32_t cap = a->emoji_cap ? a->emoji_cap * 2 : 128;
    auto* table = static_cast<EmojiBitmapEntry*>(std::calloc(cap, sizeof(EmojiBitmapEntry)));
    if (!table)
        return false;
    for (uint32_t i = 0; i < a->emoji_cap; ++i)
        if (a->emoji_bitmaps[i].key) {
            const auto e = a->emoji_bitmaps[i];
            uint32_t j = uint32_t(hash_u64(e.key)) & (cap - 1);
            while (table[j].key)
                j = (j + 1) & (cap - 1);
            table[j] = e;
        }
    std::free(a->emoji_bitmaps);
    a->emoji_bitmaps = table;
    a->emoji_cap = cap;
    return true;
}
void evict_font_slot_emoji_bitmaps(FontAtlas* a, uint16_t slot_idx) noexcept {
    if (!a || !a->emoji_bitmaps || !a->emoji_cap || !a->emoji_count)
        return;
    const uint32_t cap = a->emoji_cap;
    const uint32_t mask = cap - 1u;
    uint32_t orig_empty = cap;
    for (uint32_t i = 0; i < cap; ++i) {
        if (!a->emoji_bitmaps[i].key) {
            orig_empty = i;
            break;
        }
    }
    const uint64_t tag = uint64_t(slot_idx + 1u);
    bool removed = false;
    for (uint32_t i = 0; i < cap; ++i) {
        const EmojiBitmapEntry e = a->emoji_bitmaps[i];
        if (!e.key || (e.key >> 56) != tag)
            continue;
        if (e.sprite_id != kInvalidSprite)
            release_atlas_sprite(a, e.sprite_id);
        a->emoji_bitmaps[i] = {};
        --a->emoji_count;
        removed = true;
    }
    if (!removed || !a->emoji_count || orig_empty >= cap)
        return;
    for (uint32_t step = 1u; step < cap; ++step) {
        const uint32_t k = (orig_empty + step) & mask;
        const EmojiBitmapEntry e = a->emoji_bitmaps[k];
        if (!e.key)
            continue;
        a->emoji_bitmaps[k] = {};
        uint32_t j = uint32_t(hash_u64(e.key)) & mask;
        while (a->emoji_bitmaps[j].key)
            j = (j + 1u) & mask;
        a->emoji_bitmaps[j] = e;
    }
}
EmojiBitmapEntry cache_bitmap_strike(Context* ctx, uint16_t slot_idx, uint32_t glyph, uint32_t strike,
                                     bool sbix) noexcept {
    auto& a = ctx->atlas;
    const auto& s = a.slots[slot_idx];
    const uint64_t key = (uint64_t(slot_idx + 1) << 56) | (uint64_t(s.generation) << 48) |
                         (uint64_t(strike | (sbix ? 0u : 0x8000u)) << 32) | glyph;
    if (!a.emoji_cap || (a.emoji_count + 1) * 4 > a.emoji_cap * 3)
        if (!emoji_rehash(&a))
            return {};
    uint32_t i = uint32_t(hash_u64(key)) & (a.emoji_cap - 1);
    while (a.emoji_bitmaps[i].key && a.emoji_bitmaps[i].key != key)
        i = (i + 1) & (a.emoji_cap - 1);
    if (a.emoji_bitmaps[i].key)
        return a.emoji_bitmaps[i];
    EmojiBitmapEntry e{};
    e.key = key;
    BitmapSource source{};
    if (sbix ? sbix_source(s, strike, glyph, &source) : cbdt_source(s, strike, glyph, &source)) {
        e.ppem = source.ppem;
        DecodedImage image{};
        if (decode_image_memory(source.png, source.len, &image)) {
            e.sprite_id = create_sprite_rgba(
                ctx, {.rgba_pixels = image.rgba, .width = image.width, .height = image.height});
            e.width_px = image.width;
            e.height_px = image.height;
            e.bearing_x = source.x;
            e.bearing_y_top = source.top + (source.sbix ? float(image.height) : 0.0f);
            e.has_bitmap = e.sprite_id != kInvalidSprite;
            free_decoded_image(&image);
        }
    }
    a.emoji_bitmaps[i] = e;
    ++a.emoji_count;
    return e;
}
EmojiBitmapEntry get_or_cache_bitmap_emoji(Context* ctx, uint16_t slot_idx, uint32_t glyph,
                                           float phys_px) noexcept {
    const auto& s = ctx->atlas.slots[slot_idx];
    const float target = std::ceil(phys_px);
    // Probe metadata first: only decode/cache the chosen strike in the usual case.
    EmojiBitmapEntry best{};
    uint32_t best_strike = 0, best_ppem = 0;
    bool best_sbix = false;
    for (uint32_t kind = 0; kind < 2; ++kind) {
        const bool sbix = kind == 0;
        if (sbix ? !s.has_sbix : !s.has_cbdt)
            continue;
        const uint32_t count = ot_u32((sbix ? s.sbix_data : s.cblc_data) + 4);
        for (uint32_t i = 0; i < count && i < 0x8000u; ++i) {
            BitmapSource source{};
            if (!(sbix ? sbix_source(s, i, glyph, &source) : cbdt_source(s, i, glyph, &source)))
                continue;
            const uint32_t ppem = source.ppem;
            const bool above = float(ppem) >= target, old_above = float(best_ppem) >= target;
            if (!best_ppem || (above && !old_above) ||
                (above == old_above && (above ? ppem < best_ppem : ppem > best_ppem))) {
                best_ppem = ppem;
                best_strike = i;
                best_sbix = sbix;
            }
        }
    }
    if (best_ppem)
        best = cache_bitmap_strike(ctx, slot_idx, glyph, best_strike, best_sbix);
    return best;
}

bool hinted_rehash(FontAtlas* a) noexcept {
    const uint32_t cap = a->hinted_cap ? a->hinted_cap * 2u : 256u;
    auto* table = static_cast<EmojiBitmapEntry*>(std::calloc(cap, sizeof(EmojiBitmapEntry)));
    if (!table) return false;
    for (uint32_t i = 0; i < a->hinted_cap; ++i) {
        if (a->hinted_glyphs[i].key) {
            const auto e = a->hinted_glyphs[i];
            uint32_t j = uint32_t(hash_u64(e.key)) & (cap - 1u);
            while (table[j].key) j = (j + 1u) & (cap - 1u);
            table[j] = e;
        }
    }
    std::free(a->hinted_glyphs);
    a->hinted_glyphs = table;
    a->hinted_cap = cap;
    return true;
}

void remap_subpixel_prims_scale(Prim* prims, uint32_t count, float sx, float sy) noexcept {
    for (uint32_t i = 0u; i < count; ++i) {
        auto& p = prims[i];
        if ((p.style.x & 0xFu) != uint32_t(PrimKind::Glyph)) continue;
        p.payload.x = pack_unorm16x2(float(p.payload.x & 65535u) / 65535.0f * sx,
                                     float(p.payload.x >> 16) / 65535.0f * sy);
        p.payload.y = pack_unorm16x2(float(p.payload.y & 65535u) / 65535.0f * sx,
                                     float(p.payload.y >> 16) / 65535.0f * sy);
    }
}

bool ensure_subpixel_atlas(Context* ctx, uint32_t w, uint32_t h, uint32_t* out_x, uint32_t* out_y) noexcept {
    auto& a = ctx->atlas;
    constexpr uint32_t kSubpixelInit = 512u;
    constexpr uint32_t kSubpixelMax = 4096u;
    if (!a.subpixel_pixels) {
        a.subpixel_pixels = static_cast<uint8_t*>(std::calloc(size_t(kSubpixelInit) * kSubpixelInit * 4u, 1u));
        if (!a.subpixel_pixels) return false;
        a.subpixel_w = kSubpixelInit;
        a.subpixel_h = kSubpixelInit;
        a.subpixel_shelf_x = 1u;
        a.subpixel_shelf_y = 1u;
        a.subpixel_shelf_h = 0u;
        a.subpixel_dirty = true;
        a.subpixel_dirty_y0 = 0u;
        a.subpixel_dirty_y1 = kSubpixelInit;
    }
    const uint32_t pad_w = w + 1u;
    const uint32_t pad_h = h + 1u;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (a.subpixel_shelf_x + pad_w > a.subpixel_w) {
            a.subpixel_shelf_x = 1u;
            a.subpixel_shelf_y += a.subpixel_shelf_h + 1u;
            a.subpixel_shelf_h = 0u;
        }
        if (a.subpixel_shelf_y + pad_h <= a.subpixel_h) {
            *out_x = a.subpixel_shelf_x;
            *out_y = a.subpixel_shelf_y;
            a.subpixel_shelf_x += pad_w;
            if (pad_h > a.subpixel_shelf_h) a.subpixel_shelf_h = pad_h;
            return true;
        }
        const uint32_t old_w = a.subpixel_w, old_h = a.subpixel_h;
        uint32_t next_w = old_w, next_h = old_h;
        if (next_w <= next_h && next_w < kSubpixelMax) next_w *= 2u;
        else if (next_h < kSubpixelMax) next_h *= 2u;
        else if (next_w < kSubpixelMax) next_w *= 2u;
        else return false;
        auto* next_px = static_cast<uint8_t*>(std::calloc(size_t(next_w) * next_h * 4u, 1u));
        if (!next_px) return false;
        for (uint32_t row = 0u; row < old_h; ++row) {
            std::memcpy(next_px + size_t(row) * next_w * 4u,
                        a.subpixel_pixels + size_t(row) * old_w * 4u,
                        size_t(old_w) * 4u);
        }
        std::free(a.subpixel_pixels);
        a.subpixel_pixels = next_px;
        a.subpixel_w = next_w;
        a.subpixel_h = next_h;
        a.subpixel_dirty = true;
        a.subpixel_dirty_y0 = 0u;
        a.subpixel_dirty_y1 = next_h;
        const float sx = float(old_w) / float(next_w);
        const float sy = float(old_h) / float(next_h);
        remap_subpixel_prims_scale(ctx->prims, ctx->prim_count, sx, sy);
        if (ctx->gpu_prims && ctx->gpu_prims != ctx->prims)
            remap_subpixel_prims_scale(ctx->gpu_prims, ctx->prim_count, sx, sy);
        remap_subpixel_prims_scale(ctx->filter_prims, ctx->filter_prim_count, sx, sy);
    }
    return false;
}

void evict_font_slot_hinted_glyphs(FontAtlas* a, uint16_t slot_idx) noexcept {
    if (!a || !a->hinted_glyphs || !a->hinted_cap || !a->hinted_count) return;
    const uint32_t cap = a->hinted_cap;
    const uint32_t mask = cap - 1u;
    uint32_t orig_empty = cap;
    for (uint32_t i = 0; i < cap; ++i) {
        if (!a->hinted_glyphs[i].key) { orig_empty = i; break; }
    }
    const uint64_t tag = uint64_t(slot_idx + 1u);
    bool removed = false;
    for (uint32_t i = 0; i < cap; ++i) {
        const EmojiBitmapEntry e = a->hinted_glyphs[i];
        if (!e.key || (e.key >> 56) != tag) continue;
        a->hinted_glyphs[i] = {};
        --a->hinted_count;
        removed = true;
    }
    if (!removed || !a->hinted_count || orig_empty >= cap) return;
    for (uint32_t step = 1u; step < cap; ++step) {
        const uint32_t k = (orig_empty + step) & mask;
        const EmojiBitmapEntry e = a->hinted_glyphs[k];
        if (!e.key) continue;
        a->hinted_glyphs[k] = {};
        uint32_t j = uint32_t(hash_u64(e.key)) & mask;
        while (a->hinted_glyphs[j].key) j = (j + 1u) & mask;
        a->hinted_glyphs[j] = e;
    }
}

#if defined(_WIN32)
IDWriteFactory* ensure_dwrite_factory(FontAtlas* a) noexcept {
    if (a->dwrite_factory) return static_cast<IDWriteFactory*>(a->dwrite_factory);
    if (a->dwrite_probed) return nullptr;
    a->dwrite_probed = true;
    HMODULE mod = LoadLibraryW(L"dwrite.dll");
    if (!mod) return nullptr;
    using CreateFn = HRESULT (WINAPI *)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
    auto fn = reinterpret_cast<CreateFn>(GetProcAddress(mod, "DWriteCreateFactory"));
    if (!fn) { FreeLibrary(mod); return nullptr; }
    IUnknown* unk = nullptr;
    if (FAILED(fn(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &unk)) || !unk) {
        FreeLibrary(mod);
        return nullptr;
    }
    a->dwrite_module = mod;
    a->dwrite_factory = unk;
    return static_cast<IDWriteFactory*>(unk);
}

IDWriteFontFace* ensure_dwrite_face(FontAtlas* a, FontSlot* slot) noexcept {
    if (slot->dwrite_face) return static_cast<IDWriteFontFace*>(slot->dwrite_face);
    if (slot->dwrite_probed || slot->font_file_path[0] == '\0') return nullptr;
    slot->dwrite_probed = true;
    IDWriteFactory* factory = ensure_dwrite_factory(a);
    if (!factory) return nullptr;
    wchar_t wpath[512] = {};
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, slot->font_file_path, -1, wpath, 512);
    if (wlen <= 0) return nullptr;
    IDWriteFontFile* file = nullptr;
    if (FAILED(factory->CreateFontFileReference(wpath, nullptr, &file)) || !file) return nullptr;
    BOOL supported = FALSE;
    DWRITE_FONT_FILE_TYPE file_type = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    DWRITE_FONT_FACE_TYPE face_type = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    UINT32 num_faces = 0;
    IDWriteFontFace* face = nullptr;
    if (SUCCEEDED(file->Analyze(&supported, &file_type, &face_type, &num_faces)) && supported && num_faces > 0) {
        const UINT32 fidx = (slot->face_index >= 0 && static_cast<UINT32>(slot->face_index) < num_faces)
            ? static_cast<UINT32>(slot->face_index) : 0u;
        IDWriteFontFile* files[1] = {file};
        factory->CreateFontFace(face_type, 1, files, fidx, DWRITE_FONT_SIMULATIONS_NONE, &face);
    }
    file->Release();
    slot->dwrite_face = face;
    return face;
}
#endif

EmojiBitmapEntry get_or_cache_hinted_glyph(Context* ctx, uint16_t slot_idx, uint32_t glyph_id, float phys_px, uint8_t x_phase = 0u) noexcept {
#if defined(_WIN32)
    if (!ctx || slot_idx >= kMaxFontSlots || glyph_id > 0xFFFFu || phys_px < 6.0f || phys_px > 24.5f) return {};
    auto& a = ctx->atlas;
    auto& s = a.slots[slot_idx];
    if (!s.loaded || !s.has_outlines || s.font_file_path[0] == '\0') return {};
    const uint32_t ppem_half = static_cast<uint32_t>(std::round(phys_px * 2.0f));
    if (ppem_half < 12u || ppem_half > 50u) return {};
    const uint32_t phase2 = uint32_t(x_phase & 3u);
    const uint64_t key = (uint64_t(slot_idx + 1u) << 56) | (uint64_t(s.generation) << 48) |
                         (uint64_t(ppem_half) << 34) | (uint64_t(phase2) << 32) | uint64_t(glyph_id);
    if (!a.hinted_cap || (a.hinted_count + 1u) * 4u > a.hinted_cap * 3u) {
        if (!hinted_rehash(&a)) return {};
    }
    uint32_t idx = uint32_t(hash_u64(key)) & (a.hinted_cap - 1u);
    while (a.hinted_glyphs[idx].key && a.hinted_glyphs[idx].key != key) {
        idx = (idx + 1u) & (a.hinted_cap - 1u);
    }
    if (a.hinted_glyphs[idx].key) return a.hinted_glyphs[idx];

    EmojiBitmapEntry entry{};
    entry.key = key;
    entry.ppem = ppem_half;
    IDWriteFactory* factory = ensure_dwrite_factory(&a);
    IDWriteFontFace* face = ensure_dwrite_face(&a, &s);
    if (factory && face) {
        const float em_size = (static_cast<float>(ppem_half) * 0.5f) * s.scale_1px * static_cast<float>(s.upem);
        const UINT16 gid16 = static_cast<UINT16>(glyph_id);
        const FLOAT adv0 = 0.0f;
        const DWRITE_GLYPH_OFFSET off0 = {0.0f, 0.0f};
        DWRITE_GLYPH_RUN run{};
        run.fontFace = face;
        run.fontEmSize = em_size;
        run.glyphCount = 1;
        run.glyphIndices = &gid16;
        run.glyphAdvances = &adv0;
        run.glyphOffsets = &off0;
        run.isSideways = FALSE;
        run.bidiLevel = 0;

        const FLOAT origin_x = static_cast<FLOAT>(phase2) * 0.25f;
        IDWriteGlyphRunAnalysis* analysis = nullptr;
        const HRESULT hr = factory->CreateGlyphRunAnalysis(
            &run, 1.0f, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
            DWRITE_MEASURING_MODE_NATURAL, origin_x, 0.0f, &analysis);
        if (SUCCEEDED(hr) && analysis) {
            RECT rc{};
            if (SUCCEEDED(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &rc)) &&
                rc.right > rc.left && rc.bottom > rc.top)
            {
                const uint32_t w = static_cast<uint32_t>(rc.right - rc.left);
                const uint32_t h = static_cast<uint32_t>(rc.bottom - rc.top);
                if (w <= 128u && h <= 128u) {
                    const size_t rgb_bytes = size_t(w) * size_t(h) * 3u;
                    const size_t mark = ctx->frame_arena.mark();
                    auto* rgb = ctx->frame_arena.alloc_uninit<uint8_t>(rgb_bytes);
                    uint32_t tx = 0u, ty = 0u;
                    if (rgb &&
                        SUCCEEDED(analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &rc, rgb, static_cast<UINT32>(rgb_bytes))) &&
                        ensure_subpixel_atlas(ctx, w, h, &tx, &ty))
                    {
                        for (uint32_t row = 0u; row < h; ++row) {
                            const uint8_t* src_row = rgb + size_t(row) * w * 3u;
                            uint8_t* dst_row = a.subpixel_pixels + (size_t(ty + row) * a.subpixel_w + tx) * 4u;
                            for (uint32_t col = 0u; col < w; ++col) {
                                const uint8_t cr = src_row[col * 3u + 0u];
                                const uint8_t cg = src_row[col * 3u + 1u];
                                const uint8_t cb = src_row[col * 3u + 2u];
                                dst_row[col * 4u + 0u] = cr;
                                dst_row[col * 4u + 1u] = cg;
                                dst_row[col * 4u + 2u] = cb;
                                dst_row[col * 4u + 3u] = static_cast<uint8_t>((uint32_t(cr) + uint32_t(cg) + uint32_t(cb) + 1u) / 3u);
                            }
                        }
                        a.subpixel_dirty = true;
                        if (ty < a.subpixel_dirty_y0) a.subpixel_dirty_y0 = ty;
                        if (ty + h > a.subpixel_dirty_y1) a.subpixel_dirty_y1 = ty + h;
                        entry.tile_x = tx;
                        entry.tile_y = ty;
                        entry.width_px = w;
                        entry.height_px = h;
                        entry.bearing_x = static_cast<float>(rc.left);
                        entry.bearing_y_top = static_cast<float>(-rc.top);
                        entry.has_bitmap = true;
                    }
                    ctx->frame_arena.rewind(mark);
                }
            }
            analysis->Release();
        }
    }
    a.hinted_glyphs[idx] = entry;
    ++a.hinted_count;
    return entry;
#else
    (void)ctx; (void)slot_idx; (void)glyph_id; (void)phys_px; (void)x_phase;
    return {};
#endif
}
} // namespace

void set_hinted_small_text(Context* ctx, bool enabled) noexcept {
    if (!ctx) return;
    if (ctx->atlas.hinted_small_text != enabled) {
        ctx->atlas.hinted_small_text = enabled;
        request_redraw(ctx);
    }
}

bool hinted_small_text(const Context* ctx) noexcept {
    return ctx ? ctx->atlas.hinted_small_text : false;
}
} // namespace etch
