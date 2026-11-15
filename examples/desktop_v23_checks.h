// Shared desktop integration regression, exercised by sandbox [20] and Studio [S16].
static bool verify_desktop_v23(etch::Context* restore) noexcept {
    using namespace etch;
    Context* test=create_context({.headless=true}); if(!test) return false;
    set_current_context(test); bool ok=true;
    const auto check=[&](bool pass,int line) noexcept {if(!pass){ok=false;std::fprintf(stderr,"v23 check line %d failed\n",line);}};
    bool disabled=false, checked=false, toggled=false, modal=false; int hits=0, combo=0;
    bool show_numeric=true, show_numeric2=false, combo_changed=false, numeric2_changed=false;
    bool show_external_input=false;
    char external_text[64]={};
    float dv2=5;
    char modal_text[64]={}; TextInputResult modal_input={};
    float sv=.25f,dv=2; uint32_t tab=id_index(id("desktop_test"),102); bool closed=false; bool menu_hit=false;
    const Str items[]={str_lit("First"),str_lit("Second"),str_lit("Third")};
    const uint32_t dock=id_index(id("desktop_test"),200),p1=id_index(id("desktop_test"),201),p2=id_index(id("desktop_test"),202),p3=id_index(id("desktop_test"),203);
    uint32_t root=dock_reset_root(dock);
    check(dock_add_pane(dock,root,{.id=p1,.title=str_lit("Alpha"),.closable=true}),__LINE__);
    check(dock_add_pane(dock,root,{.id=p2,.title=str_lit("Beta"),.closable=true}),__LINE__);
    check(dock_add_pane(dock,root,{.id=p3,.title=str_lit("Gamma"),.closable=true}),__LINE__);
    const auto step=[&](Vec2 point=Vec2{-1,-1},bool down=false,uint32_t key=0,bool right=false,uint8_t clicks=1,float dpi=1.f) noexcept {
        begin_frame(test,{.viewport_w=800,.viewport_h=800,.dpi_scale=dpi,.dt_seconds=1.f/60,.pointer_pos=point,.pointer_down=down,.right_pointer_down=right,.pointer_clicks=clicks,.key_pressed_code=key});
        begin_box({.axis=Axis::Col,.width=Sizing::fixed(800),.height=Sizing::fixed(800),.gap=4});
        if(begin_menu_bar({.id=id_index(id("desktop_test"),300)})) {
            if(begin_menu({.id=id_index(id("desktop_test"),301),.label=str_lit("File")})) {menu_hit|=menu_item({.id=id_index(id("desktop_test"),303),.label=str_lit("Open")});end_menu();}
            if(begin_menu({.id=id_index(id("desktop_test"),302),.label=str_lit("View")})) {menu_hit|=menu_item({.id=id_index(id("desktop_test"),304),.label=str_lit("Grid")});end_menu();}end_menu_bar();
        }
        hits+=button({.id=id_index(id("desktop_test"),100),.label=str_lit("Action"),.disabled=disabled}) ? 1:0;
        set_tooltip(id_index(id("desktop_test"),100),{.text=str_lit("Action hint"),.delay_seconds=.05f});
        checkbox({.id=id_index(id("desktop_test"),101),.label=str_lit("Check"),.checked=&checked});
        toggle_switch({.id=id_index(id("desktop_test"),104),.label=str_lit("Toggle"),.value=&toggled});
        progress_bar({.id=id_index(id("desktop_test"),105),.fraction=.6f,.overlay_text=str_lit("60%"),.height=Sizing::fixed(18)});
        if(begin_tab_bar({.id=id_index(id("desktop_test"),106),.active_tab=&tab})) {
            tab_item({.id=id_index(id("desktop_test"),102),.label=str_lit("One")});closed|=tab_item({.id=id_index(id("desktop_test"),103),.label=str_lit("Two"),.closable=true}).closed;end_tab_bar();
        }
        slider({.id=id_index(id("desktop_test"),107),.value=&sv,.step=.1f});
        if(show_numeric) drag_value({.id=id_index(id("desktop_test"),108),.value=&dv,.min_value=0,.max_value=100,.speed=.5f});
        numeric2_changed=false;
        if(show_numeric2) numeric2_changed=drag_value({.id=id_index(id("desktop_test"),118),.value=&dv2,.min_value=0,.max_value=100,.speed=.5f});
        if(show_external_input) text_input({.id=id_index(id("desktop_test"),119),.buffer=external_text,.capacity=sizeof(external_text)});
        combo_changed=combo_box({.id=id_index(id("desktop_test"),109),.selected_index=&combo,.items=items,.width=Sizing::fixed(240)});
        if(begin_popup({.id=id_index(id("desktop_test"),110)})) {menu_item({.id=id_index(id("desktop_test"),111),.label=str_lit("Popup action")});end_popup();}
        if(begin_context_menu(id_index(id("desktop_test"),112),id_index(id("desktop_test"),105))) {menu_item({.id=id_index(id("desktop_test"),113),.label=str_lit("Context action")});end_context_menu();}
        begin_dock_space({.id=dock,.width=Sizing::fixed(800),.height=Sizing::fixed(400)});
        while(next_dock_pane()) text(str_lit("Dock content"));end_dock_space();
        if(begin_modal({.id=id_index(id("desktop_test"),114),.open=&modal,.title=str_lit("Modal")})) {
            button({.id=id_index(id("desktop_test"),115),.label=str_lit("First")});
            modal_input=text_input({.id=id_index(id("desktop_test"),117),.buffer=modal_text,.capacity=sizeof(modal_text)});
            button({.id=id_index(id("desktop_test"),116),.label=str_lit("Last")});end_modal();
        }
        end_box();end_frame(test);
    };
    const auto center=[](uint32_t id) noexcept {Rect r=element_bounds(id);return Vec2{r.x+r.w*.5f,r.y+r.h*.5f};};
    const auto click=[&](uint32_t id) noexcept {Vec2 p=center(id);step(p,true);step(p,false);};
    step();step();click(id_index(id("desktop_test"),100));check(hits==1,__LINE__);
    request_focus(id_index(id("desktop_test"),100));step({},false,SDLK_SPACE);check(hits==2,__LINE__);
    disabled=true;click(id_index(id("desktop_test"),100));request_focus(id_index(id("desktop_test"),100));step({},false,SDLK_SPACE);check(hits==2,__LINE__);disabled=false;
    click(id_index(id("desktop_test"),101));click(id_index(id("desktop_test"),104));check(checked && toggled,__LINE__);
    click(id_index(id("desktop_test"),103));check(tab==id_index(id("desktop_test"),103),__LINE__);click(id_index(id_index(id("desktop_test"),103),0x2302));check(closed,__LINE__);
    Rect sr=element_bounds(id_index(id("desktop_test"),107));step({sr.x+sr.w*.4f,sr.y+10},true);step({sr.x+sr.w*.7f,sr.y+10},true);step({sr.x+sr.w*.7f,sr.y+10},false);
    check(std::fabs(sv-.7f)<.01f,__LINE__);request_focus(id_index(id("desktop_test"),107));step({},false,SDLK_RIGHT);check(std::fabs(sv-.8f)<.01f,__LINE__);
    Vec2 dp=center(id_index(id("desktop_test"),108));step(dp,true);step({dp.x+20,dp.y},true);step({dp.x+20,dp.y},false);check(dv>2,__LINE__);
    dv=12.5f;dp=center(id_index(id("desktop_test"),108));step(dp,true,0,false,2);step(dp,false);SDL_Event ev={};ev.type=SDL_EVENT_TEXT_INPUT;ev.text.text="42";process_event(test,ev);step();step({},false,SDLK_RETURN);check(std::fabs(dv-42)<.01f,__LINE__);
    dp=center(id_index(id("desktop_test"),108));step(dp,true,0,false,2);step(dp,false);ev.text.text="77";process_event(test,ev);step();step({},false,SDLK_ESCAPE);check(std::fabs(dv-42)<.01f,__LINE__);
    request_focus(id_index(id("desktop_test"),108));step({},false,SDLK_RETURN);
    ev.text.text="23";process_event(test,ev);step({},false,SDLK_RETURN);check(std::fabs(dv-23)<.01f,__LINE__);
    // SDL keys during composition belong to the IME, not the inline editor.
    const uint32_t numeric=id_index(id("desktop_test"),108);
    request_focus(numeric);step({},false,SDLK_RETURN);
    ev.text.text="61";process_event(test,ev);step();
    const float caret_width=ime_cursor_rect(test).w;
    SDL_Event editing={};editing.type=SDL_EVENT_TEXT_EDITING;editing.edit.text="nihao";editing.edit.start=5;
    SDL_Event ime_key={};ime_key.type=SDL_EVENT_KEY_DOWN;ime_key.key.down=true;ime_key.key.key=SDLK_RETURN;
    process_event(test,editing);process_event(test,ime_key);step();
    check(focused(numeric) && std::fabs(dv-23)<.01f && ime_cursor_rect(test).w>caret_width,__LINE__);
    ime_key.key.key=SDLK_ESCAPE;process_event(test,editing);process_event(test,ime_key);step();
    check(focused(numeric) && std::fabs(dv-23)<.01f && std::fabs(ime_cursor_rect(test).w-caret_width)<.01f,__LINE__);
    ime_key.key.key=SDLK_RETURN;process_event(test,ime_key);step();
    check(!focused(numeric) && std::fabs(dv-61)<.01f,__LINE__);
    const uint32_t numeric2=id_index(id("desktop_test"),118);
    request_focus(numeric);step({},false,SDLK_RETURN);
    check(focused(numeric) && has_text_selection(numeric),__LINE__);
    show_numeric=false;step();
    show_numeric2=true;step();request_focus(numeric2);step({},false,SDLK_RETURN);
    check(focused(numeric2) && has_text_selection(numeric2),__LINE__);
    ev.text.text="37";process_event(test,ev);step({},false,SDLK_RETURN);
    check(numeric2_changed && !focused(numeric2) && std::fabs(dv2-37)<.01f && std::fabs(dv-61)<.01f,__LINE__);
    // A focus handoff can acquire the shared editor before end_frame expires its previous owner.
    show_numeric=true;step();request_focus(numeric);step({},false,SDLK_RETURN);
    check(focused(numeric) && has_text_selection(numeric),__LINE__);
    show_numeric=false;request_focus(numeric2);step({},false,SDLK_RETURN);
    check(focused(numeric2) && has_text_selection(numeric2),__LINE__);
    ev.text.text="48";process_event(test,ev);step({},false,SDLK_RETURN);
    check(numeric2_changed && !focused(numeric2) && std::fabs(dv2-48)<.01f,__LINE__);
    // Reverse emission order: numeric takes ownership before numeric2 can commit.
    show_numeric=true;step();request_focus(numeric2);step({},false,SDLK_KP_ENTER);
    check(focused(numeric2) && has_text_selection(numeric2),__LINE__);
    ev.text.text="84";process_event(test,ev);step();
    request_focus(numeric);step({},false,SDLK_RETURN);
    check(numeric2_changed && std::fabs(dv2-84.f)<.01f && focused(numeric) && has_text_selection(numeric),__LINE__);
    ev.text.text="99";process_event(test,ev);step();step({},false,SDLK_ESCAPE);
    check(!focused(numeric) && std::fabs(dv-61.f)<.01f && std::fabs(dv2-84.f)<.01f,__LINE__);
    show_numeric2=false;step();
    click(id_index(id("desktop_test"),109));step();check(is_popup_open(id_index(id_index(id("desktop_test"),109),0x2304)),__LINE__);click(id_index(id_index(id_index(id("desktop_test"),109),0x2304),2));check(combo==1 && !is_popup_open(id_index(id_index(id("desktop_test"),109),0x2304)),__LINE__);
    const uint32_t combo_id=id_index(id("desktop_test"),109),combo_popup=id_index(combo_id,0x2304);
    request_focus(combo_id);step({},false,SDLK_RETURN);
    check(is_popup_open(combo_popup) && !combo_changed && combo==1,__LINE__);
    step({},false,SDLK_DOWN);step({},false,SDLK_RETURN);
    check(combo_changed && combo==2 && !is_popup_open(combo_popup),__LINE__);
    step({},false,SDLK_RETURN);step({},false,SDLK_UP);step({},false,SDLK_KP_ENTER);
    check(combo_changed && combo==1 && !is_popup_open(combo_popup),__LINE__);
    step({},false,SDLK_RETURN);step({},false,SDLK_DOWN);step({},false,SDLK_SPACE);
    check(!combo_changed && combo==1 && !is_popup_open(combo_popup),__LINE__);
    step({},false,SDLK_RETURN);step({},false,SDLK_DOWN);click(combo_id);
    check(!combo_changed && combo==1 && !is_popup_open(combo_popup),__LINE__);
    // Dropdown keys stay with the combo or its items, including during IME composition.
    show_external_input=true;step();
    const uint32_t external_input=id_index(id("desktop_test"),119);
    const uint32_t combo_keys[]={SDLK_DOWN,SDLK_RETURN};
    for(uint32_t key : combo_keys) {
        request_focus(combo_id);step({},false,SDLK_RETURN);
        check(is_popup_open(combo_popup),__LINE__);
        request_focus(external_input);step({},false,key);
        check(!combo_changed && combo==1 && !is_popup_open(combo_popup) && focused(external_input),__LINE__);
        request_focus(combo_id);step({},false,SDLK_RETURN);
        check(is_popup_open(combo_popup),__LINE__);
        request_focus(external_input);
        process_event(test,editing);ime_key.key.key=key;process_event(test,ime_key);step();
        check(!combo_changed && combo==1 && !is_popup_open(combo_popup) && focused(external_input),__LINE__);
        editing.edit.text="";process_event(test,editing);step();editing.edit.text="nihao";
    }
    show_external_input=false;request_focus(0);step();
    open_popup(id_index(id("desktop_test"),110),{500,20});step();step();check(is_popup_open(id_index(id("desktop_test"),110)),__LINE__);step({790,790},true);step({790,790},false);check(!is_popup_open(id_index(id("desktop_test"),110)),__LINE__);
    Vec2 pp=center(id_index(id("desktop_test"),105));step(pp,false,0,true);step(pp,false);check(is_popup_open(id_index(id("desktop_test"),112)),__LINE__);step({790,790},true);step({790,790},false);check(!is_popup_open(id_index(id("desktop_test"),112)),__LINE__);
    click(id_index(id_index(id("desktop_test"),301),0x2303));step();check(is_popup_open(id_index(id("desktop_test"),301)),__LINE__);step(center(id_index(id_index(id("desktop_test"),302),0x2303)));step();check(!is_popup_open(id_index(id("desktop_test"),301)) && is_popup_open(id_index(id("desktop_test"),302)),__LINE__);click(id_index(id("desktop_test"),304));check(menu_hit && !is_popup_open(id_index(id("desktop_test"),302)),__LINE__);
    modal=true;step();step();request_focus(id_index(id("desktop_test"),116));step({},false,SDLK_TAB);check(focused(id_index(id("desktop_test"),115)),__LINE__);step({},false,SDLK_ESCAPE);check(!modal,__LINE__);
    modal=true;step();step();request_focus(id_index(id("desktop_test"),117));step();
    process_event(test,editing);step();check(modal && modal_input.focused && modal_input.composing,__LINE__);
    ime_key.key.key=SDLK_ESCAPE;process_event(test,editing);process_event(test,ime_key);step();
    check(modal && modal_input.focused && !modal_input.composing,__LINE__);
    process_event(test,ime_key);step();check(!modal,__LINE__);
    modal=true;step({},false,0,false,1,2.f);
    const Rect backdrop=element_bounds(id_index(id_index(id("desktop_test"),114),0x2306));
    check(std::fabs(backdrop.w-400)<.01f && std::fabs(backdrop.h-400)<.01f,__LINE__);
    step({},false,SDLK_ESCAPE);check(!modal,__LINE__);
    step();const Vec2 tip=center(id_index(id("desktop_test"),100));for(int i=0;i<50;++i)step(tip);check(element_bounds(id_index(id_index(id("desktop_test"),100),0x2305)).w>0 && frame_stats(test).idle_sleeping,__LINE__);
    // Real pointer payloads exercise split/merge, pruning and same-leaf reordering.
    const auto drag=[&](uint32_t pane,Vec2 to) noexcept {
        Vec2 from=center(id_index(pane,0xD300));step(from,true);step({from.x+8,from.y+8},true);step(to,true);step(to,false);step();step();
    };
    uint32_t leaf=dock_find_pane_leaf(dock,p1);Rect lr=element_bounds(id_index(dock,0xD200+leaf));
    drag(p3,{lr.x+lr.w*.5f+32,lr.y+lr.h*.5f});check(dock_find_pane_leaf(dock,p3)!=dock_find_pane_leaf(dock,p1),__LINE__);
    leaf=dock_find_pane_leaf(dock,p1);lr=element_bounds(id_index(dock,0xD200+leaf));
    drag(p2,{lr.x+lr.w*.5f,lr.y+lr.h*.5f+32});check(dock_find_pane_leaf(dock,p2)!=dock_find_pane_leaf(dock,p1),__LINE__);
    leaf=dock_find_pane_leaf(dock,p1);lr=element_bounds(id_index(dock,0xD200+leaf));drag(p2,{lr.x+lr.w*.5f,lr.y+lr.h*.5f});check(dock_find_pane_leaf(dock,p2)==dock_find_pane_leaf(dock,p1),__LINE__);
    const Rect tr=element_bounds(id_index(p1,0xD300));drag(p2,{tr.x+2,tr.y+tr.h*.5f});check(element_bounds(id_index(p2,0xD300)).x<element_bounds(id_index(p1,0xD300)).x,__LINE__);
    click(id_index(p2,0xD301));step();check(!dock_is_pane_open(dock,p2),__LINE__);
    for(int i=0;i<80;++i) step();
    check(render_headless_png(test,800,800,"v23_check.png"),__LINE__);
    const auto stats=frame_stats(test);check(stats.draw_call_count<=3 && stats.idle_sleeping,__LINE__);
    std::remove("v23_check.png");destroy_context(test);set_current_context(restore);return ok;
}
