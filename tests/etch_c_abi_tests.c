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

static bool c_line_decorator(uint32_t line_index, EtchCodeLineDecoration* out_dec, void* user_data) {
    uint32_t* calls = (uint32_t*)user_data;
    if (calls) ++(*calls);
    if (line_index == 0u && out_dec) {
        out_dec->row_bg = (EtchColor){ 60u, 180u, 80u, 24u };
        out_dec->gutter_bar_color = (EtchColor){ 63u, 185u, 80u, 255u };
        out_dec->gutter_tag = ETCH_STR_LIT("a91f");
        return true;
    }
    return false;
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
    ok = ok && etch_arena_push_str(&a, NULL, 1u).chars == NULL;
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
    const EtchColor seed=etch_color_oklch(.62795536f,.25768331f,29.233885f,1.f);
    if(seed.r!=255 || seed.g>1 || seed.b>1 || etch_theme_from_seed(seed,true,1.f).on_primary.a!=255 ||
       etch_color_lerp_oklab(seed,seed,.5f).r!=255 || etch_spring_spec_snappy().response<=0 ||
       etch_spring_spec_fluid().response<=0 || etch_spring_spec_bouncy().damping_ratio>=1 || etch_spring_spec_gentle().response<=0) return false;
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
    etch_set_time_scale(ctx,1.f);
    const EtchLayerDesc motion_layer=etch_layer_desc_default();
    etch_push_layer(&motion_layer);etch_pop_layer();
    if(!etch_presence(etch_id("c_presence"),true,etch_spring_spec_fluid()).visible) return false;


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

    etch_push_id(10u);
    const uint32_t sid_a = etch_scoped_id_str(ETCH_STR_LIT("item"));
    etch_pop_id();
    etch_push_id(20u);
    const uint32_t sid_b = etch_scoped_id_str(ETCH_STR_LIT("item"));
    etch_pop_id();
    ok = ok && (sid_a != 0u) && (sid_b != 0u) && (sid_a != sid_b);
    ok = ok && !etch_consume_key_pressed(0u, false) && !etch_consume_shortcut(0u, 0u, false);

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
    uint32_t c_dec_calls = 0u;
    ed2.id = etch_id("c_ed_custom");
    ed2.buffer = tb;
    ed2.width = etch_sizing_fixed(600.0f);
    ed2.height = etch_sizing_fixed(180.0f);
    ed2.tokenizer = c_custom_tokenizer;
    ed2.tokenizer_user_data = &custom_tok;
    ed2.line_decorator = c_line_decorator;
    ed2.line_decorator_user_data = &c_dec_calls;
    ed2.gutter_tag_width_px = 36.0f;
    const EtchCodeEditorResult r2 = etch_code_editor(&ed2);

    etch_end_box();
    etch_end_frame(ctx);

    const EtchClipboardImageData cimg = etch_get_clipboard_image(NULL);
    ok = ok && (r1.total_lines == 3u) && (r2.total_lines == 3u) && (custom_tok.calls > 0u) && (c_dec_calls > 0u) &&
         (cimg.bytes == NULL) && (cimg.size_bytes == 0u);
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
        vld.stick_to_bottom = false;
        slice = etch_begin_virtual_list(&vld);
        etch_end_virtual_list();
        etch_virtual_list_scroll_to_bottom(vlist_id, true);
        (void)etch_virtual_list_is_at_bottom(vlist_id);

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
        if (cwd.axis != ETCH_AXIS_ROW) {
            etch_end_canvas();
            etch_end_box();
            etch_end_frame(ctx);
            return false;
        }
        cwd.from_world = node_pos;
        cwd.to_world = etch_vec2(200.0f, 80.0f);
        etch_canvas_wire(&cwd);
        EtchCanvasWireDesc cwd_col = etch_canvas_wire_desc_default();
        cwd_col.from_world = etch_vec2(60.0f, 20.0f);
        cwd_col.to_world = etch_vec2(60.0f, 120.0f);
        cwd_col.axis = ETCH_AXIS_COL;
        etch_canvas_wire(&cwd_col);
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

    if (!(mstate.calls > 0u && slice.first_index > 999880u &&
           etch_virtual_list_scroll_offset(vlist_id) > 0.0)) {
        return false;
    }

    const uint32_t inv_id = etch_id("c_vlist_inv_400");
    EtchVirtualListDesc ivd = etch_virtual_list_desc_default();
    ivd.id = inv_id;
    ivd.item_count = 600u;
    ivd.item_height_px = 20.0f;
    ivd.height = etch_sizing_fixed(160.0f);
    ivd.variable_height = true;
    ivd.measure_height_fn = c_custom_row_height;
    ivd.measure_user_data = &mstate;
    etch_begin_frame(ctx, &fd);
    etch_begin_virtual_list(&ivd);
    etch_end_virtual_list();
    etch_end_frame(ctx);
    etch_virtual_list_invalidate_heights(inv_id);
    etch_virtual_list_scroll_to_row(inv_id, 400u, 20.0f, ETCH_ALIGN_Y_TOP);
    double expected_c_y = 0.0;
    for (uint32_t r = 0u; r < 400u; ++r) {
        expected_c_y += (double)c_custom_row_height(r, NULL);
    }
    const double c_off = etch_virtual_list_scroll_offset(inv_id);
    etch_begin_frame(ctx, &fd);
    const EtchVirtualListSlice islice = etch_begin_virtual_list(&ivd);
    etch_end_virtual_list();
    etch_end_frame(ctx);
    return (c_off - expected_c_y) > -0.5 && (c_off - expected_c_y) < 0.5 &&
           islice.first_index <= 400u && islice.end_index > 400u;
}

static bool test_release_api(EtchContext* ctx) {
    EtchSvgStrokeDesc stroke = etch_svg_stroke_desc_default();
    const EtchStr path = ETCH_STR_LIT("M19 19l-4-4M17 10a7 7 0 1 1-14 0a7 7 0 1 1 14 0z");
    const uint16_t id = etch_register_svg_stroke_icon(ctx, path, NULL);
    if (stroke.stroke_width != 2 || stroke.join != ETCH_STROKE_JOIN_ROUND ||
        !etch_is_icon_loaded(ctx, id) || etch_register_svg_stroke_icon(NULL, path, &stroke) != ETCH_INVALID_ICON)
        return false;
    stroke.stroke_width = -1;
    if (etch_register_svg_stroke_icon(ctx, path, &stroke) != ETCH_INVALID_ICON) return false;
    EtchBoxDesc box = etch_box_desc_default();
    EtchTextSpan span = etch_text_span_default();
    EtchTextInputHighlight ranges[3];
    for (unsigned i = 0; i < 3; ++i) ranges[i] = etch_text_input_highlight_default();
    EtchTextInputDesc input = etch_text_input_desc_default();
    if (box.border_sides.left != 0 || box.border_dash_px != 0 || box.border_gap_px != 0 ||
        span.icon_id != ETCH_INVALID_ICON || span.icon_gap_px != 4 || span.padding_y != 0 ||
        input.highlights != NULL || input.highlight_count != 0 || input.suppress_popup_keys || ranges[0].radius_px != 4) return false;

    EtchFrameDesc frame = etch_frame_desc_default();
    frame.viewport_w = 192;
    frame.viewport_h = 120;
    frame.scroll_delta = etch_vec2(0.0f, -1.5f);
    etch_begin_frame(ctx, &frame);
    const EtchVec2 sd = etch_scroll_delta();
    if (sd.y > -1.4f || sd.y < -1.6f) return false;
    box.id = etch_id("c_scroll_box");
    box.width = etch_sizing_fixed(100.0f);
    box.height = etch_sizing_fixed(50.0f);
    box.scroll_y = true;
    box.clip_children = true;
    box.border_sides.bottom = 2;
    box.border_color = (EtchColor){255, 0, 0, 255};
    box.border_dash_px = 4;
    etch_begin_box(&box);
    EtchBoxDesc child = etch_box_desc_default();
    child.width = etch_sizing_fixed(80.0f);
    child.height = etch_sizing_fixed(170.0f);
    etch_box(&child);
    span.icon_id = id;
    span.text = ETCH_STR_LIT("file");
    span.font_id = ETCH_FONT_ITALIC;
    span.padding_y = 2;
    etch_text_spans(&span, 1, NULL);
    char text[] = "@f \xC3\xA9";
    ranges[0].byte_length = 2;
    ranges[0].background = box.border_color;
    ranges[1].byte_offset = 4;
    ranges[1].byte_length = UINT32_MAX;
    ranges[1].text_color = box.border_color;
    ranges[2].byte_offset = UINT32_MAX;
    ranges[2].byte_length = 1;
    ranges[2].border_color = box.border_color;
    input.id = etch_id("c_ti_popup");
    input.buffer = text;
    input.capacity = sizeof(text);
    input.multiline = true;
    input.suppress_popup_keys = true;
    input.highlights = ranges;
    input.highlight_count = 3;
    const EtchTextInputResult result = etch_text_input(&input);
    input.highlights = NULL; /* A null list remains valid with a nonzero count. */
    (void)etch_text_input(&input);
    const uint32_t term_id = etch_id("c_term_ime");
    EtchBoxDesc term_box = etch_box_desc_default();
    term_box.id = term_id;
    term_box.focusable = true;
    etch_box(&term_box);
    etch_request_focus(term_id);
    etch_request_text_input(term_id, (EtchRect){4.0f, 8.0f, 2.0f, 14.0f});
    const EtchStr typed = etch_typed_text(term_id);
    if (typed.length != 0u) return false;
    etch_end_box();
    etch_end_frame(ctx);
    const EtchVec2 smax = etch_scroll_max(etch_id("c_scroll_box"));
    if (smax.y < 119.0f || smax.y > 121.0f || etch_scroll_delta().y != 0.0f) return false;
    return result.byte_length == sizeof(text) - 1 && etch_font_metrics(ETCH_FONT_ITALIC, 16).line_height > 0;
}

static bool test_versioned_c_abi(EtchContext* ctx) {
    if (etch_abi_version() != ETCH_ABI_VERSION || ETCH_ABI_VERSION != 1u) {
        return false;
    }
    if (etch_context_desc_default().struct_size != (uint32_t)sizeof(EtchContextDesc) ||
        etch_frame_desc_default().struct_size != (uint32_t)sizeof(EtchFrameDesc) ||
        etch_window_frame_desc_default().struct_size != (uint32_t)sizeof(EtchWindowFrameDesc) ||
        etch_box_desc_default().struct_size != (uint32_t)sizeof(EtchBoxDesc) ||
        etch_text_desc_default().struct_size != (uint32_t)sizeof(EtchTextDesc) ||
        etch_rich_text_desc_default().struct_size != (uint32_t)sizeof(EtchRichTextDesc) ||
        etch_text_input_desc_default().struct_size != (uint32_t)sizeof(EtchTextInputDesc) ||
        etch_code_editor_desc_default().struct_size != (uint32_t)sizeof(EtchCodeEditorDesc) ||
        etch_table_desc_default().struct_size != (uint32_t)sizeof(EtchTableDesc) ||
        etch_virtual_list_desc_default().struct_size != (uint32_t)sizeof(EtchVirtualListDesc) ||
        etch_split_desc_default().struct_size != (uint32_t)sizeof(EtchSplitDesc) ||
        etch_canvas_desc_default().struct_size != (uint32_t)sizeof(EtchCanvasDesc) ||
        etch_window_chrome_desc_default().struct_size != (uint32_t)sizeof(EtchWindowChromeDesc) ||
        etch_shader_surface_create_desc_default().struct_size != (uint32_t)sizeof(EtchShaderSurfaceCreateDesc) ||
        etch_shader_surface_eval_desc_default().struct_size != (uint32_t)sizeof(EtchShaderSurfaceEvalDesc) ||
        etch_sheet_desc_default().struct_size != (uint32_t)sizeof(EtchSheetDesc) ||
        etch_layer_desc_default().struct_size != (uint32_t)sizeof(EtchLayerDesc)) {
        return false;
    }

    /* Corrupt tiny struct_size (1..3 bytes) must be rejected cleanly at public boundaries. */
    {
        EtchContextDesc bad_cd = etch_context_desc_default();
        bad_cd.headless = true;
        bad_cd.struct_size = 2u;
        if (etch_create_context(&bad_cd) != NULL) return false;

        EtchFrameDesc bad_fd = etch_frame_desc_default();
        bad_fd.struct_size = 1u;
        etch_begin_frame(ctx, &bad_fd);
        if (etch_frame_in_progress(ctx)) return false;

        EtchShaderSurfaceCreateDesc bad_sc = etch_shader_surface_create_desc_default();
        bad_sc.struct_size = 3u;
        if (etch_create_shader_surface(ctx, &bad_sc) != 0u) return false;

        EtchShaderSurfaceEvalDesc bad_se = etch_shader_surface_eval_desc_default();
        bad_se.struct_size = 2u;
        if (etch_eval_shader_surface(ctx, 1u, &bad_se) != NULL) return false;
    }

    /* Zero-initialized descriptors (struct_size == 0) must be accepted cleanly. */
    {
        const uint32_t zero_box_id = etch_id("abi_zero_box");
        const uint32_t bad_box_id = etch_id("abi_bad_box");
        const uint32_t future_box_id = etch_id("abi_future_box");
        EtchFrameDesc zero_fd;
        memset(&zero_fd, 0, sizeof(zero_fd));
        zero_fd.viewport_w = 400u;
        zero_fd.viewport_h = 300u;
        etch_begin_frame(ctx, &zero_fd);
        if (!etch_frame_in_progress(ctx)) return false;

        EtchBoxDesc root = etch_box_desc_default();
        root.id = etch_id("abi_zero_root");
        root.width = etch_sizing_fixed(400.0f);
        root.height = etch_sizing_fixed(300.0f);
        root.axis = ETCH_AXIS_COL;
        etch_begin_box(&root);

        EtchBoxDesc zero_box;
        memset(&zero_box, 0, sizeof(zero_box));
        zero_box.id = zero_box_id;
        zero_box.width = etch_sizing_fixed(72.0f);
        zero_box.height = etch_sizing_fixed(28.0f);
        zero_box.opacity = 1.0f;
        etch_box(&zero_box);

        EtchTextDesc zero_td;
        memset(&zero_td, 0, sizeof(zero_td));
        zero_td.font_size_px = 14.0f;
        zero_td.color = etch_color_white(255u);
        etch_text(ETCH_STR_LIT("ZeroInit"), &zero_td);

        EtchLayerDesc zero_layer;
        memset(&zero_layer, 0, sizeof(zero_layer));
        zero_layer.opacity = 1.0f;
        zero_layer.scale = etch_vec2(1.0f, 1.0f);
        etch_push_layer(&zero_layer);
        etch_pop_layer();

        /* Larger struct_size (caller compiled against newer minor ABI fields) must read prefix safely. */
        EtchBoxDesc future_box = etch_box_desc_default();
        future_box.struct_size = (uint32_t)sizeof(EtchBoxDesc) + 64u;
        future_box.id = future_box_id;
        future_box.width = etch_sizing_fixed(84.0f);
        future_box.height = etch_sizing_fixed(36.0f);
        etch_box(&future_box);

        /* Corrupt tiny struct_size rejection inside active frame. */
        EtchBoxDesc bad_box = etch_box_desc_default();
        bad_box.struct_size = 3u;
        bad_box.id = bad_box_id;
        bad_box.width = etch_sizing_fixed(50.0f);
        bad_box.height = etch_sizing_fixed(30.0f);
        etch_box(&bad_box);

        char ti_buf[16] = "abc";
        EtchTextInputDesc bad_ti = etch_text_input_desc_default();
        bad_ti.struct_size = 2u;
        bad_ti.id = etch_id("abi_bad_ti");
        bad_ti.buffer = ti_buf;
        bad_ti.capacity = sizeof(ti_buf);
        if (etch_text_input(&bad_ti).byte_length != 0u) {
            etch_end_box();
            etch_end_frame(ctx);
            return false;
        }

        EtchCodeEditorDesc bad_ce = etch_code_editor_desc_default();
        bad_ce.struct_size = 1u;
        bad_ce.id = etch_id("abi_bad_ce");
        if (etch_code_editor(&bad_ce).total_lines != 0u) {
            etch_end_box();
            etch_end_frame(ctx);
            return false;
        }

        EtchTableDesc bad_tb = etch_table_desc_default();
        bad_tb.struct_size = 3u;
        bad_tb.id = etch_id("abi_bad_tb");
        bad_tb.item_count = 10u;
        if (etch_begin_table(&bad_tb).slice.total_count != 0u) {
            etch_end_box();
            etch_end_frame(ctx);
            return false;
        }

        EtchVirtualListDesc bad_vl = etch_virtual_list_desc_default();
        bad_vl.struct_size = 2u;
        bad_vl.id = etch_id("abi_bad_vl");
        bad_vl.item_count = 10u;
        if (etch_begin_virtual_list(&bad_vl).total_count != 0u) {
            etch_end_box();
            etch_end_frame(ctx);
            return false;
        }

        EtchSplitDesc bad_sp = etch_split_desc_default();
        bad_sp.struct_size = 1u;
        bad_sp.id = etch_id("abi_bad_sp");
        if (etch_begin_split(&bad_sp).ratio != 0.0f) {
            etch_end_box();
            etch_end_frame(ctx);
            return false;
        }

        EtchCanvasDesc bad_cv = etch_canvas_desc_default();
        bad_cv.struct_size = 2u;
        bad_cv.id = etch_id("abi_bad_cv");
        if (etch_begin_canvas(&bad_cv).zoom != 0.0f) {
            etch_end_box();
            etch_end_frame(ctx);
            return false;
        }

        EtchSheetDesc bad_sh = etch_sheet_desc_default();
        bad_sh.struct_size = 3u;
        bad_sh.id = etch_id("abi_bad_sh");
        bad_sh.open = true;
        if (etch_begin_sheet(&bad_sh)) {
            etch_end_sheet();
            etch_end_box();
            etch_end_frame(ctx);
            return false;
        }

        EtchCollapseDesc bad_col = etch_collapse_desc_default();
        bad_col.struct_size = 2u;
        bad_col.id = etch_id("abi_bad_col");
        bad_col.open = true;
        if (etch_begin_collapse(&bad_col)) {
            etch_end_collapse();
            etch_end_box();
            etch_end_frame(ctx);
            return false;
        }

        etch_end_box();
        etch_end_frame(ctx);

        const EtchRect zb = etch_element_bounds(zero_box_id);
        const EtchRect fb = etch_element_bounds(future_box_id);
        const EtchRect bb = etch_element_bounds(bad_box_id);
        if (zb.w != 72.0f || zb.h != 28.0f || fb.w != 84.0f || fb.h != 36.0f || bb.w != 0.0f || bb.h != 0.0f) {
            return false;
        }
    }

    /* Partial older ABI struct_size: read only caller's prefix and preserve defaults for newer fields. */
    {
        typedef struct OldFramePrefix {
            uint32_t struct_size;
            uint32_t viewport_w;
            uint32_t viewport_h;
        } OldFramePrefix;
        typedef struct OldBoxPrefix {
            uint32_t struct_size;
            uint32_t id;
            EtchAxis axis;
            EtchSizing width;
            EtchSizing height;
        } OldBoxPrefix;

        const uint32_t old_box_id = etch_id("abi_old_prefix_box");
        OldFramePrefix old_fd = { (uint32_t)sizeof(OldFramePrefix), 400u, 300u };
        OldBoxPrefix old_box = {
            (uint32_t)sizeof(OldBoxPrefix),
            old_box_id,
            ETCH_AXIS_ROW,
            etch_sizing_fixed(96.0f),
            etch_sizing_fixed(44.0f)
        };
        etch_begin_frame(ctx, (const EtchFrameDesc*)&old_fd);
        if (!etch_frame_in_progress(ctx)) return false;
        etch_box((const EtchBoxDesc*)&old_box);
        etch_end_frame(ctx);

        EtchElementGeometry geom;
        memset(&geom, 0, sizeof(geom));
        if (!etch_element_geometry(old_box_id, &geom) ||
            geom.bounds.w != 96.0f || geom.bounds.h != 44.0f || geom.opacity != 1.0f) {
            return false;
        }
    }

    /* New C99 ABI coverage: layered/duotone icons, collapse, widget craft, split fixed sizing, DPI/motion/theme utilities. */
    {
        EtchSvgStrokeDesc sd = etch_svg_stroke_desc_default();
        const uint16_t duo_id = etch_register_svg_duotone_icon(
            ctx, ETCH_STR_LIT("M4 4 H20 V20 H4 Z"), ETCH_STR_LIT("M4 4 H20 V20 H4 Z"), &sd, 0.25f);
        if (duo_id == ETCH_INVALID_ICON || !etch_is_icon_loaded(ctx, duo_id)) return false;

        EtchSvgIconLayer layers[2];
        layers[0] = etch_svg_icon_layer_default();
        layers[0].path_d = ETCH_STR_LIT("M2 2 H22 V22 H2 Z");
        layers[0].color_slot = 1u;
        layers[0].opacity = 0.30f;
        layers[1] = etch_svg_icon_layer_default();
        layers[1].path_d = ETCH_STR_LIT("M12 12 Z");
        layers[1].stroked = true;
        layers[1].stroke = sd;
        layers[1].color_slot = 0u;
        layers[1].opacity = 1.0f;
        const uint16_t lay_id = etch_register_svg_layered_icon(ctx, layers, 2u, (EtchVec4){0.0f, 0.0f, 24.0f, 24.0f}, ETCH_INVALID_ICON);
        if (lay_id == ETCH_INVALID_ICON || !etch_is_icon_loaded(ctx, lay_id)) return false;

        const EtchTheme td = etch_theme_dark();
        const EtchTheme tl = etch_theme_light();
        const EtchTheme tm = etch_theme_lerp_oklab(&td, &tl, 0.5f);
        if (!(tm.bg_canvas.r > td.bg_canvas.r && tm.bg_canvas.r < tl.bg_canvas.r)) return false;

        EtchFrameDesc f = etch_frame_desc_default();
        f.viewport_w = 500u;
        f.viewport_h = 300u;
        f.dpi_scale = 2.0f;
        f.dt_seconds = 1.0f / 60.0f;
        etch_begin_frame(ctx, &f);
        if (etch_dpi_scale(ctx) != 2.0f || etch_hairline_px(ctx) != 0.5f) {
            etch_end_frame(ctx);
            return false;
        }
        const EtchColor sc = etch_spring_color(etch_id("c_sp_col"), etch_rgba8(100u, 150u, 200u, 255u), etch_spring_spec_snappy());
        const float sa = etch_spring_angle(etch_id("c_sp_ang"), 1.5f, etch_spring_spec_snappy());
        if (sc.r != 100u || sc.g != 150u || sc.b != 200u || sa < 1.49f || sa > 1.51f) {
            etch_end_frame(ctx);
            return false;
        }

        EtchIconDesc ic = etch_icon_desc_default();
        if (ic.rotation_rad != 0.0f || ic.scale != 1.0f || ic.opacity != 1.0f) {
            etch_end_frame(ctx);
            return false;
        }
        ic.icon_id = duo_id;
        ic.size_px = 18.0f;
        ic.rotation_rad = 0.5f;
        ic.scale = 1.2f;
        ic.opacity = 0.8f;
        ic.secondary_color = etch_rgba8(40u, 160u, 240u, 255u);
        etch_icon(&ic);

        EtchCollapseDesc col = etch_collapse_desc_default();
        if (col.struct_size != (uint32_t)sizeof(EtchCollapseDesc) || !col.fade_content) {
            etch_end_frame(ctx);
            return false;
        }
        col.id = etch_id("c_collapse");
        col.open = true;
        if (!etch_begin_collapse(&col)) {
            etch_end_frame(ctx);
            return false;
        }
        EtchBoxDesc col_body = etch_box_desc_default();
        col_body.height = etch_sizing_fixed(40.0f);
        etch_box(&col_body);
        etch_end_collapse();

        float sval = 0.4f;
        EtchSliderDesc sld = etch_slider_desc_default();
        if (sld.style != ETCH_SLIDER_TRACK || sld.track_height_px != 4.0f || sld.thumb_size_px != 14.0f || !sld.show_value) {
            etch_end_frame(ctx);
            return false;
        }
        sld.id = etch_id("c_slider");
        sld.value = &sval;
        etch_slider(&sld);

        bool tval = true;
        EtchToggleSwitchDesc tog = etch_toggle_switch_desc_default();
        tog.id = etch_id("c_toggle");
        tog.value = &tval;
        etch_toggle_switch(&tog);

        EtchComboBoxDesc cbd = etch_combo_box_desc_default();
        EtchPopupDesc ppd = etch_popup_desc_default();
        if (cbd.border_width != 1.0f || ppd.shadow_blur != 20.0f || !ppd.flip_on_overflow) {
            etch_end_frame(ctx);
            return false;
        }

        float pane_px = 180.0f;
        EtchSplitDesc spd = etch_split_desc_default();
        if (spd.sizing != ETCH_SPLIT_RATIO || !spd.show_grip || spd.hover_highlight_px != 0.0f) {
            etch_end_frame(ctx);
            return false;
        }
        spd.id = etch_id("c_split_fixed");
        spd.width = etch_sizing_fixed(480.0f);
        spd.height = etch_sizing_fixed(100.0f);
        spd.divider_size_px = 0.0f;
        spd.sizing = ETCH_SPLIT_SECOND_FIXED_PX;
        spd.extent_px = &pane_px;
        spd.default_extent_px = 180.0f;
        spd.show_grip = false;
        spd.hover_highlight_px = 2.0f;
        etch_begin_split(&spd);
        EtchBoxDesc p1 = etch_box_desc_default();
        p1.id = etch_id("c_sp_1");
        p1.width = etch_sizing_grow(0.0f, 0.0f);
        p1.height = etch_sizing_grow(0.0f, 0.0f);
        etch_box(&p1);
        etch_split_second();
        EtchBoxDesc p2 = etch_box_desc_default();
        p2.id = etch_id("c_sp_2");
        p2.width = etch_sizing_grow(0.0f, 0.0f);
        p2.height = etch_sizing_grow(0.0f, 0.0f);
        etch_box(&p2);
        etch_end_split();

        /* Backward-compatible older partial struct_size prefix for EtchSplitDesc & EtchTextInputDesc. */
        EtchSplitDesc old_sp = etch_split_desc_default();
        old_sp.struct_size = (uint32_t)offsetof(EtchSplitDesc, sizing);
        old_sp.id = etch_id("c_split_old_prefix");
        old_sp.width = etch_sizing_fixed(300.0f);
        old_sp.height = etch_sizing_fixed(60.0f);
        old_sp.default_ratio = 0.5f;
        etch_begin_split(&old_sp);
        etch_split_second();
        etch_end_split();

        char old_ti_buf[16] = "v025";
        EtchTextInputDesc old_ti = etch_text_input_desc_default();
        old_ti.struct_size = (uint32_t)offsetof(EtchTextInputDesc, hover_border_color);
        old_ti.id = etch_id("c_ti_old_prefix");
        old_ti.buffer = old_ti_buf;
        old_ti.capacity = sizeof(old_ti_buf);
        if (etch_text_input(&old_ti).byte_length != 4u) {
            etch_end_frame(ctx);
            return false;
        }

        char c_ti_buf[32] = "hello";
        EtchTextInputDesc tid = etch_text_input_desc_default();
        if (tid.focus_glow_blur != 10.0f || !tid.continuous_corners || tid.disabled || tid.leading_icon_id != ETCH_INVALID_ICON) {
            etch_end_frame(ctx);
            return false;
        }
        tid.id = etch_id("c_ti_craft");
        tid.buffer = c_ti_buf;
        tid.capacity = sizeof(c_ti_buf);
        tid.focus_glow_blur = 0.0f;
        tid.leading_icon_id = duo_id;
        tid.disabled = true;
        etch_text_input(&tid);

        etch_end_frame(ctx);

        const EtchRect sp2_r = etch_element_bounds(etch_id("c_sp_2"));
        if (sp2_r.w < 179.5f || sp2_r.w > 180.5f) return false;
    }

    return true;
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

    uint32_t selected_focus=70u, selected_anchor=70u;
    uint64_t selected_bits[2]={0u,64u};
    EtchTableSelection selection={&selected_focus,&selected_anchor,selected_bits,2u};
    const bool ok_closure=etch_table_row_selected(&selection,70u) &&
        !etch_table_row_selected(&selection,69u) && !etch_table_row_selected(&selection,128u) &&
        etch_table_desc_default().horizontal_scroll && etch_table_desc_default().keyboard_navigation &&
        etch_split_desc_default().divider_hit_px==8.0f &&
        !etch_box_desc_default().shrink_height && etch_rect_draw_desc_default().border_align==ETCH_BORDER_INSIDE;
    const bool ok_numerals = etch_set_font_tabular_numbers(ctx, ETCH_FONT_SANS, true) &&
        etch_set_font_tabular_numbers(ctx, ETCH_FONT_SANS, false);
    const EtchFontMetrics fm_base = etch_font_metrics(ETCH_FONT_SANS, 16.0f);
    const bool ok_scale_set = etch_font_scale_adjust(ctx, ETCH_FONT_SANS) == 1.0f &&
        etch_set_font_scale_adjust(ctx, ETCH_FONT_SANS, 1.25f) &&
        etch_font_scale_adjust(ctx, ETCH_FONT_SANS) == 1.25f;
    const EtchFontMetrics fm_scaled = etch_font_metrics(ETCH_FONT_SANS, 16.0f);
    const bool ok_scale_clamp = etch_set_font_scale_adjust(ctx, ETCH_FONT_SANS, 0.1f) &&
        etch_font_scale_adjust(ctx, ETCH_FONT_SANS) == 0.5f &&
        etch_set_font_scale_adjust(ctx, ETCH_FONT_SANS, 1.0f) &&
        etch_font_scale_adjust(ctx, ETCH_FONT_SANS) == 1.0f &&
        fm_base.line_height > 0.0f &&
        fm_scaled.line_height > fm_base.line_height * 1.24f &&
        fm_scaled.line_height < fm_base.line_height * 1.26f;
    const EtchShaderSurfaceCreateDesc surface=etch_shader_surface_create_desc_default();
    const EtchShaderSurfaceEvalDesc eval=etch_shader_surface_eval_desc_default();
    const EtchTerminalCell cell=etch_terminal_cell_default();
    const EtchSheetDesc sheet=etch_sheet_desc_default();
    const EtchLayerDesc v21_layer=etch_layer_desc_default();
    const EtchTextSpan v21_span=etch_text_span_default();
    const EtchTextDrawDesc v21_text_draw=etch_text_draw_desc_default();
    const EtchImageDesc v21_img=etch_image_desc_default();
    const EtchImageDrawDesc v21_img_draw=etch_image_draw_desc_default();
    EtchBoxDesc v22_box = etch_box_desc_default();
    v22_box.edge_fade = (EtchPadding){8.0f, 8.0f, 12.0f, 12.0f};
    const uint16_t sys_font = etch_load_system_font(ctx, ETCH_STR_LIT("Segoe UI"), ETCH_FONT_WEIGHT_BOLD, ETCH_INVALID_FONT);
    const bool ok_font_fallback = !etch_set_font_fallback(ctx, ETCH_FONT_SANS, ETCH_FONT_SANS) &&
        !etch_set_font_fallback(ctx, ETCH_FONT_SANS, 50u) &&
        etch_set_font_fallback(ctx, ETCH_FONT_SANS, sys_font) &&
        etch_set_font_fallback(ctx, ETCH_FONT_SANS, ETCH_INVALID_FONT);
    const bool ok_v22_c99 = ok_scale_set && ok_scale_clamp && ok_font_fallback && !surface.enable_feedback &&
        v21_text_draw.shadow_color.a == 0u && v21_text_draw.shadow_offset.x == 0.0f &&
        v21_text_draw.shadow_offset.y == 0.0f && v21_text_draw.shadow_blur == 0.0f &&
        etch_box_desc_default().edge_fade.left == 0.0f && etch_box_desc_default().edge_fade.top == 0.0f &&
        sys_font != ETCH_INVALID_FONT && etch_is_font_loaded(ctx, sys_font) &&
        !etch_is_window_minimized(ctx, NULL);
    const EtchFileDialogResult empty_dlg = etch_poll_file_dialog(ctx, NULL);
    const bool ok_v24_c99 = !empty_dlg.ready && !etch_open_folder_dialog(ctx, NULL, NULL, 7u, false) &&
        sheet.show_handle && !sheet.dismiss_on_backdrop && !sheet.dismiss_on_escape && sheet.corner_radius_px == 16.0f &&
        v22_box.justify == ETCH_JUSTIFY_START && v22_box.align_self == ETCH_ALIGN_SELF_AUTO &&
        v22_box.flex_grow == 0.0f && v22_box.flex_shrink == 1.0f && v22_box.grid_row_span == 1u &&
        !v22_box.flip_on_overflow && v22_box.hitbox_behavior == ETCH_HITBOX_NORMAL &&
        !v22_box.stick_to_bottom &&
        etch_topmost_hovered_id() == 0u && !etch_clicked_without_drag(etch_id("none"), 4.0f);
    {
        /* BoxDesc::stick_to_bottom C99 ABI parity + backward-compatible partial struct_size prefix */
        EtchFrameDesc sf = etch_frame_desc_default();
        sf.viewport_w = 480; sf.viewport_h = 160;
        const uint32_t s_id = etch_id("c_stick_box");
        const uint32_t legacy_id = etch_id("c_stick_legacy");
        for (int pass = 0; pass < 2; ++pass) {
            etch_begin_frame(ctx, &sf);
            EtchBoxDesc sb = etch_box_desc_default();
            sb.id = s_id;
            sb.axis = ETCH_AXIS_COL;
            sb.width = etch_sizing_fixed(200.0f);
            sb.height = etch_sizing_fixed(80.0f);
            sb.scroll_y = true;
            sb.stick_to_bottom = true;
            etch_begin_box(&sb);
            for (int r = 0; r < 5; ++r) {
                EtchBoxDesc row = etch_box_desc_default();
                row.width = etch_sizing_grow(0.0f, 0.0f);
                row.height = etch_sizing_fixed(40.0f);
                etch_box(&row);
            }
            etch_end_box();

            EtchBoxDesc old_box = etch_box_desc_default();
            old_box.struct_size = (uint32_t)offsetof(EtchBoxDesc, stick_to_bottom);
            old_box.id = legacy_id;
            old_box.axis = ETCH_AXIS_COL;
            old_box.width = etch_sizing_fixed(200.0f);
            old_box.height = etch_sizing_fixed(80.0f);
            old_box.scroll_y = true;
            old_box.stick_to_bottom = true; /* Beyond prefix -> must default to false */
            etch_begin_box(&old_box);
            for (int r = 0; r < 5; ++r) {
                EtchBoxDesc row = etch_box_desc_default();
                row.width = etch_sizing_grow(0.0f, 0.0f);
                row.height = etch_sizing_fixed(40.0f);
                etch_box(&row);
            }
            etch_end_box();
            etch_end_frame(ctx);
        }
        const float off_y = etch_scroll_offset(s_id).y;
        const float legacy_y = etch_scroll_offset(legacy_id).y;
        const bool stuck_ok = etch_scroll_stuck_to_bottom(s_id) && off_y > 119.0f && off_y < 121.0f && legacy_y < 0.5f;
        etch_set_scroll_stuck_to_bottom(s_id, false);
        const bool unstick_ok = !etch_scroll_stuck_to_bottom(s_id);
        etch_set_scroll_offset(s_id, (EtchVec2){0.0f, 10.0f}, true);
        etch_set_scroll_stuck_to_bottom(s_id, true);
        const bool restick_ok = etch_scroll_stuck_to_bottom(s_id) && etch_scroll_offset(s_id).y > 119.0f;
        if (!stuck_ok || !unstick_ok || !restick_ok) {
            fprintf(stderr, "c99_abi_smoke: stick_to_bottom failed (stuck=%d unstick=%d restick=%d off=%.1f legacy=%.1f)\n",
                (int)stuck_ok, (int)unstick_ok, (int)restick_ok, off_y, legacy_y);
            etch_destroy_context(ctx);
            return 1;
        }
    }
    {
        EtchFrameDesc p2f = etch_frame_desc_default();
        p2f.viewport_w = 160; p2f.viewport_h = 80;
        const uint32_t sub = etch_id("c_p2_sub");
        int builds = 0;
        for (int pass = 0; pass < 2; ++pass) {
            etch_begin_frame(ctx, &p2f);
            if (etch_begin_cached_subtree(sub, 0xCAFEu)) {
                ++builds;
                EtchBoxDesc cb = etch_box_desc_default();
                cb.width = etch_sizing_fixed(64.0f); cb.height = etch_sizing_fixed(32.0f);
                cb.background = (EtchColor){40, 80, 120, 255};
                etch_box(&cb);
                etch_end_cached_subtree();
            }
            etch_end_frame(ctx);
        }
        etch_invalidate_cached_subtree(sub, ctx);
        if (builds != 1 || etch_frame_stats(ctx).cached_subtree_hits != 1u) {
            fprintf(stderr, "c99_abi_smoke: phase2 cached subtree failed (builds=%d hits=%u)\n",
                builds, etch_frame_stats(ctx).cached_subtree_hits);
            etch_destroy_context(ctx);
            return 1;
        }
    }
    const bool ok_material_defaults=ok_v22_c99 && ok_v24_c99 && surface.max_fps==60 && surface.spirv.entrypoint!=NULL &&
        surface.num_samplers==0u && !surface.sampler_mirror_repeat &&
        eval.width==1 && eval.enabled && eval.textures[0]==NULL && cell.width==1 && sheet.snap_count==3 &&
        v21_layer.blur_px==0.0f && !v21_layer.blend_plus_lighter &&
        !v21_span.has_wipe && v21_span.wipe_x==0.0f && v21_span.wipe_feather_px==0.0f &&
        !v21_text_draw.has_wipe && v21_text_draw.wipe_x==0.0f && v21_text_draw.wipe_feather_px==0.0f &&
        !v21_img.continuous_corners && !v21_img_draw.continuous_corners &&
        etch_window_chrome_desc_default().backdrop==ETCH_BACKDROP_OPAQUE;
    const bool ok_abi = test_versioned_c_abi(ctx);
    if (!ok_abi) fprintf(stderr, "c99_abi_smoke: test_versioned_c_abi failed\n");
    const bool ok_release = ok_abi && test_release_api(ctx);
    if (!ok_release) fprintf(stderr, "c99_abi_smoke: Etch %s public API failed\n", ETCH_VERSION_STRING);
    const bool ok_layout = ok_release && ok_material_defaults && ok_closure && ok_numerals && test_layout_widgets_and_a11y(ctx);
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
