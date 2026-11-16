#pragma once
#include "etch/etch.h"
#include "etch/etch_c.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>

inline bool verify_visual_quality_cpu() noexcept {
    using namespace etch;
    Context* ctx = create_context({.headless=true});
    if (!ctx) return false;
    bool ok = true;
    const auto check = [&](bool pass, int line) noexcept {
        if (!pass) { ok=false; std::fprintf(stderr,"visual quality line %d failed\n",line); }
    };
    check(!set_font_tabular_numbers(nullptr,kFontSans,true),__LINE__);
    check(!set_font_tabular_numbers(ctx,kMaxFontSlots,true),__LINE__);
    check(set_font_tabular_numbers(ctx,kFontSans,true),__LINE__);
    begin_frame(ctx,{.viewport_w=400,.viewport_h=200});
    float numeral_width=0;
    for (const char* digit : {"0","1","2","3","4","5","6","7","8","9"}) {
        const float width=measure_text(str_cstr(digit),kFontSans,16).x;
        if (!numeral_width) numeral_width=width;
        check(std::fabs(width-numeral_width)<.01f,__LINE__);
    }
    check(!set_font_tabular_numbers(ctx,kFontSans,false),__LINE__);
    end_frame(ctx);
    check(set_font_tabular_numbers(ctx,kFontSans,false),__LINE__);
#if defined(_WIN32)
    check(is_font_loaded(ctx,kFontSemiBold),__LINE__);
#endif
#if defined(_WIN32)
    TextBuffer buffer={};
    check(init_text_buffer(&buffer,str_lit("iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii")),__LINE__);
    double scroll_max[2]={};
    for (int pass=0;pass<2;++pass) {
        check(load_font_file(ctx,pass==0?"C:/Windows/Fonts/arial.ttf":"C:/Windows/Fonts/consola.ttf",3)==3,__LINE__);
        for (int frame=0;frame<3;++frame) {
            begin_frame(ctx,{.viewport_w=400,.viewport_h=200});
            code_editor({.id=id("quality.wrap"),.buffer=&buffer,.width=Sizing::fixed(120),.height=Sizing::fixed(40),
                .font_size_px=16,.font_id=3,.show_line_numbers=false,.word_wrap=true,.tokenizer=nullptr});
            end_frame(ctx);
        }
        set_virtual_list_scroll_offset(id("quality.wrap"),1e9);
        scroll_max[pass]=virtual_list_scroll_offset(id("quality.wrap"));
    }
    check(scroll_max[1]>scroll_max[0]+20,__LINE__);
    free_text_buffer(&buffer);
#endif
    // Fonts supplied by an application also occupy the named weight slots.
    check(kFontMedium==ETCH_FONT_MEDIUM && kFontSemiBold==ETCH_FONT_SEMIBOLD,__LINE__);
    for (float dpi : {1.0f,1.5f,2.0f}) {
        begin_frame(ctx,{.viewport_w=400,.viewport_h=250,.dpi_scale=dpi});
        begin_box({.width=Sizing::fixed(100),.height=Sizing::fixed(100),
            .background=Color::rgb_hex(0x102030),.border_color=Color::white(),.border_width=1,
            .radius=Corners::all(24),.shadow_color=Color::rgb_hex(0x203040),.shadow_blur=12,
            .shadow_offset={0,12},.clip_children=true,.continuous_corners=true,.shadow_spread=-6});
        draw_rect({.rect={0,0,100,100},.fill=Color::rgb_hex(0x304050)});
        draw_text({.text=str_lit("600"),.font_id=kFontSemiBold});
        end_box();end_frame(ctx);
        bool shadow=false,child=false,border=false;
        for (const auto& p:frame_prims(ctx)) {
            if ((p.style.x&15u)==uint32_t(PrimKind::Shadow)) {
                shadow=true;check(p.bounds.x==6*dpi && p.bounds.y==18*dpi && p.bounds.z==88*dpi,__LINE__);
                check((p.style.x&(uint32_t(PrimFlag_Continuous)<<4))!=0,__LINE__);
            }
            if (p.style.y==Color::rgb_hex(0x304050).pack_rgba8()) {
                child=true;check((p.style.x&(1u<<19))!=0 && (p.style.x>>20)==uint32_t(24*dpi*4),__LINE__);
            }
            if (p.style.z==Color::white().pack_rgba8()) {
                border=true;check((p.style.x&(uint32_t(PrimFlag_Continuous)<<4))!=0,__LINE__);
            }
        }
        check(shadow && child && border,__LINE__);
    }
    const TableColumnDesc column={.title=str_lit("Header"),.sortable=false};
    uint32_t lines[2]={};float height[2]={};
    for (int pass=0;pass<2;++pass) {
        begin_frame(ctx,{.viewport_w=400,.viewport_h=200});
        begin_table({.id=id("quality.table"),.columns={&column,1},.width=Sizing::fixed(200),
            .height=Sizing::fixed(100),.show_column_lines=pass==0,.header_font_size_px=pass==0?12.0f:18.0f});
        end_table();end_frame(ctx);
        for (const auto& p:frame_prims(ctx)) {
            if ((p.style.x&15u)==uint32_t(PrimKind::BezierWire)) ++lines[pass];
            if ((p.style.x&15u)==uint32_t(PrimKind::SlugGlyph)) height[pass]=std::fmax(height[pass],p.bounds.w);
        }
    }
    check(lines[0]==lines[1]+1 && height[1]>height[0]*1.4f,__LINE__);
    check(etch_table_desc_default().show_column_lines && etch_table_desc_default().header_font_size_px==12,__LINE__);
    check(!etch_box_desc_default().continuous_corners && etch_box_desc_default().shadow_spread==0,__LINE__);
    destroy_context(ctx);set_current_context(nullptr);return ok;
}

inline bool verify_visual_quality_gpu() noexcept {
    using namespace etch;
    Context* ctx=create_context({.headless=true});
    if (!ctx) return false;
    begin_frame(ctx,{.viewport_w=460,.viewport_h=240});
    begin_box({.width=Sizing::fixed(460),.height=Sizing::fixed(240),.padding=Padding::all(10),.gap=20});
    for (bool continuous : {false,true}) {
        begin_box({.width=Sizing::fixed(80),.height=Sizing::fixed(80),.radius=Corners::all(32),
            .clip_children=true,.continuous_corners=continuous});
        draw_rect({.rect={0,0,80,80},.fill=Color::rgb_hex(0x163A2B)});
        end_box();
    }
    box({.width=Sizing::fixed(80),.height=Sizing::fixed(80),.background=Color::white(),
        .radius=Corners::all(24),.shadow_color=Color::rgba8(0,0,0,100),.shadow_blur=12,
        .shadow_offset={0,12},.continuous_corners=true,.shadow_spread=-6});
    box({.width=Sizing::fixed(80),.height=Sizing::fixed(80),.background=Color::rgb_hex(0x163A2B),
        .radius=Corners::all(32),.backdrop_blur=8,.continuous_corners=true});
    draw_text({.text=str_lit("Small dark text 0123456789"),.pos={10,115},.size=13,.color=Color::rgba8(0,0,0,255)});
    draw_text({.text=str_lit("Small dark text 0123456789"),.pos={10,145},.size=13,.color=Color::rgba8(0,0,0,255)});
    end_box();end_frame(ctx);
    // Compare identical Slug runs with and without the public primitive darkening flag.
    for (const auto& p:frame_prims(ctx)) if ((p.style.x&15u)==uint32_t(PrimKind::SlugGlyph) && p.bounds.y>140)
        const_cast<Prim&>(p).style.x &= ~(uint32_t(PrimFlag_StemDarken)<<4);
    const char* output="quality-surfaces.png";
    bool ok=render_headless_png(ctx,460,240,output,Color::white());
    SDL_Surface* surface=ok?SDL_LoadPNG(output):nullptr;
    if (surface) {
        Uint8 r=0,g=0,b=0,a=0;
        ok &= SDL_ReadSurfacePixel(surface,18,18,&r,&g,&b,&a) && r>240;
        ok &= SDL_ReadSurfacePixel(surface,118,18,&r,&g,&b,&a) && r<60;
        ok &= SDL_ReadSurfacePixel(surface,318,18,&r,&g,&b,&a) && r<60;
        Uint8 top=0,bottom=0;
        ok &= SDL_ReadSurfacePixel(surface,250,5,&top,&g,&b,&a);
        ok &= SDL_ReadSurfacePixel(surface,250,96,&bottom,&g,&b,&a) && bottom+8<top;
        uint32_t dark_ink=0,plain_ink=0;
        for (int y=112;y<138;++y) for (int x=10;x<220;++x) {
            SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a);dark_ink+=255-r;
            SDL_ReadSurfacePixel(surface,x,y+30,&r,&g,&b,&a);plain_ink+=255-r;
        }
        ok &= dark_ink>plain_ink;
        SDL_DestroySurface(surface);
    } else ok=false;
    // Retain this gallery in the out-of-source build directory for visual inspection.
    if (!ok) std::fprintf(stderr,"visual quality GPU checks failed\n");
    destroy_context(ctx);set_current_context(nullptr);return ok;
}
