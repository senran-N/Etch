inline bool verify_workbench_materials_gpu() noexcept {
    using namespace etch;
    Context* c=create_context({.headless=true});if(!c)return false;
    bool ok=true;
    const char* path="workbench-material-check.png";
    for(int bloom=0;bloom<2;++bloom) {
        begin_frame(c,{.viewport_w=64,.viewport_h=64,.bloom_intensity=bloom ? 1.f : 0,.bloom_threshold=.5f});
        begin_box({.width=Sizing::fixed(64),.height=Sizing::fixed(64)});
        draw_rect({.rect={24,24,16,16},.fill=Color::white()});end_box();end_frame(c);
        ok &= render_headless_png(c,64,64,path,Color::black());
        if(auto* image=SDL_LoadPNG(path)) {
            Uint8 r=0,g=0,b=0,a=0;
            ok &= SDL_ReadSurfacePixel(image,22,32,&r,&g,&b,&a) && (bloom ? r>20 : r==0);
            ok &= SDL_ReadSurfacePixel(image,32,32,&r,&g,&b,&a) && r==255 && a==255;
            SDL_DestroySurface(image);
        } else ok=false;
        ok &= frame_stats(c).blur_pass_count==(bloom ? 4u : 0u);
    }
    EtchTerminalCell cells[8];for(auto& cell:cells) cell=etch_terminal_cell_default();
    cells[4].background={ETCH_TERMINAL_ANSI16,9};
    cells[5].background={ETCH_TERMINAL_ANSI256,46};
    cells[6].background={ETCH_TERMINAL_TRUE_COLOR,0x123456};
    cells[7].background={ETCH_TERMINAL_ANSI256,244};
    begin_frame(c,{.viewport_w=64,.viewport_h=64});
    auto terminal=etch_terminal_grid_desc_default();terminal.id=id("terminal-colors");terminal.cells=cells;terminal.cell_count=8;
    terminal.columns=4;terminal.rows=2;terminal.first_row=1;terminal.cell_width_px=16;terminal.line_height_px=24;
    etch_terminal_grid(&terminal);end_frame(c);
    ok &= render_headless_png(c,64,64,path,Color::black());
    if(auto* image=SDL_LoadPNG(path)) {
        Uint8 r=0,g=0,b=0,a=0;
        ok &= SDL_ReadSurfacePixel(image,8,8,&r,&g,&b,&a) && r==0xf1 && g==0x4c && b==0x4c;
        ok &= SDL_ReadSurfacePixel(image,24,8,&r,&g,&b,&a) && r==0 && g==255 && b==0;
        ok &= SDL_ReadSurfacePixel(image,40,8,&r,&g,&b,&a) && r==0x12 && g==0x34 && b==0x56;
        ok &= SDL_ReadSurfacePixel(image,56,8,&r,&g,&b,&a) && r==128 && g==128 && b==128;
        SDL_DestroySurface(image);
    } else ok=false;
    ok &= frame_stats(c).draw_call_count==1;
    // Splitting identical colors into separate style runs must not move glyphs.
    TerminalCell glyph_cells[4]={};
    for(auto& cell:glyph_cells)cell.codepoint='A';
    SDL_Surface* reference=nullptr;
    for(int split=0;split<2;++split) {
        if(split)glyph_cells[1].foreground={TerminalColorMode::TrueColor,0xffffff};
        begin_frame(c,{.viewport_w=128,.viewport_h=32});
        terminal_grid({.id=id("terminal-spacing"),.cells=glyph_cells,.columns=4,.rows=1,
            .cell_width_px=28,.line_height_px=28,.font_size_px=14,.foreground=Color::white(),.background=Color::black()});
        end_frame(c);ok &= render_headless_png(c,128,32,path,Color::black());
        SDL_Surface* image=SDL_LoadPNG(path);
        if(!image)ok=false;
        if(!split)reference=image;
        else {
            if(reference && image) {
                for(int y=0;y<32;++y)for(int x=0;x<128;++x) {
                    Uint8 r,g,b,a,rr,gg,bb,aa;
                    SDL_ReadSurfacePixel(reference,x,y,&r,&g,&b,&a);
                    SDL_ReadSurfacePixel(image,x,y,&rr,&gg,&bb,&aa);
                    ok &= r==rr && g==gg && b==bb && a==aa;
                }
                for(int cell=0;cell<4;++cell) {
                    bool ink=false;
                    for(int y=0;y<28;++y)for(int x=cell*28;x<(cell+1)*28;++x) {
                        Uint8 r,g,b,a;SDL_ReadSurfacePixel(image,x,y,&r,&g,&b,&a);ink |= r>0;
                    }
                    ok &= ink;
                }
            }
            if(image)SDL_DestroySurface(image);
        }
    }
    if(reference)SDL_DestroySurface(reference);
    std::remove(path);destroy_context(c);set_current_context(nullptr);
    if(!ok)std::fprintf(stderr,"Bloom/terminal color readback failure\n");
    return ok;
}

inline bool verify_snap_sheet() noexcept {
    using namespace etch;
    Context* c=create_context({.headless=true,.respect_reduced_motion=false});if(!c)return false;
    bool ok=true;uint32_t snap=2;
    for(uint32_t edge=0;edge<3;++edge) {
        const uint32_t sheet_id=id_index(id("snap-sheet"),edge);
        for(int frame=0;frame<70;++frame) {
            begin_frame(c,{.viewport_w=320,.viewport_h=240,.dt_seconds=1.f/60});
            SheetDesc sheet={.id=sheet_id,.edge=static_cast<SheetEdge>(edge),.extent_px=120,.snap_index=&snap};
            if(begin_sheet(sheet)) {text(str_lit("Sheet content"));end_sheet();}
            end_frame(c);
        }
        const Rect rect=element_bounds(sheet_id);
        std::fprintf(stderr,"sheet edge=%u rect=%f,%f,%f,%f\n",edge,rect.x,rect.y,rect.w,rect.h);
        ok &= edge==0 ? std::fabs(rect.y-120)<.1f : edge==1 ? std::fabs(rect.x)<.1f : std::fabs(rect.x-200)<.1f;
        if(edge==1) {
            snap=1;
            for(int frame=0;frame<90;++frame) {
                begin_frame(c,{.viewport_w=320,.viewport_h=240,.dt_seconds=1.f/60});
                if(begin_sheet({.id=sheet_id,.edge=SheetEdge::Left,.extent_px=120,.snap_index=&snap}))end_sheet();
                end_frame(c);
            }
            const Rect handle=element_bounds(id_index(sheet_id,0x53484545));
            std::fprintf(stderr,"left handle=%f,%f,%f,%f\n",handle.x,handle.y,handle.w,handle.h);
            ok &= handle.x>=0 && handle.x+handle.w<=60.1f;
            for(int frame=0;frame<4;++frame) {
                begin_frame(c,{.viewport_w=320,.viewport_h=240,.dt_seconds=1.f/60,
                    .pointer_pos={frame<2 ? 48.f : 115.f,100},.pointer_down=frame<3});
                if(begin_sheet({.id=sheet_id,.edge=SheetEdge::Left,.extent_px=120,.snap_index=&snap}))end_sheet();
                end_frame(c);
            }
            std::fprintf(stderr,"left snap=%u\n",snap);
            ok &= snap==2;
        }
        if(edge==0) {
            for(int frame=0;frame<4;++frame) {
                begin_frame(c,{.viewport_w=320,.viewport_h=240,.dt_seconds=1.f/60,
                    .pointer_pos={150,frame<2 ? 130.f : 220.f},.pointer_down=frame<3});
                if(begin_sheet({.id=sheet_id,.extent_px=120,.snap_index=&snap}))end_sheet();
                end_frame(c);
            }
            std::fprintf(stderr,"sheet snap=%u\n",snap);
            ok &= snap==0;
        }
        for(int frame=0;frame<100;++frame) {
            begin_frame(c,{.viewport_w=320,.viewport_h=240,.dt_seconds=1.f/60});
            auto sheet=etch_sheet_desc_default();sheet.id=sheet_id;sheet.open=false;sheet.edge=static_cast<EtchSheetEdge>(edge);
            sheet.extent_px=120;
            if(etch_begin_sheet(&sheet))etch_end_sheet();
            end_frame(c);
        }
        std::fprintf(stderr,"sheet idle=%d anim=%u\n",int(!needs_redraw(c)),frame_stats(c).active_animations);
        ok &= !needs_redraw(c);
        snap=2;
    }
    // Verify desktop drawer backdrop dismissal + async folder dialog queue
    {
        bool drawer_open = true;
        const uint32_t drawer_id = id("drawer_dismiss_test");
        for (int f = 0; f < 30; ++f) {
            begin_frame(c, {.viewport_w = 320, .viewport_h = 240, .dt_seconds = 1.f / 60});
            if (begin_sheet({.id = drawer_id, .edge = SheetEdge::Right, .extent_px = 140,
                             .open_state = &drawer_open, .dismiss_on_backdrop = true,
                             .show_handle = false, .backdrop_color = Color::rgba8(0, 0, 0, 120),
                             .inset_px = 8.f})) {
                text(str_lit("Drawer"));
                end_sheet();
            }
            end_frame(c);
        }
        const Rect dr = element_bounds(drawer_id);
        ok &= drawer_open && std::fabs(dr.x - (320.f - 148.f)) < 1.f;
        // Click outside on backdrop scrim (x = 20, y = 120)
        begin_frame(c, {.viewport_w = 320, .viewport_h = 240, .dt_seconds = 1.f / 60, .pointer_pos = {20.f, 120.f}, .pointer_down = true});
        if (begin_sheet({.id = drawer_id, .edge = SheetEdge::Right, .extent_px = 140, .open_state = &drawer_open, .dismiss_on_backdrop = true, .show_handle = false, .backdrop_color = Color::rgba8(0, 0, 0, 120), .inset_px = 8.f})) end_sheet();
        end_frame(c);
        begin_frame(c, {.viewport_w = 320, .viewport_h = 240, .dt_seconds = 1.f / 60, .pointer_pos = {20.f, 120.f}, .pointer_down = false});
        if (begin_sheet({.id = drawer_id, .edge = SheetEdge::Right, .extent_px = 140, .open_state = &drawer_open, .dismiss_on_backdrop = true, .show_handle = false, .backdrop_color = Color::rgba8(0, 0, 0, 120), .inset_px = 8.f})) end_sheet();
        end_frame(c);
        begin_frame(c, {.viewport_w = 320, .viewport_h = 240, .dt_seconds = 1.f / 60, .pointer_pos = {20.f, 120.f}, .pointer_down = false});
        if (begin_sheet({.id = drawer_id, .edge = SheetEdge::Right, .extent_px = 140, .open_state = &drawer_open, .dismiss_on_backdrop = true, .show_handle = false, .backdrop_color = Color::rgba8(0, 0, 0, 120), .inset_px = 8.f})) end_sheet();
        end_frame(c);
        ok &= !drawer_open;

        const Str injected[2] = {str_lit("D:/Myproject/OMP-E"), str_lit("D:/Myproject/Etch")};
        inject_file_dialog_result(c, 42u, injected, false, 0);
        begin_frame(c, {.viewport_w = 320, .viewport_h = 240, .dt_seconds = 1.f / 60});
        const FileDialogResult dlg = poll_file_dialog(c);
        ok &= dlg.ready && !dlg.cancelled && !dlg.error && dlg.request_tag == 42u && dlg.paths.size() == 2u &&
              dlg.paths[0].length == 18u && !poll_file_dialog(c).ready;
        end_frame(c);
    }
    destroy_context(c);set_current_context(nullptr);
    if(!ok)std::fprintf(stderr,"Snap sheet position/drag/exit/sleep failure\n");
    return ok;
}
