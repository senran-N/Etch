// Targeted headless & unit regression checks for icon layering/strokes,
// headless collapse, widget craft upgrades, and motion/DPI/theme utilities.
#pragma once

static bool verify_craft_upgrades() noexcept {
    using namespace etch;
    Context* ctx = create_context({.headless = true});
    if (!ctx) return false;
    set_current_context(ctx);
    bool ok = true;
    const auto check = [&](bool cond, int line) noexcept {
        if (!cond) {
            ok = false;
            std::fprintf(stderr, "craft_upgrades check failed at line %d\n", line);
        }
    };

    // ------------------------------------------------------------------
    // 1. Icon Rendering, Duotone/Layered Icons & SVG Stroke Bug Fixes
    // ------------------------------------------------------------------
    {
        // Zero-length closed dot M x y Z per SVG 2 spec.
        const uint16_t dot_round = register_svg_stroke_icon(
            ctx, str_lit("M12 12Z"), {.stroke_width = 4.0f, .cap = StrokeCap::Round});
        const uint16_t dot_square = register_svg_stroke_icon(
            ctx, str_lit("M12 12Z"), {.stroke_width = 4.0f, .cap = StrokeCap::Square});
        const uint16_t dot_butt = register_svg_stroke_icon(
            ctx, str_lit("M12 12Z"), {.stroke_width = 4.0f, .cap = StrokeCap::Butt});
        check(dot_round != kInvalidIcon && dot_square != kInvalidIcon && dot_butt == kInvalidIcon, __LINE__);

        // 180-degree diagonal hairpin round join (denom <= 1e-5 with non-zero float turn).
        const uint16_t hairpin = register_svg_stroke_icon(
            ctx, str_lit("M4 4 L18 18 L4 4"),
            {.stroke_width = 3.0f, .cap = StrokeCap::Round, .join = StrokeJoin::Round});
        check(hairpin != kInvalidIcon, __LINE__);

        // Smooth flattened curve budget check (circle arc shouldn't explode with micro-arcs).
        begin_frame(ctx, {.viewport_w = 128, .viewport_h = 128});
        end_frame(ctx);
        const uint32_t curves_before_miter = frame_stats(ctx).slug_curve_count;
        const uint16_t circle_miter = register_svg_stroke_icon(
            ctx, str_lit("M12 2 A10 10 0 1 1 11.99 2 Z"),
            {.stroke_width = 2.0f, .cap = StrokeCap::Round, .join = StrokeJoin::Miter});
        begin_frame(ctx, {.viewport_w = 128, .viewport_h = 128});
        end_frame(ctx);
        const uint32_t curves_after_miter = frame_stats(ctx).slug_curve_count;
        const uint16_t smooth_circle = register_svg_stroke_icon(
            ctx, str_lit("M12 2 A10 10 0 1 1 11.99 2 Z"),
            {.stroke_width = 2.0f, .cap = StrokeCap::Round, .join = StrokeJoin::Round});
        check(circle_miter != kInvalidIcon && smooth_circle != kInvalidIcon, __LINE__);
        begin_frame(ctx, {.viewport_w = 128, .viewport_h = 128});
        end_frame(ctx);
        const uint32_t miter_curves = curves_after_miter - curves_before_miter;
        const uint32_t round_curves = frame_stats(ctx).slug_curve_count - curves_after_miter;
        check(miter_curves > 0u && round_curves > 0u && round_curves <= miter_curves + 8u, __LINE__);

        // Duotone icon registration + invalid input rejection.
        const uint16_t duo = register_svg_duotone_icon(
            ctx,
            str_lit("M4 4 H20 V20 H4 Z"),
            str_lit("M4 4 H20 V20 H4 Z"),
            {.stroke_width = 2.0f, .cap = StrokeCap::Round, .join = StrokeJoin::Round},
            0.25f);
        check(duo != kInvalidIcon && is_icon_loaded(ctx, duo), __LINE__);
        check(register_svg_duotone_icon(ctx, {}, {}) == kInvalidIcon, __LINE__);
        check(register_svg_duotone_icon(ctx, str_lit("M0 0 H10 V10 Z"), {}, {}, -0.5f) == kInvalidIcon, __LINE__);

        // Layered icon (3 layers) + rollback on invalid layer + >4 rejection.
        const SvgIconLayer valid_layers[3] = {
            {.path_d = str_lit("M2 2 H22 V22 H2 Z"), .stroked = false, .color_slot = 1u, .opacity = 0.30f},
            {.path_d = str_lit("M6 6 H18 V18 H6 Z"), .stroked = true, .stroke = {.stroke_width = 2.0f}, .color_slot = 0u, .opacity = 1.0f},
            {.path_d = str_lit("M12 12 Z"), .stroked = true, .stroke = {.stroke_width = 3.0f, .cap = StrokeCap::Round}, .color_slot = 1u, .opacity = 0.80f},
        };
        const uint16_t layered = register_svg_layered_icon(ctx, valid_layers);
        check(layered != kInvalidIcon && is_icon_loaded(ctx, layered), __LINE__);

        begin_frame(ctx, {.viewport_w = 128, .viewport_h = 128});
        end_frame(ctx);
        const uint32_t curves_pre_fail = frame_stats(ctx).slug_curve_count;
        const SvgIconLayer bad_layers[2] = {
            {.path_d = str_lit("M2 2 H22 V22 H2 Z"), .stroked = false, .color_slot = 0u, .opacity = 1.0f},
            {.path_d = str_lit("M2 2 L6 6 L"), .stroked = true, .stroke = {.stroke_width = 2.0f}, .color_slot = 0u, .opacity = 1.0f},
        };
        check(register_svg_layered_icon(ctx, bad_layers) == kInvalidIcon, __LINE__);
        begin_frame(ctx, {.viewport_w = 128, .viewport_h = 128});
        end_frame(ctx);
        check(frame_stats(ctx).slug_curve_count == curves_pre_fail, __LINE__);

        const SvgIconLayer five_layers[5] = {
            {.path_d = str_lit("M0 0 H10 V10 Z")},
            {.path_d = str_lit("M0 0 H10 V10 Z")},
            {.path_d = str_lit("M0 0 H10 V10 Z")},
            {.path_d = str_lit("M0 0 H10 V10 Z")},
            {.path_d = str_lit("M0 0 H10 V10 Z")},
        };
        check(register_svg_layered_icon(ctx, five_layers) == kInvalidIcon, __LINE__);

        // Verify IconDesc rotation_rad, scale (without changing layout bounds), opacity,
        // secondary_color, PrimFlag_StemDarken (<28px), and layer pixel-snap skip.
        const Color pri = Color::rgba8(200, 100, 50, 255);
        const Color sec = Color::rgba8(40, 180, 240, 255);
        const float rot = 0.78539816f;
        const uint32_t id_small_duo = id("u_small_duo");
        const uint32_t id_large_scaled = id("u_large_scaled");
        const uint32_t id_zero_op = id("u_zero_op");
        begin_frame(ctx, {.viewport_w = 300, .viewport_h = 200, .dpi_scale = 1.0f});
        begin_box({.axis = Axis::Row, .width = Sizing::fixed(300), .height = Sizing::fixed(200), .gap = 12});
        // Small 16px duotone icon with explicit secondary_color, rotation, and 0.5 opacity
        icon({.id = id_small_duo, .icon_id = duo, .size_px = 16.0f, .color = pri,
              .rotation_rad = rot, .scale = 1.0f, .opacity = 0.5f, .secondary_color = sec});
        // Large 36px icon scaled 1.5x visually without changing layout size (36x36)
        icon({.id = id_large_scaled, .icon_id = dot_round, .size_px = 36.0f, .color = pri, .scale = 1.5f});
        // Zero opacity icon emits zero SlugGlyph prims
        icon({.id = id_zero_op, .icon_id = dot_round, .size_px = 20.0f, .color = pri, .opacity = 0.0f});
        // Subpixel translated layer must skip icon pixel snapping
        push_layer({.translate = {0.35f, 0.25f}});
        icon({.icon_id = dot_round, .size_px = 20.0f, .color = sec});
        pop_layer();
        end_box();
        end_frame(ctx);

        const Rect b_small = element_bounds(id_small_duo);
        const Rect b_large = element_bounds(id_large_scaled);
        check(std::fabs(b_small.w - 16.0f) < 0.05f && std::fabs(b_small.h - 16.0f) < 0.05f, __LINE__);
        check(std::fabs(b_large.w - 36.0f) < 0.05f && std::fabs(b_large.h - 36.0f) < 0.05f, __LINE__);

        uint32_t small_layers_seen = 0u;
        bool saw_small_sec = false, saw_small_pri = false, saw_large_scaled = false, saw_subpixel = false;
        const uint32_t expected_rot_bits = std::bit_cast<uint32_t>(rot);
        for (const Prim& p : frame_prims(ctx)) {
            if (static_cast<PrimKind>(p.style.x & 15u) != PrimKind::SlugGlyph) continue;
            const uint8_t flags = static_cast<uint8_t>((p.style.x >> 4) & 0xFFu);
            if (p.payload.z == expected_rot_bits) {
                ++small_layers_seen;
                check((flags & PrimFlag_StemDarken) != 0u, __LINE__);
                const uint8_t a = static_cast<uint8_t>((p.style.y >> 24) & 0xFFu);
                const uint8_t r = static_cast<uint8_t>(p.style.y & 0xFFu);
                if (r == sec.r && std::abs(int(a) - 32) <= 2) saw_small_sec = true; // 255 * 0.25 * 0.5 = 31.875
                if (r == pri.r && std::abs(int(a) - 128) <= 2) saw_small_pri = true; // 255 * 1.0 * 0.5 = 127.5
            } else if (std::fabs(p.bounds.z - 54.0f) < 0.5f && std::fabs(p.bounds.w - 54.0f) < 0.5f) {
                saw_large_scaled = true;
                check((flags & PrimFlag_StemDarken) == 0u, __LINE__); // 54px >= 28px -> no stem darken
            } else if (std::fabs(p.bounds.z - 20.0f) < 0.5f) {
                const float frac_x = std::fabs(p.bounds.x - std::round(p.bounds.x));
                const float frac_y = std::fabs(p.bounds.y - std::round(p.bounds.y));
                if (frac_x > 0.1f && frac_y > 0.1f) saw_subpixel = true;
            }
        }
        check(small_layers_seen == 2u && saw_small_sec && saw_small_pri && saw_large_scaled && saw_subpixel, __LINE__);

        // Verify headless GPU rasterization of M x y Z dot, 180-deg hairpin, and duotone icon.
        begin_frame(ctx, {.viewport_w = 180, .viewport_h = 60, .dpi_scale = 1.0f});
        begin_box({.axis = Axis::Row, .width = Sizing::fixed(180), .height = Sizing::fixed(60),
                   .padding = Padding::all(6), .gap = 12, .background = Color::white()});
        icon({.icon_id = dot_round, .size_px = 48.0f, .color = Color::rgb_hex(0xE02020)});
        icon({.icon_id = hairpin, .size_px = 48.0f, .color = Color::rgb_hex(0x2020E0)});
        icon({.icon_id = duo, .size_px = 48.0f, .color = Color::rgb_hex(0x10A040)});
        end_box();
        end_frame(ctx);
        const char* png_path = "etch_craft_icons_check.png";
        const bool png_ok = render_headless_png(ctx, 180, 60, png_path, Color::white());
        SDL_Surface* surf = png_ok ? SDL_LoadPNG(png_path) : nullptr;
        check(surf != nullptr, __LINE__);
        if (surf) {
            auto read_px = [&](int x, int y, Uint8& r, Uint8& g, Uint8& b) noexcept {
                Uint8 a = 0;
                return SDL_ReadSurfacePixel(surf, x, y, &r, &g, &b, &a);
            };
            Uint8 r = 255, g = 255, b = 255;
            // Center of dot_round (6 + 24 = 30, 6 + 24 = 30) should be red.
            check(read_px(30, 30, r, g, b) && r > 180 && g < 80 && b < 80, __LINE__);
            // Hairpin apex near (6 + 48 + 12 + 36 = 102, 6 + 36 = 42) should be blue.
            check(read_px(102, 42, r, g, b) && b > 180 && r < 80, __LINE__);
            // Interior of duotone icon (6 + 48 + 12 + 48 + 12 + 24 = 150, 30) should be tinted green (~25% linear over white -> ~225,236,226).
            check(read_px(150, 30, r, g, b) && g > r + 6 && g > b + 6 && r < 238, __LINE__);
            SDL_DestroySurface(surf);
        }
        std::remove(png_path);
    }

    // ------------------------------------------------------------------
    // 2. Headless Smooth Collapse Container (begin_collapse / ETCH_COLLAPSE)
    // ------------------------------------------------------------------
    {
        const uint32_t col_id = id("u_collapse");
        bool is_open = false;
        const auto run_collapse = [&]() noexcept -> bool {
            begin_frame(ctx, {.viewport_w = 320, .viewport_h = 240, .dt_seconds = 1.0f / 60.0f});
            bool visible = false;
            ETCH_COLLAPSE({.id = col_id, .open = is_open, .spring = SpringSpec::snappy(), .fade_content = true}) {
                visible = true;
                box({.width = Sizing::grow(), .height = Sizing::fixed(80.0f), .background = Color::rgb_hex(0x336699)});
            }
            end_frame(ctx);
            return visible;
        };
        check(!run_collapse() && frame_stats(ctx).idle_sleeping, __LINE__);
        is_open = true;
        bool saw_mid = false;
        for (int i = 0; i < 60; ++i) {
            const bool vis = run_collapse();
            check(vis, __LINE__);
            const float h = element_bounds(col_id).h;
            if (h > 5.0f && h < 75.0f) saw_mid = true;
        }
        check(saw_mid && std::fabs(element_bounds(col_id).h - 80.0f) < 0.5f && frame_stats(ctx).idle_sleeping, __LINE__);
        is_open = false;
        bool saw_closing = false;
        for (int i = 0; i < 60; ++i) {
            (void)run_collapse();
            const float h = element_bounds(col_id).h;
            if (h > 5.0f && h < 75.0f) saw_closing = true;
        }
        check(saw_closing && !run_collapse() && frame_stats(ctx).idle_sleeping, __LINE__);
    }

    // ------------------------------------------------------------------
    // 3. Native Widget Craft Upgrades
    // ------------------------------------------------------------------
    // 3a. SliderStyle::Track & SliderStyle::CompactBar
    {
        const uint32_t s_track = id("u_slider_track");
        const uint32_t s_bar = id("u_slider_bar");
        const uint32_t s_minimal = id("u_slider_min");
        float v_track = 0.25f, v_bar = 0.5f, v_min = 0.5f;
        const auto step_sliders = [&](Vec2 pt = {-1, -1}, bool down = false) noexcept {
            begin_frame(ctx, {.viewport_w = 400, .viewport_h = 300, .dt_seconds = 1.0f / 60.0f,
                              .pointer_pos = pt, .pointer_down = down});
            begin_box({.axis = Axis::Col, .width = Sizing::fixed(300), .height = Sizing::fit(), .gap = 8});
            slider({.id = s_track, .label = str_lit("Gain"), .value = &v_track, .step = 0.05f, .style = SliderStyle::Track});
            slider({.id = s_bar, .label = str_lit("Mix"), .value = &v_bar, .style = SliderStyle::CompactBar});
            slider({.id = s_minimal, .value = &v_min, .height_px = 20.0f, .style = SliderStyle::Track, .show_value = false});
            end_box();
            end_frame(ctx);
        };
        step_sliders();
        step_sliders();
        const Rect rt = element_bounds(s_track);
        const Rect rm = element_bounds(s_minimal);
        check(rt.w == 300.0f && rt.h >= 34.0f && rm.h < rt.h, __LINE__);
        bool saw_thumb_shadow = false;
        for (const Prim& p : frame_prims(ctx)) {
            if (static_cast<PrimKind>(p.style.x & 15u) == PrimKind::Shadow) saw_thumb_shadow = true;
        }
        check(saw_thumb_shadow, __LINE__);
        // Drag Track slider to 75%
        step_sliders({rt.x + rt.w * 0.5f, rt.y + rt.h * 0.5f}, true);
        step_sliders({rt.x + rt.w * 0.75f, rt.y + rt.h * 0.5f}, true);
        step_sliders({rt.x + rt.w * 0.75f, rt.y + rt.h * 0.5f}, false);
        check(std::fabs(v_track - 0.75f) < 0.01f, __LINE__);
        // Drag CompactBar slider to 20%
        const Rect rb = element_bounds(s_bar);
        step_sliders({rb.x + rb.w * 0.2f, rb.y + rb.h * 0.5f}, true);
        step_sliders({rb.x + rb.w * 0.2f, rb.y + rb.h * 0.5f}, false);
        check(std::fabs(v_bar - 0.2f) < 0.02f, __LINE__);
        for (int i = 0; i < 60; ++i) step_sliders();
        check(frame_stats(ctx).idle_sleeping, __LINE__);
    }

    // 3b. toggle_switch: empty label omits +8px height, thumb shadow, disabled dims
    {
        const uint32_t t_bare = id("u_toggle_bare");
        const uint32_t t_labeled = id("u_toggle_lbl");
        const uint32_t t_disabled = id("u_toggle_dis");
        bool b1 = false, b2 = true, b3 = false;
        begin_frame(ctx, {.viewport_w = 300, .viewport_h = 200, .dt_seconds = 1.0f / 60.0f});
        begin_box({.axis = Axis::Col, .gap = 8});
        toggle_switch({.id = t_bare, .value = &b1, .track_width_px = 38.0f, .track_height_px = 20.0f});
        toggle_switch({.id = t_labeled, .label = str_lit("Dark Mode"), .value = &b2, .track_width_px = 38.0f, .track_height_px = 20.0f});
        toggle_switch({.id = t_disabled, .value = &b3, .disabled = true, .track_width_px = 38.0f, .track_height_px = 20.0f});
        end_box();
        end_frame(ctx);
        check(std::fabs(element_bounds(t_bare).h - 20.0f) < 0.05f, __LINE__);
        check(std::fabs(element_bounds(t_labeled).h - 28.0f) < 0.05f, __LINE__);
        uint32_t shadow_count = 0u;
        for (const Prim& p : frame_prims(ctx)) {
            if (static_cast<PrimKind>(p.style.x & 15u) == PrimKind::Shadow) ++shadow_count;
        }
        check(shadow_count >= 3u, __LINE__);
    }

    // 3c. combo_box + begin_popup flip_on_overflow & shadow
    {
        const uint32_t cb_id = id("u_combo_bottom");
        const uint32_t cb_pop = id_index(cb_id, 0x2304u);
        int sel = 0;
        const Str opts[] = {
            str_lit("A very long option title that truncates with ellipsis"),
            str_lit("Second Option"),
            str_lit("Third Option"),
            str_lit("Fourth Option"),
        };
        const auto step_combo = [&](Vec2 pt = {-1, -1}, bool down = false) noexcept {
            begin_frame(ctx, {.viewport_w = 400, .viewport_h = 200, .dt_seconds = 1.0f / 60.0f,
                              .pointer_pos = pt, .pointer_down = down});
            begin_box({.axis = Axis::Col, .width = Sizing::fixed(400), .height = Sizing::fixed(200),
                       .padding = Padding{.left = 20.0f, .right = 20.0f, .top = 155.0f, .bottom = 0.0f}});
            combo_box({.id = cb_id, .selected_index = &sel, .items = opts, .width = Sizing::fixed(160.0f), .height_px = 28.0f});
            end_box();
            end_frame(ctx);
        };
        step_combo();
        const Rect cr = element_bounds(cb_id);
        step_combo({cr.x + cr.w * 0.5f, cr.y + cr.h * 0.5f}, true);
        step_combo({cr.x + cr.w * 0.5f, cr.y + cr.h * 0.5f}, false);
        step_combo();
        check(is_popup_open(cb_pop), __LINE__);
        const Rect pr = element_bounds(cb_pop);
        // Trigger is at y=155 in a 200px viewport -> popup (~124px tall) must flip upward above trigger!
        check(pr.h > 40.0f && pr.y + pr.h <= cr.y + 0.5f, __LINE__);
        bool saw_pop_shadow = false;
        for (const Prim& p : frame_prims(ctx)) {
            if (static_cast<PrimKind>(p.style.x & 15u) == PrimKind::Shadow) saw_pop_shadow = true;
        }
        check(saw_pop_shadow, __LINE__);
        close_popup(cb_pop);
        for (int i = 0; i < 60; ++i) step_combo();
        check(frame_stats(ctx).idle_sleeping, __LINE__);
    }

    // 3d. begin_split: FirstFixedPx, SecondFixedPx, divider_size_px == 0, hover_highlight_px
    {
        const uint32_t sp_sec = id("u_split_sec");
        const uint32_t sp_sec_div = id_index(sp_sec, 0x53504C54u);
        const uint32_t p_left = id("u_sp_left");
        const uint32_t p_right = id("u_sp_right");
        float sidebar_w = 220.0f;
        const auto run_split_sec = [&](float vw, Vec2 pt = {-1, -1}, bool down = false) noexcept {
            begin_frame(ctx, {.viewport_w = static_cast<uint32_t>(vw), .viewport_h = 300, .dt_seconds = 1.0f / 60.0f,
                              .pointer_pos = pt, .pointer_down = down});
            begin_split({.id = sp_sec, .axis = Axis::Row, .width = Sizing::fixed(vw), .height = Sizing::fixed(300),
                         .min_first_px = 120.0f, .min_second_px = 140.0f, .divider_size_px = 0.0f,
                         .divider_hit_px = 8.0f, .sizing = SplitSizing::SecondFixedPx,
                         .extent_px = &sidebar_w, .default_extent_px = 220.0f,
                         .show_grip = false, .hover_highlight_px = 2.0f});
            box({.id = p_left, .width = Sizing::grow(), .height = Sizing::grow()});
            split_second();
            box({.id = p_right, .width = Sizing::grow(), .height = Sizing::grow()});
            end_split();
            end_frame(ctx);
        };
        run_split_sec(700.0f);
        check(std::fabs(element_bounds(p_right).w - 220.0f) < 0.2f &&
              std::fabs(element_bounds(p_left).w - 480.0f) < 0.2f, __LINE__);
        // Resize viewport from 700 -> 1000: second pane MUST remain fixed at 220px!
        run_split_sec(1000.0f);
        check(std::fabs(element_bounds(p_right).w - 220.0f) < 0.2f &&
              std::fabs(element_bounds(p_left).w - 780.0f) < 0.2f, __LINE__);
        // Drag divider 50px left -> second pane grows to 270px!
        const Rect dr = element_bounds(sp_sec_div);
        check(dr.w == 8.0f, __LINE__);
        const float cx = dr.x + dr.w * 0.5f;
        run_split_sec(1000.0f, {cx, 150.0f}, true);
        run_split_sec(1000.0f, {cx - 50.0f, 150.0f}, true);
        run_split_sec(1000.0f, {cx - 50.0f, 150.0f}, false);
        check(std::fabs(sidebar_w - 270.0f) < 1.5f && std::fabs(element_bounds(p_right).w - 270.0f) < 1.5f, __LINE__);

        // FirstFixedPx keeps first pane fixed across viewport resize
        const uint32_t sp_fst = id("u_split_fst");
        const uint32_t p_f1 = id("u_sp_f1");
        const uint32_t p_f2 = id("u_sp_f2");
        float nav_w = 180.0f;
        for (float vw : {600.0f, 920.0f}) {
            begin_frame(ctx, {.viewport_w = static_cast<uint32_t>(vw), .viewport_h = 240, .dt_seconds = 1.0f / 60.0f});
            begin_split({.id = sp_fst, .axis = Axis::Row, .width = Sizing::fixed(vw), .height = Sizing::fixed(240),
                         .min_first_px = 100.0f, .min_second_px = 100.0f, .divider_size_px = 1.0f,
                         .sizing = SplitSizing::FirstFixedPx, .extent_px = &nav_w, .default_extent_px = 180.0f});
            box({.id = p_f1, .width = Sizing::grow(), .height = Sizing::grow()});
            split_second();
            box({.id = p_f2, .width = Sizing::grow(), .height = Sizing::grow()});
            end_split();
            end_frame(ctx);
            check(std::fabs(element_bounds(p_f1).w - 180.0f) < 0.2f &&
                  std::fabs(element_bounds(p_f2).w - (vw - 181.0f)) < 0.2f, __LINE__);
        }
    }

    // 3e. text_input: focus_glow_blur=0, leading_icon_id, disabled, continuous_corners
    {
        const uint16_t search_ic = register_svg_stroke_icon(ctx, str_lit("M11 3a8 8 0 1 0 0 16a8 8 0 1 0 0-16Z M21 21l-4.3-4.3"));
        const uint32_t ti_noglow = id("u_ti_noglow");
        const uint32_t ti_dis = id("u_ti_dis");
        char buf1[32] = "query";
        char buf2[32] = "locked";
        begin_frame(ctx, {.viewport_w = 320, .viewport_h = 160});
        text_input({.id = ti_noglow, .buffer = buf1, .capacity = sizeof(buf1), .width = Sizing::fixed(240),
                    .focus_glow_blur = 0.0f, .leading_icon_id = search_ic, .continuous_corners = true});
        end_frame(ctx);
        request_focus(ti_noglow);
        begin_frame(ctx, {.viewport_w = 320, .viewport_h = 160});
        const TextInputResult r1 = text_input({.id = ti_noglow, .buffer = buf1, .capacity = sizeof(buf1), .width = Sizing::fixed(240),
                                               .focus_glow_blur = 0.0f, .leading_icon_id = search_ic, .continuous_corners = true});
        end_frame(ctx);
        check(r1.focused, __LINE__);
        uint32_t shadow_prims = 0u;
        bool saw_icon_glyph = false, saw_continuous = false;
        for (const Prim& p : frame_prims(ctx)) {
            const PrimKind k = static_cast<PrimKind>(p.style.x & 15u);
            const uint8_t flags = static_cast<uint8_t>((p.style.x >> 4) & 0xFFu);
            if (k == PrimKind::Shadow) ++shadow_prims;
            if (k == PrimKind::SlugGlyph && p.bounds.x < 30.0f) saw_icon_glyph = true;
            if (k == PrimKind::Rect && (flags & PrimFlag_Continuous) != 0u) saw_continuous = true;
        }
        check(shadow_prims == 0u && saw_icon_glyph && saw_continuous, __LINE__);

        // Disabled text_input must not accept focus or edits and must expose A11yState_Disabled.
        request_focus(ti_dis);
        SDL_Event txt_ev = {};
        txt_ev.type = SDL_EVENT_TEXT_INPUT;
        txt_ev.text.text = "X";
        process_event(ctx, txt_ev);
        begin_frame(ctx, {.viewport_w = 320, .viewport_h = 160});
        const TextInputResult r2 = text_input({.id = ti_dis, .buffer = buf2, .capacity = sizeof(buf2),
                                               .width = Sizing::fixed(240), .clear_button = true, .disabled = true});
        end_frame(ctx);
        const A11yNode* dis_node = find_a11y_node(ti_dis, ctx);
        check(!r2.focused && !r2.changed && std::strcmp(buf2, "locked") == 0 &&
              dis_node != nullptr && (dis_node->state_flags & A11yState_Disabled) != 0u, __LINE__);
    }

    // ------------------------------------------------------------------
    // 4. Motion, Hairline & Theme Utilities
    // ------------------------------------------------------------------
    {
        begin_frame(ctx, {.viewport_w = 400, .viewport_h = 300, .dpi_scale = 2.0f});
        check(std::fabs(dpi_scale(ctx) - 2.0f) < 1e-4f && std::fabs(hairline_px(ctx) - 0.5f) < 1e-4f, __LINE__);
        end_frame(ctx);

        // Theme::lerp_oklab
        const Theme dark = Theme::dark();
        const Theme light = Theme::light();
        const Theme t0 = Theme::lerp_oklab(dark, light, 0.0f);
        const Theme t1 = Theme::lerp_oklab(dark, light, 1.0f);
        const Theme tm = Theme::lerp_oklab(dark, light, 0.5f);
        check(t0.bg_canvas.pack_rgba8() == dark.bg_canvas.pack_rgba8() &&
              t1.bg_canvas.pack_rgba8() == light.bg_canvas.pack_rgba8() &&
              tm.bg_canvas.r > dark.bg_canvas.r && tm.bg_canvas.r < light.bg_canvas.r &&
              std::fabs(tm.radius_panel_px - 0.5f * (dark.radius_panel_px + light.radius_panel_px)) < 1e-4f, __LINE__);

        // spring_color (OKLab transition + exact settle)
        const uint32_t sc_id = id("u_spring_color");
        const Color c_a = Color::rgb_hex(0xE03030);
        const Color c_b = Color::rgb_hex(0x2080F0);
        Color cur_c = {};
        begin_frame(ctx, {.viewport_w = 200, .viewport_h = 200, .dt_seconds = 1.0f / 60.0f});
        cur_c = spring_color(sc_id, c_a, SpringSpec::snappy());
        end_frame(ctx);
        check(cur_c.pack_rgba8() == c_a.pack_rgba8() && frame_stats(ctx).active_animations == 0u, __LINE__);

        bool saw_mid_color = false;
        for (int i = 0; i < 60; ++i) {
            begin_frame(ctx, {.viewport_w = 200, .viewport_h = 200, .dt_seconds = 1.0f / 60.0f});
            cur_c = spring_color(sc_id, c_b, SpringSpec::snappy());
            end_frame(ctx);
            if (cur_c.r < 200 && cur_c.r > 50 && cur_c.b > 60 && cur_c.b < 220) saw_mid_color = true;
        }
        check(saw_mid_color && cur_c.pack_rgba8() == c_b.pack_rgba8() && frame_stats(ctx).active_animations == 0u, __LINE__);

        // spring_angle (shortest arc across +/-pi: +3.0 -> -3.0 moves positive across +pi!)
        const uint32_t sa_id = id("u_spring_angle");
        float ang = 0.0f;
        begin_frame(ctx, {.viewport_w = 200, .viewport_h = 200, .dt_seconds = 1.0f / 60.0f});
        ang = spring_angle(sa_id, 3.0f, SpringSpec::snappy());
        end_frame(ctx);
        check(std::fabs(ang - 3.0f) < 1e-4f, __LINE__);

        bool moved_shortest_arc = false;
        for (int i = 0; i < 60; ++i) {
            begin_frame(ctx, {.viewport_w = 200, .viewport_h = 200, .dt_seconds = 1.0f / 60.0f});
            ang = spring_angle(sa_id, -3.0f, SpringSpec::snappy());
            end_frame(ctx);
            if (ang > 3.02f && ang < 3.40f) moved_shortest_arc = true;
            // Must never travel the long negative arc through 0!
            check(ang > 2.5f || ang < -2.5f, __LINE__);
        }
        check(moved_shortest_arc && std::fabs(ang - (-3.0f)) < 1e-3f && frame_stats(ctx).active_animations == 0u, __LINE__);
    }

    // ------------------------------------------------------------------
    // 5. Bilingual Typography: Per-slot Preferred Fallback & Optical Scale Adjust
    // ------------------------------------------------------------------
    {
        // 5a. Optical scale adjust validation, clamping, proportional metrics, hinted eviction & reset.
        check(font_scale_adjust(nullptr, kFontSans) == 1.0f, __LINE__);
        check(font_scale_adjust(ctx, kMaxFontSlots) == 1.0f, __LINE__);
        check(font_scale_adjust(ctx, 50u) == 1.0f, __LINE__);
        check(font_scale_adjust(ctx, kFontSans) == 1.0f, __LINE__);
        check(!set_font_scale_adjust(nullptr, kFontSans, 1.2f), __LINE__);
        check(!set_font_scale_adjust(ctx, kMaxFontSlots, 1.2f), __LINE__);
        check(!set_font_scale_adjust(ctx, 50u, 1.2f), __LINE__);
        check(!set_font_scale_adjust(ctx, kFontSans, std::nanf("")), __LINE__);
        check(!set_font_scale_adjust(ctx, kFontSans, HUGE_VALF), __LINE__);
        check(!set_font_scale_adjust(ctx, kFontSans, -HUGE_VALF), __LINE__);

        const FontMetrics m1 = font_metrics(kFontSans, 16.0f);
        const Vec2 t1 = measure_text(str_lit("Hello World 0123"), kFontSans, 16.0f);
        check(m1.ascent > 0.0f && m1.line_height > 0.0f && t1.x > 0.0f && t1.y > 0.0f, __LINE__);

        // Clamp to [0.5, 2.0].
        check(set_font_scale_adjust(ctx, kFontSans, 0.1f) && font_scale_adjust(ctx, kFontSans) == 0.5f, __LINE__);
        check(set_font_scale_adjust(ctx, kFontSans, 5.0f) && font_scale_adjust(ctx, kFontSans) == 2.0f, __LINE__);

        // Proportional scaling at 1.25x and 0.8x.
        check(set_font_scale_adjust(ctx, kFontSans, 1.25f) && font_scale_adjust(ctx, kFontSans) == 1.25f, __LINE__);
        const FontMetrics m125 = font_metrics(kFontSans, 16.0f);
        const Vec2 t125 = measure_text(str_lit("Hello World 0123"), kFontSans, 16.0f);
        check(std::fabs(m125.ascent - m1.ascent * 1.25f) < 1e-3f &&
              std::fabs(m125.descent - m1.descent * 1.25f) < 1e-3f &&
              std::fabs(m125.line_height - m1.line_height * 1.25f) < 1e-3f, __LINE__);
        check(std::fabs(t125.x - t1.x * 1.25f) < 0.05f &&
              std::fabs(t125.y - t1.y * 1.25f) < 0.05f, __LINE__);

        check(set_font_scale_adjust(ctx, kFontSans, 0.8f) && font_scale_adjust(ctx, kFontSans) == 0.8f, __LINE__);
        const FontMetrics m08 = font_metrics(kFontSans, 16.0f);
        const Vec2 t08 = measure_text(str_lit("Hello World 0123"), kFontSans, 16.0f);
        check(std::fabs(m08.line_height - m1.line_height * 0.8f) < 1e-3f &&
              std::fabs(t08.x - t1.x * 0.8f) < 0.05f, __LINE__);

        // Verify hinted small-text cache invalidation when scale_adjust changes.
        check(set_font_scale_adjust(ctx, kFontSans, 1.0f), __LINE__);
        float max_glyph_h_1x = 0.0f, max_glyph_h_15x = 0.0f;
        begin_frame(ctx, {.viewport_w = 240, .viewport_h = 80, .dpi_scale = 1.0f});
        begin_box({.width = Sizing::fixed(240), .height = Sizing::fixed(80)});
        draw_text({.text = str_lit("Hg"), .pos = {8.0f, 8.0f}, .font_id = kFontSans, .size = 14.0f});
        end_box();
        end_frame(ctx);
        for (const Prim& p : frame_prims(ctx)) {
            const PrimKind k = static_cast<PrimKind>(p.style.x & 15u);
            if (k == PrimKind::Glyph || k == PrimKind::SlugGlyph) {
                max_glyph_h_1x = std::fmax(max_glyph_h_1x, p.bounds.w);
            }
        }
        check(set_font_scale_adjust(ctx, kFontSans, 1.5f), __LINE__);
        begin_frame(ctx, {.viewport_w = 240, .viewport_h = 80, .dpi_scale = 1.0f});
        begin_box({.width = Sizing::fixed(240), .height = Sizing::fixed(80)});
        draw_text({.text = str_lit("Hg"), .pos = {8.0f, 8.0f}, .font_id = kFontSans, .size = 14.0f});
        end_box();
        end_frame(ctx);
        for (const Prim& p : frame_prims(ctx)) {
            const PrimKind k = static_cast<PrimKind>(p.style.x & 15u);
            if (k == PrimKind::Glyph || k == PrimKind::SlugGlyph) {
                max_glyph_h_15x = std::fmax(max_glyph_h_15x, p.bounds.w);
            }
        }
        check(max_glyph_h_1x > 4.0f && max_glyph_h_15x > max_glyph_h_1x * 1.25f, __LINE__);

        // Exact return to 1.0f.
        check(set_font_scale_adjust(ctx, kFontSans, 1.0f) && font_scale_adjust(ctx, kFontSans) == 1.0f, __LINE__);
        const FontMetrics m_back = font_metrics(kFontSans, 16.0f);
        const Vec2 t_back = measure_text(str_lit("Hello World 0123"), kFontSans, 16.0f);
        check(std::fabs(m_back.ascent - m1.ascent) < 1e-5f &&
              std::fabs(m_back.descent - m1.descent) < 1e-5f &&
              std::fabs(m_back.line_height - m1.line_height) < 1e-5f &&
              std::fabs(t_back.x - t1.x) < 1e-4f, __LINE__);

        // 5b. Per-slot preferred fallback validation, routing, multi-hop chain, cycle safety & clear.
        check(!set_font_fallback(nullptr, kFontSans, kFontBold), __LINE__);
        check(!set_font_fallback(ctx, kMaxFontSlots, kFontSans), __LINE__);
        check(!set_font_fallback(ctx, 50u, kFontSans), __LINE__);
        check(!set_font_fallback(ctx, kFontSans, kFontSans), __LINE__);
        check(!set_font_fallback(ctx, kFontSans, kMaxFontSlots), __LINE__);
        check(!set_font_fallback(ctx, kFontSans, 50u), __LINE__);
        check(set_font_fallback(ctx, kFontSans, kInvalidFont), __LINE__);

#if defined(_WIN32)
        // Load isolated Latin slots (no CJK) and two CJK slots with distinct optical scales to prove routing.
        const uint16_t slot_lat1 = load_font_file(ctx, "C:/Windows/Fonts/segoeui.ttf", 28u);
        const uint16_t slot_lat2 = load_font_file(ctx, "C:/Windows/Fonts/consola.ttf", 29u);
        const uint16_t slot_lat3 = load_font_file(ctx, "C:/Windows/Fonts/arial.ttf", 30u);
        const uint16_t slot_cjk_a = load_font_file(ctx, "C:/Windows/Fonts/msyh.ttc", 31u);
        const uint16_t slot_cjk_b = load_font_file(ctx, "C:/Windows/Fonts/msyh.ttc", 32u);
        check(slot_lat1 == 28u && slot_lat2 == 29u && slot_lat3 == 30u &&
              slot_cjk_a == 31u && slot_cjk_b == 32u, __LINE__);
        check(set_font_scale_adjust(ctx, slot_cjk_b, 1.5f), __LINE__);

        const Str cjk_sample = str_lit("\xE4\xB8\xAD\xE6\x96\x87"); // "中文"
        const float latin_a_base = measure_text(str_lit("A"), slot_lat1, 16.0f).x;
        const float cjk_global_w = measure_text(cjk_sample, slot_lat1, 16.0f).x;
        check(latin_a_base > 0.0f && cjk_global_w > 0.0f, __LINE__);

        // Direct preferred fallback overrides global fallback_order[].
        check(set_font_fallback(ctx, slot_lat1, slot_cjk_b), __LINE__);
        const float cjk_pref_w = measure_text(cjk_sample, slot_lat1, 16.0f).x;
        const float latin_a_after = measure_text(str_lit("A"), slot_lat1, 16.0f).x;
        check(std::fabs(cjk_pref_w - cjk_global_w * 1.5f) < 0.2f, __LINE__);
        check(std::fabs(latin_a_after - latin_a_base) < 1e-4f, __LINE__);

        // Multi-hop chain: slot_lat1 -> slot_lat2 (misses CJK) -> slot_cjk_b (covers CJK).
        check(set_font_fallback(ctx, slot_lat1, slot_lat2), __LINE__);
        check(set_font_fallback(ctx, slot_lat2, slot_cjk_b), __LINE__);
        const float cjk_hop2_w = measure_text(cjk_sample, slot_lat1, 16.0f).x;
        check(std::fabs(cjk_hop2_w - cjk_global_w * 1.5f) < 0.2f, __LINE__);

        // 2-node cycle (slot_lat1 <-> slot_lat2) must terminate safely and fall through to global fallback.
        check(set_font_fallback(ctx, slot_lat2, slot_lat1), __LINE__);
        const float cjk_cyc2_w = measure_text(cjk_sample, slot_lat1, 16.0f).x;
        check(std::fabs(cjk_cyc2_w - cjk_global_w) < 0.2f, __LINE__);

        // 3-node indirect cycle (slot_lat1 -> slot_lat2 -> slot_lat3 -> slot_lat2) must terminate safely.
        check(set_font_fallback(ctx, slot_lat2, slot_lat3), __LINE__);
        check(set_font_fallback(ctx, slot_lat3, slot_lat2), __LINE__);
        const float cjk_cyc3_w = measure_text(cjk_sample, slot_lat1, 16.0f).x;
        check(std::fabs(cjk_cyc3_w - cjk_global_w) < 0.2f, __LINE__);

        // Clear preferred fallback restores global fallback.
        check(set_font_fallback(ctx, slot_lat1, slot_cjk_b), __LINE__);
        check(std::fabs(measure_text(cjk_sample, slot_lat1, 16.0f).x - cjk_global_w * 1.5f) < 0.2f, __LINE__);
        check(set_font_fallback(ctx, slot_lat1, kInvalidFont), __LINE__);
        check(std::fabs(measure_text(cjk_sample, slot_lat1, 16.0f).x - cjk_global_w) < 0.2f, __LINE__);

        // Slot reload cleanly resets preferred_fallback and scale_adjust.
        check(set_font_fallback(ctx, slot_lat1, slot_cjk_b), __LINE__);
        check(set_font_scale_adjust(ctx, slot_lat1, 1.3f) && font_scale_adjust(ctx, slot_lat1) == 1.3f, __LINE__);
        check(load_font_file(ctx, "C:/Windows/Fonts/segoeui.ttf", slot_lat1) == slot_lat1, __LINE__);
        check(font_scale_adjust(ctx, slot_lat1) == 1.0f, __LINE__);
        check(std::fabs(measure_text(cjk_sample, slot_lat1, 16.0f).x - cjk_global_w) < 0.2f, __LINE__);
#endif

        // 5c. Vertical layout, clip bounds & text_input/editor alignment under scale_adjust > 1.0f.
        {
            const uint32_t id_txt_wrap = id("u_scale_txt_wrap");
            const uint32_t id_txt_frac = id("u_scale_txt_frac");
            const auto measure_text_box = [&](float scale_f, float* out_h14, float* out_h115, bool* out_desc_ok) noexcept {
                check(set_font_scale_adjust(ctx, kFontSans, scale_f), __LINE__);
                begin_frame(ctx, {.viewport_w = 320, .viewport_h = 200, .dpi_scale = 1.0f});
                begin_box({.axis = Axis::Col, .width = Sizing::fixed(320), .height = Sizing::fixed(200), .gap = 8});
                begin_box({.id = id_txt_wrap, .width = Sizing::fit(), .height = Sizing::fit(), .clip_children = true});
                text(str_lit("Hpgy_;"), {.font_size_px = 14.0f, .font_id = kFontSans, .wrap = false});
                end_box();
                begin_box({.id = id_txt_frac, .width = Sizing::fit(), .height = Sizing::fit(), .clip_children = true});
                text(str_lit("Hpgy_;"), {.font_size_px = 11.5f, .font_id = kFontSans, .wrap = false});
                end_box();
                end_box();
                end_frame(ctx);
                *out_h14 = element_bounds(id_txt_wrap).h;
                *out_h115 = element_bounds(id_txt_frac).h;
                if (out_desc_ok) {
                    bool saw_glyph = false, all_in_clip = true;
                    for (const Prim& p : frame_prims(ctx)) {
                        const PrimKind k = static_cast<PrimKind>(p.style.x & 15u);
                        if (k == PrimKind::Glyph || k == PrimKind::SlugGlyph) {
                            saw_glyph = true;
                            if (p.bounds.y < p.clip.y - 1.5f || (p.bounds.y + p.bounds.w) > (p.clip.y + p.clip.w) + 1.5f) {
                                all_in_clip = false;
                            }
                        }
                    }
                    *out_desc_ok = saw_glyph && all_in_clip;
                }
            };
            float h14_1x = 0.0f, h115_1x = 0.0f, h14_13x = 0.0f, h115_13x = 0.0f, h14_back = 0.0f, h115_back = 0.0f;
            bool desc_ok_13x = false;
            measure_text_box(1.0f, &h14_1x, &h115_1x, nullptr);
            measure_text_box(1.30f, &h14_13x, &h115_13x, &desc_ok_13x);
            const FontMetrics fm14_13x = font_metrics(kFontSans, 14.0f);
            const FontMetrics fm115_13x = font_metrics(kFontSans, 11.5f);
            check(std::fabs(h14_1x - 14.0f) < 1e-4f && std::fabs(h115_1x - 11.5f) < 1e-4f, __LINE__);
            check(h14_13x >= (fm14_13x.ascent + fm14_13x.descent) - 0.01f &&
                  std::fabs(h14_13x - h14_1x * 1.30f) < 0.05f, __LINE__);
            check(h115_13x >= (fm115_13x.ascent + fm115_13x.descent) - 0.01f &&
                  std::fabs(h115_13x - h115_1x * 1.30f) < 0.05f, __LINE__);
            check(desc_ok_13x, __LINE__);
            measure_text_box(1.0f, &h14_back, &h115_back, nullptr);
            check(std::fabs(h14_back - h14_1x) < 1e-4f && std::fabs(h115_back - h115_1x) < 1e-4f, __LINE__);

            // Single-line text_input: caret height, vertical centering, and no glyph double-scaling.
            const uint32_t ti_id = id("u_scale_ti");
            const uint32_t ti_compact_id = id("u_scale_ti_compact");
            char ti_buf[32] = "Hpgy_;";
            char ti_compact_buf[32] = "Hpgy_;";
            const Color caret_col = Color::rgb_hex(0x22EE88);
            const auto run_ti_probe = [&](float scale_f, float* out_caret_y, float* out_caret_h,
                                          float* out_ime_h, float* out_glyph_span_x, Rect* out_box) noexcept {
                check(set_font_scale_adjust(ctx, kFontSans, scale_f), __LINE__);
                request_focus(ti_id);
                begin_frame(ctx, {.viewport_w = 320, .viewport_h = 120, .dpi_scale = 1.0f});
                begin_box({.width = Sizing::fixed(320), .height = Sizing::fixed(120), .padding = Padding::all(10)});
                const TextInputResult tir = text_input({.id = ti_id, .buffer = ti_buf, .capacity = sizeof(ti_buf),
                            .width = Sizing::fixed(240), .height = Sizing::fixed(36.0f),
                            .padding = Padding::xy(12.0f, 8.0f), .font_size_px = 14.0f, .font_id = kFontSans,
                            .caret_color = caret_col});
                end_box();
                end_frame(ctx);
                *out_box = element_bounds(ti_id);
                *out_ime_h = tir.ime_cursor_rect.h;
                *out_caret_y = 0.0f;
                *out_caret_h = 0.0f;
                float gx0 = 1e9f, gx1 = -1e9f;
                const uint32_t caret_packed = caret_col.pack_rgba8();
                for (const Prim& p : frame_prims(ctx)) {
                    const PrimKind k = static_cast<PrimKind>(p.style.x & 15u);
                    if (k == PrimKind::Rect && p.style.y == caret_packed && p.bounds.z <= 3.0f) {
                        *out_caret_y = p.bounds.y;
                        *out_caret_h = p.bounds.w;
                    }
                    if (k == PrimKind::Glyph || k == PrimKind::SlugGlyph) {
                        gx0 = std::fmin(gx0, p.bounds.x);
                        gx1 = std::fmax(gx1, p.bounds.x + p.bounds.z);
                    }
                }
                *out_glyph_span_x = (gx1 > gx0) ? (gx1 - gx0) : 0.0f;
            };
            float cy_1x = 0.0f, ch_1x = 0.0f, imeh_1x = 0.0f, gx_1x = 0.0f;
            float cy_13x = 0.0f, ch_13x = 0.0f, imeh_13x = 0.0f, gx_13x = 0.0f;
            Rect box_1x = {}, box_13x = {};
            run_ti_probe(1.0f, &cy_1x, &ch_1x, &imeh_1x, &gx_1x, &box_1x);
            run_ti_probe(1.30f, &cy_13x, &ch_13x, &imeh_13x, &gx_13x, &box_13x);
            // Second frame at 1.30x verifies pre_geom IME rect once last_box_logical is populated.
            run_ti_probe(1.30f, &cy_13x, &ch_13x, &imeh_13x, &gx_13x, &box_13x);
            check(std::fabs(ch_1x - 16.0f) < 0.05f, __LINE__);
            check(std::fabs(imeh_13x - (fm14_13x.ascent + fm14_13x.descent)) < 1e-3f, __LINE__);
            check(std::fabs(ch_13x - (fm14_13x.ascent + fm14_13x.descent + 2.0f)) <= 1.0f && ch_13x >= ch_1x + 3.0f, __LINE__);
            check(std::fabs((cy_13x + ch_13x * 0.5f) - (box_13x.y + box_13x.h * 0.5f)) < 0.5f, __LINE__);
            check(gx_1x > 10.0f && gx_13x > gx_1x * 1.15f && gx_13x < gx_1x * 1.45f, __LINE__);

            // Compact single-line text_input (28px height, default 8px vertical padding) stays centered and unclipped at 1.30x.
            request_focus(ti_compact_id);
            begin_frame(ctx, {.viewport_w = 320, .viewport_h = 120, .dpi_scale = 1.0f});
            begin_box({.width = Sizing::fixed(320), .height = Sizing::fixed(120), .padding = Padding::all(10)});
            text_input({.id = ti_compact_id, .buffer = ti_compact_buf, .capacity = sizeof(ti_compact_buf),
                        .width = Sizing::fixed(240), .height = Sizing::fixed(28.0f),
                        .padding = Padding::xy(12.0f, 8.0f), .font_size_px = 14.0f, .font_id = kFontSans,
                        .caret_color = caret_col});
            end_box();
            end_frame(ctx);
            const Rect cbox = element_bounds(ti_compact_id);
            bool saw_compact_caret = false;
            const uint32_t caret_packed = caret_col.pack_rgba8();
            for (const Prim& p : frame_prims(ctx)) {
                const PrimKind k = static_cast<PrimKind>(p.style.x & 15u);
                if (k == PrimKind::Rect && p.style.y == caret_packed && p.bounds.z <= 3.0f) {
                    saw_compact_caret = true;
                    check(std::fabs((p.bounds.y + p.bounds.w * 0.5f) - (cbox.y + cbox.h * 0.5f)) < 0.5f, __LINE__);
                    check(p.bounds.y >= p.clip.y - 0.5f && (p.bounds.y + p.bounds.w) <= (p.clip.y + p.clip.w) + 0.5f, __LINE__);
                }
            }
            check(saw_compact_caret, __LINE__);

            // Multiline text_input line advance scales with line_h at 1.30x.
            const uint32_t ti_ml_id = id("u_scale_ti_ml");
            char ml_buf[32] = "AAA\nBBB";
            begin_frame(ctx, {.viewport_w = 320, .viewport_h = 160, .dpi_scale = 1.0f});
            begin_box({.width = Sizing::fixed(320), .height = Sizing::fixed(160), .padding = Padding::all(10)});
            text_input({.id = ti_ml_id, .buffer = ml_buf, .capacity = sizeof(ml_buf),
                        .width = Sizing::fixed(240), .height = Sizing::fixed(100.0f),
                        .font_size_px = 14.0f, .font_id = kFontSans, .multiline = true});
            end_box();
            end_frame(ctx);
            float row1_y = 1e9f, row2_y = -1e9f;
            for (const Prim& p : frame_prims(ctx)) {
                const PrimKind k = static_cast<PrimKind>(p.style.x & 15u);
                if (k == PrimKind::Glyph || k == PrimKind::SlugGlyph) {
                    row1_y = std::fmin(row1_y, p.bounds.y);
                    row2_y = std::fmax(row2_y, p.bounds.y);
                }
            }
            const float expected_ml_adv = std::floor((fm14_13x.ascent + fm14_13x.descent) * 1.35f + 0.5f);
            check(std::fabs((row2_y - row1_y) - expected_ml_adv) < 1.5f, __LINE__);

            // CodeEditor default line stride scales when line_height_px == 0.
            TextBuffer ed_buf = {};
            check(init_text_buffer(&ed_buf, str_lit("AAA\nBBB")), __LINE__);
            begin_frame(ctx, {.viewport_w = 320, .viewport_h = 160, .dpi_scale = 1.0f});
            begin_box({.width = Sizing::fixed(320), .height = Sizing::fixed(160)});
            code_editor({.id = id("u_scale_ed"), .buffer = &ed_buf,
                         .width = Sizing::fixed(300), .height = Sizing::fixed(120),
                         .font_size_px = 14.0f, .font_id = kFontSans, .show_line_numbers = false});
            end_box();
            end_frame(ctx);
            float ed_r1 = 1e9f, ed_r2 = -1e9f;
            for (const Prim& p : frame_prims(ctx)) {
                const PrimKind k = static_cast<PrimKind>(p.style.x & 15u);
                if (k == PrimKind::Glyph || k == PrimKind::SlugGlyph) {
                    ed_r1 = std::fmin(ed_r1, p.bounds.y);
                    ed_r2 = std::fmax(ed_r2, p.bounds.y);
                }
            }
            const float expected_ed_stride = (fm14_13x.ascent + fm14_13x.descent) * 1.5f;
            check(std::fabs((ed_r2 - ed_r1) - expected_ed_stride) < 1.5f, __LINE__);
            free_text_buffer(&ed_buf);

            check(set_font_scale_adjust(ctx, kFontSans, 1.0f), __LINE__);
        }
    }

    // ------------------------------------------------------------------
    // 6. CanvasWireDesc::axis (Vertical Axis::Col vs Horizontal Axis::Row)
    // ------------------------------------------------------------------
    {
        const uint32_t cv_id = id("u_wire_axis_canvas");
        const Color c_col = Color::rgb_hex(0x22CC88);
        const Color c_row = Color::rgb_hex(0xEE5533);
        const Color c_branch = Color::rgb_hex(0x4488FF);
        const Color c_up = Color::rgb_hex(0xCC44DD);

        // Straight vertical endpoints: Axis::Col stays razor-straight (no sideways S-kink),
        // while default Axis::Row keeps its legacy horizontal tangent handles.
        begin_frame(ctx, {.viewport_w = 400, .viewport_h = 300, .dpi_scale = 1.0f});
        begin_canvas({.id = cv_id, .width = Sizing::fixed(400), .height = Sizing::fixed(300),
                      .default_pan = {0.0f, 0.0f}, .default_zoom = 1.0f, .show_grid = false});
        canvas_wire({.from_world = {100.0f, 20.0f}, .to_world = {100.0f, 140.0f},
                     .color_start = c_col, .color_end = c_col, .thickness_world = 2.0f, .axis = Axis::Col});
        canvas_wire({.from_world = {240.0f, 20.0f}, .to_world = {240.0f, 140.0f},
                     .color_start = c_row, .color_end = c_row, .thickness_world = 2.0f});
        // Branching top-to-bottom wire: vertical exit at x=80 and vertical entry at x=200.
        canvas_wire({.from_world = {80.0f, 160.0f}, .to_world = {200.0f, 260.0f},
                     .color_start = c_branch, .color_end = c_branch, .thickness_world = 2.0f, .axis = Axis::Col});
        // Upward vertical back-edge: sign_y = -1 keeps straight vertical tangents.
        canvas_wire({.from_world = {320.0f, 260.0f}, .to_world = {320.0f, 160.0f},
                     .color_start = c_up, .color_end = c_up, .thickness_world = 2.0f, .axis = Axis::Col});
        end_canvas();
        end_frame(ctx);

        const auto unpack_pt = [](const Prim& p, uint32_t packed) noexcept -> Vec2 {
            const float u = static_cast<float>(packed & 0xFFFFu) / 65535.0f;
            const float v = static_cast<float>((packed >> 16) & 0xFFFFu) / 65535.0f;
            return {p.bounds.x + u * p.bounds.z, p.bounds.y + v * p.bounds.w};
        };

        uint32_t col_prims = 0u, row_prims = 0u, branch_prims = 0u, up_prims = 0u;
        for (const Prim& p : frame_prims(ctx)) {
            if (static_cast<PrimKind>(p.style.x & 15u) != PrimKind::BezierWire) continue;
            if (p.style.y == c_col.pack_rgba8()) {
                ++col_prims;
                // Straight vertical wire has zero horizontal bulge (bounds.z ~= 2*(hw+2) = 6px).
                check(p.bounds.z < 12.0f, __LINE__);
                const Vec2 q0 = unpack_pt(p, p.payload.x);
                const Vec2 q1 = unpack_pt(p, p.payload.y);
                const Vec2 q2 = unpack_pt(p, p.payload.z);
                check(std::fabs(q0.x - 100.0f) < 0.25f &&
                      std::fabs(q1.x - 100.0f) < 0.25f &&
                      std::fabs(q2.x - 100.0f) < 0.25f, __LINE__);
            } else if (p.style.y == c_row.pack_rgba8()) {
                ++row_prims;
                // Legacy Axis::Row emits horizontal tangents (>= 24px handle).
                check(p.bounds.z > 24.0f, __LINE__);
            } else if (p.style.y == c_branch.pack_rgba8()) {
                const Vec2 q0 = unpack_pt(p, p.payload.x);
                const Vec2 q1 = unpack_pt(p, p.payload.y);
                const Vec2 q2 = unpack_pt(p, p.payload.z);
                if (branch_prims == 0u) {
                    // First half leaves (80, 160) straight down -> q1.x == q0.x == 80, q1.y > q0.y.
                    check(std::fabs(q0.x - 80.0f) < 0.25f && std::fabs(q1.x - 80.0f) < 0.25f && q1.y > q0.y + 10.0f, __LINE__);
                } else {
                    // Second half enters (200, 260) straight down -> q1.x == q2.x == 200, q1.y < q2.y.
                    check(std::fabs(q2.x - 200.0f) < 0.25f && std::fabs(q1.x - 200.0f) < 0.25f && q1.y < q2.y - 10.0f, __LINE__);
                }
                ++branch_prims;
            } else if (p.style.y == c_up.pack_rgba8()) {
                ++up_prims;
                check(p.bounds.z < 12.0f, __LINE__);
                const Vec2 q0 = unpack_pt(p, p.payload.x);
                const Vec2 q1 = unpack_pt(p, p.payload.y);
                const Vec2 q2 = unpack_pt(p, p.payload.z);
                check(std::fabs(q0.x - 320.0f) < 0.25f && std::fabs(q1.x - 320.0f) < 0.25f &&
                      std::fabs(q2.x - 320.0f) < 0.25f && q0.y >= q1.y && q1.y >= q2.y, __LINE__);
            }
        }
        check(col_prims == 2u && row_prims == 2u && branch_prims == 2u && up_prims == 2u, __LINE__);
    }

    // 8. BoxDesc::stick_to_bottom + scroll_stuck_to_bottom / set_scroll_stuck_to_bottom
    {
        const uint32_t sb_id = id("u_stick_box");
        const auto run_stick_frame = [&](int item_count, float dt, Vec2 ptr = {-1, -1}, Vec2 scroll = {}) noexcept {
            begin_frame(ctx, {.viewport_w = 300, .viewport_h = 200, .dt_seconds = dt,
                              .pointer_pos = ptr, .scroll_delta = scroll});
            begin_box({.id = sb_id, .axis = Axis::Col, .width = Sizing::fixed(240), .height = Sizing::fixed(120),
                       .scroll_y = true, .stick_to_bottom = true});
            for (int i = 0; i < item_count; ++i) {
                box({.width = Sizing::grow(), .height = Sizing::fixed(40), .background = Color::rgb_hex(0x2A2F3A)});
            }
            end_box();
            end_frame(ctx);
        };

        // Frame 1: content=240, viewport=120 -> scroll_max=120, sticks to bottom and requests settle redraw.
        run_stick_frame(6, 0.0f);
        check(scroll_stuck_to_bottom(sb_id), __LINE__);
        check(std::fabs(scroll_max(sb_id).y - 120.0f) < 0.5f, __LINE__);
        check(std::fabs(scroll_offset(sb_id).y - 120.0f) < 0.5f, __LINE__);
        check(needs_redraw(ctx), __LINE__);

        // Settle frames reach idle without infinite redraw churn.
        for (int i = 0; i < 4; ++i) run_stick_frame(6, 0.0f);
        check(scroll_stuck_to_bottom(sb_id), __LINE__);
        check(std::fabs(scroll_offset(sb_id).y - 120.0f) < 0.5f, __LINE__);
        check(!needs_redraw(ctx), __LINE__);

        // Content growth while pinned snaps zero-lag and requests a settle redraw.
        run_stick_frame(9, 0.0f);
        check(scroll_stuck_to_bottom(sb_id), __LINE__);
        check(std::fabs(scroll_max(sb_id).y - 240.0f) < 0.5f, __LINE__);
        check(std::fabs(scroll_offset(sb_id).y - 240.0f) < 0.5f, __LINE__);
        check(needs_redraw(ctx), __LINE__);
        run_stick_frame(9, 0.0f);

        // Upward wheel immediately unpins; subsequent content growth preserves user's scroll position.
        run_stick_frame(9, 0.0f, {60.0f, 60.0f}, {0.0f, 1.0f});
        check(!scroll_stuck_to_bottom(sb_id), __LINE__);
        const float unpinned_y = scroll_offset(sb_id).y;
        check(std::fabs(unpinned_y - 192.0f) < 0.5f, __LINE__);

        run_stick_frame(12, 0.0f);
        check(!scroll_stuck_to_bottom(sb_id), __LINE__);
        check(std::fabs(scroll_max(sb_id).y - 360.0f) < 0.5f, __LINE__);
        check(std::fabs(scroll_offset(sb_id).y - unpinned_y) < 0.5f, __LINE__);

        // Scrolling back down to bottom re-pins automatically and follows further growth.
        run_stick_frame(12, 0.0f, {60.0f, 60.0f}, {0.0f, -4.0f});
        check(scroll_stuck_to_bottom(sb_id), __LINE__);
        check(std::fabs(scroll_offset(sb_id).y - 360.0f) < 0.5f, __LINE__);

        run_stick_frame(14, 0.0f);
        check(scroll_stuck_to_bottom(sb_id), __LINE__);
        check(std::fabs(scroll_offset(sb_id).y - 440.0f) < 0.5f, __LINE__);

        // set_scroll_offset away from bottom unpins; set_scroll_stuck_to_bottom(id, true) snaps and re-pins.
        set_scroll_offset(sb_id, {0.0f, 100.0f});
        check(!scroll_stuck_to_bottom(sb_id), __LINE__);
        run_stick_frame(15, 0.0f);
        check(!scroll_stuck_to_bottom(sb_id), __LINE__);
        check(std::fabs(scroll_offset(sb_id).y - 100.0f) < 0.5f, __LINE__);

        set_scroll_stuck_to_bottom(sb_id, true);
        check(scroll_stuck_to_bottom(sb_id), __LINE__);
        check(std::fabs(scroll_offset(sb_id).y - 480.0f) < 0.5f, __LINE__);
        check(needs_redraw(ctx), __LINE__);

        // Inertial wheel test: upward flick with tiny dt must NOT falsely re-attach, downward flick re-attaches.
        const uint32_t sb2_id = id("u_stick_box_inertial");
        const auto run_inertial = [&](int item_count, float dt, Vec2 ptr = {-1, -1}, Vec2 scroll = {}) noexcept {
            begin_frame(ctx, {.viewport_w = 300, .viewport_h = 200, .dt_seconds = dt,
                              .pointer_pos = ptr, .scroll_delta = scroll});
            begin_box({.id = sb2_id, .axis = Axis::Col, .width = Sizing::fixed(240), .height = Sizing::fixed(120),
                       .scroll_y = true, .stick_to_bottom = true});
            for (int i = 0; i < item_count; ++i) {
                box({.width = Sizing::grow(), .height = Sizing::fixed(40), .background = Color::rgb_hex(0x2A2F3A)});
            }
            end_box();
            end_frame(ctx);
        };
        run_inertial(10, 1.0f / 60.0f);
        run_inertial(10, 1.0f / 60.0f);
        check(scroll_stuck_to_bottom(sb2_id) && std::fabs(scroll_offset(sb2_id).y - 280.0f) < 0.5f, __LINE__);
        // Tiny dt = 1ms: moves < 1px from bottom, still within 2px tolerance, must stay unpinned!
        run_inertial(10, 0.001f, {60.0f, 60.0f}, {0.0f, 1.0f});
        check(!scroll_stuck_to_bottom(sb2_id), __LINE__);
        for (int i = 0; i < 12; ++i) run_inertial(10, 1.0f / 60.0f);
        check(!scroll_stuck_to_bottom(sb2_id) && scroll_offset(sb2_id).y < 260.0f, __LINE__);
        // Downward inertial scroll reaches bottom and auto re-pins.
        run_inertial(10, 1.0f / 60.0f, {60.0f, 60.0f}, {0.0f, -6.0f});
        for (int i = 0; i < 40; ++i) run_inertial(10, 1.0f / 60.0f);
        check(scroll_stuck_to_bottom(sb2_id) && std::fabs(scroll_offset(sb2_id).y - 280.0f) < 0.5f, __LINE__);

        // Virtual list parity for scroll_stuck_to_bottom / set_scroll_stuck_to_bottom
        const uint32_t vl_id = id("u_stick_vlist");
        begin_frame(ctx, {.viewport_w = 300, .viewport_h = 200});
        const VirtualListSlice sl = begin_virtual_list({
            .id = vl_id, .item_count = 40u, .item_height_px = 24.0f,
            .width = Sizing::fixed(240), .height = Sizing::fixed(120), .stick_to_bottom = true});
        for (uint32_t r = sl.first_index; r < sl.end_index; ++r) {
            box({.width = Sizing::grow(), .height = Sizing::fixed(24.0f)});
        }
        end_virtual_list();
        end_frame(ctx);
        check(scroll_stuck_to_bottom(vl_id), __LINE__);
        set_virtual_list_scroll_offset(vl_id, 40.0);
        check(!scroll_stuck_to_bottom(vl_id), __LINE__);
        set_scroll_stuck_to_bottom(vl_id, true);
        check(scroll_stuck_to_bottom(vl_id), __LINE__);
    }

    destroy_context(ctx);
    set_current_context(nullptr);
    return ok;
}
