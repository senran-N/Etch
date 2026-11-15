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
} // namespace
} // namespace etch
