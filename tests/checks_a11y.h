// Shared sandbox [22], Studio [S18], and CTest semantic regression.
#include <cstdio>
#include <cstring>

static bool verify_a11y_checks(etch::Context* restore) noexcept {
    using namespace etch;
    Context* ctx=create_context({.headless=true});if(!ctx)return false;set_current_context(ctx);
    bool ok=true;const auto check=[&](bool pass,int line) noexcept {if(!pass){ok=false;std::fprintf(stderr,"v30 check line %d failed\n",line);}};
    const uint32_t group=id("a11y_group"),bid=id("a11y_button"),bid_after=id("a11y_btn_after"),sid=id("a11y_slider"),tid=id("a11y_text"),tid2=id("a11y_text2"),pid=id("a11y_password"),cid=id("a11y_combo"),mid=id("a11y_modal"),scrid=id("a11y_scroll"),tblid=id("a11y_table");
    float value=5;bool checked=false,modal=false;int hits=0,index=0;TextBuffer document={};check(init_text_buffer(&document,str_lit("first\nsecond")),__LINE__);char input[32]="initial",input2[32]="second",password[32]="a\xE4\xB8\xAD";
    const Str items[]={str_lit("one"),str_lit("two")};
    const TableColumnDesc tbl_cols[]={{.title=str_lit("Key"),.width=100},{.title=str_lit("Val"),.width=100}};
    bool changed=false;
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
        begin_box({.id=scrid,.axis=Axis::Col,.width=Sizing::fixed(160),.height=Sizing::fixed(40),.clip_children=true,.scroll_y=true});
        for(uint32_t si=0;si<6;++si) box({.width=Sizing::fixed(120),.height=Sizing::fixed(20)});
        end_box();
        begin_table({.id=tblid,.columns=tbl_cols,.item_count=2,.row_height_px=20,.width=Sizing::fixed(220),.height=Sizing::fixed(80),.show_scrollbar=false});
        uint32_t ridx=0;
        while(table_next_row(&ridx)){
            if(table_next_column()) text("k");
            if(table_next_column()) text("v");
        }
        end_table();
        if(begin_collapsible({.id=id("a11y_collapse"),.title=str_lit("section"),.default_open=false}).visible)end_collapsible();
        if(begin_tree_node({.id=id("a11y_tree"),.label=str_lit("branch")}).open)end_tree_node();
        combo_box({.id=cid,.selected_index=&index,.items=items});
        char val[]="stack value";set_a11y_override({.element_id=sid,.value_text={val,11},.has_value_text=true});std::memset(val,'x',11);
        if(begin_modal({.id=mid,.open=&modal,.title=str_lit("dialog")}))end_modal();
        end_box();end_frame(ctx);
    };
    step();const auto nodes=frame_a11y_nodes(ctx);check(nodes.size()>8 && frame_stats(ctx).a11y_node_count==nodes.size(),__LINE__);
    for(uint32_t i=0;i<nodes.size();++i){const auto& n=nodes[i];uint32_t traversed=0;for(uint32_t c=n.first_child_idx;c!=0xFFFFFFFFu;c=nodes[c].next_sibling_idx){if(c>=nodes.size() || ++traversed>nodes.size()){check(false,__LINE__);break;}check(nodes[c].parent_id==n.id,__LINE__);}}
    const auto* scr=find_a11y_node(scrid,ctx);check(scr && scr->role==A11yRole::ScrollArea && (scr->actions_mask & 12u)==12u,__LINE__);
    const auto* tbl=find_a11y_node(tblid,ctx);check(tbl && tbl->role==A11yRole::Table && (tbl->actions_mask & 12u)==12u,__LINE__);
    if(tbl){
        uint32_t row_cnt=0,cell_cnt=0;
        for(uint32_t r=tbl->first_child_idx;r!=0xFFFFFFFFu && r<nodes.size();r=nodes[r].next_sibling_idx){
            ++row_cnt;
            for(uint32_t c=nodes[r].first_child_idx;c!=0xFFFFFFFFu && c<nodes.size();c=nodes[c].next_sibling_idx) ++cell_cnt;
        }
        check(row_cnt==2u && cell_cnt==4u,__LINE__);
    }
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

    // #22 / #33: Extended semantic roles, emit_a11y_node, live announcements & coalescing
    {
        check(std::strcmp(a11y_role_name(A11yRole::Heading),"heading")==0,__LINE__);
        check(std::strcmp(a11y_role_name(A11yRole::Link),"link")==0,__LINE__);
        check(std::strcmp(a11y_role_name(A11yRole::Document),"document")==0,__LINE__);
        check(std::strcmp(a11y_role_name(A11yRole::Math),"math")==0,__LINE__);
        check(std::strcmp(a11y_role_name(A11yRole::Figure),"figure")==0,__LINE__);
        check(std::strcmp(a11y_role_name(A11yRole::Alert),"alert")==0,__LINE__);
        check(std::strcmp(a11y_role_name(A11yRole::Status),"status")==0,__LINE__);

        const uint32_t hid=id("a11y_heading"),lid=id("a11y_link"),did=id("a11y_doc"),mathid=id("a11y_math"),figid=id("a11y_fig"),alid=id("a11y_alert"),stid=id("a11y_status");
        begin_frame(ctx,{.viewport_w=640,.viewport_h=480});
        emit_a11y_node(hid,A11yRole::Heading,str_lit("Section Title"),str_lit(""),A11yState_None,0u,2.0f,{10,20,300,28});
        emit_a11y_node(lid,A11yRole::Link,str_lit("Docs"),str_lit("https://example.invalid"),A11yState_None,a11y_action_bit(A11yAction::Press),0.0f,{10,56,120,20});
        emit_a11y_node(did,A11yRole::Document,str_lit("Spec"),str_lit("body"),A11yState_Busy,0u,0.0f,{10,80,400,200});
        emit_a11y_node(mathid,A11yRole::Math,str_lit("E=mc^2"),str_lit(""),A11yState_None,0u,0.0f,{10,290,100,24});
        emit_a11y_node(figid,A11yRole::Figure,str_lit("Diagram"),str_lit(""),A11yState_None,0u,0.0f,{120,290,100,60});
        emit_a11y_node(alid,A11yRole::Alert,str_lit("Connection lost"),str_lit(""),A11yState_LiveAssertive|A11yState_Invalid,0u,0.0f,{10,360,240,24});
        emit_a11y_node(stid,A11yRole::Status,str_lit("Saved"),str_lit("ok"),A11yState_LivePolite,0u,0.0f,{10,390,120,24});
        announce_a11y(str_lit("First polite"),A11yLiveMode::Polite,ctx);
        A11yLiveMode prio=A11yLiveMode::Off;uint64_t seq1=0u;
        Str msg=last_a11y_announcement(&prio,&seq1,ctx);
        check(prio==A11yLiveMode::Polite && seq1>=1u && msg.length==12u && std::memcmp(msg.chars,"First polite",12)==0,__LINE__);
        announce_a11y(str_lit("Urgent alert"),A11yLiveMode::Assertive,ctx);
        uint64_t seq2=0u;
        msg=last_a11y_announcement(&prio,&seq2,ctx);
        check(prio==A11yLiveMode::Assertive && seq2==seq1+1u && msg.length==12u && std::memcmp(msg.chars,"Urgent alert",12)==0,__LINE__);
        // Same-frame polite after assertive preserves assertive priority/message
        announce_a11y(str_lit("Lower priority"),A11yLiveMode::Polite,ctx);
        uint64_t seq3=0u;
        msg=last_a11y_announcement(&prio,&seq3,ctx);
        check(prio==A11yLiveMode::Assertive && seq3==seq2 && msg.length==12u && std::memcmp(msg.chars,"Urgent alert",12)==0,__LINE__);
        end_frame(ctx);

        const auto* hn=find_a11y_node(hid,ctx);
        check(hn && hn->role==A11yRole::Heading && hn->value_num==2.0f && hn->bounds.x==10.0f && hn->bounds.w==300.0f,__LINE__);
        const auto* ln=find_a11y_node(lid,ctx);
        check(ln && ln->role==A11yRole::Link && (ln->actions_mask & a11y_action_bit(A11yAction::Press))!=0u,__LINE__);
        const auto* dn=find_a11y_node(did,ctx);
        check(dn && dn->role==A11yRole::Document && (dn->state_flags & A11yState_Busy)!=0u,__LINE__);
        const auto* an=find_a11y_node(alid,ctx);
        check(an && an->role==A11yRole::Alert && (an->state_flags & (A11yState_LiveAssertive|A11yState_Invalid))==(A11yState_LiveAssertive|A11yState_Invalid),__LINE__);
        const auto* sn=find_a11y_node(stid,ctx);
        check(sn && sn->role==A11yRole::Status && (sn->state_flags & A11yState_LivePolite)!=0u,__LINE__);
        check(find_a11y_node(mathid,ctx)!=nullptr && find_a11y_node(figid,ctx)!=nullptr,__LINE__);

        // After commit, next-frame polite announcement succeeds
        announce_a11y(str_lit("Next frame polite"),A11yLiveMode::Polite,ctx);
        uint64_t seq4=0u;
        msg=last_a11y_announcement(&prio,&seq4,ctx);
        check(prio==A11yLiveMode::Polite && seq4==seq2+1u && msg.length==17u && std::memcmp(msg.chars,"Next frame polite",17)==0,__LINE__);
    }

    // #34: SystemPreferences live query, override & RTL locale detection
    {
        const SystemPreferences live=system_preferences(ctx);
        check(live.text_scale>=0.5f && live.text_scale<=4.0f && live.locale[0]!='\0',__LINE__);

        SystemPreferences ov={};
        ov.theme=SystemTheme::Dark;
        ov.high_contrast=true;
        ov.reduced_motion=true;
        ov.text_scale=1.5f;
        std::snprintf(ov.locale,sizeof(ov.locale),"ar-SA");
        set_system_preferences_override(ctx,&ov);
        const SystemPreferences got_ar=system_preferences(ctx);
        check(got_ar.theme==SystemTheme::Dark && got_ar.high_contrast && got_ar.reduced_motion &&
              got_ar.text_scale==1.5f && got_ar.layout_direction==LayoutDirection::Rtl &&
              std::strcmp(got_ar.locale,"ar-SA")==0,__LINE__);

        SystemPreferences ov_he={};
        std::snprintf(ov_he.locale,sizeof(ov_he.locale),"he-IL");
        set_system_preferences_override(ctx,&ov_he);
        check(system_preferences(ctx).layout_direction==LayoutDirection::Rtl,__LINE__);

        SystemPreferences ov_en={};
        std::snprintf(ov_en.locale,sizeof(ov_en.locale),"en-US");
        set_system_preferences_override(ctx,&ov_en);
        check(system_preferences(ctx).layout_direction==LayoutDirection::Ltr,__LINE__);

        set_system_preferences_override(ctx,nullptr);
        const SystemPreferences restored=system_preferences(ctx);
        check(std::strcmp(restored.locale,live.locale)==0 && restored.layout_direction==live.layout_direction,__LINE__);
    }

    // #35: PointerState via synthetic SDL pen, touch, pinch & precise wheel events
    {
        begin_frame(ctx,{.viewport_w=640,.viewport_h=480});end_frame(ctx);

        SDL_Event pprox={};pprox.type=SDL_EVENT_PEN_PROXIMITY_IN;pprox.pproximity.which=7u;
        process_event(ctx,pprox);
        SDL_Event pdown={};pdown.type=SDL_EVENT_PEN_DOWN;pdown.ptouch.which=7u;pdown.ptouch.x=120.0f;pdown.ptouch.y=80.0f;pdown.ptouch.eraser=true;pdown.ptouch.down=true;
        process_event(ctx,pdown);
        SDL_Event ppress={};ppress.type=SDL_EVENT_PEN_AXIS;ppress.paxis.which=7u;ppress.paxis.axis=SDL_PEN_AXIS_PRESSURE;ppress.paxis.value=0.65f;ppress.paxis.pen_state=SDL_PEN_INPUT_ERASER_TIP;
        process_event(ctx,ppress);
        SDL_Event ptx={};ptx.type=SDL_EVENT_PEN_AXIS;ptx.paxis.which=7u;ptx.paxis.axis=SDL_PEN_AXIS_XTILT;ptx.paxis.value=15.0f;ptx.paxis.pen_state=SDL_PEN_INPUT_ERASER_TIP;
        process_event(ctx,ptx);
        SDL_Event pty={};pty.type=SDL_EVENT_PEN_AXIS;pty.paxis.which=7u;pty.paxis.axis=SDL_PEN_AXIS_YTILT;pty.paxis.value=-20.0f;pty.paxis.pen_state=SDL_PEN_INPUT_ERASER_TIP;
        process_event(ctx,pty);
        const PointerState pen_ps=pointer_state(ctx);
        check(pen_ps.kind==PointerKind::Pen && pen_ps.pointer_id==7u && pen_ps.pen_eraser &&
              pen_ps.pressure>0.64f && pen_ps.pressure<0.66f && pen_ps.tilt_x==15.0f && pen_ps.tilt_y==-20.0f,__LINE__);

        SDL_Event pup={};pup.type=SDL_EVENT_PEN_UP;pup.ptouch.which=7u;pup.ptouch.x=120.0f;pup.ptouch.y=80.0f;
        process_event(ctx,pup);
        check(pointer_state(ctx).pressure==0.0f,__LINE__);

        SDL_Event f1={};f1.type=SDL_EVENT_FINGER_DOWN;f1.tfinger.fingerID=101u;f1.tfinger.x=0.5f;f1.tfinger.y=0.25f;f1.tfinger.pressure=0.8f;
        process_event(ctx,f1);
        SDL_Event f2={};f2.type=SDL_EVENT_FINGER_DOWN;f2.tfinger.fingerID=102u;f2.tfinger.x=0.75f;f2.tfinger.y=0.5f;f2.tfinger.pressure=0.5f;
        process_event(ctx,f2);
        const PointerState touch_ps=pointer_state(ctx);
        check(touch_ps.kind==PointerKind::Touch && touch_ps.active_touches==2u && touch_ps.pressure==0.5f,__LINE__);
        SDL_Event f1up={};f1up.type=SDL_EVENT_FINGER_UP;f1up.tfinger.fingerID=101u;
        process_event(ctx,f1up);
        check(pointer_state(ctx).active_touches==1u,__LINE__);
        SDL_Event f2cancel={};f2cancel.type=SDL_EVENT_FINGER_CANCELED;f2cancel.tfinger.fingerID=102u;
        process_event(ctx,f2cancel);
        check(pointer_state(ctx).active_touches==0u,__LINE__);

        SDL_Event pinch={};pinch.type=SDL_EVENT_PINCH_UPDATE;pinch.pinch.scale=1.25f;
        process_event(ctx,pinch);
        SDL_Event wheel={};wheel.type=SDL_EVENT_MOUSE_WHEEL;wheel.wheel.y=0.35f;
        process_event(ctx,wheel);
        begin_frame(ctx,{.viewport_w=640,.viewport_h=480});
        const PointerState frame_ps=pointer_state(ctx);
        check(frame_ps.pinch_delta>0.24f && frame_ps.pinch_delta<0.26f && frame_ps.precise_scroll,__LINE__);
        end_frame(ctx);
        begin_frame(ctx,{.viewport_w=640,.viewport_h=480});
        const PointerState next_ps=pointer_state(ctx);
        check(next_ps.pinch_delta==0.0f && !next_ps.precise_scroll,__LINE__);
        end_frame(ctx);

        SDL_Event mm={};mm.type=SDL_EVENT_MOUSE_MOTION;mm.motion.x=50.0f;mm.motion.y=60.0f;mm.motion.state=SDL_BUTTON_LMASK;
        process_event(ctx,mm);
        const PointerState mouse_ps=pointer_state(ctx);
        check(mouse_ps.kind==PointerKind::Mouse && mouse_ps.pressure==1.0f && !mouse_ps.pen_eraser && mouse_ps.tilt_x==0.0f,__LINE__);
    }

    free_text_buffer(&document);destroy_context(ctx);set_current_context(restore);return ok;
}
