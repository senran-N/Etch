// Shared sandbox [22], Studio [S18], and CTest semantic regression.
#include <cstdio>
#include <cstring>

static bool verify_a11y_checks(etch::Context* restore) noexcept {
    using namespace etch;
    Context* ctx=create_context({.headless=true});if(!ctx)return false;set_current_context(ctx);
    bool ok=true;const auto check=[&](bool pass,int line) noexcept {if(!pass){ok=false;std::fprintf(stderr,"v30 check line %d failed\n",line);}};
    const uint32_t group=id("a11y_group"),bid=id("a11y_button"),bid_after=id("a11y_btn_after"),sid=id("a11y_slider"),tid=id("a11y_text"),tid2=id("a11y_text2"),pid=id("a11y_password"),cid=id("a11y_combo"),mid=id("a11y_modal");
    float value=5;bool checked=false,modal=false;int hits=0,index=0;TextBuffer document={};check(init_text_buffer(&document,str_lit("first\nsecond")),__LINE__);char input[32]="initial",input2[32]="second",password[32]="a\xE4\xB8\xAD";
    const Str items[]={str_lit("one"),str_lit("two")};bool changed=false;
    const auto step=[&]() noexcept {
        begin_frame(ctx,{.viewport_w=640,.viewport_h=480});
        check(!frame_a11y_nodes(ctx).empty() || hits==0,__LINE__);
        begin_box({.id=group,.axis=Axis::Col,.width=Sizing::fixed(640),.height=Sizing::fixed(480)});
        char label[]="stack label";set_a11y_override({.element_id=bid,.label={label,11},.has_label=true});
        if(button({.id=bid,.label=str_lit("button")}))++hits;
        std::memset(label,'x',11);
        checkbox({.id=id("a11y_check"),.label=str_lit("check"),.checked=&checked});
        changed=slider({.id=sid,.value=&value,.min_value=0,.max_value=10,.step=1});
        text_input({.id=tid,.buffer=input,.capacity=32});text_input({.id=tid2,.buffer=input2,.capacity=32});text_input({.id=pid,.buffer=password,.capacity=32,.password=true});
        button({.id=bid_after,.label=str_lit("after")});
        code_editor({.id=id("a11y_editor"),.buffer=&document,.height=Sizing::fixed(40)});
        if(begin_collapsible({.id=id("a11y_collapse"),.title=str_lit("section"),.default_open=false}).visible)end_collapsible();
        if(begin_tree_node({.id=id("a11y_tree"),.label=str_lit("branch")}).open)end_tree_node();
        combo_box({.id=cid,.selected_index=&index,.items=items});
        char val[]="stack value";set_a11y_override({.element_id=sid,.value_text={val,11},.has_value_text=true});std::memset(val,'x',11);
        if(begin_modal({.id=mid,.open=&modal,.title=str_lit("dialog")}))end_modal();
        end_box();end_frame(ctx);
    };
    step();const auto nodes=frame_a11y_nodes(ctx);check(nodes.size()>8 && frame_stats(ctx).a11y_node_count==nodes.size(),__LINE__);
    for(uint32_t i=0;i<nodes.size();++i){const auto& n=nodes[i];uint32_t traversed=0;for(uint32_t c=n.first_child_idx;c!=0xFFFFFFFFu;c=nodes[c].next_sibling_idx){if(c>=nodes.size() || ++traversed>nodes.size()){check(false,__LINE__);break;}check(nodes[c].parent_id==n.id,__LINE__);}}
    const auto* b=find_a11y_node(bid,ctx);check(b && b->role==A11yRole::Button && b->bounds.w>0 && b->bounds.h>0 && b->label.length==11 && std::memcmp(b->label.chars,"stack label",11)==0,__LINE__);
    const auto* p=find_a11y_node(pid,ctx);check(p && p->value_text.length==6 && std::memcmp(p->value_text.chars,"\xE2\x80\xA2\xE2\x80\xA2",6)==0,__LINE__);
    const auto* s=find_a11y_node(sid,ctx);check(s && s->value_text.length==11 && std::memcmp(s->value_text.chars,"stack value",11)==0,__LINE__);
    check(perform_a11y_action(bid,A11yAction::Focus,0,{},ctx),__LINE__);step();check(focused(bid),__LINE__);
    check(perform_a11y_action(bid,A11yAction::Press,0,{},ctx),__LINE__);step();check(hits==1,__LINE__);
    check(perform_a11y_action(sid,A11yAction::Increment,0,{},ctx),__LINE__);step();check(value==6 && changed,__LINE__);
    check(perform_a11y_action(sid,A11yAction::Decrement,0,{},ctx),__LINE__);step();check(value==5 && changed,__LINE__);
    check(perform_a11y_action(sid,A11yAction::SetValue,100,{},ctx),__LINE__);step();check(value==10 && changed,__LINE__);
    check(perform_a11y_action(tid,A11yAction::SetValue,0,str_lit("replacement"),ctx),__LINE__);step();check(std::strcmp(input,"replacement")==0,__LINE__);
    check(perform_a11y_action(cid,A11yAction::Expand,0,{},ctx),__LINE__);step();check(is_popup_open(id_index(cid,0x2304)),__LINE__);
    check(perform_a11y_action(cid,A11yAction::Collapse,0,{},ctx),__LINE__);step();check(!is_popup_open(id_index(cid,0x2304)),__LINE__);
    check(perform_a11y_action(id("a11y_check"),A11yAction::Press,0,{},ctx),__LINE__);step();check(checked,__LINE__);
    check(perform_a11y_action(id("a11y_tree"),A11yAction::Expand,0,{},ctx),__LINE__);step();check(is_tree_open(id("a11y_tree")),__LINE__);
    check(perform_a11y_action(id("a11y_tree"),A11yAction::Press,0,{},ctx),__LINE__);step();check(!is_tree_open(id("a11y_tree")),__LINE__);
    check(perform_a11y_action(id("a11y_collapse"),A11yAction::Expand,0,{},ctx),__LINE__);step();check(is_collapsible_open(id("a11y_collapse")),__LINE__);
    check(perform_a11y_action(id("a11y_editor"),A11yAction::SetValue,0,str_lit("new document"),ctx),__LINE__);step();check(text_buffer_byte_length(&document)==12,__LINE__);
    check(perform_a11y_action(tid,A11yAction::Focus,0,{},ctx),__LINE__);step();check(focused(tid),__LINE__);
    {
        SDL_Event ev={};ev.type=SDL_EVENT_TEXT_INPUT;ev.text.text="Z";process_event(ctx,ev);
        check(perform_a11y_action(tid2,A11yAction::Focus,0,{},ctx),__LINE__);step();
        check(focused(tid2) && std::strcmp(input,"replacement")==0 && std::strcmp(input2,"secondZ")==0,__LINE__);
        ev.text.text="Q";process_event(ctx,ev);
        check(perform_a11y_action(bid_after,A11yAction::Focus,0,{},ctx),__LINE__);step();
        check(focused(bid_after) && std::strcmp(input2,"secondZ")==0,__LINE__);
        check(perform_a11y_action(id("a11y_editor"),A11yAction::Focus,0,{},ctx),__LINE__);step();
        check(perform_a11y_action(id("a11y_editor"),A11yAction::SetValue,0,str_lit("\r\nx"),ctx),__LINE__);
        SDL_Event home={};home.type=SDL_EVENT_KEY_DOWN;home.key.key=SDLK_HOME;home.key.mod=SDL_KMOD_SHIFT;process_event(ctx,home);
        SDL_Event copy={};copy.type=SDL_EVENT_KEY_DOWN;copy.key.key=SDLK_C;copy.key.mod=SDL_KMOD_LCTRL;process_event(ctx,copy);
        step();
        const Str ed_sel=selected_text(id("a11y_editor"));
        check(text_buffer_byte_length(&document)==2 && ed_sel.length==1 && ed_sel.chars && ed_sel.chars[0]=='x',__LINE__);
        check(perform_a11y_action(tid,A11yAction::Focus,0,{},ctx),__LINE__);step();
        SDL_Event pre={};pre.type=SDL_EVENT_TEXT_EDITING;pre.edit.text="pre";pre.edit.start=3;process_event(ctx,pre);
        check(perform_a11y_action(tid,A11yAction::SetValue,0,str_lit("undoable"),ctx),__LINE__);step();
        check(std::strcmp(input,"undoable")==0,__LINE__);
        SDL_Event bs={};bs.type=SDL_EVENT_KEY_DOWN;bs.key.key=SDLK_BACKSPACE;process_event(ctx,bs);step();
        check(std::strcmp(input,"undoabl")==0,__LINE__);
        SDL_Event undo={};undo.type=SDL_EVENT_KEY_DOWN;undo.key.key=SDLK_Z;undo.key.mod=SDL_KMOD_LCTRL;process_event(ctx,undo);step();
        check(std::strcmp(input,"undoable")==0,__LINE__);
        process_event(ctx,undo);step();
        check(std::strcmp(input,"replacement")==0,__LINE__);
        check(perform_a11y_action(id("a11y_editor"),A11yAction::Focus,0,{},ctx),__LINE__);step();
        process_event(ctx,pre);
        check(perform_a11y_action(id("a11y_editor"),A11yAction::SetValue,0,str_lit("\r\nxy"),ctx),__LINE__);step();
        process_event(ctx,bs);step();
        check(text_buffer_byte_length(&document)==2,__LINE__);
        check(perform_a11y_action(sid,A11yAction::SetValue,2,{},ctx),__LINE__);
        check(perform_a11y_action(sid,A11yAction::Increment,0,{},ctx),__LINE__);
        check(perform_a11y_action(sid,A11yAction::Increment,0,{},ctx),__LINE__);
        step();check(value==4 && changed,__LINE__);
        check(perform_a11y_action(bid,A11yAction::Press,0,{},ctx),__LINE__);
        check(perform_a11y_action(bid,A11yAction::Press,0,{},ctx),__LINE__);
        step();check(hits==2,__LINE__);step();check(hits==3,__LINE__);
    }
    modal=true;step();check(perform_a11y_action(mid,A11yAction::Dismiss,0,{},ctx),__LINE__);step();check(!modal,__LINE__);
    SDL_Window* w=SDL_CreateWindow("a11y isolation",200,100,SDL_WINDOW_HIDDEN);
    if(w) {
        const auto prior=frame_a11y_nodes(ctx);const uint32_t prior_id=prior[0].id;
        begin_window_frame(ctx,w,0.f);button({.id=id("other_window"),.label=str_lit("other")});end_frame(ctx);
        check(prior[0].id==prior_id && prior[1].id==group,__LINE__);
        check(find_a11y_node(id("other_window"),ctx,w)!=nullptr && find_a11y_node(bid,ctx,w)==nullptr,__LINE__);
        const auto first_window=frame_a11y_nodes(ctx,w);const uint32_t first_root=first_window[0].id;
        SDL_Window* second=SDL_CreateWindow("a11y second",240,120,SDL_WINDOW_HIDDEN);
        if(second) {
            begin_window_frame(ctx,second,0.f);
            check(first_window[0].id==first_root && find_a11y_node(id("other_window"),ctx,w)!=nullptr,__LINE__);
            button({.id=id("second_window"),.label=str_lit("second")});end_frame(ctx);
            check(frame_a11y_nodes(ctx,second)[0].id!=first_root && first_window[0].id==first_root,__LINE__);
            check(find_a11y_node(id("second_window"),ctx,w)==nullptr,__LINE__);release_window(ctx,second);SDL_DestroyWindow(second);
        }else check(false,__LINE__);
        release_window(ctx,w);
    }else check(false,__LINE__);
    if(w)SDL_DestroyWindow(w);
    free_text_buffer(&document);destroy_context(ctx);set_current_context(restore);return ok;
}
