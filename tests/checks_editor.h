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

        // E09: dock layout JSON save/load roundtrip, malformed preservation & window pane re-dock
        {
            const uint32_t dk_src = id("e3_e09_dock_src");
            const uint32_t dk_dst = id("e3_e09_dock_dst");
            const uint32_t jp1 = id("e3_e09_p1");
            const uint32_t jp2 = id("e3_e09_p2");
            const uint32_t jp3 = id("e3_e09_p3");
            const uint32_t r0 = dock_reset_root(dk_src);
            uint32_t l1 = 0u, l2 = 0u;
            check(r0 != 0u && dock_split_node(dk_src, r0, Axis::Row, 0.35f, &l1, &l2), __LINE__);
            check(dock_add_pane(dk_src, l1, {.id = jp1, .title = str_lit("Alpha \"Quoted\"")}) &&
                  dock_add_pane(dk_src, l2, {.id = jp2, .title = str_lit("Beta")}) &&
                  dock_add_pane(dk_src, l2, {.id = jp3, .title = str_lit("Gamma"), .default_open = false}), __LINE__);

            const uint32_t req = dock_save_layout_json(dk_src, nullptr, 0u);
            char jbuf[2048] = {};
            char trunc_buf[16] = {};
            check(req > 20u && req < sizeof(jbuf), __LINE__);
            check(dock_save_layout_json(dk_src, trunc_buf, sizeof(trunc_buf)) == req &&
                  trunc_buf[sizeof(trunc_buf) - 1u] == '\0', __LINE__);
            check(dock_save_layout_json(dk_src, jbuf, sizeof(jbuf)) == req &&
                  jbuf[req] == '\0' && std::strstr(jbuf, "\"nodes\"") != nullptr &&
                  std::strstr(jbuf, "\"is_split\":true") != nullptr &&
                  std::strstr(jbuf, "\"tabs\"") != nullptr, __LINE__);

            check(dock_load_layout_json(dk_dst, str_cstr(jbuf)), __LINE__);
            check(dock_space_initialized(dk_dst) &&
                  dock_find_pane_leaf(dk_dst, jp1) != 0u &&
                  dock_find_pane_leaf(dk_dst, jp2) != 0u &&
                  dock_find_pane_leaf(dk_dst, jp1) != dock_find_pane_leaf(dk_dst, jp2) &&
                  dock_is_pane_open(dk_dst, jp1) && dock_is_pane_open(dk_dst, jp2) &&
                  !dock_is_pane_open(dk_dst, jp3), __LINE__);

            // Malformed JSON must fail and preserve existing dock_dst layout intact
            check(!dock_load_layout_json(dk_dst, str_lit("{malformed")), __LINE__);
            check(!dock_load_layout_json(dk_dst, str_lit("{\"nodes\":[]}")), __LINE__);
            check(!dock_load_layout_json(dk_dst, str_lit(
                "{\"root\":0,\"nodes\":[{\"is_split\":true,\"axis\":0,\"ratio\":0.5,\"first_child\":0,\"second_child\":0}]}")), __LINE__);
            check(dock_find_pane_leaf(dk_dst, jp1) != 0u &&
                  dock_find_pane_leaf(dk_dst, jp2) != 0u &&
                  dock_is_pane_open(dk_dst, jp1), __LINE__);

            // Re-dock all open panes from dk_src into a fresh target dock
            const uint32_t dk_home = id("e3_e09_dock_home");
            const uint32_t hp = id("e3_e09_home_pane");
            const uint32_t hr = dock_reset_root(dk_home);
            check(hr != 0u && dock_add_pane(dk_home, hr, {.id = hp, .title = str_lit("Home")}), __LINE__);
            check(dock_redock_window_panes(nullptr, dk_src, dk_home, hp, DockZone::Right, nullptr) == 2u, __LINE__);
            check(dock_find_pane_leaf(dk_src, jp1) == 0u && dock_find_pane_leaf(dk_src, jp2) == 0u &&
                  dock_find_pane_leaf(dk_src, jp3) != 0u &&
                  dock_find_pane_leaf(dk_home, jp1) != 0u && dock_find_pane_leaf(dk_home, jp2) != 0u, __LINE__);
        }

        // E10: table frozen columns & column order permutation
        {
            const uint32_t tid = id("e3_e10_table");
            const uint32_t c0_id = id("e3_e10_c0");
            const uint32_t c1_id = id("e3_e10_c1");
            const uint32_t c2_id = id("e3_e10_c2");
            const TableColumnDesc cols[3] = {
                {.id = c0_id, .title = str_lit("Col0"), .sizing = TableColumnSizing::Fixed, .width = 120.0f},
                {.id = c1_id, .title = str_lit("Col1"), .sizing = TableColumnSizing::Fixed, .width = 140.0f},
                {.id = c2_id, .title = str_lit("Col2"), .sizing = TableColumnSizing::Fixed, .width = 160.0f},
            };

            check(table_frozen_columns(tid) == 0u, __LINE__);
            table_set_frozen_columns(tid, 99u);
            check(table_frozen_columns(tid) == kMaxTableColumns, __LINE__);
            table_set_frozen_columns(tid, 1u);
            check(table_frozen_columns(tid) == 1u, __LINE__);

            const uint32_t bad_dup[3] = {0u, 0u, 1u};
            const uint32_t bad_oor[3] = {0u, 3u, 1u};
            const uint32_t good_order[3] = {2u, 0u, 1u};
            check(!table_set_column_order(tid, bad_dup), __LINE__);
            check(!table_set_column_order(tid, bad_oor), __LINE__);
            check(table_set_column_order(tid, good_order), __LINE__);
            uint32_t got_order[3] = {};
            check(table_get_column_order(tid, got_order, 3u) == 3u &&
                  got_order[0] == 2u && got_order[1] == 0u && got_order[2] == 1u, __LINE__);

            const auto run_tbl = [&]() noexcept {
                begin_frame(test, {.viewport_w = 500, .viewport_h = 260});
                (void)begin_table({
                    .id = tid, .columns = cols, .item_count = 3u,
                    .width = Sizing::fixed(260.0f), .height = Sizing::fixed(180.0f),
                    .horizontal_scroll = true
                });
                uint32_t row = 0u;
                while (table_next_row(&row)) {
                    while (table_next_column()) {
                        text(str_lit("cell"), {.font_size_px = 12.0f});
                    }
                }
                end_table();
                end_frame(test);
            };

            run_tbl();
            const uint32_t h0 = id_index(c0_id, 0x7601u);
            const uint32_t h1 = id_index(c1_id, 0x7601u);
            const uint32_t h2 = id_index(c2_id, 0x7601u);
            const uint32_t row0_id = id_index(tid, 0x100000u);
            const uint32_t cell_frozen_id = id_index(row0_id, 2u);
            const uint32_t cell_scroll_id = id_index(row0_id, 0u);
            // Visual order is {2, 0, 1}: Col2 leftmost, then Col0, then Col1
            check(element_bounds(h2).x < element_bounds(h0).x &&
                  element_bounds(h0).x < element_bounds(h1).x, __LINE__);
            ElementGeometry g_frozen_0 = {}, g_scroll_0 = {}, g_cf_0 = {}, g_cs_0 = {};
            check(element_geometry(h2, &g_frozen_0) && element_geometry(h0, &g_scroll_0) &&
                  element_geometry(cell_frozen_id, &g_cf_0) && element_geometry(cell_scroll_id, &g_cs_0), __LINE__);

            const uint32_t outer_id = id_index(tid, 0x7400u);
            set_scroll_offset(outer_id, {80.0f, 0.0f});
            run_tbl();
            ElementGeometry g_frozen_1 = {}, g_scroll_1 = {}, g_cf_1 = {}, g_cs_1 = {};
            check(element_geometry(h2, &g_frozen_1) && element_geometry(h0, &g_scroll_1) &&
                  element_geometry(cell_frozen_id, &g_cf_1) && element_geometry(cell_scroll_id, &g_cs_1), __LINE__);
            check(std::fabs(g_frozen_1.bounds.x - g_frozen_0.bounds.x) < 1.0f &&
                  g_scroll_1.bounds.x < g_scroll_0.bounds.x - 40.0f &&
                  std::fabs(g_cf_1.bounds.x - g_cf_0.bounds.x) < 1.0f &&
                  g_cs_1.bounds.x < g_cs_0.bounds.x - 40.0f, __LINE__);
        }

        // E12: scoped commands, IME composition suppression, two-step chord & shortcut rebinding
        {
            const uint32_t scope_a = id("e3_e12_scope_a");
            check(register_command(test, {
                .id = 201u, .name = str_lit("e12.global"), .title = str_lit("Global D"),
                .shortcut_mods = Mod_Ctrl, .shortcut_keycode = SDLK_D, .allow_when_typing = true, .scope_id = 0u
            }), __LINE__);
            check(register_command(test, {
                .id = 202u, .name = str_lit("e12.scoped"), .title = str_lit("Scoped D"),
                .shortcut_mods = Mod_Ctrl, .shortcut_keycode = SDLK_D, .allow_when_typing = true, .scope_id = scope_a
            }), __LINE__);
            check(register_command(test, {
                .id = 203u, .name = str_lit("e12.chord"), .title = str_lit("Chord KC"),
                .shortcut_mods = Mod_Ctrl, .shortcut_keycode = SDLK_K, .allow_when_typing = true,
                .chord_mods = Mod_Ctrl, .chord_keycode = SDLK_C
            }), __LINE__);

            CommandDesc cinfo = {};
            check(get_command(test, 1u, &cinfo) && cinfo.scope_id == scope_a, __LINE__);
            check(get_command(test, 2u, &cinfo) && cinfo.chord_mods == Mod_Ctrl && cinfo.chord_keycode == SDLK_C, __LINE__);

            // Outside scope_a: global command triggers
            begin_frame(test, {.viewport_w = 320, .viewport_h = 160, .key_mods = Mod_Ctrl, .key_pressed_code = SDLK_D});
            check(poll_triggered_command(test) == 201u, __LINE__);
            end_frame(test);

            // Inside scope_a: scoped command overrides global on equal shortcut
            begin_frame(test, {.viewport_w = 320, .viewport_h = 160, .key_mods = Mod_Ctrl, .key_pressed_code = SDLK_D});
            begin_focus_scope(scope_a, false);
            check(poll_triggered_command(test) == 202u, __LINE__);
            end_focus_scope();
            end_frame(test);

            // IME composition active: suppress command shortcuts
            SDL_Event ime_on = {};
            ime_on.type = SDL_EVENT_TEXT_EDITING;
            ime_on.edit.text = "ni";
            ime_on.edit.start = 0;
            ime_on.edit.length = 2;
            process_event(test, ime_on);
            begin_frame(test, {.viewport_w = 320, .viewport_h = 160, .key_mods = Mod_Ctrl, .key_pressed_code = SDLK_D});
            check(poll_triggered_command(test) == 0u, __LINE__);
            end_frame(test);
            SDL_Event ime_off = {};
            ime_off.type = SDL_EVENT_TEXT_EDITING;
            ime_off.edit.text = "";
            process_event(test, ime_off);

            // Two-step key chord: Ctrl+K arms, Ctrl+C triggers
            begin_frame(test, {.viewport_w = 320, .viewport_h = 160, .key_mods = Mod_Ctrl, .key_pressed_code = SDLK_K});
            check(poll_triggered_command(test) == 0u, __LINE__);
            end_frame(test);
            begin_frame(test, {.viewport_w = 320, .viewport_h = 160, .key_mods = Mod_Ctrl, .key_pressed_code = SDLK_C});
            check(poll_triggered_command(test) == 203u, __LINE__);
            end_frame(test);

            // Rebind shortcut to single-key Ctrl+Shift+B and trigger
            check(rebind_command_shortcut(test, 203u, static_cast<uint16_t>(Mod_Ctrl | Mod_Shift), SDLK_B, 0u, 0u), __LINE__);
            check(get_command(test, 2u, &cinfo) && cinfo.shortcut_keycode == SDLK_B && cinfo.chord_keycode == 0u, __LINE__);
            begin_frame(test, {.viewport_w = 320, .viewport_h = 160,
                               .key_mods = static_cast<uint16_t>(Mod_Ctrl | Mod_Shift), .key_pressed_code = SDLK_B});
            check(poll_triggered_command(test) == 203u, __LINE__);
            end_frame(test);

            check(unregister_command(test, 201u) && unregister_command(test, 202u) &&
                  unregister_command(test, 203u) && command_count(test) == 0u, __LINE__);
        }

        // E17: automation typed_utf8 capture, deterministic replay & JSON failure report export
        {
            const uint32_t rep_in = id("e3_e17_input");
            const uint32_t rep_btn = id("e3_e17_btn");
            char live_buf[64] = {};
            char replay_buf[64] = {};
            begin_frame(test, {.viewport_w = 480, .viewport_h = 200});
            text_input({.id = rep_in, .buffer = live_buf, .capacity = sizeof(live_buf)});
            button({.id = rep_btn, .label = str_lit("ReplayBtn")});
            end_frame(test);
            request_focus(rep_in);

            automation_clear_recording(test);
            automation_set_recording(test, true);
            SDL_Event replay_tev = {};
            replay_tev.type = SDL_EVENT_TEXT_INPUT;
            replay_tev.text.text = "Hi!";
            process_event(test, replay_tev);
            begin_frame(test, {.viewport_w = 480, .viewport_h = 200, .dt_seconds = 1.0f / 60.0f,
                               .pointer_pos = {42.0f, 24.0f}, .pointer_down = false});
            text_input({.id = rep_in, .buffer = live_buf, .capacity = sizeof(live_buf)});
            button({.id = rep_btn, .label = str_lit("ReplayBtn")});
            end_frame(test);

            RecordedFrameInput r0 = {};
            check(automation_recorded_count(test) == 1u &&
                  automation_get_recorded(test, 0u, &r0) &&
                  r0.typed_utf8_len == 3u && std::memcmp(r0.typed_utf8, "Hi!", 3u) == 0, __LINE__);

            // Replay while recording is still enabled must NOT self-append to the recording ring
            request_focus(rep_in);
            check(automation_replay_frame(test, 0u, 480u, 200u, 1.0f), __LINE__);
            text_input({.id = rep_in, .buffer = replay_buf, .capacity = sizeof(replay_buf)});
            button({.id = rep_btn, .label = str_lit("ReplayBtn")});
            end_frame(test);
            check(std::strcmp(replay_buf, "Hi!") == 0 && automation_recorded_count(test) == 1u, __LINE__);

            const uint32_t rep_len = automation_export_failure_report_json(test, nullptr, 0u);
            char report_json[4096] = {};
            check(rep_len > 32u && rep_len < sizeof(report_json), __LINE__);
            check(automation_export_failure_report_json(test, report_json, sizeof(report_json)) == rep_len &&
                  report_json[rep_len] == '\0' &&
                  std::strstr(report_json, "\"recorded_frames\"") != nullptr &&
                  std::strstr(report_json, "\"a11y_nodes\"") != nullptr &&
                  std::strstr(report_json, "ReplayBtn") != nullptr &&
                  std::strstr(report_json, "Hi!") != nullptr, __LINE__);

            automation_set_recording(test, false);
            automation_clear_recording(test);
        }

        // E18: ADE primitives — terminal_grid_ex, code_editor ViewZones/InlayHints/ScrollSync,
        // TextInput auto_grow + caret_rect, hierarchical command scopes, focus restore, fuzzy_match, dock pinned/badge.
        {
            // 1. terminal_grid_ex: rich styles, selection, hyperlink hit-testing, auto_fit_container, IME rect
            TerminalCell tcells[16] = {};
            for (uint32_t i = 0u; i < 16u; ++i) {
                tcells[i].codepoint = 'A' + (i % 8u);
                tcells[i].width = 1u;
            }
            tcells[1].italic = true;
            tcells[2].dim = true;
            tcells[3].strikethrough = true;
            tcells[4].underline_style = TerminalUnderlineStyle::Curly;
            tcells[4].underline_color = {TerminalColorMode::TrueColor, 0xFF4444u};
            tcells[5].underline_style = TerminalUnderlineStyle::Double;
            tcells[6].underline_style = TerminalUnderlineStyle::Dotted;
            tcells[2].hyperlink_id = 77u;
            tcells[3].hyperlink_id = 77u;

            const uint32_t tid = id("e18_term_ex");
            TerminalGridResult tres = {};
            // Frame 1: establish layout bounds
            begin_frame(test, {.viewport_w = 400, .viewport_h = 200});
            begin_box({.width = Sizing::fixed(180.0f), .height = Sizing::fixed(80.0f)});
            tres = terminal_grid_ex({
                .id = tid, .cells = tcells, .columns = 8u, .rows = 2u,
                .cell_width_px = 10.0f, .line_height_px = 20.0f, .font_size_px = 13.0f,
                .cursor_column = 1, .cursor_row = 0,
                .cursor_shape = TerminalCursorShape::Block, .cursor_visible = true,
                .focusable = true, .auto_fit_container = true,
                .selection_start_col = 0, .selection_start_row = 0,
                .selection_end_col = 3, .selection_end_row = 0,
            });
            end_box();
            end_frame(test);

            // Frame 2: hover & click cell (2, 0) where hyperlink_id == 77
            const Rect tb_rect = element_bounds(tid);
            const Vec2 link_pt = {tb_rect.x + 25.0f, tb_rect.y + 10.0f};
            begin_frame(test, {.viewport_w = 400, .viewport_h = 200, .pointer_pos = link_pt, .pointer_down = true});
            begin_box({.width = Sizing::fixed(180.0f), .height = Sizing::fixed(80.0f)});
            terminal_grid_ex({
                .id = tid, .cells = tcells, .columns = 8u, .rows = 2u,
                .cell_width_px = 10.0f, .line_height_px = 20.0f, .font_size_px = 13.0f,
                .cursor_column = 1, .cursor_row = 0,
                .cursor_shape = TerminalCursorShape::Bar, .cursor_visible = true,
                .focusable = true, .auto_fit_container = true,
            });
            end_box();
            end_frame(test);
            begin_frame(test, {.viewport_w = 400, .viewport_h = 200, .pointer_pos = link_pt, .pointer_down = false});
            begin_box({.width = Sizing::fixed(180.0f), .height = Sizing::fixed(80.0f)});
            tres = terminal_grid_ex({
                .id = tid, .cells = tcells, .columns = 8u, .rows = 2u,
                .cell_width_px = 10.0f, .line_height_px = 20.0f, .font_size_px = 13.0f,
                .cursor_column = 1, .cursor_row = 0,
                .cursor_shape = TerminalCursorShape::Block, .cursor_visible = true,
                .focusable = true, .auto_fit_container = true,
            });
            end_box();
            end_frame(test);
            check(tres.visible_columns == 18u && tres.visible_rows == 4u &&
                  tres.hovered && tres.hovered_col == 2 && tres.hovered_row == 0 &&
                  tres.hovered_hyperlink_id == 77u &&
                  tres.clicked && tres.clicked_col == 2 && tres.clicked_row == 0 &&
                  tres.clicked_hyperlink_id == 77u &&
                  tres.focused && tres.ime_cursor_rect.w > 0.0f, __LINE__);

            // 2. code_editor ViewZones + InlayHints + ScrollSync
            TextBuffer vz_buf = {};
            check(init_text_buffer(&vz_buf, str_lit("line0\nline1\nline2\nline3\nline4\nline5\nline6\nline7\nline8\nline9\nline10\nline11")), __LINE__);
            const uint32_t vz_ed = id("e18_vz_editor");
            const uint32_t vz_ed2 = id("e18_vz_editor2");
            const CodeViewZone zones[1] = {{.zone_id = 9001u, .after_line = 1u, .height_px = 64.0f, .above_line = false, .fill_color = Color::rgba8(40, 80, 140, 32)}};
            const CodeInlayHint hints[1] = {{.line_index = 0u, .col_byte = UINT32_MAX, .text = str_lit(": i32"), .background = Color::rgba8(60, 70, 90, 80)}};
            check(code_editor_set_view_zones(vz_ed, zones), __LINE__);
            check(code_editor_set_inlay_hints(vz_ed, hints), __LINE__);
            for (int f = 0; f < 2; ++f) {
                begin_frame(test, {.viewport_w = 600, .viewport_h = 260});
                code_editor({.id = vz_ed, .buffer = &vz_buf, .width = Sizing::fixed(420), .height = Sizing::fixed(180)});
                if (code_editor_begin_view_zone(vz_ed, 9001u)) {
                    button({.id = id("e18_vz_btn"), .label = str_lit("Reply")});
                    code_editor_end_view_zone();
                }
                code_editor({.id = vz_ed2, .buffer = &vz_buf, .width = Sizing::fixed(160), .height = Sizing::fixed(180)});
                end_frame(test);
            }
            Rect zrect = {}, zclip = {};
            check(code_editor_get_view_zone_bounds(vz_ed, 9001u, &zrect, &zclip) &&
                  zrect.h == 64.0f && zrect.w > 100.0f && zclip.h > 100.0f, __LINE__);
            // Scroll sync across two editors
            code_editor_set_scroll_offset(vz_ed, {0.0f, 40.0f});
            const Vec2 so1 = code_editor_scroll_offset(vz_ed);
            code_editor_set_scroll_offset(vz_ed2, so1);
            const Vec2 so2 = code_editor_scroll_offset(vz_ed2);
            check(so1.y >= 39.5f && std::fabs(so2.y - so1.y) < 0.5f, __LINE__);
            code_editor_clear_view_zones(vz_ed);
            code_editor_clear_inlay_hints(vz_ed);
            free_text_buffer(&vz_buf);

            // 3. TextInput auto_grow + caret_rect
            char ag_buf[256] = "short";
            const uint32_t ag_id = id("e18_autogrow");
            begin_frame(test, {.viewport_w = 480, .viewport_h = 300});
            text_input({.id = ag_id, .buffer = ag_buf, .capacity = sizeof(ag_buf), .width = Sizing::fixed(240),
                        .multiline = true, .word_wrap = true, .auto_grow = true, .min_height_px = 36.0f, .max_height_px = 120.0f});
            end_frame(test);
            const float h_short = element_bounds(ag_id).h;
            std::strcpy(ag_buf, "line1\nline2\nline3\nline4\nline5\nline6\nline7\nline8");
            request_focus(ag_id);
            begin_frame(test, {.viewport_w = 480, .viewport_h = 300});
            const TextInputResult ag_r = text_input({
                .id = ag_id, .buffer = ag_buf, .capacity = sizeof(ag_buf), .width = Sizing::fixed(240),
                .multiline = true, .word_wrap = true, .auto_grow = true, .min_height_px = 36.0f, .max_height_px = 120.0f});
            end_frame(test);
            const float h_tall = element_bounds(ag_id).h;
            check(h_short >= 35.5f && h_short < 60.0f && h_tall > h_short && h_tall <= 120.5f &&
                  ag_r.focused && ag_r.caret_rect.h > 0.0f, __LINE__);

            // 4. Hierarchical command scopes + focus restore stack + fuzzy_match
            const uint32_t outer_scope = id("e18_outer_scope");
            const uint32_t inner_scope = id("e18_inner_scope");
            check(register_command(test, {.id = 801u, .name = str_lit("global.k"), .shortcut_mods = Mod_Ctrl, .shortcut_keycode = SDLK_K, .scope_id = 0u}), __LINE__);
            check(register_command(test, {.id = 802u, .name = str_lit("outer.k"), .shortcut_mods = Mod_Ctrl, .shortcut_keycode = SDLK_K, .scope_id = outer_scope}), __LINE__);
            check(register_command(test, {.id = 803u, .name = str_lit("inner.k"), .shortcut_mods = Mod_Ctrl, .shortcut_keycode = SDLK_K, .scope_id = inner_scope}), __LINE__);
            request_focus(0u);
            begin_frame(test, {.viewport_w = 320, .viewport_h = 160, .key_mods = Mod_Ctrl, .key_pressed_code = SDLK_K});
            push_command_scope(outer_scope);
            push_command_scope(inner_scope);
            check(poll_triggered_command(test) == 803u, __LINE__);
            pop_command_scope();
            pop_command_scope();
            end_frame(test);
            begin_frame(test, {.viewport_w = 320, .viewport_h = 160, .key_mods = Mod_Ctrl, .key_pressed_code = SDLK_K});
            push_command_scope(outer_scope);
            check(poll_triggered_command(test) == 802u, __LINE__);
            pop_command_scope();
            end_frame(test);
            check(unregister_command(test, 801u) && unregister_command(test, 802u) && unregister_command(test, 803u), __LINE__);

            request_focus(ag_id);
            check(push_focus_restore_point(test), __LINE__);
            request_focus(tid);
            check(focused_id() == tid, __LINE__);
            check(pop_focus_restore_point(test) == ag_id && focused_id() == ag_id, __LINE__);

            uint32_t fpos[8] = {};
            const FuzzyMatchResult fm_good = fuzzy_match(str_lit("gws"), str_lit("git_worktree_sync"), fpos, 8u);
            const FuzzyMatchResult fm_weak = fuzzy_match(str_lit("gws"), str_lit("agxwxsz"), nullptr, 0u);
            const FuzzyMatchResult fm_miss = fuzzy_match(str_lit("xyz"), str_lit("git_worktree_sync"), nullptr, 0u);
            check(fm_good.matched && fm_good.match_count == 3u && fpos[0] == 0u && fpos[1] == 4u && fpos[2] == 13u &&
                  fm_weak.matched && fm_good.score > fm_weak.score && !fm_miss.matched, __LINE__);

            // 5. DockPane pinned / dirty / badge / focused_leaf
            const uint32_t dk = id("e18_dock");
            const uint32_t leaf0 = dock_reset_root(dk);
            check(dock_add_pane(dk, leaf0, {.id = id("e18_p1"), .title = str_lit("Pinned"), .closable = true, .pinned = true, .dirty = true, .badge_color = Color::rgb_hex(0x22C55E)}), __LINE__);
            check(dock_is_pane_pinned(dk, id("e18_p1")), __LINE__);
            dock_set_pane_pinned(dk, id("e18_p1"), false);
            check(!dock_is_pane_pinned(dk, id("e18_p1")), __LINE__);
            dock_set_pane_dirty(dk, id("e18_p1"), false);
            dock_set_pane_badge(dk, id("e18_p1"), Color::rgb_hex(0xF59E0B));
            begin_frame(test, {.viewport_w = 480, .viewport_h = 240});
            begin_dock_space({.id = dk, .width = Sizing::fixed(480), .height = Sizing::fixed(240)});
            while (next_dock_pane() != 0u) {
                text(str_lit("pane"));
            }
            end_dock_space();
            end_frame(test);
            check(dock_focused_leaf(dk) == leaf0 && !dock_pane_right_clicked(dk, id("e18_p1")), __LINE__);

            // 6. Round 2: CodeEditor inline highlights, overview ruler, sticky header, auto_height, gutter drag range
            {
                TextBuffer dbuf = {};
                check(init_text_buffer(&dbuf, str_lit("fn alpha() {\n    let old_val = 1;\n    let new_val = 2;\n    return new_val;\n}\n")), __LINE__);
                const uint32_t ded = id("e19_diff_ed");
                const CodeInlineHighlight ihs[1] = {{
                    .line_index = 1u, .start_col_byte = 8u, .end_col_byte = 15u,
                    .background = Color::rgba8(239, 68, 68, 90), .border_color = Color::rgba8(239, 68, 68, 160), .radius_px = 3.0f
                }};
                const CodeOverviewMarker mks[2] = {
                    {.line_index = 1u, .color = Color::rgb_hex(0xEF4444), .lane = 1u},
                    {.line_index = 2u, .color = Color::rgb_hex(0x22C55E), .lane = 2u},
                };
                check(code_editor_set_inline_highlights(ded, ihs), __LINE__);
                check(code_editor_set_overview_markers(ded, mks), __LINE__);
                CodeEditorResult dr = {};
                // Prime bounds & test auto_height
                begin_frame(test, {.viewport_w = 500, .viewport_h = 300});
                dr = code_editor({.id = ded, .buffer = &dbuf, .width = Sizing::fixed(440), .height = Sizing::fixed(80),
                    .auto_height = true, .show_overview_ruler = true});
                end_frame(test);
                const float auto_h = element_bounds(ded).h;
                check(auto_h > 100.0f && dr.end_visible_line == dr.total_lines, __LINE__);

                // Switch to fixed height + sticky header + gutter range drag (line 1 -> line 3)
                const auto gdrag = [&](Vec2 p, bool down) noexcept {
                    begin_frame(test, {.viewport_w = 500, .viewport_h = 300, .pointer_pos = p, .pointer_down = down});
                    dr = code_editor({.id = ded, .buffer = &dbuf, .width = Sizing::fixed(440), .height = Sizing::fixed(180),
                        .line_height_px = 20.0f, .show_overview_ruler = true, .show_sticky_header = true,
                        .sticky_header_text = str_lit("fn alpha()")});
                    end_frame(test);
                };
                gdrag({-1.0f, -1.0f}, false);
                gdrag({18.0f, 36.0f}, true);   // press gutter line 1 (pad.top=8 + 1*20 = 28..48)
                check(dr.clicked_gutter_line == 1 && dr.gutter_dragging && dr.gutter_drag_start_line == 1 && dr.gutter_drag_end_line == 1, __LINE__);
                gdrag({18.0f, 76.0f}, true);   // drag to line 3 (pad.top=8 + 3*20 = 68..88)
                check(dr.gutter_dragging && dr.gutter_drag_start_line == 1 && dr.gutter_drag_end_line == 3, __LINE__);
                gdrag({18.0f, 76.0f}, false);  // release commits range
                check(!dr.gutter_dragging && dr.gutter_drag_start_line == 1 && dr.gutter_drag_end_line == 3, __LINE__);
                code_editor_clear_inline_highlights(ded);
                code_editor_clear_overview_markers(ded);
                free_text_buffer(&dbuf);
            }

            // 7. Round 2: TerminalGrid search matches, vector box-drawing, interactive drag/word/line selection, wheel_delta_y
            {
                TerminalCell bcells[24] = {};
                const char* row0 = "hello world ";
                for (uint32_t i = 0u; i < 12u; ++i) bcells[i].codepoint = static_cast<uint32_t>(row0[i]);
                bcells[12].codepoint = 0x250Cu; // ┌
                bcells[13].codepoint = 0x2500u; // ─
                bcells[14].codepoint = 0x2510u; // ┐
                bcells[15].codepoint = 0x2588u; // █
                bcells[16].codepoint = 0x2592u; // ▒
                for (uint32_t i = 17u; i < 24u; ++i) bcells[i].codepoint = ' ';
                const TerminalSearchMatch sm[1] = {{.row = 0u, .start_col = 6u, .end_col = 11u, .active = true}};
                const uint32_t btid = id("e19_box_term");
                TerminalGridResult btr = {};
                const auto tstep = [&](Vec2 p, bool down, uint8_t clicks = 1u, float wy = 0.0f) noexcept {
                    begin_frame(test, {.viewport_w = 320, .viewport_h = 120, .pointer_pos = p, .pointer_down = down,
                        .scroll_delta = {0.0f, wy}, .pointer_clicks = clicks});
                    btr = terminal_grid_ex({
                        .id = btid, .cells = bcells, .columns = 12, .rows = 2,
                        .cell_width_px = 10.0f, .line_height_px = 20.0f, .font_size_px = 14.0f,
                        .focusable = true, .search_matches = sm, .interactive_selection = true,
                    });
                    end_frame(test);
                };
                tstep({-1.0f, -1.0f}, false);
                tstep({15.0f, 10.0f}, true);     // press col 1 row 0
                tstep({45.0f, 10.0f}, true);     // drag to col 4 row 0
                check(btr.has_selection && btr.selection_changed && btr.sel_start_col == 1 && btr.sel_end_col == 4, __LINE__);
                tstep({45.0f, 10.0f}, false, 1u, -2.0f);
                check(btr.has_selection && std::fabs(btr.wheel_delta_y + 2.0f) < 0.01f, __LINE__);
                tstep({75.0f, 10.0f}, true, 2u); // double click inside "world" (cols 6..10)
                check(btr.has_selection && btr.sel_start_col == 6 && btr.sel_end_col == 10, __LINE__);
                tstep({75.0f, 10.0f}, false);
                tstep({25.0f, 10.0f}, true, 3u); // triple click selects full row (cols 0..11)
                check(btr.has_selection && btr.sel_start_col == 0 && btr.sel_end_col == 11, __LINE__);
                tstep({25.0f, 10.0f}, false);
            }

            // 8. Round 2: TextInput atomic mention chip + pasted_image + global ui_zoom
            {
                char chip_buf[64] = "hi @main.cpp ok";
                const uint32_t chip_id = id("e19_chip_input");
                const TextInputHighlight chip_hl[1] = {{
                    .byte_offset = 3u, .byte_length = 9u,
                    .text_color = Color::rgb_hex(0x38BDF8), .background = Color::rgba8(56, 189, 248, 36),
                    .radius_px = 4.0f, .atomic = true
                }};
                TextInputResult cr = {};
                const auto cstep = [&](uint32_t key = 0u, uint16_t mods = 0u) noexcept {
                    begin_frame(test, {.viewport_w = 400, .viewport_h = 120, .key_mods = mods, .key_pressed_code = key});
                    cr = text_input({.id = chip_id, .buffer = chip_buf, .capacity = sizeof(chip_buf),
                        .width = Sizing::fixed(320), .highlights = chip_hl});
                    end_frame(test);
                };
                cstep();
                request_focus(chip_id);
                set_text_selection(chip_id, 12u, 12u); // right after "@main.cpp"
                cstep();
                cstep(SDLK_LEFT); // jump over entire atomic chip 12 -> 3
                check(cr.cursor_byte == 3u, __LINE__);
                cstep(SDLK_RIGHT); // jump over entire atomic chip 3 -> 12
                check(cr.cursor_byte == 12u, __LINE__);
                cstep(SDLK_BACKSPACE); // delete entire "@main.cpp" in 1 keypress
                check(cr.changed && std::strcmp(chip_buf, "hi  ok") == 0 && cr.cursor_byte == 3u, __LINE__);

                // Image paste signal when clipboard has image/png and no text
                (void)set_clipboard_text("");
                static const uint8_t kPngMagic[8] = {0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au};
                const MimeDragEntry img_mime[1] = {{.mime_type = str_lit("image/png"), .bytes = kPngMagic, .size = sizeof(kPngMagic)}};
                if (set_clipboard_mime(img_mime) && has_clipboard_mime(str_lit("image/png"))) {
                    cstep(SDLK_V, Mod_Ctrl);
                    check(cr.pasted_image, __LINE__);
                }

                // Global ui_zoom
                check(std::fabs(ui_zoom(test) - 1.0f) < 1e-4f, __LINE__);
                set_ui_zoom(test, 1.5f);
                check(std::fabs(ui_zoom(test) - 1.5f) < 1e-4f, __LINE__);
                begin_frame(test, {.viewport_w = 600, .viewport_h = 300, .dpi_scale = 1.0f});
                check(std::fabs(dpi_scale(test) - 1.5f) < 1e-4f, __LINE__);
                end_frame(test);
                set_ui_zoom(test, 99.0f);
                check(std::fabs(ui_zoom(test) - 3.0f) < 1e-4f, __LINE__);
                set_ui_zoom(test, 1.0f);
            }

            // 9. Round 3: Multi-language tokenizer, interactive multi-cursor, brackets, indent guides, diag tooltip,
            //    terminal graphemes/palette/input/VT encoding, frame key enumeration, desktop OS APIs.
            {
                SyntaxToken toks[64] = {};
                check(tokenize_language(str_lit("typescript"), 0u, str_lit("export async function run(x: number): Promise<void> { return; }"), toks, 64u) >= 5u, __LINE__);
                check(tokenize_language(str_lit("python"), 0u, str_lit("def greet(name): return 'hello' # hi"), toks, 64u) >= 4u, __LINE__);
                check(tokenize_language(str_lit("rust"), 0u, str_lit("pub fn main() -> Result<i32, ()> { Ok(42) }"), toks, 64u) >= 4u, __LINE__);
                check(tokenize_language(str_lit("go"), 0u, str_lit("func serve() { defer close(ch) }"), toks, 64u) >= 2u, __LINE__);
                check(tokenize_language(str_lit("json"), 0u, str_lit("{\"enabled\": true, \"count\": 12}"), toks, 64u) >= 4u, __LINE__);
                check(tokenize_language(str_lit("sql"), 0u, str_lit("SELECT id FROM users WHERE active = 1 -- note"), toks, 64u) >= 5u, __LINE__);
                check(tokenize_language(str_lit("diff"), 0u, str_lit("+added line"), toks, 64u) == 1u, __LINE__);
                check(tokenize_language(str_lit("diff"), 1u, str_lit("-removed line"), toks, 64u) == 1u, __LINE__);

                // Interactive multi-cursor: Ctrl+D select word -> Ctrl+D next match -> type -> Backspace -> Escape
                TextBuffer mcb = {};
                check(init_text_buffer(&mcb, str_lit("foo(1) + foo(2)\n    bar(foo)")), __LINE__);
                const uint32_t mcid = id("e20_mc_ed");
                const TextDiagnostic diag[1] = {{.byte_start = 0u, .byte_end = 3u, .severity = TextDiagnosticSeverity::Error, .message = str_lit("undefined foo")}};
                check(text_buffer_set_diagnostics(&mcb, text_buffer_revision(&mcb), diag), __LINE__);
                CodeEditorResult mcr = {};
                const auto mc_step = [&](uint32_t key = 0u, uint16_t mods = 0u, const char* typed = nullptr, Vec2 ptr = {-1.0f, -1.0f}) noexcept {
                    if (typed) {
                        SDL_Event tev = {};
                        tev.type = SDL_EVENT_TEXT_INPUT;
                        tev.text.text = typed;
                        process_event(test, tev);
                    }
                    begin_frame(test, {.viewport_w = 500, .viewport_h = 200, .pointer_pos = ptr, .key_mods = mods, .key_pressed_code = key});
                    mcr = code_editor({.id = mcid, .buffer = &mcb, .width = Sizing::fixed(460), .height = Sizing::fixed(160),
                        .language = str_lit("ts"), .highlight_matching_brackets = true,
                        .show_indent_guides = true, .show_diagnostic_tooltips = true});
                    end_frame(test);
                };
                mc_step();
                request_focus(mcid);
                code_editor_set_cursor(mcid, 1u, 1u, false); // inside first "foo"
                mc_step();
                mc_step(SDLK_D, Mod_Ctrl); // selects "foo" (0..3)
                check(mcr.selection_anchor_byte == 0u && mcr.cursor_byte == 3u && code_editor_cursor_count(mcid) == 1u, __LINE__);
                mc_step(SDLK_D, Mod_Ctrl); // adds second "foo" (9..12)
                check(code_editor_cursor_count(mcid) == 2u, __LINE__);
                mc_step(0u, 0u, "baz");    // replaces both "foo" with "baz"
                check(mcr.changed && code_editor_cursor_count(mcid) == 2u, __LINE__);
                char mc_out[64] = {};
                const uint32_t mclen = text_buffer_copy_range(&mcb, 0u, text_buffer_byte_length(&mcb), mc_out, sizeof(mc_out) - 1u);
                mc_out[mclen] = '\0';
                check(std::strncmp(mc_out, "baz(1) + baz(2)", 15u) == 0, __LINE__);
                mc_step(SDLK_BACKSPACE);   // deletes 'z' at both cursors -> "ba(1) + ba(2)"
                const uint32_t mclen2 = text_buffer_copy_range(&mcb, 0u, text_buffer_byte_length(&mcb), mc_out, sizeof(mc_out) - 1u);
                mc_out[mclen2] = '\0';
                check(std::strncmp(mc_out, "ba(1) + ba(2)", 13u) == 0, __LINE__);
                mc_step(SDLK_ESCAPE);      // collapses extra cursors back to 1
                check(code_editor_cursor_count(mcid) == 1u, __LINE__);
                // Bracket highlight check: cursor right after '(' (byte 3 in "ba(1) + ba(2)")
                code_editor_set_cursor(mcid, 3u, 3u, false);
                mc_step(0u, 0u, nullptr, {72.0f, 16.0f});
                check(frame_prims(test).size() > 0u, __LINE__);
                free_text_buffer(&mcb);

                // Terminal grapheme cluster, custom 16-color palette, focused key/paste/mouse capture & VT encoders
                char vt[32] = {};
                uint32_t vlen = terminal_encode_key(SDLK_UP, 0u, false, vt, sizeof(vt));
                check(vlen == 3u && std::memcmp(vt, "\x1b[A", 3u) == 0, __LINE__);
                vlen = terminal_encode_key(SDLK_UP, 0u, true, vt, sizeof(vt));
                check(vlen == 3u && std::memcmp(vt, "\x1bOA", 3u) == 0, __LINE__);
                vlen = terminal_encode_key(SDLK_C, Mod_Ctrl, false, vt, sizeof(vt));
                check(vlen == 1u && vt[0] == 3, __LINE__);
                vlen = terminal_encode_mouse_sgr(1u, 4, 2, 0u, true, false, vt, sizeof(vt));
                check(vlen > 0u && std::strcmp(vt, "\x1b[<0;5;3M") == 0, __LINE__);

                Color custom_pal[16] = {};
                for (uint32_t pi = 0u; pi < 16u; ++pi) custom_pal[pi] = Color::rgba8(uint8_t(10u + pi * 12u), 180u, 220u, 255u);
                static const char kCluster[] = "e\xCC\x81"; // combining acute accent grapheme cluster
                TerminalCell gcells[8] = {};
                gcells[0].codepoint = 'e';
                gcells[0].grapheme_utf8 = kCluster;
                gcells[0].grapheme_len = 3u;
                gcells[0].foreground = {TerminalColorMode::Ansi16, 2u};
                gcells[1].codepoint = 'X';
                const uint32_t gtid = id("e20_grapheme_term");
                TerminalGridResult gtr = {};
                begin_frame(test, {.viewport_w = 320, .viewport_h = 120});
                gtr = terminal_grid_ex({.id = gtid, .cells = gcells, .columns = 4u, .rows = 2u,
                    .cell_width_px = 10.0f, .line_height_px = 20.0f, .focusable = true,
                    .ansi_palette = custom_pal, .capture_keyboard_input = true, .bracketed_paste = true, .mouse_reporting = true});
                end_frame(test);
                request_focus(gtid);
                begin_frame(test, {.viewport_w = 320, .viewport_h = 120, .pointer_pos = {15.0f, 10.0f}, .pointer_down = true,
                    .key_mods = Mod_Ctrl, .key_pressed_code = SDLK_C});
                gtr = terminal_grid_ex({.id = gtid, .cells = gcells, .columns = 4u, .rows = 2u,
                    .cell_width_px = 10.0f, .line_height_px = 20.0f, .focusable = true,
                    .ansi_palette = custom_pal, .capture_keyboard_input = true, .bracketed_paste = true, .mouse_reporting = true});
                end_frame(test);
                check(gtr.focused && gtr.key_pressed == SDLK_C && (gtr.key_mods & Mod_Ctrl) != 0u &&
                      gtr.mouse_down && gtr.mouse_button == 1u, __LINE__);

                // Frame key enumeration API
                begin_frame(test, {.viewport_w = 200, .viewport_h = 100, .key_mods = Mod_Shift, .key_pressed_code = SDLK_P});
                FrameKeyEvent fk = {};
                check(frame_key_count() == 1u && frame_key_at(0u, &fk) &&
                      fk.keycode == SDLK_P && (fk.mods & Mod_Shift) != 0u && !fk.consumed, __LINE__);
                check(consume_frame_key_at(0u) && !consume_frame_key_at(0u), __LINE__);
                check(frame_key_at(0u, &fk) && fk.consumed, __LINE__);
                end_frame(test);

                // Desktop OS integration (headless-safe)
                check(!open_url(str_lit("javascript:alert(1)")), __LINE__);
                check(!open_url(str_lit("")), __LINE__);
                const uint32_t n0 = desktop_notification_count(test);
                check(notify_desktop(test, {.title = str_lit("Done"), .body = str_lit("Build finished"), .tag = 7u, .silent = true}), __LINE__);
                check(desktop_notification_count(test) == n0 + 1u, __LINE__);
                Tray* tray = create_tray(test, str_lit("Etch Test"));
                check(tray != nullptr, __LINE__);
                const TrayItemDesc titems[2] = {
                    {.id = 1u, .label = str_lit("Open")},
                    {.id = 2u, .label = str_lit("Quit")},
                };
                check(tray_set_tooltip(tray, str_lit("Etch Ready")) && tray_set_menu(tray, titems) &&
                      tray_poll_clicked_item(tray) == 0u, __LINE__);
                destroy_tray(tray);
                check(!flash_window(nullptr, WindowFlashKind::Brief), __LINE__);
                check(!set_window_progress(nullptr, WindowProgressState::Normal, 0.5f), __LINE__);
            }

            // 10. Round 4: Stateful multiline lexer, regex search/replace, atomic multi-cursor undo/redo,
            //     Alt+Up/Down, Ctrl+/, Alt+Shift+Drag box selection, minimap, auto sticky scroll,
            //     custom display line numbers, Braille/Powerline + auto-URL terminal, tray icon/submenu & native menu bar.
            {
                // 10a. Stateful multiline syntax tokenizer
                SyntaxToken stoks[32] = {};
                uint8_t lex_st = 0u;
                // C/TS multiline /* ... */
                check(tokenize_language_stateful(str_lit("ts"), str_lit("const a = 1; /* start"), 0u, &lex_st, stoks, 32u) >= 2u && lex_st == 1u, __LINE__);
                check(tokenize_language_stateful(str_lit("ts"), str_lit("still comment"), lex_st, &lex_st, stoks, 32u) == 1u && lex_st == 1u, __LINE__);
                check(tokenize_language_stateful(str_lit("ts"), str_lit("end */ const b = 2;"), lex_st, &lex_st, stoks, 32u) >= 2u && lex_st == 0u, __LINE__);
                // JS multiline template string
                check(tokenize_language_stateful(str_lit("js"), str_lit("const s = `hello"), 0u, &lex_st, stoks, 32u) >= 2u && lex_st == 2u, __LINE__);
                check(tokenize_language_stateful(str_lit("js"), str_lit("world`; const n = 7;"), lex_st, &lex_st, stoks, 32u) >= 2u && lex_st == 0u, __LINE__);
                // Python triple quotes
                check(tokenize_language_stateful(str_lit("py"), str_lit("doc = \"\"\"line1"), 0u, &lex_st, stoks, 32u) >= 1u && lex_st == 3u, __LINE__);
                check(tokenize_language_stateful(str_lit("py"), str_lit("line2\"\"\" + 1"), lex_st, &lex_st, stoks, 32u) >= 2u && lex_st == 0u, __LINE__);
                // Rust nested /* /* */ */
                check(tokenize_language_stateful(str_lit("rs"), str_lit("/* outer /* inner"), 0u, &lex_st, stoks, 32u) == 1u && lex_st == 6u, __LINE__);
                check(tokenize_language_stateful(str_lit("rs"), str_lit("close inner */ still outer"), lex_st, &lex_st, stoks, 32u) == 1u && lex_st == 5u, __LINE__);
                check(tokenize_language_stateful(str_lit("rs"), str_lit("close outer */ fn ok() {}"), lex_st, &lex_st, stoks, 32u) >= 2u && lex_st == 0u, __LINE__);

                // 10b. Regex search & replace_all on TextBuffer
                TextBuffer rbuf = {};
                check(init_text_buffer(&rbuf, str_lit("item_12 = foo;\nitem_345 = bar;\nother = baz;\n")), __LINE__);
                TextSearchMatch rm[8] = {};
                const TextSearchOptions re_opts = {.case_sensitive = true, .whole_word = false, .regex = true};
                check(text_buffer_find_all_ex(&rbuf, str_lit("item_\\d+"), re_opts, rm, 8u) == 2u &&
                      rm[0].byte_start == 0u && rm[0].byte_end == 7u &&
                      rm[1].byte_start == 15u && rm[1].byte_end == 23u, __LINE__);
                check(text_buffer_find_all_ex(&rbuf, str_lit("(foo|bar)"), re_opts, rm, 8u) == 2u, __LINE__);
                const uint32_t rep_n = text_buffer_replace_all(&rbuf, str_lit("item_\\d+"), str_lit("val"), re_opts);
                check(rep_n == 2u, __LINE__);
                char rstr[128] = {};
                uint32_t rlen = text_buffer_copy_range(&rbuf, 0u, text_buffer_byte_length(&rbuf), rstr, sizeof(rstr) - 1u);
                rstr[rlen] = '\0';
                check(std::strcmp(rstr, "val = foo;\nval = bar;\nother = baz;\n") == 0, __LINE__);
                check(text_buffer_undo(&rbuf), __LINE__);
                rlen = text_buffer_copy_range(&rbuf, 0u, text_buffer_byte_length(&rbuf), rstr, sizeof(rstr) - 1u);
                rstr[rlen] = '\0';
                check(std::strcmp(rstr, "item_12 = foo;\nitem_345 = bar;\nother = baz;\n") == 0, __LINE__);
                free_text_buffer(&rbuf);

                // 10c. Atomic multi-cursor undo/redo + Alt+Up/Down + Ctrl+/ + Alt+Shift+Drag box selection
                TextBuffer r4b = {};
                check(init_text_buffer(&r4b, str_lit("alpha\nbeta\ngamma\ndelta")), __LINE__);
                const uint32_t r4ed = id("e21_r4_ed");
                CodeEditorResult r4r = {};
                const auto r4_step = [&](uint32_t key = 0u, uint16_t mods = 0u, const char* typed = nullptr,
                                         Vec2 ptr = {-1.0f, -1.0f}, bool down = false, Str lang = str_lit("ts")) noexcept {
                    if (typed) {
                        SDL_Event tev = {};
                        tev.type = SDL_EVENT_TEXT_INPUT;
                        tev.text.text = typed;
                        process_event(test, tev);
                    }
                    begin_frame(test, {.viewport_w = 520, .viewport_h = 220, .pointer_pos = ptr, .pointer_down = down,
                                       .key_mods = mods, .key_pressed_code = key});
                    r4r = code_editor({.id = r4ed, .buffer = &r4b, .width = Sizing::fixed(480), .height = Sizing::fixed(180),
                                       .line_height_px = 20.0f, .show_sticky_header = true, .language = lang, .show_minimap = true});
                    end_frame(test);
                };
                auto read_r4 = [&]() -> Str {
                    static char buf_out[256];
                    const uint32_t n = text_buffer_copy_range(&r4b, 0u, text_buffer_byte_length(&r4b), buf_out, sizeof(buf_out) - 1u);
                    buf_out[n] = '\0';
                    return { buf_out, n };
                };
                r4_step();
                request_focus(r4ed);
                // Move line 1 ("beta") up -> "beta\nalpha\ngamma\ndelta"
                code_editor_set_cursor(r4ed, 7u, 7u, false);
                r4_step();
                r4_step(SDLK_UP, Mod_Alt);
                check(std::strcmp(read_r4().chars, "beta\nalpha\ngamma\ndelta") == 0, __LINE__);
                // Move it back down -> "alpha\nbeta\ngamma\ndelta"
                r4_step(SDLK_DOWN, Mod_Alt);
                check(std::strcmp(read_r4().chars, "alpha\nbeta\ngamma\ndelta") == 0, __LINE__);
                // Ctrl+/ toggle comment on line 1 (TS -> "// beta") then uncomment
                r4_step(SDLK_SLASH, Mod_Ctrl);
                check(std::strcmp(read_r4().chars, "alpha\n// beta\ngamma\ndelta") == 0, __LINE__);
                r4_step(SDLK_SLASH, Mod_Ctrl);
                check(std::strcmp(read_r4().chars, "alpha\nbeta\ngamma\ndelta") == 0, __LINE__);
                // Ctrl+/ in Python uses "# "
                r4_step(SDLK_SLASH, Mod_Ctrl, nullptr, {-1.0f, -1.0f}, false, str_lit("python"));
                check(std::strcmp(read_r4().chars, "alpha\n# beta\ngamma\ndelta") == 0, __LINE__);
                r4_step(SDLK_SLASH, Mod_Ctrl, nullptr, {-1.0f, -1.0f}, false, str_lit("python"));
                check(std::strcmp(read_r4().chars, "alpha\nbeta\ngamma\ndelta") == 0, __LINE__);
                // Multi-cursor edit + atomic Undo restores both text AND extra cursors!
                code_editor_set_cursor(r4ed, 0u, 0u, false);
                check(code_editor_add_cursor(r4ed, 6u), __LINE__);
                r4_step(0u, 0u, "X_");
                check(std::strcmp(read_r4().chars, "X_alpha\nX_beta\ngamma\ndelta") == 0 && code_editor_cursor_count(r4ed) == 2u, __LINE__);
                r4_step(SDLK_Z, Mod_Ctrl);
                check(std::strcmp(read_r4().chars, "alpha\nbeta\ngamma\ndelta") == 0 && code_editor_cursor_count(r4ed) == 2u, __LINE__);
                r4_step(SDLK_Z, static_cast<uint16_t>(Mod_Ctrl | Mod_Shift));
                check(std::strcmp(read_r4().chars, "X_alpha\nX_beta\ngamma\ndelta") == 0 && code_editor_cursor_count(r4ed) == 2u, __LINE__);
                r4_step(SDLK_Z, Mod_Ctrl);
                code_editor_clear_extra_cursors(r4ed);
                // Alt+Shift+Drag box selection across lines 0..2
                r4_step(0u, static_cast<uint16_t>(Mod_Alt | Mod_Shift), nullptr, {70.0f, 16.0f}, true);
                r4_step(0u, static_cast<uint16_t>(Mod_Alt | Mod_Shift), nullptr, {90.0f, 56.0f}, true);
                check(code_editor_cursor_count(r4ed) >= 3u, __LINE__);
                r4_step(0u, 0u, nullptr, {90.0f, 56.0f}, false);
                free_text_buffer(&r4b);

                // 10d. Minimap click-scroll + Auto Sticky Scroll + Custom Display Line Numbers
                TextBuffer mmb = {};
                check(init_text_buffer(&mmb, str_lit(
                    "function outerScope() {\n"
                    "    const a = 1;\n    const b = 2;\n    const c = 3;\n    const d = 4;\n"
                    "    const e = 5;\n    const f = 6;\n    const g = 7;\n    const h = 8;\n"
                    "    const i = 9;\n    const j = 10;\n    const k = 11;\n    const l = 12;\n"
                    "    return a + l;\n}\n")), __LINE__);
                const uint32_t mmed = id("e21_mm_ed");
                auto custom_line_dec = [](uint32_t li, CodeLineDecoration* out_dec, void*) noexcept -> bool {
                    if (li == 1u) { out_dec->hide_line_number = true; return true; }
                    out_dec->has_display_line_number = true;
                    out_dec->display_line_number = 100u + li;
                    return true;
                };
                begin_frame(test, {.viewport_w = 500, .viewport_h = 200});
                (void)code_editor({.id = mmed, .buffer = &mmb, .width = Sizing::fixed(460), .height = Sizing::fixed(120),
                                   .line_height_px = 20.0f, .line_decorator = custom_line_dec,
                                   .show_sticky_header = true, .language = str_lit("ts"), .show_minimap = true});
                end_frame(test);
                // Click lower half of minimap (right side, x ~ 460 - 14 - 36 = 410, y ~ 95) -> scrolls down
                begin_frame(test, {.viewport_w = 500, .viewport_h = 200, .pointer_pos = {410.0f, 95.0f}, .pointer_down = true});
                (void)code_editor({.id = mmed, .buffer = &mmb, .width = Sizing::fixed(460), .height = Sizing::fixed(120),
                                   .line_height_px = 20.0f, .line_decorator = custom_line_dec,
                                   .show_sticky_header = true, .language = str_lit("ts"), .show_minimap = true});
                end_frame(test);
                check(code_editor_scroll_offset(mmed).y > 10.0f, __LINE__);
                free_text_buffer(&mmb);

                // 10e. Terminal Braille + Powerline + Auto-URL hover/click detection
                TerminalCell ucells[32] = {};
                const char* urow = "go https://etch.dev/ui ok ";
                for (uint32_t k = 0u; k < 26u; ++k) ucells[k].codepoint = static_cast<uint32_t>(urow[k]);
                ucells[26].codepoint = 0x28FFu; // Braille full 8-dot
                ucells[27].codepoint = 0xE0B0u; // Powerline right triangle
                ucells[28].codepoint = 0xE0B1u; // Powerline right chevron
                ucells[29].codepoint = 0xE0B2u; // Powerline left triangle
                ucells[30].codepoint = 0xE0B4u; // Powerline right pill
                ucells[31].codepoint = 0xE0B6u; // Powerline left pill
                const uint32_t utid = id("e21_url_term");
                TerminalGridResult utr = {};
                begin_frame(test, {.viewport_w = 400, .viewport_h = 100});
                utr = terminal_grid_ex({.id = utid, .cells = ucells, .columns = 32u, .rows = 1u,
                                        .cell_width_px = 10.0f, .line_height_px = 20.0f});
                end_frame(test);
                const Vec2 url_pt = {65.0f, 10.0f}; // col 6 -> inside "https://etch.dev/ui"
                begin_frame(test, {.viewport_w = 400, .viewport_h = 100, .pointer_pos = url_pt, .pointer_down = true});
                utr = terminal_grid_ex({.id = utid, .cells = ucells, .columns = 32u, .rows = 1u,
                                        .cell_width_px = 10.0f, .line_height_px = 20.0f});
                end_frame(test);
                begin_frame(test, {.viewport_w = 400, .viewport_h = 100, .pointer_pos = url_pt, .pointer_down = false});
                utr = terminal_grid_ex({.id = utid, .cells = ucells, .columns = 32u, .rows = 1u,
                                        .cell_width_px = 10.0f, .line_height_px = 20.0f});
                end_frame(test);
                check(utr.hovered_url.length == 19u && std::memcmp(utr.hovered_url.chars, "https://etch.dev/ui", 19u) == 0 &&
                      utr.clicked_url.length == 19u && std::memcmp(utr.clicked_url.chars, "https://etch.dev/ui", 19u) == 0, __LINE__);

                // 10f. Tray custom RGBA icon + submenu + Native Menu Bar
                Tray* tray4 = create_tray(test, str_lit("Etch R4"));
                uint8_t icon_px[4u * 4u * 4u] = {};
                for (uint32_t pi = 0u; pi < sizeof(icon_px); ++pi) icon_px[pi] = 220u;
                const TrayItemDesc sub_items[2] = {
                    {.id = 21u, .label = str_lit("Sub A")},
                    {.id = 22u, .label = str_lit("Sub B"), .checked = true, .checkbox = true},
                };
                const TrayItemDesc top_items[2] = {
                    {.id = 10u, .label = str_lit("Recent"), .submenu = sub_items},
                    {.id = 11u, .label = str_lit("Quit")},
                };
                check(tray4 != nullptr &&
                      tray_set_icon_rgba(tray4, icon_px, 4u, 4u) &&
                      !tray_set_icon_rgba(tray4, nullptr, 4u, 4u) &&
                      tray_set_menu(tray4, top_items), __LINE__);
                destroy_tray(tray4);

                const MenuBarItemDesc file_children[2] = {
                    {.id = 101u, .label = str_lit("Open"), .shortcut = str_lit("Ctrl+O")},
                    {.id = 102u, .label = str_lit("Save"), .shortcut = str_lit("Ctrl+S")},
                };
                const MenuBarItemDesc menu_roots[1] = {
                    {.id = 100u, .label = str_lit("File"), .children = file_children},
                };
                check(set_native_menu_bar(test, nullptr, menu_roots) &&
                      poll_native_menu_item(test) == 0u &&
                      set_native_menu_bar(test, nullptr, {}), __LINE__);
            }
        }
    }

    destroy_context(test);set_current_context(restore);free_text_buffer(&b);return ok;
}
