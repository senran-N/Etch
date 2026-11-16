#include "etch/etch.h"
#include "etch/etch_c.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include "checks_desktop.h"
#include "checks_desktop_precision.h"
#include "checks_editor.h"
#include "checks_a11y.h"
#include "checks_media_emoji.h"
#include "checks_theme_wrap.h"
#include "checks_os_window_a11y.h"
#include "etch_uber_vs_msl.h"
#include "etch_uber_ps_msl.h"
#include "etch_kawase_vs_msl.h"
#include "etch_kawase_ps_msl.h"
using namespace etch;
#if defined(ETCH_CMAKE_VERSION_MAJOR)
static_assert(ETCH_VERSION_MAJOR == ETCH_CMAKE_VERSION_MAJOR && ETCH_VERSION_MINOR == ETCH_CMAKE_VERSION_MINOR && ETCH_VERSION_PATCH == ETCH_CMAKE_VERSION_PATCH);
#define ETCH_EXPECTED_VERSION_STRING ETCH_VERSION_STR(ETCH_CMAKE_VERSION_MAJOR) "." ETCH_VERSION_STR(ETCH_CMAKE_VERSION_MINOR) "." ETCH_VERSION_STR(ETCH_CMAKE_VERSION_PATCH)
#else
#define ETCH_EXPECTED_VERSION_STRING ETCH_VERSION_STRING
#endif
static bool arena_suite() noexcept {
    static_assert(sizeof(EtchPrim) == sizeof(Prim) && sizeof(EtchGlobalsUniform) == sizeof(GlobalsUniform));
    Arena a=arena_create(128);if(!a.base)return false;
    auto* p=a.alloc<uint32_t>(8);const bool ok=p && (reinterpret_cast<uintptr_t>(p)%alignof(uint32_t)==0) && a.remaining()==96 && std::strcmp(ETCH_VERSION_STRING, ETCH_EXPECTED_VERSION_STRING) == 0 && etch_id("x") == id("x");
    a.reset();const bool reset=a.offset==0 && a.alloc<uint32_t>(8)==p;arena_destroy(&a);
    EtchArena ca = etch_arena_create(64);
    const bool ca_ok = ca.base && etch_arena_alloc_bytes(&ca, 16, 8) != nullptr;
    etch_arena_destroy(&ca);
    Context* c=create_context({.max_prims_per_frame=64,.headless=true});if(!c)return false;
    begin_frame(c,{.viewport_w=800,.viewport_h=500});for(uint32_t i=0;i<256;++i)box({.width=Sizing::fixed(1),.height=Sizing::fixed(1),.background=Color::white()});end_frame(c);
    const bool elastic=frame_stats(c).prim_capacity>64;destroy_context(c);set_current_context(nullptr);return ok && reset && ca_ok && elastic;
}
static bool unicode_suite() noexcept {
    Context* c=create_context({.headless=true});if(!c)return false;set_current_context(c);
    char data[64]="A\xCC\x81 \xD7\x90\xD7\x91 \xF0\x9F\x91\xA9";const uint32_t tid=id("unicode");
    begin_frame(c,{.viewport_w=400,.viewport_h=200});text_input({.id=tid,.buffer=data,.capacity=64});end_frame(c);
    const auto* n=find_a11y_node(tid,c);bool ok=n && n->value_text.length==std::strlen(data) && n->bounds.w>0;
    request_focus(tid);set_text_selection(tid,3,3);
    begin_frame(c,{.viewport_w=400,.viewport_h=200,.key_pressed_code=SDLK_LEFT});const auto moved=text_input({.id=tid,.buffer=data,.capacity=64});end_frame(c);
    ok &= moved.cursor_byte==0;
    begin_frame(c,{.viewport_w=400,.viewport_h=200,.key_pressed_code=SDLK_DELETE});text_input({.id=tid,.buffer=data,.capacity=64});end_frame(c);
    ok &= data[0]==' ' && static_cast<unsigned char>(data[1])==0xD7;
    TextBuffer b={};ok &= init_text_buffer(&b,str_cstr(data));ok &= text_buffer_line_count(&b)==1;free_text_buffer(&b);
    destroy_context(c);set_current_context(nullptr);return ok;
}
static bool layout_suite() noexcept {
    Context* c=create_context({.headless=true});if(!c)return false;
    begin_frame(c,{.viewport_w=400,.viewport_h=200});begin_box({.id=id("grid"),.width=Sizing::fixed(400),.height=Sizing::fixed(200),.gap=10,.grid_columns=2});
    box({.id=id("left"),.width=Sizing::grow(),.height=Sizing::fixed(40)});box({.id=id("right"),.width=Sizing::grow(),.height=Sizing::fixed(40)});end_box();end_frame(c);
    const auto* l=find_a11y_node(id("left"),c);const auto* r=find_a11y_node(id("right"),c);
    const bool ok=l && r && l->bounds.w>0 && r->bounds.x>=l->bounds.x+l->bounds.w && l->parent_id==id("grid");destroy_context(c);set_current_context(nullptr);return ok;
}
static bool list_suite() noexcept {
    Context* c=create_context({.headless=true});if(!c)return false;VirtualListSlice slice={};
    for(uint32_t i=0;i<3;++i){begin_frame(c,{.viewport_w=400,.viewport_h=200});slice=begin_virtual_list({.id=id("million"),.item_count=1000000,.item_height_px=20,.height=Sizing::fixed(200),.variable_height=true});end_virtual_list();end_frame(c);if(i==0)virtual_list_scroll_to_row(id("million"),999900,20,AlignY::Top);}
    const bool ok=slice.first_index>999880 && slice.end_index-slice.first_index<20 && frame_stats(c).arena_used_bytes<1024*1024;destroy_context(c);set_current_context(nullptr);return ok;
}
static bool source_ok(const unsigned char* p,size_t size,const char* entry) noexcept {
    if(size<2 || p[size-1]!=0 || std::strlen(reinterpret_cast<const char*>(p))!=size-1)return false;
    const char* source=reinterpret_cast<const char*>(p);
    return std::strstr(source,entry) && !std::strstr(source,"SampleLevel") && !std::strstr(source,"StructuredBuffer") && !std::strstr(source,"register(");
}
static bool gpu_suite() noexcept {
    const bool media_ok=verify_media_emoji_checks() && verify_border_coverage();
    const bool v070_ok=verify_theme_wrap_checks();
    static_assert(sizeof(Prim)==64);
    bool ok=source_ok(g_etch_uber_vs_msl,sizeof(g_etch_uber_vs_msl),"vertex VSOut vs_main(") && source_ok(g_etch_uber_ps_msl,sizeof(g_etch_uber_ps_msl),"fragment float4 ps_main(") && source_ok(g_etch_kawase_vs_msl,sizeof(g_etch_kawase_vs_msl),"vertex VSOut vs_main(") && source_ok(g_etch_kawase_ps_msl,sizeof(g_etch_kawase_ps_msl),"fragment float4 ps_main(");
    const char* vs=reinterpret_cast<const char*>(g_etch_uber_vs_msl);const char* ps=reinterpret_cast<const char*>(g_etch_uber_ps_msl);const char* ks=reinterpret_cast<const char*>(g_etch_kawase_ps_msl);
    ok &= std::strstr(vs,"globals [[buffer(0)]]") && std::strstr(vs,"g_prims [[buffer(1)]]") && std::strstr(ks,"kawase [[buffer(0)]]") && std::strstr(ks,"g_src_tex [[texture(0)]]") && std::strstr(ks,"g_src_smp [[sampler(0)]]");
    for(uint32_t i=0;i<4;++i){char slot[80];std::snprintf(slot,sizeof(slot),"g_tex%u [[texture(%u)]]",i,i);ok &= std::strstr(ps,slot)!=nullptr;std::snprintf(slot,sizeof(slot),"g_smp%u [[sampler(%u)]]",i,i);ok &= std::strstr(ps,slot)!=nullptr;}
    const char* buffers[]={"g_slug_curves [[buffer(0)]]","g_slug_bands [[buffer(1)]]","g_dyn_slug_curves [[buffer(2)]]","g_dyn_slug_bands [[buffer(3)]]"};for(const char* b:buffers)ok &= std::strstr(ps,b)!=nullptr;
    const char* kinds[]={"RECT","SHADOW","GLYPH","TEXTURE","SLUG_GLYPH","BLUR_GLASS","CANVAS_GRID","BEZIER_WIRE","RING_ARC","SLUG_FILL","ROT_RECT"};
    for(const char* k:kinds){char branch[80];std::snprintf(branch,sizeof(branch),"kind == ETCH_PRIM_%s",k);ok &= std::strstr(ps,branch)!=nullptr;}
    Context* bad=create_context({.headless=true,.gpu_driver="nonexistent_driver_xyz"});
    if(!bad)return false;
    ok &= gpu_device(bad)==nullptr;
    destroy_context(bad);set_current_context(nullptr);
    Context* vk=create_context({.headless=true,.gpu_driver="vulkan"});
    if(!vk)return false;
    if(SDL_GPUDevice* vdev=gpu_device(vk);vdev!=nullptr)ok &= std::strcmp(SDL_GetGPUDeviceDriver(vdev),"vulkan")==0;
    destroy_context(vk);set_current_context(nullptr);
    Context* c=create_context({.headless=true});if(!c)return false;
    ok &= gpu_device(c)!=nullptr;
    for(uint32_t i=0;i<4;++i){begin_frame(c,{.viewport_w=320,.viewport_h=200});button({.id=id("gpu"),.label=str_lit("GPU")});end_frame(c);}
    ok &= frame_stats(c).idle_sleeping && render_headless_png(c,320,200,"etch_ctest_gpu.png") && frame_stats(c).draw_call_count<=3;
    ok &= media_ok && v070_ok;
    std::remove("etch_ctest_gpu.png");destroy_context(c);set_current_context(nullptr);return ok;
}
int main(int argc,char** argv) {
    const char* names[]={"arena_and_elasticity","unicode_bidi_grapheme","layout_clay_flex_grid","fenwick_virtual_list_1m","widgets_and_dockspace","text_buffer_and_code_editor","a11y_semantic_tree","headless_gpu_invariants"};
    const char* selected=argc==2 && std::strncmp(argv[1],"--suite=",8)==0?argv[1]+8:nullptr;
    if(argc>1 && !selected) { return 2; } bool found=false,ok=true;
    for(uint32_t i=0;i<8;++i)if(!selected || std::strcmp(selected,names[i])==0){found=true;bool pass=false;
        switch(i){case 0:pass=arena_suite();break;case 1:pass=unicode_suite();break;case 2:pass=layout_suite() && verify_desktop_precision();break;case 3:pass=list_suite();break;case 4:pass=verify_desktop_checks(nullptr);break;case 5:pass=verify_editor_checks(nullptr);break;case 6:pass=verify_a11y_checks(nullptr) && verify_os_window_a11y_checks();break;case 7:pass=gpu_suite();break;}
        std::printf("%s: %s\n",names[i],pass?"PASS":"FAIL");ok &= pass;
    }return !found?2:ok?0:1;
}
