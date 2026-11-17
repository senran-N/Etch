#include <etch/etch.h>

#if __has_include("etch_testing.h") || __has_include("clay.h") || \
    __has_include("stb_truetype.h") || __has_include("etch_uber_vs_spv.h")
#error "Etch exposes private headers to consumers"
#endif

int main() {
    if (ETCH_VERSION_STRING[0] == '\0') return 1;
    const auto seed=etch::Color::oklch(.6f,.15f,240.f);
    const auto seeded=etch::Theme::from_seed(seed);
    if (seeded.on_primary.a!=255 || etch::Color::lerp_oklab(seed,seed,.5f).pack_rgba8()!=seed.pack_rgba8() || etch::SpringSpec::fluid().response<=0) return 1;
    etch::Arena arena = etch::arena_create(256);
    const bool allocated = arena.alloc<uint32_t>(4) != nullptr;
    etch::arena_destroy(&arena);
    if (!allocated) return 1;

    etch::Context* ctx = etch::create_context({.headless = true});
    if (!ctx) return 1;
    etch::begin_frame(ctx, {.viewport_w = 64, .viewport_h = 64});
    etch::set_time_scale(ctx,.2f);
    const auto pr=etch::presence(etch::id("presence"),true);
    if(!pr.visible) return 1;
    etch::push_layer({.opacity=.5f});
    etch::pop_layer();
    etch::box({.id = etch::id("smoke"),
        .width = etch::Sizing::fixed(32), .height = etch::Sizing::fixed(32)});
    etch::begin_box({.width=etch::Sizing::fixed(32),.height=etch::Sizing::fixed(32),.border_beam={.color=etch::Color::white()},.top_rim_alpha=.2f,.scroll_fade_px=12});
    etch::draw_mesh_rect({.rect={0,0,16,16},.color_tl=seed,.color_tr=seed,.color_br=seed,.color_bl=seed});
    etch::draw_border_beam({.rect={0,0,16,16},.beam={.color=seed}});
    etch::punch_hole({.rect={4,4,8,8}});
    etch::end_box();
    if(etch::update_texture_region_rgba(nullptr,nullptr,0,0,1,1,nullptr)) return 1;
    const etch::TerminalCell cells[2]={};
    etch::terminal_grid({.id=etch::id("terminal"),.cells=cells,.columns=2,.rows=1});
    if(etch::begin_sheet({.id=etch::id("sheet"),.open=false}))etch::end_sheet();
    if(etch::eval_shader_surface(ctx,0,{})!=nullptr)return 1;
    etch::end_frame(ctx);
    etch::destroy_shader_surface(ctx,0);
    const bool found = etch::find_a11y_node(etch::id("smoke"), ctx) != nullptr;
    etch::destroy_context(ctx);
    etch::set_current_context(nullptr);
    return found ? 0 : 1;
}
