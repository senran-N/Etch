// Shared sandbox [21] / Studio [S17] document and editor regression.
static bool verify_editor_v24(etch::Context* restore) noexcept {
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
    check(render_headless_png(test,800,500,"editor_v24_check.png"),__LINE__);
    std::remove("editor_v24_check.png");
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
    SyntaxToken tokens[32];const uint32_t count=tokenize_cpp_hlsl(0,str_lit("float4 x = 1.0; // comment"),tokens,32,nullptr);check(count>3 && tokens[0].byte_start==0 && tokens[0].byte_end==6,__LINE__);
    step(0,0,{-1,-1},false,1,true);check(text_buffer_byte_length(&b)==22,__LINE__);
    clear_text_selection(eid);request_focus(0);for(uint32_t i=0;i<120;++i)step();
    const auto stats=frame_stats(test);check(stats.draw_call_count<=3 && stats.idle_sleeping,__LINE__);
    destroy_context(test);set_current_context(restore);free_text_buffer(&b);return ok;
}
