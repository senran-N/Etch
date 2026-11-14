// Etch v0.3-A sandbox & "--headless" verification harness.
// Strictly follows Orthodox C++20 (zero exceptions, RTTI, virtual trees, or hot-path heap containers).

#include "etch.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

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
    SDL_GPUDevice* dev = etch::gpu_device(ctx);
    if (!dev) {
        return nullptr;
    }

    const SDL_GPUTextureCreateInfo tex_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = kOscWidth,
        .height = kOscHeight,
        .layer_count_or_depth = 1u,
        .num_levels = 1u,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0u,
    };
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(dev, &tex_info);
    if (!tex) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateGPUTexture (osc) failed: %s", SDL_GetError());
        return nullptr;
    }

    const uint32_t byte_size = kOscWidth * kOscHeight * 4u;
    const SDL_GPUTransferBufferCreateInfo tbuf_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = byte_size,
        .props = 0u,
    };
    SDL_GPUTransferBuffer* tbuf = SDL_CreateGPUTransferBuffer(dev, &tbuf_info);
    if (!tbuf) {
        SDL_ReleaseGPUTexture(dev, tex);
        return nullptr;
    }

    uint8_t* dst = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(dev, tbuf, false));
    if (!dst) {
        SDL_ReleaseGPUTransferBuffer(dev, tbuf);
        SDL_ReleaseGPUTexture(dev, tex);
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

    SDL_UnmapGPUTransferBuffer(dev, tbuf);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    bool uploaded = false;
    if (cmd) {
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
        if (copy_pass) {
            const SDL_GPUTextureTransferInfo src = {
                .transfer_buffer = tbuf,
                .offset = 0u,
                .pixels_per_row = kOscWidth,
                .rows_per_layer = kOscHeight,
            };
            const SDL_GPUTextureRegion region = {
                .texture = tex,
                .mip_level = 0u,
                .layer = 0u,
                .x = 0u,
                .y = 0u,
                .z = 0u,
                .w = kOscWidth,
                .h = kOscHeight,
                .d = 1u,
            };
            SDL_UploadToGPUTexture(copy_pass, &src, &region, false);
            SDL_EndGPUCopyPass(copy_pass);
            SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
            if (fence) {
                uploaded = SDL_WaitForGPUFences(dev, true, &fence, 1u);
                SDL_ReleaseGPUFence(dev, fence);
            }
        } else {
            SDL_CancelGPUCommandBuffer(cmd);
        }
    }

    SDL_ReleaseGPUTransferBuffer(dev, tbuf);
    if (!uploaded) {
        SDL_ReleaseGPUTexture(dev, tex);
        return nullptr;
    }
    return tex;
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
                text("ETCH v0.6", {
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
                text("Etch Pure-GPU Engine v0.6 (Slug · HarfBuzz · Kawase)", {
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

                // Card 4: Live v0.6 Telemetry & Slug Specimen
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
                    text("v0.6 Telemetry (Slug \xE6\x9B\xB2\xE7\xBA\xBF \xC2\xB7 HarfBuzz \xC2\xB7 Kawase)", {
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
                            .text = str_lit("v1.1 "),
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
                            .text = str_lit("v1.0"),
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
                            .id = id("v12_plot_box"),
                            .width = Sizing::grow(),
                            .height = Sizing::fixed(44.0f),
                            .background = Color::rgba8(15, 23, 42, 160),
                            .radius = Corners::all(6.0f),
                        }) {
                            static constexpr Vec2 kSparkPts[5] = {
                                { 8.0f, 32.0f }, { 36.0f, 20.0f }, { 64.0f, 26.0f }, { 96.0f, 12.0f }, { 128.0f, 16.0f },
                            };
                            draw_polyline({
                                .points = kSparkPts,
                                .color = Color::rgba8(148, 163, 184, 150),
                                .thickness = 1.4f,
                            });
                            draw_bezier({
                                .p0 = { 8.0f, 34.0f },
                                .p1 = { 46.0f, 4.0f },
                                .p2 = { 88.0f, 40.0f },
                                .p3 = { 128.0f, 10.0f },
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
    if (!app || !app->ctx || !app->window) {
        return;
    }

    int px_w = 1280;
    int px_h = 800;
    SDL_GetWindowSizeInPixels(app->window, &px_w, &px_h);
    if (px_w <= 0 || px_h <= 0) {
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
}

bool SDLCALL window_event_watch(void* userdata, SDL_Event* event) noexcept {
    AppState* app = static_cast<AppState*>(userdata);
    if (!app || !event) {
        return true;
    }
    if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
        event->type == SDL_EVENT_WINDOW_EXPOSED)
    {
        etch::request_redraw(app->ctx);
        render_window_frame(app, 0.0f);
    }
    return true;
}

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
            SDL_ReleaseGPUTexture(dev, osc_tex);
        }
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

    const bool png_ok = etch::render_headless_png(
        ctx,
        kHeadlessW,
        kHeadlessH,
        out_png_path,
        etch::Color::rgb_hex(0x0B0D12));

    const etch::FrameStats final_stats = etch::frame_stats(ctx);
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

    const bool ok = png_ok && stats_ok && slug_ok && v10a_ok && v11_ok && v12_ok;
    std::printf(
        "Headless %s -> %s (asserts=12/12, prims=%u, draw_calls=%u, slug_glyphs=%u, slug_curves=%u, layout_ms=%.3f, gpu_submit_ms=%.3f, arena_used=%zuB, scroll_y=%.1f, focus=0x%08X, idle=%d)\n",
        ok ? "OK" : "FAILED",
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

int run_interactive() noexcept {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Etch v1.2 — Pure-GPU UI Engine",
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

    SDL_AddEventWatch(window_event_watch, &app);
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

    SDL_RemoveEventWatch(window_event_watch, &app);

    SDL_GPUDevice* dev = etch::gpu_device(ctx);
    if (dev && osc_tex) {
        SDL_WaitForGPUIdle(dev);
        SDL_ReleaseGPUTexture(dev, osc_tex);
    }
    etch::destroy_context(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    bool headless = false;
    const char* out_png = "frame_check.png";

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
