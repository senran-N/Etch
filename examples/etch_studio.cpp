// Etch Studio 1.0 — Pure-GPU Render-Graph & 1M Telemetry Workbench.
// Showcases all 12 built-in Etch 1.0 primitives in Orthodox C++20 with zero
// per-frame heap allocations, 1-3 draw-call batching, and "--headless" validation.

#include "etch.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

inline constexpr uint32_t kPreviewWidth = 256u;
inline constexpr uint32_t kPreviewHeight = 256u;
inline constexpr uint32_t kNodeCount = 5u;
inline constexpr uint32_t kTraceRowCount = 1000000u;
inline constexpr float kTraceRowHeight = 26.0f;
inline constexpr float kDividerPx = 6.0f;
inline constexpr float kNodePortOffsetY = 44.0f;

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
    { "Cyber Neon",      "High bloom + chromatic glow",  0.94f, 1.38f, etch::Color::rgb_hex(0xF472B6), etch::Color::rgb_hex(0x22D3EE) },
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
    etch::Context* ctx = nullptr;
    SDL_Window* window = nullptr;
    SDL_GPUTexture* preview_tex = nullptr;

    GraphNode nodes[kNodeCount] = {
        { "Camera HDR",     "RGBA16F Linear",   { 36.0f,  42.0f }, 196.0f, etch::Color::rgb_hex(0x38BDF8), UINT32_MAX, UINT32_MAX },
        { "Simplex Noise",  "fBm 4 Octaves",    { 36.0f, 224.0f }, 196.0f, etch::Color::rgb_hex(0xA78BFA), UINT32_MAX, UINT32_MAX },
        { "Kawase Bloom",   "Dual-Pass 14 px",  {312.0f, 126.0f }, 204.0f, etch::Color::rgb_hex(0xF472B6), 0u,         1u         },
        { "ACES ToneMap",   "Filmic LUT v1.0",  {584.0f,  42.0f }, 204.0f, etch::Color::rgb_hex(0xFBBF24), 2u,         UINT32_MAX },
        { "Viewport Out",   "Zero-Copy GPU",    {584.0f, 198.0f }, 228.0f, etch::Color::rgb_hex(0x34D399), 3u,         UINT32_MAX },
    };

    uint32_t selected_node = 2u;
    uint32_t pending_wire_src = UINT32_MAX;
    uint32_t selected_preset = 0u;
    float target_bloom = kPresets[0].bloom;
    float target_exposure = kPresets[0].exposure;
    float uploaded_bloom = -1.0f;
    float uploaded_exposure = -1.0f;
    uint32_t uploaded_preset = UINT32_MAX;
    bool wire_glow_enabled = true;
    bool preset_popover_open = false;
    bool running = true;

    char filter_buf[256] = "Kawase";
    etch::TextInputResult filter_res = {};
    etch::VirtualListSlice last_trace_slice = {};
};

constexpr float clamp_f(float x, float lo, float hi) noexcept {
    return (x < lo) ? lo : ((x > hi) ? hi : x);
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
    SDL_GPUDevice* dev = etch::gpu_device(app->ctx);
    if (!dev) {
        return false;
    }

    if (!app->preview_tex) {
        const SDL_GPUTextureCreateInfo tex_info = {
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
            .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
            .width = kPreviewWidth,
            .height = kPreviewHeight,
            .layer_count_or_depth = 1u,
            .num_levels = 1u,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .props = 0u,
        };
        app->preview_tex = SDL_CreateGPUTexture(dev, &tex_info);
        if (!app->preview_tex) {
            return false;
        }
    }

    const uint32_t byte_size = kPreviewWidth * kPreviewHeight * 4u;
    const SDL_GPUTransferBufferCreateInfo tbuf_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = byte_size,
        .props = 0u,
    };
    SDL_GPUTransferBuffer* tbuf = SDL_CreateGPUTransferBuffer(dev, &tbuf_info);
    if (!tbuf) {
        return false;
    }

    uint8_t* mapped = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(dev, tbuf, false));
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(dev, tbuf);
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

            uint8_t* dst = mapped + (size_t(py) * kPreviewWidth + px) * 4u;
            dst[0] = to_srgb_u8(aces_tonemap(hdr_r));
            dst[1] = to_srgb_u8(aces_tonemap(hdr_g));
            dst[2] = to_srgb_u8(aces_tonemap(hdr_b));
            dst[3] = 255u;
        }
    }

    SDL_UnmapGPUTransferBuffer(dev, tbuf);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    bool uploaded = false;
    if (cmd) {
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        if (copy) {
            const SDL_GPUTextureTransferInfo src = {
                .transfer_buffer = tbuf,
                .offset = 0u,
                .pixels_per_row = kPreviewWidth,
                .rows_per_layer = kPreviewHeight,
            };
            const SDL_GPUTextureRegion dst_reg = {
                .texture = app->preview_tex,
                .mip_level = 0u,
                .layer = 0u,
                .x = 0u,
                .y = 0u,
                .z = 0u,
                .w = kPreviewWidth,
                .h = kPreviewHeight,
                .d = 1u,
            };
            SDL_UploadToGPUTexture(copy, &src, &dst_reg, false);
            SDL_EndGPUCopyPass(copy);
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
    if (uploaded) {
        app->uploaded_bloom = bloom;
        app->uploaded_exposure = exposure;
        app->uploaded_preset = preset_idx;
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

    const uint32_t kBtnPresets = etch::id("studio_btn_presets");
    const uint32_t kBtnResetView = etch::id("studio_btn_reset_view");
    const uint32_t kBtnHud = etch::id("studio_btn_hud");
    const uint32_t kBtnClearFilter = etch::id("studio_btn_clear_filter");
    const uint32_t kBtnBloomDown = etch::id("studio_btn_bloom_down");
    const uint32_t kBtnBloomUp = etch::id("studio_btn_bloom_up");
    const uint32_t kBtnExpDown = etch::id("studio_btn_exp_down");
    const uint32_t kBtnExpUp = etch::id("studio_btn_exp_up");
    const uint32_t kBtnWireGlow = etch::id("studio_btn_wire_glow");
    const uint32_t kBtnRow0 = etch::id("studio_btn_row0");
    const uint32_t kBtnRow500k = etch::id("studio_btn_row500k");
    const uint32_t kBtnRowEnd = etch::id("studio_btn_rowend");
    const uint32_t kSplitH = etch::id("studio_split_h");
    const uint32_t kSplitV = etch::id("studio_split_v");
    const uint32_t kCanvas = etch::id("studio_canvas");
    const uint32_t kTraceList = etch::id("studio_trace_list");
    const uint32_t kFilterInput = etch::id("studio_filter");

    if (etch::clicked(kBtnPresets)) {
        app->preset_popover_open = !app->preset_popover_open;
    }
    if (etch::clicked(kBtnResetView)) {
        etch::set_canvas_view(kCanvas, etch::Vec2{36.0f, 28.0f}, 1.0f);
    }
    if (etch::clicked(kBtnHud)) {
        etch::set_debug_hud_open(app->ctx, !etch::debug_hud_open(app->ctx));
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
    if (etch::clicked(kBtnRowEnd)) {
        etch::virtual_list_scroll_to_row(kTraceList, 999990u, kTraceRowHeight, etch::AlignY::Top);
    }

    const etch::CanvasState prev_cs = etch::canvas_state(kCanvas);

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
        // Top Toolbar
        ETCH_BOX({
            .id = etch::id("studio_toolbar"),
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
                .padding = etch::Padding::xy(9.0f, 4.0f),
                .background = etch::Color::rgb_hex(0x0EA5E9),
                .background_end = etch::Color::rgb_hex(0x6366F1),
                .gradient_dir = {1.0f, 0.0f},
                .radius = etch::Corners::all(6.0f),
            }) {
                etch::text(etch::str_lit("ETCH 1.0"), {
                    .color = etch::Color::white(),
                    .font_size_px = 12.0f,
                    .wrap = false,
                });
            }

            etch::text(etch::str_lit("Etch Studio — GPU Render-Graph & 1M Trace Workbench"), {
                .color = etch::Color::rgb_hex(0xE2E8F0),
                .font_size_px = 14.0f,
                .wrap = false,
            });

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
        }

        // Main 3-Pane Split Workspace
        etch::begin_split(etch::SplitDesc{
            .id = kSplitH,
            .axis = etch::Axis::Row,
            .width = etch::Sizing::grow(),
            .height = etch::Sizing::grow(),
            .default_ratio = 0.25f,
            .min_first_px = 280.0f,
            .min_second_px = 520.0f,
            .divider_size_px = kDividerPx,
        });

        // Pane 1: Left Inspector (UTF-8 IME filter + Spring shader controls + Inertial Node List)
        ETCH_BOX({
            .axis = etch::Axis::Col,
            .width = etch::Sizing::grow(),
            .height = etch::Sizing::grow(),
            .padding = etch::Padding::all(12.0f),
            .gap = 10.0f,
            .background = etch::Color::rgb_hex(0x0F1522),
            .border_color = etch::Color::rgba8(148, 163, 184, 38),
            .border_width = 1.0f,
            .radius = etch::Corners::all(10.0f),
        }) {
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
                .width = etch::Sizing::grow(),
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
                for (uint32_t i = 0u; i < kNodeCount; ++i) {
                    const uint32_t item_id = etch::id_index(etch::id("studio_node_dir"), i);
                    if (etch::clicked(item_id)) {
                        app->selected_node = i;
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
                    const bool is_sel = (app->selected_node == i);
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

        // Right Column Split: Infinite 2D Canvas (Top) / 1M Virtual Trace List (Bottom)
        etch::split_second();
        etch::begin_split(etch::SplitDesc{
            .id = kSplitV,
            .axis = etch::Axis::Col,
            .width = etch::Sizing::grow(),
            .height = etch::Sizing::grow(),
            .default_ratio = 0.62f,
            .min_first_px = 260.0f,
            .min_second_px = 180.0f,
            .divider_size_px = kDividerPx,
        });

        // Pane 2: Infinite 2D Node-Graph Canvas
        etch::begin_canvas(etch::CanvasDesc{
            .id = kCanvas,
            .width = etch::Sizing::grow(),
            .height = etch::Sizing::grow(),
            .default_pan = {36.0f, 28.0f},
            .default_zoom = 1.0f,
            .min_zoom = 0.20f,
            .max_zoom = 6.0f,
            .grid_step_world = 24.0f,
            .major_grid_subdiv = 4u,
            .background = etch::Color::rgb_hex(0x090D16),
            .minor_grid_color = etch::Color::rgba8(148, 163, 184, 34),
            .major_grid_color = etch::Color::rgba8(56, 189, 248, 30),
            .border_color = etch::Color::rgba8(148, 163, 184, 44),
            .border_width = 1.0f,
            .radius = etch::Corners::all(10.0f),
            .show_grid = true,
            .interactive = true,
        });

        const float wire_glow = app->wire_glow_enabled ? 5.5f : 0.0f;
        const auto emit_link = [&](uint32_t src_idx, uint32_t dst_idx) noexcept {
            if (src_idx >= kNodeCount || dst_idx >= kNodeCount || src_idx == dst_idx) {
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

        for (uint32_t i = 0u; i < kNodeCount; ++i) {
            if (app->nodes[i].input_from < kNodeCount) {
                emit_link(app->nodes[i].input_from, i);
            }
            if (app->nodes[i].aux_from < kNodeCount) {
                emit_link(app->nodes[i].aux_from, i);
            }
        }

        for (uint32_t i = 0u; i < kNodeCount; ++i) {
            GraphNode& node = app->nodes[i];
            const uint32_t node_id = etch::id_index(etch::id("studio_node"), i);
            const uint32_t in_port_id = etch::id_index(etch::id("studio_port_in"), i);
            const uint32_t out_port_id = etch::id_index(etch::id("studio_port_out"), i);

            if (etch::clicked(out_port_id)) {
                app->pending_wire_src = (app->pending_wire_src == i) ? UINT32_MAX : i;
                app->selected_node = i;
            }
            if (etch::clicked(in_port_id)) {
                if (app->pending_wire_src < kNodeCount && app->pending_wire_src != i) {
                    node.input_from = app->pending_wire_src;
                    app->pending_wire_src = UINT32_MAX;
                }
                app->selected_node = i;
            }

            const bool is_sel = (app->selected_node == i);
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
            if (nr.pressed || nr.dragging || nr.clicked) {
                app->selected_node = i;
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
                ETCH_BOX({
                    .width = etch::Sizing::fixed(8.0f),
                    .height = etch::Sizing::fixed(8.0f),
                    .background = node.accent,
                    .radius = etch::Corners::all(4.0f),
                }) {}
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
                    const bool has_wire = (node.input_from < kNodeCount);
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
                if (i < kNodeCount - 1u) {
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

        // Pane 3: 1,000,000-Row Virtual Telemetry Log
        etch::split_second();
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
                    "GPU Trace Virtual List  [%u..%u) / 1,000,000 rows",
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

            const etch::VirtualListSlice slice = etch::begin_virtual_list(etch::VirtualListDesc{
                .id = kTraceList,
                .item_count = kTraceRowCount,
                .item_height_px = kTraceRowHeight,
                .row_gap_px = 0.0f,
                .overscan_rows = 2u,
                .width = etch::Sizing::grow(),
                .height = etch::Sizing::grow(),
                .padding = etch::Padding::xy(6.0f, 4.0f),
                .background = etch::Color::rgb_hex(0x090D16),
                .border_color = etch::Color::rgba8(148, 163, 184, 34),
                .border_width = 1.0f,
                .radius = etch::Corners::all(8.0f),
                .show_scrollbar = true,
            });
            app->last_trace_slice = slice;

            const bool has_filter = (app->filter_buf[0] != '\0');
            for (uint32_t row = slice.first_index; row < slice.end_index; ++row) {
                const uint32_t pass_idx = row % 6u;
                const float gpu_us = 18.0f + static_cast<float>((row * 37u) % 140u) * 0.45f;
                const uint32_t prim_est = 920u + (row * 13u) % 480u;
                char row_line[160] = {};
                std::snprintf(
                    row_line,
                    sizeof(row_line),
                    "#%07u   %-26s   %6.1f us   %4u prims   1-3 DC",
                    row,
                    kTracePassNames[pass_idx],
                    gpu_us,
                    prim_est);
                const bool matched = has_filter && (std::strstr(row_line, app->filter_buf) != nullptr);
                const etch::Color row_bg = matched
                    ? etch::Color::rgba8(14, 116, 144, 92)
                    : ((row & 1u) ? etch::Color::rgba8(30, 41, 59, 44) : etch::Color::transparent());
                ETCH_BOX({
                    .axis = etch::Axis::Row,
                    .width = etch::Sizing::grow(),
                    .height = etch::Sizing::fixed(kTraceRowHeight),
                    .padding = etch::Padding::xy(10.0f, 0.0f),
                    .align_y = etch::AlignY::Center,
                    .background = row_bg,
                    .border_color = matched ? etch::Color::rgba8(56, 189, 248, 140) : etch::Color::transparent(),
                    .border_width = matched ? 1.0f : 0.0f,
                    .radius = etch::Corners::all(4.0f),
                }) {
                    etch::text(etch::str_cstr(row_line), {
                        .color = matched ? etch::Color::rgb_hex(0xE0F2FE) : etch::Color::rgb_hex(0xCBD5E1),
                        .font_size_px = 12.0f,
                        .wrap = false,
                    });
                }
            }
            etch::end_virtual_list();
        }

        etch::end_split();
        etch::end_split();
    }

    // Floating Dual-Kawase Frosted-Glass Preset Popover (NO clip_children on floating element)
    if (app->preset_popover_open) {
        ETCH_BOX({
            .id = etch::id("studio_preset_popover"),
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
    }
}

void step_headless_frame(
    StudioApp* app,
    uint32_t w,
    uint32_t h,
    float dt_seconds,
    etch::Vec2 pointer_pos = {-1.0f, -1.0f},
    bool pointer_down = false) noexcept
{
    const etch::FrameStats prev = etch::frame_stats(app->ctx);
    etch::begin_frame(app->ctx, etch::FrameDesc{
        .viewport_w = w,
        .viewport_h = h,
        .dpi_scale = 1.0f,
        .dt_seconds = dt_seconds,
        .pointer_pos = pointer_pos,
        .pointer_down = pointer_down,
    });
    build_studio_ui(app, prev);
    etch::end_frame(app->ctx);
}

void render_window_frame(StudioApp* app, float dt_seconds) noexcept {
    int px_w = 1440;
    int px_h = 900;
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
}

bool SDLCALL window_event_watch(void* userdata, SDL_Event* event) noexcept {
    StudioApp* app = static_cast<StudioApp*>(userdata);
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
        std::fprintf(stderr, "Etch Studio headless error: create_context failed (%s)\n", SDL_GetError());
        return 1;
    }

    StudioApp app = { .ctx = ctx };
    constexpr uint32_t kW = 1440u;
    constexpr uint32_t kH = 900u;
    constexpr float kDt = 1.0f / 60.0f;

    const auto cleanup_release = [&]() noexcept {
        SDL_GPUDevice* dev = etch::gpu_device(ctx);
        if (dev && app.preview_tex) {
            SDL_WaitForGPUIdle(dev);
            SDL_ReleaseGPUTexture(dev, app.preview_tex);
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
    const auto click_point = [&](etch::Vec2 pt) noexcept {
        step_headless_frame(&app, kW, kH, kDt, pt, true);
        step_headless_frame(&app, kW, kH, kDt, pt, false);
    };

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
        app.last_trace_slice.end_index < 999999u ||
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

    const char* driver = SDL_GetGPUDeviceDriver(etch::gpu_device(ctx));
    std::printf(
        "Headless Studio OK (%s) -> %s (asserts=6/6, prims=%u, draw_calls=%u, blur_pass=%u, slug_glyphs=%u, grid=%u, wire=%u, layout_us=%.0f, vrows=[%u..%u))\n",
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
        "Etch Studio 1.0 — GPU Render-Graph & 1M Trace Workbench",
        1440,
        900,
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

    StudioApp app = {
        .ctx = ctx,
        .window = window,
        .running = true,
    };

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
                    if (app.preset_popover_open) {
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

    SDL_GPUDevice* dev = etch::gpu_device(ctx);
    if (dev && app.preview_tex) {
        SDL_WaitForGPUIdle(dev);
        SDL_ReleaseGPUTexture(dev, app.preview_tex);
    }
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
