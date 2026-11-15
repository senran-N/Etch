// UTF-8 grapheme/word segmentation and single-line/undo text input.
// Compiled via src/etch_unity.cpp.

namespace etch {

#include "etch_grapheme_data.h"
uint8_t grapheme_property(uint32_t cp, const GraphemeRange* ranges, uint32_t count) noexcept {
    uint32_t lo=0,hi=count;
    while(lo<hi){const uint32_t m=(lo+hi)/2;if(ranges[m].last<cp)lo=m+1;else hi=m;}
    return lo<count && ranges[lo].first<=cp ? ranges[lo].prop : 0;
}
uint32_t utf8_next_grapheme(const char* s, uint32_t len, uint32_t pos) noexcept {
    if(pos>=len)return len;
    const uint32_t first=utf8_decode_at(s,len,pos);
    uint8_t prev=grapheme_property(first,kGraphemeRanges,ETCH_COUNTOF(kGraphemeRanges));
    uint32_t end=utf8_next_boundary(s,len,pos),ri=prev==6?1u:0u;
    bool pict=grapheme_property(first,kPictographicRanges,ETCH_COUNTOF(kPictographicRanges))!=0,zwj_pict=false;
    while(end<len) {
        const uint32_t cp=utf8_decode_at(s,len,end);
        const uint8_t cur=grapheme_property(cp,kGraphemeRanges,ETCH_COUNTOF(kGraphemeRanges));
        const bool ep=grapheme_property(cp,kPictographicRanges,ETCH_COUNTOF(kPictographicRanges))!=0;
        const bool control=(prev>=1&&prev<=3)||(cur>=1&&cur<=3);
        const bool join=(prev==1&&cur==2)||(!control&&(
            (prev==9&&(cur==9||cur==10||cur==12||cur==13))||
            ((prev==10||prev==12)&&(cur==10||cur==11))||((prev==11||prev==13)&&cur==11)||
            cur==4||cur==5||cur==8||prev==7||(prev==5&&zwj_pict&&ep)||(prev==6&&cur==6&&(ri&1u))));
        if(!join)break;
        if(cur==5)zwj_pict=pict;else if(cur!=4)pict=ep;
        ri=cur==6?ri+1:0;prev=cur;end=utf8_next_boundary(s,len,end);
    }
    return end;
}
uint32_t utf8_prev_grapheme(const char* s, uint32_t len, uint32_t pos) noexcept {
    uint32_t cur = 0;
    while (cur < pos) { const uint32_t next = utf8_next_grapheme(s, len, cur); if (next >= pos) return cur; cur = next; }
    return cur;
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
        const uint32_t prev = utf8_prev_grapheme(s, len, cur);
        if (utf8_char_class(utf8_decode_at(s, len, prev)) != 0u) {
            break;
        }
        cur = prev;
    }
    if (cur == 0u) {
        return 0u;
    }
    const uint32_t first_prev = utf8_prev_grapheme(s, len, cur);
    const uint8_t target_cls = utf8_char_class(utf8_decode_at(s, len, first_prev));
    cur = first_prev;
    while (cur > 0u) {
        const uint32_t prev = utf8_prev_grapheme(s, len, cur);
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
        cur = utf8_next_grapheme(s, len, cur);
    }
    if (cur >= len) {
        return len;
    }
    const uint8_t target_cls = utf8_char_class(utf8_decode_at(s, len, cur));
    while (cur < len && utf8_char_class(utf8_decode_at(s, len, cur)) == target_cls) {
        cur = utf8_next_grapheme(s, len, cur);
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
        pos = utf8_prev_grapheme(s, len, pos);
    }
    const uint32_t cp = utf8_decode_at(s, len, pos);
    if (cp == '\n') {
        *out_start = pos;
        *out_end = utf8_next_grapheme(s, len, pos);
        return;
    }
    const uint8_t cls = utf8_char_class(cp);
    uint32_t a = pos;
    while (a > 0u) {
        const uint32_t prev = utf8_prev_grapheme(s, len, a);
        const uint32_t pcp = utf8_decode_at(s, len, prev);
        if (pcp == '\n' || utf8_char_class(pcp) != cls) {
            break;
        }
        a = prev;
    }
    uint32_t b = utf8_next_grapheme(s, len, pos);
    while (b < len) {
        const uint32_t ncp = utf8_decode_at(s, len, b);
        if (ncp == '\n' || utf8_char_class(ncp) != cls) {
            break;
        }
        b = utf8_next_grapheme(s, len, b);
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
    if (!ctx || ctx->active_ui->pending_text_op_count >= kMaxTextOpsPerFrame) {
        return;
    }
    if (utf8.chars && utf8.length > 0u) {
        const uint32_t avail = kMaxPendingTextBytes - ctx->active_ui->pending_text_utf8_len;
        const uint32_t max_copy = (utf8.length < avail) ? utf8.length : avail;
        const uint32_t copy_len = utf8_clamp_boundary(utf8.chars, utf8.length, max_copy);
        if (copy_len == 0u) {
            return;
        }
        op.text_offset = static_cast<uint16_t>(ctx->active_ui->pending_text_utf8_len);
        op.text_len = static_cast<uint16_t>(copy_len);
        std::memcpy(ctx->active_ui->pending_text_utf8 + ctx->active_ui->pending_text_utf8_len, utf8.chars, copy_len);
        ctx->active_ui->pending_text_utf8_len += copy_len;
    }
    ctx->active_ui->pending_text_ops[ctx->active_ui->pending_text_op_count++] = op;
}

void clear_ime_composition(Context* ctx) noexcept {
    if (!ctx) {
        return;
    }
    ctx->active_ui->ime_comp_len = 0u;
    ctx->active_ui->ime_comp_utf8[0] = '\0';
    ctx->active_ui->ime_comp_cursor_byte = 0u;
    ctx->active_ui->ime_comp_sel_len_byte = 0u;
}

void cancel_ime_composition(Context* ctx) noexcept {
    clear_ime_composition(ctx);
    if (ctx && !ctx->headless && ctx->active_window) {
        SDL_ClearComposition(ctx->active_window);
    }
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
    if (!desc.multiline) {
        measure_line_segment_x(
            ctx, desc.font_id, text.chars, text.length, clamped, 0u,
            base_px, desc.letter_spacing, desc.password, false, &out.x, nullptr, nullptr);
        return out;
    }

    uint32_t line_start = 0u;
    for (;;) {
        uint32_t line_end = line_start;
        while (line_end < text.length && text.chars[line_end] != '\n') {
            ++line_end;
        }
        if (clamped <= line_end) {
            measure_line_segment_x(
                ctx, desc.font_id, text.chars + line_start, line_end - line_start, clamped - line_start, 0u,
                base_px, desc.letter_spacing, desc.password, false, &out.x, nullptr, nullptr);
            return out;
        }
        out.y += line_adv;
        ++out.line_idx;
        line_start = line_end + 1u;
    }
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

    uint32_t line_start = 0u;
    uint32_t line_end = text.length;
    if (desc.multiline) {
        uint32_t cur_line = 0u;
        while (cur_line < target_line && line_start < text.length) {
            if (text.chars[line_start] == '\n') {
                ++cur_line;
            }
            ++line_start;
        }
        if (cur_line < target_line) {
            return text.length;
        }
        line_end = line_start;
        while (line_end < text.length && text.chars[line_end] != '\n') {
            ++line_end;
        }
    }

    const char* seg = text.chars + line_start;
    const uint32_t seg_len = line_end - line_start;
    float pen_x = 0.0f;

    const uint16_t slot_idx = (ctx && ctx->atlas.font_valid) ? resolve_font_slot_idx(&ctx->atlas, desc.font_id) : kInvalidFont;
    if (!ctx || !ctx->atlas.font_valid || slot_idx == kInvalidFont || desc.password) {
        const float cp_adv = (!ctx || !ctx->atlas.font_valid || slot_idx == kInvalidFont)
            ? (base_px * 0.55f)
            : (ctx->atlas.slots[slot_idx].bullet_advance_em * base_px);
        const char* p = seg;
        const char* end = seg + seg_len;
        while (p < end) {
            const char* cp_start = p;
            const uint32_t cp = next_utf8_codepoint(p, end);
            const float adv = ((cp == '\t' && !desc.password) ? 4.0f * cp_adv : cp_adv) + desc.letter_spacing;
            if (local_x < pen_x + 0.5f * adv) {
                return line_start + static_cast<uint32_t>(cp_start - seg);
            }
            pen_x += adv;
        }
        return line_end;
    }

    const FontSlot& primary = ctx->atlas.slots[slot_idx];
    bool last_run_rtl = false;
    uint32_t last_run_start = 0u;
    uint32_t pos = 0u;
    while (pos < seg_len) {
        if (seg[pos] == '\t') {
            const float adv = 4.0f * primary.space_advance_em * base_px + desc.letter_spacing;
            if (local_x < pen_x + 0.5f * adv) {
                return line_start + pos;
            }
            pen_x += adv;
            last_run_rtl = false;
            last_run_start = pos;
            ++pos;
            continue;
        }
        uint32_t run_end = pos;
        while (run_end < seg_len && seg[run_end] != '\t') {
            ++run_end;
        }
        const uint32_t run_len = run_end - pos;
        bool is_rtl = false;
        const size_t run_mark = ctx->frame_arena.mark();
        const std::span<const ShapedGlyph> glyphs = shape_utf8_run(ctx, slot_idx, seg + pos, run_len, &is_rtl);
        const uint32_t gcount = static_cast<uint32_t>(glyphs.size());
        uint32_t gi = 0u;
        while (gi < gcount) {
            const uint32_t c0 = glyphs[gi].cluster;
            uint32_t gi_end = gi + 1u;
            while (gi_end < gcount && glyphs[gi_end].cluster == c0) {
                ++gi_end;
            }
            float cluster_adv = 0.0f;
            for (uint32_t k = gi; k < gi_end; ++k) {
                cluster_adv += glyphs[k].x_advance_1px * base_px + desc.letter_spacing;
            }
            is_rtl = glyphs[gi].is_rtl != 0;
            const uint32_t c1 = glyphs[gi].cluster_end;
            if (local_x < pen_x + cluster_adv && c1 > c0) {
                const uint32_t n_cp = count_utf8_cps(seg + pos + c0, c1 - c0);
                if (is_rtl) {
                    const float rel_x = local_x - pen_x;
                    const uint32_t slot = (cluster_adv > 0.0f && rel_x > 0.0f)
                        ? static_cast<uint32_t>(std::floor((rel_x / cluster_adv) * static_cast<float>(n_cp) + 0.5f))
                        : 0u;
                    const uint32_t clamped_slot = (slot < n_cp) ? slot : n_cp;
                    const uint32_t target_k = n_cp - clamped_slot;
                    const char* cp_p = seg + pos + c0;
                    const char* cl_end = seg + pos + c1;
                    for (uint32_t k = 0u; k < target_k && cp_p < cl_end; ++k) {
                        (void)next_utf8_codepoint(cp_p, cl_end);
                    }
                    ctx->frame_arena.rewind(run_mark);
                    return line_start + static_cast<uint32_t>(cp_p - seg);
                }
                const char* cp_p = seg + pos + c0;
                const char* cl_end = seg + pos + c1;
                for (uint32_t k = 0u; k < n_cp && cp_p < cl_end; ++k) {
                    const uint32_t cp_off = static_cast<uint32_t>(cp_p - seg);
                    (void)next_utf8_codepoint(cp_p, cl_end);
                    const float mid = pen_x + ((static_cast<float>(k) + 0.5f) / static_cast<float>(n_cp)) * cluster_adv;
                    if (local_x < mid) {
                        ctx->frame_arena.rewind(run_mark);
                        return line_start + cp_off;
                    }
                }
                ctx->frame_arena.rewind(run_mark);
                return line_start + pos + c1;
            }
            pen_x += cluster_adv;
            gi = gi_end;
        }
        ctx->frame_arena.rewind(run_mark);
        last_run_rtl = is_rtl;
        last_run_start = pos;
        pos = run_end;
    }
    return last_run_rtl ? (line_start + last_run_start) : line_end;
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
    float cx = 0.0f;
    float cy = 0.0f;
    float cw = 0.0f;
    float caret_dx = 0.0f;

    if (composing && ctx && ctx->active_ui->ime_comp_len > 0u) {
        const auto walk_slice = [&](const char* beg, uint32_t slice_len, bool mask) noexcept {
            uint32_t pos = 0u;
            while (pos <= slice_len) {
                uint32_t seg_end = pos;
                while (seg_end < slice_len && !(desc.multiline && beg[seg_end] == '\n')) {
                    ++seg_end;
                }
                if (seg_end > pos) {
                    float seg_w = 0.0f;
                    measure_line_segment_x(
                        ctx, desc.font_id, beg + pos, seg_end - pos, 0u, 0u,
                        base_px, desc.letter_spacing, mask, false, nullptr, nullptr, &seg_w);
                    pen_x += seg_w;
                }
                if (pen_x > max_w) {
                    max_w = pen_x;
                }
                if (seg_end >= slice_len) {
                    break;
                }
                pen_x = 0.0f;
                pen_y += line_adv;
                pos = seg_end + 1u;
            }
        };

        walk_slice(s, c_byte, desc.password);
        cx = pen_x;
        cy = pen_y;
        measure_line_segment_x(
            ctx, desc.font_id, ctx->active_ui->ime_comp_utf8, ctx->active_ui->ime_comp_len, ctx->active_ui->ime_comp_cursor_byte, 0u,
            base_px, desc.letter_spacing, false, false, &caret_dx, nullptr, &cw);
        pen_x += cw;
        if (pen_x > max_w) {
            max_w = pen_x;
        }
        if (c_byte < len) {
            walk_slice(s + c_byte, len - c_byte, desc.password);
        }
    } else if (!desc.multiline) {
        measure_line_segment_x(
            ctx, desc.font_id, s, len, c_byte, 0u,
            base_px, desc.letter_spacing, desc.password, false, &cx, nullptr, &max_w);
    } else {
        uint32_t line_start = 0u;
        for (;;) {
            uint32_t line_end = line_start;
            while (line_end < len && s[line_end] != '\n') {
                ++line_end;
            }
            float line_w = 0.0f;
            if (c_byte >= line_start && c_byte <= line_end) {
                cy = pen_y;
                measure_line_segment_x(
                    ctx, desc.font_id, s + line_start, line_end - line_start, c_byte - line_start, 0u,
                    base_px, desc.letter_spacing, desc.password, false, &cx, nullptr, &line_w);
            } else {
                measure_line_segment_x(
                    ctx, desc.font_id, s + line_start, line_end - line_start, 0u, 0u,
                    base_px, desc.letter_spacing, desc.password, false, nullptr, nullptr, &line_w);
            }
            if (line_w > max_w) {
                max_w = line_w;
            }
            if (line_end >= len) {
                break;
            }
            pen_y += line_adv;
            line_start = line_end + 1u;
        }
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
            ctx->active_ui->ime_cursor_logical = Rect{
                .x = geom.origin_x + comp_x - sx,
                .y = geom.origin_y + comp_y - sy,
                .w = (comp_w > 2.0f) ? comp_w : 2.0f,
                .h = geom.line_h,
            };
            ctx->active_ui->ime_cursor_offset_win = static_cast<int32_t>(std::floor((caret_x - comp_x) * win_scale + 0.5f));
        } else {
            ctx->active_ui->ime_cursor_logical = Rect{
                .x = geom.origin_x + caret_x - sx,
                .y = geom.origin_y + caret_y - sy,
                .w = 2.0f,
                .h = geom.line_h,
            };
            ctx->active_ui->ime_cursor_offset_win = 0;
        }
    }
}

void emit_utf8_glyph_run(
    Context* ctx,
    uint16_t font_id,
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
    float* io_pen_y,
    uint32_t tab_width = 4u) noexcept
{
    if (!ctx || !ctx->atlas.font_valid || !s || len == 0u || !io_pen_x || !io_pen_y) {
        return;
    }
    const uint16_t primary_slot = resolve_font_slot_idx(&ctx->atlas, font_id);
    if (primary_slot == kInvalidFont) {
        return;
    }
    const FontSlot& primary = ctx->atlas.slots[primary_slot];
    const float base_px = (font_size_px > 0.0f) ? font_size_px : 14.0f;
    const float phys_px = base_px * dpi;

    if (mask_bullets) {
        const float bullet_adv_logical = primary.bullet_advance_em * base_px + letter_spacing;
        const char* p = s;
        const char* end = s + len;
        while (p < end) {
            const uint32_t cp = next_utf8_codepoint(p, end);
            if (cp == '\n') {
                if (multiline) {
                    *io_pen_x = 0.0f;
                    *io_pen_y += line_adv_logical;
                }
                continue;
            }
            const float pen_x_px = (origin_x + *io_pen_x - scroll_x) * dpi;
            const float base_y_px = (origin_y + *io_pen_y - scroll_y) * dpi + ascender_px;
            emit_slug_glyph_quad(
                ctx, primary_slot, primary.bullet_glyph_id, phys_px, pen_x_px, base_y_px, 0.0f, 0.0f, clip_px, tint);
            *io_pen_x += bullet_adv_logical;
        }
        return;
    }

    uint32_t pos = 0u;
    while (pos < len) {
        const char ch = s[pos];
        if (ch == '\n') {
            if (multiline) {
                *io_pen_x = 0.0f;
                *io_pen_y += line_adv_logical;
            }
            ++pos;
            continue;
        }
        if (ch == '\t') {
            *io_pen_x += static_cast<float>(tab_width) * primary.space_advance_em * base_px + letter_spacing;
            ++pos;
            continue;
        }
        uint32_t run_end = pos;
        while (run_end < len && s[run_end] != '\t' && s[run_end] != '\n') {
            ++run_end;
        }
        const std::span<const ShapedGlyph> glyphs = shape_utf8_run(ctx, primary_slot, s + pos, run_end - pos);
        for (const ShapedGlyph& g : glyphs) {
            const float pen_x_px = (origin_x + *io_pen_x - scroll_x) * dpi;
            const float base_y_px = (origin_y + *io_pen_y - scroll_y) * dpi + ascender_px;
            emit_slug_glyph_quad(
                ctx,
                g.font_slot,
                g.glyph_id,
                phys_px,
                pen_x_px,
                base_y_px,
                g.x_offset_em,
                g.y_offset_em,
                clip_px,
                tint);
            *io_pen_x += g.x_advance_1px * base_px + letter_spacing;
        }
        pos = run_end;
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
    WidgetState* ws = (desc.id != 0u) ? state_map_find(&ctx->active_ui->states, desc.id) : nullptr;
    const Rect box_logical = { bounds_logical.x, bounds_logical.y, bounds_logical.width, bounds_logical.height };
    if (ws) {
        ws->last_box_logical = box_logical;
    }

    const TextInnerGeom geom = compute_text_inner_geom(box_logical, desc);
    const Corners rad_px = scale_corners(desc.radius, dpi);
    const bool is_focused = ti->is_focused;
    const bool composing = is_focused && !desc.read_only && !desc.password && ctx->active_ui->ime_comp_len > 0u;

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
    const uint16_t slot_idx = ctx->atlas.font_valid ? resolve_font_slot_idx(&ctx->atlas, desc.font_id) : kInvalidFont;
    const float ascender_px = (slot_idx != kInvalidFont)
        ? (ctx->atlas.slots[slot_idx].ascent_em * phys_px)
        : (phys_px * 0.8f);

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
                float x0 = 0.0f;
                float x1 = 0.0f;
                measure_line_segment_x(
                    ctx, desc.font_id, s + ls, le - ls, hs - ls, he - ls,
                    base_px, desc.letter_spacing, desc.password, false, &x0, &x1, nullptr);
                float sx0 = (x0 < x1) ? x0 : x1;
                float sx1 = (x0 < x1) ? x1 : x0;
                if (spans_nl) {
                    sx1 += 6.0f;
                }
                const Rect sel_px = {
                    (geom.origin_x + sx0 - scroll_x) * dpi,
                    (geom.origin_y + line_y - scroll_y - 1.0f) * dpi,
                    ((sx1 > sx0) ? (sx1 - sx0) : 2.0f) * dpi,
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
                ctx, desc.font_id, desc.placeholder.chars, desc.placeholder.length,
                base_px, desc.letter_spacing, ascender_px, geom.line_adv,
                desc.placeholder_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
                inner_clip, dpi, desc.multiline, false, &px, &py);
        }
    } else if (composing) {
        float pen_x = 0.0f;
        float pen_y = 0.0f;
        emit_utf8_glyph_run(
            ctx, desc.font_id, s, c_byte, base_px, desc.letter_spacing, ascender_px, geom.line_adv,
            desc.text_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
            inner_clip, dpi, desc.multiline, desc.password, &pen_x, &pen_y);

        const float comp_start_x = pen_x;
        const float comp_start_y = pen_y;
        float comp_w = 0.0f;
        float caret_dx = 0.0f;
        float clause_end_dx = 0.0f;
        uint32_t cl_end = ctx->active_ui->ime_comp_cursor_byte + ctx->active_ui->ime_comp_sel_len_byte;
        if (cl_end > ctx->active_ui->ime_comp_len) {
            cl_end = ctx->active_ui->ime_comp_len;
        }
        measure_line_segment_x(
            ctx, desc.font_id, ctx->active_ui->ime_comp_utf8, ctx->active_ui->ime_comp_len,
            ctx->active_ui->ime_comp_cursor_byte, cl_end,
            base_px, desc.letter_spacing, false, false,
            &caret_dx, &clause_end_dx, &comp_w);
        caret_vx = comp_start_x + caret_dx;
        caret_vy = comp_start_y;

        if (ctx->active_ui->ime_comp_sel_len_byte > 0u) {
            const float cl_left = (caret_dx < clause_end_dx) ? caret_dx : clause_end_dx;
            const float cl_w = (caret_dx < clause_end_dx) ? (clause_end_dx - caret_dx) : (caret_dx - clause_end_dx);
            if (cl_w > 0.0f) {
                const Rect clause_bg = {
                    (geom.origin_x + comp_start_x + cl_left - scroll_x) * dpi,
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
            ctx, desc.font_id, ctx->active_ui->ime_comp_utf8, ctx->active_ui->ime_comp_len, base_px, desc.letter_spacing, ascender_px, geom.line_adv,
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
                ctx, desc.font_id, s + c_byte, len - c_byte, base_px, desc.letter_spacing, ascender_px, geom.line_adv,
                desc.text_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
                inner_clip, dpi, desc.multiline, desc.password, &pen_x, &pen_y);
        }
    } else {
        float pen_x = 0.0f;
        float pen_y = 0.0f;
        emit_utf8_glyph_run(
            ctx, desc.font_id, s, len, base_px, desc.letter_spacing, ascender_px, geom.line_adv,
            desc.text_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
            inner_clip, dpi, desc.multiline, desc.password, &pen_x, &pen_y);
        const TextVisualPos cpos = text_byte_to_visual(ctx, desc, ti->committed_text, c_byte);
        caret_vx = cpos.x;
        caret_vy = cpos.y;
    }

    // 3. Emit SDF caret
    if (is_focused && ctx->active_ui->caret_blink_on && desc.caret_color.a > 0u) {
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

constexpr uint16_t sdl_mod_to_etch(uint16_t sdl_mod) noexcept {
    uint16_t out = Mod_None;
    if ((sdl_mod & SDL_KMOD_SHIFT) != 0u) out = static_cast<uint16_t>(out | Mod_Shift);
    if ((sdl_mod & SDL_KMOD_CTRL)  != 0u) out = static_cast<uint16_t>(out | Mod_Ctrl);
    if ((sdl_mod & SDL_KMOD_ALT)   != 0u) out = static_cast<uint16_t>(out | Mod_Alt);
    if ((sdl_mod & SDL_KMOD_GUI)   != 0u) out = static_cast<uint16_t>(out | Mod_Super);
    return out;
}

constexpr uint16_t etch_mod_to_sdl(uint16_t emod) noexcept {
    uint16_t out = 0u;
    if ((emod & Mod_Shift) != 0u) out = static_cast<uint16_t>(out | SDL_KMOD_SHIFT);
    if ((emod & Mod_Ctrl)  != 0u) out = static_cast<uint16_t>(out | SDL_KMOD_CTRL);
    if ((emod & Mod_Alt)   != 0u) out = static_cast<uint16_t>(out | SDL_KMOD_ALT);
    if ((emod & Mod_Super) != 0u) out = static_cast<uint16_t>(out | SDL_KMOD_GUI);
    return out;
}

WidgetState* find_topmost_user_element(
    Context* ctx,
    Clay_ElementIdArray over,
    uint32_t hovered_span_id) noexcept
{
    if (hovered_span_id != 0u) {
        if (WidgetState* sws = state_map_find(&ctx->active_ui->states, hovered_span_id)) {
            if (sws->is_user_element) {
                return sws;
            }
        }
    }
    for (int32_t i = over.length - 1; i >= 0; --i) {
        WidgetState* ws = state_map_find(&ctx->active_ui->states, over.internalArray[i].id);
        if (ws && ws->is_user_element) {
            return ws;
        }
    }
    return nullptr;
}

bool is_single_nonbreak_utf8_codepoint(Str ins) noexcept {
    if (!ins.chars || ins.length == 0u || ins.length > 4u) {
        return false;
    }
    const char* p = ins.chars;
    const char* end = ins.chars + ins.length;
    const uint32_t cp = next_utf8_codepoint(p, end);
    if (p != end) {
        return false;
    }
    return cp != ' ' && cp != '\n' && cp != '\t';
}

void undo_reset_slot(TextUndoSlot* slot) noexcept {
    slot->head = 0u;
    slot->count = 0u;
    slot->cur = 0u;
    slot->write_pos = 0u;
    slot->bytes_used = 0u;
    slot->last_coalesce_sec = -10.0f;
    slot->last_op_group = 0u;
}

TextUndoSlot* undo_acquire_slot(Context* ctx, uint32_t widget_id, uint32_t frame_index) noexcept {
    auto& ui = *ctx->active_ui;
    TextUndoSlot* lru = nullptr;
    for (uint32_t i = 0; i < ui.undo_cap; ++i) {
        auto* slot = &ui.undo_pool[i];
        if (slot->widget_id == widget_id) { slot->last_used_frame = frame_index; return slot; }
        if (!slot->widget_id) { lru = slot; break; }
        if (!lru || slot->last_used_frame < lru->last_used_frame) lru = slot;
    }
    if ((!lru || lru->widget_id) && ui.undo_cap < 512u) {
        const uint32_t cap = ui.undo_cap ? ui.undo_cap * 2u : 16u;
        void* p = std::realloc(ui.undo_pool, size_t(cap) * sizeof(TextUndoSlot));
        if (!p) return nullptr;
        ui.undo_pool = static_cast<TextUndoSlot*>(p);
        for (uint32_t i = ui.undo_cap; i < cap; ++i) ui.undo_pool[i] = {};
        lru = &ui.undo_pool[ui.undo_cap]; ui.undo_cap = cap;
    }
    if (!lru->entries) {
        lru->entries = static_cast<TextUndoRecord*>(std::calloc(32u, sizeof(TextUndoRecord)));
        if (!lru->entries) return nullptr;
        lru->entries_cap = 32u;
    }
    if (!lru->bytes) {
        lru->bytes = static_cast<char*>(std::calloc(8192u, 1));
        if (!lru->bytes) return nullptr;
        lru->bytes_cap = 8192u;
    }
    lru->widget_id = widget_id;
    lru->last_used_frame = frame_index;
    undo_reset_slot(lru);
    return lru;
}

void undo_pop_tail(TextUndoSlot* slot) noexcept {
    const uint32_t tail_idx = (slot->head + slot->count - 1u) % slot->entries_cap;
    const TextUndoRecord& tail = slot->entries[tail_idx];
    slot->write_pos = tail.offset;
    slot->bytes_used = slot->bytes_used - tail.length;
    --slot->count;
}

void undo_push_snapshot(
    TextUndoSlot* slot,
    const char* buf,
    uint32_t len,
    uint32_t cursor,
    uint32_t anchor,
    float elapsed_sec,
    uint8_t op_group) noexcept
{
    if (len > 262144u) {
        undo_reset_slot(slot);
        return;
    }
    while (slot->count > 0u && slot->count > slot->cur + 1u) {
        undo_pop_tail(slot);
    }
    const float dt = elapsed_sec - slot->last_coalesce_sec;
    if (op_group != 0u && op_group == slot->last_op_group &&
        slot->count >= 2u && dt > 0.0f && dt <= 0.30f)
    {
        undo_pop_tail(slot);
    }
    if (slot->count == slot->entries_cap && slot->entries_cap < 512u) {
        const uint32_t cap = slot->entries_cap * 2u;
        auto* entries = static_cast<TextUndoRecord*>(std::calloc(cap, sizeof(TextUndoRecord)));
        if (entries) {
            for (uint32_t i=0;i<slot->count;++i) entries[i]=slot->entries[(slot->head+i)%slot->entries_cap];
            std::free(slot->entries); slot->entries=entries; slot->entries_cap=cap; slot->head=0;
        }
    }
    if (slot->bytes_used + len > slot->bytes_cap && slot->bytes_cap < 262144u) {
        uint32_t cap=slot->bytes_cap; while(cap<slot->bytes_used+len && cap<262144u)cap*=2u;
        auto* bytes=static_cast<char*>(std::calloc(cap,1));
        if(bytes) {
            uint32_t write=0;
            for(uint32_t i=0;i<slot->count;++i) {
                auto& r=slot->entries[(slot->head+i)%slot->entries_cap];
                const uint32_t first=r.length < slot->bytes_cap-r.offset ? r.length : slot->bytes_cap-r.offset;
                std::memcpy(bytes+write,slot->bytes+r.offset,first);
                if(first<r.length)std::memcpy(bytes+write+first,slot->bytes,r.length-first);
                r.offset=write;write+=r.length;
            }
            std::free(slot->bytes);slot->bytes=bytes;slot->bytes_cap=cap;slot->write_pos=write%cap;
        }
    }
    if (len > slot->bytes_cap) { undo_reset_slot(slot); return; }
    while (slot->count > 0u &&
           (slot->count >= slot->entries_cap || uint32_t(slot->bytes_used) + len > slot->bytes_cap))
    {
        const TextUndoRecord& oldest = slot->entries[slot->head];
        slot->bytes_used = slot->bytes_used - oldest.length;
        slot->head = (slot->head + 1u) % slot->entries_cap;
        --slot->count;
    }
    if (slot->count == 0u) {
        slot->head = 0u;
        slot->write_pos = 0u;
        slot->bytes_used = 0u;
    }
    const uint32_t hash = (len > 0u && buf) ? fnv1a_32_step(2166136261u, buf, len) : 2166136261u;
    const uint32_t start_off = slot->write_pos;
    if (len > 0u && buf) {
        const uint32_t first = slot->bytes_cap - start_off;
        if (len <= first) {
            std::memcpy(slot->bytes + start_off, buf, len);
        } else {
            std::memcpy(slot->bytes + start_off, buf, first);
            std::memcpy(slot->bytes, buf + first, len - first);
        }
    }
    const uint32_t ins_idx = (slot->head + slot->count) % slot->entries_cap;
    slot->entries[ins_idx] = TextUndoRecord{
        .offset = start_off,
        .length = len,
        .cursor = cursor,
        .anchor = anchor,
        .hash = hash,
    };
    slot->write_pos = (start_off + len) % slot->bytes_cap;
    slot->bytes_used = slot->bytes_used + len;
    slot->cur = slot->count;
    ++slot->count;
    slot->last_coalesce_sec = elapsed_sec;
    slot->last_op_group = op_group;
}

bool undo_restore(
    TextUndoSlot* slot,
    uint32_t target_cur,
    char* buf,
    uint32_t cap,
    uint32_t* io_len,
    uint32_t* io_cursor,
    uint32_t* io_anchor) noexcept
{
    if (!slot || !buf || cap == 0u || target_cur >= slot->count) {
        return false;
    }
    const TextUndoRecord& rec = slot->entries[(slot->head + target_cur) % slot->entries_cap];
    const uint32_t max_copy = cap - 1u;
    const uint32_t to_copy = (rec.length <= max_copy) ? rec.length : max_copy;
    if (to_copy > 0u) {
        const uint32_t first = slot->bytes_cap - rec.offset;
        if (to_copy <= first) {
            std::memcpy(buf, slot->bytes + rec.offset, to_copy);
        } else {
            std::memcpy(buf, slot->bytes + rec.offset, first);
            std::memcpy(buf + first, slot->bytes, to_copy - first);
        }
    }
    uint32_t clamped_len = to_copy;
    if (clamped_len > 0u && to_copy < rec.length) {
        const uint32_t lead = utf8_prev_boundary(buf, to_copy, to_copy);
        const uint8_t lb = static_cast<uint8_t>(buf[lead]);
        const uint32_t cp_bytes = (lb < 0x80u) ? 1u
            : ((lb & 0xE0u) == 0xC0u) ? 2u
            : ((lb & 0xF0u) == 0xE0u) ? 3u
            : (((lb & 0xF8u) == 0xF0u) ? 4u : 1u);
        if (lead + cp_bytes > to_copy) {
            clamped_len = lead;
        }
    }
    buf[clamped_len] = '\0';
    *io_len = clamped_len;
    *io_cursor = utf8_clamp_boundary(buf, clamped_len, rec.cursor);
    *io_anchor = utf8_clamp_boundary(buf, clamped_len, rec.anchor);
    slot->cur = target_cur;
    slot->last_op_group = 0u;
    return true;
}

} // namespace etch
