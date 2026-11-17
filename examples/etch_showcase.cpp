// Public-API material, motion, dashboard and terminal showcase.
#include <etch/etch.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include "etch_surface_demo_dxil.h"
#include "etch_surface_demo_spv.h"
#include "etch_surface_demo_msl.h"

namespace {
using namespace etch;
struct Showcase {
    ShaderSurfaceId aurora=0;
    TerminalCell cells[8*64]={};
    uint32_t tab=0,snap=1;
    bool drawer=true;
};
void label(const char* value,float size,Color color,uint16_t font=kFontSans) noexcept {
    text(str_cstr(value),{.color=color,.font_size_px=size,.font_id=font,.wrap=false});
}
void fill_terminal(Showcase* app) noexcept {
    const char* lines[]={"$ etch run --backend native","  > agent.plan    connected","  > shader.aurora compiled in 4ms",
        "  > ui.batch      1 draw / 2,418 prims","  > motion        springs settled","  > tests         all checks passed",
        "  ready for the next idea","_"};
    for(uint32_t row=0;row<8;++row) for(uint32_t col=0;col<64;++col) {
        auto& cell=app->cells[row*64+col];cell={};
        const size_t len=std::strlen(lines[row]);cell.codepoint=col<len ? uint32_t(lines[row][col]) : ' ';
        cell.foreground={TerminalColorMode::Ansi256,row==0 ? 117u : row==5 ? 84u : 250u};
    }
}
void node(const char* title,const char* subtitle,Vec2 pos,Color accent) noexcept {
    begin_box({.id=id(title),.axis=Axis::Col,.width=Sizing::fixed(192),.height=Sizing::fixed(94),
        .padding=Padding::all(16),.gap=8,.background=Color::rgba8(17,26,42,245),.radius=Corners::all(14),
        .attach_to=AttachTo::Parent,.anchor_parent=Anchor::TopLeft,.floating_offset=pos,.z_index=2,
        .hover_border_color=accent,.hover_scale=1.025f,.press_scale=.98f,
        .border_beam={.color=accent,.base_color=Color::rgba8(80,120,160,90),.width=1,.angle_rad=.8f,.glow_px=9},.top_rim_alpha=.3f});
    label(title,15,Color::white(),kFontBold);label(subtitle,11,Color::rgb_hex(0xA5B9CC),kFontMono);
    end_box();
}
void build_ui(Context* ctx,Showcase* app) noexcept {
    const Color muted=Color::rgb_hex(0x8D9BAE),mint=Color::oklch(.82f,.14f,165);
    begin_box({.axis=Axis::Col,.width=Sizing::grow(),.height=Sizing::grow(),.padding=Padding::all(28),.gap=24,.background=Color::rgb_hex(0x0A101C)});
    begin_box({.width=Sizing::grow(),.height=Sizing::fixed(48),.gap=14,.align_y=AlignY::Center});
    label("etch /",30,mint,kFontBold);label("two worlds, one surface",18,Color::rgb_hex(0xDCE7F2));
    box({.width=Sizing::grow()});label("NATIVE  ·  " ETCH_VERSION_STRING,11,muted,kFontMono);end_box();
    begin_box({.width=Sizing::grow(),.height=Sizing::grow(),.gap=24});
    begin_box({.axis=Axis::Col,.width=Sizing::fixed(322),.height=Sizing::grow(),.gap=18});
    label("YOUR NETWORK",11,mint,kFontMono);label("Everything flows.",27,Color::white(),kFontBold);
    label("A quiet space for a busy connection.",13,muted);
    begin_box({.width=Sizing::grow(),.height=Sizing::fixed(42),.padding=Padding::all(4),.gap=4,.background=Color::rgb_hex(0x192335),.radius=Corners::all(24)});
    const char* tabs[]={"Overview","Traffic","Routes"};
    for(uint32_t i=0;i<3;++i) {
        const uint32_t tab_id=id(tabs[i]);
        begin_box({.id=tab_id,.width=Sizing::grow(),.height=Sizing::grow(),.align_x=AlignX::Center,.align_y=AlignY::Center,.radius=Corners::all(20)});
        if(i==app->tab) box({.width=Sizing::grow(),.height=Sizing::grow(),.background=Color::rgb_hex(0x375B58),.radius=Corners::all(20),
            .attach_to=AttachTo::Parent,.anchor_parent=Anchor::TopLeft,.z_index=-1,.pointer_passthrough=true,.hero_id=id("sliding-pill"),.transition=SpringSpec::fluid()});
        label(tabs[i],12,i==app->tab ? mint : muted);end_box();if(clicked(tab_id))app->tab=i;
    }
    end_box();
    begin_box({.axis=Axis::Col,.width=Sizing::grow(),.height=Sizing::fixed(186),.padding=Padding::all(22),.gap=10,
        .background=Color::rgb_hex(0x132A2B),.radius=Corners::all(20),.border_beam={.color=mint,.glow_px=3},.top_rim_alpha=.2f});
    label("CONNECTED",11,mint,kFontMono);label("Tokyo · 03",26,Color::white(),kFontBold);label("32 ms   /   Direct route",13,Color::rgb_hex(0x9EBBB7));
    box({.height=Sizing::grow()});
    begin_box({.gap=34});label("↓  128.4 Mbps",14,mint,kFontMono);label("↑  24.8",14,mint,kFontMono);end_box();end_box();
    begin_box({.axis=Axis::Col,.width=Sizing::grow(),.height=Sizing::fixed(160),.padding=Padding::all(20),.gap=12,
        .background=Color::rgb_hex(0x141E2E),.radius=Corners::all(18)});
    label("Traffic, last 24 hours",13,Color::white());
    begin_box({.width=Sizing::grow(),.height=Sizing::fixed(75)});
    for(uint32_t i=0;i<28;++i) {const float h=12+float((i*17+9)%54);draw_rect({.rect={float(i)*10,75-h,6,h},.fill=Color::oklch(.55f+float(i%5)*.05f,.1f,165),.radius=Corners::all(3)});}
    end_box();end_box();
    if(button({.id=id("drawer-toggle"),.label=str_lit("Connection details  ↗"),.width=Sizing::grow()}))app->drawer=!app->drawer;
    end_box();
    begin_box({.axis=Axis::Col,.width=Sizing::grow(),.height=Sizing::grow(),.gap=14});
    begin_box({.width=Sizing::grow(),.height=Sizing::fixed(28),.align_y=AlignY::Center});
    label("CROMMA / MATERIAL WORKSPACE",11,muted,kFontMono);box({.width=Sizing::grow()});label("●  LIVE",11,mint,kFontMono);end_box();
    begin_box({.id=id("aurora-canvas"),.width=Sizing::grow(),.height=Sizing::fixed(318),.background=Color::rgb_hex(0x102030),.radius=Corners::all(20),.clip_children=true});
    const auto bounds=element_bounds(id("aurora-canvas"));
    ShaderSurfaceEvalDesc eval={.width=768,.height=318};eval.params[0].w=-1;
    if(auto* texture=eval_shader_surface(ctx,app->aurora,eval))draw_image({.texture=texture,.rect={0,0,bounds.w>0 ? bounds.w : 768,318}});
    const Vec2 line1[]={{175,110},{280,110},{340,190},{455,190}};
    draw_polyline({.points=line1,.color=mint,.thickness=1.5f,.glow_color=Color::rgba8(80,240,190,110),.glow_px=8});
    node("01 / Planner","Reasoning · ready",{32,64},mint);
    node("02 / Builder","Material pass · active",{390,152},Color::oklch(.75f,.16f,285));
    end_box();
    begin_box({.axis=Axis::Col,.width=Sizing::grow(),.height=Sizing::grow(),.padding=Padding::all(18),.gap=12,.background=Color::rgb_hex(0x101826),.radius=Corners::all(16),.clip_children=true});
    label("TERMINAL   /   native Slug glyphs",10,muted,kFontMono);
    terminal_grid({.id=id("showcase-terminal"),.cells=app->cells,.columns=64,.rows=8,.cell_width_px=9,.line_height_px=20,.font_size_px=12,
        .foreground=Color::white(),.background=Color::rgb_hex(0x101826),.cursor_column=1,.cursor_row=7,.cursor_color=mint});
    end_box();end_box();end_box();end_box();
    if(begin_sheet({.id=id("connection-sheet"),.open=app->drawer,.extent_px=120,.snap_points={0,.65f,1},.snap_count=3,.snap_index=&app->snap,
            .background=Color::rgb_hex(0x1B2A3A)})) {
        begin_box({.width=Sizing::grow(),.height=Sizing::grow(),.padding=Padding::xy(32,4),.gap=36,.align_y=AlignY::Center});
        label("Connection details",16,Color::white(),kFontBold);label("AES-256-GCM  /  Tokyo  /  32 ms",12,mint,kFontMono);
        box({.width=Sizing::grow()});label("DRAG TO SNAP  ↑ ↓",10,muted,kFontMono);end_box();end_sheet();
    }
}
}
int main(int argc,char** argv) {
    const bool headless=argc>1 && std::strcmp(argv[1],"--headless")==0;
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS))return 1;
    SDL_Window* window=headless ? nullptr : SDL_CreateWindow("Etch Showcase " ETCH_VERSION_STRING,1200,800,SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if(!headless && !window){SDL_Quit();return 1;}
    Context* ctx=create_context({.window=window,.headless=headless});if(!ctx){if(window)SDL_DestroyWindow(window);SDL_Quit();return 1;}
    set_theme(Theme::from_seed(Color::oklch(.8f,.14f,165)),ctx);
    Showcase app;fill_terminal(&app);
    app.aurora=create_shader_surface(ctx,{.spirv={g_etch_surface_demo_spv,sizeof(g_etch_surface_demo_spv)},
        .dxil={g_etch_surface_demo_dxil,sizeof(g_etch_surface_demo_dxil)},.msl={g_etch_surface_demo_msl,sizeof(g_etch_surface_demo_msl)},.continuous_time=!headless,.max_fps=30});
    bool ok=app.aurora!=0,running=ok;
    if(headless) {
        for(int i=0;i<90;++i){begin_frame(ctx,{.viewport_w=1200,.viewport_h=800,.dt_seconds=1.f/60});build_ui(ctx,&app);end_frame(ctx);}
        ok &= render_headless_png(ctx,1200,800,argc>2 ? argv[2] : "showcase.png");
        ok &= frame_stats(ctx).draw_call_count<=3 && element_bounds(id("aurora-canvas")).w>500;
        std::printf("Showcase material/layout/batch checks: %s\n",ok ? "PASS" : "FAIL");
    } else {
        uint64_t previous=SDL_GetTicksNS();
        while(running) {
            SDL_Event event;
            if(wait_events(ctx,&event))do {process_event(ctx,event);if(event.type==SDL_EVENT_QUIT || event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED)running=false;}while(SDL_PollEvent(&event));
            if(!running)break;
            const uint64_t now=SDL_GetTicksNS();const float dt=float(double(now-previous)/1e9);previous=now;
            begin_window_frame(ctx,window,dt);build_ui(ctx,&app);end_window_frame(ctx);ok &= render_window(ctx);
        }
    }
    destroy_context(ctx);if(window)SDL_DestroyWindow(window);SDL_Quit();return ok ? 0 : 1;
}
