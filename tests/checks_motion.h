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
    for(const auto& p:frame_prims(c)) if((p.style.x&15u)==uint32_t(PrimKind::SlugGlyph)) {
        ++glyphs;ok &= p.clip.x>=250 && p.bounds.x>=250;
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
