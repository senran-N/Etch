// Public-API flagship material, elevation, vibrancy, motion, mipmap and terminal showcase.
#include <etch/etch.h>
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "etch_surface_demo_dxil.h"
#include "etch_surface_demo_spv.h"
#include "etch_surface_demo_msl.h"

namespace {
using namespace etch;

struct Showcase {
    ShaderSurfaceId aurora = 0u;
    LoadedTexture spectrum_tex = {};
    TerminalCell cells[8 * 64] = {};
    uint32_t tab = 0u;
    uint32_t snap = 1u;
    bool drawer = true;
    bool light_mode = false;
};

void label(const char* value, float size, Color color, uint16_t font = kFontSans) noexcept {
    text(str_cstr(value), {.color = color, .font_size_px = size, .font_id = font, .wrap = false});
}

void fill_terminal(Showcase* app) noexcept {
    const char* lines[] = {
        "$ etch run --backend native --vibrancy",
        "  > agent.plan    connected  (Tokyo-03 / 32 ms)",
        "  > shader.aurora compiled in 4 ms",
        "  > surface.fx    elevation + inset + oklab stops",
        "  > mipmaps       256x128 telemetry cover active",
        "  > tests         all dual-backend checks passed",
        "  ready for the next idea",
        "_",
    };
    const uint32_t c_prompt = app->light_mode ? 0x0369A1u : 0x7DD3FCu;
    const uint32_t c_pass = app->light_mode ? 0x047857u : 0x5EEAD4u;
    const uint32_t c_body = app->light_mode ? 0x334155u : 0xCBD5E1u;
    for (uint32_t row = 0; row < 8u; ++row) {
        const size_t len = std::strlen(lines[row]);
        const uint32_t rgb = (row == 0u) ? c_prompt : ((row == 5u) ? c_pass : c_body);
        for (uint32_t col = 0; col < 64u; ++col) {
            auto& cell = app->cells[row * 64u + col];
            cell = {};
            cell.codepoint = (col < len) ? static_cast<uint32_t>(static_cast<unsigned char>(lines[row][col])) : static_cast<uint32_t>(' ');
            cell.foreground = {TerminalColorMode::TrueColor, rgb};
        }
    }
}

LoadedTexture create_spectrum_texture(Context* ctx) noexcept {
    constexpr uint32_t kW = 256u;
    constexpr uint32_t kH = 128u;
    auto* px = static_cast<uint8_t*>(std::malloc(size_t(kW) * size_t(kH) * 4u));
    if (!px) return {};

    const auto clamp01 = [](float v) noexcept -> float {
        return (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
    };
    const auto to_u8 = [&](float v) noexcept -> uint8_t {
        return static_cast<uint8_t>(clamp01(v) * 255.0f + 0.5f);
    };

    for (uint32_t y = 0; y < kH; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(kH);
        for (uint32_t x = 0; x < kW; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kW);

            // Rich deep-studio gradient backdrop
            float r = 0.055f + 0.035f * u + 0.020f * v;
            float g = 0.085f + 0.075f * u + 0.055f * v;
            float b = 0.145f + 0.105f * (1.0f - v) + 0.045f * u;

            // Fine high-frequency telemetry grid (exercises hardware mipmap filtering)
            const bool major_grid = (x % 32u == 0u) || (y % 32u == 0u);
            const bool minor_grid = (x % 8u == 0u) || (y % 8u == 0u);
            if (major_grid) {
                r += 0.09f; g += 0.13f; b += 0.17f;
            } else if (minor_grid) {
                r += 0.03f; g += 0.05f; b += 0.07f;
            }

            // Primary emerald-cyan telemetry wave + soft under-fill
            const float y1 = 0.54f - 0.19f * std::sin(u * 9.8f + 0.45f) - 0.08f * std::cos(u * 19.2f);
            const float d1 = std::fabs(v - y1);
            if (v > y1) {
                const float fill = std::exp(-(v - y1) * 5.2f) * 0.36f;
                r += fill * 0.08f;
                g += fill * 0.72f;
                b += fill * 0.62f;
            }
            const float glow1 = std::exp(-d1 * 34.0f);
            r += glow1 * 0.25f;
            g += glow1 * 0.92f;
            b += glow1 * 0.78f;

            // Secondary indigo-violet harmonic wave
            const float y2 = 0.44f + 0.15f * std::cos(u * 8.1f - 0.75f) + 0.06f * std::sin(u * 16.5f);
            const float d2 = std::fabs(v - y2);
            const float glow2 = std::exp(-d2 * 36.0f);
            r += glow2 * 0.48f;
            g += glow2 * 0.42f;
            b += glow2 * 0.96f;

            // Crisp glowing beacon nodes along the primary curve
            const float beacons[3] = {0.24f, 0.56f, 0.82f};
            for (int bi = 0; bi < 3; ++bi) {
                const float bx = beacons[bi];
                const float by = 0.54f - 0.19f * std::sin(bx * 9.8f + 0.45f) - 0.08f * std::cos(bx * 19.2f);
                const float dx = (u - bx) * 2.0f;
                const float dy = v - by;
                const float dist = std::sqrt(dx * dx + dy * dy);
                const float dot = std::exp(-dist * 28.0f);
                r += dot * 0.85f;
                g += dot * 0.98f;
                b += dot * 0.95f;
            }

            const size_t idx = (size_t(y) * size_t(kW) + size_t(x)) * 4u;
            px[idx + 0] = to_u8(r);
            px[idx + 1] = to_u8(g);
            px[idx + 2] = to_u8(b);
            px[idx + 3] = 255u;
        }
    }

    LoadedTexture tex = create_texture_rgba(ctx, {
        .rgba_pixels = px,
        .width = kW,
        .height = kH,
        .srgb = true,
        .debug_name = "showcase_spectrum_mip",
        .generate_mipmaps = true,
    });
    std::free(px);
    return tex;
}

void glass_node(const char* title, const char* subtitle, Vec2 pos, Color accent, bool light_mode) noexcept {
    const Color glass_bg = light_mode ? Color::rgba8(245, 250, 255, 182) : Color::rgba8(14, 22, 36, 138);
    const Color base_border = light_mode ? Color::rgba8(255, 255, 255, 185) : Color::rgba8(120, 165, 210, 96);
    const Color title_col = light_mode ? Color::rgb_hex(0x0F172A) : Color::white();
    const Color sub_col = light_mode ? Color::rgb_hex(0x334155) : Color::rgb_hex(0xB8C8DC);

    begin_box({
        .id = id(title),
        .axis = Axis::Col,
        .width = Sizing::fixed(206.0f),
        .height = Sizing::fixed(94.0f),
        .padding = Padding::all(16.0f),
        .gap = 8.0f,
        .background = glass_bg,
        .radius = Corners::all(14.0f),
        .attach_to = AttachTo::Parent,
        .anchor_parent = Anchor::TopLeft,
        .floating_offset = pos,
        .z_index = 2,
        .backdrop_blur = 14.0f,
        .continuous_corners = true,
        .hover_border_color = accent,
        .hover_scale = 1.025f,
        .press_scale = 0.98f,
        .border_beam = {.color = accent, .base_color = base_border, .width = 1.0f, .angle_rad = 0.8f, .glow_px = 9.0f},
        .top_rim_alpha = light_mode ? 0.45f : 0.35f,
        .elevation = Elevation::Md,
        .backdrop_saturation = 1.45f,
        .backdrop_brightness = 1.06f,
        .backdrop_noise = 0.015f,
    });
    label(title, 15.0f, title_col, kFontBold);
    label(subtitle, 11.0f, sub_col, kFontMono);
    end_box();
}

void build_ui(Context* ctx, Showcase* app) noexcept {
    const Theme& t = theme(ctx);
    const bool light = (t.id == ThemeId::Light);
    const Color emerald = light ? Color::rgb_hex(0x059669) : Color::oklch(0.82f, 0.14f, 165.0f);
    const Color indigo = light ? Color::rgb_hex(0x4F46E5) : Color::oklch(0.76f, 0.16f, 275.0f);
    const Color card_border = light ? Color::rgba8(15, 23, 42, 26) : Color::rgba8(255, 255, 255, 24);
    const Color well_bg = light ? Color::rgb_hex(0xEDF1F7) : Color::rgb_hex(0x090C12);
    const Color well_shadow = light ? Color::rgba8(15, 23, 42, 70) : Color::rgba8(0, 0, 0, 175);
    const Color tab_track_bg = light ? Color::rgb_hex(0xE6ECF4) : Color::rgb_hex(0x111520);
    const Color tab_pill_bg = light ? Color::white() : Color::rgb_hex(0x1E2A3A);
    const Color hero_card_bg = light ? Color::white() : Color::rgb_hex(0x121A24);

    const GradientDesc hero_pill_grad = {
        .kind = GradientKind::Linear,
        .oklab = true,
        .dir = {1.0f, 0.25f},
        .stop_count = 4u,
        .stops = {
            {Color::rgb_hex(0x6366F1), 0.00f},
            {Color::rgb_hex(0x3B82F6), 0.34f},
            {Color::rgb_hex(0x06B6D4), 0.68f},
            {Color::rgb_hex(0x10B981), 1.00f},
        },
    };
    const GradientDesc accent_bar_grad = {
        .kind = GradientKind::Linear,
        .oklab = true,
        .dir = {1.0f, 0.0f},
        .stop_count = 4u,
        .stops = {
            {Color::rgb_hex(0x6366F1), 0.00f},
            {Color::rgb_hex(0x0EA5E9), 0.35f},
            {Color::rgb_hex(0x10B981), 0.70f},
            {Color::rgb_hex(0xF59E0B), 1.00f},
        },
    };
    const GradientDesc signal_bar_grad = {
        .kind = GradientKind::Linear,
        .oklab = true,
        .dir = {1.0f, 0.0f},
        .stop_count = 3u,
        .stops = {
            {Color::rgb_hex(0x0EA5E9), 0.00f},
            {Color::rgb_hex(0x10B981), 0.55f},
            {Color::rgb_hex(0x34D399), 1.00f},
        },
    };

    const float bottom_pad = app->drawer ? 84.0f : 24.0f;
    begin_box({
        .axis = Axis::Col,
        .width = Sizing::grow(),
        .height = Sizing::grow(),
        .padding = {.left = 24.0f, .right = 24.0f, .top = 20.0f, .bottom = bottom_pad},
        .gap = 14.0f,
        .background = t.bg_canvas,
    });

    // Header row: Oklab gradient hero pill + title + subtle rotated badge + theme switch + version
    begin_box({
        .axis = Axis::Row,
        .width = Sizing::grow(),
        .height = Sizing::fixed(44.0f),
        .gap = 14.0f,
        .align_y = AlignY::Center,
    });
    begin_box({
        .padding = Padding::xy(14.0f, 6.0f),
        .align_x = AlignX::Center,
        .align_y = AlignY::Center,
        .radius = Corners::all(999.0f),
        .elevation = Elevation::Sm,
        .gradient = hero_pill_grad,
    });
    label("etch /", 16.0f, Color::white(), kFontBold);
    end_box();

    label("two worlds, one surface", 17.0f, t.text_primary);
    box({.width = Sizing::grow()});

    // Subtle rotated badge using LayerDesc::rotation_rad
    ETCH_LAYER({.pivot_norm = {0.5f, 0.5f}, .rotation_rad = -0.055f}) {
        begin_box({
            .padding = Padding::xy(10.0f, 4.0f),
            .align_x = AlignX::Center,
            .align_y = AlignY::Center,
            .background = light ? Color::rgba8(2, 132, 199, 24) : Color::rgba8(56, 189, 248, 28),
            .border_color = light ? Color::rgba8(2, 132, 199, 90) : Color::rgba8(56, 189, 248, 96),
            .border_width = 1.0f,
            .radius = Corners::all(999.0f),
        });
        label("PURE GPU", 10.5f, light ? Color::rgb_hex(0x0369A1) : Color::rgb_hex(0x7DD3FC), kFontMono);
        end_box();
    }

    if (button({
            .id = id("theme-toggle"),
            .label = light ? str_lit("Dark Theme") : str_lit("Light Theme"),
            .variant = ButtonVariant::Ghost,
            .height = Sizing::fixed(28.0f),
            .padding = Padding::xy(10.0f, 0.0f),
            .font_size_px = 11.5f,
        })) {
        app->light_mode = !app->light_mode;
        set_theme(app->light_mode ? Theme::light() : Theme::dark(), ctx);
        fill_terminal(app);
    }

    label("NATIVE  ·  " ETCH_VERSION_STRING, 11.0f, t.text_muted, kFontMono);
    end_box();

    // Multi-stop Oklab gradient accent hairline
    box({
        .width = Sizing::grow(),
        .height = Sizing::fixed(2.5f),
        .radius = Corners::all(2.0f),
        .gradient = accent_bar_grad,
    });

    // Main 2-column workspace
    begin_box({
        .axis = Axis::Row,
        .width = Sizing::grow(),
        .height = Sizing::grow(),
        .gap = 20.0f,
    });

    // Left column: Title, recessed pill tabs, Elevation::Lg Hero Status card, mipmapped artwork card, sheet button
    begin_box({
        .axis = Axis::Col,
        .width = Sizing::fixed(336.0f),
        .height = Sizing::grow(),
        .gap = 14.0f,
    });

    begin_box({.axis = Axis::Col, .width = Sizing::grow(), .gap = 3.0f});
    label("YOUR NETWORK", 10.5f, emerald, kFontMono);
    label("Everything flows.", 25.0f, t.text_primary, kFontBold);
    label("A quiet space for a busy connection.", 12.5f, t.text_muted);
    end_box();

    // Recessed segmented tab bar with sliding hero pill
    begin_box({
        .axis = Axis::Row,
        .width = Sizing::grow(),
        .height = Sizing::fixed(38.0f),
        .padding = Padding::all(4.0f),
        .gap = 4.0f,
        .background = tab_track_bg,
        .border_color = card_border,
        .border_width = 1.0f,
        .radius = Corners::all(999.0f),
        .shadow_color = light ? Color::rgba8(15, 23, 42, 42) : Color::rgba8(0, 0, 0, 120),
        .shadow_blur = 6.0f,
        .shadow_offset = {0.0f, 1.5f},
        .shadow_inset = true,
    });
    const char* tabs[] = {"Overview", "Traffic", "Routes"};
    for (uint32_t i = 0; i < 3u; ++i) {
        const uint32_t tab_id = id(tabs[i]);
        begin_box({
            .id = tab_id,
            .width = Sizing::grow(),
            .height = Sizing::grow(),
            .align_x = AlignX::Center,
            .align_y = AlignY::Center,
            .radius = Corners::all(999.0f),
        });
        if (i == app->tab) {
            box({
                .width = Sizing::grow(),
                .height = Sizing::grow(),
                .background = tab_pill_bg,
                .radius = Corners::all(999.0f),
                .attach_to = AttachTo::Parent,
                .anchor_parent = Anchor::TopLeft,
                .z_index = -1,
                .pointer_passthrough = true,
                .hero_id = id("sliding-pill"),
                .transition = SpringSpec::fluid(),
                .elevation = light ? Elevation::Sm : Elevation::None,
            });
        }
        label(tabs[i], 12.0f, (i == app->tab) ? (light ? t.text_primary : emerald) : t.text_muted,
              (i == app->tab) ? kFontBold : kFontSans);
        end_box();
        if (clicked(tab_id)) app->tab = i;
    }
    end_box();

    // Elevated Hero Status Card (Elevation::Lg)
    begin_box({
        .axis = Axis::Col,
        .width = Sizing::grow(),
        .height = Sizing::fixed(180.0f),
        .padding = Padding::all(18.0f),
        .gap = 8.0f,
        .background = hero_card_bg,
        .border_color = card_border,
        .border_width = 1.0f,
        .radius = Corners::all(16.0f),
        .continuous_corners = true,
        .border_beam = light ? BorderBeamStyle{} : BorderBeamStyle{.color = emerald, .glow_px = 3.0f},
        .top_rim_alpha = light ? 0.0f : 0.22f,
        .elevation = Elevation::Lg,
    });
    begin_box({.axis = Axis::Row, .width = Sizing::grow(), .align_y = AlignY::Center});
    label("CONNECTED", 10.5f, emerald, kFontMono);
    box({.width = Sizing::grow()});
    label("●  32 ms", 10.5f, emerald, kFontMono);
    end_box();

    label("Tokyo · 03", 24.0f, t.text_primary, kFontBold);
    label("Direct route  ·  AES-256-GCM", 12.5f, t.text_muted);
    box({.height = Sizing::grow()});

    // Signal throughput Oklab gradient bar
    begin_box({
        .width = Sizing::grow(),
        .height = Sizing::fixed(6.0f),
        .background = tab_track_bg,
        .radius = Corners::all(3.0f),
        .clip_children = true,
    });
    box({
        .width = Sizing::percent(0.78f),
        .height = Sizing::grow(),
        .radius = Corners::all(3.0f),
        .gradient = signal_bar_grad,
    });
    end_box();

    begin_box({.axis = Axis::Row, .gap = 28.0f});
    label("↓  128.4 Mbps", 13.0f, emerald, kFontMono);
    label("↑  24.8 Mbps", 13.0f, t.text_secondary, kFontMono);
    end_box();
    end_box();

    // Mipmapped Procedural Telemetry Artwork Card (Elevation::Md + ImageFit::Cover)
    begin_box({
        .axis = Axis::Col,
        .width = Sizing::grow(),
        .height = Sizing::grow(),
        .padding = Padding::all(16.0f),
        .gap = 10.0f,
        .background = t.bg_surface,
        .border_color = card_border,
        .border_width = 1.0f,
        .radius = Corners::all(16.0f),
        .continuous_corners = true,
        .top_rim_alpha = light ? 0.0f : 0.14f,
        .elevation = Elevation::Md,
    });
    begin_box({.axis = Axis::Row, .width = Sizing::grow(), .align_y = AlignY::Center});
    label("Spectrum, last 24 hours", 13.0f, t.text_primary, kFontBold);
    box({.width = Sizing::grow()});
    label("MIPMAP · COVER", 9.5f, t.text_muted, kFontMono);
    end_box();

    if (app->spectrum_tex) {
        image({
            .id = id("spectrum-artwork"),
            .texture = app->spectrum_tex.texture,
            .width = Sizing::grow(),
            .height = Sizing::grow(),
            .radius = Corners::all(10.0f),
            .border_color = card_border,
            .border_width = 1.0f,
            .continuous_corners = true,
            .fit = ImageFit::Cover,
            .source_size = {static_cast<float>(app->spectrum_tex.width), static_cast<float>(app->spectrum_tex.height)},
        });
    }
    end_box();

    if (button({
            .id = id("drawer-toggle"),
            .label = str_lit("Connection details  ↗"),
            .variant = ButtonVariant::Primary,
            .width = Sizing::grow(),
            .height = Sizing::fixed(36.0f),
        })) {
        app->drawer = !app->drawer;
    }
    end_box();

    // Right column: Aurora Material Canvas with optical vibrancy frosted glass nodes + recessed inset terminal well
    begin_box({
        .axis = Axis::Col,
        .width = Sizing::grow(),
        .height = Sizing::grow(),
        .gap = 14.0f,
    });
    begin_box({.axis = Axis::Row, .width = Sizing::grow(), .height = Sizing::fixed(24.0f), .align_y = AlignY::Center});
    label("CROMMA / MATERIAL WORKSPACE", 10.5f, t.text_muted, kFontMono);
    box({.width = Sizing::grow()});
    label("●  LIVE", 10.5f, emerald, kFontMono);
    end_box();

    begin_box({
        .id = id("aurora-canvas"),
        .width = Sizing::grow(),
        .height = Sizing::fixed(322.0f),
        .background = Color::rgb_hex(0x0D1A2B),
        .border_color = card_border,
        .border_width = 1.0f,
        .radius = Corners::all(18.0f),
        .clip_children = true,
        .continuous_corners = true,
        .elevation = Elevation::Lg,
    });
    const Rect bounds = element_bounds(id("aurora-canvas"));
    ShaderSurfaceEvalDesc eval = {.width = 796u, .height = 322u};
    eval.params[0].w = -1.0f;
    if (SDL_GPUTexture* texture = eval_shader_surface(ctx, app->aurora, eval)) {
        draw_image({
            .texture = texture,
            .rect = {0.0f, 0.0f, (bounds.w > 0.0f) ? bounds.w : 796.0f, 322.0f},
        });
    }
    const Vec2 wire[] = {{236.0f, 107.0f}, {306.0f, 107.0f}, {366.0f, 199.0f}, {432.0f, 199.0f}};
    draw_polyline({
        .points = wire,
        .color = Color::oklch(0.84f, 0.14f, 165.0f),
        .thickness = 1.75f,
        .glow_color = Color::rgba8(80, 240, 190, 120),
        .glow_px = 9.0f,
    });
    glass_node("01 / Planner", "Reasoning · ready", {36.0f, 60.0f}, emerald, light);
    glass_node("02 / Builder", "Vibrancy pass · active", {426.0f, 152.0f}, indigo, light);
    end_box();

    // Recessed Terminal / Metrics Well using shadow_inset
    begin_box({
        .axis = Axis::Col,
        .width = Sizing::grow(),
        .height = Sizing::grow(),
        .padding = Padding::all(18.0f),
        .gap = 10.0f,
        .background = well_bg,
        .border_color = card_border,
        .border_width = 1.0f,
        .radius = Corners::all(16.0f),
        .shadow_color = well_shadow,
        .shadow_blur = 12.0f,
        .shadow_offset = {0.0f, 3.0f},
        .clip_children = true,
        .continuous_corners = true,
        .shadow_inset = true,
    });
    begin_box({.axis = Axis::Row, .width = Sizing::grow(), .align_y = AlignY::Center});
    label("TERMINAL   /   native Slug & R8 glyphs", 10.0f, t.text_muted, kFontMono);
    box({.width = Sizing::grow()});
    label("RECESSED WELL", 9.5f, t.text_muted, kFontMono);
    end_box();

    terminal_grid({
        .id = id("showcase-terminal"),
        .cells = app->cells,
        .columns = 64u,
        .rows = 8u,
        .cell_width_px = 9.0f,
        .line_height_px = 20.0f,
        .font_size_px = 12.0f,
        .foreground = t.text_primary,
        .background = Color::transparent(),
        .cursor_column = 1,
        .cursor_row = 7,
        .cursor_color = emerald,
    });
    end_box();
    end_box();
    end_box();
    end_box();

    if (begin_sheet({
            .id = id("connection-sheet"),
            .open = app->drawer,
            .extent_px = 92.0f,
            .snap_points = {0.0f, 0.72f, 1.0f},
            .snap_count = 3u,
            .snap_index = &app->snap,
            .background = light ? Color::rgba8(255, 255, 255, 236) : Color::rgba8(22, 26, 36, 228),
            .border_color = card_border,
            .border_width = 1.0f,
            .backdrop_blur = 10.0f,
            .shadow_color = light ? Color::rgba8(15, 23, 42, 38) : Color::rgba8(0, 0, 0, 145),
            .shadow_blur = 20.0f,
            .shadow_offset = {0.0f, -4.0f},
        })) {
        begin_box({
            .axis = Axis::Row,
            .width = Sizing::grow(),
            .height = Sizing::grow(),
            .padding = Padding::xy(28.0f, 2.0f),
            .gap = 32.0f,
            .align_y = AlignY::Center,
        });
        label("Connection details", 15.0f, t.text_primary, kFontBold);
        label("AES-256-GCM  /  Tokyo  /  32 ms", 12.0f, emerald, kFontMono);
        box({.width = Sizing::grow()});
        label("DRAG TO SNAP  ↑ ↓", 10.0f, t.text_muted, kFontMono);
        end_box();
        end_sheet();
    }
}

} // namespace

int main(int argc, char** argv) {
    bool headless = false;
    bool light_mode = false;
    const char* out_png = "showcase.png";
    const char* gpu_driver = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            headless = true;
            if ((i + 1) < argc && argv[i + 1][0] != '-') {
                out_png = argv[++i];
            }
        } else if (std::strcmp(argv[i], "--light") == 0) {
            light_mode = true;
        } else if (std::strcmp(argv[i], "--renderer") == 0 && (i + 1) < argc) {
            gpu_driver = argv[++i];
            if (std::strcmp(gpu_driver, "d3d12") == 0) gpu_driver = "direct3d12";
        } else if (argv[i][0] != '-') {
            out_png = argv[i];
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) return 1;
    SDL_Window* window = headless
        ? nullptr
        : SDL_CreateWindow("Etch Showcase " ETCH_VERSION_STRING, 1200, 800,
                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!headless && !window) {
        SDL_Quit();
        return 1;
    }

    Context* ctx = create_context({.window = window, .headless = headless, .gpu_driver = gpu_driver});
    if (!ctx) {
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    set_theme(light_mode ? Theme::light() : Theme::dark(), ctx);

    Showcase app = {};
    app.light_mode = light_mode;
    fill_terminal(&app);
    app.aurora = create_shader_surface(ctx, {
        .spirv = {g_etch_surface_demo_spv, sizeof(g_etch_surface_demo_spv)},
        .dxil = {g_etch_surface_demo_dxil, sizeof(g_etch_surface_demo_dxil)},
        .msl = {g_etch_surface_demo_msl, sizeof(g_etch_surface_demo_msl)},
        .continuous_time = !headless,
        .max_fps = 30.0f,
    });
    app.spectrum_tex = create_spectrum_texture(ctx);

    bool ok = (app.aurora != 0u) && bool(app.spectrum_tex);
    bool running = ok;
    if (headless) {
        for (int i = 0; i < 90; ++i) {
            begin_frame(ctx, {.viewport_w = 1200u, .viewport_h = 800u, .dt_seconds = 1.0f / 60.0f});
            build_ui(ctx, &app);
            end_frame(ctx);
        }
        ok &= render_headless_png(ctx, 1200u, 800u, out_png, theme(ctx).bg_canvas);
        ok &= frame_stats(ctx).draw_call_count > 0u &&
              frame_stats(ctx).draw_call_count <= 32u &&
              element_bounds(id("aurora-canvas")).w > 500.0f;
        std::printf("Showcase material/layout/batch checks: %s\n", ok ? "PASS" : "FAIL");
    } else {
        uint64_t previous = SDL_GetTicksNS();
        while (running) {
            SDL_Event event;
            if (wait_events(ctx, &event)) {
                do {
                    process_event(ctx, event);
                    if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                        running = false;
                    }
                } while (SDL_PollEvent(&event));
            }
            if (!running) break;
            const uint64_t now = SDL_GetTicksNS();
            const float dt = static_cast<float>(static_cast<double>(now - previous) / 1e9);
            previous = now;
            begin_window_frame(ctx, window, dt);
            build_ui(ctx, &app);
            end_window_frame(ctx);
            ok &= render_window(ctx);
        }
    }

    destroy_texture(ctx, &app.spectrum_tex);
    if (app.aurora != 0u) destroy_shader_surface(ctx, app.aurora);
    destroy_context(ctx);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return ok ? 0 : 1;
}
