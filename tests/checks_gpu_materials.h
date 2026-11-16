// M2 texture and material GPU regressions.
inline bool verify_beam_fade_gpu() noexcept {
    using namespace etch;
    Context* c=create_context({.headless=true,.respect_reduced_motion=false});if(!c)return false;
    begin_frame(c,{.viewport_w=220,.viewport_h=100});
    begin_box({.width=Sizing::fixed(220),.height=Sizing::fixed(100),.gap=20});
    begin_box({.width=Sizing::fixed(100),.height=Sizing::fixed(100),.radius=Corners::all(24),.continuous_corners=true,
        .border_beam={.color=Color::rgba8(0,255,255),.width=3,.angle_rad=0,.sweep_fraction=.25f,.glow_px=4},.top_rim_alpha=.8f});
    end_box();
    begin_box({.width=Sizing::fixed(100),.height=Sizing::fixed(100)});
    auto beam=etch_border_beam_draw_desc_default();beam.rect={0,0,100,100};beam.radius={24,24,24,24};
    beam.beam.color={0,255,255,255};beam.beam.width=3;beam.beam.glow_px=4;beam.top_rim_alpha=.8f;beam.continuous_corners=true;
    etch_draw_border_beam(&beam);
    end_box();end_box();end_frame(c);
    const char* path="beam-fade-check.png";
    const Prim packed=Prim::make_border_beam({0,0,20,20},{0,0,20,20},Color::white(),Color::black(),Corners::all(4),2,.5f,.25f,8,.7f,1,true,true);
    bool ok=packed.payload.x==pack_f16x2(.5f,.25f) && packed.payload.y==pack_f16x2(2,8)
        && packed.style.w==pack_f16x2(.7f,1) && ((packed.style.x>>4)&35u)==35u;
    ok &= render_headless_png(c,220,100,path,Color::transparent());
    if(auto* surface=SDL_LoadPNG(path)) {
        Uint8 r=0,g=0,b=0,a=0;
        ok &= SDL_ReadSurfacePixel(surface,50,0,&r,&g,&b,&a) && a>180;
        ok &= SDL_ReadSurfacePixel(surface,50,50,&r,&g,&b,&a) && a==0;
        ok &= SDL_ReadSurfacePixel(surface,0,0,&r,&g,&b,&a) && a==0;
        for(int y=0;y<100;++y) for(int x=0;x<100;++x) {
            Uint8 rr=0,gg=0,bb=0,aa=0;
            ok &= SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a);
            ok &= SDL_ReadSurfacePixel(surface,x+120,y,&rr,&gg,&bb,&aa);
            ok &= std::abs(int(r)-rr)<=1 && std::abs(int(g)-gg)<=1 && std::abs(int(b)-bb)<=1 && std::abs(int(a)-aa)<=1;
        }
        SDL_DestroySurface(surface);
    } else ok=false;
    ok &= frame_stats(c).draw_call_count==1;
    const auto list_id=id("fade-list");
    for(int frame=0;frame<3;++frame) {
        if(frame==2)set_virtual_list_scroll_offset(list_id,100);
        begin_frame(c,{.viewport_w=100,.viewport_h=100});
        const auto slice=begin_virtual_list({.id=list_id,.item_count=30,.item_height_px=20,
            .width=Sizing::fixed(100),.height=Sizing::fixed(100),.show_scrollbar=false,.scroll_fade_px=20});
        for(uint32_t i=slice.first_index;i<slice.end_index;++i)
            box({.width=Sizing::fixed(100),.height=Sizing::fixed(20),.background=Color::white()});
        end_virtual_list();end_frame(c);
        if(frame==0)continue; // Establish the live viewport before checking scroll endpoints.
        ok &= render_headless_png(c,100,100,path,Color::transparent());
        if(auto* surface=SDL_LoadPNG(path)) {
            Uint8 r=0,g=0,b=0,a=0;
            ok &= SDL_ReadSurfacePixel(surface,50,0,&r,&g,&b,&a) && (frame==1 ? a==255 : a<3);
            ok &= SDL_ReadSurfacePixel(surface,50,10,&r,&g,&b,&a) && (frame==1 ? a==255 : a>125 && a<150);
            ok &= SDL_ReadSurfacePixel(surface,50,25,&r,&g,&b,&a) && a==255;
            ok &= SDL_ReadSurfacePixel(surface,50,99,&r,&g,&b,&a) && a<3;
            SDL_DestroySurface(surface);
        } else ok=false;
        ok &= frame_stats(c).draw_call_count==1;
    }
    for(int axis=0;axis<2;++axis) for(int frame=0;frame<3;++frame) {
        begin_frame(c,{.viewport_w=100,.viewport_h=100,.dt_seconds=0,
            .pointer_pos={50,50},.scroll_delta={axis && frame==2 ? -1.f : 0,!axis && frame==2 ? -1.f : 0}});
        auto scroll=etch_box_desc_default();scroll.id=id(axis ? "fade-box-x" : "fade-box");
        scroll.width=etch_sizing_fixed(100);scroll.height=etch_sizing_fixed(100);
        scroll.scroll_y=!axis;scroll.scroll_x=axis!=0;scroll.show_scrollbar=false;scroll.scroll_fade_px=20;
        etch_begin_box(&scroll);
        box({.width=Sizing::fixed(axis ? 300.f : 100.f),.height=Sizing::fixed(axis ? 100.f : 300.f),.background=Color::white()});
        etch_end_box();end_frame(c);
        if(frame==0)continue;
        ok &= render_headless_png(c,100,100,path,Color::transparent());
        if(auto* surface=SDL_LoadPNG(path)) {
            Uint8 r=0,g=0,b=0,a=0;
            ok &= SDL_ReadSurfacePixel(surface,axis ? 0 : 50,axis ? 50 : 0,&r,&g,&b,&a) && (frame==1 ? a==255 : a<3);
            ok &= SDL_ReadSurfacePixel(surface,axis ? 10 : 50,axis ? 50 : 10,&r,&g,&b,&a) && (frame==1 ? a==255 : a>125 && a<150);
            SDL_DestroySurface(surface);
        } else ok=false;
    }
    const auto nested_id=id("nested-fade-list");
    int base_alpha=0;
    for(int frame=0;frame<3;++frame) {
        if(frame==2)set_virtual_list_scroll_offset(nested_id,100);
        begin_frame(c,{.viewport_w=100,.viewport_h=100});
        begin_box({.width=Sizing::fixed(100),.height=Sizing::fixed(100),.radius=Corners::all(20),.clip_children=true});
        const auto slice=begin_virtual_list({.id=nested_id,.item_count=30,.item_height_px=20,
            .width=Sizing::fixed(100),.height=Sizing::fixed(100),.show_scrollbar=false,.scroll_fade_px=frame==2 ? 20.f : 0});
        for(uint32_t i=slice.first_index;i<slice.end_index;++i)
            box({.width=Sizing::fixed(100),.height=Sizing::fixed(20),.background=Color::white()});
        end_virtual_list();end_box();end_frame(c);
        if(frame==0)continue;
        ok &= render_headless_png(c,100,100,path,Color::transparent());
        if(auto* surface=SDL_LoadPNG(path)) {
            Uint8 r=0,g=0,b=0,a=0;
            ok &= SDL_ReadSurfacePixel(surface,3,8,&r,&g,&b,&a);
            if(frame==1) {base_alpha=a;ok &= base_alpha>40 && base_alpha<220;}
            else ok &= std::abs(int(a)-int(float(base_alpha)*.38834375f+.5f))<=2;
            SDL_DestroySurface(surface);
        } else ok=false;
    }
    Theme scrollbar_theme=theme(c);
    scrollbar_theme.scrollbar_track=Color::white();
    scrollbar_theme.scrollbar_thumb=Color::white();
    set_theme(scrollbar_theme,c);
    for(int frame=0;frame<2;++frame) {
        begin_frame(c,{.viewport_w=100,.viewport_h=100});
        const auto slice=begin_virtual_list({.id=list_id,.item_count=30,.item_height_px=20,
            .width=Sizing::fixed(100),.height=Sizing::fixed(100),.scroll_fade_px=20});
        for(uint32_t i=slice.first_index;i<slice.end_index;++i)
            box({.width=Sizing::fixed(100),.height=Sizing::fixed(20),.background=Color::white()});
        end_virtual_list();end_frame(c);
        ok &= render_headless_png(c,100,100,path,Color::transparent());
        if(auto* surface=SDL_LoadPNG(path)) {
            Uint8 r=0,g=0,b=0,a=0;
            ok &= SDL_ReadSurfacePixel(surface,93,8,&r,&g,&b,&a) && a==255;
            ok &= SDL_ReadSurfacePixel(surface,50,8,&r,&g,&b,&a) && a<110;
            SDL_DestroySurface(surface);
        } else ok=false;
    }
    for(int frame=0;frame<3;++frame) {
        set_time_scale(c,frame==2 ? 0.f : 1.f);
        begin_frame(c,{.viewport_w=200,.viewport_h=100,.dt_seconds=1.f/60});
        begin_box({.width=Sizing::fixed(200),.height=Sizing::fixed(100)});
        for(int panel=0;panel<2;++panel) {
            draw_border_beam({.rect={100.f*float(panel),0,100,100},.radius=Corners::all(16),
                .beam={.color=Color::white(),.width=3,.angle_rad=.1f,.beam_active=frame>0,.symmetric_dual=panel==0}});
        }
        end_box();end_frame(c);
        ok &= frame_stats(c).active_animations==uint32_t(frame==1 ? 2 : 0);
        if(frame==0) {
            ok &= render_headless_png(c,200,100,path,Color::transparent());
            if(auto* surface=SDL_LoadPNG(path)) {
                Uint8 r=0,g=0,b=0,a=0;
                ok &= SDL_ReadSurfacePixel(surface,1,50,&r,&g,&b,&a) && a>230;
                ok &= SDL_ReadSurfacePixel(surface,99,50,&r,&g,&b,&a) && a>230;
                ok &= SDL_ReadSurfacePixel(surface,101,50,&r,&g,&b,&a) && a==0;
                SDL_DestroySurface(surface);
            } else ok=false;
        }
    }
    std::remove(path);destroy_context(c);set_current_context(nullptr);
    if(!ok)std::fprintf(stderr,"beam / scroll fade regression failed\n");
    return ok;
}

inline bool verify_punch_gpu() noexcept {
    using namespace etch;
    Context* c=create_context({.headless=true});if(!c)return false;
    begin_frame(c,{.viewport_w=100,.viewport_h=100});
    begin_box({.width=Sizing::fixed(100),.height=Sizing::fixed(100)});
    draw_rect({.rect={0,0,100,100},.fill=Color::rgba8(255,0,0,255)});
    auto hole=etch_punch_hole_desc_default();
    hole.rect={20,20,60,60};hole.radius={12,12,12,12};
    etch_punch_hole(&hole);
    draw_rect({.rect={40,40,20,20},.fill=Color::rgba8(0,255,0,255)});
    end_box();end_frame(c);
    const char* path="punch-hole-check.png";
    bool ok=render_headless_png(c,100,100,path,Color::black());
    if(auto* surface=SDL_LoadPNG(path)) {
        Uint8 r=0,g=0,b=0,a=0;
        ok &= SDL_ReadSurfacePixel(surface,30,50,&r,&g,&b,&a) && a==0;
        ok &= SDL_ReadSurfacePixel(surface,50,50,&r,&g,&b,&a) && r==0 && g==255 && b==0 && a==255;
        ok &= SDL_ReadSurfacePixel(surface,20,20,&r,&g,&b,&a) && r==255 && g==0 && b==0 && a==255;
        ok &= SDL_ReadSurfacePixel(surface,10,50,&r,&g,&b,&a) && r==255 && g==0 && b==0 && a==255;
        SDL_DestroySurface(surface);
    } else ok=false;
    ok &= frame_stats(c).draw_call_count==3;
    begin_frame(c,{.viewport_w=100,.viewport_h=100});
    begin_box({.width=Sizing::fixed(100),.height=Sizing::fixed(100)});
    draw_rect({.rect={0,0,100,100},.fill=Color::white()});
    punch_hole({.rect={20,20,60,60},.softness=20});
    end_box();end_frame(c);
    ok &= render_headless_png(c,100,100,path,Color::black());
    if(auto* surface=SDL_LoadPNG(path)) {
        Uint8 r=0,g=0,b=0,a=0;
        ok &= SDL_ReadSurfacePixel(surface,15,50,&r,&g,&b,&a) && std::abs(int(a)-185)<=2;
        ok &= SDL_ReadSurfacePixel(surface,30,50,&r,&g,&b,&a) && a==0;
        SDL_DestroySurface(surface);
    } else ok=false;
    begin_frame(c,{.viewport_w=100,.viewport_h=100});
    begin_box({.width=Sizing::fixed(100),.height=Sizing::fixed(100)});
    draw_rect({.rect={0,0,100,100},.fill=Color::white()});
    end_box();end_frame(c);
    ok &= render_headless_png(c,100,100,path) && frame_stats(c).draw_call_count==1;
    std::remove(path);destroy_context(c);set_current_context(nullptr);
    if(!ok)std::fprintf(stderr,"punch hole regression failed\n");
    return ok;
}

inline bool verify_texture_regions_gpu() noexcept {
    using namespace etch;
    Context* c=create_context({.headless=true});
    if(!c) return false;
    uint8_t base[8*8*4];
    for(uint32_t i=0;i<64;++i) {base[4*i]=255;base[4*i+1]=0;base[4*i+2]=0;base[4*i+3]=255;}
    auto tex=create_texture_rgba(c,{.rgba_pixels=base,.width=8,.height=8});
    bool ok=tex.texture!=nullptr;
    uint8_t rows[]={0,255,255,255,0,255,255,255,0,0,0,0, 0,255,255,255,0,255,255,255};
    const uint8_t magenta[]={255,0,255,255};
    ok &= update_texture_region_rgba(c,tex.texture,2,3,2,2,rows,12);
    ok &= etch_update_texture_region_rgba(reinterpret_cast<EtchContext*>(c),tex.texture,6,0,1,1,magenta,0);
    std::memset(rows,0,sizeof(rows)); // The upload must already own the caller bytes.
    ok &= !update_texture_region_rgba(c,tex.texture,0,0,2,2,base,7);
    ok &= !update_texture_region_rgba(c,tex.texture,0,0,0,2,base);
    begin_frame(c,{.viewport_w=8,.viewport_h=8});
    begin_box({.width=Sizing::fixed(8),.height=Sizing::fixed(8)});
    draw_image({.texture=tex.texture,.rect={0,0,8,8}});
    end_box();end_frame(c);
    const char* path="texture-region-check.png";
    ok &= render_headless_png(c,8,8,path);
    SDL_Surface* surface=SDL_LoadPNG(path);
    if(surface) {
        for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
            Uint8 r=0,g=0,b=0,a=0;
            ok &= SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a);
            const bool cyan=x>=2 && x<4 && y>=3 && y<5;
            const bool pink=x==6 && y==0;
            ok &= r==(cyan ? 0 : 255) && g==(cyan ? 255 : 0) && b==((cyan || pink) ? 255 : 0) && a==255;
        }
        SDL_DestroySurface(surface);
    } else ok=false;
    std::remove(path);destroy_texture(c,&tex);destroy_context(c);set_current_context(nullptr);
    if(!ok) std::fprintf(stderr,"texture region regression failed\n");
    return ok;
}

inline bool verify_mesh_gpu() noexcept {
    using namespace etch;
    Context* c=create_context({.headless=true});if(!c)return false;
    begin_frame(c,{.viewport_w=205,.viewport_h=65});
    begin_box({.width=Sizing::fixed(205),.height=Sizing::fixed(65)});
    draw_mesh_rect({.rect={0,0,65,65},.color_tl=Color::black(),.color_tr=Color::white(),
        .color_br=Color::white(),.color_bl=Color::black()});
    auto d=etch_mesh_rect_draw_desc_default();
    d.rect={70,0,65,65};d.color_tl=etch_rgba8(0,0,0,255);d.color_tr=etch_rgba8(255,255,255,255);
    d.color_br=d.color_tr;d.color_bl=d.color_tl;d.oklab=false;
    etch_draw_mesh_rect(&d);
    begin_box({.width=Sizing::fixed(65),.height=Sizing::fixed(65),.radius=Corners::all(16),.clip_children=true,
        .attach_to=AttachTo::Parent,.anchor_parent=Anchor::TopLeft,.floating_offset={140,0},.pointer_passthrough=true});
    draw_mesh_rect({.rect={0,0,65,65},.color_tl=Color::white(),.color_tr=Color::white(),.color_br=Color::white(),.color_bl=Color::white()});
    end_box();end_box();end_frame(c);
    bool ok=frame_prims(c).size()==3;
    const char* path="mesh-gradient-check.png";
    ok &= render_headless_png(c,205,65,path,Color::black());
    if(auto* surface=SDL_LoadPNG(path)) {
        for(int panel=0;panel<2;++panel) {
            Uint8 r=0,g=0,b=0,a=0;
            ok &= SDL_ReadSurfacePixel(surface,32+70*panel,32,&r,&g,&b,&a);
            const int expected=panel ? 188 : 99;
            ok &= std::abs(int(r)-expected)<=2 && r==g && g==b && a==255;
        }
        Uint8 r=0,g=0,b=0,a=0;
        ok &= SDL_ReadSurfacePixel(surface,140,0,&r,&g,&b,&a) && r==0 && g==0 && b==0;
        ok &= SDL_ReadSurfacePixel(surface,172,32,&r,&g,&b,&a) && r==255 && g==255 && b==255;
        SDL_DestroySurface(surface);
    } else ok=false;
    ok &= frame_stats(c).draw_call_count==1;
    begin_frame(c,{.viewport_w=130,.viewport_h=65});
    begin_box({.width=Sizing::fixed(130),.height=Sizing::fixed(65)});
    for(int panel=0;panel<2;++panel) {
        draw_mesh_rect({.rect={float(65*panel),0,65,65},.color_tl=Color::rgba8(255,0,0),
            .color_tr=Color::rgba8(0,255,0),.color_br=Color::rgba8(0,0,255),.color_bl=Color::white(),
            .grain_intensity=panel ? .08f : 0});
    }
    end_box();end_frame(c);
    ok &= render_headless_png(c,130,65,path,Color::transparent());
    if(auto* surface=SDL_LoadPNG(path)) {
        const int xs[]={1,63,63,1},ys[]={1,1,63,63};
        for(int corner=0;corner<4;++corner) {
            Uint8 r=0,g=0,b=0,a=0;
            ok &= SDL_ReadSurfacePixel(surface,xs[corner],ys[corner],&r,&g,&b,&a);
            ok &= a==255;
            if(corner==0)ok &= r>230 && g<80 && b<80;
            if(corner==1)ok &= r<80 && g>230 && b<80;
            if(corner==2)ok &= r<80 && g<80 && b>230;
            if(corner==3)ok &= r>230 && g>230 && b>230;
        }
        int changed=0;
        for(int x=15;x<50;++x) {
            Uint8 r=0,g=0,b=0,a=0,rr=0,gg=0,bb=0,aa=0;
            ok &= SDL_ReadSurfacePixel(surface,x,32,&r,&g,&b,&a);
            ok &= SDL_ReadSurfacePixel(surface,x+65,32,&rr,&gg,&bb,&aa);
            changed += r!=rr || g!=gg || b!=bb;
        }
        ok &= changed>25;
        SDL_DestroySurface(surface);
    } else ok=false;
    std::remove(path);destroy_context(c);set_current_context(nullptr);
    if(!ok)std::fprintf(stderr,"mesh gradient regression failed\n");
    return ok;
}
