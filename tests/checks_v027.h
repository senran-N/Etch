#pragma once
// Milestone B (v027) GPU & Motion regression checks:
// - Elevation presets, inset shadows, and multi-layer custom ShadowLayer stacks
// - Multi-stop Linear, Radial, and Conic gradients (Oklab & Linear) on BoxDesc & RectDrawDesc
// - Optical vibrancy BlurGlass (saturation, brightness, micro-grain)
// - Full 2D subtree affine rotation (LayerDesc::rotation_rad) + inverse occlusion hitbox
// - Hardware mipmaps (TextureDesc::generate_mipmaps) + ImageFit (Contain & Cover)
// - Easing curves, timed tween with delay, and stagger_delay
// - Single-digit draw call batching on rich combined cards

inline bool verify_v027_driver(const char* driver) noexcept {
    using namespace etch;
    Context* c = create_context({.headless = true, .gpu_driver = driver, .respect_reduced_motion = false});
    if (!c) return false;
    SDL_GPUDevice* dev = gpu_device(c);
    if (!dev || (driver != nullptr && std::strcmp(SDL_GetGPUDeviceDriver(dev), driver) != 0)) {
        destroy_context(c);
        set_current_context(nullptr);
        return false;
    }
    set_current_context(c);

    bool ok = true;
    const char* drv_label = driver ? driver : "default";
    const auto check = [&](bool cond, int line) noexcept {
        if (!cond) {
            ok = false;
            std::fprintf(stderr, "v027 [%s] check failed at line %d\n", drv_label, line);
        }
    };
    const char* png_path = "v027-check.png";

    // ------------------------------------------------------------------
    // (a) Elevation & Inset Shadows + Multi-Layer Custom ShadowLayer[2]
    // ------------------------------------------------------------------
    {
        const Color canvas = Color::rgb_hex(0xF6F7F9);
        begin_frame(c, {.viewport_w = 540, .viewport_h = 200});
        begin_box({
            .axis = Axis::Row,
            .width = Sizing::fixed(540.0f),
            .height = Sizing::fixed(200.0f),
            .padding = Padding::xy(36.0f, 40.0f),
            .gap = 64.0f,
            .background = canvas,
        });
        // Card 1 [36..136, 40..140]: Elevation::Lg soft outer shadow
        box({
            .width = Sizing::fixed(100.0f),
            .height = Sizing::fixed(100.0f),
            .background = Color::white(),
            .radius = Corners::all(12.0f),
            .elevation = Elevation::Lg,
        });
        // Card 2 [200..300, 40..140]: Recessed well with inset shadow (zero outer bleed)
        box({
            .width = Sizing::fixed(100.0f),
            .height = Sizing::fixed(100.0f),
            .background = Color::white(),
            .radius = Corners::all(10.0f),
            .shadow_color = Color::rgba8(0, 0, 0, 160),
            .shadow_blur = 10.0f,
            .shadow_offset = {0.0f, 3.0f},
            .shadow_inset = true,
        });
        // Card 3 [364..464, 40..140]: Custom multi-layer shadows[2] (1 drop + 1 inset)
        box({
            .width = Sizing::fixed(100.0f),
            .height = Sizing::fixed(100.0f),
            .background = Color::white(),
            .radius = Corners::all(10.0f),
            .shadows = {
                ShadowLayer{.offset = {0.0f, 8.0f}, .blur_px = 12.0f, .spread_px = 0.0f, .color = Color::rgba8(0, 0, 0, 140), .inset = false},
                ShadowLayer{.offset = {0.0f, 4.0f}, .blur_px = 10.0f, .spread_px = 0.0f, .color = Color::rgba8(0, 0, 0, 160), .inset = true},
            },
            .shadow_count = 2u,
        });
        end_box();
        end_frame(c);

        check(render_headless_png(c, 540u, 200u, png_path, canvas), __LINE__);
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            // Canvas reference at (4, 4)
            check(SDL_ReadSurfacePixel(img, 4, 4, &r, &g, &b, &a) &&
                  std::abs(int(r) - 246) <= 2 && std::abs(int(g) - 247) <= 2 && std::abs(int(b) - 249) <= 2, __LINE__);

            // Card 1: white interior + darkened soft outer shadow below bottom edge y=140
            check(SDL_ReadSurfacePixel(img, 86, 90, &r, &g, &b, &a) && r >= 250 && g >= 250 && b >= 250, __LINE__);
            int min_drop_r = 255;
            for (int sy = 141; sy <= 152; ++sy) {
                if (SDL_ReadSurfacePixel(img, 86, sy, &r, &g, &b, &a)) {
                    if (int(r) < min_drop_r) min_drop_r = int(r);
                }
            }
            if (min_drop_r >= 242) {
                std::fprintf(stderr, "v027 [%s] Elevation::Lg min_drop_r=%d (expected < 242)\n", drv_label, min_drop_r);
            }
            check(min_drop_r < 242, __LINE__);

            // Card 2: recessed inset shadow darkens top inner edge while exterior stays pure #F6F7F9 (zero bleed)
            Uint8 top_r = 255, top_g = 255, top_b = 255, ctr_r = 0, ctr_g = 0, ctr_b = 0;
            check(SDL_ReadSurfacePixel(img, 250, 42, &top_r, &top_g, &top_b, &a), __LINE__);
            check(SDL_ReadSurfacePixel(img, 250, 100, &ctr_r, &ctr_g, &ctr_b, &a), __LINE__);
            if (!(top_r < 215 && ctr_r > 230 && int(ctr_r) > int(top_r) + 25)) {
                std::fprintf(stderr, "v027 [%s] inset well top=(%u,%u,%u) ctr=(%u,%u,%u)\n",
                    drv_label, top_r, top_g, top_b, ctr_r, ctr_g, ctr_b);
            }
            check(top_r < 215 && ctr_r > 230 && int(ctr_r) > int(top_r) + 25, __LINE__);
            const int ext_pts[4][2] = {{250, 37}, {250, 143}, {197, 90}, {303, 90}};
            for (int i = 0; i < 4; ++i) {
                check(SDL_ReadSurfacePixel(img, ext_pts[i][0], ext_pts[i][1], &r, &g, &b, &a) &&
                      std::abs(int(r) - 246) <= 3 && std::abs(int(g) - 247) <= 3 && std::abs(int(b) - 249) <= 3, __LINE__);
            }

            // Card 3: multi-layer shadows[2] -> both drop shadow below and inset shadow inside top edge
            Uint8 m_drop_r = 255, m_top_r = 255, m_ctr_r = 0;
            check(SDL_ReadSurfacePixel(img, 414, 146, &m_drop_r, &g, &b, &a), __LINE__);
            check(SDL_ReadSurfacePixel(img, 414, 42, &m_top_r, &g, &b, &a), __LINE__);
            check(SDL_ReadSurfacePixel(img, 414, 100, &m_ctr_r, &g, &b, &a), __LINE__);
            if (!(m_drop_r < 230 && m_top_r < 215 && m_ctr_r > 230)) {
                std::fprintf(stderr, "v027 [%s] multi-layer shadows drop=%u top=%u ctr=%u\n",
                    drv_label, m_drop_r, m_top_r, m_ctr_r);
            }
            check(m_drop_r < 230 && m_top_r < 215 && m_ctr_r > 230, __LINE__);

            SDL_DestroySurface(img);
        } else {
            check(false, __LINE__);
        }

        // Inset shadow edge cases:
        // - Card A [36..84, 36..84]: 48x48 white box with inset spread=100 (> half-size 24) -> fully darkened center
        // - Card B [148..248, 36..136]: 100x100 white box with Elevation::Lg + inset shadows[0] (shadow_count=1)
        begin_frame(c, {.viewport_w = 300, .viewport_h = 180});
        begin_box({
            .axis = Axis::Row,
            .width = Sizing::fixed(300.0f),
            .height = Sizing::fixed(180.0f),
            .padding = Padding::xy(36.0f, 36.0f),
            .gap = 64.0f,
            .background = canvas,
        });
        box({
            .width = Sizing::fixed(48.0f),
            .height = Sizing::fixed(48.0f),
            .background = Color::white(),
            .radius = Corners::all(8.0f),
            .shadow_color = Color::rgba8(0, 0, 0, 220),
            .shadow_blur = 6.0f,
            .shadow_spread = 100.0f,
            .shadow_inset = true,
        });
        box({
            .width = Sizing::fixed(100.0f),
            .height = Sizing::fixed(100.0f),
            .background = Color::white(),
            .radius = Corners::all(10.0f),
            .shadows = {
                ShadowLayer{.offset = {0.0f, 4.0f}, .blur_px = 10.0f, .spread_px = 0.0f, .color = Color::rgba8(0, 0, 0, 160), .inset = true},
            },
            .shadow_count = 1u,
            .elevation = Elevation::Lg,
        });
        end_box();
        end_frame(c);

        check(render_headless_png(c, 300u, 180u, png_path, canvas), __LINE__);
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            // Card A [36..84, 36..84]: inset spread=100 must fully darken the center (no bright unshadowed dot)
            Uint8 c0_r = 255, c0_g = 255, c0_b = 255, c1_r = 255, c1_g = 255, c1_b = 255;
            Uint8 edge_r = 255, edge_g = 255, edge_b = 255;
            check(SDL_ReadSurfacePixel(img, 60, 60, &c0_r, &c0_g, &c0_b, &a), __LINE__);
            check(SDL_ReadSurfacePixel(img, 59, 59, &c1_r, &c1_g, &c1_b, &a), __LINE__);
            check(SDL_ReadSurfacePixel(img, 44, 60, &edge_r, &edge_g, &edge_b, &a), __LINE__);
            const bool spread_dark = c0_r < 130 && c0_g < 130 && c0_b < 130 &&
                                     c1_r < 130 && c1_g < 130 && c1_b < 130 &&
                                     std::abs(int(c0_r) - int(edge_r)) <= 6;
            if (!spread_dark) {
                std::fprintf(stderr, "v027 [%s] inset spread=100 center=(%u,%u,%u)/(%u,%u,%u) edge=(%u,%u,%u)\n",
                    drv_label, c0_r, c0_g, c0_b, c1_r, c1_g, c1_b, edge_r, edge_g, edge_b);
            }
            check(spread_dark, __LINE__);

            // Card B [148..248, 36..136]: Elevation::Lg outer drop shadow + inset shadows[0] inner top shadow
            int elev_drop_r = 255;
            for (int sy = 137; sy <= 148; ++sy) {
                if (SDL_ReadSurfacePixel(img, 198, sy, &r, &g, &b, &a)) {
                    if (int(r) < elev_drop_r) elev_drop_r = int(r);
                }
            }
            Uint8 el_top_r = 255, el_ctr_r = 0;
            check(SDL_ReadSurfacePixel(img, 198, 38, &el_top_r, &g, &b, &a), __LINE__);
            check(SDL_ReadSurfacePixel(img, 198, 96, &el_ctr_r, &g, &b, &a), __LINE__);
            if (!(elev_drop_r < 242 && el_top_r < 215 && el_ctr_r > 230 && int(el_ctr_r) > int(el_top_r) + 25)) {
                std::fprintf(stderr, "v027 [%s] elevation+inset drop=%d top=%u ctr=%u\n",
                    drv_label, elev_drop_r, el_top_r, el_ctr_r);
            }
            check(elev_drop_r < 242 && el_top_r < 215 && el_ctr_r > 230 && int(el_ctr_r) > int(el_top_r) + 25, __LINE__);
            SDL_DestroySurface(img);
        } else {
            check(false, __LINE__);
        }
    }

    // ------------------------------------------------------------------
    // (b) Multi-Stop Linear, Radial & Conic Gradients (Oklab & Linear)
    // ------------------------------------------------------------------
    {
        const GradientDesc lin4_oklab = {
            .kind = GradientKind::Linear,
            .oklab = true,
            .dir = {1.0f, 0.0f},
            .stop_count = 4u,
            .stops = {
                {Color::rgb_hex(0xEF4444), 0.00f},
                {Color::rgb_hex(0xF59E0B), 0.33f},
                {Color::rgb_hex(0x10B981), 0.67f},
                {Color::rgb_hex(0x3B82F6), 1.00f},
            },
        };
        GradientDesc lin4_rgb = lin4_oklab;
        lin4_rgb.oklab = false;

        const GradientDesc rad3_oklab = {
            .kind = GradientKind::Radial,
            .oklab = true,
            .center = {0.5f, 0.5f},
            .stop_count = 3u,
            .stops = {
                {Color::rgba8(255, 32, 32, 255), 0.0f},
                {Color::rgba8(32, 238, 64, 255), 0.5f},
                {Color::rgba8(32, 64, 255, 255), 1.0f},
            },
        };
        const GradientDesc conic4_oklab = {
            .kind = GradientKind::Conic,
            .oklab = true,
            .center = {0.5f, 0.5f},
            .angle_rad = 0.0f,
            .stop_count = 4u,
            .stops = {
                {Color::rgba8(255, 20, 20, 255), 0.00f},
                {Color::rgba8(20, 240, 20, 255), 0.33f},
                {Color::rgba8(20, 40, 255, 255), 0.67f},
                {Color::rgba8(240, 20, 230, 255), 1.00f},
            },
        };
        GradientDesc rad3_linear = rad3_oklab;
        rad3_linear.oklab = false;

        begin_frame(c, {.viewport_w = 480, .viewport_h = 240});
        begin_box({
            .axis = Axis::Col,
            .width = Sizing::fixed(480.0f),
            .height = Sizing::fixed(240.0f),
            .padding = Padding::all(16.0f),
            .gap = 20.0f,
            .background = Color::rgb_hex(0x0E1117),
        });
        // Row 1: Box 4-stop Linear Oklab [16..256, 16..76] + draw_rect 4-stop Linear RGB [272..464, 16..76]
        begin_box({.axis = Axis::Row, .width = Sizing::fixed(448.0f), .height = Sizing::fixed(60.0f), .gap = 16.0f});
        box({
            .width = Sizing::fixed(240.0f),
            .height = Sizing::fixed(60.0f),
            .gradient = lin4_oklab,
        });
        begin_box({.width = Sizing::fixed(192.0f), .height = Sizing::fixed(60.0f)});
        draw_rect({.rect = {0.0f, 0.0f, 192.0f, 60.0f}, .gradient = lin4_rgb});
        end_box();
        end_box();

        // Row 2: Radial [16..136, 96..216], Conic [156..276, 96..216], draw_rect Radial [296..416, 96..216]
        begin_box({.axis = Axis::Row, .width = Sizing::fixed(448.0f), .height = Sizing::fixed(120.0f), .gap = 20.0f});
        box({
            .width = Sizing::fixed(120.0f),
            .height = Sizing::fixed(120.0f),
            .gradient = rad3_oklab,
        });
        box({
            .width = Sizing::fixed(120.0f),
            .height = Sizing::fixed(120.0f),
            .gradient = conic4_oklab,
        });
        begin_box({.width = Sizing::fixed(120.0f), .height = Sizing::fixed(120.0f)});
        draw_rect({.rect = {0.0f, 0.0f, 120.0f, 120.0f}, .gradient = rad3_linear});
        end_box();
        end_box();

        end_box();
        end_frame(c);

        check(render_headless_png(c, 480u, 240u, png_path, Color::rgb_hex(0x0E1117)), __LINE__);
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            // 4-stop Linear Oklab box: Red -> Amber -> Emerald -> Blue
            check(SDL_ReadSurfacePixel(img, 22, 46, &r, &g, &b, &a) && r > 195 && g < 110 && b < 100, __LINE__);
            check(SDL_ReadSurfacePixel(img, 96, 46, &r, &g, &b, &a) && r > 195 && g > 115 && g < 205 && b < 70, __LINE__);
            check(SDL_ReadSurfacePixel(img, 176, 46, &r, &g, &b, &a) && g > 145 && r < 90 && b > 75 && b < 175, __LINE__);
            check(SDL_ReadSurfacePixel(img, 250, 46, &r, &g, &b, &a) && b > 195 && r < 105 && g > 85 && g < 175, __LINE__);

            // draw_rect 4-stop Linear non-Oklab: Red -> Amber -> Emerald -> Blue
            check(SDL_ReadSurfacePixel(img, 278, 46, &r, &g, &b, &a) && r > 195 && g < 110 && b < 100, __LINE__);
            check(SDL_ReadSurfacePixel(img, 336, 46, &r, &g, &b, &a) && r > 195 && g > 115 && g < 205 && b < 70, __LINE__);
            check(SDL_ReadSurfacePixel(img, 400, 46, &r, &g, &b, &a) && g > 145 && r < 90 && b > 75 && b < 175, __LINE__);
            check(SDL_ReadSurfacePixel(img, 458, 46, &r, &g, &b, &a) && b > 195 && r < 105 && g > 85 && g < 175, __LINE__);

            // 3-stop Radial Oklab box [16..136, 96..216], center (76, 156)
            check(SDL_ReadSurfacePixel(img, 76, 156, &r, &g, &b, &a) && r > 200 && g < 80 && b < 80, __LINE__);
            check(SDL_ReadSurfacePixel(img, 108, 156, &r, &g, &b, &a) && g > 160 && int(g) > int(r) + 40 && int(g) > int(b) + 40, __LINE__);
            check(SDL_ReadSurfacePixel(img, 21, 101, &r, &g, &b, &a) && b > 190 && r < 80 && g < 110, __LINE__);

            // draw_rect 3-stop Radial Linear [296..416, 96..216], center (356, 156)
            check(SDL_ReadSurfacePixel(img, 356, 156, &r, &g, &b, &a) && r > 200 && g < 80 && b < 80, __LINE__);
            check(SDL_ReadSurfacePixel(img, 388, 156, &r, &g, &b, &a) && g > 160 && int(g) > int(r) + 40 && int(g) > int(b) + 40, __LINE__);
            check(SDL_ReadSurfacePixel(img, 301, 101, &r, &g, &b, &a) && b > 190 && r < 80 && g < 110, __LINE__);

            // 4-stop Conic Oklab [156..276, 96..216], center (216, 156): 4 diagonal quadrants have distinct RGB hues
            const int q_pts[4][2] = {{252, 120}, {252, 192}, {180, 192}, {180, 120}};
            Uint8 qr[4] = {}, qg[4] = {}, qb[4] = {};
            for (int i = 0; i < 4; ++i) {
                check(SDL_ReadSurfacePixel(img, q_pts[i][0], q_pts[i][1], &qr[i], &qg[i], &qb[i], &a), __LINE__);
            }
            for (int i = 0; i < 4; ++i) {
                for (int j = i + 1; j < 4; ++j) {
                    const int dist = std::abs(int(qr[i]) - int(qr[j])) +
                                     std::abs(int(qg[i]) - int(qg[j])) +
                                     std::abs(int(qb[i]) - int(qb[j]));
                    if (dist < 80) {
                        std::fprintf(stderr, "v027 [%s] conic Q%d(%u,%u,%u) vs Q%d(%u,%u,%u) dist=%d\n",
                            drv_label, i, qr[i], qg[i], qb[i], j, qr[j], qg[j], qb[j], dist);
                    }
                    check(dist >= 80, __LINE__);
                }
            }
            SDL_DestroySurface(img);
        } else {
            check(false, __LINE__);
        }
    }

    // ------------------------------------------------------------------
    // (c) Optical Vibrancy BlurGlass (plain vs saturated/lifted/micro-grain)
    // ------------------------------------------------------------------
    {
        begin_frame(c, {.viewport_w = 320, .viewport_h = 160});
        begin_box({
            .axis = Axis::Row,
            .width = Sizing::fixed(320.0f),
            .height = Sizing::fixed(160.0f),
            .padding = Padding::all(24.0f),
            .gap = 32.0f,
            .background = Color::rgb_hex(0x2868B8),
            .background_end = Color::rgb_hex(0x1E8F7A),
            .gradient_dir = {0.0f, 1.0f},
        });
        // Left [24..144, 24..136]: plain BlurGlass
        box({
            .width = Sizing::fixed(120.0f),
            .height = Sizing::fixed(112.0f),
            .background = Color::rgba8(20, 24, 32, 80),
            .radius = Corners::all(12.0f),
            .backdrop_blur = 12.0f,
            .backdrop_saturation = 1.0f,
            .backdrop_brightness = 1.0f,
            .backdrop_noise = 0.0f,
        });
        // Right [176..296, 24..136]: optical vibrancy BlurGlass
        box({
            .width = Sizing::fixed(120.0f),
            .height = Sizing::fixed(112.0f),
            .background = Color::rgba8(20, 24, 32, 80),
            .radius = Corners::all(12.0f),
            .backdrop_blur = 12.0f,
            .backdrop_saturation = 2.0f,
            .backdrop_brightness = 1.25f,
            .backdrop_noise = 0.02f,
        });
        end_box();
        end_frame(c);

        check(render_headless_png(c, 320u, 160u, png_path, Color::black()), __LINE__);
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            const auto measure_patch = [&](int x0, int y0, float* out_luma, float* out_chroma) noexcept {
                double sum_l = 0.0, sum_c = 0.0;
                int count = 0;
                for (int y = y0; y < y0 + 40; ++y) {
                    for (int x = x0; x < x0 + 40; ++x) {
                        Uint8 r = 0, g = 0, b = 0, a = 0;
                        if (SDL_ReadSurfacePixel(img, x, y, &r, &g, &b, &a)) {
                            const double l = 0.2126 * double(r) + 0.7152 * double(g) + 0.0722 * double(b);
                            const Uint8 mx = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
                            const Uint8 mn = (r < g) ? ((r < b) ? r : b) : ((g < b) ? g : b);
                            sum_l += l;
                            sum_c += double(mx - mn);
                            ++count;
                        }
                    }
                }
                *out_luma = count > 0 ? static_cast<float>(sum_l / double(count)) : 0.0f;
                *out_chroma = count > 0 ? static_cast<float>(sum_c / double(count)) : 0.0f;
            };
            float left_luma = 0.0f, left_chroma = 0.0f, right_luma = 0.0f, right_chroma = 0.0f;
            measure_patch(64, 60, &left_luma, &left_chroma);
            measure_patch(216, 60, &right_luma, &right_chroma);
            if (!(right_luma > left_luma + 6.0f && right_chroma > left_chroma + 10.0f)) {
                std::fprintf(stderr, "v027 [%s] vibrancy left(L=%.1f C=%.1f) right(L=%.1f C=%.1f)\n",
                    drv_label, left_luma, left_chroma, right_luma, right_chroma);
            }
            check(right_luma > left_luma + 6.0f && right_chroma > left_chroma + 10.0f, __LINE__);
            SDL_DestroySurface(img);
        } else {
            check(false, __LINE__);
        }
    }

    // ------------------------------------------------------------------
    // (d) Full 2D Subtree Affine Rotation (LayerDesc::rotation_rad) + Inverse Hitbox
    // ------------------------------------------------------------------
    {
        const uint32_t rot_id = id("v027_rotated_bar");
        const auto build_rotated = [&](Vec2 pointer) noexcept {
            begin_frame(c, {.viewport_w = 400, .viewport_h = 240, .pointer_pos = pointer});
            begin_box({
                .width = Sizing::fixed(400.0f),
                .height = Sizing::fixed(240.0f),
                .padding = {.left = 140.0f, .top = 104.0f},
                .background = Color::rgb_hex(0x0E121A),
            });
            // Unrotated rect [140..260, 104..136], center (200, 120), rotated +45 deg CW.
            ETCH_LAYER({.pivot_norm = {0.5f, 0.5f}, .rotation_rad = 0.78539816f}) {
                begin_box({
                    .id = rot_id,
                    .width = Sizing::fixed(120.0f),
                    .height = Sizing::fixed(32.0f),
                    .align_x = AlignX::Center,
                    .align_y = AlignY::Center,
                    .background = Color::rgb_hex(0x2563EB),
                    .radius = Corners::all(6.0f),
                });
                text(str_lit("ROTATED"), {.color = Color::white(), .font_size_px = 14.0f, .wrap = false});
                end_box();
            }
            end_box();
            end_frame(c);
        };

        build_rotated({-1.0f, -1.0f});
        check(render_headless_png(c, 400u, 240u, png_path, Color::rgb_hex(0x0E121A)), __LINE__);
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            // Rotated diagonal ends at +/-50 * 0.7071 = +/-(35, 35) from (200, 120): (235, 155) & (165, 85)
            // Both lie 19px outside the original [104..136] vertical span.
            check(SDL_ReadSurfacePixel(img, 235, 155, &r, &g, &b, &a) && b > 170 && r < 90, __LINE__);
            check(SDL_ReadSurfacePixel(img, 165, 85, &r, &g, &b, &a) && b > 170 && r < 90, __LINE__);
            // Unrotated ends (200 + 55, 120) and (200 - 55, 120) rotated away -> dark canvas background
            check(SDL_ReadSurfacePixel(img, 255, 120, &r, &g, &b, &a) && r < 30 && g < 35 && b < 45, __LINE__);
            check(SDL_ReadSurfacePixel(img, 145, 120, &r, &g, &b, &a) && r < 30 && g < 35 && b < 45, __LINE__);
            // Crisp white 14px rotated text ink inside diagonal band
            uint32_t text_ink = 0u;
            for (int y = 85; y <= 155; ++y) {
                for (int x = 165; x <= 235; ++x) {
                    if (SDL_ReadSurfacePixel(img, x, y, &r, &g, &b, &a) && r > 180 && g > 180 && b > 180) {
                        ++text_ink;
                    }
                }
            }
            if (text_ink < 35u) {
                std::fprintf(stderr, "v027 [%s] rotated text ink=%u (expected >= 35)\n", drv_label, text_ink);
            }
            check(text_ink >= 35u, __LINE__);
            SDL_DestroySurface(img);
        } else {
            check(false, __LINE__);
        }

        // Inverse-transformed hitbox checks: rotated diagonal hits, unrotated corner misses!
        build_rotated({235.0f, 155.0f});
        build_rotated({235.0f, 155.0f});
        check(hovered(rot_id), __LINE__);

        build_rotated({165.0f, 85.0f});
        build_rotated({165.0f, 85.0f});
        check(hovered(rot_id), __LINE__);

        build_rotated({255.0f, 120.0f});
        build_rotated({255.0f, 120.0f});
        check(!hovered(rot_id), __LINE__);

        build_rotated({200.0f, 120.0f});
        build_rotated({200.0f, 120.0f});
        check(hovered(rot_id), __LINE__);
    }

    // ------------------------------------------------------------------
    // (e) Hardware Mipmaps (generate_mipmaps = true) + ImageFit (Contain & Cover)
    // ------------------------------------------------------------------
    {
        auto* checker = static_cast<uint8_t*>(std::malloc(128u * 128u * 4u));
        check(checker != nullptr, __LINE__);
        if (checker) {
            for (uint32_t y = 0; y < 128u; ++y) {
                for (uint32_t x = 0; x < 128u; ++x) {
                    const uint8_t v = ((x ^ y) & 1u) ? 255u : 0u;
                    const size_t idx = (size_t(y) * 128u + size_t(x)) * 4u;
                    checker[idx + 0] = v;
                    checker[idx + 1] = v;
                    checker[idx + 2] = v;
                    checker[idx + 3] = 255u;
                }
            }
        }
        uint8_t wide_px[64u * 16u * 4u] = {};
        for (uint32_t y = 0; y < 16u; ++y) {
            for (uint32_t x = 0; x < 64u; ++x) {
                const size_t idx = (size_t(y) * 64u + size_t(x)) * 4u;
                const bool edge_red = (x < 4u || x >= 60u);
                wide_px[idx + 0] = edge_red ? 240u : 20u;
                wide_px[idx + 1] = edge_red ? 20u : 220u;
                wide_px[idx + 2] = edge_red ? 20u : 60u;
                wide_px[idx + 3] = 255u;
            }
        }

        LoadedTexture mip_tex = checker
            ? create_texture_rgba(c, {.rgba_pixels = checker, .width = 128u, .height = 128u, .srgb = true, .generate_mipmaps = true})
            : LoadedTexture{};
        std::free(checker);
        LoadedTexture wide_tex = create_texture_rgba(c, {.rgba_pixels = wide_px, .width = 64u, .height = 16u, .srgb = true});
        check(bool(mip_tex) && bool(wide_tex), __LINE__);
        check(flush_texture_uploads(c), __LINE__);

        const Color fit_bg = Color::rgb_hex(0x142038);
        begin_frame(c, {.viewport_w = 260, .viewport_h = 80});
        begin_box({
            .axis = Axis::Row,
            .width = Sizing::fixed(260.0f),
            .height = Sizing::fixed(80.0f),
            .padding = Padding::all(16.0f),
            .gap = 16.0f,
            .background = fit_bg,
        });
        // [16..32, 16..32]: 128x128 checkerboard drawn at 16x16 with hardware mipmaps
        begin_box({.width = Sizing::fixed(24.0f), .height = Sizing::fixed(48.0f)});
        draw_image({.texture = mip_tex.texture, .rect = {0.0f, 0.0f, 16.0f, 16.0f}});
        end_box();
        // [56..104, 16..64]: 64x16 wide texture with ImageFit::Contain into 48x48
        image({
            .texture = wide_tex.texture,
            .width = Sizing::fixed(48.0f),
            .height = Sizing::fixed(48.0f),
            .fit = ImageFit::Contain,
            .source_size = {64.0f, 16.0f},
        });
        // [120..168, 16..64]: 64x16 wide texture with ImageFit::Cover into 48x48
        image({
            .texture = wide_tex.texture,
            .width = Sizing::fixed(48.0f),
            .height = Sizing::fixed(48.0f),
            .fit = ImageFit::Cover,
            .source_size = {64.0f, 16.0f},
        });
        // [184..232, 16..64]: draw_image ImageFit::Cover into 48x48
        begin_box({.width = Sizing::fixed(48.0f), .height = Sizing::fixed(48.0f)});
        draw_image({
            .texture = wide_tex.texture,
            .rect = {0.0f, 0.0f, 48.0f, 48.0f},
            .fit = ImageFit::Cover,
            .source_size = {64.0f, 16.0f},
        });
        end_box();
        end_box();
        end_frame(c);

        check(render_headless_png(c, 260u, 80u, png_path, fit_bg), __LINE__);
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            // Mipmapped 16x16 checkerboard interior [18..29, 18..29] must average smoothly to mid-gray [80..210]
            bool mip_smooth = true;
            for (int y = 18; y <= 29 && mip_smooth; ++y) {
                for (int x = 18; x <= 29; ++x) {
                    if (!SDL_ReadSurfacePixel(img, x, y, &r, &g, &b, &a) ||
                        r < 80 || r > 210 || g < 80 || g > 210 || b < 80 || b > 210) {
                        std::fprintf(stderr, "v027 [%s] mip checkerboard (%d,%d)=(%u,%u,%u) out of [80..210]\n",
                            drv_label, x, y, r, g, b);
                        mip_smooth = false;
                        break;
                    }
                }
            }
            check(mip_smooth, __LINE__);

            // ImageFit::Contain [56..104, 16..64]: top/bottom letterbox = fit_bg (20,32,56), middle = green, side = red
            check(SDL_ReadSurfacePixel(img, 80, 22, &r, &g, &b, &a) && r < 35 && g < 45 && b > 40 && b < 72, __LINE__);
            check(SDL_ReadSurfacePixel(img, 80, 58, &r, &g, &b, &a) && r < 35 && g < 45 && b > 40 && b < 72, __LINE__);
            check(SDL_ReadSurfacePixel(img, 80, 40, &r, &g, &b, &a) && g > 180 && r < 50 && b < 90, __LINE__);
            check(SDL_ReadSurfacePixel(img, 57, 40, &r, &g, &b, &a) && r > 150 && g < 90, __LINE__);

            // ImageFit::Cover [120..168, 16..64]: full 48x48 is solid green; red edge stripes cropped out
            const int cov_pts[5][2] = {{144, 20}, {144, 60}, {124, 40}, {164, 40}, {144, 40}};
            for (int i = 0; i < 5; ++i) {
                check(SDL_ReadSurfacePixel(img, cov_pts[i][0], cov_pts[i][1], &r, &g, &b, &a) &&
                      g > 180 && r < 50 && b < 90, __LINE__);
            }

            // draw_image ImageFit::Cover [184..232, 16..64]: full 48x48 is solid green
            const int dcov_pts[4][2] = {{208, 20}, {208, 60}, {188, 40}, {228, 40}};
            for (int i = 0; i < 4; ++i) {
                check(SDL_ReadSurfacePixel(img, dcov_pts[i][0], dcov_pts[i][1], &r, &g, &b, &a) &&
                      g > 180 && r < 50 && b < 90, __LINE__);
            }
            SDL_DestroySurface(img);
        } else {
            check(false, __LINE__);
        }

        // Mipmap regeneration on update_texture_rgba: replace 128x128 checkerboard with solid bright green,
        // draw downscaled to 16x16, and verify interior pixels are bright green (not stale/black mip VRAM).
        if (mip_tex) {
            auto* green_px = static_cast<uint8_t*>(std::malloc(128u * 128u * 4u));
            check(green_px != nullptr, __LINE__);
            if (green_px) {
                for (uint32_t i = 0; i < 128u * 128u; ++i) {
                    const size_t idx = size_t(i) * 4u;
                    green_px[idx + 0] = 16u;
                    green_px[idx + 1] = 236u;
                    green_px[idx + 2] = 20u;
                    green_px[idx + 3] = 255u;
                }
                check(update_texture_rgba(c, mip_tex.texture, 128u, 128u, green_px), __LINE__);
                std::free(green_px);
                check(flush_texture_uploads(c), __LINE__);

                begin_frame(c, {.viewport_w = 64, .viewport_h = 64});
                begin_box({
                    .width = Sizing::fixed(64.0f),
                    .height = Sizing::fixed(64.0f),
                    .padding = Padding::all(16.0f),
                    .background = fit_bg,
                });
                begin_box({.width = Sizing::fixed(16.0f), .height = Sizing::fixed(16.0f)});
                draw_image({.texture = mip_tex.texture, .rect = {0.0f, 0.0f, 16.0f, 16.0f}});
                end_box();
                end_box();
                end_frame(c);

                check(render_headless_png(c, 64u, 64u, png_path, fit_bg), __LINE__);
                if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
                    Uint8 r = 0, g = 0, b = 0, a = 0;
                    bool mip_green = true;
                    for (int y = 18; y <= 29 && mip_green; ++y) {
                        for (int x = 18; x <= 29; ++x) {
                            if (!SDL_ReadSurfacePixel(img, x, y, &r, &g, &b, &a) ||
                                !(g > 190 && r < 40 && b < 40)) {
                                std::fprintf(stderr, "v027 [%s] mip updated (%d,%d)=(%u,%u,%u) expected bright green\n",
                                    drv_label, x, y, r, g, b);
                                mip_green = false;
                                break;
                            }
                        }
                    }
                    check(mip_green, __LINE__);
                    SDL_DestroySurface(img);
                } else {
                    check(false, __LINE__);
                }
            }
        }

        destroy_texture(c, &mip_tex);
        destroy_texture(c, &wide_tex);
    }

    // ------------------------------------------------------------------
    // (f) Easing Curves, Timed Tween with Delay & Stagger Helper
    // ------------------------------------------------------------------
    {
        const EasingSpec lin = EasingSpec::linear();
        const EasingSpec ein = EasingSpec::ease_in();
        const EasingSpec eout = EasingSpec::ease_out();
        const EasingSpec eio = EasingSpec::ease_in_out();
        const EasingSpec cb = EasingSpec::cubic_bezier(0.22f, 1.0f, 0.36f, 1.0f);

        check(std::fabs(eval_easing(lin, 0.0f)) < 1e-5f && std::fabs(eval_easing(lin, 1.0f) - 1.0f) < 1e-5f, __LINE__);
        check(std::fabs(eval_easing(lin, 0.5f) - 0.5f) < 1e-4f, __LINE__);
        check(std::fabs(eval_easing(lin, -0.5f)) < 1e-5f && std::fabs(eval_easing(lin, 1.5f) - 1.0f) < 1e-5f, __LINE__);

        check(std::fabs(eval_easing(ein, 0.0f)) < 1e-5f && std::fabs(eval_easing(ein, 1.0f) - 1.0f) < 1e-5f, __LINE__);
        check(eval_easing(ein, 0.5f) < 0.5f, __LINE__);

        check(std::fabs(eval_easing(eout, 0.0f)) < 1e-5f && std::fabs(eval_easing(eout, 1.0f) - 1.0f) < 1e-5f, __LINE__);
        check(eval_easing(eout, 0.5f) > 0.5f, __LINE__);

        check(std::fabs(eval_easing(eio, 0.0f)) < 1e-5f && std::fabs(eval_easing(eio, 1.0f) - 1.0f) < 1e-5f, __LINE__);
        check(eval_easing(eio, 0.25f) < 0.25f && eval_easing(eio, 0.75f) > 0.75f, __LINE__);

        check(std::fabs(eval_easing(cb, 0.0f)) < 1e-5f && std::fabs(eval_easing(cb, 1.0f) - 1.0f) < 1e-5f, __LINE__);
        check(eval_easing(cb, 0.5f) > 0.7f, __LINE__);

        check(std::fabs(stagger_delay(0u, 0.05f, 0.12f)) < 1e-5f, __LINE__);
        check(std::fabs(stagger_delay(2u, 0.05f, 0.12f) - 0.10f) < 1e-5f, __LINE__);
        check(std::fabs(stagger_delay(3u, 0.05f, 0.12f) - 0.12f) < 1e-5f, __LINE__);

        // Timed tween: prime at 0, trigger target=100 with duration=0.20s, delay=0.05s
        const uint32_t tw_id = id("v027_tween_delay");
        begin_frame(c, {.viewport_w = 120, .viewport_h = 80, .dt_seconds = 0.0f});
        const float v_init = tween(tw_id, 0.0f, 0.2f, EasingSpec::ease_in_out(), 0.05f);
        end_frame(c);
        check(std::fabs(v_init) < 1e-5f, __LINE__);

        float prev_v = 0.0f;
        uint32_t mid_steps = 0u;
        bool held_during_delay = true;
        float final_v = 0.0f;
        for (int step = 1; step <= 20; ++step) {
            begin_frame(c, {.viewport_w = 120, .viewport_h = 80, .dt_seconds = 0.02f});
            const float v = tween(tw_id, 100.0f, 0.2f, EasingSpec::ease_in_out(), 0.05f);
            end_frame(c);
            if (step <= 2) {
                // Elapsed <= 0.04s < 0.05s delay -> must hold 0
                if (std::fabs(v) > 1e-4f) held_during_delay = false;
            }
            if (v > 5.0f && v < 95.0f && v > prev_v + 0.5f) {
                ++mid_steps;
            }
            check(v >= prev_v - 1e-4f && v <= 100.0001f, __LINE__);
            prev_v = v;
            final_v = v;
        }
        check(held_during_delay && mid_steps >= 4u, __LINE__);
        check(std::fabs(final_v - 100.0f) < 1e-4f && frame_stats(c).active_animations == 0u, __LINE__);
    }

    // ------------------------------------------------------------------
    // (g) Single-Digit Draw Call Check (<= 6 DC on rich combined frame)
    // ------------------------------------------------------------------
    {
        const GradientDesc bar_grad = {
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
        begin_frame(c, {.viewport_w = 480, .viewport_h = 240});
        begin_box({
            .axis = Axis::Col,
            .width = Sizing::fixed(480.0f),
            .height = Sizing::fixed(240.0f),
            .padding = Padding::all(20.0f),
            .gap = 14.0f,
            .background = Color::rgb_hex(0x0D0E11),
        });
        box({
            .width = Sizing::grow(),
            .height = Sizing::fixed(8.0f),
            .radius = Corners::all(4.0f),
            .gradient = bar_grad,
        });
        begin_box({.axis = Axis::Row, .width = Sizing::grow(), .height = Sizing::grow(), .gap = 16.0f});
        begin_box({
            .axis = Axis::Col,
            .width = Sizing::grow(),
            .height = Sizing::grow(),
            .padding = Padding::all(16.0f),
            .gap = 8.0f,
            .background = Color::rgb_hex(0x161922),
            .border_color = Color::rgba8(255, 255, 255, 26),
            .border_width = 1.0f,
            .radius = Corners::all(12.0f),
            .elevation = Elevation::Lg,
        });
        text(str_lit("Primary Metric"), {.color = Color::white(), .font_size_px = 14.0f, .wrap = false});
        text(str_lit("Latency 12 ms"), {.color = Color::rgb_hex(0x38BDF8), .font_size_px = 14.0f, .wrap = false});
        text(str_lit("Status Healthy"), {.color = Color::rgb_hex(0x10B981), .font_size_px = 14.0f, .wrap = false});
        end_box();

        begin_box({
            .axis = Axis::Col,
            .width = Sizing::grow(),
            .height = Sizing::grow(),
            .padding = Padding::all(16.0f),
            .gap = 8.0f,
            .background = Color::rgb_hex(0x141720),
            .border_color = Color::rgba8(255, 255, 255, 22),
            .border_width = 1.0f,
            .radius = Corners::all(12.0f),
            .shadows = {
                ShadowLayer{.offset = {0.0f, 6.0f}, .blur_px = 16.0f, .spread_px = 0.0f, .color = Color::rgba8(0, 0, 0, 120), .inset = false},
                ShadowLayer{.offset = {0.0f, 2.0f}, .blur_px = 8.0f, .spread_px = 0.0f, .color = Color::rgba8(0, 0, 0, 140), .inset = true},
            },
            .shadow_count = 2u,
        });
        text(str_lit("Multi-Layer Card"), {.color = Color::rgb_hex(0xF59E0B), .font_size_px = 14.0f, .wrap = false});
        text(str_lit("Batched R8 Text"), {.color = Color::rgb_hex(0xF43F5E), .font_size_px = 14.0f, .wrap = false});
        end_box();
        end_box();
        end_box();
        end_frame(c);

        check(render_headless_png(c, 480u, 240u, png_path, Color::rgb_hex(0x0D0E11)), __LINE__);
        const uint32_t dc = frame_stats(c).draw_call_count;
        if (dc == 0u || dc > 6u) {
            std::fprintf(stderr, "v027 [%s] combined card draw_call_count=%u (expected 1..6)\n", drv_label, dc);
        }
        check(dc > 0u && dc <= 6u, __LINE__);
    }

    std::remove(png_path);
    destroy_context(c);
    set_current_context(nullptr);
    return ok;
}

inline bool verify_v027() noexcept {
    const char* env_drv = SDL_getenv("ETCH_GPU_DRIVER");
    if (!env_drv || !env_drv[0]) env_drv = SDL_getenv("SDL_GPU_DRIVER");
    if (env_drv && env_drv[0]) {
        return verify_v027_driver(env_drv);
    }
#if defined(_WIN32)
    return verify_v027_driver("direct3d12") && verify_v027_driver("vulkan");
#else
    return verify_v027_driver(nullptr);
#endif
}
