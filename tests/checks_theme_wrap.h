#pragma once
#include "etch/etch.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

inline bool verify_theme_wrap_checks(const char* temp_prefix = "_etch_v070") noexcept {
    using namespace etch;
    Context* previous = current_context();
    bool ok = true;
    const auto check = [&](bool pass, int line) noexcept {
        if (!pass) {
            ok = false;
            std::fprintf(stderr, "v070 theme/wrap check line %d failed\n", line);
        }
    };

    // 1. Theme preset invariants
    const Theme dark = Theme::dark();
    const Theme light = Theme::light();
    const Theme hc = Theme::high_contrast();
    const Theme ind = Theme::industrial();
    check(dark.id == ThemeId::Dark && light.id == ThemeId::Light && hc.id == ThemeId::HighContrast && ind.id == ThemeId::Industrial, __LINE__);
    check(light.bg_canvas.a > 0u && light.accent.a > 0u && light.text_body.a > 0u, __LINE__);
    check(hc.bg_canvas.a > 0u && hc.accent.a > 0u && hc.text_body.a > 0u, __LINE__);
    check(ind.bg_canvas.a > 0u && ind.accent.a > 0u && ind.text_body.a > 0u, __LINE__);
    check(ind.radius_control_px < dark.radius_control_px, __LINE__);
    check(light.bg_canvas.r > dark.bg_canvas.r && light.text_body.r < dark.text_body.r, __LINE__);

    Context* c = create_context({.headless = true});
    if (!c) return false;
    set_current_context(c);

    // 2. set_theme idle stability & redundant assignment no-op
    for (int i = 0; i < 4; ++i) {
        begin_frame(c, {.viewport_w = 320, .viewport_h = 200});
        button({.id = id("v070_theme_btn"), .label = str_lit("Theme")});
        end_frame(c);
    }
    check(frame_stats(c).idle_sleeping, __LINE__);

    set_theme(light, c);
    check(theme(c).id == ThemeId::Light && needs_redraw(c), __LINE__);
    for (int i = 0; i < 4; ++i) {
        begin_frame(c, {.viewport_w = 320, .viewport_h = 200});
        button({.id = id("v070_theme_btn"), .label = str_lit("Theme")});
        end_frame(c);
    }
    check(frame_stats(c).idle_sleeping, __LINE__);

    // Assigning identical theme must not wake idle sleep.
    set_theme(light, c);
    check(!needs_redraw(c), __LINE__);
    begin_frame(c, {.viewport_w = 320, .viewport_h = 200});
    button({.id = id("v070_theme_btn"), .label = str_lit("Theme")});
    end_frame(c);
    check(frame_stats(c).idle_sleeping, __LINE__);

    // 3. Nested ETCH_THEME scopes + stack overflow graceful recovery
    begin_frame(c, {.viewport_w = 320, .viewport_h = 200});
    check(theme(c).id == ThemeId::Light, __LINE__);
    ETCH_THEME(hc) {
        check(theme(c).id == ThemeId::HighContrast, __LINE__);
        ETCH_THEME(ind) {
            check(theme(c).id == ThemeId::Industrial, __LINE__);
            button({.id = id("v070_ind_btn"), .label = str_lit("Ind")});
        }
        check(theme(c).id == ThemeId::HighContrast, __LINE__);
    }
    check(theme(c).id == ThemeId::Light, __LINE__);
    for (int k = 0; k < 20; ++k) push_theme(dark);
    for (int k = 0; k < 20; ++k) pop_theme();
    pop_theme(); // extra pop on empty stack must be harmless
    check(theme(c).id == ThemeId::Light, __LINE__);
    end_frame(c);

    // 4. Explicit overrides (transparent & custom hex), text_spans inheritance, offscreen Light PNG verification
    char render_path[512];
    std::snprintf(render_path, sizeof(render_path), "%s.v070.png", temp_prefix);

    set_theme(light, c);
    begin_frame(c, {.viewport_w = 320, .viewport_h = 200});
    begin_box({.width = Sizing::fixed(320), .height = Sizing::fixed(200), .background = Color::rgb_hex(0x112233)});
    box({.width = Sizing::fixed(40), .height = Sizing::fixed(40), .background = Color::transparent()});
    box({.width = Sizing::fixed(40), .height = Sizing::fixed(40), .background = theme(c).bg_canvas});
    box({.width = Sizing::fixed(40), .height = Sizing::fixed(40), .background = Color::rgb_hex(0x22AA44)});
    begin_box({.width = Sizing::fixed(140), .height = Sizing::fixed(60), .background = Color::rgb_hex(0x000000)});
    text_spans({
        {.text = str_lit("MMMM"), .color = Color::theme_auto()},
    }, {.color = Color::rgb_hex(0xFF00AA), .font_size_px = 24.0f});
    end_box();
    end_box();
    end_frame(c);
    check(render_headless_png(c, 320, 200, render_path), __LINE__);
    if (SDL_Surface* surf = SDL_LoadPNG(render_path)) {
        const auto px_match = [&](int x, int y, Color col) noexcept {
            uint8_t r = 0u, g = 0u, b = 0u, a = 0u;
            if (!SDL_ReadSurfacePixel(surf, x, y, &r, &g, &b, &a)) return false;
            return std::abs(int(r) - int(col.r)) <= 4 &&
                   std::abs(int(g) - int(col.g)) <= 4 &&
                   std::abs(int(b) - int(col.b)) <= 4;
        };
        check(px_match(15, 15, Color::rgb_hex(0x112233)), __LINE__);
        check(px_match(55, 15, light.bg_canvas), __LINE__);
        check(px_match(95, 15, Color::rgb_hex(0x22AA44)), __LINE__);
        bool saw_magenta = false;
        for (int y = 4; y < 56 && !saw_magenta; ++y) {
            for (int x = 122; x < 256; ++x) {
                uint8_t r = 0u, g = 0u, b = 0u, a = 0u;
                if (SDL_ReadSurfacePixel(surf, x, y, &r, &g, &b, &a) && r > 120u && b > 80u && g < 60u) {
                    saw_magenta = true;
                    break;
                }
            }
        }
        check(saw_magenta, __LINE__);
        SDL_DestroySurface(surf);
    } else {
        check(false, __LINE__);
    }
    std::remove(render_path);
    set_theme(dark, c);

    // 5. Editable text_input(.multiline = true, .word_wrap = true)
    const uint32_t ti_id = id("v070_wrap_ti");
    char ti_buf[512] = "alpha bravo charlie delta echo foxtrot";
    const uint32_t ti_len = static_cast<uint32_t>(std::strlen(ti_buf));
    TextInputResult ti_res = {};
    const auto ti_step = [&](SDL_Keycode key = 0, uint16_t mod = 0, Vec2 ptr = Vec2{-1.0f, -1.0f}, bool down = false) noexcept {
        begin_frame(c, {
            .viewport_w = 480,
            .viewport_h = 300,
            .dt_seconds = 1.0f / 60.0f,
            .pointer_pos = ptr,
            .pointer_down = down,
            .key_mods = mod,
            .key_pressed_code = key,
        });
        ti_res = text_input({
            .id = ti_id,
            .buffer = ti_buf,
            .capacity = sizeof(ti_buf),
            .width = Sizing::fixed(160),
            .height = Sizing::fixed(120),
            .padding = Padding::all(6),
            .font_size_px = 14.0f,
            .multiline = true,
            .word_wrap = true,
        });
        end_frame(c);
    };

    ti_step();
    request_focus(ti_id);
    set_text_selection(ti_id, 0u, 0u);
    ti_step();
    const Rect ti_bounds = element_bounds(ti_id);
    const float ti_x0 = ti_res.ime_cursor_rect.x;
    const float ti_y0 = ti_res.ime_cursor_rect.y;
    check(ti_res.focused && ti_bounds.w > 150.0f && ti_res.ime_cursor_rect.h > 0.0f, __LINE__);

    set_text_selection(ti_id, ti_len, ti_len);
    ti_step();
    check(ti_res.ime_cursor_rect.y > ti_y0 + 5.0f && ti_res.ime_cursor_rect.x >= ti_bounds.x, __LINE__);

    // Boundary affinity: End stays on subline 0, Home from affinity returns to 0; Home on subline 1 goes to sub1.start
    set_text_selection(ti_id, 0u, 0u);
    ti_step();
    ti_step(SDLK_END);
    const uint32_t end0 = ti_res.cursor_byte;
    check(end0 > 0u && end0 < ti_len, __LINE__);
    check(std::fabs(ti_res.ime_cursor_rect.y - ti_y0) < 1.0f && ti_res.ime_cursor_rect.x > ti_x0 + 10.0f, __LINE__);

    ti_step(SDLK_HOME);
    check(ti_res.cursor_byte == 0u && std::fabs(ti_res.ime_cursor_rect.y - ti_y0) < 1.0f, __LINE__);

    ti_step(SDLK_END);
    ti_step(SDLK_RIGHT);
    const float ti_y1 = ti_res.ime_cursor_rect.y;
    check(ti_res.cursor_byte > end0 && ti_y1 > ti_y0 + 5.0f, __LINE__);
    ti_step(SDLK_HOME);
    check(ti_res.cursor_byte == end0 && std::fabs(ti_res.ime_cursor_rect.y - ti_y1) < 1.0f && std::fabs(ti_res.ime_cursor_rect.x - ti_x0) < 2.0f, __LINE__);

    // Visual Down / Up preserving preferred_x
    set_text_selection(ti_id, 3u, 3u);
    ti_step();
    ti_step(SDLK_DOWN);
    check(ti_res.cursor_byte >= end0 && ti_res.ime_cursor_rect.y > ti_y0 + 5.0f, __LINE__);
    ti_step(SDLK_UP);
    check(ti_res.cursor_byte == 3u && std::fabs(ti_res.ime_cursor_rect.y - ti_y0) < 1.0f, __LINE__);

    // Pointer drag across wrap boundary
    ti_step(0, 0, {ti_bounds.x + 12.0f, ti_y0 + 6.0f}, true);
    ti_step(0, 0, {ti_bounds.x + 30.0f, ti_y1 + 6.0f}, true);
    ti_step(0, 0, {ti_bounds.x + 30.0f, ti_y1 + 6.0f}, false);
    check(has_text_selection(ti_id) && ti_res.selection_anchor_byte < end0 && ti_res.cursor_byte >= end0, __LINE__);

    // Emergency break + ZWJ cluster atomic navigation + CJK ideograph wrap
    static constexpr const char kZwjCjk[] =
        "supercalifragilisticexpialidocious\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x92\xBBtail\n"
        "\xE8\xBF\x99\xE6\x98\xAF\xE4\xB8\x80\xE6\xAE\xB5\xE8\x87\xAA\xE5\x8A\xA8\xE8\xBD\xAF\xE6\x8A\x98\xE8\xA1\x8C"
        "\xE7\x9A\x84\xE4\xB8\xAD\xE6\x96\x87\xE6\xB5\x8B\xE8\xAF\x95\xE6\x96\x87\xE6\x9C\xAC\xE7\x94\xA8\xE4\xBA\x8E";
    std::memcpy(ti_buf, kZwjCjk, sizeof(kZwjCjk));
    ti_step();
    set_text_selection(ti_id, 0u, 0u);
    ti_step();
    bool saw_zwj_start = false;
    bool crossed_zwj_cleanly = false;
    for (uint32_t s = 0u; s < 55u && ti_res.cursor_byte < sizeof(kZwjCjk) - 1u; ++s) {
        if (ti_res.cursor_byte == 34u) saw_zwj_start = true;
        ti_step(SDLK_RIGHT);
        if (ti_res.cursor_byte > 34u && ti_res.cursor_byte < 45u) {
            check(false, __LINE__);
            break;
        }
        if (saw_zwj_start && ti_res.cursor_byte == 45u) crossed_zwj_cleanly = true;
    }
    check(saw_zwj_start && crossed_zwj_cleanly, __LINE__);

    // Emergency break happened on long ASCII word before 34 and CJK line wrapped before end
    set_text_selection(ti_id, 0u, 0u);
    ti_step();
    ti_step(SDLK_END);
    check(ti_res.cursor_byte > 5u && ti_res.cursor_byte < 34u, __LINE__);

    const uint32_t cjk_start = 50u; // after "...tail\n"
    set_text_selection(ti_id, cjk_start, cjk_start);
    ti_step();
    ti_step(SDLK_END);
    check(ti_res.cursor_byte > cjk_start && ti_res.cursor_byte < sizeof(kZwjCjk) - 1u, __LINE__);

    // Inline IME preedit inside wrapped text_input
    SDL_Event ime_ev = {};
    ime_ev.type = SDL_EVENT_TEXT_EDITING;
    ime_ev.edit.text = "\xE6\x8B\xBC\xE9\x9F\xB3";
    ime_ev.edit.start = 1;
    ime_ev.edit.length = 1;
    process_event(c, ime_ev);
    ti_step();
    check(ti_res.composing && ti_res.ime_cursor_rect.w >= 2.0f && ti_res.ime_cursor_rect.h > 0.0f, __LINE__);
    ime_ev.edit.text = "";
    ime_ev.edit.start = 0;
    ime_ev.edit.length = 0;
    process_event(c, ime_ev);
    ti_step();

    // 256-byte chunk boundary crossing inside a ZWJ sequence in wrapped text_input
    std::memset(ti_buf, 'a', 252u);
    static constexpr const char kChunkZwj[] = "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBBz";
    std::memcpy(ti_buf + 252u, kChunkZwj, sizeof(kChunkZwj));
    ti_step();
    set_text_selection(ti_id, 252u, 252u);
    ti_step();
    ti_step(SDLK_RIGHT);
    check(ti_res.cursor_byte == 263u, __LINE__);
    ti_step(SDLK_LEFT);
    check(ti_res.cursor_byte == 252u, __LINE__);

    // 6. Editable code_editor(.word_wrap = true)
    TextBuffer ed_buf = {};
    check(init_text_buffer(&ed_buf, str_lit(
        "float4 color = float4(1.0, 0.5, 0.25, 1.0) + float4(0.1, 0.2, 0.3, 0.4);\n"
        "return color;\n")), __LINE__);
    const uint32_t ed_id = id("v070_wrap_ed");
    CodeEditorResult ed_res = {};
    const auto ed_step = [&](SDL_Keycode key = 0, Vec2 ptr = Vec2{-1.0f, -1.0f}, bool down = false) noexcept {
        begin_frame(c, {
            .viewport_w = 480,
            .viewport_h = 300,
            .dt_seconds = 1.0f / 60.0f,
            .pointer_pos = ptr,
            .pointer_down = down,
            .key_pressed_code = key,
        });
        ed_res = code_editor({
            .id = ed_id,
            .buffer = &ed_buf,
            .width = Sizing::fixed(260),
            .height = Sizing::fixed(180),
            .font_size_px = 14.0f,
            .word_wrap = true,
            .tokenizer = tokenize_cpp_hlsl,
        });
        end_frame(c);
    };

    ed_step();
    request_focus(ed_id);
    code_editor_set_cursor(ed_id, 0u, 0u, false);
    ed_step();
    const Rect ed_bounds = element_bounds(ed_id);
    const float ed_y0 = ed_res.ime_cursor_rect.y;
    ed_step(SDLK_END);
    const uint32_t ed_end0 = ed_res.cursor_col_byte;
    check(ed_res.cursor_line == 0u && ed_end0 > 5u && ed_end0 < 72u, __LINE__);
    check(std::fabs(ed_res.ime_cursor_rect.y - ed_y0) < 1.0f, __LINE__);

    ed_step(SDLK_DOWN);
    const float ed_y1 = ed_res.ime_cursor_rect.y;
    check(ed_res.cursor_line == 0u && ed_res.cursor_col_byte >= ed_end0 && ed_y1 > ed_y0 + 5.0f, __LINE__);

    ed_step(SDLK_HOME);
    check(ed_res.cursor_line == 0u && ed_res.cursor_col_byte == ed_end0 && std::fabs(ed_res.ime_cursor_rect.y - ed_y1) < 1.0f, __LINE__);

    // Pointer click on wrapped subline 1
    ed_step(0, {ed_bounds.x + 90.0f, ed_y1 + 4.0f}, true);
    ed_step(0, {ed_bounds.x + 90.0f, ed_y1 + 4.0f}, false);
    check(ed_res.cursor_line == 0u && ed_res.cursor_col_byte > ed_end0, __LINE__);

    // Inline IME in wrapped code_editor + headless draw-call budget
    code_editor_set_cursor(ed_id, 72u, 72u, false);
    ed_step();
    ime_ev.edit.text = "preedit_alpha_bravo_charlie_delta_echo_foxtrot";
    ime_ev.edit.start = 0;
    ime_ev.edit.length = 1;
    process_event(c, ime_ev);
    ed_step();
    const Rect ed_ime_r0 = ime_cursor_rect(c);
    check(ed_res.composing && ed_ime_r0.h > 0.0f, __LINE__);

    ime_ev.edit.start = 46;
    ime_ev.edit.length = 0;
    process_event(c, ime_ev);
    ed_step();
    const Rect ed_ime_r1 = ime_cursor_rect(c);
    check(ed_ime_r1.y > ed_ime_r0.y + 10.0f, __LINE__);
    check(render_headless_png(c, 480, 300, render_path) && frame_stats(c).draw_call_count <= 3u, __LINE__);
    std::remove(render_path);

    // Click line 1 below expanded multi-subline preedit (verifies shifted row geometry + cancel)
    ed_step(0, {ed_bounds.x + 80.0f, ed_ime_r1.y + 30.0f}, true);
    ed_step(0, {ed_bounds.x + 80.0f, ed_ime_r1.y + 30.0f}, false);
    check(!ed_res.composing, __LINE__);
    check(ed_res.cursor_line == 1u, __LINE__);
    free_text_buffer(&ed_buf);

    // Bottom-edge wrapped preedit reveals the active subline within scroll bounds
    TextBuffer btm_buf = {};
    check(init_text_buffer(&btm_buf, str_lit("l0\nl1\nl2\nl3")), __LINE__);
    const uint32_t btm_id = id("v070_wrap_ed_btm");
    CodeEditorResult btm_res = {};
    const auto btm_step = [&]() noexcept {
        begin_frame(c, {.viewport_w = 480, .viewport_h = 300, .dt_seconds = 1.0f / 60.0f});
        btm_res = code_editor({
            .id = btm_id,
            .buffer = &btm_buf,
            .width = Sizing::fixed(160),
            .height = Sizing::fixed(60),
            .font_size_px = 14.0f,
            .word_wrap = true,
        });
        end_frame(c);
    };
    btm_step();
    request_focus(btm_id);
    const uint32_t btm_eof = text_buffer_byte_length(&btm_buf);
    code_editor_set_cursor(btm_id, btm_eof, btm_eof, true);
    btm_step();
    const Rect btm_bounds = element_bounds(btm_id);
    ime_ev.edit.text = "wrap_bottom_preedit_line_two";
    ime_ev.edit.start = 28;
    ime_ev.edit.length = 0;
    process_event(c, ime_ev);
    btm_step();
    const Rect btm_ime = ime_cursor_rect(c);
    check(btm_res.composing && btm_ime.y >= btm_bounds.y && btm_ime.y + 18.0f <= btm_bounds.y + btm_bounds.h + 2.0f, __LINE__);
    ime_ev.edit.text = "";
    ime_ev.edit.start = 0;
    ime_ev.edit.length = 0;
    process_event(c, ime_ev);
    btm_step();
    free_text_buffer(&btm_buf);

    // 7. 1,000,000-line code_editor(.word_wrap = true) Fenwick jump & single-char edit budget
    constexpr uint32_t kLines1M = 1000000u;
    constexpr uint32_t kLineStride = 64u;
    static constexpr const char kLine64[] =
        "float4 c = float4(1.0, 0.5, 0.25, 1.0) + float4(0.1, 0.2, 0.3);\n";
    static_assert(sizeof(kLine64) - 1u == kLineStride, "kLine64 stride mismatch");
    TextBuffer b1m = {};
    char* src1m = static_cast<char*>(std::malloc(size_t(kLines1M) * kLineStride));
    check(src1m != nullptr, __LINE__);
    if (src1m) {
        for (uint32_t i = 0u; i < kLines1M; ++i) {
            std::memcpy(src1m + size_t(i) * kLineStride, kLine64, kLineStride);
        }
        check(init_text_buffer(&b1m, {src1m, kLines1M * kLineStride - 1u}), __LINE__);
        std::free(src1m);
    }
    if (b1m.data) {
        const uint32_t ed1m_id = id("v070_wrap_ed_1m");
        CodeEditorResult res1m = {};
        const auto step1m = [&]() noexcept {
            begin_frame(c, {.viewport_w = 480, .viewport_h = 300, .dt_seconds = 1.0f / 60.0f});
            res1m = code_editor({
                .id = ed1m_id,
                .buffer = &b1m,
                .width = Sizing::fixed(260),
                .height = Sizing::fixed(200),
                .font_size_px = 14.0f,
                .word_wrap = true,
                .tokenizer = tokenize_cpp_hlsl,
            });
            end_frame(c);
        };
        step1m();
        request_focus(ed1m_id);
        code_editor_scroll_to_line(ed1m_id, 500000u, AlignY::Top);
        const uint32_t cur500k = text_buffer_line_start(&b1m, 500000u) + 4u;
        code_editor_set_cursor(ed1m_id, cur500k, cur500k, false);
        step1m();

        SDL_Event ins = {};
        ins.type = SDL_EVENT_TEXT_INPUT;
        ins.text.text = "x";
        process_event(c, ins);
        step1m();
        check(res1m.changed && res1m.cursor_line == 500000u && res1m.cursor_col_byte == 5u, __LINE__);
        check(frame_stats(c).arena_used_bytes < 512u * 1024u, __LINE__);

        ins.text.text = "y";
        process_event(c, ins);
        step1m();

        const FrameStats st1m = frame_stats(c);
        check(text_buffer_line_count(&b1m) == kLines1M, __LINE__);
        check(res1m.changed && res1m.cursor_line == 500000u && res1m.cursor_col_byte == 6u, __LINE__);
        check(res1m.first_visible_line == 500000u && res1m.end_visible_line > 500000u, __LINE__);
        check(res1m.end_visible_line - res1m.first_visible_line < 16u, __LINE__);
        check(st1m.layout_ms < 10.0f && st1m.arena_used_bytes < 512u * 1024u, __LINE__);
        free_text_buffer(&b1m);
    }

    // 8. 40,000-byte wrapped single line exceeding 16,384 px remains scrollable & editable to end
    constexpr uint32_t kLen40k = 40000u;
    char* src40k = static_cast<char*>(std::malloc(kLen40k));
    check(src40k != nullptr, __LINE__);
    TextBuffer b40k = {};
    if (src40k) {
        for (uint32_t i = 0u; i < kLen40k; i += 5u) {
            std::memcpy(src40k + i, "abcd ", 5u);
        }
        check(init_text_buffer(&b40k, {src40k, kLen40k}), __LINE__);
        std::free(src40k);
    }
    if (b40k.data) {
        const uint32_t ed40k_id = id("v070_wrap_ed_40k");
        CodeEditorResult res40k = {};
        const auto step40k = [&]() noexcept {
            begin_frame(c, {.viewport_w = 300, .viewport_h = 240, .dt_seconds = 1.0f / 60.0f});
            res40k = code_editor({
                .id = ed40k_id,
                .buffer = &b40k,
                .width = Sizing::fixed(260),
                .height = Sizing::fixed(200),
                .font_size_px = 14.0f,
                .word_wrap = true,
            });
            end_frame(c);
        };
        step40k();
        request_focus(ed40k_id);
        code_editor_set_cursor(ed40k_id, kLen40k, kLen40k, true);
        step40k();
        check(res40k.cursor_byte == kLen40k, __LINE__);
        check(res40k.ime_cursor_rect.y >= 0.0f && res40k.ime_cursor_rect.y + res40k.ime_cursor_rect.h <= 200.0f, __LINE__);
        step40k();
        check(res40k.ime_cursor_rect.y >= 0.0f && res40k.ime_cursor_rect.y + res40k.ime_cursor_rect.h <= 200.0f, __LINE__);

        SDL_Event ins40k = {};
        ins40k.type = SDL_EVENT_TEXT_INPUT;
        ins40k.text.text = "z";
        process_event(c, ins40k);
        step40k();
        check(res40k.changed && res40k.cursor_byte == kLen40k + 1u, __LINE__);
        check(res40k.ime_cursor_rect.y >= 0.0f && res40k.ime_cursor_rect.y + res40k.ime_cursor_rect.h <= 200.0f, __LINE__);
        step40k();
        check(res40k.ime_cursor_rect.y >= 0.0f && res40k.ime_cursor_rect.y + res40k.ime_cursor_rect.h <= 200.0f, __LINE__);
        free_text_buffer(&b40k);
    }

    // 9. Scoped ETCH_THEME colors preserved in deferred code_editor, scrollbar & focus ring rendering
    {
        set_theme(dark, c);
        Theme scoped = light;
        scoped.syntax_keyword = Color::rgb_hex(0xFF00AA);
        scoped.scrollbar_thumb = Color::rgb_hex(0x00EE55);
        scoped.scrollbar_thumb_hover = Color::rgb_hex(0x00EE55);
        scoped.scrollbar_thumb_drag = Color::rgb_hex(0x00EE55);
        scoped.focus_ring = Color::rgb_hex(0xFFAA00);

        TextBuffer sc_buf = {};
        check(init_text_buffer(&sc_buf, str_lit("return\n1\n2\n3\n4\n5\n6\n7\n8")), __LINE__);
        const uint32_t sc_pre_id = id("v070_scoped_pre");
        const uint32_t sc_focus_id = id("v070_scoped_focus");
        const uint32_t sc_ed_id = id("v070_scoped_ed");
        const uint32_t sc_sb_id = id("v070_scoped_sb");

        const auto run_scoped = [&](SDL_Keycode key = 0) noexcept {
            begin_frame(c, {.viewport_w = 360, .viewport_h = 120, .dt_seconds = 1.0f / 60.0f, .key_pressed_code = key});
            begin_box({.axis = Axis::Row, .width = Sizing::fixed(360), .height = Sizing::fixed(120), .padding = Padding::all(8), .gap = 6, .background = dark.bg_canvas});
            ETCH_THEME(scoped) {
                box({.id = sc_pre_id, .width = Sizing::fixed(4), .height = Sizing::fixed(36), .background = Color::rgb_hex(0x181818), .border_width = 0.0f, .focusable = true});
                box({.id = sc_focus_id, .width = Sizing::fixed(60), .height = Sizing::fixed(36), .background = Color::rgb_hex(0x181818), .border_width = 0.0f, .focusable = true});
                code_editor({
                    .id = sc_ed_id,
                    .buffer = &sc_buf,
                    .width = Sizing::fixed(170),
                    .height = Sizing::fixed(80),
                    .font_size_px = 18.0f,
                    .show_line_numbers = false,
                    .word_wrap = false,
                    .tokenizer = tokenize_cpp_hlsl,
                });
                begin_box({.id = sc_sb_id, .width = Sizing::fixed(80), .height = Sizing::fixed(80), .background = Color::rgb_hex(0x181818), .border_width = 0.0f, .scroll_y = true, .show_scrollbar = true});
                box({.width = Sizing::fixed(60), .height = Sizing::fixed(240), .background = Color::rgb_hex(0x202020), .border_width = 0.0f});
                end_box();
            }
            end_box();
            end_frame(c);
        };

        run_scoped();
        request_focus(sc_pre_id);
        run_scoped(SDLK_TAB);
        check(focused(sc_focus_id) && keyboard_focus_visible(sc_focus_id), __LINE__);

        char scoped_path[512];
        std::snprintf(scoped_path, sizeof(scoped_path), "%s.v070_scoped.png", temp_prefix);
        check(render_headless_png(c, 360, 120, scoped_path), __LINE__);
        if (SDL_Surface* surf = SDL_LoadPNG(scoped_path)) {
            const Rect f_b = element_bounds(sc_focus_id);
            const Rect e_b = element_bounds(sc_ed_id);
            const Rect s_b = element_bounds(sc_sb_id);
            const auto has_pixel_in = [&](Rect r, auto pred) noexcept {
                const int x0 = static_cast<int>(r.x) > 0 ? static_cast<int>(r.x) : 0;
                const int y0 = static_cast<int>(r.y) > 0 ? static_cast<int>(r.y) : 0;
                const int x1 = static_cast<int>(std::ceil(r.x + r.w)) < 360 ? static_cast<int>(std::ceil(r.x + r.w)) : 360;
                const int y1 = static_cast<int>(std::ceil(r.y + r.h)) < 120 ? static_cast<int>(std::ceil(r.y + r.h)) : 120;
                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x) {
                        uint8_t pr = 0u, pg = 0u, pb = 0u, pa = 0u;
                        if (SDL_ReadSurfacePixel(surf, x, y, &pr, &pg, &pb, &pa) && pred(pr, pg, pb)) {
                            return true;
                        }
                    }
                }
                return false;
            };
            const bool saw_ring = has_pixel_in(f_b, [](uint8_t r, uint8_t g, uint8_t b) noexcept {
                return r > 200u && g > 130u && g < 210u && b < 50u;
            });
            const bool saw_kw = has_pixel_in(e_b, [](uint8_t r, uint8_t g, uint8_t b) noexcept {
                return r > 180u && g < 60u && b > 120u;
            });
            const bool saw_ed_sb = has_pixel_in(e_b, [](uint8_t r, uint8_t g, uint8_t b) noexcept {
                return r < 50u && g > 190u && b > 50u && b < 130u;
            });
            const bool saw_box_sb = has_pixel_in(s_b, [](uint8_t r, uint8_t g, uint8_t b) noexcept {
                return r < 50u && g > 190u && b > 50u && b < 130u;
            });
            check(saw_ring && saw_kw && saw_ed_sb && saw_box_sb, __LINE__);
            SDL_DestroySurface(surf);
        } else {
            check(false, __LINE__);
        }
        std::remove(scoped_path);
        free_text_buffer(&sc_buf);
    }

    destroy_context(c);
    set_current_context(previous);
    return ok;
}
