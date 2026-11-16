#pragma once
#include "etch/etch.h"
#include "etch/etch_c.h"
#include "etch_testing.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstring>

inline bool verify_workbench_checks() noexcept {
    using namespace etch;
    Context* ctx=create_context({.headless=true});
    if (!ctx) return false;
    bool ok=true;
    const auto check=[&](bool pass,int line) noexcept {
        if (!pass) { ok=false;std::fprintf(stderr,"workbench check line %d failed\n",line); }
    };
    const TableColumnDesc columns[]={
        {.title=str_lit("File name"),.sizing=TableColumnSizing::Stretch,.width=2,.min_width=20},
        {.title=str_lit("Size"),.sizing=TableColumnSizing::Stretch,.width=1,.min_width=20}
    };
    const uint32_t tid=id("workbench.table");uint32_t selected=UINT32_MAX;
    TableRowResult row={};
    const auto table_frame=[&](float width,Vec2 point=Vec2{-1,-1},bool down=false,bool right=false,uint8_t clicks=1) noexcept {
        begin_frame(ctx,{.viewport_w=700,.viewport_h=350,.pointer_pos=point,.pointer_down=down,.right_pointer_down=right,.pointer_clicks=clicks});
        begin_box({.width=Sizing::fixed(width),.height=Sizing::fixed(200)});
        begin_table({.id=tid,.columns=columns,.item_count=10,.width=Sizing::grow(),.height=Sizing::grow(),
            .show_scrollbar=false,.selected_row=&selected});
        uint32_t index=0;
        while (table_next_row(&index,Color::rgb_hex(0x203040))) {
            if (table_next_column()) text("A multiword filename that needs a real ellipsis",{.wrap=false,.ellipsis=true});
            if (table_next_column()) text("1234",{.wrap=false});
        }
        row=table_row_result(tid,0);end_table();end_box();end_frame(ctx);
    };
    table_frame(240);
    check(std::fabs(table_column_width(tid,0)-160)<1 && std::fabs(table_column_width(tid,1)-80)<1,__LINE__);
    const auto rid=table_row_result(tid,0).id;
    const Rect rb=element_bounds(rid);
    check(rb.h>20 && rb.w<=240.1f,__LINE__);
    table_frame(240);
    const Vec2 hit={rb.x+25,rb.y+rb.h*.5f};
    table_frame(240,hit);
    check(row.hovered,__LINE__);
    bool hover_color=false;
    for (const auto& p:frame_prims(ctx)) if ((p.style.x&15u)==uint32_t(PrimKind::Rect) && std::fabs(p.bounds.y-rb.y)<.1f)
        hover_color |= p.style.y!=Color::rgb_hex(0x203040).pack_rgba8();
    check(hover_color,__LINE__);
    table_frame(240,hit,true);check(row.pressed,__LINE__);
    table_frame(240,hit);check(row.clicked && selected==0,__LINE__);
    table_frame(240,hit,true,false,2);check(row.double_clicked,__LINE__);
    table_frame(240,hit);table_frame(240,hit,false,true);table_frame(240,hit);
    check(row.right_clicked,__LINE__);
    table_frame(390);check(std::fabs(table_column_width(tid,0)-260)<1 && std::fabs(table_column_width(tid,1)-130)<1,__LINE__);

    // A post-layout drawing callback observes the width on the very first frame and on resize.
    struct Capture { Rect local; uint32_t calls; } capture={};
    const auto paint=[](Rect local,void* ptr) noexcept {
        auto* c=static_cast<Capture*>(ptr);c->local=local;++c->calls;
        draw_rect({.rect={local.w-8,0,8,local.h},.fill=Color::rgb_hex(0xE01020)});
    };
    for (float width:{180.0f,290.0f}) {
        begin_frame(ctx,{.viewport_w=500,.viewport_h=200});
        begin_box({.width=Sizing::fixed(width),.height=Sizing::fixed(60)});
        draw_after_layout(paint,&capture);end_box();end_frame(ctx);
        check(capture.local.w==width && capture.local.h==60,__LINE__);
        bool fill=false;
        for (const auto& p:frame_prims(ctx)) if (p.style.y==Color::rgb_hex(0xE01020).pack_rgba8()) fill=p.bounds.x==width-8;
        check(fill,__LINE__);
    }
    check(capture.calls==2,__LINE__);

    // Whole-line metrics avoid false ellipsis when the measured label fits exactly.
    const Str phrase=str_lit("AV To file name office 123");
    float exact_width=0;
    begin_frame(ctx,{.viewport_w=500,.viewport_h=200});exact_width=measure_text(phrase,kFontSans,14).x;end_frame(ctx);
    uint32_t full_glyphs=0,elided_glyphs=0;
    for (int pass=0;pass<2;++pass) {
        begin_frame(ctx,{.viewport_w=500,.viewport_h=200,.pixel_snap=false});
        begin_box({.width=Sizing::fixed(exact_width+.01f),.height=Sizing::fixed(30),.clip_children=true});
        text(phrase,{.font_size_px=14,.wrap=false,.ellipsis=pass==1});end_box();end_frame(ctx);
        for (const auto& p:frame_prims(ctx)) if ((p.style.x&15u)==uint32_t(PrimKind::SlugGlyph)) { if(pass)++elided_glyphs;else++full_glyphs; }
    }
    check(full_glyphs>10 && full_glyphs==elided_glyphs,__LINE__);
    begin_frame(ctx,{.viewport_w=500,.viewport_h=200});
    begin_box({.width=Sizing::fixed(80),.height=Sizing::fixed(30),.clip_children=true});
    text(phrase,{.wrap=false,.ellipsis=true});end_box();end_frame(ctx);
    float right=0;
    for (const auto& p:frame_prims(ctx)) if ((p.style.x&15u)==uint32_t(PrimKind::SlugGlyph)) right=std::fmax(right,p.bounds.x+p.bounds.z);
    check(right<81,__LINE__);

    begin_frame(ctx,{.viewport_w=400,.viewport_h=300});
    begin_box({.axis=Axis::Col,.width=Sizing::fixed(80)});
    text(phrase,{.wrap=false});text(phrase,{.wrap=true,.id=id("wrap.same")});end_box();end_frame(ctx);
    const auto* wrapped=find_a11y_node(id("wrap.same"));
    check(wrapped && wrapped->bounds.h>25,__LINE__);
    // Semantic foreground tokens and pressed feedback preserve variant colors.
    Theme custom=Theme::dark();custom.on_primary=Color::rgb_hex(0x010203);custom.on_error=Color::rgb_hex(0x040506);
    set_theme(custom,ctx);const uint32_t bid=id("workbench.button");
    uint32_t bg[3]={};
    for (int pass=0;pass<3;++pass) {
        begin_frame(ctx,{.viewport_w=400,.viewport_h=100,.dt_seconds=1.f/60,.pointer_pos=pass?Vec2{50,16}:Vec2{-1,-1},.pointer_down=pass==2});
        button({.id=bid,.label=str_lit("Center"),.variant=ButtonVariant::Primary,.width=Sizing::fixed(160)});end_frame(ctx);
        bool foreground=false;float first=1000,last=0;
        for (const auto& p:frame_prims(ctx)) {
            if ((p.style.x&15u)==uint32_t(PrimKind::Rect) && p.bounds.z==160) bg[pass]=p.style.y;
            if ((p.style.x&15u)==uint32_t(PrimKind::SlugGlyph)) { foreground |= p.style.y==custom.on_primary.pack_rgba8();first=std::fmin(first,p.bounds.x);last=std::fmax(last,p.bounds.x+p.bounds.z); }
        }
        check(foreground && std::fabs((first+last)*.5f-80)<3,__LINE__);
    }
    check(bg[0]!=bg[1] && bg[1]!=bg[2],__LINE__);
    begin_frame(ctx,{.viewport_w=400,.viewport_h=100});end_frame(ctx);

    char input[64]="hello 世界";const uint32_t iid=id("workbench.input");TextInputResult ir={};
    const auto input_frame=[&](uint32_t key=0,uint16_t mods=0,Vec2 point=Vec2{-1,-1},bool down=false,bool right=false) noexcept {
        begin_frame(ctx,{.viewport_w=400,.viewport_h=200,.pointer_pos=point,.pointer_down=down,.right_pointer_down=right,.key_mods=mods,.key_pressed_code=key});
        ir=text_input({.id=iid,.buffer=input,.capacity=sizeof(input),.width=Sizing::fixed(200),.clear_button=true});end_frame(ctx);
    };
    input_frame();request_focus(iid);set_text_selection(iid,0,5);
    input_frame(SDLK_ESCAPE);check(ir.focused && ir.cursor_byte==ir.selection_anchor_byte,__LINE__);
    input_frame(SDLK_ESCAPE);check(!ir.focused,__LINE__);
    request_focus(iid);set_text_selection(iid,0,5);
    input_frame(0,0,{20,18},false,true);input_frame(0,0,{20,18});
    const uint32_t popup=id_index(iid,0xED1700u);
    check(is_popup_open(popup),__LINE__);
    const Rect copy=element_bounds(id_index(popup,2));check(copy.w>0,__LINE__);
    input_frame(0,0,{copy.x+12,copy.y+12},true);input_frame(0,0,{copy.x+12,copy.y+12});
    check(!is_popup_open(popup) && ir.focused,__LINE__);
    const Rect clear=element_bounds(id_index(iid,0xED1701u));check(clear.w==20,__LINE__);
    begin_frame(ctx,{.viewport_w=400,.viewport_h=200});const Str clipboard=get_clipboard_text();check(clipboard.length==5 && std::memcmp(clipboard.chars,"hello",5)==0,__LINE__);end_frame(ctx);
    input_frame();
    input_frame(0,0,{clear.x+10,clear.y+10},true);input_frame(0,0,{clear.x+10,clear.y+10});
    check(ir.changed && input[0]==0,__LINE__);
    input_frame(SDLK_Z,Mod_Ctrl);check(std::strcmp(input,"hello 世界")==0,__LINE__);
    check(Color::blend_over({},Color::white(26)).pack_rgba8()==Color::white(26).pack_rgba8(),__LINE__);
    check(Theme::high_contrast().on_primary.r<32 && Theme::industrial().on_primary.r<32,__LINE__);
    const auto luma=[](Color c) noexcept {const auto linear=[](uint8_t v) noexcept {float x=v/255.0f;return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);};return .2126f*linear(c.r)+.7152f*linear(c.g)+.0722f*linear(c.b);};
    for(Theme thm:{Theme::high_contrast(),Theme::industrial()}) check((luma(thm.accent_primary)+.05f)/(luma(thm.on_primary)+.05f)>=4.5f,__LINE__);
    // Read-only buffers need not reserve a terminator byte, and must never be written.
    const char raw[]={'A','V','X'};
    begin_frame(ctx,{.viewport_w=400,.viewport_h=200});
    const auto ro=text_input({.id=id("readonly"),.buffer=const_cast<char*>(raw),.capacity=3,.read_only=true});
    end_frame(ctx);check(ro.byte_length==3 && raw[2]=='X',__LINE__);
    // Captured motion can cross both negative client axes.
    begin_frame(ctx,{.viewport_w=400,.viewport_h=200,.pointer_pos={8,9}});end_frame(ctx);
    begin_frame(ctx,{.viewport_w=400,.viewport_h=200,.pointer_pos={-12,-21},.pointer_down=true});
    check(pointer_delta().x==-20 && pointer_delta().y==-30,__LINE__);end_frame(ctx);
    begin_frame(ctx,{.viewport_w=400,.viewport_h=200});end_frame(ctx);
    // Independent popups survive closing an earlier sibling; negative anchors remain explicit.
    begin_frame(ctx,{.viewport_w=400,.viewport_h=200});open_popup(id("one"),{-20,15});open_popup(id("two"),{40,50});
    close_popup(id("one"));check(is_popup_open(id("two")),__LINE__);close_popup();end_frame(ctx);
    // Large drop batches retain long UTF-8 payloads beyond the former fixed limits.
    char path[3000];std::memset(path,'x',sizeof(path));path[0]='/';
    for(uint32_t i=0;i<40;++i) inject_drop_file(ctx,{path,sizeof(path)});
    begin_frame(ctx,{.viewport_w=400,.viewport_h=200});
    const auto files=dropped_files();check(files.size()==40 && files[39].length==sizeof(path) && files[39].chars[2999]=='x',__LINE__);
    end_frame(ctx);
    // Mouse wheel scrolls multiline content without forcing the caret back into view.
    char lines[256]="one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten";
    const auto multi=[&](float wheel) noexcept {
        begin_frame(ctx,{.viewport_w=400,.viewport_h=200,.pointer_pos={20,20},.scroll_delta={0,wheel}});
        text_input({.id=id("multiline"),.buffer=lines,.capacity=sizeof(lines),.width=Sizing::fixed(200),.height=Sizing::fixed(60),.multiline=true});end_frame(ctx);
        float first=1000;for(const auto& p:frame_prims(ctx)) if((p.style.x&15u)==uint32_t(PrimKind::SlugGlyph))first=std::fmin(first,p.bounds.y);return first;
    };
    multi(0);const float y0=multi(0),y1=multi(-1);check(y1<y0,__LINE__);
    // A split uses the same available-space formula on first layout and after resize.
    for(float width:{300.0f,420.0f}) {
        begin_frame(ctx,{.viewport_w=500,.viewport_h=300});
        begin_split({.id=id("first.split"),.width=Sizing::fixed(width),.height=Sizing::fixed(120),.default_ratio=.4f,.min_first_px=0,.min_second_px=0,.divider_size_px=8});
        box({.id=id("split.first"),.width=Sizing::grow(),.height=Sizing::grow()});split_second();end_split();end_frame(ctx);
        check(std::fabs(element_bounds(id("split.first")).w-(width-8)*.4f)<.1f,__LINE__);
    }
    // Trigger attachment resolves popup position and width in the current layout pass.
    begin_frame(ctx,{.viewport_w=500,.viewport_h=300});
    box({.id=id("trigger"),.width=Sizing::fixed(180),.height=Sizing::fixed(32)});
    open_popup(id("attached"),{1,1});
    if(begin_popup({.id=id("attached"),.trigger_id=id("trigger"),.attach_to_trigger=true,.match_trigger_width=true})) {text("Entry");end_popup();}
    end_frame(ctx);const Rect attached=element_bounds(id("attached"));
    check(std::fabs(attached.w-180)<.1f && std::fabs(attached.y-32)<.1f,__LINE__);close_popup();
    bool modal_open=true;
    const auto modal_frame=[&](uint32_t key=0,Vec2 pointer=Vec2{-1,-1},bool down=false) noexcept {
        begin_frame(ctx,{.viewport_w=500,.viewport_h=300,.pointer_pos=pointer,.pointer_down=down,.key_pressed_code=key});
        if(begin_modal({.id=id("modal"),.open=&modal_open,.width_px=200,.height=Sizing::fixed(100),.dismiss_on_backdrop=true})) {
            if(begin_popup({.id=id("modal.popup"),.width=Sizing::fixed(150),.height=Sizing::fixed(80)})) {text("Choice");end_popup();}
            end_modal();
        }
        end_frame(ctx);
    };
    modal_frame();open_popup(id("modal.popup"),{190,220});modal_frame();
    modal_frame(0,{220,270},true);check(modal_open && is_popup_open(id("modal.popup")),__LINE__);
    modal_frame();modal_frame(SDLK_ESCAPE);check(modal_open && !is_popup_open(id("modal.popup")),__LINE__);
    modal_frame(SDLK_ESCAPE);check(!modal_open,__LINE__);
    char source[5401];for(uint32_t i=0;i<600;++i)std::memcpy(source+i*9,"return 1;",9);source[5400]=0;
    SyntaxToken long_tokens[1200];
    const uint32_t token_count=tokenize_cpp_hlsl(0,{source,5400},long_tokens,1200,nullptr);
    check(token_count==1200 && long_tokens[1199].byte_end>5300,__LINE__);
    EtchSyntaxToken c_tokens[1200];
    check(etch_tokenize_cpp_hlsl(0,{source,5400,false},c_tokens,1200,nullptr)==1200 && c_tokens[1199].byte_end>5300,__LINE__);
    TextBuffer document={};check(init_text_buffer(&document,str_lit("float x = 1;")),__LINE__);
    for(bool wrap:{false,true}) {
        begin_frame(ctx,{.viewport_w=400,.viewport_h=200});
        code_editor({.id=id("token.boundary"),.buffer=&document,.width=Sizing::fixed(120),.height=Sizing::fixed(60),.word_wrap=wrap,
            .tokenizer=[](uint32_t,Str line,SyntaxToken* out,uint32_t cap,void*) noexcept -> uint32_t {
                for(uint32_t i=0;i<cap;++i)out[i]={0,line.length,Color::white(),kFontMono};return UINT32_MAX;
            }});end_frame(ctx);
        check(frame_stats(ctx).slug_glyph_count>0,__LINE__);
    }
    free_text_buffer(&document);
    check(verify_text_undo_ring(),__LINE__);
    destroy_context(ctx);set_current_context(nullptr);return ok;
}
