#include "etch_surface_sampler_test_dxil.h"
#include "etch_surface_sampler_test_spv.h"
#include "etch_surface_sampler_test_msl.h"

inline bool verify_v021_driver(const char* driver) noexcept {
    using namespace etch;
    Context* c = create_context({.headless = true, .gpu_driver = driver, .respect_reduced_motion = false});
    if (!c) return false;
    SDL_GPUDevice* dev = gpu_device(c);
    if (!dev || std::strcmp(SDL_GetGPUDeviceDriver(dev), driver) != 0) {
        destroy_context(c);
        set_current_context(nullptr);
        return false;
    }

    bool ok = true;
    const char* png_path = "v021-check.png";

    // (a1) LayerDesc::blur_px monotonic edge softening (0.0 -> 0.5 -> 2.4 -> 5.0 -> 12.0) & zero dark-halo.
    {
        const float blurs[5] = {0.0f, 0.5f, 2.4f, 5.0f, 12.0f};
        int center_r[5] = {};
        int halo_r[5] = {};
        for (int bi = 0; bi < 5; ++bi) {
            begin_frame(c, {.viewport_w = 200, .viewport_h = 120});
            ETCH_LAYER({.blur_px = blurs[bi]}) {
                begin_box({.width = Sizing::fixed(200), .height = Sizing::fixed(120)});
                draw_rect({.rect = {40.0f, 55.0f, 120.0f, 10.0f}, .fill = Color::white()});
                end_box();
            }
            end_frame(c);
            ok &= render_headless_png(c, 200, 120, png_path, Color::black());
            const bool bp_ok = (bi == 0) ? (frame_stats(c).blur_pass_count == 0u) : (frame_stats(c).blur_pass_count >= 1u);
            if (!bp_ok) std::fprintf(stderr, "v021 [%s] bi=%d blur_pass_count=%u\n", driver, bi, frame_stats(c).blur_pass_count);
            ok &= bp_ok;
            if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
                Uint8 r = 0, g = 0, b = 0, a = 0;
                ok &= SDL_ReadSurfacePixel(img, 100, 60, &r, &g, &b, &a);
                center_r[bi] = int(r);
                ok &= SDL_ReadSurfacePixel(img, 100, 50, &r, &g, &b, &a);
                halo_r[bi] = int(r);
                SDL_DestroySurface(img);
            } else {
                ok = false;
            }
        }
        // Center brightness drops monotonically as energy spreads out; halo spreads outward.
        ok &= (center_r[0] >= 253) && (center_r[0] >= center_r[1]) && (center_r[1] > center_r[2]) &&
              (center_r[2] > center_r[3]) && (center_r[3] > center_r[4]) && (center_r[4] > 40);
        ok &= (halo_r[0] == 0) && (halo_r[1] >= 0) && (halo_r[2] > 0) && (halo_r[3] >= halo_r[2]) && (halo_r[4] > 0);
        if (!ok) {
            std::fprintf(stderr, "v021 [%s] blur monotonic failed: passes=%u center=(%d,%d,%d,%d,%d) halo=(%d,%d,%d,%d,%d)\n",
                driver, frame_stats(c).blur_pass_count, center_r[0], center_r[1], center_r[2], center_r[3], center_r[4],
                halo_r[0], halo_r[1], halo_r[2], halo_r[3], halo_r[4]);
        }

        // Zero dark-halo: white box blurred over white background must stay pure white everywhere.
        begin_frame(c, {.viewport_w = 120, .viewport_h = 120});
        ETCH_LAYER({.blur_px = 6.0f}) {
            begin_box({.width = Sizing::fixed(120), .height = Sizing::fixed(120)});
            draw_rect({.rect = {40.0f, 40.0f, 40.0f, 40.0f}, .fill = Color::white()});
            end_box();
        }
        end_frame(c);
        ok &= render_headless_png(c, 120, 120, png_path, Color::white());
        ok &= frame_stats(c).blur_pass_count == 3u;
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            for (int x = 20; x <= 100; x += 2) {
                Uint8 r = 0, g = 0, b = 0, a = 0;
                const bool px_ok = SDL_ReadSurfacePixel(img, x, 60, &r, &g, &b, &a) && r >= 253 && g >= 253 && b >= 253;
                if (!px_ok) std::fprintf(stderr, "v021 [%s] dark halo at x=%d: %u,%u,%u,%u\n", driver, x, r, g, b, a);
                ok &= px_ok;
            }
            SDL_DestroySurface(img);
        } else {
            ok = false;
        }
    }

    // (a2) Overlapping blurred layers use distinct slices -> zero cross-bleed + blend_plus_lighter luminance boost.
    {
        begin_frame(c, {.viewport_w = 200, .viewport_h = 100});
        begin_box({.width = Sizing::fixed(200), .height = Sizing::fixed(100)});
        ETCH_LAYER({.blur_px = 10.0f}) {
            begin_box({.width = Sizing::fixed(200), .height = Sizing::fixed(20)});
            draw_rect({.rect = {20.0f, 20.0f, 160.0f, 20.0f}, .fill = Color::rgba8(255, 0, 0, 255)});
            end_box();
        }
        // Opaque black blocker covers entire canvas between line 1 and line 2.
        begin_box({.width = Sizing::fixed(200), .height = Sizing::fixed(100),
            .background = Color::black(), .attach_to = AttachTo::Root, .anchor_parent = Anchor::TopLeft, .z_index = 20});
        ETCH_LAYER({.blur_px = 10.0f}) {
            begin_box({.width = Sizing::fixed(200), .height = Sizing::fixed(100)});
            draw_rect({.rect = {20.0f, 44.0f, 160.0f, 20.0f}, .fill = Color::rgba8(0, 255, 0, 255)});
            end_box();
        }
        end_box();
        end_box();
        end_frame(c);
        ok &= render_headless_png(c, 200, 100, png_path, Color::black());
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            // Line 2 center must have green blur and ZERO red cross-bleed from line 1.
            const bool l2_ok = SDL_ReadSurfacePixel(img, 100, 54, &r, &g, &b, &a) && r == 0 && g > 80 && b == 0;
            if (!l2_ok) std::fprintf(stderr, "v021 [%s] line2=(%u,%u,%u,%u)\n", driver, r, g, b, a);
            ok &= l2_ok;
            SDL_DestroySurface(img);
        } else {
            ok = false;
        }

        // Plus-lighter additive boost over mid-blue backdrop & valid [0,1] alpha over transparent.
        begin_frame(c, {.viewport_w = 160, .viewport_h = 80});
        begin_box({.width = Sizing::fixed(160), .height = Sizing::fixed(80)});
        draw_rect({.rect = {10.0f, 10.0f, 60.0f, 60.0f}, .fill = Color::rgba8(100, 80, 120, 255)});
        draw_rect({.rect = {90.0f, 10.0f, 60.0f, 60.0f}, .fill = Color::rgba8(100, 80, 120, 180)});
        ETCH_LAYER({.blend_plus_lighter = true}) {
            begin_box({.width = Sizing::fixed(160), .height = Sizing::fixed(80)});
            draw_rect({.rect = {10.0f, 10.0f, 60.0f, 60.0f}, .fill = Color::rgba8(150, 80, 60, 255)});
            draw_rect({.rect = {90.0f, 10.0f, 60.0f, 60.0f}, .fill = Color::rgba8(150, 80, 60, 180)});
            end_box();
        }
        end_box();
        end_frame(c);
        ok &= render_headless_png(c, 160, 80, png_path, Color::transparent());
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            const bool a_ok = SDL_ReadSurfacePixel(img, 40, 40, &r, &g, &b, &a) && r > 170 && g > 105 && b > 125 && a == 255;
            if (!a_ok) std::fprintf(stderr, "v021 [%s] add1=(%u,%u,%u,%u)\n", driver, r, g, b, a);
            ok &= a_ok;
            const bool a2_ok = SDL_ReadSurfacePixel(img, 120, 40, &r, &g, &b, &a) && a >= 230 && a <= 235 && r > 120;
            if (!a2_ok) std::fprintf(stderr, "v021 [%s] add2=(%u,%u,%u,%u)\n", driver, r, g, b, a);
            ok &= a2_ok;
            SDL_DestroySurface(img);
        } else {
            ok = false;
        }
        if (!ok) std::fprintf(stderr, "v021 [%s] slice isolation / plus_lighter failed\n", driver);
    }

    // (b) TextDrawDesc & TextSpan has_wipe mid-glyph horizontal smooth feather.
    {
        const Str sample = str_lit("MMMM");
        const Vec2 m = measure_text(sample, kFontBold, 48.0f);
        const float wipe_mid = m.x * 0.5f;
        begin_frame(c, {.viewport_w = 320, .viewport_h = 160});
        begin_box({.axis = Axis::Col, .width = Sizing::fixed(320), .height = Sizing::fixed(160)});
        begin_box({.width = Sizing::fixed(320), .height = Sizing::fixed(80)});
        draw_text({
            .text = sample,
            .pos = {20.0f, 12.0f},
            .font_id = kFontBold,
            .size = 48.0f,
            .color = Color::rgba8(255, 0, 0, 255),
            .has_wipe = true,
            .color_end = Color::rgba8(0, 0, 255, 255),
            .wipe_x = wipe_mid,
            .wipe_feather_px = 24.0f,
        });
        end_box();
        text_spans({{
            .text = sample,
            .color = Color::rgba8(255, 0, 0, 255),
            .font_size_px = 48.0f,
            .font_id = kFontBold,
            .has_wipe = true,
            .color_end = Color::rgba8(0, 0, 255, 255),
            .wipe_x = wipe_mid,
            .wipe_feather_px = 24.0f,
        }});
        end_box();
        end_frame(c);
        ok &= render_headless_png(c, 320, 160, png_path, Color::black());
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            const auto check_wipe_row = [&](int y0, int y1, int x_origin) noexcept {
                bool saw_red = false, saw_mid = false, saw_blue = false;
                const int cx = x_origin + int(wipe_mid);
                for (int y = y0; y <= y1; ++y) {
                    for (int x = x_origin; x < x_origin + int(m.x); ++x) {
                        Uint8 r = 0, g = 0, b = 0, a = 0;
                        if (!SDL_ReadSurfacePixel(img, x, y, &r, &g, &b, &a)) return false;
                        if (x < cx - 20 && r > 180 && b < 20) saw_red = true;
                        if (x >= cx - 6 && x <= cx + 6 && r > 40 && b > 40) saw_mid = true;
                        if (x > cx + 20 && b > 180 && r < 20) saw_blue = true;
                    }
                }
                if (!saw_red || !saw_mid || !saw_blue) {
                    std::fprintf(stderr, "v021 [%s] wipe (%d..%d, x0=%d, m.x=%.1f): red=%d mid=%d blue=%d\n",
                        driver, y0, y1, x_origin, m.x, int(saw_red), int(saw_mid), int(saw_blue));
                }
                return saw_red && saw_mid && saw_blue;
            };
            ok &= check_wipe_row(18, 56, 20);
            ok &= check_wipe_row(85, 130, 0);
            SDL_DestroySurface(img);
        } else {
            ok = false;
        }
        if (!ok) std::fprintf(stderr, "v021 [%s] text wipe failed\n", driver);
    }

    // (c) ShaderSurface sampling an input SDL_GPUTexture* with num_samplers = 1 + sampler_mirror_repeat.
    {
        uint8_t tex_px[2 * 4] = {255, 0, 0, 255, 0, 0, 255, 255};
        LoadedTexture src_tex = create_texture_rgba(c, {.rgba_pixels = tex_px, .width = 2, .height = 1, .srgb = true});
        ok &= bool(src_tex);
        ok &= flush_texture_uploads(c);
        const ShaderSurfaceCreateDesc sdesc = {
            .spirv = {g_etch_surface_sampler_test_spv, sizeof(g_etch_surface_sampler_test_spv)},
            .dxil = {g_etch_surface_sampler_test_dxil, sizeof(g_etch_surface_sampler_test_dxil)},
            .msl = {g_etch_surface_sampler_test_msl, sizeof(g_etch_surface_sampler_test_msl)},
            .num_samplers = 1u,
            .sampler_mirror_repeat = true,
        };
        const ShaderSurfaceId sid = create_shader_surface(c, sdesc);
        ok &= (sid != 0u);
        if (sid == 0u) std::fprintf(stderr, "v021 [%s] create_shader_surface failed\n", driver);
        if (sid != 0u && src_tex) {
            for (int pass = 0; pass < 2; ++pass) {
                if (pass == 1) {
                    // Mutate input texture without changing revision or params -> must auto-dirty!
                    tex_px[0] = 0; tex_px[1] = 255; tex_px[2] = 0; tex_px[3] = 255;
                    ok &= update_texture_rgba(c, src_tex.texture, 2, 1, tex_px, 0u);
                }
                begin_frame(c, {.viewport_w = 64, .viewport_h = 16});
                ShaderSurfaceEvalDesc ed = {};
                ed.width = 64;
                ed.height = 16;
                ed.params[0] = {2.0f, 1.0f, 0.0f, 0.0f};
                ed.textures[0] = src_tex.texture;
                ed.enabled = true;
                SDL_GPUTexture* out_tex = eval_shader_surface(c, sid, ed);
                ok &= (out_tex != nullptr);
                begin_box({.width = Sizing::fixed(64), .height = Sizing::fixed(16)});
                draw_image({.texture = out_tex, .rect = {0.0f, 0.0f, 64.0f, 16.0f}});
                end_box();
                end_frame(c);
                ok &= render_headless_png(c, 64, 16, png_path, Color::black());
                if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
                    Uint8 r0 = 0, g0 = 0, b0 = 0, a0 = 0;
                    Uint8 r1 = 0, g1 = 0, b1 = 0, a1 = 0;
                    Uint8 r2 = 0, g2 = 0, b2 = 0, a2 = 0;
                    Uint8 r3 = 0, g3 = 0, b3 = 0, a3 = 0;
                    ok &= SDL_ReadSurfacePixel(img, 4, 8, &r0, &g0, &b0, &a0);
                    ok &= SDL_ReadSurfacePixel(img, 28, 8, &r1, &g1, &b1, &a1);
                    ok &= SDL_ReadSurfacePixel(img, 36, 8, &r2, &g2, &b2, &a2);
                    ok &= SDL_ReadSurfacePixel(img, 60, 8, &r3, &g3, &b3, &a3);
                    if (pass == 0) {
                        const bool p0 = (r0 > 220 && b0 < 15) && (b1 > 220 && r1 < 15) &&
                              (b2 > 220 && r2 < 15) && (r3 > 220 && b3 < 15);
                        if (!p0) std::fprintf(stderr, "v021 [%s] surf p0: (%u,%u,%u) (%u,%u,%u) (%u,%u,%u) (%u,%u,%u)\n",
                            driver, r0, g0, b0, r1, g1, b1, r2, g2, b2, r3, g3, b3);
                        ok &= p0;
                    } else {
                        const bool p1 = (g0 > 220 && r0 < 15) && (b1 > 220 && g1 < 15) &&
                              (b2 > 220 && g2 < 15) && (g3 > 220 && r3 < 15);
                        if (!p1) std::fprintf(stderr, "v021 [%s] surf p1: (%u,%u,%u) (%u,%u,%u) (%u,%u,%u) (%u,%u,%u)\n",
                            driver, r0, g0, b0, r1, g1, b1, r2, g2, b2, r3, g3, b3);
                        ok &= p1;
                    }
                    SDL_DestroySurface(img);
                } else {
                    ok = false;
                }
            }
            destroy_shader_surface(c, sid);
        }
        destroy_texture(c, &src_tex);
        if (!ok) std::fprintf(stderr, "v021 [%s] ShaderSurface sampler/mirror failed\n", driver);
    }

    // (c2) Surface-to-surface topological ordering & continuous_time dirty propagation (A created before B, plus self-reference guard).
    {
        const ShaderSurfaceCreateDesc cons_desc = {
            .spirv = {g_etch_surface_sampler_test_spv, sizeof(g_etch_surface_sampler_test_spv)},
            .dxil = {g_etch_surface_sampler_test_dxil, sizeof(g_etch_surface_sampler_test_dxil)},
            .msl = {g_etch_surface_sampler_test_msl, sizeof(g_etch_surface_sampler_test_msl)},
            .num_samplers = 1u,
        };
        const ShaderSurfaceCreateDesc src_desc = {
            .spirv = {g_etch_surface_demo_spv, sizeof(g_etch_surface_demo_spv)},
            .dxil = {g_etch_surface_demo_dxil, sizeof(g_etch_surface_demo_dxil)},
            .msl = {g_etch_surface_demo_msl, sizeof(g_etch_surface_demo_msl)},
            .continuous_time = true,
            .max_fps = 240.0f,
        };
        // Intentionally create consumer A BEFORE continuous source B so A occupies a lower slot index.
        const ShaderSurfaceId surf_a = create_shader_surface(c, cons_desc);
        const ShaderSurfaceId surf_b = create_shader_surface(c, src_desc);
        ok &= (surf_a != 0u && surf_b != 0u);
        if (surf_a != 0u && surf_b != 0u) {
            Uint8 rA1 = 0, gA1 = 0, bA1 = 0, rB1 = 0, gB1 = 0, bB1 = 0;
            Uint8 rA2 = 0, gA2 = 0, bA2 = 0, rB2 = 0, gB2 = 0, bB2 = 0;
            SDL_GPUTexture* last_tex_a = nullptr;
            SDL_GPUTexture* last_tex_b = nullptr;
            for (int frame = 0; frame < 2; ++frame) {
                if (frame == 1) SDL_Delay(10);
                begin_frame(c, {.viewport_w = 64, .viewport_h = 32, .dt_seconds = frame == 1 ? 2.2f : 0.0f});
                ShaderSurfaceEvalDesc eb = {};
                eb.width = 32;
                eb.height = 32;
                eb.params[0] = {0.0f, 0.0f, 0.0f, -1.0f}; // Use time-varying wave mode.
                eb.enabled = true;
                SDL_GPUTexture* tex_b = eval_shader_surface(c, surf_b, eb);
                ShaderSurfaceEvalDesc ea = {};
                ea.width = 32;
                ea.height = 32;
                ea.params[0] = {1.0f, 1.0f, 0.0f, 0.0f};
                ea.textures[0] = tex_b;
                ea.enabled = true;
                SDL_GPUTexture* tex_a = eval_shader_surface(c, surf_a, ea);
                last_tex_a = tex_a;
                last_tex_b = tex_b;
                ok &= (tex_a != nullptr && tex_b != nullptr);
                begin_box({.width = Sizing::fixed(64), .height = Sizing::fixed(32)});
                draw_image({.texture = tex_b, .rect = {0.0f, 0.0f, 32.0f, 32.0f}});
                draw_image({.texture = tex_a, .rect = {32.0f, 0.0f, 32.0f, 32.0f}});
                end_box();
                end_frame(c);
                ok &= render_headless_png(c, 64, 32, png_path, Color::black());
                if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
                    Uint8 aB = 0, aA = 0;
                    if (frame == 0) {
                        ok &= SDL_ReadSurfacePixel(img, 8, 20, &rB1, &gB1, &bB1, &aB);
                        ok &= SDL_ReadSurfacePixel(img, 40, 20, &rA1, &gA1, &bA1, &aA);
                        const bool f1_ok = std::abs(int(rA1) - int(rB1)) <= 4 &&
                            std::abs(int(gA1) - int(gB1)) <= 4 &&
                            std::abs(int(bA1) - int(bB1)) <= 4 && bA1 > 170;
                        if (!f1_ok) std::fprintf(stderr, "v021 [%s] surf-chain f1: B=(%u,%u,%u) A=(%u,%u,%u)\n",
                            driver, rB1, gB1, bB1, rA1, gA1, bA1);
                        ok &= f1_ok;
                    } else {
                        ok &= SDL_ReadSurfacePixel(img, 8, 20, &rB2, &gB2, &bB2, &aB);
                        ok &= SDL_ReadSurfacePixel(img, 40, 20, &rA2, &gA2, &bA2, &aA);
                        const bool f2_ok = std::abs(int(rA2) - int(rB2)) <= 4 &&
                            std::abs(int(gA2) - int(gB2)) <= 4 &&
                            std::abs(int(bA2) - int(bB2)) <= 4 &&
                            (int(gA1) - int(gA2) > 50) && (int(bA1) - int(bA2) > 50);
                        if (!f2_ok) std::fprintf(stderr, "v021 [%s] surf-chain f2: B=(%u,%u,%u) A=(%u,%u,%u) prevA=(%u,%u,%u)\n",
                            driver, rB2, gB2, bB2, rA2, gA2, bA2, rA1, gA1, bA1);
                        ok &= f2_ok;
                    }
                    SDL_DestroySurface(img);
                } else {
                    ok = false;
                }
            }
            // Frame N: surf_b renders a brand-new solid color while surf_a skips evaluation.
            begin_frame(c, {.viewport_w = 32, .viewport_h = 32});
            ShaderSurfaceEvalDesc eb_skip = {};
            eb_skip.width = 32;
            eb_skip.height = 32;
            eb_skip.params[0] = {1.0f, 0.25f, 0.0f, 1.0f};
            eb_skip.enabled = true;
            last_tex_b = eval_shader_surface(c, surf_b, eb_skip);
            begin_box({.width = Sizing::fixed(32), .height = Sizing::fixed(32)});
            draw_image({.texture = last_tex_b, .rect = {0.0f, 0.0f, 32.0f, 32.0f}});
            end_box();
            end_frame(c);
            ok &= render_headless_png(c, 32, 32, png_path, Color::black());
            // Frame N+1: surf_a returns with the same tex_b pointer & params; output_epoch mismatch must update surf_a!
            begin_frame(c, {.viewport_w = 32, .viewport_h = 32});
            ShaderSurfaceEvalDesc ea_ret = {};
            ea_ret.width = 32;
            ea_ret.height = 32;
            ea_ret.params[0] = {1.0f, 1.0f, 0.0f, 0.0f};
            ea_ret.textures[0] = last_tex_b;
            ea_ret.enabled = true;
            last_tex_a = eval_shader_surface(c, surf_a, ea_ret);
            begin_box({.width = Sizing::fixed(32), .height = Sizing::fixed(32)});
            draw_image({.texture = last_tex_a, .rect = {0.0f, 0.0f, 32.0f, 32.0f}});
            end_box();
            end_frame(c);
            ok &= render_headless_png(c, 32, 32, png_path, Color::black());
            if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
                Uint8 rS = 0, gS = 0, bS = 0, aS = 0;
                ok &= SDL_ReadSurfacePixel(img, 16, 16, &rS, &gS, &bS, &aS) &&
                    rS > 240 && gS > 120 && gS < 160 && bS < 10;
                SDL_DestroySurface(img);
            } else {
                ok = false;
            }
            // Self-reference guard in its own begin_frame/end_frame: feeding prev_surf_a_tex back into A must substitute fallback cleanly.
            begin_frame(c, {.viewport_w = 32, .viewport_h = 32});
            ShaderSurfaceEvalDesc self_ed = {};
            self_ed.width = 32;
            self_ed.height = 32;
            self_ed.params[0] = {1.0f, 1.0f, 0.0f, 0.0f};
            self_ed.textures[0] = last_tex_a;
            self_ed.revision = 1u;
            self_ed.enabled = true;
            SDL_GPUTexture* self_tex = eval_shader_surface(c, surf_a, self_ed);
            ok &= (self_tex != nullptr);
            begin_box({.width = Sizing::fixed(32), .height = Sizing::fixed(32)});
            draw_image({.texture = self_tex, .rect = {0.0f, 0.0f, 32.0f, 32.0f}});
            end_box();
            end_frame(c);
            ok &= render_headless_png(c, 32, 32, png_path, Color::black());
            if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
                Uint8 rW = 0, gW = 0, bW = 0, aW = 0;
                const bool w_ok = SDL_ReadSurfacePixel(img, 16, 16, &rW, &gW, &bW, &aW) &&
                    rW > 245 && gW > 245 && bW > 245 && aW > 245;
                if (!w_ok) std::fprintf(stderr, "v021 [%s] self-sample fallback=(%u,%u,%u,%u)\n", driver, rW, gW, bW, aW);
                ok &= w_ok;
                SDL_DestroySurface(img);
            } else {
                ok = false;
            }
            destroy_shader_surface(c, surf_b);
            destroy_shader_surface(c, surf_a);
        }
        if (!ok) std::fprintf(stderr, "v021 [%s] surface-to-surface DAG/propagation failed\n", driver);
    }

    // (d) ImageDesc::continuous_corners & ImageDrawDesc::continuous_corners superellipse vs circular corner diff.
    {
        uint8_t white_px[4 * 4] = {
            255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255,
        };
        LoadedTexture white_tex = create_texture_rgba(c, {.rgba_pixels = white_px, .width = 2, .height = 2, .srgb = true});
        ok &= bool(white_tex);
        ok &= flush_texture_uploads(c);

        begin_frame(c, {.viewport_w = 200, .viewport_h = 200});
        begin_box({.axis = Axis::Col, .width = Sizing::fixed(200), .height = Sizing::fixed(200), .gap = 20.0f});
        begin_box({.axis = Axis::Row, .width = Sizing::fixed(200), .height = Sizing::fixed(80), .gap = 20.0f});
        image({.texture = white_tex.texture, .width = Sizing::fixed(80), .height = Sizing::fixed(80),
               .radius = Corners::all(24.0f), .continuous_corners = false});
        image({.texture = white_tex.texture, .width = Sizing::fixed(80), .height = Sizing::fixed(80),
               .radius = Corners::all(24.0f), .continuous_corners = true});
        end_box();
        begin_box({.width = Sizing::fixed(200), .height = Sizing::fixed(80)});
        draw_image({.texture = white_tex.texture, .rect = {0.0f, 0.0f, 80.0f, 80.0f}, .radius = 24.0f, .continuous_corners = false});
        draw_image({.texture = white_tex.texture, .rect = {100.0f, 0.0f, 80.0f, 80.0f}, .radius = 24.0f, .continuous_corners = true});
        end_box();
        end_box();
        end_frame(c);
        ok &= render_headless_png(c, 200, 200, png_path, Color::black());
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            Uint8 rc = 0, gc = 0, bc = 0, ac = 0;
            Uint8 rs = 0, gs = 0, bs = 0, as = 0;
            // At (5, 5) relative to 80x80 with r=24: circular corner is outside (black), squircle is inside (white).
            const bool c0 = SDL_ReadSurfacePixel(img, 5, 5, &rc, &gc, &bc, &ac) && rc < 15;
            const bool s0 = SDL_ReadSurfacePixel(img, 105, 5, &rs, &gs, &bs, &as) && rs > 180;
            if (!c0 || !s0) std::fprintf(stderr, "v021 [%s] img0: circ=(%u,%u,%u,%u) sq=(%u,%u,%u,%u)\n", driver, rc, gc, bc, ac, rs, gs, bs, as);
            ok &= c0 && s0;
            const bool c1 = SDL_ReadSurfacePixel(img, 5, 105, &rc, &gc, &bc, &ac) && rc < 15;
            const bool s1 = SDL_ReadSurfacePixel(img, 105, 105, &rs, &gs, &bs, &as) && rs > 180;
            if (!c1 || !s1) std::fprintf(stderr, "v021 [%s] img1: circ=(%u,%u,%u,%u) sq=(%u,%u,%u,%u)\n", driver, rc, gc, bc, ac, rs, gs, bs, as);
            ok &= c1 && s1;
            SDL_DestroySurface(img);
        } else {
            ok = false;
        }
        destroy_texture(c, &white_tex);
        if (!ok) std::fprintf(stderr, "v021 [%s] continuous_corners image failed\n", driver);
    }

    std::remove(png_path);
    destroy_context(c);
    set_current_context(nullptr);
    return ok;
}

inline bool verify_v021_gpu() noexcept {
    return verify_v021_driver("direct3d12") && verify_v021_driver("vulkan");
}
