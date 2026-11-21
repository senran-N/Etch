#pragma once

#include "etch/etch.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstring>

inline bool verify_phase2_layout_cpu() noexcept {
    using namespace etch;
    bool ok = true;
    const auto check = [&](bool pass, int line) noexcept {
        if (!pass) {
            ok = false;
            std::fprintf(stderr, "phase2_layout_cpu check line %d failed\n", line);
        }
    };
    const auto near_f = [](float a, float b, float eps = 0.25f) noexcept {
        return std::fabs(a - b) <= eps;
    };

    Context* ctx = create_context({.headless = true, .gpu_driver = "nonexistent_driver_xyz"});
    if (!ctx) return false;

    // 1. Flexbox Justify (SpaceBetween / SpaceAround / SpaceEvenly), weighted flex_grow,
    //    flex_shrink, AlignSelf, margin, and sub-pixel float padding/gap.
    {
        begin_frame(ctx, {.viewport_w = 500, .viewport_h = 400});
        begin_box({.axis = Axis::Col, .width = Sizing::fixed(500), .height = Sizing::fixed(400)});

        // SpaceBetween: 300px wide, 3x 40px children -> x = 0, 130, 260
        begin_box({.axis = Axis::Row, .width = Sizing::fixed(300), .height = Sizing::fixed(30), .justify = Justify::SpaceBetween});
        box({.id = id("p2_sb0"), .width = Sizing::fixed(40), .height = Sizing::fixed(20)});
        box({.id = id("p2_sb1"), .width = Sizing::fixed(40), .height = Sizing::fixed(20)});
        box({.id = id("p2_sb2"), .width = Sizing::fixed(40), .height = Sizing::fixed(20)});
        end_box();

        // SpaceAround: 300px wide, 2x 50px children -> leftover=200, slot=100 -> x = 50, 200
        begin_box({.axis = Axis::Row, .width = Sizing::fixed(300), .height = Sizing::fixed(30), .justify = Justify::SpaceAround});
        box({.id = id("p2_sa0"), .width = Sizing::fixed(50), .height = Sizing::fixed(20)});
        box({.id = id("p2_sa1"), .width = Sizing::fixed(50), .height = Sizing::fixed(20)});
        end_box();

        // SpaceEvenly: 300px wide, 2x 60px children -> leftover=180, step=60 -> x = 60, 180
        begin_box({.axis = Axis::Row, .width = Sizing::fixed(300), .height = Sizing::fixed(30), .justify = Justify::SpaceEvenly});
        box({.id = id("p2_se0"), .width = Sizing::fixed(60), .height = Sizing::fixed(20)});
        box({.id = id("p2_se1"), .width = Sizing::fixed(60), .height = Sizing::fixed(20)});
        end_box();

        // Weighted flex_grow (1:3 across 400px -> 100px and 300px) + AlignSelf + Margin
        begin_box({.axis = Axis::Row, .width = Sizing::fixed(400), .height = Sizing::fixed(60)});
        box({.id = id("p2_fg1"), .width = Sizing::grow(), .height = Sizing::fixed(20), .align_self = AlignSelf::Center, .flex_grow = 1.0f});
        box({.id = id("p2_fg3"), .width = Sizing::grow(), .height = Sizing::fixed(20), .align_self = AlignSelf::End, .flex_grow = 3.0f});
        end_box();

        // flex_shrink: 200px container, two 150px Fit children (min_w = 0 via shrink_width).
        // First has flex_shrink=0 (stays 150), second has flex_shrink=1 (shrinks to 50).
        begin_box({.axis = Axis::Row, .width = Sizing::fixed(200), .height = Sizing::fixed(30)});
        begin_box({.id = id("p2_fs0"), .width = Sizing::fit(), .height = Sizing::fixed(20), .shrink_width = true, .flex_shrink = 0.0f});
        box({.width = Sizing::fixed(150), .height = Sizing::fixed(20)});
        end_box();
        begin_box({.id = id("p2_fs1"), .width = Sizing::fit(), .height = Sizing::fixed(20), .shrink_width = true, .flex_shrink = 1.0f});
        box({.width = Sizing::fixed(150), .height = Sizing::fixed(20)});
        end_box();
        end_box();

        // Float sub-pixel padding + gap + margin
        begin_box({.axis = Axis::Row, .width = Sizing::fixed(200), .height = Sizing::fixed(40), .padding = {12.5f, 12.5f, 4.5f, 4.5f}, .gap = 3.5f});
        box({.id = id("p2_fp0"), .width = Sizing::fixed(20), .height = Sizing::fixed(10), .margin = {2.5f, 1.5f, 0.0f, 0.0f}});
        box({.id = id("p2_fp1"), .width = Sizing::fixed(20), .height = Sizing::fixed(10)});
        end_box();

        end_box();
        end_frame(ctx);

        const Rect sb0 = element_bounds(id("p2_sb0"));
        const Rect sb1 = element_bounds(id("p2_sb1"));
        const Rect sb2 = element_bounds(id("p2_sb2"));
        check(near_f(sb0.x, 0.0f) && near_f(sb1.x, 130.0f) && near_f(sb2.x, 260.0f), __LINE__);

        const Rect sa0 = element_bounds(id("p2_sa0"));
        const Rect sa1 = element_bounds(id("p2_sa1"));
        check(near_f(sa0.x, 50.0f) && near_f(sa1.x, 200.0f), __LINE__);

        const Rect se0 = element_bounds(id("p2_se0"));
        const Rect se1 = element_bounds(id("p2_se1"));
        check(near_f(se0.x, 60.0f) && near_f(se1.x, 180.0f), __LINE__);

        const Rect fg1 = element_bounds(id("p2_fg1"));
        const Rect fg3 = element_bounds(id("p2_fg3"));
        check(near_f(fg1.w, 100.0f) && near_f(fg3.w, 300.0f), __LINE__);
        check(near_f(fg1.y - 90.0f, 20.0f) && near_f(fg3.y - 90.0f, 40.0f), __LINE__);

        const Rect fs0 = element_bounds(id("p2_fs0"));
        const Rect fs1 = element_bounds(id("p2_fs1"));
        check(near_f(fs0.w, 150.0f) && near_f(fs1.w, 50.0f), __LINE__);

        const Rect fp0 = element_bounds(id("p2_fp0"));
        const Rect fp1 = element_bounds(id("p2_fp1"));
        // fp0.x = 12.5 + 2.5 = 15.0; fp1.x = 15.0 + 20.0 + 1.5 + 3.5 = 40.0
        check(near_f(fp0.x, 15.0f, 0.05f) && near_f(fp1.x, 40.0f, 0.05f), __LINE__);
    }

    // 2. Full 2D Grid with explicit grid_row_tracks + grid_row_span=2 + grid_col_span=2
    {
        const GridTrack col_tracks[3] = {GridTrack::px(80.0f), GridTrack::fr(1.0f), GridTrack::fr(2.0f)};
        const GridTrack row_tracks[2] = {GridTrack::px(40.0f), GridTrack::px(60.0f)};
        begin_frame(ctx, {.viewport_w = 400, .viewport_h = 200});
        // Total inner width = 380 - 2*10 = 360 -> col0=80, remaining=280 -> col1=93.33, col2=186.67
        // Total height = 40 + 10 + 60 = 110
        begin_box({.width = Sizing::fixed(380), .height = Sizing::fixed(110), .gap = 10.0f, .row_gap = 10.0f,
            .grid_tracks = col_tracks, .grid_row_tracks = row_tracks});
        // Cell (0,0) spanning 2 rows -> x=0, y=0, w=80, h=110
        box({.id = id("p2_g_span"), .width = Sizing::grow(), .height = Sizing::grow(), .grid_row_span = 2});
        // Cell (0,1..2) spanning 2 cols -> x=90, y=0, w=290, h=40
        box({.id = id("p2_g_top"), .width = Sizing::grow(), .height = Sizing::grow(), .grid_col_span = 2});
        // Cell (1,1) auto-flows past occupied (1,0) -> x=90, y=50, w=93.33, h=60
        box({.id = id("p2_g_b1"), .width = Sizing::grow(), .height = Sizing::grow()});
        // Cell (1,2) -> x=193.33, y=50, w=186.67, h=60
        box({.id = id("p2_g_b2"), .width = Sizing::grow(), .height = Sizing::grow()});
        end_box();
        end_frame(ctx);

        const Rect gs = element_bounds(id("p2_g_span"));
        const Rect gt = element_bounds(id("p2_g_top"));
        const Rect b1 = element_bounds(id("p2_g_b1"));
        const Rect b2 = element_bounds(id("p2_g_b2"));
        check(near_f(gs.x, 0.0f) && near_f(gs.y, 0.0f) && near_f(gs.w, 80.0f) && near_f(gs.h, 110.0f), __LINE__);
        check(near_f(gt.x, 90.0f) && near_f(gt.y, 0.0f) && near_f(gt.w, 290.0f) && near_f(gt.h, 40.0f), __LINE__);
        check(near_f(b1.x, 90.0f) && near_f(b1.y, 50.0f) && near_f(b1.w, 93.33f, 0.5f) && near_f(b1.h, 60.0f), __LINE__);
        check(near_f(b2.x, 193.33f, 0.5f) && near_f(b2.y, 50.0f) && near_f(b2.w, 186.67f, 0.5f) && near_f(b2.h, 60.0f), __LINE__);
    }

    // 3. > 16,384 elements in a single frame (proves zero Clay fixed element cap)
    {
        constexpr uint32_t kStressCount = 20000u;
        begin_frame(ctx, {.viewport_w = 300, .viewport_h = 200});
        begin_box({.axis = Axis::Col, .width = Sizing::fixed(200), .height = Sizing::fit()});
        for (uint32_t i = 0; i < kStressCount; ++i) {
            box({.id = (i == kStressCount - 1u) ? id("p2_last_20k") : 0u,
                 .width = Sizing::fixed(100), .height = Sizing::fixed(2)});
        }
        end_box();
        end_frame(ctx);
        const Rect last = element_bounds(id("p2_last_20k"));
        check(near_f(last.y, float(kStressCount - 1u) * 2.0f, 0.5f) && near_f(last.h, 2.0f), __LINE__);
    }

    // 4. Zero 1-frame lag on AttachTo::Element + flip_on_overflow on Frame 0 of fresh context
    {
        Context* fresh = create_context({.headless = true, .gpu_driver = "nonexistent_driver_xyz"});
        check(fresh != nullptr, __LINE__);
        if (fresh) {
            begin_frame(fresh, {.viewport_w = 400, .viewport_h = 200});
            begin_box({.axis = Axis::Col, .width = Sizing::fixed(400), .height = Sizing::fixed(200)});
            box({.width = Sizing::fixed(10), .height = Sizing::fixed(160)});
            box({.id = id("p2_trig"), .width = Sizing::fixed(140), .height = Sizing::fixed(28)});
            // Popover attached below trigger (y=188..248 -> overflows 200px viewport!), flip_on_overflow flips above (y=100..160).
            box({.id = id("p2_pop"), .width = Sizing::percent(1.0f), .height = Sizing::fixed(60),
                 .attach_to = AttachTo::Element, .attach_element_id = id("p2_trig"),
                 .anchor_element = Anchor::TopLeft, .anchor_parent = Anchor::BottomLeft,
                 .flip_on_overflow = true});
            end_box();
            end_frame(fresh);

            const Rect trig = element_bounds(id("p2_trig"));
            const Rect pop = element_bounds(id("p2_pop"));
            check(near_f(trig.y, 160.0f) && near_f(pop.w, 140.0f) && near_f(pop.y, 100.0f), __LINE__);
            destroy_context(fresh);
            set_current_context(ctx);
        }
    }

    // 5. Inverse-Transformed True Occlusion Hitbox Tree + topmost_hovered_id + clicked_without_drag
    {
        const uint32_t back_id = id("p2_back");
        const uint32_t front_id = id("p2_front");
        const uint32_t xform_id = id("p2_xform");
        const auto run_occ_frame = [&](Vec2 ptr, bool down, HitboxBehavior front_behavior) noexcept {
            begin_frame(ctx, {.viewport_w = 400, .viewport_h = 300, .pointer_pos = ptr, .pointer_down = down});
            begin_box({.width = Sizing::fixed(400), .height = Sizing::fixed(300)});
            box({.id = back_id, .width = Sizing::fixed(120), .height = Sizing::fixed(120),
                 .background = Color::rgb_hex(0x1E293B)});
            box({.id = front_id, .width = Sizing::fixed(100), .height = Sizing::fixed(100),
                 .background = front_behavior == HitboxBehavior::BlockMouse ? Color::transparent() : Color::rgb_hex(0x3B82F6),
                 .attach_to = AttachTo::Parent, .anchor_element = Anchor::TopLeft, .anchor_parent = Anchor::TopLeft,
                 .floating_offset = {40.0f, 40.0f}, .z_index = 5, .hitbox_behavior = front_behavior});
            push_layer({.translate = {180.0f, 60.0f}, .scale = {1.5f, 1.5f}});
            box({.id = xform_id, .width = Sizing::fixed(60), .height = Sizing::fixed(40),
                 .background = Color::rgb_hex(0x10B981)});
            pop_layer();
            end_box();
            end_frame(ctx);
        };

        // Prime hitboxes
        run_occ_frame({-1, -1}, false, HitboxBehavior::Normal);
        // Overlap at (60, 60): front strictly occludes back (zero hover bleed-through!)
        run_occ_frame({60, 60}, false, HitboxBehavior::Normal);
        check(hovered(front_id) && !hovered(back_id) && topmost_hovered_id() == front_id, __LINE__);

        // Explicit BlockMouse even with transparent background blocks back
        run_occ_frame({60, 60}, false, HitboxBehavior::BlockMouse);
        run_occ_frame({60, 60}, false, HitboxBehavior::BlockMouse);
        check(hovered(front_id) && !hovered(back_id), __LINE__);

        // Passthrough lets pointer hit back directly underneath
        run_occ_frame({60, 60}, false, HitboxBehavior::Passthrough);
        run_occ_frame({60, 60}, false, HitboxBehavior::Passthrough);
        check(!hovered(front_id) && hovered(back_id) && topmost_hovered_id() == back_id, __LINE__);

        // Transformed layer hitbox: xform raw layout rect is (120,0,60,40) (sibling of back in Row!),
        // translated by (+180,+60) & scaled 1.5x around pivot (150,20):
        // visual center = (150+180, 20+60) = (330, 80), size = (90, 60) -> [285..375, 50..110]!
        run_occ_frame({140, 20}, false, HitboxBehavior::Normal); // raw un-transformed pos misses!
        check(!hovered(xform_id), __LINE__);
        run_occ_frame({330, 80}, false, HitboxBehavior::Normal); // transformed visual center hits!
        check(hovered(xform_id) && topmost_hovered_id() == xform_id, __LINE__);

        // clicked_without_drag: small click (<4px) succeeds; large drag-and-release inside button fails clicked_without_drag
        run_occ_frame({10, 10}, false, HitboxBehavior::Normal);
        run_occ_frame({10, 10}, true, HitboxBehavior::Normal);
        run_occ_frame({11, 11}, true, HitboxBehavior::Normal);
        run_occ_frame({11, 11}, false, HitboxBehavior::Normal);
        check(clicked(back_id) && clicked_without_drag(back_id, 4.0f), __LINE__);

        run_occ_frame({10, 10}, true, HitboxBehavior::Normal);
        run_occ_frame({35, 10}, true, HitboxBehavior::Normal);
        run_occ_frame({35, 10}, false, HitboxBehavior::Normal);
        check(clicked(back_id) && !clicked_without_drag(back_id, 4.0f), __LINE__);
    }

    destroy_context(ctx);
    set_current_context(nullptr);
    return ok;
}

inline bool verify_phase2_cached_subtree_gpu() noexcept {
    using namespace etch;
    bool ok = true;
    const auto check = [&](bool pass, int line) noexcept {
        if (!pass) {
            ok = false;
            std::fprintf(stderr, "phase2_cached_subtree_gpu check line %d failed\n", line);
        }
    };

    Context* ctx = create_context({.headless = true});
    if (!ctx) return false;

    const uint32_t sub_id = id("p2_cached_card");
    const uint32_t inner_lbl_id = id("p2_cached_lbl");
    uint32_t body_runs = 0u;
    const auto run_card_frame = [&](float pad_x, float pad_y, uint64_t hash) noexcept {
        begin_frame(ctx, {.viewport_w = 240, .viewport_h = 160});
        begin_box({.width = Sizing::fixed(240), .height = Sizing::fixed(160),
                   .padding = {pad_x, 0.0f, pad_y, 0.0f}, .background = Color::rgb_hex(0x0F172A)});
        if (begin_cached_subtree(sub_id, hash)) {
            ++body_runs;
            begin_box({.width = Sizing::fixed(120), .height = Sizing::fixed(60),
                       .padding = Padding::all(10.0f),
                       .background = Color::rgb_hex(0x1E293B),
                       .border_color = Color::rgb_hex(0x38BDF8), .border_width = 2.0f,
                       .radius = Corners::all(8.0f)});
            text(str_lit("Cached OK"), {.color = Color::rgb_hex(0xF8FAFC), .font_size_px = 14.0f, .id = inner_lbl_id});
            end_box();
            end_cached_subtree();
        }
        end_box();
        end_frame(ctx);
    };

    // Frame 1: Cache MISS -> builds subtree and snapshots prims + hitboxes + A11y
    run_card_frame(16.0f, 16.0f, 0x1111ull);
    check(body_runs == 1u && frame_stats(ctx).cached_subtree_hits == 0u, __LINE__);
    const char* file_miss = "p2_cache_miss.png";
    const char* file_hit = "p2_cache_hit.png";
    const char* file_shift = "p2_cache_shift.png";
    check(render_headless_png(ctx, 240, 160, file_miss), __LINE__);

    // Frame 2: Cache HIT (same position, same hash) -> body skipped, pixel-identical PNG, A11y intact!
    run_card_frame(16.0f, 16.0f, 0x1111ull);
    check(body_runs == 1u && frame_stats(ctx).cached_subtree_hits == 1u, __LINE__);
    check(render_headless_png(ctx, 240, 160, file_hit), __LINE__);
    {
        SDL_Surface* s0 = SDL_LoadPNG(file_miss);
        SDL_Surface* s1 = SDL_LoadPNG(file_hit);
        check(s0 != nullptr && s1 != nullptr && s0->w == s1->w && s0->h == s1->h, __LINE__);
        if (s0 && s1) {
            bool exact = true;
            for (int y = 0; y < s0->h && exact; ++y) {
                for (int x = 0; x < s0->w; ++x) {
                    Uint8 r0 = 0, g0 = 0, b0 = 0, a0 = 0;
                    Uint8 r1 = 0, g1 = 0, b1 = 0, a1 = 0;
                    SDL_ReadSurfacePixel(s0, x, y, &r0, &g0, &b0, &a0);
                    SDL_ReadSurfacePixel(s1, x, y, &r1, &g1, &b1, &a1);
                    if (r0 != r1 || g0 != g1 || b0 != b1 || a0 != a1) {
                        exact = false;
                        break;
                    }
                }
            }
            check(exact, __LINE__);
        }
        if (s0) SDL_DestroySurface(s0);
        if (s1) SDL_DestroySurface(s1);
    }
    bool found_a11y_hit = false;
    Rect hit_a11y_bounds = {};
    for (const A11yNode& n : frame_a11y_nodes(ctx)) {
        if (n.id == inner_lbl_id) { found_a11y_hit = true; hit_a11y_bounds = n.bounds; break; }
    }
    check(found_a11y_hit && std::fabs(hit_a11y_bounds.x - 26.0f) < 1.0f && std::fabs(hit_a11y_bounds.y - 26.0f) < 1.0f, __LINE__);

    // Frame 3: Cache HIT with translated container (+40, +25) -> body skipped, shifted pixels + shifted A11y!
    run_card_frame(56.0f, 41.0f, 0x1111ull);
    check(body_runs == 1u && frame_stats(ctx).cached_subtree_hits == 1u, __LINE__);
    check(render_headless_png(ctx, 240, 160, file_shift), __LINE__);
    {
        SDL_Surface* s = SDL_LoadPNG(file_shift);
        check(s != nullptr, __LINE__);
        if (s) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            // Old location (20, 20) should now be canvas bg (0x0F172A), new location (62, 47) should be card bg (0x1E293B)!
            SDL_ReadSurfacePixel(s, 20, 20, &r, &g, &b, &a);
            check(std::abs(int(r) - 0x0F) < 4 && std::abs(int(g) - 0x17) < 4 && std::abs(int(b) - 0x2A) < 4, __LINE__);
            SDL_ReadSurfacePixel(s, 62, 47, &r, &g, &b, &a);
            check(std::abs(int(r) - 0x1E) < 4 && std::abs(int(g) - 0x29) < 4 && std::abs(int(b) - 0x3B) < 4, __LINE__);
            SDL_DestroySurface(s);
        }
    }
    bool found_a11y_shift = false;
    Rect shift_a11y_bounds = {};
    for (const A11yNode& n : frame_a11y_nodes(ctx)) {
        if (n.id == inner_lbl_id) { found_a11y_shift = true; shift_a11y_bounds = n.bounds; break; }
    }
    check(found_a11y_shift && std::fabs(shift_a11y_bounds.x - 66.0f) < 1.0f && std::fabs(shift_a11y_bounds.y - 51.0f) < 1.0f, __LINE__);

    // Frame 4: Hash change -> cache MISS, body re-runs
    run_card_frame(56.0f, 41.0f, 0x2222ull);
    check(body_runs == 2u && frame_stats(ctx).cached_subtree_hits == 0u, __LINE__);

    // Frame 5: Explicit invalidation -> cache MISS, body re-runs
    invalidate_cached_subtree(sub_id, ctx);
    run_card_frame(56.0f, 41.0f, 0x2222ull);
    check(body_runs == 3u && frame_stats(ctx).cached_subtree_hits == 0u, __LINE__);

    std::remove(file_miss);
    std::remove(file_hit);
    std::remove(file_shift);
    destroy_context(ctx);
    set_current_context(nullptr);
    return ok;
}
