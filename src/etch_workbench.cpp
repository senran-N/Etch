// Caller-owned terminal cells and floating snap sheets; compiled via etch_unity.cpp.
namespace etch {
namespace {
Color terminal_color(TerminalColor c, Color fallback) noexcept {
    constexpr uint32_t ansi[16] = {0x16161e,0xcd3131,0x0dbc79,0xe5e510,0x2472c8,0xbc3fbc,0x11a8cd,0xe5e5e5,
        0x666666,0xf14c4c,0x23d18b,0xf5f543,0x3b8eea,0xd670d6,0x29b8db,0xffffff};
    uint32_t rgb = c.value;
    if (c.mode == TerminalColorMode::Default) return fallback;
    if (c.mode == TerminalColorMode::Ansi16) rgb = ansi[c.value & 15];
    if (c.mode == TerminalColorMode::Ansi256) {
        const uint32_t n = c.value & 255;
        if (n < 16) rgb = ansi[n];
        else if (n >= 232) { const uint32_t v=8+(n-232)*10; rgb=(v<<16)|(v<<8)|v; }
        else {
            const uint32_t cube=n-16;
            const auto level=[](uint32_t v) noexcept {return v ? 55+40*v : 0;};
            rgb=(level(cube/36)<<16)|(level(cube/6%6)<<8)|level(cube%6);
        }
    }
    return Color::rgba8(uint8_t(rgb>>16),uint8_t(rgb>>8),uint8_t(rgb),255);
}
uint32_t terminal_encode(char* out, uint32_t cp) noexcept {
    if (cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) cp=0xfffd;
    if (cp<0x80) {out[0]=char(cp ? cp : ' ');return 1;}
    if (cp<0x800) {out[0]=char(0xc0|(cp>>6));out[1]=char(0x80|(cp&63));return 2;}
    if (cp<0x10000) {out[0]=char(0xe0|(cp>>12));out[1]=char(0x80|((cp>>6)&63));out[2]=char(0x80|(cp&63));return 3;}
    out[0]=char(0xf0|(cp>>18));out[1]=char(0x80|((cp>>12)&63));out[2]=char(0x80|((cp>>6)&63));out[3]=char(0x80|(cp&63));return 4;
}
bool terminal_grid_valid(const TerminalGridDesc& d, size_t count) noexcept {
    return desktop_frame(d.id) && d.columns>0 && d.columns<=4096 && d.rows>0 && d.rows<=4096 &&
        d.rows<=count/d.columns && std::isfinite(d.cell_width_px) && d.cell_width_px>0 && d.cell_width_px<=1024 &&
        std::isfinite(d.line_height_px) && d.line_height_px>0 && d.line_height_px<=1024 &&
        std::isfinite(d.font_size_px) && d.font_size_px>0 && d.font_size_px<=512 &&
        std::isfinite(d.cursor_motion.response) && d.cursor_motion.response>0 &&
        std::isfinite(d.cursor_motion.damping_ratio) && d.cursor_motion.damping_ratio>=0;
}
using TerminalCellReader = TerminalCell (*)(const void*,size_t) noexcept;
TerminalCell read_terminal_cell(const void* cells,size_t index) noexcept {return static_cast<const TerminalCell*>(cells)[index];}
void terminal_grid_run(const TerminalGridDesc& d,const void* cells,size_t count,TerminalCellReader read) noexcept {
    Context* ctx=current_context();
    auto& ui=*ctx->active_ui;
    const Theme& theme=active_theme(ctx);
    const Color fg=resolve_theme_color(d.foreground,theme.text_primary),bg=resolve_theme_color(d.background,theme.bg_canvas);
    const size_t ring_rows=count/d.columns;
    begin_box({.id=d.id,.width=Sizing::fixed(float(d.columns)*d.cell_width_px),
        .height=Sizing::fixed(float(d.rows)*d.line_height_px),.background=bg,.clip_children=true});
    for(uint32_t row=0;row<d.rows;++row) {
        const size_t offset=((size_t(d.first_row)+row)%ring_rows)*d.columns;
        char* row_text=ctx->frame_arena.alloc<char>(d.columns*4);
        uint32_t* columns=ctx->frame_arena.alloc<uint32_t>(d.columns*4);
        if(!columns || !row_text) {end_box();return;}
        uint32_t row_bytes=0;
        uint32_t column=0;
        while(column<d.columns) {
            const TerminalCell first=read(cells,offset+column);
            if(first.width==0) {++column;continue;}
            Color foreground=terminal_color(first.foreground,fg),background=terminal_color(first.background,bg);
            if(first.inverse) {const Color swap=foreground;foreground=background;background=swap;}
            const uint32_t start=column;
            char* text_data=row_text+row_bytes;
            uint32_t bytes=0;
            do {
                const TerminalCell cell=read(cells,offset+column);
                const uint32_t encoded=terminal_encode(text_data+bytes,cell.codepoint);
                for(uint32_t byte=0;byte<encoded;++byte) columns[row_bytes+bytes+byte]=column-start;
                bytes+=encoded;
                column+=cell.width==2 && column+1<d.columns ? 2u : 1u;
                if(column==d.columns || first.width==2) break;
                const TerminalCell next=read(cells,offset+column);
                if(next.width!=1 || next.bold!=first.bold || next.underline!=first.underline || next.inverse!=first.inverse ||
                    next.foreground.mode!=first.foreground.mode || next.foreground.value!=first.foreground.value ||
                    next.background.mode!=first.background.mode || next.background.value!=first.background.value) break;
            } while(true);
            row_bytes+=bytes;
            const float x=float(start)*d.cell_width_px,y=float(row)*d.line_height_px,w=float(column-start)*d.cell_width_px;
            if(background.pack_rgba8()!=bg.pack_rgba8()) draw_rect({.rect={x,y,w,d.line_height_px},.fill=background});
            const auto draw_run=[&](float bold_offset) noexcept {
                DrawCmdNode* node=append_open_box_draw_cmd(ctx,false);
                if(!node)return;
                node->kind=DrawCmdKind::Text;
                node->text_payload = DrawCmdTextPayload{
                    .cmd = {.text={text_data,bytes,true},.pos={x+bold_offset,y+(d.line_height_px-d.font_size_px)*.5f},
                            .font_id=d.font_id,.size=d.font_size_px,.color=foreground},
                    .terminal_columns = columns+row_bytes-bytes,
                    .terminal_cell_width = d.cell_width_px,
                };
            };
            draw_run(0);
            if(first.bold)draw_run(.35f);
            if(first.underline) draw_rect({.rect={x,y+d.line_height_px-2,w,1},.fill=foreground});
        }
    }
    if(d.cursor_column>=0 && uint32_t(d.cursor_column)<d.columns && d.cursor_row>=0 && uint32_t(d.cursor_row)<d.rows) {
        WidgetState* ws=state_map_get_or_insert(&ui.states,d.id,ui.frame_index);
        if(WidgetCold* c = ensure_widget_cold(ws)) {
            const Vec2 target={float(d.cursor_column)*d.cell_width_px,float(d.cursor_row)*d.line_height_px};
            if(!c->terminal_initialized) {c->terminal_cursor=target;c->terminal_initialized=true;}
            advance_motion(ctx,&c->terminal_cursor.x,&c->terminal_velocity.x,target.x,d.cursor_motion);
            advance_motion(ctx,&c->terminal_cursor.y,&c->terminal_velocity.y,target.y,d.cursor_motion);
            const Vec2 cursor=c->terminal_cursor;
            draw_rect({.rect={cursor.x,cursor.y+d.line_height_px-3,d.cell_width_px,2},
                .fill=resolve_theme_color(d.cursor_color,theme.accent_primary),.foreground=true});
        }
    }
    end_box();
}
} // namespace

void terminal_grid(const TerminalGridDesc& d) noexcept {
    if(!terminal_grid_valid(d,d.cells.size())) return;
    terminal_grid_run(d,d.cells.data(),d.cells.size(),read_terminal_cell);
}

bool begin_sheet(const SheetDesc& d) noexcept {
    if(!desktop_frame(d.id) || uint8_t(d.edge)>uint8_t(SheetEdge::Right) || !std::isfinite(d.extent_px) ||
        d.extent_px<=0 || d.extent_px>8192 || d.snap_count==0 || d.snap_count>8 ||
        !std::isfinite(d.motion.response) || d.motion.response<=0 ||
        !std::isfinite(d.motion.damping_ratio) || d.motion.damping_ratio<0 ||
        !std::isfinite(d.corner_radius_px) || d.corner_radius_px<0 ||
        !std::isfinite(d.inset_px) || d.inset_px<0 ||
        !std::isfinite(d.border_width) || d.border_width<0 ||
        !std::isfinite(d.backdrop_blur) || d.backdrop_blur<0 ||
        !std::isfinite(d.top_inset_px) || d.top_inset_px<0 ||
        !std::isfinite(d.shadow_blur) || d.shadow_blur<0) return false;
    for(uint32_t i=0;i<d.snap_count;++i)
        if(!std::isfinite(d.snap_points[i]) || d.snap_points[i]<0 || d.snap_points[i]>1 || (i && d.snap_points[i]<=d.snap_points[i-1])) return false;
    Context* ctx=current_context();auto& ui=*ctx->active_ui;
    bool open_val=d.open_state ? *d.open_state : d.open;
    const uint32_t handle=id_index(d.id,0x53484545);
    const uint32_t scrim_id=id_index(d.id,0x5343524D);
    const bool handle_pressed=d.show_handle && pressed(handle);
    WidgetState* ws=state_map_get_or_insert(&ui.states,d.id,ui.frame_index);
    WidgetCold* c = ensure_widget_cold(ws);
    if(!c) return false;
    if(!c->sheet_initialized) {c->sheet_snap=d.snap_count-1;c->sheet_initialized=true;}
    if(d.snap_index) c->sheet_snap=*d.snap_index<d.snap_count ? *d.snap_index : d.snap_count-1;
    if(c->sheet_snap>=d.snap_count) c->sheet_snap=d.snap_count-1;
    if(open_val && d.open_state && d.dismiss_on_escape && ui.popup_count==0u && ui.modal_count==0u && !ui.frame_escape_consumed_by_ime && consume_key_pressed(SDLK_ESCAPE,false)) {
        *d.open_state=false;open_val=false;request_redraw(ctx);
    }
    if(open_val && d.open_state && d.dismiss_on_backdrop && clicked(scrim_id)) {
        *d.open_state=false;open_val=false;request_redraw(ctx);
    }
    const float pointer=d.edge==SheetEdge::Bottom ? ui.pointer_pos.y : ui.pointer_pos.x;
    const float direction=d.edge==SheetEdge::Left ? 1.f : -1.f;
    if(open_val && handle_pressed && ui.prev_pointer_down && !c->sheet_dragging) {
        c->sheet_dragging=true;c->sheet_last_pointer=pointer;c->sheet_velocity=0;
    }
    if(c->sheet_dragging) {
        if(ui.prev_pointer_down && open_val) {
            const float delta=(pointer-c->sheet_last_pointer)*direction/d.extent_px;
            c->sheet_pos=clamp_range(c->sheet_pos+delta,0,1);
            if(ui.sim_dt>0) c->sheet_velocity=delta/ui.sim_dt;
            c->sheet_last_pointer=pointer;
        } else {
            const float projected=clamp_range(c->sheet_pos+c->sheet_velocity*.18f,0,1);
            uint32_t nearest=0;
            for(uint32_t i=1;i<d.snap_count;++i)
                if(std::fabs(d.snap_points[i]-projected)<std::fabs(d.snap_points[nearest]-projected)) nearest=i;
            c->sheet_snap=nearest;
            if(d.snap_index) *d.snap_index=nearest;
            if(d.open_state && d.snap_points[nearest]<=0.0001f) {
                *d.open_state=false;open_val=false;c->sheet_snap=d.snap_count-1;
                if(d.snap_index) *d.snap_index=c->sheet_snap;
            }
            c->sheet_dragging=false;
        }
    }
    if(!c->sheet_dragging) advance_motion(ctx,&c->sheet_pos,&c->sheet_velocity,open_val ? d.snap_points[c->sheet_snap] : 0,d.motion);
    const float visible=clamp_range(c->sheet_pos,0,1);
    if(!open_val && c->sheet_pos==0 && c->sheet_velocity==0) return false;
    const float width=float(ui.viewport_w)/ui.dpi_scale,height=float(ui.viewport_h)/ui.dpi_scale;
    if((d.backdrop_color.a>0 || d.dismiss_on_backdrop) && visible>0.002f) {
        Color sc=d.backdrop_color;
        sc.a=static_cast<uint8_t>(std::round(float(sc.a)*visible));
        box({.id=scrim_id,.width=Sizing::fixed(width),.height=Sizing::fixed(height),
            .background=sc,.attach_to=AttachTo::Root,.anchor_parent=Anchor::TopLeft,
            .z_index=static_cast<int16_t>(d.z_index>0 ? d.z_index-1 : 0)});
    }
    const bool bottom=d.edge==SheetEdge::Bottom;
    const float inset=std::fmin(d.inset_px,std::fmin(width,height)*.25f);
    const float top_inset=d.top_inset_px>0.f ? std::fmin(d.top_inset_px,height*.45f) : inset;
    const float avail_along=std::fmax(1.f,bottom ? (height-top_inset-inset) : (width-inset*2.f));
    const float avail_cross=std::fmax(1.f,bottom ? (width-inset*2.f) : (height-top_inset-inset));
    const float extent=std::fmin(d.extent_px,avail_along);
    const float slide_dist=extent+inset;
    const Vec2 offset=bottom ? Vec2{inset,height-slide_dist*visible} :
        d.edge==SheetEdge::Left ? Vec2{inset-slide_dist*(1-visible),top_inset} : Vec2{width-slide_dist*visible,top_inset};
    const Color background=resolve_theme_color(d.background,active_theme(ctx).surface_control);
    begin_box({.id=d.id,.axis=bottom ? Axis::Col : Axis::Row,.width=Sizing::fixed(bottom ? avail_cross : extent),
        .height=Sizing::fixed(bottom ? extent : avail_cross),.background=background,
        .border_color=d.border_color,.border_width=d.border_width,
        .radius=Corners::all(d.corner_radius_px),
        .shadow_color=d.shadow_color,.shadow_blur=d.shadow_blur,.shadow_offset=d.shadow_offset,
        .clip_children=true,
        .attach_to=AttachTo::Root,.anchor_parent=Anchor::TopLeft,.floating_offset=offset,.z_index=d.z_index,
        .backdrop_blur=d.backdrop_blur,.continuous_corners=(inset>0.f || top_inset>0.f)});
    const bool focus_scoped = (d.dismiss_on_backdrop || d.backdrop_color.a > 0u) && ui.sheet_depth < 16u;
    if (ui.sheet_depth < 16u) ui.sheet_focus_scoped[ui.sheet_depth++] = focus_scoped;
    if (focus_scoped) begin_focus_scope(d.id, false);
    if(d.show_handle) {
        begin_box({.id=handle,.width=bottom ? Sizing::grow() : Sizing::fixed(24),
            .height=bottom ? Sizing::fixed(24) : Sizing::grow(),.align_x=AlignX::Center,.align_y=AlignY::Center,
            .attach_to=d.edge==SheetEdge::Left ? AttachTo::Parent : AttachTo::None,
            .anchor_parent=Anchor::TopLeft,
            .floating_offset=d.edge==SheetEdge::Left ? Vec2{extent-24,0} : Vec2{},
            .z_index=d.z_index});
        box({.width=Sizing::fixed(bottom ? 36.f : 4.f),.height=Sizing::fixed(bottom ? 4.f : 36.f),
            .background=active_theme(ctx).text_secondary,.radius=Corners::all(2)});
        end_box();
    }
    return true;
}
void end_sheet() noexcept {
    if (Context* ctx = current_context(); ctx && ctx->in_frame && ctx->active_ui->sheet_depth > 0u) {
        if (ctx->active_ui->sheet_focus_scoped[--ctx->active_ui->sheet_depth]) end_focus_scope();
    }
    end_box();
}
} // namespace etch
