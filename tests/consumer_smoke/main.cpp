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
    etch::end_frame(ctx);
    const bool found = etch::find_a11y_node(etch::id("smoke"), ctx) != nullptr;
    etch::destroy_context(ctx);
    etch::set_current_context(nullptr);
    return found ? 0 : 1;
}
