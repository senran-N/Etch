// Desktop primitives share Clay layout, StateMap input and the existing Prim batch.
namespace etch {
namespace {
bool desktop_frame(uint32_t id) noexcept { Context* c = current_context(); return c && c->in_frame && id; }
bool desktop_activate(uint32_t id, bool disabled) noexcept {
    auto& ui = *current_context()->active_ui;
    if (!disabled && hovered(id)) set_cursor(Cursor::Hand);
    return !disabled && (clicked(id) || (focused(id) && ui.ime_comp_len == 0u && !ui.frame_escape_consumed_by_ime &&
        (key_pressed(SDLK_RETURN) || key_pressed(SDLK_KP_ENTER) || key_pressed(SDLK_SPACE))));
}
Color desktop_bg(uint32_t id, Color bg, bool disabled) noexcept {
    const Theme& thm = active_theme(current_context());
    if (disabled) return thm.surface_disabled;
    if (hovered(id)) set_cursor(Cursor::Hand);
    Context* ctx=current_context();
    auto* ws=state_map_get_or_insert(&ctx->active_ui->states,id,ctx->active_ui->frame_index);
    float hover=hovered(id) ? 1.f : 0.f, press=pressed(id) ? 1.f : 0.f;
    if(ws) {
        advance_motion(ctx,&ws->hover_t,&ws->hover_vel,hover,SpringSpec::snappy());
        advance_motion(ctx,&ws->press_t,&ws->press_vel,press,SpringSpec::snappy());
        hover=ws->hover_t;press=ws->press_t;
    }
    Color overlay = thm.text_primary;
    overlay.a = uint8_t(std::fmin(51.f,std::fmax(0.f,26*hover+25*press)));
    return Color::blend_over(bg, overlay);
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
struct SliderTrackOpts {
    Color track_color = Color::theme_auto();
    Color thumb_color = Color::theme_auto();
    float track_height_px = 4.0f;
    float thumb_size_px = 14.0f;
    bool show_value = true;
};
bool desktop_number(uint32_t id, Str label, float* value, float lo, float hi, float step, float speed,
    const char* format, Sizing width, float height, bool disabled, bool is_slider, Color bg, Color fill, Color fg,
    const SliderTrackOpts* track_opts = nullptr) noexcept {
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
    Context* ctx = current_context();
    const Theme& thm = active_theme(ctx);
    bg = resolve_theme_color(bg, thm.surface_control);
    fill = resolve_theme_color(fill, thm.accent);
    fg = resolve_theme_color(fg, disabled ? thm.text_disabled : thm.text_primary);
    char number[64]; const char* fallback = desktop_format(format);
    std::snprintf(number,sizeof(number),fallback ? fallback : format,double(*value));

    if (is_slider && track_opts) {
        const float track_h = (std::isfinite(track_opts->track_height_px) && track_opts->track_height_px > 0.0f) ? track_opts->track_height_px : 4.0f;
        const float thumb_sz = (std::isfinite(track_opts->thumb_size_px) && track_opts->thumb_size_px > 0.0f) ? track_opts->thumb_size_px : 14.0f;
        const Color track_col = resolve_theme_color(track_opts->track_color, thm.surface_control);
        const Color thumb_base = resolve_theme_color(track_opts->thumb_color, Color::white());
        const Color thumb_col = disabled ? Color::lerp_oklab(thumb_base, thm.surface_disabled, 0.4f) : thumb_base;
        const Color val_col = disabled ? thm.text_disabled : thm.text_muted;
        const bool has_label = (label.chars != nullptr && label.length > 0u);
        const bool has_header = has_label || track_opts->show_value;
        const bool hov = !disabled && hovered(id);
        const bool prs = !disabled && (pressed(id) || active(id));
        if (hov || prs) set_cursor(Cursor::Hand);

        float ht = 0.0f, pt = 0.0f;
        if (WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, id, ctx->active_ui->frame_index)) {
            if (disabled) {
                ws->hover_t = 0.0f; ws->hover_vel = 0.0f;
                ws->press_t = 0.0f; ws->press_vel = 0.0f;
            } else {
                advance_motion(ctx, &ws->hover_t, &ws->hover_vel, (hov || prs) ? 1.0f : 0.0f, SpringSpec::snappy());
                advance_motion(ctx, &ws->press_t, &ws->press_vel, prs ? 1.0f : 0.0f, SpringSpec::snappy());
                ht = clamp_range(ws->hover_t, 0.0f, 1.0f);
                pt = clamp_range(ws->press_t, 0.0f, 1.0f);
            }
        }
        const float thumb_scale = disabled ? 1.0f : (1.0f + 0.12f * ht - 0.16f * pt);
        const float track_area_h = std::fmax(thumb_sz + 6.0f, 18.0f);
        const float total_h = has_header ? std::fmax(height, track_area_h + 16.0f) : std::fmax(height, track_area_h);

        begin_box({.id=id,.axis=Axis::Col,.width=width,.height=Sizing::fixed(total_h),.gap=2.0f,.focusable=!disabled});
        a11y_semantic(id,A11yRole::Slider,label,disabled?A11yState_Disabled:0u,141u,*value,lo,hi);
        if (has_header) {
            begin_box({.axis=Axis::Row,.width=Sizing::grow(),.height=Sizing::fixed(14.0f),.align_y=AlignY::Center});
            if (has_label) desktop_label(label,12,kFontSans,fg);
            begin_box({.width=Sizing::grow()}); end_box();
            if (track_opts->show_value) desktop_label(str_cstr(number),11.5f,kFontMono,val_col);
            end_box();
        }
        begin_box({.width=Sizing::grow(),.height=Sizing::grow()});
        struct TrackPaint {
            float fraction;
            float track_h;
            float thumb_sz;
            float thumb_scale;
            Color track_col;
            Color fill_col;
            Color thumb_col;
            Color thumb_border;
            Color shadow_col;
            Color focus_col;
            bool show_focus;
        };
        if (auto* paint = ctx->frame_arena.alloc<TrackPaint>()) {
            Color sh = thm.shadow;
            sh.a = disabled ? static_cast<uint8_t>(sh.a / 3u) : static_cast<uint8_t>(std::min<uint32_t>(105u, sh.a));
            Color fc = thm.focus_ring;
            fc.a = 90u;
            *paint = TrackPaint{
                .fraction = clamp_range((*value - lo) / (hi - lo), 0.0f, 1.0f),
                .track_h = track_h,
                .thumb_sz = thumb_sz,
                .thumb_scale = thumb_scale,
                .track_col = track_col,
                .fill_col = disabled ? thm.surface_disabled : fill,
                .thumb_col = thumb_col,
                .thumb_border = thm.border_subtle,
                .shadow_col = sh,
                .focus_col = fc,
                .show_focus = !disabled && focused(id),
            };
            draw_after_layout([](Rect tr, void* data) noexcept {
                const auto& p = *static_cast<const TrackPaint*>(data);
                if (tr.w <= 0.0f || tr.h <= 0.0f) return;
                const float cy = tr.h * 0.5f;
                const float eff_thumb = p.thumb_sz * p.thumb_scale;
                const float half_base = p.thumb_sz * 0.5f;
                const float cx = half_base + p.fraction * std::fmax(0.0f, tr.w - p.thumb_sz);
                const Corners tr_rad = Corners::all(p.track_h * 0.5f);
                draw_rect({.rect={0.0f, cy - p.track_h * 0.5f, tr.w, p.track_h}, .fill=p.track_col, .radius=tr_rad});
                if (cx > 0.5f) {
                    draw_rect({.rect={0.0f, cy - p.track_h * 0.5f, cx, p.track_h}, .fill=p.fill_col, .radius=tr_rad});
                }
                if (p.show_focus) {
                    const float ring_d = eff_thumb + 6.0f;
                    draw_rect({
                        .rect = {cx - ring_d * 0.5f, cy - ring_d * 0.5f, ring_d, ring_d},
                        .fill = p.focus_col,
                        .radius = Corners::all(ring_d * 0.5f),
                    });
                }
                const Rect thumb_rect = {cx - eff_thumb * 0.5f, cy - eff_thumb * 0.5f, eff_thumb, eff_thumb};
                const Corners thumb_rad = Corners::all(eff_thumb * 0.5f);
                draw_shadow_internal(thumb_rect, thumb_rad, p.shadow_col, 6.0f, Vec2{0.0f, 1.5f});
                draw_rect({
                    .rect = thumb_rect,
                    .fill = p.thumb_col,
                    .radius = thumb_rad,
                    .border_color = p.thumb_border,
                    .border_width = 1.0f,
                });
            }, paint);
        }
        end_box();
        end_box();
        return *value != before;
    }

    begin_box({.id=id,.width=width,.height=Sizing::fixed(height),.padding=Padding::xy(8,0),.gap=8,.align_y=AlignY::Center,
        .background=desktop_bg(id,bg,disabled),.radius=Corners::all(thm.radius_small_px),.focusable=!disabled});
    if (is_slider) {
        struct Paint { float fraction; Color color; };
        auto* paint=ctx->frame_arena.alloc<Paint>();
        if(paint) { *paint={clamp_range((*value-lo)/(hi-lo),0,1),fill};
            draw_after_layout([](Rect r,void* data) noexcept {
                const auto& p=*static_cast<Paint*>(data);
                draw_rect({.rect={0,r.h-4,r.w*p.fraction,4},.fill=p.color,.radius=Corners::all(2)});
            },paint);
        }
    }
    a11y_semantic(id,is_slider?A11yRole::Slider:A11yRole::SpinButton,label,disabled?A11yState_Disabled:0u,141u,*value,lo,hi);
    desktop_label(label,12,kFontSans,fg);
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
        : (d.variant == ButtonVariant::Primary ? thm.on_primary : d.variant == ButtonVariant::Danger ? thm.on_error : thm.text_primary);
    begin_box({.id=d.id,.width=d.width,.height=d.height,.padding=d.padding,.gap=8,.align_x=d.align_x,.align_y=AlignY::Center,
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
    if (d.indeterminate) draw_line({.from={b*.25f,b*.5f},.to={b*.75f,b*.5f},.color=thm.on_primary,.thickness=2});
    else if (*d.checked) {
        draw_line({.from={b*.22f,b*.5f},.to={b*.43f,b*.72f},.color=thm.on_primary,.thickness=2});
        draw_line({.from={b*.43f,b*.72f},.to={b*.8f,b*.28f},.color=thm.on_primary,.thickness=2});
    }
    end_box(); desktop_label(d.label,d.font_size_px,d.font_id,d.disabled ? thm.text_disabled : thm.text_primary); end_box(); return change;
}
bool toggle_switch(const ToggleSwitchDesc& d) noexcept {
    if (!desktop_frame(d.id) || !d.value || !std::isfinite(d.track_width_px) || !std::isfinite(d.track_height_px) || d.track_height_px < 4 || d.track_width_px < d.track_height_px) return false;
    Context* ctx = current_context();
    const Theme& thm = active_theme(ctx);
    const Color on_c = resolve_theme_color(d.on_color, thm.accent);
    const Color off_c = resolve_theme_color(d.off_color, thm.surface_hover);
    const Color thumb_base = resolve_theme_color(d.thumb_color, Color::white());
    const Color thumb_c = d.disabled ? Color::lerp_oklab(thumb_base, thm.surface_disabled, 0.35f) : thumb_base;
    const Color off_border = resolve_theme_color(d.border_color, thm.border_subtle);
    const Corners pill = Corners::all(thm.radius_pill_px);
    const bool change = desktop_activate(d.id, d.disabled);
    if (change) *d.value = !*d.value;
    const float t = clamp_range(spring(id_index(d.id, 0x2301u), *d.value ? 1.0f : 0.0f, SpringSpec::snappy()), 0.0f, 1.0f);
    const bool has_label = (d.label.chars != nullptr && d.label.length > 0u);
    const Color track_bg = desktop_bg(d.id, Color::lerp_oklab(off_c, on_c, t), d.disabled);
    float press_t = 0.0f;
    if (!d.disabled) {
        if (const WidgetState* ws = state_map_find(&ctx->active_ui->states, d.id)) {
            press_t = clamp_range(ws->press_t, 0.0f, 1.0f);
        }
    }
    Color track_border = off_border;
    track_border.a = static_cast<uint8_t>(std::round(static_cast<float>(off_border.a) * (1.0f - t)));
    const float track_bw = (track_border.a > 0u) ? 1.0f : 0.0f;

    const float pad = 2.0f;
    const float thumb_d = d.track_height_px - 2.0f * pad;
    const float max_stretch = std::fmin(2.5f, (d.track_width_px - d.track_height_px) * 0.25f);
    const float stretch = press_t * max_stretch;
    const float thumb_w = thumb_d + stretch;
    const float travel = std::fmax(0.0f, (d.track_width_px - 2.0f * pad) - thumb_w);
    const Rect thumb_rect = { pad + t * travel, pad, thumb_w, thumb_d };
    Color sh_col = thm.shadow;
    sh_col.a = d.disabled ? static_cast<uint8_t>(sh_col.a / 3u) : static_cast<uint8_t>(std::min<uint32_t>(95u, sh_col.a));

    begin_box({.id=d.id,.height=Sizing::fixed(has_label ? (d.track_height_px + 8.0f) : d.track_height_px),.gap=has_label ? 8.0f : 0.0f,.align_y=AlignY::Center,.focusable=!d.disabled});
    a11y_semantic(d.id,A11yRole::Switch,d.label,(d.disabled?A11yState_Disabled:0u)|(*d.value?A11yState_Checked:0u),3u);
    begin_box({.width=Sizing::fixed(d.track_width_px),.height=Sizing::fixed(d.track_height_px),.background=track_bg,.border_color=track_border,.border_width=track_bw,.radius=pill});
    draw_shadow_internal(thumb_rect, pill, sh_col, 6.0f, Vec2{0.0f, 1.5f});
    draw_rect({.rect=thumb_rect,.fill=thumb_c,.radius=pill});
    end_box();
    if (has_label) desktop_label(d.label,d.font_size_px,d.font_id,d.disabled ? thm.text_disabled : thm.text_primary);
    end_box();
    return change;
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
    Color tab_bg=desktop_bg(d.id,r.selected ? bar.active_bg : Color{},d.disabled);
    if(!r.selected && !d.disabled) {
        const auto* ws=state_map_find(&current_context()->active_ui->states,d.id);
        tab_bg=Color::lerp_oklab({},bar.hover_bg,ws ? ws->hover_t : 0.f);
    }
    begin_box({.id=d.id,.height=Sizing::grow(),.padding=Padding::xy(12,0),.gap=8,.align_y=AlignY::Center,.background=tab_bg,.focusable=!d.disabled});
    a11y_semantic(d.id,A11yRole::Tab,d.label,(d.disabled?A11yState_Disabled:0u)|(r.selected?A11yState_Selected:0u),3u);
    if(d.icon_id!=kInvalidIcon) icon({.icon_id=d.icon_id,.size_px=14,.color=r.selected ? bar.active_text_color : bar.text_color});
    desktop_label(d.label,bar.font_size_px,kFontSans,r.selected ? bar.active_text_color : bar.text_color);
    if(d.closable) {
        begin_box({.id=close_id,.width=Sizing::fixed(20),.height=Sizing::fixed(20),.background=desktop_bg(close_id,{},d.disabled),.radius=Corners::all(4)});
        const Color color=d.disabled ? active_theme(current_context()).text_disabled : bar.text_color;
        draw_line({.from={6,6},.to={14,14},.color=color,.thickness=1.5f});
        draw_line({.from={14,6},.to={6,14},.color=color,.thickness=1.5f});end_box();
    }
    if(r.selected) {
        auto* color=current_context()->frame_arena.alloc<Color>();
        if(color) { *color=bar.accent;draw_after_layout([](Rect r,void* data) noexcept {
            draw_line({.from={0,r.h-2},.to={r.w,r.h-2},.color=*static_cast<Color*>(data),.thickness=2});
        },color); }
    }
    end_box();return r;
}
void end_tab_bar() noexcept { if(current_context() && current_context()->active_ui->tab_bar_depth) {--current_context()->active_ui->tab_bar_depth;end_box();} }
bool slider(const SliderDesc& d) noexcept {
    if(!desktop_frame(d.id) || !d.value || !std::isfinite(*d.value) || !std::isfinite(d.min_value) || !std::isfinite(d.max_value) || d.max_value<=d.min_value || !std::isfinite(d.step) || d.step<0) return false;
    if (d.style == SliderStyle::Track) {
        const SliderTrackOpts opts = {
            .track_color = d.track_color,
            .thumb_color = d.thumb_color,
            .track_height_px = d.track_height_px,
            .thumb_size_px = d.thumb_size_px,
            .show_value = d.show_value,
        };
        return desktop_number(d.id,d.label,d.value,d.min_value,d.max_value,d.step,0,d.format,d.width,d.height_px,d.disabled,true,d.background,d.fill_color,d.text_color,&opts);
    }
    return desktop_number(d.id,d.label,d.value,d.min_value,d.max_value,d.step,0,d.format,d.width,d.height_px,d.disabled,true,d.background,d.fill_color,d.text_color);
}
bool drag_value(const DragValueDesc& d) noexcept {
    if(!desktop_frame(d.id) || !d.value || !std::isfinite(*d.value) || !std::isfinite(d.min_value) || !std::isfinite(d.max_value) || d.max_value<d.min_value || !std::isfinite(d.speed) || !std::isfinite(d.step) || d.step<0) return false;
    return desktop_number(d.id,d.label,d.value,d.min_value,d.max_value,d.step,d.speed,d.format,d.width,d.height_px,d.disabled,false,d.background,d.fill_color,d.text_color);
}
void open_popup(uint32_t id, Vec2 anchor) noexcept {
    if(!current_context() || !id) return;
    auto& ui=*current_context()->active_ui;
    for(uint32_t i=0;i<ui.popup_count;++i) if(ui.popups[i].id==id) return;
    if(ui.popup_count==16) return;
    if(std::isnan(anchor.x) && std::isnan(anchor.y)) anchor=pointer_pos();
    if(!std::isfinite(anchor.x) || !std::isfinite(anchor.y)) return;
    uint32_t parent=0;
    if(ui.popup_depth) parent=ui.popup_build_ids[ui.popup_depth-1];
    ui.popups[ui.popup_count++]={id,anchor,ui.frame_index,0,parent};request_redraw(current_context());
}
void close_popup(uint32_t id) noexcept {
    if(!current_context()) { return; } auto& ui=*current_context()->active_ui;
    if(!id) ui.popup_count=0;
    else {
        uint32_t removed[16]={id},count=1,write=0;
        for(uint32_t i=0;i<ui.popup_count;++i) {
            bool erase=ui.popups[i].id==id;
            for(uint32_t j=0;j<count;++j) erase |= ui.popups[i].parent_id==removed[j];
            if(erase) { if(ui.popups[i].id!=id) removed[count++]=ui.popups[i].id; }
            else ui.popups[write++]=ui.popups[i];
        }
        ui.popup_count=write;
    }
    if(!ui.popup_count) {ui.menu_open_id=0;ui.menu_owner=0;} request_redraw(current_context());
}
bool is_popup_open(uint32_t id) noexcept {
    if(!current_context() || !id) { return false; } auto& ui=*current_context()->active_ui;
    for(uint32_t i=0;i<ui.popup_count;++i) { if(ui.popups[i].id==id) return true; } return false;
}
bool begin_popup(const PopupDesc& d) noexcept {
    if(!desktop_frame(d.id) || !is_popup_open(d.id) || current_context()->active_ui->popup_depth >= 16u) return false;
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
    const Color sh_col = resolve_theme_color(d.shadow_color, thm.shadow);
    const float sh_blur = (std::isfinite(d.shadow_blur) && d.shadow_blur > 0.0f) ? d.shadow_blur : 0.0f;
    const float b_blur = (std::isfinite(d.backdrop_blur) && d.backdrop_blur > 0.0f) ? d.backdrop_blur : 0.0f;
    const bool attached = d.attach_to_trigger && d.trigger_id != 0u;
    begin_box({.id=d.id,.axis=Axis::Col,.width=d.match_trigger_width && d.trigger_id ? Sizing::percent(1) : d.width,.height=d.height,.padding=d.padding,.gap=d.gap,
        .background=resolve_theme_color(d.background, thm.bg_popup),
        .border_color=resolve_theme_color(d.border_color, thm.border_control),.border_width=1,
        .radius=resolve_theme_corners(d.radius, Corners::all(thm.radius_control_px)),
        .shadow_color=sh_blur > 0.0f ? sh_col : Color{},
        .shadow_blur=sh_blur,
        .shadow_offset=sh_blur > 0.0f ? Vec2{0.0f, 6.0f} : Vec2{},
        .scroll_y=d.scroll_y && d.height.kind==SizingKind::Fixed,
        .attach_to=attached ? AttachTo::Element : AttachTo::Root,.attach_element_id=d.trigger_id,
        .anchor_parent=attached ? Anchor::BottomLeft : Anchor::TopLeft,.floating_offset=attached ? Vec2{} : pop.anchor,.z_index=d.z_index,
        .backdrop_blur=b_blur,.clamp_to_viewport=true,.flip_on_overflow=d.flip_on_overflow});
    a11y_semantic(d.id,A11yRole::Menu,{},0,64u);
    ui.popup_build_ids[ui.popup_depth++]=d.id;return true;
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
    return begin_popup({.id=d.id,.trigger_id=trigger,.attach_to_trigger=true});
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
    if(d.checked) {
        begin_box({.width=Sizing::fixed(12),.height=Sizing::fixed(12)});
        draw_line({.from={1,6},.to={5,10},.color=thm.accent,.thickness=1.5f});
        draw_line({.from={5,10},.to={11,2},.color=thm.accent,.thickness=1.5f});end_box();
    }
    if(d.icon_id!=kInvalidIcon) icon({.icon_id=d.icon_id,.size_px=14,.color=fg});
    desktop_label(d.label,13,kFontSans,fg);
    begin_box({.width=Sizing::grow()});end_box();
    desktop_label(d.shortcut_hint,12,kFontSans,thm.text_muted);end_box();
    if(d.separator_after) menu_separator();
    if(hit) {
        auto& ui=*current_context()->active_ui;
        if(ui.menu_open_id) close_popup(ui.menu_open_id);
        else if(ui.popup_depth) close_popup(ui.popup_build_ids[ui.popup_depth-1u]);
        else close_popup();
    }
    return hit;
}
void menu_separator() noexcept {begin_box({.width=Sizing::grow(),.height=Sizing::fixed(1),.background=active_theme(current_context()).border_control});end_box();}
bool combo_box(const ComboBoxDesc& d) noexcept {
    if(!desktop_frame(d.id) || !d.selected_index || d.items.empty() || d.items.size()>uint32_t(INT32_MAX) || !std::isfinite(d.max_popup_height_px) || d.max_popup_height_px<40 || d.max_popup_height_px>16384) return false;
    Context* ctx = current_context();
    const Theme& thm = active_theme(ctx);
    const Color bg = resolve_theme_color(d.background, thm.surface_control);
    const Color fg = resolve_theme_color(d.text_color, thm.text_primary);
    const Color border_c = resolve_theme_color(d.border_color, thm.border_control);
    const Color focus_border_c = resolve_theme_color(d.focus_border_color, thm.accent);
    const float bw = (std::isfinite(d.border_width) && d.border_width >= 0.0f) ? d.border_width : 1.0f;
    const uint32_t pop=id_index(d.id,0x2304);bool changed=false;
    const bool pop_open_now = !d.disabled && is_popup_open(pop);
    const float chev_t = clamp_range(spring(id_index(d.id, 0x230Au), pop_open_now ? 1.0f : 0.0f, SpringSpec::snappy()), 0.0f, 1.0f);
    Color trigger_bg = desktop_bg(d.id, bg, d.disabled);
    if (!d.disabled && !d.hover_bg.is_theme_auto()) {
        const WidgetState* ws_cb = state_map_find(&ctx->active_ui->states, d.id);
        const float ht = ws_cb ? clamp_range(ws_cb->hover_t, 0.0f, 1.0f) : 0.0f;
        const float pt = ws_cb ? clamp_range(ws_cb->press_t, 0.0f, 1.0f) : 0.0f;
        trigger_bg = Color::lerp_oklab(bg, d.hover_bg, ht);
        if (pt > 0.001f) {
            Color press_overlay = thm.text_primary;
            press_overlay.a = static_cast<uint8_t>(std::round(25.0f * pt));
            trigger_bg = Color::blend_over(trigger_bg, press_overlay);
        }
    }
    const bool active_border = !d.disabled && (focused(d.id) || pop_open_now);
    Str label=*d.selected_index>=0 && size_t(*d.selected_index)<d.items.size() ? d.items[size_t(*d.selected_index)] : d.label;
    begin_box({.id=d.id,.width=d.width,.height=Sizing::fixed(d.height_px),.padding=Padding::xy(10,0),.gap=8,.align_y=AlignY::Center,
        .background=trigger_bg,.border_color=active_border ? focus_border_c : border_c,.border_width=bw,
        .radius=Corners::all(thm.radius_control_px),.focusable=!d.disabled});
    a11y_semantic(d.id,A11yRole::ComboBox,d.label,d.disabled?A11yState_Disabled:0u,51u);
    begin_box({.width=Sizing::grow(),.height=Sizing::fit(),.clip_children=true,.shrink_width=true});
    text(label,{.color=d.disabled ? thm.text_disabled : fg,.font_size_px=13,.font_id=kFontSans,.wrap=false,.ellipsis=true});
    end_box();
    begin_box({.width=Sizing::fixed(12),.height=Sizing::fixed(12)});
    const Color arrow=d.disabled ? thm.text_disabled : thm.text_muted;
    const float chev_angle = chev_t * 3.14159265f;
    const float cs = std::cos(chev_angle), sn = std::sin(chev_angle);
    constexpr Vec2 chev_pts[3] = {{-3.5f, -1.75f}, {0.0f, 1.75f}, {3.5f, -1.75f}};
    Vec2 rot_pts[3] = {};
    for (uint32_t i = 0; i < 3; ++i) {
        rot_pts[i] = {6.0f + chev_pts[i].x * cs - chev_pts[i].y * sn,
                      6.0f + chev_pts[i].x * sn + chev_pts[i].y * cs};
    }
    draw_polyline({.points=rot_pts,.color=arrow,.thickness=1.5f});
    end_box();end_box();
    if(d.disabled) {if(is_popup_open(pop)) close_popup(pop);return false;}
    PendingA11yAction cb_act={};const bool got_cb=a11y_take_any(d.id,50u,&cb_act);
    bool expanded=is_popup_open(pop);
    if(got_cb && cb_act.action!=A11yAction::Press)expanded=(cb_act.action==A11yAction::Expand);
    if(expanded && !is_popup_open(pop)){const Rect r=element_bounds(d.id);open_popup(pop,{r.x,r.y+r.h});request_focus(d.id);}
    if(!expanded && is_popup_open(pop))close_popup(pop);
    auto& ui=*current_context()->active_ui;int index=*d.selected_index;
    const WidgetState* focus=state_map_find(&current_context()->active_ui->states,ui.focus_id);
    const bool combo_owns_focus=focused(d.id) || (focus && focus->popup_owner==pop);
    bool reveal=false;
    const bool ime_active=ui.ime_comp_len != 0u || ui.frame_escape_consumed_by_ime;
    if(is_popup_open(pop)) {
        for(uint32_t i=0;i<ui.popup_count;++i) if(ui.popups[i].id==pop) {
            auto& p=ui.popups[i];
            const bool just_opened=p.opened_frame==ui.frame_index;
            if(!just_opened && ui.focus_id != 0u && !combo_owns_focus) {close_popup(pop);break;}
            int& sel=p.selection;
            if(combo_owns_focus && !ime_active) {
                const int previous=sel;
                if(key_pressed(SDLK_DOWN,true) && sel<int(d.items.size())-1) ++sel;
                if(key_pressed(SDLK_UP,true) && sel>0) --sel;
                const int page=static_cast<int>(std::fmax(1,std::floor((d.max_popup_height_px-12)/28)));
                if(key_pressed(SDLK_PAGEUP,true)) sel=sel>page ? sel-page : 0;
                if(key_pressed(SDLK_PAGEDOWN,true)) sel=int(d.items.size())-1-sel>page ? sel+page : int(d.items.size())-1;
                if(key_pressed(SDLK_HOME)) sel=0;
                if(key_pressed(SDLK_END)) sel=int(d.items.size())-1;
                reveal=sel!=previous;
            }
            sel=sel<0 ? 0 : sel>=int(d.items.size()) ? int(d.items.size())-1 : sel;index=sel;
            if(!just_opened && combo_owns_focus && !ime_active && (consume_key_pressed(SDLK_RETURN) || consume_key_pressed(SDLK_KP_ENTER))) {
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
        const float height=std::fmin(float(d.items.size())*28+12,d.max_popup_height_px);
        const uint32_t list=id_index(pop,0xC011u);
        bool opened=false;
        for(uint32_t i=0;i<ui.popup_count;++i) if(ui.popups[i].id==pop) opened=ui.popups[i].opened_frame==ui.frame_index;
        if(begin_popup({.id=pop,.trigger_id=d.id,.height=Sizing::fixed(height),.attach_to_trigger=true,.match_trigger_width=true,.scroll_y=false})) {
            if((opened || reveal) && index>=0) {
                if(auto* ws=state_map_get_or_insert(&current_context()->active_ui->states,list,ui.frame_index)) {
                    ws->viewport_dim.y=height-12;ws->vlist_stride=28;
                    const double top=double(index)*28,bottom=top+26;
                    if(opened || top<ws->vscroll_pos_y || bottom>ws->vscroll_pos_y+height-12)
                        virtual_list_scroll_to_row(list,uint32_t(index),28,top<ws->vscroll_pos_y ? AlignY::Top : AlignY::Bottom);
                }
            }
            const VirtualListSlice slice=begin_virtual_list({.id=list,.item_count=uint32_t(d.items.size()),.item_height_px=26,.row_gap_px=2,
                .width=Sizing::grow(),.height=Sizing::fixed(height-12),.background=Color::transparent()});
            for(uint32_t i=slice.first_index;i<slice.end_index;++i)
                if(menu_item({.id=id_index(pop,i+1),.label=d.items[i],.checked=int(i)==index})) {*d.selected_index=int(i);changed=true;}
            end_virtual_list();end_popup();
        }
    }
    a11y_semantic(d.id,A11yRole::ComboBox,d.label,is_popup_open(pop)?A11yState_Expanded:0u,51u);
    return changed;
}
void set_tooltip(uint32_t target, const TooltipDesc& d) noexcept {
    if(!desktop_frame(target) || !d.text.chars || !std::isfinite(d.delay_seconds) || !std::isfinite(d.max_width_px) || d.max_width_px<=0) return;
    Context* ctx = current_context();
    auto& ui = *ctx->active_ui;
    if(!hovered(target)) {if(ui.tooltip_target==target) {ui.tooltip_target=0;ui.tooltip_wake_tick=0;} return;}
    uint32_t rank = 0u;
    if (const WidgetState* ws = state_map_find(&ui.states, target); ws && ws->is_text_span && ws->span_hovered) {
        rank = ui.pointer_over_ids.count + 1u;
    } else {
        for (uint32_t i = ui.pointer_over_ids.count; i > 0u; --i) {
            if (ui.pointer_over_ids.data[i - 1u] == target) { rank = i; break; }
        }
    }
    if (rank > ui.tooltip_frame_rank) { ui.tooltip_frame_rank = rank; ui.tooltip_frame_winner = target; }
    if ((ui.tooltip_prev_winner != 0u && ui.tooltip_prev_winner != target) || ui.tooltip_frame_winner != target) return;
    if(ui.tooltip_target!=target) {
        ui.tooltip_target=target;
        ui.tooltip_start=ui.elapsed_seconds;
        ui.tooltip_start_tick=SDL_GetTicks();
        ui.tooltip_wake_tick=0;
    }
    const float frame_elapsed = ui.elapsed_seconds - ui.tooltip_start;
    const float elapsed = ctx->headless
        ? frame_elapsed
        : std::fmax(frame_elapsed, static_cast<float>(SDL_GetTicks() - ui.tooltip_start_tick) * 0.001f);
    if(elapsed < d.delay_seconds) {
        const uint64_t rem_ms = static_cast<uint64_t>(std::ceil((d.delay_seconds - elapsed) * 1000.0f));
        ui.tooltip_wake_tick = SDL_GetTicks() + (rem_ms > 0u ? rem_ms : 1u);
        return;
    }
    ui.tooltip_wake_tick = 0;
    const Theme& thm = active_theme(ctx);
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
    bool over_popup = false;
    for (uint32_t i=0;i<ui.popup_count;++i) over_popup |= element_bounds(ui.popups[i].id).contains(pointer_pos());
    const bool is_top = (ui.prev_top_modal_id == 0u || ui.prev_top_modal_id == d.id);
    const bool esc_dismiss = is_top && ui.popup_count==0 && d.dismiss_on_escape && !ui.frame_escape_consumed_by_ime && consume_key_pressed(SDLK_ESCAPE);
    const bool backdrop_dismiss = is_top && !over_popup && d.dismiss_on_backdrop && ui.left_pressed_this_frame && element_bounds(backdrop).contains(pointer_pos()) && !element_bounds(d.id).contains(pointer_pos());
    if(esc_dismiss || backdrop_dismiss) {
        if(backdrop_dismiss) ui.left_pressed_this_frame=false;
        *d.open=false;request_redraw(current_context());return false;
    }
    if(a11y_take(d.id,A11yAction::Dismiss)){*d.open=false;return false;}
    ui.top_modal_id=d.id;
    const int16_t z = static_cast<int16_t>(d.z_index + static_cast<int16_t>(ui.modal_depth * 4u));
    const float dpi = ui.dpi_scale > 0.0f ? ui.dpi_scale : 1.0f;
    begin_box({.id=backdrop,.width=Sizing::fixed(float(ui.viewport_w)/dpi),.height=Sizing::fixed(float(ui.viewport_h)/dpi),.background=resolve_theme_color(d.backdrop_color,thm.backdrop),.attach_to=AttachTo::Root,.anchor_parent=Anchor::TopLeft,.z_index=z});
    begin_focus_scope(d.id,true);
    begin_box({.id=d.id,.axis=Axis::Col,.width=Sizing::fixed(d.width_px),.height=d.height,.padding=Padding::all(18),.gap=12,.background=resolve_theme_color(d.background,thm.bg_popup),.border_color=resolve_theme_color(d.border_color,thm.border_control),.border_width=1,.radius=resolve_theme_corners(d.radius,Corners::all(thm.radius_panel_px)),
        .attach_to=AttachTo::Parent,.anchor_element=Anchor::Center,.anchor_parent=Anchor::Center,.z_index=static_cast<int16_t>(z+1),.clamp_to_viewport=true});
    a11y_semantic(d.id,A11yRole::Dialog,d.title,A11yState_Modal,64u);
    desktop_label(d.title,16,kFontBold);++ui.modal_depth;return true;
}
void end_modal() noexcept {if(current_context() && current_context()->in_frame && current_context()->active_ui->modal_depth > 0u) {
    auto& ui=*current_context()->active_ui;--ui.modal_depth;end_box();end_focus_scope();end_box();
}}
} // namespace etch
