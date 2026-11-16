#pragma once
#include "etch/etch.h"
#include "etch/etch_c.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>

inline bool verify_desktop_precision() noexcept {
    using namespace etch;
    Context* ctx = create_context({.headless = true});
    if (!ctx) return false;
    bool ok = true;
    const auto check = [&](bool pass, int line) noexcept {
        if (!pass) { ok = false; std::fprintf(stderr, "desktop precision line %d failed\n", line); }
    };
    for (float dpi : {1.0f, 1.25f, 1.5f, 2.0f}) {
        begin_frame(ctx, {.viewport_w=500, .viewport_h=300, .dpi_scale=dpi});
        begin_box({.id=id("precision.host"), .width=Sizing::fixed(200), .height=Sizing::fixed(100)});
        draw_rect({.rect={.3f,.7f,30.2f,20.4f}, .fill=Color::rgb_hex(0xE01020),
            .border_color=Color::white(), .border_width=.7f, .softness=.5f});
        end_box(); end_frame(ctx);
        bool found = false;
        for (const auto& p : frame_prims(ctx)) if (p.style.y == Color::rgb_hex(0xE01020).pack_rgba8()) {
            found = true;
            check(p.bounds.x == std::round(.3f*dpi) && p.bounds.z == std::round(30.5f*dpi)-p.bounds.x, __LINE__);
            check(p.style.w == pack_f16x2(std::fmax(1.0f,std::round(.7f*dpi)),.5f), __LINE__);
        }
        check(found, __LINE__);
    }
    begin_frame(ctx, {.viewport_w=400, .viewport_h=200, .pixel_snap=false});
    begin_box({.width=Sizing::fixed(200), .height=Sizing::fixed(40)});
    draw_rect({.rect={.3f,.7f,30.2f,20.4f}, .fill=Color::rgb_hex(0xE01020)});
    end_box(); end_frame(ctx);
    bool fractional=false;
    for (const auto& p : frame_prims(ctx)) if (p.style.y==Color::rgb_hex(0xE01020).pack_rgba8()) fractional=p.bounds.x==.3f;
    check(fractional, __LINE__);
    for (int frame=0;frame<4;++frame) {
        begin_frame(ctx, {.viewport_w=400, .viewport_h=200, .dt_seconds=.016f});
        const float value=spring(id("entrance"),100,0.0f);
        check(frame==0 ? value==0 : value>0 && value<100, __LINE__);
        check(spring(id("legacy"),12)==12, __LINE__);
        begin_box({.width=Sizing::fixed(140), .height=Sizing::fixed(40), .gap=4});
        begin_box({.id=id("shrink"), .width=Sizing::grow(), .height=Sizing::grow(), .shrink_width=true});
        text(str_lit("LongUnbreakableBreadcrumbThatMustNotPushTheCloseButtonAway"), {.wrap=false});
        end_box();
        box({.id=id("close"), .width=Sizing::fixed(32), .height=Sizing::fixed(32)});
        end_box(); end_frame(ctx);
        const auto* tail=find_a11y_node(id("close"),ctx);
        const auto* content=find_a11y_node(id("shrink"),ctx);
        check(tail && content && tail->bounds.x+tail->bounds.w<=140.01f && content->bounds.w<=104.01f, __LINE__);
    }
    begin_frame(ctx,{.viewport_w=400,.viewport_h=200});
    begin_box({.width=Sizing::fixed(300),.height=Sizing::fixed(100),.radius=Corners::all(20),.clip_children=true});
    draw_text({.text=str_lit("Long label é אבג 👩‍🔬"),.pos={100,4},.align_x=AlignX::Right,.max_width_px=40,.ellipsis=true});
    end_box();end_frame(ctx);
    bool glyph=false;
    for (const auto& p:frame_prims(ctx)) if ((p.style.x&15u)==uint32_t(PrimKind::SlugGlyph)) {
        glyph=true;check((p.style.x & (uint32_t(PrimFlag_RoundedClip)<<4)) != 0,__LINE__);check((p.style.x & (uint32_t(PrimFlag_TextClip)<<4)) != 0 && std::bit_cast<float>(p.style.z)==60 && std::bit_cast<float>(p.style.w)==100,__LINE__);
    }
    check(glyph,__LINE__);
    TableColumnDesc columns[2] = {
        {.title=str_lit("PhysicalV"),.width=120,.align_x=AlignX::Right},
        {.title=str_lit("Other"),.width=120}
    };
    const uint32_t table_id=id("precision.table");
    begin_frame(ctx,{.viewport_w=400,.viewport_h=200});
    begin_table({.id=table_id,.columns={columns,2},.width=Sizing::fixed(260),.height=Sizing::fixed(100)});
    end_table();end_frame(ctx);
    set_table_column_width(table_id,0,220);
    begin_frame(ctx,{.viewport_w=400,.viewport_h=200});
    begin_table({.id=table_id,.columns={columns,1},.width=Sizing::fixed(130),.height=Sizing::fixed(100)});
    end_table();end_frame(ctx);
    check(table_column_width(table_id,0)==120,__LINE__);
    bool header=false;
    for (const auto& p:frame_prims(ctx)) if ((p.style.x&15u)==uint32_t(PrimKind::SlugGlyph)) {
        header=true;check(p.bounds.x+p.bounds.z<100,__LINE__);
    }
    check(header,__LINE__);
    check(etch_frame_desc_default().pixel_snap && etch_window_frame_desc_default().pixel_snap && etch_rect_draw_desc_default().softness==1,__LINE__);
    check(create_desktop_window("invalid",0,100)==nullptr,__LINE__);
    destroy_context(ctx);set_current_context(nullptr);
    return ok;
}

inline bool verify_border_coverage() noexcept {
    using namespace etch;
    Context* ctx=create_context({.headless=true});
    if (!ctx) return false;
    begin_frame(ctx,{.viewport_w=100,.viewport_h=80});
    begin_box({.width=Sizing::fixed(100),.height=Sizing::fixed(80)});
    draw_rect({.rect={10,10,60,40},.fill=Color::white(),.border_color=Color::rgba8(255,0,0,128),.border_width=4,.softness=2});
    end_box();end_frame(ctx);
    const char* output="precision-border.png";
    bool ok=render_headless_png(ctx,100,80,output,Color::rgba8(0,0,0,255));
    SDL_Surface* surface=ok?SDL_LoadPNG(output):nullptr;
    if (surface) {
        Uint8 r=0,g=0,b=0,a=0;
        ok=SDL_ReadSurfacePixel(surface,11,30,&r,&g,&b,&a) && r>100 && g<8 && b<8;
        SDL_DestroySurface(surface);
    } else ok=false;
    std::remove(output);destroy_context(ctx);set_current_context(nullptr);
    return ok;
}
