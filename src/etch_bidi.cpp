// CPU text itemization. Included after the directional HarfBuzz shaper.
#include "etch_bidi_data.h"
enum BidiClass : uint8_t { BiL, BiR, BiAL, BiEN, BiES, BiET, BiAN, BiCS, BiNSM, BiBN, BiB, BiS, BiWS, BiON,
    BiLRE, BiLRO, BiRLE, BiRLO, BiPDF, BiLRI, BiRLI, BiFSI, BiPDI };
uint8_t bidi_class(uint32_t cp) noexcept {
    uint32_t lo = 0, hi = ETCH_COUNTOF(kBidiRanges);
    while (lo < hi) { const uint32_t m = (lo + hi) / 2; if (cp > kBidiRanges[m].last) lo = m + 1; else hi = m; }
    return kBidiRanges[lo].cls;
}
struct BidiItem { uint32_t byte, cp; hb_script_t script; uint8_t cls, level; };
struct BidiRun { uint32_t first, end; hb_script_t script; uint8_t level; };
const BidiBracket* bidi_bracket(uint32_t cp) noexcept {
    uint32_t lo = 0, hi = ETCH_COUNTOF(kBidiBrackets);
    while (lo < hi) { const uint32_t m = (lo + hi) / 2; if (kBidiBrackets[m].cp < cp) lo = m + 1; else hi = m; }
    return lo < ETCH_COUNTOF(kBidiBrackets) && kBidiBrackets[lo].cp == cp ? &kBidiBrackets[lo] : nullptr;
}
std::span<const ShapedGlyph> shape_utf8_run(Context* ctx, uint16_t font_id, const char* utf8, uint32_t byte_len, bool* out_is_rtl = nullptr) noexcept {
    if (out_is_rtl) *out_is_rtl = false;
    if (!byte_len || !ctx->atlas.font_valid) return {};
    bool ascii = true;
    for (uint32_t i = 0; i < byte_len; ++i) if (static_cast<uint8_t>(utf8[i]) >= 128u) { ascii = false; break; }
    if (ascii) return shape_directional_run(ctx, font_id, utf8, byte_len, out_is_rtl, HB_DIRECTION_LTR, HB_SCRIPT_LATIN);
    auto* items = ctx->frame_arena.alloc_uninit<BidiItem>(byte_len);
    auto* runs = ctx->frame_arena.alloc_uninit<BidiRun>(byte_len);
    if (!items || !runs) return {};
    const char* p = utf8; uint32_t n = 0;
    while (p < utf8 + byte_len) {
        const uint32_t off = static_cast<uint32_t>(p - utf8), cp = next_utf8_codepoint(p, utf8 + byte_len);
        items[n++] = {off, cp, hb_unicode_script(hb_unicode_funcs_get_default(), cp), bidi_class(cp), 0};
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
    // W1-W7, N1-N2 and I1-I2 within embedding level runs.
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
        // N0: paired brackets take the enclosed strong direction when appropriate.
        uint32_t bracket_stack[63] = {}; uint32_t bracket_depth = 0;
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
            bool same = false, opposite = false;
            for (uint32_t k = open + 1; k < i; ++k) {
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
            items[open].cls = resolved; items[i].cls = resolved;
            for (uint32_t k = open + 1; k < end && bidi_class(items[k].cp) == BiNSM; ++k) items[k].cls = resolved;
            for (uint32_t k = i + 1; k < end && bidi_class(items[k].cp) == BiNSM; ++k) items[k].cls = resolved;
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
    hb_script_t inherited = HB_SCRIPT_LATIN;
    for (uint32_t i = 0; i < n; ++i) if (items[i].script != HB_SCRIPT_COMMON && items[i].script != HB_SCRIPT_INHERITED && items[i].script != HB_SCRIPT_UNKNOWN) { inherited = items[i].script; break; }
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
