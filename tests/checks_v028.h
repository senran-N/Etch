#pragma once
// v0.28.0 GPU, Typography & Batching regression checks:
// - Hardware BlendMode (Normal, Additive, Multiply, Screen) on BoxDesc & LayerDesc (plus blend_plus_lighter alias)
// - AlphaMaskDesc (Rounded, LinearGradient, RadialSpotlight, Texture + invert) on boxes & text
// - OpenType opentype_flags (TabularNums, NoLiga, SlashedZero, SmallCaps) + font_features + ShapeCache distinct hashes
// - create_font_variation deduplication & hb_font_draw_glyph() Slug rendering
// - Clipped hollow-bordered cards batching (<= 8 draw calls across 6 cards + multi-color text)

inline bool verify_v028_driver(const char* driver) noexcept {
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
            std::fprintf(stderr, "v028 [%s] check failed at line %d\n", drv_label, line);
        }
    };
    const char* png_path = "v028-check.png";

    // ------------------------------------------------------------------
    // (a) Hardware BlendMode (Normal, Additive, Multiply, Screen)
    //     on BoxDesc (Row 1) and LayerDesc (Row 2) + blend_plus_lighter alias
    // ------------------------------------------------------------------
    {
        const Color backdrop = Color::rgba8(150, 150, 150, 255);
        const Color swatch = Color::rgba8(160, 120, 180, 255);
        const BlendMode modes[4] = {
            BlendMode::Normal,
            BlendMode::Multiply,
            BlendMode::Screen,
            BlendMode::Additive,
        };

        begin_frame(c, {.viewport_w = 460, .viewport_h = 200});
        begin_box({
            .axis = Axis::Col,
            .width = Sizing::fixed(460.0f),
            .height = Sizing::fixed(200.0f),
            .padding = Padding::all(16.0f),
            .gap = 16.0f,
            .background = backdrop,
        });
        // Row 1 [y: 16..92]: BoxDesc::blend_mode
        begin_box({.axis = Axis::Row, .width = Sizing::fixed(428.0f), .height = Sizing::fixed(76.0f), .gap = 12.0f});
        for (int i = 0; i < 4; ++i) {
            box({
                .width = Sizing::fixed(76.0f),
                .height = Sizing::fixed(76.0f),
                .background = swatch,
                .blend_mode = modes[i],
            });
        }
        // 5th slot: LayerDesc::blend_plus_lighter legacy alias (must match Additive)
        ETCH_LAYER({.blend_plus_lighter = true}) {
            box({
                .width = Sizing::fixed(76.0f),
                .height = Sizing::fixed(76.0f),
                .background = swatch,
            });
        }
        end_box();

        // Row 2 [y: 108..184]: LayerDesc::blend_mode
        begin_box({.axis = Axis::Row, .width = Sizing::fixed(428.0f), .height = Sizing::fixed(76.0f), .gap = 12.0f});
        for (int i = 0; i < 4; ++i) {
            ETCH_LAYER({.blend_mode = modes[i]}) {
                box({
                    .width = Sizing::fixed(76.0f),
                    .height = Sizing::fixed(76.0f),
                    .background = swatch,
                });
            }
        }
        end_box();

        end_box();
        end_frame(c);

        check(render_headless_png(c, 460u, 200u, png_path, backdrop), __LINE__);
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            Uint8 br[4] = {}, bg[4] = {}, bb[4] = {};
            Uint8 lr[4] = {}, lg[4] = {}, lb[4] = {};
            Uint8 a = 0;
            for (int i = 0; i < 4; ++i) {
                const int cx = 16 + i * 88 + 38;
                check(SDL_ReadSurfacePixel(img, cx, 54, &br[i], &bg[i], &bb[i], &a), __LINE__);
                check(SDL_ReadSurfacePixel(img, cx, 146, &lr[i], &lg[i], &lb[i], &a), __LINE__);
                const bool parity =
                    std::abs(int(br[i]) - int(lr[i])) <= 2 &&
                    std::abs(int(bg[i]) - int(lg[i])) <= 2 &&
                    std::abs(int(bb[i]) - int(lb[i])) <= 2;
                if (!parity) {
                    std::fprintf(stderr, "v028 [%s] blend[%d] Box(%u,%u,%u) vs Layer(%u,%u,%u)\n",
                        drv_label, i, br[i], bg[i], bb[i], lr[i], lg[i], lb[i]);
                }
                check(parity, __LINE__);
            }
            // Expected linear->sRGB ordering on all 3 channels: Multiply < Normal < Screen < Additive
            // Normal ~(160,120,180), Multiply ~(92,68,105), Screen ~(196,176,207), Additive ~(212,187,226)
            const bool order_r = (br[1] + 40 < br[0]) && (br[0] + 20 < br[2]) && (br[2] + 8 < br[3]);
            const bool order_g = (bg[1] + 35 < bg[0]) && (bg[0] + 35 < bg[2]) && (bg[2] + 5 < bg[3]);
            const bool order_b = (bb[1] + 50 < bb[0]) && (bb[0] + 15 < bb[2]) && (bb[2] + 8 < bb[3]);
            if (!(order_r && order_g && order_b)) {
                std::fprintf(stderr, "v028 [%s] blend order Mul(%u,%u,%u) Norm(%u,%u,%u) Scr(%u,%u,%u) Add(%u,%u,%u)\n",
                    drv_label, br[1], bg[1], bb[1], br[0], bg[0], bb[0], br[2], bg[2], bb[2], br[3], bg[3], bb[3]);
            }
            check(order_r && order_g && order_b, __LINE__);

            // Legacy blend_plus_lighter at index 4 (x = 16 + 4*88 + 38 = 406, y = 54) must match Additive
            Uint8 pr = 0, pg = 0, pb = 0;
            check(SDL_ReadSurfacePixel(img, 406, 54, &pr, &pg, &pb, &a), __LINE__);
            check(std::abs(int(pr) - int(br[3])) <= 2 &&
                  std::abs(int(pg) - int(bg[3])) <= 2 &&
                  std::abs(int(pb) - int(bb[3])) <= 2, __LINE__);

            SDL_DestroySurface(img);
        } else {
            check(false, __LINE__);
        }
    }

    // ------------------------------------------------------------------
    // (b) AlphaMaskDesc (Rounded, LinearGradient, RadialSpotlight, Texture + invert)
    // ------------------------------------------------------------------
    {
        // 32x32 alpha mask texture: left half alpha=255, right half alpha=0
        uint8_t mask_px[32u * 32u * 4u] = {};
        for (uint32_t y = 0; y < 32u; ++y) {
            for (uint32_t x = 0; x < 32u; ++x) {
                const size_t idx = (size_t(y) * 32u + size_t(x)) * 4u;
                mask_px[idx + 0] = 255u;
                mask_px[idx + 1] = 255u;
                mask_px[idx + 2] = 255u;
                mask_px[idx + 3] = (x < 16u) ? 255u : 0u;
            }
        }
        LoadedTexture mask_tex = create_texture_rgba(c, {.rgba_pixels = mask_px, .width = 32u, .height = 32u, .srgb = false});
        check(bool(mask_tex) && flush_texture_uploads(c), __LINE__);

        const Color bg = Color::rgb_hex(0x10141C);
        const Color fg = Color::rgba8(40, 220, 240, 255);
        const auto emit_mask_row = [&](bool inv) noexcept {
            begin_box({.axis = Axis::Row, .width = Sizing::fixed(368.0f), .height = Sizing::fixed(80.0f), .gap = 16.0f});
            // Col 0 [16..96]: Rounded (radius = 28px)
            ETCH_LAYER({.alpha_mask = {.kind = AlphaMaskKind::Rounded, .invert = inv, .radius = Corners::all(28.0f)}}) {
                box({.width = Sizing::fixed(80.0f), .height = Sizing::fixed(80.0f), .background = fg});
            }
            // Col 1 [112..192]: LinearGradient left->right (angle_deg = 0)
            ETCH_LAYER({.alpha_mask = {.kind = AlphaMaskKind::LinearGradient, .invert = inv, .angle_deg = 0.0f,
                                       .start_t = 0.1f, .end_t = 0.9f, .alpha_start = 1.0f, .alpha_end = 0.0f}}) {
                begin_box({.width = Sizing::fixed(80.0f), .height = Sizing::fixed(80.0f),
                           .align_x = AlignX::Center, .align_y = AlignY::Center, .background = fg});
                text(str_lit("MASK"), {.color = Color::white(), .font_size_px = 14.0f, .wrap = false});
                end_box();
            }
            // Col 2 [208..288]: RadialSpotlight
            ETCH_LAYER({.alpha_mask = {.kind = AlphaMaskKind::RadialSpotlight, .invert = inv,
                                       .start_t = 0.1f, .end_t = 0.85f, .alpha_start = 1.0f, .alpha_end = 0.0f,
                                       .center_norm = {0.5f, 0.5f}, .radius_norm = {0.45f, 0.45f}}}) {
                box({.width = Sizing::fixed(80.0f), .height = Sizing::fixed(80.0f), .background = fg});
            }
            // Col 3 [304..384]: Texture mask
            ETCH_LAYER({.alpha_mask = {.kind = AlphaMaskKind::Texture, .invert = inv,
                                       .alpha_start = 1.0f, .alpha_end = 0.0f, .texture = mask_tex.texture}}) {
                box({.width = Sizing::fixed(80.0f), .height = Sizing::fixed(80.0f), .background = fg});
            }
            end_box();
        };

        begin_frame(c, {.viewport_w = 400, .viewport_h = 208});
        begin_box({
            .axis = Axis::Col,
            .width = Sizing::fixed(400.0f),
            .height = Sizing::fixed(208.0f),
            .padding = Padding::all(16.0f),
            .gap = 16.0f,
            .background = bg,
        });
        emit_mask_row(false); // Row 0: y = 16..96
        emit_mask_row(true);  // Row 1: y = 112..192
        end_box();
        end_frame(c);

        check(render_headless_png(c, 400u, 208u, png_path, bg), __LINE__);
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            const auto is_bright = [&](int x, int y) noexcept {
                Uint8 r = 0, g = 0, b = 0, a = 0;
                return SDL_ReadSurfacePixel(img, x, y, &r, &g, &b, &a) && g > 170 && b > 180;
            };
            const auto is_dark = [&](int x, int y) noexcept {
                Uint8 r = 0, g = 0, b = 0, a = 0;
                return SDL_ReadSurfacePixel(img, x, y, &r, &g, &b, &a) && r < 40 && g < 45 && b < 55;
            };

            // Row 0 (invert = false):
            // Rounded [16..96, 16..96]: center bright, corner (18,18) dark
            check(is_bright(56, 56) && is_dark(18, 18), __LINE__);
            // LinearGradient [112..192, 16..96]: left (118,22) bright, right (186,22) dark
            check(is_bright(118, 22) && is_dark(186, 22), __LINE__);
            // RadialSpotlight [208..288, 16..96]: center (248,56) bright, corner (212,20) dark
            check(is_bright(248, 56) && is_dark(212, 20), __LINE__);
            // Texture [304..384, 16..96]: left half (324,56) bright, right half (364,56) dark
            check(is_bright(324, 56) && is_dark(364, 56), __LINE__);

            // Row 1 (invert = true): exact inversion of all 4 masks
            // Rounded [16..96, 112..192]: center dark, corner (18,114) bright
            check(is_dark(56, 152) && is_bright(18, 114), __LINE__);
            // LinearGradient [112..192, 112..192]: left (118,118) dark, right (186,118) bright
            check(is_dark(118, 118) && is_bright(186, 118), __LINE__);
            // RadialSpotlight [208..288, 112..192]: center (248,152) dark, corner (212,116) bright
            check(is_dark(248, 152) && is_bright(212, 116), __LINE__);
            // Texture [304..384, 112..192]: left half (324,152) dark, right half (364,152) bright
            check(is_dark(324, 152) && is_bright(364, 152), __LINE__);

            SDL_DestroySurface(img);
        } else {
            check(false, __LINE__);
        }
        destroy_texture(c, &mask_tex);

        // Uniform rounded clip_children box + child AlphaMask (normal & blur layer):
        // rounded ancestor corners must remain preserved when AlphaMask activates indexed clips.
        {
            begin_frame(c, {.viewport_w = 240, .viewport_h = 120});
            begin_box({
                .axis = Axis::Row,
                .width = Sizing::fixed(240.0f),
                .height = Sizing::fixed(120.0f),
                .padding = Padding::all(10.0f),
                .gap = 20.0f,
                .background = bg,
            });
            // Card 1 [10..110, 10..110]: uniform radius=32 + clip_children + LinearGradient AlphaMask
            begin_box({
                .width = Sizing::fixed(100.0f),
                .height = Sizing::fixed(100.0f),
                .radius = Corners::all(32.0f),
                .clip_children = true,
            });
            ETCH_LAYER({.alpha_mask = {.kind = AlphaMaskKind::LinearGradient, .angle_deg = 0.0f,
                                       .start_t = 0.0f, .end_t = 1.0f, .alpha_start = 1.0f, .alpha_end = 0.1f}}) {
                box({.width = Sizing::grow(), .height = Sizing::grow(), .background = fg});
            }
            end_box();
            // Card 2 [130..230, 10..110]: uniform radius=32 + clip_children + blur_px + LinearGradient AlphaMask
            begin_box({
                .width = Sizing::fixed(100.0f),
                .height = Sizing::fixed(100.0f),
                .radius = Corners::all(32.0f),
                .clip_children = true,
            });
            ETCH_LAYER({.blur_px = 6.0f,
                        .alpha_mask = {.kind = AlphaMaskKind::LinearGradient, .angle_deg = 0.0f,
                                       .start_t = 0.0f, .end_t = 1.0f, .alpha_start = 1.0f, .alpha_end = 0.1f}}) {
                box({.width = Sizing::grow(), .height = Sizing::grow(), .background = fg});
            }
            end_box();
            end_box();
            end_frame(c);

            check(render_headless_png(c, 240u, 120u, png_path, bg), __LINE__);
            if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
                auto is_bg = [&](int x, int y) noexcept {
                    Uint8 r = 0, g = 0, b = 0, a = 0;
                    return SDL_ReadSurfacePixel(img, x, y, &r, &g, &b, &a) && r < 40 && g < 45 && b < 55;
                };
                auto is_fg = [&](int x, int y) noexcept {
                    Uint8 r = 0, g = 0, b = 0, a = 0;
                    return SDL_ReadSurfacePixel(img, x, y, &r, &g, &b, &a) && g > 150 && b > 160;
                };
                // Card 1: outer corners outside radius-32 arc must stay dark bg; left interior bright; right interior dimmer
                check(is_bg(12, 12) && is_bg(12, 108) && is_fg(24, 60), __LINE__);
                Uint8 lr = 0, lg = 0, lb = 0, rr = 0, rg = 0, rb = 0, a = 0;
                check(SDL_ReadSurfacePixel(img, 24, 60, &lr, &lg, &lb, &a) &&
                      SDL_ReadSurfacePixel(img, 104, 60, &rr, &rg, &rb, &a) &&
                      lg > rg + 60, __LINE__);
                // Card 2 (blurred AlphaMask layer): outer corners outside radius-32 arc must also stay dark bg
                check(is_bg(132, 12) && is_bg(132, 108) && is_fg(154, 60), __LINE__);
                SDL_DestroySurface(img);
            } else {
                check(false, __LINE__);
            }
        }
    }

    // ------------------------------------------------------------------
    // (c) OpenType opentype_flags (TabularNums, NoLiga, SlashedZero, SmallCaps)
    //     + font_features + ShapeCache distinct-hash verification
    // ------------------------------------------------------------------
    {
        const FontFeature feat_tnum[1] = {{make_opentype_tag('t', 'n', 'u', 'm'), 1u}};
        const FontFeature feat_noliga[1] = {{make_opentype_tag('l', 'i', 'g', 'a'), 0u}};
        const uint32_t id_prop1 = id("ot_prop_1");
        const uint32_t id_prop8 = id("ot_prop_8");
        const uint32_t id_tnum1 = id("ot_tnum_1");
        const uint32_t id_tnum8 = id("ot_tnum_8");
        const uint32_t id_feat1 = id("ot_feat_1");
        const uint32_t id_prop1_after = id("ot_prop_1_after");
        const uint32_t id_liga_on = id("ot_liga_on");
        const uint32_t id_liga_off = id("ot_liga_off");
        const uint32_t id_liga_feat_off = id("ot_liga_feat_off");
        const uint32_t id_liga_on_after = id("ot_liga_on_after");
        const uint32_t id_zero = id("ot_zero");
        const uint32_t id_smcp = id("ot_smcp");

        auto emit_ot_line = [&](uint32_t box_id, Str s, uint32_t ot_flags, std::span<const FontFeature> feats = {}) noexcept {
            begin_box({.id = box_id, .width = Sizing::fit(), .height = Sizing::fit()});
            text(s, {.font_size_px = 18.0f, .wrap = false, .opentype_flags = ot_flags, .font_features = feats});
            end_box();
        };

        begin_frame(c, {.viewport_w = 500, .viewport_h = 360});
        begin_box({.axis = Axis::Col, .width = Sizing::fixed(500.0f), .height = Sizing::fixed(360.0f), .gap = 4.0f});
        // Same string "11111111" shaped under None -> TabularNums -> custom font_features(tnum) -> None
        emit_ot_line(id_prop1, str_lit("11111111"), OpenTypeFlag_None);
        emit_ot_line(id_prop8, str_lit("88888888"), OpenTypeFlag_None);
        emit_ot_line(id_tnum1, str_lit("11111111"), OpenTypeFlag_TabularNums);
        emit_ot_line(id_tnum8, str_lit("88888888"), OpenTypeFlag_TabularNums);
        emit_ot_line(id_feat1, str_lit("11111111"), OpenTypeFlag_None, feat_tnum);
        emit_ot_line(id_prop1_after, str_lit("11111111"), OpenTypeFlag_None);

        // Same string "fi" shaped under None -> NoLiga -> custom font_features(liga=0) -> None
        emit_ot_line(id_liga_on, str_lit("fi"), OpenTypeFlag_None);
        emit_ot_line(id_liga_off, str_lit("fi"), OpenTypeFlag_NoLiga);
        emit_ot_line(id_liga_feat_off, str_lit("fi"), OpenTypeFlag_None, feat_noliga);
        emit_ot_line(id_liga_on_after, str_lit("fi"), OpenTypeFlag_None);

        // Exercise SlashedZero and SmallCaps
        emit_ot_line(id_zero, str_lit("0000"), OpenTypeFlag_SlashedZero);
        emit_ot_line(id_smcp, str_lit("SmallCaps"), OpenTypeFlag_SmallCaps);
        end_box();
        end_frame(c);

        const Rect r_prop1 = element_bounds(id_prop1);
        const Rect r_prop8 = element_bounds(id_prop8);
        const Rect r_tnum1 = element_bounds(id_tnum1);
        const Rect r_tnum8 = element_bounds(id_tnum8);
        const Rect r_feat1 = element_bounds(id_feat1);
        const Rect r_prop1_after = element_bounds(id_prop1_after);
        const Rect r_liga_on = element_bounds(id_liga_on);
        const Rect r_liga_off = element_bounds(id_liga_off);
        const Rect r_liga_feat_off = element_bounds(id_liga_feat_off);
        const Rect r_liga_on_after = element_bounds(id_liga_on_after);
        const Rect r_zero = element_bounds(id_zero);
        const Rect r_smcp = element_bounds(id_smcp);

        check(r_prop1.w > 20.0f && r_tnum1.w > 20.0f && r_zero.w > 10.0f && r_smcp.w > 20.0f, __LINE__);
        // TabularNums flag and explicit tnum=1 feature must agree, and restoring None must not collide in ShapeCache!
        check(std::fabs(r_tnum1.w - r_feat1.w) < 0.5f, __LINE__);
        check(std::fabs(r_prop1.w - r_prop1_after.w) < 0.5f, __LINE__);
        // When default sans has proportional numerals (e.g. Segoe UI), tnum makes "11111111" wider to match "88888888"
        if (r_prop8.w > r_prop1.w + 2.0f) {
            check(std::fabs(r_tnum1.w - r_tnum8.w) < 1.0f && r_tnum1.w > r_prop1.w + 2.0f, __LINE__);
        }

        // Count glyphs emitted in each "fi" row's vertical band to verify liga vs NoLiga / font_features + ShapeCache isolation
        auto count_glyphs_in_band = [&](const Rect& box_r) noexcept -> uint32_t {
            uint32_t n = 0u;
            for (const Prim& p : frame_prims(c)) {
                const PrimKind pk = static_cast<PrimKind>(p.style.x & 0xFu);
                if (pk == PrimKind::Glyph || pk == PrimKind::SlugGlyph) {
                    const float cy = p.bounds.y + p.bounds.w * 0.5f;
                    if (cy >= box_r.y - 2.0f && cy <= box_r.y + box_r.h + 2.0f) ++n;
                }
            }
            return n;
        };
        const uint32_t g_liga_on = count_glyphs_in_band(r_liga_on);
        const uint32_t g_liga_off = count_glyphs_in_band(r_liga_off);
        const uint32_t g_liga_feat = count_glyphs_in_band(r_liga_feat_off);
        const uint32_t g_liga_after = count_glyphs_in_band(r_liga_on_after);
        check(g_liga_on >= 1u && g_liga_off >= g_liga_on && g_liga_feat == g_liga_off && g_liga_after == g_liga_on, __LINE__);
    }

    // ------------------------------------------------------------------
    // (d) create_font_variation deduplication & hb_font_draw_glyph() Slug rendering
    // ------------------------------------------------------------------
    {
        // Invalid inputs & empty-variation identity
        check(create_font_variation(nullptr, kFontSans, {}) == kInvalidFont, __LINE__);
        check(create_font_variation(c, 60u, {}) == kInvalidFont, __LINE__);
        check(create_font_variation(c, kFontSans, {}) == kFontSans, __LINE__);

        // Prefer a real variable font if present on the OS (e.g. Bahnschrift on Windows), fallback to kFontSans
        uint16_t vf_base = kInvalidFont;
        bool has_true_vf = false;
#if defined(_WIN32)
        vf_base = load_font_file(c, "C:/Windows/Fonts/bahnschrift.ttf");
        if (vf_base != kInvalidFont) has_true_vf = true;
#endif
        if (vf_base == kInvalidFont) vf_base = kFontSans;

        const FontAxisValue axes_a[2] = {{kAxisWeight, 700.0f}, {kAxisWidth, 85.0f}};
        const FontAxisValue axes_b[2] = {{kAxisWidth, 85.0f}, {kAxisWeight, 700.0f}};
        const FontFeature feats_a[2] = {{make_opentype_tag('t', 'n', 'u', 'm'), 1u}, {make_opentype_tag('z', 'e', 'r', 'o'), 1u}};
        const FontFeature feats_b[2] = {{make_opentype_tag('z', 'e', 'r', 'o'), 1u}, {make_opentype_tag('t', 'n', 'u', 'm'), 1u}};

        const uint16_t v_bold_1 = create_font_variation(c, vf_base, axes_a, feats_a);
        const uint16_t v_bold_2 = create_font_variation(c, vf_base, axes_b, feats_b);
        const FontAxisValue axes_thin[1] = {{kAxisWeight, 300.0f}};
        const uint16_t v_thin = create_font_variation(c, vf_base, axes_thin);

        check(v_bold_1 != kInvalidFont && v_bold_1 != vf_base && is_font_loaded(c, v_bold_1), __LINE__);
        check(v_bold_2 == v_bold_1, __LINE__); // Deduplication regardless of axis/feature order
        check(v_thin != kInvalidFont && v_thin != vf_base && v_thin != v_bold_1 && is_font_loaded(c, v_thin), __LINE__);

        // Chained variations resolve to canonical root slot and deduplicate cleanly
        check(create_font_variation(c, v_thin, {}) == v_thin, __LINE__);
        check(create_font_variation(c, v_thin, axes_a, feats_a) == v_bold_1, __LINE__);
        const FontAxisValue axes_wdth[1] = {{kAxisWidth, 85.0f}};
        const FontAxisValue axes_thin_narrow[2] = {{kAxisWeight, 300.0f}, {kAxisWidth, 85.0f}};
        const uint16_t v_chain_new = create_font_variation(c, v_thin, axes_wdth);
        const uint16_t v_from_root = create_font_variation(c, vf_base, axes_thin_narrow);
        check(v_chain_new != kInvalidFont && v_chain_new != v_thin && v_chain_new != vf_base &&
              v_from_root == v_chain_new, __LINE__);

        // Render large 44px Slug text (exercises hb_font_draw_glyph() curve extraction on both variations)
        const Color dark_bg = Color::rgb_hex(0x0B0E14);
        begin_frame(c, {.viewport_w = 420, .viewport_h = 160});
        // Reject mid-frame font variation mutations cleanly
        const FontAxisValue axes_mid[1] = {{kAxisWeight, 550.0f}};
        check(create_font_variation(c, vf_base, axes_mid) == kInvalidFont, __LINE__);
        begin_box({
            .axis = Axis::Col,
            .width = Sizing::fixed(420.0f),
            .height = Sizing::fixed(160.0f),
            .padding = Padding::all(16.0f),
            .gap = 12.0f,
            .background = dark_bg,
        });
        text(str_lit("VARIABLE 0123"), {.color = Color::white(), .font_size_px = 44.0f, .font_id = v_thin, .wrap = false});
        text(str_lit("VARIABLE 0123"), {.color = Color::white(), .font_size_px = 44.0f, .font_id = v_bold_1, .wrap = false});
        end_box();
        end_frame(c);

        check(render_headless_png(c, 420u, 160u, png_path, dark_bg), __LINE__);
        check(frame_stats(c).slug_curve_count > 0u && frame_stats(c).slug_glyph_count > 0u, __LINE__);
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            auto count_ink = [&](int y0, int y1) noexcept -> uint32_t {
                uint32_t lit = 0u;
                for (int y = y0; y < y1; ++y) {
                    for (int x = 12; x < 408; ++x) {
                        Uint8 r = 0, g = 0, b = 0, a = 0;
                        if (SDL_ReadSurfacePixel(img, x, y, &r, &g, &b, &a) && r > 120 && g > 120 && b > 120) {
                            ++lit;
                        }
                    }
                }
                return lit;
            };
            const uint32_t thin_ink = count_ink(12, 76);
            const uint32_t bold_ink = count_ink(76, 152);
            check(thin_ink >= 250u && bold_ink >= 250u, __LINE__);
            if (has_true_vf) {
                // Bahnschrift wght=700 must produce noticeably heavier strokes than wght=300
                if (!(bold_ink > thin_ink + (thin_ink / 5u))) {
                    std::fprintf(stderr, "v028 [%s] variable font ink thin=%u bold=%u (expected bold > 1.2x thin)\n",
                        drv_label, thin_ink, bold_ink);
                }
                check(bold_ink > thin_ink + (thin_ink / 5u), __LINE__);
            }
            SDL_DestroySurface(img);
        } else {
            check(false, __LINE__);
        }
    }

    // ------------------------------------------------------------------
    // (e) Clipped Hollow-Bordered Cards Batching (<= 8 Draw Calls)
    // ------------------------------------------------------------------
    {
        const Color canvas = Color::rgb_hex(0x0D0F14);
        const Color card_bg = Color::rgb_hex(0x161A24);
        const Color card_border = Color::rgba8(255, 255, 255, 36);
        const Color accents[6] = {
            Color::rgb_hex(0x38BDF8),
            Color::rgb_hex(0x34D399),
            Color::rgb_hex(0xA78BFA),
            Color::rgb_hex(0xFBBF24),
            Color::rgb_hex(0xFB7185),
            Color::rgb_hex(0x60A5FA),
        };
        const char* titles[6] = {
            "Telemetry 01", "Pipelines 02", "Shaping 03",
            "Masks 04",     "Blending 05",  "Batching 06",
        };

        begin_frame(c, {.viewport_w = 660, .viewport_h = 280});
        begin_box({
            .axis = Axis::Col,
            .width = Sizing::fixed(660.0f),
            .height = Sizing::fixed(280.0f),
            .padding = Padding::all(16.0f),
            .gap = 12.0f,
            .background = canvas,
        });
        for (int row = 0; row < 2; ++row) {
            begin_box({.axis = Axis::Row, .width = Sizing::grow(), .height = Sizing::grow(), .gap = 12.0f});
            for (int col = 0; col < 3; ++col) {
                const int idx = row * 3 + col;
                begin_box({
                    .axis = Axis::Col,
                    .width = Sizing::grow(),
                    .height = Sizing::grow(),
                    .padding = Padding::all(12.0f),
                    .gap = 6.0f,
                    .background = card_bg,
                    .border_color = card_border,
                    .border_width = 1.0f,
                    .radius = Corners::all(10.0f),
                    .clip_children = true,
                });
                text(str_cstr(titles[idx]), {.color = accents[idx], .font_size_px = 14.0f, .font_id = kFontBold, .wrap = false});
                text(str_lit("Metric 128.40 ms"), {.color = Color::white(), .font_size_px = 13.0f, .wrap = false, .opentype_flags = OpenTypeFlag_TabularNums});
                text(str_lit("Zero-occlusion hollow border"), {.color = Color::rgb_hex(0x94A3B8), .font_size_px = 12.0f, .wrap = false});
                end_box();
            }
            end_box();
        }
        end_box();
        end_frame(c);

        check(render_headless_png(c, 660u, 280u, png_path, canvas), __LINE__);
        const uint32_t dc = frame_stats(c).draw_call_count;
        if (dc == 0u || dc > 8u) {
            std::fprintf(stderr, "v028 [%s] clipped hollow-bordered 6-card grid draw_call_count=%u (expected 1..8)\n", drv_label, dc);
        }
        check(dc > 0u && dc <= 8u, __LINE__);
    }

    // ------------------------------------------------------------------
    // (f) release_window() active_ui fallback (single-window & multi-window)
    // ------------------------------------------------------------------
    {
        SDL_Window* win1 = SDL_CreateWindow("Etch RW1", 320, 200, SDL_WINDOW_HIDDEN);
        SDL_Window* win2 = SDL_CreateWindow("Etch RW2", 320, 200, SDL_WINDOW_HIDDEN);
        check(win1 != nullptr && win2 != nullptr, __LINE__);
        if (win1 && win2) {
            // Single-window context: releasing the sole window must keep active_ui valid (headless fallback)
            Context* rw1 = create_context({.window = win1, .headless = false, .gpu_driver = driver});
            check(rw1 != nullptr && claimed_window_count(rw1) == 1u, __LINE__);
            if (rw1) {
                set_current_context(rw1);
                release_window(rw1, win1);
                check(claimed_window_count(rw1) == 0u, __LINE__);
                check(!debug_hud_open(rw1), __LINE__);
                set_debug_hud_open(rw1, true);
                check(debug_hud_open(rw1), __LINE__);
                const uint32_t sid = id("v028_rw_split1");
                set_split_ratio(sid, 0.35f);
                check(std::fabs(split_ratio(sid) - 0.35f) < 1e-4f, __LINE__);
                destroy_context(rw1);
            }

            // Multi-window context: releasing active window falls back to remaining window, then headless
            Context* rw2 = create_context({.window = win1, .headless = false, .gpu_driver = driver});
            check(rw2 != nullptr && claimed_window_count(rw2) == 1u, __LINE__);
            if (rw2) {
                set_current_context(rw2);
                const uint32_t sid2 = id("v028_rw_split2");
                set_split_ratio(sid2, 0.62f);
                // Claim win2 and make win2's UI active
                begin_window_frame(rw2, win2, 1.0f / 60.0f);
                box({.width = Sizing::fixed(40.0f), .height = Sizing::fixed(40.0f), .background = Color::white()});
                end_window_frame(rw2);
                check(claimed_window_count(rw2) == 2u, __LINE__);

                // Releasing win2 while win2 is active_ui must restore win1's UI state
                release_window(rw2, win2);
                check(claimed_window_count(rw2) == 1u, __LINE__);
                check(std::fabs(split_ratio(sid2) - 0.62f) < 1e-4f, __LINE__);
                check(!debug_hud_open(rw2), __LINE__);

                // Releasing win1 (last window) must fall back to headless_ui without crashing
                release_window(rw2, win1);
                check(claimed_window_count(rw2) == 0u, __LINE__);
                set_debug_hud_open(rw2, true);
                check(debug_hud_open(rw2), __LINE__);
                check(std::fabs(split_ratio(sid2) - 0.5f) < 1e-4f, __LINE__);
                destroy_context(rw2);
            }
            set_current_context(c);
        }
        if (win2) SDL_DestroyWindow(win2);
        if (win1) SDL_DestroyWindow(win1);
    }

    // ------------------------------------------------------------------
    // (f) Native SVG document rasterization (LunaSVG+PlutoVG) & WebP decoding (SimpleWebP)
    // ------------------------------------------------------------------
    {
        static const char kTestSvg[] =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"80\" height=\"40\" viewBox=\"0 0 80 40\">"
            "<rect width=\"80\" height=\"40\" rx=\"6\" fill=\"#E59A38\"/>"
            "<circle cx=\"20\" cy=\"20\" r=\"10\" fill=\"#2E9A74\"/>"
            "<text x=\"36\" y=\"25\" font-size=\"14\" fill=\"#121214\">OK</text>"
            "</svg>";
        LoadedTexture svg_tex = load_svg_memory(c, kTestSvg, sizeof(kTestSvg) - 1u, {.scale = 2.0f});
        check(svg_tex.texture != nullptr && svg_tex.width == 160u && svg_tex.height == 80u, __LINE__);
        LoadedTexture auto_svg = load_texture_memory(c, kTestSvg, sizeof(kTestSvg) - 1u, true);
        check(auto_svg.texture != nullptr && auto_svg.width == 160u && auto_svg.height == 80u, __LINE__);

        // 2x2 solid RGBA(40, 200, 120, 255) lossless WebP fixture
        static const uint8_t kTestWebp[38] = {
            0x52,0x49,0x46,0x46,0x1E,0x00,0x00,0x00,0x57,0x45,0x42,0x50,0x56,0x50,0x38,0x4C,
            0x11,0x00,0x00,0x00,0x2F,0x01,0x40,0x00,0x00,0x07,0x50,0xE4,0xA2,0x14,0xAF,0xFF,
            0x81,0x88,0xE8,0x7F,0x00,0x00
        };
        LoadedTexture webp_tex = load_texture_memory(c, kTestWebp, sizeof(kTestWebp), true);
        check(webp_tex.texture != nullptr && webp_tex.width == 2u && webp_tex.height == 2u, __LINE__);

        begin_frame(c, {.viewport_w = 220, .viewport_h = 100});
        begin_box({.axis = Axis::Row, .width = Sizing::fixed(220.0f), .height = Sizing::fixed(100.0f),
                   .padding = Padding::all(10.0f), .gap = 10.0f, .background = Color::rgb_hex(0x101014)});
        if (svg_tex.texture) {
            image({.texture = svg_tex.texture, .width = Sizing::fixed(120.0f), .height = Sizing::fixed(60.0f)});
        }
        if (webp_tex.texture) {
            image({.texture = webp_tex.texture, .width = Sizing::fixed(60.0f), .height = Sizing::fixed(60.0f)});
        }
        end_box();
        end_frame(c);
        check(render_headless_png(c, 220u, 100u, png_path, Color::rgb_hex(0x101014)), __LINE__);
        if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            // Center of WebP swatch at (140 + 30, 10 + 30) -> expected ~(40, 200, 120)
            check(SDL_ReadSurfacePixel(img, 170, 40, &r, &g, &b, &a), __LINE__);
            check(r < 65 && g > 175 && b > 95 && b < 145, __LINE__);
            // Green circle center inside SVG at x = 10 + 120*(20/80) = 40, y = 10 + 60*(20/40) = 40 -> expected ~(46, 154, 116)
            check(SDL_ReadSurfacePixel(img, 40, 40, &r, &g, &b, &a), __LINE__);
            check(g > r + 40 && g > 120, __LINE__);
            SDL_DestroySurface(img);
        }
        destroy_texture(c, &webp_tex);
        destroy_texture(c, &auto_svg);
        destroy_texture(c, &svg_tex);
    }

    {
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = 64u;
        info.height = 64u;
        info.layer_count_or_depth = 1u;
        info.num_levels = 1u;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        if (SDL_GPUTexture* target = SDL_CreateGPUTexture(dev, &info)) {
            const RenderTargetDesc output = {.texture = target, .width = 64u, .height = 64u,
                .sdl_texture_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
                .clear_color = Color::rgb_hex(0x101010)};
            begin_frame(c, {.viewport_w = 64u, .viewport_h = 64u});
            box({.width = Sizing::fixed(32.f), .height = Sizing::fixed(32.f),
                 .background = Color::rgb_hex(0xF02020)});
            check(!render_to_texture(c, output), __LINE__);
            end_frame(c);
            check(render_to_texture(c, output), __LINE__);
            begin_frame(c, {.viewport_w = 64u, .viewport_h = 64u});
            begin_box({.width = Sizing::fixed(64.f), .height = Sizing::fixed(64.f)});
            draw_image({.texture = target, .rect = {0.f, 0.f, 64.f, 64.f}});
            end_box();
            end_frame(c);
            uint32_t received = 0u;
            ReadPixelsDesc capture = {.frame_width = 64u, .frame_height = 64u,
                .x = 16u, .y = 28u, .width = 19u, .height = 8u,
                .receive = [](const uint8_t* rgba, uint32_t w, uint32_t h, uint32_t pitch, void* user) {
                    ++*static_cast<uint32_t*>(user);
                    return w == 19u && h == 8u && pitch >= w * 4u &&
                           rgba[0] > 200u && rgba[1] < 55u && rgba[2] < 55u &&
                           rgba[18u * 4u] < 50u && rgba[7u * pitch] < 50u;
                }, .user_data = &received};
            check(render_headless_pixels(c, capture) && received == 1u, __LINE__);
            capture.x = 60u;
            check(!render_headless_pixels(c, capture) && received == 1u, __LINE__);
            capture.width = 0u;
            check(!render_headless_pixels(c, capture) && received == 2u, __LINE__);
            check(render_headless_png(c, 64u, 64u, png_path), __LINE__);
            if (SDL_Surface* img = SDL_LoadPNG(png_path)) {
                Uint8 r = 0u, g = 0u, b = 0u, a = 0u;
                check(SDL_ReadSurfacePixel(img, 16, 16, &r, &g, &b, &a) &&
                      r > 200u && g < 55u && b < 55u, __LINE__);
                check(SDL_ReadSurfacePixel(img, 48, 48, &r, &g, &b, &a) &&
                      r < 50u && g < 50u && b < 50u, __LINE__);
                SDL_DestroySurface(img);
            } else {
                check(false, __LINE__);
            }
            SDL_ReleaseGPUTexture(dev, target);
        } else {
            check(false, __LINE__);
        }
    }
    std::remove(png_path);
    destroy_context(c);
    set_current_context(nullptr);
    return ok;
}

inline bool verify_v028() noexcept {
    const char* env_drv = SDL_getenv("ETCH_GPU_DRIVER");
    if (!env_drv || !env_drv[0]) env_drv = SDL_getenv("SDL_GPU_DRIVER");
    if (env_drv && env_drv[0]) {
        return verify_v028_driver(env_drv);
    }
#if defined(_WIN32)
    return verify_v028_driver("direct3d12") && verify_v028_driver("vulkan");
#else
    return verify_v028_driver(nullptr);
#endif
}
