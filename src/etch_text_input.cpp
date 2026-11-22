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
        if(cur==5){zwj_pict=pict;pict=false;}else if(cur!=4){pict=ep;zwj_pict=false;}
        ri=cur==6?ri+1:0;prev=cur;end=utf8_next_boundary(s,len,end);
    }
    return end;
}
uint32_t utf8_prev_grapheme(const char* s, uint32_t len, uint32_t pos) noexcept {
    if (!s || len == 0u || pos == 0u) return 0u;
    if (pos > len) pos = len;
    uint32_t anchor = utf8_prev_boundary(s, len, pos);
    while (anchor > 0u) {
        const uint32_t cp = utf8_decode_at(s, len, anchor);
        if (grapheme_property(cp, kGraphemeRanges, ETCH_COUNTOF(kGraphemeRanges)) == 0u &&
            grapheme_property(cp, kPictographicRanges, ETCH_COUNTOF(kPictographicRanges)) == 0u)
        {
            const uint32_t prev = utf8_prev_boundary(s, len, anchor);
            if (grapheme_property(utf8_decode_at(s, len, prev), kGraphemeRanges, ETCH_COUNTOF(kGraphemeRanges)) != 7u) {
                break;
            }
        }
        anchor = utf8_prev_boundary(s, len, anchor);
    }
    uint32_t cur = anchor;
    while (cur < pos) {
        const uint32_t next = utf8_next_grapheme(s, len, cur);
        if (next >= pos) return cur;
        cur = next;
    }
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
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0x00A0u || cp == 0x3000u) {
        return 0u;
    }
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') || cp == '_') {
        return 1u;
    }
    if ((cp >= 0x2000u && cp <= 0x206Fu) || (cp >= 0x3001u && cp <= 0x303Fu) ||
        (cp >= 0xFF01u && cp <= 0xFF0Fu) || (cp >= 0xFF1Au && cp <= 0xFF20u) ||
        (cp >= 0xFF3Bu && cp <= 0xFF40u) || (cp >= 0xFF5Bu && cp <= 0xFF65u)) {
        return 3u;
    }
    if ((cp >= 0x3040u && cp <= 0x30FFu) || (cp >= 0x3400u && cp <= 0x4DBFu) ||
        (cp >= 0x4E00u && cp <= 0x9FFFu) || (cp >= 0xAC00u && cp <= 0xD7AFu) ||
        (cp >= 0xF900u && cp <= 0xFAFFu) || (cp >= 0x20000u && cp <= 0x3FFFFu)) {
        return 4u;
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

float text_input_line_height_logical(Context* ctx, const TextInputDesc& desc) noexcept {
    const float font_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f;
    return scaled_font_span_px(ctx, desc.font_id, font_px);
}

float text_input_line_advance_logical(Context* ctx, const TextInputDesc& desc) noexcept {
    const float line_h = text_input_line_height_logical(ctx, desc);
    return desc.multiline ? std::floor(line_h * 1.35f + 0.5f) : line_h;
}

struct TextInnerGeom {
    Rect  clip_logical = {};
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    float font_px = 14.0f;
    float line_h = 14.0f;
    float line_adv = 14.0f;
};

TextInnerGeom compute_text_inner_geom(Context* ctx, Rect box_logical, const TextInputDesc& desc) noexcept {
    const float font_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f;
    const float line_h = scaled_font_span_px(ctx, desc.font_id, font_px);
    const float line_adv = desc.multiline ? std::floor(line_h * 1.35f + 0.5f) : line_h;
    const float ix = box_logical.x + desc.padding.left;
    const float iy = box_logical.y + desc.padding.top;
    const float iw = (box_logical.w > desc.padding.left + desc.padding.right)
        ? (box_logical.w - desc.padding.left - desc.padding.right) : 0.0f;
    const float ih = (box_logical.h > desc.padding.top + desc.padding.bottom)
        ? (box_logical.h - desc.padding.top - desc.padding.bottom) : 0.0f;
    const bool has_scale_adjust = (line_h != font_px);
    const float oy = desc.multiline
        ? iy
        : (ih > line_h
            ? (iy + 0.5f * (ih - line_h))
            : (has_scale_adjust ? std::fmax(box_logical.y + 1.0f, iy + 0.5f * (ih - line_h)) : iy));
    const float clip_pad_top = (desc.padding.top > 2.0f) ? 2.0f : desc.padding.top;
    const float clip_pad_bot = (desc.padding.bottom > 3.0f) ? 3.0f : desc.padding.bottom;
    float clip_y0 = iy - clip_pad_top;
    float clip_y1 = iy + ih + clip_pad_bot;
    if (!desc.multiline && has_scale_adjust && line_h > ih) {
        const float box_inner_y0 = box_logical.y + 1.0f;
        const float box_inner_y1 = box_logical.y + box_logical.h - 1.0f;
        if (box_inner_y1 > box_inner_y0) {
            clip_y0 = std::fmin(clip_y0, std::fmax(box_inner_y0, oy - 1.0f));
            clip_y1 = std::fmax(clip_y1, std::fmin(box_inner_y1, oy + line_h + 1.0f));
        }
    }
    return TextInnerGeom{
        .clip_logical = Rect{ ix, clip_y0, iw, clip_y1 - clip_y0 },
        .origin_x = ix,
        .origin_y = oy,
        .font_px = font_px,
        .line_h = line_h,
        .line_adv = line_adv,
    };
}

struct TextVisualPos {
    float    x = 0.0f;
    float    y = 0.0f;
    uint32_t line_idx = 0u;
};

struct WrappedSubline {
    uint32_t start = 0u;
    uint32_t end = 0u;
    uint32_t sub_idx = 0u;
    float    width_px = 0.0f;
};

constexpr bool is_wrap_ideograph(uint32_t cp) noexcept {
    return (cp >= 0x3000u && cp <= 0x30FFu) ||
           (cp >= 0x3400u && cp <= 0x4DBFu) ||
           (cp >= 0x4E00u && cp <= 0x9FFFu) ||
           (cp >= 0xAC00u && cp <= 0xD7AFu) ||
           (cp >= 0xF900u && cp <= 0xFAFFu) ||
           (cp >= 0xFF00u && cp <= 0xFFEFu) ||
           (cp >= 0x20000u && cp <= 0x3FFFFu);
}

constexpr bool is_kinsoku_no_start(uint32_t cp) noexcept {
    return cp == 0x3001u || cp == 0x3002u || cp == 0x3009u || cp == 0x300Bu ||
           cp == 0x300Du || cp == 0x300Fu || cp == 0x3011u || cp == 0x3015u ||
           cp == 0x3017u || cp == 0x3019u || cp == 0x301Bu || cp == 0x30FCu ||
           cp == 0x2019u || cp == 0x201Du || cp == 0x2026u || cp == 0xFF01u ||
           cp == 0xFF09u || cp == 0xFF0Cu || cp == 0xFF0Eu || cp == 0xFF1Au ||
           cp == 0xFF1Bu || cp == 0xFF1Fu || cp == 0xFF3Du || cp == 0xFF5Du;
}

constexpr bool is_kinsoku_no_end(uint32_t cp) noexcept {
    return cp == 0x3008u || cp == 0x300Au || cp == 0x300Cu || cp == 0x300Eu ||
           cp == 0x3010u || cp == 0x3014u || cp == 0x3016u || cp == 0x3018u ||
           cp == 0x301Au || cp == 0x2018u || cp == 0x201Cu || cp == 0xFF08u ||
           cp == 0xFF3Bu || cp == 0xFF5Bu;
}

inline float text_input_wrap_width(const TextInputDesc& desc, const TextInnerGeom& geom) noexcept {
    if (!desc.multiline || !desc.word_wrap || geom.clip_logical.w <= 6.0f) {
        return 0.0f;
    }
    return geom.clip_logical.w - 3.0f;
}

template <typename Fn>
void for_each_wrapped_subline(
    Context* ctx,
    uint16_t font_id,
    const char* s,
    uint32_t len,
    float font_size_px,
    float letter_spacing,
    bool password,
    uint32_t tab_width,
    float wrap_w,
    Fn&& emit) noexcept
{
    if (!s || len == 0u) {
        (void)emit(WrappedSubline{ 0u, 0u, 0u, 0.0f });
        return;
    }
    if (wrap_w <= 0.0f) {
        float total_w = 0.0f;
        measure_line_segment_x(
            ctx, font_id, s, len, 0u, 0u,
            font_size_px, letter_spacing, password, false, nullptr, nullptr, &total_w, tab_width);
        (void)emit(WrappedSubline{ 0u, len, 0u, total_w });
        return;
    }

    const float base_px = (font_size_px > 0.0f) ? font_size_px : 14.0f;
    const uint16_t slot_idx = (ctx && ctx->atlas.font_valid) ? resolve_font_slot_idx(&ctx->atlas, font_id) : kInvalidFont;
    const bool fallback = (!ctx || !ctx->atlas.font_valid || slot_idx == kInvalidFont || password || !ctx->frame_arena.base);
    const float cp_adv = (!ctx || !ctx->atlas.font_valid || slot_idx == kInvalidFont)
        ? (base_px * 0.55f)
        : (ctx->atlas.slots[slot_idx].bullet_advance_em * base_px);
    const float tab_w_px = (!fallback)
        ? (static_cast<float>(tab_width) * ctx->atlas.slots[slot_idx].space_advance_em * base_px + letter_spacing)
        : (static_cast<float>(tab_width) * cp_adv + letter_spacing);

    uint32_t sub_start = 0u;
    uint32_t sub_idx = 0u;

    while (sub_start < len) {
        uint32_t pos = sub_start;
        float pen_x = 0.0f;
        uint32_t last_break_off = sub_start;
        float last_break_w = 0.0f;
        bool prev_was_ws = false;
        bool broken = false;

        while (pos < len) {
            if (s[pos] == '\t') {
                const uint32_t g_end = pos + 1u;
                if (pos > sub_start && pen_x + tab_w_px > wrap_w + 0.01f) {
                    if (!prev_was_ws && pen_x <= wrap_w + 0.01f) {
                        pen_x += tab_w_px;
                        last_break_off = g_end;
                        last_break_w = pen_x;
                        prev_was_ws = true;
                        pos = g_end;
                        continue;
                    }
                    const uint32_t brk = (last_break_off > sub_start) ? last_break_off : pos;
                    const float brk_w = (last_break_off > sub_start) ? last_break_w : pen_x;
                    if (!emit(WrappedSubline{ sub_start, brk, sub_idx++, brk_w })) {
                        return;
                    }
                    sub_start = brk;
                    broken = true;
                    break;
                }
                pen_x += tab_w_px;
                last_break_off = g_end;
                last_break_w = pen_x;
                prev_was_ws = true;
                pos = g_end;
                continue;
            }

            if (fallback) {
                const uint32_t g_end = password
                    ? utf8_next_boundary(s, len, pos)
                    : utf8_next_grapheme(s, len, pos);
                const float g_adv = cp_adv + letter_spacing;
                const uint32_t cp = utf8_decode_at(s, len, pos);
                const bool is_ws = (cp == ' ');
                if (pos > sub_start && pen_x + g_adv > wrap_w + 0.01f) {
                    if (is_ws && !prev_was_ws && pen_x <= wrap_w + 0.01f) {
                        pen_x += g_adv;
                        last_break_off = g_end;
                        last_break_w = pen_x;
                        prev_was_ws = true;
                        pos = g_end;
                        continue;
                    }
                    const uint32_t brk = (last_break_off > sub_start) ? last_break_off : pos;
                    const float brk_w = (last_break_off > sub_start) ? last_break_w : pen_x;
                    if (!emit(WrappedSubline{ sub_start, brk, sub_idx++, brk_w })) {
                        return;
                    }
                    sub_start = brk;
                    broken = true;
                    break;
                }
                pen_x += g_adv;
                prev_was_ws = is_ws;
                const uint32_t next_cp = (g_end < len) ? utf8_decode_at(s, len, g_end) : 0u;
                const bool next_no_start = (g_end < len) && is_kinsoku_no_start(next_cp);
                const bool next_ideo = (g_end < len) && is_wrap_ideograph(next_cp) && !next_no_start;
                if (!is_kinsoku_no_end(cp) && !next_no_start &&
                    (is_ws || cp == '-' || cp == '/' || cp == '\\' || is_wrap_ideograph(cp) || next_ideo))
                {
                    last_break_off = g_end;
                    last_break_w = pen_x;
                }
                pos = g_end;
                continue;
            }

            uint32_t chunk_end = pos;
            uint32_t last_ws_end = 0u;
            const uint32_t target_cap = (pos + 256u < len) ? (pos + 256u) : len;
            while (chunk_end < target_cap && s[chunk_end] != '\t') {
                const uint32_t g_next = utf8_next_grapheme(s, len, chunk_end);
                if (g_next <= chunk_end) {
                    ++chunk_end;
                    break;
                }
                chunk_end = g_next;
                if (chunk_end - pos >= 128u && s[chunk_end - 1u] == ' ') {
                    last_ws_end = chunk_end;
                }
            }
            if (chunk_end < len && s[chunk_end] != '\t' && last_ws_end > pos) {
                chunk_end = last_ws_end;
            }
            if (chunk_end <= pos) {
                chunk_end = utf8_next_grapheme(s, len, pos);
                if (chunk_end <= pos) {
                    chunk_end = pos + 1u;
                }
            }

            const uint32_t run_len = chunk_end - pos;
            const size_t run_mark = ctx->frame_arena.mark();
            bool is_rtl = false;
            const std::span<const ShapedGlyph> glyphs = shape_utf8_run(ctx, slot_idx, s + pos, run_len, &is_rtl);

            uint32_t ng = 0u;
            for (uint32_t gp = 0u; gp < run_len; gp = utf8_next_grapheme(s + pos, run_len, gp)) {
                ++ng;
            }
            uint32_t* grp_end = (ng > 0u) ? ctx->frame_arena.alloc_uninit<uint32_t>(ng) : nullptr;
            float* grp_adv = (ng > 0u) ? ctx->frame_arena.alloc<float>(ng) : nullptr;
            uint32_t* byte_to_grp = (ng > 0u) ? ctx->frame_arena.alloc_uninit<uint32_t>(run_len) : nullptr;
            if (!grp_end || !grp_adv || !byte_to_grp) {
                ctx->frame_arena.rewind(run_mark);
                return;
            }
            {
                uint32_t k = 0u;
                for (uint32_t gp = 0u; gp < run_len;) {
                    const uint32_t gn = utf8_next_grapheme(s + pos, run_len, gp);
                    grp_end[k] = gn;
                    for (uint32_t b = gp; b < gn; ++b) {
                        byte_to_grp[b] = k;
                    }
                    ++k;
                    gp = gn;
                }
            }

            const uint32_t gcount = static_cast<uint32_t>(glyphs.size());
            uint32_t gi = 0u;
            while (gi < gcount) {
                const uint32_t c0 = glyphs[gi].cluster;
                uint32_t c1 = glyphs[gi].cluster_end;
                uint32_t gi_end = gi + 1u;
                while (gi_end < gcount && glyphs[gi_end].cluster == c0) {
                    if (glyphs[gi_end].cluster_end > c1) {
                        c1 = glyphs[gi_end].cluster_end;
                    }
                    ++gi_end;
                }
                float cluster_adv = 0.0f;
                for (uint32_t m = gi; m < gi_end; ++m) {
                    cluster_adv += glyphs[m].x_advance_1px * base_px + letter_spacing;
                }
                if (c0 < run_len) {
                    const uint32_t c1_clamped = (c1 > c0 && c1 <= run_len) ? c1 : (c0 + 1u);
                    const uint32_t ga = byte_to_grp[c0];
                    const uint32_t gb = byte_to_grp[c1_clamped - 1u];
                    const uint32_t span_g = (gb >= ga) ? (gb - ga + 1u) : 1u;
                    const float per_g = cluster_adv / static_cast<float>(span_g);
                    for (uint32_t k = ga; k <= gb; ++k) {
                        grp_adv[k] += per_g;
                    }
                }
                gi = gi_end;
            }

            for (uint32_t k = 0u; k < ng; ++k) {
                const uint32_t g_start = pos + ((k > 0u) ? grp_end[k - 1u] : 0u);
                const uint32_t g_end = pos + grp_end[k];
                const float g_adv = grp_adv[k];
                const uint32_t cp = utf8_decode_at(s, len, g_start);
                const bool is_ws = (cp == ' ');
                if (g_start > sub_start && pen_x + g_adv > wrap_w + 0.01f) {
                    if (is_ws && !prev_was_ws && pen_x <= wrap_w + 0.01f) {
                        pen_x += g_adv;
                        last_break_off = g_end;
                        last_break_w = pen_x;
                        prev_was_ws = true;
                        continue;
                    }
                    const uint32_t brk = (last_break_off > sub_start) ? last_break_off : g_start;
                    const float brk_w = (last_break_off > sub_start) ? last_break_w : pen_x;
                    ctx->frame_arena.rewind(run_mark);
                    if (!emit(WrappedSubline{ sub_start, brk, sub_idx++, brk_w })) {
                        return;
                    }
                    sub_start = brk;
                    broken = true;
                    break;
                }
                pen_x += g_adv;
                prev_was_ws = is_ws;
                const uint32_t next_cp = (g_end < len) ? utf8_decode_at(s, len, g_end) : 0u;
                const bool next_no_start = (g_end < len) && is_kinsoku_no_start(next_cp);
                const bool next_ideo = (g_end < len) && is_wrap_ideograph(next_cp) && !next_no_start;
                if (!is_kinsoku_no_end(cp) && !next_no_start &&
                    (is_ws || cp == '-' || cp == '/' || cp == '\\' || is_wrap_ideograph(cp) || next_ideo))
                {
                    last_break_off = g_end;
                    last_break_w = pen_x;
                }
            }
            ctx->frame_arena.rewind(run_mark);
            if (broken) {
                break;
            }
            pos = chunk_end;
        }

        if (!broken) {
            (void)emit(WrappedSubline{ sub_start, len, sub_idx++, pen_x });
            break;
        }
    }
}

uint32_t count_wrapped_sublines(
    Context* ctx,
    uint16_t font_id,
    const char* s,
    uint32_t len,
    float font_size_px,
    float letter_spacing,
    bool password,
    uint32_t tab_width,
    float wrap_w) noexcept
{
    uint32_t count = 0u;
    for_each_wrapped_subline(ctx, font_id, s, len, font_size_px, letter_spacing, password, tab_width, wrap_w,
        [&](const WrappedSubline&) noexcept {
            ++count;
            return true;
        });
    return (count > 0u) ? count : 1u;
}

WrappedSubline locate_wrapped_offset(
    Context* ctx,
    uint16_t font_id,
    const char* s,
    uint32_t len,
    uint32_t rel_offset,
    bool wrap_affinity,
    float font_size_px,
    float letter_spacing,
    bool password,
    uint32_t tab_width,
    float wrap_w,
    float* out_x = nullptr) noexcept
{
    const uint32_t clamped = (rel_offset <= len) ? rel_offset : len;
    WrappedSubline found = {};
    bool matched = false;
    for_each_wrapped_subline(ctx, font_id, s, len, font_size_px, letter_spacing, password, tab_width, wrap_w,
        [&](const WrappedSubline& sub) noexcept {
            const bool is_last = (sub.end >= len);
            if (clamped < sub.end || (clamped == sub.end && (is_last || wrap_affinity))) {
                found = sub;
                matched = true;
                return false;
            }
            found = sub;
            return true;
        });
    if (out_x) {
        if (!matched) {
            *out_x = found.width_px;
        } else if (s && found.end > found.start) {
            float x = 0.0f;
            measure_line_segment_x(
                ctx, font_id, s + found.start, found.end - found.start,
                (clamped > found.start) ? (clamped - found.start) : 0u, 0u,
                font_size_px, letter_spacing, password, false, &x, nullptr, nullptr, tab_width);
            *out_x = x;
        } else {
            *out_x = 0.0f;
        }
    }
    return found;
}

uint32_t hit_line_segment_x(
    Context* ctx,
    uint16_t font_id,
    const char* seg,
    uint32_t seg_len,
    float font_size_px,
    float letter_spacing,
    bool password,
    uint32_t tab_width,
    float local_x,
    bool* out_past_end = nullptr) noexcept
{
    if (out_past_end) {
        *out_past_end = false;
    }
    if (!seg || seg_len == 0u || local_x <= 0.0f) {
        return 0u;
    }
    const float base_px = (font_size_px > 0.0f) ? font_size_px : 14.0f;
    float pen_x = 0.0f;
    const uint16_t slot_idx = (ctx && ctx->atlas.font_valid) ? resolve_font_slot_idx(&ctx->atlas, font_id) : kInvalidFont;
    if (!ctx || !ctx->atlas.font_valid || slot_idx == kInvalidFont || password) {
        const float cp_adv = (!ctx || !ctx->atlas.font_valid || slot_idx == kInvalidFont)
            ? (base_px * 0.55f)
            : (ctx->atlas.slots[slot_idx].bullet_advance_em * base_px);
        uint32_t p = 0u;
        while (p < seg_len) {
            const uint32_t next = password ? utf8_next_boundary(seg, seg_len, p) : utf8_next_grapheme(seg, seg_len, p);
            const uint32_t cp = utf8_decode_at(seg, seg_len, p);
            const float adv = ((cp == '\t' && !password) ? static_cast<float>(tab_width) * cp_adv : cp_adv) + letter_spacing;
            if (local_x < pen_x + 0.5f * adv) {
                return p;
            }
            pen_x += adv;
            p = next;
        }
        if (out_past_end) {
            *out_past_end = (seg_len > 0u && local_x >= pen_x);
        }
        return seg_len;
    }

    const FontSlot& primary = ctx->atlas.slots[slot_idx];
    bool last_run_rtl = false;
    uint32_t last_run_start = 0u;
    uint32_t pos = 0u;
    while (pos < seg_len) {
        if (seg[pos] == '\t') {
            const float adv = static_cast<float>(tab_width) * primary.space_advance_em * base_px + letter_spacing;
            if (local_x < pen_x + 0.5f * adv) {
                return pos;
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
            uint32_t c1 = glyphs[gi].cluster_end;
            uint32_t gi_end = gi + 1u;
            while (gi_end < gcount && glyphs[gi_end].cluster == c0) {
                if (glyphs[gi_end].cluster_end > c1) {
                    c1 = glyphs[gi_end].cluster_end;
                }
                ++gi_end;
            }
            float cluster_adv = 0.0f;
            for (uint32_t k = gi; k < gi_end; ++k) {
                cluster_adv += glyphs[k].x_advance_1px * base_px + letter_spacing;
            }
            is_rtl = glyphs[gi].is_rtl != 0;
            if (local_x < pen_x + cluster_adv && c1 > c0) {
                const uint32_t start = pos + c0;
                const uint32_t end = pos + c1;
                uint32_t n_grp = 0u;
                for (uint32_t p = start; p < end; p = utf8_next_grapheme(seg, end, p)) {
                    ++n_grp;
                }
                uint32_t target = 0u;
                const float rel_x = local_x - pen_x;
                while (target < n_grp && rel_x >= ((static_cast<float>(target) + 0.5f) / static_cast<float>(n_grp)) * cluster_adv) {
                    ++target;
                }
                if (is_rtl) {
                    target = n_grp - target;
                }
                uint32_t hit = start;
                for (uint32_t k = 0u; k < target; ++k) {
                    hit = utf8_next_grapheme(seg, end, hit);
                }
                ctx->frame_arena.rewind(run_mark);
                return hit;
            }
            pen_x += cluster_adv;
            gi = gi_end;
        }
        ctx->frame_arena.rewind(run_mark);
        last_run_rtl = is_rtl;
        last_run_start = pos;
        pos = run_end;
    }
    if (out_past_end) {
        *out_past_end = (seg_len > 0u && local_x >= pen_x);
    }
    return last_run_rtl ? last_run_start : seg_len;
}

TextVisualPos text_byte_to_visual(
    Context* ctx,
    const TextInputDesc& desc,
    Str text,
    uint32_t byte_pos,
    float wrap_w = 0.0f,
    bool wrap_affinity = false) noexcept
{
    const float base_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f;
    const float line_adv = text_input_line_advance_logical(ctx, desc);
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
        const uint32_t line_len = line_end - line_start;
        if (wrap_w <= 0.0f) {
            if (clamped <= line_end) {
                measure_line_segment_x(
                    ctx, desc.font_id, text.chars + line_start, line_len, clamped - line_start, 0u,
                    base_px, desc.letter_spacing, desc.password, false, &out.x, nullptr, nullptr);
                return out;
            }
            out.y += line_adv;
            ++out.line_idx;
        } else {
            const uint32_t rel = (clamped <= line_end && clamped >= line_start) ? (clamped - line_start) : 0u;
            bool found = false;
            for_each_wrapped_subline(ctx, desc.font_id, text.chars + line_start, line_len, base_px, desc.letter_spacing, desc.password, 4u, wrap_w,
                [&](const WrappedSubline& sub) noexcept {
                    if (clamped <= line_end) {
                        const bool is_last = (sub.end >= line_len);
                        if (rel < sub.end || (rel == sub.end && (is_last || wrap_affinity))) {
                            measure_line_segment_x(
                                ctx, desc.font_id, text.chars + line_start + sub.start, sub.end - sub.start,
                                (rel > sub.start) ? (rel - sub.start) : 0u, 0u,
                                base_px, desc.letter_spacing, desc.password, false, &out.x, nullptr, nullptr);
                            found = true;
                            return false;
                        }
                    }
                    out.y += line_adv;
                    ++out.line_idx;
                    return true;
                });
            if (found) {
                return out;
            }
        }
        if (line_end >= text.length) {
            return out;
        }
        line_start = line_end + 1u;
    }
}

uint32_t visual_point_to_byte(
    Context* ctx,
    const TextInputDesc& desc,
    Str text,
    float local_x,
    float local_y,
    float wrap_w = 0.0f,
    bool* out_affinity = nullptr) noexcept
{
    if (out_affinity) {
        *out_affinity = false;
    }
    if (!text.chars || text.length == 0u) {
        return 0u;
    }
    const float base_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f;
    const float line_adv = text_input_line_advance_logical(ctx, desc);
    const uint32_t target_line = desc.multiline
        ? static_cast<uint32_t>((local_y > 0.0f) ? std::floor(local_y / line_adv) : 0.0f)
        : 0u;

    if (!desc.multiline || wrap_w <= 0.0f) {
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
        return line_start + hit_line_segment_x(
            ctx, desc.font_id, text.chars + line_start, line_end - line_start,
            base_px, desc.letter_spacing, desc.password, 4u, local_x, nullptr);
    }

    uint32_t line_start = 0u;
    uint32_t cur_vline = 0u;
    for (;;) {
        uint32_t line_end = line_start;
        while (line_end < text.length && text.chars[line_end] != '\n') {
            ++line_end;
        }
        const uint32_t line_len = line_end - line_start;
        uint32_t found_byte = text.length;
        bool matched = false;
        for_each_wrapped_subline(ctx, desc.font_id, text.chars + line_start, line_len, base_px, desc.letter_spacing, desc.password, 4u, wrap_w,
            [&](const WrappedSubline& sub) noexcept {
                if (cur_vline == target_line) {
                    bool past_end = false;
                    const uint32_t rel = hit_line_segment_x(
                        ctx, desc.font_id, text.chars + line_start + sub.start, sub.end - sub.start,
                        base_px, desc.letter_spacing, desc.password, 4u, local_x, &past_end);
                    found_byte = line_start + sub.start + rel;
                    if (out_affinity) {
                        *out_affinity = (sub.start + rel == sub.end) && (sub.end < line_len) && past_end;
                    }
                    matched = true;
                    return false;
                }
                ++cur_vline;
                return true;
            });
        if (matched) {
            return found_byte;
        }
        if (line_end >= text.length) {
            return text.length;
        }
        line_start = line_end + 1u;
    }
}

void text_wrapped_home_end(
    Context* ctx,
    const TextInputDesc& desc,
    Str text,
    uint32_t cursor_byte,
    bool cur_affinity,
    bool is_end,
    float wrap_w,
    uint32_t* out_cursor,
    bool* out_affinity) noexcept
{
    const char* s = text.chars ? text.chars : "";
    const uint32_t len = text.length;
    uint32_t ls = 0u;
    uint32_t le = 0u;
    line_bounds_at(s, len, cursor_byte, &ls, &le);
    const uint32_t line_len = le - ls;
    const uint32_t rel = (cursor_byte > ls) ? (cursor_byte - ls) : 0u;
    const float base_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f;
    const WrappedSubline sub = locate_wrapped_offset(
        ctx, desc.font_id, s + ls, line_len, rel, cur_affinity,
        base_px, desc.letter_spacing, desc.password, 4u, wrap_w, nullptr);
    if (is_end) {
        *out_cursor = ls + sub.end;
        *out_affinity = (sub.end < line_len);
    } else {
        *out_cursor = ls + sub.start;
        *out_affinity = false;
    }
}

void move_caret_vertical(
    Context* ctx,
    const TextInputDesc& desc,
    Str text,
    WidgetState* ws,
    int32_t delta,
    bool extend_selection,
    float wrap_w = 0.0f) noexcept
{
    if (!ws || !desc.multiline) {
        return;
    }
    const TextVisualPos cur = text_byte_to_visual(ctx, desc, text, ws->edit_cursor, wrap_w, ws->edit_wrap_affinity);
    if (ws->edit_preferred_x < 0.0f) {
        ws->edit_preferred_x = cur.x;
    }
    const float line_adv = text_input_line_advance_logical(ctx, desc);
    const TextVisualPos total_end = text_byte_to_visual(ctx, desc, text, text.length, wrap_w, false);

    if (delta < 0) {
        if (cur.line_idx == 0u) {
            ws->edit_cursor = 0u;
            ws->edit_wrap_affinity = false;
        } else {
            const uint32_t target_line = cur.line_idx - 1u;
            bool aff = false;
            ws->edit_cursor = visual_point_to_byte(
                ctx,
                desc,
                text,
                ws->edit_preferred_x,
                (static_cast<float>(target_line) + 0.5f) * line_adv,
                wrap_w,
                &aff);
            ws->edit_wrap_affinity = aff;
        }
    } else if (delta > 0) {
        if (cur.line_idx >= total_end.line_idx) {
            ws->edit_cursor = text.length;
            ws->edit_wrap_affinity = false;
        } else {
            const uint32_t target_line = cur.line_idx + 1u;
            bool aff = false;
            ws->edit_cursor = visual_point_to_byte(
                ctx,
                desc,
                text,
                ws->edit_preferred_x,
                (static_cast<float>(target_line) + 0.5f) * line_adv,
                wrap_w,
                &aff);
            ws->edit_wrap_affinity = aff;
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
    float* out_caret_y,
    float wrap_w = 0.0f,
    bool wrap_affinity = false) noexcept
{
    const float base_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f;
    const float line_h = text_input_line_height_logical(ctx, desc);
    const float line_adv = desc.multiline ? std::floor(line_h * 1.35f + 0.5f) : line_h;
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
        if (wrap_w > 0.0f && desc.multiline) {
            const uint32_t comp_len = ctx->active_ui->ime_comp_len;
            const uint32_t comp_cbyte = utf8_clamp_boundary(ctx->active_ui->ime_comp_utf8, comp_len, ctx->active_ui->ime_comp_cursor_byte);
            uint32_t line_start = 0u;
            bool any_line = false;
            for (;;) {
                uint32_t line_end = line_start;
                while (line_end < len && s[line_end] != '\n') {
                    ++line_end;
                }
                const uint32_t line_len = line_end - line_start;
                if (any_line) {
                    pen_y += line_adv;
                }
                any_line = true;
                if (c_byte >= line_start && c_byte <= line_end) {
                    const uint32_t rel_c = c_byte - line_start;
                    const size_t sp_mark = ctx->frame_arena.mark();
                    char* spliced = ctx->frame_arena.alloc_uninit<char>(line_len + comp_len + 1u);
                    if (spliced) {
                        if (rel_c > 0u) {
                            std::memcpy(spliced, s + line_start, rel_c);
                        }
                        std::memcpy(spliced + rel_c, ctx->active_ui->ime_comp_utf8, comp_len);
                        if (line_len > rel_c) {
                            std::memcpy(spliced + rel_c + comp_len, s + line_start + rel_c, line_len - rel_c);
                        }
                        const uint32_t spliced_len = line_len + comp_len;
                        bool first_sub = true;
                        bool found_caret = false;
                        for_each_wrapped_subline(ctx, desc.font_id, spliced, spliced_len, base_px, desc.letter_spacing, false, 4u, wrap_w,
                            [&](const WrappedSubline& sub) noexcept {
                                if (!first_sub) {
                                    pen_y += line_adv;
                                }
                                first_sub = false;
                                if (sub.width_px > max_w) {
                                    max_w = sub.width_px;
                                }
                                const uint32_t caret_spliced = rel_c + comp_cbyte;
                                const bool is_last_spliced = (sub.end >= spliced_len);
                                if (!found_caret && (caret_spliced < sub.end || (caret_spliced == sub.end && (is_last_spliced || comp_len == 0u)))) {
                                    found_caret = true;
                                    cy = pen_y;
                                    measure_line_segment_x(
                                        ctx, desc.font_id, spliced + sub.start, sub.end - sub.start,
                                        (caret_spliced > sub.start) ? (caret_spliced - sub.start) : 0u, 0u,
                                        base_px, desc.letter_spacing, false, false, &cx, nullptr, nullptr);
                                    const uint32_t cs = (rel_c > sub.start) ? rel_c : sub.start;
                                    const uint32_t ce = (rel_c + comp_len < sub.end) ? (rel_c + comp_len) : sub.end;
                                    if (ce > cs) {
                                        float c0x = 0.0f;
                                        float c1x = 0.0f;
                                        measure_line_segment_x(
                                            ctx, desc.font_id, spliced + sub.start, sub.end - sub.start,
                                            cs - sub.start, ce - sub.start,
                                            base_px, desc.letter_spacing, false, false, &c0x, &c1x, nullptr);
                                        cw = (c1x > c0x) ? (c1x - c0x) : 0.0f;
                                        caret_dx = (cx > c0x) ? (cx - c0x) : 0.0f;
                                        cx = c0x;
                                    } else {
                                        cw = 0.0f;
                                        caret_dx = 0.0f;
                                    }
                                }
                                return true;
                            });
                    }
                    ctx->frame_arena.rewind(sp_mark);
                } else {
                    bool first_sub = true;
                    for_each_wrapped_subline(ctx, desc.font_id, s + line_start, line_len, base_px, desc.letter_spacing, desc.password, 4u, wrap_w,
                        [&](const WrappedSubline& sub) noexcept {
                            if (!first_sub) {
                                pen_y += line_adv;
                            }
                            first_sub = false;
                            if (sub.width_px > max_w) {
                                max_w = sub.width_px;
                            }
                            return true;
                        });
                }
                if (line_end >= len) {
                    break;
                }
                line_start = line_end + 1u;
            }
        } else {
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
        }
    } else if (!desc.multiline) {
        measure_line_segment_x(
            ctx, desc.font_id, s, len, c_byte, 0u,
            base_px, desc.letter_spacing, desc.password, false, &cx, nullptr, &max_w);
    } else {
        uint32_t line_start = 0u;
        bool first_visual = true;
        for (;;) {
            uint32_t line_end = line_start;
            while (line_end < len && s[line_end] != '\n') {
                ++line_end;
            }
            const uint32_t line_len = line_end - line_start;
            if (wrap_w <= 0.0f) {
                if (!first_visual) {
                    pen_y += line_adv;
                }
                first_visual = false;
                float line_w = 0.0f;
                if (c_byte >= line_start && c_byte <= line_end) {
                    cy = pen_y;
                    measure_line_segment_x(
                        ctx, desc.font_id, s + line_start, line_len, c_byte - line_start, 0u,
                        base_px, desc.letter_spacing, desc.password, false, &cx, nullptr, &line_w);
                } else {
                    measure_line_segment_x(
                        ctx, desc.font_id, s + line_start, line_len, 0u, 0u,
                        base_px, desc.letter_spacing, desc.password, false, nullptr, nullptr, &line_w);
                }
                if (line_w > max_w) {
                    max_w = line_w;
                }
            } else {
                const bool caret_in_line = (c_byte >= line_start && c_byte <= line_end);
                const uint32_t rel = caret_in_line ? (c_byte - line_start) : 0u;
                bool caret_found = false;
                for_each_wrapped_subline(ctx, desc.font_id, s + line_start, line_len, base_px, desc.letter_spacing, desc.password, 4u, wrap_w,
                    [&](const WrappedSubline& sub) noexcept {
                        if (!first_visual) {
                            pen_y += line_adv;
                        }
                        first_visual = false;
                        if (sub.width_px > max_w) {
                            max_w = sub.width_px;
                        }
                        if (caret_in_line && !caret_found) {
                            const bool is_last = (sub.end >= line_len);
                            if (rel < sub.end || (rel == sub.end && (is_last || wrap_affinity))) {
                                caret_found = true;
                                cy = pen_y;
                                measure_line_segment_x(
                                    ctx, desc.font_id, s + line_start + sub.start, sub.end - sub.start,
                                    (rel > sub.start) ? (rel - sub.start) : 0u, 0u,
                                    base_px, desc.letter_spacing, desc.password, false, &cx, nullptr, nullptr);
                            }
                        }
                        return true;
                    });
            }
            if (line_end >= len) {
                break;
            }
            line_start = line_end + 1u;
        }
    }

    if (out_total_w) *out_total_w = max_w;
    if (out_total_h) *out_total_h = pen_y + line_h;
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
    const float wrap_w = text_input_wrap_width(desc, geom);
    const bool wrap_aff = (ws && wrap_w > 0.0f) ? ws->edit_wrap_affinity : false;
    float total_w = 0.0f;
    float total_h = 0.0f;
    float comp_x = 0.0f;
    float comp_y = 0.0f;
    float comp_w = 0.0f;
    float caret_x = 0.0f;
    float caret_y = 0.0f;
    measure_text_content_and_ime(
        ctx, desc, committed, cursor_byte, composing,
        &total_w, &total_h, &comp_x, &comp_y, &comp_w, &caret_x, &caret_y, wrap_w, wrap_aff);

    const float view_w = geom.clip_logical.w;
    const float view_h = geom.clip_logical.h;
    float sx = ws ? ws->edit_scroll_x : 0.0f;
    float sy = ws ? ws->edit_scroll_y : 0.0f;

    if (wrap_w > 0.0f || view_w <= 0.0f) {
        sx = 0.0f;
    } else {
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
    }

    if (desc.multiline && view_h > 0.0f) {
        const float max_sy = (total_h > view_h) ? (total_h - view_h) : 0.0f;
        if (is_focused && (!ws || ws->edit_reveal_caret)) {
            if (caret_y + geom.line_h > sy + view_h) {
                sy = (caret_y + geom.line_h) - view_h;
            }
            if (caret_y < sy) {
                sy = caret_y;
            }
        }
        sy = clamp_range(sy, 0.0f, max_sy);
        if(ws) {ws->edit_max_scroll_y=max_sy;ws->viewport_dim.y=view_h;}
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
    uint32_t tab_width,
    std::span<const TextInputHighlight> highlights,
    uint32_t byte_base) noexcept
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

    const auto cluster_color = [&](uint32_t lo, uint32_t hi) noexcept {
        Color color = tint;
        for (const TextInputHighlight& h : highlights)
            if (h.text_color.a && lo < h.byte_offset + h.byte_length && hi > h.byte_offset)
                color = h.text_color;
        return color;
    };
    if (mask_bullets) {
        const float bullet_adv_logical = primary.bullet_advance_em * base_px + letter_spacing;
        const char* p = s;
        const char* end = s + len;
        while (p < end) {
            const char* start = p;
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
                ctx, primary_slot, primary.bullet_glyph_id, phys_px, pen_x_px, base_y_px, 0.0f, 0.0f, clip_px,
                cluster_color(byte_base + static_cast<uint32_t>(start - s), byte_base + static_cast<uint32_t>(p - s)));
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
                cluster_color(byte_base + pos + g.cluster, byte_base + pos + g.cluster_end));
            *io_pen_x += g.x_advance_1px * base_px + letter_spacing;
        }
        pos = run_end;
    }
}

void emit_selection_bands(
    Context* ctx,
    uint16_t font_id,
    const char* seg,
    uint32_t seg_len,
    uint32_t sel_a,
    uint32_t sel_b,
    float base_px,
    float letter_spacing,
    bool mask_bullets,
    bool spacing_between_glyphs_only,
    uint32_t tab_width,
    float origin_x,
    float y_px,
    float h_px,
    float scroll_x,
    float dpi,
    const Rect& clip_rect,
    Color color,
    float radius_px,
    float nl_extra_logical) noexcept
{
    if (sel_b <= sel_a && nl_extra_logical <= 0.0f) return;
    const auto emit_band = [&](float left, float right) noexcept {
        if (right <= left) return;
        const Rect sel_px = {
            (origin_x + left - scroll_x) * dpi,
            y_px,
            (right - left) * dpi,
            h_px,
        };
        push_prim(ctx, Prim::make_rect(sel_px, clip_rect, color, Corners::all(radius_px), {}, 0.0f, 1.0f, 0.0f));
    };
    struct SelBandState {
        const decltype(emit_band)& emit;
        const char* text;
        uint32_t first;
        uint32_t last;
        float left = 0.0f;
        float right = 0.0f;
    };
    SelBandState st{ emit_band, seg ? seg : "", sel_a, sel_b };
    float total_w = 0.0f;
    if (seg && seg_len > 0u) {
        measure_line_segment_x(
            ctx, font_id, seg, seg_len, 0u, 0u,
            base_px, letter_spacing, mask_bullets, spacing_between_glyphs_only,
            nullptr, nullptr, &total_w, tab_width,
            (sel_b > sel_a)
                ? [](void* data, uint32_t a, uint32_t b, float pen, float advance, bool rtl) noexcept {
                      auto& visual = *static_cast<SelBandState*>(data);
                      if (visual.first >= b || visual.last <= a || advance <= 0.0f) return;
                      float left = pen;
                      float right = pen + advance;
                      if (visual.first > a || visual.last < b) {
                          const uint32_t cps = count_utf8_cps(visual.text + a, b - a);
                          const float count = static_cast<float>(cps > 0u ? cps : 1u);
                          const float f0 = static_cast<float>(count_utf8_cps(visual.text + a, std::max(visual.first, a) - a)) / count;
                          const float f1 = static_cast<float>(count_utf8_cps(visual.text + a, std::min(visual.last, b) - a)) / count;
                          left = pen + advance * (rtl ? 1.0f - f1 : f0);
                          right = pen + advance * (rtl ? 1.0f - f0 : f1);
                      }
                      if (left > visual.right + 0.001f || visual.left == visual.right) {
                          visual.emit(visual.left, visual.right);
                          visual.left = left;
                      }
                      visual.right = right;
                  }
                : nullptr,
            &st);
    }
    if (nl_extra_logical > 0.0f) {
        if (st.left < st.right && std::fabs(st.right - total_w) <= 0.01f) {
            st.right = total_w + nl_extra_logical;
        } else {
            emit_band(st.left, st.right);
            st.left = total_w;
            st.right = total_w + nl_extra_logical;
        }
    }
    emit_band(st.left, st.right);
}

void emit_text_input_command(
    Context* ctx,
    const TextInputExtra* ti,
    Rect bounds_logical,
    Rect box_px,
    const ClipEntry& outer_clip,
    float dpi) noexcept
{
    if (!ctx || !ti) {
        return;
    }
    const TextInputDesc& desc = ti->desc;
    WidgetState* ws = (desc.id != 0u) ? state_map_find(&ctx->active_ui->states, desc.id) : nullptr;
    const Rect box_logical = bounds_logical;
    if (ws) {
        ws->last_box_logical = box_logical;
    }

    const TextInnerGeom geom = compute_text_inner_geom(ctx, box_logical, desc);
    const Corners rad_px = scale_corners(desc.radius, dpi);
    const bool is_focused = ti->is_focused;
    const bool composing = is_focused && !desc.read_only && !desc.password && ctx->active_ui->ime_comp_len > 0u;

    if (is_focused && desc.focus_border_color.a > 0u && desc.focus_glow_blur > 0.0f) {
        const Color glow_col = Color::rgba8(desc.focus_border_color.r, desc.focus_border_color.g, desc.focus_border_color.b, 80u);
        const float glow_blur_px = desc.focus_glow_blur * dpi;
        push_prim(
            ctx,
            continuous_prim(Prim::make_shadow(box_px, outer_clip.rect, glow_col, rad_px, glow_blur_px, outer_clip.radius), desc.continuous_corners),
            nullptr,
            2.0f * glow_blur_px + 2.0f);
    }

    const Color resting_border = (ti->hover_t > 0.001f && desc.hover_border_color.a > 0u)
        ? Color::lerp_oklab(desc.border_color, desc.hover_border_color, ti->hover_t)
        : desc.border_color;
    const Color border_col = is_focused ? desc.focus_border_color : resting_border;
    const float border_w = (is_focused ? desc.focus_border_width : desc.border_width) * dpi;
    push_prim(
        ctx,
        continuous_prim(Prim::make_rect(box_px, outer_clip.rect, desc.background, rad_px, border_col, border_w, 1.0f, outer_clip.radius), desc.continuous_corners));

    if (desc.leading_icon_id != kInvalidIcon && is_icon_loaded(ctx, desc.leading_icon_id)) {
        const float icon_sz = std::fmax(12.0f, std::fmin((desc.font_size_px > 0.0f ? desc.font_size_px : 14.0f), 18.0f));
        const float icon_x = box_logical.x + ti->leading_icon_pad_left;
        const float icon_y = desc.multiline
            ? (box_logical.y + desc.padding.top + std::fmax(0.0f, (geom.line_h - icon_sz) * 0.5f))
            : (box_logical.y + (box_logical.h - icon_sz) * 0.5f);
        emit_vector_icon_quad(
            ctx,
            desc.leading_icon_id,
            Rect{ icon_x * dpi, icon_y * dpi, icon_sz * dpi, icon_sz * dpi },
            desc.leading_icon_color,
            outer_clip);
    }

    const float wrap_w = text_input_wrap_width(desc, geom);
    if (ws) {
        if (desc.multiline && desc.word_wrap && wrap_w > 0.0f) {
            if (std::fabs(ws->edit_wrap_width - wrap_w) > 0.5f) {
                ws->edit_wrap_width = wrap_w;
                request_redraw(ctx);
            }
        } else {
            ws->edit_wrap_width = 0.0f;
            ws->edit_wrap_affinity = false;
        }
    }

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
        .rect = rect_intersect(outer_clip.rect, layer_rect(ctx,inner_px)),
        .radius = 0.0f,
    };

    const float base_px = geom.font_px;
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
    const bool wrap_aff = (ws && wrap_w > 0.0f) ? ws->edit_wrap_affinity : false;

    const auto highlights = desc.highlights;
    const auto emit_highlights = [&](uint32_t start, uint32_t end, float x, float y) noexcept {
        if (y + geom.line_h < scroll_y || y > scroll_y + geom.clip_logical.h) return;
        for (const TextInputHighlight& h : highlights) {
            const uint32_t lo = std::max(start, h.byte_offset);
            const uint32_t hi = std::min(end, h.byte_offset + h.byte_length);
            if (lo >= hi || !(h.background.a || h.border_color.a)) continue;
            const auto pill = [&](float left, float right) noexcept {
                if (right <= left) return;
                const Rect bounds{(geom.origin_x + x + left - scroll_x) * dpi,
                                  (geom.origin_y + y - scroll_y) * dpi, (right - left) * dpi, geom.line_h * dpi};
                push_prim(ctx, Prim::make_rect(bounds, inner_clip.rect, h.background,
                    Corners::all(std::fmin(h.radius_px * dpi, std::fmin(bounds.w, bounds.h) * 0.5f)),
                    h.border_color, h.border_color.a ? dpi : 0.0f));
            };
            struct HighlightRun {
                const decltype(pill)& emit;
                const char* text;
                uint32_t first, last;
                float left = 0.0f, right = 0.0f;
            };
            HighlightRun run{pill, s + start, lo - start, hi - start};
            measure_line_segment_x(ctx, desc.font_id, run.text, end - start, 0u, 0u,
                base_px, desc.letter_spacing, desc.password, false, nullptr, nullptr, nullptr, 4u,
                [](void* data, uint32_t a, uint32_t b, float pen, float advance, bool rtl) noexcept {
                    auto& visual = *static_cast<HighlightRun*>(data);
                    if (visual.first >= b || visual.last <= a || advance <= 0.0f) return;
                    float left = pen, right = pen + advance;
                    if (visual.first > a || visual.last < b) {
                        const float count = static_cast<float>(count_utf8_cps(visual.text + a, b - a));
                        const float f0 = static_cast<float>(count_utf8_cps(visual.text + a, std::max(visual.first, a) - a)) / count;
                        const float f1 = static_cast<float>(count_utf8_cps(visual.text + a, std::min(visual.last, b) - a)) / count;
                        left = pen + advance * (rtl ? 1.0f - f1 : f0);
                        right = pen + advance * (rtl ? 1.0f - f0 : f1);
                    }
                    if (left > visual.right + 0.001f || visual.left == visual.right) {
                        visual.emit(visual.left, visual.right);
                        visual.left = left;
                    }
                    visual.right = right;
                }, &run);
            pill(run.left, run.right);
        }
    };
    const bool draw_selection = !composing && sel_max > sel_min && desc.selection_color.a > 0u;
    if (draw_selection || (!composing && !highlights.empty())) {
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
            const uint32_t line_len = le - ls;
            if (wrap_w <= 0.0f) {
                const uint32_t hs = (sel_min > ls) ? sel_min : ls;
                const uint32_t he = (sel_max < le) ? sel_max : le;
                const bool spans_nl = desc.multiline && sel_max > le && sel_min <= le;
                if (!composing && !highlights.empty()) emit_highlights(ls, le, 0.0f, line_y);
                if (draw_selection && (he > hs || spans_nl)) {
                    emit_selection_bands(
                        ctx, desc.font_id, s + ls, line_len, hs - ls, (he > ls) ? (he - ls) : 0u,
                        base_px, desc.letter_spacing, desc.password, false, 4u,
                        geom.origin_x, (geom.origin_y + line_y - scroll_y - 1.0f) * dpi,
                        (geom.line_h + 2.0f) * dpi, scroll_x, dpi,
                        inner_clip.rect, sel_col, 2.0f * dpi, spans_nl ? 6.0f : 0.0f);
                }
                line_y += geom.line_adv;
            } else {
                for_each_wrapped_subline(ctx, desc.font_id, s + ls, line_len, base_px, desc.letter_spacing, desc.password, 4u, wrap_w,
                    [&](const WrappedSubline& sub) noexcept {
                        const uint32_t sub_abs_s = ls + sub.start;
                        const uint32_t sub_abs_e = ls + sub.end;
                        const uint32_t hs = (sel_min > sub_abs_s) ? sel_min : sub_abs_s;
                        const uint32_t he = (sel_max < sub_abs_e) ? sel_max : sub_abs_e;
                        const bool is_last = (sub.end >= line_len);
                        const bool spans_nl = is_last && desc.multiline && sel_max > le && sel_min <= le;
                        if (!composing && !highlights.empty()) emit_highlights(sub_abs_s, sub_abs_e, 0.0f, line_y);
                        if (draw_selection && (he > hs || spans_nl)) {
                            emit_selection_bands(
                                ctx, desc.font_id, s + sub_abs_s, sub.end - sub.start,
                                hs - sub_abs_s, (he > sub_abs_s) ? (he - sub_abs_s) : 0u,
                                base_px, desc.letter_spacing, desc.password, false, 4u,
                                geom.origin_x, (geom.origin_y + line_y - scroll_y - 1.0f) * dpi,
                                (geom.line_h + 2.0f) * dpi, scroll_x, dpi,
                                inner_clip.rect, sel_col, 2.0f * dpi, spans_nl ? 6.0f : 0.0f);
                        }
                        line_y += geom.line_adv;
                        return true;
                    });
            }
            if (!desc.multiline || le >= len) {
                break;
            }
            pos = le + 1u;
        }
    }

    // 2. Emit placeholder or committed text + inline IME preedit composition
    float caret_vx = 0.0f;
    float caret_vy = 0.0f;
    if (len == 0u && !composing) {
        if (desc.placeholder.chars && desc.placeholder.length > 0u) {
            if (wrap_w <= 0.0f) {
                float px = 0.0f;
                float py = 0.0f;
                emit_utf8_glyph_run(
                    ctx, desc.font_id, desc.placeholder.chars, desc.placeholder.length,
                    base_px, desc.letter_spacing, ascender_px, geom.line_adv,
                    desc.placeholder_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
                    inner_clip, dpi, desc.multiline, false, &px, &py);
            } else {
                uint32_t pos = 0u;
                float py = 0.0f;
                while (pos <= desc.placeholder.length) {
                    uint32_t ls = 0u;
                    uint32_t le = 0u;
                    line_bounds_at(desc.placeholder.chars, desc.placeholder.length, pos, &ls, &le);
                    for_each_wrapped_subline(ctx, desc.font_id, desc.placeholder.chars + ls, le - ls, base_px, desc.letter_spacing, false, 4u, wrap_w,
                        [&](const WrappedSubline& sub) noexcept {
                            float px = 0.0f;
                            float sub_py = py;
                            emit_utf8_glyph_run(
                                ctx, desc.font_id, desc.placeholder.chars + ls + sub.start, sub.end - sub.start,
                                base_px, desc.letter_spacing, ascender_px, geom.line_adv,
                                desc.placeholder_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
                                inner_clip, dpi, false, false, &px, &sub_py);
                            py += geom.line_adv;
                            return true;
                        });
                    if (le >= desc.placeholder.length) {
                        break;
                    }
                    pos = le + 1u;
                }
            }
        }
    } else if (composing && wrap_w > 0.0f && desc.multiline) {
        const uint32_t comp_len = ctx->active_ui->ime_comp_len;
        const uint32_t comp_cbyte = utf8_clamp_boundary(ctx->active_ui->ime_comp_utf8, comp_len, ctx->active_ui->ime_comp_cursor_byte);
        uint32_t cl_end = comp_cbyte + ctx->active_ui->ime_comp_sel_len_byte;
        if (cl_end > comp_len) {
            cl_end = comp_len;
        }
        uint32_t pos = 0u;
        float py = 0.0f;
        bool caret_set = false;
        while (pos <= len) {
            uint32_t ls = 0u;
            uint32_t le = 0u;
            line_bounds_at(s, len, pos, &ls, &le);
            const uint32_t line_len = le - ls;
            if (c_byte >= ls && c_byte <= le) {
                const uint32_t rel_c = c_byte - ls;
                const size_t sp_mark = ctx->frame_arena.mark();
                char* spliced = ctx->frame_arena.alloc_uninit<char>(line_len + comp_len + 1u);
                if (spliced) {
                    if (rel_c > 0u) {
                        std::memcpy(spliced, s + ls, rel_c);
                    }
                    std::memcpy(spliced + rel_c, ctx->active_ui->ime_comp_utf8, comp_len);
                    if (line_len > rel_c) {
                        std::memcpy(spliced + rel_c + comp_len, s + ls + rel_c, line_len - rel_c);
                    }
                    const uint32_t spliced_len = line_len + comp_len;
                    const uint32_t comp_end_off = rel_c + comp_len;
                    const uint32_t cl_a = rel_c + comp_cbyte;
                    const uint32_t cl_b = rel_c + cl_end;
                    const uint32_t caret_spliced = rel_c + comp_cbyte;

                    for_each_wrapped_subline(ctx, desc.font_id, spliced, spliced_len, base_px, desc.letter_spacing, false, 4u, wrap_w,
                        [&](const WrappedSubline& sub) noexcept {
                            float px = 0.0f;
                            float sub_py = py;
                            if (sub.start < rel_c && sub.start < sub.end) {
                                const uint32_t p_end = (sub.end < rel_c) ? sub.end : rel_c;
                                if (!highlights.empty()) emit_highlights(ls + sub.start, ls + p_end, px, py);
                                emit_utf8_glyph_run(
                                    ctx, desc.font_id, spliced + sub.start, p_end - sub.start,
                                    base_px, desc.letter_spacing, ascender_px, geom.line_adv,
                                    desc.text_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
                                    inner_clip, dpi, false, false, &px, &sub_py, 4u, highlights, ls + sub.start);
                            }
                            const uint32_t cs = (sub.start > rel_c) ? sub.start : rel_c;
                            const uint32_t ce = (sub.end < comp_end_off) ? sub.end : comp_end_off;
                            if (ce > cs) {
                                const float comp_sx = px;
                                const uint32_t hs = (cl_a > cs) ? cl_a : cs;
                                const uint32_t he = (cl_b < ce) ? cl_b : ce;
                                if (he > hs) {
                                    float c0 = 0.0f;
                                    float c1 = 0.0f;
                                    measure_line_segment_x(
                                        ctx, desc.font_id, spliced + cs, ce - cs, hs - cs, he - cs,
                                        base_px, desc.letter_spacing, false, false, &c0, &c1, nullptr);
                                    const float cl_l = (c0 < c1) ? c0 : c1;
                                    const float cl_w = (c0 < c1) ? (c1 - c0) : (c0 - c1);
                                    if (cl_w > 0.0f) {
                                        const Rect clause_bg = {
                                            (geom.origin_x + comp_sx + cl_l - scroll_x) * dpi,
                                            (geom.origin_y + py - scroll_y - 1.0f) * dpi,
                                            cl_w * dpi,
                                            (geom.line_h + 2.0f) * dpi,
                                        };
                                        const Color clause_col = Color::rgba8(
                                            desc.composition_color.r, desc.composition_color.g, desc.composition_color.b, 48u);
                                        push_prim(ctx, Prim::make_rect(clause_bg, inner_clip.rect, clause_col, Corners::all(2.0f * dpi)));
                                    }
                                }
                                sub_py = py;
                                emit_utf8_glyph_run(
                                    ctx, desc.font_id, spliced + cs, ce - cs,
                                    base_px, desc.letter_spacing, ascender_px, geom.line_adv,
                                    desc.composition_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
                                    inner_clip, dpi, false, false, &px, &sub_py);
                                if (px > comp_sx) {
                                    const Rect uline = {
                                        (geom.origin_x + comp_sx - scroll_x) * dpi,
                                        (geom.origin_y + py + geom.line_h - scroll_y + 0.5f) * dpi,
                                        (px - comp_sx) * dpi,
                                        (1.5f * dpi > 1.0f) ? (1.5f * dpi) : 1.0f,
                                    };
                                    push_prim(ctx, Prim::make_rect(uline, inner_clip.rect, desc.composition_color));
                                }
                            }
                            if (!caret_set && (caret_spliced < sub.end || (caret_spliced == sub.end && (sub.end >= spliced_len || comp_len == 0u)))) {
                                caret_set = true;
                                caret_vy = py;
                                measure_line_segment_x(
                                    ctx, desc.font_id, spliced + sub.start, sub.end - sub.start,
                                    (caret_spliced > sub.start) ? (caret_spliced - sub.start) : 0u, 0u,
                                    base_px, desc.letter_spacing, false, false, &caret_vx, nullptr, nullptr);
                            }
                            if (sub.end > comp_end_off) {
                                const uint32_t ss = (sub.start > comp_end_off) ? sub.start : comp_end_off;
                                if (sub.end > ss) {
                                    sub_py = py;
                                    if (!highlights.empty()) emit_highlights(ls + ss - comp_len, ls + sub.end - comp_len, px, py);
                                    emit_utf8_glyph_run(
                                        ctx, desc.font_id, spliced + ss, sub.end - ss,
                                        base_px, desc.letter_spacing, ascender_px, geom.line_adv,
                                        desc.text_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
                                        inner_clip, dpi, false, false, &px, &sub_py, 4u, highlights, ls + ss - comp_len);
                                }
                            }
                            py += geom.line_adv;
                            return true;
                        });
                }
                ctx->frame_arena.rewind(sp_mark);
            } else {
                for_each_wrapped_subline(ctx, desc.font_id, s + ls, line_len, base_px, desc.letter_spacing, desc.password, 4u, wrap_w,
                    [&](const WrappedSubline& sub) noexcept {
                        float px = 0.0f;
                        float sub_py = py;
                        if (!highlights.empty()) emit_highlights(ls + sub.start, ls + sub.end, px, py);
                        emit_utf8_glyph_run(
                            ctx, desc.font_id, s + ls + sub.start, sub.end - sub.start,
                            base_px, desc.letter_spacing, ascender_px, geom.line_adv,
                            desc.text_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
                            inner_clip, dpi, false, desc.password, &px, &sub_py, 4u, highlights, ls + sub.start);
                        py += geom.line_adv;
                        return true;
                    });
            }
            if (le >= len) {
                break;
            }
            pos = le + 1u;
        }
    } else if (composing) {
        const auto emit_committed_highlights = [&](uint32_t start, uint32_t end, float x, float y) noexcept {
            if (highlights.empty()) return;
            while (start < end) {
                uint32_t stop = end;
                if (desc.multiline) {
                    const void* nl = std::memchr(s + start, '\n', end - start);
                    if (nl) stop = static_cast<uint32_t>(static_cast<const char*>(nl) - s);
                }
                emit_highlights(start, stop, x, y);
                if (stop == end) break;
                start = stop + 1u;
                x = 0.0f;
                y += geom.line_adv;
            }
        };
        emit_committed_highlights(0, c_byte, 0, 0);
        float pen_x = 0.0f;
        float pen_y = 0.0f;
        emit_utf8_glyph_run(
            ctx, desc.font_id, s, c_byte, base_px, desc.letter_spacing, ascender_px, geom.line_adv,
            desc.text_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
            inner_clip, dpi, desc.multiline, desc.password, &pen_x, &pen_y, 4u, highlights);

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
            emit_committed_highlights(c_byte, len, pen_x, pen_y);
            emit_utf8_glyph_run(
                ctx, desc.font_id, s + c_byte, len - c_byte, base_px, desc.letter_spacing, ascender_px, geom.line_adv,
                desc.text_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
                inner_clip, dpi, desc.multiline, desc.password, &pen_x, &pen_y, 4u, highlights, c_byte);
        }
    } else if (wrap_w <= 0.0f) {
        float pen_x = 0.0f;
        float pen_y = 0.0f;
        emit_utf8_glyph_run(
            ctx, desc.font_id, s, len, base_px, desc.letter_spacing, ascender_px, geom.line_adv,
            desc.text_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
            inner_clip, dpi, desc.multiline, desc.password, &pen_x, &pen_y, 4u, highlights);
        const TextVisualPos cpos = text_byte_to_visual(ctx, desc, ti->committed_text, c_byte);
        caret_vx = cpos.x;
        caret_vy = cpos.y;
    } else {
        uint32_t pos = 0u;
        float py = 0.0f;
        while (pos <= len) {
            uint32_t ls = 0u;
            uint32_t le = 0u;
            line_bounds_at(s, len, pos, &ls, &le);
            for_each_wrapped_subline(ctx, desc.font_id, s + ls, le - ls, base_px, desc.letter_spacing, desc.password, 4u, wrap_w,
                [&](const WrappedSubline& sub) noexcept {
                    float px = 0.0f;
                    float sub_py = py;
                    emit_utf8_glyph_run(
                        ctx, desc.font_id, s + ls + sub.start, sub.end - sub.start,
                        base_px, desc.letter_spacing, ascender_px, geom.line_adv,
                        desc.text_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
                        inner_clip, dpi, false, desc.password, &px, &sub_py, 4u, highlights, ls + sub.start);
                    py += geom.line_adv;
                    return true;
                });
            if (le >= len) {
                break;
            }
            pos = le + 1u;
        }
        const TextVisualPos cpos = text_byte_to_visual(ctx, desc, ti->committed_text, c_byte, wrap_w, wrap_aff);
        caret_vx = cpos.x;
        caret_vy = cpos.y;
    }

    // 2b. Emit inline predictive ghost text after caret (never mutates caller buffer)
    if (!composing && is_focused && !desc.read_only && !desc.password &&
        desc.ghost_text.chars && desc.ghost_text.length > 0u &&
        c_byte == len && c_byte == a_byte && desc.ghost_color.a > 0u)
    {
        float gpx = caret_vx;
        float gpy = caret_vy;
        emit_utf8_glyph_run(
            ctx, desc.font_id, desc.ghost_text.chars, desc.ghost_text.length,
            base_px, desc.letter_spacing, ascender_px, geom.line_adv,
            desc.ghost_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
            inner_clip, dpi, desc.multiline, false, &gpx, &gpy);
    }

    if (desc.multiline && ws) {
        const ScrollbarGeom sb = compute_vscrollbar_geom(ws);
        if (sb.visible) {
            const Theme& thm = active_theme(ctx);
            emit_scrollbar_overlay(
                ctx, desc.id, sb, false, outer_clip,
                thm.scrollbar_track, thm.scrollbar_thumb,
                thm.scrollbar_thumb_hover, thm.scrollbar_thumb_drag, dpi);
        }
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
    std::span<const uint32_t> over,
    uint32_t hovered_span_id) noexcept
{
    if (hovered_span_id != 0u) {
        if (WidgetState* sws = state_map_find(&ctx->active_ui->states, hovered_span_id)) {
            if (sws->is_user_element) {
                return sws;
            }
        }
    }
    for (int32_t i = static_cast<int32_t>(over.size()) - 1; i >= 0; --i) {
        WidgetState* ws = state_map_find(&ctx->active_ui->states, over[static_cast<size_t>(i)]);
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
        const TextUndoRecord& prev = slot->entries[(slot->head + slot->count - 1u) % slot->entries_cap];
        const bool continuous =
            (op_group == 1u && prev.cursor == prev.anchor && cursor == anchor &&
             len > prev.length && cursor == prev.cursor + (len - prev.length)) ||
            (op_group == 2u && prev.cursor == prev.anchor && cursor == anchor &&
             len < prev.length && cursor + (prev.length - len) == prev.cursor) ||
            (op_group == 3u && prev.cursor == prev.anchor && cursor == anchor &&
             len < prev.length && cursor == prev.cursor);
        if (continuous) {
            undo_pop_tail(slot);
        }
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
    const uint32_t first_bytes = rec.length < slot->bytes_cap - rec.offset ? rec.length : slot->bytes_cap - rec.offset;
    uint32_t hash = fnv1a_32_step(2166136261u, slot->bytes + rec.offset, first_bytes);
    if (first_bytes < rec.length) hash = fnv1a_32_step(hash, slot->bytes, rec.length - first_bytes);
    if (hash != rec.hash) return false;
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

// Internal regression exercises wrap, coalescing, redo truncation, and corruption rejection.
bool verify_text_undo_ring() noexcept {
    TextUndoSlot slot={};slot.entries_cap=512;slot.bytes_cap=262144;
    slot.entries=static_cast<TextUndoRecord*>(std::calloc(slot.entries_cap,sizeof(TextUndoRecord)));
    slot.bytes=static_cast<char*>(std::malloc(slot.bytes_cap));
    char* source=static_cast<char*>(std::malloc(60001));char* restored=static_cast<char*>(std::malloc(60001));
    if(!slot.entries || !slot.bytes || !source || !restored) {std::free(slot.entries);std::free(slot.bytes);std::free(source);std::free(restored);return false;}
    bool ok=true;uint32_t random=1234567;
    for(uint32_t step=0;step<400 && ok;++step) {
        random=random*1664525u+1013904223u;const uint32_t length=1+random%60000;
        std::memset(source,'A'+int(step%26),length);
        if(slot.count>2 && step%7==0)slot.cur=slot.count/2;
        undo_push_snapshot(&slot,source,length,length,0,float(step)*.1f,uint8_t(step%3==0?0:1));
        const uint32_t current=slot.cur;const uint8_t group=slot.last_op_group;
        for(uint32_t i=0;i<slot.count;++i) {uint32_t n=0,c=0,a=0;ok &= undo_restore(&slot,i,restored,60001,&n,&c,&a);}
        slot.cur=current;slot.last_op_group=group;
    }
    if(ok && slot.count) {
        const auto& rec=slot.entries[slot.head];slot.bytes[rec.offset]^=1;
        uint32_t n=0,c=0,a=0;restored[0]='!';const uint32_t current=slot.cur;
        ok=!undo_restore(&slot,0,restored,60001,&n,&c,&a) && restored[0]=='!' && slot.cur==current;
    }
    std::free(slot.entries);std::free(slot.bytes);std::free(source);std::free(restored);return ok;
}

} // namespace etch
