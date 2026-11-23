#pragma once
#include "etch/etch.h"
#include "etch/etch_c.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>

inline bool verify_closure_controls() noexcept {
    using namespace etch;
    Context* ctx=create_context({.headless=true});
    if(!ctx) return false;
    bool ok=true;
    const auto check=[&](bool pass,int line) noexcept {if(!pass){std::fprintf(stderr,"closure line %d failed\n",line);ok=false;}};
    begin_frame(ctx,{.viewport_w=500,.viewport_h=300});
    begin_box({.axis=Axis::Col,.width=Sizing::fixed(200),.height=Sizing::fixed(100)});
    begin_box({.id=id("vertical.flex"),.axis=Axis::Col,.width=Sizing::grow(),.height=Sizing::grow(),.shrink_height=true});
    box({.height=Sizing::fixed(300)});end_box();
    box({.id=id("footer"),.height=Sizing::fixed(20)});end_box();end_frame(ctx);
    check(std::fabs(element_bounds(id("footer")).y-80)<.1f,__LINE__);
    check(std::fabs(element_bounds(id("vertical.flex")).h-80)<.1f,__LINE__);
    const auto scroll=[&](Vec2 pointer,bool down) noexcept {
        begin_frame(ctx,{.viewport_w=500,.viewport_h=300,.pointer_pos=pointer,.pointer_down=down});
        begin_box({.id=id("horizontal"),.width=Sizing::fixed(200),.height=Sizing::fixed(100),.scroll_x=true});
        box({.id=id("wide"),.width=Sizing::fixed(600),.height=Sizing::fixed(40)});end_box();end_frame(ctx);
    };
    scroll({10,90},false);scroll({30,90},true);scroll({170,90},true);scroll({170,90},false);
    check(scroll_offset(id("horizontal")).x>200,__LINE__);
    scroll({170,90},false);check(element_bounds(id("wide")).x<0,__LINE__);
    // Hidden scrollbars must leave the bottom content's pointer hit target intact.
    const uint32_t hidden=id("hidden.scroll"),target=id("hidden.target");
    const auto hidden_frame=[&](bool down) noexcept {
        begin_frame(ctx,{.viewport_w=500,.viewport_h=300,.pointer_pos={100,90},.pointer_down=down});
        begin_box({.id=hidden,.width=Sizing::fixed(200),.height=Sizing::fixed(100),.scroll_x=true,.show_scrollbar=false});
        box({.id=target,.width=Sizing::fixed(600),.height=Sizing::fixed(100)});end_box();end_frame(ctx);
    };
    hidden_frame(false);hidden_frame(true);check(pressed(target),__LINE__);hidden_frame(false);
    const uint32_t split=id("fine.split"),divider=id_index(split,0x53504C54u);
    const auto split_frame=[&](uint32_t key=0,Vec2 point=Vec2{NAN,NAN},bool down=false) noexcept {
        begin_frame(ctx,{.viewport_w=500,.viewport_h=300,.pointer_pos=point,.pointer_down=down,.key_pressed_code=key});
        begin_split({.id=split,.width=Sizing::fixed(300),.height=Sizing::fixed(100),.min_first_px=0,.min_second_px=0,.divider_size_px=1});
        box({.id=id("fine.first"),.width=Sizing::grow(),.height=Sizing::grow()});split_second();end_split();end_frame(ctx);
    };
    split_frame();check(std::fabs(element_bounds(divider).w-8)<.1f,__LINE__);
    split_frame(0,{147,40},true);split_frame(0,{147,40},false);
    check(focused(divider),__LINE__);const float ratio=split_ratio(split);split_frame(SDLK_RIGHT);
    check(split_ratio(split)>ratio,__LINE__);
    check(perform_a11y_action(divider,A11yAction::SetValue,.7f,{},ctx),__LINE__);
    split_frame();check(std::fabs(split_ratio(split)-.7f)<.001f,__LINE__);
    const A11yNode* divider_node=find_a11y_node(divider,ctx);
    check(divider_node && std::fabs(divider_node->value_num-.7f)<.001f,__LINE__);
    static Str items[10000];for(auto& item:items)item=str_lit("Option");
    int selected=9000;const uint32_t combo=id("large.combo"),pop=id_index(combo,0x2304),list=id_index(pop,0xC011u);
    const auto combo_frame=[&](uint32_t key=0) noexcept {
        begin_frame(ctx,{.viewport_w=500,.viewport_h=400,.key_pressed_code=key});
        combo_box({.id=combo,.selected_index=&selected,.items=items});end_frame(ctx);
    };
    combo_frame();request_focus(combo);combo_frame(SDLK_RETURN);combo_frame();
    check(is_popup_open(pop),__LINE__);check(virtual_list_scroll_offset(list)>200000,__LINE__);
    check(frame_stats(ctx).slug_glyph_count<300,__LINE__);
    combo_frame(SDLK_END);check(virtual_list_scroll_offset(list)>270000,__LINE__);
    combo_frame(SDLK_RETURN);check(selected==9999 && !is_popup_open(pop),__LINE__);
    const TableColumnDesc cols[]={{.title=str_lit("A"),.width=220},{.title=str_lit("B"),.width=220}};
    uint32_t focus=UINT32_MAX,anchor=UINT32_MAX;uint64_t bits[2]={};
    const TableSelection selection={&focus,&anchor,bits,2};
    const uint32_t table=id("keys.table");
    const auto table_frame=[&](uint32_t key=0,uint16_t mods=0) noexcept {
        begin_frame(ctx,{.viewport_w=500,.viewport_h=300,.key_mods=mods,.key_pressed_code=key});
        begin_table({.id=table,.columns=cols,.item_count=100,.width=Sizing::fixed(260),.height=Sizing::fixed(180),.selection=selection});
        while(table_next_row())while(table_next_column())text("data");
        end_table();end_frame(ctx);
    };
    table_frame();check(table_column_width(table,0)>=219,__LINE__);
    check(perform_a11y_action(table,A11yAction::Focus,0,{},ctx),__LINE__);table_frame(SDLK_DOWN);check(focus==0 && table_row_selected(selection,0),__LINE__);
    table_frame(SDLK_DOWN,Mod_Shift);check(focus==1 && table_row_selected(selection,0) && table_row_selected(selection,1),__LINE__);
    table_frame(SDLK_END);check(focus==99 && virtual_list_scroll_offset(table)>2000,__LINE__);
    table_frame(SDLK_A,Mod_Primary);check(bits[0]==UINT64_MAX && bits[1]==((uint64_t(1)<<36)-1),__LINE__);
    // A horizontal wheel over the header moves the same viewport as all body rows.
    const uint32_t rid=table_row_result(table,96).id;
    table_frame();
    const Rect before=element_bounds(rid);
    begin_frame(ctx,{.viewport_w=500,.viewport_h=300,.pointer_pos={100,10},.scroll_delta={-2,0}});
    begin_table({.id=table,.columns=cols,.item_count=100,.width=Sizing::fixed(260),.height=Sizing::fixed(180),.selection=selection});
    while(table_next_row())while(table_next_column())text("data");
    end_table();end_frame(ctx);
    check(element_bounds(rid).x<before.x-50,__LINE__);
    // The multi-selection bitmap is authoritative when legacy focus state is also supplied.
    uint32_t legacy=0;uint64_t mixed_bits=0;
    const Color selection_color=Color::rgb_hex(0x37A5D1);
    for(uint32_t pass=0;pass<2;++pass) {
        mixed_bits=pass;
        begin_frame(ctx,{.viewport_w=500,.viewport_h=300});
        begin_table({.id=id("mixed.selection"),.columns=cols,.item_count=1,.width=Sizing::fixed(260),.height=Sizing::fixed(180),
            .selected_row=&legacy,.selected_row_color=selection_color,.selection={nullptr,nullptr,&mixed_bits,1}});
        while(table_next_row())while(table_next_column())text("data");
        end_table();end_frame(ctx);
        bool painted=false;
        for(const auto& prim:frame_prims(ctx)) painted |= prim.style.y==selection_color.pack_rgba8();
        check(painted==(pass==1),__LINE__);
    }
    // Invalid negative gaps are normalized once at the public table boundary.
    begin_frame(ctx,{.viewport_w=500,.viewport_h=300});
    begin_table({.id=table,.columns=cols,.item_count=100,.row_gap_px=-26,.width=Sizing::fixed(260),.height=Sizing::fixed(180)});end_table();end_frame(ctx);
    check(kMaxFontSlots==64 && kMaxVectorIcons==1024,__LINE__);
    const Str triangle=str_lit("M0 0 L10 0 L5 10 Z");
    for(uint16_t i=0;i<300;++i) check(register_svg_icon(ctx,triangle,{0,0,10,10})!=kInvalidIcon,__LINE__);
    check(register_svg_icon(ctx,triangle,{0,0,10,10},1023)==1023,__LINE__);
#if defined(_WIN32)
    check(load_font_file(ctx,"C:/Windows/Fonts/segoeui.ttf",63)==63,__LINE__);
#endif
    check(etch_box_desc_default().shrink_height==false && etch_split_desc_default().divider_hit_px==8,__LINE__);
    destroy_context(ctx);set_current_context(nullptr);return ok;
}

inline bool verify_closure_gpu() noexcept {
    using namespace etch;
    Context* ctx=create_context({.headless=true});if(!ctx)return false;
    LoadedTexture textures[9]={};
    bool ok=true;
    for(uint32_t i=0;i<9;++i) {const uint8_t pixel[]={uint8_t(20+i*20),40,180,255};textures[i]=create_texture_rgba(ctx,{.rgba_pixels=pixel,.width=1,.height=1});ok &= textures[i].texture!=nullptr;}
    const uint8_t cyan[]={0,200,180,255},magenta[]={220,0,180,255};
    ok &= update_texture_rgba(ctx,textures[0].texture,1,1,cyan);
    ok &= update_texture_rgba(ctx,textures[0].texture,1,1,magenta);
    uint32_t dcs[2]={};
    for(uint32_t count:{8u,9u}) {
        begin_frame(ctx,{.viewport_w=410,.viewport_h=190});
        begin_box({.width=Sizing::fixed(410),.height=Sizing::fixed(190)});
        for(uint32_t i=0;i<count;++i) {
            draw_rect({.rect={float(10+i*32),8,24,24},.fill=Color::rgb_hex(0x809090)});
            draw_image({.texture=textures[i].texture,.rect={float(10+i*32),8,24,24}});
        }
        begin_box({.width=Sizing::fixed(80),.height=Sizing::fixed(80),.radius={32,0,24,8},.clip_children=true,
            .attach_to=AttachTo::Parent,.anchor_parent=Anchor::TopLeft,.floating_offset={10,60}});
        begin_box({.width=Sizing::fixed(80),.height=Sizing::fixed(80),.clip_children=true});
        draw_rect({.rect={0,0,80,80},.fill=Color::rgb_hex(0x008000)});end_box();end_box();
        for(uint32_t i=0;i<3;++i) draw_rect({.rect={230.f+55*i,70,40,30},.fill=Color::rgb_hex(0x00A000),
            .border_color=Color::black(),.border_width=4,.border_align=static_cast<BorderAlign>(i)});
        end_box();end_frame(ctx);
        ok &= render_headless_png(ctx,410,190,"closure-surfaces.png",Color::white());
        dcs[count==8?0:1]=frame_stats(ctx).draw_call_count;
        if(count==9) ok &= dcs[0]>0 && dcs[0]<=5u && dcs[1]==dcs[0]+1u;
        SDL_Surface* surface=SDL_LoadPNG("closure-surfaces.png");
        if(!surface) {ok=false;continue;}
        const auto pixel=[&](int x,int y,int channel) noexcept {Uint8 r=0,g=0,b=0,a=0;if(!SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a))return -1;return int(channel==0?r:channel==1?g:b);};
        ok &= pixel(20,20,0)>200 && pixel(20,20,1)<10;
        ok &= pixel(12,62,0)>240 && pixel(88,62,0)<10 && pixel(88,138,0)>240;
        ok &= pixel(228,80,0)>240 && pixel(284,80,0)<10 && pixel(337,80,0)<10;
        SDL_DestroySurface(surface);
        std::remove("closure-surfaces.png");
    }
    ok &= flush_texture_uploads(ctx);
    for(auto& texture:textures)destroy_texture(ctx,&texture);
    if(!ok)std::fprintf(stderr,"closure GPU checks failed\n");
    destroy_context(ctx);set_current_context(nullptr);return ok;
}
