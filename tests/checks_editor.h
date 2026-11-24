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
    check(!b.undo_ops && !b.undo_blob,__LINE__);
    {
        char stack_msg[] = "header";
        const TextDiagnostic diagnostics[] = {
            {0u, 5u, TextDiagnosticSeverity::Warning, {stack_msg, 6u}},
            {stride, stride + 5u, TextDiagnosticSeverity::Error, str_lit("line")}
        };
        const uint64_t diagnostic_revision = text_buffer_revision(&b);
        check(text_buffer_set_diagnostics(&b, diagnostic_revision, diagnostics) &&
              b.diagnostic_count == 2u && b.diagnostics_revision == diagnostic_revision,__LINE__);
        stack_msg[0] = 'X';
        check(b.diagnostics[0].message.length == 6u &&
              std::memcmp(b.diagnostics[0].message.chars, "header", 6u) == 0,__LINE__);
        check(!text_buffer_set_diagnostics(&b, diagnostic_revision - 1u, diagnostics),__LINE__);
        check(text_buffer_replace(&b, 0u, 0u, str_lit("x"), false) &&
              b.diagnostics_revision != text_buffer_revision(&b),__LINE__);
        check(text_buffer_replace(&b, 0u, 1u, {}, false),__LINE__);
        text_buffer_clear_diagnostics(&b);
    }
    {
        const uint32_t length = text_buffer_byte_length(&b);
        const TextEdit patch[] = {{0, 1, str_lit("头")}, {length-1u, length, str_lit("尾")}};
        const uint64_t rev = text_buffer_revision(&b);
        check(text_buffer_apply_edits(&b, rev, patch) && b.undo_count == 1u &&
              b.undo_blob_used < 128u && text_buffer_revision(&b) == rev + 1u,__LINE__);
        check(!text_buffer_apply_edits(&b, rev, patch),__LINE__);
        check(text_buffer_undo(&b) && text_buffer_byte_length(&b) == length &&
              text_buffer_redo(&b) && text_buffer_byte_length(&b) == length + 4u,__LINE__);
        check(text_buffer_undo(&b),__LINE__);
        text_buffer_clear_undo(&b);
    }
    TextBuffer multilingual = {};
    if (init_text_buffer(&multilingual, str_lit("a\r\n中😀z"))) {
        check(text_buffer_replace(&multilingual, 5u, 5u, str_lit("+")),__LINE__);
        const auto pos = text_buffer_utf16_position(&multilingual, 8u);
        check(pos.line == 1u && pos.character == 2u,__LINE__);
        check(text_buffer_byte_from_utf16(&multilingual, {1u,3u}) == 6u,__LINE__);
        check(text_buffer_byte_from_utf16(&multilingual, {1u,4u}) == 10u,__LINE__);
        check(text_buffer_byte_from_utf16(&multilingual, {3u,0u}) == 11u,__LINE__);
        free_text_buffer(&multilingual);
    } else check(false,__LINE__);
    {
        TextBuffer batch = {};
        check(init_text_buffer(&batch, str_lit("alpha\nbeta\n")),__LINE__);
        const uint64_t batch_revision = text_buffer_revision(&batch);
        const TextEdit invalid_utf8[] = {{0, 0, str_lit("\xc0\x80")}};
        check(!text_buffer_apply_edits(&batch, batch_revision, invalid_utf8),__LINE__);
        const TextEdit overlap[] = {{0, 2, {}}, {1, 2, {}}};
        check(!text_buffer_apply_edits(&batch, batch_revision, overlap),__LINE__);
        const Str alias = text_buffer_line(&batch, 0);
        const TextEdit many[] = {{0, 0, str_lit("1")}, {0, 0, alias}, {6, 10, str_lit("z\r\n")}};
        check(text_buffer_apply_edits(&batch, batch_revision, many),__LINE__);
        char batch_actual[32] = {};
        const auto want = str_lit("1alphaalpha\nz\n\n");
        check(text_buffer_copy_range(&batch,0,UINT32_MAX,batch_actual,sizeof(batch_actual)) == want.length &&
              std::memcmp(batch_actual,want.chars,want.length) == 0,__LINE__);
        uint32_t cursor = 0, anchor = 0;
        check(text_buffer_undo(&batch,&cursor,&anchor) && cursor == 0 && anchor == 0 &&
              text_buffer_redo(&batch,&cursor,&anchor) && cursor == 14 && anchor == 14,__LINE__);
        free_text_buffer(&batch);
    }
    check(text_buffer_line_count(&b)==lines && text_buffer_line_start(&b,999985)==999985*stride,__LINE__);
    const uint32_t at=999985*stride+5;
    check(text_buffer_replace(&b,at,at,str_lit("x")),__LINE__);
    const uint32_t suffix=b.line_gap_end;const uint32_t encoded=b.line_data[suffix];
    for(uint32_t i=0;i<1000;++i)check(text_buffer_replace(&b,at+1+i,at+1+i,str_lit("a"),true,0,0,0,0,1,double(i)*.001),__LINE__);
    check(b.line_gap_end==suffix && b.line_data[suffix]==encoded,__LINE__);
    check(text_buffer_line_start(&b,999986)==999986*stride+1001,__LINE__);
    check(text_buffer_undo(&b) && text_buffer_redo(&b),__LINE__);
    check(b.undo_blob_cap <= 4096u && b.undo_cap <= 16u,__LINE__);
    check(text_buffer_replace(&b,at,at+1001,{}),__LINE__);
    check(text_buffer_line_start(&b,999986)==999986*stride,__LINE__);
    check(text_buffer_replace(&b,at,at,str_lit("\r\n")),__LINE__);
    check(text_buffer_line_count(&b)==lines+1 && text_buffer_line_start(&b,999986)==at+1,__LINE__);
    check(text_buffer_undo(&b) && text_buffer_line_count(&b)==lines,__LINE__);
    Context* test=create_context({.headless=true});if(!test){free_text_buffer(&b);return false;}set_current_context(test);
    {
        const uint32_t input_id = id("queued_text_input");
        char buffer[4096] = {};
        char commit[1537] = {};
        std::memset(commit, 'x', sizeof(commit) - 1u);
        const auto input_frame = [&]() noexcept {
            begin_frame(test, {.viewport_w = 640, .viewport_h = 240});
            const auto edit = text_input({.id = input_id, .buffer = buffer, .capacity = sizeof(buffer)});
            end_frame(test);
            return edit;
        };
        input_frame();
        request_focus(input_id);
        input_frame();
        SDL_Event event = {};
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.text = commit;
        process_event(test, event);
        event.text.text = "y";
        for (uint32_t i = 0; i < 40u; ++i) process_event(test, event);
        check(input_frame().byte_length == 1576u &&
            std::memcmp(buffer, commit, 1536u) == 0 && buffer[1536] == 'y' && buffer[1575] == 'y', __LINE__);
        event.text.text = "z";
        process_event(test, event);
        check(input_frame().byte_length == 1577u && buffer[1576] == 'z', __LINE__);
    }
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
    const auto stats=frame_stats(test);check(stats.draw_call_count>0 && stats.draw_call_count<=24 && stats.idle_sleeping,__LINE__);

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

    // E5: Diagnostic severity precedence, half-open range non-bleed, and auto-hide on revision change.
    {
        TextBuffer db = {};
        check(init_text_buffer(&db, str_lit("alpha\nbeta\n")), __LINE__);
        const TextDiagnostic diags[] = {
            {0u, 6u, TextDiagnosticSeverity::Warning, str_lit("warn")},
            {1u, 3u, TextDiagnosticSeverity::Error, str_lit("err")}
        };
        check(text_buffer_set_diagnostics(&db, text_buffer_revision(&db), diags), __LINE__);
        const uint32_t ded = id("e5_diag_editor");
        const uint32_t err_bar = Color::rgba8(234, 67, 53).pack_rgba8();
        const uint32_t warn_bar = Color::rgba8(245, 158, 11).pack_rgba8();
        auto count_bars = [&](uint32_t packed) noexcept {
            uint32_t n = 0u;
            for (const Prim& p : frame_prims(test)) {
                if ((p.style.x & 0xFu) == static_cast<uint32_t>(PrimKind::Rect) && p.style.y == packed) ++n;
            }
            return n;
        };
        begin_frame(test, {.viewport_w = 500, .viewport_h = 200});
        code_editor({.id = ded, .buffer = &db, .width = Sizing::fixed(420), .height = Sizing::fixed(160)});
        end_frame(test);
        // Line 0 has Warning + Error -> Error wins (1 bar); half-open [0,6) must not bleed to line 1 (0 warn bars).
        check(count_bars(err_bar) == 1u && count_bars(warn_bar) == 0u, __LINE__);
        // After mutating buffer, stale diagnostics must automatically hide.
        check(text_buffer_replace(&db, 0u, 0u, str_lit("x"), false), __LINE__);
        begin_frame(test, {.viewport_w = 500, .viewport_h = 200});
        code_editor({.id = ded, .buffer = &db, .width = Sizing::fixed(420), .height = Sizing::fixed(160)});
        end_frame(test);
        check(count_bars(err_bar) == 0u && count_bars(warn_bar) == 0u, __LINE__);
        free_text_buffer(&db);
    }

    // E3: Kernel audit #27 (find/find_all, inline ghost completion, search highlights,
    // folding, multi-cursor), #31 (command registry), #47 (automation), #56 (dock detach/
    // transfer/tear-off), and #57 (multi-MIME drag negotiation).
    {
        // #27a: text_buffer_find & text_buffer_find_all (gap-straddling, case, whole-word)
        TextBuffer sb = {};
        check(init_text_buffer(&sb, str_lit("Foo foo_bar FOO (foo) foobar")), __LINE__);
        // Force gap inside the first word to verify gap-straddling search.
        check(text_buffer_replace(&sb, 2u, 2u, str_lit("Z"), false) &&
              text_buffer_replace(&sb, 2u, 3u, {}, false), __LINE__);
        TextSearchMatch m = {};
        check(text_buffer_find(&sb, str_lit("foo"), 0u, true, false, &m) && m.byte_start == 4u && m.byte_end == 7u, __LINE__);
        check(text_buffer_find(&sb, str_lit("foo"), 0u, false, false, &m) && m.byte_start == 0u && m.byte_end == 3u, __LINE__);
        check(text_buffer_find(&sb, str_lit("foo"), 0u, true, true, &m) && m.byte_start == 17u && m.byte_end == 20u, __LINE__);
        TextSearchMatch matches[8] = {};
        const uint32_t ww_cnt = text_buffer_find_all(&sb, str_lit("foo"), false, true, matches, 8u);
        check(ww_cnt == 3u && matches[0].byte_start == 0u && matches[1].byte_start == 12u && matches[2].byte_start == 17u, __LINE__);
        check(text_buffer_find_all(&sb, str_lit("foo"), false, false, nullptr, 0u) == 5u, __LINE__);
        free_text_buffer(&sb);

        // #27b: inline ghost completion (Tab accept & Esc dismiss) + search highlights
        TextBuffer gb = {};
        check(init_text_buffer(&gb, str_lit("ret")), __LINE__);
        const uint32_t ged = id("e3_ghost_ed");
        const auto gstep = [&](uint32_t key = 0u) noexcept {
            begin_frame(test, {.viewport_w = 600, .viewport_h = 220, .key_pressed_code = key});
            code_editor({.id = ged, .buffer = &gb, .width = Sizing::fixed(500), .height = Sizing::fixed(180)});
            end_frame(test);
        };
        code_editor_set_cursor(ged, 3u, 3u);
        gstep();
        request_focus(ged);
        code_editor_set_inline_completion(ged, str_lit("urn 0;"));
        gstep();
        check(!code_editor_completion_accepted(ged) && !code_editor_completion_dismissed(ged), __LINE__);
        gstep(SDLK_TAB);
        char gcopy[32] = {};
        const uint32_t glen = text_buffer_copy_range(&gb, 0u, UINT32_MAX, gcopy, sizeof(gcopy));
        check(code_editor_completion_accepted(ged) && !code_editor_completion_dismissed(ged) &&
              glen == 9u && std::memcmp(gcopy, "return 0;", 9u) == 0, __LINE__);
        gstep();
        check(!code_editor_completion_accepted(ged), __LINE__);
        code_editor_set_inline_completion(ged, str_lit(" // note"));
        gstep();
        gstep(SDLK_ESCAPE);
        check(code_editor_completion_dismissed(ged) && !code_editor_completion_accepted(ged) &&
              text_buffer_byte_length(&gb) == 9u, __LINE__);

        // Search highlights emit amber highlight rects.
        const uint32_t hl_packed = Color::rgba8(245, 196, 66, 84).pack_rgba8();
        auto count_rect_color = [&](uint32_t packed) noexcept {
            uint32_t n = 0u;
            for (const Prim& p : frame_prims(test)) {
                if ((p.style.x & 0xFu) == static_cast<uint32_t>(PrimKind::Rect) && p.style.y == packed) ++n;
            }
            return n;
        };
        code_editor_set_search_highlights(ged, str_lit("RET"), false);
        gstep();
        check(count_rect_color(hl_packed) >= 1u, __LINE__);
        code_editor_set_search_highlights(ged, str_lit("RET"), true);
        gstep();
        check(count_rect_color(hl_packed) == 0u, __LINE__);
        code_editor_set_search_highlights(ged, {});
        free_text_buffer(&gb);

        // #27c: code folding hides strictly interior lines (start_line, end_line] & emits '...' badge
        TextBuffer fb = {};
        check(init_text_buffer(&fb, str_lit("L0\nL1\nL2\nL3\nL4\nL5")), __LINE__);
        const uint32_t fed = id("e3_fold_ed");
        const uint32_t fold_pill_packed = Color::rgba8(140, 150, 170, 36).pack_rgba8();
        check(!code_editor_set_fold(fed, 2u, 2u, true), __LINE__);
        check(code_editor_set_fold(fed, 1u, 3u, true), __LINE__);
        check(!code_editor_is_line_folded(fed, 0u) && !code_editor_is_line_folded(fed, 1u) &&
              code_editor_is_line_folded(fed, 2u) && code_editor_is_line_folded(fed, 3u) &&
              !code_editor_is_line_folded(fed, 4u), __LINE__);
        begin_frame(test, {.viewport_w = 500, .viewport_h = 220});
        code_editor({.id = fed, .buffer = &fb, .width = Sizing::fixed(420), .height = Sizing::fixed(180)});
        end_frame(test);
        check(count_rect_color(fold_pill_packed) == 1u, __LINE__);
        check(code_editor_set_fold(fed, 1u, 3u, false), __LINE__);
        check(!code_editor_is_line_folded(fed, 2u) && !code_editor_is_line_folded(fed, 3u), __LINE__);
        begin_frame(test, {.viewport_w = 500, .viewport_h = 220});
        code_editor({.id = fed, .buffer = &fb, .width = Sizing::fixed(420), .height = Sizing::fixed(180)});
        end_frame(test);
        check(count_rect_color(fold_pill_packed) == 0u, __LINE__);
        free_text_buffer(&fb);

        // #27d: multi-cursor atomic edit in one undo step
        TextBuffer mb = {};
        check(init_text_buffer(&mb, str_lit("aaa\nbbb\nccc")), __LINE__);
        const uint32_t med = id("e3_multi_ed");
        const auto mstep = [&]() noexcept {
            begin_frame(test, {.viewport_w = 500, .viewport_h = 220});
            code_editor({.id = med, .buffer = &mb, .width = Sizing::fixed(420), .height = Sizing::fixed(180)});
            end_frame(test);
        };
        code_editor_set_cursor(med, 0u, 0u);
        mstep();
        request_focus(med);
        mstep();
        check(code_editor_cursor_count(med) == 1u, __LINE__);
        check(code_editor_add_cursor(med, 4u) && code_editor_add_cursor(med, 8u) &&
              !code_editor_add_cursor(med, 4u) && !code_editor_add_cursor(med, 0u) &&
              code_editor_cursor_count(med) == 3u, __LINE__);
        SDL_Event tev = {};
        tev.type = SDL_EVENT_TEXT_INPUT;
        tev.text.text = "X";
        process_event(test, tev);
        mstep();
        char mcopy[32] = {};
        const uint32_t mlen = text_buffer_copy_range(&mb, 0u, UINT32_MAX, mcopy, sizeof(mcopy));
        check(mlen == 14u && std::memcmp(mcopy, "Xaaa\nXbbb\nXccc", 14u) == 0, __LINE__);
        check(text_buffer_undo(&mb) && text_buffer_byte_length(&mb) == 11u, __LINE__);
        code_editor_clear_extra_cursors(med);
        check(code_editor_cursor_count(med) == 1u, __LINE__);
        free_text_buffer(&mb);

        // #31: command registry, shortcut conflict, dispatch & recent history
        char tmp_title[] = "Save Doc";
        check(register_command(test, {
            .id = 101u, .name = str_lit("file.save"), .title = {tmp_title, 8u},
            .category = str_lit("File"), .shortcut_mods = Mod_Ctrl,
            .shortcut_keycode = SDLK_S, .enabled = true, .allow_when_typing = true
        }), __LINE__);
        tmp_title[0] = 'X';
        check(register_command(test, {
            .id = 102u, .name = str_lit("file.open"), .title = str_lit("Open Doc"),
            .category = str_lit("File"), .shortcut_mods = Mod_Ctrl,
            .shortcut_keycode = SDLK_O, .enabled = true, .allow_when_typing = true
        }), __LINE__);
        CommandDesc got_cmd = {};
        check(command_count(test) == 2u && get_command(test, 0u, &got_cmd) &&
              got_cmd.id == 101u && got_cmd.title.length == 8u &&
              std::memcmp(got_cmd.title.chars, "Save Doc", 8u) == 0, __LINE__);
        check(find_command_shortcut_conflict(test, Mod_Ctrl, SDLK_S, 0u) == 101u &&
              find_command_shortcut_conflict(test, Mod_Ctrl, SDLK_S, 101u) == 0u, __LINE__);
        check(set_command_enabled(test, 102u, false) && !is_command_enabled(test, 102u) &&
              !dispatch_command(test, 102u), __LINE__);
        check(set_command_enabled(test, 102u, true) && is_command_enabled(test, 102u) &&
              dispatch_command(test, 102u), __LINE__);
        begin_frame(test, {.viewport_w = 400, .viewport_h = 200, .key_mods = Mod_Ctrl, .key_pressed_code = SDLK_S});
        check(poll_triggered_command(test) == 101u && poll_triggered_command(test) == 0u, __LINE__);
        end_frame(test);
        check(recent_command_count(test) == 2u && recent_command_at(test, 0u) == 101u &&
              recent_command_at(test, 1u) == 102u, __LINE__);
        check(unregister_command(test, 101u) && unregister_command(test, 102u) && command_count(test) == 0u, __LINE__);

        // #47: automation semantic queries, deterministic clock & input recording
        const uint32_t save_btn = id("e3_auto_save");
        const uint32_t del_btn = id("e3_auto_del");
        begin_frame(test, {.viewport_w = 480, .viewport_h = 200});
        begin_box({.axis = Axis::Row, .width = Sizing::fixed(480), .height = Sizing::fixed(200), .gap = 8});
        button({.id = save_btn, .label = str_lit("Save Project")});
        button({.id = del_btn, .label = str_lit("Delete Item"), .disabled = true});
        end_box();
        end_frame(test);
        uint32_t found_ids[4] = {};
        check(automation_find_nodes(test, {.role = A11yRole::Button, .label_contains = str_lit("save")}, found_ids, 4u) == 1u &&
              found_ids[0] == save_btn, __LINE__);
        A11yNode asserted = {};
        check(automation_assert_node(test, {.role = A11yRole::Button, .label_contains = str_lit("DELETE"),
              .required_states = A11yState_Disabled}, &asserted) && asserted.id == del_btn, __LINE__);
        check(!automation_assert_node(test, {.role = A11yRole::Button, .label_contains = str_lit("delete"),
              .forbidden_states = A11yState_Disabled}), __LINE__);
        set_time_scale(test, 2.0f);
        check(time_scale(test) == 2.0f, __LINE__);
        set_time_scale(test, 1.0f);
        check(time_scale(test) == 1.0f, __LINE__);
        automation_clear_recording(test);
        automation_set_recording(test, true);
        begin_frame(test, {.viewport_w = 400, .viewport_h = 200, .dt_seconds = 1.0f / 60.0f,
                           .pointer_pos = {50.0f, 60.0f}, .pointer_down = true});
        end_frame(test);
        automation_set_recording(test, false);
        if (automation_recorded_count(test) > 0u) {
            RecordedFrameInput rec = {};
            check(automation_get_recorded(test, 0u, &rec) && rec.pointer_down && rec.pointer_pos.x == 50.0f, __LINE__);
        }
        automation_clear_recording(test);
        check(automation_recorded_count(test) == 0u, __LINE__);

        // #56: dock detach, transfer & tear-off
        const uint32_t dk_a = id("e3_dock_a");
        const uint32_t dk_b = id("e3_dock_b");
        const uint32_t p1 = id("e3_pane_1");
        const uint32_t p2 = id("e3_pane_2");
        const uint32_t p3 = id("e3_pane_3");
        const uint32_t ra = dock_reset_root(dk_a);
        uint32_t la = 0u, lb = 0u;
        check(ra != 0u && dock_split_node(dk_a, ra, Axis::Row, 0.5f, &la, &lb), __LINE__);
        check(dock_add_pane(dk_a, la, {.id = p1, .title = str_lit("Left")}) &&
              dock_add_pane(dk_a, lb, {.id = p2, .title = str_lit("Right")}), __LINE__);
        const uint32_t rb = dock_reset_root(dk_b);
        check(rb != 0u && dock_add_pane(dk_b, rb, {.id = p3, .title = str_lit("Target")}), __LINE__);
        check(dock_transfer_pane(dk_a, dk_b, p2, p3, DockZone::Right, 0.5f), __LINE__);
        check(dock_find_pane_leaf(dk_a, p2) == 0u && dock_find_pane_leaf(dk_a, p1) != 0u &&
              dock_find_pane_leaf(dk_b, p2) != 0u && dock_find_pane_leaf(dk_b, p2) != dock_find_pane_leaf(dk_b, p3), __LINE__);
        check(dock_detach_pane(dk_b, p2) && dock_find_pane_leaf(dk_b, p2) == 0u &&
              dock_find_pane_leaf(dk_b, p3) != 0u, __LINE__);
        const auto run_dock_a = [&](Vec2 ptr, bool down) noexcept {
            begin_frame(test, {.viewport_w = 600, .viewport_h = 400, .pointer_pos = ptr, .pointer_down = down});
            begin_dock_space({.id = dk_a, .width = Sizing::fixed(300), .height = Sizing::fixed(200)});
            while (next_dock_pane() != 0u) {
                box({.width = Sizing::grow(), .height = Sizing::grow()});
            }
            end_dock_space();
            end_frame(test);
        };
        run_dock_a({-1.0f, -1.0f}, false);
        const Rect tab_r = element_bounds(id_index(p1, 0xD300u));
        const Vec2 tab_pt = {tab_r.x + 8.0f, tab_r.y + tab_r.h * 0.5f};
        run_dock_a(tab_pt, true);
        run_dock_a({450.0f, 320.0f}, true);
        run_dock_a({450.0f, 320.0f}, false);
        DockTearOffEvent tear = {};
        check(dock_poll_tear_off(&tear) && tear.dock_id == dk_a && tear.pane_id == p1 &&
              tear.title.length == 4u && std::memcmp(tear.title.chars, "Left", 4u) == 0 &&
              dock_find_pane_leaf(dk_a, p1) == 0u && !dock_poll_tear_off(&tear), __LINE__);

        // #57: multi-MIME drag negotiation
        const uint32_t m_src = id("e3_mime_src");
        const uint32_t m_dst = id("e3_mime_dst");
        static constexpr uint8_t kTxtBytes[5] = {'h', 'e', 'l', 'l', 'o'};
        static constexpr uint8_t kJsonBytes[7] = {'{', '"', 'k', '"', ':', '1', '}'};
        const MimeDragEntry m_entries[2] = {
            {.mime_type = str_lit("text/plain"), .bytes = kTxtBytes, .size = 5u},
            {.mime_type = str_lit("application/json"), .bytes = kJsonBytes, .size = 7u},
        };
        begin_frame(test, {.viewport_w = 400, .viewport_h = 120});
        begin_box({.axis = Axis::Row, .width = Sizing::fixed(400), .height = Sizing::fixed(80), .gap = 20});
        box({.id = m_src, .width = Sizing::fixed(100), .height = Sizing::fixed(40)});
        box({.id = m_dst, .width = Sizing::fixed(120), .height = Sizing::fixed(40)});
        end_box();
        end_frame(test);
        const Rect dst_r = element_bounds(m_dst);
        begin_frame(test, {.viewport_w = 400, .viewport_h = 120, .pointer_pos = {20.0f, 20.0f}, .pointer_down = true});
        check(begin_mime_drag_payload(m_src, str_lit("Asset"), m_entries), __LINE__);
        MimeDragEntry pe = {};
        check(mime_drag_format_count() == 2u &&
              peek_mime_drag_entry(str_lit("TEXT/PLAIN"), &pe) && pe.size == 5u &&
              std::memcmp(pe.bytes, "hello", 5u) == 0 &&
              !peek_mime_drag_entry(str_lit("image/png"), &pe), __LINE__);
        begin_box({.axis = Axis::Row, .width = Sizing::fixed(400), .height = Sizing::fixed(80), .gap = 20});
        box({.id = m_src, .width = Sizing::fixed(100), .height = Sizing::fixed(40)});
        box({.id = m_dst, .width = Sizing::fixed(120), .height = Sizing::fixed(40)});
        end_box();
        end_frame(test);
        begin_frame(test, {.viewport_w = 400, .viewport_h = 120,
                           .pointer_pos = {dst_r.x + 10.0f, dst_r.y + 10.0f}, .pointer_down = false});
        MimeDragEntry ae = {};
        check(!accept_mime_drag_payload(m_dst, str_lit("image/png"), &ae), __LINE__);
        check(accept_mime_drag_payload(m_dst, str_lit("application/json"), &ae) &&
              ae.size == 7u && std::memcmp(ae.bytes, "{\"k\":1}", 7u) == 0, __LINE__);
        check(!accept_mime_drag_payload(m_dst, str_lit("text/plain"), &ae), __LINE__);
        end_frame(test);
    }

    destroy_context(test);set_current_context(restore);free_text_buffer(&b);return ok;
}
