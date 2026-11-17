#include "etch_surface_demo_dxil.h"
#include "etch_surface_demo_spv.h"
#include "etch_surface_demo_msl.h"

inline bool verify_shader_surface_gpu() noexcept {
    using namespace etch;
    Context* c = create_context({.headless=true,.respect_reduced_motion=false});
    if (!c) return false;
    const ShaderSurfaceCreateDesc code = {
        .spirv={g_etch_surface_demo_spv,sizeof(g_etch_surface_demo_spv)},
        .dxil={g_etch_surface_demo_dxil,sizeof(g_etch_surface_demo_dxil)},
        .msl={g_etch_surface_demo_msl,sizeof(g_etch_surface_demo_msl)}};
    const auto id = create_shader_surface(c,code);
    bool ok = id != 0 && create_shader_surface(c,{}) == 0;
    const char* path = "surface-check.png";
    SDL_GPUTexture* previous = nullptr;
    for (uint32_t frame=0;frame<6;++frame) {
        begin_frame(c,{.viewport_w=32,.viewport_h=32});
        auto eval = etch_shader_surface_eval_desc_default();
        eval.width=eval.height=frame<3 ? 16u : 32u;
        eval.params[0]={frame<2 ? .25f : frame==5 ? .5f : .75f,0,0,frame==5 ? .5f : 1.f};
        auto* texture=etch_eval_shader_surface(reinterpret_cast<EtchContext*>(c),id,&eval);
        ok &= texture != nullptr;
        if(frame==1) ok &= texture==previous; // Unchanged material reuses the target.
        previous=texture;
        begin_box({.width=Sizing::fixed(32),.height=Sizing::fixed(32)});
        draw_image({.texture=texture,.rect={0,0,32,32}});
        end_box();end_frame(c);
        ok &= render_headless_png(c,32,32,path,Color::black());
        if (SDL_Surface* image=SDL_LoadPNG(path)) {
            Uint8 r=0,g=0,b=0,a=0;
            ok &= SDL_ReadSurfacePixel(image,16,16,&r,&g,&b,&a) &&
                std::abs(int(r)-(frame<2 ? 137 : frame==5 ? 188 : 225))<=2 && g==0 && b==0 && a==255;
            SDL_DestroySurface(image);
        } else ok=false;
    }
    destroy_shader_surface(c,id);
    const auto replacement=create_shader_surface(c,code);
    ok &= replacement && replacement!=id;
    begin_frame(c,{.viewport_w=32,.viewport_h=32});
    ok &= eval_shader_surface(c,id,{})==nullptr && eval_shader_surface(c,replacement,{.width=8193})==nullptr;
    end_frame(c);
    destroy_shader_surface(c,id); // Stale destroy must leave the replacement alive.
    begin_frame(c,{.viewport_w=32,.viewport_h=32});
    ok &= eval_shader_surface(c,replacement,{})!=nullptr;
    end_frame(c);
    destroy_shader_surface(c,replacement);
    auto animated=code;animated.continuous_time=true;animated.max_fps=1;
    const auto ticking=create_shader_surface(c,animated);
    for(int i=0;i<3;++i) {
        begin_frame(c,{.viewport_w=32,.viewport_h=32});
        ok &= eval_shader_surface(c,ticking,{})!=nullptr;
        end_frame(c);
        ok &= render_headless_png(c,32,32,path,Color::black());
    }
    ok &= !needs_redraw(c); // A 1fps surface must sleep between deadlines.
    SDL_Delay(1050);
    ok &= needs_redraw(c);
    set_time_scale(c,0);
    for(int i=0;i<3;++i) {begin_frame(c,{.viewport_w=32,.viewport_h=32});eval_shader_surface(c,ticking,{});end_frame(c);}
    ok &= !needs_redraw(c);
    destroy_shader_surface(c,ticking);
    destroy_context(c);set_current_context(nullptr);
    std::remove(path);
    if(!ok)std::fprintf(stderr,"ShaderSurface cache/readback/lifetime/scheduling failure\n");
    return ok;
}
