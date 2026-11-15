int run_headless(const char* out_png_path) noexcept {
    etch::Context* ctx = etch::create_context(etch::ContextDesc{
        .headless = true,
    });
    if (!ctx) {
        std::fprintf(stderr, "Etch headless error: create_context failed (%s)\n", SDL_GetError());
        return 1;
    }

    SDL_GPUTexture* osc_tex = create_oscilloscope_texture(ctx);
    if (!osc_tex) {
        etch::destroy_context(ctx);
        std::fprintf(stderr, "Etch headless error: create_oscilloscope_texture failed\n");
        return 1;
    }

    AppState app = {
        .ctx = ctx,
        .osc_tex = osc_tex,
    };
    init_sandbox_icons(&app);
    constexpr uint32_t kHeadlessW = 1280u;
    constexpr uint32_t kHeadlessH = 800u;
    constexpr float kStepDt = 1.0f / 60.0f;

    const auto cleanup_release = [&]() noexcept {
        SDL_GPUDevice* dev = etch::gpu_device(ctx);
        if (dev) {
            SDL_WaitForGPUIdle(dev);
        }
        etch::destroy_texture(ctx, osc_tex);
        osc_tex = nullptr;
        etch::destroy_context(ctx);
    };

    const auto inject_key = [&](SDL_Keycode key, SDL_Keymod mod = 0u) noexcept {
        SDL_Event ev = {};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.key = key;
        ev.key.mod = mod;
        etch::process_event(ctx, ev);
    };
    const auto inject_text = [&](const char* utf8) noexcept {
        SDL_Event ev = {};
        ev.type = SDL_EVENT_TEXT_INPUT;
        ev.text.text = utf8;
        etch::process_event(ctx, ev);
    };
    const auto inject_editing = [&](const char* utf8, int32_t start, int32_t length) noexcept {
        SDL_Event ev = {};
        ev.type = SDL_EVENT_TEXT_EDITING;
        ev.edit.text = utf8;
        ev.edit.start = start;
        ev.edit.length = length;
        etch::process_event(ctx, ev);
    };

    // 1. Idle Sleep Assertion: after 2 initial settle frames with no motion, needs_redraw() must be false.
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    if (etch::needs_redraw(ctx)) {
        std::fprintf(stderr, "Headless assert [1] failed: expected needs_redraw() == false after idle settle\n");
        cleanup_release();
        return 1;
    }

    // 2. Analytic Spring Assertion: flip toggle_on = true, verify mid-flight & auto-wakeup, then settle to 1.0.
    app.toggle_on = true;
    for (int f = 0; f < 4; ++f) {
        step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    }
    if (!etch::needs_redraw(ctx) || app.last_toggle_t <= 0.05f || app.last_toggle_t >= 0.98f) {
        std::fprintf(
            stderr,
            "Headless assert [2a] failed: expected mid-flight spring (needs_redraw=%d, t=%.4f)\n",
            etch::needs_redraw(ctx) ? 1 : 0,
            app.last_toggle_t);
        cleanup_release();
        return 1;
    }
    for (int f = 0; f < 70; ++f) {
        step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    }
    if (std::fabs(app.last_toggle_t - 1.0f) > 0.01f || etch::needs_redraw(ctx)) {
        std::fprintf(
            stderr,
            "Headless assert [2b] failed: spring failed to converge to idle (t=%.4f, needs_redraw=%d)\n",
            app.last_toggle_t,
            etch::needs_redraw(ctx) ? 1 : 0);
        cleanup_release();
        return 1;
    }

    // 3. Tab & Shift+Tab Focus Traversal Assertion: input_single -> input_multi -> btn_accent -> input_multi -> input_single.
    inject_key(SDLK_TAB);
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    if (etch::focused_id() != etch::id("input_single")) {
        std::fprintf(stderr, "Headless assert [3a] failed: expected focus == input_single, got 0x%08X\n", etch::focused_id());
        cleanup_release();
        return 1;
    }
    inject_key(SDLK_TAB);
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    if (etch::focused_id() != etch::id("input_multi")) {
        std::fprintf(stderr, "Headless assert [3b] failed: expected focus == input_multi, got 0x%08X\n", etch::focused_id());
        cleanup_release();
        return 1;
    }
    inject_key(SDLK_TAB);
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    if (etch::focused_id() != etch::id("btn_accent")) {
        std::fprintf(stderr, "Headless assert [3c] failed: expected focus == btn_accent, got 0x%08X\n", etch::focused_id());
        cleanup_release();
        return 1;
    }
    inject_key(SDLK_TAB, SDL_KMOD_SHIFT);
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    if (etch::focused_id() != etch::id("input_multi")) {
        std::fprintf(stderr, "Headless assert [3d] failed: expected Shift+Tab == input_multi, got 0x%08X\n", etch::focused_id());
        cleanup_release();
        return 1;
    }
    inject_key(SDLK_TAB, SDL_KMOD_SHIFT);
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    if (etch::focused_id() != etch::id("input_single")) {
        std::fprintf(stderr, "Headless assert [3e] failed: expected Shift+Tab == input_single, got 0x%08X\n", etch::focused_id());
        cleanup_release();
        return 1;
    }

    // 4. UTF-8 4-byte & 3-byte Codepoint Atomic Backspace + Enter Submit Assertion.
    const uint32_t base_len = static_cast<uint32_t>(std::strlen(app.single_buf)); // 15 B ("Etch 纯血 GPU")
    inject_text("\xF0\x9F\x9A\x80"); // 4-byte emoji
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    if (app.single_res.byte_length != base_len + 4u || app.single_res.cursor_byte != base_len + 4u) {
        std::fprintf(stderr, "Headless assert [4a] failed: 4-byte UTF-8 insert len=%u\n", app.single_res.byte_length);
        cleanup_release();
        return 1;
    }
    inject_key(SDLK_BACKSPACE);
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    if (app.single_res.byte_length != base_len || std::strcmp(app.single_buf, "Etch 纯血 GPU") != 0) {
        std::fprintf(stderr, "Headless assert [4b] failed: 4-byte atomic backspace failed (%s)\n", app.single_buf);
        cleanup_release();
        return 1;
    }
    inject_text("\xE4\xB8\xAD"); // 3-byte CJK "中"
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    if (app.single_res.byte_length != base_len + 3u) {
        std::fprintf(stderr, "Headless assert [4c] failed: 3-byte UTF-8 insert len=%u\n", app.single_res.byte_length);
        cleanup_release();
        return 1;
    }
    inject_key(SDLK_BACKSPACE);
    inject_key(SDLK_RETURN);
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    if (!app.single_res.submitted || app.submit_count != 1u || std::strcmp(app.single_buf, "Etch 纯血 GPU") != 0) {
        std::fprintf(
            stderr,
            "Headless assert [4d] failed: expected submit_count=1 & text restored (submits=%u, buf=%s)\n",
            app.submit_count,
            app.single_buf);
        cleanup_release();
        return 1;
    }

    // 5. Clipboard Roundtrip (Ctrl+A/C/X/V) + Multiline "纯血 GPU" Highlight Assertion.
    inject_key(SDLK_A, SDL_KMOD_CTRL);
    inject_key(SDLK_C, SDL_KMOD_CTRL);
    inject_key(SDLK_X, SDL_KMOD_CTRL);
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    if (app.single_res.byte_length != 0u) {
        std::fprintf(stderr, "Headless assert [5a] failed: expected Ctrl+A/X empty buffer, len=%u\n", app.single_res.byte_length);
        cleanup_release();
        return 1;
    }
    inject_key(SDLK_V, SDL_KMOD_CTRL);
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    if (std::strcmp(app.single_buf, "Etch 纯血 GPU") != 0) {
        std::fprintf(stderr, "Headless assert [5b] failed: expected Ctrl+V restore 'Etch 纯血 GPU', got '%s'\n", app.single_buf);
        cleanup_release();
        return 1;
    }

    const char* target_tok = "\xE7\xBA\xAF\xE8\xA1\x80 GPU"; // "纯血 GPU"
    const char* found_tok = std::strstr(app.multi_buf, target_tok);
    if (!found_tok) {
        std::fprintf(stderr, "Headless assert [5c] failed: missing target token in multi_buf\n");
        cleanup_release();
        return 1;
    }
    const uint32_t m_sel_a = static_cast<uint32_t>(found_tok - app.multi_buf);
    const uint32_t m_sel_b = m_sel_a + static_cast<uint32_t>(std::strlen(target_tok));
    etch::set_text_selection(etch::id("input_multi"), m_sel_a, m_sel_b);
    inject_key(SDLK_TAB); // Focus input_multi to verify multiline selection copy roundtrip
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    inject_key(SDLK_C, SDL_KMOD_CTRL);
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    const etch::Str clip_check = etch::get_clipboard_text(etch::frame_arena(ctx));
    if (clip_check.length != (m_sel_b - m_sel_a) || std::memcmp(clip_check.chars, target_tok, clip_check.length) != 0) {
        std::fprintf(stderr, "Headless assert [5d] failed: multiline Ctrl+C mismatch (len=%u)\n", clip_check.length);
        cleanup_release();
        return 1;
    }
    inject_key(SDLK_TAB, SDL_KMOD_SHIFT); // Return focus to input_single, leaving muted selection on input_multi!
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    if (etch::focused_id() != etch::id("input_single") ||
        app.multi_res.selection_anchor_byte != m_sel_a ||
        app.multi_res.cursor_byte != m_sel_b)
    {
        std::fprintf(stderr, "Headless assert [5e] failed: expected muted selection preserved on input_multi\n");
        cleanup_release();
        return 1;
    }

    // 6. Chinese IME Preedit, ime_cursor_rect Tracking, Commit & Inline Active "nihao" Assertion.
    inject_key(SDLK_END);
    inject_text(" \xC2\xB7 "); // " · "
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    const etch::Rect ime_before = app.single_res.ime_cursor_rect;
    if (ime_before.w <= 0.0f || ime_before.h <= 0.0f) {
        std::fprintf(stderr, "Headless assert [6a] failed: expected valid initial ime_cursor_rect\n");
        cleanup_release();
        return 1;
    }

    inject_editing("zhongwen", 5, 3);
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    const etch::Rect ime_comp = app.single_res.ime_cursor_rect;
    if (!app.single_res.composing || ime_comp.w <= ime_before.w || std::strcmp(app.single_buf, "Etch \xE7\xBA\xAF\xE8\xA1\x80 GPU \xC2\xB7 ") != 0) {
        std::fprintf(stderr, "Headless assert [6b] failed: expected IME preedit expanded rect & uncommitted buffer\n");
        cleanup_release();
        return 1;
    }

    inject_text("\xE4\xB8\xAD\xE6\x96\x87\xE8\xBE\x93\xE5\x85\xA5"); // "中文输入"
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    const etch::Rect ime_after = app.single_res.ime_cursor_rect;
    if (app.single_res.composing || !app.single_res.changed ||
        std::strcmp(app.single_buf, "Etch \xE7\xBA\xAF\xE8\xA1\x80 GPU \xC2\xB7 \xE4\xB8\xAD\xE6\x96\x87\xE8\xBE\x93\xE5\x85\xA5") != 0 ||
        ime_after.x <= ime_before.x)
    {
        std::fprintf(stderr, "Headless assert [6c] failed: expected IME commit + cursor rect advance (%.1f -> %.1f)\n", ime_before.x, ime_after.x);
        cleanup_release();
        return 1;
    }

    inject_editing("nihao", 2, 3);
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    if (!app.single_res.composing) {
        std::fprintf(stderr, "Headless assert [6d] failed: expected active inline 'nihao' preedit\n");
        cleanup_release();
        return 1;
    }

    // 7. Inertial Wheel Scroll + Popover Open + Idle Sleep Settle Assertion.
    const etch::Rect sb_bounds = etch::element_bounds(etch::id("scroll_list"));
    if (sb_bounds.w <= 10.0f || sb_bounds.h <= 10.0f) {
        std::fprintf(stderr, "Headless assert [7a] failed: invalid scroll_list bounds\n");
        cleanup_release();
        return 1;
    }
    SDL_Event wheel_ev = {};
    wheel_ev.type = SDL_EVENT_MOUSE_WHEEL;
    wheel_ev.wheel.y = -3.0f;
    wheel_ev.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    wheel_ev.wheel.mouse_x = sb_bounds.x + 0.5f * sb_bounds.w;
    wheel_ev.wheel.mouse_y = sb_bounds.y + 0.5f * sb_bounds.h;
    etch::process_event(ctx, wheel_ev);

    for (int f = 0; f < 15; ++f) {
        step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    }
    const uint32_t scroll_id = etch::id("scroll_list");
    const etch::Vec2 mid_scroll = etch::scroll_offset(scroll_id);
    if (mid_scroll.y <= 30.0f) {
        std::fprintf(stderr, "Headless assert [7b] failed: expected inertial scroll > 30px, got %.2f\n", mid_scroll.y);
        cleanup_release();
        return 1;
    }

    const etch::FrameStats pre_blur_stats = etch::frame_stats(ctx);
    if (pre_blur_stats.draw_call_count != 1u) {
        std::fprintf(stderr, "Headless assert [8a] failed: expected 1 draw call without blur, got %u\n", pre_blur_stats.draw_call_count);
        cleanup_release();
        return 1;
    }

    app.popover_open = true;
    app.selected_preset = 1u;
    for (int f = 0; f < 90 && (f < 2 || etch::needs_redraw(ctx)); ++f) {
        step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    }
    if (etch::needs_redraw(ctx) || !app.single_res.composing) {
        std::fprintf(stderr, "Headless assert [7c] failed: expected idle settle with active IME preedit\n");
        cleanup_release();
        return 1;
    }

    // 10a. F12 Inspector HUD Toggle Assertion (adds HUD prims with 0 extra draw calls, then restores off).
    const etch::FrameStats pre_hud_stats = etch::frame_stats(ctx);
    inject_key(SDLK_F12);
    if (!etch::debug_hud_open(ctx)) {
        std::fprintf(stderr, "Headless assert [10a-1] failed: SDLK_F12 did not open HUD\n");
        cleanup_release();
        return 1;
    }
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    const etch::FrameStats hud_stats = etch::frame_stats(ctx);
    if (hud_stats.prim_count <= pre_hud_stats.prim_count + 16u ||
        hud_stats.draw_call_count != pre_hud_stats.draw_call_count)
    {
        std::fprintf(
            stderr,
            "Headless assert [10a-2] failed: HUD expected more prims & 0 extra draw calls (%u->%u prims, %u->%u dc)\n",
            pre_hud_stats.prim_count,
            hud_stats.prim_count,
            pre_hud_stats.draw_call_count,
            hud_stats.draw_call_count);
        cleanup_release();
        return 1;
    }
    inject_key(SDLK_F12);
    if (etch::debug_hud_open(ctx)) {
        std::fprintf(stderr, "Headless assert [10a-3] failed: second SDLK_F12 did not close HUD\n");
        cleanup_release();
        return 1;
    }
    for (int f = 0; f < 4; ++f) {
        step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    }

    const etch::FrameStats final_stats = etch::frame_stats(ctx);
    // Leave a real keyboard focus ring visible in the requested output.
    etch::request_focus(etch::id("btn_accent"));
    inject_key(SDLK_TAB);
    step_headless_frame(&app, kHeadlessW, kHeadlessH, kStepDt);
    const bool png_ok = etch::render_headless_png(
        ctx,
        kHeadlessW,
        kHeadlessH,
        out_png_path,
        etch::Color::rgb_hex(0x0B0D12));

    const etch::Vec2 final_scroll = etch::scroll_offset(scroll_id);
    const bool stats_ok = (final_stats.draw_call_count == 2u) && final_stats.idle_sleeping;
    if (!stats_ok) {
        std::fprintf(stderr, "Headless assert [8b] failed: expected draw_calls=2 and idle=1 with frosted popover, got draw_calls=%u idle=%d\n",
            final_stats.draw_call_count, final_stats.idle_sleeping ? 1 : 0);
    }
    uint32_t slug_prim_count = 0u;
    uint32_t legacy_glyph_count = 0u;
    for (const etch::Prim& p : etch::frame_prims(ctx)) {
        const auto kind = static_cast<etch::PrimKind>(p.style.x & 0xFu);
        if (kind == etch::PrimKind::SlugGlyph) {
            ++slug_prim_count;
        } else if (kind == etch::PrimKind::Glyph) {
            ++legacy_glyph_count;
        }
    }
    const bool slug_ok = (slug_prim_count > 0u) && (legacy_glyph_count == 0u) &&
                         (final_stats.slug_glyph_count > 0u) && (final_stats.slug_curve_count > 0u);
    if (!slug_ok) {
        std::fprintf(stderr, "Headless assert [9a] failed: expected Slug text pipeline (slug_prims=%u, legacy=%u, glyphs=%u, curves=%u)\n",
            slug_prim_count, legacy_glyph_count, final_stats.slug_glyph_count, final_stats.slug_curve_count);
    }

    // 10b. Split Pane, Million-Row Virtual List, Infinite 2D Canvas + Wires + Node Drag Assertion.
    bool v10a_ok = false;
    bool v11_ok = false;
    {
        etch::Context* vctx = etch::create_context(etch::ContextDesc{ .headless = true });
        if (!vctx) {
            std::fprintf(stderr, "Headless assert [10b] failed: create_context for v1.0 primitives failed\n");
        } else {
            etch::Vec2 node_pos = { 120.0f, 80.0f };
            etch::VirtualListSlice last_slice = {};
            etch::CanvasState last_cs = {};
            const uint32_t kSplitId = etch::id("v10_split");
            const uint32_t kListId = etch::id("v10_vlist");
            const uint32_t kCanvasId = etch::id("v10_canvas");
            const uint32_t kNodeId = etch::id("v10_node");

            const auto step_v10_frame = [&](etch::Vec2 ptr, bool down, etch::Vec2 wheel) noexcept {
                etch::begin_frame(vctx, etch::FrameDesc{
                    .viewport_w = kHeadlessW,
                    .viewport_h = kHeadlessH,
                    .dpi_scale = 1.0f,
                    .dt_seconds = kStepDt,
                    .pointer_pos = ptr,
                    .pointer_down = down,
                    .scroll_delta = wheel,
                });
                etch::begin_split(etch::SplitDesc{
                    .id = kSplitId,
                    .axis = etch::Axis::Row,
                    .width = etch::Sizing::grow(),
                    .height = etch::Sizing::grow(),
                    .default_ratio = 0.40f,
                    .min_first_px = 120.0f,
                    .min_second_px = 120.0f,
                    .divider_size_px = 6.0f,
                });
                last_slice = etch::begin_virtual_list(etch::VirtualListDesc{
                    .id = kListId,
                    .item_count = 1000000u,
                    .item_height_px = 28.0f,
                    .row_gap_px = 2.0f,
                    .overscan_rows = 2u,
                    .width = etch::Sizing::grow(),
                    .height = etch::Sizing::grow(),
                });
                for (uint32_t r = last_slice.first_index; r < last_slice.end_index; ++r) {
                    char row_lbl[32] = {};
                    std::snprintf(row_lbl, sizeof(row_lbl), "Row %u", r);
                    etch::begin_box(etch::BoxDesc{
                        .width = etch::Sizing::grow(),
                        .height = etch::Sizing::fixed(28.0f),
                    });
                    etch::text(etch::str_cstr(row_lbl), etch::TextDesc{ .font_size_px = 13.0f, .wrap = false });
                    etch::end_box();
                }
                etch::end_virtual_list();
                etch::split_second();
                last_cs = etch::begin_canvas(etch::CanvasDesc{
                    .id = kCanvasId,
                    .width = etch::Sizing::grow(),
                    .height = etch::Sizing::grow(),
                });
                etch::canvas_wire(etch::CanvasWireDesc{
                    .from_world = node_pos + etch::Vec2{ 180.0f, 36.0f },
                    .to_world = node_pos + etch::Vec2{ 360.0f, 140.0f },
                    .glow_color = etch::Color::rgba8(56, 189, 248, 80),
                    .glow_world = 8.0f,
                });
                etch::begin_canvas_node(etch::CanvasNodeDesc{
                    .id = kNodeId,
                    .world_pos = &node_pos,
                    .width_world = etch::Sizing::fixed(180.0f),
                    .padding_world = etch::Padding::all(10.0f),
                });
                etch::text("Canvas Node Alpha", etch::TextDesc{ .font_size_px = 13.5f, .wrap = false });
                etch::end_canvas_node();
                etch::end_canvas();
                etch::end_split();
                etch::end_frame(vctx);
            };

            // Warmup & verify initial 1M list O(visible) slice + Canvas prims
            step_v10_frame({-1.0f, -1.0f}, false, {});
            step_v10_frame({-1.0f, -1.0f}, false, {});

            const float div_x = (static_cast<float>(kHeadlessW) - 6.0f) * 0.40f + 3.0f;
            step_v10_frame({div_x, 400.0f}, true, {});
            step_v10_frame({div_x + 120.0f, 400.0f}, true, {});
            step_v10_frame({div_x + 120.0f, 400.0f}, false, {});
            const float dragged_ratio = etch::split_ratio(kSplitId);

            etch::virtual_list_scroll_to_row(kListId, 999990u, 28.0f, etch::AlignY::Top);
            step_v10_frame({-1.0f, -1.0f}, false, {});

            const etch::Vec2 zoom_anchor = { 860.0f, 380.0f };
            const etch::Vec2 world_before = etch::canvas_state(kCanvasId).screen_to_world(zoom_anchor);
            step_v10_frame(zoom_anchor, false, {0.0f, 2.0f});
            const etch::CanvasState zoomed_cs = etch::canvas_state(kCanvasId);
            const etch::Vec2 world_after = zoomed_cs.screen_to_world(zoom_anchor);
            const float anchor_dx = (world_after.x > world_before.x) ? (world_after.x - world_before.x) : (world_before.x - world_after.x);
            const float anchor_dy = (world_after.y > world_before.y) ? (world_after.y - world_before.y) : (world_before.y - world_after.y);

            const etch::Rect nb = etch::element_bounds(kNodeId);
            const etch::Vec2 node_grab = { nb.x + 20.0f, nb.y + 18.0f };
            const etch::Vec2 orig_node_pos = node_pos;
            step_v10_frame(node_grab, true, {});
            step_v10_frame({node_grab.x + 60.0f, node_grab.y + 40.0f}, true, {});
            step_v10_frame({node_grab.x + 60.0f, node_grab.y + 40.0f}, false, {});
            step_v10_frame({-1.0f, -1.0f}, false, {});

            uint32_t grid_prims = 0u;
            uint32_t wire_prims = 0u;
            for (const etch::Prim& p : etch::frame_prims(vctx)) {
                const auto kind = static_cast<etch::PrimKind>(p.style.x & 0xFu);
                if (kind == etch::PrimKind::CanvasGrid) ++grid_prims;
                if (kind == etch::PrimKind::BezierWire) ++wire_prims;
            }
            const etch::FrameStats vstats = etch::frame_stats(vctx);
            const bool split_ok = dragged_ratio > 0.47f;
            const bool vlist_ok = (last_slice.total_count == 1000000u) &&
                                  (last_slice.first_index <= 999990u) &&
                                  (last_slice.end_index > 999990u) &&
                                  ((last_slice.end_index - last_slice.first_index) < 64u) &&
                                  (last_slice.scroll_y > 27000000.0);
            const bool canvas_ok = (zoomed_cs.zoom > 1.20f) &&
                                   (anchor_dx < 0.05f) && (anchor_dy < 0.05f) &&
                                   (node_pos.x > orig_node_pos.x + 20.0f) &&
                                   (node_pos.y > orig_node_pos.y + 15.0f) &&
                                   (grid_prims >= 1u) && (wire_prims >= 2u) &&
                                   (vstats.draw_call_count == 1u);
            v10a_ok = split_ok && vlist_ok && canvas_ok;
            if (!v10a_ok) {
                std::fprintf(
                    stderr,
                    "Headless assert [10b] failed: split=%d(r=%.3f) vlist=%d([%u,%u) y=%.1f) canvas=%d(z=%.3f ad=%.3f,%.3f pos=%.1f,%.1f grid=%u wire=%u dc=%u)\n",
                    split_ok ? 1 : 0, dragged_ratio,
                    vlist_ok ? 1 : 0, last_slice.first_index, last_slice.end_index, last_slice.scroll_y,
                    canvas_ok ? 1 : 0, zoomed_cs.zoom, anchor_dx, anchor_dy, node_pos.x, node_pos.y,
                    grid_prims, wire_prims, vstats.draw_call_count);
            }
            etch::FrameStats v11_stats = {};
            const bool fonts_ok =
                etch::is_font_loaded(vctx, etch::kFontSans) &&
                etch::is_font_loaded(vctx, etch::kFontBold) &&
                etch::is_font_loaded(vctx, etch::kFontMono) &&
                !etch::is_font_loaded(vctx, 15u);

#if defined(_WIN32)
            const char* probe_font_a = "C:/Windows/Fonts/arial.ttf";
            const char* probe_font_b = "C:/Windows/Fonts/consolab.ttf";
#elif defined(__APPLE__)
            const char* probe_font_a = "/System/Library/Fonts/Helvetica.ttc";
            const char* probe_font_b = "/System/Library/Fonts/Menlo.ttc";
#else
            const char* probe_font_a = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
            const char* probe_font_b = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";
#endif
            const uint16_t slot7a = etch::load_font_file(vctx, probe_font_a, 7u);
            const bool load7a_ok = (slot7a == 7u) && etch::is_font_loaded(vctx, 7u);
            const uint16_t slot7b = etch::load_font_file(vctx, probe_font_b, 7u);
            const bool reload7b_ok = (slot7b == 7u) && etch::is_font_loaded(vctx, 7u);

            const uint32_t kV11Link = etch::id("v11_rich_link");
            uint32_t v11_clicks = 0u;
            bool show_blocker = false;
            bool span_hov_seen = false;
            const auto step_v11_frame = [&](etch::Vec2 ptr, bool down) noexcept {
                etch::begin_frame(vctx, etch::FrameDesc{
                    .viewport_w = kHeadlessW,
                    .viewport_h = kHeadlessH,
                    .dpi_scale = 1.0f,
                    .dt_seconds = kStepDt,
                    .pointer_pos = ptr,
                    .pointer_down = down,
                });
                if (etch::clicked(kV11Link)) {
                    ++v11_clicks;
                }
                if (etch::hovered(kV11Link)) {
                    span_hov_seen = true;
                }
                etch::begin_box(etch::BoxDesc{
                    .axis = etch::Axis::Col,
                    .width = etch::Sizing::fixed(210.0f),
                    .height = etch::Sizing::fit(),
                    .padding = etch::Padding::all(12.0f),
                    .background = etch::Color::rgb_hex(0x0F172A),
                });
                etch::text_spans({
                    etch::TextSpan{
                        .text = etch::str_lit("Slot7 "),
                        .color = etch::Color::rgb_hex(0xF8FAFC),
                        .font_size_px = 16.0f,
                        .font_id = 7u,
                    },
                    etch::TextSpan{
                        .text = etch::str_lit("MonoPill"),
                        .color = etch::Color::rgb_hex(0x38BDF8),
                        .background = etch::Color::rgba8(14, 116, 144, 80),
                        .border_color = etch::Color::rgb_hex(0x38BDF8),
                        .font_size_px = 12.0f,
                        .font_id = etch::kFontMono,
                        .padding_x = 6.0f,
                        .bg_radius = 4.0f,
                    },
                    etch::TextSpan{
                        .text = etch::str_lit(" \xE4\xB8\xAD\xE6\x96\x87 \xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9 \xE2\x9C\x93 "),
                        .color = etch::Color::rgb_hex(0xCBD5E1),
                        .font_size_px = 14.0f,
                        .strikethrough = true,
                    },
                    etch::TextSpan{
                        .text = etch::str_lit("ActionLink"),
                        .color = etch::Color::rgb_hex(0xF472B6),
                        .font_size_px = 14.0f,
                        .font_id = etch::kFontBold,
                        .underline = true,
                        .id = kV11Link,
                    },
                }, etch::RichTextDesc{ .font_size_px = 14.0f, .wrap = true });
                etch::end_box();

                if (show_blocker) {
                    etch::begin_box(etch::BoxDesc{
                        .id = etch::id("v11_blocker"),
                        .width = etch::Sizing::fixed(300.0f),
                        .height = etch::Sizing::fixed(300.0f),
                        .background = etch::Color::rgba8(15, 23, 42, 180),
                        .attach_to = etch::AttachTo::Root,
                        .anchor_parent = etch::Anchor::TopLeft,
                        .floating_offset = { 0.0f, 0.0f },
                        .z_index = 20,
                        .pointer_passthrough = false,
                    });
                    etch::end_box();
                }
                etch::end_frame(vctx);
            };

            step_v11_frame({-1.0f, -1.0f}, false);
            step_v11_frame({-1.0f, -1.0f}, false);
            const etch::Rect link_bounds = etch::element_bounds(kV11Link);
            const etch::Vec2 link_pt = {
                link_bounds.x + 0.5f * link_bounds.w,
                link_bounds.y + 0.5f * link_bounds.h,
            };
            step_v11_frame(link_pt, false);
            step_v11_frame(link_pt, true);
            step_v11_frame(link_pt, false);
            step_v11_frame(link_pt, false);

            show_blocker = true;
            step_v11_frame(link_pt, false);
            step_v11_frame(link_pt, true);
            step_v11_frame(link_pt, false);
            step_v11_frame(link_pt, false);
            const uint32_t clicks_while_blocked = v11_clicks;

            show_blocker = false;
            step_v11_frame(link_pt, false);
            step_v11_frame(link_pt, true);
            step_v11_frame(link_pt, false);
            step_v11_frame(link_pt, false);
            v11_stats = etch::frame_stats(vctx);

            v11_ok =
                fonts_ok &&
                load7a_ok &&
                reload7b_ok &&
                (link_bounds.w > 20.0f && link_bounds.h > 10.0f) &&
                span_hov_seen &&
                (clicks_while_blocked == 1u) &&
                (v11_clicks == 2u) &&
                (v11_stats.prim_count >= 12u) &&
                (v11_stats.draw_call_count == 1u);
            if (!v11_ok) {
                std::fprintf(
                    stderr,
                    "Headless assert [11] failed: fonts=%d load7a=%d reload7b=%d bounds=(%.1f,%.1f %.1fx%.1f) hov=%d blk_clicks=%u final_clicks=%u prims=%u dc=%u\n",
                    fonts_ok ? 1 : 0,
                    load7a_ok ? 1 : 0,
                    reload7b_ok ? 1 : 0,
                    link_bounds.x, link_bounds.y, link_bounds.w, link_bounds.h,
                    span_hov_seen ? 1 : 0,
                    clicks_while_blocked,
                    v11_clicks,
                    v11_stats.prim_count,
                    v11_stats.draw_call_count);
            }

            etch::destroy_context(vctx);
            etch::set_current_context(ctx);
        }
    }

    // 12. v1.2 Vector Icons, RingArc, Box-Local Draw API, Viewport Popover Clamping, Pro Input, Drop & UTF-8 Undo/Redo Assertion.
    bool v12_ok = false;
    {
        etch::Context* v12ctx = etch::create_context(etch::ContextDesc{ .headless = true });
        if (!v12ctx) {
            std::fprintf(stderr, "Headless assert [12] failed: create_context failed\n");
        } else {
            const uint16_t ico_arc = etch::register_svg_icon(
                v12ctx,
                etch::str_lit("M4 16A8 8 0 1 1 20 16Z"),
                {0.0f, 0.0f, 24.0f, 24.0f});
            const uint16_t ico_donut = etch::register_svg_icon(
                v12ctx,
                etch::str_lit("M12 2A10 10 0 1 1 2 12A10 10 0 0 1 12 2Z M12 6A6 6 0 1 0 18 12A6 6 0 0 0 12 6Z"),
                {0.0f, 0.0f, 24.0f, 24.0f});
            const bool icons_ok =
                etch::is_icon_loaded(v12ctx, ico_arc) &&
                etch::is_icon_loaded(v12ctx, ico_donut) &&
                !etch::is_icon_loaded(v12ctx, 200u);

            const uint32_t kBtnId = etch::id("v12_btn");
            const uint32_t kTiId  = etch::id("v12_ti");
            const uint32_t kPopId = etch::id("v12_pop");
            char u_buf[128] = "Hello";
            etch::TextInputResult ti_res = {};
            bool pop_open = false;
            uint32_t shortcut_gated = 0u;
            uint32_t shortcut_forced = 0u;
            uint32_t btn_right_clicks = 0u;
            uint32_t any_right_clicks = 0u;
            uint32_t btn_dbl_clicks = 0u;
            bool drop_seen = false;
            bool drop_cleared = false;

            const auto step_v12 = [&](
                etch::Vec2 ptr,
                bool left_down,
                bool right_down = false,
                uint8_t clicks = 1u,
                uint16_t kmods = etch::Mod_None,
                uint32_t key = 0u) noexcept
            {
                etch::begin_frame(v12ctx, etch::FrameDesc{
                    .viewport_w = kHeadlessW,
                    .viewport_h = kHeadlessH,
                    .dpi_scale = 1.0f,
                    .dt_seconds = kStepDt,
                    .pointer_pos = ptr,
                    .pointer_down = left_down,
                    .right_pointer_down = right_down,
                    .pointer_clicks = clicks,
                    .key_mods = kmods,
                    .key_pressed_code = key,
                });
                if (etch::shortcut(etch::Mod_Ctrl, SDLK_K, false)) {
                    ++shortcut_gated;
                }
                if (etch::shortcut(etch::Mod_Ctrl, SDLK_K, true)) {
                    ++shortcut_forced;
                }
                if (etch::right_clicked(kBtnId)) {
                    ++btn_right_clicks;
                }
                if (etch::right_clicked_anywhere()) {
                    ++any_right_clicks;
                }
                if (etch::double_clicked(kBtnId)) {
                    ++btn_dbl_clicks;
                }
                if (pop_open && etch::should_close_popover(kPopId, kBtnId)) {
                    pop_open = false;
                }

                const auto drops = etch::dropped_files();
                if (!drops.empty()) {
                    const etch::Str expected = etch::str_lit("C:/demo/\xE7\x9F\xA2\xE9\x87\x8F\xE5\x9B\xBE.svg");
                    if (drops.size() == 1u &&
                        drops[0].length == expected.length &&
                        std::memcmp(drops[0].chars, expected.chars, expected.length) == 0)
                    {
                        drop_seen = true;
                    }
                } else if (drop_seen) {
                    drop_cleared = true;
                }

                etch::begin_box(etch::BoxDesc{
                    .axis = etch::Axis::Col,
                    .width = etch::Sizing::grow(),
                    .height = etch::Sizing::grow(),
                    .padding = etch::Padding::all(60.0f),
                    .gap = 16.0f,
                    .background = etch::Color::rgb_hex(0x0F172A),
                });
                etch::begin_box(etch::BoxDesc{
                    .id = kBtnId,
                    .axis = etch::Axis::Row,
                    .width = etch::Sizing::fixed(160.0f),
                    .height = etch::Sizing::fixed(52.0f),
                    .padding = etch::Padding::xy(10.0f, 8.0f),
                    .gap = 8.0f,
                    .align_y = etch::AlignY::Center,
                    .background = etch::Color::rgb_hex(0x1E293B),
                    .radius = etch::Corners::all(8.0f),
                });
                etch::draw_ring({
                    .center = { 132.0f, 26.0f },
                    .radius = 14.0f,
                    .thickness = 3.5f,
                    .sweep_angle_rad = 4.2f,
                    .color = etch::Color::rgb_hex(0x38BDF8),
                    .color_end = etch::Color::rgb_hex(0xA855F7),
                    .track_color = etch::Color::rgba8(148, 163, 184, 50),
                });
                etch::draw_bezier({
                    .p0 = { 6.0f, 44.0f },
                    .p1 = { 48.0f, 8.0f },
                    .p2 = { 90.0f, 46.0f },
                    .p3 = { 120.0f, 12.0f },
                    .cubic = true,
                    .color_start = etch::Color::rgb_hex(0x34D399),
                    .color_end = etch::Color::rgb_hex(0xFBBF24),
                    .thickness = 2.0f,
                    .foreground = true,
                });
                etch::icon({ .icon_id = ico_arc, .size_px = 20.0f, .color = etch::Color::rgb_hex(0xF8FAFC) });
                etch::icon({ .icon_id = ico_donut, .size_px = 20.0f, .color = etch::Color::rgb_hex(0x38BDF8) });
                etch::end_box();

                ti_res = etch::text_input(etch::TextInputDesc{
                    .id = kTiId,
                    .buffer = u_buf,
                    .capacity = sizeof(u_buf),
                    .width = etch::Sizing::fixed(260.0f),
                    .height = etch::Sizing::fixed(36.0f),
                });

                if (pop_open) {
                    etch::begin_box(etch::BoxDesc{
                        .id = kPopId,
                        .width = etch::Sizing::fixed(220.0f),
                        .height = etch::Sizing::fixed(120.0f),
                        .background = etch::Color::rgb_hex(0x1E293B),
                        .border_color = etch::Color::rgb_hex(0x38BDF8),
                        .border_width = 1.0f,
                        .radius = etch::Corners::all(10.0f),
                        .attach_to = etch::AttachTo::Root,
                        .anchor_parent = etch::Anchor::BottomRight,
                        .floating_offset = { 40.0f, 40.0f },
                        .z_index = 25,
                        .clamp_to_viewport = true,
                    });
                    etch::end_box();
                }

                etch::end_box();
                etch::end_frame(v12ctx);
            };

            step_v12({-1.0f, -1.0f}, false);
            step_v12({-1.0f, -1.0f}, false);
            const etch::FrameStats v12_stats = etch::frame_stats(v12ctx);
            uint32_t ring_prims = 0u;
            uint32_t bezier_prims = 0u;
            uint32_t slug_prims = 0u;
            for (const etch::Prim& p : etch::frame_prims(v12ctx)) {
                const auto kind = static_cast<etch::PrimKind>(p.style.x & 0xFu);
                if (kind == etch::PrimKind::RingArc) ++ring_prims;
                else if (kind == etch::PrimKind::BezierWire) ++bezier_prims;
                else if (kind == etch::PrimKind::SlugGlyph) ++slug_prims;
            }

            const etch::Rect b_bounds = etch::element_bounds(kBtnId);
            const etch::Vec2 b_pt = { b_bounds.x + 0.5f * b_bounds.w, b_bounds.y + 0.5f * b_bounds.h };
            const etch::Rect t_bounds = etch::element_bounds(kTiId);
            const etch::Vec2 t_pt = { t_bounds.x + 0.5f * t_bounds.w, t_bounds.y + 0.5f * t_bounds.h };

            // Frame-0 popover viewport clamp & outside-click immunity
            pop_open = true;
            step_v12({20.0f, 20.0f}, true);
            const bool frame0_immune = pop_open;
            const etch::Rect p_bounds = etch::element_bounds(kPopId);
            const bool clamp_ok =
                (p_bounds.w > 200.0f) &&
                (p_bounds.x >= 5.5f) && (p_bounds.y >= 5.5f) &&
                ((p_bounds.x + p_bounds.w) <= 1274.5f) &&
                ((p_bounds.y + p_bounds.h) <= 794.5f);
            step_v12({20.0f, 20.0f}, false);
            step_v12({ p_bounds.x + 40.0f, p_bounds.y + 40.0f }, true);
            const bool stay_inside = pop_open;
            step_v12({ p_bounds.x + 40.0f, p_bounds.y + 40.0f }, false);
            step_v12({20.0f, 20.0f}, true);
            const bool closed_outside = !pop_open;
            step_v12({20.0f, 20.0f}, false);

            // Double click + Right click
            step_v12(b_pt, true, false, 2u);
            step_v12(b_pt, false, false, 1u);
            step_v12(b_pt, false, true);
            step_v12(b_pt, false, false);

            // Shortcut suppression while writable text_input is focused
            step_v12(t_pt, true);
            step_v12(t_pt, false);
            step_v12({-1.0f, -1.0f}, false, false, 1u, etch::Mod_Ctrl, SDLK_K);
            const bool gated_while_typing = (shortcut_gated == 0u) && (shortcut_forced == 1u);
            step_v12({20.0f, 20.0f}, true);
            step_v12({20.0f, 20.0f}, false);
            step_v12({-1.0f, -1.0f}, false, false, 1u, etch::Mod_Ctrl, SDLK_K);
            const bool fired_unfocused = (shortcut_gated == 1u) && (shortcut_forced == 2u);

            // UTF-8 text_input Undo/Redo + redo branch truncation on new edit
            step_v12(t_pt, true);
            step_v12(t_pt, false);
            step_v12({-1.0f, -1.0f}, false, false, 1u, etch::Mod_None, SDLK_END);
            {
                SDL_Event tev = {};
                tev.type = SDL_EVENT_TEXT_INPUT;
                tev.text.text = " \xE4\xB8\x96\xE7\x95\x8C";
                etch::process_event(v12ctx, tev);
            }
            step_v12({-1.0f, -1.0f}, false);
            const bool ins_ok = (std::strcmp(u_buf, "Hello \xE4\xB8\x96\xE7\x95\x8C") == 0) && (ti_res.cursor_byte == 12u);
            step_v12({-1.0f, -1.0f}, false, false, 1u, etch::Mod_Ctrl, SDLK_Z);
            const bool undo_ok = (std::strcmp(u_buf, "Hello") == 0) && (ti_res.cursor_byte == 5u);
            step_v12({-1.0f, -1.0f}, false, false, 1u, etch::Mod_Ctrl, SDLK_Y);
            const bool redo_ok = (std::strcmp(u_buf, "Hello \xE4\xB8\x96\xE7\x95\x8C") == 0) && (ti_res.cursor_byte == 12u);
            step_v12({-1.0f, -1.0f}, false, false, 1u, etch::Mod_Ctrl, SDLK_Z);
            {
                SDL_Event tev = {};
                tev.type = SDL_EVENT_TEXT_INPUT;
                tev.text.text = "!";
                etch::process_event(v12ctx, tev);
            }
            step_v12({-1.0f, -1.0f}, false);
            step_v12({-1.0f, -1.0f}, false, false, 1u, etch::Mod_Ctrl, SDLK_Y);
            const bool trunc_ok = (std::strcmp(u_buf, "Hello!") == 0) && (ti_res.cursor_byte == 6u);

            // Drop file injection & single-frame consumption
            etch::inject_drop_file(v12ctx, etch::str_lit("C:/demo/\xE7\x9F\xA2\xE9\x87\x8F\xE5\x9B\xBE.svg"));
            step_v12({-1.0f, -1.0f}, false);
            step_v12({-1.0f, -1.0f}, false);

            v12_ok =
                icons_ok &&
                (ring_prims >= 2u) &&
                (bezier_prims >= 4u) &&
                (slug_prims >= 2u) &&
                (v12_stats.draw_call_count == 1u) &&
                frame0_immune &&
                clamp_ok &&
                stay_inside &&
                closed_outside &&
                (btn_dbl_clicks == 1u) &&
                (btn_right_clicks == 1u) &&
                (any_right_clicks == 1u) &&
                gated_while_typing &&
                fired_unfocused &&
                ins_ok &&
                undo_ok &&
                redo_ok &&
                trunc_ok &&
                drop_seen &&
                drop_cleared;
            if (!v12_ok) {
                std::fprintf(
                    stderr,
                    "Headless assert [12] failed: icons=%d ring=%u bez=%u slug=%u dc=%u imm=%d clamp=%d(%.1f,%.1f %.1fx%.1f) stay=%d cls=%d dbl=%u rc=%u/%u sc=%d/%d undo=%d/%d/%d/%d drop=%d/%d\n",
                    icons_ok ? 1 : 0, ring_prims, bezier_prims, slug_prims, v12_stats.draw_call_count,
                    frame0_immune ? 1 : 0, clamp_ok ? 1 : 0, p_bounds.x, p_bounds.y, p_bounds.w, p_bounds.h,
                    stay_inside ? 1 : 0, closed_outside ? 1 : 0, btn_dbl_clicks, btn_right_clicks, any_right_clicks,
                    gated_while_typing ? 1 : 0, fired_unfocused ? 1 : 0,
                    ins_ok ? 1 : 0, undo_ok ? 1 : 0, redo_ok ? 1 : 0, trunc_ok ? 1 : 0,
                    drop_seen ? 1 : 0, drop_cleared ? 1 : 0);
            }

            etch::destroy_context(v12ctx);
            etch::set_current_context(ctx);
        }
    }

    // 13. v1.3 Window Chrome HitTest, Multi-Window Swapchains, draw_text/draw_image/measure_text, Native Texture Loader, Zero-Heap DND & Canvas Marquee.
    bool v13_ok = false;
    {
        const bool v13_owns_video = !SDL_WasInit(SDL_INIT_VIDEO) && SDL_InitSubSystem(SDL_INIT_VIDEO);
        etch::Context* v13ctx = etch::create_context(etch::ContextDesc{ .headless = true });
        if (!v13ctx) {
            std::fprintf(stderr, "Headless assert [13] failed: create_context failed\n");
        } else {
            // 13a. Texture loader (RGBA create/update, BMP file & memory decode)
            uint8_t tex_px[4u * 4u * 4u] = {};
            for (uint32_t i = 0u; i < 16u; ++i) {
                tex_px[i * 4u + 0u] = static_cast<uint8_t>((i * 15u) & 255u);
                tex_px[i * 4u + 1u] = 180u;
                tex_px[i * 4u + 2u] = 240u;
                tex_px[i * 4u + 3u] = 255u;
            }
            etch::LoadedTexture dyn_tex = etch::create_texture_rgba(v13ctx, etch::TextureDesc{
                .rgba_pixels = tex_px,
                .width = 4u,
                .height = 4u,
                .srgb = true,
            });
            tex_px[0] = 255u;
            const bool upd_ok = dyn_tex && etch::update_texture_rgba(v13ctx, dyn_tex.texture, 4u, 4u, tex_px);

            const char* kProbeBmp = "_etch_v13_probe.bmp";
            etch::LoadedTexture file_tex = {};
            etch::LoadedTexture mem_tex = {};
            if (SDL_Surface* surf = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32)) {
                std::memcpy(surf->pixels, tex_px, sizeof(tex_px));
                if (SDL_SaveBMP(surf, kProbeBmp)) {
                    file_tex = etch::load_texture_file(v13ctx, kProbeBmp, true);
                    if (FILE* f = std::fopen(kProbeBmp, "rb")) {
                        uint8_t bmp_buf[512] = {};
                        const size_t nread = std::fread(bmp_buf, 1u, sizeof(bmp_buf), f);
                        std::fclose(f);
                        if (nread > 0u) {
                            mem_tex = etch::load_texture_memory(v13ctx, bmp_buf, nread, true);
                        }
                    }
                    std::remove(kProbeBmp);
                }
                SDL_DestroySurface(surf);
            }
            const bool tex_ok = dyn_tex && upd_ok && file_tex && file_tex.width == 4u && mem_tex && mem_tex.height == 4u;

            // 13b. Multi-window swapchains + release_window lifecycle
            SDL_Window* win_a = SDL_CreateWindow("Etch Win A", 320, 240, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
            SDL_Window* win_b = SDL_CreateWindow("Etch Win B", 320, 240, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
            bool multi_win_ok = false;
            if (win_a && win_b) {
                etch::begin_frame(v13ctx, etch::FrameDesc{ .viewport_w = 320u, .viewport_h = 240u, .dpi_scale = 1.0f, .dt_seconds = kStepDt });
                etch::box({ .width = etch::Sizing::grow(), .height = etch::Sizing::grow(), .background = etch::Color::rgb_hex(0x0F172A) });
                etch::end_frame(v13ctx);
                const bool ra = etch::render_window(v13ctx, etch::Color::rgb_hex(0x0E1014), win_a);
                const bool rb = etch::render_window(v13ctx, etch::Color::rgb_hex(0x0E1014), win_b);
                const uint32_t cnt2 = etch::claimed_window_count(v13ctx);
                etch::release_window(v13ctx, win_b);
                const uint32_t cnt1 = etch::claimed_window_count(v13ctx);
                etch::release_window(v13ctx, win_a);
                const uint32_t cnt0 = etch::claimed_window_count(v13ctx);
                multi_win_ok = ra && rb && (cnt2 == 2u) && (cnt1 == 1u) && (cnt0 == 0u);
            }

            // 13c. Box-local draw_text/draw_image/measure_text, Custom Chrome HitTest, Drag-Drop & Canvas Marquee
            etch::set_window_chrome(etch::WindowChromeDesc{
                .enabled = true,
                .resizable = true,
                .border_thickness = 6.0f,
                .corner_size = 12.0f,
            });
            const etch::Vec2 measured = etch::measure_text(etch::str_lit("ACES\nFilmic"), etch::kFontSans, 14.0f);
            const bool measure_ok = (measured.x > 10.0f && measured.y > 20.0f);

            const uint32_t kTitleFocusBtn = etch::id("v13_focus_btn");
            const uint32_t kDragSrcId     = etch::id("v13_dnd_src");
            const uint32_t kDropDstId     = etch::id("v13_dnd_dst");
            const uint32_t kMarqueeCanvas = etch::id("v13_marquee_canvas");
            const uint32_t kPayloadKind   = etch::id("v13_kind");

            uint32_t src_clicks = 0u;
            bool dnd_active_seen = false;
            bool dnd_peek_seen = false;
            bool dnd_drop_ok = false;
            bool marquee_active_seen = false;
            etch::Rect marquee_final_rect = {};
            bool marquee_fin_seen = false;

            const auto step_v13 = [&](etch::Vec2 ptr, bool left_down, bool start_dnd = false) noexcept {
                etch::begin_frame(v13ctx, etch::FrameDesc{
                    .viewport_w = kHeadlessW,
                    .viewport_h = kHeadlessH,
                    .dpi_scale = 1.0f,
                    .dt_seconds = kStepDt,
                    .pointer_pos = ptr,
                    .pointer_down = left_down,
                });
                ETCH_BOX({
                    .axis = etch::Axis::Col,
                    .width = etch::Sizing::grow(),
                    .height = etch::Sizing::grow(),
                    .background = etch::Color::rgb_hex(0x0B0F17),
                }) {
                    // Titlebar region
                    ETCH_BOX({
                        .id = etch::id("v13_title"),
                        .axis = etch::Axis::Row,
                        .width = etch::Sizing::grow(),
                        .height = etch::Sizing::fixed(40.0f),
                        .padding = etch::Padding::xy(16.0f, 6.0f),
                        .gap = 12.0f,
                    }) {
                        etch::window_drag_region(etch::Rect{ 0.0f, 0.0f, 1280.0f, 40.0f });
                        etch::window_client_region(etch::Rect{ 20.0f, 6.0f, 80.0f, 28.0f });
                        ETCH_BOX({
                            .id = kTitleFocusBtn,
                            .width = etch::Sizing::fixed(70.0f),
                            .height = etch::Sizing::fixed(28.0f),
                            .background = etch::Color::rgb_hex(0x1E293B),
                            .focusable = true,
                        }) {}
                    }
                    // Plot box with draw_image + draw_text
                    ETCH_BOX({
                        .width = etch::Sizing::fixed(240.0f),
                        .height = etch::Sizing::fixed(100.0f),
                        .background = etch::Color::rgb_hex(0x111827),
                    }) {
                        if (dyn_tex) {
                            etch::draw_image(etch::ImageDrawDesc{
                                .texture = dyn_tex.texture,
                                .rect = etch::Rect{ 8.0f, 8.0f, 48.0f, 48.0f },
                                .radius = 6.0f,
                            });
                        }
                        char dynamic_lbl[48] = {};
                        std::snprintf(dynamic_lbl, sizeof(dynamic_lbl), "Peak %.1f dB", 3.5f);
                        etch::draw_text(etch::TextDrawDesc{
                            .text = etch::str_cstr(dynamic_lbl),
                            .pos = etch::Vec2{ 64.0f, 16.0f },
                            .font_id = etch::kFontSans,
                            .size = 13.0f,
                            .color = etch::Color::white(),
                        });
                    }
                    // Drag-and-Drop source & target
                    ETCH_BOX({
                        .axis = etch::Axis::Row,
                        .width = etch::Sizing::grow(),
                        .height = etch::Sizing::fixed(80.0f),
                        .gap = 20.0f,
                    }) {
                        ETCH_BOX({
                            .id = kDragSrcId,
                            .width = etch::Sizing::fixed(120.0f),
                            .height = etch::Sizing::fixed(44.0f),
                            .background = etch::Color::rgb_hex(0x0EA5E9),
                        }) {}
                        if (etch::clicked(kDragSrcId)) {
                            ++src_clicks;
                        }
                        if (start_dnd && etch::dragging(kDragSrcId, 4.0f)) {
                            const uint32_t node_tag = 42u;
                            etch::begin_drag_payload(
                                kDragSrcId,
                                kPayloadKind,
                                etch::str_lit("BloomPass"),
                                &node_tag,
                                sizeof(node_tag),
                                777ull,
                                42u);
                        }
                        if (etch::is_dragging_payload(kPayloadKind)) {
                            dnd_active_seen = true;
                        }
                        etch::DragPayloadView pv = {};
                        if (etch::peek_drag_payload(&pv, kPayloadKind) && pv.u64 == 777ull) {
                            dnd_peek_seen = true;
                        }

                        ETCH_BOX({
                            .id = kDropDstId,
                            .width = etch::Sizing::fixed(180.0f),
                            .height = etch::Sizing::fixed(60.0f),
                            .background = etch::Color::rgb_hex(0x1E293B),
                        }) {}
                        etch::DragPayloadView dv = {};
                        if (etch::accept_drag_payload(kDropDstId, kPayloadKind, &dv)) {
                            dnd_drop_ok = (dv.u32 == 42u && dv.size == sizeof(uint32_t) &&
                                           dv.data && *static_cast<const uint32_t*>(dv.data) == 42u &&
                                           dv.label.length == 9u);
                        }
                    }
                    // Canvas with marquee_select
                    const etch::CanvasState cs = etch::begin_canvas(etch::CanvasDesc{
                        .id = kMarqueeCanvas,
                        .width = etch::Sizing::grow(),
                        .height = etch::Sizing::grow(),
                        .default_pan = etch::Vec2{ 0.0f, 0.0f },
                        .default_zoom = 1.0f,
                        .marquee_select = true,
                    });
                    if (cs.marquee_active) {
                        marquee_active_seen = true;
                    }
                    if (cs.marquee_finished) {
                        marquee_fin_seen = true;
                        marquee_final_rect = cs.marquee_world;
                    }
                    etch::end_canvas();
                }
                etch::end_frame(v13ctx);
            };

            // Warm layout frame
            step_v13({ -1.0f, -1.0f }, false);
            step_v13({ -1.0f, -1.0f }, false);

            // Check Window Chrome HitTests (corners, edges, caption, explicit client, focusable button)
            const etch::Rect btn_bounds = etch::element_bounds(kTitleFocusBtn);
            const bool chrome_ok =
                etch::evaluate_window_hit(v13ctx, { -2.0f, 10.0f }) == etch::WindowHit::Normal &&
                etch::evaluate_window_hit(v13ctx, { 2.0f, 2.0f }) == etch::WindowHit::ResizeTopLeft &&
                etch::evaluate_window_hit(v13ctx, { 640.0f, 2.0f }) == etch::WindowHit::ResizeTop &&
                etch::evaluate_window_hit(v13ctx, { 1278.0f, 2.0f }) == etch::WindowHit::ResizeTopRight &&
                etch::evaluate_window_hit(v13ctx, { 2.0f, 400.0f }) == etch::WindowHit::ResizeLeft &&
                etch::evaluate_window_hit(v13ctx, { 1278.0f, 400.0f }) == etch::WindowHit::ResizeRight &&
                etch::evaluate_window_hit(v13ctx, { 2.0f, 798.0f }) == etch::WindowHit::ResizeBottomLeft &&
                etch::evaluate_window_hit(v13ctx, { 640.0f, 798.0f }) == etch::WindowHit::ResizeBottom &&
                etch::evaluate_window_hit(v13ctx, { 1278.0f, 798.0f }) == etch::WindowHit::ResizeBottomRight &&
                etch::evaluate_window_hit(v13ctx, { 50.0f, 20.0f }) == etch::WindowHit::Client &&
                btn_bounds.w > 0.0f &&
                etch::evaluate_window_hit(v13ctx, { btn_bounds.x + 10.0f, btn_bounds.y + 10.0f }) == etch::WindowHit::Client &&
                etch::evaluate_window_hit(v13ctx, { 640.0f, 20.0f }) == etch::WindowHit::Caption &&
                etch::evaluate_window_hit(v13ctx, { 640.0f, 300.0f }) == etch::WindowHit::Client;

            // Drag & Drop from kDragSrcId -> kDropDstId
            const etch::Rect src_r = etch::element_bounds(kDragSrcId);
            const etch::Rect dst_r = etch::element_bounds(kDropDstId);
            step_v13({ src_r.x + 15.0f, src_r.y + 15.0f }, true, false);
            step_v13({ dst_r.x + 40.0f, dst_r.y + 25.0f }, true, true);
            step_v13({ dst_r.x + 40.0f, dst_r.y + 25.0f }, false, false);
            step_v13({ src_r.x + 15.0f, src_r.y + 15.0f }, false, false);
            const bool dnd_ok = dnd_active_seen && dnd_peek_seen && dnd_drop_ok && (src_clicks == 0u);

            // Canvas Marquee Multi-Selection drag inside empty canvas
            const etch::Rect cv_r = etch::element_bounds(kMarqueeCanvas);
            const etch::Vec2 m0 = { cv_r.x + 40.0f, cv_r.y + 40.0f };
            const etch::Vec2 m1 = { cv_r.x + 240.0f, cv_r.y + 160.0f };
            step_v13(m0, true);
            step_v13(m1, true);
            step_v13(m1, false);
            const bool marquee_ok = marquee_active_seen && marquee_fin_seen &&
                marquee_final_rect.w > 195.0f && marquee_final_rect.h > 115.0f;

            v13_ok = tex_ok && multi_win_ok && measure_ok && chrome_ok && dnd_ok && marquee_ok;
            if (!v13_ok) {
                std::fprintf(
                    stderr,
                    "Headless assert [13] failed: tex=%d multi=%d meas=%d chrome=%d dnd=%d(act=%d,pk=%d,drop=%d,clk=%u) marq=%d(act=%d,fin=%d,%.1fx%.1f)\n",
                    tex_ok ? 1 : 0, multi_win_ok ? 1 : 0, measure_ok ? 1 : 0, chrome_ok ? 1 : 0,
                    dnd_ok ? 1 : 0, dnd_active_seen ? 1 : 0, dnd_peek_seen ? 1 : 0, dnd_drop_ok ? 1 : 0, src_clicks,
                    marquee_ok ? 1 : 0, marquee_active_seen ? 1 : 0, marquee_fin_seen ? 1 : 0, marquee_final_rect.w, marquee_final_rect.h);
            }

            if (SDL_GPUDevice* v13_dev = etch::gpu_device(v13ctx)) {
                SDL_WaitForGPUIdle(v13_dev);
            }
            etch::destroy_texture(v13ctx, &dyn_tex);
            etch::destroy_texture(v13ctx, &file_tex);
            etch::destroy_texture(v13ctx, &mem_tex);
            etch::destroy_context(v13ctx);
            if (win_b) SDL_DestroyWindow(win_b);
            if (win_a) SDL_DestroyWindow(win_a);
            if (v13_owns_video) SDL_QuitSubSystem(SDL_INIT_VIDEO);
            etch::set_current_context(ctx);
        }
    }


    // [14] Dock topology, balanced early exit, virtual table interaction and tree input.
    bool v14_ok = false;
    {
        auto* test = etch::create_context({ .headless = true });
        if (test) {
            etch::set_current_context(test);
            const uint32_t dock = etch::id("v14_dock"), p1 = etch::id("v14_p1"), p2 = etch::id("v14_p2"), p3 = etch::id("v14_p3");
            uint32_t left = 0, right = 0;
            const uint32_t root = etch::dock_reset_root(dock);
            bool checks = etch::dock_split_node(dock,root,etch::Axis::Row,.5f,&left,&right);
            checks &= etch::dock_add_pane(dock,left,{ .id = p1, .title = etch::str_lit("P1") });
            checks &= etch::dock_add_pane(dock,left,{ .id = p2, .title = etch::str_lit("P2"), .closable = true });
            checks &= etch::dock_add_pane(dock,right,{ .id = p3, .title = etch::str_lit("P3") });
            const uint32_t table_id = etch::id("v14_table"), tree = etch::id("v14_tree");
            const etch::TableColumnDesc cols[] = {
                { .title = etch::str_lit("Fixed"), .width = 120 },
                { .title = etch::str_lit("Stretch"), .sizing = etch::TableColumnSizing::Stretch, .width = 1 },
                { .title = etch::str_lit("Sort"), .width = 100 },
                { .title = etch::str_lit("Tail"), .sizing = etch::TableColumnSizing::Stretch, .width = 1 },
            };
            etch::TableResult tr = {}; etch::TreeNodeResult nr = {}; uint32_t first_row = 0;
            bool early = false;
            const auto step = [&](etch::Vec2 point = {-1,-1}, bool down = false, uint32_t key = 0) noexcept {
                etch::begin_frame(test,{ .viewport_w = 1000, .viewport_h = 720, .dt_seconds = 1.0f/60,
                    .pointer_pos = point, .pointer_down = down, .key_pressed_code = key });
                etch::begin_box({ .axis = etch::Axis::Col, .width = etch::Sizing::grow(), .height = etch::Sizing::grow() });
                etch::begin_dock_space({ .id = dock, .height = etch::Sizing::fixed(180) });
                while (const uint32_t pane = etch::next_dock_pane()) { etch::text(etch::str_lit("Content")); (void)pane; if (early) break; }
                etch::end_dock_space();
                tr = etch::begin_table({ .id = table_id, .columns = cols, .item_count = 1000000, .height = etch::Sizing::fixed(300), .padding = etch::Padding::xy(7,3) });
                uint32_t row = 0; first_row = tr.slice.first_index;
                while (etch::table_next_row(&row)) {
                    while (etch::table_next_column()) { etch::text(etch::str_lit("cell")); if (early) break; }
                    if (early) break;
                }
                etch::end_table();
                nr = etch::begin_tree_node({ .id = tree, .label = etch::str_lit("Parent") });
                if (nr.open) { const auto leaf = etch::begin_tree_node({ .id = etch::id("v14_leaf"), .label = etch::str_lit("Leaf"), .default_open = true, .leaf = true }); checks &= !leaf.open; etch::end_tree_node(); }
                etch::end_box(); etch::end_frame(test);
            };
            const auto click = [&](uint32_t widget) noexcept {
                const auto b = etch::element_bounds(widget); const etch::Vec2 p = {b.x+b.w*.5f,b.y+b.h*.5f}; step(p,true); step(p,false);
            };
            step(); step(); click(etch::id_index(p2,0xD300u));
            checks &= etch::dock_active_pane(dock,left) == p2;
            checks &= etch::dock_dock_pane(dock,p2,p3,etch::DockZone::Right); step(); step();
            const uint32_t moved = etch::dock_find_pane_leaf(dock,p2);
            checks &= moved != etch::dock_find_pane_leaf(dock,p3);
            checks &= etch::dock_dock_pane(dock,p2,p3,etch::DockZone::Center); step();
            checks &= etch::dock_find_pane_leaf(dock,p3) == right;
            checks &= etch::dock_dock_pane(dock,p2,p3,etch::DockZone::Bottom); step();
            etch::dock_set_pane_open(dock,p2,false); step(); checks &= etch::dock_find_pane_leaf(dock,p3) == right;
            etch::dock_set_pane_open(dock,p2,true); checks &= etch::dock_is_pane_open(dock,p2);
            // Release a real tab payload over each of the five geometric dock sectors.
            const etch::DockZone zones[] = {etch::DockZone::Left,etch::DockZone::Right,etch::DockZone::Top,etch::DockZone::Bottom,etch::DockZone::Center};
            for (const auto zone : zones) {
                etch::dock_activate_pane(dock,p2); step(); step();
                const uint32_t source_leaf = etch::dock_find_pane_leaf(dock,p2);
                const auto tab = etch::element_bounds(etch::id_index(p2,0xD300u));
                const uint32_t target_leaf = etch::dock_find_pane_leaf(dock,p1);
                const auto target_rect = etch::element_bounds(etch::id_index(dock,0xD200u+target_leaf));
                const etch::Vec2 start = {tab.x+tab.w*.5f,tab.y+tab.h*.5f};
                const etch::Vec2 finish = {target_rect.x+target_rect.w*(zone == etch::DockZone::Left ? .1f : (zone == etch::DockZone::Right ? .9f : .5f)),
                    target_rect.y+target_rect.h*(zone == etch::DockZone::Top ? .1f : (zone == etch::DockZone::Bottom ? .9f : .5f))};
                step(start,true); step(finish,true); step(finish,false); step();
                checks &= (etch::dock_find_pane_leaf(dock,p2) == etch::dock_find_pane_leaf(dock,p1)) == (zone == etch::DockZone::Center);
                if (zone != etch::DockZone::Center) {
                    checks &= etch::dock_find_pane_leaf(dock,p2) != source_leaf;
                    checks &= etch::dock_dock_pane(dock,p2,p3,etch::DockZone::Center);
                }
            }
            // Node storage grows beyond the former 32-node boundary.
            const uint32_t full = etch::id("v14_full"); uint32_t branch = etch::dock_reset_root(full), spare = 0;
            for (uint32_t n = 0; n < 15; ++n) {
                uint32_t child = 0; checks &= etch::dock_split_node(full,branch,etch::Axis::Row,.5f,&child,&spare); branch = child;
            }
            checks &= etch::dock_add_pane(full,branch,{ .id = p1, .title = etch::str_lit("source") });
            checks &= etch::dock_add_pane(full,spare,{ .id = p2, .title = etch::str_lit("target") });
            checks &= etch::dock_dock_pane(full,p1,p2,etch::DockZone::Right);
            checks &= etch::dock_find_pane_leaf(full,p1) != etch::dock_find_pane_leaf(full,p2);
            early = true; step(); early = false; step();
            const uint32_t col0 = etch::id_index(table_id,0x7500u);
            const auto handle = etch::element_bounds(etch::id_index(col0,0x7600u));
            const float before = etch::table_column_width(table_id,0);
            const etch::Vec2 hp = {handle.x+3,handle.y+handle.h*.5f}; step(hp,true); step({hp.x+42,hp.y},true); step({hp.x+42,hp.y},false);
            checks &= etch::table_column_width(table_id,0) > before+30 && !tr.sort_changed;
            click(etch::id_index(etch::id_index(table_id,0x7502u),0x7601u)); checks &= tr.sort_column == 2 && tr.sort_changed;
            etch::table_scroll_to_row(table_id,750000,26); step(); step();
            checks &= tr.slice.first_index <= 750000 && tr.slice.end_index > 750000;
            const auto header = etch::element_bounds(etch::id_index(etch::id_index(table_id,0x7501u),0x7601u));
            const auto cell = etch::element_bounds(etch::id_index(etch::id_index(table_id,0x100000u+first_row),1));
            checks &= std::fabs(header.x-cell.x) < 1.0f;
            click(etch::id_index(tree,0x7EE0u)); checks &= nr.open && nr.toggled;
            click(tree); step({-1,-1},false,SDLK_LEFT); checks &= !nr.open;
            step({-1,-1},false,SDLK_RIGHT); checks &= nr.open;
            click(etch::id_index(tree,0x7EE0u)); checks &= !nr.open;
            v14_ok = checks;
            if (!checks) std::fprintf(stderr,"Headless assert [14] failed: dock/table/tree regression\n");
            etch::destroy_context(test); etch::set_current_context(ctx);
        }
    }

    // [15] Multi-window isolation, spatial OS drop, modal focus and system theme.
    const bool v15_ok = check_v15_window_ui();
    // [16] Transient vectors, rotation, stack lifetime, capacity growth and idle reset.
    etch::set_current_context(ctx);
    bool v16_ok = true;
    char vector_png[1024] = {};
    std::snprintf(vector_png, sizeof(vector_png), "%s.vectors.png", out_png_path);
    uint32_t baseline_fills = 0u, baseline_wires = 0u;
    for (int frame = 0; frame < 4; ++frame) {
        etch::begin_frame(ctx, {.viewport_w = 320, .viewport_h = 180});
        ETCH_BOX({.width = etch::Sizing::fixed(320), .height = etch::Sizing::fixed(180),
            .radius = etch::Corners::all(14.0f), .clip_children = true}) {
            etch::Vec2 vertices[600] = {};
            for (uint32_t i = 0; i < 600; ++i) {
                const float a = float(i)*0.0104719755f;
                vertices[i] = {70+45*std::cos(a),70+45*std::sin(a)};
            }
            etch::draw_filled_polygon({.points = vertices, .fill = etch::Color::white()});
            etch::Vec2 star[10] = {};
            for (uint32_t i = 0; i < 10; ++i) {
                const float a = float(i)*0.62831853f-1.57079633f;
                const float r = (i & 1u) ? 9.0f : 20.0f;
                star[i] = {100.0f+std::cos(a)*r, 125.0f+std::sin(a)*r};
            }
            etch::draw_filled_polygon({.points = star, .fill = etch::Color::rgb_hex(0xFBBF24),
                .fill_end = etch::Color::rgb_hex(0xEA580C), .radial_gradient = true,
                .stroke_color = etch::Color::white(), .stroke_width = 1.5f});
            char svg[] = "M 130 20 C 210 0 240 120 140 100 Z";
            etch::draw_filled_path({.svg_path_d = etch::str_cstr(svg), .fill = etch::Color::rgb_hex(0x38BDF8),
                .fill_end = etch::Color::rgba8(56,189,248,0)});
            etch::draw_rect({.rect = {140,50,70,15}, .fill = etch::Color::white(), .angle_rad = 7.1f});
            etch::draw_icon({.icon_id = app.ico_spark, .rect = {220,40,35,35},
                .color = etch::Color::white(), .angle_rad = -0.5f});
            etch::draw_rect({.rect = {20.0f,-18.0f,90.0f,12.0f},
                .fill = etch::Color::white(), .angle_rad = 0.785398f});
            etch::draw_icon({.icon_id = app.ico_spark, .rect = {110.0f,-14.0f,24.0f,24.0f},
                .color = etch::Color::white(), .angle_rad = 0.785398f});
            // This icon is wholly above the clip until rotation, including the CPU cull pad.
            etch::draw_icon({.icon_id = app.ico_spark, .rect = {150.0f,-28.0f,24.0f,24.0f},
                .color = etch::Color::white(), .angle_rad = 0.785398f});
            // Only the quadratic arc: one wire, with no horizontal closing chord.
            if (frame > 0) {
                etch::draw_filled_path({.svg_path_d = etch::str_lit("M 160 145 Q 200 105 250 145"),
                    .fill = {}, .stroke_color = etch::Color::white(), .stroke_width = 2.0f});
            }
        }
        etch::end_frame(ctx);
        bool fill = false, rect = false, glyph = false;
        bool clipped_rect = false, clipped_glyph = false, above_clip_glyph = false;
        uint32_t fills = 0u, wires = 0u;
        for (const etch::Prim& p : etch::frame_prims(ctx)) {
            fill |= (p.style.x & 15u) == uint32_t(etch::PrimKind::SlugFill);
            rect |= (p.style.x & 15u) == uint32_t(etch::PrimKind::RotatedRect);
            glyph |= (p.style.x & 15u) == uint32_t(etch::PrimKind::SlugGlyph) && p.payload.z != 0;
            if (p.bounds.y < p.clip.y) {
                clipped_rect |= (p.style.x & 15u) == uint32_t(etch::PrimKind::RotatedRect);
                if ((p.style.x & 15u) == uint32_t(etch::PrimKind::SlugGlyph) && p.payload.z != 0u) {
                    clipped_glyph |= p.bounds.x == 110.0f;
                    above_clip_glyph |= p.bounds.y + p.bounds.w + 2.0f <= p.clip.y;
                }
            }
            fills += (p.style.x & 15u) == uint32_t(etch::PrimKind::SlugFill) ? 1u : 0u;
            wires += (p.style.x & 15u) == uint32_t(etch::PrimKind::BezierWire) ? 1u : 0u;
        }
        if (frame == 0) {
            baseline_fills = fills;
            baseline_wires = wires;
        } else {
            v16_ok &= fills == baseline_fills && wires == baseline_wires + 1u;
        }
        v16_ok &= fill && rect && glyph && etch::frame_stats(ctx).dynamic_slug_curve_count >= 600;
        v16_ok &= clipped_rect && clipped_glyph && above_clip_glyph;
        v16_ok &= etch::render_headless_png(ctx, 320, 180, vector_png);
    }
    v16_ok &= etch::frame_stats(ctx).draw_call_count <= 3 && !etch::needs_redraw(ctx) && etch::frame_stats(ctx).idle_sleeping;
    etch::begin_frame(ctx, {.viewport_w = 320, .viewport_h = 180});
    ETCH_BOX({.width = etch::Sizing::fixed(320), .height = etch::Sizing::fixed(180)}) {
        etch::text(etch::str_lit("Pure UI"));
    }
    etch::end_frame(ctx);
    v16_ok &= etch::frame_stats(ctx).dynamic_slug_curve_count == 0;
    if (!v16_ok) std::fprintf(stderr, "Headless assert [16] failed: transient fill/rotation/idle regression\n");
    std::remove(vector_png);
    // [17] Elastic buffers, wrapped UTF-8 selection, clipboard and spring idle.
    bool v20_ok = true;
    etch::Context* elastic = etch::create_context({.max_prims_per_frame=64u,.headless=true});
    if (!elastic) v20_ok = false;
    else {
        etch::set_current_context(elastic);
        etch::begin_frame(elastic,{.viewport_w=256,.viewport_h=128});
        ETCH_BOX({.width=etch::Sizing::grow(),.height=etch::Sizing::grow()}) {
            for (uint32_t i=0;i<620;++i) etch::draw_rect({.rect={float(i%200),float(i%100),4,4},.fill=etch::Color::white()});
        }
        etch::end_frame(elastic);
        char elastic_png[1024] = {};
        std::snprintf(elastic_png,sizeof(elastic_png),"%s.elastic.png",out_png_path);
        v20_ok &= etch::render_headless_png(elastic,256,128,elastic_png);
        const auto es = etch::frame_stats(elastic);
        v20_ok &= es.prim_count >= 620 && es.prim_capacity >= 1024 && es.draw_call_count == 1;
        std::remove(elastic_png);
        etch::destroy_context(elastic);
        etch::set_current_context(ctx);
    }
    const uint32_t sel_id = etch::id("v20_sel_text"), rich_id = etch::id("v20_sel_spans"), col_id = etch::id("v20_col");
    const etch::Str sample = etch::str_lit("Etch " ETCH_VERSION_STRING " 矢量引擎 — Zero-Heap UI Selection");
    etch::CollapsibleResult col = {};
    const auto v20_frame = [&](etch::Vec2 pointer=etch::Vec2{-1,-1}, bool down=false, uint32_t key=0u, uint16_t mods=etch::Mod_None) noexcept {
        etch::begin_frame(ctx,{.viewport_w=320,.viewport_h=240,.dt_seconds=1.0f/60.0f,.pointer_pos=pointer,.pointer_down=down,.key_mods=mods,.key_pressed_code=key});
        ETCH_BOX({.axis=etch::Axis::Col,.width=etch::Sizing::fixed(220),.height=etch::Sizing::grow(),.gap=8}) {
            etch::text(sample,{.font_size_px=16,.id=sel_id,.selectable=true});
            etch::text_spans({{.text=etch::str_lit("Rich 矢量 ")},{.text=etch::str_lit("selection across wrapped lines"),.font_id=etch::kFontBold}},
                {.font_size_px=14,.id=rich_id,.selectable=true});
            col = etch::begin_collapsible({.id=col_id,.title=etch::str_lit("Spring body")});
            if (col.visible) { etch::text(etch::str_lit("Child text")); etch::end_collapsible(); }
        }
        etch::end_frame(ctx);
    };
    v20_frame(); v20_frame();
    for (uint32_t target : {sel_id,rich_id}) {
        const auto bounds = etch::element_bounds(target);
        const float y = bounds.y + 5.0f;
        v20_frame({3,y},true);
        v20_frame({160,y+20},true);
        v20_ok &= etch::has_text_selection(target);
        const etch::Str drag = etch::selected_text(target);
        v20_ok &= drag.length > 0 && (static_cast<unsigned char>(drag.chars[0]) & 0xC0u) != 0x80u;
        const etch::Str full = target == sel_id ? sample : etch::str_lit("Rich 矢量 selection across wrapped lines");
        if (drag.length > 0) {
            const char* start = nullptr;
            for (uint32_t off=0;off+drag.length<=full.length;++off) {
                if (std::memcmp(full.chars+off,drag.chars,drag.length)==0) { start = full.chars+off; break; }
            }
            v20_ok &= start && (start+drag.length == full.chars+full.length || (static_cast<unsigned char>(start[drag.length]) & 0xC0u) != 0x80u);
        }
        bool highlight = false;
        for (const auto& prim : etch::frame_prims(ctx)) highlight |= (prim.style.x & 15u) == uint32_t(etch::PrimKind::Rect) && prim.style.y == etch::Color::rgba8(56,189,248,88).pack_rgba8();
        v20_ok &= highlight && etch::frame_stats(ctx).draw_call_count <= 2;
        v20_frame({160,y+20},false);
        etch::request_focus(target);
        v20_frame({-1,-1},false,SDLK_A,etch::Mod_Primary);
        v20_ok &= etch::selected_text(target).length == (target == sel_id ? sample.length : etch::str_lit("Rich 矢量 selection across wrapped lines").length);
        v20_frame({-1,-1},false,SDLK_C,etch::Mod_Primary);
        const auto selected = etch::selected_text(target), copied = etch::get_clipboard_text();
        v20_ok &= selected.length == copied.length && selected.length > 0 && std::memcmp(selected.chars,copied.chars,selected.length)==0;
        etch::clear_text_selection(target);
        v20_ok &= !etch::has_text_selection(target);
    }
    // Isolate the selection color so another element cannot satisfy the geometry check.
    const uint32_t rtl_id = etch::id("v20_sel_rtl");
    const etch::Color rtl_selection = etch::Color::rgba8(220,80,160,96);
    const auto rtl_frame = [&](etch::Str value, uint32_t key=0u, uint16_t mods=etch::Mod_None) noexcept {
        etch::begin_frame(ctx,{.viewport_w=320,.viewport_h=240,.key_mods=mods,.key_pressed_code=key});
        ETCH_BOX({.axis=etch::Axis::Col,.width=etch::Sizing::fixed(80),.height=etch::Sizing::grow()}) {
            etch::text(value,{.font_size_px=16,.id=rtl_id,.selectable=true,.selection_color=rtl_selection});
        }
        etch::end_frame(ctx);
    };
    const etch::Str rtl_cases[] = {
        etch::str_lit("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"),
        etch::str_lit("abc \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"),
        etch::str_lit("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D abc"),
        etch::str_lit("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D   \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D   \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D")
    };
    for (const auto value : rtl_cases) {
        rtl_frame(value);
        etch::request_focus(rtl_id);
        rtl_frame(value,SDLK_A,etch::Mod_Primary);
        v20_ok &= etch::has_text_selection(rtl_id) && etch::selected_text(rtl_id).length == value.length;
        uint32_t highlights = 0;
        for (const auto& prim : etch::frame_prims(ctx)) {
            if ((prim.style.x & 15u) == uint32_t(etch::PrimKind::Rect) &&
                prim.style.y == rtl_selection.pack_rgba8() && prim.bounds.z > prim.bounds.x + 1.0f) ++highlights;
        }
        v20_ok &= highlights >= (value.length > 20u ? 2u : 1u);
        etch::clear_text_selection(rtl_id);
    }
    v20_frame();
    etch::Rect hdr = etch::element_bounds(etch::id_index(col_id,0xC011u));
    const etch::Vec2 hc = {hdr.x+hdr.w*.5f,hdr.y+hdr.h*.5f};
    v20_frame(hc,true); v20_frame(hc,false);
    bool intermediate = false;
    for (int i=0;i<90;++i) { v20_frame(); intermediate |= col.anim_t>.02f && col.anim_t<.98f && col.visible; if (!etch::needs_redraw(ctx)) break; }
    v20_ok &= intermediate && !col.open && !col.visible && col.anim_t==0 && !etch::needs_redraw(ctx) && etch::frame_stats(ctx).idle_sleeping && etch::frame_stats(ctx).draw_call_count<=3;
    etch::request_focus(etch::id_index(col_id,0xC011u));
    v20_frame({-1,-1},false,SDLK_SPACE);
    for (int i=0;i<90 && etch::needs_redraw(ctx);++i) v20_frame();
    v20_ok &= col.open && col.visible && col.anim_t==1 && etch::frame_stats(ctx).idle_sleeping && etch::frame_stats(ctx).draw_call_count<=3;
    if (!v20_ok) std::fprintf(stderr,"Headless assert [17] failed: elastic/selection/collapsible regression\n");
    // [18] Elastic focus/docking, constrained typography, bidi and grapheme editing.
    bool v21_ok = true;
    {
        auto* test = etch::create_context({.headless=true});
        if (!test) v21_ok=false;
        else {
            etch::set_current_context(test);
            const uint32_t dock=etch::id("v21_dock"), root=etch::dock_reset_root(dock);
            for(uint32_t i=0;i<48;++i) {
                char title[128]={};std::snprintf(title,sizeof(title),"Tab %u with an arbitrary length title exceeding sixty bytes for retention checks",i);
                if (!(etch::dock_add_pane(dock,root,{.id=etch::id_index(dock,i+1),.title=etch::str_cstr(title)}))) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }
            }
            for(uint32_t i=0;i<48;++i){const uint32_t pane=etch::id_index(dock,i+1);etch::dock_activate_pane(dock,pane);if (!(etch::dock_active_pane(dock,root)==pane)) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }}
            char edit[128]="a👨‍👩‍👧 b"; etch::TextInputResult result={};
            const uint32_t field=etch::id("v21_edit"),label=etch::id("v21_label"),mixed=etch::id("v21_bidi");
            const auto step=[&](uint32_t key=0u, etch::Vec2 pointer=etch::Vec2{-1,-1}, bool down=false) noexcept {
                etch::begin_frame(test,{.viewport_w=640,.viewport_h=720,.pointer_pos=pointer,.pointer_down=down,.key_pressed_code=key});
                ETCH_BOX({.axis=etch::Axis::Col,.width=etch::Sizing::fixed(180),.height=etch::Sizing::grow()}) {
                    etch::text(etch::str_lit("A constrained inspector label with hidden content"),{.wrap=false,.id=label,.selectable=true,.ellipsis=true});
                    etch::text(etch::str_lit("Words wrap over several lines and stop after exactly two lines with an ellipsis"),{.id=etch::id("v21_wrap"),.selectable=true,.line_height_px=20,.ellipsis=true,.max_lines=2});
                    etch::text(etch::str_lit("User שלום 42 End"),{.wrap=false,.id=mixed,.selectable=true});
                    etch::text(etch::str_lit("🚀🔥✨"),{.font_size_px=28});
                    result=etch::text_input({.id=field,.buffer=edit,.capacity=sizeof(edit)});
                    for(uint32_t i=0;i<320;++i){etch::begin_box({.id=etch::id_index(dock,1000+i),.width=etch::Sizing::fixed(1),.height=etch::Sizing::fixed(1),.focusable=true});etch::end_box();}
                }
                etch::begin_dock_space({.id=dock,.width=etch::Sizing::fixed(640),.height=etch::Sizing::fixed(80)});
                while(etch::next_dock_pane())etch::text(etch::str_lit("Elastic dock content"));
                etch::end_dock_space();
                etch::end_frame(test);
            };
            step();step();
            etch::request_focus(etch::id_index(dock,1298));step(SDLK_TAB);
            if (!(etch::focused_id()==etch::id_index(dock,1299))) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }
            const auto bounds=etch::element_bounds(label),bidi=etch::element_bounds(mixed);
            if (!(bounds.w<=180.5f && bounds.w>0 && bidi.w>0)) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }
            const auto wrapped_bounds=etch::element_bounds(etch::id("v21_wrap"));
            if (!(wrapped_bounds.w<=180.5f && wrapped_bounds.h<=40.5f && wrapped_bounds.h>20)) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }
            // A marker must be present in the single-line label, not merely clipped original glyphs.
            bool marker=false;
            const auto reference=etch::measure_text(etch::str_lit("…"));
            if (!(reference.x>0)) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }
            for(const auto& prim:etch::frame_prims(test)) {
                if((prim.style.x&15u)==uint32_t(etch::PrimKind::SlugGlyph) && prim.bounds.x<bounds.x+bounds.w && prim.bounds.y>=bounds.y && prim.bounds.y<bounds.y+bounds.h) {
                    marker |= prim.bounds.x>bounds.x+bounds.w-20;
                    if (!(prim.clip.z<=180.5f)) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }
                }
            }
            if (!(marker)) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }
            etch::request_focus(mixed);etch::set_text_selection(mixed,5,14);step();
            bool highlight=false;
            for(const auto& prim:etch::frame_prims(test))highlight|=(prim.style.x&15u)==uint32_t(etch::PrimKind::Rect)&&prim.style.y==etch::Color::rgba8(56,189,248,88).pack_rgba8()&&prim.bounds.z>0.1f;
            if (!(highlight)) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }
            uint32_t color_count=0,last_color=0;
            for(const auto& prim:etch::frame_prims(test)) {
                if((prim.style.x&15u)==uint32_t(etch::PrimKind::SlugGlyph) && prim.style.y!=etch::Color::white().pack_rgba8() && prim.style.y!=last_color) {
                    ++color_count;last_color=prim.style.y;
                }
            }
#if defined(_WIN32)
            // Segoe UI Emoji on this host supplies vector COLR v0 layers.
            std::FILE* emoji_file=std::fopen("C:/Windows/Fonts/seguiemj.ttf","rb");
            if(emoji_file){std::fclose(emoji_file);if (!(color_count>=3)) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }}
#endif
            etch::request_focus(field);etch::set_text_selection(field,1,1);step(SDLK_RIGHT);
            if (!(result.cursor_byte==19)) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }
            step(SDLK_BACKSPACE);if (!(std::strcmp(edit,"a b")==0)) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }
            std::memcpy(edit,"e\xCC\x81z",5);step();etch::set_text_selection(field,0,0);step(SDLK_RIGHT);
            if (!(result.cursor_byte==3)) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }step(SDLK_BACKSPACE);if (!(std::strcmp(edit,"z")==0)) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }
            const char* graphemes[]={"\r\nz","🇨🇳z","각z","✊🏽z"};
            const uint32_t stops[]={2,8,9,7};
            for(uint32_t i=0;i<4;++i) {
                std::snprintf(edit,sizeof(edit),"%s",graphemes[i]);step();etch::set_text_selection(field,0,0);step(SDLK_RIGHT);
                if (!(result.cursor_byte==stops[i])) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }step(SDLK_BACKSPACE);if (!(std::strcmp(edit,"z")==0)) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }
            }
            // Trailing tabs must retain every preceding glyph advance in the caret and IME edge.
            std::memcpy(edit, "a\tb\t", 5); step();
            etch::set_text_selection(field, 4, 4); step();
            const float tab_width = etch::measure_text(etch::str_lit("\t"), etch::kFontSans, 14).x;
            const float glyph_width = etch::measure_text(etch::str_lit("a"), etch::kFontSans, 14).x
                + etch::measure_text(etch::str_lit("b"), etch::kFontSans, 14).x;
            const float text_width = etch::measure_text(etch::str_lit("a\tb\t"), etch::kFontSans, 14).x;
            const float text_origin = etch::element_bounds(field).x + etch::TextInputDesc{}.padding.left;
            const float caret_x = result.ime_cursor_rect.x - text_origin;
            bool caret_found = false;
            for (const auto& prim : etch::frame_prims(test)) {
                if ((prim.style.x & 15u) == uint32_t(etch::PrimKind::Rect)
                    && prim.style.y == etch::TextInputDesc{}.caret_color.pack_rgba8()
                    && std::abs(prim.bounds.z - 2.0f) < 0.01f) {
                    caret_found |= std::abs(prim.bounds.x - std::floor(text_origin + text_width + 0.5f)) < 0.01f;
                }
            }
            if (!(result.cursor_byte == 4 && std::abs(caret_x - text_width) < 0.01f
                && std::abs(caret_x - (glyph_width + 2.0f * tab_width)) < 0.01f
                && caret_x > 2.0f * tab_width + 0.01f && caret_found)) {
                v21_ok = false; std::fprintf(stderr, "v21 trailing-tab caret check failed\n");
            }
            const char* tab_deletions[] = {"a\tb", "a\t", "a", ""};
            for (uint32_t i = 0; i < 4; ++i) {
                step(SDLK_BACKSPACE);
                if (!(result.changed && result.cursor_byte == 3u - i && result.byte_length == 3u - i
                    && std::strcmp(edit, tab_deletions[i]) == 0)) {
                    v21_ok = false; std::fprintf(stderr, "v21 trailing-tab backspace check %u failed\n", i);
                }
            }
            std::snprintf(edit,sizeof(edit),"User שלום 42 End");step();step();
            const auto input_bounds=etch::element_bounds(field);
            const float prefix=etch::measure_text(etch::str_lit("User 42 "),etch::kFontSans,14).x;
            const etch::Vec2 hebrew_point={input_bounds.x+12+prefix+8,input_bounds.y+14};
            step(0,hebrew_point,true);step(0,hebrew_point,false);
            if (!(result.cursor_byte>=5 && result.cursor_byte<=13)) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }
            const etch::Vec2 latin_point={input_bounds.x+16,input_bounds.y+14};
            step(0,latin_point,true);step(0,latin_point,false);if (!(result.cursor_byte<=4)) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }
            char path[1024]={};std::snprintf(path,sizeof(path),"%s.v21.png",out_png_path);
            step();if (!(etch::render_headless_png(test,640,720,path))) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }
            const auto stats=etch::frame_stats(test);if (!(stats.draw_call_count<=3)) { v21_ok=false; std::fprintf(stderr,"v21 check line %d failed\n",__LINE__); }
            std::remove(path);etch::destroy_context(test);etch::set_current_context(ctx);
        }
    }
    if(!v21_ok)std::fprintf(stderr,"Headless assert [18] failed: v2.1 typography/focus/grapheme regression\n");
    // [19] Grid/wrap geometry and sparse million-row height reconciliation.
    bool v22_ok = true;
    {
        auto* test = etch::create_context({.headless=true});
        if (!test) v22_ok = false;
        else {
            etch::set_current_context(test);
            const uint32_t wrap=etch::id("v22_wrap"), grid=etch::id("v22_grid"), list=etch::id("v22_list");
            etch::VirtualListSlice slice = {};
            bool tall_first = false;
            const auto measure = [](uint32_t i, void*) noexcept -> float { return i%3 == 0 ? 24.0f : i%3 == 1 ? 52.0f : 88.0f; };
            const auto step = [&]() noexcept {
                etch::begin_frame(test,{.viewport_w=900,.viewport_h=900});
                etch::begin_box({.axis=etch::Axis::Col,.width=etch::Sizing::fixed(900),.height=etch::Sizing::fixed(900),.gap=8});
                etch::begin_box({.id=wrap,.width=etch::Sizing::fixed(260),.gap=6,.wrap=true,.row_gap=8});
                for(uint32_t i=0;i<7;++i) etch::box({.id=etch::id_index(wrap,i+1),.width=i==1?etch::Sizing::grow(80):etch::Sizing::fixed(80),.height=i==1?etch::Sizing::grow(24):etch::Sizing::fixed(24),.background=etch::Color::rgb_hex(0x2563EB)});
                etch::end_box();
                const etch::GridTrack tracks[]={etch::GridTrack::px(80),etch::GridTrack::fr(1),etch::GridTrack::fr(2)};
                etch::begin_box({.id=grid,.width=etch::Sizing::fixed(320),.gap=8,.row_gap=6,.grid_tracks=tracks});
                etch::box({.id=etch::id_index(grid,1),.width=etch::Sizing::grow(),.height=etch::Sizing::fixed(24),.grid_col_span=2});
                etch::box({.id=etch::id_index(grid,2),.width=etch::Sizing::grow(),.height=etch::Sizing::fixed(24)});
                for(uint32_t i=0;i<3;++i) {
                    etch::begin_box({.id=etch::id_index(grid,i+3),.width=etch::Sizing::grow(),.height=etch::Sizing::fit()});
                    etch::text(etch::str_lit("Grid cell wraps text within its track width."));etch::end_box();
                }
                etch::end_box();
                const etch::GridTrack bounded_tracks[]={etch::GridTrack::fr(1,80),etch::GridTrack::fr(1,0,40)};
                etch::begin_box({.width=etch::Sizing::fixed(100),.grid_tracks=bounded_tracks});
                etch::box({.id=etch::id("v22_bounded0"),.width=etch::Sizing::grow(),.height=etch::Sizing::fixed(8)});
                etch::box({.id=etch::id("v22_bounded1"),.width=etch::Sizing::grow(),.height=etch::Sizing::fixed(8)});
                etch::end_box();
                etch::box({.id=etch::id("v22_aspect"),.width=etch::Sizing::fixed(160),.aspect_ratio=16.0f/9});
                etch::image({.id=etch::id("v22_image"),.width=etch::Sizing::fit(),.height=etch::Sizing::fixed(90),.aspect_ratio=16.0f/9});
                slice=etch::begin_virtual_list({.id=list,.item_count=1000000,.item_height_px=28,.width=etch::Sizing::fixed(400),.height=etch::Sizing::fixed(240),.measure_height_fn=measure,.variable_height=true});
                for(uint32_t i=slice.first_index;i<slice.end_index;++i) etch::box({.id=etch::id_index(list,i+1),.width=etch::Sizing::grow(),.height=etch::Sizing::fixed(tall_first && i==0?64:i==499990?120:measure(i,nullptr)),.background=etch::Color::rgb_hex(0x164E63)});
                etch::end_virtual_list();etch::end_box();etch::end_frame(test);
            };
            const auto check=[&](bool value,int line) noexcept { if(!value){v22_ok=false;std::fprintf(stderr,"v22 check line %d failed\n",line);} };
            step();step();
            auto w=etch::element_bounds(wrap); auto a=etch::element_bounds(etch::id_index(wrap,1)); auto b=etch::element_bounds(etch::id_index(wrap,4)); auto c=etch::element_bounds(etch::id_index(wrap,7));
            check(std::fabs(w.h-88)<.2f && std::fabs(b.y-a.y-32)<.2f && std::fabs(c.y-a.y-64)<.2f && std::fabs(b.x-a.x)<.2f,__LINE__);
            check(etch::element_bounds(etch::id_index(wrap,2)).w>80,__LINE__);
            auto g0=etch::element_bounds(etch::id_index(grid,3)),g1=etch::element_bounds(etch::id_index(grid,4)),g2=etch::element_bounds(etch::id_index(grid,5));
            check(std::fabs(g0.w-80)<.2f && std::fabs(g2.w-2*g1.w)<.2f && std::fabs(g0.y-g2.y)<.2f,__LINE__);
            check(std::fabs(etch::element_bounds(etch::id_index(grid,1)).w-(80+g1.w+8))<.2f,__LINE__);
            check(std::fabs(etch::element_bounds(etch::id("v22_aspect")).h-90)<.2f && std::fabs(etch::element_bounds(etch::id("v22_image")).w-160)<.2f,__LINE__);
            check(std::fabs(etch::element_bounds(etch::id("v22_bounded0")).w-80)<.2f && std::fabs(etch::element_bounds(etch::id("v22_bounded1")).w-20)<.2f,__LINE__);
            check(slice.end_index-slice.first_index<=32,__LINE__);
            etch::virtual_list_scroll_to_row(list,500000,0);step();step();
            check(slice.first_index<=500000 && slice.end_index>500000 && std::fabs(etch::element_bounds(etch::id_index(list,500001)).y-etch::element_bounds(list).y)<.2f,__LINE__);
            const float before_y=etch::element_bounds(etch::id_index(list,500001)).y;
            const double before=etch::virtual_list_scroll_offset(list);
            etch::virtual_list_set_item_height(list,499990,120);
            check(std::fabs(etch::virtual_list_scroll_offset(list)-before-(120-measure(499990,nullptr)))<.01,__LINE__);
            step();step();check(std::fabs(etch::element_bounds(etch::id_index(list,500001)).y-before_y)<.2f,__LINE__);
            etch::virtual_list_scroll_to_row(list,999995,0);step();step();
            check(slice.end_index==1000000 && slice.scroll_y<=slice.max_scroll_y,__LINE__);
            for(int i=0;i<8;++i)step();
            char path[1024]={};std::snprintf(path,sizeof(path),"%s.v22.png",out_png_path);
            check(etch::render_headless_png(test,900,900,path),__LINE__);
            auto stats=etch::frame_stats(test);check(stats.draw_call_count<=3 && stats.idle_sleeping,__LINE__);
            etch::set_virtual_list_scroll_offset(list,0);tall_first=true;step();step();
            etch::virtual_list_scroll_to_row(list,1,0);
            check(std::fabs(etch::virtual_list_scroll_offset(list)-64)<.01,__LINE__);
            tall_first=false;etch::virtual_list_invalidate_heights(list);etch::virtual_list_scroll_to_row(list,1,0);
            check(std::fabs(etch::virtual_list_scroll_offset(list)-24)<.01,__LINE__);
            step();step();
            const uint32_t anchor_list = etch::id("v22_anchor_list");
            etch::VirtualListSlice anchor_slice = {};
            const auto anchor_step = [&]() noexcept {
                etch::begin_frame(test, {.viewport_w=900, .viewport_h=900});
                etch::begin_box({.axis=etch::Axis::Col, .width=etch::Sizing::fixed(900), .height=etch::Sizing::fixed(900)});
                for (uint32_t mode = 0; mode < 2; ++mode) {
                    etch::begin_box({.width=etch::Sizing::fixed(100), .height=etch::Sizing::fixed(100), .wrap=mode == 0, .grid_columns=static_cast<uint16_t>(mode == 1 ? 1 : 0)});
                    etch::box({.id=etch::id_index(etch::id("v22_percent"), mode), .width=etch::Sizing::fixed(50), .height=etch::Sizing::percent(0.5f)});
                    etch::end_box();
                }
                anchor_slice = etch::begin_virtual_list({.id=anchor_list, .item_count=2000, .item_height_px=28, .overscan_rows=2,
                    .width=etch::Sizing::fixed(400), .height=etch::Sizing::fixed(300),
                    .measure_height_fn=[](uint32_t, void*) noexcept -> float { return 100.0f; }, .variable_height=true});
                for (uint32_t i=anchor_slice.first_index; i<anchor_slice.end_index; ++i)
                    etch::box({.id=etch::id_index(anchor_list, i+1), .width=etch::Sizing::grow(), .height=etch::Sizing::fixed(100)});
                etch::end_virtual_list();
                etch::end_box();
                etch::end_frame(test);
            };
            anchor_step();
            for (uint32_t mode = 0; mode < 2; ++mode)
                check(std::fabs(etch::element_bounds(etch::id_index(etch::id("v22_percent"), mode)).h - 50.0f) < .2f, __LINE__);
            etch::virtual_list_scroll_to_row(anchor_list, 512, 0);
            for (uint32_t frame = 0; frame < 2; ++frame) {
                anchor_step();
                check(anchor_slice.first_index <= 512 && anchor_slice.end_index > 512, __LINE__);
                check(std::fabs(etch::element_bounds(etch::id_index(anchor_list, 513)).y - etch::element_bounds(anchor_list).y) < .2f, __LINE__);
            }
            std::remove(path);etch::destroy_context(test);etch::set_current_context(ctx);
        }
    }
    // [20] Desktop primitives and interactive five-zone docking.
    const bool v23_ok = verify_desktop_checks(ctx);
    const bool v24_ok = verify_editor_checks(ctx);
    const bool v30_ok = verify_a11y_checks(ctx);
    const bool ok = v30_ok && v24_ok && v23_ok && v22_ok && v21_ok && v20_ok && v16_ok && v15_ok && png_ok && stats_ok && slug_ok && v10a_ok && v11_ok && v12_ok && v13_ok && v14_ok;
    const char* driver = SDL_GetGPUDeviceDriver(etch::gpu_device(ctx));
    std::printf(
        "Headless %s (%s) -> %s (asserts=22/22, prims=%u, draw_calls=%u, slug_glyphs=%u, slug_curves=%u, layout_ms=%.3f, gpu_submit_ms=%.3f, arena_used=%zuB, scroll_y=%.1f, focus=0x%08X, idle=%d)\n",
        ok ? "OK" : "FAILED",
        driver ? driver : "unknown",
        out_png_path,
        final_stats.prim_count,
        final_stats.draw_call_count,
        final_stats.slug_glyph_count,
        final_stats.slug_curve_count,
        final_stats.layout_ms,
        final_stats.gpu_submit_ms,
        final_stats.arena_used_bytes,
        final_scroll.y,
        etch::focused_id(),
        final_stats.idle_sleeping ? 1 : 0);

    cleanup_release();
    return ok ? 0 : 1;
}
