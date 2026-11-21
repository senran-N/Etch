#include <etch/etch_c.h>

#if defined(__has_include)
#if __has_include("etch_testing.h") || __has_include("clay.h") || \
    __has_include("stb_truetype.h") || __has_include("etch_uber_vs_spv.h")
#error "Etch exposes private headers to consumers"
#endif
#endif

typedef char c_chk_prim[(sizeof(EtchPrim) == 64u) ? 1 : -1];

int main(void) {
    if (etch_abi_version() != ETCH_ABI_VERSION || ETCH_ABI_VERSION != 1u) return 1;
    if (etch_font_metrics(ETCH_FONT_SANS,14).ascent != 0) return 1;

    const EtchColor seed=etch_color_oklch(.6f,.15f,240.f,1.f);
    const EtchTheme seeded=etch_theme_from_seed(seed,true,1.f);
    if (seeded.on_primary.a!=255 || etch_color_lerp_oklab(seed,seed,.5f).r!=seed.r || etch_spring_spec_fluid().response<=0) return 1;
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

    EtchContextDesc bad_cd = etch_context_desc_default();
    bad_cd.headless = true;
    bad_cd.struct_size = 2u;
    if (etch_create_context(&bad_cd) != NULL) {
        return 1;
    }

    EtchContextDesc cd = etch_context_desc_default();
    if (cd.struct_size != (uint32_t)sizeof(EtchContextDesc)) {
        return 1;
    }
    cd.headless = true;
    EtchContext* ctx = etch_create_context(&cd);
    if (!ctx) {
        return 1;
    }

    const EtchSvgStrokeDesc stroke = etch_svg_stroke_desc_default();
    const uint16_t icon = etch_register_svg_stroke_icon(ctx, ETCH_STR_LIT("M4 12h16"), &stroke);
    if (!etch_is_icon_loaded(ctx, icon)) return 1;
    EtchFrameDesc fd = etch_frame_desc_default();
    if (fd.struct_size != (uint32_t)sizeof(EtchFrameDesc)) return 1;
    fd.viewport_w = 128u;
    fd.viewport_h = 96u;
    etch_begin_frame(ctx, &fd);
    etch_set_time_scale(ctx,.2f);
    if(!etch_presence(etch_id("presence"),true,etch_spring_spec_fluid()).visible) return 1;
    EtchLayerDesc layer=etch_layer_desc_default();
    if (layer.struct_size != (uint32_t)sizeof(EtchLayerDesc)) return 1;
    layer.blur_px = 2.0f;
    layer.blend_plus_lighter = true;
    etch_push_layer(&layer);etch_pop_layer();


    EtchBoxDesc box = etch_box_desc_default();
    if (box.struct_size != (uint32_t)sizeof(EtchBoxDesc)) return 1;
    box.id = etch_id("smoke_c");
    box.width = etch_sizing_fixed(64.0f);
    box.height = etch_sizing_fixed(48.0f);
    box.border_color = seed;
    box.border_sides.bottom = 2;
    box.border_dash_px = 3;
    etch_begin_box(&box);

    EtchButtonDesc btn = etch_button_desc_default();
    btn.id = etch_id("smoke_c_btn");
    btn.label = ETCH_STR_LIT("OK");
    (void)etch_button(&btn);
    EtchMeshRectDrawDesc mesh=etch_mesh_rect_draw_desc_default();
    mesh.rect=(EtchRect){0,0,16,16};mesh.color_tl=seed;mesh.color_tr=seed;mesh.color_br=seed;mesh.color_bl=seed;
    etch_draw_mesh_rect(&mesh);
    EtchBorderBeamDrawDesc beam=etch_border_beam_draw_desc_default();
    beam.rect=mesh.rect;beam.beam.color=seed;etch_draw_border_beam(&beam);
    EtchPunchHoleDesc hole=etch_punch_hole_desc_default();hole.rect=(EtchRect){4,4,8,8};etch_punch_hole(&hole);
    if(etch_update_texture_region_rgba(NULL,NULL,0,0,1,1,NULL,0)) return 1;
    etch_end_box();

    EtchTerminalCell cell=etch_terminal_cell_default();
    EtchTerminalGridDesc terminal=etch_terminal_grid_desc_default();
    terminal.id=etch_id("terminal_c");terminal.cells=&cell;terminal.cell_count=1;terminal.columns=1;terminal.rows=1;
    etch_terminal_grid(&terminal);
    etch_request_text_input(terminal.id, (EtchRect){0.0f, 0.0f, 2.0f, 16.0f});
    if (etch_typed_text(terminal.id).length != 0u) return 1;
    if (etch_scroll_delta().y != 0.0f || etch_scroll_max(box.id).y != 0.0f) return 1;
    EtchSheetDesc sheet=etch_sheet_desc_default();sheet.id=etch_id("sheet_c");sheet.open=false;
    if(etch_begin_sheet(&sheet))etch_end_sheet();
    EtchShaderSurfaceCreateDesc surf_create = etch_shader_surface_create_desc_default();
    if (surf_create.num_samplers != 0u || surf_create.sampler_mirror_repeat) return 1;
    EtchShaderSurfaceEvalDesc eval=etch_shader_surface_eval_desc_default();
    if(eval.textures[0] != NULL || etch_eval_shader_surface(ctx,0,&eval)!=NULL)return 1;
    EtchImageDesc img_desc = etch_image_desc_default();
    EtchImageDrawDesc img_draw = etch_image_draw_desc_default();
    EtchTextDrawDesc txt_draw = etch_text_draw_desc_default();
    if (img_desc.continuous_corners || img_draw.continuous_corners || txt_draw.has_wipe) return 1;
    EtchTextSpan span = etch_text_span_default();
    span.text = ETCH_STR_LIT("file");
    span.font_id = ETCH_FONT_ITALIC;
    span.icon_id = icon;
    span.padding_y = 2;
    span.has_wipe = true;
    span.color_end = seed;
    span.wipe_x = 12.0f;
    span.wipe_feather_px = 4.0f;
    etch_text_spans(&span, 1, NULL);
    char input[] = "@file";
    EtchTextInputHighlight range = etch_text_input_highlight_default();
    range.byte_length = 5;
    range.background = seed;
    EtchTextInputDesc field = etch_text_input_desc_default();
    field.buffer = input;
    field.capacity = sizeof(input);
    field.suppress_popup_keys = true;
    field.highlights = &range;
    field.highlight_count = 1;
    if (etch_text_input(&field).byte_length != 5) return 1;
    if (etch_is_window_minimized(ctx, NULL)) return 1;
    if (etch_load_system_font(ctx, ETCH_STR_LIT("Segoe UI"), ETCH_FONT_WEIGHT_REGULAR, ETCH_INVALID_FONT) == ETCH_INVALID_FONT) return 1;
    EtchBoxDesc fade_box = etch_box_desc_default();
    fade_box.width = etch_sizing_fixed(32.0f);
    fade_box.height = etch_sizing_fixed(32.0f);
    fade_box.clip_children = true;
    fade_box.edge_fade.top = 4.0f;
    fade_box.edge_fade.bottom = 4.0f;
    etch_begin_box(&fade_box);
    EtchTextDrawDesc shadow_td = etch_text_draw_desc_default();
    shadow_td.text = ETCH_STR_LIT("S");
    shadow_td.shadow_color = seed;
    shadow_td.shadow_offset = (EtchVec2){1.0f, 1.0f};
    shadow_td.shadow_blur = 2.0f;
    etch_draw_text(&shadow_td);
    etch_end_box();
    etch_end_frame(ctx);
    etch_destroy_shader_surface(ctx,0);

    EtchElementGeometry geometry;
    if (!etch_element_geometry(etch_id("smoke_c"), &geometry) || etch_element_geometry(0, NULL)) return 1;
    EtchA11yNode node;
    node.id = 0u;
    const bool found = etch_find_a11y_node(etch_id("smoke_c"), &node, ctx, NULL) &&
                       etch_find_a11y_node(etch_id("smoke_c_btn"), &node, ctx, NULL);

    /* Verify zero-init and older partial struct_size acceptance in downstream C99 consumers. */
    {
        typedef struct DownstreamOldFramePrefix {
            uint32_t struct_size;
            uint32_t viewport_w;
            uint32_t viewport_h;
        } DownstreamOldFramePrefix;
        typedef struct DownstreamOldBoxPrefix {
            uint32_t struct_size;
            uint32_t id;
            EtchAxis axis;
            EtchSizing width;
            EtchSizing height;
        } DownstreamOldBoxPrefix;
        DownstreamOldFramePrefix old_fd = { (uint32_t)sizeof(DownstreamOldFramePrefix), 128u, 96u };
        DownstreamOldBoxPrefix old_box = {
            (uint32_t)sizeof(DownstreamOldBoxPrefix),
            etch_id("smoke_old_box"),
            ETCH_AXIS_ROW,
            etch_sizing_fixed(40.0f),
            etch_sizing_fixed(20.0f)
        };
        EtchBoxDesc zero_box = etch_box_desc_default();
        zero_box.struct_size = 0u;
        zero_box.id = etch_id("smoke_zero_box");
        zero_box.width = etch_sizing_fixed(32.0f);
        zero_box.height = etch_sizing_fixed(16.0f);
        etch_begin_frame(ctx, (const EtchFrameDesc*)&old_fd);
        etch_box((const EtchBoxDesc*)&old_box);
        etch_box(&zero_box);
        etch_end_frame(ctx);
        EtchElementGeometry og;
        if (!etch_element_geometry(old_box.id, &og) || og.bounds.w != 40.0f || og.bounds.h != 20.0f || og.opacity != 1.0f) {
            etch_destroy_context(ctx);
            return 1;
        }
        EtchElementGeometry zg;
        if (!etch_element_geometry(zero_box.id, &zg) || zg.bounds.w != 32.0f || zg.bounds.h != 16.0f) {
            etch_destroy_context(ctx);
            return 1;
        }
    }

    etch_destroy_context(ctx);
    etch_set_current_context(NULL);
    return found ? 0 : 1;
}
