// Desktop primitives share Clay layout, StateMap input and the existing Prim batch.
namespace etch {
namespace {
bool desktop_frame(uint32_t id) noexcept { Context* c = current_context(); return c && c->in_frame && id; }
bool desktop_activate(uint32_t id, bool disabled) noexcept {
    auto& ui = *current_context()->active_ui;
    return !disabled && (clicked(id) || (focused(id) && ui.ime_comp_len == 0u && !ui.frame_escape_consumed_by_ime &&
        (key_pressed(SDLK_RETURN) || key_pressed(SDLK_KP_ENTER) || key_pressed(SDLK_SPACE))));
}
Color desktop_bg(uint32_t id, Color bg, bool disabled) noexcept {
    const Theme& thm = active_theme(current_context());
    return disabled ? thm.surface_disabled : hovered(id) ? thm.surface_hover : bg;
}
void desktop_label(Str label, float size, uint16_t font, Color color = Color::theme_auto()) noexcept {
    text(label, {.color=color, .font_size_px=size, .font_id=font, .wrap=false});
}
float desktop_snap(float value, float lo, float hi, float step) noexcept {
    if (step > 0) value = lo + std::round((value-lo)/step)*step;
    return clamp_range(value,lo,hi);
}
const char* desktop_format(const char* fmt) noexcept {
    if (!fmt || *fmt++ != '%') return "%.2f";
    if (*fmt == '.') { ++fmt; unsigned digits = 0; while (*fmt >= '0' && *fmt <= '9' && digits < 2) { ++fmt; ++digits; } }
    return ((*fmt == 'f' || *fmt == 'g' || *fmt == 'e') && fmt[1] == '\0') ? nullptr : "%.2f";
}
bool desktop_number(uint32_t id, Str label, float* value, float lo, float hi, float step, float speed,
    const char* format, Sizing width, float height, bool disabled, bool is_slider, Color bg, Color fill, Color fg) noexcept {
    auto& ui = *current_context()->active_ui;
    const float before = *value;
    PendingA11yAction command={};
    if(!disabled) {
        const float delta=step>0?step:(hi-lo)*.05f;
        while(a11y_take_any(id,140u,&command)) {
            if(command.action==A11yAction::Increment)*value=desktop_snap(*value+delta,lo,hi,step);
            else if(command.action==A11yAction::Decrement)*value=desktop_snap(*value-delta,lo,hi,step);
            else *value=desktop_snap(command.number,lo,hi,step);
        }
    }
    if (ui.drag_edit_pending_id == id) {
        if (!disabled) {
            char* end = nullptr; const float parsed = std::strtof(ui.drag_edit_pending_buf,&end);
            if (end != ui.drag_edit_pending_buf && *end == '\0' && std::isfinite(parsed)) *value = desktop_snap(parsed,lo,hi,step);
        }
        ui.drag_edit_pending_id = 0;
    }
    bool started = false;
    if (!disabled && (!ui.drag_edit_id || !focused(ui.drag_edit_id)) && (double_clicked(id) ||
        (focused(id) && ui.ime_comp_len == 0u && !ui.frame_escape_consumed_by_ime && (key_pressed(SDLK_RETURN) || key_pressed(SDLK_KP_ENTER))))) {
        if (ui.drag_edit_id != 0u && ui.drag_edit_id != id) {
            ui.drag_edit_pending_id = ui.drag_edit_id;
            std::memcpy(ui.drag_edit_pending_buf, ui.drag_edit_buf, sizeof(ui.drag_edit_buf));
        }
        started = true; ui.drag_edit_id = id;
        std::snprintf(ui.drag_edit_buf, sizeof(ui.drag_edit_buf), "%.9g", double(*value));
        ui.drag_edit_len = uint32_t(std::strlen(ui.drag_edit_buf));
        request_focus(id); set_text_selection(id,0,ui.drag_edit_len);
        // The activation Enter belongs to the control, not the new editor.
        for (uint32_t i=0;i<ui.frame_text_op_count;++i)
            if (ui.frame_text_ops[i].kind == TextEditOpKind::KeyPress &&
                (ui.frame_text_ops[i].key == SDLK_RETURN || ui.frame_text_ops[i].key == SDLK_KP_ENTER)) ui.frame_text_ops[i].key = 0;
    }
    if (ui.drag_edit_id == id) {
        const bool cancel = !ui.frame_escape_consumed_by_ime && key_pressed(SDLK_ESCAPE);
        const bool outside_or_blur = !started && !cancel && ((ui.left_pressed_this_frame && !element_bounds(id).contains(pointer_pos())) || !focused(id));
        if (cancel || outside_or_blur || disabled) {
            if (outside_or_blur && !disabled) {
                char* end = nullptr; const float parsed = std::strtof(ui.drag_edit_buf,&end);
                if (end != ui.drag_edit_buf && *end == '\0' && std::isfinite(parsed)) *value = desktop_snap(parsed,lo,hi,step);
            }
            ui.drag_edit_id = 0; if (focused(id) && (cancel || disabled)) request_focus(0);
        } else {
            ui.drag_edit_seen_frame = ui.frame_index;
            const auto r = text_input({.id=id,.buffer=ui.drag_edit_buf,.capacity=64,.width=width,.height=Sizing::fixed(height),.padding=Padding::xy(8,3)});
            ui.drag_edit_len = r.byte_length;
            if (started) set_text_selection(id,0,ui.drag_edit_len);
            else if (r.submitted && !disabled) {
                char* end = nullptr; const float parsed = std::strtof(ui.drag_edit_buf,&end);
                if (end != ui.drag_edit_buf && *end == '\0' && std::isfinite(parsed)) *value = desktop_snap(parsed,lo,hi,step);
                ui.drag_edit_id = 0; request_focus(0);
                return *value != before;
            }
            return false;
        }
    }
    if (!disabled) {
        if ((active(id) && ui.left_pressed_this_frame) || (active(id) && ui.scrub_id != id)) {ui.scrub_id=id;ui.scrub_value=*value;}
        if (active(id) && (ui.prev_pointer_down || ui.left_pressed_this_frame)) {
            if (is_slider) {
                const Rect r = element_bounds(id);
                if (r.w > 0) *value = desktop_snap(lo+(hi-lo)*clamp_range((pointer_pos().x-r.x)/r.w,0,1),lo,hi,step);
            } else if (dragging(id)) {
                float multiplier = (key_mods() & Mod_Shift) ? .1f : (key_mods() & Mod_Ctrl) ? 10.f : 1.f;
                ui.scrub_value = clamp_range(ui.scrub_value+pointer_delta().x*speed*multiplier,lo,hi);
                *value = desktop_snap(ui.scrub_value,lo,hi,step);
            }
        }
        if (focused(id)) {
            const float increment = step > 0 ? step : (is_slider ? (hi-lo)*.01f : speed);
            if (key_pressed(SDLK_LEFT,true)) *value = desktop_snap(*value-increment,lo,hi,step);
            if (key_pressed(SDLK_RIGHT,true)) *value = desktop_snap(*value+increment,lo,hi,step);
            if (key_pressed(SDLK_HOME)) *value = lo;
            if (key_pressed(SDLK_END)) *value = hi;
        }
    }
    const Theme& thm = active_theme(current_context());
    bg = resolve_theme_color(bg, thm.surface_control);
    fill = resolve_theme_color(fill, thm.accent);
    fg = resolve_theme_color(fg, thm.text_primary);
    begin_box({.id=id,.width=width,.height=Sizing::fixed(height),.padding=Padding::xy(8,0),.gap=8,.align_y=AlignY::Center,
        .background=desktop_bg(id,bg,disabled),.radius=Corners::all(thm.radius_small_px),.focusable=!disabled});
    if (is_slider) {
        const float w = element_bounds(id).w;
        draw_rect({.rect={0,height-4,w*clamp_range((*value-lo)/(hi-lo),0,1),4},.fill=fill,.radius=Corners::all(2)});
    }
    a11y_semantic(id,is_slider?A11yRole::Slider:A11yRole::SpinButton,label,disabled?A11yState_Disabled:0u,141u,*value,lo,hi);
    desktop_label(label,12,kFontSans,fg);
    char number[64]; const char* fallback = desktop_format(format);
    std::snprintf(number,sizeof(number),fallback ? fallback : format,double(*value));
    desktop_label(str_cstr(number),12,kFontMono,fg); end_box();
    return *value != before;
}
}
bool button(const ButtonDesc& d) noexcept {
    if (!desktop_frame(d.id)) return false;
    const Theme& thm = active_theme(current_context());
    const Color bg = d.variant == ButtonVariant::Primary ? thm.accent_primary
        : d.variant == ButtonVariant::Danger ? thm.danger
        : d.variant == ButtonVariant::Ghost ? Color{}
        : thm.surface_control;
    const Color label_col = d.disabled
        ? thm.text_disabled
        : ((d.variant == ButtonVariant::Primary || d.variant == ButtonVariant::Danger) ? Color::white() : thm.text_primary);
    begin_box({.id=d.id,.width=d.width,.height=d.height,.padding=d.padding,.gap=8,.align_y=AlignY::Center,
        .background=desktop_bg(d.id,bg,d.disabled),.radius=resolve_theme_corners(d.radius,Corners::all(thm.radius_control_px)),.focusable=!d.disabled});
    a11y_semantic(d.id,A11yRole::Button,d.label,d.disabled?A11yState_Disabled:0u,3u);
    if (d.icon_id != kInvalidIcon) icon({.icon_id=d.icon_id,.size_px=d.font_size_px,.color=label_col});
    desktop_label(d.label,d.font_size_px,d.font_id,label_col);
    desktop_label(d.shortcut_hint,d.font_size_px,d.font_id,thm.text_muted); end_box();
    return desktop_activate(d.id,d.disabled);
}
bool checkbox(const CheckboxDesc& d) noexcept {
    if (!desktop_frame(d.id) || !d.checked || !std::isfinite(d.box_size_px) || d.box_size_px <= 0) return false;
    const Theme& thm = active_theme(current_context());
    const Color accent = resolve_theme_color(d.accent, thm.accent);
    const bool change = desktop_activate(d.id,d.disabled); if (change) *d.checked = !*d.checked;
    begin_box({.id=d.id,.height=Sizing::fixed(d.box_size_px+8),.gap=8,.align_y=AlignY::Center,.focusable=!d.disabled});
    a11y_semantic(d.id,A11yRole::Checkbox,d.label,(d.disabled?A11yState_Disabled:0u)|(*d.checked?A11yState_Checked:0u),3u);
    begin_box({.width=Sizing::fixed(d.box_size_px),.height=Sizing::fixed(d.box_size_px),.background=(*d.checked || d.indeterminate) ? accent : thm.surface_control,.radius=Corners::all(thm.radius_small_px * 0.6f)});
    const float b=d.box_size_px;
    if (d.indeterminate) draw_line({.from={b*.25f,b*.5f},.to={b*.75f,b*.5f},.thickness=2});
    else if (*d.checked) {
        draw_line({.from={b*.22f,b*.5f},.to={b*.43f,b*.72f},.thickness=2});
        draw_line({.from={b*.43f,b*.72f},.to={b*.8f,b*.28f},.thickness=2});
    }
    end_box(); desktop_label(d.label,d.font_size_px,d.font_id,d.disabled ? thm.text_disabled : thm.text_primary); end_box(); return change;
}
bool toggle_switch(const ToggleSwitchDesc& d) noexcept {
    if (!desktop_frame(d.id) || !d.value || !std::isfinite(d.track_width_px) || !std::isfinite(d.track_height_px) || d.track_height_px < 4 || d.track_width_px < d.track_height_px) return false;
    const Theme& thm = active_theme(current_context());
    const Color on_c = resolve_theme_color(d.on_color, thm.accent);
    const Color off_c = resolve_theme_color(d.off_color, thm.surface_hover);
    const Corners pill = Corners::all(thm.radius_pill_px);
    const bool change=desktop_activate(d.id,d.disabled); if(change) *d.value=!*d.value;
    const float t=spring(id_index(d.id,0x2301),*d.value ? 1.f : 0.f);
    begin_box({.id=d.id,.height=Sizing::fixed(d.track_height_px+8),.gap=8,.align_y=AlignY::Center,.focusable=!d.disabled});
    a11y_semantic(d.id,A11yRole::Switch,d.label,(d.disabled?A11yState_Disabled:0u)|(*d.value?A11yState_Checked:0u),3u);
    begin_box({.width=Sizing::fixed(d.track_width_px),.height=Sizing::fixed(d.track_height_px),.background=*d.value ? on_c : off_c,.radius=pill});
    draw_rect({.rect={2+t*(d.track_width_px-d.track_height_px),2,d.track_height_px-4,d.track_height_px-4},.fill=Color::white(),.radius=pill});
    end_box(); desktop_label(d.label,d.font_size_px,d.font_id,d.disabled ? thm.text_disabled : thm.text_primary);end_box();return change;
}
void progress_bar(const ProgressBarDesc& d) noexcept {
    if (!current_context() || !current_context()->in_frame || !std::isfinite(d.fraction)) return;
    const Theme& thm = active_theme(current_context());
    const Color track_c = resolve_theme_color(d.track_color, thm.surface_control);
    const Color fill_c = resolve_theme_color(d.fill_color, thm.accent);
    const Color txt_c = resolve_theme_color(d.text_color, thm.text_primary);
    const Corners rad = resolve_theme_corners(d.radius, Corners::all(thm.radius_pill_px));
    begin_box({.id=d.id,.width=d.width,.height=d.height,.background=track_c,.radius=rad,.clip_children=true});
    a11y_semantic(d.id,A11yRole::ProgressBar,d.overlay_text,0u,0u,clamp_range(d.fraction,0,1),0,1);
    begin_box({.width=Sizing::percent(clamp_range(d.fraction,0,1)),.height=Sizing::grow(),.background=fill_c,.radius=rad});end_box();
    if(d.overlay_text.chars && d.overlay_text.length) {
        begin_box({.attach_to=AttachTo::Parent,.anchor_element=Anchor::Center,.anchor_parent=Anchor::Center,.pointer_passthrough=true,.clip_to_parent=true});
        desktop_label(d.overlay_text,d.font_size_px,kFontSans,txt_c);end_box();
    }
    end_box();
}
bool begin_tab_bar(const TabBarDesc& raw) noexcept {
    if (!desktop_frame(raw.id) || !raw.active_tab || current_context()->active_ui->tab_bar_depth == 8) return false;
    const Theme& thm = active_theme(current_context());
    TabBarDesc d = raw;
    d.background = resolve_theme_color(d.background, thm.bg_chrome);
    d.active_bg = resolve_theme_color(d.active_bg, thm.surface_active);
    d.hover_bg = resolve_theme_color(d.hover_bg, thm.bg_popup);
    d.accent = resolve_theme_color(d.accent, thm.accent);
    d.text_color = resolve_theme_color(d.text_color, thm.text_muted);
    d.active_text_color = resolve_theme_color(d.active_text_color, thm.text_primary);
    auto& ui=*current_context()->active_ui;ui.tab_bars[ui.tab_bar_depth++]=d;
    begin_box({.id=d.id,.width=d.width,.height=Sizing::fixed(d.height_px),.background=d.background});a11y_semantic(d.id,A11yRole::TabList);return true;
}
TabItemResult tab_item(const TabItemDesc& d) noexcept {
    if (!desktop_frame(d.id) || !current_context()->active_ui->tab_bar_depth) return {};
    const auto bar=current_context()->active_ui->tab_bars[current_context()->active_ui->tab_bar_depth-1];
    const uint32_t close_id=id_index(d.id,0x2302);TabItemResult r={};
    r.closed=d.closable && !d.disabled && clicked(close_id); r.clicked=!r.closed && desktop_activate(d.id,d.disabled);
    if (r.clicked) *bar.active_tab=d.id;
    r.selected=*bar.active_tab==d.id;
    begin_box({.id=d.id,.height=Sizing::grow(),.padding=Padding::xy(12,0),.gap=8,.align_y=AlignY::Center,.background=r.selected ? bar.active_bg : hovered(d.id) ? bar.hover_bg : Color{},.focusable=!d.disabled});
    a11y_semantic(d.id,A11yRole::Tab,d.label,(d.disabled?A11yState_Disabled:0u)|(r.selected?A11yState_Selected:0u),3u);
    if(d.icon_id!=kInvalidIcon) icon({.icon_id=d.icon_id,.size_px=14,.color=r.selected ? bar.active_text_color : bar.text_color});
    desktop_label(d.label,bar.font_size_px,kFontSans,r.selected ? bar.active_text_color : bar.text_color);
    if(d.closable) button({.id=close_id,.label=str_lit("x"),.variant=ButtonVariant::Ghost,.height=Sizing::fixed(24),.padding=Padding::xy(3,0),.disabled=d.disabled});
    if(r.selected) draw_line({.from={0,bar.height_px-2},.to={element_bounds(d.id).w,bar.height_px-2},.color=bar.accent,.thickness=2});
    end_box();return r;
}
void end_tab_bar() noexcept { if(current_context() && current_context()->active_ui->tab_bar_depth) {--current_context()->active_ui->tab_bar_depth;end_box();} }
bool slider(const SliderDesc& d) noexcept {
    if(!desktop_frame(d.id) || !d.value || !std::isfinite(*d.value) || !std::isfinite(d.min_value) || !std::isfinite(d.max_value) || d.max_value<=d.min_value || !std::isfinite(d.step) || d.step<0) return false;
    return desktop_number(d.id,d.label,d.value,d.min_value,d.max_value,d.step,0,d.format,d.width,d.height_px,d.disabled,true,d.background,d.fill_color,d.text_color);
}
bool drag_value(const DragValueDesc& d) noexcept {
    if(!desktop_frame(d.id) || !d.value || !std::isfinite(*d.value) || !std::isfinite(d.min_value) || !std::isfinite(d.max_value) || d.max_value<d.min_value || !std::isfinite(d.speed) || !std::isfinite(d.step) || d.step<0) return false;
    return desktop_number(d.id,d.label,d.value,d.min_value,d.max_value,d.step,d.speed,d.format,d.width,d.height_px,d.disabled,false,d.background,d.fill_color,d.text_color);
}
void open_popup(uint32_t id, Vec2 anchor) noexcept {
    if(!current_context() || !id || !std::isfinite(anchor.x) || !std::isfinite(anchor.y)) return;
    auto& ui=*current_context()->active_ui;
    for(uint32_t i=0;i<ui.popup_count;++i) if(ui.popups[i].id==id) return;
    if(ui.popup_count==16) return;
    if(anchor.x<0 || anchor.y<0) anchor=pointer_pos();
    ui.popups[ui.popup_count++]={id,anchor,ui.frame_index,0};request_redraw(current_context());
}
void close_popup(uint32_t id) noexcept {
    if(!current_context()) return;auto& ui=*current_context()->active_ui;
    if(!id) ui.popup_count=0;
    else for(uint32_t i=0;i<ui.popup_count;++i) if(ui.popups[i].id==id) {ui.popup_count=i;break;}
    if(!ui.popup_count) {ui.menu_open_id=0;ui.menu_owner=0;} request_redraw(current_context());
}
bool is_popup_open(uint32_t id) noexcept {
    if(!current_context() || !id) return false;auto& ui=*current_context()->active_ui;
    for(uint32_t i=0;i<ui.popup_count;++i) if(ui.popups[i].id==id) return true;return false;
}
bool begin_popup(const PopupDesc& d) noexcept {
    if(!desktop_frame(d.id) || !is_popup_open(d.id)) return false;
    if(a11y_take(d.id,A11yAction::Dismiss)){close_popup(d.id);return false;}
    auto& ui=*current_context()->active_ui; DesktopPopup pop={};
    for(uint32_t i=0;i<ui.popup_count;++i) if(ui.popups[i].id==d.id) pop=ui.popups[i];
    bool over_child = false;
    bool descendant = false;
    for (uint32_t i=0;i<ui.popup_count;++i) {
        if (descendant && element_bounds(ui.popups[i].id).contains(pointer_pos())) over_child=true;
        if (ui.popups[i].id==d.id) descendant=true;
    }
    if(pop.opened_frame != ui.frame_index && !over_child && should_close_popover(d.id,d.trigger_id)) {close_popup(d.id);return false;}
    const Theme& thm = active_theme(current_context());
    begin_box({.id=d.id,.axis=Axis::Col,.width=d.width,.height=d.height,.padding=d.padding,.gap=d.gap,
        .background=resolve_theme_color(d.background, thm.bg_popup),
        .border_color=resolve_theme_color(d.border_color, thm.border_control),.border_width=1,
        .radius=resolve_theme_corners(d.radius, Corners::all(thm.radius_control_px)),.scroll_y=d.height.kind==SizingKind::Fixed,
        .attach_to=AttachTo::Root,.anchor_parent=Anchor::TopLeft,.floating_offset=pop.anchor,.z_index=d.z_index,.clamp_to_viewport=true});
    a11y_semantic(d.id,A11yRole::Menu,{},0,64u);
    ++ui.popup_depth;return true;
}
void end_popup() noexcept {if(current_context() && current_context()->active_ui->popup_depth) {--current_context()->active_ui->popup_depth;end_box();}}
bool begin_context_menu(uint32_t id, uint32_t target) noexcept {
    if(!desktop_frame(id)) return false;
    if(target ? right_clicked(target) : right_clicked_anywhere()) {close_popup();open_popup(id);}
    return begin_popup({.id=id,.trigger_id=target});
}
void end_context_menu() noexcept {end_popup();}
bool begin_menu_bar(const MenuBarDesc& d) noexcept {
    if(!desktop_frame(d.id) || current_context()->active_ui->menu_bar_id) return false;
    const Theme& thm = active_theme(current_context());
    current_context()->active_ui->menu_bar_id=d.id;begin_box({.id=d.id,.width=d.width,.height=Sizing::fixed(d.height_px),.background=resolve_theme_color(d.background, thm.bg_chrome)});a11y_semantic(d.id,A11yRole::MenuBar);return true;
}
void end_menu_bar() noexcept {if(current_context() && current_context()->active_ui->menu_bar_id) {current_context()->active_ui->menu_bar_id=0;end_box();}}
bool begin_menu(const MenuDesc& d) noexcept {
    if(!desktop_frame(d.id)) return false;
    auto& ui=*current_context()->active_ui;const bool was=is_popup_open(d.id);
    const bool activate=button({.id=id_index(d.id,0x2303),.label=d.label,.variant=ButtonVariant::Ghost,.height=Sizing::fixed(28),.disabled=d.disabled});
    const uint32_t trigger=id_index(d.id,0x2303);
    if(activate || (!d.disabled && hovered(trigger) && ui.menu_owner==ui.menu_bar_id && ui.menu_open_id && ui.menu_open_id!=d.id)) {
        close_popup();if(!activate || !was) {const Rect r=element_bounds(trigger);open_popup(d.id,{r.x,r.y+r.h});ui.menu_owner=ui.menu_bar_id;ui.menu_open_id=d.id;}
    }
    return begin_popup({.id=d.id,.trigger_id=trigger});
}
void end_menu() noexcept {end_popup();}
bool menu_item(const MenuItemDesc& d) noexcept {
    if (!desktop_frame(d.id)) return false;
    const bool hit=desktop_activate(d.id,d.disabled);
    const Theme& thm = active_theme(current_context());
    const Color fg = d.disabled ? thm.text_disabled : thm.text_primary;
    begin_box({.id=d.id,.width=Sizing::grow(),.height=Sizing::fixed(26),.padding=Padding::xy(8,0),.gap=8,.align_y=AlignY::Center,
        .background=desktop_bg(d.id,Color{},d.disabled),.radius=Corners::all(thm.radius_small_px * 0.6f),.focusable=!d.disabled});
    a11y_semantic(d.id,A11yRole::MenuItem,d.label,(d.disabled?A11yState_Disabled:0u)|(d.checked?A11yState_Checked:0u),3u);
    if(d.checked) desktop_label(str_lit("✓"),12,kFontSans,thm.accent);
    if(d.icon_id!=kInvalidIcon) icon({.icon_id=d.icon_id,.size_px=14,.color=fg});
    desktop_label(d.label,13,kFontSans,fg);
    begin_box({.width=Sizing::grow()});end_box();
    desktop_label(d.shortcut_hint,12,kFontSans,thm.text_muted);end_box();
    if(d.separator_after) menu_separator();
    if(hit) close_popup();
    return hit;
}
void menu_separator() noexcept {begin_box({.width=Sizing::grow(),.height=Sizing::fixed(1),.background=active_theme(current_context()).border_control});end_box();}
bool combo_box(const ComboBoxDesc& d) noexcept {
    if(!desktop_frame(d.id) || !d.selected_index || d.items.empty() || d.items.size()>uint32_t(INT32_MAX)) return false;
    const Theme& thm = active_theme(current_context());
    const Color bg = resolve_theme_color(d.background, thm.surface_control);
    const Color hbg = resolve_theme_color(d.hover_bg, thm.surface_hover);
    const Color fg = resolve_theme_color(d.text_color, thm.text_primary);
    const uint32_t pop=id_index(d.id,0x2304);bool changed=false;
    Str label=*d.selected_index>=0 && size_t(*d.selected_index)<d.items.size() ? d.items[size_t(*d.selected_index)] : d.label;
    begin_box({.id=d.id,.width=d.width,.height=Sizing::fixed(d.height_px),.padding=Padding::xy(10,0),.gap=8,.align_y=AlignY::Center,
        .background=d.disabled ? thm.surface_disabled : hovered(d.id) ? hbg : bg,.radius=Corners::all(thm.radius_control_px),.focusable=!d.disabled});
    a11y_semantic(d.id,A11yRole::ComboBox,d.label,d.disabled?A11yState_Disabled:0u,51u);
    desktop_label(label,13,kFontSans,d.disabled ? thm.text_disabled : fg);
    begin_box({.width=Sizing::grow()});end_box();desktop_label(str_lit("▾"),12,kFontSans,d.disabled ? thm.text_disabled : fg);end_box();
    if(d.disabled) {if(is_popup_open(pop)) close_popup(pop);return false;}
    PendingA11yAction cb_act={};const bool got_cb=a11y_take_any(d.id,50u,&cb_act);
    bool expanded=is_popup_open(pop);
    if(got_cb && cb_act.action!=A11yAction::Press)expanded=(cb_act.action==A11yAction::Expand);
    if(expanded && !is_popup_open(pop)){const Rect r=element_bounds(d.id);open_popup(pop,{r.x,r.y+r.h});request_focus(d.id);}
    if(!expanded && is_popup_open(pop))close_popup(pop);
    auto& ui=*current_context()->active_ui;int index=*d.selected_index;
    bool combo_owns_focus=focused(d.id);
    for(uint32_t i=0;i<d.items.size();++i) if(ui.focus_id==id_index(pop,i+1)) combo_owns_focus=true;
    const bool ime_active=ui.ime_comp_len != 0u || ui.frame_escape_consumed_by_ime;
    if(is_popup_open(pop)) {
        for(uint32_t i=0;i<ui.popup_count;++i) if(ui.popups[i].id==pop) {
            auto& p=ui.popups[i];
            const bool just_opened=p.opened_frame==ui.frame_index;
            if(!just_opened && ui.focus_id != 0u && !combo_owns_focus) {close_popup(pop);break;}
            int& sel=p.selection;
            if(combo_owns_focus && !ime_active) {
                if(key_pressed(SDLK_DOWN,true)) ++sel;if(key_pressed(SDLK_UP,true)) --sel;
            }
            sel=sel<0 ? 0 : sel>=int(d.items.size()) ? int(d.items.size())-1 : sel;index=sel;
            if(!just_opened && combo_owns_focus && !ime_active && (key_pressed(SDLK_RETURN) || key_pressed(SDLK_KP_ENTER))) {
                *d.selected_index=sel;changed=true;close_popup(pop);
            }
            break;
        }
    }
    if(!changed && ((got_cb && cb_act.action==A11yAction::Press) || (!got_cb && desktop_activate(d.id,d.disabled)))) {
        if(is_popup_open(pop)) close_popup(pop);else {
            const Rect r=element_bounds(d.id);open_popup(pop,{r.x,r.y+r.h});
            request_focus(d.id);
            if(ui.popup_count > 0 && ui.popups[ui.popup_count-1].id == pop) ui.popups[ui.popup_count-1].selection=*d.selected_index;
            index=*d.selected_index;
        }
    }
    if(is_popup_open(pop)) {
        const float h=float(d.items.size())*28+12;
        if(begin_popup({.id=pop,.trigger_id=d.id,.width=Sizing::fixed(element_bounds(d.id).w),.height=Sizing::fixed(h<d.max_popup_height_px ? h : d.max_popup_height_px)})) {
            for(uint32_t i=0;i<d.items.size();++i) if(menu_item({.id=id_index(pop,i+1),.label=d.items[i],.checked=int(i)==index})) {*d.selected_index=int(i);changed=true;}
            end_popup();
        }
    }
    a11y_semantic(d.id,A11yRole::ComboBox,d.label,is_popup_open(pop)?A11yState_Expanded:0u,51u);
    return changed;
}
void set_tooltip(uint32_t target, const TooltipDesc& d) noexcept {
    if(!desktop_frame(target) || !d.text.chars || !std::isfinite(d.delay_seconds) || !std::isfinite(d.max_width_px) || d.max_width_px<=0) return;
    auto& ui=*current_context()->active_ui;
    if(!hovered(target)) {if(ui.tooltip_target==target) ui.tooltip_target=0;return;}
    if(ui.tooltip_target!=target) {ui.tooltip_target=target;ui.tooltip_start=ui.elapsed_seconds;}
    if(ui.elapsed_seconds-ui.tooltip_start<d.delay_seconds) {request_redraw(current_context());return;}
    const Theme& thm = active_theme(current_context());
    const uint32_t id=id_index(target,0x2305);const Vec2 p=pointer_pos();
    begin_box({.id=id,.width=Sizing::fit(0,d.max_width_px),.padding=Padding::all(8),.background=thm.bg_tooltip,.border_color=thm.border_tooltip,.border_width=1,.radius=Corners::all(thm.radius_small_px),
        .attach_to=AttachTo::Root,.anchor_parent=Anchor::TopLeft,.floating_offset={p.x+12,p.y+18},.z_index=d.z_index,.pointer_passthrough=true,.clamp_to_viewport=true});
    a11y_semantic(id,A11yRole::Tooltip,d.text);
    text(d.text,{.color=thm.text_primary,.font_size_px=d.font_size_px});end_box();
}
void set_tooltip(uint32_t target, Str text_value) noexcept {set_tooltip(target,TooltipDesc{.text=text_value});}
bool begin_modal(const ModalDesc& d) noexcept {
    if(!desktop_frame(d.id) || !d.open || !*d.open || !std::isfinite(d.width_px) || d.width_px<=0 || current_context()->active_ui->modal_depth >= 32u) return false;
    const Theme& thm = active_theme(current_context());
    auto& ui=*current_context()->active_ui;const uint32_t backdrop=id_index(d.id,0x2306);
    if((d.dismiss_on_escape && !ui.frame_escape_consumed_by_ime && key_pressed(SDLK_ESCAPE)) || (d.dismiss_on_backdrop && ui.left_pressed_this_frame && element_bounds(backdrop).contains(pointer_pos()) && !element_bounds(d.id).contains(pointer_pos()))) {*d.open=false;request_redraw(current_context());return false;}
    if(a11y_take(d.id,A11yAction::Dismiss)){*d.open=false;return false;}
    const float dpi = ui.dpi_scale > 0.0f ? ui.dpi_scale : 1.0f;
    begin_box({.id=backdrop,.width=Sizing::fixed(float(ui.viewport_w)/dpi),.height=Sizing::fixed(float(ui.viewport_h)/dpi),.background=resolve_theme_color(d.backdrop_color,thm.backdrop),.attach_to=AttachTo::Root,.anchor_parent=Anchor::TopLeft,.z_index=d.z_index});
    begin_focus_scope(d.id,true);
    begin_box({.id=d.id,.axis=Axis::Col,.width=Sizing::fixed(d.width_px),.height=d.height,.padding=Padding::all(18),.gap=12,.background=resolve_theme_color(d.background,thm.bg_popup),.border_color=resolve_theme_color(d.border_color,thm.border_control),.border_width=1,.radius=resolve_theme_corners(d.radius,Corners::all(thm.radius_panel_px)),
        .attach_to=AttachTo::Parent,.anchor_element=Anchor::Center,.anchor_parent=Anchor::Center,.z_index=int16_t(d.z_index+1),.clamp_to_viewport=true});
    a11y_semantic(d.id,A11yRole::Dialog,d.title,A11yState_Modal,64u);
    desktop_label(d.title,16,kFontBold);++ui.modal_depth;return true;
}
void end_modal() noexcept {if(current_context() && current_context()->in_frame && current_context()->active_ui->modal_depth > 0u) {
    auto& ui=*current_context()->active_ui;--ui.modal_depth;end_box();end_focus_scope();end_box();
}}
} // namespace etch
