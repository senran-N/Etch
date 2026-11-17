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
                node->text_cmd={.text={text_data,bytes,true},.pos={x+bold_offset,y+(d.line_height_px-d.font_size_px)*.5f},
                    .font_id=d.font_id,.size=d.font_size_px,.color=foreground};
                node->terminal_columns=columns+row_bytes-bytes;
                node->terminal_cell_width=d.cell_width_px;
            };
            draw_run(0);
            if(first.bold)draw_run(.35f);
            if(first.underline) draw_rect({.rect={x,y+d.line_height_px-2,w,1},.fill=foreground});
        }
    }
    if(d.cursor_column>=0 && uint32_t(d.cursor_column)<d.columns && d.cursor_row>=0 && uint32_t(d.cursor_row)<d.rows) {
        WidgetState* ws=state_map_get_or_insert(&ui.states,d.id,ui.frame_index);
        if(ws) {
            const Vec2 target={float(d.cursor_column)*d.cell_width_px,float(d.cursor_row)*d.line_height_px};
            if(!ws->terminal_initialized) {ws->terminal_cursor=target;ws->terminal_initialized=true;}
            advance_motion(ctx,&ws->terminal_cursor.x,&ws->terminal_velocity.x,target.x,d.cursor_motion);
            advance_motion(ctx,&ws->terminal_cursor.y,&ws->terminal_velocity.y,target.y,d.cursor_motion);
            const Vec2 cursor=ws->terminal_cursor;
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
        !std::isfinite(d.motion.damping_ratio) || d.motion.damping_ratio<0) return false;
    for(uint32_t i=0;i<d.snap_count;++i)
        if(!std::isfinite(d.snap_points[i]) || d.snap_points[i]<0 || d.snap_points[i]>1 || (i && d.snap_points[i]<=d.snap_points[i-1])) return false;
    Context* ctx=current_context();auto& ui=*ctx->active_ui;
    const uint32_t handle=id_index(d.id,0x53484545);
    const bool handle_pressed=pressed(handle);
    WidgetState* ws=state_map_get_or_insert(&ui.states,d.id,ui.frame_index);
    if(!ws) return false;
    if(!ws->sheet_initialized) {ws->sheet_snap=d.snap_count-1;ws->sheet_initialized=true;}
    if(d.snap_index) ws->sheet_snap=*d.snap_index<d.snap_count ? *d.snap_index : d.snap_count-1;
    if(ws->sheet_snap>=d.snap_count) ws->sheet_snap=d.snap_count-1;
    const float pointer=d.edge==SheetEdge::Bottom ? ui.pointer_pos.y : ui.pointer_pos.x;
    const float direction=d.edge==SheetEdge::Left ? 1.f : -1.f;
    if(d.open && handle_pressed && ui.prev_pointer_down && !ws->sheet_dragging) {
        ws->sheet_dragging=true;ws->sheet_last_pointer=pointer;ws->sheet_velocity=0;
    }
    if(ws->sheet_dragging) {
        if(ui.prev_pointer_down && d.open) {
            const float delta=(pointer-ws->sheet_last_pointer)*direction/d.extent_px;
            ws->sheet_pos=clamp_range(ws->sheet_pos+delta,0,1);
            if(ui.sim_dt>0) ws->sheet_velocity=delta/ui.sim_dt;
            ws->sheet_last_pointer=pointer;
        } else {
            const float projected=clamp_range(ws->sheet_pos+ws->sheet_velocity*.18f,0,1);
            uint32_t nearest=0;
            for(uint32_t i=1;i<d.snap_count;++i)
                if(std::fabs(d.snap_points[i]-projected)<std::fabs(d.snap_points[nearest]-projected)) nearest=i;
            ws->sheet_snap=nearest;
            if(d.snap_index) *d.snap_index=nearest;
            ws->sheet_dragging=false;
        }
    }
    if(!ws->sheet_dragging) advance_motion(ctx,&ws->sheet_pos,&ws->sheet_velocity,d.open ? d.snap_points[ws->sheet_snap] : 0,d.motion);
    const float visible=clamp_range(ws->sheet_pos,0,1);
    if(!d.open && ws->sheet_pos==0 && ws->sheet_velocity==0) return false;
    const float width=float(ui.viewport_w)/ui.dpi_scale,height=float(ui.viewport_h)/ui.dpi_scale;
    const bool bottom=d.edge==SheetEdge::Bottom;
    const float extent=std::fmin(d.extent_px,bottom ? height : width);
    const Vec2 offset=bottom ? Vec2{0,height-extent*visible} :
        d.edge==SheetEdge::Left ? Vec2{-extent*(1-visible),0} : Vec2{width-extent*visible,0};
    const Color background=resolve_theme_color(d.background,active_theme(ctx).surface_control);
    begin_box({.id=d.id,.axis=bottom ? Axis::Col : Axis::Row,.width=Sizing::fixed(bottom ? width : extent),
        .height=Sizing::fixed(bottom ? extent : height),.background=background,.radius=Corners::all(16),.clip_children=true,
        .attach_to=AttachTo::Root,.anchor_parent=Anchor::TopLeft,.floating_offset=offset,.z_index=d.z_index});
    begin_box({.id=handle,.width=bottom ? Sizing::grow() : Sizing::fixed(24),
        .height=bottom ? Sizing::fixed(24) : Sizing::grow(),.align_x=AlignX::Center,.align_y=AlignY::Center,
        .attach_to=d.edge==SheetEdge::Left ? AttachTo::Parent : AttachTo::None,
        .anchor_parent=Anchor::TopLeft,
        .floating_offset=d.edge==SheetEdge::Left ? Vec2{extent-24,0} : Vec2{},
        .z_index=d.z_index});
    box({.width=Sizing::fixed(bottom ? 36.f : 4.f),.height=Sizing::fixed(bottom ? 4.f : 36.f),
        .background=active_theme(ctx).text_secondary,.radius=Corners::all(2)});
    end_box();
    return true;
}
void end_sheet() noexcept {end_box();}
} // namespace etch
