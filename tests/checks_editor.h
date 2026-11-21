// Shared sandbox [21] / Studio [S17] document and editor regression.
#include <cstdio>
#include <cstdlib>
#include <cstring>

static bool verify_editor_checks(etch::Context* restore) noexcept {
    using namespace etch;
    bool ok=true;
    const auto check=[&](bool pass,int line) noexcept {if(!pass){ok=false;std::fprintf(stderr,"v24 check line %d failed\n",line);}};
    TextBuffer b={};
    constexpr uint32_t lines=1000000,stride=32;
    char* source=static_cast<char*>(std::malloc(size_t(lines)*stride));if(!source)return false;
    for(uint32_t i=0;i<lines;++i)std::memcpy(source+size_t(i)*stride,"float4 color = float4(1, 0, 0, 1);",stride);
    for(uint32_t i=0;i<lines;++i)source[size_t(i)*stride+stride-1]='\n';
    check(init_text_buffer(&b,{source,lines*stride-1}),__LINE__);std::free(source);if(!b.data)return false;
    check(text_buffer_line_count(&b)==lines && text_buffer_line_start(&b,999985)==999985*stride,__LINE__);
    const uint32_t at=999985*stride+5;
    check(text_buffer_replace(&b,at,at,str_lit("x")),__LINE__);
    const uint32_t suffix=b.line_gap_end;const uint32_t encoded=b.line_data[suffix];
    for(uint32_t i=0;i<1000;++i)check(text_buffer_replace(&b,at+1+i,at+1+i,str_lit("a"),true,0,0,0,0,1,double(i)*.001),__LINE__);
    check(b.line_gap_end==suffix && b.line_data[suffix]==encoded,__LINE__);
    check(text_buffer_line_start(&b,999986)==999986*stride+1001,__LINE__);
    check(text_buffer_undo(&b) && text_buffer_redo(&b),__LINE__);
    check(text_buffer_replace(&b,at,at+1001,{}),__LINE__);
    check(text_buffer_line_start(&b,999986)==999986*stride,__LINE__);
    check(text_buffer_replace(&b,at,at,str_lit("\r\n")),__LINE__);
    check(text_buffer_line_count(&b)==lines+1 && text_buffer_line_start(&b,999986)==at+1,__LINE__);
    check(text_buffer_undo(&b) && text_buffer_line_count(&b)==lines,__LINE__);
    Context* test=create_context({.headless=true});if(!test){free_text_buffer(&b);return false;}set_current_context(test);
    const uint32_t eid=id("v24_editor"); CodeEditorResult result={};
    const auto step=[&](uint32_t key=0,uint16_t mods=0,Vec2 pointer=Vec2{-1,-1},bool down=false,uint8_t clicks=1,bool mutate=false) noexcept {
        begin_frame(test,{.viewport_w=800,.viewport_h=500,.dt_seconds=1.f/60,.pointer_pos=pointer,.pointer_down=down,.pointer_clicks=clicks,.key_mods=mods,.key_pressed_code=key});
        result=code_editor({.id=eid,.buffer=&b,.width=Sizing::fixed(800),.height=Sizing::fixed(500)});
        if(mutate)text_buffer_set(&b,str_lit("mutated after snapshot"));
        end_frame(test);
    };
    step();code_editor_scroll_to_line(eid,999985,AlignY::Top);step();
    check(result.first_visible_line<=999985 && result.first_visible_line>999970 && result.end_visible_line==1000000 && result.end_visible_line-result.first_visible_line<30,__LINE__);
    check(frame_stats(test).arena_used_bytes<1024u*1024u,__LINE__);
    check(render_headless_png(test,800,500,"etch_editor_check.png"),__LINE__);
    std::remove("etch_editor_check.png");
    // Gap-straddling line copies use only that line's bytes.
    check(text_buffer_replace(&b,at,at,{}),__LINE__);
    begin_frame(test,{.viewport_w=800,.viewport_h=500});const Str crossed=text_buffer_line(&b,999985);check(crossed.length==31 && crossed.chars,__LINE__);end_frame(test);
    // Deletion can form CRLF; history must restore those bytes verbatim.
    TextBuffer crlf={};check(init_text_buffer(&crlf,str_lit("\rX\n")),__LINE__);
    check(text_buffer_replace(&crlf,1,2,{}),__LINE__);
    check(text_buffer_replace(&crlf,0,2,{}),__LINE__);
    char crlf_bytes[2]={};
    check(text_buffer_undo(&crlf) && text_buffer_byte_length(&crlf)==2,__LINE__);
    check(text_buffer_copy_range(&crlf,0,2,crlf_bytes,sizeof(crlf_bytes))==2 && crlf_bytes[0]=='\r' && crlf_bytes[1]=='\n',__LINE__);
    check(text_buffer_redo(&crlf) && text_buffer_byte_length(&crlf)==0,__LINE__);
    check(text_buffer_undo(&crlf) && text_buffer_byte_length(&crlf)==2,__LINE__);
    check(text_buffer_copy_range(&crlf,0,2,crlf_bytes,sizeof(crlf_bytes))==2 && crlf_bytes[0]=='\r' && crlf_bytes[1]=='\n',__LINE__);
    check(text_buffer_line_count(&crlf)==2 && text_buffer_line_start(&crlf,1)==2,__LINE__);
    free_text_buffer(&crlf);
    // Unmounted editors must release their frame's document backing.
    const uint32_t temp_id=id("v24_temporary_editor");
    {
        TextBuffer temp={};check(init_text_buffer(&temp,str_lit("temporary")),__LINE__);
        code_editor_set_cursor(temp_id,0,4);
        begin_frame(test,{.viewport_w=800,.viewport_h=500});
        code_editor({.id=temp_id,.buffer=&temp,.width=Sizing::fixed(800),.height=Sizing::fixed(500)});
        end_frame(test);
        check(has_text_selection(temp_id),__LINE__);
        free_text_buffer(&temp);
    }
    step();
    check(!has_text_selection(temp_id) && selected_text(temp_id).length==0,__LINE__);
    // Compare hundreds of edits with a small independent contiguous reference.
    TextBuffer small={};check(init_text_buffer(&small),__LINE__);char model[4096]={};uint32_t used=0,seed=7;
    for(uint32_t i=0;i<1000;++i){seed=seed*1664525u+1013904223u;uint32_t a=used?seed%(used+1):0;seed=seed*1664525u+1013904223u;uint32_t z=a+(used-a?seed%(used-a+1):0);const char ch=i%3?'a':'\n';
        check(text_buffer_replace(&small,a,z,{&ch,1}),__LINE__);std::memmove(model+a+1,model+z,used-z);model[a]=ch;used=used-(z-a)+1;
        char actual[4096];check(text_buffer_copy_range(&small,0,used,actual,sizeof(actual))==used && !std::memcmp(actual,model,used),__LINE__);
        uint32_t li=0;check(text_buffer_line_start(&small,0)==0,__LINE__);for(uint32_t p=0;p<used;++p)if(model[p]=='\n'){++li;check(text_buffer_line_start(&small,li)==p+1,__LINE__);}check(text_buffer_line_count(&small)==li+1,__LINE__);
    }
    check(text_buffer_set(&small,{}),__LINE__);
    for(uint32_t i=0;i<1500;++i)check(text_buffer_replace(&small,i,i,str_lit("x")),__LINE__);
    uint32_t undos=0;while(text_buffer_undo(&small))++undos;
    check(undos==1024 && text_buffer_byte_length(&small)==476,__LINE__);
    while(text_buffer_redo(&small)){}check(text_buffer_byte_length(&small)==1500,__LINE__);
    check(text_buffer_undo(&small) && text_buffer_replace(&small,0,0,str_lit("y")) && !text_buffer_redo(&small),__LINE__);
    char* chunk=static_cast<char*>(std::malloc(65536));
    if(chunk){std::memset(chunk,'b',65536);check(text_buffer_set(&small,{}),__LINE__);
        for(uint32_t i=0;i<70;++i)check(text_buffer_replace(&small,0,text_buffer_byte_length(&small),{chunk,65536}),__LINE__);
        undos=0;while(text_buffer_undo(&small))++undos;check(undos==32,__LINE__);
        while(text_buffer_redo(&small)){}check(text_buffer_byte_length(&small)==65536,__LINE__);std::free(chunk);
    }else check(false,__LINE__);
    // Leave a short suffix at cap after deleting a large, many-line prefix.
    TextBuffer boundary={};
    char* boundary_source=static_cast<char*>(std::malloc(65536));
    if(boundary_source){
        for(uint32_t i=0;i<65536;++i)boundary_source[i]=i%2?'\n':'x';
        constexpr Str expected=str_lit("head\nsuffix\nlast");
        std::memcpy(boundary_source+65536-expected.length,expected.chars,expected.length);
        check(init_text_buffer(&boundary,{boundary_source,65536},131072),__LINE__);
        if(boundary.data){
            check(text_buffer_replace(&boundary,0,65536-expected.length,{},false),__LINE__);
            // Move right and left to exercise both byte and line gap helpers.
            check(text_buffer_replace(&boundary,expected.length,expected.length,{},false),__LINE__);
            check(text_buffer_replace(&boundary,2,2,{},false),__LINE__);
            check(boundary.gap_end-boundary.gap_start>100000 && boundary.cap-boundary.gap_end==expected.length-2,__LINE__);
            char actual[16];
            check(text_buffer_copy_range(&boundary,0,expected.length,actual,sizeof(actual))==expected.length && !std::memcmp(actual,expected.chars,expected.length),__LINE__);
            char tail[4];
            check(text_buffer_copy_range(&boundary,12,16,tail,sizeof(tail))==4 && !std::memcmp(tail,"last",4),__LINE__);
            begin_frame(test,{.viewport_w=800,.viewport_h=500});
            const Str first=text_buffer_line(&boundary,0),middle=text_buffer_line(&boundary,1),last=text_buffer_line(&boundary,2);
            check(first.length==4 && !std::memcmp(first.chars,"head",4) && middle.length==6 && !std::memcmp(middle.chars,"suffix",6) && last.length==4 && !std::memcmp(last.chars,"last",4),__LINE__);
            end_frame(test);
            free_text_buffer(&boundary);
        }
        std::free(boundary_source);
    }else check(false,__LINE__);
    free_text_buffer(&small);
    // Long-line clicks and vertical motion must use bounded shaping scratch.
    char long_lines[8001];
    std::memset(long_lines,'a',4000);long_lines[4000]='\n';std::memset(long_lines+4001,'b',4000);
    check(text_buffer_set(&b,{long_lines,sizeof(long_lines)}),__LINE__);
    code_editor_scroll_to_line(eid,0,AlignY::Top);code_editor_set_cursor(eid,0,0);step();request_focus(eid);step();
    step(0,0,{400.0f,18.0f},true);
    check(result.cursor_line==0 && result.cursor_byte>10 && result.cursor_byte<200,__LINE__);
    check(frame_stats(test).arena_used_bytes<512u*1024u,__LINE__);
    const uint32_t clicked_col=result.cursor_col_byte;
    step(0,0,{400.0f,18.0f},false);step(SDLK_DOWN);
    check(result.cursor_line==1 && result.cursor_col_byte==clicked_col && frame_stats(test).arena_used_bytes<512u*1024u,__LINE__);
    check(text_buffer_set(&b,str_lit("纯血🚀e\xCC\x81\n    float value = 1;\n    return value;")),__LINE__);
    code_editor_scroll_to_line(eid,0,AlignY::Top);code_editor_set_cursor(eid,0,0);step();request_focus(eid);step();
    step(SDLK_RIGHT);check(result.cursor_byte==3,__LINE__);step(SDLK_RIGHT);check(result.cursor_byte==6,__LINE__);step(SDLK_RIGHT);check(result.cursor_byte==10,__LINE__);step(SDLK_RIGHT);check(result.cursor_byte==13,__LINE__);
    step(SDLK_BACKSPACE);check(result.cursor_byte==10 && text_buffer_line_end(&b,0)==10,__LINE__);
    const uint32_t line1=text_buffer_line_start(&b,1);
    code_editor_set_cursor(eid,line1,line1);step();step(SDLK_LEFT,Mod_Ctrl);
    check(result.cursor_line==0 && result.cursor_byte<10,__LINE__);
    code_editor_set_cursor(eid,10,10);step();step(SDLK_RIGHT,Mod_Alt);
    check(result.cursor_line==1 && result.cursor_col_byte==9,__LINE__);
    code_editor_set_cursor(eid,0,text_buffer_byte_length(&b));step();
    check(has_text_selection(eid),__LINE__);
    const Str selected=selected_text(eid);check(selected.length==text_buffer_byte_length(&b),__LINE__);
    const uint32_t original=text_buffer_byte_length(&b);
    step(SDLK_C,Mod_Ctrl);step(SDLK_X,Mod_Ctrl);check(text_buffer_byte_length(&b)==0,__LINE__);
    step(SDLK_V,Mod_Ctrl);check(text_buffer_byte_length(&b)==original,__LINE__);
    step(SDLK_A,Mod_Ctrl);step(SDLK_TAB);check(text_buffer_byte_length(&b)==original+12,__LINE__);
    step(SDLK_TAB,Mod_Shift);check(text_buffer_byte_length(&b)==original,__LINE__);
    step(SDLK_Z,Mod_Ctrl);check(text_buffer_byte_length(&b)==original+12,__LINE__);
    step(SDLK_Y,Mod_Ctrl);check(text_buffer_byte_length(&b)==original,__LINE__);
    code_editor_set_cursor(eid,text_buffer_line_end(&b,1),text_buffer_line_end(&b,1));step();step(SDLK_RETURN);
    check(text_buffer_line_count(&b)==4 && result.cursor_col_byte==4,__LINE__);
    step(SDLK_HOME);check(result.cursor_col_byte==0,__LINE__);step(SDLK_HOME);check(result.cursor_col_byte==4,__LINE__);step(SDLK_END);check(result.cursor_col_byte==4,__LINE__);
    step(SDLK_PAGEUP);check(result.cursor_line==0,__LINE__);step(SDLK_PAGEDOWN);check(result.cursor_line==3,__LINE__);
    code_editor_set_cursor(eid,0,0);step();step(0,0,{110,18},true);step(0,0,{110,18},false);step(0,0,{110,18},true,2);check(has_text_selection(eid),__LINE__);step(0,0,{110,18},false);
    SDL_Event ev={};ev.type=SDL_EVENT_TEXT_EDITING;ev.edit.text="拼音";ev.edit.start=0;ev.edit.length=1;process_event(test,ev);step();check(result.composing && result.ime_cursor_rect.h>0,__LINE__);
    ev={};ev.type=SDL_EVENT_KEY_DOWN;ev.key.key=SDLK_ESCAPE;process_event(test,ev);step();check(!result.composing && has_text_selection(eid),__LINE__);
    step(SDLK_ESCAPE);check(!has_text_selection(eid),__LINE__);
    // A text input's nonzero preedit cursor must not survive editor focus.
    const uint32_t input_id=id("v24_ime_input");char input_text[32]="input";
    TextInputResult input_result={};
    const auto input_step=[&]() noexcept {
        begin_frame(test,{.viewport_w=800,.viewport_h=500});
        input_result=text_input({.id=input_id,.buffer=input_text,.capacity=sizeof(input_text)});
        end_frame(test);
    };
    input_step();request_focus(input_id);input_step();
    ev={};ev.type=SDL_EVENT_TEXT_EDITING;ev.edit.text="拼音";ev.edit.start=1;ev.edit.length=1;
    process_event(test,ev);input_step();
    check(input_result.focused && input_result.composing && ime_cursor_rect(test).h>0 && frame_stats(test).ime_cursor_offset_win>0,__LINE__);
    request_focus(eid);step();
    check(result.focused && !result.composing && result.ime_cursor_rect.h>0 && ime_cursor_rect(test).w==2.0f && frame_stats(test).ime_cursor_offset_win==0,__LINE__);
    ev={};ev.type=SDL_EVENT_TEXT_EDITING;ev.edit.text="拼音";ev.edit.start=1;ev.edit.length=1;
    process_event(test,ev);step();
    check(result.composing && ime_cursor_rect(test).w>2.0f && frame_stats(test).ime_cursor_offset_win>0,__LINE__);
    ev={};ev.type=SDL_EVENT_TEXT_EDITING;ev.edit.text="";
    process_event(test,ev);step();
    check(!result.composing && ime_cursor_rect(test).h>0 && ime_cursor_rect(test).w==2.0f && frame_stats(test).ime_cursor_offset_win==0,__LINE__);
    SyntaxToken tokens[32];const uint32_t count=tokenize_cpp_hlsl(0,str_lit("float4 x = 1.0; // comment"),tokens,32,nullptr);check(count==3 && tokens[0].byte_start==0 && tokens[0].byte_end==6 && tokens[2].byte_start==16,__LINE__);
    step(0,0,{-1,-1},false,1,true);check(text_buffer_byte_length(&b)==22,__LINE__);
    clear_text_selection(eid);request_focus(0);for(uint32_t i=0;i<120;++i)step();
    const auto stats=frame_stats(test);check(stats.draw_call_count<=3 && stats.idle_sleeping,__LINE__);

    // v0.23.0: ghost_text Tab acceptance without losing focus, code_editor line_decorator + gutter click,
    // sticky-bottom VirtualList, and headless clipboard image probe.
    {
        char gbuf[64] = "bun test";
        const uint32_t gid = id("v23_ghost_input");
        const uint32_t other_id = id("v23_other_input");
        char obuf[16] = "";
        TextInputResult gres = {};
        const auto gstep = [&](uint32_t key = 0u) noexcept {
            begin_frame(test, {.viewport_w = 800, .viewport_h = 500, .key_pressed_code = key});
            begin_box({.axis = Axis::Col, .width = Sizing::fixed(800), .height = Sizing::fixed(500)});
            gres = text_input({.id = gid, .buffer = gbuf, .capacity = sizeof(gbuf),
                .ghost_text = str_lit(" --bail"), .accept_ghost_on_tab = true});
            text_input({.id = other_id, .buffer = obuf, .capacity = sizeof(obuf)});
            end_box();
            end_frame(test);
        };
        gstep(); request_focus(gid); gstep();
        check(gres.focused && !gres.ghost_accepted, __LINE__);
        gstep(SDLK_TAB);
        check(gres.focused && gres.ghost_accepted, __LINE__);

        uint32_t dec_calls = 0u;
        const auto dec_fn = [](uint32_t line, CodeLineDecoration* out, void* ud) noexcept -> bool {
            ++(*static_cast<uint32_t*>(ud));
            if (line == 0u) {
                out->row_bg = Color::rgba8(60, 180, 80, 24);
                out->gutter_bar_color = Color::rgb_hex(0x3FB950);
                out->gutter_tag = str_lit("70a2");
                return true;
            }
            return false;
        };
        const uint32_t deid = id("v23_diff_ed");
        CodeEditorResult dres = {};
        const auto dstep = [&](Vec2 ptr, bool down) noexcept {
            begin_frame(test, {.viewport_w = 800, .viewport_h = 500, .pointer_pos = ptr, .pointer_down = down});
            begin_box({.width = Sizing::fixed(800), .height = Sizing::fixed(500)});
            dres = code_editor({.id = deid, .buffer = &b, .width = Sizing::fixed(700), .height = Sizing::fixed(240),
                .line_decorator = dec_fn, .line_decorator_user_data = &dec_calls, .gutter_tag_width_px = 40.0f});
            end_box();
            end_frame(test);
        };
        dstep({-1.0f, -1.0f}, false);
        dstep({20.0f, 16.0f}, true);
        check(dec_calls > 0u && dres.hovered_line == 0 && dres.clicked_gutter_line == 0, __LINE__);
        dstep({20.0f, 16.0f}, false);

        const uint32_t slid = id("v23_sticky_list");
        VirtualListSlice sl = {};
        const auto sstep = [&](uint32_t n, Vec2 wheel = {}) noexcept {
            begin_frame(test, {.viewport_w = 800, .viewport_h = 500, .pointer_pos = {100.0f, 80.0f}, .scroll_delta = wheel});
            begin_box({.width = Sizing::fixed(800), .height = Sizing::fixed(500)});
            sl = begin_virtual_list({.id = slid, .item_count = n, .item_height_px = 30.0f,
                .width = Sizing::fixed(400), .height = Sizing::fixed(150), .stick_to_bottom = true});
            for (uint32_t i = sl.first_index; i < sl.end_index; ++i) {
                box({.width = Sizing::grow(), .height = Sizing::fixed(30.0f)});
            }
            end_virtual_list();
            end_box();
            end_frame(test);
        };
        sstep(20u); sstep(20u);
        check(sl.at_bottom && virtual_list_is_at_bottom(slid) && sl.scroll_y >= sl.max_scroll_y - 1.0, __LINE__);
        sstep(40u);
        check(sl.at_bottom && sl.scroll_y >= sl.max_scroll_y - 1.0, __LINE__);
        sstep(40u, {0.0f, 2.0f});
        check(!sl.at_bottom && !virtual_list_is_at_bottom(slid), __LINE__);
        const double detached_y = sl.scroll_y;
        sstep(50u);
        check(!sl.at_bottom && std::fabs(sl.scroll_y - detached_y) < 1.0, __LINE__);
        virtual_list_scroll_to_bottom(slid);
        sstep(50u);
        check(sl.at_bottom && virtual_list_is_at_bottom(slid) && sl.scroll_y >= sl.max_scroll_y - 1.0, __LINE__);

        const ClipboardImageData img = get_clipboard_image();
        check(img.bytes == nullptr && img.size_bytes == 0u, __LINE__);
    }

    // E4: CodeEditor RTL selection positive-width bands, Backspace vs Delete undo groups,
    // and wrapped-editor incremental Fenwick splice on Enter/Backspace.
    {
        TextBuffer rtl_buf = {};
        check(init_text_buffer(&rtl_buf, str_lit("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xD7\xA2\xD7\x95\xD7\x9C\xD7\x9D")), __LINE__);
        const uint32_t rtl_ed = id("e4_rtl_editor");
        const Color sel_col = Color::rgba8(90, 140, 230, 160);
        const uint32_t sel_packed = sel_col.pack_rgba8();
        code_editor_set_cursor(rtl_ed, 0u, 4u);
        begin_frame(test, {.viewport_w = 600, .viewport_h = 200});
        code_editor({.id = rtl_ed, .buffer = &rtl_buf, .width = Sizing::fixed(500), .height = Sizing::fixed(160), .selection_color = sel_col});
        end_frame(test);
        bool saw_sel = false;
        for (const Prim& p : frame_prims(test)) {
            if ((p.style.x & 0xFu) == static_cast<uint32_t>(PrimKind::Rect) && p.style.y == sel_packed) {
                saw_sel = true;
                check(p.bounds.z > 0.0f && p.bounds.w > 0.0f, __LINE__);
            }
        }
        check(saw_sel, __LINE__);
        free_text_buffer(&rtl_buf);

        // TextBuffer Backspace (group 2) and Delete (group 3) coalesce within group, not across groups.
        TextBuffer ub = {};
        check(init_text_buffer(&ub, str_lit("abcdef")), __LINE__);
        check(text_buffer_replace(&ub, 2, 3, {}, true, 3, 3, 2, 2, 2, 1.0), __LINE__); // BS 'c' -> "abdef"
        check(text_buffer_replace(&ub, 1, 2, {}, true, 2, 2, 1, 1, 2, 1.1), __LINE__); // BS 'b' -> "adef" (coalesced)
        check(text_buffer_replace(&ub, 1, 2, {}, true, 1, 1, 1, 1, 3, 1.2), __LINE__); // DEL 'd' -> "aef" (new group)
        check(text_buffer_replace(&ub, 1, 2, {}, true, 1, 1, 1, 1, 3, 1.3), __LINE__); // DEL 'e' -> "af" (coalesced)
        uint32_t uc = 0u, ua = 0u;
        check(text_buffer_undo(&ub, &uc, &ua) && text_buffer_byte_length(&ub) == 4u && uc == 1u, __LINE__); // restores "adef"
        check(text_buffer_undo(&ub, &uc, &ua) && text_buffer_byte_length(&ub) == 6u && uc == 3u, __LINE__); // restores "abcdef"
        free_text_buffer(&ub);

        // Wrapped CodeEditor incremental Fenwick splice on Enter + Backspace preserves heights.
        TextBuffer wb = {};
        check(init_text_buffer(&wb), __LINE__);
        char wseed[80];
        for (uint32_t i = 0u; i < 400u; ++i) {
            const int n = std::snprintf(wseed, sizeof(wseed), "line_%03u_0123456789_abcdefghijklmnopqrstuvwxyz_extra%s", i, i + 1u < 400u ? "\n" : "");
            check(text_buffer_replace(&wb, text_buffer_byte_length(&wb), text_buffer_byte_length(&wb), {wseed, static_cast<uint32_t>(n)}, false), __LINE__);
        }
        const uint32_t wed = id("e4_wrap_splice");
        CodeEditorResult wres = {};
        const auto wstep = [&](uint32_t key = 0u) noexcept {
            begin_frame(test, {.viewport_w = 400, .viewport_h = 260, .key_pressed_code = key});
            wres = code_editor({.id = wed, .buffer = &wb, .width = Sizing::fixed(300), .height = Sizing::fixed(240), .word_wrap = true});
            end_frame(test);
        };
        wstep(); request_focus(wed);
        const uint32_t l5 = text_buffer_line_start(&wb, 5u) + 10u;
        code_editor_set_cursor(wed, l5, l5); wstep();
        wstep(SDLK_RETURN);
        check(text_buffer_line_count(&wb) == 401u && wres.cursor_line == 6u, __LINE__);
        wstep(SDLK_BACKSPACE);
        check(text_buffer_line_count(&wb) == 400u && wres.cursor_line == 5u, __LINE__);
        free_text_buffer(&wb);
    }

    destroy_context(test);set_current_context(restore);free_text_buffer(&b);return ok;
}
