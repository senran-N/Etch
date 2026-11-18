// Etch sandbox & "--headless" verification harness.
// Strictly follows Orthodox C++20 (zero exceptions, RTTI, virtual trees, or hot-path heap containers).

#include "etch/etch.h"
#include "etch_testing.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include "clay.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "checks_desktop.h"
#include "checks_editor.h"
#include "checks_a11y.h"
#include "checks_media_emoji.h"
#include "checks_theme_wrap.h"

namespace {

bool check_v15_window_ui() noexcept {
    etch::Context* previous = etch::current_context();
    etch::Context* ctx = etch::create_context({.headless = true});
    if (!ctx) return false;
    bool ok = true;
    const uint32_t target = etch::id("v15_target");
    const uint32_t plain = etch::id("v15_plain");
    const uint32_t background = etch::id("v15_background");
    const uint32_t first = etch::id("v15_modal_first");
    const uint32_t second = etch::id("v15_modal_second");
    bool blur = false;
    const uint32_t scroll_id = etch::id("v15_scroll");
    const uint32_t scope = etch::id("v15_modal");
    const auto build = [&](bool overlay, bool modal) noexcept {
        etch::begin_box({.axis = etch::Axis::Col, .width = etch::Sizing::grow(), .height = etch::Sizing::grow(), .background = etch::Color::rgb_hex(0x0E1014)});
        etch::box({.id = background, .width = etch::Sizing::fixed(120), .height = etch::Sizing::fixed(30), .focusable = true});
        etch::box({.id = target, .width = etch::Sizing::fixed(200), .height = etch::Sizing::fixed(100), .background = etch::Color::rgb_hex(0x1E293B), .backdrop_blur = blur ? 8.0f : 0.0f});
        etch::begin_box({.id = scroll_id, .width = etch::Sizing::fixed(200), .height = etch::Sizing::fixed(70), .scroll_y = true});
        etch::box({.width = etch::Sizing::fixed(180), .height = etch::Sizing::fixed(500)});
        etch::end_box();
        etch::box({.id = plain, .width = etch::Sizing::fixed(200), .height = etch::Sizing::fixed(30)});
        if (overlay) etch::box({.width = etch::Sizing::fixed(200), .height = etch::Sizing::fixed(240),
            .background = etch::Color::rgb_hex(0x334155), .attach_to = etch::AttachTo::Root, .anchor_parent = etch::Anchor::TopLeft, .z_index = 20});
        if (modal) {
            etch::begin_focus_scope(scope);
            etch::box({.id = first, .width = etch::Sizing::fixed(100), .height = etch::Sizing::fixed(30), .focusable = true});
            etch::box({.id = second, .width = etch::Sizing::fixed(100), .height = etch::Sizing::fixed(30), .focusable = true});
            etch::end_focus_scope();
        }
        etch::end_box();
    };
    const auto step = [&](bool overlay, bool modal, uint32_t key = 0, uint16_t mods = 0) noexcept {
        etch::begin_frame(ctx, {.viewport_w = 400, .viewport_h = 300, .key_mods = mods, .key_pressed_code = key});
        build(overlay, modal); etch::end_frame(ctx);
    };
    step(false, false);
    const etch::Vec2 plain_pos = {20, 210};
    etch::inject_drop_file(ctx, etch::str_lit("stale.svg"));
    etch::inject_drop_text(ctx, etch::str_lit("stale text"));
    etch::inject_os_drop_begin(ctx, plain_pos);
    etch::begin_frame(ctx, {.viewport_w = 400, .viewport_h = 300});
    ok &= etch::os_drag_hovering(plain) && etch::dropped_files().empty() && etch::dropped_texts().empty();
    build(false, false); etch::end_frame(ctx);
    const etch::Vec2 pos = {20, 60};
    etch::inject_os_drop_begin(ctx, pos);
    etch::begin_frame(ctx, {.viewport_w = 400, .viewport_h = 300});
    ok &= etch::is_os_dragging() && etch::os_drag_hovering(target);
    build(false, false); etch::end_frame(ctx);
    if (!ok) std::fprintf(stderr, "v15 live hover failed\n");
    step(true, false);
    etch::begin_frame(ctx, {.viewport_w = 400, .viewport_h = 300});
    ok &= !etch::os_drag_hovering(target);
    etch::inject_os_drop_move(ctx, plain_pos);
    ok &= !etch::os_drag_hovering(plain);
    etch::inject_os_drop_move(ctx, pos);
    build(false, false); etch::end_frame(ctx);
    if (!ok) std::fprintf(stderr, "v15 occlusion failed\n");
    // Complete arrives before building, exactly as in an SDL polling drain.
    char path[] = "C:/assets/input.svg";
    etch::inject_drop_file(ctx, etch::str_cstr(path));
    etch::inject_drop_text(ctx, etch::str_lit("External note"));
    path[0] = 'X';
    step(false, false);
    etch::begin_frame(ctx, {.viewport_w = 400, .viewport_h = 300});
    ok &= etch::os_drag_hovering(target) && etch::dropped_files().empty() && etch::dropped_texts().empty();
    build(false, false); etch::end_frame(ctx);
    etch::inject_os_drop_complete(ctx, pos);
    etch::begin_frame(ctx, {.viewport_w = 400, .viewport_h = 300});
    const auto files = etch::dropped_files_on(target);
    ok &= !etch::is_os_dragging() && files.size() == 1 && files[0].chars[0] == 'C' && etch::dropped_texts_on(target).size() == 1;
    build(false, false); etch::end_frame(ctx);
    if (!ok) std::fprintf(stderr, "v15 delivery failed\n");
    etch::begin_frame(ctx, {.viewport_w = 400, .viewport_h = 300});
    ok &= etch::dropped_files().empty() && etch::dropped_texts().empty();
    etch::request_focus(background);
    etch::begin_focus_scope(scope);
    etch::box({.id = first, .width = etch::Sizing::fixed(100), .height = etch::Sizing::fixed(30), .focusable = true});
    ok &= etch::focused(first); // synchronous Frame-0 reconciliation
    etch::end_focus_scope();
    etch::end_frame(ctx);
    if (!ok) std::fprintf(stderr, "v15 frame0 focus failed\n");
    step(false, true);
    step(false, true, SDLK_TAB);
    ok &= etch::focused(second) && etch::keyboard_focus_visible(second);
    step(false, true, SDLK_TAB);
    ok &= etch::focused(first);
    step(false, true, SDLK_TAB, etch::Mod_Shift);
    ok &= etch::focused(second);
    etch::begin_frame(ctx, {.viewport_w = 400, .viewport_h = 300, .key_mods = etch::Mod_Ctrl, .key_pressed_code = SDLK_K});
    ok &= !etch::shortcut(etch::Mod_Ctrl, SDLK_K, true);
    build(false, true); etch::end_frame(ctx);
    step(false, false);
    ok &= etch::focused(background);
    etch::begin_frame(ctx, {.viewport_w = 400, .viewport_h = 300, .pointer_pos = {10,10}, .pointer_down = true});
    ok &= !etch::keyboard_focus_visible();
    build(false, false); etch::end_frame(ctx);
    etch::inject_drop_file(ctx, etch::str_lit("direct.svg"));
    etch::begin_frame(ctx, {.viewport_w = 400, .viewport_h = 300});
    ok &= etch::dropped_files().size() == 1;
    build(false, false); etch::end_frame(ctx);
    const etch::SystemTheme theme = etch::system_theme();
    ok &= theme == etch::SystemTheme::Unknown ? etch::system_dark_mode(true) && !etch::system_dark_mode(false) :
        etch::system_dark_mode(true) == etch::system_dark_mode(false) && etch::system_dark_mode() == (theme == etch::SystemTheme::Dark);
    if (!ok) std::fprintf(stderr, "v15 focus and theme failed\n");
    // A modal text input must keep its IME preedit when pointer focus is rejected.
    const uint32_t modal_input = etch::id("v15_modal_input");
    char modal_text[32] = {};
    const auto ime_step = [&](etch::Vec2 pointer, bool down) noexcept {
        etch::begin_frame(ctx, {.viewport_w = 400, .viewport_h = 300, .pointer_pos = pointer, .pointer_down = down});
        etch::begin_box({.axis = etch::Axis::Col, .width = etch::Sizing::grow(), .height = etch::Sizing::grow()});
        etch::box({.id = background, .width = etch::Sizing::fixed(120), .height = etch::Sizing::fixed(30), .focusable = true});
        etch::begin_focus_scope(scope, true);
        const etch::TextInputResult result = etch::text_input({.id = modal_input, .buffer = modal_text,
            .capacity = sizeof(modal_text), .width = etch::Sizing::fixed(200)});
        etch::end_focus_scope();
        etch::end_box();
        etch::end_frame(ctx);
        return etch::focused(modal_input) && result.composing;
    };
    ime_step({-1, -1}, false);
    SDL_Event editing = {};
    editing.type = SDL_EVENT_TEXT_EDITING;
    editing.edit.text = "ni";
    editing.edit.start = 2;
    etch::process_event(ctx, editing);
    bool modal_ime_ok = ime_step({-1, -1}, false);
    modal_ime_ok &= ime_step({300, 250}, true); // Non-focusable space.
    modal_ime_ok &= ime_step({300, 250}, false);
    modal_ime_ok &= ime_step({10, 10}, true); // Focusable background outside the modal.
    modal_ime_ok &= ime_step({10, 10}, false);
    ok &= modal_ime_ok;
    if (!modal_ime_ok) std::fprintf(stderr, "Headless assert [15] failed: rejected modal focus dropped IME preedit\n");
    SDL_Window* a = SDL_CreateWindow("Etch Window A", 400, 300, SDL_WINDOW_HIDDEN);
    SDL_Window* b = SDL_CreateWindow("Etch Window B", 500, 350, SDL_WINDOW_HIDDEN);
    ok &= a && b;
    if (a && b) {
        for (SDL_Window* win : {a,b}) {
            etch::begin_window_frame(ctx, win, 0.016f); build(false, false); etch::end_window_frame(ctx);
        }
        SDL_Event ev = {}; ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        ev.button.windowID = SDL_GetWindowID(a); ev.button.button = SDL_BUTTON_LEFT; ev.button.x = 10; ev.button.y = 10;
        etch::process_event(ctx, ev);
        etch::begin_window_frame(ctx, a, 0.016f); build(false, false); etch::end_window_frame(ctx);
        ok &= etch::active(background);
        ev.button.windowID = SDL_GetWindowID(b); etch::process_event(ctx, ev);
        etch::begin_window_frame(ctx, b, 0.016f); build(false, false); etch::end_window_frame(ctx);
        ev.type = SDL_EVENT_MOUSE_BUTTON_UP; etch::process_event(ctx, ev);
        etch::begin_window_frame(ctx, b, 0.016f);
        ok &= etch::clicked(background); build(false, false); etch::end_window_frame(ctx);
        etch::begin_window_frame(ctx, a, 0.016f);
        ok &= etch::active(background); build(false, false); etch::end_window_frame(ctx);
        if (!ok) std::fprintf(stderr, "v15 dual input failed\n");
        ev.type = SDL_EVENT_MOUSE_MOTION; ev.motion.windowID = SDL_GetWindowID(a);
        ev.motion.x = 40; ev.motion.y = 15; etch::process_event(ctx, ev);
        etch::begin_window_frame(ctx, a, 0.016f);
        ok &= etch::dragging(background); build(false, false); etch::end_window_frame(ctx);
        ev = {}; ev.type = SDL_EVENT_MOUSE_BUTTON_UP; ev.button.button = SDL_BUTTON_LEFT;
        ev.button.x = 40; ev.button.y = 15; ev.button.windowID = SDL_GetWindowID(a); etch::process_event(ctx, ev);
        etch::begin_window_frame(ctx, a, 0.016f); build(false, false); etch::end_window_frame(ctx);
        // Momentum advances only when its owning window builds.
        SDL_Event wheel = {}; wheel.type = SDL_EVENT_MOUSE_WHEEL; wheel.wheel.windowID = SDL_GetWindowID(a);
        wheel.wheel.y = -3;
        const float input_scale = SDL_GetWindowDisplayScale(a) / SDL_GetWindowPixelDensity(a);
        wheel.wheel.mouse_x = 20 * input_scale; wheel.wheel.mouse_y = 160 * input_scale;
        etch::process_event(ctx, wheel);
        etch::begin_window_frame(ctx, a, 0.016f); build(false, false); etch::end_window_frame(ctx);
        const float scroll_a = etch::scroll_offset(scroll_id).y;
        etch::begin_window_frame(ctx, b, 0.016f); build(false, false); etch::end_window_frame(ctx);
        ok &= etch::scroll_offset(scroll_id).y == 0;
        etch::begin_window_frame(ctx, a, 0.016f); build(false, false); etch::end_window_frame(ctx);
        if (etch::scroll_offset(scroll_id).y == scroll_a) {
            const etch::Rect bounds = etch::element_bounds(scroll_id);
            std::fprintf(stderr, "v15 scroll: before=%.2f after=%.2f rect=%.1f,%.1f,%.1f,%.1f pointer=%.1f,%.1f\n", scroll_a, etch::scroll_offset(scroll_id).y, bounds.x,bounds.y,bounds.w,bounds.h,etch::pointer_pos().x,etch::pointer_pos().y);
        }
        ok &= etch::scroll_offset(scroll_id).y != scroll_a;
        if (!ok) std::fprintf(stderr, "v15 scroll inertia failed\n");
        // Alternate window layouts with independent blur state.
        blur = true;
        etch::begin_window_frame(ctx, a, 0.016f); build(false, false); etch::end_window_frame(ctx);
        etch::begin_window_frame(ctx, b, 0.016f); build(false, false); etch::end_window_frame(ctx);
        etch::begin_window_frame(ctx, a, 0.016f); build(false, false); etch::end_window_frame(ctx);
        ok &= etch::frame_stats(ctx).blur_pass_count > 0;
        if (!ok) std::fprintf(stderr, "v15 blur failed\n");
        blur = false;
        etch::set_window_chrome({.enabled = true, .resizable = true}, a);
        etch::set_window_chrome({.enabled = false}, b);
        ok &= etch::evaluate_window_hit(ctx, {1,1}, a) == etch::WindowHit::ResizeTopLeft;
        ok &= etch::evaluate_window_hit(ctx, {1,1}, b) == etch::WindowHit::Normal;
        SDL_Event drop = {}; drop.type = SDL_EVENT_DROP_BEGIN; drop.drop.windowID = SDL_GetWindowID(b);
        drop.drop.x = pos.x; drop.drop.y = pos.y; etch::process_event(ctx, drop);
        drop.type = SDL_EVENT_DROP_POSITION; etch::process_event(ctx, drop);
        char external[] = "Window B";
        drop.type = SDL_EVENT_DROP_TEXT; drop.drop.data = external; etch::process_event(ctx, drop);
        external[0] = 'X';
        etch::begin_window_frame(ctx, b, 0.016f);
        ok &= etch::os_drag_hovering(target) && etch::dropped_texts().empty();
        build(false, false); etch::end_window_frame(ctx);
        drop.type = SDL_EVENT_DROP_COMPLETE; drop.drop.data = nullptr; etch::process_event(ctx, drop);
        etch::release_window(ctx, a);
        ok &= etch::claimed_window_count(ctx) == 1;
        etch::begin_window_frame(ctx, b, 0.016f);
        const auto delivered = etch::dropped_texts_on(target);
        ok &= delivered.size() == 1 && delivered[0].chars[0] == 'W';
        build(false, false); etch::end_window_frame(ctx);
        if (!ok) std::fprintf(stderr, "v15 release failed\n");
        etch::release_window(ctx, b);
        ok &= etch::claimed_window_count(ctx) == 0;
    }
    if (!a || !b) std::fprintf(stderr, "v15 hidden windows: %s\n", SDL_GetError());
    if (a && b) {
        // Keep A's Clay layout open across window claim/release and destruction of B.
        etch::Context* other = etch::create_context({.window = b, .headless = true});
        ok &= other != nullptr;
        if (other) {
            const uint32_t probe = etch::id("v15_cross_context");
            etch::begin_frame(ctx, {.viewport_w = 400, .viewport_h = 300});
            Clay_Context* clay_a = Clay_GetCurrentContext();
            etch::begin_box({.width = etch::Sizing::fixed(80), .height = etch::Sizing::fixed(60)});
            ok &= etch::render_window(other, {}, a);
            ok &= etch::current_context() == ctx && Clay_GetCurrentContext() == clay_a;
            etch::release_window(other, a);
            ok &= etch::claimed_window_count(other) == 1;
            ok &= etch::current_context() == ctx && Clay_GetCurrentContext() == clay_a;
            etch::destroy_context(other); // B still owns b, so this also releases its window.
            const bool preserved = etch::current_context() == ctx && Clay_GetCurrentContext() == clay_a;
            ok &= preserved;
            if (!preserved) etch::set_current_context(ctx); // Finish A safely on regression failure.
            etch::box({.id = probe, .width = etch::Sizing::fixed(30), .height = etch::Sizing::fixed(20),
                .background = etch::Color::rgb_hex(0x334155)});
            etch::end_box();
            etch::end_frame(ctx);
            const etch::Rect bounds = etch::element_bounds(probe);
            ok &= bounds.w == 30 && bounds.h == 20 && etch::frame_stats(ctx).prim_count > 0;
            if (!ok) std::fprintf(stderr, "v15 cross-context Clay lifetime failed\n");
        }
    }
    if (a) SDL_DestroyWindow(a);
    if (b) SDL_DestroyWindow(b);
    etch::destroy_context(ctx);
    etch::set_current_context(previous);
    if (!ok) std::fprintf(stderr, "Milestone 2 window/drop/focus/theme assertions failed\n");
    return ok;
}


inline constexpr uint32_t kOscWidth = 256u;
inline constexpr uint32_t kOscHeight = 256u;

struct AppState {
    etch::Context*        ctx = nullptr;
    SDL_Window*           window = nullptr;
    SDL_GPUTexture*       osc_tex = nullptr;
    uint16_t              ico_spark = etch::kInvalidIcon;
    uint16_t              ico_arc_bolt = etch::kInvalidIcon;
    uint16_t              ico_gear = etch::kInvalidIcon;
    uint32_t              click_count = 0u;
    uint32_t              rich_link_clicks = 0u;
    uint32_t              selected_preset = 0u;
    uint32_t              submit_count = 0u;
    bool                  toggle_on = false;
    bool                  popover_open = false;
    float                 last_toggle_t = 0.0f;
    char                  single_buf[256] = "Etch 纯血 GPU";
    char                  multi_buf[512] = "Line 1: 零拷贝 SDF 合批\nLine 2: Ctrl+C/V · 纯血 GPU 多行选区";
    etch::TextInputResult single_res = {};
    etch::TextInputResult multi_res = {};
    bool                  running = true;
    bool                  rendering = false;
};

void init_sandbox_icons(AppState* app) noexcept {
    if (!app || !app->ctx) {
        return;
    }
    app->ico_spark = etch::register_svg_icon(
        app->ctx,
        etch::str_lit("M13 2L4.5 13.5H11L9.5 22L19.5 10.5H13Z"),
        {0.0f, 0.0f, 24.0f, 24.0f});
    app->ico_arc_bolt = etch::register_svg_icon(
        app->ctx,
        etch::str_lit("M4 16A8 8 0 1 1 20 16H17A5 5 0 1 0 7 16Z M12 8L15 15L9 15Z"),
        {0.0f, 0.0f, 24.0f, 24.0f});
    app->ico_gear = etch::register_svg_icon(
        app->ctx,
        etch::str_lit("M12 2A10 10 0 1 1 2 12A10 10 0 0 1 12 2Z M12 6A6 6 0 1 0 18 12A6 6 0 0 0 12 6Z"),
        {0.0f, 0.0f, 24.0f, 24.0f});
}

constexpr float clamp_f(float x, float lo, float hi) noexcept {
    return (x < lo) ? lo : ((x > hi) ? hi : x);
}

inline uint8_t clamp_u8(float x) noexcept {
    if (x <= 0.0f) return 0u;
    if (x >= 255.0f) return 255u;
    return static_cast<uint8_t>(x + 0.5f);
}

inline float smooth_line_cov(float dist_px, float half_thickness) noexcept {
    const float edge = half_thickness + 0.90f;
    if (dist_px >= edge) return 0.0f;
    if (dist_px <= half_thickness - 0.90f) return 1.0f;
    const float t = (edge - dist_px) / 1.80f;
    return t * t * (3.0f - 2.0f * t);
}

SDL_GPUTexture* create_oscilloscope_texture(etch::Context* ctx) noexcept {
    const uint32_t byte_size = kOscWidth * kOscHeight * 4u;
    uint8_t* dst = static_cast<uint8_t*>(std::malloc(byte_size));
    if (!dst) {
        return nullptr;
    }

    constexpr float kTwoPi = 6.2831853f;
    constexpr float kOmega = kTwoPi * 2.0f;
    for (uint32_t py = 0u; py < kOscHeight; ++py) {
        const float fy = static_cast<float>(py);
        const float v = (fy + 0.5f) / static_cast<float>(kOscHeight);
        for (uint32_t px = 0u; px < kOscWidth; ++px) {
            const float fx = static_cast<float>(px);
            const float u = (fx + 0.5f) / static_cast<float>(kOscWidth);

            // Subtle blueprint radial background gradient
            const float cx = u - 0.5f;
            const float cy = v - 0.5f;
            const float glow = 1.0f - std::sqrt(cx * cx + cy * cy) * 1.15f;
            const float bg_scale = (glow > 0.0f) ? glow : 0.0f;
            float r = 10.0f + 8.0f * bg_scale;
            float g = 16.0f + 16.0f * bg_scale;
            float b = 29.0f + 26.0f * bg_scale;

            // Major & minor oscilloscope reticle grid
            const bool major_x = (px % 64u) == 0u;
            const bool major_y = (py % 64u) == 0u;
            const bool minor_x = (px % 16u) == 0u;
            const bool minor_y = (py % 16u) == 0u;
            if (major_x || major_y) {
                r += 24.0f;
                g += 38.0f;
                b += 58.0f;
            } else if (minor_x || minor_y) {
                r += 9.0f;
                g += 15.0f;
                b += 26.0f;
            }

            if ((py == kOscHeight / 2u && (px % 4u) < 2u) || (px == kOscWidth / 2u && (py % 4u) < 2u)) {
                r += 22.0f;
                g += 32.0f;
                b += 48.0f;
            }

            // Dual continuous Euclidean-distance AA traces (no beading on steep slopes)
            const float phase = u * kOmega;
            const float env1 = std::exp(-cx * cx * 2.8f);
            const float wave1 = 0.28f * std::sin(phase) + 0.09f * std::sin(phase * 3.0f + 0.6f);
            const float dwave1 = (0.28f * kOmega * std::cos(phase) + 0.27f * kOmega * std::cos(phase * 3.0f + 0.6f)) * env1
                               - wave1 * env1 * (5.6f * cx);
            const float y1_px = (0.50f - wave1 * env1) * static_cast<float>(kOscHeight);
            const float slope1 = -dwave1 * (static_cast<float>(kOscHeight) / static_cast<float>(kOscWidth));
            const float dist1 = std::fabs(fy - y1_px) / std::sqrt(1.0f + slope1 * slope1);

            const float env2 = std::exp(-cx * cx * 2.2f);
            const float wave2 = 0.22f * std::cos(phase * 1.5f - 0.5f) - 0.08f * std::sin(phase * 4.0f);
            const float dwave2 = (-0.33f * kOmega * std::sin(phase * 1.5f - 0.5f) - 0.32f * kOmega * std::cos(phase * 4.0f)) * env2
                               - wave2 * env2 * (4.4f * cx);
            const float y2_px = (0.50f - wave2 * env2) * static_cast<float>(kOscHeight);
            const float slope2 = -dwave2 * (static_cast<float>(kOscHeight) / static_cast<float>(kOscWidth));
            const float dist2 = std::fabs(fy - y2_px) / std::sqrt(1.0f + slope2 * slope2);

            const float glow1 = std::exp(-dist1 * 0.24f) * 0.45f;
            const float line1 = smooth_line_cov(dist1, 1.35f);
            r = r * (1.0f - line1) + 56.0f * line1 + 24.0f * glow1;
            g = g * (1.0f - line1) + 225.0f * line1 + 195.0f * glow1;
            b = b * (1.0f - line1) + 255.0f * line1 + 235.0f * glow1;

            const float glow2 = std::exp(-dist2 * 0.24f) * 0.40f;
            const float line2 = smooth_line_cov(dist2, 1.25f);
            r = r * (1.0f - line2) + 251.0f * line2 + 235.0f * glow2;
            g = g * (1.0f - line2) + 146.0f * line2 + 120.0f * glow2;
            b = b * (1.0f - line2) + 60.0f * line2 + 50.0f * glow2;

            uint8_t* out = dst + (size_t(py) * kOscWidth + px) * 4u;
            out[0] = clamp_u8(r);
            out[1] = clamp_u8(g);
            out[2] = clamp_u8(b);
            out[3] = 255u;
        }
    }

    const etch::LoadedTexture loaded = etch::create_texture_rgba(ctx, etch::TextureDesc{
        .rgba_pixels = dst,
        .width = kOscWidth,
        .height = kOscHeight,
        .row_pitch_bytes = kOscWidth * 4u,
        .srgb = true,
        .debug_name = "oscilloscope_tex",
    });
    std::free(dst);
    return loaded.texture;
}

void build_sandbox_ui(AppState* app, const etch::FrameStats& prev_stats) noexcept {
    using namespace etch;
    if (!app) {
        return;
    }

    const Color panel_bg     = Color::rgb_hex(0x131722);
    const Color panel_border = Color::rgba8(148, 163, 184, 38);
    const Color sub_surface  = Color::rgb_hex(0x1A2130);
    const Color text_primary = Color::rgb_hex(0xF1F5F9);
    const Color text_muted   = Color::rgb_hex(0x94A3B8);
    const Color shadow_col   = Color::rgba8(0, 0, 0, 132);

    const uint32_t single_id      = id("input_single");
    const uint32_t multi_id       = id("input_multi");
    const uint32_t btn_accent_id  = id("btn_accent");
    const uint32_t btn_toggle_id  = id("btn_toggle");
    const uint32_t btn_popover_id = id("btn_popover");
    const uint32_t scroll_id      = id("scroll_list");

    const bool accent_hov   = hovered(btn_accent_id);
    const bool accent_focus = focused(btn_accent_id);
    const bool toggle_hov   = hovered(btn_toggle_id);
    const bool toggle_focus = focused(btn_toggle_id);
    const bool pop_hov      = hovered(btn_popover_id);
    const bool pop_focus    = focused(btn_popover_id);

    if (accent_hov || toggle_hov || pop_hov) {
        set_cursor(Cursor::Hand);
    }

    if (clicked(btn_accent_id)) {
        ++app->click_count;
    }
    if (clicked(btn_toggle_id)) {
        app->toggle_on = !app->toggle_on;
    }
    if (clicked(btn_popover_id)) {
        app->popover_open = !app->popover_open;
    }
    if (app->popover_open && should_close_popover(id("popover_menu"), btn_popover_id)) {
        app->popover_open = false;
    }

    const SpringSpec preset_spec = (app->selected_preset == 1u)
        ? SpringSpec{ .response = 0.28f, .damping_ratio = 0.62f }
        : ((app->selected_preset == 2u)
            ? SpringSpec{ .response = 0.14f, .damping_ratio = 1.00f }
            : SpringSpec{ .response = 0.22f, .damping_ratio = 0.72f });

    const float accent_ring_t = spring(id("sp_accent_ring"), (accent_hov || accent_focus) ? 1.0f : 0.0f, { .response = 0.14f, .damping_ratio = 1.0f });
    const float toggle_t = spring(id("sp_toggle"), app->toggle_on ? 1.0f : 0.0f, preset_spec);
    app->last_toggle_t = toggle_t;

    const Vec2 cur_scroll = scroll_offset(scroll_id);

    // Root container
    ETCH_BOX({
        .id = id("root"),
        .axis = Axis::Col,
        .width = Sizing::grow(),
        .height = Sizing::grow(),
        .padding = Padding::all(24.0f),
        .gap = 18.0f,
    }) {
        // 1. Header bar
        ETCH_BOX({
            .id = id("header"),
            .axis = Axis::Row,
            .width = Sizing::grow(),
            .height = Sizing::fit(),
            .padding = Padding::xy(18.0f, 13.0f),
            .gap = 16.0f,
            .align_y = AlignY::Center,
            .background = panel_bg,
            .border_color = panel_border,
            .border_width = 1.0f,
            .radius = Corners::all(14.0f),
            .shadow_color = shadow_col,
            .shadow_blur = 18.0f,
            .shadow_offset = {0.0f, 6.0f},
        }) {
            ETCH_BOX({
                .padding = Padding::xy(12.0f, 6.0f),
                .align_x = AlignX::Center,
                .align_y = AlignY::Center,
                .background = Color::rgb_hex(0x2563EB),
                .background_end = Color::rgb_hex(0x06B6D4),
                .gradient_dir = {1.0f, 0.2f},
                .radius = Corners::all(999.0f),
            }) {
                text("ETCH v" ETCH_VERSION_STRING, {
                    .color = Color::white(),
                    .font_size_px = 13.0f,
                    .letter_spacing = 0.4f,
                    .wrap = false,
                });
            }

            ETCH_BOX({
                .axis = Axis::Col,
                .width = Sizing::grow(),
                .gap = 3.0f,
            }) {
                text("Etch Pure-GPU Engine v" ETCH_VERSION_STRING " (Slug · HarfBuzz · Kawase)", {
                    .color = text_primary,
                    .font_size_px = 19.0f,
                    .wrap = false,
                });
                text("UTF-8 单/多行编辑 · 原生剪贴板 · 中文 IME 候选框追踪 · 阻尼弹簧 · 惯性滚动 · 0% 休眠", {
                    .color = text_muted,
                    .font_size_px = 13.0f,
                    .wrap = false,
                });
            }

            char hdr_stat[96] = {};
            const char* sleep_tag = needs_redraw(app->ctx)
                ? "ACTIVE"
                : "IDLE 0% SLEEP";
            std::snprintf(
                hdr_stat,
                sizeof(hdr_stat),
                "%u Draw Call | %s",
                prev_stats.draw_call_count > 0u ? prev_stats.draw_call_count : 1u,
                sleep_tag);
            ETCH_BOX({
                .padding = Padding::xy(14.0f, 7.0f),
                .align_x = AlignX::Center,
                .align_y = AlignY::Center,
                .background = sub_surface,
                .border_color = Color::rgba8(52, 211, 153, 110),
                .border_width = 1.0f,
                .radius = Corners::all(999.0f),
            }) {
                text(hdr_stat, {
                    .color = Color::rgb_hex(0x6EE7B7),
                    .font_size_px = 12.5f,
                    .wrap = false,
                });
            }
        }

        // 2. Main 2-column layout
        ETCH_BOX({
            .id = id("main_columns"),
            .axis = Axis::Row,
            .width = Sizing::grow(),
            .height = Sizing::grow(),
            .gap = 18.0f,
        }) {
            // Left column: Interaction + Text Input Card & Scrollable Inertial List
            ETCH_BOX({
                .id = id("left_col"),
                .axis = Axis::Col,
                .width = Sizing::grow(),
                .height = Sizing::grow(),
                .gap = 18.0f,
            }) {
                // Card 1: Text Core, IME, Springs & Floating Popover
                ETCH_BOX({
                    .id = id("card_controls"),
                    .axis = Axis::Col,
                    .width = Sizing::grow(),
                    .height = Sizing::fit(),
                    .padding = Padding::xy(20.0f, 16.0f),
                    .gap = 10.0f,
                    .background = panel_bg,
                    .border_color = panel_border,
                    .border_width = 1.0f,
                    .radius = Corners::all(16.0f),
                    .shadow_color = shadow_col,
                    .shadow_blur = 20.0f,
                    .shadow_offset = {0.0f, 7.0f},
                }) {
                    text("UTF-8 Text Core, Chinese IME, Springs & Popover", {
                        .color = text_primary,
                        .font_size_px = 16.5f,
                        .wrap = false,
                    });

                    // Row 1: Single-line UTF-8 + IME input and status badge
                    ETCH_BOX({
                        .axis = Axis::Row,
                        .width = Sizing::grow(),
                        .height = Sizing::fit(),
                        .gap = 10.0f,
                        .align_y = AlignY::Center,
                    }) {
                        app->single_res = text_input({
                            .id = single_id,
                            .buffer = app->single_buf,
                            .capacity = static_cast<uint32_t>(sizeof(app->single_buf)),
                            .placeholder = "输入中文 / UTF-8 文本 · Ctrl+A/C/X/V...",
                            .width = Sizing::grow(),
                            .height = Sizing::fixed(36.0f),
                            .padding = Padding::xy(12.0f, 8.0f),
                            .font_size_px = 14.0f,
                        });
                        if (app->single_res.submitted) {
                            ++app->submit_count;
                        }

                        char single_badge[48] = {};
                        if (app->single_res.composing) {
                            std::snprintf(single_badge, sizeof(single_badge), "IME Preedit");
                        } else {
                            std::snprintf(single_badge, sizeof(single_badge), "Submits: %u", app->submit_count);
                        }
                        ETCH_BOX({
                            .padding = Padding::xy(12.0f, 8.0f),
                            .align_x = AlignX::Center,
                            .align_y = AlignY::Center,
                            .background = app->single_res.composing
                                ? Color::rgba8(251, 191, 36, 36)
                                : sub_surface,
                            .border_color = app->single_res.composing
                                ? Color::rgba8(251, 191, 36, 175)
                                : Color::rgba8(148, 163, 184, 70),
                            .border_width = 1.0f,
                            .radius = Corners::all(9.0f),
                        }) {
                            text(single_badge, {
                                .color = app->single_res.composing
                                    ? Color::rgb_hex(0xFDE68A)
                                    : Color::rgb_hex(0x94A3B8),
                                .font_size_px = 12.5f,
                                .wrap = false,
                            });
                        }
                    }

                    // Row 2: Multiline UTF-8 text editor
                    app->multi_res = text_input({
                        .id = multi_id,
                        .buffer = app->multi_buf,
                        .capacity = static_cast<uint32_t>(sizeof(app->multi_buf)),
                        .placeholder = "多行编辑 · Enter 换行 · Up/Down 跨行 · Ctrl+C/V...",
                        .width = Sizing::grow(),
                        .height = Sizing::fixed(58.0f),
                        .padding = Padding::xy(12.0f, 8.0f),
                        .font_size_px = 13.0f,
                        .multiline = true,
                    });

                    // Analytic spring progress bar
                    ETCH_BOX({
                        .axis = Axis::Row,
                        .width = Sizing::grow(),
                        .height = Sizing::fit(),
                        .gap = 12.0f,
                        .align_y = AlignY::Center,
                    }) {
                        const float bar_ratio = clamp_f(0.08f + toggle_t * 0.88f, 0.04f, 0.99f);
                        ETCH_BOX({
                            .width = Sizing::grow(),
                            .height = Sizing::fixed(16.0f),
                            .padding = Padding::all(2.0f),
                            .background = Color::rgb_hex(0x0B0F18),
                            .border_color = panel_border,
                            .border_width = 1.0f,
                            .radius = Corners::all(999.0f),
                        }) {
                            box({
                                .width = Sizing::percent(bar_ratio),
                                .height = Sizing::grow(),
                                .background = Color::rgb_hex(0x2563EB),
                                .background_end = Color::rgb_hex(0x22D3EE),
                                .gradient_dir = {1.0f, 0.0f},
                                .radius = Corners::all(999.0f),
                            });
                        }

                        char sp_txt[48] = {};
                        std::snprintf(sp_txt, sizeof(sp_txt), "Spring t = %.2f", toggle_t);
                        text(sp_txt, {
                            .color = Color::rgb_hex(0x7DD3FC),
                            .font_size_px = 12.5f,
                            .wrap = false,
                        });
                    }

                    // Row 3: Interactive controls + right-anchored Z=50 Popover
                    ETCH_BOX({
                        .axis = Axis::Row,
                        .width = Sizing::grow(),
                        .height = Sizing::fit(),
                        .gap = 12.0f,
                        .align_y = AlignY::Center,
                    }) {
                        // Focusable pill button
                        char btn_label[64] = {};
                        std::snprintf(btn_label, sizeof(btn_label), "Capture Pill · Clicks: %u", app->click_count);
                        const Color accent_border = accent_focus
                            ? Color::rgb_hex(0x38BDF8)
                            : Color::lerp(Color::rgba8(255, 255, 255, 75), Color::rgb_hex(0x67E8F9), accent_ring_t);
                        ETCH_BOX({
                            .id = btn_accent_id,
                            .padding = Padding::xy(16.0f, 9.5f),
                            .align_x = AlignX::Center,
                            .align_y = AlignY::Center,
                            .background = accent_hov ? Color::rgb_hex(0x3B82F6) : Color::rgb_hex(0x2563EB),
                            .background_end = accent_hov ? Color::rgb_hex(0x22D3EE) : Color::rgb_hex(0x7C3AED),
                            .gradient_dir = {1.0f, 0.25f},
                            .border_color = accent_border,
                            .border_width = accent_focus ? 2.5f : 1.0f,
                            .radius = Corners::all(999.0f),
                            .shadow_color = Color::rgba8(56, 189, 248, accent_focus ? 140u : 70u),
                            .shadow_blur = accent_focus ? 18.0f : 12.0f,
                            .shadow_offset = {0.0f, 4.0f},
                            .focusable = true,
                        }) {
                            text(btn_label, {
                                .color = Color::white(),
                                .font_size_px = 13.5f,
                                .wrap = false,
                            });
                        }

                        // Spring toggle switch
                        const float knob_clamped = clamp_f(toggle_t, 0.0f, 1.0f);
                        const Color track_bg = Color::lerp(Color::rgb_hex(0x273247), Color::rgb_hex(0x0284C7), knob_clamped);
                        ETCH_BOX({
                            .id = btn_toggle_id,
                            .axis = Axis::Row,
                            .width = Sizing::fixed(60.0f),
                            .height = Sizing::fixed(30.0f),
                            .padding = Padding{ .left = 4.0f + knob_clamped * 30.0f, .right = 4.0f, .top = 4.0f, .bottom = 4.0f },
                            .align_y = AlignY::Center,
                            .background = track_bg,
                            .border_color = toggle_focus ? Color::rgb_hex(0x38BDF8) : Color::rgba8(255, 255, 255, 65),
                            .border_width = toggle_focus ? 2.0f : 1.0f,
                            .radius = Corners::all(999.0f),
                            .focusable = true,
                        }) {
                            box({
                                .width = Sizing::fixed(22.0f),
                                .height = Sizing::fixed(22.0f),
                                .background = Color::white(),
                                .radius = Corners::all(11.0f),
                                .shadow_color = Color::rgba8(0, 0, 0, 110),
                                .shadow_blur = 6.0f,
                            });
                        }

                        // Asymmetric badge
                        ETCH_BOX({
                            .padding = Padding::xy(11.0f, 7.5f),
                            .background = Color::transparent(),
                            .border_color = Color::rgb_hex(0xF472B6),
                            .border_width = 1.5f,
                            .radius = Corners{14.0f, 4.0f, 14.0f, 4.0f},
                        }) {
                            text("Tab + IME Focus", {
                                .color = Color::rgb_hex(0xF9A8D4),
                                .font_size_px = 12.0f,
                                .wrap = false,
                            });
                        }

                        // Right-aligned Floating Popover trigger + anchored Z-ordered menu
                        ETCH_BOX({
                            .width = Sizing::grow(),
                            .align_x = AlignX::Right,
                        }) {
                            ETCH_BOX({
                                .id = btn_popover_id,
                                .padding = Padding::xy(14.0f, 9.0f),
                                .align_x = AlignX::Center,
                                .align_y = AlignY::Center,
                                .background = app->popover_open ? Color::rgb_hex(0x1E293B) : sub_surface,
                                .border_color = (pop_focus || app->popover_open)
                                    ? Color::rgb_hex(0x38BDF8)
                                    : Color::rgba8(148, 163, 184, 80),
                                .border_width = pop_focus ? 2.0f : 1.0f,
                                .radius = Corners::all(10.0f),
                                .focusable = true,
                            }) {
                                text(app->popover_open ? "Spring Popover  ▲" : "Spring Popover  ▼", {
                                    .color = Color::rgb_hex(0xE2E8F0),
                                    .font_size_px = 13.0f,
                                    .wrap = false,
                                });

                                if (app->popover_open) {
                                    ETCH_BOX({
                                        .id = id("popover_menu"),
                                        .axis = Axis::Col,
                                        .width = Sizing::fixed(244.0f),
                                        .height = Sizing::fit(),
                                        .padding = Padding::all(10.0f),
                                        .gap = 5.0f,
                                        .background = Color::rgba8(18, 25, 42, 138),
                                        .border_color = Color::rgba8(125, 211, 252, 170),
                                        .border_width = 1.0f,
                                        .radius = Corners::all(12.0f),
                                        .shadow_color = Color::rgba8(0, 0, 0, 190),
                                        .shadow_blur = 26.0f,
                                        .shadow_offset = {0.0f, 10.0f},
                                        .attach_to = AttachTo::Parent,
                                        .anchor_element = Anchor::TopRight,
                                        .anchor_parent = Anchor::BottomRight,
                                        .floating_offset = {-16.0f, 8.0f},
                                        .z_index = 50,
                                        .backdrop_blur = 20.0f,
                                        .clamp_to_viewport = true,
                                    }) {
                                        text("Floating Z=50 Layer (Passthrough Guarded)", {
                                            .color = Color::rgb_hex(0x7DD3FC),
                                            .font_size_px = 11.5f,
                                            .wrap = false,
                                        });
                                        const char* presets[3] = {
                                            "Spring: Balanced (0.22s / 0.72)",
                                            "Spring: Bouncy (0.28s / 0.62)",
                                            "Spring: Critical (0.14s / 1.00)",
                                        };
                                        for (uint32_t pi = 0u; pi < 3u; ++pi) {
                                            const uint32_t item_id = id_index(id("popover_item"), pi);
                                            const bool item_hov = hovered(item_id);
                                            const bool is_sel = (app->selected_preset == pi);
                                            if (item_hov) {
                                                set_cursor(Cursor::Hand);
                                            }
                                            if (clicked(item_id)) {
                                                app->selected_preset = pi;
                                                app->popover_open = false;
                                            }
                                            ETCH_BOX({
                                                .id = item_id,
                                                .width = Sizing::grow(),
                                                .padding = Padding::xy(10.0f, 7.0f),
                                                .background = item_hov
                                                    ? Color::rgb_hex(0x2563EB)
                                                    : (is_sel ? Color::rgba8(56, 189, 248, 38) : Color::transparent()),
                                                .radius = Corners::all(7.0f),
                                            }) {
                                                text(presets[pi], {
                                                    .color = item_hov ? Color::white() : (is_sel ? Color::rgb_hex(0xBAE6FD) : text_primary),
                                                    .font_size_px = 12.5f,
                                                    .wrap = false,
                                                });
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Card 2: Inertial Scrollable Container (32 rows, SDF overlay scrollbar, rounded clipping, offscreen cull)
                ETCH_BOX({
                    .id = scroll_id,
                    .axis = Axis::Col,
                    .width = Sizing::grow(),
                    .height = Sizing::grow(),
                    .padding = Padding::all(0.0f),
                    .gap = 0.0f,
                    .background = panel_bg,
                    .border_color = Color::rgba8(96, 165, 250, 125),
                    .border_width = 1.5f,
                    .radius = Corners::all(16.0f),
                    .shadow_color = shadow_col,
                    .shadow_blur = 20.0f,
                    .shadow_offset = {0.0f, 6.0f},
                    .scroll_y = true,
                    .show_scrollbar = true,
                }) {
                    // Flush sharp-cornered banner inside scroll viewport (shaved by parent 16px rounded clip when at y=0)
                    ETCH_BOX({
                        .width = Sizing::grow(),
                        .height = Sizing::fit(),
                        .padding = Padding::xy(20.0f, 13.0f),
                        .background = Color::rgb_hex(0x1D4ED8),
                        .background_end = Color::rgb_hex(0x0284C7),
                        .gradient_dir = {1.0f, 0.0f},
                        .radius = Corners::all(0.0f),
                    }) {
                        text("Inertial Scroll Container (32 Rows · Wheel + Draggable Scrollbar + Offscreen Cull)", {
                            .color = Color::white(),
                            .font_size_px = 14.5f,
                            .wrap = false,
                        });
                    }

                    // Scrollable inner list wrapper
                    ETCH_BOX({
                        .axis = Axis::Col,
                        .width = Sizing::grow(),
                        .height = Sizing::fit(),
                        .padding = Padding{ .left = 16.0f, .right = 24.0f, .top = 12.0f, .bottom = 14.0f },
                        .gap = 8.0f,
                    }) {
                        for (uint32_t r = 0u; r < 32u; ++r) {
                            const uint32_t row_id = id_index(id("scroll_row"), r);
                            const bool row_hov = hovered(row_id);
                            if (row_hov) {
                                set_cursor(Cursor::Hand);
                            }

                            ETCH_BOX({
                                .id = row_id,
                                .axis = Axis::Row,
                                .width = Sizing::grow(),
                                .height = Sizing::fixed(40.0f),
                                .padding = Padding::xy(14.0f, 0.0f),
                                .gap = 12.0f,
                                .align_y = AlignY::Center,
                                .background = row_hov
                                    ? Color::rgb_hex(0x1E293B)
                                    : ((r & 1u) ? Color::rgb_hex(0x0F1420) : Color::rgb_hex(0x151B2B)),
                                .border_color = row_hov ? Color::rgba8(56, 189, 248, 120) : Color::rgba8(148, 163, 184, 24),
                                .border_width = 1.0f,
                                .radius = Corners::all(9.0f),
                            }) {
                                char idx_buf[24] = {};
                                std::snprintf(idx_buf, sizeof(idx_buf), "ITEM #%02u", r + 1u);
                                ETCH_BOX({
                                    .padding = Padding::xy(8.0f, 3.5f),
                                    .background = Color::rgba8(37, 99, 235, 55),
                                    .radius = Corners::all(6.0f),
                                }) {
                                    text(idx_buf, {
                                        .color = Color::rgb_hex(0x93C5FD),
                                        .font_size_px = 11.5f,
                                        .wrap = false,
                                    });
                                }

                                char desc_buf[96] = {};
                                std::snprintf(
                                    desc_buf,
                                    sizeof(desc_buf),
                                    "虚拟列表行 %02u · Offscreen Culled + SDF Rounded Clip",
                                    r + 1u);
                                ETCH_BOX({ .width = Sizing::grow() }) {
                                    text(desc_buf, {
                                        .color = text_primary,
                                        .font_size_px = 13.0f,
                                        .wrap = false,
                                    });
                                }

                                char badge_buf[24] = {};
                                std::snprintf(badge_buf, sizeof(badge_buf), "Batch 0");
                                text(badge_buf, {
                                    .color = Color::rgb_hex(0x34D399),
                                    .font_size_px = 12.0f,
                                    .wrap = false,
                                });
                            }
                        }
                    }
                }
            }

            // Right column: External SDL_GPUTexture* + v0.3-A Telemetry
            ETCH_BOX({
                .id = id("right_col"),
                .axis = Axis::Col,
                .width = Sizing::fixed(410.0f),
                .height = Sizing::grow(),
                .gap = 18.0f,
            }) {
                // Card 3: Zero-copy external GPU texture slot
                ETCH_BOX({
                    .id = id("card_texture"),
                    .axis = Axis::Col,
                    .width = Sizing::grow(),
                    .height = Sizing::fit(),
                    .padding = Padding::all(18.0f),
                    .gap = 10.0f,
                    .background = panel_bg,
                    .border_color = panel_border,
                    .border_width = 1.0f,
                    .radius = Corners::all(16.0f),
                    .shadow_color = shadow_col,
                    .shadow_blur = 20.0f,
                    .shadow_offset = {0.0f, 7.0f},
                }) {
                    text("Zero-Copy SDL_GPUTexture* Slot", {
                        .color = text_primary,
                        .font_size_px = 16.5f,
                        .wrap = false,
                    });
                    text("外部 GPU 纹理直嵌圆角容器（槽位 1 · 连续斜率抗锯齿波形）", {
                        .color = text_muted,
                        .font_size_px = 12.5f,
                        .wrap = false,
                    });

                    image({
                        .id = id("osc_viewport"),
                        .texture = app->osc_tex,
                        .width = Sizing::grow(),
                        .height = Sizing::fixed(220.0f),
                        .radius = Corners::all(12.0f),
                        .border_color = Color::rgba8(56, 189, 248, 120),
                        .border_width = 1.0f,
                    });
                }

                ETCH_COLLAPSIBLE({.id = id("demo_v20_collapsible"), .title = str_lit("Selectable text / Ctrl+C"),
                    .header_height_px = 20, .body_padding = Padding::xy(8, 3), .body_gap = 2}) {
                    text(str_lit("Etch " ETCH_VERSION_STRING " — Select and copy this text"), {.font_size_px=10,.id=id("demo_v20_text"),.selectable=true});
                    text_spans({{.text=str_lit("Rich text ")},{.text=str_lit("selection"),.color=Color::rgb_hex(0x38BDF8)}},
                        {.font_size_px=10,.id=id("demo_v20_spans"),.selectable=true});
                }

                // Card 4: Live v1.6 Telemetry & Slug Specimen
                ETCH_BOX({
                    .id = id("card_stats"),
                    .axis = Axis::Col,
                    .width = Sizing::grow(),
                    .height = Sizing::grow(),
                    .padding = Padding::all(16.0f),
                    .gap = 6.0f,
                    .background = panel_bg,
                    .border_color = panel_border,
                    .border_width = 1.0f,
                    .radius = Corners::all(16.0f),
                    .shadow_color = shadow_col,
                    .shadow_blur = 18.0f,
                    .shadow_offset = {0.0f, 6.0f},
                }) {
                    text("v" ETCH_VERSION_STRING " Telemetry (Slug \xE6\x9B\xB2\xE7\xBA\xBF \xC2\xB7 HarfBuzz \xC2\xB7 Kawase)", {
                        .color = text_primary,
                        .font_size_px = 15.5f,
                        .wrap = false,
                    });

                    ETCH_BOX({
                        .id = id("slug_specimen"),
                        .axis = Axis::Row,
                        .width = Sizing::grow(),
                        .padding = Padding::xy(14.0f, 6.0f),
                        .gap = 12.0f,
                        .align_y = AlignY::Center,
                        .background = sub_surface,
                        .border_color = Color::rgba8(56, 189, 248, 85),
                        .border_width = 1.0f,
                        .radius = Corners::all(10.0f),
                    }) {
                        text("\xE6\xB0\xB8 Ag fi AV", {
                            .color = text_primary,
                            .font_size_px = 32.0f,
                            .wrap = false,
                        });
                        ETCH_BOX({
                            .axis = Axis::Col,
                            .width = Sizing::grow(),
                            .gap = 2.0f,
                        }) {
                            text("HarfBuzz + Slug GPU Bezier", {
                                .color = Color::rgb_hex(0x38BDF8),
                                .font_size_px = 12.0f,
                                .wrap = false,
                            });
                            text("\xE6\x97\xA0\xE7\xBA\xB9\xE7\x90\x86\xE5\x9B\xBE\xE9\x9B\x86 \xC2\xB7 \xE4\xBB\xBB\xE6\x84\x8F\xE7\xBC\xA9\xE6\x94\xBE\xE9\x94\x90\xE5\x88\xA9 \xC2\xB7 kern/liga", {
                                .color = text_muted,
                                .font_size_px = 11.5f,
                                .wrap = false,
                            });
                        }
                    }

                    char line1[128] = {};
                    std::snprintf(
                        line1,
                        sizeof(line1),
                        "Draw Calls: %u  |  Prims: %u  |  Slug: %ug / %uc",
                        prev_stats.draw_call_count,
                        prev_stats.prim_count,
                        prev_stats.slug_glyph_count,
                        prev_stats.slug_curve_count);
                    text(line1, { .color = Color::rgb_hex(0x38BDF8), .font_size_px = 12.5f, .wrap = false });

                    char line2[96] = {};
                    std::snprintf(
                        line2,
                        sizeof(line2),
                        "Scroll Y: %.1f px  |  Focus ID: 0x%08X",
                        cur_scroll.y,
                        focused_id());
                    text(line2, { .color = Color::rgb_hex(0xC084FC), .font_size_px = 12.5f, .wrap = false });

                    char line3[96] = {};
                    std::snprintf(
                        line3,
                        sizeof(line3),
                        "Arena: %zu B  |  Layout: %.3f ms  |  Anims: %u",
                        prev_stats.arena_used_bytes,
                        prev_stats.layout_ms,
                        prev_stats.active_animations);
                    text(line3, { .color = Color::rgb_hex(0x34D399), .font_size_px = 12.5f, .wrap = false });

                    const Rect tracked_ime = ime_cursor_rect(app->ctx);
                    char line4[96] = {};
                    std::snprintf(
                        line4,
                        sizeof(line4),
                        "IME Rect: (%.0f, %.0f, %.0fx%.0f)  |  Composing: %s",
                        tracked_ime.x,
                        tracked_ime.y,
                        tracked_ime.w,
                        tracked_ime.h,
                        app->single_res.composing ? "YES" : "NO");
                    text(line4, { .color = Color::rgb_hex(0xFBBF24), .font_size_px = 12.5f, .wrap = false });

                    char line5[96] = {};
                    std::snprintf(
                        line5,
                        sizeof(line5),
                        "Bytes/Caret: %u/%u B  |  Multi Sel: [%u, %u)",
                        app->single_res.byte_length,
                        app->single_res.cursor_byte,
                        app->multi_res.selection_anchor_byte,
                        app->multi_res.cursor_byte);
                    text(line5, { .color = Color::rgb_hex(0x7DD3FC), .font_size_px = 12.5f, .wrap = false });

                    const uint32_t kRichLinkId = id("sandbox_rich_link");
                    if (clicked(kRichLinkId)) {
                        ++app->rich_link_clicks;
                    }
                    const bool rich_hov = hovered(kRichLinkId);
                    if (rich_hov) {
                        set_cursor(Cursor::Hand);
                    }
                    char link_lbl[48] = {};
                    std::snprintf(link_lbl, sizeof(link_lbl), "Span Link (%u)", app->rich_link_clicks);
                    text_spans({
                        TextSpan{
                            .text = str_lit("Rich "),
                            .color = Color::rgb_hex(0xF8FAFC),
                            .font_id = kFontBold,
                        },
                        TextSpan{
                            .text = str_lit("Bold+Mono"),
                            .color = Color::rgb_hex(0x38BDF8),
                            .background = Color::rgba8(14, 116, 144, 68),
                            .border_color = Color::rgba8(56, 189, 248, 120),
                            .font_size_px = 11.0f,
                            .font_id = kFontMono,
                            .padding_x = 5.0f,
                            .bg_radius = 4.0f,
                        },
                        TextSpan{
                            .text = str_lit(" \xE4\xB8\xAD\xE6\x96\x87\xE2\x9C\x93 "),
                            .color = Color::rgb_hex(0xCBD5E1),
                        },
                        TextSpan{
                            .text = str_lit("Atlas"),
                            .color = Color::rgb_hex(0x64748B),
                            .font_size_px = 11.0f,
                            .strikethrough = true,
                        },
                        TextSpan{
                            .text = str_lit(" "),
                        },
                        TextSpan{
                            .text = str_cstr(link_lbl),
                            .color = rich_hov ? Color::rgb_hex(0x7DD3FC) : Color::rgb_hex(0x38BDF8),
                            .underline = true,
                            .id = kRichLinkId,
                        },
                    }, {
                        .font_size_px = 12.0f,
                        .wrap = false,
                    });

                    // v1.2 Vector Showcase: SVG Slug icons, analytic RingArc gauges, Polyline + Cubic Bezier
                    ETCH_BOX({
                        .id = id("v12_showcase"),
                        .axis = Axis::Row,
                        .width = Sizing::grow(),
                        .height = Sizing::fixed(58.0f),
                        .padding = Padding::xy(10.0f, 6.0f),
                        .gap = 10.0f,
                        .align_y = AlignY::Center,
                        .background = sub_surface,
                        .border_color = Color::rgba8(56, 189, 248, 70),
                        .border_width = 1.0f,
                        .radius = Corners::all(10.0f),
                    }) {
                        ETCH_BOX({
                            .axis = Axis::Row,
                            .gap = 6.0f,
                            .align_y = AlignY::Center,
                        }) {
                            if (is_icon_loaded(app->ctx, app->ico_spark)) {
                                icon({ .icon_id = app->ico_spark, .size_px = 18.0f, .color = Color::rgb_hex(0xFBBF24) });
                            }
                            if (is_icon_loaded(app->ctx, app->ico_arc_bolt)) {
                                icon({ .icon_id = app->ico_arc_bolt, .size_px = 18.0f, .color = Color::rgb_hex(0x38BDF8) });
                            }
                            if (is_icon_loaded(app->ctx, app->ico_gear)) {
                                icon({ .icon_id = app->ico_gear, .size_px = 18.0f, .color = Color::rgb_hex(0xA78BFA) });
                            }
                        }

                        ETCH_BOX({
                            .id = id("v12_ring_box"),
                            .width = Sizing::fixed(102.0f),
                            .height = Sizing::fixed(44.0f),
                        }) {
                            const float na = -0.65f;
                            draw_rect({
                                .rect = {22.0f + std::cos(na) * 8.0f - 4.5f, 22.0f + std::sin(na) * 8.0f - 1.1f, 9.0f, 2.2f},
                                .fill = Color::white(),
                                .radius = Corners::all(1.1f),
                                .angle_rad = na,
                            });
                            draw_icon({.icon_id = app->ico_spark, .rect = {66.0f,14.0f,16.0f,16.0f},
                                .color = Color::white(), .angle_rad = 0.45f});
                            draw_ring({
                                .center = { 22.0f, 22.0f },
                                .radius = 16.0f,
                                .thickness = 4.5f,
                                .start_angle_rad = -1.57079633f,
                                .sweep_angle_rad = 4.65f,
                                .color = Color::rgb_hex(0x38BDF8),
                                .color_end = Color::rgb_hex(0xA855F7),
                                .track_color = Color::rgba8(148, 163, 184, 48),
                                .glow_color = Color::rgba8(56, 189, 248, 110),
                                .glow_px = 4.0f,
                                .round_caps = true,
                            });
                            draw_ring({
                                .center = { 74.0f, 22.0f },
                                .radius = 16.0f,
                                .thickness = 4.5f,
                                .start_angle_rad = -1.57079633f,
                                .sweep_angle_rad = 3.55f,
                                .color = Color::rgb_hex(0x34D399),
                                .color_end = Color::rgb_hex(0xFBBF24),
                                .track_color = Color::rgba8(148, 163, 184, 48),
                                .glow_color = Color::rgba8(52, 211, 153, 95),
                                .glow_px = 3.5f,
                                .round_caps = true,
                            });
                        }

                        ETCH_BOX({
                            .id = id("v16_fill_box"),
                            .width = Sizing::fixed(72.0f),
                            .height = Sizing::fixed(44.0f),
                        }) {
                            Vec2 star[10] = {};
                            for (uint32_t i = 0; i < 10; ++i) {
                                const float a = float(i)*0.62831853f-1.57079633f;
                                const float r = (i & 1u) ? 6.0f : 14.0f;
                                star[i] = {18.0f+std::cos(a)*r, 22.0f+std::sin(a)*r};
                            }
                            draw_filled_polygon({.points = star, .fill = Color::rgb_hex(0xFBBF24)});
                            draw_filled_path({.svg_path_d = str_lit("M 40 34 Q 54 4 68 22 Q 60 38 40 34 Z"),
                                .fill = Color::rgb_hex(0xA78BFA), .fill_end = Color::rgb_hex(0x38BDF8)});
                        }

                        ETCH_BOX({
                            .id = id("v12_plot_box"),
                            .width = Sizing::grow(),
                            .height = Sizing::fixed(44.0f),
                            .background = Color::rgba8(15, 23, 42, 160),
                            .radius = Corners::all(6.0f),
                            .clip_children = true,
                        }) {
                            static constexpr Vec2 kSparkPts[5] = {
                                { 6.0f, 32.0f }, { 32.0f, 20.0f }, { 58.0f, 26.0f }, { 84.0f, 12.0f }, { 108.0f, 16.0f },
                            };
                            draw_area_series({.points = kSparkPts, .baseline_y = 42, .smooth = true});
                            draw_polyline({
                                .points = kSparkPts,
                                .color = Color::rgba8(148, 163, 184, 150),
                                .thickness = 1.4f,
                            });
                            draw_bezier({
                                .p0 = { 6.0f, 34.0f },
                                .p1 = { 46.0f, 4.0f },
                                .p2 = { 88.0f, 40.0f },
                                .p3 = { 108.0f, 10.0f },
                                .cubic = true,
                                .color_start = Color::rgb_hex(0x38BDF8),
                                .color_end = Color::rgb_hex(0xF472B6),
                                .thickness = 2.0f,
                                .glow_color = Color::rgba8(56, 189, 248, 90),
                                .glow_px = 3.0f,
                            });
                        }
                    }
                }
            }
        }
    }
}

void step_headless_frame(AppState* app, uint32_t w, uint32_t h, float dt_seconds) noexcept {
    const etch::FrameStats prev_stats = etch::frame_stats(app->ctx);
    etch::begin_frame(app->ctx, etch::FrameDesc{
        .viewport_w = w,
        .viewport_h = h,
        .dpi_scale = 1.0f,
        .dt_seconds = dt_seconds,
    });
    build_sandbox_ui(app, prev_stats);
    etch::end_frame(app->ctx);
}

void render_window_frame(AppState* app, float dt_seconds) noexcept {
    if (!app || !app->ctx || !app->window || app->rendering || etch::frame_in_progress(app->ctx)) {
        return;
    }

    app->rendering = true;
    int px_w = 1280;
    int px_h = 800;
    SDL_GetWindowSizeInPixels(app->window, &px_w, &px_h);
    if (px_w <= 0 || px_h <= 0) {
        app->rendering = false;
        return;
    }

    float dpi = SDL_GetWindowDisplayScale(app->window);
    if (dpi <= 0.0f) {
        dpi = 1.0f;
    }

    etch::Vec2 pointer = {-1.0f, -1.0f};
    bool left_down = false;
    const SDL_WindowFlags wflags = SDL_GetWindowFlags(app->window);
    if ((wflags & SDL_WINDOW_MOUSE_FOCUS) != 0u || etch::active_id() != 0u) {
        float mx = -1.0f;
        float my = -1.0f;
        const SDL_MouseButtonFlags mb = SDL_GetMouseState(&mx, &my);
        float density = SDL_GetWindowPixelDensity(app->window);
        if (density <= 0.0f) {
            density = 1.0f;
        }
        const float pscale = density / dpi;
        pointer = {mx * pscale, my * pscale};
        left_down = (mb & SDL_BUTTON_LMASK) != 0u;
    }

    const etch::FrameStats prev_stats = etch::frame_stats(app->ctx);
    etch::begin_frame(app->ctx, etch::FrameDesc{
        .viewport_w = static_cast<uint32_t>(px_w),
        .viewport_h = static_cast<uint32_t>(px_h),
        .dpi_scale = dpi,
        .dt_seconds = dt_seconds,
        .pointer_pos = pointer,
        .pointer_down = left_down,
    });
    build_sandbox_ui(app, prev_stats);
    etch::end_frame(app->ctx);

    etch::render_window(app->ctx, etch::Color::rgb_hex(0x0B0D12));
    app->rendering = false;
}

void sandbox_on_window_refresh(etch::Context* ctx, SDL_Window* win, float dt_seconds, void* userdata) noexcept {
    AppState* app = static_cast<AppState*>(userdata);
    if (!app || app->rendering || !app->window || win != app->window) {
        return;
    }
    (void)ctx;
    etch::request_window_redraw(app->ctx, app->window);
    render_window_frame(app, dt_seconds);
}

#include "checks_sandbox_headless.h"

int run_interactive() noexcept {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Etch v" ETCH_VERSION_STRING " — Pure-GPU UI Engine",
        1280,
        800,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    etch::Context* ctx = etch::create_context(etch::ContextDesc{
        .window = window,
    });
    if (!ctx) {
        std::fprintf(stderr, "etch::create_context failed\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GPUTexture* osc_tex = create_oscilloscope_texture(ctx);
    AppState app = {
        .ctx = ctx,
        .window = window,
        .osc_tex = osc_tex,
        .running = true,
    };
    init_sandbox_icons(&app);

    etch::set_window_refresh_callback(ctx, sandbox_on_window_refresh, &app);
    render_window_frame(&app, 0.0f);

    Uint64 prev_ns = SDL_GetTicksNS();
    while (app.running) {
        SDL_Event ev;
        if (etch::wait_events(ctx, &ev)) {
            do {
                etch::process_event(ctx, ev);
                if (ev.type == SDL_EVENT_QUIT) {
                    app.running = false;
                } else if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) {
                    if (app.popover_open) {
                        app.popover_open = false;
                        etch::request_redraw(ctx);
                    } else {
                        app.running = false;
                    }
                }
            } while (SDL_PollEvent(&ev));
        }

        if (!app.running) {
            break;
        }

        if (etch::needs_redraw(ctx)) {
            const Uint64 now_ns = SDL_GetTicksNS();
            float dt = static_cast<float>(now_ns - prev_ns) * 1e-9f;
            prev_ns = now_ns;
            if (dt <= 0.0f || dt > 0.05f) {
                dt = 1.0f / 60.0f;
            }
            render_window_frame(&app, dt);
        } else {
            prev_ns = SDL_GetTicksNS();
        }
    }

    etch::set_window_refresh_callback(ctx, nullptr, nullptr);

    SDL_GPUDevice* dev = etch::gpu_device(ctx);
    if (dev) {
        SDL_WaitForGPUIdle(dev);
    }
    etch::destroy_texture(ctx, osc_tex);
    osc_tex = nullptr;
    etch::destroy_context(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    bool headless = false;
    const char* out_png = "frame_check.png";
    const char* renderer = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            headless = true;
            if ((i + 1) < argc && argv[i + 1][0] != '-') {
                out_png = argv[i + 1];
                ++i;
            }
        } else if (std::strcmp(argv[i], "--renderer") == 0 && i + 1 < argc) {
            renderer = argv[++i];
            if (std::strcmp(renderer, "d3d12") == 0) renderer = "direct3d12";
        }
    }

    if (headless) {
        return run_headless(out_png, renderer);
    }
    return run_interactive();
}
