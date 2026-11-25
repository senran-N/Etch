// Caller-owned terminal cells and floating snap sheets; compiled via etch_unity.cpp.
namespace etch {
namespace {
Color terminal_color(TerminalColor c, Color fallback, std::span<const Color> palette) noexcept {
    constexpr uint32_t ansi[16] = {0x16161e,0xcd3131,0x0dbc79,0xe5e510,0x2472c8,0xbc3fbc,0x11a8cd,0xe5e5e5,
        0x666666,0xf14c4c,0x23d18b,0xf5f543,0x3b8eea,0xd670d6,0x29b8db,0xffffff};
    uint32_t rgb = c.value;
    if (c.mode == TerminalColorMode::Default) return fallback;
    if (c.mode == TerminalColorMode::Ansi16) {
        const uint32_t idx = c.value & 15u;
        if (palette.size() >= 16u) return palette[idx];
        rgb = ansi[idx];
    }
    if (c.mode == TerminalColorMode::Ansi256) {
        const uint32_t n = c.value & 255u;
        if (n < 16u) {
            if (palette.size() >= 16u) return palette[n];
            rgb = ansi[n];
        } else if (n >= 232u) {
            const uint32_t v = 8u + (n - 232u) * 10u;
            rgb = (v << 16) | (v << 8) | v;
        } else {
            const uint32_t cube = n - 16u;
            const auto level = [](uint32_t v) noexcept { return v ? 55u + 40u * v : 0u; };
            rgb = (level(cube / 36u) << 16) | (level(cube / 6u % 6u) << 8) | level(cube % 6u);
        }
    }
    return Color::rgba8(uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb), 255u);
}
uint32_t terminal_encode(char* out, uint32_t cp) noexcept {
    if (cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) cp=0xfffd;
    if (cp<0x80) {out[0]=char(cp ? cp : ' ');return 1;}
    if (cp<0x800) {out[0]=char(0xc0|(cp>>6));out[1]=char(0x80|(cp&63));return 2;}
    if (cp<0x10000) {out[0]=char(0xe0|(cp>>12));out[1]=char(0x80|((cp>>6)&63));out[2]=char(0x80|(cp&63));return 3;}
    out[0]=char(0xf0|(cp>>18));out[1]=char(0x80|((cp>>12)&63));out[2]=char(0x80|((cp>>6)&63));out[3]=char(0x80|(cp&63));return 4;
}
bool terminal_grid_valid(const TerminalGridDesc& d, size_t count) noexcept {
    return desktop_frame(d.id) && d.columns>0 && d.columns<=4096 && d.rows>0 && d.rows<=4096 &&
        d.rows<=count/d.columns && std::isfinite(d.cell_width_px) && d.cell_width_px>0 && d.cell_width_px<=1024 &&
        std::isfinite(d.line_height_px) && d.line_height_px>0 && d.line_height_px<=1024 &&
        std::isfinite(d.font_size_px) && d.font_size_px>0 && d.font_size_px<=512 &&
        std::isfinite(d.cursor_motion.response) && d.cursor_motion.response>0 &&
        std::isfinite(d.cursor_motion.damping_ratio) && d.cursor_motion.damping_ratio>=0;
}
using TerminalCellReader = TerminalCell (*)(const void*,size_t) noexcept;
TerminalCell read_terminal_cell(const void* cells,size_t index) noexcept {return static_cast<const TerminalCell*>(cells)[index];}
bool terminal_cell_selected_coords(int32_t sr, int32_t sc, int32_t er, int32_t ec, bool block, uint32_t col, uint32_t row) noexcept {
    if (sr < 0 || er < 0 || sc < 0 || ec < 0) return false;
    const int32_t r = static_cast<int32_t>(row), c = static_cast<int32_t>(col);
    if (block) {
        if (sr > er) { const int32_t t = sr; sr = er; er = t; }
        if (sc > ec) { const int32_t t = sc; sc = ec; ec = t; }
        return r >= sr && r <= er && c >= sc && c <= ec;
    }
    if (sr > er || (sr == er && sc > ec)) {
        const int32_t tr = sr; sr = er; er = tr;
        const int32_t tc = sc; sc = ec; ec = tc;
    }
    if (r < sr || r > er) return false;
    if (sr == er) return c >= sc && c <= ec;
    if (r == sr) return c >= sc;
    if (r == er) return c <= ec;
    return true;
}
bool terminal_word_char(uint32_t cp) noexcept {
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') ||
           cp == '_' || cp == '-' || cp == '.' || cp == '/' || cp == ':' || cp == '~' || cp >= 0x80u;
}
constexpr uint8_t tbox(uint8_t l, uint8_t r, uint8_t u, uint8_t d) noexcept {
    return static_cast<uint8_t>((l & 3u) | ((r & 3u) << 2u) | ((u & 3u) << 4u) | ((d & 3u) << 6u));
}
// Lookup table for Unicode Box Drawing U+2500..U+257F (0 = let font render, e.g. diagonals/dashes).
constexpr uint8_t kTermBoxArms[128] = {
    tbox(1,1,0,0), tbox(2,2,0,0), tbox(0,0,1,1), tbox(0,0,2,2), 0,0,0,0, 0,0,0,0,
    tbox(0,1,0,1), tbox(0,2,0,1), tbox(0,1,0,2), tbox(0,2,0,2),
    tbox(1,0,0,1), tbox(2,0,0,1), tbox(1,0,0,2), tbox(2,0,0,2),
    tbox(0,1,1,0), tbox(0,2,1,0), tbox(0,1,2,0), tbox(0,2,2,0),
    tbox(1,0,1,0), tbox(2,0,1,0), tbox(1,0,2,0), tbox(2,0,2,0),
    tbox(0,1,1,1), tbox(0,2,1,1), tbox(0,1,2,1), tbox(0,1,1,2),
    tbox(0,1,2,2), tbox(0,2,2,1), tbox(0,2,1,2), tbox(0,2,2,2),
    tbox(1,0,1,1), tbox(2,0,1,1), tbox(1,0,2,1), tbox(1,0,1,2),
    tbox(1,0,2,2), tbox(2,0,2,1), tbox(2,0,1,2), tbox(2,0,2,2),
    tbox(1,1,0,1), tbox(2,1,0,1), tbox(1,2,0,1), tbox(2,2,0,1),
    tbox(1,1,0,2), tbox(2,1,0,2), tbox(1,2,0,2), tbox(2,2,0,2),
    tbox(1,1,1,0), tbox(2,1,1,0), tbox(1,2,1,0), tbox(2,2,1,0),
    tbox(1,1,2,0), tbox(2,1,2,0), tbox(1,2,2,0), tbox(2,2,2,0),
    tbox(1,1,1,1), tbox(2,1,1,1), tbox(1,2,1,1), tbox(2,2,1,1),
    tbox(1,1,2,1), tbox(1,1,1,2), tbox(1,1,2,2), tbox(2,1,2,1),
    tbox(1,2,2,1), tbox(2,1,1,2), tbox(1,2,1,2), tbox(2,2,2,1),
    tbox(2,2,1,2), tbox(2,1,2,2), tbox(1,2,2,2), tbox(2,2,2,2),
    0,0,0,0,
    tbox(3,3,0,0), tbox(0,0,3,3),
    tbox(0,3,0,1), tbox(0,1,0,3), tbox(0,3,0,3),
    tbox(3,0,0,1), tbox(1,0,0,3), tbox(3,0,0,3),
    tbox(0,3,1,0), tbox(0,1,3,0), tbox(0,3,3,0),
    tbox(3,0,1,0), tbox(1,0,3,0), tbox(3,0,3,0),
    tbox(0,3,1,1), tbox(0,1,3,3), tbox(0,3,3,3),
    tbox(3,0,1,1), tbox(1,0,3,3), tbox(3,0,3,3),
    tbox(3,3,0,1), tbox(1,1,0,3), tbox(3,3,0,3),
    tbox(3,3,1,0), tbox(1,1,3,0), tbox(3,3,3,0),
    tbox(3,3,1,1), tbox(1,1,3,3), tbox(3,3,3,3),
    tbox(0,1,0,1), tbox(1,0,0,1), tbox(1,0,1,0), tbox(0,1,1,0), // 256D..2570 rounded corners
    0,0,0,
    tbox(1,0,0,0), tbox(0,0,1,0), tbox(0,1,0,0), tbox(0,0,0,1),
    tbox(2,0,0,0), tbox(0,0,2,0), tbox(0,2,0,0), tbox(0,0,0,2),
    tbox(1,2,0,0), tbox(0,0,1,2), tbox(2,1,0,0), tbox(0,0,2,1),
};
bool terminal_is_vector_glyph(uint32_t cp) noexcept {
    if (cp >= 0x2500u && cp <= 0x257Fu) return kTermBoxArms[cp - 0x2500u] != 0u;
    if (cp >= 0x2580u && cp <= 0x259Fu) return true;
    if (cp >= 0x2800u && cp <= 0x28FFu) return true; // Braille patterns
    if ((cp >= 0xE0B0u && cp <= 0xE0B3u) || cp == 0xE0B4u || cp == 0xE0B6u) return true; // Powerline separators
    return false;
}
void terminal_draw_vector_cell(uint32_t cp, float x, float y, float cw, float ch, Color fg) noexcept {
    if (cp >= 0x2800u && cp <= 0x28FFu) {
        const uint32_t bits = cp - 0x2800u;
        if (bits == 0u) return;
        // Standard 2x4 Braille dot mapping: bit0..2 left col rows 0..2, bit3..5 right col rows 0..2, bit6 left row 3, bit7 right row 3.
        static constexpr uint8_t kDotCol[8] = { 0, 0, 0, 1, 1, 1, 0, 1 };
        static constexpr uint8_t kDotRow[8] = { 0, 1, 2, 0, 1, 2, 3, 3 };
        const float col_w = cw * 0.5f;
        const float row_h = ch * 0.25f;
        const float dot = std::fmax(1.5f, std::fmin(col_w, row_h) * 0.58f);
        const Corners cr = Corners::all(dot * 0.5f);
        for (uint32_t b = 0u; b < 8u; ++b) {
            if ((bits & (1u << b)) == 0u) continue;
            const float dx = x + (float(kDotCol[b]) + 0.5f) * col_w - dot * 0.5f;
            const float dy = y + (float(kDotRow[b]) + 0.5f) * row_h - dot * 0.5f;
            draw_rect({.rect={dx, dy, dot, dot}, .fill=fg, .radius=cr});
        }
        return;
    }
    if ((cp >= 0xE0B0u && cp <= 0xE0B3u) || cp == 0xE0B4u || cp == 0xE0B6u) {
        const float lw = std::fmax(1.0f, std::round(cw * 0.14f));
        if (cp == 0xE0B0u) {
            const Vec2 pts[3] = { {x, y}, {x + cw, y + ch * 0.5f}, {x, y + ch} };
            draw_filled_polygon({.points=pts, .fill=fg});
        } else if (cp == 0xE0B1u) {
            const Vec2 pts[3] = { {x, y}, {x + cw * 0.9f, y + ch * 0.5f}, {x, y + ch} };
            draw_polyline({.points=pts, .color=fg, .thickness=lw});
        } else if (cp == 0xE0B2u) {
            const Vec2 pts[3] = { {x + cw, y}, {x, y + ch * 0.5f}, {x + cw, y + ch} };
            draw_filled_polygon({.points=pts, .fill=fg});
        } else if (cp == 0xE0B3u) {
            const Vec2 pts[3] = { {x + cw, y}, {x + cw * 0.1f, y + ch * 0.5f}, {x + cw, y + ch} };
            draw_polyline({.points=pts, .color=fg, .thickness=lw});
        } else if (cp == 0xE0B4u) {
            const float r = ch * 0.5f;
            draw_rect({.rect={x, y, cw, ch}, .fill=fg, .radius={0.0f, r, r, 0.0f}});
        } else {
            const float r = ch * 0.5f;
            draw_rect({.rect={x, y, cw, ch}, .fill=fg, .radius={r, 0.0f, 0.0f, r}});
        }
        return;
    }
    if (cp >= 0x2580u && cp <= 0x259Fu) {
        if (cp == 0x2580u) {
            draw_rect({.rect={x, y, cw, ch * 0.5f}, .fill=fg});
        } else if (cp >= 0x2581u && cp <= 0x2588u) {
            const float frac = float(cp - 0x2580u) / 8.0f;
            const float bh = ch * frac;
            draw_rect({.rect={x, y + ch - bh, cw, bh}, .fill=fg});
        } else if (cp >= 0x2589u && cp <= 0x258Fu) {
            const float frac = float(0x2590u - cp) / 8.0f;
            draw_rect({.rect={x, y, cw * frac, ch}, .fill=fg});
        } else if (cp == 0x2590u) {
            draw_rect({.rect={x + cw * 0.5f, y, cw * 0.5f, ch}, .fill=fg});
        } else if (cp >= 0x2591u && cp <= 0x2593u) {
            const uint32_t mult = (cp == 0x2591u) ? 64u : ((cp == 0x2592u) ? 128u : 192u);
            const Color shade = Color::rgba8(fg.r, fg.g, fg.b, static_cast<uint8_t>((uint32_t(fg.a) * mult) / 255u));
            draw_rect({.rect={x, y, cw, ch}, .fill=shade});
        } else if (cp == 0x2594u) {
            draw_rect({.rect={x, y, cw, ch * 0.125f}, .fill=fg});
        } else if (cp == 0x2595u) {
            draw_rect({.rect={x + cw * 0.875f, y, cw * 0.125f, ch}, .fill=fg});
        } else {
            static constexpr uint8_t kQuadMask[10] = { 4, 8, 1, 13, 9, 7, 11, 2, 6, 14 }; // bit0=UL,1=UR,2=LL,3=LR
            const uint8_t m = kQuadMask[cp - 0x2596u];
            const float hw = cw * 0.5f, hh = ch * 0.5f;
            if (m & 1u) draw_rect({.rect={x, y, hw, hh}, .fill=fg});
            if (m & 2u) draw_rect({.rect={x + hw, y, hw, hh}, .fill=fg});
            if (m & 4u) draw_rect({.rect={x, y + hh, hw, hh}, .fill=fg});
            if (m & 8u) draw_rect({.rect={x + hw, y + hh, hw, hh}, .fill=fg});
        }
        return;
    }
    const uint8_t arms = kTermBoxArms[cp - 0x2500u];
    const uint8_t l = arms & 3u, r = (arms >> 2u) & 3u, u = (arms >> 4u) & 3u, d = (arms >> 6u) & 3u;
    const float lw = std::fmax(1.0f, std::round(cw * 0.12f));
    const float hw = std::fmax(2.0f, lw * 2.0f);
    const float gap = std::fmax(1.0f, lw);
    const float cx = x + cw * 0.5f;
    const float cy = y + ch * 0.5f;
    const float h_half = (l == 3u || r == 3u) ? (lw + gap * 0.5f) : ((l == 2u || r == 2u) ? hw * 0.5f : lw * 0.5f);
    const float v_half = (u == 3u || d == 3u) ? (lw + gap * 0.5f) : ((u == 2u || d == 2u) ? hw * 0.5f : lw * 0.5f);
    const auto draw_h = [&](uint8_t style, float x0, float x1) noexcept {
        if (style == 0u || x1 <= x0) return;
        if (style == 3u) {
            draw_rect({.rect={x0, cy - gap * 0.5f - lw, x1 - x0, lw}, .fill=fg});
            draw_rect({.rect={x0, cy + gap * 0.5f, x1 - x0, lw}, .fill=fg});
        } else {
            const float t = (style == 2u) ? hw : lw;
            draw_rect({.rect={x0, cy - t * 0.5f, x1 - x0, t}, .fill=fg});
        }
    };
    const auto draw_v = [&](uint8_t style, float y0, float y1) noexcept {
        if (style == 0u || y1 <= y0) return;
        if (style == 3u) {
            draw_rect({.rect={cx - gap * 0.5f - lw, y0, lw, y1 - y0}, .fill=fg});
            draw_rect({.rect={cx + gap * 0.5f, y0, lw, y1 - y0}, .fill=fg});
        } else {
            const float t = (style == 2u) ? hw : lw;
            draw_rect({.rect={cx - t * 0.5f, y0, t, y1 - y0}, .fill=fg});
        }
    };
    if (l) draw_h(l, x, cx + v_half);
    if (r) draw_h(r, cx - v_half, x + cw);
    if (u) draw_v(u, y, cy + h_half);
    if (d) draw_v(d, cy - h_half, y + ch);
}
TerminalGridResult terminal_grid_run(const TerminalGridDesc& d,const void* cells,size_t count,TerminalCellReader read) noexcept {
    Context* ctx=current_context();
    auto& ui=*ctx->active_ui;
    const Theme& theme=active_theme(ctx);
    const Color fg=resolve_theme_color(d.foreground,theme.text_primary),bg=resolve_theme_color(d.background,theme.bg_canvas);
    const Color sel_col=resolve_theme_color(d.selection_color,theme.selection);
    const Color match_col = d.search_match_color.is_theme_auto() ? Color::rgba8(245, 196, 66, 82) : d.search_match_color;
    const Color active_match_col = d.search_active_color.is_theme_auto() ? Color::rgba8(255, 146, 43, 152) : d.search_active_color;
    const size_t ring_rows=count/d.columns;
    const Rect prev_bounds = element_bounds(d.id);
    const uint32_t vis_cols = (d.auto_fit_container && prev_bounds.w > d.cell_width_px)
        ? static_cast<uint32_t>(std::floor(prev_bounds.w / d.cell_width_px))
        : d.columns;
    const uint32_t vis_rows = (d.auto_fit_container && prev_bounds.h > d.line_height_px)
        ? static_cast<uint32_t>(std::floor(prev_bounds.h / d.line_height_px))
        : d.rows;

    TerminalGridResult res = {};
    res.visible_columns = vis_cols > 0u ? vis_cols : 1u;
    res.visible_rows = vis_rows > 0u ? vis_rows : 1u;
    res.hovered = pointer_over_id(&ui, d.id);
    res.clicked = clicked(d.id);
    res.right_clicked = right_clicked(d.id);
    if (res.hovered) {
        res.wheel_delta_y = ui.frame_scroll_delta.y;
    }
    WidgetState* term_ws = state_map_get_or_insert(&ui.states, d.id, ui.frame_index);
    WidgetCold* term_cold = ensure_widget_cold(term_ws);
    int32_t auto_url_row = -1;
    uint32_t auto_url_col0 = 0u;
    uint32_t auto_url_col1 = 0u;
    if (res.hovered && prev_bounds.w > 0.0f && prev_bounds.h > 0.0f && ui.has_pointer) {
        const int32_t hc = static_cast<int32_t>(std::floor((ui.pointer_pos.x - prev_bounds.x) / d.cell_width_px));
        const int32_t hr = static_cast<int32_t>(std::floor((ui.pointer_pos.y - prev_bounds.y) / d.line_height_px));
        if (hc >= 0 && uint32_t(hc) < d.columns && hr >= 0 && uint32_t(hr) < d.rows) {
            res.hovered_col = hc;
            res.hovered_row = hr;
            const size_t row_off = ((size_t(d.first_row) + size_t(hr)) % ring_rows) * d.columns;
            const size_t cell_idx = row_off + size_t(hc);
            res.hovered_hyperlink_id = read(cells, cell_idx).hyperlink_id;
            // Auto-detect http:// or https:// URL spanning the hovered cell.
            auto is_url_cp = [](uint32_t cp) noexcept -> bool {
                return cp > 0x20u && cp < 0x7Fu &&
                       cp != '"' && cp != '\'' && cp != '<' && cp != '>' && cp != '`' && cp != '\\';
            };
            auto match_scheme = [&](uint32_t col, uint32_t* out_scheme_len) noexcept -> bool {
                static constexpr const char kHttps[] = "https://";
                static constexpr const char kHttp[]  = "http://";
                if (col + 8u <= d.columns) {
                    bool m = true;
                    for (uint32_t k = 0u; k < 8u; ++k) {
                        if (read(cells, row_off + col + k).codepoint != uint32_t(kHttps[k])) { m = false; break; }
                    }
                    if (m) { *out_scheme_len = 8u; return true; }
                }
                if (col + 7u <= d.columns) {
                    bool m = true;
                    for (uint32_t k = 0u; k < 7u; ++k) {
                        if (read(cells, row_off + col + k).codepoint != uint32_t(kHttp[k])) { m = false; break; }
                    }
                    if (m) { *out_scheme_len = 7u; return true; }
                }
                return false;
            };
            for (uint32_t scan = 0u; scan < d.columns; ) {
                uint32_t slen = 0u;
                if (!match_scheme(scan, &slen)) { ++scan; continue; }
                uint32_t end_col = scan + slen;
                while (end_col < d.columns && is_url_cp(read(cells, row_off + end_col).codepoint)) ++end_col;
                while (end_col > scan + slen) {
                    const uint32_t tail = read(cells, row_off + end_col - 1u).codepoint;
                    if (tail == '.' || tail == ',' || tail == ';' || tail == ':' || tail == '!' || tail == '?' || tail == ')' || tail == ']') {
                        --end_col;
                    } else {
                        break;
                    }
                }
                if (end_col > scan + slen && uint32_t(hc) >= scan && uint32_t(hc) < end_col) {
                    const uint32_t ulen = end_col - scan;
                    if (char* ubuf = ctx->frame_arena.alloc<char>(size_t(ulen) + 1u)) {
                        for (uint32_t k = 0u; k < ulen; ++k) {
                            ubuf[k] = static_cast<char>(read(cells, row_off + scan + k).codepoint & 0x7Fu);
                        }
                        ubuf[ulen] = '\0';
                        res.hovered_url = { ubuf, ulen };
                        auto_url_row = hr;
                        auto_url_col0 = scan;
                        auto_url_col1 = end_col;
                    }
                    break;
                }
                scan = end_col > scan ? end_col : (scan + 1u);
            }
            if (res.clicked) {
                res.clicked_col = hc;
                res.clicked_row = hr;
                res.clicked_hyperlink_id = res.hovered_hyperlink_id;
                res.clicked_url = res.hovered_url;
            }
            set_cursor((res.hovered_hyperlink_id != 0u || res.hovered_url.length > 0u) ? Cursor::Hand : Cursor::IBeam);
        }
    }
    if (d.interactive_selection && term_ws && term_cold && prev_bounds.w > 0.0f && prev_bounds.h > 0.0f && ui.has_pointer) {
        const auto clamp_col = [&](float px) noexcept -> int32_t {
            const int32_t c = static_cast<int32_t>(std::floor((px - prev_bounds.x) / d.cell_width_px));
            return c < 0 ? 0 : (uint32_t(c) >= d.columns ? int32_t(d.columns - 1u) : c);
        };
        const auto clamp_row = [&](float py) noexcept -> int32_t {
            const int32_t r = static_cast<int32_t>(std::floor((py - prev_bounds.y) / d.line_height_px));
            return r < 0 ? 0 : (uint32_t(r) >= d.rows ? int32_t(d.rows - 1u) : r);
        };
        if (term_ws->pressed_this_frame && res.hovered) {
            const int32_t hc = clamp_col(ui.pointer_pos.x);
            const int32_t hr = clamp_row(ui.pointer_pos.y);
            if (ui.last_mouse_clicks >= 3u) {
                term_cold->term_sel_start_col = 0;
                term_cold->term_sel_start_row = hr;
                term_cold->term_sel_end_col = static_cast<int32_t>(d.columns - 1u);
                term_cold->term_sel_end_row = hr;
                term_cold->term_sel_active = true;
                term_cold->term_sel_dragging = false;
                res.selection_changed = true;
            } else if (ui.last_mouse_clicks == 2u) {
                const size_t row_off = ((size_t(d.first_row) + size_t(hr)) % ring_rows) * d.columns;
                const bool target_word = terminal_word_char(read(cells, row_off + size_t(hc)).codepoint);
                int32_t s0 = hc, s1 = hc;
                while (s0 > 0 && terminal_word_char(read(cells, row_off + size_t(s0 - 1)).codepoint) == target_word) --s0;
                while (uint32_t(s1 + 1) < d.columns && terminal_word_char(read(cells, row_off + size_t(s1 + 1)).codepoint) == target_word) ++s1;
                term_cold->term_sel_start_col = s0;
                term_cold->term_sel_start_row = hr;
                term_cold->term_sel_end_col = s1;
                term_cold->term_sel_end_row = hr;
                term_cold->term_sel_active = true;
                term_cold->term_sel_dragging = false;
                res.selection_changed = true;
            } else {
                const bool was_active = term_cold->term_sel_active;
                term_cold->term_sel_start_col = hc;
                term_cold->term_sel_start_row = hr;
                term_cold->term_sel_end_col = hc;
                term_cold->term_sel_end_row = hr;
                term_cold->term_sel_active = false;
                term_cold->term_sel_dragging = true;
                if (was_active) res.selection_changed = true;
            }
            request_redraw(ctx);
        } else if (term_cold->term_sel_dragging && ui.active_id == d.id && term_ws->pressed_down) {
            const int32_t dc = clamp_col(ui.pointer_pos.x);
            const int32_t dr = clamp_row(ui.pointer_pos.y);
            if (dc != term_cold->term_sel_end_col || dr != term_cold->term_sel_end_row) {
                term_cold->term_sel_end_col = dc;
                term_cold->term_sel_end_row = dr;
                term_cold->term_sel_active = true;
                res.selection_changed = true;
                request_redraw(ctx);
            }
        } else if (term_cold->term_sel_dragging) {
            term_cold->term_sel_dragging = false;
        }
    }
    const bool ext_sel = (d.selection_start_row >= 0 && d.selection_end_row >= 0 &&
                          d.selection_start_col >= 0 && d.selection_end_col >= 0);
    const bool use_internal_sel = !ext_sel && d.interactive_selection && term_cold && term_cold->term_sel_active;
    const bool has_sel = ext_sel || use_internal_sel;
    const int32_t eff_sc = ext_sel ? d.selection_start_col : (use_internal_sel ? term_cold->term_sel_start_col : -1);
    const int32_t eff_sr = ext_sel ? d.selection_start_row : (use_internal_sel ? term_cold->term_sel_start_row : -1);
    const int32_t eff_ec = ext_sel ? d.selection_end_col : (use_internal_sel ? term_cold->term_sel_end_col : -1);
    const int32_t eff_er = ext_sel ? d.selection_end_row : (use_internal_sel ? term_cold->term_sel_end_row : -1);
    if (has_sel) {
        res.has_selection = true;
        res.sel_start_col = eff_sc;
        res.sel_start_row = eff_sr;
        res.sel_end_col = eff_ec;
        res.sel_end_row = eff_er;
    }
    const uint32_t active_hl = d.hovered_hyperlink_id != 0u ? d.hovered_hyperlink_id : res.hovered_hyperlink_id;
    const uint16_t italic_font = (d.font_italic_id != kInvalidFont && is_font_loaded(ctx, d.font_italic_id))
        ? d.font_italic_id : d.font_id;

    begin_box({.id=d.id,
        .width=d.auto_fit_container ? Sizing::grow() : Sizing::fixed(float(d.columns)*d.cell_width_px),
        .height=d.auto_fit_container ? Sizing::grow() : Sizing::fixed(float(d.rows)*d.line_height_px),
        .background=bg,.clip_children=true,.focusable=d.focusable});
    const bool is_focused = d.focusable && focused(d.id);
    res.focused = is_focused;
    if (d.focusable) {
        if (WidgetState* fws = state_map_find(&ui.states, d.id)) {
            fws->is_text_input_writable = true;
        }
    }
    if (d.mouse_reporting && term_ws && term_cold) {
        const bool ldown = term_ws->pressed_down;
        const bool rdown = ui.prev_right_down && res.hovered;
        const uint8_t cur_btn = ldown ? 1u : (rdown ? 3u : 0u);
        if (res.hovered && (term_ws->pressed_this_frame || res.right_clicked)) {
            res.mouse_down = true;
            res.mouse_button = term_ws->pressed_this_frame ? 1u : 3u;
            term_cold->term_mouse_down = true;
            term_cold->term_mouse_btn = res.mouse_button;
        } else if (term_cold->term_mouse_down && cur_btn == 0u) {
            res.mouse_released = true;
            res.mouse_button = term_cold->term_mouse_btn;
            term_cold->term_mouse_down = false;
            term_cold->term_mouse_btn = 0u;
        }
        if (res.hovered && res.hovered_col >= 0 && res.hovered_row >= 0 &&
            (res.hovered_col != term_cold->term_last_col || res.hovered_row != term_cold->term_last_row)) {
            res.mouse_moved = true;
            if (!res.mouse_down && !res.mouse_released) {
                res.mouse_button = term_cold->term_mouse_down ? term_cold->term_mouse_btn : 0u;
            }
            term_cold->term_last_col = res.hovered_col;
            term_cold->term_last_row = res.hovered_row;
        }
    }
    if (is_focused && d.capture_keyboard_input) {
        auto is_pure_mod_key = [](uint32_t k) noexcept -> bool {
            return k == SDLK_LCTRL || k == SDLK_RCTRL || k == SDLK_LSHIFT || k == SDLK_RSHIFT ||
                   k == SDLK_LALT || k == SDLK_RALT || k == SDLK_LGUI || k == SDLK_RGUI || k == SDLK_CAPSLOCK;
        };
        bool handled_paste = false;
        for (uint32_t ki = 0u; ki < ui.frame_key_count; ++ki) {
            KeyEventEntry& ev = ui.frame_keys[ki];
            if (ev.consumed || is_pure_mod_key(ev.key)) continue;
            const bool primary = (ev.mods & Mod_Primary) != 0u;
            if (primary && ev.key == SDLK_V) {
                const Str clip = get_clipboard_text(&ctx->frame_arena);
                if (clip.chars && clip.length > 0u) {
                    if (d.bracketed_paste) {
                        const uint32_t total = 6u + clip.length + 6u;
                        if (char* buf = ctx->frame_arena.alloc<char>(total + 1u)) {
                            std::memcpy(buf, "\x1b[200~", 6u);
                            std::memcpy(buf + 6u, clip.chars, clip.length);
                            std::memcpy(buf + 6u + clip.length, "\x1b[201~", 6u);
                            buf[total] = '\0';
                            res.pasted_text = { buf, total };
                        }
                    } else {
                        res.pasted_text = clip;
                    }
                    ev.consumed = true;
                    handled_paste = true;
                    break;
                }
            }
            if (res.key_pressed == 0u) {
                res.key_pressed = ev.key;
                res.key_mods = ev.mods;
                res.key_repeat = ev.repeat;
                ev.consumed = true;
                break;
            }
        }
        if (!handled_paste && ui.frame_text_utf8_len > 0u) {
            res.typed_text = ctx->frame_arena.push_str(ui.frame_text_utf8, ui.frame_text_utf8_len);
        }
    }
    for(uint32_t row=0;row<d.rows;++row) {
        const size_t offset=((size_t(d.first_row)+row)%ring_rows)*d.columns;
        size_t extra_grapheme_bytes = 0u;
        for (uint32_t cg = 0u; cg < d.columns; ++cg) {
            const TerminalCell gc = read(cells, offset + cg);
            if (gc.grapheme_utf8 && gc.grapheme_len > 0u) extra_grapheme_bytes += gc.grapheme_len;
        }
        const size_t row_buf_cap = size_t(d.columns) * 4u + extra_grapheme_bytes;
        char* row_text=ctx->frame_arena.alloc<char>(row_buf_cap);
        uint32_t* columns=ctx->frame_arena.alloc<uint32_t>(row_buf_cap);
        if(!columns || !row_text) {end_box();return res;}
        // Pass 1: cell background fills so selection & search highlights layer cleanly on top.
        const uint32_t bg_packed = bg.pack_rgba8();
        for (uint32_t c0 = 0u; c0 < d.columns; ) {
            const TerminalCell cell = read(cells, offset + c0);
            if (cell.width == 0u) { ++c0; continue; }
            Color cell_bg = cell.inverse ? terminal_color(cell.foreground, fg, d.ansi_palette) : terminal_color(cell.background, bg, d.ansi_palette);
            const uint32_t c_start = c0;
            const uint32_t c_pack = cell_bg.pack_rgba8();
            c0 += (cell.width == 2u && c0 + 1u < d.columns) ? 2u : 1u;
            if (c_pack == bg_packed) continue;
            while (c0 < d.columns) {
                const TerminalCell nxt = read(cells, offset + c0);
                if (nxt.width == 0u) { ++c0; continue; }
                const Color nbg = nxt.inverse ? terminal_color(nxt.foreground, fg, d.ansi_palette) : terminal_color(nxt.background, bg, d.ansi_palette);
                if (nbg.pack_rgba8() != c_pack) break;
                c0 += (nxt.width == 2u && c0 + 1u < d.columns) ? 2u : 1u;
            }
            draw_rect({.rect={float(c_start)*d.cell_width_px, float(row)*d.line_height_px,
                              float(c0 - c_start)*d.cell_width_px, d.line_height_px}, .fill=cell_bg});
        }
        // Pass 2: search match highlights & selection highlights.
        for (const TerminalSearchMatch& sm : d.search_matches) {
            if (sm.row != row || sm.end_col <= sm.start_col || sm.start_col >= d.columns) continue;
            const uint32_t s1 = sm.end_col <= d.columns ? sm.end_col : d.columns;
            draw_rect({.rect={float(sm.start_col)*d.cell_width_px, float(row)*d.line_height_px,
                              float(s1 - sm.start_col)*d.cell_width_px, d.line_height_px},
                       .fill=sm.active ? active_match_col : match_col});
        }
        if (has_sel) {
            uint32_t sc = 0u;
            while (sc < d.columns) {
                while (sc < d.columns && !terminal_cell_selected_coords(eff_sr, eff_sc, eff_er, eff_ec, d.selection_block, sc, row)) ++sc;
                if (sc >= d.columns) break;
                const uint32_t s0 = sc;
                while (sc < d.columns && terminal_cell_selected_coords(eff_sr, eff_sc, eff_er, eff_ec, d.selection_block, sc, row)) ++sc;
                draw_rect({.rect={float(s0)*d.cell_width_px, float(row)*d.line_height_px,
                                  float(sc-s0)*d.cell_width_px, d.line_height_px}, .fill=sel_col});
            }
        }
        // Pass 3: vector box-drawing/block glyphs, text runs, underlines, strikethrough.
        uint32_t row_bytes=0;
        uint32_t column=0;
        while(column<d.columns) {
            const TerminalCell first=read(cells,offset+column);
            if(first.width==0) {++column;continue;}
            Color foreground=terminal_color(first.foreground,fg,d.ansi_palette),background=terminal_color(first.background,bg,d.ansi_palette);
            if(first.inverse) {const Color swap=foreground;foreground=background;background=swap;}
            if(first.dim) {foreground.a = static_cast<uint8_t>((uint32_t(foreground.a) * 153u) / 255u);}
            const uint32_t start=column;
            const bool first_has_grapheme = (first.grapheme_utf8 != nullptr && first.grapheme_len > 0u);
            if (!first_has_grapheme && first.width == 1u && terminal_is_vector_glyph(first.codepoint)) {
                const float vx = float(start) * d.cell_width_px;
                const float vy = float(row) * d.line_height_px;
                terminal_draw_vector_cell(first.codepoint, vx, vy, d.cell_width_px, d.line_height_px, foreground);
                ++column;
                continue;
            }
            char* text_data=row_text+row_bytes;
            uint32_t bytes=0;
            do {
                const TerminalCell cell=read(cells,offset+column);
                const bool has_g = (cell.grapheme_utf8 != nullptr && cell.grapheme_len > 0u);
                uint32_t encoded = 0u;
                if (has_g) {
                    std::memcpy(text_data + bytes, cell.grapheme_utf8, cell.grapheme_len);
                    encoded = cell.grapheme_len;
                } else {
                    encoded = terminal_encode(text_data + bytes, cell.codepoint);
                }
                for(uint32_t byte=0;byte<encoded;++byte) columns[row_bytes+bytes+byte]=column-start;
                bytes+=encoded;
                column+=cell.width==2 && column+1<d.columns ? 2u : 1u;
                if(column==d.columns || first.width==2 || first_has_grapheme || has_g) break;
                const TerminalCell next=read(cells,offset+column);
                if(next.width!=1 || (next.grapheme_utf8 && next.grapheme_len > 0u) || terminal_is_vector_glyph(next.codepoint) ||
                    next.bold!=first.bold || next.underline!=first.underline || next.inverse!=first.inverse ||
                    next.italic!=first.italic || next.dim!=first.dim || next.strikethrough!=first.strikethrough ||
                    next.underline_style!=first.underline_style ||
                    next.underline_color.mode!=first.underline_color.mode || next.underline_color.value!=first.underline_color.value ||
                    next.hyperlink_id!=first.hyperlink_id ||
                    next.foreground.mode!=first.foreground.mode || next.foreground.value!=first.foreground.value ||
                    next.background.mode!=first.background.mode || next.background.value!=first.background.value) break;
            } while(true);
            row_bytes+=bytes;
            const float x=float(start)*d.cell_width_px,y=float(row)*d.line_height_px,w=float(column-start)*d.cell_width_px;
            const uint16_t run_font = first.italic ? italic_font : d.font_id;
            const auto draw_run=[&](float bold_offset) noexcept {
                DrawCmdNode* node=append_open_box_draw_cmd(ctx,false);
                if(!node)return;
                node->kind=DrawCmdKind::Text;
                node->text_payload = DrawCmdTextPayload{
                    .cmd = {.text={text_data,bytes,true},.pos={x+bold_offset,y+(d.line_height_px-d.font_size_px)*.5f},
                            .font_id=run_font,.size=d.font_size_px,.color=foreground},
                    .terminal_columns = columns+row_bytes-bytes,
                    .terminal_cell_width = d.cell_width_px,
                };
            };
            draw_run(0);
            if(first.bold)draw_run(.35f);
            TerminalUnderlineStyle ustyle = first.underline_style;
            if (ustyle == TerminalUnderlineStyle::None &&
                (first.underline || (active_hl != 0u && first.hyperlink_id == active_hl))) {
                ustyle = TerminalUnderlineStyle::Single;
            }
            if (ustyle != TerminalUnderlineStyle::None) {
                const Color ucol = (first.underline_color.mode != TerminalColorMode::Default)
                    ? terminal_color(first.underline_color, foreground, d.ansi_palette)
                    : foreground;
                const float uy = y + d.line_height_px - 2.0f;
                if (ustyle == TerminalUnderlineStyle::Single) {
                    draw_rect({.rect={x, uy, w, 1.0f}, .fill=ucol});
                } else if (ustyle == TerminalUnderlineStyle::Double) {
                    draw_rect({.rect={x, uy - 1.5f, w, 1.0f}, .fill=ucol});
                    draw_rect({.rect={x, uy + 0.5f, w, 1.0f}, .fill=ucol});
                } else if (ustyle == TerminalUnderlineStyle::Dotted) {
                    for (float dx = 0.0f; dx < w; dx += 3.0f) {
                        const float dw = (dx + 1.5f <= w) ? 1.5f : (w - dx);
                        draw_rect({.rect={x + dx, uy, dw, 1.0f}, .fill=ucol});
                    }
                } else if (ustyle == TerminalUnderlineStyle::Curly) {
                    const uint32_t segs = static_cast<uint32_t>(std::fmax(2.0f, std::ceil(w / 3.0f)));
                    const uint32_t pts = segs + 1u;
                    if (Vec2* wave = ctx->frame_arena.alloc_uninit<Vec2>(pts)) {
                        for (uint32_t k = 0u; k < pts; ++k) {
                            const float wx = x + std::fmin(w, float(k) * 3.0f);
                            const float wy = uy + ((k & 1u) ? 1.2f : -1.2f);
                            wave[k] = {wx, wy};
                        }
                        draw_polyline({.points={wave, pts}, .color=ucol, .thickness=1.1f});
                    }
                }
            }
            if(first.strikethrough) draw_rect({.rect={x,y+d.line_height_px*.5f,w,1.0f},.fill=foreground});
        }
        if (auto_url_row >= 0 && uint32_t(auto_url_row) == row && auto_url_col1 > auto_url_col0) {
            const float ux = float(auto_url_col0) * d.cell_width_px;
            const float uw = float(auto_url_col1 - auto_url_col0) * d.cell_width_px;
            const float uy = float(row) * d.line_height_px + d.line_height_px - 2.0f;
            draw_rect({.rect={ux, uy, uw, 1.0f}, .fill=theme.accent_primary});
        }
    }
    for (const TerminalInlineImage& img : d.inline_images) {
        if (!img.texture || img.cols == 0u || img.rows == 0u) continue;
        draw_image({
            .texture = img.texture,
            .rect = {float(img.col) * d.cell_width_px, float(img.row) * d.line_height_px,
                     float(img.cols) * d.cell_width_px, float(img.rows) * d.line_height_px},
            .uv_rect = {img.uv_min.x, img.uv_min.y, img.uv_max.x - img.uv_min.x, img.uv_max.y - img.uv_min.y},
            .foreground = true,
        });
    }
    if(d.cursor_visible && d.cursor_column>=0 && uint32_t(d.cursor_column)<d.columns && d.cursor_row>=0 && uint32_t(d.cursor_row)<d.rows) {
        WidgetState* ws=state_map_get_or_insert(&ui.states,d.id,ui.frame_index);
        if(WidgetCold* c = ensure_widget_cold(ws)) {
            const Vec2 target={float(d.cursor_column)*d.cell_width_px,float(d.cursor_row)*d.line_height_px};
            if(!c->terminal_initialized) {c->terminal_cursor=target;c->terminal_initialized=true;}
            advance_motion(ctx,&c->terminal_cursor.x,&c->terminal_velocity.x,target.x,d.cursor_motion);
            advance_motion(ctx,&c->terminal_cursor.y,&c->terminal_velocity.y,target.y,d.cursor_motion);
            const Vec2 cursor=c->terminal_cursor;
            const Color cur_col = resolve_theme_color(d.cursor_color,theme.accent_primary);
            const TerminalCursorShape shape = (d.focusable && !is_focused) ? TerminalCursorShape::Hollow : d.cursor_shape;
            if (shape == TerminalCursorShape::Block) {
                const Color block_col = Color::rgba8(cur_col.r, cur_col.g, cur_col.b, 140u);
                draw_rect({.rect={cursor.x, cursor.y, d.cell_width_px, d.line_height_px}, .fill=block_col, .foreground=true});
            } else if (shape == TerminalCursorShape::Bar) {
                draw_rect({.rect={cursor.x, cursor.y + 1.0f, 2.0f, std::fmax(2.0f, d.line_height_px - 2.0f)}, .fill=cur_col, .foreground=true});
            } else if (shape == TerminalCursorShape::Hollow) {
                draw_rect({.rect={cursor.x, cursor.y, d.cell_width_px, d.line_height_px}, .fill=Color::transparent(), .border_color=cur_col, .border_width=1.5f, .foreground=true});
            } else {
                draw_rect({.rect={cursor.x,cursor.y+d.line_height_px-3,d.cell_width_px,2},.fill=cur_col,.foreground=true});
            }
            const Rect ime_r = {prev_bounds.x + cursor.x, prev_bounds.y + cursor.y, d.cell_width_px, d.line_height_px};
            res.ime_cursor_rect = ime_r;
            if (is_focused) {
                ui.ime_focused_id = d.id;
                ui.ime_cursor_logical = ime_r;
                ui.ime_cursor_offset_win = 0;
            }
        }
    }
    end_box();
    return res;
}
} // namespace

void terminal_grid(const TerminalGridDesc& d) noexcept {
    if(!terminal_grid_valid(d,d.cells.size())) return;
    (void)terminal_grid_run(d,d.cells.data(),d.cells.size(),read_terminal_cell);
}

TerminalGridResult terminal_grid_ex(const TerminalGridDesc& d) noexcept {
    if(!terminal_grid_valid(d,d.cells.size())) return {};
    return terminal_grid_run(d,d.cells.data(),d.cells.size(),read_terminal_cell);
}

uint32_t terminal_encode_key(uint32_t keycode, uint16_t mods, bool app_cursor, char* out_buf, uint32_t cap) noexcept {
    char tmp[32] = {};
    uint32_t len = 0u;
    const bool shift = (mods & Mod_Shift) != 0u;
    const bool alt = (mods & Mod_Alt) != 0u;
    const bool ctrl = (mods & Mod_Ctrl) != 0u;
    const uint32_t xmod = 1u + (shift ? 1u : 0u) + (alt ? 2u : 0u) + (ctrl ? 4u : 0u);
    auto write_csi_letter = [&](char letter) noexcept {
        if (xmod > 1u) len = static_cast<uint32_t>(std::snprintf(tmp, sizeof(tmp), "\x1b[1;%u%c", xmod, letter));
        else { tmp[0] = '\x1b'; tmp[1] = app_cursor ? 'O' : '['; tmp[2] = letter; len = 3u; }
    };
    auto write_csi_tilde = [&](uint32_t num) noexcept {
        if (xmod > 1u) len = static_cast<uint32_t>(std::snprintf(tmp, sizeof(tmp), "\x1b[%u;%u~", num, xmod));
        else len = static_cast<uint32_t>(std::snprintf(tmp, sizeof(tmp), "\x1b[%u~", num));
    };
    switch (keycode) {
        case SDLK_RETURN: case SDLK_KP_ENTER:
            if (alt) { tmp[0] = '\x1b'; tmp[1] = '\r'; len = 2u; } else { tmp[0] = '\r'; len = 1u; }
            break;
        case SDLK_ESCAPE: tmp[0] = '\x1b'; len = 1u; break;
        case SDLK_BACKSPACE:
            if (alt) { tmp[0] = '\x1b'; tmp[1] = ctrl ? '\x08' : '\x7f'; len = 2u; }
            else { tmp[0] = ctrl ? '\x08' : '\x7f'; len = 1u; }
            break;
        case SDLK_TAB:
            if (shift) { tmp[0] = '\x1b'; tmp[1] = '['; tmp[2] = 'Z'; len = 3u; }
            else { tmp[0] = '\t'; len = 1u; }
            break;
        case SDLK_UP:    write_csi_letter('A'); break;
        case SDLK_DOWN:  write_csi_letter('B'); break;
        case SDLK_RIGHT: write_csi_letter('C'); break;
        case SDLK_LEFT:  write_csi_letter('D'); break;
        case SDLK_HOME:  write_csi_letter('H'); break;
        case SDLK_END:   write_csi_letter('F'); break;
        case SDLK_INSERT:   write_csi_tilde(2u); break;
        case SDLK_DELETE:   write_csi_tilde(3u); break;
        case SDLK_PAGEUP:   write_csi_tilde(5u); break;
        case SDLK_PAGEDOWN: write_csi_tilde(6u); break;
        case SDLK_F1:  if (xmod > 1u) write_csi_letter('P'); else { tmp[0]='\x1b'; tmp[1]='O'; tmp[2]='P'; len=3u; } break;
        case SDLK_F2:  if (xmod > 1u) write_csi_letter('Q'); else { tmp[0]='\x1b'; tmp[1]='O'; tmp[2]='Q'; len=3u; } break;
        case SDLK_F3:  if (xmod > 1u) write_csi_letter('R'); else { tmp[0]='\x1b'; tmp[1]='O'; tmp[2]='R'; len=3u; } break;
        case SDLK_F4:  if (xmod > 1u) write_csi_letter('S'); else { tmp[0]='\x1b'; tmp[1]='O'; tmp[2]='S'; len=3u; } break;
        case SDLK_F5:  write_csi_tilde(15u); break;
        case SDLK_F6:  write_csi_tilde(17u); break;
        case SDLK_F7:  write_csi_tilde(18u); break;
        case SDLK_F8:  write_csi_tilde(19u); break;
        case SDLK_F9:  write_csi_tilde(20u); break;
        case SDLK_F10: write_csi_tilde(21u); break;
        case SDLK_F11: write_csi_tilde(23u); break;
        case SDLK_F12: write_csi_tilde(24u); break;
        default:
            if (ctrl && keycode >= 'a' && keycode <= 'z') {
                const char c = static_cast<char>(keycode - 'a' + 1u);
                if (alt) { tmp[0] = '\x1b'; tmp[1] = c; len = 2u; } else { tmp[0] = c; len = 1u; }
            } else if (ctrl && keycode == ' ') {
                tmp[0] = '\0'; len = 1u;
            } else if (ctrl && keycode == '[') {
                tmp[0] = '\x1b'; len = 1u;
            } else if (ctrl && keycode == '\\') {
                tmp[0] = '\x1c'; len = 1u;
            } else if (ctrl && keycode == ']') {
                tmp[0] = '\x1d'; len = 1u;
            } else if (alt && keycode >= 0x20u && keycode < 0x7Fu) {
                char c = static_cast<char>(keycode);
                if (shift && c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
                tmp[0] = '\x1b'; tmp[1] = c; len = 2u;
            }
            break;
    }
    if (out_buf && cap > 0u) {
        const uint32_t copy = len < cap ? len : (cap - 1u);
        if (copy > 0u) std::memcpy(out_buf, tmp, copy);
        if (copy < cap) out_buf[copy] = '\0';
    }
    return len;
}

uint32_t terminal_encode_mouse_sgr(uint8_t button, int32_t col, int32_t row, uint16_t mods, bool pressed, bool motion, char* out_buf, uint32_t cap) noexcept {
    uint32_t cb = (button == 1u) ? 0u : ((button == 2u) ? 1u : ((button == 3u) ? 2u : 3u));
    if (motion) cb += 32u;
    if (mods & Mod_Shift) cb += 4u;
    if (mods & Mod_Alt)   cb += 8u;
    if (mods & Mod_Ctrl)  cb += 16u;
    const int32_t cx = (col >= 0 ? col : 0) + 1;
    const int32_t cy = (row >= 0 ? row : 0) + 1;
    char tmp[32] = {};
    const int n = std::snprintf(tmp, sizeof(tmp), "\x1b[<%u;%d;%d%c", cb, cx, cy, (pressed || motion) ? 'M' : 'm');
    const uint32_t len = n > 0 ? static_cast<uint32_t>(n) : 0u;
    if (out_buf && cap > 0u) {
        const uint32_t copy = len < cap ? len : (cap - 1u);
        if (copy > 0u) std::memcpy(out_buf, tmp, copy);
        if (copy < cap) out_buf[copy] = '\0';
    }
    return len;
}

bool begin_sheet(const SheetDesc& d) noexcept {
    if(!desktop_frame(d.id) || uint8_t(d.edge)>uint8_t(SheetEdge::Right) || !std::isfinite(d.extent_px) ||
        d.extent_px<=0 || d.extent_px>8192 || d.snap_count==0 || d.snap_count>8 ||
        !std::isfinite(d.motion.response) || d.motion.response<=0 ||
        !std::isfinite(d.motion.damping_ratio) || d.motion.damping_ratio<0 ||
        !std::isfinite(d.corner_radius_px) || d.corner_radius_px<0 ||
        !std::isfinite(d.inset_px) || d.inset_px<0 ||
        !std::isfinite(d.border_width) || d.border_width<0 ||
        !std::isfinite(d.backdrop_blur) || d.backdrop_blur<0 ||
        !std::isfinite(d.top_inset_px) || d.top_inset_px<0 ||
        !std::isfinite(d.shadow_blur) || d.shadow_blur<0) return false;
    for(uint32_t i=0;i<d.snap_count;++i)
        if(!std::isfinite(d.snap_points[i]) || d.snap_points[i]<0 || d.snap_points[i]>1 || (i && d.snap_points[i]<=d.snap_points[i-1])) return false;
    Context* ctx=current_context();auto& ui=*ctx->active_ui;
    bool open_val=d.open_state ? *d.open_state : d.open;
    const uint32_t handle=id_index(d.id,0x53484545);
    const uint32_t scrim_id=id_index(d.id,0x5343524D);
    const bool handle_pressed=d.show_handle && pressed(handle);
    WidgetState* ws=state_map_get_or_insert(&ui.states,d.id,ui.frame_index);
    WidgetCold* c = ensure_widget_cold(ws);
    if(!c) return false;
    if(!c->sheet_initialized) {c->sheet_snap=d.snap_count-1;c->sheet_initialized=true;}
    if(d.snap_index) c->sheet_snap=*d.snap_index<d.snap_count ? *d.snap_index : d.snap_count-1;
    if(c->sheet_snap>=d.snap_count) c->sheet_snap=d.snap_count-1;
    if(open_val && d.open_state && d.dismiss_on_escape && ui.popup_count==0u && ui.modal_count==0u && !ui.frame_escape_consumed_by_ime && consume_key_pressed(SDLK_ESCAPE,false)) {
        *d.open_state=false;open_val=false;request_redraw(ctx);
    }
    if(open_val && d.open_state && d.dismiss_on_backdrop && clicked(scrim_id)) {
        *d.open_state=false;open_val=false;request_redraw(ctx);
    }
    const float pointer=d.edge==SheetEdge::Bottom ? ui.pointer_pos.y : ui.pointer_pos.x;
    const float direction=d.edge==SheetEdge::Left ? 1.f : -1.f;
    if(open_val && handle_pressed && ui.prev_pointer_down && !c->sheet_dragging) {
        c->sheet_dragging=true;c->sheet_last_pointer=pointer;c->sheet_velocity=0;
    }
    if(c->sheet_dragging) {
        if(ui.prev_pointer_down && open_val) {
            const float delta=(pointer-c->sheet_last_pointer)*direction/d.extent_px;
            c->sheet_pos=clamp_range(c->sheet_pos+delta,0,1);
            if(ui.sim_dt>0) c->sheet_velocity=delta/ui.sim_dt;
            c->sheet_last_pointer=pointer;
        } else {
            const float projected=clamp_range(c->sheet_pos+c->sheet_velocity*.18f,0,1);
            uint32_t nearest=0;
            for(uint32_t i=1;i<d.snap_count;++i)
                if(std::fabs(d.snap_points[i]-projected)<std::fabs(d.snap_points[nearest]-projected)) nearest=i;
            c->sheet_snap=nearest;
            if(d.snap_index) *d.snap_index=nearest;
            if(d.open_state && d.snap_points[nearest]<=0.0001f) {
                *d.open_state=false;open_val=false;c->sheet_snap=d.snap_count-1;
                if(d.snap_index) *d.snap_index=c->sheet_snap;
            }
            c->sheet_dragging=false;
        }
    }
    if(!c->sheet_dragging) advance_motion(ctx,&c->sheet_pos,&c->sheet_velocity,open_val ? d.snap_points[c->sheet_snap] : 0,d.motion);
    const float visible=clamp_range(c->sheet_pos,0,1);
    if(!open_val && c->sheet_pos==0 && c->sheet_velocity==0) return false;
    const float width=float(ui.viewport_w)/ui.dpi_scale,height=float(ui.viewport_h)/ui.dpi_scale;
    if((d.backdrop_color.a>0 || d.dismiss_on_backdrop) && visible>0.002f) {
        Color sc=d.backdrop_color;
        sc.a=static_cast<uint8_t>(std::round(float(sc.a)*visible));
        box({.id=scrim_id,.width=Sizing::fixed(width),.height=Sizing::fixed(height),
            .background=sc,.attach_to=AttachTo::Root,.anchor_parent=Anchor::TopLeft,
            .z_index=static_cast<int16_t>(d.z_index>0 ? d.z_index-1 : 0)});
    }
    const bool bottom=d.edge==SheetEdge::Bottom;
    const float inset=std::fmin(d.inset_px,std::fmin(width,height)*.25f);
    const float top_inset=d.top_inset_px>0.f ? std::fmin(d.top_inset_px,height*.45f) : inset;
    const float avail_along=std::fmax(1.f,bottom ? (height-top_inset-inset) : (width-inset*2.f));
    const float avail_cross=std::fmax(1.f,bottom ? (width-inset*2.f) : (height-top_inset-inset));
    const float extent=std::fmin(d.extent_px,avail_along);
    const float slide_dist=extent+inset;
    const Vec2 offset=bottom ? Vec2{inset,height-slide_dist*visible} :
        d.edge==SheetEdge::Left ? Vec2{inset-slide_dist*(1-visible),top_inset} : Vec2{width-slide_dist*visible,top_inset};
    const Color background=resolve_theme_color(d.background,active_theme(ctx).surface_control);
    begin_box({.id=d.id,.axis=bottom ? Axis::Col : Axis::Row,.width=Sizing::fixed(bottom ? avail_cross : extent),
        .height=Sizing::fixed(bottom ? extent : avail_cross),.background=background,
        .border_color=d.border_color,.border_width=d.border_width,
        .radius=Corners::all(d.corner_radius_px),
        .shadow_color=d.shadow_color,.shadow_blur=d.shadow_blur,.shadow_offset=d.shadow_offset,
        .clip_children=true,
        .attach_to=AttachTo::Root,.anchor_parent=Anchor::TopLeft,.floating_offset=offset,.z_index=d.z_index,
        .backdrop_blur=d.backdrop_blur,.continuous_corners=(inset>0.f || top_inset>0.f)});
    const bool focus_scoped = (d.dismiss_on_backdrop || d.backdrop_color.a > 0u) && ui.sheet_depth < 16u;
    if (ui.sheet_depth < 16u) ui.sheet_focus_scoped[ui.sheet_depth++] = focus_scoped;
    if (focus_scoped) begin_focus_scope(d.id, false);
    if(d.show_handle) {
        begin_box({.id=handle,.width=bottom ? Sizing::grow() : Sizing::fixed(24),
            .height=bottom ? Sizing::fixed(24) : Sizing::grow(),.align_x=AlignX::Center,.align_y=AlignY::Center,
            .attach_to=d.edge==SheetEdge::Left ? AttachTo::Parent : AttachTo::None,
            .anchor_parent=Anchor::TopLeft,
            .floating_offset=d.edge==SheetEdge::Left ? Vec2{extent-24,0} : Vec2{},
            .z_index=d.z_index});
        box({.width=Sizing::fixed(bottom ? 36.f : 4.f),.height=Sizing::fixed(bottom ? 4.f : 36.f),
            .background=active_theme(ctx).text_secondary,.radius=Corners::all(2)});
        end_box();
    }
    return true;
}
void end_sheet() noexcept {
    if (Context* ctx = current_context(); ctx && ctx->in_frame && ctx->active_ui->sheet_depth > 0u) {
        if (ctx->active_ui->sheet_focus_scoped[--ctx->active_ui->sheet_depth]) end_focus_scope();
    }
    end_box();
}
} // namespace etch
