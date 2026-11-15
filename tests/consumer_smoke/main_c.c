#include <etch/etch_c.h>

#if defined(__has_include)
#if __has_include("etch_testing.h") || __has_include("clay.h") || \
    __has_include("stb_truetype.h") || __has_include("etch_uber_vs_spv.h")
#error "Etch exposes private headers to consumers"
#endif
#endif

typedef char c_chk_prim[(sizeof(EtchPrim) == 64u) ? 1 : -1];

int main(void) {
    const char* ver = ETCH_VERSION_STRING;
    if (ver[0] == '\0') {
        return 1;
    }

    EtchArena arena = etch_arena_create(256u);
    const bool allocated = etch_arena_alloc_bytes(&arena, 16u, 4u) != NULL;
    etch_arena_destroy(&arena);
    if (!allocated) {
        return 1;
    }

    EtchContextDesc cd = etch_context_desc_default();
    cd.headless = true;
    EtchContext* ctx = etch_create_context(&cd);
    if (!ctx) {
        return 1;
    }

    EtchFrameDesc fd = etch_frame_desc_default();
    fd.viewport_w = 128u;
    fd.viewport_h = 96u;
    etch_begin_frame(ctx, &fd);

    EtchBoxDesc box = etch_box_desc_default();
    box.id = etch_id("smoke_c");
    box.width = etch_sizing_fixed(64.0f);
    box.height = etch_sizing_fixed(48.0f);
    etch_begin_box(&box);

    EtchButtonDesc btn = etch_button_desc_default();
    btn.id = etch_id("smoke_c_btn");
    btn.label = ETCH_STR_LIT("OK");
    (void)etch_button(&btn);
    etch_end_box();

    etch_end_frame(ctx);

    EtchA11yNode node;
    node.id = 0u;
    const bool found = etch_find_a11y_node(etch_id("smoke_c"), &node, ctx, NULL) &&
                       etch_find_a11y_node(etch_id("smoke_c_btn"), &node, ctx, NULL);
    etch_destroy_context(ctx);
    etch_set_current_context(NULL);
    return found ? 0 : 1;
}
