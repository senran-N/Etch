#include "etch/etch_c.h"

#include <stdio.h>
#include <string.h>

#if defined(ETCH_CMAKE_VERSION_MAJOR)
#define ETCH_EXPECTED_VERSION_STRING \
    ETCH_VERSION_STR(ETCH_CMAKE_VERSION_MAJOR) "." \
    ETCH_VERSION_STR(ETCH_CMAKE_VERSION_MINOR) "." \
    ETCH_VERSION_STR(ETCH_CMAKE_VERSION_PATCH)
#else
#define ETCH_EXPECTED_VERSION_STRING ETCH_VERSION_STRING
#endif

typedef char c_chk_v4[(sizeof(EtchVec4) == 16u) ? 1 : -1];
typedef char c_chk_u4[(sizeof(EtchUint4) == 16u) ? 1 : -1];
typedef char c_chk_pr[(sizeof(EtchPrim) == 64u) ? 1 : -1];
typedef char c_chk_gl[(sizeof(EtchGlobalsUniform) == 32u) ? 1 : -1];

static void c_after_layout(EtchRect bounds,void* user_data) {
    uint32_t* calls=(uint32_t*)user_data;
    if(bounds.w>0 && bounds.h>0)++*calls;
}

typedef struct CustomTokState {
    uint32_t calls;
    uint32_t last_line;
} CustomTokState;

static uint32_t c_custom_tokenizer(
    uint32_t line_index,
    EtchStr line,
    EtchSyntaxToken* out_tokens,
    uint32_t max_tokens,
    void* user_data)
{
    CustomTokState* st = (CustomTokState*)user_data;
    if (st) {
        st->calls += 1u;
        st->last_line = line_index;
    }
    if (out_tokens && max_tokens > 0u && line.length > 0u) {
        EtchSyntaxToken tok;
        tok.byte_start = 0u;
        tok.byte_end = line.length;
        tok.color = etch_rgb_hex(0x38BDF8u, 255u);
        tok.font_id = ETCH_FONT_MONO;
        out_tokens[0] = tok;
        return 1u;
    }
    return 0u;
}

typedef struct CustomMeasureState {
    uint32_t calls;
} CustomMeasureState;

static float c_custom_row_height(uint32_t index, void* user_data) {
    CustomMeasureState* st = (CustomMeasureState*)user_data;
    if (st) {
        st->calls += 1u;
    }
    return 20.0f + (float)(index % 3u) * 4.0f;
}

#if defined(ETCH_CMAKE_VERSION_MAJOR)
#if ETCH_VERSION_MAJOR != ETCH_CMAKE_VERSION_MAJOR || \
    ETCH_VERSION_MINOR != ETCH_CMAKE_VERSION_MINOR || \
    ETCH_VERSION_PATCH != ETCH_CMAKE_VERSION_PATCH
#error "etch_c.h version macros disagree with CMake project VERSION"
#endif
#endif

static bool test_abi_and_arena(void) {
    const char* ver = ETCH_VERSION_STRING;
    if (strcmp(ver, ETCH_EXPECTED_VERSION_STRING) != 0) {
        return false;
    }

    EtchArena a = etch_arena_create(256u);
    if (!a.base || a.capacity < 256u) {
        etch_arena_destroy(&a);
        return false;
    }
    if (etch_arena_alloc_bytes(&a, 16u, 3u) != NULL) {
        etch_arena_destroy(&a);
        return false;
    }
    void* p0 = etch_arena_alloc_bytes(&a, 32u, 16u);
    const size_t mark = etch_arena_mark(&a);
    EtchStr s = etch_arena_push_str(&a, "hello", 5u);
    bool ok = (p0 != NULL) && s.chars != NULL && s.length == 5u && strncmp(s.chars, "hello", 5u) == 0;
    etch_arena_rewind(&a, mark);
    ok = ok && (etch_arena_mark(&a) == mark);
    etch_arena_reset(&a);
    ok = ok && (etch_arena_mark(&a) == 0u);
    etch_arena_destroy(&a);

    uint8_t stack_buf[64];
    EtchArena b = etch_arena_from_buffer(stack_buf, sizeof(stack_buf));
    ok = ok && (etch_arena_alloc_bytes(&b, 16u, 8u) != NULL);
    return ok;
}

static bool test_layout_widgets_and_a11y(EtchContext* ctx) {
    const EtchTheme dark = etch_theme_dark();
    const EtchTheme light = etch_theme_light();
    const EtchTheme hc = etch_theme_high_contrast();
    const EtchTheme ind = etch_theme_industrial();
    if (dark.id != ETCH_THEME_DARK || light.id != ETCH_THEME_LIGHT ||
        hc.id != ETCH_THEME_HIGH_CONTRAST || ind.id != ETCH_THEME_INDUSTRIAL) {
        return false;
    }
    etch_set_theme(&light, ctx);
    bool ok = (etch_get_theme(ctx).id == ETCH_THEME_LIGHT);
    etch_push_theme(&ind);
    etch_pop_theme();
    etch_set_theme(&dark, ctx);

    bool checked = false;
    bool toggled = true;
    float slider_val = 0.5f;
    int combo_idx = 0;
    const EtchStr combo_items[2] = { ETCH_STR_LIT("Alpha"), ETCH_STR_LIT("Beta") };
    char input_buf[128] = "Soft wrap test text inside a narrow C99 container";

    const uint32_t btn_id = etch_id("c_btn");
    const uint32_t chk_id = etch_id("c_chk");

    EtchFrameDesc fd = etch_frame_desc_default();
    fd.viewport_w = 640u;
    fd.viewport_h = 480u;
    etch_begin_frame(ctx, &fd);

    EtchBoxDesc root_d = etch_box_desc_default();
    root_d.id = etch_id("c_root");
    root_d.width = etch_sizing_fixed(640.0f);
    root_d.height = etch_sizing_fixed(480.0f);
    root_d.axis = ETCH_AXIS_COL;
    root_d.gap = 8.0f;
    etch_begin_box(&root_d);

    EtchGridTrack tracks[2] = {
        etch_grid_px(180.0f),
        etch_grid_fr(1.0f, 0.0f, 0.0f)
    };
    EtchBoxDesc grid_d = etch_box_desc_default();
    grid_d.id = etch_id("c_grid");
    grid_d.width = etch_sizing_grow(0.0f, 0.0f);
    grid_d.height = etch_sizing_fixed(80.0f);
    grid_d.grid_columns = 2u;
    grid_d.grid_tracks = tracks;
    grid_d.grid_track_count = 2u;
    etch_begin_box(&grid_d);

    EtchTextDesc td = etch_text_desc_default();
    etch_text(ETCH_STR_LIT("Header"), &td);

    EtchTextSpan spans[2];
    spans[0] = etch_text_span_default();
    spans[0].text = ETCH_STR_LIT("PartA ");
    spans[1] = etch_text_span_default();
    spans[1].text = ETCH_STR_LIT("PartB");
    spans[1].color = etch_rgb_hex(0x38BDF8u, 255u);
    EtchRichTextDesc rtd = etch_rich_text_desc_default();
    etch_text_spans(spans, 2u, &rtd);
    etch_end_box();

    EtchCollapsibleDesc col_d = etch_collapsible_desc_default();
    col_d.id = etch_id("c_col");
    col_d.title = ETCH_STR_LIT("Details");
    EtchCollapsibleResult col_r = etch_begin_collapsible(&col_d);
    if (col_r.visible) {
        EtchButtonDesc bd = etch_button_desc_default();
        bd.id = btn_id;
        bd.label = ETCH_STR_LIT("PressMe");
        (void)etch_button(&bd);

        EtchCheckboxDesc cd = etch_checkbox_desc_default();
        cd.id = chk_id;
        cd.label = ETCH_STR_LIT("Check");
        cd.checked = &checked;
        (void)etch_checkbox(&cd);
        etch_end_collapsible();
    }

    EtchToggleSwitchDesc tsd = etch_toggle_switch_desc_default();
    tsd.id = etch_id("c_toggle");
    tsd.label = ETCH_STR_LIT("Switch");
    tsd.value = &toggled;
    (void)etch_toggle_switch(&tsd);

    EtchSliderDesc sld = etch_slider_desc_default();
    sld.id = etch_id("c_slider");
    sld.label = ETCH_STR_LIT("Gain");
    sld.value = &slider_val;
    (void)etch_slider(&sld);

    EtchComboBoxDesc cbd = etch_combo_box_desc_default();
    cbd.id = etch_id("c_combo");
    cbd.label = ETCH_STR_LIT("Mode");
    cbd.selected_index = &combo_idx;
    cbd.items = combo_items;
    cbd.item_count = 2u;
    (void)etch_combo_box(&cbd);

    EtchTextInputDesc tid = etch_text_input_desc_default();
    tid.id = etch_id("c_input");
    tid.buffer = input_buf;
    tid.capacity = sizeof(input_buf);
    tid.width = etch_sizing_fixed(180.0f);
    tid.height = etch_sizing_fixed(64.0f);
    tid.multiline = true;
    tid.word_wrap = true;
    (void)etch_text_input(&tid);

    EtchBoxDesc draw_box = etch_box_desc_default();
    draw_box.id = etch_id("c_draw");
    draw_box.width = etch_sizing_fixed(120.0f);
    draw_box.height = etch_sizing_fixed(60.0f);
    etch_begin_box(&draw_box);
    uint32_t layout_calls=0;
    etch_draw_after_layout(c_after_layout,&layout_calls,false);
    EtchRectDrawDesc rdd = etch_rect_draw_desc_default();
    rdd.rect = etch_rect(4.0f, 4.0f, 24.0f, 24.0f);
    rdd.fill = etch_rgb_hex(0x22D3EEu, 255u);
    etch_draw_rect(&rdd);
    EtchLineDesc ldd = etch_line_draw_desc_default();
    ldd.from = etch_vec2(0.0f, 0.0f);
    ldd.to = etch_vec2(40.0f, 20.0f);
    etch_draw_line(&ldd);
    const EtchVec2 poly_pts[3] = { etch_vec2(0.0f, 0.0f), etch_vec2(20.0f, 0.0f), etch_vec2(10.0f, 20.0f) };
    EtchPolylineDesc pld = etch_polyline_desc_default();
    pld.points = poly_pts;
    pld.point_count = 3u;
    pld.closed = true;
    etch_draw_polyline(&pld);
    EtchFilledPolygonDesc fpg = etch_filled_polygon_desc_default();
    fpg.points = poly_pts;
    fpg.point_count = 3u;
    fpg.fill = etch_color_white(200u);
    etch_draw_filled_polygon(&fpg);
    EtchFilledPathDesc fpd = etch_filled_path_desc_default();
    fpd.svg_path_d = ETCH_STR_LIT("M 0 0 L 16 0 L 16 16 Z");
    fpd.fill = etch_rgb_hex(0xF59E0Bu, 255u);
    etch_draw_filled_path(&fpd);
    EtchRingDesc rgd = etch_ring_desc_default();
    rgd.center = etch_vec2(80.0f, 30.0f);
    rgd.radius = 12.0f;
    rgd.thickness = 3.0f;
    rgd.start_angle_rad = 0.0f;
    rgd.sweep_angle_rad = 3.14f;
    etch_draw_ring(&rgd);
    etch_end_box();

    etch_end_box();
    etch_end_frame(ctx);
    if(layout_calls!=1)return 1;

    const uint32_t prim_count = etch_frame_prim_count(ctx);
    EtchPrim prim0;
    ok = ok && (prim_count > 0u) && etch_get_frame_prim(ctx, 0u, &prim0);
    const EtchFrameStats stats = etch_frame_stats(ctx);
    ok = ok && (stats.prim_capacity >= 64u);

    const uint32_t a11y_count = etch_a11y_node_count(ctx, NULL);
    EtchA11yNode btn_node;
    memset(&btn_node, 0, sizeof(btn_node));
    ok = ok && (a11y_count > 0u) && etch_find_a11y_node(btn_id, &btn_node, ctx, NULL);
    ok = ok && (btn_node.role == ETCH_A11Y_BUTTON) && (strcmp(etch_a11y_role_name(btn_node.role), "Button") == 0);
    ok = ok && etch_perform_a11y_action(chk_id, ETCH_A11Y_ACT_PRESS, 0.0f, etch_str_n(NULL, 0u, false), ctx, NULL);

    etch_begin_frame(ctx, &fd);
    etch_begin_box(&root_d);
    EtchCheckboxDesc cd2 = etch_checkbox_desc_default();
    cd2.id = chk_id;
    cd2.label = ETCH_STR_LIT("Check");
    cd2.checked = &checked;
    const bool chk_toggled = etch_checkbox(&cd2);
    etch_end_box();
    etch_end_frame(ctx);

    return ok && chk_toggled && checked;
}

static bool test_text_buffer_and_code_editor(EtchContext* ctx) {
    EtchTextBuffer* tb = etch_create_text_buffer(ETCH_STR_LIT("int a = 1;\nreturn a;"), 0u);
    if (!tb) {
        return false;
    }
    bool ok = (etch_text_buffer_line_count(tb) == 2u);
    const uint32_t len0 = etch_text_buffer_byte_length(tb);
    ok = ok && etch_text_buffer_replace(tb, len0, len0, ETCH_STR_LIT("\nint b = 2;"), true, len0, len0, len0 + 11u, len0 + 11u, 1u, 0.5);
    ok = ok && (etch_text_buffer_line_count(tb) == 3u);
    uint32_t cu = 0u;
    uint32_t an = 0u;
    ok = ok && etch_text_buffer_undo(tb, &cu, &an) && (etch_text_buffer_line_count(tb) == 2u);
    ok = ok && etch_text_buffer_redo(tb, &cu, &an) && (etch_text_buffer_line_count(tb) == 3u);

    CustomTokState custom_tok = { 0u, 0u };
    EtchFrameDesc fd = etch_frame_desc_default();
    fd.viewport_w = 640u;
    fd.viewport_h = 480u;
    etch_begin_frame(ctx, &fd);

    EtchBoxDesc col = etch_box_desc_default();
    col.id = etch_id("c_ed_box");
    col.width = etch_sizing_fixed(640.0f);
    col.height = etch_sizing_fixed(480.0f);
    col.axis = ETCH_AXIS_COL;
    etch_begin_box(&col);

    EtchCodeEditorDesc ed1 = etch_code_editor_desc_default();
    ed1.id = etch_id("c_ed_default");
    ed1.buffer = tb;
    ed1.width = etch_sizing_fixed(600.0f);
    ed1.height = etch_sizing_fixed(180.0f);
    const EtchCodeEditorResult r1 = etch_code_editor(&ed1);

    EtchCodeEditorDesc ed2 = etch_code_editor_desc_default();
    ed2.id = etch_id("c_ed_custom");
    ed2.buffer = tb;
    ed2.width = etch_sizing_fixed(600.0f);
    ed2.height = etch_sizing_fixed(180.0f);
    ed2.tokenizer = c_custom_tokenizer;
    ed2.tokenizer_user_data = &custom_tok;
    const EtchCodeEditorResult r2 = etch_code_editor(&ed2);

    etch_end_box();
    etch_end_frame(ctx);

    ok = ok && (r1.total_lines == 3u) && (r2.total_lines == 3u) && (custom_tok.calls > 0u);
    etch_destroy_text_buffer(tb);
    return ok;
}

static bool test_containers_and_canvas(EtchContext* ctx) {
    const uint32_t vlist_id = etch_id("c_vlist");
    CustomMeasureState mstate = { 0u };
    EtchFrameDesc fd = etch_frame_desc_default();
    fd.viewport_w = 800u;
    fd.viewport_h = 600u;

    EtchVirtualListSlice slice;
    memset(&slice, 0, sizeof(slice));
    for (uint32_t pass = 0u; pass < 2u; ++pass) {
        etch_begin_frame(ctx, &fd);
        EtchBoxDesc root = etch_box_desc_default();
        root.id = etch_id("c_cont_root");
        root.width = etch_sizing_fixed(800.0f);
        root.height = etch_sizing_fixed(600.0f);
        root.axis = ETCH_AXIS_COL;
        etch_begin_box(&root);

        EtchVirtualListDesc vld = etch_virtual_list_desc_default();
        vld.id = vlist_id;
        vld.item_count = 1000000u;
        vld.item_height_px = 20.0f;
        vld.height = etch_sizing_fixed(160.0f);
        vld.variable_height = true;
        vld.measure_height_fn = c_custom_row_height;
        vld.measure_user_data = &mstate;
        slice = etch_begin_virtual_list(&vld);
        etch_end_virtual_list();

        EtchTableColumnDesc cols[2];
        cols[0] = etch_table_column_desc_default();
        cols[0].id = etch_id("col_a");
        cols[0].title = ETCH_STR_LIT("ColA");
        cols[1] = etch_table_column_desc_default();
        cols[1].id = etch_id("col_b");
        cols[1].title = ETCH_STR_LIT("ColB");
        EtchTableDesc tbd = etch_table_desc_default();
        tbd.id = etch_id("c_table");
        tbd.columns = cols;
        tbd.column_count = 2u;
        tbd.item_count = 50u;
        tbd.show_column_lines = false;
        tbd.header_font_size_px = 14;
        tbd.height = etch_sizing_fixed(120.0f);
        const EtchTableResult tr = etch_begin_table(&tbd);
        uint32_t row_idx = 0u;
        if (etch_table_next_row(&row_idx, etch_color_theme_auto(), etch_color_theme_auto())) {
            if (etch_table_next_column()) {
                EtchTextDesc td = etch_text_desc_default();
                etch_text(ETCH_STR_LIT("Cell0"), &td);
            }
        }
        etch_end_table();

        EtchTreeNodeDesc tnd = etch_tree_node_desc_default();
        tnd.id = etch_id("c_tree_root");
        tnd.label = ETCH_STR_LIT("Root");
        const EtchTreeNodeResult tnr = etch_begin_tree_node(&tnd);
        if (tnr.open) {
            etch_end_tree_node();
        }

        const uint32_t dock_id = etch_id("c_dock");
        if (!etch_dock_space_initialized(dock_id)) {
            const uint32_t root_leaf = etch_dock_reset_root(dock_id);
            EtchDockPaneDesc p0 = etch_dock_pane_desc_default();
            p0.id = etch_id("c_pane0");
            p0.title = ETCH_STR_LIT("Pane0");
            etch_dock_add_pane(dock_id, root_leaf, &p0);
        }
        EtchDockSpaceDesc dsd = etch_dock_space_desc_default();
        dsd.id = dock_id;
        dsd.height = etch_sizing_fixed(120.0f);
        etch_begin_dock_space(&dsd);
        while (etch_next_dock_pane() != 0u) {
            EtchTextDesc td = etch_text_desc_default();
            etch_text(ETCH_STR_LIT("PaneBody"), &td);
        }
        etch_end_dock_space();

        EtchCanvasDesc cvd = etch_canvas_desc_default();
        cvd.id = etch_id("c_canvas");
        cvd.width = etch_sizing_fixed(400.0f);
        cvd.height = etch_sizing_fixed(140.0f);
        const EtchCanvasState cs = etch_begin_canvas(&cvd);
        EtchVec2 node_pos = etch_vec2(40.0f, 30.0f);
        EtchCanvasNodeDesc cnd = etch_canvas_node_desc_default();
        cnd.id = etch_id("c_node1");
        cnd.world_pos = &node_pos;
        cnd.width_world = etch_sizing_fixed(100.0f);
        cnd.height_world = etch_sizing_fixed(40.0f);
        (void)etch_begin_canvas_node(&cnd);
        etch_end_canvas_node();
        EtchCanvasWireDesc cwd = etch_canvas_wire_desc_default();
        cwd.from_world = node_pos;
        cwd.to_world = etch_vec2(200.0f, 80.0f);
        etch_canvas_wire(&cwd);
        etch_end_canvas();

        if (pass == 0u && (tr.slice.total_count != 50u || cs.zoom <= 0.0f)) {
            etch_end_box();
            etch_end_frame(ctx);
            return false;
        }

        etch_end_box();
        etch_end_frame(ctx);
        if (pass == 0u) {
            etch_virtual_list_scroll_to_row(vlist_id, 999900u, 22.0f, ETCH_ALIGN_Y_TOP);
        }
    }

    return mstate.calls > 0u && slice.first_index > 999880u &&
           etch_virtual_list_scroll_offset(vlist_id) > 0.0;
}

int main(void) {
    if (!test_abi_and_arena()) {
        fprintf(stderr, "c99_abi_smoke: test_abi_and_arena failed\n");
        return 1;
    }

    EtchContextDesc cd = etch_context_desc_default();
    cd.headless = true;
    cd.max_prims_per_frame = 64u;
    EtchContext* ctx = etch_create_context(&cd);
    if (!ctx) {
        fprintf(stderr, "c99_abi_smoke: etch_create_context failed\n");
        return 1;
    }

    const bool ok_numerals = etch_set_font_tabular_numbers(ctx, ETCH_FONT_SANS, true) &&
        etch_set_font_tabular_numbers(ctx, ETCH_FONT_SANS, false);
    const bool ok_layout = ok_numerals && test_layout_widgets_and_a11y(ctx);
    const bool ok_editor = test_text_buffer_and_code_editor(ctx);
    const bool ok_containers = test_containers_and_canvas(ctx);

    etch_destroy_context(ctx);
    etch_set_current_context(NULL);

    if (!ok_layout || !ok_editor || !ok_containers) {
        fprintf(stderr, "c99_abi_smoke: failed (layout=%d editor=%d containers=%d)\n",
            (int)ok_layout, (int)ok_editor, (int)ok_containers);
        return 1;
    }
    printf("c99_abi_smoke: PASS\n");
    return 0;
}
