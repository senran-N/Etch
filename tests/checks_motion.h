// Motion lifecycle and state-map relocation regressions.
inline bool verify_motion_checks() noexcept {
    using namespace etch;
    Context* c=create_context({.frame_arena_bytes=32u*1024u*1024u,.headless=true,.respect_reduced_motion=false});
    if(!c) return false;
    bool ok=true;
    const uint32_t color=Color::rgba8(255,0,0).pack_rgba8();
    for(uint32_t f=0;f<100;++f) {
        begin_frame(c,{.viewport_w=500,.viewport_h=200,.dt_seconds=1.f/60});
        push_layer({.opacity=.5f,.translate={10.25f,0},.animating=true});
        box({.id=id("layer_test"),.width=Sizing::fixed(20),.height=Sizing::fixed(20),.background=Color::rgba8(255,0,0),.opacity=.5f});
        pop_layer();
        box({.id=id("plain_test"),.width=Sizing::fixed(20),.height=Sizing::fixed(20),.background=Color::rgba8(255,0,0)});
        const auto pr=presence(id("presence_lifecycle"),f<40);
        if(f==39) ok &= pr.visible && pr.opacity>.99f && !pr.exiting;
        if(f==40) ok &= pr.visible && pr.exiting;
        if(f==99) ok &= !pr.visible && pr.opacity==0;
        end_frame(c);
        const auto ps=frame_prims(c);
        ok &= ps.size()==2;
        if(ps.size()==2) ok &= std::fabs(ps[0].bounds.x-10.25f)<.01f && (ps[0].style.y>>24)==64 && ps[1].style.y==color && ps[1].bounds.x==20;
    }
    for(uint32_t f=0;f<100;++f) {
        begin_frame(c,{.viewport_w=500,.viewport_h=200,.dt_seconds=1.f/60});
        box({.width=Sizing::fixed(f<2 ? 40.f : 100.f),.height=Sizing::fixed(20)});
        begin_box({.id=id_index(id("hero_parent"),f<2 ? 0u : 1u),.width=Sizing::fixed(20),.height=Sizing::fixed(20)});
        box({.id=id_index(id("hero_instance"),f<2 ? 0u : 1u),.width=Sizing::fixed(20),.height=Sizing::fixed(20),.background=Color::rgba8(0,255,0),.animate_layout=true,.hero_id=id("hero_pill")});
        end_box();
        end_frame(c);
        const auto ps=frame_prims(c);
        ok &= ps.size()==1;
        if(ps.size()==1) {
            if(f<2) ok &= ps[0].bounds.x==40;
            if(f==2) ok &= ps[0].bounds.x>40 && ps[0].bounds.x<100;
            if(f==99) ok &= ps[0].bounds.x==100;
        }
    }
    // Scope destructors must restore opacity on early control flow.
    begin_frame(c,{.viewport_w=500,.viewport_h=200,.dt_seconds=1.f/60});
    ETCH_LAYER({.opacity=.25f}) { if(frame_prims(c).empty()) break; }
    const auto early_return=[c]() noexcept {
        ETCH_LAYER({.opacity=.25f}) { if(frame_prims(c).empty()) return; }
    };
    early_return();
    ETCH_PRESENCE(id("scope_presence"),true) { if(frame_prims(c).empty()) break; }
    const auto presence_return=[c]() noexcept {
        ETCH_PRESENCE(id("return_presence"),true) { if(frame_prims(c).empty()) return; }
    };
    presence_return();
    box({.width=Sizing::fixed(20),.height=Sizing::fixed(20),.background=Color::white()});
    end_frame(c);
    ok &= frame_prims(c).size()==1 && (frame_prims(c)[0].style.y>>24)==255;

    // Pausing freezes spring position; slow motion advances less than real time.
    float paused=0;
    for(uint32_t f=0;f<3;++f) {
        set_time_scale(c,f==1 ? 0.f : .2f);
        begin_frame(c,{.viewport_w=500,.viewport_h=200,.dt_seconds=1.f/60});
        const auto pr=presence(id("paused_presence"),true);
        if(f==0) paused=pr.opacity;
        if(f==1) ok &= pr.opacity==paused;
        if(f==2) ok &= pr.opacity>paused && pr.opacity<.5f;
        end_frame(c);
    }
    set_time_scale(c,1);

    // Child clips follow their layer while the viewport remains fixed.
    begin_frame(c,{.viewport_w=500,.viewport_h=200,.dt_seconds=1.f/60});
    ETCH_LAYER({.translate={30,20},.scale={2,2},.pivot_px={0,0}}) {
        begin_box({.width=Sizing::fixed(20),.height=Sizing::fixed(20),.clip_children=true});
        box({.width=Sizing::fixed(10),.height=Sizing::fixed(10),.background=Color::white()});
        end_box();
    }
    end_frame(c);
    if(frame_prims(c).size()==1) {
        const auto& p=frame_prims(c)[0];
        ok &= p.bounds.x==30 && p.bounds.y==20 && p.bounds.z==20;
        ok &= p.clip.x==30 && p.clip.y==20 && p.clip.z==70 && p.clip.w==60;
    } else ok=false;

    // Offscreen layout can be translated into the visible viewport.
    begin_frame(c,{.viewport_w=500,.viewport_h=200,.dt_seconds=1.f/60});
    box({.width=Sizing::fixed(600),.height=Sizing::fixed(20)});
    ETCH_LAYER({.translate={-500,0}}) {
        box({.width=Sizing::fixed(20),.height=Sizing::fixed(20),.background=Color::white()});
    }
    end_frame(c);
    ok &= frame_prims(c).size()==1 && frame_prims(c)[0].bounds.x==100;

    // Translating farther than an input width must retain its glyphs.
    char input[16]="Layer text";
    begin_frame(c,{.viewport_w=500,.viewport_h=200,.dt_seconds=1.f/60});
    ETCH_LAYER({.translate={250,0}}) {
        text_input({.id=id("layer_input"),.buffer=input,.capacity=16,.width=Sizing::fixed(180)});
    }
    end_frame(c);
    uint32_t glyphs=0;
    for(const auto& p:frame_prims(c)) {
        const uint32_t k = p.style.x & 15u;
        if(k==uint32_t(PrimKind::SlugGlyph) || k==uint32_t(PrimKind::Glyph)) {
            ++glyphs;ok &= p.clip.x>=250 && p.bounds.x>=250;
        }
    }
    ok &= glyphs>0;

    set_time_scale(c,0);
    for(uint32_t f=0;f<10;++f) {
        begin_frame(c,{.viewport_w=500,.viewport_h=200,.dt_seconds=1.f/60});
        presence(id("paused_presence"),true);
        end_frame(c);
    }
    ok &= !needs_redraw(c);
    set_time_scale(c,1);

    begin_frame(c,{.viewport_w=500,.viewport_h=200,.dt_seconds=1.f/60});
    ETCH_LAYER({.translate={250,0}}) {
        begin_canvas({.id=id("layer_canvas"),.width=Sizing::fixed(100),.height=Sizing::fixed(100)});
        end_canvas();
    }
    end_frame(c);
    bool grid=false;
    for(const auto& p:frame_prims(c)) if((p.style.x&15u)==uint32_t(PrimKind::CanvasGrid)) {
        grid=true;ok &= p.clip.x>=250 && p.bounds.x>=250;
    }
    ok &= grid;

    // Nested FLIP must inherit the parent's displacement exactly once.
    for(uint32_t f=0;f<4;++f) {
        begin_frame(c,{.viewport_w=500,.viewport_h=200,.dt_seconds=1.f/60});
        box({.width=Sizing::fixed(f<2 ? 0.f : 100.f),.height=Sizing::fixed(20)});
        begin_box({.id=id("nested_flip_parent"),.width=Sizing::fixed(40),.height=Sizing::fixed(20),.background=Color::rgba8(255,0,0),.animate_layout=true});
        box({.id=id("nested_flip_child"),.width=Sizing::fixed(20),.height=Sizing::fixed(20),.background=Color::white(),.animate_layout=true});
        end_box();end_frame(c);
        const auto ps=frame_prims(c);
        ok &= ps.size()==2;
        if(ps.size()==2) ok &= std::fabs(ps[0].bounds.x-ps[1].bounds.x)<.01f;
    }

    for(uint32_t f=0;f<4;++f) {
        begin_frame(c,{.viewport_w=500,.viewport_h=200,.dt_seconds=1.f/60});
        box({.width=Sizing::fixed(f<2 ? 0.f : 100.f),.height=Sizing::fixed(20)});
        begin_box({.id=id("nested_hero_parent"),.width=Sizing::fixed(40),.height=Sizing::fixed(20),.background=Color::rgba8(255,0,0),.animate_layout=true});
        box({.id=id("nested_hero_child"),.width=Sizing::fixed(20),.height=Sizing::fixed(20),.background=Color::white(),.hero_id=id("nested_hero")});
        end_box();end_frame(c);
        const auto ps=frame_prims(c);
        ok &= ps.size()==2;
        if(ps.size()==2) ok &= std::fabs(ps[0].bounds.x-ps[1].bounds.x)<.01f;
    }

    begin_frame(c,{.viewport_w=500,.viewport_h=200,.dt_seconds=1.f/60});
    ETCH_LAYER({.translate={30,0},.scale={2,2},.pivot_px={0,0}}) {
        box({.id=id("explicit_hero_child"),.width=Sizing::fixed(20),.height=Sizing::fixed(20),.background=Color::white(),.hero_id=id("explicit_hero")});
    }
    end_frame(c);
    ok &= frame_prims(c).size()==1 && frame_prims(c)[0].bounds.x==30 && frame_prims(c)[0].bounds.z==40;

    // Portal bridge: combine Canvas zoom, nested visual layers and logical DPI.
    const uint32_t portal = id("portal_bridge");
    for (uint32_t f=0; f<2; ++f) {
        begin_frame(c,{.viewport_w=1000,.viewport_h=800,.dpi_scale=2,.dt_seconds=1.f/60});
        begin_canvas({.id=id("bridge_canvas"),.width=Sizing::fixed(400),.height=Sizing::fixed(300),
                      .default_pan={120,80},.default_zoom=.65f,.show_grid=false});
        begin_canvas_node({.id=id("bridge_node"),.pos_value={20,30},.width_world=Sizing::fixed(200),.height_world=Sizing::fixed(150)});
        ETCH_LAYER({.translate={4,6},.scale={1.5f,2},.pivot_px={0,0}}) {
            ETCH_LAYER({.translate={3,2},.scale={2,.5f},.pivot_px={0,0}}) {
                box({.id=portal,.width=Sizing::fixed(80),.height=Sizing::fixed(60),.background=Color::white()});
            }
        }
        end_canvas_node();end_canvas();end_frame(c);
        ElementGeometry g;
        ok &= element_geometry(portal,&g);
        ok &= std::fabs(g.local_scale.x-1.95f)<.001f && std::fabs(g.local_scale.y-.65f)<.001f;
        ElementGeometry node_geometry;
        ok &= element_geometry(id("bridge_node"),&node_geometry) && std::fabs(node_geometry.local_scale.x-.65f)<.001f;
        Vec2 local = {};
        ok &= screen_to_element_local(portal,{g.bounds.x+g.local_scale.x*17,g.bounds.y+g.local_scale.y*23},&local);
        ok &= std::fabs(local.x-17)<.001f && std::fabs(local.y-23)<.001f;
        EtchElementGeometry cg = {};
        EtchVec2 cp = {};
        ok &= etch_element_geometry(portal,&cg) && etch_screen_to_element_local(portal,{g.bounds.x,g.bounds.y},&cp);
        ok &= cg.bounds.x==g.bounds.x && cg.local_scale.x==g.local_scale.x && cp.x==0 && cp.y==0;
        ok &= g.visible_bounds.w<=g.bounds.w && g.visible_bounds.h<=g.bounds.h;
    }
    ElementGeometry canvas_geometry;
    ok &= element_geometry(id("bridge_canvas"),&canvas_geometry) && canvas_geometry.local_scale.x==1;
    begin_frame(c,{.viewport_w=500,.viewport_h=200});
    box({.id=id("culled_bridge"),.width=Sizing::fixed(40),.height=Sizing::fixed(30),
         .attach_to=AttachTo::Root,.floating_offset={800,900}});
    begin_virtual_list({.id=id("bridge_list"),.item_count=1,.item_height_px=20});end_virtual_list();
    end_frame(c);
    ElementGeometry culled, list_geometry;
    ok &= element_geometry(id("culled_bridge"),&culled) && culled.bounds.x==800 && culled.visible_bounds.w==0;
    ok &= element_geometry(id("bridge_list"),&list_geometry) && list_geometry.local_scale.x==1;
    Vec2 captured={};
    ok &= screen_to_element_local(id("culled_bridge"),{culled.bounds.x+10,culled.bounds.y+20},&captured) && captured.x==10 && captured.y==20;
    Vec2 untouched={7,9};
    ok &= !screen_to_element_local(0,{},&untouched) && untouched.x==7 && untouched.y==9;
    ok &= !screen_to_element_local(portal,{NAN,0},&untouched) && untouched.x==7;
    ok &= !element_geometry(portal,nullptr);
    begin_frame(c,{.viewport_w=500,.viewport_h=200});
    ElementGeometry previous;
    ok &= element_geometry(id("culled_bridge"),&previous); // Snapshot survives the build phase.
    end_frame(c);
    ok &= !element_geometry(id("culled_bridge"),&previous); // Omitted elements have no stale geometry.

    // Rehash repeatedly while earlier layers retain source boxes, never WidgetState pointers.
    begin_frame(c,{.viewport_w=500,.viewport_h=200,.dt_seconds=1.f/60});
    for(uint32_t i=0;i<5000;++i) {
        const uint32_t key=id_index(id("rehash_motion"),i);
        presence(key,true);
        box({.id=id_index(key,1),.width=Sizing::fixed(1),.height=Sizing::fixed(1),.background=Color::white(),.animate_layout=true,.hero_id=id_index(key,2)});
    }
    end_frame(c);
    for(uint32_t f=0;f<90;++f) {
        begin_frame(c,{.viewport_w=500,.viewport_h=200,.dt_seconds=1.f/60});
        const auto pr=presence(id("presence_lifecycle"),false);
        ok &= !pr.visible;
        end_frame(c);
    }
    ok &= !needs_redraw(c);
    destroy_context(c);set_current_context(nullptr);
    if(!ok)std::fprintf(stderr,"motion regression failed\n");
    return ok;
}
