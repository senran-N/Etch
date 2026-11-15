// Etch Studio — Pure-GPU Render-Graph, 1M Editor & Telemetry Workbench.
// Showcases Etch capabilities (frameless window chrome, box-local draw_text,
// native RGBA texture upload/update, zero-heap drag-drop, canvas marquee
// multi-selection, RingArc & SVG Slug icons) in Orthodox C++20 with zero
// per-frame heap allocations, 1-3 draw-call batching, and "--headless" checks.

#include "etch/etch.h"
#include "etch_testing.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <cmath>
#include <cstdio>
#include <cstring>


#include "checks_desktop.h"
#include "checks_editor.h"
#include "checks_a11y.h"

namespace {

inline constexpr uint32_t kPreviewWidth = 256u;
inline constexpr uint32_t kPreviewHeight = 256u;
inline constexpr uint32_t kInitialNodes = 5u;
inline constexpr uint32_t kMaxNodes = 8u;
inline constexpr uint32_t kTraceRowCount = 1000000u;
inline constexpr float kTraceRowHeight = 26.0f;
inline constexpr float kDividerPx = 6.0f;
inline constexpr float kNodePortOffsetY = 44.0f;
inline constexpr float kTau = 6.28318530717958647692f;
inline constexpr uint32_t kDragKindPass = 0x50415353u; // "PASS"

static uint8_t g_preview_rgba[kPreviewWidth * kPreviewHeight * 4u] = {};

struct PalettePassSpec {
    const char* title;
    const char* subtitle;
    etch::Color accent;
};

inline constexpr PalettePassSpec kPalettePasses[3] = {
    { "Bloom Pass",    "Mip Chain 5x",   etch::Color::rgb_hex(0xF472B6) },
    { "CAS Sharpen",   "Adaptive 0.65",  etch::Color::rgb_hex(0x38BDF8) },
    { "Film Vignette", "Cos4 Falloff",   etch::Color::rgb_hex(0xA78BFA) },
};

struct GraphNode {
    const char* title;
    const char* subtitle;
    etch::Vec2 pos;
    float width_world;
    etch::Color accent;
    uint32_t input_from; // UINT32_MAX = generator node (no input wire)
    uint32_t aux_from;   // Optional second input link (UINT32_MAX = unused)
};

struct PresetSpec {
    const char* name;
    const char* desc;
    float bloom;
    float exposure;
    etch::Color primary;
    etch::Color secondary;
};

inline constexpr PresetSpec kPresets[3] = {
    { "Cinematic ACES",  "Balanced HDR + soft bloom",    0.68f, 1.15f, etch::Color::rgb_hex(0x38BDF8), etch::Color::rgb_hex(0xF59E0B) },
    { "Cyber Neon",      "High bloom + chromatic glow",  0.94f, 1.48f, etch::Color::rgb_hex(0xF472B6), etch::Color::rgb_hex(0x22D3EE) },
    { "Clinical Studio", "Crisp reference tone curve",   0.34f, 0.96f, etch::Color::rgb_hex(0x34D399), etch::Color::rgb_hex(0x94A3B8) },
};

inline constexpr const char* kTracePassNames[6] = {
    "Pass::CameraHDR",
    "Pass::SimplexNoise",
    "Pass::KawaseDownsample",
    "Pass::KawaseUpsample",
    "Pass::ACESToneMap",
    "Pass::SlugBezierComposite",
};

struct StudioApp {
    etch::TextBuffer shader_buf = {};
    etch::Context* ctx = nullptr;
    SDL_Window* window = nullptr;
    SDL_GPUTexture* preview_tex = nullptr;
    SDL_Window* tear_off_window = nullptr;
    bool rendering = false;
    bool desktop_enabled = true; int desktop_quality = 0; bool desktop_about = false;
    bool shader_word_wrap = false;

    GraphNode nodes[kMaxNodes] = {
        { "Camera HDR",     "RGBA16F Linear",   { 36.0f,  42.0f }, 196.0f, etch::Color::rgb_hex(0x38BDF8), UINT32_MAX, UINT32_MAX },
        { "Simplex Noise",  "fBm 4 Octaves",    { 36.0f, 224.0f }, 196.0f, etch::Color::rgb_hex(0xA78BFA), UINT32_MAX, UINT32_MAX },
        { "Kawase Bloom",   "Dual-Pass 14 px",  {312.0f, 126.0f }, 204.0f, etch::Color::rgb_hex(0xF472B6), 0u,         1u         },
        { "ACES ToneMap",   "Filmic LUT v1.0",  {584.0f,  42.0f }, 204.0f, etch::Color::rgb_hex(0xFBBF24), 2u,         UINT32_MAX },
        { "Viewport Out",   "Zero-Copy GPU",    {584.0f, 198.0f }, 228.0f, etch::Color::rgb_hex(0x34D399), 3u,         UINT32_MAX },
        {},
        {},
        {},
    };

    char drop_labels[kMaxNodes][128] = {};
    uint32_t node_count = kInitialNodes;
    uint32_t trace_sort_column = 0;
    uint32_t trace_first_row = 0;
    uint32_t trace_selected_row = 0;
    bool trace_sort_descending = false;
    bool trace_sort_changed = false;
    uint32_t selected_node = 2u;
    uint32_t node_selected_mask = (1u << 2u);
    uint32_t jump_clicks = 0u;
    uint32_t pending_wire_src = UINT32_MAX;
    uint32_t selected_preset = 0u;
    float target_bloom = kPresets[0].bloom;
    float target_exposure = kPresets[0].exposure;
    float uploaded_bloom = -1.0f;
    float uploaded_exposure = -1.0f;
    uint32_t uploaded_preset = UINT32_MAX;
    bool wire_glow_enabled = true;
    bool preset_popover_open = false;
    bool ctx_menu_open = false;
    etch::Vec2 ctx_menu_pos = {0.0f, 0.0f};
    bool cmd_palette_open = false;
    bool running = true;

    uint16_t ico_bloom = etch::kInvalidIcon;
    uint16_t ico_expose = etch::kInvalidIcon;
    uint16_t ico_node = etch::kInvalidIcon;
    uint16_t ico_menu = etch::kInvalidIcon;
    uint16_t ico_search = etch::kInvalidIcon;
    uint16_t ico_win_min = etch::kInvalidIcon;
    uint16_t ico_win_max = etch::kInvalidIcon;
    uint16_t ico_win_close = etch::kInvalidIcon;

    char filter_buf[256] = "Kawase";
    etch::TextInputResult filter_res = {};
    char cmd_filter_buf[128] = "";
    etch::TextInputResult cmd_filter_res = {};
    etch::VirtualListSlice last_trace_slice = {};
};

constexpr float clamp_f(float x, float lo, float hi) noexcept {
    return (x < lo) ? lo : ((x > hi) ? hi : x);
}

inline char ascii_lower(char ch) noexcept {
    return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch + ('a' - 'A')) : ch;
}

inline bool contains_ascii_ci(const char* haystack, const char* needle) noexcept {
    if (!needle || needle[0] == '\0') {
        return true;
    }
    if (!haystack) {
        return false;
    }
    for (size_t i = 0; haystack[i] != '\0'; ++i) {
        size_t j = 0;
        while (needle[j] != '\0' && haystack[i + j] != '\0' &&
               ascii_lower(haystack[i + j]) == ascii_lower(needle[j]))
        {
            ++j;
        }
        if (needle[j] == '\0') {
            return true;
        }
    }
    return false;
}

void init_studio_icons(StudioApp* app) noexcept {
    if (!app || !app->ctx) {
        return;
    }
    const etch::Vec4 vb = {0.0f, 0.0f, 24.0f, 24.0f};
    app->ico_bloom = etch::register_svg_icon(
        app->ctx,
        etch::str_lit(
            "M 12 1.5 L 14.2 8.2 L 20.5 3.5 L 15.8 9.8 L 22.5 12 L 15.8 14.2 L 20.5 20.5 L 14.2 15.8 "
            "L 12 22.5 L 9.8 15.8 L 3.5 20.5 L 8.2 14.2 L 1.5 12 L 8.2 9.8 L 3.5 3.5 L 9.8 8.2 Z"),
        vb);
    app->ico_expose = etch::register_svg_icon(
        app->ctx,
        etch::str_lit(
            "M 12 2 A 10 10 0 1 1 12 22 A 10 10 0 1 1 12 2 Z "
            "M 12 6.8 A 5.2 5.2 0 1 0 12 17.2 A 5.2 5.2 0 1 0 12 6.8 Z"),
        vb);
    app->ico_node = etch::register_svg_icon(
        app->ctx,
        etch::str_lit(
            "M 12 2 L 20.6 7 L 20.6 17 L 12 22 L 3.4 17 L 3.4 7 Z "
            "M 12 7 L 7.6 12 L 12 17 L 16.4 12 Z"),
        vb);
    app->ico_menu = etch::register_svg_icon(
        app->ctx,
        etch::str_lit(
            "M 3 5 L 21 5 L 21 7.6 L 3 7.6 Z "
            "M 3 10.7 L 21 10.7 L 21 13.3 L 3 13.3 Z "
            "M 3 16.4 L 21 16.4 L 21 19 L 3 19 Z"),
        vb);
    app->ico_search = etch::register_svg_icon(
        app->ctx,
        etch::str_lit(
            "M 10.5 3 A 7.5 7.5 0 1 1 10.5 18 A 7.5 7.5 0 1 1 10.5 3 Z "
            "M 10.5 5.6 A 4.9 4.9 0 1 0 10.5 15.4 A 4.9 4.9 0 1 0 10.5 5.6 Z "
            "M 16.2 14.8 L 21.4 20.0 L 20.0 21.4 L 14.8 16.2 Z"),
        vb);
    app->ico_win_min = etch::register_svg_icon(
        app->ctx,
        etch::str_lit("M 5 11.2 L 19 11.2 L 19 13.4 L 5 13.4 Z"),
        vb);
    app->ico_win_max = etch::register_svg_icon(
        app->ctx,
        etch::str_lit(
            "M 5 5 L 19 5 L 19 19 L 5 19 Z "
            "M 7.2 7.2 L 7.2 16.8 L 16.8 16.8 L 16.8 7.2 Z"),
        vb);
    app->ico_win_close = etch::register_svg_icon(
        app->ctx,
        etch::str_lit(
            "M 6.0 4.6 L 12.0 10.6 L 18.0 4.6 L 19.4 6.0 L 13.4 12.0 L 19.4 18.0 "
            "L 18.0 19.4 L 12.0 13.4 L 6.0 19.4 L 4.6 18.0 L 10.6 12.0 L 4.6 6.0 Z"),
        vb);
}

inline uint8_t to_srgb_u8(float linear_val) noexcept {
    const float c = clamp_f(linear_val, 0.0f, 1.0f);
    const float s = std::pow(c, 1.0f / 2.2f);
    return static_cast<uint8_t>(clamp_f(s * 255.0f + 0.5f, 0.0f, 255.0f));
}

inline float aces_tonemap(float x) noexcept {
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return clamp_f((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

bool upload_preview_texture(StudioApp* app, float bloom, float exposure, uint32_t preset_idx) noexcept {
    if (!app || !app->ctx) {
        return false;
    }

    const PresetSpec& preset = kPresets[preset_idx % 3u];
    const float p1r = static_cast<float>(preset.primary.r) / 255.0f;
    const float p1g = static_cast<float>(preset.primary.g) / 255.0f;
    const float p1b = static_cast<float>(preset.primary.b) / 255.0f;
    const float p2r = static_cast<float>(preset.secondary.r) / 255.0f;
    const float p2g = static_cast<float>(preset.secondary.g) / 255.0f;
    const float p2b = static_cast<float>(preset.secondary.b) / 255.0f;

    for (uint32_t py = 0u; py < kPreviewHeight; ++py) {
        const float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(kPreviewHeight) * 2.0f - 1.0f;
        for (uint32_t px = 0u; px < kPreviewWidth; ++px) {
            const float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(kPreviewWidth) * 2.0f - 1.0f;
            const float d0 = std::sqrt((u + 0.28f) * (u + 0.28f) + (v + 0.14f) * (v + 0.14f));
            const float d1 = std::sqrt((u - 0.30f) * (u - 0.30f) + (v - 0.18f) * (v - 0.18f));
            const float ring = std::exp(-std::fabs(std::sqrt(u * u + v * v) - 0.48f) * 14.0f);

            const float orb0 = std::exp(-d0 * d0 * (6.2f - bloom * 2.8f));
            const float orb1 = std::exp(-d1 * d1 * (6.8f - bloom * 3.0f));
            const float checker = (((px >> 4) ^ (py >> 4)) & 1u) ? 0.022f : 0.012f;
            const float vig = clamp_f(1.15f - 0.55f * (u * u + v * v), 0.12f, 1.0f);

            float hdr_r = (checker + orb0 * p1r * 1.9f + orb1 * p2r * 1.7f + ring * (p1r + p2r) * 0.45f) * exposure * vig;
            float hdr_g = (checker * 1.15f + orb0 * p1g * 1.9f + orb1 * p2g * 1.7f + ring * (p1g + p2g) * 0.45f) * exposure * vig;
            float hdr_b = (checker * 1.50f + orb0 * p1b * 1.9f + orb1 * p2b * 1.7f + ring * (p1b + p2b) * 0.45f) * exposure * vig;

            const float scan = ((py % 32u) == 0u || (px % 32u) == 0u) ? 0.035f : 0.0f;
            hdr_r += scan * p1r;
            hdr_g += scan * p1g;
            hdr_b += scan * p1b;

            uint8_t* dst = g_preview_rgba + (size_t(py) * kPreviewWidth + px) * 4u;
            dst[0] = to_srgb_u8(aces_tonemap(hdr_r));
            dst[1] = to_srgb_u8(aces_tonemap(hdr_g));
            dst[2] = to_srgb_u8(aces_tonemap(hdr_b));
            dst[3] = 255u;
        }
    }

    bool uploaded = false;
    if (!app->preview_tex) {
        const etch::LoadedTexture created = etch::create_texture_rgba(
            app->ctx,
            etch::TextureDesc{
                .rgba_pixels = g_preview_rgba,
                .width = kPreviewWidth,
                .height = kPreviewHeight,
                .row_pitch_bytes = kPreviewWidth * 4u,
                .srgb = true,
            });
        app->preview_tex = created.texture;
        uploaded = (app->preview_tex != nullptr);
    } else {
        uploaded = etch::update_texture_rgba(
            app->ctx,
            app->preview_tex,
            kPreviewWidth,
            kPreviewHeight,
            g_preview_rgba,
            kPreviewWidth * 4u);
    }

    if (uploaded) {
        app->uploaded_bloom = bloom;
        app->uploaded_exposure = exposure;
        app->uploaded_preset = preset_idx;
        if (app->tear_off_window) etch::request_window_redraw(app->ctx, app->tear_off_window);
    }
    return uploaded;
}

void draw_button(
    uint32_t btn_id,
    const char* label,
    etch::Color bg,
    etch::Color border,
    float pad_x = 12.0f,
    float pad_y = 6.0f,
    float font_px = 12.5f,
    etch::Sizing width = etch::Sizing::fit()) noexcept
{
    const bool is_hov = etch::hovered(btn_id);
    const bool is_prs = etch::pressed(btn_id);
    const etch::Color fill = is_prs
        ? etch::Color::lerp(bg, etch::Color::white(), 0.22f)
        : (is_hov ? etch::Color::lerp(bg, etch::Color::white(), 0.10f) : bg);
    ETCH_BOX({
        .id = btn_id,
        .width = width,
        .height = etch::Sizing::fit(),
        .padding = etch::Padding::xy(pad_x, pad_y),
        .align_x = etch::AlignX::Center,
        .align_y = etch::AlignY::Center,
        .background = fill,
        .border_color = is_hov ? etch::Color::rgb_hex(0x38BDF8) : border,
        .border_width = 1.0f,
        .radius = etch::Corners::all(7.0f),
        .focusable = true,
    }) {
        etch::text(etch::str_cstr(label), {
            .color = etch::Color::rgb_hex(0xF1F5F9),
            .font_size_px = font_px,
            .wrap = false,
        });
    }
}

void build_studio_ui(StudioApp* app, const etch::FrameStats& prev_stats) noexcept {
    const float anim_bloom = etch::spring(
        etch::id("studio_anim_bloom"),
        app->target_bloom,
        etch::SpringSpec{ .response = 0.22f, .damping_ratio = 0.92f });
    const float anim_exposure = etch::spring(
        etch::id("studio_anim_exposure"),
        app->target_exposure,
        etch::SpringSpec{ .response = 0.22f, .damping_ratio = 0.92f });

    if (!app->preview_tex ||
        std::fabs(anim_bloom - app->uploaded_bloom) > 4e-3f ||
        std::fabs(anim_exposure - app->uploaded_exposure) > 4e-3f ||
        app->selected_preset != app->uploaded_preset)
    {
        upload_preview_texture(app, anim_bloom, anim_exposure, app->selected_preset);
    }

    const uint32_t kToolbar = etch::id("studio_toolbar");
    const uint32_t kBtnPresets = etch::id("studio_btn_presets");
    const uint32_t kBtnPalette = etch::id("studio_btn_palette");
    const uint32_t kBtnResetView = etch::id("studio_btn_reset_view");
    const uint32_t kBtnHud = etch::id("studio_btn_hud");
    const uint32_t kBtnWinMin = etch::id("studio_win_min");
    const uint32_t kBtnWinMax = etch::id("studio_win_max");
    const uint32_t kBtnWinClose = etch::id("studio_win_close");
    const uint32_t kBtnClearFilter = etch::id("studio_btn_clear_filter");
    const uint32_t kBtnBloomDown = etch::id("studio_btn_bloom_down");
    const uint32_t kBtnBloomUp = etch::id("studio_btn_bloom_up");
    const uint32_t kBtnExpDown = etch::id("studio_btn_exp_down");
    const uint32_t kBtnExpUp = etch::id("studio_btn_exp_up");
    const uint32_t kDialBloom = etch::id("studio_dial_bloom");
    const uint32_t kDialExp = etch::id("studio_dial_exp");
    const uint32_t kCurvePlot = etch::id("studio_curve_plot");
    const uint32_t kBtnWireGlow = etch::id("studio_btn_wire_glow");
    const uint32_t kBtnRow0 = etch::id("studio_btn_row0");
    const uint32_t kBtnRow500k = etch::id("studio_btn_row500k");
    const uint32_t kBtnRowEnd = etch::id("studio_btn_rowend");
    const uint32_t kSpanJumpMid = etch::id("studio_span_jump_mid");
    const uint32_t kSplitH = etch::id("studio_split_h");
    const uint32_t kSplitV = etch::id("studio_split_v");
    const uint32_t kCanvas = etch::id("studio_canvas");
    const uint32_t kTraceList = etch::id("studio_trace_list");
    const uint32_t kFilterInput = etch::id("studio_filter");
    const uint32_t kPresetPopover = etch::id("studio_preset_popover");
    const uint32_t kCtxMenu = etch::id("studio_ctx_menu");
    const uint32_t kCmdPalette = etch::id("studio_cmd_palette");
    const uint32_t kCmdInput = etch::id("studio_cmd_input");

    bool presets_toggled_now = false;
    bool palette_toggled_now = false;
    bool ctx_opened_now = false;

    if (etch::clicked(kBtnPresets)) {
        app->preset_popover_open = !app->preset_popover_open;
        presets_toggled_now = true;
        if (app->preset_popover_open) {
            app->ctx_menu_open = false;
            app->cmd_palette_open = false;
        }
    }
    if (etch::shortcut(etch::Mod_Primary, SDLK_K, true) || etch::clicked(kBtnPalette)) {
        app->cmd_palette_open = !app->cmd_palette_open;
        palette_toggled_now = true;
        if (app->cmd_palette_open) {
            app->preset_popover_open = false;
            app->ctx_menu_open = false;
        }
    }
    if (etch::right_clicked(kCanvas)) {
        app->ctx_menu_open = true;
        app->ctx_menu_pos = etch::pointer_pos();
        ctx_opened_now = true;
        app->preset_popover_open = false;
        app->cmd_palette_open = false;
    }

    if (app->preset_popover_open && !presets_toggled_now &&
        etch::should_close_popover(kPresetPopover, kBtnPresets))
    {
        app->preset_popover_open = false;
    }
    if (app->ctx_menu_open && !ctx_opened_now &&
        etch::should_close_popover(kCtxMenu))
    {
        app->ctx_menu_open = false;
    }
    if (app->cmd_palette_open && !palette_toggled_now &&
        etch::should_close_popover(kCmdPalette, kBtnPalette))
    {
        app->cmd_palette_open = false;
    }

    if (etch::dragging(kDialBloom, 2.0f)) {
        app->target_bloom = clamp_f(app->target_bloom + etch::pointer_delta().x * 0.008f, 0.10f, 1.20f);
    }
    if (etch::double_clicked(kDialBloom)) {
        app->target_bloom = kPresets[app->selected_preset].bloom;
    }
    if (etch::dragging(kDialExp, 2.0f)) {
        app->target_exposure = clamp_f(app->target_exposure + etch::pointer_delta().x * 0.010f, 0.50f, 2.00f);
    }
    if (etch::double_clicked(kDialExp)) {
        app->target_exposure = kPresets[app->selected_preset].exposure;
    }
    if (etch::clicked(kBtnResetView)) {
        etch::set_canvas_view(kCanvas, etch::Vec2{36.0f, 28.0f}, 1.0f);
    }
    if (etch::clicked(kBtnHud)) {
        etch::set_debug_hud_open(app->ctx, !etch::debug_hud_open(app->ctx));
    }
    if (etch::clicked(kBtnWinMin)) {
        etch::minimize_window(app->window);
    }
    if (etch::clicked(kBtnWinMax) || etch::double_clicked(kToolbar)) {
        if (etch::is_window_maximized(app->ctx, app->window)) {
            etch::restore_window(app->window);
        } else {
            etch::maximize_window(app->window);
        }
    }
    if (etch::clicked(kBtnWinClose)) {
        app->running = false;
    }
    if (etch::clicked(kBtnClearFilter)) {
        app->filter_buf[0] = '\0';
        etch::set_text_selection(kFilterInput, 0u, 0u);
    }
    if (etch::clicked(kBtnBloomDown)) {
        app->target_bloom = clamp_f(app->target_bloom - 0.12f, 0.10f, 1.20f);
    }
    if (etch::clicked(kBtnBloomUp)) {
        app->target_bloom = clamp_f(app->target_bloom + 0.12f, 0.10f, 1.20f);
    }
    if (etch::clicked(kBtnExpDown)) {
        app->target_exposure = clamp_f(app->target_exposure - 0.15f, 0.50f, 2.00f);
    }
    if (etch::clicked(kBtnExpUp)) {
        app->target_exposure = clamp_f(app->target_exposure + 0.15f, 0.50f, 2.00f);
    }
    if (etch::clicked(kBtnWireGlow)) {
        app->wire_glow_enabled = !app->wire_glow_enabled;
    }
    if (etch::clicked(kBtnRow0)) {
        etch::virtual_list_scroll_to_row(kTraceList, 0u, kTraceRowHeight, etch::AlignY::Top);
    }
    if (etch::clicked(kBtnRow500k)) {
        etch::virtual_list_scroll_to_row(kTraceList, 500000u, kTraceRowHeight, etch::AlignY::Top);
    }
    if (etch::clicked(kSpanJumpMid)) {
        ++app->jump_clicks;
        etch::virtual_list_scroll_to_row(kTraceList, 500000u, kTraceRowHeight, etch::AlignY::Top);
    }
    if (etch::clicked(kBtnRowEnd)) {
        etch::virtual_list_scroll_to_row(kTraceList, 999990u, kTraceRowHeight, etch::AlignY::Top);
    }

    const etch::CanvasState drop_view = etch::canvas_state(kCanvas);
    const etch::Rect drop_canvas = etch::element_bounds(kCanvas);
    const etch::Vec2 drop_pos = etch::os_drop_pos();
    const auto files = etch::dropped_files_on(kCanvas);
    const auto texts = etch::dropped_texts_on(kCanvas);
    for (uint32_t i = 0; i < files.size() + texts.size() && app->node_count < kMaxNodes; ++i) {
        const etch::Str value = i < files.size() ? files[i] : texts[i - static_cast<uint32_t>(files.size())];
        const uint32_t n = app->node_count++;
        uint32_t len = value.length < 127 ? value.length : 127;
        while (len < value.length && len && (static_cast<uint8_t>(value.chars[len]) & 0xC0u) == 0x80u) --len;
        std::memcpy(app->drop_labels[n], value.chars, len);
        app->drop_labels[n][len] = '\0';
        app->nodes[n] = GraphNode{app->drop_labels[n], i < files.size() ? "External asset" : "Dropped note",
            {(drop_pos.x - drop_canvas.x - drop_view.pan.x) / drop_view.zoom,
             (drop_pos.y - drop_canvas.y - drop_view.pan.y) / drop_view.zoom},
            210.0f, etch::Color::rgb_hex(0x34D399), UINT32_MAX, UINT32_MAX};
    }
    const etch::CanvasState prev_cs = etch::canvas_state(kCanvas);

    const etch::Rect tb_bounds = etch::element_bounds(kToolbar);
    etch::window_drag_region(
        (tb_bounds.w > 20.0f && tb_bounds.h > 10.0f)
            ? tb_bounds
            : etch::Rect{10.0f, 10.0f, 1420.0f, 44.0f});

    etch::DragPayloadView dropped_pass = {};
    if (etch::accept_drag_payload(kCanvas, kDragKindPass, &dropped_pass)) {
        if (dropped_pass.u32 < 3u && app->node_count < kMaxNodes) {
            const uint32_t new_idx = app->node_count++;
            const PalettePassSpec& spec = kPalettePasses[dropped_pass.u32];
            const etch::Vec2 drop_w = prev_cs.screen_to_world(etch::pointer_pos());
            app->nodes[new_idx] = GraphNode{
                spec.title,
                spec.subtitle,
                { drop_w.x - 96.0f, drop_w.y - 24.0f },
                196.0f,
                spec.accent,
                (app->selected_node < new_idx) ? app->selected_node : UINT32_MAX,
                UINT32_MAX,
            };
            app->selected_node = new_idx;
            app->node_selected_mask = (1u << new_idx);
        }
    }

    // Root workbench frame
    ETCH_BOX({
        .id = etch::id("studio_root"),
        .axis = etch::Axis::Col,
        .width = etch::Sizing::grow(),
        .height = etch::Sizing::grow(),
        .padding = etch::Padding::all(10.0f),
        .gap = 8.0f,
        .background = etch::Color::rgb_hex(0x090C12),
    }) {
        if (etch::begin_menu_bar({.id=etch::id("studio_desktop_menu")})) {
            if (etch::begin_menu({.id=etch::id("studio_file_menu"),.label=etch::str_lit("File")})) {
                if (etch::menu_item({.id=etch::id("studio_about"),.label=etch::str_lit("About Etch Studio")})) app->desktop_about=true;
                if (etch::menu_item({.id=etch::id("studio_quit"),.label=etch::str_lit("Quit"),.shortcut_hint=etch::str_lit("Alt+F4")})) app->running=false;
                etch::end_menu();
            }
            if (etch::begin_menu({.id=etch::id("studio_view_menu"),.label=etch::str_lit("View")})) {
                const etch::ThemeId cur_theme = etch::theme(app->ctx).id;
                if (etch::menu_item({.id=etch::id("studio_view_glow"),.label=etch::str_lit("Wire glow"),.checked=app->wire_glow_enabled})) app->wire_glow_enabled=!app->wire_glow_enabled;
                if (etch::menu_item({.id=etch::id("studio_view_wrap"),.label=etch::str_lit("Word wrap shader"),.checked=app->shader_word_wrap})) app->shader_word_wrap=!app->shader_word_wrap;
                etch::menu_separator();
                if (etch::menu_item({.id=etch::id("studio_theme_dark"),.label=etch::str_lit("Theme: Dark"),.checked=cur_theme==etch::ThemeId::Dark})) etch::set_theme(etch::Theme::dark(), app->ctx);
                if (etch::menu_item({.id=etch::id("studio_theme_light"),.label=etch::str_lit("Theme: Light"),.checked=cur_theme==etch::ThemeId::Light})) etch::set_theme(etch::Theme::light(), app->ctx);
                if (etch::menu_item({.id=etch::id("studio_theme_hc"),.label=etch::str_lit("Theme: High Contrast"),.checked=cur_theme==etch::ThemeId::HighContrast})) etch::set_theme(etch::Theme::high_contrast(), app->ctx);
                if (etch::menu_item({.id=etch::id("studio_theme_ind"),.label=etch::str_lit("Theme: Industrial"),.checked=cur_theme==etch::ThemeId::Industrial})) etch::set_theme(etch::Theme::industrial(), app->ctx);
                etch::menu_separator();
                if (etch::menu_item({.id=etch::id("studio_restore_panels"),.label=etch::str_lit("Restore panels")})) {
                    const char* names[]={"studio_pane_inspector","studio_pane_outliner","studio_pane_canvas","studio_pane_trace","studio_pane_shader"};
                    for (const char* name : names) etch::dock_set_pane_open(etch::id("studio_dock"),etch::id(name),true);
                }
                etch::end_menu();
            }
            if (etch::begin_menu({.id=etch::id("studio_preset_menu"),.label=etch::str_lit("Preset")})) {
                for(uint32_t i=0;i<3;++i) if(etch::menu_item({.id=etch::id_index(etch::id("studio_desktop_preset"),i),.label=etch::str_cstr(kPresets[i].name)})) {
                    app->selected_preset=i;app->target_bloom=kPresets[i].bloom;app->target_exposure=kPresets[i].exposure;
                }
                etch::end_menu();
            }
            etch::end_menu_bar();
        }
        if(etch::begin_modal({.id=etch::id("studio_about_modal"),.open=&app->desktop_about,.title=etch::str_lit("Etch Studio " ETCH_VERSION_STRING)})) {
            etch::text(etch::str_lit("Standard desktop primitives / interactive five-zone docking"));
            if(etch::button({.id=etch::id("studio_about_close"),.label=etch::str_lit("Close")})) app->desktop_about=false;
            etch::end_modal();
        }
        if(!app->ctx_menu_open && etch::begin_context_menu(etch::id("studio_desktop_context"),etch::id("studio_dock"))) {
            if(etch::menu_item({.id=etch::id("studio_context_reset"),.label=etch::str_lit("Reset exposure")})) app->target_exposure=1;
            etch::end_context_menu();
        }
        // Top Toolbar (Custom Window Chrome Titlebar)
        ETCH_BOX({
            .id = kToolbar,
            .axis = etch::Axis::Row,
            .width = etch::Sizing::grow(),
            .height = etch::Sizing::fixed(44.0f),
            .padding = etch::Padding::xy(14.0f, 0.0f),
            .gap = 10.0f,
            .align_y = etch::AlignY::Center,
            .background = etch::Color::rgb_hex(0x111724),
            .border_color = etch::Color::rgba8(148, 163, 184, 42),
            .border_width = 1.0f,
            .radius = etch::Corners::all(10.0f),
        }) {
            ETCH_BOX({
                .axis = etch::Axis::Row,
                .padding = etch::Padding::xy(9.0f, 4.0f),
                .gap = 6.0f,
                .align_y = etch::AlignY::Center,
                .background = etch::Color::rgb_hex(0x0EA5E9),
                .background_end = etch::Color::rgb_hex(0x6366F1),
                .gradient_dir = {1.0f, 0.0f},
                .radius = etch::Corners::all(6.0f),
            }) {
                if (app->ico_node != etch::kInvalidIcon) {
                    etch::icon(etch::IconDesc{
                        .icon_id = app->ico_node,
                        .size_px = 13.0f,
                        .color = etch::Color::white(),
                    });
                }
                etch::text(etch::str_lit("ETCH " ETCH_VERSION_STRING), {
                    .color = etch::Color::white(),
                    .font_size_px = 12.0f,
                    .wrap = false,
                });
            }

            etch::text(etch::str_lit("Etch Studio " ETCH_VERSION_STRING " — GPU Render-Graph & Telemetry"), {
                .color = etch::Color::rgb_hex(0xE2E8F0),
                .font_size_px = 14.0f,
                .wrap = false,
            });
            const uint32_t tear_id = etch::id("studio_tear_off");
            if (etch::clicked(tear_id)) {
                if (app->tear_off_window) {
                    // Destruction occurs after the serialized main build ends.
                    SDL_HideWindow(app->tear_off_window);
                } else {
                    app->tear_off_window = SDL_CreateWindow("Etch Studio " ETCH_VERSION_STRING " — Tear-Off Preview", 480, 360, SDL_WINDOW_RESIZABLE);
                }
            }
            draw_button(tear_id, "Tear-Off Preview", etch::Color::rgb_hex(0x1E293B), etch::Color::rgb_hex(0x475569));


            char preset_btn_label[64] = {};
            std::snprintf(preset_btn_label, sizeof(preset_btn_label), "Preset: %s", kPresets[app->selected_preset].name);
            draw_button(
                kBtnPresets,
                preset_btn_label,
                app->preset_popover_open ? etch::Color::rgb_hex(0x1D4ED8) : etch::Color::rgb_hex(0x1E293B),
                etch::Color::rgba8(56, 189, 248, 96));

            draw_button(
                kBtnResetView,
                "Reset View",
                etch::Color::rgb_hex(0x172033),
                etch::Color::rgba8(148, 163, 184, 52));

            draw_button(
                kBtnPalette,
                "Cmd Palette [Ctrl+K]",
                app->cmd_palette_open ? etch::Color::rgb_hex(0x0F766E) : etch::Color::rgb_hex(0x172033),
                etch::Color::rgba8(45, 212, 191, 88));

            ETCH_BOX({ .width = etch::Sizing::grow() }) {}

            char telemetry[128] = {};
            std::snprintf(
                telemetry,
                sizeof(telemetry),
                "%u prims | %u DC | %.0f us | %.0f%% zoom",
                prev_stats.prim_count,
                prev_stats.draw_call_count,
                prev_stats.layout_ms * 1000.0,
                prev_cs.zoom * 100.0f);
            etch::text(etch::str_cstr(telemetry), {
                .color = etch::Color::rgb_hex(0x94A3B8),
                .font_size_px = 12.0f,
                .wrap = false,
            });

            const bool hud_on = etch::debug_hud_open(app->ctx);
            draw_button(
                kBtnHud,
                hud_on ? "F12 HUD: ON" : "F12 HUD",
                hud_on ? etch::Color::rgb_hex(0x0369A1) : etch::Color::rgb_hex(0x172033),
                etch::Color::rgba8(56, 189, 248, 84));

            const auto draw_chrome_btn = [&](uint32_t btn_id, uint16_t ico_id, etch::Color hover_bg) noexcept {
                const bool hov = etch::hovered(btn_id);
                const bool prs = etch::pressed(btn_id);
                ETCH_BOX({
                    .id = btn_id,
                    .width = etch::Sizing::fixed(26.0f),
                    .height = etch::Sizing::fixed(26.0f),
                    .align_x = etch::AlignX::Center,
                    .align_y = etch::AlignY::Center,
                    .background = prs
                        ? etch::Color::lerp(hover_bg, etch::Color::white(), 0.18f)
                        : (hov ? hover_bg : etch::Color::rgb_hex(0x172033)),
                    .border_color = etch::Color::rgba8(148, 163, 184, 48),
                    .border_width = 1.0f,
                    .radius = etch::Corners::all(6.0f),
                    .focusable = true,
                }) {
                    if (ico_id != etch::kInvalidIcon) {
                        etch::icon(etch::IconDesc{
                            .icon_id = ico_id,
                            .size_px = 12.0f,
                            .color = hov ? etch::Color::white() : etch::Color::rgb_hex(0xCBD5E1),
                        });
                    }
                }
            };
            draw_chrome_btn(kBtnWinMin, app->ico_win_min, etch::Color::rgb_hex(0x1E293B));
            draw_chrome_btn(kBtnWinMax, app->ico_win_max, etch::Color::rgb_hex(0x1E293B));
            draw_chrome_btn(kBtnWinClose, app->ico_win_close, etch::Color::rgb_hex(0xDC2626));
        }

        const uint32_t kDockMain = etch::id("studio_dock");
        const uint32_t kPaneInspector = etch::id("studio_pane_inspector");
        const uint32_t kPaneOutliner = etch::id("studio_pane_outliner");
        const uint32_t kPaneCanvas = etch::id("studio_pane_canvas");
        const uint32_t kPaneShader = etch::id("studio_pane_shader");
        const uint32_t kPaneTrace = etch::id("studio_pane_trace");
        if (!etch::dock_space_initialized(kDockMain)) {
            const uint32_t root = etch::dock_reset_root(kDockMain);
            uint32_t left = 0, right = 0, top = 0, bottom = 0;
            etch::dock_split_node(kDockMain, root, etch::Axis::Row, .25f, &left, &right, kSplitH, 280, 520);
            etch::dock_split_node(kDockMain, right, etch::Axis::Col, .62f, &top, &bottom, kSplitV, 260, 180);
            etch::dock_add_pane(kDockMain, left, { .id = kPaneInspector, .title = etch::str_lit("Inspector"), .closable=true });
            etch::dock_add_pane(kDockMain, left, { .id = kPaneOutliner, .title = etch::str_lit("Outliner"), .closable=true });
            uint32_t graph=0,shader=0;
            etch::dock_split_node(kDockMain, top, etch::Axis::Row, .60f, &graph, &shader, etch::id("studio_shader_split"), 240, 240);
            etch::dock_add_pane(kDockMain, shader, {.id=kPaneShader,.title=etch::str_lit("Shader Source")});
            etch::dock_add_pane(kDockMain, graph, { .id = kPaneCanvas, .title = etch::str_lit("Node Graph"), .closable=true });
            etch::dock_add_pane(kDockMain, bottom, { .id = kPaneTrace, .title = etch::str_lit("1M Trace Table"), .closable=true });
        }
        etch::begin_dock_space({ .id = kDockMain, .divider_size_px = kDividerPx });
        while (const uint32_t pane = etch::next_dock_pane()) {
        if(pane == kPaneShader) {
            if(!app->shader_buf.data) {
                char source[24000]; uint32_t used=0;
                const char* header="// Etch analytic SDF uber shader\ncbuffer Globals : register(b0) { float4 viewport; float4 time; };\nstruct Prim { float4 bounds; float4 clip; float4 payload; uint4 style; };\nStructuredBuffer<Prim> primitives : register(t4);\n\nfloat rounded_rect(float2 p, float2 half_size, float radius) {\n    float2 q = abs(p) - half_size + radius;\n    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;\n}\n\n";
                const size_t n=std::strlen(header);std::memcpy(source,header,n);used=static_cast<uint32_t>(n);
                for(uint32_t pass=0;pass<12;++pass){const int written=std::snprintf(source+used,sizeof(source)-used,
                    "// Analytic coverage stage %u\nfloat4 shade_%u(float2 position, float4 bounds, float4 tint) {\n    float2 center = bounds.xy + bounds.zw * 0.5;\n    float distance = rounded_rect(position - center, bounds.zw * 0.5, 6.0);\n    float coverage = saturate(0.5 - distance);\n    return float4(tint.rgb * coverage, tint.a * coverage);\n}\n\n",pass,pass);used+=static_cast<uint32_t>(written);}
                etch::init_text_buffer(&app->shader_buf,{source,used});
            }
            etch::code_editor({.id=etch::id("studio_shader_editor"),.buffer=&app->shader_buf,.font_size_px=12,.word_wrap=app->shader_word_wrap});
        }
        if (pane == kPaneOutliner) {
            const auto root = etch::begin_tree_node({ .id = etch::id("studio_tree_root"), .label = etch::str_lit("FramePipeline"), .default_open = true });
            if (root.open) {
                static constexpr const char* groups[] = {"Deferred", "Tone", "Bloom Passes"};
                for (uint32_t g = 0; g < 3; ++g) {
                    const auto group = etch::begin_tree_node({ .id = etch::id_index(etch::id("studio_tree_group"),g), .label = etch::Str{groups[g]}, .default_open = true });
                    if (group.open) {
                        for (uint32_t i = g; i < app->node_count; i += 3) {
                            const auto item = etch::begin_tree_node({ .id = etch::id_index(etch::id("studio_tree_node"),i), .label = etch::Str{app->nodes[i].title}, .selected = app->selected_node == i, .leaf = true });
                            if (item.clicked) {
                                app->selected_node = i; app->node_selected_mask = 1u << i;
                                etch::CanvasState view = etch::canvas_state(kCanvas);
                                view.pan = {100.0f-app->nodes[i].pos.x*view.zoom, 80.0f-app->nodes[i].pos.y*view.zoom};
                                etch::set_canvas_view(kCanvas,view.pan,view.zoom);
                                etch::request_focus(kCanvas);
                            }
                            const auto shader = etch::begin_tree_node({ .id = etch::id_index(etch::id("studio_tree_shader"),i), .label = etch::str_lit("    Shader / vertex + fragment"), .leaf = true });
                            (void)shader;
                        }
                        etch::end_tree_node();
                    }
                }
                etch::end_tree_node();
            }
        }
        if (pane == kPaneInspector) {
        // Pane 1: Left Inspector (UTF-8 IME filter + Spring shader controls + Inertial Node List)
        ETCH_BOX({
            .axis = etch::Axis::Col,
            .width = etch::Sizing::percent(1),
            .height = etch::Sizing::grow(),
            .padding = etch::Padding::all(12.0f),
            .gap = 10.0f,
            .background = etch::Color::rgb_hex(0x0F1522),
            .border_color = etch::Color::rgba8(148, 163, 184, 38),
            .border_width = 1.0f,
            .radius = etch::Corners::all(10.0f),
            .clip_children = true,
            .scroll_y = true,
        }) {
            if(etch::begin_collapsible({.id=etch::id("studio_a11y_panel"),.title=etch::str_lit("A11y Semantic Tree")}).visible) {
                char line[160];std::snprintf(line,sizeof(line),"Nodes: %u",etch::frame_stats(app->ctx).a11y_node_count);etch::text(etch::str_cstr(line),{.font_size_px=11});
                const auto semantics=etch::frame_a11y_nodes(app->ctx,app->window);
                for(const auto& n:semantics)if(n.state_flags&etch::A11yState_Focused) {
                    std::snprintf(line,sizeof(line),"Focus %s %08X [%.0f %.0f %.0f %.0f]",etch::a11y_role_name(n.role),n.id,double(n.bounds.x),double(n.bounds.y),double(n.bounds.w),double(n.bounds.h));etch::text(etch::str_cstr(line),{.font_size_px=10});break;
                }
                for(uint32_t i=0;i<semantics.size() && i<6;++i) {
                    const auto& n=semantics[i];std::snprintf(line,sizeof(line),"%s %08X",etch::a11y_role_name(n.role),n.id);etch::text(etch::str_cstr(line),{.font_size_px=10});
                }
                etch::end_collapsible();
            }
            etch::text(etch::str_lit("Trace Filter (UTF-8 / 中文 IME)"), {
                .color = etch::Color::rgb_hex(0x94A3B8),
                .font_size_px = 12.0f,
                .wrap = false,
            });

            ETCH_BOX({
                .axis = etch::Axis::Row,
                .width = etch::Sizing::grow(),
                .height = etch::Sizing::fit(),
                .gap = 6.0f,
                .align_y = etch::AlignY::Center,
            }) {
                app->filter_res = etch::text_input(etch::TextInputDesc{
                    .id = kFilterInput,
                    .buffer = app->filter_buf,
                    .capacity = sizeof(app->filter_buf),
                    .placeholder = etch::str_lit("Filter 1M GPU trace..."),
                    .width = etch::Sizing::grow(),
                    .height = etch::Sizing::fixed(34.0f),
                    .padding = etch::Padding::xy(10.0f, 7.0f),
                    .font_size_px = 13.0f,
                });
                draw_button(
                    kBtnClearFilter,
                    "Clear",
                    etch::Color::rgb_hex(0x1E293B),
                    etch::Color::rgba8(148, 163, 184, 52),
                    10.0f,
                    7.0f,
                    12.0f);
            }

            // Spring-animated shader parameters card
            const GraphNode& sel = app->nodes[app->selected_node];
            ETCH_BOX({
                .axis = etch::Axis::Col,
                .width = etch::Sizing::percent(1),
                .height = etch::Sizing::fit(),
                .padding = etch::Padding::all(12.0f),
                .gap = 8.0f,
                .background = etch::Color::rgb_hex(0x141C2E),
                .border_color = etch::Color::rgba8(56, 189, 248, 60),
                .border_width = 1.0f,
                .radius = etch::Corners::all(9.0f),
            }) {
                char sel_hdr[96] = {};
                std::snprintf(sel_hdr, sizeof(sel_hdr), "Inspector: %s", sel.title);
                etch::text(etch::str_cstr(sel_hdr), {
                    .color = etch::Color::rgb_hex(0xF8FAFC),
                    .font_size_px = 13.5f,
                    .wrap = false,
                });

                char pos_line[96] = {};
                std::snprintf(pos_line, sizeof(pos_line), "%s  |  pos (%.0f, %.0f)", sel.subtitle, sel.pos.x, sel.pos.y);
                etch::text(etch::str_cstr(pos_line), {
                    .color = etch::Color::rgb_hex(0x94A3B8),
                    .font_size_px = 11.5f,
                    .wrap = false,
                });

                const etch::GridTrack property_tracks[]={etch::GridTrack::px(88),etch::GridTrack::fr()};
                ETCH_BOX({.id=etch::id("studio_v22_grid"),.width=etch::Sizing::grow(),.gap=4,.row_gap=3,.grid_tracks=property_tracks}) {
                    const char* labels[]={"Node","Shader","Passes","Memory"};
                    const char* values[]={sel.title,sel.subtitle,"1–3 GPU draws","Sparse / arena"};
                    for(uint32_t i=0;i<4;++i) {
                        ETCH_BOX({.id=etch::id_index(etch::id("studio_v22_key"),i),.width=etch::Sizing::grow()}) { etch::text(etch::str_cstr(labels[i]),{.font_size_px=11,.wrap=false}); }
                        ETCH_BOX({.id=etch::id_index(etch::id("studio_v22_value"),i),.width=etch::Sizing::grow()}) { etch::text(etch::str_cstr(values[i]),{.font_size_px=11,.wrap=false,.ellipsis=true}); }
                    }
                }
                ETCH_BOX({.id=etch::id("studio_v22_tags"),.width=etch::Sizing::grow(0,300),.gap=5,.wrap=true,.row_gap=4}) {
                    const char* tags[]={"Slug","HarfBuzz","Grid","FlexWrap","Fenwick","ZeroHeap"};
                    for(uint32_t i=0;i<6;++i) {
                        ETCH_BOX({.id=etch::id_index(etch::id("studio_v22_tag"),i),.width=etch::Sizing::fixed(68),.height=etch::Sizing::fixed(20),.padding=etch::Padding::xy(5,2),.background=etch::Color::rgb_hex(0x1E3A5F),.radius=etch::Corners::all(4)}) { etch::text(etch::str_cstr(tags[i]),{.font_size_px=10,.wrap=false}); }
                    }
                }

                ETCH_COLLAPSIBLE({.id=etch::id("studio_desktop_controls"),.title=etch::str_lit("Desktop Controls"),.default_open=false,.header_height_px=22,.body_gap=3}) {
                    etch::drag_value({.id=etch::id("studio_exposure_value"),.label=etch::str_lit("Exposure"),.value=&app->target_exposure,.min_value=.5f,.max_value=2,.speed=.01f,.width=etch::Sizing::grow(),.height_px=22});
                    etch::set_tooltip(etch::id("studio_exposure_value"),etch::str_lit("Drag to scrub; double-click to type. Shift for precision."));
                    etch::slider({.id=etch::id("studio_bloom_slider"),.label=etch::str_lit("Bloom"),.value=&app->target_bloom,.max_value=1.2f,.step=.01f,.height_px=22});
                    const etch::Str quality[]={etch::str_lit("Draft"),etch::str_lit("Balanced"),etch::str_lit("High quality")};
                    etch::combo_box({.id=etch::id("studio_quality"),.selected_index=&app->desktop_quality,.items=quality,.height_px=22});
                    ETCH_BOX({.width=etch::Sizing::grow(),.gap=8}) {
                        etch::toggle_switch({.id=etch::id("studio_desktop_glow"),.label=etch::str_lit("Glow"),.value=&app->wire_glow_enabled});
                        etch::checkbox({.id=etch::id("studio_desktop_enable"),.label=etch::str_lit("Enabled"),.checked=&app->desktop_enabled});
                    }
                    etch::progress_bar({.id=etch::id("studio_desktop_progress"),.fraction=app->target_bloom/1.2f});
                    if(etch::button({.id=etch::id("studio_desktop_reset"),.label=etch::str_lit("Reset controls"),.height=etch::Sizing::fixed(22)})) {app->target_bloom=.5f;app->target_exposure=1;}
                    etch::set_tooltip(etch::id("studio_desktop_reset"),etch::str_lit("Restore the default shader controls"));
                }
                // Bloom spring bar + steppers
                ETCH_BOX({
                    .axis = etch::Axis::Row,
                    .width = etch::Sizing::grow(),
                    .gap = 6.0f,
                    .align_y = etch::AlignY::Center,
                }) {
                    char btxt[48] = {};
                    std::snprintf(btxt, sizeof(btxt), "Bloom %.2f", anim_bloom);
                    etch::text(etch::str_cstr(btxt), {
                        .color = etch::Color::rgb_hex(0xE2E8F0),
                        .font_size_px = 12.0f,
                        .wrap = false,
                    });
                    ETCH_BOX({ .width = etch::Sizing::grow() }) {}
                    draw_button(kBtnBloomDown, "-", etch::Color::rgb_hex(0x1E293B), etch::Color::rgba8(148, 163, 184, 52), 9.0f, 3.0f);
                    draw_button(kBtnBloomUp, "+", etch::Color::rgb_hex(0x1E293B), etch::Color::rgba8(148, 163, 184, 52), 9.0f, 3.0f);
                }
                ETCH_BOX({
                    .width = etch::Sizing::grow(),
                    .height = etch::Sizing::fixed(6.0f),
                    .background = etch::Color::rgb_hex(0x0B101B),
                    .radius = etch::Corners::all(3.0f),
                }) {
                    ETCH_BOX({
                        .width = etch::Sizing::percent(clamp_f(anim_bloom / 1.20f, 0.04f, 1.0f)),
                        .height = etch::Sizing::grow(),
                        .background = etch::Color::rgb_hex(0x38BDF8),
                        .background_end = etch::Color::rgb_hex(0xF472B6),
                        .gradient_dir = {1.0f, 0.0f},
                        .radius = etch::Corners::all(3.0f),
                    }) {}
                }

                // Exposure spring bar + steppers
                ETCH_BOX({
                    .axis = etch::Axis::Row,
                    .width = etch::Sizing::grow(),
                    .gap = 6.0f,
                    .align_y = etch::AlignY::Center,
                }) {
                    char etxt[48] = {};
                    std::snprintf(etxt, sizeof(etxt), "Exposure %.2f", anim_exposure);
                    etch::text(etch::str_cstr(etxt), {
                        .color = etch::Color::rgb_hex(0xE2E8F0),
                        .font_size_px = 12.0f,
                        .wrap = false,
                    });
                    ETCH_BOX({ .width = etch::Sizing::grow() }) {}
                    draw_button(kBtnExpDown, "-", etch::Color::rgb_hex(0x1E293B), etch::Color::rgba8(148, 163, 184, 52), 9.0f, 3.0f);
                    draw_button(kBtnExpUp, "+", etch::Color::rgb_hex(0x1E293B), etch::Color::rgba8(148, 163, 184, 52), 9.0f, 3.0f);
                }
                ETCH_BOX({
                    .width = etch::Sizing::grow(),
                    .height = etch::Sizing::fixed(6.0f),
                    .background = etch::Color::rgb_hex(0x0B101B),
                    .radius = etch::Corners::all(3.0f),
                }) {
                    ETCH_BOX({
                        .width = etch::Sizing::percent(clamp_f((anim_exposure - 0.50f) / 1.50f, 0.04f, 1.0f)),
                        .height = etch::Sizing::grow(),
                        .background = etch::Color::rgb_hex(0xFBBF24),
                        .background_end = etch::Color::rgb_hex(0x34D399),
                        .gradient_dir = {1.0f, 0.0f},
                        .radius = etch::Corners::all(3.0f),
                    }) {}
                }

                // Interactive RingArc rotary dials (Drag to tune, Double-click to reset)
                ETCH_BOX({
                    .axis = etch::Axis::Row,
                    .width = etch::Sizing::grow(),
                    .height = etch::Sizing::fit(),
                    .gap = 8.0f,
                    .align_y = etch::AlignY::Center,
                }) {
                    const auto draw_dial = [&](
                        uint32_t dial_id,
                        float norm_val,
                        uint16_t icon_id,
                        etch::Color c0,
                        etch::Color c1,
                        const char* label_title,
                        const char* label_sub) noexcept
                    {
                        if (etch::hovered(dial_id)) {
                            etch::set_cursor(etch::Cursor::Hand);
                        }
                        ETCH_BOX({
                            .axis = etch::Axis::Row,
                            .width = etch::Sizing::grow(),
                            .height = etch::Sizing::fit(),
                            .padding = etch::Padding::xy(6.0f, 4.0f),
                            .gap = 6.0f,
                            .align_y = etch::AlignY::Center,
                            .background = etch::Color::rgb_hex(0x0E1525),
                            .border_color = etch::hovered(dial_id)
                                ? etch::Color::rgba8(56, 189, 248, 150)
                                : etch::Color::rgba8(148, 163, 184, 34),
                            .border_width = 1.0f,
                            .radius = etch::Corners::all(7.0f),
                        }) {
                            ETCH_BOX({
                                .id = dial_id,
                                .width = etch::Sizing::fixed(38.0f),
                                .height = etch::Sizing::fixed(38.0f),
                            }) {
                                const float start_a = 0.375f * kTau;
                                const float full_sweep = 0.75f * kTau;
                                const float active_sweep = clamp_f(norm_val, 0.04f, 1.0f) * full_sweep;
                                etch::draw_ring(etch::RingDesc{
                                    .center = {19.0f, 19.0f},
                                    .radius = 14.5f,
                                    .thickness = 3.2f,
                                    .start_angle_rad = start_a,
                                    .sweep_angle_rad = full_sweep,
                                    .color = etch::Color::rgba8(51, 65, 85, 160),
                                    .round_caps = true,
                                });
                                etch::draw_ring(etch::RingDesc{
                                    .center = {19.0f, 19.0f},
                                    .radius = 14.5f,
                                    .thickness = 3.2f,
                                    .start_angle_rad = start_a,
                                    .sweep_angle_rad = active_sweep,
                                    .color = c0,
                                    .color_end = c1,
                                    .glow_color = c0,
                                    .glow_px = 3.0f,
                                    .round_caps = true,
                                });
                                const float tip_a = start_a + active_sweep;
                                const float nx = 19.0f + std::cos(tip_a) * 11.2f;
                                const float ny = 19.0f + std::sin(tip_a) * 11.2f;
                                etch::draw_rect({
                                    .rect = {nx - 3.4f, ny - 1.1f, 6.8f, 2.2f},
                                    .fill = etch::Color::white(),
                                    .radius = etch::Corners::all(1.1f),
                                    .angle_rad = tip_a,
                                });
                                if (icon_id != etch::kInvalidIcon) {
                                    etch::draw_icon(etch::IconDrawDesc{
                                        .icon_id = icon_id,
                                        .rect = {13.0f, 13.0f, 12.0f, 12.0f},
                                        .color = c0,
                                    });
                                }
                            }
                            ETCH_BOX({
                                .axis = etch::Axis::Col,
                                .gap = 1.0f,
                            }) {
                                etch::text(etch::str_cstr(label_title), {
                                    .color = etch::Color::rgb_hex(0xE2E8F0),
                                    .font_size_px = 11.0f,
                                    .wrap = false,
                                });
                                etch::text(etch::str_cstr(label_sub), {
                                    .color = etch::Color::rgb_hex(0x64748B),
                                    .font_size_px = 9.5f,
                                    .wrap = false,
                                });
                            }
                        }
                    };
                    draw_dial(
                        kDialBloom,
                        clamp_f((anim_bloom - 0.10f) / 1.10f, 0.0f, 1.0f),
                        app->ico_bloom,
                        etch::Color::rgb_hex(0x38BDF8),
                        etch::Color::rgb_hex(0xF472B6),
                        "Bloom",
                        "Drag / 2x");
                    draw_dial(
                        kDialExp,
                        clamp_f((anim_exposure - 0.50f) / 1.50f, 0.0f, 1.0f),
                        app->ico_expose,
                        etch::Color::rgb_hex(0xFBBF24),
                        etch::Color::rgb_hex(0x34D399),
                        "Expose",
                        "Drag / 2x");
                }

                // Live HDR ACES Tone-Curve & Bloom Knee Plot (box-local 2D draw API)
                ETCH_BOX({
                    .id = kCurvePlot,
                    .width = etch::Sizing::grow(),
                    .height = etch::Sizing::fixed(52.0f),
                    .background = etch::Color::rgb_hex(0x0B101B),
                    .border_color = etch::Color::rgba8(148, 163, 184, 38),
                    .border_width = 1.0f,
                    .radius = etch::Corners::all(6.0f),
                    .clip_children = true,
                }) {
                    const etch::Rect plot_bounds = etch::element_bounds(kCurvePlot);
                    const float pw = (plot_bounds.w > 40.0f) ? plot_bounds.w : 260.0f;
                    const float ph = (plot_bounds.h > 20.0f) ? plot_bounds.h : 52.0f;
                    const float pad_x = 8.0f;
                    const float pad_y = 6.0f;
                    const float inner_w = pw - 2.0f * pad_x;
                    const float inner_h = ph - 2.0f * pad_y;
                    etch::draw_line(etch::LineDesc{
                        .from = {pad_x, pad_y + 0.5f * inner_h},
                        .to = {pw - pad_x, pad_y + 0.5f * inner_h},
                        .color = etch::Color::rgba8(148, 163, 184, 34),
                        .thickness = 1.0f,
                    });
                    etch::draw_line(etch::LineDesc{
                        .from = {pad_x, ph - pad_y},
                        .to = {pw - pad_x, pad_y},
                        .color = etch::Color::rgba8(148, 163, 184, 42),
                        .thickness = 1.0f,
                    });

                    etch::Vec2 pts[17] = {};
                    for (uint32_t s = 0u; s < 17u; ++s) {
                        const float t = static_cast<float>(s) / 16.0f;
                        const float yv = aces_tonemap(t * anim_exposure * 1.6f);
                        pts[s] = { pad_x + t * inner_w, (ph - pad_y) - yv * inner_h };
                    }
                    etch::draw_area_series({.points = pts, .baseline_y = ph-pad_y, .smooth = true});
                    etch::draw_filled_path({.svg_path_d = etch::str_lit("M 0 8 Q 8 -4 16 8 Q 8 20 0 8 Z"),
                        .offset = {pad_x + 28.0f, 5.0f}, .scale = 0.75f, .fill = etch::Color::rgb_hex(0x38BDF8)});
                    etch::draw_polyline(etch::PolylineDesc{
                        .points = std::span<const etch::Vec2>(pts, 17),
                        .color = etch::Color::rgb_hex(0x38BDF8),
                        .thickness = 1.8f,
                        .glow_color = etch::Color::rgba8(56, 189, 248, 140),
                        .glow_px = 2.0f,
                    });

                    const float knee = clamp_f(anim_bloom * 0.72f, 0.08f, 0.92f);
                    const etch::Vec2 b0 = {pad_x, ph - pad_y};
                    const etch::Vec2 b1 = {pad_x + 0.35f * inner_w, (ph - pad_y) - knee * 0.30f * inner_h};
                    const etch::Vec2 b2 = {pad_x + 0.65f * inner_w, pad_y + (1.0f - knee) * 0.45f * inner_h};
                    const etch::Vec2 b3 = {pw - pad_x, pad_y + 2.0f};
                    etch::draw_bezier(etch::BezierDesc{
                        .p0 = b0,
                        .p1 = b1,
                        .p2 = b2,
                        .p3 = b3,
                        .cubic = true,
                        .color_start = etch::Color::rgb_hex(0xF472B6),
                        .color_end = etch::Color::rgb_hex(0xFBBF24),
                        .thickness = 1.6f,
                    });
                    etch::draw_ring(etch::RingDesc{
                        .center = pts[8],
                        .radius = 3.0f,
                        .thickness = 1.5f,
                        .color = etch::Color::rgb_hex(0x34D399),
                    });
                    etch::draw_text(etch::TextDrawDesc{
                        .text = etch::str_lit("1.0"),
                        .pos = {pad_x + 2.0f, 3.0f},
                        .font_id = etch::kFontMono,
                        .size = 9.5f,
                        .color = etch::Color::rgb_hex(0x94A3B8),
                    });
                    etch::draw_text(etch::TextDrawDesc{
                        .text = etch::str_lit("ACES Filmic"),
                        .pos = {pw - pad_x, ph - 14.0f},
                        .font_id = etch::kFontSans,
                        .size = 9.5f,
                        .color = etch::Color::rgb_hex(0x7DD3FC),
                        .align_x = etch::AlignX::Right,
                    });
                }

                draw_button(
                    kBtnWireGlow,
                    app->wire_glow_enabled ? "Wire Glow: Neon" : "Wire Glow: Flat",
                    app->wire_glow_enabled ? etch::Color::rgb_hex(0x1D4ED8) : etch::Color::rgb_hex(0x1E293B),
                    etch::Color::rgba8(148, 163, 184, 58),
                    10.0f,
                    5.0f,
                    12.0f,
                    etch::Sizing::grow());
            }

            const bool jump_hov = etch::hovered(kSpanJumpMid);
            if (jump_hov) {
                etch::set_cursor(etch::Cursor::Hand);
            }
            etch::text_spans({
                etch::TextSpan{
                    .text = etch::str_lit("v" ETCH_VERSION_STRING " "),
                    .color = etch::Color::rgb_hex(0xF8FAFC),
                    .font_id = etch::kFontBold,
                },
                etch::TextSpan{
                    .text = etch::str_lit("Mono+Slug"),
                    .color = etch::Color::rgb_hex(0x38BDF8),
                    .background = etch::Color::rgba8(14, 116, 144, 64),
                    .border_color = etch::Color::rgba8(56, 189, 248, 110),
                    .font_size_px = 10.5f,
                    .font_id = etch::kFontMono,
                    .padding_x = 5.0f,
                    .bg_radius = 4.0f,
                },
                etch::TextSpan{
                    .text = etch::str_lit(" \xE4\xB8\xAD\xE6\x96\x87\xE2\x9C\x93 "),
                    .color = etch::Color::rgb_hex(0x94A3B8),
                },
                etch::TextSpan{
                    .text = etch::str_lit("[Jump -> #500k]"),
                    .color = jump_hov ? etch::Color::rgb_hex(0x7DD3FC) : etch::Color::rgb_hex(0xF472B6),
                    .font_size_px = 11.0f,
                    .font_id = etch::kFontMono,
                    .underline = true,
                    .id = kSpanJumpMid,
                },
            }, {
                .font_size_px = 11.5f,
                .wrap = true,
                .id = etch::id("studio_telemetry_selection"),
                .selectable = true,
            });

            ETCH_BOX({.axis=etch::Axis::Col,.width=etch::Sizing::grow(),.height=etch::Sizing::fit(),.padding=etch::Padding::xy(8,6),.gap=3,.background=etch::Color::rgb_hex(0x142032)}) {
            char trace_detail[160] = {};
            const uint32_t selected_trace = app->trace_selected_row;
            std::snprintf(trace_detail,sizeof(trace_detail),"Selected #%07u   %s   |   %.2f us   |   %u prims",selected_trace,kTracePassNames[selected_trace%6],18.0+static_cast<double>((selected_trace*37u)%kTraceRowCount)*.0001,920u+(selected_trace*13u)%kTraceRowCount);
            ETCH_BOX({.id=etch::id("studio_v22_trace_detail"),.width=etch::Sizing::grow(),.height=etch::Sizing::fixed(24),.padding=etch::Padding::xy(8,5),.background=etch::Color::rgb_hex(0x14283D),.radius=etch::Corners::all(4)}) {
                etch::text(etch::str_cstr(trace_detail),{.font_size_px=11,.wrap=false,.ellipsis=true});
            }
                etch::text(etch::str_lit("Inspector label with a deliberately long constrained title that demonstrates trailing ellipsis in this panel"),{.font_size_px=11,.wrap=false,.id=etch::id("studio_v21_label"),.selectable=true,.ellipsis=true});
                etch::text(etch::str_lit("User שלום 42 End  🚀✨"),{.font_size_px=15,.wrap=false});
            }

            ETCH_COLLAPSIBLE({.id = etch::id("studio_pass_collapsible"),
                .title = etch::str_lit("Pass Palette (Drag -> Canvas)"), .default_open = true,
                .header_height_px = 22, .body_padding = etch::Padding::xy(0, 2)}) {
                ETCH_BOX({
                    .axis = etch::Axis::Row,
                    .width = etch::Sizing::grow(),
                    .height = etch::Sizing::fit(),
                    .gap = 6.0f,
                }) {
                    const char* pal_labels[3] = { "Bloom", "Sharpen", "Vignette" };
                    for (uint32_t pi = 0u; pi < 3u; ++pi) {
                        const uint32_t pal_id = etch::id_index(etch::id("studio_palette"), pi);
                        if (etch::dragging(pal_id, 4.0f) && !etch::is_dragging_payload()) {
                            etch::begin_drag_payload(
                                pal_id,
                                kDragKindPass,
                                etch::str_cstr(kPalettePasses[pi].title),
                                &pi,
                                sizeof(pi),
                                0u,
                                pi);
                        }
                        const bool pal_hov = etch::hovered(pal_id);
                        if (pal_hov) {
                            etch::set_cursor(etch::Cursor::Hand);
                        }
                        ETCH_BOX({
                            .id = pal_id,
                            .width = etch::Sizing::grow(),
                            .height = etch::Sizing::fit(),
                            .padding = etch::Padding::xy(6.0f, 5.0f),
                            .align_x = etch::AlignX::Center,
                            .align_y = etch::AlignY::Center,
                            .background = pal_hov ? etch::Color::rgb_hex(0x1E293B) : etch::Color::rgb_hex(0x111726),
                            .border_color = pal_hov ? kPalettePasses[pi].accent : etch::Color::rgba8(148, 163, 184, 42),
                            .border_width = 1.0f,
                            .radius = etch::Corners::all(6.0f),
                        }) {
                            etch::text(etch::str_cstr(pal_labels[pi]), {
                                .color = etch::Color::rgb_hex(0xE2E8F0),
                                .font_size_px = 11.0f,
                                .wrap = false,
                            });
                        }
                    }
                }

            }

            etch::text(etch::str_lit("Render-Graph Nodes (Click to Focus)"), {
                .color = etch::Color::rgb_hex(0x94A3B8),
                .font_size_px = 12.0f,
                .wrap = false,
            });

            // Inertial scrollable node directory
            ETCH_BOX({
                .id = etch::id("studio_node_scroll"),
                .axis = etch::Axis::Col,
                .width = etch::Sizing::grow(),
                .height = etch::Sizing::grow(),
                .padding = etch::Padding::xy(4.0f, 4.0f),
                .gap = 6.0f,
                .background = etch::Color::rgb_hex(0x0B101A),
                .border_color = etch::Color::rgba8(148, 163, 184, 32),
                .border_width = 1.0f,
                .radius = etch::Corners::all(8.0f),
                .clip_children = true,
                .scroll_y = true,
                .show_scrollbar = true,
            }) {
                for (uint32_t i = 0u; i < app->node_count; ++i) {
                    const uint32_t item_id = etch::id_index(etch::id("studio_node_dir"), i);
                    if (etch::clicked(item_id)) {
                        app->selected_node = i;
                        app->node_selected_mask = (1u << i);
                        const etch::CanvasState cur_cs = etch::canvas_state(kCanvas);
                        if (cur_cs.viewport.w > 20.0f && cur_cs.viewport.h > 20.0f) {
                            const etch::Vec2 center_world = {
                                app->nodes[i].pos.x + 0.5f * app->nodes[i].width_world,
                                app->nodes[i].pos.y + 44.0f,
                            };
                            const etch::Vec2 target_pan = {
                                0.5f * cur_cs.viewport.w - center_world.x * cur_cs.zoom,
                                0.5f * cur_cs.viewport.h - center_world.y * cur_cs.zoom,
                            };
                            etch::set_canvas_view(kCanvas, target_pan, cur_cs.zoom);
                        }
                    }
                    const bool is_sel = (app->node_selected_mask & (1u << i)) != 0u ||
                        (app->node_selected_mask == 0u && app->selected_node == i);
                    const bool is_hov = etch::hovered(item_id);
                    ETCH_BOX({
                        .id = item_id,
                        .axis = etch::Axis::Row,
                        .width = etch::Sizing::grow(),
                        .height = etch::Sizing::fit(),
                        .padding = etch::Padding::xy(10.0f, 8.0f),
                        .gap = 8.0f,
                        .align_y = etch::AlignY::Center,
                        .background = is_sel
                            ? etch::Color::rgb_hex(0x1A2942)
                            : (is_hov ? etch::Color::rgb_hex(0x162033) : etch::Color::rgb_hex(0x111726)),
                        .border_color = is_sel ? app->nodes[i].accent : etch::Color::rgba8(148, 163, 184, 28),
                        .border_width = 1.0f,
                        .radius = etch::Corners::all(7.0f),
                    }) {
                        ETCH_BOX({
                            .width = etch::Sizing::fixed(8.0f),
                            .height = etch::Sizing::fixed(24.0f),
                            .background = app->nodes[i].accent,
                            .radius = etch::Corners::all(4.0f),
                        }) {}
                        ETCH_BOX({
                            .axis = etch::Axis::Col,
                            .width = etch::Sizing::grow(),
                            .gap = 2.0f,
                        }) {
                            etch::text(etch::str_cstr(app->nodes[i].title), {
                                .color = etch::Color::rgb_hex(0xF1F5F9),
                                .font_size_px = 12.5f,
                                .wrap = false,
                            });
                            etch::text(etch::str_cstr(app->nodes[i].subtitle), {
                                .color = etch::Color::rgb_hex(0x94A3B8),
                                .font_size_px = 11.0f,
                                .wrap = false,
                            });
                        }
                    }
                }
            }
        }

        }
        if (pane == kPaneCanvas) {
        // Pane 2: Infinite 2D Node-Graph Canvas
        const etch::CanvasState cs = etch::begin_canvas(etch::CanvasDesc{
            .id = kCanvas,
            .width = etch::Sizing::grow(),
            .height = etch::Sizing::grow(),
            .default_pan = {36.0f, 28.0f},
            .default_zoom = 0.5f,
            .min_zoom = 0.20f,
            .max_zoom = 6.0f,
            .grid_step_world = 24.0f,
            .major_grid_subdiv = 4u,
            .background = etch::Color::rgb_hex(0x090D16),
            .minor_grid_color = etch::Color::rgba8(148, 163, 184, 34),
            .major_grid_color = etch::Color::rgba8(56, 189, 248, 30),
            .border_color = etch::os_drag_hovering(kCanvas) ? etch::Color::rgb_hex(0x34D399) : etch::Color::rgba8(148, 163, 184, 44),
            .border_width = 1.0f,
            .radius = etch::Corners::all(10.0f),
            .show_grid = true,
            .interactive = true,
            .marquee_select = true,
        });

        if (cs.marquee_finished) {
            uint32_t mask = 0u;
            uint32_t first_hit = app->selected_node;
            for (uint32_t i = 0u; i < app->node_count; ++i) {
                const etch::Rect nw = {
                    app->nodes[i].pos.x,
                    app->nodes[i].pos.y,
                    app->nodes[i].width_world,
                    (i == 4u) ? 214.0f : 92.0f,
                };
                if (nw.intersects(cs.marquee_world)) {
                    if (mask == 0u) {
                        first_hit = i;
                    }
                    mask |= (1u << i);
                }
            }
            app->node_selected_mask = mask;
            if (mask != 0u) {
                app->selected_node = first_hit;
            }
        }

        const float wire_glow = app->wire_glow_enabled ? 5.5f : 0.0f;
        const auto emit_link = [&](uint32_t src_idx, uint32_t dst_idx) noexcept {
            if (src_idx >= app->node_count || dst_idx >= app->node_count || src_idx == dst_idx) {
                return;
            }
            const GraphNode& src = app->nodes[src_idx];
            const GraphNode& dst = app->nodes[dst_idx];
            etch::canvas_wire(etch::CanvasWireDesc{
                .from_world = { src.pos.x + src.width_world, src.pos.y + kNodePortOffsetY },
                .to_world = { dst.pos.x, dst.pos.y + kNodePortOffsetY },
                .color_start = src.accent,
                .color_end = dst.accent,
                .glow_color = etch::Color::lerp(src.accent, dst.accent, 0.5f),
                .thickness_world = 2.4f,
                .glow_world = wire_glow,
                .tangent_bias = 0.48f,
                .z_index = 1,
            });
        };

        for (uint32_t i = 0u; i < app->node_count; ++i) {
            if (app->nodes[i].input_from < app->node_count) {
                emit_link(app->nodes[i].input_from, i);
            }
            if (app->nodes[i].aux_from < app->node_count) {
                emit_link(app->nodes[i].aux_from, i);
            }
        }

        for (uint32_t i = 0u; i < app->node_count; ++i) {
            GraphNode& node = app->nodes[i];
            const uint32_t node_id = etch::id_index(etch::id("studio_node"), i);
            const uint32_t in_port_id = etch::id_index(etch::id("studio_port_in"), i);
            const uint32_t out_port_id = etch::id_index(etch::id("studio_port_out"), i);

            if (etch::clicked(out_port_id)) {
                app->pending_wire_src = (app->pending_wire_src == i) ? UINT32_MAX : i;
                app->selected_node = i;
                app->node_selected_mask = (1u << i);
            }
            if (etch::clicked(in_port_id)) {
                if (app->pending_wire_src < app->node_count && app->pending_wire_src != i) {
                    node.input_from = app->pending_wire_src;
                    app->pending_wire_src = UINT32_MAX;
                }
                app->selected_node = i;
                app->node_selected_mask = (1u << i);
            }

            const bool is_sel = (app->node_selected_mask & (1u << i)) != 0u ||
                (app->node_selected_mask == 0u && app->selected_node == i);
            const etch::CanvasNodeResult nr = etch::begin_canvas_node(etch::CanvasNodeDesc{
                .id = node_id,
                .world_pos = &node.pos,
                .width_world = etch::Sizing::fixed(node.width_world),
                .height_world = etch::Sizing::fit(),
                .padding_world = etch::Padding::all(0.0f),
                .gap_world = 0.0f,
                .axis = etch::Axis::Col,
                .background = etch::Color::rgb_hex(0x11192B),
                .background_end = etch::Color::rgb_hex(0x0D1322),
                .border_color = is_sel ? node.accent : etch::Color::rgba8(148, 163, 184, 58),
                .border_width_world = is_sel ? 1.6f : 1.0f,
                .radius_world = etch::Corners::all(10.0f),
                .shadow_color = etch::Color::rgba8(0, 0, 0, 145),
                .shadow_blur_world = 14.0f,
                .shadow_offset_world = {0.0f, 6.0f},
                .z_index = is_sel ? int16_t(4) : int16_t(2),
                .draggable = true,
            });
            if (nr.pressed) {
                app->selected_node = i;
                if ((app->node_selected_mask & (1u << i)) == 0u) {
                    app->node_selected_mask = (1u << i);
                }
            }
            if (nr.dragging) {
                app->selected_node = i;
                if ((app->node_selected_mask & (1u << i)) == 0u) {
                    app->node_selected_mask = (1u << i);
                } else if (nr.drag_delta_world.x != 0.0f || nr.drag_delta_world.y != 0.0f) {
                    for (uint32_t j = 0u; j < app->node_count; ++j) {
                        if (j != i && (app->node_selected_mask & (1u << j)) != 0u) {
                            app->nodes[j].pos = app->nodes[j].pos + nr.drag_delta_world;
                        }
                    }
                }
            }
            if (nr.clicked) {
                app->selected_node = i;
                app->node_selected_mask = (1u << i);
            }

            // Header bar: 28 world units tall
            ETCH_BOX({
                .axis = etch::Axis::Row,
                .width = etch::Sizing::grow(),
                .height = etch::Sizing::fixed(28.0f),
                .padding = etch::Padding::xy(10.0f, 0.0f),
                .gap = 6.0f,
                .align_y = etch::AlignY::Center,
                .background = etch::Color::lerp(etch::Color::rgb_hex(0x182238), node.accent, 0.22f),
                .radius = {10.0f, 10.0f, 0.0f, 0.0f},
            }) {
                if (app->ico_node != etch::kInvalidIcon) {
                    etch::icon(etch::IconDesc{
                        .icon_id = app->ico_node,
                        .size_px = 12.0f,
                        .color = node.accent,
                    });
                } else {
                    ETCH_BOX({
                        .width = etch::Sizing::fixed(8.0f),
                        .height = etch::Sizing::fixed(8.0f),
                        .background = node.accent,
                        .radius = etch::Corners::all(4.0f),
                    }) {}
                }
                etch::text(etch::str_cstr(node.title), {
                    .color = etch::Color::rgb_hex(0xF8FAFC),
                    .font_size_px = 12.5f,
                    .wrap = false,
                });
            }

            // Port strip row: 32 world units tall -> center aligns at kNodePortOffsetY (44.0)
            ETCH_BOX({
                .axis = etch::Axis::Row,
                .width = etch::Sizing::grow(),
                .height = etch::Sizing::fixed(32.0f),
                .padding = etch::Padding::xy(6.0f, 0.0f),
                .align_y = etch::AlignY::Center,
            }) {
                if (i > 1u) {
                    const bool has_wire = (node.input_from < app->node_count);
                    ETCH_BOX({
                        .id = in_port_id,
                        .padding = etch::Padding::xy(7.0f, 3.0f),
                        .background = has_wire ? etch::Color::rgb_hex(0x1E293B) : etch::Color::rgb_hex(0x0F172A),
                        .border_color = node.accent,
                        .border_width = 1.0f,
                        .radius = etch::Corners::all(5.0f),
                    }) {
                        etch::text(etch::str_lit("IN"), {
                            .color = etch::Color::rgb_hex(0xE2E8F0),
                            .font_size_px = 10.5f,
                            .wrap = false,
                        });
                    }
                }
                ETCH_BOX({ .width = etch::Sizing::grow() }) {}
                if (i != 4u) {
                    const bool armed = (app->pending_wire_src == i);
                    ETCH_BOX({
                        .id = out_port_id,
                        .padding = etch::Padding::xy(7.0f, 3.0f),
                        .background = armed ? etch::Color::rgb_hex(0x0284C7) : etch::Color::rgb_hex(0x1E293B),
                        .border_color = armed ? etch::Color::white() : node.accent,
                        .border_width = 1.0f,
                        .radius = etch::Corners::all(5.0f),
                    }) {
                        etch::text(etch::str_lit("OUT"), {
                            .color = etch::Color::rgb_hex(0xF8FAFC),
                            .font_size_px = 10.5f,
                            .wrap = false,
                        });
                    }
                }
            }

            // Node detail body
            ETCH_BOX({
                .axis = etch::Axis::Col,
                .width = etch::Sizing::grow(),
                .height = etch::Sizing::fit(),
                .padding = etch::Padding{ 10.0f, 10.0f, 2.0f, 10.0f },
                .gap = 6.0f,
            }) {
                etch::text(etch::str_cstr(node.subtitle), {
                    .color = etch::Color::rgb_hex(0x94A3B8),
                    .font_size_px = 11.0f,
                    .wrap = false,
                });
                if (i == 4u && app->preview_tex) {
                    etch::image(etch::ImageDesc{
                        .id = etch::id("studio_viewport_img"),
                        .texture = app->preview_tex,
                        .width = etch::Sizing::fixed(208.0f),
                        .height = etch::Sizing::fixed(122.0f),
                        .radius = etch::Corners::all(7.0f),
                        .tint = etch::Color::white(),
                        .border_color = etch::Color::rgba8(52, 211, 153, 140),
                        .border_width = 1.0f,
                    });
                }
            }

            etch::end_canvas_node();
        }

        etch::end_canvas();

        }
        if (pane == kPaneTrace) {
        // Pane 3: Resizable, sortable virtual telemetry table
        ETCH_BOX({
            .axis = etch::Axis::Col,
            .width = etch::Sizing::grow(),
            .height = etch::Sizing::grow(),
            .padding = etch::Padding::all(10.0f),
            .gap = 6.0f,
            .background = etch::Color::rgb_hex(0x0F1522),
            .border_color = etch::Color::rgba8(148, 163, 184, 38),
            .border_width = 1.0f,
            .radius = etch::Corners::all(10.0f),
        }) {
            ETCH_BOX({
                .axis = etch::Axis::Row,
                .width = etch::Sizing::grow(),
                .height = etch::Sizing::fit(),
                .gap = 8.0f,
                .align_y = etch::AlignY::Center,
            }) {
                char vhdr[96] = {};
                std::snprintf(
                    vhdr,
                    sizeof(vhdr),
                    "GPU Trace Table  [%u..%u) / 1,000,000 rows",
                    app->last_trace_slice.first_index,
                    app->last_trace_slice.end_index);
                etch::text(etch::str_cstr(vhdr), {
                    .color = etch::Color::rgb_hex(0xCBD5E1),
                    .font_size_px = 12.5f,
                    .wrap = false,
                });
                ETCH_BOX({ .width = etch::Sizing::grow() }) {}
                draw_button(kBtnRow0, "Row 0", etch::Color::rgb_hex(0x1E293B), etch::Color::rgba8(148, 163, 184, 52), 8.0f, 3.0f, 11.5f);
                draw_button(kBtnRow500k, "500k", etch::Color::rgb_hex(0x1E293B), etch::Color::rgba8(148, 163, 184, 52), 8.0f, 3.0f, 11.5f);
                draw_button(kBtnRowEnd, "999,990", etch::Color::rgb_hex(0x1E293B), etch::Color::rgba8(148, 163, 184, 52), 8.0f, 3.0f, 11.5f);
            }

            const etch::TableColumnDesc columns[] = {
                { .title = etch::str_lit("#Row"), .width = 100 },
                { .title = etch::str_lit("Render Pass"), .sizing = etch::TableColumnSizing::Stretch, .width = 2 },
                { .title = etch::str_lit("GPU Time"), .width = 110 },
                { .title = etch::str_lit("Prims"), .width = 90 },
                { .title = etch::str_lit("Pipeline"), .sizing = etch::TableColumnSizing::Stretch, .width = 1 },
            };
            const auto table = etch::begin_table({ .id = kTraceList, .columns = columns, .item_count = kTraceRowCount,
                .row_height_px = kTraceRowHeight, .padding = etch::Padding::xy(6,4) });
            app->last_trace_slice = table.slice;
            app->trace_sort_column = table.sort_column; app->trace_sort_descending = table.sort_descending;
            app->trace_sort_changed = table.sort_changed;
            uint32_t rank = 0;
            while (etch::table_next_row(&rank)) {
                // Each metric is a bijective arithmetic permutation: inversion of rank is O(1).
                const uint32_t ordered = table.sort_descending ? kTraceRowCount-1-rank : rank;
                const uint32_t row = table.sort_column == 2 ? static_cast<uint32_t>((static_cast<uint64_t>(ordered)*972973u)%kTraceRowCount) :
                    (table.sort_column == 3 ? static_cast<uint32_t>((static_cast<uint64_t>(ordered)*230769u)%kTraceRowCount) : ordered);
                if (rank == table.slice.first_index) app->trace_first_row = row;
                if (etch::clicked(etch::id_index(kTraceList,0x100000u+rank))) { app->trace_selected_row = row; etch::request_redraw(app->ctx); }
                char cells[5][64] = {};
                std::snprintf(cells[0],sizeof(cells[0]),"#%07u",row);
                std::snprintf(cells[1],sizeof(cells[1]),"%s",kTracePassNames[row%6]);
                std::snprintf(cells[2],sizeof(cells[2]),"%.2f us",18.0+static_cast<double>((row*37u)%kTraceRowCount)*.0001);
                std::snprintf(cells[3],sizeof(cells[3]),"%u",920u+(row*13u)%kTraceRowCount);
                std::snprintf(cells[4],sizeof(cells[4]),"GPU / Slug");
                uint32_t col = 0;
                while (etch::table_next_column()) {
                    if (etch::clicked(etch::id_index(etch::id_index(kTraceList,0x100000u+rank),col))) { app->trace_selected_row = row; etch::request_redraw(app->ctx); }
                    etch::text(etch::Str{cells[col++]},{ .font_size_px = 11.5f, .font_id = etch::kFontMono, .wrap = false });
                }
            }
            etch::end_table();
        }

        }
        }
        etch::end_dock_space();
    }

    // Floating Dual-Kawase Frosted-Glass Preset Popover (NO clip_children on floating element)
    if (app->preset_popover_open) {
        etch::begin_focus_scope(kPresetPopover, true);
        ETCH_BOX({
            .id = kPresetPopover,
            .axis = etch::Axis::Col,
            .width = etch::Sizing::fixed(264.0f),
            .height = etch::Sizing::fit(),
            .padding = etch::Padding::all(10.0f),
            .gap = 6.0f,
            .background = etch::Color::rgba8(15, 23, 42, 172),
            .border_color = etch::Color::rgba8(56, 189, 248, 140),
            .border_width = 1.0f,
            .radius = etch::Corners::all(10.0f),
            .shadow_color = etch::Color::rgba8(0, 0, 0, 150),
            .shadow_blur = 20.0f,
            .shadow_offset = {0.0f, 8.0f},
            .attach_to = etch::AttachTo::Element,
            .attach_element_id = kBtnPresets,
            .anchor_element = etch::Anchor::TopLeft,
            .anchor_parent = etch::Anchor::BottomLeft,
            .floating_offset = {0.0f, 6.0f},
            .z_index = 40,
            .backdrop_blur = 14.0f,
            .clamp_to_viewport = true,
        }) {
            etch::text(etch::str_lit("Dual-Kawase Shader Presets"), {
                .color = etch::Color::rgb_hex(0xF8FAFC),
                .font_size_px = 12.5f,
                .wrap = false,
            });
            for (uint32_t p = 0u; p < 3u; ++p) {
                const uint32_t pid = etch::id_index(etch::id("studio_preset_item"), p);
                if (etch::clicked(pid)) {
                    app->selected_preset = p;
                    app->target_bloom = kPresets[p].bloom;
                    app->target_exposure = kPresets[p].exposure;
                    app->preset_popover_open = false;
                }
                const bool active_p = (app->selected_preset == p);
                const bool hov_p = etch::hovered(pid);
                ETCH_BOX({
                    .id = pid,
                    .axis = etch::Axis::Col,
                    .width = etch::Sizing::grow(),
                    .padding = etch::Padding::xy(10.0f, 7.0f),
                    .gap = 2.0f,
                    .background = active_p
                        ? etch::Color::rgba8(14, 165, 233, 84)
                        : (hov_p ? etch::Color::rgba8(51, 65, 85, 110) : etch::Color::rgba8(15, 23, 42, 95)),
                    .border_color = active_p ? kPresets[p].primary : etch::Color::rgba8(148, 163, 184, 34),
                    .border_width = 1.0f,
                    .radius = etch::Corners::all(7.0f),
                    .focusable = true,
                }) {
                    etch::text(etch::str_cstr(kPresets[p].name), {
                        .color = etch::Color::rgb_hex(0xF8FAFC),
                        .font_size_px = 12.5f,
                        .wrap = false,
                    });
                    etch::text(etch::str_cstr(kPresets[p].desc), {
                        .color = etch::Color::rgb_hex(0x94A3B8),
                        .font_size_px = 11.0f,
                        .wrap = false,
                    });
                }
            }
        }
        etch::end_focus_scope();
    }

    // Floating Right-Click Canvas Context Menu (Frame-0 clamp_to_viewport)
    if (app->ctx_menu_open) {
        ETCH_BOX({
            .id = kCtxMenu,
            .axis = etch::Axis::Col,
            .width = etch::Sizing::fixed(210.0f),
            .height = etch::Sizing::fit(),
            .padding = etch::Padding::all(8.0f),
            .gap = 4.0f,
            .background = etch::Color::rgba8(15, 23, 42, 186),
            .border_color = etch::Color::rgba8(56, 189, 248, 130),
            .border_width = 1.0f,
            .radius = etch::Corners::all(9.0f),
            .shadow_color = etch::Color::rgba8(0, 0, 0, 160),
            .shadow_blur = 18.0f,
            .shadow_offset = {0.0f, 8.0f},
            .attach_to = etch::AttachTo::Root,
            .anchor_element = etch::Anchor::TopLeft,
            .anchor_parent = etch::Anchor::TopLeft,
            .floating_offset = app->ctx_menu_pos,
            .z_index = 45,
            .backdrop_blur = 14.0f,
            .clamp_to_viewport = true,
        }) {
            ETCH_BOX({
                .axis = etch::Axis::Row,
                .padding = etch::Padding::xy(6.0f, 2.0f),
                .gap = 6.0f,
                .align_y = etch::AlignY::Center,
            }) {
                if (app->ico_menu != etch::kInvalidIcon) {
                    etch::icon(etch::IconDesc{
                        .icon_id = app->ico_menu,
                        .size_px = 12.0f,
                        .color = etch::Color::rgb_hex(0x38BDF8),
                    });
                }
                etch::text(etch::str_lit("Canvas Actions"), {
                    .color = etch::Color::rgb_hex(0x94A3B8),
                    .font_size_px = 11.0f,
                    .wrap = false,
                });
            }
            static constexpr const char* kCtxLabels[3] = {
                "Reset Canvas View",
                "Toggle Neon Wire Glow",
                "Jump Trace -> #500k",
            };
            for (uint32_t m = 0u; m < 3u; ++m) {
                const uint32_t mid = etch::id_index(etch::id("studio_ctx_item"), m);
                if (etch::clicked(mid)) {
                    if (m == 0u) {
                        etch::set_canvas_view(kCanvas, etch::Vec2{36.0f, 28.0f}, 1.0f);
                    } else if (m == 1u) {
                        app->wire_glow_enabled = !app->wire_glow_enabled;
                    } else {
                        etch::virtual_list_scroll_to_row(kTraceList, 500000u, kTraceRowHeight, etch::AlignY::Top);
                    }
                    app->ctx_menu_open = false;
                }
                const bool mhov = etch::hovered(mid);
                ETCH_BOX({
                    .id = mid,
                    .width = etch::Sizing::grow(),
                    .padding = etch::Padding::xy(9.0f, 6.0f),
                    .background = mhov ? etch::Color::rgba8(56, 189, 248, 56) : etch::Color::rgba8(15, 23, 42, 70),
                    .radius = etch::Corners::all(6.0f),
                }) {
                    etch::text(etch::str_cstr(kCtxLabels[m]), {
                        .color = etch::Color::rgb_hex(0xF1F5F9),
                        .font_size_px = 12.0f,
                        .wrap = false,
                    });
                }
            }
        }
    }

    // Floating Ctrl+K Command Palette Overlay (Search with Undo/Redo + Frosted Glass)
    if (app->cmd_palette_open) {
        etch::begin_focus_scope(kCmdPalette, true);
        ETCH_BOX({
            .id = kCmdPalette,
            .axis = etch::Axis::Col,
            .width = etch::Sizing::fixed(420.0f),
            .height = etch::Sizing::fit(),
            .padding = etch::Padding::all(12.0f),
            .gap = 8.0f,
            .background = etch::Color::rgba8(15, 23, 42, 198),
            .border_color = etch::Color::rgba8(45, 212, 191, 155),
            .border_width = 1.2f,
            .radius = etch::Corners::all(11.0f),
            .shadow_color = etch::Color::rgba8(0, 0, 0, 175),
            .shadow_blur = 24.0f,
            .shadow_offset = {0.0f, 10.0f},
            .attach_to = etch::AttachTo::Root,
            .anchor_element = etch::Anchor::TopCenter,
            .anchor_parent = etch::Anchor::TopCenter,
            .floating_offset = {0.0f, 84.0f},
            .z_index = 55,
            .backdrop_blur = 16.0f,
            .clamp_to_viewport = true,
        }) {
            ETCH_BOX({
                .axis = etch::Axis::Row,
                .width = etch::Sizing::grow(),
                .gap = 8.0f,
                .align_y = etch::AlignY::Center,
            }) {
                if (app->ico_search != etch::kInvalidIcon) {
                    etch::icon(etch::IconDesc{
                        .icon_id = app->ico_search,
                        .size_px = 14.0f,
                        .color = etch::Color::rgb_hex(0x2DD4BF),
                    });
                }
                app->cmd_filter_res = etch::text_input(etch::TextInputDesc{
                    .id = kCmdInput,
                    .buffer = app->cmd_filter_buf,
                    .capacity = sizeof(app->cmd_filter_buf),
                    .placeholder = etch::str_lit("Search commands (supports Ctrl+Z / Ctrl+Y)..."),
                    .width = etch::Sizing::grow(),
                    .height = etch::Sizing::fixed(34.0f),
                    .padding = etch::Padding::xy(10.0f, 7.0f),
                    .font_size_px = 13.0f,
                });
            }
            static constexpr const char* kPaletteCmds[4] = {
                "Preset: Cyberpunk Bloom",
                "Preset: Solar Cinema",
                "Preset: Ice Lab",
                "Toggle Neon Wire Glow",
            };
            for (uint32_t c = 0u; c < 4u; ++c) {
                if (!contains_ascii_ci(kPaletteCmds[c], app->cmd_filter_buf)) {
                    continue;
                }
                const uint32_t cid = etch::id_index(etch::id("studio_palette_cmd"), c);
                if (etch::clicked(cid)) {
                    if (c < 3u) {
                        app->selected_preset = c;
                        app->target_bloom = kPresets[c].bloom;
                        app->target_exposure = kPresets[c].exposure;
                    } else {
                        app->wire_glow_enabled = !app->wire_glow_enabled;
                    }
                    app->cmd_palette_open = false;
                }
                const bool chov = etch::hovered(cid);
                ETCH_BOX({
                    .id = cid,
                    .width = etch::Sizing::grow(),
                    .padding = etch::Padding::xy(10.0f, 6.0f),
                    .background = chov ? etch::Color::rgba8(45, 212, 191, 56) : etch::Color::rgba8(15, 23, 42, 85),
                    .border_color = chov ? etch::Color::rgba8(45, 212, 191, 130) : etch::Color::rgba8(148, 163, 184, 28),
                    .border_width = 1.0f,
                    .radius = etch::Corners::all(6.0f),
                    .focusable = true,
                }) {
                    etch::text(etch::str_cstr(kPaletteCmds[c]), {
                        .color = etch::Color::rgb_hex(0xF8FAFC),
                        .font_size_px = 12.5f,
                        .wrap = false,
                    });
                }
            }
        }
        etch::end_focus_scope();
    }
}

void step_headless_frame(
    StudioApp* app,
    uint32_t w,
    uint32_t h,
    float dt_seconds,
    etch::Vec2 pointer_pos = {-1.0f, -1.0f},
    bool pointer_down = false,
    bool right_down = false,
    uint8_t pointer_clicks = 0u,
    uint16_t key_mods = etch::Mod_None,
    uint32_t key_pressed_code = 0u) noexcept
{
    const etch::FrameStats prev = etch::frame_stats(app->ctx);
    etch::begin_frame(app->ctx, etch::FrameDesc{
        .viewport_w = w,
        .viewport_h = h,
        .dpi_scale = 1.0f,
        .dt_seconds = dt_seconds,
        .pointer_pos = pointer_pos,
        .pointer_down = pointer_down,
        .right_pointer_down = right_down,
        .pointer_clicks = pointer_clicks,
        .key_mods = key_mods,
        .key_pressed_code = key_pressed_code,
    });
    build_studio_ui(app, prev);
    etch::end_frame(app->ctx);
}

void build_preview_ui(StudioApp* app) noexcept {
    ETCH_BOX({.id = etch::id("tear_preview"), .width = etch::Sizing::grow(), .height = etch::Sizing::grow(), .background = etch::Color::rgb_hex(0x111827), .focusable = true}) {
        etch::text(etch::str_lit("Etch Studio " ETCH_VERSION_STRING " — Preview"));
        etch::image({.texture = app->preview_tex, .width = etch::Sizing::fixed(256), .height = etch::Sizing::fixed(256)});
    }
}

void render_window_frame(StudioApp* app, float dt_seconds) noexcept {
    if (app->rendering || etch::frame_in_progress(app->ctx)) return;
    app->rendering = true;
    int px_w = 1440;
    int px_h = 900;
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
    if (SDL_GetMouseFocus() == app->window) {
        float mx = 0.0f;
        float my = 0.0f;
        const SDL_MouseButtonFlags mb = SDL_GetMouseState(&mx, &my);
        float density = SDL_GetWindowPixelDensity(app->window);
        if (density <= 0.0f) {
            density = 1.0f;
        }
        const float pscale = density / dpi;
        pointer = { mx * pscale, my * pscale };
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
    build_studio_ui(app, prev_stats);
    etch::end_frame(app->ctx);

    etch::render_window(app->ctx, etch::Color::rgb_hex(0x090C12));
    if (app->tear_off_window) {
        if (SDL_GetWindowFlags(app->tear_off_window) & SDL_WINDOW_HIDDEN) {
            etch::release_window(app->ctx, app->tear_off_window);
            SDL_DestroyWindow(app->tear_off_window); app->tear_off_window = nullptr;
        } else if (etch::claimed_window_count(app->ctx) < 2 || etch::needs_window_redraw(app->ctx, app->tear_off_window)) {
            etch::begin_window_frame(app->ctx, app->tear_off_window, dt_seconds);
            build_preview_ui(app);
            etch::end_window_frame(app->ctx);
            etch::render_window(app->ctx, etch::Color::rgb_hex(0x090C12), app->tear_off_window);
        }
    }
    app->rendering = false;
}

bool SDLCALL window_event_watch(void* userdata, SDL_Event* event) noexcept {
    StudioApp* app = static_cast<StudioApp*>(userdata);
    if (!app || !event || etch::frame_in_progress(app->ctx) || !app->window || app->rendering) {
        return true;
    }
    if ((event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
         event->type == SDL_EVENT_WINDOW_EXPOSED) &&
        event->window.windowID == SDL_GetWindowID(app->window))
    {
        etch::request_window_redraw(app->ctx, app->window);
        render_window_frame(app, 0.0f);
    }
    return true;
}

int run_headless(const char* out_png_path) noexcept {
    etch::Context* ctx = etch::create_context(etch::ContextDesc{
        .headless = true,
    });
    if (!ctx) {
        std::fprintf(stderr, "Etch Studio headless error: create_context failed (%s)\n", SDL_GetError());
        return 1;
    }

    StudioApp app = { .ctx = ctx };
    init_studio_icons(&app);
    etch::set_window_chrome(etch::WindowChromeDesc{
        .enabled = true,
        .resizable = true,
        .border_thickness = 6.0f,
        .corner_size = 12.0f,
    });
    constexpr uint32_t kW = 1440u;
    constexpr uint32_t kH = 900u;
    constexpr float kDt = 1.0f / 60.0f;

    const auto cleanup_release = [&]() noexcept {
        if (app.preview_tex) {
            etch::destroy_texture(ctx, app.preview_tex);
            app.preview_tex = nullptr;
        }
        etch::free_text_buffer(&app.shader_buf);
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
    const auto click_point = [&](etch::Vec2 pt) noexcept {
        step_headless_frame(&app, kW, kH, kDt, pt, true);
        step_headless_frame(&app, kW, kH, kDt, pt, false);
    };

    for (uint32_t warm = 0; warm < 4; ++warm) step_headless_frame(&app, kW, kH, kDt);

    // [S1] Idle sleep & 1-draw-call batching (Popover closed)
    step_headless_frame(&app, kW, kH, kDt);
    step_headless_frame(&app, kW, kH, kDt);
    const etch::FrameStats base_stats = etch::frame_stats(ctx);
    if (etch::needs_redraw(ctx) || base_stats.draw_call_count != 1u || base_stats.blur_pass_count != 0u) {
        std::fprintf(
            stderr,
            "Headless Studio [S1] failed: needs_redraw=%d, draw_calls=%u, blur_pass=%u\n",
            etch::needs_redraw(ctx) ? 1 : 0,
            base_stats.draw_call_count,
            base_stats.blur_pass_count);
        cleanup_release();
        return 1;
    }

    // [S2] Horizontal & Vertical Split divider drag
    const uint32_t kSplitH = etch::id("studio_split_h");
    const uint32_t kSplitV = etch::id("studio_split_v");
    const etch::Rect split_h_rect = etch::element_bounds(kSplitH);
    const float init_ratio_h = etch::split_ratio(kSplitH);
    const float div_x = split_h_rect.x + (split_h_rect.w - kDividerPx) * init_ratio_h + 0.5f * kDividerPx;
    const float div_y = split_h_rect.y + 0.5f * split_h_rect.h;
    step_headless_frame(&app, kW, kH, kDt, etch::Vec2{div_x, div_y}, true);
    step_headless_frame(&app, kW, kH, kDt, etch::Vec2{div_x + 48.0f, div_y}, true);
    step_headless_frame(&app, kW, kH, kDt, etch::Vec2{div_x + 48.0f, div_y}, false);
    if (etch::split_ratio(kSplitH) <= init_ratio_h + 0.015f) {
        std::fprintf(stderr, "Headless Studio [S2a] failed: studio_split_h did not resize (%.3f -> %.3f)\n", init_ratio_h, etch::split_ratio(kSplitH));
        cleanup_release();
        return 1;
    }

    const etch::Rect split_v_rect = etch::element_bounds(kSplitV);
    const float init_ratio_v = etch::split_ratio(kSplitV);
    const float vdiv_x = split_v_rect.x + 0.5f * split_v_rect.w;
    const float vdiv_y = split_v_rect.y + (split_v_rect.h - kDividerPx) * init_ratio_v + 0.5f * kDividerPx;
    step_headless_frame(&app, kW, kH, kDt, etch::Vec2{vdiv_x, vdiv_y}, true);
    step_headless_frame(&app, kW, kH, kDt, etch::Vec2{vdiv_x, vdiv_y + 32.0f}, true);
    step_headless_frame(&app, kW, kH, kDt, etch::Vec2{vdiv_x, vdiv_y + 32.0f}, false);
    if (etch::split_ratio(kSplitV) <= init_ratio_v + 0.015f) {
        std::fprintf(
            stderr,
            "Headless Studio [S2b] failed: studio_split_v did not resize (%.3f -> %.3f) vrect=(%.1f,%.1f %.1fx%.1f)\n",
            init_ratio_v,
            etch::split_ratio(kSplitV),
            split_v_rect.x,
            split_v_rect.y,
            split_v_rect.w,
            split_v_rect.h);
        cleanup_release();
        return 1;
    }
    etch::set_split_ratio(kSplitH, 0.25f);
    etch::set_split_ratio(kSplitV, 0.62f);
    step_headless_frame(&app, kW, kH, kDt);

    // [S3] 1,000,000-row Virtual List O(visible) jump
    const uint32_t kTraceList = etch::id("studio_trace_list");
    etch::virtual_list_scroll_to_row(kTraceList, 999990u, kTraceRowHeight, etch::AlignY::Top);
    step_headless_frame(&app, kW, kH, kDt);
    step_headless_frame(&app, kW, kH, kDt);
    const uint32_t vis_count = app.last_trace_slice.end_index - app.last_trace_slice.first_index;
    if (app.last_trace_slice.first_index > 999990u ||
        app.last_trace_slice.end_index <= 999990u ||
        vis_count == 0u ||
        vis_count >= 64u)
    {
        std::fprintf(
            stderr,
            "Headless Studio [S3] failed: slice=[%u..%u)\n",
            app.last_trace_slice.first_index,
            app.last_trace_slice.end_index);
        cleanup_release();
        return 1;
    }

    // [S4] Canvas cursor-anchored wheel zoom, node drag, and port click-to-rewire
    const uint32_t kCanvas = etch::id("studio_canvas");
    const etch::Rect canvas_vp = etch::element_bounds(kCanvas);
    const etch::CanvasState cs_before = etch::canvas_state(kCanvas);
    const etch::Vec2 anchor_screen = { canvas_vp.x + 0.42f * canvas_vp.w, canvas_vp.y + 0.45f * canvas_vp.h };
    const etch::Vec2 anchor_world_before = cs_before.screen_to_world(anchor_screen);

    SDL_Event zoom_ev = {};
    zoom_ev.type = SDL_EVENT_MOUSE_WHEEL;
    zoom_ev.wheel.y = 1.0f;
    zoom_ev.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    zoom_ev.wheel.mouse_x = anchor_screen.x;
    zoom_ev.wheel.mouse_y = anchor_screen.y;
    etch::process_event(ctx, zoom_ev);
    step_headless_frame(&app, kW, kH, kDt, anchor_screen, false);

    const etch::CanvasState cs_after = etch::canvas_state(kCanvas);
    const etch::Vec2 anchor_world_after = cs_after.screen_to_world(anchor_screen);
    if (cs_after.zoom <= cs_before.zoom * 1.10f ||
        std::fabs(anchor_world_after.x - anchor_world_before.x) > 0.25f ||
        std::fabs(anchor_world_after.y - anchor_world_before.y) > 0.25f)
    {
        std::fprintf(
            stderr,
            "Headless Studio [S4a] failed: zoom %.3f->%.3f drift=(%.3f,%.3f)\n",
            cs_before.zoom,
            cs_after.zoom,
            anchor_world_after.x - anchor_world_before.x,
            anchor_world_after.y - anchor_world_before.y);
        cleanup_release();
        return 1;
    }

    const etch::Rect node2_bounds = etch::element_bounds(etch::id_index(etch::id("studio_node"), 2u));
    const etch::Vec2 node2_before = app.nodes[2].pos;
    const etch::Vec2 drag_start = { node2_bounds.x + 0.5f * node2_bounds.w, node2_bounds.y + 12.0f };
    const etch::Vec2 drag_end = { drag_start.x + 36.0f, drag_start.y + 18.0f };
    step_headless_frame(&app, kW, kH, kDt, drag_start, true);
    step_headless_frame(&app, kW, kH, kDt, drag_end, true);
    step_headless_frame(&app, kW, kH, kDt, drag_end, false);
    const etch::Vec2 node2_delta = app.nodes[2].pos - node2_before;
    const float expected_dx = 36.0f / cs_after.zoom;
    const float expected_dy = 18.0f / cs_after.zoom;
    if (std::fabs(node2_delta.x - expected_dx) > 0.6f || std::fabs(node2_delta.y - expected_dy) > 0.6f) {
        std::fprintf(
            stderr,
            "Headless Studio [S4b] failed: node drag delta=(%.2f,%.2f), expected=(%.2f,%.2f)\n",
            node2_delta.x,
            node2_delta.y,
            expected_dx,
            expected_dy);
        cleanup_release();
        return 1;
    }

    // Rewire Node 0 OUT -> Node 3 IN via port clicks, then restore
    const etch::Rect out0 = etch::element_bounds(etch::id_index(etch::id("studio_port_out"), 0u));
    const etch::Rect in3 = etch::element_bounds(etch::id_index(etch::id("studio_port_in"), 3u));
    click_point(etch::Vec2{ out0.x + 0.5f * out0.w, out0.y + 0.5f * out0.h });
    click_point(etch::Vec2{ in3.x + 0.5f * in3.w, in3.y + 0.5f * in3.h });
    if (app.nodes[3].input_from != 0u) {
        std::fprintf(stderr, "Headless Studio [S4c] failed: port rewire expected input_from=0, got %u\n", app.nodes[3].input_from);
        cleanup_release();
        return 1;
    }
    app.nodes[3].input_from = 2u;
    app.nodes[2].pos = {312.0f, 126.0f};
    etch::set_canvas_view(kCanvas, etch::Vec2{36.0f, 28.0f}, 1.0f);
    step_headless_frame(&app, kW, kH, kDt);

    // [S5] UTF-8 4-byte emoji + atomic Backspace + Chinese IME inline preedit
    const etch::Rect filter_rect = etch::element_bounds(etch::id("studio_filter"));
    click_point(etch::Vec2{ filter_rect.x + 0.5f * filter_rect.w, filter_rect.y + 0.5f * filter_rect.h });
    const uint32_t len_before = static_cast<uint32_t>(std::strlen(app.filter_buf));
    inject_text("\xF0\x9F\x9A\x80");
    step_headless_frame(&app, kW, kH, kDt);
    if (app.filter_res.byte_length != len_before + 4u) {
        std::fprintf(stderr, "Headless Studio [S5a] failed: 4-byte UTF-8 insert len=%u\n", app.filter_res.byte_length);
        cleanup_release();
        return 1;
    }
    inject_key(SDLK_BACKSPACE);
    step_headless_frame(&app, kW, kH, kDt);
    if (app.filter_res.byte_length != len_before) {
        std::fprintf(stderr, "Headless Studio [S5b] failed: atomic UTF-8 backspace len=%u\n", app.filter_res.byte_length);
        cleanup_release();
        return 1;
    }
    inject_editing("nihao", 2, 3);
    step_headless_frame(&app, kW, kH, kDt);
    const etch::Rect ime_rect = etch::ime_cursor_rect(ctx);
    if (!app.filter_res.composing || ime_rect.w <= 0.0f || ime_rect.h <= 0.0f) {
        std::fprintf(stderr, "Headless Studio [S5c] failed: IME composition/cursor rect inactive\n");
        cleanup_release();
        return 1;
    }
    inject_editing("", 0, 0);

    // [S7] Interactive Rich TextSpan click jump to #500k trace row
    const uint32_t kSpanJumpMid = etch::id("studio_span_jump_mid");
    const etch::Rect jump_rect = etch::element_bounds(kSpanJumpMid);
    if (jump_rect.w <= 0.0f || jump_rect.h <= 0.0f) {
        std::fprintf(stderr, "Headless Studio [S7a] failed: missing kSpanJumpMid bounds\n");
        cleanup_release();
        return 1;
    }
    click_point(etch::Vec2{ jump_rect.x + 0.5f * jump_rect.w, jump_rect.y + 0.5f * jump_rect.h });
    step_headless_frame(&app, kW, kH, kDt);
    if (app.jump_clicks != 1u ||
        app.last_trace_slice.first_index < 499990u ||
        app.last_trace_slice.first_index > 500010u)
    {
        std::fprintf(
            stderr,
            "Headless Studio [S7b] failed: clicks=%u first=%u\n",
            app.jump_clicks,
            app.last_trace_slice.first_index);
        cleanup_release();
        return 1;
    }

    // [S8] v1.2 Vector Icons, RingArc Dial Drag & 2x Reset, Clamped Context Menu, Ctrl+K Command Palette & Undo/Redo
    if (!etch::is_icon_loaded(ctx, app.ico_bloom) ||
        !etch::is_icon_loaded(ctx, app.ico_expose) ||
        !etch::is_icon_loaded(ctx, app.ico_node) ||
        !etch::is_icon_loaded(ctx, app.ico_menu) ||
        !etch::is_icon_loaded(ctx, app.ico_search))
    {
        std::fprintf(stderr, "Headless Studio [S8a] failed: SVG studio icons not loaded\n");
        cleanup_release();
        return 1;
    }
    uint32_t ring_arc_count = 0u;
    for (const etch::Prim& p : etch::frame_prims(ctx)) {
        if (static_cast<uint8_t>(p.style.x & 0xFu) == static_cast<uint8_t>(etch::PrimKind::RingArc)) {
            ++ring_arc_count;
        }
    }
    if (ring_arc_count == 0u) {
        std::fprintf(stderr, "Headless Studio [S8a] failed: expected RingArc prims > 0\n");
        cleanup_release();
        return 1;
    }

    const etch::Rect dial_r = etch::element_bounds(etch::id("studio_dial_bloom"));
    if (dial_r.w <= 0.0f || dial_r.h <= 0.0f) {
        std::fprintf(stderr, "Headless Studio [S8b] failed: missing studio_dial_bloom bounds\n");
        cleanup_release();
        return 1;
    }
    const float bloom_before = app.target_bloom;
    const etch::Vec2 dial_center = { dial_r.x + 0.5f * dial_r.w, dial_r.y + 0.5f * dial_r.h };
    const etch::Vec2 dial_drag_end = { dial_center.x + 45.0f, dial_center.y };
    step_headless_frame(&app, kW, kH, kDt, dial_center, true);
    step_headless_frame(&app, kW, kH, kDt, dial_drag_end, true);
    step_headless_frame(&app, kW, kH, kDt, dial_drag_end, false);
    if (app.target_bloom <= bloom_before + 0.20f) {
        std::fprintf(stderr, "Headless Studio [S8b] failed: dial drag bloom %.3f -> %.3f\n", bloom_before, app.target_bloom);
        cleanup_release();
        return 1;
    }
    step_headless_frame(&app, kW, kH, kDt, dial_center, true, false, 2u);
    step_headless_frame(&app, kW, kH, kDt, dial_center, false);
    if (std::fabs(app.target_bloom - kPresets[app.selected_preset].bloom) > 1e-4f) {
        std::fprintf(stderr, "Headless Studio [S8b] failed: dial double-click reset bloom=%.3f\n", app.target_bloom);
        cleanup_release();
        return 1;
    }

    const etch::Vec2 rc_pt = { canvas_vp.x + canvas_vp.w - 24.0f, canvas_vp.y + 48.0f };
    step_headless_frame(&app, kW, kH, kDt, rc_pt, false, true);
    step_headless_frame(&app, kW, kH, kDt, rc_pt, false, false);
    const etch::Rect ctx_menu_r = etch::element_bounds(etch::id("studio_ctx_menu"));
    if (!app.ctx_menu_open || ctx_menu_r.w <= 0.0f || (ctx_menu_r.x + ctx_menu_r.w) > static_cast<float>(kW)) {
        std::fprintf(
            stderr,
            "Headless Studio [S8c] failed: ctx_open=%d rect=(%.1f,%.1f %.1fx%.1f)\n",
            app.ctx_menu_open ? 1 : 0,
            ctx_menu_r.x,
            ctx_menu_r.y,
            ctx_menu_r.w,
            ctx_menu_r.h);
        cleanup_release();
        return 1;
    }
    click_point(etch::Vec2{ 20.0f, 20.0f });
    step_headless_frame(&app, kW, kH, kDt);
    if (app.ctx_menu_open) {
        std::fprintf(stderr, "Headless Studio [S8c] failed: outside click did not close ctx menu\n");
        cleanup_release();
        return 1;
    }

    step_headless_frame(&app, kW, kH, kDt, etch::Vec2{-1.0f, -1.0f}, false, false, 0u, etch::Mod_Primary, SDLK_K);
    const etch::Rect cmd_in_r = etch::element_bounds(etch::id("studio_cmd_input"));
    if (!app.cmd_palette_open || cmd_in_r.w <= 0.0f || cmd_in_r.h <= 0.0f) {
        std::fprintf(stderr, "Headless Studio [S8d] failed: Ctrl+K palette did not open\n");
        cleanup_release();
        return 1;
    }
    click_point(etch::Vec2{ cmd_in_r.x + 0.5f * cmd_in_r.w, cmd_in_r.y + 0.5f * cmd_in_r.h });
    inject_text("Glow");
    step_headless_frame(&app, kW, kH, kDt);
    step_headless_frame(&app, kW, kH, 0.8f);
    inject_text("!");
    step_headless_frame(&app, kW, kH, kDt);
    if (!app.cmd_palette_open || std::strcmp(app.cmd_filter_buf, "Glow!") != 0) {
        std::fprintf(stderr, "Headless Studio [S8d] failed: cmd input='%s'\n", app.cmd_filter_buf);
        cleanup_release();
        return 1;
    }
    inject_key(SDLK_Z, SDL_KMOD_CTRL);
    step_headless_frame(&app, kW, kH, kDt);
    if (std::strcmp(app.cmd_filter_buf, "Glow") != 0) {
        std::fprintf(stderr, "Headless Studio [S8d] failed: Ctrl+Z undo got '%s'\n", app.cmd_filter_buf);
        cleanup_release();
        return 1;
    }
    inject_key(SDLK_Y, SDL_KMOD_CTRL);
    step_headless_frame(&app, kW, kH, kDt);
    if (std::strcmp(app.cmd_filter_buf, "Glow!") != 0) {
        std::fprintf(stderr, "Headless Studio [S8d] failed: Ctrl+Y redo got '%s'\n", app.cmd_filter_buf);
        cleanup_release();
        return 1;
    }
    app.cmd_filter_buf[0] = '\0';
    inject_key(SDLK_ESCAPE);
    step_headless_frame(&app, kW, kH, kDt);
    if (app.cmd_palette_open) {
        std::fprintf(stderr, "Headless Studio [S8d] failed: Escape did not close Ctrl+K palette\n");
        cleanup_release();
        return 1;
    }
    for (int f = 0; f < 40 && etch::needs_redraw(ctx); ++f) {
        step_headless_frame(&app, kW, kH, kDt);
    }

    // [S9] Etch 1.5 Chrome Hit-Test, Box-Local draw_text, Palette->Canvas DND & Marquee Group Drag
    const etch::Rect tb_r = etch::element_bounds(etch::id("studio_toolbar"));
    const etch::Rect pr_r = etch::element_bounds(etch::id("studio_btn_presets"));
    if (etch::evaluate_window_hit(ctx, etch::Vec2{2.0f, 2.0f}) != etch::WindowHit::ResizeTopLeft ||
        etch::evaluate_window_hit(ctx, etch::Vec2{720.0f, 2.0f}) != etch::WindowHit::ResizeTop ||
        etch::evaluate_window_hit(ctx, etch::Vec2{tb_r.x + 120.0f, tb_r.y + 22.0f}) != etch::WindowHit::Caption ||
        etch::evaluate_window_hit(ctx, etch::Vec2{pr_r.x + 0.5f * pr_r.w, pr_r.y + 0.5f * pr_r.h}) != etch::WindowHit::Client)
    {
        std::fprintf(stderr, "Headless Studio [S9a] failed: evaluate_window_hit mismatch\n");
        cleanup_release();
        return 1;
    }

    const etch::Rect plot_r = etch::element_bounds(etch::id("studio_curve_plot"));
    const etch::Vec2 lbl_sz = etch::measure_text(etch::str_lit("ACES Filmic"), etch::kFontSans, 9.5f);
    if (plot_r.h < 48.0f || lbl_sz.x <= 20.0f) {
        std::fprintf(stderr, "Headless Studio [S9b] failed: curve plot / measure_text check failed\n");
        cleanup_release();
        return 1;
    }

    const uint32_t kPal0 = etch::id_index(etch::id("studio_palette"), 0u);
    const etch::Rect pal_r = etch::element_bounds(kPal0);
    const etch::Rect cv_r = etch::element_bounds(kCanvas);
    const etch::Vec2 drag_from = { pal_r.x + 0.5f * pal_r.w, pal_r.y + 0.5f * pal_r.h };
    const etch::Vec2 drag_mid = { drag_from.x + 8.0f, drag_from.y };
    const etch::Vec2 drop_pt = { cv_r.x + 0.62f * cv_r.w, cv_r.y + 0.82f * cv_r.h };
    step_headless_frame(&app, kW, kH, kDt, drag_from, true);
    step_headless_frame(&app, kW, kH, kDt, drag_mid, true);
    if (!etch::is_dragging_payload(kDragKindPass)) {
        std::fprintf(stderr, "Headless Studio [S9c] failed: drag payload did not activate from palette\n");
        cleanup_release();
        return 1;
    }
    step_headless_frame(&app, kW, kH, kDt, drop_pt, true);
    step_headless_frame(&app, kW, kH, kDt, drop_pt, false);
    if (etch::is_dragging_payload() || app.node_count != 6u) {
        std::fprintf(stderr, "Headless Studio [S9c] failed: drop into canvas expected node_count=6, got %u\n", app.node_count);
        cleanup_release();
        return 1;
    }
    step_headless_frame(&app, kW, kH, kDt);

    const etch::CanvasState cs_m = etch::canvas_state(kCanvas);
    const etch::Vec2 m_start = cs_m.world_to_screen(etch::Vec2{10.0f, 10.0f});
    const etch::Vec2 m_end = cs_m.world_to_screen(etch::Vec2{255.0f, 345.0f});
    step_headless_frame(&app, kW, kH, kDt, m_start, true);
    step_headless_frame(&app, kW, kH, kDt, m_end, true);
    if (!etch::canvas_state(kCanvas).marquee_active) {
        std::fprintf(stderr, "Headless Studio [S9d] failed: marquee did not activate on empty canvas drag\n");
        cleanup_release();
        return 1;
    }
    step_headless_frame(&app, kW, kH, kDt, m_end, false);
    if ((app.node_selected_mask & 0x3u) != 0x3u) {
        std::fprintf(stderr, "Headless Studio [S9d] failed: expected nodes 0 & 1 selected, mask=0x%X\n", app.node_selected_mask);
        cleanup_release();
        return 1;
    }

    const etch::Rect n0_r = etch::element_bounds(etch::id_index(etch::id("studio_node"), 0u));
    const etch::Vec2 n0_before = app.nodes[0].pos;
    const etch::Vec2 n1_before = app.nodes[1].pos;
    const etch::Vec2 g_start = { n0_r.x + 0.5f * n0_r.w, n0_r.y + 12.0f };
    const etch::Vec2 g_end = { g_start.x + 24.0f, g_start.y + 12.0f };
    step_headless_frame(&app, kW, kH, kDt, g_start, true);
    step_headless_frame(&app, kW, kH, kDt, g_end, true);
    step_headless_frame(&app, kW, kH, kDt, g_end, false);
    const etch::Vec2 n0_delta = app.nodes[0].pos - n0_before;
    const etch::Vec2 n1_delta = app.nodes[1].pos - n1_before;
    const float g_dx = 24.0f / cs_m.zoom;
    const float g_dy = 12.0f / cs_m.zoom;
    if (std::fabs(n0_delta.x - g_dx) > 0.6f || std::fabs(n0_delta.y - g_dy) > 0.6f ||
        std::fabs(n1_delta.x - g_dx) > 0.6f || std::fabs(n1_delta.y - g_dy) > 0.6f)
    {
        std::fprintf(
            stderr,
            "Headless Studio [S9d] failed: group drag n0=(%.2f,%.2f) n1=(%.2f,%.2f) expected=(%.2f,%.2f)\n",
            n0_delta.x, n0_delta.y, n1_delta.x, n1_delta.y, g_dx, g_dy);
        cleanup_release();
        return 1;
    }

    // [S10] Live dock tabs/splits, hierarchical selection, table resize/sort and large jumps.
    const uint32_t dock = etch::id("studio_dock"), inspector = etch::id("studio_pane_inspector"), outliner = etch::id("studio_pane_outliner");
    const auto click_widget = [&](uint32_t widget) noexcept {
        const auto r = etch::element_bounds(widget); click_point({r.x+r.w*.5f,r.y+r.h*.5f});
    };
    bool s10_ok = true;
    click_widget(etch::id_index(outliner,0xD300u));
    s10_ok &= etch::dock_active_pane(dock,etch::dock_find_pane_leaf(dock,outliner)) == outliner;
    const uint32_t tree_root = etch::id("studio_tree_root");
    click_widget(etch::id_index(tree_root,0x7EE0u)); s10_ok &= !etch::is_tree_open(tree_root);
    click_widget(etch::id_index(tree_root,0x7EE0u)); s10_ok &= etch::is_tree_open(tree_root);
    click_widget(etch::id_index(etch::id("studio_tree_node"),0)); s10_ok &= app.selected_node == 0;
    s10_ok &= etch::dock_dock_pane(dock,outliner,inspector,etch::DockZone::Bottom);
    step_headless_frame(&app,kW,kH,kDt); step_headless_frame(&app,kW,kH,kDt);
    const uint32_t inspector_leaf = etch::dock_find_pane_leaf(dock,inspector), outliner_leaf = etch::dock_find_pane_leaf(dock,outliner);
    const auto ir = etch::element_bounds(etch::id_index(dock,0xD200u+inspector_leaf));
    const auto obr = etch::element_bounds(etch::id_index(dock,0xD200u+outliner_leaf));
    s10_ok &= ir.h > 0 && obr.h > 0 && obr.y > ir.y;
    s10_ok &= etch::dock_dock_pane(dock,outliner,inspector,etch::DockZone::Center);
    etch::dock_activate_pane(dock,inspector); step_headless_frame(&app,kW,kH,kDt); step_headless_frame(&app,kW,kH,kDt);
    const auto restored = etch::element_bounds(etch::id_index(dock,0xD200u+etch::dock_find_pane_leaf(dock,inspector)));
    s10_ok &= restored.h > ir.h+obr.h;
    const uint32_t column1 = etch::id_index(kTraceList,0x7501u);
    const auto handle = etch::element_bounds(etch::id_index(column1,0x7600u));
    const float width_before = etch::table_column_width(kTraceList,1);
    const auto sort_before = app.trace_sort_column;
    const etch::Vec2 table_drag_start = {handle.x+3,handle.y+handle.h*.5f};
    step_headless_frame(&app,kW,kH,kDt,table_drag_start,true);
    step_headless_frame(&app,kW,kH,kDt,{table_drag_start.x+32,table_drag_start.y},true);
    step_headless_frame(&app,kW,kH,kDt,{table_drag_start.x+32,table_drag_start.y},false);
    s10_ok &= etch::table_column_width(kTraceList,1) > width_before+20 && app.trace_sort_column == sort_before && !app.trace_sort_changed;
    etch::table_scroll_to_row(kTraceList,500000,kTraceRowHeight); step_headless_frame(&app,kW,kH,kDt);
    const uint32_t gpu_header = etch::id_index(etch::id_index(kTraceList,0x7502u),0x7601u);
    click_widget(gpu_header); s10_ok &= app.trace_sort_changed && app.trace_sort_column == 2 && app.trace_sort_descending;
    const uint32_t descending_row = app.trace_first_row;
    click_widget(gpu_header); s10_ok &= app.trace_sort_changed && !app.trace_sort_descending && app.trace_first_row != descending_row;
    s10_ok &= app.last_trace_slice.first_index <= 500000 && app.last_trace_slice.end_index > 500000;
    etch::table_scroll_to_row(kTraceList,999990,kTraceRowHeight); step_headless_frame(&app,kW,kH,kDt); step_headless_frame(&app,kW,kH,kDt);
    const uint32_t first_trace = app.last_trace_slice.first_index;
    const auto header1 = etch::element_bounds(etch::id_index(column1,0x7601u));
    const auto cell1 = etch::element_bounds(etch::id_index(etch::id_index(kTraceList,0x100000u+first_trace),1));
    s10_ok &= first_trace <= 999990 && app.last_trace_slice.end_index == 1000000 && std::fabs(header1.x-cell1.x) < 1;
    if (!s10_ok) { std::fprintf(stderr,"Headless Studio [S10] failed: dock/tree/table regression\n"); cleanup_release(); return 1; }

    // [S6] Spring preset change + Dual-Kawase Popover + F12 HUD + PNG Export
    const uint32_t kBtnPresets = etch::id("studio_btn_presets");
    const etch::Rect presets_rect = etch::element_bounds(kBtnPresets);
    click_point(etch::Vec2{ presets_rect.x + 0.5f * presets_rect.w, presets_rect.y + 0.5f * presets_rect.h });
    app.selected_preset = 1u;
    app.target_bloom = kPresets[1].bloom;
    app.target_exposure = kPresets[1].exposure;
    for (int f = 0; f < 90 && (f < 2 || etch::needs_redraw(ctx)); ++f) {
        step_headless_frame(&app, kW, kH, kDt);
    }

    inject_key(SDLK_F12);
    step_headless_frame(&app, kW, kH, kDt);
    step_headless_frame(&app, kW, kH, kDt);
    if (!etch::debug_hud_open(ctx)) {
        std::fprintf(stderr, "Headless Studio [S6a] failed: F12 HUD did not open\n");
        cleanup_release();
        return 1;
    }

    if (!etch::render_headless_png(ctx, kW, kH, out_png_path, etch::Color::rgb_hex(0x090C12))) {
        std::fprintf(stderr, "Headless Studio [S6b] failed: render_headless_png failed for %s\n", out_png_path);
        cleanup_release();
        return 1;
    }

    const etch::FrameStats final_stats = etch::frame_stats(ctx);
    const std::span<const etch::Prim> prims = etch::frame_prims(ctx);
    uint32_t grid_prims = 0u;
    uint32_t wire_prims = 0u;
    for (const etch::Prim& p : prims) {
        const uint8_t kind = static_cast<uint8_t>(p.style.x & 0xFu);
        if (kind == static_cast<uint8_t>(etch::PrimKind::CanvasGrid)) {
            ++grid_prims;
        } else if (kind == static_cast<uint8_t>(etch::PrimKind::BezierWire)) {
            ++wire_prims;
        }
    }

    if (final_stats.draw_call_count == 0u ||
        final_stats.draw_call_count > 3u ||
        final_stats.blur_pass_count == 0u ||
        final_stats.slug_glyph_count == 0u ||
        grid_prims == 0u ||
        wire_prims == 0u)
    {
        std::fprintf(
            stderr,
            "Headless Studio [S6c] failed: dc=%u blur=%u glyphs=%u grid=%u wire=%u\n",
            final_stats.draw_call_count,
            final_stats.blur_pass_count,
            final_stats.slug_glyph_count,
            grid_prims,
            wire_prims);
        cleanup_release();
        return 1;
    }

    // [S12] Studio uses transient Slug fills and analytically rotated gauge needles.
    bool s12_fill = false, s12_rotation = false;
    for (const etch::Prim& p : prims) {
        s12_fill |= (p.style.x & 15u) == uint32_t(etch::PrimKind::SlugFill);
        s12_rotation |= (p.style.x & 15u) == uint32_t(etch::PrimKind::RotatedRect);
    }
    if (!s12_fill || !s12_rotation || final_stats.dynamic_slug_curve_count == 0 || final_stats.draw_call_count > 3) {
        std::fprintf(stderr, "Headless Studio [S12] failed: vector completeness\n");
        cleanup_release(); return 1;
    }

    // [S11] Exercise Studio's own palette, spatial canvas drops and preview UI.
    app.preset_popover_open = false;
    app.ctx_menu_open = false;
    etch::close_popup();
    step_headless_frame(&app, kW, kH, kDt);
    const uint32_t restore_focus = etch::id("studio_filter");
    etch::request_focus(restore_focus);
    app.cmd_palette_open = true;
    step_headless_frame(&app, kW, kH, kDt);
    bool s11_ok = etch::focused(etch::id("studio_cmd_input"));
    // Ctrl+K is Studio's background palette toggle: the modal must block it.
    step_headless_frame(&app, kW, kH, kDt, {-1,-1}, false, false, 0, etch::Mod_Primary, SDLK_K);
    s11_ok &= app.cmd_palette_open && etch::focused(etch::id("studio_cmd_input"));
    app.cmd_palette_open = false;
    step_headless_frame(&app, kW, kH, kDt);
    s11_ok &= etch::focused(restore_focus);
    const etch::Rect drop_rect = etch::element_bounds(kCanvas);
    const uint32_t before_drop = app.node_count;
    const etch::Vec2 external_pos = {drop_rect.x + drop_rect.w * 0.95f, drop_rect.y + drop_rect.h * 0.95f};
    etch::inject_os_drop_begin(ctx, external_pos);
    step_headless_frame(&app, kW, kH, kDt);
    etch::inject_drop_file(ctx, etch::str_lit("C:/assets/S11.svg"));
    etch::inject_drop_text(ctx, etch::str_lit("S11 canvas note"));
    step_headless_frame(&app, kW, kH, kDt);
    s11_ok &= etch::os_drag_hovering(kCanvas) && app.node_count == before_drop;
    etch::inject_os_drop_complete(ctx, external_pos);
    step_headless_frame(&app, kW, kH, kDt);
    s11_ok &= before_drop + 2 <= kMaxNodes && app.node_count == before_drop + 2;
    if (app.node_count == before_drop + 2) {
        s11_ok &= std::strcmp(app.nodes[before_drop].title, "C:/assets/S11.svg") == 0 &&
            std::strcmp(app.nodes[before_drop].subtitle, "External asset") == 0 &&
            std::strcmp(app.nodes[before_drop + 1].title, "S11 canvas note") == 0 &&
            std::strcmp(app.nodes[before_drop + 1].subtitle, "Dropped note") == 0;
    }
    step_headless_frame(&app, kW, kH, kDt);
    s11_ok &= app.node_count == before_drop + 2;
    app.tear_off_window = SDL_CreateWindow("Etch Studio " ETCH_VERSION_STRING " — Tear-Off Preview", 480, 360, SDL_WINDOW_HIDDEN);
    s11_ok &= app.tear_off_window != nullptr;
    if (app.tear_off_window) {
        etch::begin_window_frame(ctx, app.tear_off_window, kDt);
        s11_ok &= etch::frame_in_progress(ctx) && etch::claimed_window_count(ctx) == 1;
        build_preview_ui(&app);
        etch::end_window_frame(ctx);
        s11_ok &= etch::element_bounds(etch::id("tear_preview")).w > 0;
        for (int f = 0; f < 3; ++f) {
            etch::begin_window_frame(ctx, app.tear_off_window, kDt);
            build_preview_ui(&app);
            etch::end_window_frame(ctx);
        }
        s11_ok &= !etch::needs_window_redraw(ctx, app.tear_off_window);
        etch::request_window_redraw(ctx, app.tear_off_window);
        s11_ok &= etch::needs_window_redraw(ctx, app.tear_off_window);
        etch::release_window(ctx, app.tear_off_window);
        s11_ok &= etch::claimed_window_count(ctx) == 0;
        SDL_DestroyWindow(app.tear_off_window);
        app.tear_off_window = nullptr;
    }
    if (!s11_ok) {
        std::fprintf(stderr, "Headless Studio [S11] failed: palette/drop/preview regression\n");
        cleanup_release(); return 1;
    }
    // [S13] Selectable telemetry and animated Inspector palette.
    step_headless_frame(&app,kW,kH,kDt);
    const uint32_t telem_id = etch::id("studio_telemetry_selection"), card_id = etch::id("studio_pass_collapsible");
    etch::set_text_selection(telem_id,0,8);
    bool s13_ok = etch::has_text_selection(telem_id) && etch::selected_text(telem_id).length > 0;
    const auto tb = etch::element_bounds(telem_id);
    step_headless_frame(&app,kW,kH,kDt,{tb.x+2,tb.y+3},true);
    step_headless_frame(&app,kW,kH,kDt,{tb.x+tb.w*.6f,tb.y+3},true);
    s13_ok &= etch::has_text_selection(telem_id);
    step_headless_frame(&app,kW,kH,kDt,{tb.x+tb.w*.6f,tb.y+3},false);
    etch::request_focus(telem_id);
    step_headless_frame(&app,kW,kH,kDt,{-1,-1},false,false,0,etch::Mod_Primary,SDLK_A);
    step_headless_frame(&app,kW,kH,kDt,{-1,-1},false,false,0,etch::Mod_Primary,SDLK_C);
    const auto selected = etch::selected_text(telem_id), copied = etch::get_clipboard_text();
    s13_ok &= selected.length > 8 && copied.length == selected.length && std::memcmp(selected.chars,copied.chars,selected.length)==0;
    const uint32_t header = etch::id_index(card_id,0xC011u);
    const auto hb = etch::element_bounds(header);
    const etch::Vec2 hp = {hb.x+hb.w*.5f,hb.y+hb.h*.5f};
    step_headless_frame(&app,kW,kH,kDt,hp,true);
    step_headless_frame(&app,kW,kH,kDt,hp,false);
    bool intermediate = false;
    for (int f=0;f<60;++f) {
        step_headless_frame(&app,kW,kH,kDt);
        const float h = etch::element_bounds(etch::id_index(card_id,0xC013u)).h;
        intermediate |= h > .5f && h < 20;
    }
    s13_ok &= !etch::is_collapsible_open(card_id) && intermediate;
    etch::request_focus(header);
    step_headless_frame(&app,kW,kH,kDt,{-1,-1},false,false,0,etch::Mod_None,SDLK_SPACE);
    for (int f=0;f<60;++f) step_headless_frame(&app,kW,kH,kDt);
    s13_ok &= etch::is_collapsible_open(card_id) && etch::element_bounds(etch::id_index(card_id,0xC013u)).h > 1 && etch::frame_stats(ctx).draw_call_count <= 3;
    if (!s13_ok) { std::fprintf(stderr,"Headless Studio [S13] failed: selection/collapsible regression\n"); cleanup_release(); return 1; }
    const char* driver = SDL_GetGPUDeviceDriver(etch::gpu_device(ctx));
    // [S14] Visible v2.1 strip fits the inspector and settled frames sleep.
    for(int f=0;f<90 && etch::needs_redraw(ctx);++f)step_headless_frame(&app,kW,kH,kDt);
    const auto v21_bounds=etch::element_bounds(etch::id("studio_v21_label"));
    const auto v21_stats=etch::frame_stats(ctx);
    if(!(v21_bounds.w>0 && v21_bounds.w<400 && v21_stats.draw_call_count<=3 && v21_stats.idle_sleeping)) {
        std::fprintf(stderr,"Headless Studio [S14] failed: showcase bounds/batching/idle\n");cleanup_release();return 1;
    }
    // [S15] Visible grid properties and multi-line pass tags settle without extra draws.
    const uint32_t selected_rank=app.last_trace_slice.first_index+3;
    click_widget(etch::id_index(etch::id_index(kTraceList,0x100000u+selected_rank),0));
    for(int f=0;f<8;++f)step_headless_frame(&app,kW,kH,kDt);
    const auto detail=etch::element_bounds(etch::id("studio_v22_trace_detail"));
    const auto selection_stats=etch::frame_stats(ctx);
    const auto key0=etch::element_bounds(etch::id_index(etch::id("studio_v22_key"),0));
    const auto key1=etch::element_bounds(etch::id_index(etch::id("studio_v22_key"),1));
    const auto value0=etch::element_bounds(etch::id_index(etch::id("studio_v22_value"),0));
    const auto tags=etch::element_bounds(etch::id("studio_v22_tags"));
    if (!(std::fabs(key0.w-88)<.2f && std::fabs(key0.x-key1.x)<.2f && std::fabs(value0.y-key0.y)<.2f && tags.h>=44 && detail.h==24 && app.trace_selected_row!=0 && selection_stats.draw_call_count<=3 && selection_stats.idle_sleeping)) {
        std::fprintf(stderr,"Headless Studio [S15] failed: key=(%g,%g,%g,%g), key1=(%g,%g), value=(%g,%g), tags=%g detail=%g selected=%u idle=%d\n",key0.x,key0.y,key0.w,key0.h,key1.x,key1.y,value0.x,value0.y,tags.h,detail.h,app.trace_selected_row,int(selection_stats.idle_sleeping));cleanup_release();return 1;
    }
    if (!etch::render_headless_png(ctx,kW,kH,out_png_path)) { cleanup_release();return 1; }
    // [S16] Desktop menus, numeric editing and pointer dock integration.
    if (!verify_desktop_checks(ctx)) { cleanup_release();return 1; }
    bool s16_ok=true;
    click_widget(etch::id_index(etch::id("studio_file_menu"),0x2303));
    step_headless_frame(&app,kW,kH,kDt);
    s16_ok &= etch::is_popup_open(etch::id("studio_file_menu"));
    const auto view_menu=etch::element_bounds(etch::id_index(etch::id("studio_view_menu"),0x2303));
    step_headless_frame(&app,kW,kH,kDt,{view_menu.x+4,view_menu.y+4});
    step_headless_frame(&app,kW,kH,kDt);
    s16_ok &= etch::is_popup_open(etch::id("studio_view_menu"));
    const bool old_glow=app.wire_glow_enabled;
    click_widget(etch::id("studio_view_glow"));
    s16_ok &= app.wire_glow_enabled!=old_glow && !etch::is_popup_open(etch::id("studio_view_menu"));
    etch::set_collapsible_open(etch::id("studio_desktop_controls"),true);
    for(int i=0;i<60;++i) step_headless_frame(&app,kW,kH,kDt);
    const float old_exposure=app.target_exposure;
    etch::request_focus(etch::id("studio_exposure_value"));
    step_headless_frame(&app,kW,kH,kDt,{-1,-1},false,false,0,etch::Mod_None,SDLK_LEFT);
    s16_ok &= app.target_exposure<old_exposure;
    const auto source_tab=etch::element_bounds(etch::id_index(outliner,0xD300));
    const auto target_tab=etch::element_bounds(etch::id_index(inspector,0xD300));
    const etch::Vec2 start_tab={source_tab.x+8,source_tab.y+8};
    const etch::Vec2 end_tab={target_tab.x+2,target_tab.y+8};
    step_headless_frame(&app,kW,kH,kDt,start_tab,true);
    step_headless_frame(&app,kW,kH,kDt,{start_tab.x+8,start_tab.y+8},true);
    step_headless_frame(&app,kW,kH,kDt,end_tab,true);
    step_headless_frame(&app,kW,kH,kDt,end_tab,false);
    step_headless_frame(&app,kW,kH,kDt);
    s16_ok &= etch::element_bounds(etch::id_index(outliner,0xD300)).x<etch::element_bounds(etch::id_index(inspector,0xD300)).x;
    etch::dock_activate_pane(dock,inspector);
    etch::set_collapsible_open(etch::id("studio_desktop_controls"),false);
    for(int i=0;i<90;++i) step_headless_frame(&app,kW,kH,kDt);
    s16_ok &= etch::frame_stats(ctx).idle_sleeping;
    s16_ok &= etch::render_headless_png(ctx,kW,kH,out_png_path) && etch::frame_stats(ctx).draw_call_count<=3;
    if(!s16_ok) {std::fprintf(stderr,"Headless Studio [S16] failed: desktop showcase integration\n");cleanup_release();return 1;}

    // [S17] Both docked panes remain visible with a live, highlighted document.
    const auto shader_bounds=etch::element_bounds(etch::id("studio_shader_editor"));
    const auto graph_bounds=etch::element_bounds(etch::id("studio_canvas"));
    if(!verify_a11y_checks(ctx) || !verify_editor_checks(ctx) || etch::text_buffer_line_count(&app.shader_buf)<100 || shader_bounds.w<200 || graph_bounds.w<200 || shader_bounds.x<graph_bounds.x+graph_bounds.w-1) { cleanup_release();return 1; }
    std::printf(
        "Headless Studio OK (%s) -> %s (asserts=18/18, prims=%u, draw_calls=%u, blur_pass=%u, slug_glyphs=%u, grid=%u, wire=%u, layout_us=%.0f, vrows=[%u..%u))\n",
        driver ? driver : "unknown",
        out_png_path,
        final_stats.prim_count,
        final_stats.draw_call_count,
        final_stats.blur_pass_count,
        final_stats.slug_glyph_count,
        grid_prims,
        wire_prims,
        final_stats.layout_ms * 1000.0,
        app.last_trace_slice.first_index,
        app.last_trace_slice.end_index);

    cleanup_release();
    return 0;
}

int run_interactive() noexcept {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Etch Studio " ETCH_VERSION_STRING " — GPU Render-Graph, 1M Editor & Telemetry",
        1440,
        900,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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

    StudioApp app = {
        .ctx = ctx,
        .window = window,
        .running = true,
    };
    init_studio_icons(&app);
    etch::set_window_chrome(etch::WindowChromeDesc{
        .enabled = true,
        .resizable = true,
        .border_thickness = 6.0f,
        .corner_size = 12.0f,
    });

    SDL_AddEventWatch(window_event_watch, &app);
    render_window_frame(&app, 0.0f);

    Uint64 prev_ns = SDL_GetTicksNS();
    while (app.running) {
        SDL_Event ev;
        if (etch::wait_events(ctx, &ev)) {
            do {
                const bool was_dragging = etch::is_dragging_payload();
                etch::process_event(ctx, ev);
                if (ev.type == SDL_EVENT_QUIT) {
                    app.running = false;
                } else if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    if (app.tear_off_window && ev.window.windowID == SDL_GetWindowID(app.tear_off_window)) SDL_HideWindow(app.tear_off_window);
                    else if (ev.window.windowID == SDL_GetWindowID(app.window)) app.running = false;
                } else if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.windowID == SDL_GetWindowID(app.window) && ev.key.key == SDLK_ESCAPE) {
                    if (was_dragging) {
                        etch::request_redraw(ctx);
                    } else if (app.cmd_palette_open) {
                        app.cmd_palette_open = false;
                        etch::request_redraw(ctx);
                    } else if (app.ctx_menu_open) {
                        app.ctx_menu_open = false;
                        etch::request_redraw(ctx);
                    } else if (app.preset_popover_open) {
                        app.preset_popover_open = false;
                        etch::request_redraw(ctx);
                    } else if (app.pending_wire_src != UINT32_MAX) {
                        app.pending_wire_src = UINT32_MAX;
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

    SDL_RemoveEventWatch(window_event_watch, &app);
    if (app.tear_off_window) { etch::release_window(ctx, app.tear_off_window); SDL_DestroyWindow(app.tear_off_window); }

    if (app.preview_tex) {
        etch::destroy_texture(ctx, app.preview_tex);
        app.preview_tex = nullptr;
    }
    etch::free_text_buffer(&app.shader_buf);
    etch::destroy_context(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    bool headless = false;
    const char* out_png = "studio_check.png";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            headless = true;
            if ((i + 1) < argc && argv[i + 1][0] != '-') {
                out_png = argv[i + 1];
                ++i;
            }
        }
    }

    if (headless) {
        return run_headless(out_png);
    }
    return run_interactive();
}
