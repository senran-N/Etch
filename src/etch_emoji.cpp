// OpenType bitmap strikes; all offsets are validated before forming pointers.
#if defined(_WIN32)
#    include <dwrite_3.h>
#    include <new>
#elif defined(__APPLE__)
#    include <CoreFoundation/CoreFoundation.h>
#    include <CoreGraphics/CoreGraphics.h>
#    include <CoreText/CoreText.h>
#elif defined(ETCH_USE_FREETYPE)
#    include <ft2build.h>
#    include FT_FREETYPE_H
#    include FT_LCD_FILTER_H
#    include FT_MULTIPLE_MASTERS_H
#endif

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

constexpr uint32_t kSubpixelInit = 512u;
constexpr uint32_t kSubpixelSteadyCap = 1024u;
constexpr uint32_t kSubpixelBurstMaxW = 2048u;
constexpr uint32_t kSubpixelBurstMaxH = 1024u;

uint32_t hinted_occupied_shelf_area(const FontAtlas& a) noexcept {
    uint32_t area = 0u;
    for (uint32_t i = 0u; i < a.hinted_shelf_count; ++i) {
        const auto& sh = a.hinted_shelves[i];
        if (sh.live_count > 0u && sh.max_x > sh.x0) {
            area += uint32_t(sh.max_x - sh.x0) * uint32_t(sh.h);
        }
    }
    return area;
}

bool ensure_subpixel_atlas(
    Context* ctx,
    uint32_t w,
    uint32_t h,
    uint32_t* out_x,
    uint32_t* out_y,
    uint16_t* out_shelf = nullptr) noexcept
{
    if (!ctx || !out_x || !out_y || w == 0u || h == 0u) return false;
    auto& a = ctx->atlas;
    const uint32_t cur_frame = ctx->active_ui ? ctx->active_ui->frame_index : 0u;
    const uint32_t bpp = a.subpixel_is_r8 ? 1u : 4u;
    if (!a.subpixel_pixels) {
        a.subpixel_pixels = static_cast<uint8_t*>(std::calloc(size_t(kSubpixelInit) * kSubpixelInit * bpp, 1u));
        if (!a.subpixel_pixels) return false;
        a.subpixel_w = kSubpixelInit;
        a.subpixel_h = kSubpixelInit;
        a.subpixel_shelf_x0 = 1u;
        a.subpixel_shelf_x = 1u;
        a.subpixel_shelf_y = 1u;
        a.subpixel_shelf_h = 0u;
        a.hinted_shelf_count = 0u;
        a.subpixel_dirty = true;
        a.subpixel_dirty_y0 = 0u;
        a.subpixel_dirty_y1 = kSubpixelInit;
    }
    const uint32_t pad_w = w + 1u;
    const uint32_t pad_h = h + 1u;

    const auto update_gc_flag = [&]() noexcept {
        if (a.subpixel_w > kSubpixelSteadyCap || a.subpixel_h > kSubpixelSteadyCap) {
            a.hinted_gc_pending = true;
            return;
        }
        if (a.subpixel_w >= kSubpixelSteadyCap && a.subpixel_h >= kSubpixelSteadyCap &&
            a.subpixel_shelf_y >= 900u)
        {
            const uint32_t occ = hinted_occupied_shelf_area(a);
            if (a.hinted_count == 0u || occ >= (kSubpixelSteadyCap * kSubpixelSteadyCap * 85u) / 100u) {
                a.hinted_gc_pending = true;
            }
        }
    };

    for (int attempt = 0; attempt < 4; ++attempt) {
        // 1. Fit into an existing open or LRU-reclaimed shelf with tight height match.
        uint32_t best_si = UINT32_MAX;
        uint32_t best_slack = UINT32_MAX;
        for (uint32_t i = 0u; i < a.hinted_shelf_count; ++i) {
            const auto& sh = a.hinted_shelves[i];
            if (pad_h <= sh.h && uint32_t(sh.cursor_x) + pad_w <= uint32_t(sh.max_x)) {
                const uint32_t slack = uint32_t(sh.h) - pad_h;
                if (slack <= 6u && slack < best_slack) {
                    best_slack = slack;
                    best_si = i;
                    if (slack == 0u) break;
                }
            }
        }
        if (best_si != UINT32_MAX) {
            auto& sh = a.hinted_shelves[best_si];
            *out_x = sh.cursor_x;
            *out_y = sh.y;
            sh.cursor_x = static_cast<uint16_t>(sh.cursor_x + pad_w);
            ++sh.live_count;
            if (cur_frame > sh.last_used_frame) sh.last_used_frame = cur_frame;
            if (out_shelf) *out_shelf = static_cast<uint16_t>(best_si);
            update_gc_flag();
            return true;
        }

        // 2. Open a new shelf in the active vertical strip.
        const uint32_t strip_x0 = (a.subpixel_shelf_x0 > 0u) ? a.subpixel_shelf_x0 : 1u;
        if (strip_x0 + pad_w <= a.subpixel_w &&
            a.subpixel_shelf_y + pad_h <= a.subpixel_h &&
            a.hinted_shelf_count < FontAtlas::kMaxHintedShelves)
        {
            const uint32_t si = a.hinted_shelf_count++;
            auto& sh = a.hinted_shelves[si];
            sh.x0 = static_cast<uint16_t>(strip_x0);
            sh.y = static_cast<uint16_t>(a.subpixel_shelf_y);
            sh.max_x = static_cast<uint16_t>(a.subpixel_w);
            sh.h = static_cast<uint16_t>(pad_h);
            sh.cursor_x = static_cast<uint16_t>(strip_x0 + pad_w);
            sh.live_count = 1u;
            sh.last_used_frame = cur_frame;
            a.subpixel_shelf_x = sh.cursor_x;
            a.subpixel_shelf_y += pad_h + 1u;
            a.subpixel_shelf_h = pad_h;
            *out_x = sh.x0;
            *out_y = sh.y;
            if (out_shelf) *out_shelf = static_cast<uint16_t>(si);
            update_gc_flag();
            return true;
        }

        // 3. Before growing, fall back to any wider-height open/reclaimed shelf that fits.
        for (uint32_t i = 0u; i < a.hinted_shelf_count; ++i) {
            const auto& sh = a.hinted_shelves[i];
            if (pad_h <= sh.h && uint32_t(sh.cursor_x) + pad_w <= uint32_t(sh.max_x)) {
                const uint32_t slack = uint32_t(sh.h) - pad_h;
                if (slack < best_slack) {
                    best_slack = slack;
                    best_si = i;
                }
            }
        }
        if (best_si != UINT32_MAX) {
            auto& sh = a.hinted_shelves[best_si];
            *out_x = sh.cursor_x;
            *out_y = sh.y;
            sh.cursor_x = static_cast<uint16_t>(sh.cursor_x + pad_w);
            ++sh.live_count;
            if (cur_frame > sh.last_used_frame) sh.last_used_frame = cur_frame;
            if (out_shelf) *out_shelf = static_cast<uint16_t>(best_si);
            update_gc_flag();
            return true;
        }

        // 4. Grow atlas: 512x512 -> 512x1024 -> 1024x1024 -> 2048x1024 burst.
        const uint32_t old_w = a.subpixel_w, old_h = a.subpixel_h;
        uint32_t next_w = old_w, next_h = old_h;
        if (next_h <= next_w && next_h < kSubpixelBurstMaxH) next_h *= 2u;
        else if (next_w < kSubpixelBurstMaxW) next_w *= 2u;
        else if (next_h < kSubpixelBurstMaxH) next_h *= 2u;
        else {
            a.hinted_gc_pending = true;
            return false;
        }
        auto* next_px = static_cast<uint8_t*>(std::calloc(size_t(next_w) * next_h * bpp, 1u));
        if (!next_px) return false;
        for (uint32_t row = 0u; row < old_h; ++row) {
            std::memcpy(next_px + size_t(row) * next_w * bpp,
                        a.subpixel_pixels + size_t(row) * old_w * bpp,
                        size_t(old_w) * bpp);
        }
        std::free(a.subpixel_pixels);
        a.subpixel_pixels = next_px;
        a.subpixel_w = next_w;
        a.subpixel_h = next_h;
        if (next_w > kSubpixelSteadyCap || next_h > kSubpixelSteadyCap) {
            a.hinted_gc_pending = true;
        }
        if (next_h > old_h) {
            a.subpixel_shelf_x0 = 1u;
            a.subpixel_shelf_x = 1u;
            a.subpixel_shelf_y = old_h;
            a.subpixel_shelf_h = 0u;
        } else {
            a.subpixel_shelf_x0 = old_w;
            a.subpixel_shelf_x = old_w;
            a.subpixel_shelf_y = 1u;
            a.subpixel_shelf_h = 0u;
        }
        a.subpixel_dirty = true;
        a.subpixel_dirty_y0 = 0u;
        a.subpixel_dirty_y1 = next_h;
        ++a.uv_epoch;
        const float sx = float(old_w) / float(next_w);
        const float sy = float(old_h) / float(next_h);
        remap_subpixel_prims_scale(ctx->prims, ctx->prim_count, sx, sy);
        if (ctx->gpu_prims && ctx->gpu_prims != ctx->prims)
            remap_subpixel_prims_scale(ctx->gpu_prims, ctx->prim_count, sx, sy);
        remap_subpixel_prims_scale(ctx->filter_prims, ctx->filter_prim_count, sx, sy);
    }
    return false;
}

void reset_hinted_atlas_cache(FontAtlas* a) noexcept {
    if (!a) return;
    if (a->hinted_glyphs && a->hinted_cap > 0u) {
        std::fill_n(a->hinted_glyphs, a->hinted_cap, EmojiBitmapEntry{});
    }
    a->hinted_count = 0u;
    std::free(a->subpixel_pixels);
    a->subpixel_pixels = nullptr;
    a->subpixel_w = 0u;
    a->subpixel_h = 0u;
    a->subpixel_shelf_x0 = 1u;
    a->subpixel_shelf_x = 0u;
    a->subpixel_shelf_y = 2u;
    a->subpixel_shelf_h = 0u;
    a->hinted_shelf_count = 0u;
    a->subpixel_dirty = false;
    a->subpixel_dirty_y0 = UINT32_MAX;
    a->subpixel_dirty_y1 = 0u;
    a->hinted_gc_pending = false;
    ++a->uv_epoch;
}

void hinted_reprobe_table(FontAtlas* a) noexcept {
    if (!a->hinted_glyphs || !a->hinted_cap || !a->hinted_count) return;
    const uint32_t cap = a->hinted_cap;
    const uint32_t mask = cap - 1u;
    uint32_t orig_empty = cap;
    for (uint32_t i = 0u; i < cap; ++i) {
        if (!a->hinted_glyphs[i].key) { orig_empty = i; break; }
    }
    if (orig_empty >= cap) return;
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

void hinted_sort_indices_desc(const EmojiBitmapEntry* tbl, uint32_t* order, uint32_t n) noexcept {
    auto less_priority = [tbl](uint32_t a_idx, uint32_t b_idx) noexcept {
        const auto& ea = tbl[a_idx];
        const auto& eb = tbl[b_idx];
        if (ea.last_used_frame != eb.last_used_frame) return ea.last_used_frame < eb.last_used_frame;
        return ea.height_px < eb.height_px;
    };
    // In-place min-heap sort so order[0..n) ends sorted from highest priority (hottest) to coldest.
    auto sift_down = [&](uint32_t root, uint32_t end) noexcept {
        while (root * 2u + 1u < end) {
            uint32_t child = root * 2u + 1u;
            if (child + 1u < end && less_priority(order[child + 1u], order[child])) ++child;
            if (!less_priority(order[child], order[root])) break;
            const uint32_t tmp = order[root];
            order[root] = order[child];
            order[child] = tmp;
            root = child;
        }
    };
    if (n < 2u) return;
    for (uint32_t start = n / 2u; start > 0u; --start) sift_down(start - 1u, n);
    for (uint32_t end = n; end > 1u; --end) {
        const uint32_t tmp = order[0];
        order[0] = order[end - 1u];
        order[end - 1u] = tmp;
        sift_down(0u, end - 1u);
    }
}

bool compact_hinted_burst_lru(FontAtlas* a) noexcept {
    const uint32_t bpp = a->subpixel_is_r8 ? 1u : 4u;
    const uint32_t cap = a->hinted_cap;
    const uint32_t old_w = a->subpixel_w;
    if (!a->hinted_glyphs || !cap || !a->hinted_count || !a->subpixel_pixels) return false;

    auto* order = static_cast<uint32_t*>(std::malloc(size_t(a->hinted_count) * sizeof(uint32_t)));
    auto* new_px = static_cast<uint8_t*>(std::calloc(size_t(kSubpixelSteadyCap) * kSubpixelSteadyCap * bpp, 1u));
    auto* new_tbl = static_cast<EmojiBitmapEntry*>(std::calloc(cap, sizeof(EmojiBitmapEntry)));
    if (!order || !new_px || !new_tbl) {
        std::free(order);
        std::free(new_px);
        std::free(new_tbl);
        return false;
    }

    uint32_t n = 0u;
    uint32_t max_frame = 0u;
    for (uint32_t i = 0u; i < cap; ++i) {
        if (a->hinted_glyphs[i].key) {
            if (a->hinted_glyphs[i].last_used_frame > max_frame) max_frame = a->hinted_glyphs[i].last_used_frame;
            if (n < a->hinted_count) order[n++] = i;
        }
    }
    hinted_sort_indices_desc(a->hinted_glyphs, order, n);

    HintedShelf new_shelves[FontAtlas::kMaxHintedShelves] = {};
    uint32_t new_shelf_count = 0u;
    uint32_t next_y = 1u;
    constexpr uint32_t kCompactTargetY = 768u; // Leave ~25% headroom on steady-state 1024x1024
    uint32_t kept = 0u;
    const uint32_t mask = cap - 1u;

    for (uint32_t oi = 0u; oi < n; ++oi) {
        EmojiBitmapEntry e = a->hinted_glyphs[order[oi]];
        if (!e.has_bitmap) {
            if (e.last_used_frame + 1u < max_frame) continue;
            e.shelf_idx = UINT16_MAX;
            uint32_t j = uint32_t(hash_u64(e.key)) & mask;
            while (new_tbl[j].key) j = (j + 1u) & mask;
            new_tbl[j] = e;
            ++kept;
            continue;
        }
        const uint32_t pad_w = e.width_px + 1u;
        const uint32_t pad_h = e.height_px + 1u;
        uint32_t best_si = UINT32_MAX;
        uint32_t best_slack = UINT32_MAX;
        for (uint32_t si = 0u; si < new_shelf_count; ++si) {
            const auto& sh = new_shelves[si];
            if (pad_h <= sh.h && uint32_t(sh.cursor_x) + pad_w <= uint32_t(sh.max_x)) {
                const uint32_t slack = uint32_t(sh.h) - pad_h;
                if (slack <= 6u && slack < best_slack) {
                    best_slack = slack;
                    best_si = si;
                    if (slack == 0u) break;
                }
            }
        }
        uint32_t dst_x = 0u, dst_y = 0u;
        uint16_t dst_si = UINT16_MAX;
        if (best_si != UINT32_MAX) {
            auto& sh = new_shelves[best_si];
            dst_x = sh.cursor_x;
            dst_y = sh.y;
            sh.cursor_x = static_cast<uint16_t>(sh.cursor_x + pad_w);
            ++sh.live_count;
            if (e.last_used_frame > sh.last_used_frame) sh.last_used_frame = e.last_used_frame;
            dst_si = static_cast<uint16_t>(best_si);
        } else if (1u + pad_w <= kSubpixelSteadyCap &&
                   next_y + pad_h <= kCompactTargetY &&
                   new_shelf_count < FontAtlas::kMaxHintedShelves)
        {
            const uint32_t si = new_shelf_count++;
            auto& sh = new_shelves[si];
            sh.x0 = 1u;
            sh.y = static_cast<uint16_t>(next_y);
            sh.max_x = static_cast<uint16_t>(kSubpixelSteadyCap);
            sh.h = static_cast<uint16_t>(pad_h);
            sh.cursor_x = static_cast<uint16_t>(1u + pad_w);
            sh.live_count = 1u;
            sh.last_used_frame = e.last_used_frame;
            dst_x = 1u;
            dst_y = next_y;
            next_y += pad_h + 1u;
            dst_si = static_cast<uint16_t>(si);
        } else {
            continue; // Cold overflow glyph dropped cleanly
        }

        for (uint32_t row = 0u; row < e.height_px; ++row) {
            std::memcpy(new_px + (size_t(dst_y + row) * kSubpixelSteadyCap + dst_x) * bpp,
                        a->subpixel_pixels + (size_t(e.tile_y + row) * old_w + e.tile_x) * bpp,
                        size_t(e.width_px) * bpp);
        }
        e.tile_x = dst_x;
        e.tile_y = dst_y;
        e.shelf_idx = dst_si;
        uint32_t j = uint32_t(hash_u64(e.key)) & mask;
        while (new_tbl[j].key) j = (j + 1u) & mask;
        new_tbl[j] = e;
        ++kept;
    }

    std::free(order);
    if (kept == 0u) {
        std::free(new_px);
        std::free(new_tbl);
        return false;
    }

    std::free(a->subpixel_pixels);
    std::free(a->hinted_glyphs);
    a->subpixel_pixels = new_px;
    a->hinted_glyphs = new_tbl;
    a->hinted_count = kept;
    a->subpixel_w = kSubpixelSteadyCap;
    a->subpixel_h = kSubpixelSteadyCap;
    a->subpixel_shelf_x0 = 1u;
    a->subpixel_shelf_x = 1u;
    a->subpixel_shelf_y = next_y;
    a->subpixel_shelf_h = 0u;
    a->hinted_shelf_count = new_shelf_count;
    std::memcpy(a->hinted_shelves, new_shelves, sizeof(new_shelves));
    a->subpixel_dirty = true;
    a->subpixel_dirty_y0 = 0u;
    a->subpixel_dirty_y1 = next_y > 0u ? next_y : 1u;
    a->hinted_gc_pending = false;
    ++a->uv_epoch;
    return true;
}

void maybe_gc_hinted_atlas(Context* ctx) noexcept {
    if (!ctx || !ctx->atlas.hinted_gc_pending || ctx->prim_count != 0u || ctx->filter_prim_count != 0u) return;
    auto& a = ctx->atlas;
    const bool mode_changed = (a.subpixel_is_r8 != !a.lcd_subpixel_text);
    if (mode_changed || a.hinted_count == 0u || !a.subpixel_pixels || a.hinted_shelf_count == 0u) {
        a.subpixel_is_r8 = !a.lcd_subpixel_text;
        reset_hinted_atlas_cache(&a);
        return;
    }

    // Burst atlas (> 1024x1024): shrink back to 1024x1024 preserving hottest glyphs without re-rasterization.
    if (a.subpixel_w > kSubpixelSteadyCap || a.subpixel_h > kSubpixelSteadyCap) {
        if (!compact_hinted_burst_lru(&a)) {
            reset_hinted_atlas_cache(&a);
        }
        return;
    }

    // Steady-state (<= 1024x1024): evict coldest shelves in-place while preserving all hot shelves untouched.
    uint32_t max_frame = 0u;
    uint32_t occupied_area = 0u;
    for (uint32_t i = 0u; i < a.hinted_shelf_count; ++i) {
        const auto& sh = a.hinted_shelves[i];
        if (sh.live_count > 0u) {
            if (sh.last_used_frame > max_frame) max_frame = sh.last_used_frame;
            if (sh.max_x > sh.x0) occupied_area += uint32_t(sh.max_x - sh.x0) * uint32_t(sh.h);
        }
    }

    uint16_t cand[FontAtlas::kMaxHintedShelves] = {};
    uint32_t cand_count = 0u;
    for (uint32_t i = 0u; i < a.hinted_shelf_count; ++i) {
        const auto& sh = a.hinted_shelves[i];
        if (sh.live_count > 0u && sh.last_used_frame < max_frame) {
            cand[cand_count++] = static_cast<uint16_t>(i);
        }
    }
    if (cand_count == 0u) {
        a.hinted_gc_pending = false;
        return;
    }

    // Insertion-sort cold candidate shelves by ascending last_used_frame (oldest first).
    for (uint32_t i = 1u; i < cand_count; ++i) {
        const uint16_t idx = cand[i];
        const uint32_t f = a.hinted_shelves[idx].last_used_frame;
        uint32_t j = i;
        while (j > 0u && a.hinted_shelves[cand[j - 1u]].last_used_frame > f) {
            cand[j] = cand[j - 1u];
            --j;
        }
        cand[j] = idx;
    }

    const uint32_t target_reclaim = (occupied_area * 35u) / 100u;
    const uint32_t bpp = a.subpixel_is_r8 ? 1u : 4u;
    uint64_t evict_mask[4] = {};
    uint32_t reclaimed_area = 0u;
    uint32_t dirty_y0 = UINT32_MAX;
    uint32_t dirty_y1 = 0u;
    bool evicted_any = false;

    for (uint32_t i = 0u; i < cand_count; ++i) {
        const uint16_t si = cand[i];
        auto& sh = a.hinted_shelves[si];
        evict_mask[si >> 6] |= (1ULL << (si & 63u));
        evicted_any = true;
        const uint32_t span_w = (sh.max_x > sh.x0) ? uint32_t(sh.max_x - sh.x0) : 0u;
        reclaimed_area += span_w * uint32_t(sh.h);
        if (span_w > 0u && uint32_t(sh.y) + uint32_t(sh.h) <= a.subpixel_h) {
            for (uint32_t row = 0u; row < sh.h; ++row) {
                std::memset(a.subpixel_pixels + (size_t(sh.y + row) * a.subpixel_w + sh.x0) * bpp,
                            0, size_t(span_w) * bpp);
            }
            if (sh.y < dirty_y0) dirty_y0 = sh.y;
            if (uint32_t(sh.y) + uint32_t(sh.h) > dirty_y1) dirty_y1 = uint32_t(sh.y) + uint32_t(sh.h);
        }
        sh.cursor_x = sh.x0;
        sh.live_count = 0u;
        sh.last_used_frame = 0u;
        if (reclaimed_area >= target_reclaim) break;
    }

    if (evicted_any && a.hinted_glyphs && a.hinted_cap > 0u) {
        for (uint32_t i = 0u; i < a.hinted_cap; ++i) {
            const auto& e = a.hinted_glyphs[i];
            if (!e.key || !e.has_bitmap) continue;
            if (e.shelf_idx < FontAtlas::kMaxHintedShelves &&
                (evict_mask[e.shelf_idx >> 6] & (1ULL << (e.shelf_idx & 63u))) != 0ULL)
            {
                a.hinted_glyphs[i] = {};
                --a.hinted_count;
            }
        }
        hinted_reprobe_table(&a);
        if (dirty_y0 < dirty_y1) {
            a.subpixel_dirty = true;
            if (dirty_y0 < a.subpixel_dirty_y0) a.subpixel_dirty_y0 = dirty_y0;
            if (dirty_y1 > a.subpixel_dirty_y1) a.subpixel_dirty_y1 = dirty_y1;
        }
        ++a.uv_epoch;
    }
    a.hinted_gc_pending = false;
}

void evict_font_slot_hinted_glyphs(FontAtlas* a, uint16_t slot_idx) noexcept {
    if (!a || !a->hinted_glyphs || !a->hinted_cap || !a->hinted_count) return;
    const uint32_t cap = a->hinted_cap;
    const uint64_t tag = uint64_t(slot_idx + 1u);
    bool removed = false;
    for (uint32_t i = 0u; i < cap; ++i) {
        const EmojiBitmapEntry e = a->hinted_glyphs[i];
        if (!e.key || (e.key >> 56) != tag) continue;
        if (e.has_bitmap && e.shelf_idx < a->hinted_shelf_count && a->hinted_shelves[e.shelf_idx].live_count > 0u) {
            --a->hinted_shelves[e.shelf_idx].live_count;
        }
        a->hinted_glyphs[i] = {};
        --a->hinted_count;
        removed = true;
    }
    if (removed) hinted_reprobe_table(a);
}

#if defined(_WIN32)
struct EtchDWriteMemKey {
    const void* data;
    size_t      size;
    uint64_t    unique_id;
};

class EtchDWriteMemStream final : public IDWriteFontFileStream {
public:
    EtchDWriteMemStream(uint8_t* owned_data, size_t size) noexcept
        : ref_count_(1), data_(owned_data), size_(static_cast<UINT64>(size)) {}

    ~EtchDWriteMemStream() noexcept {
        if (data_) SDL_free(data_);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(IDWriteFontFileStream))) {
            *ppv = static_cast<IDWriteFontFileStream*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const LONG c = InterlockedDecrement(&ref_count_);
        if (c == 0) {
            this->~EtchDWriteMemStream();
            std::free(this);
        }
        return static_cast<ULONG>(c);
    }

    HRESULT STDMETHODCALLTYPE ReadFileFragment(
        void const** fragmentStart,
        UINT64 fileOffset,
        UINT64 fragmentSize,
        void** fragmentContext) override
    {
        if (!fragmentStart || !fragmentContext) return E_INVALIDARG;
        if (fileOffset > size_ || fragmentSize > size_ - fileOffset) {
            *fragmentStart = nullptr;
            *fragmentContext = nullptr;
            return E_FAIL;
        }
        *fragmentStart = data_ + fileOffset;
        *fragmentContext = nullptr;
        return S_OK;
    }

    void STDMETHODCALLTYPE ReleaseFileFragment(void* fragmentContext) override {
        (void)fragmentContext;
    }

    HRESULT STDMETHODCALLTYPE GetFileSize(UINT64* fileSize) override {
        if (!fileSize) return E_INVALIDARG;
        *fileSize = size_;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetLastWriteTime(UINT64* lastWriteTime) override {
        if (!lastWriteTime) return E_INVALIDARG;
        *lastWriteTime = 0u;
        return E_NOTIMPL;
    }

private:
    LONG     ref_count_;
    uint8_t* data_;
    UINT64   size_;
};

class EtchDWriteMemLoader final : public IDWriteFontFileLoader {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(IDWriteFontFileLoader))) {
            *ppv = static_cast<IDWriteFontFileLoader*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        return static_cast<ULONG>(InterlockedDecrement(&ref_count_));
    }

    HRESULT STDMETHODCALLTYPE CreateStreamFromKey(
        void const* key,
        UINT32 keySize,
        IDWriteFontFileStream** outStream) override
    {
        if (!outStream) return E_POINTER;
        *outStream = nullptr;
        if (!key || keySize != sizeof(EtchDWriteMemKey)) return E_INVALIDARG;
        EtchDWriteMemKey mk{};
        std::memcpy(&mk, key, sizeof(mk));
        if (!mk.data || mk.size == 0u) return E_INVALIDARG;
        void* copy = SDL_malloc(mk.size);
        if (!copy) return E_OUTOFMEMORY;
        std::memcpy(copy, mk.data, mk.size);
        void* mem = std::malloc(sizeof(EtchDWriteMemStream));
        if (!mem) {
            SDL_free(copy);
            return E_OUTOFMEMORY;
        }
        *outStream = ::new (mem) EtchDWriteMemStream(static_cast<uint8_t*>(copy), mk.size);
        return S_OK;
    }

private:
    LONG ref_count_ = 1;
};

SRWLOCK             g_dwrite_lock = SRWLOCK_INIT;
EtchDWriteMemLoader g_dwrite_mem_loader;
LONG                g_dwrite_factory_refs = 0;
bool                g_dwrite_custom_loader_registered = false;
#if defined(NTDDI_WIN10_RS2) && (NTDDI_VERSION >= NTDDI_WIN10_RS2)
IDWriteInMemoryFontFileLoader* g_dwrite_inmem_loader = nullptr;
#endif

#if defined(NTDDI_WIN10_RS3) && (NTDDI_VERSION >= NTDDI_WIN10_RS3)
constexpr DWRITE_FONT_AXIS_TAG to_dwrite_axis_tag(uint32_t ot_tag) noexcept {
    // OpenType/HarfBuzz tags are big-endian ('wght' = 0x77676874); DirectWrite tags are little-endian.
    return static_cast<DWRITE_FONT_AXIS_TAG>(
        ((ot_tag & 0x000000FFu) << 24) |
        ((ot_tag & 0x0000FF00u) << 8)  |
        ((ot_tag & 0x00FF0000u) >> 8)  |
        ((ot_tag & 0xFF000000u) >> 24));
}
#endif

void release_platform_font_face(FontSlot* slot) noexcept {
    if (slot->dwrite_face) {
        static_cast<IDWriteFontFace*>(slot->dwrite_face)->Release();
        slot->dwrite_face = nullptr;
    }
}

void release_platform_font_factory(FontAtlas* a) noexcept {
    if (a->dwrite_factory) {
        auto* factory = static_cast<IDWriteFactory*>(a->dwrite_factory);
        AcquireSRWLockExclusive(&g_dwrite_lock);
        if (g_dwrite_factory_refs > 0 && --g_dwrite_factory_refs == 0) {
#if defined(NTDDI_WIN10_RS2) && (NTDDI_VERSION >= NTDDI_WIN10_RS2)
            if (g_dwrite_inmem_loader) {
                factory->UnregisterFontFileLoader(g_dwrite_inmem_loader);
                g_dwrite_inmem_loader->Release();
                g_dwrite_inmem_loader = nullptr;
            }
#endif
            if (g_dwrite_custom_loader_registered) {
                factory->UnregisterFontFileLoader(&g_dwrite_mem_loader);
                g_dwrite_custom_loader_registered = false;
            }
        }
        ReleaseSRWLockExclusive(&g_dwrite_lock);
        factory->Release();
        a->dwrite_factory = nullptr;
    }
    if (a->dwrite_module) {
        FreeLibrary(static_cast<HMODULE>(a->dwrite_module));
        a->dwrite_module = nullptr;
    }
}

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
    auto* factory = static_cast<IDWriteFactory*>(unk);
    AcquireSRWLockExclusive(&g_dwrite_lock);
    if (++g_dwrite_factory_refs == 1) {
#if defined(NTDDI_WIN10_RS2) && (NTDDI_VERSION >= NTDDI_WIN10_RS2)
        IDWriteFactory5* f5 = nullptr;
        if (SUCCEEDED(factory->QueryInterface(__uuidof(IDWriteFactory5), reinterpret_cast<void**>(&f5))) && f5) {
            IDWriteInMemoryFontFileLoader* inmem = nullptr;
            if (SUCCEEDED(f5->CreateInMemoryFontFileLoader(&inmem)) && inmem) {
                if (SUCCEEDED(factory->RegisterFontFileLoader(inmem))) {
                    g_dwrite_inmem_loader = inmem;
                } else {
                    inmem->Release();
                }
            }
            f5->Release();
        }
        if (!g_dwrite_inmem_loader) {
            g_dwrite_custom_loader_registered = SUCCEEDED(factory->RegisterFontFileLoader(&g_dwrite_mem_loader));
        }
#else
        g_dwrite_custom_loader_registered = SUCCEEDED(factory->RegisterFontFileLoader(&g_dwrite_mem_loader));
#endif
    }
    ReleaseSRWLockExclusive(&g_dwrite_lock);
    a->dwrite_module = mod;
    a->dwrite_factory = unk;
    return factory;
}

IDWriteFontFile* create_dwrite_font_file(IDWriteFactory* factory, const FontSlot* slot) noexcept {
    IDWriteFontFile* file = nullptr;
    if (slot->font_file_path[0] != '\0') {
        wchar_t wpath[512] = {};
        const int wlen = MultiByteToWideChar(CP_UTF8, 0, slot->font_file_path, -1, wpath, 512);
        if (wlen > 0 && SUCCEEDED(factory->CreateFontFileReference(wpath, nullptr, &file)) && file) {
            return file;
        }
        file = nullptr;
    }
    if (!slot->font_file_data || slot->font_file_size == 0u || slot->font_file_size > 0xFFFFFFFFu) {
        return nullptr;
    }
#if defined(NTDDI_WIN10_RS2) && (NTDDI_VERSION >= NTDDI_WIN10_RS2)
    IDWriteInMemoryFontFileLoader* inmem = nullptr;
    AcquireSRWLockShared(&g_dwrite_lock);
    inmem = g_dwrite_inmem_loader;
    if (inmem) inmem->AddRef();
    const bool custom_registered = g_dwrite_custom_loader_registered;
    ReleaseSRWLockShared(&g_dwrite_lock);
    if (inmem) {
        const HRESULT hr = inmem->CreateInMemoryFontFileReference(
            factory, slot->font_file_data, static_cast<UINT32>(slot->font_file_size), nullptr, &file);
        inmem->Release();
        if (SUCCEEDED(hr) && file) {
            return file;
        }
        file = nullptr;
    }
#else
    AcquireSRWLockShared(&g_dwrite_lock);
    const bool custom_registered = g_dwrite_custom_loader_registered;
    ReleaseSRWLockShared(&g_dwrite_lock);
#endif
    if (custom_registered) {
        static volatile LONG64 s_mem_key_seq = 0;
        const EtchDWriteMemKey key = {
            slot->font_file_data,
            slot->font_file_size,
            static_cast<uint64_t>(InterlockedIncrement64(&s_mem_key_seq)),
        };
        if (SUCCEEDED(factory->CreateCustomFontFileReference(&key, sizeof(key), &g_dwrite_mem_loader, &file)) && file) {
            return file;
        }
    }
    return nullptr;
}

IDWriteFontFace* ensure_dwrite_face(FontAtlas* a, FontSlot* slot) noexcept {
    if (slot->dwrite_face) return static_cast<IDWriteFontFace*>(slot->dwrite_face);
    if (slot->dwrite_probed) return nullptr;
    if (slot->font_file_path[0] == '\0' && (!slot->font_file_data || slot->font_file_size == 0u)) return nullptr;
    slot->dwrite_probed = true;
    IDWriteFactory* factory = ensure_dwrite_factory(a);
    if (!factory) return nullptr;

    IDWriteFontFace* face = nullptr;
    const bool want_var_axes = slot->is_variation && slot->axis_count > 0u;
#if defined(NTDDI_WIN10_RS3) && (NTDDI_VERSION >= NTDDI_WIN10_RS3)
    DWRITE_FONT_AXIS_VALUE dw_axes[8] = {};
    const UINT32 dw_axis_count = want_var_axes
        ? static_cast<UINT32>(slot->axis_count <= 8u ? slot->axis_count : 8u)
        : 0u;
    for (UINT32 i = 0u; i < dw_axis_count; ++i) {
        dw_axes[i] = DWRITE_FONT_AXIS_VALUE{
            to_dwrite_axis_tag(slot->axes[i].tag),
            slot->axes[i].value,
        };
    }
    // Reuse parent slot's IDWriteFontResource when available to avoid duplicating in-memory font bytes.
    if (want_var_axes && slot->parent_slot < kMaxFontSlots && a->slots[slot->parent_slot].dwrite_face) {
        auto* parent_face = static_cast<IDWriteFontFace*>(a->slots[slot->parent_slot].dwrite_face);
        IDWriteFontFace5* parent_face5 = nullptr;
        if (SUCCEEDED(parent_face->QueryInterface(__uuidof(IDWriteFontFace5), reinterpret_cast<void**>(&parent_face5))) && parent_face5) {
            IDWriteFontResource* resource = nullptr;
            if (SUCCEEDED(parent_face5->GetFontResource(&resource)) && resource) {
                IDWriteFontFace5* var_face5 = nullptr;
                if (SUCCEEDED(resource->CreateFontFace(DWRITE_FONT_SIMULATIONS_NONE, dw_axes, dw_axis_count, &var_face5)) && var_face5) {
                    face = var_face5;
                }
                resource->Release();
            }
            parent_face5->Release();
        }
        if (face) {
            slot->dwrite_face = face;
            return face;
        }
    }
#endif

    IDWriteFontFile* file = create_dwrite_font_file(factory, slot);
    if (!file) return nullptr;

    BOOL supported = FALSE;
    DWRITE_FONT_FILE_TYPE file_type = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    DWRITE_FONT_FACE_TYPE face_type = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    UINT32 num_faces = 0;
    if (SUCCEEDED(file->Analyze(&supported, &file_type, &face_type, &num_faces)) && supported && num_faces > 0) {
        const UINT32 fidx = (slot->face_index >= 0 && static_cast<UINT32>(slot->face_index) < num_faces)
            ? static_cast<UINT32>(slot->face_index) : 0u;
        if (want_var_axes) {
#if defined(NTDDI_WIN10_RS3) && (NTDDI_VERSION >= NTDDI_WIN10_RS3)
            IDWriteFactory6* f6 = nullptr;
            if (SUCCEEDED(factory->QueryInterface(__uuidof(IDWriteFactory6), reinterpret_cast<void**>(&f6))) && f6) {
                IDWriteFontResource* resource = nullptr;
                if (SUCCEEDED(f6->CreateFontResource(file, fidx, &resource)) && resource) {
                    IDWriteFontFace5* var_face5 = nullptr;
                    if (SUCCEEDED(resource->CreateFontFace(DWRITE_FONT_SIMULATIONS_NONE, dw_axes, dw_axis_count, &var_face5)) && var_face5) {
                        face = var_face5;
                    }
                    resource->Release();
                }
                f6->Release();
            }
#endif
            // Only fall back to the default static face if the font has no fvar variation table;
            // otherwise return nullptr so Slug renders the exact HarfBuzz variation outline.
            if (!face && (!slot->hb_face || !hb_ot_var_has_data(slot->hb_face))) {
                IDWriteFontFile* files[1] = {file};
                factory->CreateFontFace(face_type, 1, files, fidx, DWRITE_FONT_SIMULATIONS_NONE, &face);
            }
        } else {
            IDWriteFontFile* files[1] = {file};
            factory->CreateFontFace(face_type, 1, files, fidx, DWRITE_FONT_SIMULATIONS_NONE, &face);
        }
    }
    file->Release();
    slot->dwrite_face = face;
    return face;
}
#elif defined(__APPLE__)
void release_platform_font_face(FontSlot* slot) noexcept {
    if (slot->dwrite_face) {
        CGFontRelease(static_cast<CGFontRef>(slot->dwrite_face));
        slot->dwrite_face = nullptr;
    }
}

void release_platform_font_factory(FontAtlas* a) noexcept {
    (void)a;
}

CGFontRef ensure_cg_font(FontSlot* slot) noexcept {
    if (slot->dwrite_face) return static_cast<CGFontRef>(slot->dwrite_face);
    if (slot->dwrite_probed || !slot->font_file_data || slot->font_file_size == 0u) return nullptr;
    slot->dwrite_probed = true;
    CGFontRef cg_font = nullptr;
    if (slot->face_index == 0) {
        CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr, slot->font_file_data, slot->font_file_size, nullptr);
        if (provider) {
            cg_font = CGFontCreateWithDataProvider(provider);
            CGDataProviderRelease(provider);
        }
    }
    if (!cg_font) {
        CFDataRef cf_data = CFDataCreateWithBytesNoCopy(
            kCFAllocatorDefault, slot->font_file_data, static_cast<CFIndex>(slot->font_file_size), kCFAllocatorNull);
        if (cf_data) {
            CFArrayRef descs = CTFontManagerCreateFontDescriptorsFromData(cf_data);
            if (descs) {
                const CFIndex count = CFArrayGetCount(descs);
                if (count > 0) {
                    const CFIndex idx = (slot->face_index >= 0 && static_cast<CFIndex>(slot->face_index) < count)
                        ? static_cast<CFIndex>(slot->face_index) : 0;
                    auto desc = static_cast<CTFontDescriptorRef>(CFArrayGetValueAtIndex(descs, idx));
                    CTFontRef ct = desc ? CTFontCreateWithFontDescriptor(desc, 0.0, nullptr) : nullptr;
                    if (ct) {
                        cg_font = CTFontCopyGraphicsFont(ct, nullptr);
                        CFRelease(ct);
                    }
                }
                CFRelease(descs);
            }
            CFRelease(cf_data);
        }
    }
    slot->dwrite_face = cg_font;
    return cg_font;
}
#elif defined(ETCH_USE_FREETYPE)
void release_platform_font_face(FontSlot* slot) noexcept {
    if (slot->dwrite_face) {
        FT_Done_Face(static_cast<FT_Face>(slot->dwrite_face));
        slot->dwrite_face = nullptr;
    }
}

void release_platform_font_factory(FontAtlas* a) noexcept {
    if (a->dwrite_factory) {
        FT_Done_FreeType(static_cast<FT_Library>(a->dwrite_factory));
        a->dwrite_factory = nullptr;
    }
}

FT_Library ensure_ft_library(FontAtlas* a) noexcept {
    if (a->dwrite_factory) return static_cast<FT_Library>(a->dwrite_factory);
    if (a->dwrite_probed) return nullptr;
    a->dwrite_probed = true;
    FT_Library lib = nullptr;
    if (FT_Init_FreeType(&lib) != 0 || !lib) return nullptr;
    (void)FT_Library_SetLcdFilter(lib, FT_LCD_FILTER_DEFAULT);
    a->dwrite_factory = lib;
    return lib;
}

FT_Face ensure_ft_face(FontAtlas* a, FontSlot* slot) noexcept {
    if (slot->dwrite_face) return static_cast<FT_Face>(slot->dwrite_face);
    if (slot->dwrite_probed) return nullptr;
    slot->dwrite_probed = true;
    FT_Library lib = ensure_ft_library(a);
    if (!lib) return nullptr;
    FT_Face face = nullptr;
    if (slot->font_file_data && slot->font_file_size > 0u) {
        if (FT_New_Memory_Face(lib, slot->font_file_data, static_cast<FT_Long>(slot->font_file_size),
                               static_cast<FT_Long>(slot->face_index), &face) != 0)
        {
            face = nullptr;
        }
    } else if (slot->font_file_path[0] != '\0') {
        if (FT_New_Face(lib, slot->font_file_path, static_cast<FT_Long>(slot->face_index), &face) != 0) {
            face = nullptr;
        }
    }
    if (!face) return nullptr;
    if (slot->is_variation && slot->axis_count > 0u) {
        FT_MM_Var* mm = nullptr;
        if (FT_Get_MM_Var(face, &mm) == 0 && mm) {
            const FT_UInt num_axes = mm->num_axis < 32u ? mm->num_axis : 32u;
            FT_Fixed coords[32] = {};
            for (FT_UInt k = 0u; k < num_axes; ++k) {
                coords[k] = mm->axis[k].def;
                const uint32_t axis_tag = static_cast<uint32_t>(mm->axis[k].tag);
                const uint8_t ac = slot->axis_count <= 8u ? slot->axis_count : 8u;
                for (uint8_t i = 0u; i < ac; ++i) {
                    if (slot->axes[i].tag == axis_tag) {
                        FT_Fixed val = static_cast<FT_Fixed>(std::lround(static_cast<double>(slot->axes[i].value) * 65536.0));
                        if (val < mm->axis[k].minimum) val = mm->axis[k].minimum;
                        if (val > mm->axis[k].maximum) val = mm->axis[k].maximum;
                        coords[k] = val;
                        break;
                    }
                }
            }
            (void)FT_Set_Var_Design_Coordinates(face, num_axes, coords);
            FT_Done_MM_Var(lib, mm);
        }
    }
    slot->dwrite_face = face;
    return face;
}
#else
void release_platform_font_face(FontSlot* slot) noexcept {
    (void)slot;
}

void release_platform_font_factory(FontAtlas* a) noexcept {
    (void)a;
}
#endif

#if !defined(_WIN32)
EmojiBitmapEntry rasterize_hinted_stbtt(
    Context* ctx,
    FontAtlas& a,
    const FontSlot& s,
    uint32_t idx,
    EmojiBitmapEntry entry,
    uint32_t glyph_id,
    uint32_t ppem_half,
    uint32_t phase2) noexcept
{
    if (!s.font_info.data) return {};
    if (s.is_variation && s.axis_count > 0u && s.hb_face && hb_ot_var_has_data(s.hb_face)) return {};
    const float scale = (static_cast<float>(ppem_half) * 0.5f) * s.scale_1px;
    const float shift_x = static_cast<float>(phase2) * 0.25f;
    int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
    stbtt_GetGlyphBitmapBoxSubpixel(&s.font_info, static_cast<int>(glyph_id), scale, scale, shift_x, 0.0f, &ix0, &iy0, &ix1, &iy1);
    if (ix1 <= ix0 || iy1 <= iy0) {
        a.hinted_glyphs[idx] = entry;
        ++a.hinted_count;
        return entry;
    }
    const uint32_t w = static_cast<uint32_t>(ix1 - ix0);
    const uint32_t h = static_cast<uint32_t>(iy1 - iy0);
    if (w > 128u || h > 128u) return {};
    uint32_t tx = 0u, ty = 0u;
    uint16_t shelf_idx = UINT16_MAX;
    if (!ensure_subpixel_atlas(ctx, w, h, &tx, &ty, &shelf_idx)) return {};
    if (a.subpixel_is_r8) {
        stbtt_MakeGlyphBitmapSubpixel(
            &s.font_info,
            a.subpixel_pixels + size_t(ty) * a.subpixel_w + tx,
            static_cast<int>(w), static_cast<int>(h), static_cast<int>(a.subpixel_w),
            scale, scale, shift_x, 0.0f, static_cast<int>(glyph_id));
    } else {
        const size_t gray_bytes = size_t(w) * size_t(h);
        uint8_t stack_gray[4096];
        uint8_t* gray = nullptr;
        size_t arena_mark = 0u;
        bool used_arena = false;
        bool used_heap = false;
        if (gray_bytes <= sizeof(stack_gray)) {
            gray = stack_gray;
        } else {
            arena_mark = ctx->frame_arena.mark();
            gray = ctx->frame_arena.alloc_uninit<uint8_t>(gray_bytes);
            if (gray) {
                used_arena = true;
            } else {
                gray = static_cast<uint8_t*>(std::malloc(gray_bytes));
                used_heap = (gray != nullptr);
            }
        }
        if (!gray) return {};
        stbtt_MakeGlyphBitmapSubpixel(
            &s.font_info, gray,
            static_cast<int>(w), static_cast<int>(h), static_cast<int>(w),
            scale, scale, shift_x, 0.0f, static_cast<int>(glyph_id));
        for (uint32_t row = 0u; row < h; ++row) {
            const uint8_t* src_row = gray + size_t(row) * w;
            uint8_t* dst_row = a.subpixel_pixels + (size_t(ty + row) * a.subpixel_w + tx) * 4u;
            for (uint32_t col = 0u; col < w; ++col) {
                const uint8_t g = src_row[col];
                dst_row[col * 4u + 0u] = g;
                dst_row[col * 4u + 1u] = g;
                dst_row[col * 4u + 2u] = g;
                dst_row[col * 4u + 3u] = g;
            }
        }
        if (used_arena) ctx->frame_arena.rewind(arena_mark);
        if (used_heap) std::free(gray);
    }
    a.subpixel_dirty = true;
    if (ty < a.subpixel_dirty_y0) a.subpixel_dirty_y0 = ty;
    if (ty + h > a.subpixel_dirty_y1) a.subpixel_dirty_y1 = ty + h;
    entry.tile_x = tx;
    entry.tile_y = ty;
    entry.width_px = w;
    entry.height_px = h;
    entry.bearing_x = static_cast<float>(ix0);
    entry.bearing_y_top = static_cast<float>(-iy0);
    entry.shelf_idx = shelf_idx;
    entry.has_bitmap = true;
    a.hinted_glyphs[idx] = entry;
    ++a.hinted_count;
    return entry;
}
#endif

EmojiBitmapEntry get_or_cache_hinted_glyph(Context* ctx, uint16_t slot_idx, uint32_t glyph_id, float phys_px, uint8_t x_phase = 0u) noexcept {
    if (!ctx || slot_idx >= kMaxFontSlots || glyph_id > 0xFFFFu || phys_px < 6.0f || phys_px > 24.5f) return {};
    auto& a = ctx->atlas;
    auto& s = a.slots[slot_idx];
    if (!s.loaded || !s.has_outlines) return {};
    const uint32_t ppem_half = static_cast<uint32_t>(std::round(phys_px * 2.0f));
    if (ppem_half < 12u || ppem_half > 50u) return {};
    const uint32_t phase2 = uint32_t(x_phase & 3u);
    const uint64_t key = (uint64_t(slot_idx + 1u) << 56) | (uint64_t(s.generation) << 48) |
                         (uint64_t(ppem_half) << 34) | (uint64_t(phase2) << 32) | uint64_t(glyph_id);
    const uint32_t cur_frame = ctx->active_ui ? ctx->active_ui->frame_index : 0u;
    if (!a.hinted_cap || (a.hinted_count + 1u) * 4u > a.hinted_cap * 3u) {
        if (!hinted_rehash(&a)) return {};
    }
    uint32_t idx = uint32_t(hash_u64(key)) & (a.hinted_cap - 1u);
    while (a.hinted_glyphs[idx].key && a.hinted_glyphs[idx].key != key) {
        idx = (idx + 1u) & (a.hinted_cap - 1u);
    }
    if (a.hinted_glyphs[idx].key) {
        a.hinted_glyphs[idx].last_used_frame = cur_frame;
        const uint16_t si = a.hinted_glyphs[idx].shelf_idx;
        if (si < a.hinted_shelf_count && cur_frame > a.hinted_shelves[si].last_used_frame) {
            a.hinted_shelves[si].last_used_frame = cur_frame;
        }
        return a.hinted_glyphs[idx];
    }

    EmojiBitmapEntry entry{};
    entry.key = key;
    entry.ppem = ppem_half;
    entry.last_used_frame = cur_frame;

#if defined(_WIN32)
    IDWriteFactory* factory = ensure_dwrite_factory(&a);
    IDWriteFontFace* face = ensure_dwrite_face(&a, &s);
    if (!factory || !face) {
        return {};
    }
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
    if (FAILED(hr) || !analysis) {
        return {};
    }

    RECT rc{};
    const HRESULT bounds_hr = analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &rc);
    if (FAILED(bounds_hr)) {
        analysis->Release();
        return {};
    }
    if (rc.right <= rc.left || rc.bottom <= rc.top) {
        analysis->Release();
        a.hinted_glyphs[idx] = entry;
        ++a.hinted_count;
        return entry;
    }

    const uint32_t w = static_cast<uint32_t>(rc.right - rc.left);
    const uint32_t h = static_cast<uint32_t>(rc.bottom - rc.top);
    if (w > 128u || h > 128u) {
        analysis->Release();
        return {};
    }

    const size_t rgb_bytes = size_t(w) * size_t(h) * 3u;
    uint8_t stack_rgb[4096];
    uint8_t* rgb = nullptr;
    size_t arena_mark = 0u;
    bool used_arena = false;
    bool used_heap = false;
    if (rgb_bytes <= sizeof(stack_rgb)) {
        rgb = stack_rgb;
    } else {
        arena_mark = ctx->frame_arena.mark();
        rgb = ctx->frame_arena.alloc_uninit<uint8_t>(rgb_bytes);
        if (rgb) {
            used_arena = true;
        } else {
            rgb = static_cast<uint8_t*>(std::malloc(rgb_bytes));
            used_heap = (rgb != nullptr);
        }
    }

    uint32_t tx = 0u, ty = 0u;
    uint16_t shelf_idx = UINT16_MAX;
    if (rgb &&
        SUCCEEDED(analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &rc, rgb, static_cast<UINT32>(rgb_bytes))) &&
        ensure_subpixel_atlas(ctx, w, h, &tx, &ty, &shelf_idx))
    {
        if (a.subpixel_is_r8) {
            for (uint32_t row = 0u; row < h; ++row) {
                const uint8_t* src_row = rgb + size_t(row) * w * 3u;
                uint8_t* dst_row = a.subpixel_pixels + size_t(ty + row) * a.subpixel_w + tx;
                for (uint32_t col = 0u; col < w; ++col) {
                    const uint32_t cr = src_row[col * 3u + 0u];
                    const uint32_t cg = src_row[col * 3u + 1u];
                    const uint32_t cb = src_row[col * 3u + 2u];
                    dst_row[col] = static_cast<uint8_t>((cr * 77u + cg * 150u + cb * 29u + 128u) >> 8);
                }
            }
        } else {
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
        entry.shelf_idx = shelf_idx;
        entry.has_bitmap = true;
    }
    if (used_arena) ctx->frame_arena.rewind(arena_mark);
    if (used_heap) std::free(rgb);
    analysis->Release();

    if (!entry.has_bitmap) {
        return {};
    }
    a.hinted_glyphs[idx] = entry;
    ++a.hinted_count;
    return entry;
#elif defined(__APPLE__)
    CGFontRef cg_font = ensure_cg_font(&s);
    if (!cg_font) {
        return rasterize_hinted_stbtt(ctx, a, s, idx, entry, glyph_id, ppem_half, phase2);
    }
    const float em_size = (static_cast<float>(ppem_half) * 0.5f) * s.scale_1px * static_cast<float>(s.upem);
    CTFontDescriptorRef var_desc = nullptr;
    if (s.is_variation && s.axis_count > 0u) {
        const uint8_t ac = s.axis_count <= 8u ? s.axis_count : 8u;
        CFNumberRef keys[8] = {};
        CFNumberRef vals[8] = {};
        uint8_t valid_n = 0u;
        for (uint8_t i = 0u; i < ac; ++i) {
            const int32_t tag_i32 = static_cast<int32_t>(s.axes[i].tag);
            const float axis_f32 = s.axes[i].value;
            CFNumberRef k = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &tag_i32);
            CFNumberRef v = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat32Type, &axis_f32);
            if (k && v) {
                keys[valid_n] = k;
                vals[valid_n] = v;
                ++valid_n;
            } else {
                if (k) CFRelease(k);
                if (v) CFRelease(v);
            }
        }
        if (valid_n > 0u) {
            CFDictionaryRef var_dict = CFDictionaryCreate(
                kCFAllocatorDefault,
                reinterpret_cast<const void**>(keys),
                reinterpret_cast<const void**>(vals),
                static_cast<CFIndex>(valid_n),
                &kCFTypeDictionaryKeyCallBacks,
                &kCFTypeDictionaryValueCallBacks);
            if (var_dict) {
                const void* attr_k[1] = { kCTFontVariationAttribute };
                const void* attr_v[1] = { var_dict };
                CFDictionaryRef attrs = CFDictionaryCreate(
                    kCFAllocatorDefault, attr_k, attr_v, 1,
                    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
                if (attrs) {
                    var_desc = CTFontDescriptorCreateWithAttributes(attrs);
                    CFRelease(attrs);
                }
                CFRelease(var_dict);
            }
        }
        for (uint8_t i = 0u; i < valid_n; ++i) {
            CFRelease(keys[i]);
            CFRelease(vals[i]);
        }
    }
    CTFontRef ct_font = CTFontCreateWithGraphicsFont(cg_font, static_cast<CGFloat>(em_size), nullptr, var_desc);
    if (var_desc) CFRelease(var_desc);
    if (!ct_font) {
        return rasterize_hinted_stbtt(ctx, a, s, idx, entry, glyph_id, ppem_half, phase2);
    }

    const CGGlyph cg_glyph = static_cast<CGGlyph>(glyph_id);
    const CGRect bbox = CTFontGetBoundingRectsForGlyphs(ct_font, kCTFontOrientationDefault, &cg_glyph, nullptr, 1);
    if (bbox.size.width <= 0.0 || bbox.size.height <= 0.0) {
        CFRelease(ct_font);
        a.hinted_glyphs[idx] = entry;
        ++a.hinted_count;
        return entry;
    }

    const CGFloat shift_x = static_cast<CGFloat>(phase2) * 0.25;
    const int ix0 = static_cast<int>(std::floor(bbox.origin.x + shift_x)) - 1;
    const int iy_bot = static_cast<int>(std::floor(bbox.origin.y)) - 1;
    const int ix1 = static_cast<int>(std::ceil(bbox.origin.x + shift_x + bbox.size.width)) + 1;
    const int iy_top = static_cast<int>(std::ceil(bbox.origin.y + bbox.size.height)) + 1;
    if (ix1 <= ix0 || iy_top <= iy_bot) {
        CFRelease(ct_font);
        a.hinted_glyphs[idx] = entry;
        ++a.hinted_count;
        return entry;
    }
    const uint32_t w = static_cast<uint32_t>(ix1 - ix0);
    const uint32_t h = static_cast<uint32_t>(iy_top - iy_bot);
    if (w > 128u || h > 128u) {
        CFRelease(ct_font);
        return {};
    }

    const size_t gray_bytes = size_t(w) * size_t(h);
    uint8_t stack_gray[4096];
    uint8_t* gray = nullptr;
    size_t arena_mark = 0u;
    bool used_arena = false;
    bool used_heap = false;
    if (gray_bytes <= sizeof(stack_gray)) {
        gray = stack_gray;
    } else {
        arena_mark = ctx->frame_arena.mark();
        gray = ctx->frame_arena.alloc_uninit<uint8_t>(gray_bytes);
        if (gray) {
            used_arena = true;
        } else {
            gray = static_cast<uint8_t*>(std::malloc(gray_bytes));
            used_heap = (gray != nullptr);
        }
    }

    uint32_t tx = 0u, ty = 0u;
    uint16_t shelf_idx = UINT16_MAX;
    if (gray) {
        std::memset(gray, 0, gray_bytes);
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceGray();
        CGContextRef cg = cs
            ? CGBitmapContextCreate(gray, w, h, 8, w, cs, static_cast<uint32_t>(kCGImageAlphaNone))
            : nullptr;
        if (cg && ensure_subpixel_atlas(ctx, w, h, &tx, &ty, &shelf_idx)) {
            CGContextSetAllowsAntialiasing(cg, true);
            CGContextSetShouldAntialias(cg, true);
            CGContextSetAllowsFontSmoothing(cg, true);
            CGContextSetShouldSmoothFonts(cg, true);
            CGContextSetAllowsFontSubpixelPositioning(cg, true);
            CGContextSetShouldSubpixelPositionFonts(cg, true);
            CGContextSetAllowsFontSubpixelQuantization(cg, false);
            CGContextSetShouldSubpixelQuantizeFonts(cg, false);
            CGContextSetGrayFillColor(cg, 1.0, 1.0);
            const CGPoint pos = CGPointMake(shift_x - static_cast<CGFloat>(ix0), static_cast<CGFloat>(-iy_bot));
            CTFontDrawGlyphs(ct_font, &cg_glyph, &pos, 1, cg);
            if (a.subpixel_is_r8) {
                for (uint32_t row = 0u; row < h; ++row) {
                    std::memcpy(a.subpixel_pixels + size_t(ty + row) * a.subpixel_w + tx,
                                gray + size_t(row) * w, w);
                }
            } else {
                for (uint32_t row = 0u; row < h; ++row) {
                    const uint8_t* src_row = gray + size_t(row) * w;
                    uint8_t* dst_row = a.subpixel_pixels + (size_t(ty + row) * a.subpixel_w + tx) * 4u;
                    for (uint32_t col = 0u; col < w; ++col) {
                        const uint8_t g = src_row[col];
                        dst_row[col * 4u + 0u] = g;
                        dst_row[col * 4u + 1u] = g;
                        dst_row[col * 4u + 2u] = g;
                        dst_row[col * 4u + 3u] = g;
                    }
                }
            }
            a.subpixel_dirty = true;
            if (ty < a.subpixel_dirty_y0) a.subpixel_dirty_y0 = ty;
            if (ty + h > a.subpixel_dirty_y1) a.subpixel_dirty_y1 = ty + h;
            entry.tile_x = tx;
            entry.tile_y = ty;
            entry.width_px = w;
            entry.height_px = h;
            entry.bearing_x = static_cast<float>(ix0);
            entry.bearing_y_top = static_cast<float>(iy_top);
            entry.shelf_idx = shelf_idx;
            entry.has_bitmap = true;
        }
        if (cg) CGContextRelease(cg);
        if (cs) CGColorSpaceRelease(cs);
    }
    if (used_arena) ctx->frame_arena.rewind(arena_mark);
    if (used_heap) std::free(gray);
    CFRelease(ct_font);

    if (!entry.has_bitmap) return {};
    a.hinted_glyphs[idx] = entry;
    ++a.hinted_count;
    return entry;
#elif defined(ETCH_USE_FREETYPE)
    FT_Face face = ensure_ft_face(&a, &s);
    if (!face) {
        return rasterize_hinted_stbtt(ctx, a, s, idx, entry, glyph_id, ppem_half, phase2);
    }
    const float em_size = (static_cast<float>(ppem_half) * 0.5f) * s.scale_1px * static_cast<float>(s.upem);
    const FT_F26Dot6 em_26_6 = static_cast<FT_F26Dot6>(std::lround(static_cast<double>(em_size) * 64.0));
    if (em_26_6 <= 0 || FT_Set_Char_Size(face, em_26_6, em_26_6, 72u, 72u) != 0) {
        return rasterize_hinted_stbtt(ctx, a, s, idx, entry, glyph_id, ppem_half, phase2);
    }
    FT_Vector delta = { static_cast<FT_Pos>(phase2) * 16, 0 };
    FT_Set_Transform(face, nullptr, &delta);
    const FT_Int32 load_flags = a.subpixel_is_r8
        ? (FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT)
        : (FT_LOAD_DEFAULT | FT_LOAD_TARGET_LCD);
    const FT_Render_Mode render_mode = a.subpixel_is_r8 ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_LCD;
    const FT_Error load_err = FT_Load_Glyph(face, glyph_id, load_flags);
    FT_Set_Transform(face, nullptr, nullptr);
    if (load_err != 0 || FT_Render_Glyph(face->glyph, render_mode) != 0) {
        return {};
    }
    const FT_Bitmap& bm = face->glyph->bitmap;
    if (bm.width == 0u || bm.rows == 0u || !bm.buffer) {
        a.hinted_glyphs[idx] = entry;
        ++a.hinted_count;
        return entry;
    }
    const bool lcd_mode = (!a.subpixel_is_r8 && bm.pixel_mode == FT_PIXEL_MODE_LCD);
    if (a.subpixel_is_r8 && bm.pixel_mode != FT_PIXEL_MODE_GRAY) return {};
    if (!a.subpixel_is_r8 && bm.pixel_mode != FT_PIXEL_MODE_LCD && bm.pixel_mode != FT_PIXEL_MODE_GRAY) return {};
    const uint32_t w = lcd_mode ? (bm.width / 3u) : bm.width;
    const uint32_t h = bm.rows;
    if (w == 0u || w > 128u || h > 128u) return {};
    uint32_t tx = 0u, ty = 0u;
    uint16_t shelf_idx = UINT16_MAX;
    if (!ensure_subpixel_atlas(ctx, w, h, &tx, &ty, &shelf_idx)) return {};
    const int pitch = bm.pitch;
    for (uint32_t row = 0u; row < h; ++row) {
        const uint8_t* src_row = (pitch >= 0)
            ? (bm.buffer + size_t(row) * size_t(pitch))
            : (bm.buffer + size_t(h - 1u - row) * size_t(-pitch));
        if (a.subpixel_is_r8) {
            std::memcpy(a.subpixel_pixels + size_t(ty + row) * a.subpixel_w + tx, src_row, w);
        } else if (lcd_mode) {
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
        } else {
            uint8_t* dst_row = a.subpixel_pixels + (size_t(ty + row) * a.subpixel_w + tx) * 4u;
            for (uint32_t col = 0u; col < w; ++col) {
                const uint8_t g = src_row[col];
                dst_row[col * 4u + 0u] = g;
                dst_row[col * 4u + 1u] = g;
                dst_row[col * 4u + 2u] = g;
                dst_row[col * 4u + 3u] = g;
            }
        }
    }
    a.subpixel_dirty = true;
    if (ty < a.subpixel_dirty_y0) a.subpixel_dirty_y0 = ty;
    if (ty + h > a.subpixel_dirty_y1) a.subpixel_dirty_y1 = ty + h;
    entry.tile_x = tx;
    entry.tile_y = ty;
    entry.width_px = w;
    entry.height_px = h;
    entry.bearing_x = static_cast<float>(face->glyph->bitmap_left);
    entry.bearing_y_top = static_cast<float>(face->glyph->bitmap_top);
    entry.shelf_idx = shelf_idx;
    entry.has_bitmap = true;
    a.hinted_glyphs[idx] = entry;
    ++a.hinted_count;
    return entry;
#else
    return rasterize_hinted_stbtt(ctx, a, s, idx, entry, glyph_id, ppem_half, phase2);
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

void set_lcd_subpixel_text(Context* ctx, bool enabled) noexcept {
    if (!ctx) return;
    if (ctx->atlas.lcd_subpixel_text != enabled) {
        ctx->atlas.lcd_subpixel_text = enabled;
        if (!ctx->in_frame && ctx->prim_count == 0u && ctx->filter_prim_count == 0u) {
            ctx->atlas.subpixel_is_r8 = !enabled;
            reset_hinted_atlas_cache(&ctx->atlas);
        } else {
            ctx->atlas.hinted_gc_pending = true;
        }
        request_redraw(ctx);
    }
}

bool lcd_subpixel_text(const Context* ctx) noexcept {
    return ctx ? ctx->atlas.lcd_subpixel_text : false;
}
} // namespace etch
