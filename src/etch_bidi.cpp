// CPU text itemization. Included after the directional HarfBuzz shaper.
#include "etch_bidi_data.h"
enum BidiClass : uint8_t { BiL, BiR, BiAL, BiEN, BiES, BiET, BiAN, BiCS, BiNSM, BiBN, BiB, BiS, BiWS, BiON,
    BiLRE, BiLRO, BiRLE, BiRLO, BiPDF, BiLRI, BiRLI, BiFSI, BiPDI };
uint8_t bidi_class(uint32_t cp) noexcept {
    uint32_t lo = 0, hi = ETCH_COUNTOF(kBidiRanges);
    while (lo < hi) { const uint32_t m = (lo + hi) / 2; if (cp > kBidiRanges[m].last) lo = m + 1; else hi = m; }
    return kBidiRanges[lo].cls;
}
struct BidiItem { uint32_t byte, cp; hb_script_t script; uint8_t cls, orig_cls, level; };
struct BidiRun { uint32_t first, end; hb_script_t script; uint8_t level; };
struct BidiBracketPair { uint32_t open, close; };
const BidiBracket* bidi_bracket(uint32_t cp) noexcept {
    uint32_t lo = 0, hi = ETCH_COUNTOF(kBidiBrackets);
    while (lo < hi) { const uint32_t m = (lo + hi) / 2; if (kBidiBrackets[m].cp < cp) lo = m + 1; else hi = m; }
    return lo < ETCH_COUNTOF(kBidiBrackets) && kBidiBrackets[lo].cp == cp ? &kBidiBrackets[lo] : nullptr;
}

constexpr uint32_t kShapeCacheEntryCap = 256u;
constexpr uint32_t kShapeCacheGlyphCap = 2048u;
constexpr uint32_t kShapeCacheTextCap  = 8192u;

bool shape_cache_ensure(Context* ctx) noexcept {
    auto& sc = ctx->shape_cache;
    if (sc.initialized) return true;
    for (uint32_t i = 0u; i < 2u; ++i) {
        sc.banks[i].entries = static_cast<Context::ShapeCacheEntry*>(std::calloc(kShapeCacheEntryCap, sizeof(Context::ShapeCacheEntry)));
        sc.banks[i].glyphs = static_cast<ShapedGlyph*>(std::malloc(kShapeCacheGlyphCap * sizeof(ShapedGlyph)));
        sc.banks[i].text = static_cast<char*>(std::malloc(kShapeCacheTextCap));
        if (!sc.banks[i].entries || !sc.banks[i].glyphs || !sc.banks[i].text) {
            for (uint32_t j = 0u; j <= i; ++j) {
                std::free(sc.banks[j].entries);
                std::free(sc.banks[j].glyphs);
                std::free(sc.banks[j].text);
                sc.banks[j] = {};
            }
            return false;
        }
    }
    sc.active_bank = 0u;
    sc.font_revision = ctx->atlas.font_revision;
    sc.initialized = true;
    return true;
}

void shape_cache_clear_bank(Context::ShapeCacheBank& b) noexcept {
    if (b.entries && b.entry_count > 0u) {
        std::memset(b.entries, 0, kShapeCacheEntryCap * sizeof(Context::ShapeCacheEntry));
    }
    b.entry_count = 0u;
    b.glyph_used = 0u;
    b.text_used = 0u;
}

void shape_cache_begin_frame(Context* ctx) noexcept {
    auto& sc = ctx->shape_cache;
    if (!sc.initialized) return;
    if (sc.font_revision != ctx->atlas.font_revision) {
        shape_cache_clear_bank(sc.banks[0]);
        shape_cache_clear_bank(sc.banks[1]);
        sc.font_revision = ctx->atlas.font_revision;
        return;
    }
    const auto& cur = sc.banks[sc.active_bank];
    if (cur.entry_count >= (kShapeCacheEntryCap * 3u) / 4u ||
        cur.glyph_used >= (kShapeCacheGlyphCap * 3u) / 4u ||
        cur.text_used >= (kShapeCacheTextCap * 3u) / 4u)
    {
        sc.active_bank ^= 1u;
        shape_cache_clear_bank(sc.banks[sc.active_bank]);
    }
}

bool shape_cache_find_in_bank(
    const Context::ShapeCacheBank& b,
    uint64_t hash,
    uint16_t slot,
    uint32_t font_rev,
    const char* utf8,
    uint32_t byte_len,
    std::span<const ShapedGlyph>* out_span,
    bool* out_rtl) noexcept
{
    if (!b.entries || b.entry_count == 0u) return false;
    constexpr uint32_t mask = kShapeCacheEntryCap - 1u;
    uint32_t idx = static_cast<uint32_t>(hash) & mask;
    for (uint32_t probe = 0u; probe < 16u; ++probe) {
        const auto& e = b.entries[idx];
        if (!e.used) return false;
        if (e.hash == hash && e.font_slot == slot && e.font_revision == font_rev &&
            e.text_len == byte_len && std::memcmp(b.text + e.text_off, utf8, byte_len) == 0)
        {
            *out_span = std::span<const ShapedGlyph>(b.glyphs + e.glyph_off, e.glyph_count);
            if (out_rtl) *out_rtl = e.is_rtl != 0u;
            return true;
        }
        idx = (idx + 1u) & mask;
    }
    return false;
}

std::span<const ShapedGlyph> shape_cache_insert(
    Context::ShapeCacheBank& b,
    uint64_t hash,
    uint16_t slot,
    uint32_t font_rev,
    const char* utf8,
    uint32_t byte_len,
    std::span<const ShapedGlyph> glyphs,
    bool is_rtl) noexcept
{
    const uint32_t gcount = static_cast<uint32_t>(glyphs.size());
    if (!b.entries || b.entry_count >= (kShapeCacheEntryCap * 7u) / 8u ||
        b.glyph_used + gcount > kShapeCacheGlyphCap ||
        b.text_used + byte_len > kShapeCacheTextCap)
    {
        return glyphs;
    }
    constexpr uint32_t mask = kShapeCacheEntryCap - 1u;
    uint32_t idx = static_cast<uint32_t>(hash) & mask;
    for (uint32_t probe = 0u; probe < 16u; ++probe) {
        auto& e = b.entries[idx];
        if (!e.used) {
            const uint32_t toff = b.text_used;
            const uint32_t goff = b.glyph_used;
            std::memcpy(b.text + toff, utf8, byte_len);
            if (gcount > 0u) std::memcpy(b.glyphs + goff, glyphs.data(), size_t(gcount) * sizeof(ShapedGlyph));
            b.text_used += byte_len;
            b.glyph_used += gcount;
            e = Context::ShapeCacheEntry{
                .hash = hash,
                .text_off = toff,
                .text_len = byte_len,
                .glyph_off = goff,
                .glyph_count = gcount,
                .font_revision = font_rev,
                .font_slot = slot,
                .is_rtl = static_cast<uint8_t>(is_rtl),
                .used = 1u,
            };
            ++b.entry_count;
            return std::span<const ShapedGlyph>(b.glyphs + goff, gcount);
        }
        idx = (idx + 1u) & mask;
    }
    return glyphs;
}

std::span<const ShapedGlyph> shape_utf8_run_uncached(Context* ctx, uint16_t font_id, const char* utf8, uint32_t byte_len, bool* out_is_rtl) noexcept {
    bool ascii = true;
    for (uint32_t i = 0; i < byte_len; ++i) if (static_cast<uint8_t>(utf8[i]) >= 128u) { ascii = false; break; }
    if (ascii) return shape_directional_run(ctx, font_id, utf8, byte_len, out_is_rtl, HB_DIRECTION_LTR, HB_SCRIPT_LATIN);
    auto* items = ctx->frame_arena.alloc_uninit<BidiItem>(byte_len);
    auto* runs = ctx->frame_arena.alloc_uninit<BidiRun>(byte_len);
    if (!items || !runs) return {};
    const char* p = utf8; uint32_t n = 0;
    while (p < utf8 + byte_len) {
        const uint32_t off = static_cast<uint32_t>(p - utf8), cp = next_utf8_codepoint(p, utf8 + byte_len);
        const uint8_t cls = bidi_class(cp);
        items[n++] = {off, cp, hb_unicode_script(hb_unicode_funcs_get_default(), cp), cls, cls, 0};
    }
    uint8_t base = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (items[i].cls == BiL) break;
        if (items[i].cls == BiR || items[i].cls == BiAL) { base = 1; break; }
    }
    struct Embedding { uint8_t level, override_cls; bool isolate; };
    Embedding stack[126] = {}; uint32_t depth = 1; stack[0] = {base, BiON, false};
    for (uint32_t i = 0; i < n; ++i) {
        auto& it = items[i]; const uint8_t c = it.cls; it.level = stack[depth - 1].level;
        if (c == BiLRE || c == BiRLE || c == BiLRO || c == BiRLO || c == BiLRI || c == BiRLI || c == BiFSI) {
            bool right = c == BiRLE || c == BiRLO || c == BiRLI;
            if (c == BiFSI) {
                for (uint32_t j = i + 1; j < n && items[j].cls != BiPDI; ++j) {
                    if (items[j].cls == BiL) break;
                    if (items[j].cls == BiR || items[j].cls == BiAL) { right = true; break; }
                }
            }
            const uint8_t next = static_cast<uint8_t>(right ? ((it.level + 1u) | 1u) : ((it.level + 2u) & 254u));
            if (next <= 125 && depth < 126) stack[depth++] = {next, static_cast<uint8_t>(c == BiLRO ? BiL : c == BiRLO ? BiR : BiON), c >= BiLRI};
            it.cls = BiBN;
        } else if (c == BiPDF) { if (depth > 1 && !stack[depth - 1].isolate) --depth; it.cls = BiBN; }
        else if (c == BiPDI) {
            while (depth > 1 && !stack[depth - 1].isolate) --depth;
            if (depth > 1) --depth;
            it.level = stack[depth - 1].level; it.cls = BiBN;
        } else if (stack[depth - 1].override_cls != BiON) it.cls = stack[depth - 1].override_cls;
    }
    // W1-W7, N0-N2 and I1-I2 within embedding level runs.
    for (uint32_t start = 0; start < n;) {
        uint32_t end = start + 1; while (end < n && items[end].level == items[start].level) ++end;
        const uint8_t embedding = (items[start].level & 1u) ? BiR : BiL;
        uint8_t prev = embedding, strong = embedding;
        for (uint32_t i = start; i < end; ++i) {
            auto& c = items[i].cls;
            if (c == BiBN) continue;
            if (c == BiNSM) c = prev;
            if (c == BiEN && strong == BiAL) c = BiAN;
            if (c == BiL || c == BiR || c == BiAL) strong = c;
            prev = c;
            if (c == BiAL) c = BiR;
        }
        for (uint32_t i = start + 1; i + 1 < end; ++i) {
            auto& c = items[i].cls; const uint8_t a = items[i - 1].cls, b = items[i + 1].cls;
            if (c == BiES && a == BiEN && b == BiEN) c = BiEN;
            if (c == BiCS && a == b && (a == BiEN || a == BiAN)) c = a;
        }
        for (uint32_t i = start; i < end;) {
            if (items[i].cls != BiET) { ++i; continue; }
            uint32_t j = i + 1; while (j < end && items[j].cls == BiET) ++j;
            if ((i > start && items[i - 1].cls == BiEN) || (j < end && items[j].cls == BiEN))
                for (uint32_t k = i; k < j; ++k) items[k].cls = BiEN;
            i = j;
        }
        strong = embedding;
        for (uint32_t i = start; i < end; ++i) {
            auto& c = items[i].cls;
            if (c == BiES || c == BiET || c == BiCS) c = BiON;
            if (c == BiL || c == BiR) strong = c;
            else if (c == BiEN && strong == BiL) c = BiL;
        }
        // N0 / BD16: collect bracket pairs and resolve in ascending order of opening bracket position.
        uint32_t bracket_stack[63] = {}; uint32_t bracket_depth = 0;
        BidiBracketPair pairs[64] = {}; uint32_t pair_count = 0;
        for (uint32_t i = start; i < end; ++i) {
            if (items[i].cls != BiON) continue;
            const auto* bracket = bidi_bracket(items[i].cp);
            if (!bracket) continue;
            if (bracket->open) {
                if (bracket_depth == 63) break;
                bracket_stack[bracket_depth++] = i;
                continue;
            }
            uint32_t match = bracket_depth;
            while (match && items[bracket_stack[match - 1]].cp != bracket->pair) --match;
            if (!match) continue;
            const uint32_t open = bracket_stack[match - 1]; bracket_depth = match - 1;
            if (pair_count < 64u) pairs[pair_count++] = {open, i};
        }
        for (uint32_t a = 1u; a < pair_count; ++a) {
            const BidiBracketPair key = pairs[a];
            uint32_t b = a;
            while (b > 0u && pairs[b - 1u].open > key.open) { pairs[b] = pairs[b - 1u]; --b; }
            pairs[b] = key;
        }
        for (uint32_t pi = 0u; pi < pair_count; ++pi) {
            const uint32_t open = pairs[pi].open, close = pairs[pi].close;
            bool same = false, opposite = false;
            for (uint32_t k = open + 1; k < close; ++k) {
                uint8_t c = items[k].cls; if (c == BiEN || c == BiAN) c = BiR;
                same |= c == embedding; opposite |= c == (embedding == BiL ? BiR : BiL);
            }
            uint8_t resolved = embedding;
            if (!same && !opposite) continue;
            if (!same) {
                for (uint32_t k = open; k > start;) {
                    uint8_t c = items[--k].cls; if (c == BiEN || c == BiAN) c = BiR;
                    if (c == BiL || c == BiR) { resolved = c; break; }
                }
            }
            items[open].cls = resolved; items[close].cls = resolved;
            for (uint32_t k = open + 1; k < end && items[k].orig_cls == BiNSM; ++k) items[k].cls = resolved;
            for (uint32_t k = close + 1; k < end && items[k].orig_cls == BiNSM; ++k) items[k].cls = resolved;
        }
        for (uint32_t i = start; i < end;) {
            if (items[i].cls == BiL || items[i].cls == BiR || items[i].cls == BiEN || items[i].cls == BiAN) { ++i; continue; }
            uint32_t j = i + 1;
            while (j < end && items[j].cls != BiL && items[j].cls != BiR && items[j].cls != BiEN && items[j].cls != BiAN) ++j;
            uint8_t a = i > start ? items[i - 1].cls : embedding, b = j < end ? items[j].cls : embedding;
            if (a == BiEN || a == BiAN) a = BiR;
            if (b == BiEN || b == BiAN) b = BiR;
            for (uint32_t k = i; k < j; ++k) items[k].cls = a == b ? a : embedding;
            i = j;
        }
        for (uint32_t i = start; i < end; ++i) {
            auto& it = items[i];
            if (it.level & 1u) { if (it.cls == BiL || it.cls == BiEN || it.cls == BiAN) ++it.level; }
            else { if (it.cls == BiR) ++it.level; else if (it.cls == BiEN || it.cls == BiAN) it.level += 2; }
        }
        start = end;
    }
    // UAX#9 Rule L1: reset level of separators and preceding/trailing whitespace & formatting to paragraph base level.
    auto is_l1_resettable = [](uint8_t oc) noexcept -> bool {
        return oc == BiWS || oc == BiFSI || oc == BiLRI || oc == BiRLI || oc == BiPDI ||
               oc == BiLRE || oc == BiRLE || oc == BiLRO || oc == BiRLO || oc == BiPDF || oc == BiBN;
    };
    for (uint32_t i = 0; i < n; ++i) {
        if (items[i].orig_cls == BiB || items[i].orig_cls == BiS) {
            items[i].level = base;
            for (uint32_t k = i; k > 0u && is_l1_resettable(items[k - 1u].orig_cls); --k) {
                items[k - 1u].level = base;
            }
        }
    }
    for (uint32_t k = n; k > 0u && is_l1_resettable(items[k - 1u].orig_cls); --k) {
        items[k - 1u].level = base;
    }
    hb_script_t inherited = HB_SCRIPT_LATIN;
    for (uint32_t i = 0; i < n; ++i) if (items[i].script != HB_SCRIPT_COMMON && items[i].script != HB_SCRIPT_INHERITED && items[i].script != HB_SCRIPT_UNKNOWN) { inherited = items[i].script; break; }
    uint32_t script_brackets[32]={}, script_depth=0;
    for (uint32_t i=0;i<n;++i) {
        auto& it=items[i];
        if (const auto* b=bidi_bracket(it.cp)) {
            if (b->open) { if(script_depth<32) script_brackets[script_depth++]=i; }
            else {
                uint32_t match=script_depth;
                while(match && items[script_brackets[match-1]].cp!=b->pair) --match;
                if(match) {
                    const uint32_t open=script_brackets[match-1];script_depth=match-1;
                    hb_script_t script=inherited;
                    for(uint32_t k=open+1;k<i;++k) if(items[k].script!=HB_SCRIPT_COMMON && items[k].script!=HB_SCRIPT_INHERITED && items[k].script!=HB_SCRIPT_UNKNOWN) {script=items[k].script;break;}
                    items[open].script=it.script=script;
                }
            }
        }
    }
    uint32_t nr = 0; uint8_t max_level = base;
    for (uint32_t i = 0; i < n; ++i) {
        auto& it = items[i];
        if (it.script == HB_SCRIPT_COMMON || it.script == HB_SCRIPT_INHERITED || it.script == HB_SCRIPT_UNKNOWN) it.script = inherited;
        else inherited = it.script;
        const uint32_t end = i + 1 < n ? items[i + 1].byte : byte_len;
        if (nr && runs[nr - 1].level == it.level && runs[nr - 1].script == it.script) runs[nr - 1].end = end;
        else runs[nr++] = {it.byte, end, it.script, it.level};
        if (it.level > max_level) max_level = it.level;
    }
    for (int l = max_level; l >= 1; --l) for (uint32_t i = 0; i < nr;) {
        if (runs[i].level < l) { ++i; continue; }
        uint32_t j = i + 1; while (j < nr && runs[j].level >= l) ++j;
        for (uint32_t a = i, b = j - 1; a < b; ++a, --b) { const auto t = runs[a]; runs[a] = runs[b]; runs[b] = t; }
        i = j;
    }
    auto* spans = ctx->frame_arena.alloc_uninit<std::span<const ShapedGlyph>>(nr);
    if (!spans) return {};
    uint32_t count = 0;
    for (uint32_t i = 0; i < nr; ++i) {
        const auto& r = runs[i]; const bool rtl = (r.level & 1u) != 0;
        if (out_is_rtl && rtl) *out_is_rtl = true;
        spans[i] = shape_directional_run(ctx, font_id, utf8, byte_len, nullptr, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR, r.script, r.first, r.end);
        count += static_cast<uint32_t>(spans[i].size());
    }
    auto* out = ctx->frame_arena.alloc_uninit<ShapedGlyph>(count);
    if (!out) return {};
    uint32_t k = 0;
    for (uint32_t i = 0; i < nr; ++i) for (const auto& g : spans[i]) {
        out[k++] = g;
    }
    return {out, count};
}

uint64_t compute_active_ot_feature_hash(const Context* ctx) noexcept {
    uint64_t h = 0u;
    if (ctx->active_ot_flags != 0u) {
        h = hash_u64(static_cast<uint64_t>(ctx->active_ot_flags) | 0xA500000000ULL);
    }
    if (ctx->active_ot_features && ctx->active_ot_feature_count > 0u) {
        uint64_t fh = 14695981039346656037ULL ^ h;
        for (uint32_t i = 0u; i < ctx->active_ot_feature_count; ++i) {
            const uint64_t packed =
                (static_cast<uint64_t>(ctx->active_ot_features[i].tag) << 32) |
                static_cast<uint64_t>(ctx->active_ot_features[i].value);
            fh ^= packed;
            fh *= 1099511628211ULL;
        }
        h = hash_u64(fh);
        if (h == 0u) h = 1u;
    }
    return h;
}

std::span<const ShapedGlyph> shape_utf8_run(Context* ctx, uint16_t font_id, const char* utf8, uint32_t byte_len, bool* out_is_rtl = nullptr) noexcept {
    if (out_is_rtl) *out_is_rtl = false;
    if (!byte_len || !ctx->atlas.font_valid) return {};
    if (byte_len <= 1024u && shape_cache_ensure(ctx)) {
        const uint16_t slot = resolve_font_slot_idx(&ctx->atlas, font_id);
        const uint32_t font_rev = ctx->atlas.font_revision;
        const uint64_t ot_feature_hash = compute_active_ot_feature_hash(ctx);
        const uint64_t h = hash_u64(
            fnv1a_64(utf8, byte_len) ^
            (uint64_t(slot) * 0x9E3779B97F4A7C15ULL) ^
            (uint64_t(font_rev) * 0xBF58476D1CE4E5B9ULL) ^
            (ot_feature_hash * 0x94D049BB133111EBULL));
        auto& cur = ctx->shape_cache.banks[ctx->shape_cache.active_bank];
        auto& prev = ctx->shape_cache.banks[ctx->shape_cache.active_bank ^ 1u];
        std::span<const ShapedGlyph> hit = {};
        bool hit_rtl = false;
        if (shape_cache_find_in_bank(cur, h, slot, font_rev, utf8, byte_len, &hit, &hit_rtl)) {
            if (out_is_rtl) *out_is_rtl = hit_rtl;
            return hit;
        }
        if (shape_cache_find_in_bank(prev, h, slot, font_rev, utf8, byte_len, &hit, &hit_rtl)) {
            if (out_is_rtl) *out_is_rtl = hit_rtl;
            return shape_cache_insert(cur, h, slot, font_rev, utf8, byte_len, hit, hit_rtl);
        }
        const size_t arena_mark = ctx->frame_arena.mark();
        bool shaped_rtl = false;
        const std::span<const ShapedGlyph> shaped = shape_utf8_run_uncached(ctx, font_id, utf8, byte_len, &shaped_rtl);
        if (out_is_rtl) *out_is_rtl = shaped_rtl;
        // Re-check font_revision in case fallback fonts were lazily loaded during shaping.
        const uint32_t post_rev = ctx->atlas.font_revision;
        if (post_rev == font_rev && !shaped.empty()) {
            const std::span<const ShapedGlyph> cached = shape_cache_insert(cur, h, slot, post_rev, utf8, byte_len, shaped, shaped_rtl);
            if (cached.data() != shaped.data()) {
                ctx->frame_arena.rewind(arena_mark);
            }
            return cached;
        }
        return shaped;
    }
    return shape_utf8_run_uncached(ctx, font_id, utf8, byte_len, out_is_rtl);
}
