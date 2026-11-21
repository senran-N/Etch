#pragma once
// Phase 1 Multi-Pipeline GPU, Hardware ClearType RGB Subpixel, Early-Z, Spatial Batching & BlurGlass Sharing checks.

inline bool verify_phase1_gpu() noexcept {
    using namespace etch;
    Context* c = create_context({.headless = true});
    if (!c) return false;
    set_current_context(c);
    bool ok = true;
    const auto check = [&](bool cond, int line) noexcept {
        if (!cond) {
            ok = false;
            std::fprintf(stderr, "phase1_gpu check failed at line %d\n", line);
        }
    };
    const char* path = "phase1-gpu-check.png";

    // 1. Multi-pipeline dispatch, over(fill, border) compositing, ClearType RGB subpixel & TextClip.
    {
        begin_frame(c, {.viewport_w = 420, .viewport_h = 200});
        begin_box({.width = Sizing::fixed(420), .height = Sizing::fixed(200), .background = Color::rgb_hex(0x10141A)});
        // Opaque QuadOpaque
        draw_rect({.rect = {10, 10, 60, 60}, .fill = Color::rgb_hex(0x1020D0)});
        // QuadAlpha with opaque white fill + 50% red border: border must blend over fill -> bright pink, not darkened gray.
        draw_rect({.rect = {80, 10, 60, 60}, .fill = Color::white(), .radius = Corners::all(8), .border_color = Color::rgba8(255, 0, 0, 128), .border_width = 8});
        // 14px axis-aligned white text -> ClearType 3-channel RGB SubpixelText on _WIN32.
        draw_text({.text = str_lit("ClearType RGB Subpixel 012345"), .pos = {10, 90}, .size = 14, .color = Color::white()});
        // 14px text clipped by max_width_px -> PrimFlag_TextClip in etch_subpixel.hlsl.
        draw_text({.text = str_lit("WWWWWWWWWWWWWWWWWWWW"), .pos = {10, 125}, .size = 14, .color = Color::white(), .max_width_px = 42.0f});
        // 32px large text -> Slug GPU Bezier pipeline (Uber).
        draw_text({.text = str_lit("Slug"), .pos = {170, 10}, .size = 32, .color = Color::rgb_hex(0x38BDF8)});
        end_box();
        end_frame(c);

        uint32_t slug_count = 0u, subpixel_count = 0u;
        for (const Prim& p : frame_prims(c)) {
            const uint32_t kind = p.style.x & 0xFu;
            if (kind == uint32_t(PrimKind::SlugGlyph)) ++slug_count;
            else if (kind == uint32_t(PrimKind::Glyph)) ++subpixel_count;
        }
        check(slug_count >= 4u, __LINE__);
#if defined(_WIN32)
        check(subpixel_count >= 20u, __LINE__);
#endif
        check(render_headless_png(c, 420, 200, path, Color::black()), __LINE__);
        if (SDL_Surface* surf = SDL_LoadPNG(path)) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            // Opaque blue rect
            check(SDL_ReadSurfacePixel(surf, 40, 40, &r, &g, &b, &a) && r < 30 && g < 50 && b > 190, __LINE__);
            // Border over white fill -> bright pink (R~255, G/B ~ 120..200 in sRGB)
            check(SDL_ReadSurfacePixel(surf, 83, 40, &r, &g, &b, &a) && r > 240 && g > 110 && g < 215 && b > 110 && b < 215, __LINE__);
            // Center of bordered rect -> pure white
            check(SDL_ReadSurfacePixel(surf, 110, 40, &r, &g, &b, &a) && r > 250 && g > 250 && b > 250, __LINE__);
            // Large Slug glyph ink
            uint32_t slug_ink = 0u;
            for (int y = 10; y < 55; ++y)
                for (int x = 170; x < 260; ++x)
                    if (SDL_ReadSurfacePixel(surf, x, y, &r, &g, &b, &a) && b > 180) ++slug_ink;
            check(slug_ink > 100u, __LINE__);
            // Subpixel text ink & 3-channel RGB fringe on Windows
            uint32_t sub_ink = 0u, sub_rgb_edges = 0u;
            for (int y = 86; y < 114; ++y) {
                for (int x = 10; x < 260; ++x) {
                    if (SDL_ReadSurfacePixel(surf, x, y, &r, &g, &b, &a)) {
                        if (r > 120 || g > 120 || b > 120) ++sub_ink;
                        if (std::abs(int(r) - int(b)) > 20) ++sub_rgb_edges;
                    }
                }
            }
            check(sub_ink > 80u, __LINE__);
#if defined(_WIN32)
            check(sub_rgb_edges > 20u, __LINE__);
#endif
            // TextClip: ink inside [10, 54], zero ink in [62, 220]
            uint32_t clip_in = 0u, clip_out = 0u;
            for (int y = 122; y < 148; ++y) {
                for (int x = 10; x < 54; ++x)
                    if (SDL_ReadSurfacePixel(surf, x, y, &r, &g, &b, &a) && (r > 80 || g > 80 || b > 80)) ++clip_in;
                for (int x = 62; x < 220; ++x)
                    if (SDL_ReadSurfacePixel(surf, x, y, &r, &g, &b, &a) && (r > 40 || g > 40 || b > 40)) ++clip_out;
            }
            check(clip_in > 30u && clip_out == 0u, __LINE__);
            SDL_DestroySurface(surf);
        } else {
            check(false, __LINE__);
        }
    }

    // 2. Early-Z front-to-back opaque occlusion correctness.
    {
        begin_frame(c, {.viewport_w = 120, .viewport_h = 120});
        begin_box({.width = Sizing::fixed(120), .height = Sizing::fixed(120), .background = Color::black()});
        // Earlier translucent green
        draw_rect({.rect = {10, 10, 100, 100}, .fill = Color::rgba8(0, 220, 0, 200), .radius = Corners::all(6)});
        // Later opaque square red (hoisted into Early-Z pass)
        draw_rect({.rect = {25, 25, 70, 70}, .fill = Color::rgba8(230, 20, 20, 255)});
        // Even later translucent blue (must blend OVER opaque red, NOT be rejected by Early-Z)
        draw_rect({.rect = {45, 45, 30, 30}, .fill = Color::rgba8(0, 90, 255, 190), .radius = Corners::all(4)});
        end_box();
        end_frame(c);
        check(render_headless_png(c, 120, 120, path, Color::black()), __LINE__);
        if (SDL_Surface* surf = SDL_LoadPNG(path)) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            // (16,16): green only
            check(SDL_ReadSurfacePixel(surf, 16, 16, &r, &g, &b, &a) && g > 140 && r < 25 && b < 25, __LINE__);
            // (32,32): opaque red occludes earlier green completely
            check(SDL_ReadSurfacePixel(surf, 32, 32, &r, &g, &b, &a) && r > 220 && g < 30 && b < 30, __LINE__);
            // (60,60): translucent blue over opaque red -> strong blue + non-zero red
            check(SDL_ReadSurfacePixel(surf, 60, 60, &r, &g, &b, &a) && b > 140 && r > 40, __LINE__);
            SDL_DestroySurface(surf);
        } else {
            check(false, __LINE__);
        }
    }

    // 3. Spatial non-overlapping batch coalescing: 10 interleaved cards collapse into <= 4 draw calls.
    {
        begin_frame(c, {.viewport_w = 640, .viewport_h = 260});
        begin_box({.width = Sizing::fixed(640), .height = Sizing::fixed(260), .background = Color::rgb_hex(0x0D1117)});
        for (uint32_t i = 0; i < 10; ++i) {
            const float x = 16.0f + float(i % 5u) * 122.0f;
            const float y = 16.0f + float(i / 5u) * 116.0f;
            draw_rect({.rect = {x, y, 108, 96}, .fill = Color::rgba8(30, 41, 59, 220),
                .radius = Corners::all(10), .border_color = Color::rgba8(148, 163, 184, 80), .border_width = 1});
            draw_text({.text = str_lit("Card"), .pos = {x + 12.0f, y + 14.0f}, .size = 14, .color = Color::white()});
        }
        end_box();
        end_frame(c);
        check(frame_stats(c).draw_call_count <= 4u, __LINE__);
    }

    // 4. Non-overlapping BlurGlass cards WITH text inside share 1 BlurSplit; overlapping modal triggers 2.
    {
        for (int overlap = 0; overlap < 2; ++overlap) {
            begin_frame(c, {.viewport_w = 640, .viewport_h = 260});
            begin_box({.width = Sizing::fixed(640), .height = Sizing::fixed(260), .background = Color::rgb_hex(0x1E293B)});
            draw_mesh_rect({.rect = {0, 0, 640, 260}, .color_tl = Color::rgb_hex(0x0EA5E9),
                .color_tr = Color::rgb_hex(0x6366F1), .color_br = Color::rgb_hex(0xEC4899), .color_bl = Color::rgb_hex(0x10B981)});
            for (uint32_t i = 0; i < 10; ++i) {
                const float x = 16.0f + float(i % 5u) * 122.0f;
                const float y = 16.0f + float(i / 5u) * 116.0f;
                begin_box({.width = Sizing::fixed(108), .height = Sizing::fixed(96), .padding = Padding::all(10),
                    .background = Color::rgba8(15, 23, 42, 120), .radius = Corners::all(10),
                    .attach_to = AttachTo::Root, .anchor_parent = Anchor::TopLeft, .floating_offset = {x, y}, .backdrop_blur = 10});
                text(str_lit("Frosted"), {.color = Color::white(), .font_size_px = 13});
                end_box();
            }
            if (overlap) {
                begin_box({.width = Sizing::fixed(180), .height = Sizing::fixed(120), .padding = Padding::all(12),
                    .background = Color::rgba8(15, 23, 42, 160), .radius = Corners::all(12),
                    .attach_to = AttachTo::Root, .anchor_parent = Anchor::TopLeft, .floating_offset = {40, 40}, .z_index = 20, .backdrop_blur = 12});
                text(str_lit("Modal"), {.color = Color::white(), .font_size_px = 14});
                end_box();
            }
            end_box();
            end_frame(c);
            check(render_headless_png(c, 640, 260, path, Color::black()), __LINE__);
            const uint32_t expected_splits = overlap ? 2u : 1u;
            check(frame_stats(c).blur_pass_count == expected_splits, __LINE__);
        }
    }

    std::remove(path);
    destroy_context(c);
    set_current_context(nullptr);
    return ok;
}
