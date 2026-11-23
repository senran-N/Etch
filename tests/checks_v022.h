inline bool verify_v022_driver(const char* driver) noexcept {
    using namespace etch;
    Context* c = create_context({.enable_gpu_debug = true, .headless = true, .gpu_driver = driver, .respect_reduced_motion = false});
    if (!c) return false;
    SDL_GPUDevice* dev = gpu_device(c);
    if (!dev || std::strcmp(SDL_GetGPUDeviceDriver(dev), driver) != 0) {
        destroy_context(c);
        set_current_context(nullptr);
        return false;
    }

    bool ok = true;
    const char* png_path = "v022-check.png";

    // (1) push_layer pivot_norm binding to the first top-level box under explicit layer.
    {
        const uint32_t box_id = id("v022_pivot_box");
        begin_frame(c, {.viewport_w = 400, .viewport_h = 240});
        begin_box({.width = Sizing::fixed(400), .height = Sizing::fixed(240), .padding = {.left = 200, .top = 100}});
        ETCH_LAYER({.scale = {0.5f, 0.5f}, .pivot_norm = {1.0f, 0.5f}}) {
            box({.id = box_id, .width = Sizing::fixed(100), .height = Sizing::fixed(40), .background = Color::rgba8(240, 60, 60, 255)});
        }
        end_box();
        end_frame(c);
        ok &= render_headless_png(c, 400, 240, png_path, Color::black());
        // Box at (200, 100, 100, 40) with pivot_norm (1.0, 0.5) => pivot (300, 120).
        // Scaled by (0.5, 0.5) => visual rect [250..300, 110..130].
        ElementGeometry geom = {};
        ok &= element_geometry(box_id, &geom);
        const bool geom_ok = std::fabs(geom.bounds.x - 250.0f) < 1.0f &&
            std::fabs(geom.bounds.y - 110.0f) < 1.0f &&
            std::fabs(geom.bounds.w - 50.0f) < 1.0f &&
            std::fabs(geom.bounds.h - 20.0f) < 1.0f;
        if (!geom_ok) {
            std::fprintf(stderr, "v022 [%s] pivot_norm geom=(%.1f,%.1f,%.1f,%.1f)\n",
                driver, geom.bounds.x, geom.bounds.y, geom.bounds.w, geom.bounds.h);
        }
        ok &= geom_ok;
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            const bool hit_inside = SDL_ReadSurfacePixel(img, 275, 120, &r, &g, &b, &a) && r > 200 && g < 80;
            const bool miss_origin = SDL_ReadSurfacePixel(img, 125, 60, &r, &g, &b, &a) && r < 10 && g < 10 && b < 10;
            const bool miss_left = SDL_ReadSurfacePixel(img, 220, 120, &r, &g, &b, &a) && r < 10 && g < 10 && b < 10;
            if (!hit_inside || !miss_origin || !miss_left) {
                std::fprintf(stderr, "v022 [%s] pivot_norm pixels failed (%d,%d,%d)\n",
                    driver, int(hit_inside), int(miss_origin), int(miss_left));
            }
            ok &= hit_inside && miss_origin && miss_left;
            SDL_DestroySurface(img);
        } else {
            ok = false;
        }
    }

    // (2) BoxDesc::edge_fade static per-side edge fade mask on non-scroll clipped container.
    {
        begin_frame(c, {.viewport_w = 120, .viewport_h = 120});
        begin_box({
            .width = Sizing::fixed(100),
            .height = Sizing::fixed(80),
            .clip_children = true,
            .edge_fade = {.left = 20.0f, .right = 20.0f, .top = 20.0f, .bottom = 20.0f},
        });
        box({.width = Sizing::fixed(100), .height = Sizing::fixed(80), .background = Color::white()});
        end_box();
        end_frame(c);
        ok &= render_headless_png(c, 120, 120, png_path, Color::black());
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            Uint8 rt = 0, rm = 0, rc = 0, rb = 0, rl = 0, rr = 0, g = 0, b = 0, a = 0;
            ok &= SDL_ReadSurfacePixel(img, 50, 2, &rt, &g, &b, &a);
            ok &= SDL_ReadSurfacePixel(img, 50, 10, &rm, &g, &b, &a);
            ok &= SDL_ReadSurfacePixel(img, 50, 40, &rc, &g, &b, &a);
            ok &= SDL_ReadSurfacePixel(img, 50, 77, &rb, &g, &b, &a);
            ok &= SDL_ReadSurfacePixel(img, 2, 40, &rl, &g, &b, &a);
            ok &= SDL_ReadSurfacePixel(img, 97, 40, &rr, &g, &b, &a);
            const bool fade_ok = rt < rm && rm < rc && rc >= 250 && rb < rm && rl < 60 && rr < 60;
            if (!fade_ok) {
                std::fprintf(stderr, "v022 [%s] edge_fade failed: top=%u mid=%u ctr=%u bot=%u l=%u r=%u\n",
                    driver, rt, rm, rc, rb, rl, rr);
            }
            ok &= fade_ok;
            SDL_DestroySurface(img);
        } else {
            ok = false;
        }
    }

    // (3) TextDrawDesc shadow_color + shadow_offset + shadow_blur rendering.
    {
        begin_frame(c, {.viewport_w = 320, .viewport_h = 160});
        begin_box({.width = Sizing::fixed(320), .height = Sizing::fixed(160)});
        draw_text({
            .text = str_lit("II"),
            .pos = {24.0f, 20.0f},
            .font_id = kFontBold,
            .size = 48.0f,
            .color = Color::rgba8(255, 0, 0, 255),
            .shadow_color = Color::rgba8(0, 255, 0, 255),
            .shadow_offset = {10.0f, 6.0f},
            .shadow_blur = 0.0f,
        });
        draw_text({
            .text = str_lit("II"),
            .pos = {160.0f, 20.0f},
            .font_id = kFontBold,
            .size = 48.0f,
            .color = Color::white(),
            .shadow_color = Color::rgba8(0, 120, 255, 220),
            .shadow_offset = {0.0f, 0.0f},
            .shadow_blur = 10.0f,
        });
        end_box();
        end_frame(c);
        ok &= render_headless_png(c, 320, 160, png_path, Color::black());
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            bool saw_sharp_green = false, saw_sharp_red = false, saw_glow_blue = false, saw_glow_white = false;
            for (int y = 10; y < 100; ++y) {
                for (int x = 10; x < 140; ++x) {
                    Uint8 r = 0, g = 0, b = 0, a = 0;
                    if (SDL_ReadSurfacePixel(img, x, y, &r, &g, &b, &a)) {
                        if (g > 180 && r < 30 && b < 30) saw_sharp_green = true;
                        if (r > 180 && g < 30 && b < 30) saw_sharp_red = true;
                    }
                }
                for (int x = 145; x < 240; ++x) {
                    Uint8 r = 0, g = 0, b = 0, a = 0;
                    if (SDL_ReadSurfacePixel(img, x, y, &r, &g, &b, &a)) {
                        if (b > 20 && r < 40) saw_glow_blue = true;
                        if (r > 200 && g > 200 && b > 200) saw_glow_white = true;
                    }
                }
            }
            if (!saw_sharp_green || !saw_sharp_red || !saw_glow_blue || !saw_glow_white) {
                std::fprintf(stderr, "v022 [%s] text shadow failed: sg=%d sr=%d gb=%d gw=%d\n",
                    driver, int(saw_sharp_green), int(saw_sharp_red), int(saw_glow_blue), int(saw_glow_white));
            }
            ok &= saw_sharp_green && saw_sharp_red && saw_glow_blue && saw_glow_white;
            SDL_DestroySurface(img);
        } else {
            ok = false;
        }
    }

    // (4) LayerDesc::blur_px with 6 overlapping groups (>4 slices), R8G8B8A8_UNORM_SRGB, child blend_plus_lighter, and 120-frame idle release.
    {
        begin_frame(c, {.viewport_w = 240, .viewport_h = 160});
        begin_box({.width = Sizing::fixed(240), .height = Sizing::fixed(160)});
        // 6 overlapping blurred layers (slices 0..5), with an opaque blocker before slice 5 to isolate slice cross-bleed.
        for (int i = 0; i < 6; ++i) {
            if (i == 5) {
                begin_box({.width = Sizing::fixed(0), .height = Sizing::fixed(0)});
                draw_rect({.rect = {0.0f, 0.0f, 120.0f, 160.0f}, .fill = Color::rgba8(20, 20, 20, 255)});
                end_box();
            }
            const Color col = (i == 5) ? Color::rgba8(0, 255, 0, 255) : Color::rgba8(255, 0, 0, 255);
            ETCH_LAYER({.blur_px = 6.0f}) {
                begin_box({.width = Sizing::fixed(0), .height = Sizing::fixed(0)});
                draw_rect({.rect = {20.0f, 20.0f + float(i) * 4.0f, 80.0f, 16.0f}, .fill = col});
                end_box();
            }
        }
        // Blurred group WITHOUT blend_plus_lighter containing a dim base rect + a child WITH blend_plus_lighter.
        ETCH_LAYER({.blur_px = 4.0f}) {
            begin_box({.width = Sizing::fixed(0), .height = Sizing::fixed(0)});
            draw_rect({.rect = {130.0f, 30.0f, 70.0f, 70.0f}, .fill = Color::rgba8(100, 80, 120, 255)});
            ETCH_LAYER({.blend_plus_lighter = true}) {
                begin_box({.width = Sizing::fixed(0), .height = Sizing::fixed(0)});
                draw_rect({.rect = {130.0f, 30.0f, 70.0f, 70.0f}, .fill = Color::rgba8(150, 80, 60, 255)});
                end_box();
            }
            end_box();
        }
        end_box();
        end_frame(c);
        ok &= render_headless_png(c, 240, 160, png_path, Color::rgba8(20, 20, 20, 255));
        ok &= frame_stats(c).blur_pass_count >= 6u;
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            // Slice 5 (green) must not cross-bleed red from slices 0..4.
            const bool slice_ok = SDL_ReadSurfacePixel(img, 60, 48, &r, &g, &b, &a) && g > 70 && r < 40;
            // Child blend_plus_lighter boosts red luminance inside blurred group (>165).
            const bool child_add_ok = SDL_ReadSurfacePixel(img, 165, 65, &r, &g, &b, &a) && r > 165 && g > 95;
            if (!slice_ok || !child_add_ok) {
                std::fprintf(stderr, "v022 [%s] 8-slice/child-plus-lighter failed: slice_ok=%d child_add=%d (%u,%u,%u)\n",
                    driver, int(slice_ok), int(child_add_ok), r, g, b);
            }
            ok &= slice_ok && child_add_ok;
            SDL_DestroySurface(img);
        } else {
            ok = false;
        }

        // 125 idle frames (no filter groups) -> triggers 120-frame VRAM release, then re-activates cleanly.
        for (int f = 0; f < 125; ++f) {
            begin_frame(c, {.viewport_w = 240, .viewport_h = 160});
            box({.width = Sizing::fixed(40), .height = Sizing::fixed(40), .background = Color::white()});
            end_frame(c);
            ok &= render_headless_png(c, 240, 160, png_path, Color::black());
        }
        begin_frame(c, {.viewport_w = 240, .viewport_h = 160});
        ETCH_LAYER({.blur_px = 6.0f}) {
            begin_box({.width = Sizing::fixed(240), .height = Sizing::fixed(160)});
            draw_rect({.rect = {40.0f, 40.0f, 60.0f, 40.0f}, .fill = Color::white()});
            end_box();
        }
        end_frame(c);
        ok &= render_headless_png(c, 240, 160, png_path, Color::black());
        ok &= frame_stats(c).blur_pass_count >= 2u;
    }

    // (5) load_system_font & is_window_minimized checks.
    {
        const uint16_t sys_reg = load_system_font(c, str_lit("Segoe UI"), FontWeight::Bold);
        const uint16_t sys_mono = load_system_font(c, str_lit("Consolas"), FontWeight::Mono);
        const uint16_t sys_fb = load_system_font(c, str_lit("EtchNonExistentFontXYZ_022"), FontWeight::Regular);
        const bool font_ok = sys_reg != kInvalidFont && sys_mono != kInvalidFont && sys_fb != kInvalidFont &&
            is_font_loaded(c, sys_reg) && is_font_loaded(c, sys_mono) && is_font_loaded(c, sys_fb) &&
            measure_text(str_lit("Etch"), sys_reg, 16.0f).x > 0.0f &&
            !is_window_minimized(c);
        if (!font_ok) std::fprintf(stderr, "v022 [%s] load_system_font/is_window_minimized failed\n", driver);
        ok &= font_ok;
    }

    // (6) ShaderSurfaceCreateDesc::enable_feedback ping-pong persistence across frames + resize.
    {
        uint8_t split_px[2 * 4] = {255, 0, 0, 255, 0, 0, 255, 255};
        LoadedTexture seed_tex = create_texture_rgba(c, {.rgba_pixels = split_px, .width = 2, .height = 1, .srgb = true});
        ok &= bool(seed_tex);
        ok &= flush_texture_uploads(c);
        const ShaderSurfaceCreateDesc fb_desc = {
            .spirv = {g_etch_surface_sampler_test_spv, sizeof(g_etch_surface_sampler_test_spv)},
            .dxil = {g_etch_surface_sampler_test_dxil, sizeof(g_etch_surface_sampler_test_dxil)},
            .msl = {g_etch_surface_sampler_test_msl, sizeof(g_etch_surface_sampler_test_msl)},
            .num_samplers = 1u,
            .enable_feedback = true,
        };
        const ShaderSurfaceId fb_id = create_shader_surface(c, fb_desc);
        ok &= (fb_id != 0u);
        if (fb_id != 0u && seed_tex) {
            SDL_GPUTexture* fb_tex = nullptr;
            // Frame 0: seed left=RED, right=BLUE.
            begin_frame(c, {.viewport_w = 64, .viewport_h = 32});
            ShaderSurfaceEvalDesc e0 = {.width = 64, .height = 32, .revision = 1u, .enabled = true};
            e0.params[0] = {1.0f, 1.0f, 0.0f, 0.0f};
            e0.textures[0] = seed_tex.texture;
            fb_tex = eval_shader_surface(c, fb_id, e0);
            begin_box({.width = Sizing::fixed(64), .height = Sizing::fixed(32)});
            draw_image({.texture = fb_tex, .rect = {0.0f, 0.0f, 64.0f, 32.0f}});
            end_box();
            end_frame(c);
            ok &= render_headless_png(c, 64, 32, png_path, Color::black());

            // Frame 1: self-reference fb_tex with UV shift +0.5 in X -> left side samples previous right (BLUE)!
            begin_frame(c, {.viewport_w = 64, .viewport_h = 32});
            ShaderSurfaceEvalDesc e1 = {.width = 64, .height = 32, .revision = 2u, .enabled = true};
            e1.params[0] = {1.0f, 1.0f, 0.5f, 0.0f};
            e1.textures[0] = fb_tex;
            fb_tex = eval_shader_surface(c, fb_id, e1);
            begin_box({.width = Sizing::fixed(64), .height = Sizing::fixed(32)});
            draw_image({.texture = fb_tex, .rect = {0.0f, 0.0f, 64.0f, 32.0f}});
            end_box();
            end_frame(c);
            ok &= render_headless_png(c, 64, 32, png_path, Color::black());
            if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
                Uint8 r = 0, g = 0, b = 0, a = 0;
                const bool p1_ok = SDL_ReadSurfacePixel(img, 24, 16, &r, &g, &b, &a) && b > 220 && r < 20;
                if (!p1_ok) std::fprintf(stderr, "v022 [%s] feedback f1=(%u,%u,%u,%u)\n", driver, r, g, b, a);
                ok &= p1_ok;
                SDL_DestroySurface(img);
            } else {
                ok = false;
            }

            // Frame 2: self-reference again with zero UV shift -> sample at x=24 must remain BLUE (persisted from Frame 1)!
            begin_frame(c, {.viewport_w = 64, .viewport_h = 32});
            ShaderSurfaceEvalDesc e2 = {.width = 64, .height = 32, .revision = 3u, .enabled = true};
            e2.params[0] = {1.0f, 1.0f, 0.0f, 0.0f};
            e2.textures[0] = fb_tex;
            fb_tex = eval_shader_surface(c, fb_id, e2);
            begin_box({.width = Sizing::fixed(64), .height = Sizing::fixed(32)});
            draw_image({.texture = fb_tex, .rect = {0.0f, 0.0f, 64.0f, 32.0f}});
            end_box();
            end_frame(c);
            ok &= render_headless_png(c, 64, 32, png_path, Color::black());
            if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
                Uint8 r = 0, g = 0, b = 0, a = 0;
                const bool p2_ok = SDL_ReadSurfacePixel(img, 24, 16, &r, &g, &b, &a) && b > 220 && r < 20;
                if (!p2_ok) std::fprintf(stderr, "v022 [%s] feedback f2=(%u,%u,%u,%u)\n", driver, r, g, b, a);
                ok &= p2_ok;
                SDL_DestroySurface(img);
            } else {
                ok = false;
            }

            // Frame 3: resize while self-referencing -> falls back cleanly to white (no uninitialized read!).
            begin_frame(c, {.viewport_w = 48, .viewport_h = 48});
            ShaderSurfaceEvalDesc e3 = {.width = 48, .height = 48, .revision = 4u, .enabled = true};
            e3.params[0] = {1.0f, 1.0f, 0.0f, 0.0f};
            e3.textures[0] = fb_tex;
            fb_tex = eval_shader_surface(c, fb_id, e3);
            begin_box({.width = Sizing::fixed(48), .height = Sizing::fixed(48)});
            draw_image({.texture = fb_tex, .rect = {0.0f, 0.0f, 48.0f, 48.0f}});
            end_box();
            end_frame(c);
            ok &= render_headless_png(c, 48, 48, png_path, Color::black());
            if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
                Uint8 r = 0, g = 0, b = 0, a = 0;
                const bool p3_ok = SDL_ReadSurfacePixel(img, 24, 24, &r, &g, &b, &a) && r > 240 && g > 240 && b > 240;
                if (!p3_ok) std::fprintf(stderr, "v022 [%s] feedback resize=(%u,%u,%u,%u)\n", driver, r, g, b, a);
                ok &= p3_ok;
                SDL_DestroySurface(img);
            } else {
                ok = false;
            }

            destroy_shader_surface(c, fb_id);
        }
        destroy_texture(c, &seed_tex);
    }

    destroy_context(c);
    set_current_context(nullptr);

    // (7) Fresh context first-frame + resize combination of LayerDesc::blur_px and BoxDesc::backdrop_blur.
    if (Context* fc = create_context({.enable_gpu_debug = true, .headless = true, .gpu_driver = driver, .respect_reduced_motion = false})) {
        auto run_combo = [&](uint32_t vw, uint32_t vh, const char* tag) noexcept -> bool {
            begin_frame(fc, {.viewport_w = vw, .viewport_h = vh});
            begin_box({.width = Sizing::fixed(float(vw)), .height = Sizing::fixed(float(vh))});
            ETCH_LAYER({.blur_px = 6.0f}) {
                begin_box({.width = Sizing::fixed(0), .height = Sizing::fixed(0)});
                draw_rect({.rect = {20.0f, 20.0f, 80.0f, 60.0f}, .fill = Color::rgba8(0, 220, 80, 255)});
                end_box();
            }
            begin_box({.width = Sizing::fixed(float(vw)), .height = Sizing::fixed(float(vh)), .padding = {.left = 140, .top = 20}});
            box({.width = Sizing::fixed(80), .height = Sizing::fixed(60), .background = Color::rgba8(30, 50, 90, 140), .backdrop_blur = 8.0f});
            end_box();
            end_box();
            end_frame(fc);
            if (!render_headless_png(fc, vw, vh, png_path, Color::rgba8(16, 16, 20, 255))) return false;
            if (frame_stats(fc).blur_pass_count < 2u) return false;
            SDL_Surface* img = SDL_LoadPNG(png_path);
            if (!img) return false;
            Uint8 r = 0, g = 0, b = 0, a = 0;
            const bool layer_ok = SDL_ReadSurfacePixel(img, 60, 50, &r, &g, &b, &a) &&
                g > 150 && r < 40 && b < 110;
            Uint8 br = 0, bg = 0, bb = 0, ba = 0;
            const bool backdrop_ok = SDL_ReadSurfacePixel(img, 180, 50, &br, &bg, &bb, &ba) &&
                bb > br && bb > 30;
            if (!layer_ok || !backdrop_ok) {
                std::fprintf(stderr, "v022 [%s] %s blur+backdrop_blur failed: layer=(%u,%u,%u,%u) backdrop=(%u,%u,%u,%u)\n",
                    driver, tag, r, g, b, a, br, bg, bb, ba);
            }
            SDL_DestroySurface(img);
            return layer_ok && backdrop_ok;
        };
        ok &= run_combo(240u, 160u, "first_frame");
        ok &= run_combo(280u, 180u, "resize");
        // (8) FilterBlurGroup must preserve inner sharp-corner clip_children rects (not erase them).
        {
            begin_frame(fc, {.viewport_w = 200, .viewport_h = 120});
            begin_box({.width = Sizing::fixed(200), .height = Sizing::fixed(120)});
            ETCH_LAYER({.blur_px = 4.0f}) {
                begin_box({.width = Sizing::fixed(60), .height = Sizing::fixed(120), .clip_children = true});
                box({.width = Sizing::fixed(180), .height = Sizing::fixed(120), .background = Color::rgba8(240, 40, 40, 255)});
                end_box();
            }
            end_box();
            end_frame(fc);
            ok &= render_headless_png(fc, 200, 120, png_path, Color::black());
            if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
                Uint8 r_in = 0, g_in = 0, b_in = 0, a_in = 0;
                Uint8 r_out = 0, g_out = 0, b_out = 0, a_out = 0;
                const bool in_ok = SDL_ReadSurfacePixel(img, 30, 60, &r_in, &g_in, &b_in, &a_in) && r_in > 180;
                const bool out_ok = SDL_ReadSurfacePixel(img, 140, 60, &r_out, &g_out, &b_out, &a_out) && r_out < 15;
                if (!in_ok || !out_ok) {
                    std::fprintf(stderr, "v022 [%s] blur inner clip failed: in=(%u,%u,%u) out=(%u,%u,%u)\n",
                        driver, r_in, g_in, b_in, r_out, g_out, b_out);
                }
                ok &= in_ok && out_ok;
                SDL_DestroySurface(img);
            } else {
                ok = false;
            }
        }
        destroy_context(fc);
        set_current_context(nullptr);
    } else {
        ok = false;
    }

    std::remove(png_path);
    return ok;
}

inline bool verify_v022_gpu() noexcept {
    if (const char* forced = SDL_GetHint(SDL_HINT_GPU_DRIVER); forced && *forced) return verify_v022_driver(forced);
#if defined(_WIN32)
    return verify_v022_driver("direct3d12") && verify_v022_driver("vulkan");
#elif defined(__APPLE__)
    return verify_v022_driver("metal");
#else
    return verify_v022_driver("vulkan");
#endif
}
