// Clay element tree, vector icons, text shaping/rendering, widgets & scroll containers.
// Compiled via src/etch_unity.cpp.

namespace etch {

void emit_vector_icon_quad(
    Context* ctx,
    uint16_t icon_id,
    Rect target_px,
    Color color,
    const ClipEntry& clip,
    float angle_rad,
    Color secondary_color,
    float opacity,
    bool allow_snap) noexcept
{
    if (icon_id >= kMaxVectorIcons || target_px.w <= 0.0f || target_px.h <= 0.0f ||
        !std::isfinite(opacity) || opacity <= 0.0f) {
        return;
    }
    const VectorIconEntry& ic = ctx->atlas.icons[icon_id];
    if (!ic.loaded || ic.layer_count == 0u || ic.view_size.x <= 0.0f || ic.view_size.y <= 0.0f) {
        return;
    }
    const float sx = target_px.w / ic.view_size.x;
    const float sy = target_px.h / ic.view_size.y;
    const float s = (sx < sy) ? sx : sy;
    const float w = ic.view_size.x * s;
    const float h = ic.view_size.y * s;

    float layer_scale_max = 1.0f;
    bool layer_blocks_snap = false;
    if (ctx->active_render_layer) {
        resolve_layer_chain(ctx, ctx->active_render_layer, ctx->active_ui->dpi_scale);
        const VisualLayerNode* ln = ctx->active_render_layer;
        layer_scale_max = std::fmax(std::fabs(ln->resolved_scale.x), std::fabs(ln->resolved_scale.y));
        layer_blocks_snap = ln->resolved_animating ||
            std::fabs(ln->resolved_scale.x - 1.0f) > 1e-4f ||
            std::fabs(ln->resolved_scale.y - 1.0f) > 1e-4f ||
            std::fabs(ln->resolved_offset_px.x) > 1e-4f ||
            std::fabs(ln->resolved_offset_px.y) > 1e-4f ||
            std::fabs(ln->resolved_rotation_rad) > 1e-4f;
    }
    const bool snap_icon = allow_snap && (angle_rad == 0.0f) && ctx->active_ui->pixel_snap && !layer_blocks_snap;
    const float qx = target_px.x + 0.5f * (target_px.w - w);
    const float qy = target_px.y + 0.5f * (target_px.h - h);
    const Rect quad = snap_icon
        ? Rect{ std::round(qx), std::round(qy), std::fmax(1.0f, std::round(w)), std::fmax(1.0f, std::round(h)) }
        : Rect{ qx, qy, w, h };

    const float physical_size = std::fmin(quad.w, quad.h) * layer_scale_max;
    const uint8_t stem_flag = (physical_size < 28.0f) ? PrimFlag_StemDarken : PrimFlag_None;
    const float eff_opacity = std::fmin(1.0f, std::fmax(0.0f, opacity));

    for (uint8_t li = 0u; li < ic.layer_count; ++li) {
        const VectorIconLayer& layer = ic.layers[li];
        if (!layer.has_outline || layer.opacity <= 0.0f) {
            continue;
        }
        Color layer_col = (layer.color_slot == 1u && !secondary_color.is_theme_auto()) ? secondary_color : color;
        const float total_alpha = eff_opacity * layer.opacity;
        if (total_alpha < 0.9999f) {
            const float scaled_a = static_cast<float>(layer_col.a) * total_alpha;
            layer_col.a = static_cast<uint8_t>(std::fmin(255.0f, std::fmax(0.0f, std::round(scaled_a))));
        }
        if (layer_col.a == 0u) {
            continue;
        }
        push_prim(
            ctx,
            Prim::make_slug_glyph(
                quad,
                clip.rect,
                layer_col,
                layer.band_offset,
                layer.num_hbands,
                layer.num_vbands,
                clip.radius,
                stem_flag,
                angle_rad));
    }
}

DrawCmdNode* append_open_box_draw_cmd(Context* ctx, bool foreground) noexcept {
    if (!ctx || !ctx->in_frame || !ctx->active_ui->layout) {
        return nullptr;
    }
    BoxExtra* extra = ctx->layout_draw_target;
    LayoutNode* open_elem = nullptr;
    if (!extra) {
        LayoutEngine* le = ctx->active_ui->layout;
        if (le->open_stack.count < 2u) {
            return nullptr;
        }
        open_elem = layout_engine_open_node(le);
        if (!open_elem || open_elem->is_text) {
            return nullptr;
        }
        extra = static_cast<BoxExtra*>(open_elem->cfg.user_data);
        if (!extra) {
            extra = ctx->frame_arena.alloc<BoxExtra>();
            if (!extra) {
                return nullptr;
            }
            extra->layer = ctx->active_ui->current_layer;
            extra->kind = CustomCmdKind::Box;
            open_elem->cfg.user_data = extra;
        }
    }
    if (!ctx->layout_draw_target && !foreground) {
        if (!open_elem) open_elem = layout_engine_open_node(ctx->active_ui->layout);
        if (open_elem && !open_elem->cfg.emit_custom) {
            open_elem->cfg.emit_custom = true;
            open_elem->cfg.custom_data = extra;
        }
    } else if (!ctx->layout_draw_target) {
        if (!open_elem) open_elem = layout_engine_open_node(ctx->active_ui->layout);
        if (open_elem) {
            open_elem->cfg.emit_border = true;
        }
    }
    DrawCmdNode* node = ctx->frame_arena.alloc<DrawCmdNode>();
    if (!node) {
        return nullptr;
    }
    node->layer = ctx->layout_draw_target ? ctx->active_render_layer : ctx->active_ui->current_layer;
    node->world_scale = ctx->layout_draw_target ? ctx->layout_draw_scale : current_world_scale(ctx);
    if (!foreground) {
        if (!extra->draw_head) {
            extra->draw_head = node;
            extra->draw_tail = node;
        } else {
            extra->draw_tail->next = node;
            extra->draw_tail = node;
        }
    } else {
        if (!extra->fg_draw_head) {
            extra->fg_draw_head = node;
            extra->fg_draw_tail = node;
        } else {
            extra->fg_draw_tail->next = node;
            extra->fg_draw_tail = node;
        }
    }
    return node;
}

Vec2 canvas_screen_to_world(const WidgetState* cws, Vec2 screen) noexcept {
    const WidgetCold* cc = cws ? cws->cold : nullptr;
    const float z = (cc && cc->canvas_zoom > 1e-4f) ? cc->canvas_zoom : 1.0f;
    const Vec2 pan = cc ? cc->canvas_pan : Vec2{};
    return Vec2{
        (screen.x - cws->last_box_logical.x - pan.x) / z,
        (screen.y - cws->last_box_logical.y - pan.y) / z,
    };
}

void push_chrome_rect(Rect* dst, uint32_t* count, uint32_t cap, Rect r) noexcept {
    if (r.w > 0.0f && r.h > 0.0f && *count < cap) {
        dst[(*count)++] = r;
    }
}

Vec2 measure_multiline_text_metrics(
    Context* ctx,
    Str text,
    uint16_t font_id,
    float size,
    float letter_spacing,
    uint32_t* out_line_count,
    float* out_line_h) noexcept
{
    float line_h = size * 1.25f;
    if (ctx && ctx->atlas.font_valid) {
        const uint16_t slot_idx = resolve_font_slot_idx(&ctx->atlas, font_id);
        if (slot_idx != kInvalidFont && ctx->atlas.slots[slot_idx].line_height_em > 0.0f) {
            line_h = ctx->atlas.slots[slot_idx].line_height_em * size;
        }
    }
    if (out_line_h) {
        *out_line_h = line_h;
    }
    if (!text.chars || text.length == 0u || size <= 0.0f) {
        if (out_line_count) {
            *out_line_count = 0u;
        }
        return Vec2{ 0.0f, 0.0f };
    }

    float max_w = 0.0f;
    uint32_t line_count = 0u;
    uint32_t pos = 0u;
    while (pos <= text.length) {
        uint32_t end = pos;
        while (end < text.length && text.chars[end] != '\n') {
            ++end;
        }
        uint32_t trim_end = end;
        if (trim_end > pos && text.chars[trim_end - 1u] == '\r') {
            --trim_end;
        }
        float line_w = 0.0f;
        measure_line_segment_x(
            ctx,
            font_id,
            text.chars + pos,
            trim_end - pos,
            0u,
            0u,
            size,
            letter_spacing,
            false,
            false,
            nullptr,
            nullptr,
            &line_w);
        if (line_w > max_w) {
            max_w = line_w;
        }
        ++line_count;
        if (end >= text.length) {
            break;
        }
        pos = end + 1u;
    }
    if (out_line_count) {
        *out_line_count = line_count;
    }
    return Vec2{ max_w, static_cast<float>(line_count) * line_h };
}

void emit_text_cmd_pass(
    Context* ctx,
    const TextDrawDesc& tc,
    Vec2 origin,
    float scale,
    const ClipEntry& clip,
    const uint32_t* terminal_columns,
    float terminal_cell_width,
    uint16_t primary_slot,
    const FontSlot& primary,
    float start_y_logical,
    float line_h_logical,
    Vec2 pass_offset_logical,
    Color pass_color,
    bool use_wipe) noexcept
{
    const float phys_px = tc.size * scale;
    const float asc_px = primary.ascent_em * phys_px;
    const float ls_px = tc.letter_spacing * scale;

    uint32_t line_idx = 0u;
    uint32_t pos = 0u;
    while (pos <= tc.text.length) {
        uint32_t end = pos;
        while (end < tc.text.length && tc.text.chars[end] != '\n') {
            ++end;
        }
        uint32_t trim_end = end;
        if (trim_end > pos && tc.text.chars[trim_end - 1u] == '\r') {
            --trim_end;
        }
        const uint32_t seg_len = trim_end - pos;

        float line_w_logical = 0.0f;
        if ((tc.align_x != AlignX::Left || tc.max_width_px > 0) && seg_len > 0u) {
            measure_line_segment_x(
                ctx,
                tc.font_id,
                tc.text.chars + pos,
                seg_len,
                0u,
                0u,
                tc.size,
                tc.letter_spacing,
                false,
                false,
                nullptr,
                nullptr,
                &line_w_logical);
        }

        const bool elided = tc.max_width_px > 0 && line_w_logical > tc.max_width_px;
        if (elided && tc.ellipsis) line_w_logical = tc.max_width_px;
        float start_x_logical = tc.pos.x;
        if (tc.align_x == AlignX::Center) {
            start_x_logical -= line_w_logical * 0.5f;
        } else if (tc.align_x == AlignX::Right) {
            start_x_logical -= line_w_logical;
        }

        float pen_x = origin.x + (start_x_logical + pass_offset_logical.x) * scale;
        const float base_y = origin.y + (start_y_logical + pass_offset_logical.y + static_cast<float>(line_idx) * line_h_logical) * scale + asc_px;
        TextWipeParams wipe = {};
        if (use_wipe && std::isfinite(tc.wipe_x)) {
            const float feather = (std::isfinite(tc.wipe_feather_px) && tc.wipe_feather_px > 0.0f) ? tc.wipe_feather_px * scale : 0.0f;
            const float center_px = pen_x + tc.wipe_x * scale;
            wipe = {true, tc.color_end, center_px - 0.5f * feather, center_px + 0.5f * feather};
        }

        ClipEntry text_clip = clip;
        float clip_left = origin.x + tc.pos.x * scale;
        if (tc.align_x == AlignX::Right) clip_left -= tc.max_width_px * scale;
        else if (tc.align_x == AlignX::Center) clip_left -= tc.max_width_px * scale * 0.5f;
        if (wipe.enabled && tc.max_width_px > 0) {
            const auto* layer = ctx->active_render_layer;
            if (layer) resolve_layer_chain(ctx, layer, ctx->active_ui->dpi_scale);
            const float sx = layer ? layer->resolved_scale.x : 1.f;
            const float ox = layer ? layer->resolved_offset_px.x : 0.f;
            text_clip.rect = rect_intersect(clip.rect, {clip_left * sx + ox, clip.rect.y, tc.max_width_px * scale * sx, clip.rect.h});
        }
        const uint32_t first_text_prim = ctx->active_filter_group ? ctx->filter_prim_count : ctx->prim_count;
        if (elided && tc.ellipsis) {
            emit_elided_line(ctx, tc.text.chars + pos, seg_len, tc.font_id, tc.size, tc.letter_spacing,
                pen_x / scale, base_y / scale, tc.max_width_px, text_clip, pass_color, scale, wipe.enabled ? &wipe : nullptr);
        }
        uint32_t run_pos = elided && tc.ellipsis ? trim_end : pos;
        while (run_pos < trim_end) {
            if (tc.text.chars[run_pos] == '\t') {
                pen_x += 4.0f * primary.space_advance_em * phys_px + ls_px;
                ++run_pos;
                continue;
            }
            uint32_t run_end = run_pos;
            while (run_end < trim_end && tc.text.chars[run_end] != '\t') {
                ++run_end;
            }
            const std::span<const ShapedGlyph> glyphs = shape_utf8_run(ctx, primary_slot, tc.text.chars + run_pos, run_end - run_pos);
            uint32_t last_cluster = UINT32_MAX;
            for (const ShapedGlyph& g : glyphs) {
                if (terminal_columns && g.cluster != last_cluster) {
                    pen_x = origin.x + (start_x_logical + pass_offset_logical.x + float(terminal_columns[run_pos + g.cluster]) * terminal_cell_width) * scale;
                    last_cluster = g.cluster;
                }
                emit_slug_glyph_quad(
                    ctx,
                    g.font_slot,
                    g.glyph_id,
                    phys_px,
                    pen_x,
                    base_y,
                    g.x_offset_em,
                    g.y_offset_em,
                    text_clip,
                    pass_color,
                    wipe.enabled ? &wipe : nullptr);
                pen_x += g.x_advance_1px * phys_px + ls_px;
            }
            run_pos = run_end;
        }

        if (!wipe.enabled && tc.max_width_px > 0) {
            Prim* target_prims = ctx->active_filter_group ? ctx->filter_prims : ctx->prims;
            const uint32_t target_count = ctx->active_filter_group ? ctx->filter_prim_count : ctx->prim_count;
            for (uint32_t i = first_text_prim; i < target_count; ++i) {
                Prim& p = target_prims[i];
                p.style.x |= uint32_t(PrimFlag_TextClip) << 4;
                const auto* layer=ctx->active_render_layer;
                const float sx=layer ? layer->resolved_scale.x : 1.f;
                const float ox=layer ? layer->resolved_offset_px.x : 0.f;
                p.style.z = std::bit_cast<uint32_t>(clip_left*sx+ox);
                p.style.w = std::bit_cast<uint32_t>((clip_left + tc.max_width_px * scale)*sx+ox);
            }
        }
        ++line_idx;
        if (end >= tc.text.length) {
            break;
        }
        pos = end + 1u;
    }
}

void emit_draw_text_cmd(
    Context* ctx,
    const TextDrawDesc& tc,
    Vec2 origin,
    float scale,
    const ClipEntry& clip, const uint32_t* terminal_columns = nullptr, float terminal_cell_width = 0) noexcept
{
    const Color shadow_col = resolve_theme_color(tc.shadow_color, active_theme(ctx).shadow);
    const bool has_main = tc.color.a > 0u || (tc.has_wipe && tc.color_end.a > 0u);
    if (!ctx || !ctx->atlas.font_valid || !tc.text.chars || tc.text.length == 0u || tc.size <= 0.0f ||
        (!has_main && shadow_col.a == 0u)) {
        return;
    }
    const uint16_t primary_slot = resolve_font_slot_idx(&ctx->atlas, tc.font_id);
    if (primary_slot == kInvalidFont) {
        return;
    }
    const FontSlot& primary = ctx->atlas.slots[primary_slot];

    uint32_t line_count = 0u;
    float line_h_logical = 0.0f;
    const Vec2 total_sz = measure_multiline_text_metrics(
        ctx,
        tc.text,
        tc.font_id,
        tc.size,
        tc.letter_spacing,
        &line_count,
        &line_h_logical);
    if (line_count == 0u) {
        return;
    }

    float start_y_logical = tc.pos.y;
    if (tc.align_y == AlignY::Center) {
        start_y_logical -= total_sz.y * 0.5f;
    } else if (tc.align_y == AlignY::Bottom) {
        start_y_logical -= total_sz.y;
    }

    if (shadow_col.a > 0u) {
        const Vec2 base_off = {
            std::isfinite(tc.shadow_offset.x) ? tc.shadow_offset.x : 0.0f,
            std::isfinite(tc.shadow_offset.y) ? tc.shadow_offset.y : 0.0f,
        };
        const float blur = (std::isfinite(tc.shadow_blur) && tc.shadow_blur > 0.0f) ? tc.shadow_blur : 0.0f;
        if (blur <= 0.5f) {
            emit_text_cmd_pass(ctx, tc, origin, scale, clip, terminal_columns, terminal_cell_width,
                primary_slot, primary, start_y_logical, line_h_logical, base_off, shadow_col, false);
        } else {
            static constexpr float kCos8[8] = {1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f, 0.0f, 0.70710678f};
            static constexpr float kSin8[8] = {0.0f, 0.70710678f, 1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f};
            static constexpr float kCos4[4] = {0.70710678f, -0.70710678f, -0.70710678f, 0.70710678f};
            static constexpr float kSin4[4] = {0.70710678f, 0.70710678f, -0.70710678f, -0.70710678f};
            const float r = clamp_range(blur * 0.45f, 0.5f, 24.0f);
            const bool two_ring = blur >= 4.0f;
            const float w_inner = two_ring ? 0.10f : 0.125f;
            const uint8_t inner_a = static_cast<uint8_t>(clamp_range(std::round(float(shadow_col.a) * w_inner), 0.0f, 255.0f));
            if (inner_a > 0u) {
                const Color tap_col = {shadow_col.r, shadow_col.g, shadow_col.b, inner_a};
                for (uint32_t k = 0u; k < 8u; ++k) {
                    emit_text_cmd_pass(ctx, tc, origin, scale, clip, terminal_columns, terminal_cell_width,
                        primary_slot, primary, start_y_logical, line_h_logical,
                        {base_off.x + kCos8[k] * r, base_off.y + kSin8[k] * r}, tap_col, false);
                }
            }
            if (two_ring) {
                const float r_out = r * 1.65f;
                const uint8_t outer_a = static_cast<uint8_t>(clamp_range(std::round(float(shadow_col.a) * 0.08f), 0.0f, 255.0f));
                if (outer_a > 0u) {
                    const Color tap_col = {shadow_col.r, shadow_col.g, shadow_col.b, outer_a};
                    for (uint32_t k = 0u; k < 4u; ++k) {
                        emit_text_cmd_pass(ctx, tc, origin, scale, clip, terminal_columns, terminal_cell_width,
                            primary_slot, primary, start_y_logical, line_h_logical,
                            {base_off.x + kCos4[k] * r_out, base_off.y + kSin4[k] * r_out}, tap_col, false);
                    }
                }
            }
        }
    }

    if (has_main) {
        emit_text_cmd_pass(ctx, tc, origin, scale, clip, terminal_columns, terminal_cell_width,
            primary_slot, primary, start_y_logical, line_h_logical, {0.0f, 0.0f}, tc.color, tc.has_wipe);
    }
}

void emit_vector_fill(Context* ctx, const FilledPathDesc& desc, Vec2 origin, float scale,
    const ClipEntry& clip) noexcept {
    const size_t mark = ctx->frame_arena.mark();
    uint32_t count = 0;
    const SlugRawCurve* raw = decode_vector_path(ctx, desc.svg_path_d, desc.commands, {}, &count);
    if (count == 0) { ctx->frame_arena.rewind(mark); return; }
    float min_x = raw[0].p1x, max_x = min_x, min_y = raw[0].p1y, max_y = min_y;
    for (uint32_t i = 0; i < count; ++i) {
        const SlugRawCurve& r = raw[i];
        min_x = std::fmin(min_x, std::fmin(r.p1x, std::fmin(r.p2x, r.p3x)));
        max_x = std::fmax(max_x, std::fmax(r.p1x, std::fmax(r.p2x, r.p3x)));
        min_y = std::fmin(min_y, std::fmin(r.p1y, std::fmin(r.p2y, r.p3y)));
        max_y = std::fmax(max_y, std::fmax(r.p1y, std::fmax(r.p2y, r.p3y)));
    }
    const float path_scale = scale * desc.scale;
    const Vec2 base = origin + desc.offset * scale;
    if (desc.fill.a > 0u || desc.fill_end.a > 0u) {
        const CompiledSlugShape shape = compile_slug_contours(&ctx->atlas, raw, count,
            min_x, min_y, max_x-min_x, max_y-min_y, ctx);
        if (shape.has_outline) {
            push_prim(ctx, Prim::make_slug_fill({base.x+min_x*path_scale, base.y-max_y*path_scale,
                (max_x-min_x)*path_scale, (max_y-min_y)*path_scale}, clip.rect, desc.fill,
                shape.band_offset, shape.num_hbands, shape.num_vbands, desc.fill_end,
                desc.gradient_dir, desc.radial_gradient, clip.radius));
        }
    }
    if (desc.stroke_width > 0.0f && desc.stroke_color.a > 0u) {
        for (uint32_t i = 0; i < count; ++i) {
            const SlugRawCurve& r = raw[i];
            if (r.implicit_close) continue;
            push_prim(ctx, Prim::make_bezier_wire(base + Vec2{r.p1x,-r.p1y}*path_scale,
                base + Vec2{r.p2x,-r.p2y}*path_scale, base + Vec2{r.p3x,-r.p3y}*path_scale,
                clip.rect, desc.stroke_color, desc.stroke_color, desc.stroke_width*path_scale*.5f,
                {}, 0, clip.radius));
        }
    }
    ctx->frame_arena.rewind(mark);
}

void emit_draw_list_command(
    Context* ctx,
    const DrawCmdNode* head,
    Rect box_px,
    const ClipEntry& clip,
    float dpi) noexcept
{
    const uint32_t previous_clip=ctx->render_clip_index;
    const uint32_t previous_shape=ctx->render_clip_shape;
    ctx->render_clip_index=clip.indexed ? clip.shape : 0;
    ctx->render_clip_shape=clip.shape;
    const Vec2 origin = { box_px.x, box_px.y };
    const auto* previous_layer=ctx->active_render_layer;
    for (const DrawCmdNode* node = head; node != nullptr; node = node->next) {
        ctx->active_render_layer = node->layer;
        if (ctx->active_render_layer) {
            resolve_layer_chain(ctx, ctx->active_render_layer, dpi);
        }
        const float scale = node->world_scale * dpi;
        switch (node->kind) {
            case DrawCmdKind::Callback: {
                BoxExtra target = {};
                ctx->layout_draw_target = &target;
                ctx->layout_draw_scale = node->world_scale;
                push_theme(*node->cb.theme);
                node->cb.fn({0,0,box_px.w / scale,box_px.h / scale},node->cb.data);
                pop_theme();
                ctx->layout_draw_target = nullptr;
                emit_draw_list_command(ctx,target.draw_head,box_px,clip,dpi);
                emit_draw_list_command(ctx,target.fg_draw_head,box_px,clip,dpi);
                break;
            }
            case DrawCmdKind::Beam: {
                const auto& d=node->beam;
                const Rect r={origin.x+d.rect.x*scale,origin.y+d.rect.y*scale,d.rect.w*scale,d.rect.h*scale};
                emit_border_beam(ctx,r,clip,scale_corners(d.radius,scale),d.beam,d.top_rim_alpha,d.continuous_corners,scale);
                break;
            }
            case DrawCmdKind::Hole: {
                const auto& d=node->hole;
                const Rect r={origin.x+d.rect.x*scale,origin.y+d.rect.y*scale,d.rect.w*scale,d.rect.h*scale};
                push_prim(ctx,Prim::make_punch_hole(r,clip.rect,scale_corners(d.radius,scale),d.softness,d.continuous_corners,clip.radius));
                break;
            }
            case DrawCmdKind::Mesh: {
                const auto& d=node->mesh;
                const Rect r={origin.x+d.rect.x*scale,origin.y+d.rect.y*scale,d.rect.w*scale,d.rect.h*scale};
                push_prim(ctx,Prim::make_mesh_grad(r,clip.rect,d.color_tl,d.color_tr,d.color_br,d.color_bl,
                    scale_corners(d.radius,scale),d.oklab,d.grain_intensity,d.warp_phase,d.continuous_corners,clip.radius));
                break;
            }
            case DrawCmdKind::Fill: emit_vector_fill(ctx, node->fill_cmd, origin, scale, clip); break;
            case DrawCmdKind::Shadow: {
                const ShadowDrawCmd& sd = node->shadow_cmd;
                if (sd.color.a > 0u && sd.blur > 0.0f) {
                    const Rect r_px = {
                        .x = origin.x + sd.rect.x * scale,
                        .y = origin.y + sd.rect.y * scale,
                        .w = sd.rect.w * scale,
                        .h = sd.rect.h * scale,
                    };
                    const float blur_px = sd.blur * scale;
                    push_prim(
                        ctx,
                        continuous_prim(
                            Prim::make_shadow(r_px, clip.rect, sd.color, scale_corners(sd.radius, scale), blur_px, clip.radius, sd.offset * scale),
                            sd.continuous_corners),
                        nullptr,
                        2.0f * blur_px + 2.0f);
                }
                break;
            }
            case DrawCmdKind::Rect: {
                const RectDrawDesc& rd = node->rect;
                Rect r_px = {
                    .x = origin.x + rd.rect.x * scale,
                    .y = origin.y + rd.rect.y * scale,
                    .w = rd.rect.w * scale,
                    .h = rd.rect.h * scale,
                };
                Corners rad_px = scale_corners(rd.radius, scale);
                const float bw_px = rd.border_width * scale;
                align_border_bounds(&r_px,&rad_px,bw_px,rd.border_align);
                const bool want_multi_grad = (rd.gradient.kind != GradientKind::None) && (rd.gradient.stop_count >= 2u);
                const bool has_grad = (rd.fill_end.pack_rgba8() != 0u) || rd.radial_gradient;
                if (want_multi_grad) {
                    push_prim(
                        ctx,
                        make_multistop_or_fallback_rect_prim(
                            ctx, r_px, clip, rd.gradient, rd.fill, rad_px, rd.border_color, bw_px,
                            rd.continuous_corners, rd.softness, rd.angle_rad));
                } else if (std::fabs(rd.angle_rad) > 1e-5f) {
                    push_prim(ctx, continuous_prim(Prim::make_rotated_rect(r_px, clip.rect, rd.fill, rd.angle_rad,
                        rad_px, rd.border_color, bw_px, rd.fill_end, rd.gradient_dir, rd.radial_gradient, clip.radius), rd.continuous_corners));
                } else if (has_grad) {
                    push_prim(
                        ctx,
                        continuous_prim(Prim::make_gradient_rect(
                            r_px,
                            clip.rect,
                            rd.fill,
                            rd.fill_end,
                            rd.gradient_dir,
                            rad_px,
                            rd.border_color,
                            bw_px,
                            clip.radius,
                            rd.radial_gradient, rd.softness), rd.continuous_corners));
                } else if (rd.fill.a > 0u || (bw_px > 0.0f && rd.border_color.a > 0u)) {
                    push_prim(
                        ctx,
                        continuous_prim(Prim::make_rect(
                            r_px,
                            clip.rect,
                            rd.fill,
                            rad_px,
                            rd.border_color,
                            bw_px,
                            rd.softness,
                            clip.radius), rd.continuous_corners));
                }
                break;
            }
            case DrawCmdKind::Line: {
                const LineDesc& ld = node->line;
                Vec2 a = origin + ld.from * scale;
                Vec2 b = origin + ld.to * scale;
                float thickness = std::fmax(ld.thickness * scale, 0.75f);
                if (ctx->active_ui->pixel_snap && ld.glow_px <= 0 && (a.x == b.x || a.y == b.y)) {
                    thickness = std::fmax(1.0f, std::round(thickness));
                    const float offset = std::fmod(thickness, 2.0f) * 0.5f;
                    if (a.x == b.x) a.x = b.x = std::round(a.x - offset) + offset;
                    else a.y = b.y = std::round(a.y - offset) + offset;
                }
                const Vec2 mid = (a + b) * 0.5f;
                const float hw = thickness * 0.5f;
                const Color c_end = (ld.color_end.a > 0u) ? ld.color_end : ld.color;
                push_prim(
                    ctx,
                    Prim::make_bezier_wire(
                        a, mid, b, clip.rect, ld.color, c_end, hw, ld.glow_color, ld.glow_px * scale, clip.radius));
                break;
            }
            case DrawCmdKind::Polyline: {
                const DrawCmdPolyPayload& pl = node->poly;
                if (!pl.points || pl.count < 2u || pl.color.a == 0u) {
                    break;
                }
                const float hw = 0.5f * ((pl.thickness * scale > 0.75f) ? (pl.thickness * scale) : 0.75f);
                const float glow = pl.glow_px * scale;
                const uint32_t segs = (pl.closed && pl.count >= 3u)
                    ? pl.count
                    : (pl.count - 1u);
                for (uint32_t i = 0u; i < segs; ++i) {
                    const Vec2 a = origin + pl.points[i] * scale;
                    const Vec2 b = origin + pl.points[(i + 1u) % pl.count] * scale;
                    const Vec2 mid = (a + b) * 0.5f;
                    push_prim(
                        ctx,
                        Prim::make_bezier_wire(
                            a, mid, b, clip.rect, pl.color, pl.color, hw, pl.glow_color, glow, clip.radius));
                }
                break;
            }
            case DrawCmdKind::Bezier: {
                const BezierDesc& bz = node->bezier;
                const float hw = 0.5f * ((bz.thickness * scale > 0.75f) ? (bz.thickness * scale) : 0.75f);
                const float glow = bz.glow_px * scale;
                const Color c_end = (bz.color_end.a > 0u) ? bz.color_end : bz.color_start;
                const Vec2 p0 = origin + bz.p0 * scale;
                const Vec2 p1 = origin + bz.p1 * scale;
                const Vec2 p2 = origin + bz.p2 * scale;
                if (!bz.cubic) {
                    push_prim(
                        ctx,
                        Prim::make_bezier_wire(
                            p0, p1, p2, clip.rect, bz.color_start, c_end, hw, bz.glow_color, glow, clip.radius));
                } else {
                    const Vec2 p3 = origin + bz.p3 * scale;
                    const auto eval_c = [&](float t) noexcept -> Vec2 {
                        const float u = 1.0f - t;
                        return p0 * (u * u * u) + p1 * (3.0f * u * u * t) + p2 * (3.0f * u * t * t) + p3 * (t * t * t);
                    };
                    const auto eval_d = [&](float t) noexcept -> Vec2 {
                        const float u = 1.0f - t;
                        return ((p1 - p0) * (u * u) + (p2 - p1) * (2.0f * u * t) + (p3 - p2) * (t * t)) * 3.0f;
                    };
                    constexpr float dt = 0.25f;
                    for (uint32_t i = 0u; i < 4u; ++i) {
                        const float t0 = static_cast<float>(i) * dt;
                        const float t1 = static_cast<float>(i + 1u) * dt;
                        const Vec2 a = eval_c(t0);
                        const Vec2 d = eval_c(t1);
                        const Vec2 b_ctrl = a + eval_d(t0) * (dt / 3.0f);
                        const Vec2 c_ctrl = d - eval_d(t1) * (dt / 3.0f);
                        const Vec2 q_ctrl = (b_ctrl + c_ctrl) * 0.75f - (a + d) * 0.25f;
                        const Color cs = Color::lerp(bz.color_start, c_end, t0);
                        const Color ce = Color::lerp(bz.color_start, c_end, t1);
                        push_prim(
                            ctx,
                            Prim::make_bezier_wire(
                                a, q_ctrl, d, clip.rect, cs, ce, hw, bz.glow_color, glow, clip.radius));
                    }
                }
                break;
            }
            case DrawCmdKind::Ring: {
                const RingDesc& rn = node->ring;
                const Vec2 center_px = origin + rn.center * scale;
                const float r_px = (rn.radius > 0.5f) ? (rn.radius * scale) : (0.5f * scale);
                const float half_t_px = 0.5f * ((rn.thickness > 0.75f) ? (rn.thickness * scale) : (0.75f * scale));
                if (rn.track_color.a > 0u) {
                    push_prim(
                        ctx,
                        Prim::make_ring_arc(
                            center_px,
                            r_px,
                            half_t_px,
                            0.0f,
                            6.28318531f,
                            clip.rect,
                            rn.track_color,
                            rn.track_color,
                            false,
                            Color{},
                            0.0f,
                            clip.radius));
                }
                const Color col_end = (rn.color_end.a > 0u) ? rn.color_end : rn.color;
                if (rn.color.a > 0u || col_end.a > 0u) {
                    push_prim(
                        ctx,
                        Prim::make_ring_arc(
                            center_px,
                            r_px,
                            half_t_px,
                            rn.start_angle_rad,
                            rn.sweep_angle_rad,
                            clip.rect,
                            rn.color,
                            col_end,
                            rn.round_caps,
                            rn.glow_color,
                            rn.glow_px * scale,
                            clip.radius));
                }
                break;
            }
            case DrawCmdKind::Icon: {
                const IconDrawDesc& ic = node->icon;
                const Rect target_px = {
                    .x = origin.x + ic.rect.x * scale,
                    .y = origin.y + ic.rect.y * scale,
                    .w = ic.rect.w * scale,
                    .h = ic.rect.h * scale,
                };
                emit_vector_icon_quad(ctx, ic.icon_id, target_px, ic.color, clip, ic.angle_rad);
                break;
            }
            case DrawCmdKind::Text: {
                emit_draw_text_cmd(ctx, node->text_payload.cmd, origin, scale, clip,
                    node->text_payload.terminal_columns, node->text_payload.terminal_cell_width);
                break;
            }
            case DrawCmdKind::Image: {
                const ImageDrawDesc& im = node->image_cmd;
                if ((!im.texture && im.sprite_id == kInvalidSprite) || im.tint.a == 0u || im.rect.w <= 0.0f || im.rect.h <= 0.0f) {
                    break;
                }
                Rect target_px = {
                    .x = origin.x + im.rect.x * scale,
                    .y = origin.y + im.rect.y * scale,
                    .w = im.rect.w * scale,
                    .h = im.rect.h * scale,
                };
                Vec2 uv_min = { im.uv_rect.x, im.uv_rect.y };
                Vec2 uv_max = { im.uv_rect.x + im.uv_rect.w, im.uv_rect.y + im.uv_rect.h };
                apply_image_fit(ctx, im.fit, im.source_size, im.sprite_id, &target_px, &uv_min, &uv_max);
                const bool layer_rot = ctx->active_render_layer && std::fabs(ctx->active_render_layer->resolved_rotation_rad) > 1e-4f;
                if (ctx->active_ui->pixel_snap && !layer_rot) target_px = snap_pixel_rect(target_px);
                if (im.sprite_id != kInvalidSprite && !resolve_sprite_uv_rect(ctx, im.sprite_id, uv_min, uv_max, &uv_min, &uv_max)) break;
                const uint8_t img_flags = static_cast<uint8_t>(
                    ((im.sprite_id != kInvalidSprite || surface_texture_premultiplied(ctx, im.texture)) ? PrimFlag_Premultiplied : PrimFlag_None) |
                    (im.continuous_corners ? PrimFlag_LinearGrad : PrimFlag_None));
                push_prim(
                    ctx,
                    Prim::make_texture(
                        target_px,
                        clip.rect,
                        uv_min,
                        uv_max,
                        0u,
                        Corners::all(im.radius * scale),
                        im.tint,
                        Color{},
                        0.0f,
                        img_flags,
                        clip.radius),
                    im.sprite_id != kInvalidSprite ? nullptr : im.texture);
                break;
            }
        }
    }
    ctx->render_clip_index=previous_clip;
    ctx->render_clip_shape=previous_shape;
    ctx->active_render_layer=previous_layer;
}

// End internal unity-build helpers.

bool needs_redraw(const Context* ctx) noexcept {
    if (!ctx) return false;
    if ((ctx->headless || ctx->claimed_window_count == 0u) && ctx->headless_ui &&
        (ctx->headless_ui->redraw_budget || ctx->headless_ui->active_anim_count))
    {
        return true;
    }
    for (uint32_t i = 0; i < ctx->claimed_window_cap; ++i) {
        if (!ctx->claimed_windows[i]) continue;
        const ClaimedWindowSlot& slot = *ctx->claimed_windows[i];
        if (!slot.window) continue;
        if ((SDL_GetWindowFlags(slot.window) & SDL_WINDOW_MINIMIZED) != 0u) continue;
        if (slot.ui.redraw_budget || slot.ui.active_anim_count) return true;
    }
    return shader_surface_timeout(ctx, nullptr, true, -1) == 0;
}

void request_redraw(Context* ctx_arg) noexcept {
    Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (ctx && ctx->active_ui) {
        ctx->active_ui->redraw_budget = 2u;
    }
}

void wake_up(Context* ctx_arg) noexcept {
    Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (!ctx) {
        return;
    }
    if (ctx->wake_event_type != 0u) {
        SDL_Event ev = {};
        ev.type = ctx->wake_event_type;
        SDL_PushEvent(&ev);
    } else if (ctx->headless && ctx->active_ui) {
        ctx->active_ui->redraw_budget = 2u;
    }
}

bool wait_events(Context* ctx, SDL_Event* first_event_out, int32_t timeout_ms) noexcept {
    if (!first_event_out) {
        return false;
    }
    const uint64_t now = SDL_GetTicks();
    if (ctx) timeout_ms = idle_gpu_timeout(ctx, now, timeout_ms);
    WindowUiState* caret_ui = ctx ? window_ui(ctx, ctx->ime_active_window) : nullptr;
    if (caret_ui && caret_ui->ime_focused_id != 0u && caret_ui->caret_blink_cycles < 20u) {
        if (!caret_ui->caret_blink_tick) caret_ui->caret_blink_tick = now;
        const uint64_t elapsed = now - caret_ui->caret_blink_tick;
        if (elapsed >= 500) {
            caret_ui->caret_blink_on = !caret_ui->caret_blink_on;
            ++caret_ui->caret_blink_cycles;
            if (caret_ui->caret_blink_cycles == 20u) caret_ui->caret_blink_on = true;
            caret_ui->caret_blink_tick = now;
            caret_ui->redraw_budget = 2;
        }
        const int32_t remaining = elapsed >= 500 ? 500 : int32_t(500 - elapsed);
        if (timeout_ms < 0 || remaining < timeout_ms) timeout_ms = remaining;
    }
    if (ctx) {
        auto check_tooltip_deadline = [&](WindowUiState& wui) noexcept {
            if (wui.tooltip_wake_tick == 0u) return;
            if (now >= wui.tooltip_wake_tick) {
                wui.tooltip_wake_tick = 0u;
                if (wui.redraw_budget < 2u) wui.redraw_budget = 2u;
            } else {
                const uint64_t diff = wui.tooltip_wake_tick - now;
                const int32_t rem = diff > 60000u ? 60000 : static_cast<int32_t>(diff);
                if (timeout_ms < 0 || rem < timeout_ms) timeout_ms = rem;
            }
        };
        if ((ctx->headless || ctx->claimed_window_count == 0u) && ctx->headless_ui) {
            check_tooltip_deadline(*ctx->headless_ui);
        }
        for (uint32_t i = 0; i < ctx->claimed_window_cap; ++i) {
            ClaimedWindowSlot* slot = ctx->claimed_windows[i];
            if (!slot || !slot->window) continue;
            if ((SDL_GetWindowFlags(slot->window) & SDL_WINDOW_MINIMIZED) != 0u) continue;
            check_tooltip_deadline(slot->ui);
        }
    }
    if (needs_redraw(ctx)) return SDL_PollEvent(first_event_out);
    if (ctx) timeout_ms = shader_surface_timeout(ctx, nullptr, true, timeout_ms);
    if (timeout_ms < 0) {
        return SDL_WaitEvent(first_event_out);
    }
    const bool received = SDL_WaitEventTimeout(first_event_out, timeout_ms);
    if (ctx) idle_gpu_timeout(ctx, SDL_GetTicks(), 0);
    return received;
}

bool verify_caret_deadline() noexcept {
    Context* ctx=create_context({.headless=true});
    if(!ctx)return false;
    WindowUiState* ui_ptr=ensure_headless_ui(ctx);
    if(!ui_ptr){destroy_context(ctx);set_current_context(nullptr);return false;}
    auto& ui=*ui_ptr;
    ui.ime_focused_id=1;
    ui.caret_blink_on=true;
    ui.caret_blink_tick=SDL_GetTicks()-501;
    SDL_Event event={};
    wait_events(ctx,&event,0); // A short shader/event deadline must not starve blinking.
    const bool hidden=!ui.caret_blink_on && ui.caret_blink_cycles==1;
    ui.caret_blink_tick=SDL_GetTicks()-501;
    wait_events(ctx,&event,0);
    const bool shown=ui.caret_blink_on && ui.caret_blink_cycles==2;
    destroy_context(ctx);
    set_current_context(nullptr);
    return hidden && shown;
}

static void touch_track_upsert(WindowUiState* ui, uint64_t finger_id, Vec2 pos, float pressure) noexcept {
    const TouchPoint tp = { .finger_id = finger_id, .pos = pos, .pressure = pressure };
    for (uint32_t i = 0u; i < ui->active_touch_count; ++i) {
        if (ui->active_touch_ids[i] == finger_id) {
            ui->active_touch_points[i] = tp;
            return;
        }
    }
    if (ui->active_touch_count < ETCH_COUNTOF(ui->active_touch_ids)) {
        const uint32_t idx = ui->active_touch_count++;
        ui->active_touch_ids[idx] = finger_id;
        ui->active_touch_points[idx] = tp;
    }
}

static void touch_track_remove(WindowUiState* ui, uint64_t finger_id) noexcept {
    for (uint32_t i = 0u; i < ui->active_touch_count; ++i) {
        if (ui->active_touch_ids[i] == finger_id) {
            for (uint32_t j = i + 1u; j < ui->active_touch_count; ++j) {
                ui->active_touch_ids[j - 1u] = ui->active_touch_ids[j];
                ui->active_touch_points[j - 1u] = ui->active_touch_points[j];
            }
            --ui->active_touch_count;
            ui->active_touch_ids[ui->active_touch_count] = 0u;
            ui->active_touch_points[ui->active_touch_count] = {};
            return;
        }
    }
}

static void process_window_event(Context* ctx, const SDL_Event& event) noexcept {
    if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
        if (SDL_Window* window = SDL_GetWindowFromID(event.window.windowID)) sync_desktop_window(window, ctx);
    }
    if (ctx->wake_event_type != 0u && event.type == ctx->wake_event_type) {
        request_redraw(ctx);
        return;
    }

    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
            ctx->active_ui->event_pointer_pos = window_to_logical_pos_for(
                ctx, SDL_GetWindowFromID(event.motion.windowID), event.motion.x, event.motion.y);
            ctx->active_ui->has_event_pointer = true;
            if (event.motion.which != SDL_PEN_MOUSEID && event.motion.which != SDL_TOUCH_MOUSEID) {
                PointerState& ps = ctx->active_ui->pointer_state;
                ps.kind = PointerKind::Mouse;
                ps.pointer_id = static_cast<uint32_t>(event.motion.which);
                ps.pressure = (event.motion.state != 0u) ? 1.0f : 0.0f;
                ps.tilt_x = 0.0f;
                ps.tilt_y = 0.0f;
                ps.pen_eraser = false;
            }
            request_redraw(ctx);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.which != SDL_PEN_MOUSEID && event.button.which != SDL_TOUCH_MOUSEID) {
                PointerState& ps = ctx->active_ui->pointer_state;
                ps.kind = PointerKind::Mouse;
                ps.pointer_id = static_cast<uint32_t>(event.button.which);
                ps.pressure = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? 1.0f : 0.0f;
                ps.tilt_x = 0.0f;
                ps.tilt_y = 0.0f;
                ps.pen_eraser = false;
            }
            if (event.button.button == SDL_BUTTON_LEFT) {
                ctx->active_ui->event_pointer_pos = window_to_logical_pos_for(
                    ctx, SDL_GetWindowFromID(event.button.windowID), event.button.x, event.button.y);
                const bool down = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                ctx->active_ui->event_pointer_down = down;
                if (down) ctx->active_ui->event_left_pressed_edge = true;
                else ctx->active_ui->event_left_released_edge = true;
                ctx->active_ui->has_event_pointer = true;
                if (down) {
                    ctx->active_ui->last_mouse_clicks = (event.button.clicks > 0u) ? event.button.clicks : 1u;
                    ctx->active_ui->caret_blink_on = true;
                    ctx->active_ui->caret_blink_cycles = 0u;
                    ctx->active_ui->caret_blink_tick = SDL_GetTicks();
                }
                request_redraw(ctx);
            } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                ctx->active_ui->event_pointer_pos = window_to_logical_pos_for(
                    ctx, SDL_GetWindowFromID(event.button.windowID), event.button.x, event.button.y);
                ctx->active_ui->has_event_pointer = true;
                ctx->active_ui->event_middle_down = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                request_redraw(ctx);
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                ctx->active_ui->event_pointer_pos = window_to_logical_pos_for(
                    ctx, SDL_GetWindowFromID(event.button.windowID), event.button.x, event.button.y);
                ctx->active_ui->has_event_pointer = true;
                const bool rdown = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                ctx->active_ui->event_right_down = rdown;
                if (rdown) ctx->active_ui->event_right_pressed_edge = true;
                else ctx->active_ui->event_right_released_edge = true;
                request_redraw(ctx);
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL: {
            float wx = event.wheel.x;
            float wy = event.wheel.y;
            if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                wx = -wx;
                wy = -wy;
            }
            ctx->active_ui->event_scroll_delta.x += wx;
            ctx->active_ui->event_scroll_delta.y += wy;
            if (std::fabs(wx - std::round(wx)) > 0.01f || std::fabs(wy - std::round(wy)) > 0.01f) {
                ctx->active_ui->pointer_state.precise_scroll = true;
            }
            if (event.wheel.mouse_x >= 0.0f && event.wheel.mouse_y >= 0.0f) {
                ctx->active_ui->event_pointer_pos = window_to_logical_pos_for(
                    ctx, SDL_GetWindowFromID(event.wheel.windowID), event.wheel.mouse_x, event.wheel.mouse_y);
                ctx->active_ui->has_event_pointer = true;
            }
            request_redraw(ctx);
            break;
        }
        case SDL_EVENT_PEN_PROXIMITY_IN:
        case SDL_EVENT_PEN_PROXIMITY_OUT: {
            PointerState& ps = ctx->active_ui->pointer_state;
            ps.kind = PointerKind::Pen;
            ps.pointer_id = static_cast<uint32_t>(event.pproximity.which);
            if (event.type == SDL_EVENT_PEN_PROXIMITY_OUT) {
                ps.pressure = 0.0f;
                ps.tilt_x = 0.0f;
                ps.tilt_y = 0.0f;
                ps.pen_eraser = false;
            } else {
                ps.pen_eraser = (event.pproximity.pen_state & SDL_PEN_INPUT_ERASER_TIP) != 0u;
            }
            request_redraw(ctx);
            break;
        }
        case SDL_EVENT_PEN_DOWN:
        case SDL_EVENT_PEN_UP: {
            const bool down = (event.type == SDL_EVENT_PEN_DOWN);
            PointerState& ps = ctx->active_ui->pointer_state;
            ps.kind = PointerKind::Pen;
            ps.pointer_id = static_cast<uint32_t>(event.ptouch.which);
            ps.pen_eraser = event.ptouch.eraser || ((event.ptouch.pen_state & SDL_PEN_INPUT_ERASER_TIP) != 0u);
            if (!down) {
                ps.pressure = 0.0f;
            } else if (ps.pressure <= 0.0f) {
                ps.pressure = 1.0f;
            }
            ctx->active_ui->event_pointer_pos = window_to_logical_pos_for(
                ctx, SDL_GetWindowFromID(event.ptouch.windowID), event.ptouch.x, event.ptouch.y);
            ctx->active_ui->has_event_pointer = true;
            ctx->active_ui->event_pointer_down = down;
            if (down) {
                ctx->active_ui->event_left_pressed_edge = true;
                ctx->active_ui->last_mouse_clicks = 1u;
                ctx->active_ui->caret_blink_on = true;
                ctx->active_ui->caret_blink_cycles = 0u;
                ctx->active_ui->caret_blink_tick = SDL_GetTicks();
            } else {
                ctx->active_ui->event_left_released_edge = true;
            }
            request_redraw(ctx);
            break;
        }
        case SDL_EVENT_PEN_MOTION: {
            PointerState& ps = ctx->active_ui->pointer_state;
            ps.kind = PointerKind::Pen;
            ps.pointer_id = static_cast<uint32_t>(event.pmotion.which);
            ps.pen_eraser = (event.pmotion.pen_state & SDL_PEN_INPUT_ERASER_TIP) != 0u;
            ctx->active_ui->event_pointer_pos = window_to_logical_pos_for(
                ctx, SDL_GetWindowFromID(event.pmotion.windowID), event.pmotion.x, event.pmotion.y);
            ctx->active_ui->has_event_pointer = true;
            request_redraw(ctx);
            break;
        }
        case SDL_EVENT_PEN_AXIS: {
            PointerState& ps = ctx->active_ui->pointer_state;
            ps.kind = PointerKind::Pen;
            ps.pointer_id = static_cast<uint32_t>(event.paxis.which);
            ps.pen_eraser = (event.paxis.pen_state & SDL_PEN_INPUT_ERASER_TIP) != 0u;
            if (std::isfinite(event.paxis.value)) {
                switch (event.paxis.axis) {
                    case SDL_PEN_AXIS_PRESSURE:
                        ps.pressure = clamp_range(event.paxis.value, 0.0f, 1.0f);
                        break;
                    case SDL_PEN_AXIS_XTILT:
                        ps.tilt_x = clamp_range(event.paxis.value, -90.0f, 90.0f);
                        break;
                    case SDL_PEN_AXIS_YTILT:
                        ps.tilt_y = clamp_range(event.paxis.value, -90.0f, 90.0f);
                        break;
                    default:
                        break;
                }
            }
            if (std::isfinite(event.paxis.x) && std::isfinite(event.paxis.y) &&
                (event.paxis.x != 0.0f || event.paxis.y != 0.0f))
            {
                ctx->active_ui->event_pointer_pos = window_to_logical_pos_for(
                    ctx, SDL_GetWindowFromID(event.paxis.windowID), event.paxis.x, event.paxis.y);
                ctx->active_ui->has_event_pointer = true;
            }
            request_redraw(ctx);
            break;
        }
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            const uint64_t fid = static_cast<uint64_t>(event.tfinger.fingerID);
            const bool is_down = (event.type == SDL_EVENT_FINGER_DOWN);
            const bool is_canceled = (event.type == SDL_EVENT_FINGER_CANCELED);
            const bool is_up = (event.type == SDL_EVENT_FINGER_UP || is_canceled);
            const bool was_primary = (ctx->active_ui->active_touch_count == 0u) ||
                                     (ctx->active_ui->active_touch_ids[0] == fid);
            const float dpi = (ctx->active_ui->dpi_scale > 0.0f) ? ctx->active_ui->dpi_scale : 1.0f;
            const float lw = static_cast<float>(ctx->active_ui->viewport_w) / dpi;
            const float lh = static_cast<float>(ctx->active_ui->viewport_h) / dpi;
            const bool valid_xy = std::isfinite(event.tfinger.x) && std::isfinite(event.tfinger.y);
            const Vec2 logical_pos = valid_xy ? Vec2{ event.tfinger.x * lw, event.tfinger.y * lh } : Vec2{};
            const float touch_pressure = (!is_up && std::isfinite(event.tfinger.pressure))
                ? clamp_range(event.tfinger.pressure, 0.0f, 1.0f)
                : 0.0f;
            if (is_up) {
                touch_track_remove(ctx->active_ui, fid);
            } else {
                touch_track_upsert(ctx->active_ui, fid, logical_pos, touch_pressure);
            }
            if (is_canceled) {
                ctx->active_ui->gesture_phase = PointerGesturePhase::Cancel;
            }
            PointerState& ps = ctx->active_ui->pointer_state;
            ps.kind = PointerKind::Touch;
            ps.pointer_id = static_cast<uint32_t>(fid);
            ps.active_touches = ctx->active_ui->active_touch_count;
            ps.pressure = touch_pressure;
            ps.tilt_x = 0.0f;
            ps.tilt_y = 0.0f;
            ps.pen_eraser = false;
            if (was_primary && valid_xy) {
                ctx->active_ui->event_pointer_pos = logical_pos;
                ctx->active_ui->has_event_pointer = true;
                if (is_down) {
                    ctx->active_ui->event_pointer_down = true;
                    ctx->active_ui->event_left_pressed_edge = true;
                    ctx->active_ui->last_mouse_clicks = 1u;
                    ctx->active_ui->caret_blink_on = true;
                    ctx->active_ui->caret_blink_cycles = 0u;
                    ctx->active_ui->caret_blink_tick = SDL_GetTicks();
                } else if (is_up) {
                    ctx->active_ui->event_pointer_down = false;
                    ctx->active_ui->event_left_released_edge = true;
                }
            }
            request_redraw(ctx);
            break;
        }
        case SDL_EVENT_PINCH_BEGIN:
            ctx->active_ui->gesture_phase = PointerGesturePhase::Begin;
            request_redraw(ctx);
            break;
        case SDL_EVENT_PINCH_UPDATE:
            ctx->active_ui->gesture_phase = PointerGesturePhase::Update;
            if (std::isfinite(event.pinch.scale)) {
                ctx->active_ui->pointer_state.pinch_delta += (event.pinch.scale - 1.0f);
            }
            request_redraw(ctx);
            break;
        case SDL_EVENT_PINCH_END:
            ctx->active_ui->gesture_phase = PointerGesturePhase::End;
            request_redraw(ctx);
            break;
        case SDL_EVENT_TEXT_EDITING: {
            const char* src = event.edit.text ? event.edit.text : "";
            const uint32_t raw_len = static_cast<uint32_t>(std::strlen(src));
            const uint32_t max_copy = (raw_len < (kMaxImeCompBytes - 1u)) ? raw_len : (kMaxImeCompBytes - 1u);
            const uint32_t clamped = utf8_clamp_boundary(src, raw_len, max_copy);
            if (clamped > 0u) {
                std::memcpy(ctx->active_ui->ime_comp_utf8, src, clamped);
            }
            ctx->active_ui->ime_comp_utf8[clamped] = '\0';
            ctx->active_ui->ime_comp_len = clamped;
            ctx->active_ui->ime_comp_cursor_byte = (event.edit.start < 0)
                ? clamped
                : utf8_codepoint_to_byte_offset(ctx->active_ui->ime_comp_utf8, clamped, event.edit.start);
            ctx->active_ui->ime_comp_sel_len_byte = (event.edit.length <= 0)
                ? 0u
                : utf8_codepoint_to_byte_offset(
                    ctx->active_ui->ime_comp_utf8 + ctx->active_ui->ime_comp_cursor_byte,
                    clamped - ctx->active_ui->ime_comp_cursor_byte,
                    event.edit.length);
            ctx->active_ui->caret_blink_on = true;
            ctx->active_ui->caret_blink_cycles = 0u;
            ctx->active_ui->caret_blink_tick = SDL_GetTicks();
            ctx->active_ui->pending_ime_comp_edited = true;
            request_redraw(ctx);
            break;
        }
        case SDL_EVENT_TEXT_INPUT: {
            clear_ime_composition(ctx);
            if (event.text.text && event.text.text[0] != '\0') {
                push_pending_text_op(
                    ctx,
                    PendingTextOp{ .kind = TextEditOpKind::InsertText },
                    str_cstr(event.text.text));
            }
            ctx->active_ui->caret_blink_on = true;
            ctx->active_ui->caret_blink_cycles = 0u;
            ctx->active_ui->caret_blink_tick = SDL_GetTicks();
            request_redraw(ctx);
            break;
        }
        case SDL_EVENT_KEY_DOWN:
            ctx->active_ui->event_key_mods = sdl_mod_to_etch(event.key.mod);
            if (event.key.key == SDLK_F12 && !event.key.repeat) {
                ctx->active_ui->debug_hud_open = !ctx->active_ui->debug_hud_open;
                request_redraw(ctx);
                break;
            }
            if (event.key.key == SDLK_F11 && !event.key.repeat && ctx->active_ui->debug_hud_open) {
                ctx->active_ui->debug_wireframe_open = !ctx->active_ui->debug_wireframe_open;
                request_redraw(ctx);
                break;
            }
            if (event.key.key == SDLK_F10 && !event.key.repeat && ctx->active_ui->debug_hud_open) {
                set_time_scale(ctx, ctx->time_scale == 1.f ? .2f : 1.f);
                break;
            }
            if (event.key.key == SDLK_SPACE) {
                ctx->active_ui->space_down = true;
            }
            if (event.key.key == SDLK_ESCAPE && !event.key.repeat) {
                if (ctx->active_ui->drag_active) {
                    ctx->active_ui->drag_active = false;
                    ctx->active_ui->drag_drop_pending = false;
                    ctx->active_ui->drag_accepted_this_frame = false;
                }
                if (ctx->active_ui->marquee_canvas_id != 0u) {
                    if (WidgetState* mws = state_map_find(&ctx->active_ui->states, ctx->active_ui->marquee_canvas_id)) {
                        if (mws->cold) mws->cold->canvas_marquee_active = false;
                    }
                    ctx->active_ui->marquee_canvas_id = 0u;
                }
            }
            if (ctx->active_ui->pending_key_count < kMaxKeyEventsPerFrame) {
                ctx->active_ui->pending_keys[ctx->active_ui->pending_key_count++] = KeyEventEntry{
                    .key = static_cast<uint32_t>(event.key.key),
                    .mods = ctx->active_ui->event_key_mods,
                    .repeat = event.key.repeat,
                };
            }
            ctx->active_ui->caret_blink_on = true;
            ctx->active_ui->caret_blink_cycles = 0u;
            ctx->active_ui->caret_blink_tick = SDL_GetTicks();
            if (ctx->active_ui->ime_comp_len > 0u) {
                if (event.key.key == SDLK_ESCAPE) {
                    clear_ime_composition(ctx);
                    ctx->active_ui->pending_escape_consumed_by_ime = true;
                    if (!ctx->headless && ctx->active_window) {
                        SDL_ClearComposition(ctx->active_window);
                    }
                }
            } else {
                const WidgetState* tab_ws=state_map_find(&ctx->active_ui->states,ctx->active_ui->focus_id);
                const bool tab_shift = (event.key.mod & SDL_KMOD_SHIFT) != 0u;
                const bool tab_has_buf = tab_ws && tab_ws->cold && tab_ws->cold->edit_text_buffer != nullptr;
                if (event.key.key == SDLK_TAB && !(tab_ws && (tab_has_buf || tab_ws->suppress_popup_keys || (tab_ws->accept_ghost_on_tab && !tab_shift)))) {
                    ctx->active_ui->pending_tab_delta += tab_shift ? -1 : 1;
                } else {
                    push_pending_text_op(
                        ctx,
                        PendingTextOp{
                            .kind = TextEditOpKind::KeyPress,
                            .mod = event.key.mod,
                            .key = event.key.key,
                        },
                        {});
                }
            }
            request_redraw(ctx);
            break;
        case SDL_EVENT_KEY_UP:
            ctx->active_ui->event_key_mods = sdl_mod_to_etch(event.key.mod);
            if (event.key.key == SDLK_SPACE) {
                ctx->active_ui->space_down = false;
            }
            request_redraw(ctx);
            break;
        case SDL_EVENT_DROP_BEGIN:
        case SDL_EVENT_DROP_POSITION:
        case SDL_EVENT_DROP_COMPLETE: {
            const Vec2 pos = window_to_logical_pos_for(ctx, ctx->active_window, event.drop.x, event.drop.y);
            if (event.type == SDL_EVENT_DROP_BEGIN) {
                ctx->active_ui->pending_drop_count = 0;
                ctx->active_ui->pending_text_drop_count = 0;
                ctx->active_ui->pending_drop_bytes = 0;
                ctx->active_ui->has_delivered_drop_pos = false;
            }
            ctx->active_ui->os_drop_pos = pos;
            ctx->active_ui->has_os_drop_pos = true;
            ctx->active_ui->os_dragging = event.type != SDL_EVENT_DROP_COMPLETE;
            if (event.type == SDL_EVENT_DROP_COMPLETE) {
                ctx->active_ui->delivered_drop_pos = pos;
                ctx->active_ui->has_delivered_drop_pos = true;
            }
            request_redraw(ctx);
            break;
        }
        case SDL_EVENT_DROP_FILE:
            if (event.drop.data) inject_drop_file(ctx, str_cstr(event.drop.data), ctx->active_window);
            break;
        case SDL_EVENT_DROP_TEXT:
            if (event.drop.data) inject_drop_text(ctx, str_cstr(event.drop.data), ctx->active_window);
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            if (!ctx->active_ui->mouse_captured) {
                ctx->active_ui->event_pointer_pos = Vec2{-1.0f, -1.0f};
                ctx->active_ui->has_event_pointer = false;
            }
            request_redraw(ctx);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            ctx->active_ui->event_pointer_down = false;
            ctx->active_ui->event_middle_down = false;
            ctx->active_ui->event_right_down = false;
            ctx->active_ui->event_key_mods = 0u;
            ctx->active_ui->right_active_id = 0u;
            ctx->active_ui->right_drag_exceeded = false;
            ctx->active_ui->pan_canvas_id = 0u;
            if (ctx->active_ui->marquee_canvas_id != 0u) {
                if (WidgetState* mws = state_map_find(&ctx->active_ui->states, ctx->active_ui->marquee_canvas_id)) {
                    if (mws->cold) mws->cold->canvas_marquee_active = false;
                }
                ctx->active_ui->marquee_canvas_id = 0u;
            }
            ctx->active_ui->drag_active = false;
            ctx->active_ui->drag_drop_pending = false;
            ctx->active_ui->drag_accepted_this_frame = false;
            ctx->active_ui->space_down = false;
            ctx->active_ui->active_id = 0u;
            if (ctx->active_ui->active_touch_count > 0u ||
                ctx->active_ui->gesture_phase == PointerGesturePhase::Begin ||
                ctx->active_ui->gesture_phase == PointerGesturePhase::Update)
            {
                ctx->active_ui->gesture_phase = PointerGesturePhase::Cancel;
            }
            ctx->active_ui->active_touch_count = 0u;
            ctx->active_ui->pointer_state.active_touches = 0u;
            ctx->active_ui->pointer_state.pressure = 0.0f;
            clear_ime_composition(ctx);
            if (ctx->active_ui->mouse_captured) {
                if (ctx->mouse_capture_window == ctx->active_window) { SDL_CaptureMouse(false); ctx->mouse_capture_window = nullptr; }
                ctx->active_ui->mouse_captured = false;
            }
            request_redraw(ctx);
            break;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_RESTORED: {
            if (ctx->active_window && !ctx->headless) {
                sync_window_metrics_from_os(ctx->active_ui, ctx->active_window);
            } else if ((event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                        event.type == SDL_EVENT_WINDOW_METAL_VIEW_RESIZED) &&
                       event.window.data1 > 0 && event.window.data2 > 0)
            {
                ctx->active_ui->viewport_w = static_cast<uint32_t>(event.window.data1);
                ctx->active_ui->viewport_h = static_cast<uint32_t>(event.window.data2);
            }
            if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
                event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
                event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                event.type == SDL_EVENT_WINDOW_METAL_VIEW_RESIZED)
            {
                if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
                    ctx->atlas.hinted_gc_pending = true;
                }
                ctx->active_ui->applied_ime_rect = {-1, -1, 0, 0};
                ctx->active_ui->applied_ime_cursor = -1;
            }
            if (ctx->active_ui->redraw_budget < 2u) {
                ctx->active_ui->redraw_budget = 2u;
            }
            request_redraw(ctx);
            break;
        }
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            request_redraw(ctx);
            break;
        default:
            break;
    }
}

static void begin_bound_frame(Context* ctx, const FrameDesc& desc) noexcept {
    if (!ctx || ctx->in_frame || !ctx->active_ui) {
        return;
    }
    if (!ctx->active_ui->layout) {
        const float raw_dpi = (desc.dpi_scale > 0.0f) ? desc.dpi_scale : 1.0f;
        const float dpi = raw_dpi * (ctx->ui_zoom > 0.0f ? ctx->ui_zoom : 1.0f);
        const float init_w = desc.viewport_w ? (static_cast<float>(desc.viewport_w) / dpi) : 1280.0f;
        const float init_h = desc.viewport_h ? (static_cast<float>(desc.viewport_h) / dpi) : 720.0f;
        if (!ensure_window_ui_clay(ctx, ctx->active_ui, init_w, init_h)) {
            return;
        }
    }
    set_current_context(ctx);
    ctx->in_frame = true;
    ctx->theme_depth = 0u;
    ctx->theme_overflow = 0u;
    ctx->active_ui->bloom_intensity = std::isfinite(desc.bloom_intensity) ? clamp_range(desc.bloom_intensity,0,8) : 0;
    ctx->active_ui->bloom_threshold = std::isfinite(desc.bloom_threshold) ? clamp_range(desc.bloom_threshold,0,1) : .7f;
    ctx->draw_list_window = ctx->active_window;
    ctx->active_ui->current_layer=nullptr;
    ctx->active_ui->layout->culling_enabled = true;
    ctx->active_ui->subtree_depth = 0u;
    ctx->active_ui->cached_subtree_hits = 0u;
    ctx->active_ui->last_released_drag_dist_sq = 0.0f;
    ctx->active_ui->box_build_stack=nullptr;
    ctx->active_render_layer=nullptr;
    ctx->active_ui->focus_scope_depth = 0;
    ctx->active_ui->popup_depth = 0;
    ctx->active_ui->tab_bar_depth = 0;
    ctx->active_ui->modal_depth = 0;
    ctx->active_ui->menu_bar_id = 0;
    ctx->active_ui->focus_scope = 0;
    ctx->layout_start_ticks = SDL_GetPerformanceCounter();
    ++ctx->active_ui->frame_index;
    const float ts = std::isfinite(ctx->time_scale)
        ? ((ctx->time_scale > 0.0f) ? ctx->time_scale : 0.0f)
        : 1.0f;
    const float raw_dt = (std::isfinite(desc.dt_seconds) && desc.dt_seconds > 0.0f)
        ? desc.dt_seconds
        : 0.0f;
    ctx->active_ui->elapsed_seconds += raw_dt * ts;

    if (ctx->active_ui->redraw_budget > 0u) {
        --ctx->active_ui->redraw_budget;
    }
    ctx->active_ui->sim_dt = (raw_dt > 0.1f) ? (1.0f / 60.0f) : raw_dt;
    ctx->active_ui->sim_dt *= ts;
    ctx->active_ui->active_anim_count = 0u;
    ctx->active_ui->desired_cursor = Cursor::Arrow;

    ctx->active_ui->prev_ime_focused_id = ctx->active_ui->ime_focused_id;
    ctx->active_ui->ime_focused_id = 0u;
    ctx->active_ui->ime_focused_password = false;
    auto& input_ui = *ctx->active_ui;
    if (input_ui.pending_text_op_count) {
        std::swap(input_ui.frame_text_ops, input_ui.pending_text_ops);
        std::swap(input_ui.frame_text_op_capacity, input_ui.pending_text_op_capacity);
    }
    if (input_ui.pending_text_utf8_len) {
        std::swap(input_ui.frame_text_utf8, input_ui.pending_text_utf8);
        std::swap(input_ui.frame_text_utf8_capacity, input_ui.pending_text_utf8_capacity);
    }
    input_ui.frame_text_op_count = input_ui.pending_text_op_count;
    input_ui.frame_text_utf8_len = input_ui.pending_text_utf8_len;
    input_ui.pending_text_op_count = 0u;
    input_ui.pending_text_utf8_len = 0u;

    uint16_t kmods = static_cast<uint16_t>(desc.key_mods | ctx->active_ui->event_key_mods);
    ctx->active_ui->frame_key_mods = kmods;
    ctx->active_ui->frame_escape_consumed_by_ime = ctx->active_ui->pending_escape_consumed_by_ime;
    ctx->active_ui->pending_escape_consumed_by_ime = false;
    ctx->active_ui->frame_ime_comp_edited = ctx->active_ui->pending_ime_comp_edited;
    ctx->active_ui->pending_ime_comp_edited = false;

    ctx->active_ui->frame_key_count = ctx->active_ui->pending_key_count;
    if (ctx->active_ui->frame_key_count > 0u) {
        std::memcpy(ctx->active_ui->frame_keys, ctx->active_ui->pending_keys, size_t(ctx->active_ui->frame_key_count) * sizeof(KeyEventEntry));
    }
    ctx->active_ui->pending_key_count = 0u;

    if (desc.key_pressed_code == SDLK_TAB || desc.key_pressed_code == SDLK_LEFT || desc.key_pressed_code == SDLK_RIGHT ||
        desc.key_pressed_code == SDLK_UP || desc.key_pressed_code == SDLK_DOWN) ctx->active_ui->keyboard_focus_active = true;
    if (desc.key_pressed_code != 0u) {
        if (ctx->active_ui->frame_key_count < kMaxKeyEventsPerFrame) {
            ctx->active_ui->frame_keys[ctx->active_ui->frame_key_count++] = KeyEventEntry{
                .key = desc.key_pressed_code,
                .mods = ctx->active_ui->frame_key_mods,
                .repeat = false,
            };
        }
        if (ctx->active_ui->ime_comp_len == 0u) {
            const WidgetState* tab_ws=state_map_find(&ctx->active_ui->states,ctx->active_ui->focus_id);
            const bool tab_shift = (ctx->active_ui->frame_key_mods & Mod_Shift) != 0u;
            const bool tab_has_buf = tab_ws && tab_ws->cold && tab_ws->cold->edit_text_buffer != nullptr;
            if (desc.key_pressed_code == SDLK_TAB && !(tab_ws && (tab_has_buf || tab_ws->suppress_popup_keys || (tab_ws->accept_ghost_on_tab && !tab_shift)))) {
                ctx->active_ui->pending_tab_delta += tab_shift ? -1 : 1;
            } else if (reserve_text_ops(input_ui.frame_text_ops, input_ui.frame_text_op_capacity,
                                        input_ui.frame_text_op_count)) {
                ctx->active_ui->frame_text_ops[ctx->active_ui->frame_text_op_count++] = PendingTextOp{
                    .kind = TextEditOpKind::KeyPress,
                    .mod = etch_mod_to_sdl(ctx->active_ui->frame_key_mods),
                    .key = static_cast<SDL_Keycode>(desc.key_pressed_code),
                };
            }
        }
    }

    ctx->active_ui->prev_focus_count = ctx->active_ui->focus_count;
    if (ctx->active_ui->prev_focus_count > 0u) {
        std::memcpy(ctx->active_ui->prev_focus_order, ctx->active_ui->focus_order, size_t(ctx->active_ui->prev_focus_count) * sizeof(uint32_t));
    }
    ctx->active_ui->focus_count = 0u;

    if (ctx->active_ui->modal_scope) {
        uint32_t count = 0;
        for (uint32_t i = 0; i < ctx->active_ui->prev_focus_count; ++i) {
            const WidgetState* ws = state_map_find(&ctx->active_ui->states, ctx->active_ui->prev_focus_order[i]);
            if (ws && ws->focus_scope == ctx->active_ui->modal_scope)
                ctx->active_ui->prev_focus_order[count++] = ws->key;
        }
        ctx->active_ui->prev_focus_count = count;
        if (!count) ctx->active_ui->pending_tab_delta = 0;
    }
    if (ctx->active_ui->pending_tab_delta != 0 && ctx->active_ui->prev_focus_count > 0u) {
        int32_t cur_idx = -1;
        for (uint32_t i = 0u; i < ctx->active_ui->prev_focus_count; ++i) {
            if (ctx->active_ui->prev_focus_order[i] == ctx->active_ui->focus_id) {
                cur_idx = static_cast<int32_t>(i);
                break;
            }
        }
        const int32_t n = static_cast<int32_t>(ctx->active_ui->prev_focus_count);
        int32_t next_idx = (cur_idx < 0)
            ? ((ctx->active_ui->pending_tab_delta > 0) ? 0 : (n - 1))
            : ((cur_idx + ctx->active_ui->pending_tab_delta) % n);
        if (next_idx < 0) {
            next_idx += n;
        }
        const uint32_t next_focus = ctx->active_ui->prev_focus_order[next_idx];
        if (next_focus != ctx->active_ui->focus_id) {
            clear_ime_composition(ctx);
        }
        ctx->active_ui->focus_id = next_focus;
        ctx->active_ui->pending_tab_delta = 0;
        ctx->active_ui->keyboard_focus_active = true;
        ctx->active_ui->caret_blink_on = true;
        ctx->active_ui->caret_blink_cycles = 0u;
        ctx->active_ui->caret_blink_tick = SDL_GetTicks();
        request_redraw(ctx);
    }

    constexpr size_t kMaxAutoFrameArenaBytes = 32u * 1024u * 1024u;
    if (ctx->frame_arena.capacity > 0u &&
        ctx->frame_arena.capacity < kMaxAutoFrameArenaBytes &&
        ctx->frame_arena.peak_offset >= (ctx->frame_arena.capacity * 7u) / 8u)
    {
        size_t next_cap = ctx->frame_arena.capacity * 2u;
        if (next_cap > kMaxAutoFrameArenaBytes) next_cap = kMaxAutoFrameArenaBytes;
        Arena grown = arena_create(next_cap);
        if (grown.base) {
            arena_destroy(&ctx->frame_arena);
            ctx->frame_arena = grown;
        }
    }
    ctx->frame_arena.reset();
    ctx->active_ui->frame_drop_count = 0;
    ctx->active_ui->frame_text_drop_count = 0;
    if (!ctx->active_ui->os_dragging &&
        (ctx->active_ui->pending_drop_count > 0 || ctx->active_ui->pending_text_drop_count > 0)) {
        ctx->active_ui->frame_drop_count = ctx->active_ui->pending_drop_count;
        ctx->active_ui->frame_drops=ctx->frame_arena.alloc<Str>(ctx->active_ui->pending_drop_count);
        ctx->active_ui->frame_text_drops=ctx->frame_arena.alloc<Str>(ctx->active_ui->pending_text_drop_count);
        ctx->active_ui->frame_drop_buf=ctx->frame_arena.alloc<char>(ctx->active_ui->pending_drop_bytes);
        if(!ctx->active_ui->frame_drop_buf || (ctx->active_ui->pending_drop_count && !ctx->active_ui->frame_drops) ||
            (ctx->active_ui->pending_text_drop_count && !ctx->active_ui->frame_text_drops)) {
            ctx->active_ui->frame_drop_count=0;
            ctx->active_ui->pending_drop_count=ctx->active_ui->pending_text_drop_count=0;
            ctx->active_ui->pending_drop_bytes=0;
        }
        if (ctx->active_ui->pending_drop_bytes > 0u) {
            std::memcpy(ctx->active_ui->frame_drop_buf, ctx->active_ui->pending_drop_buf, ctx->active_ui->pending_drop_bytes);
            for (uint32_t i = 0u; i < ctx->active_ui->frame_drop_count; ++i) {
                ctx->active_ui->frame_drops[i] = Str{
                    ctx->active_ui->frame_drop_buf + ctx->active_ui->pending_drop_records[i].offset,
                    ctx->active_ui->pending_drop_records[i].length,
                };
            }
        }
        ctx->active_ui->frame_text_drop_count = ctx->active_ui->pending_text_drop_count;
        for (uint32_t i = 0; i < ctx->active_ui->frame_text_drop_count; ++i)
            ctx->active_ui->frame_text_drops[i] = Str{ctx->active_ui->frame_drop_buf + ctx->active_ui->pending_text_drop_records[i].offset, ctx->active_ui->pending_text_drop_records[i].length};
        ctx->active_ui->pending_text_drop_count = 0;
        ctx->active_ui->pending_drop_count = 0u;
        ctx->active_ui->pending_drop_bytes = 0u;
    }


    a11y_begin(ctx);
    ctx->active_ui->active_vlists = nullptr;
    ctx->active_ui->active_vlist_count = 0;
    ++ctx->arena_epoch;
    shape_cache_begin_frame(ctx);
    ctx->clip_shape_count = 1u;
    ctx->grad_stop_count = 1u;
    ctx->render_clip_index = 0u;
    ctx->render_clip_shape = 0u;
    ctx->dyn_slug_curve_count = 0;
    ctx->dyn_slug_band_count = 0;
    ctx->prim_count = 0u;
    ctx->batch_count = 0u;
    ctx->blur_split_count = 0u;
    ctx->blur_splits_need_load_preserve = false;
    ctx->filter_prim_count = 0u;
    ctx->filter_batch_count = 0u;
    ctx->filter_group_count = 0u;
    maybe_gc_hinted_atlas(ctx);
    ctx->active_filter_group = nullptr;
    ctx->layer_has_bg_prims = true;
    ctx->blur_dirty_valid = false;
    ctx->blur_dirty_grid[0] = ctx->blur_dirty_grid[1] = ctx->blur_dirty_grid[2] = ctx->blur_dirty_grid[3] = 0ULL;
    ctx->gpu_batch_count = 0u;
    ctx->gpu_blur_split_count = 0u;
    ctx->gpu_use_early_z = false;
    ctx->active_ui->split_depth = 0u;
    ctx->active_ui->canvas_depth = 0u;
    ctx->active_ui->last_canvas_clip_px = {};
    ctx->active_ui->last_canvas_clip_radius_px = 0.0f;
    ctx->active_ui->pending_chrome_drag_count = 0u;
    ctx->active_ui->pending_chrome_client_count = 0u;
    if (ctx->active_ui->tooltip_frame_winner == 0u) {
        ctx->active_ui->tooltip_target = 0u;
        ctx->active_ui->tooltip_wake_tick = 0u;
    }
    ctx->active_ui->tooltip_prev_winner = ctx->active_ui->tooltip_frame_winner;
    ctx->active_ui->tooltip_frame_winner = 0u;
    ctx->active_ui->tooltip_frame_rank = 0u;
    ctx->active_ui->prev_top_modal_id = ctx->active_ui->top_modal_id;
    ctx->active_ui->top_modal_id = 0u;
    ctx->active_ui->id_stack_depth = 0u;
    ctx->active_ui->id_stack_overflow = 0u;
    ctx->active_ui->sheet_depth = 0u;
    ctx->active_ui->drag_drop_pending = false;
    ctx->active_ui->drag_accepted_this_frame = false;
    if (ctx->active_ui->marquee_finished_canvas_id != 0u) {
        if (WidgetState* mws = state_map_find(&ctx->active_ui->states, ctx->active_ui->marquee_finished_canvas_id)) {
            if (mws->cold) {
                mws->cold->canvas_marquee_finished = false;
                mws->cold->canvas_marquee_world = {};
            }
        }
        ctx->active_ui->marquee_finished_canvas_id = 0u;
    }

    const float raw_dpi = (desc.dpi_scale > 0.0f) ? desc.dpi_scale : 1.0f;
    const float dpi = raw_dpi * (ctx->ui_zoom > 0.0f ? ctx->ui_zoom : 1.0f);
    ctx->active_ui->viewport_w = (desc.viewport_w > 0u) ? desc.viewport_w : 1u;
    ctx->active_ui->viewport_h = (desc.viewport_h > 0u) ? desc.viewport_h : 1u;
    ctx->active_ui->dpi_scale = dpi;
    ctx->active_ui->pixel_snap = desc.pixel_snap;
    if (std::fabs(dpi - ctx->active_ui->applied_dpi_scale) > 1e-4f) {
        ctx->active_ui->applied_dpi_scale = dpi;
        if (ctx->active_ui->frame_index > 1u && ctx->atlas.hinted_count > 0u) {
            reset_hinted_atlas_cache(&ctx->atlas);
        }
        ctx->active_ui->applied_ime_rect = {-1, -1, 0, 0};
        ctx->active_ui->applied_ime_cursor = -1;
        if (ctx->active_ui->redraw_budget < 1u) {
            ctx->active_ui->redraw_budget = 1u;
        }
    }

    const float logical_w = static_cast<float>(ctx->active_ui->viewport_w) / dpi;
    const float logical_h = static_cast<float>(ctx->active_ui->viewport_h) / dpi;
    layout_engine_set_viewport(ctx->active_ui->layout, logical_w, logical_h);

    ctx->active_ui->had_pointer = ctx->active_ui->has_pointer;
    ctx->active_ui->prev_pointer_pos = ctx->active_ui->pointer_pos;
    Vec2 pointer_pos = desc.pointer_pos;
    if ((!std::isfinite(pointer_pos.x) || !std::isfinite(pointer_pos.y)) && ctx->active_ui->has_event_pointer) {
        pointer_pos = ctx->active_ui->event_pointer_pos;
    }
    ctx->active_ui->has_pointer = std::isfinite(pointer_pos.x) && std::isfinite(pointer_pos.y);
    ctx->active_ui->pointer_pos = pointer_pos;
    if (ctx->active_ui->had_pointer && ctx->active_ui->has_pointer)
    {
        ctx->active_ui->pointer_delta = Vec2{ pointer_pos.x - ctx->active_ui->prev_pointer_pos.x, pointer_pos.y - ctx->active_ui->prev_pointer_pos.y };
    } else {
        ctx->active_ui->pointer_delta = Vec2{ 0.0f, 0.0f };
    }

    const bool had_left_press = ctx->active_ui->event_left_pressed_edge;
    const bool had_left_release = ctx->active_ui->event_left_released_edge;
    ctx->active_ui->event_left_pressed_edge = false;
    ctx->active_ui->event_left_released_edge = false;
    const bool pointer_down = desc.pointer_down || ctx->active_ui->event_pointer_down;
    const bool pressed_this_frame = (pointer_down && !ctx->active_ui->prev_pointer_down) || (!ctx->active_ui->prev_pointer_down && had_left_press);
    const bool released_this_frame = (!pointer_down && ctx->active_ui->prev_pointer_down) || (had_left_press && had_left_release && !pointer_down);
    ctx->active_ui->prev_pointer_down = pointer_down;
    ctx->active_ui->left_pressed_this_frame = pressed_this_frame;
    if (pressed_this_frame) ctx->active_ui->keyboard_focus_active = false;
    if (pressed_this_frame) {
        if (desc.pointer_clicks > 0u) {
            ctx->active_ui->last_mouse_clicks = desc.pointer_clicks;
        } else if (!ctx->active_ui->event_pointer_down && !had_left_press) {
            ctx->active_ui->last_mouse_clicks = 1u;
        }
    }

    const bool middle_down = ctx->active_ui->event_middle_down;
    const bool middle_pressed_this_frame = middle_down && !ctx->active_ui->prev_pan_button_down;
    ctx->active_ui->prev_pan_button_down = middle_down;
    ctx->active_ui->middle_pressed_this_frame = middle_pressed_this_frame;
    ctx->active_ui->command_scope_depth = 0u;

    const bool had_right_press = ctx->active_ui->event_right_pressed_edge;
    const bool had_right_release = ctx->active_ui->event_right_released_edge;
    ctx->active_ui->event_right_pressed_edge = false;
    ctx->active_ui->event_right_released_edge = false;
    const bool right_down = desc.right_pointer_down || ctx->active_ui->event_right_down;
    const bool right_pressed_this_frame = (right_down && !ctx->active_ui->prev_right_down) || (!ctx->active_ui->prev_right_down && had_right_press);
    const bool right_released_this_frame = (!right_down && ctx->active_ui->prev_right_down) || (had_right_press && had_right_release && !right_down);
    ctx->active_ui->prev_right_down = right_down;
    ctx->active_ui->right_pressed_this_frame = right_pressed_this_frame;
    if (right_pressed_this_frame || middle_pressed_this_frame) ctx->active_ui->keyboard_focus_active = false;
    ctx->active_ui->right_clicked_any_this_frame = false;

    const Vec2 scroll = (desc.scroll_delta.x != 0.0f || desc.scroll_delta.y != 0.0f)
        ? desc.scroll_delta
        : ctx->active_ui->event_scroll_delta;
    ctx->active_ui->frame_scroll_delta = scroll;
    ctx->active_ui->event_scroll_delta = {};

    // Run inverse-transformed true occlusion hit testing against the previous frame's hitbox tree
    // BEFORE layout_begin() resets the current-frame tree.
    hitbox_run_hit_test(
        ctx->active_ui->hitboxes.data,
        ctx->active_ui->hitboxes.count,
        pointer_pos,
        ctx->active_ui->has_pointer,
        &ctx->active_ui->pointer_over_ids,
        &ctx->active_ui->topmost_hovered_id,
        &ctx->active_ui->hovered_span_id);

    state_map_clear_flags(&ctx->active_ui->states, true, false);
    const std::span<const uint32_t> over{
        ctx->active_ui->pointer_over_ids.data,
        ctx->active_ui->pointer_over_ids.count,
    };
    const uint32_t hovered_span_id = hit_test_span_regions(ctx, pointer_pos);
    if (hovered_span_id != 0u) {
        if (WidgetState* sws = state_map_find(&ctx->active_ui->states, hovered_span_id)) {
            sws->span_hovered = true;
        }
    }
    bool pan_started_this_frame = false;

    if (middle_pressed_this_frame) {
        if (WidgetState* cws = resolve_canvas_owner(ctx, over)) {
            ctx->active_ui->pan_canvas_id = cws->key;
            pan_started_this_frame = true;
            request_redraw(ctx);
        }
    }

    if (right_pressed_this_frame) {
        ctx->active_ui->right_press_origin = pointer_pos;
        ctx->active_ui->right_drag_exceeded = false;
        ctx->active_ui->right_active_id = 0u;
        if (WidgetState* rws = find_topmost_user_element(ctx, over, hovered_span_id)) {
            ctx->active_ui->right_active_id = rws->key;
        }
    }
    if (right_down && !ctx->active_ui->right_drag_exceeded &&
        ctx->active_ui->has_pointer)
    {
        const float dx = pointer_pos.x - ctx->active_ui->right_press_origin.x;
        const float dy = pointer_pos.y - ctx->active_ui->right_press_origin.y;
        if (dx * dx + dy * dy > 16.0f) {
            ctx->active_ui->right_drag_exceeded = true;
            if (ctx->active_ui->pan_canvas_id == 0u) {
                if (WidgetState* cws = resolve_canvas_owner(ctx, over)) {
                    ctx->active_ui->pan_canvas_id = cws->key;
                    pan_started_this_frame = true;
                    request_redraw(ctx);
                }
            }
        }
    } else if (right_released_this_frame) {
        bool drag_exceeded = ctx->active_ui->right_drag_exceeded;
        if (!drag_exceeded && ctx->active_ui->has_pointer) {
            const float dx = pointer_pos.x - ctx->active_ui->right_press_origin.x;
            const float dy = pointer_pos.y - ctx->active_ui->right_press_origin.y;
            drag_exceeded = (dx * dx + dy * dy > 16.0f);
        }
        if (!drag_exceeded) {
            ctx->active_ui->right_clicked_any_this_frame = true;
            if (ctx->active_ui->right_active_id != 0u) {
                if (WidgetState* ws = state_map_find(&ctx->active_ui->states, ctx->active_ui->right_active_id)) {
                    const bool over_right = ws->is_text_span
                        ? ws->span_hovered
                        : pointer_over_id(ctx->active_ui, ctx->active_ui->right_active_id);
                    if (ws->is_user_element && over_right) {
                        ws->right_clicked_this_frame = true;
                    }
                }
            }
        }
        ctx->active_ui->right_active_id = 0u;
        ctx->active_ui->right_drag_exceeded = false;
    }

    if (pressed_this_frame) {
        ctx->active_ui->left_press_origin = pointer_pos;
        ctx->active_ui->left_max_drag_dist_sq = 0.0f;
        state_map_clear_flags(&ctx->active_ui->states, false, true);
        WidgetState* space_pan_canvas = (ctx->active_ui->space_down && ctx->active_ui->focus_id == 0u) ? resolve_canvas_owner(ctx, over) : nullptr;
        if (space_pan_canvas) {
            ctx->active_ui->pan_canvas_id = space_pan_canvas->key;
            pan_started_this_frame = true;
            request_redraw(ctx);
        } else {
            bool handled_scrollbar = false;
            for (int32_t i = static_cast<int32_t>(over.size()) - 1; i >= 0; --i) {
                WidgetState* ws = state_map_find(&ctx->active_ui->states, over[static_cast<size_t>(i)]);
                if (!ws || !ws->is_user_element) continue;
                for(uint32_t axis=0;axis<2;++axis) {
                    const bool horizontal=axis==1;
                    const ScrollbarGeom sb=horizontal ? compute_hscrollbar_geom(ws) : compute_vscrollbar_geom(ws);
                    if(!sb.visible || !sb.hit_track.contains(pointer_pos)) continue;
                    ctx->active_ui->active_id=id_index(ws->key,horizontal ? kScrollbarXTag : kScrollbarYTag);
                    ws->scroll_vel={};if (ws->cold) ws->cold->vscroll_vel_y=0;
                    const float pointer=horizontal ? pointer_pos.x : pointer_pos.y;
                    const float start=horizontal ? sb.thumb.x : sb.thumb.y,length=horizontal ? sb.thumb.w : sb.thumb.h;
                    ctx->active_ui->scrollbar_drag_grab_offset=clamp_range(pointer-start,0,length);
                    if(pointer<start || pointer>start+length) {
                        ctx->active_ui->scrollbar_drag_grab_offset=length*.5f;
                        apply_scrollbar_drag(ws,pointer,length*.5f,horizontal);
                    }
                    request_redraw(ctx);handled_scrollbar=true;break;
                }
                if(handled_scrollbar) break;
            }
            if (!handled_scrollbar) {
                uint32_t top_hit = 0u;
                uint32_t top_focus = 0u;
                if (hovered_span_id != 0u) {
                    if (WidgetState* sws = state_map_find(&ctx->active_ui->states, hovered_span_id)) {
                        if (sws->is_user_element) {
                            top_hit = hovered_span_id;
                            if (sws->is_focusable) top_focus = hovered_span_id;
                            sws->pressed_down = true;
                            sws->pressed_this_frame = true;
                            if (ctx->active_ui->last_mouse_clicks >= 2u) {
                                sws->double_clicked_this_frame = true;
                            }
                        }
                    }
                }
                for (int32_t i = static_cast<int32_t>(over.size()) - 1; i >= 0; --i) {
                    const uint32_t oid = over[static_cast<size_t>(i)];
                    WidgetState* ws = state_map_find(&ctx->active_ui->states, oid);
                    if (ws && ws->is_user_element) {
                        if (top_hit == 0u) {
                            top_hit = oid;
                            ws->pressed_down = true;
                            ws->pressed_this_frame = true;
                            if (ctx->active_ui->last_mouse_clicks >= 2u) {
                                ws->double_clicked_this_frame = true;
                            }
                            if (ws->is_canvas && ws->cold && ws->cold->canvas_interactive) {
                                WidgetCold* cc = ws->cold;
                                if (cc->canvas_marquee_enabled) {
                                    if (ctx->active_ui->marquee_canvas_id == 0u) {
                                        ctx->active_ui->marquee_canvas_id = oid;
                                        const Vec2 start_w = canvas_screen_to_world(ws, pointer_pos);
                                        cc->canvas_marquee_start_world = start_w;
                                        cc->canvas_marquee_world = Rect{ start_w.x, start_w.y, 0.0f, 0.0f };
                                        cc->canvas_marquee_active = true;
                                        cc->canvas_marquee_finished = false;
                                        request_redraw(ctx);
                                    }
                                } else if (ctx->active_ui->pan_canvas_id == 0u) {
                                    ctx->active_ui->pan_canvas_id = oid;
                                    pan_started_this_frame = true;
                                }
                            }
                        }
                        if (top_focus == 0u && ws->is_focusable) {
                            top_focus = oid;
                        }
                    }
                }
                const WidgetState* target_focus = state_map_find(&ctx->active_ui->states, top_focus);
                const bool accept_focus = !ctx->active_ui->modal_scope ||
                    (target_focus && target_focus->focus_scope == ctx->active_ui->modal_scope);
                if (accept_focus && top_focus != ctx->active_ui->focus_id) {
                    clear_ime_composition(ctx);
                }
                ctx->active_ui->active_id = top_hit;
                if (accept_focus) {
                    ctx->active_ui->focus_id = top_focus;
                }
            }
        }
    }

    if (pointer_down && ctx->active_ui->active_id != 0u) {
        if (ctx->active_ui->has_pointer) {
            const float dx = pointer_pos.x - ctx->active_ui->left_press_origin.x;
            const float dy = pointer_pos.y - ctx->active_ui->left_press_origin.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 > ctx->active_ui->left_max_drag_dist_sq) {
                ctx->active_ui->left_max_drag_dist_sq = d2;
            }
        }
        if (ctx->active_ui->marquee_canvas_id != 0u && ctx->active_ui->has_pointer) {
            if (WidgetState* mws = state_map_find(&ctx->active_ui->states, ctx->active_ui->marquee_canvas_id); mws && mws->cold) {
                WidgetCold* mc = mws->cold;
                const Vec2 p0 = mc->canvas_marquee_start_world;
                const Vec2 p1 = canvas_screen_to_world(mws, pointer_pos);
                const float x0 = (p0.x < p1.x) ? p0.x : p1.x;
                const float y0 = (p0.y < p1.y) ? p0.y : p1.y;
                const float x1 = (p0.x > p1.x) ? p0.x : p1.x;
                const float y1 = (p0.y > p1.y) ? p0.y : p1.y;
                mc->canvas_marquee_world = Rect{ x0, y0, x1 - x0, y1 - y0 };
                ctx->active_ui->desired_cursor = Cursor::Crosshair;
                request_redraw(ctx);
            } else {
                ctx->active_ui->marquee_canvas_id = 0u;
            }
        }
        if (!ctx->headless && ctx->active_window && !ctx->active_ui->mouse_captured) {
            if ((!ctx->mouse_capture_window || ctx->mouse_capture_window == ctx->active_window) && SDL_CaptureMouse(true)) {
                ctx->mouse_capture_window = ctx->active_window;
                ctx->active_ui->mouse_captured = true;
            }
        }
        if (WidgetState* sb_ws = find_scrollbar_owner(ctx, ctx->active_ui->active_id)) {
            const bool horizontal=ctx->active_ui->active_id==id_index(sb_ws->key,kScrollbarXTag);
            apply_scrollbar_drag(sb_ws,horizontal ? pointer_pos.x : pointer_pos.y,ctx->active_ui->scrollbar_drag_grab_offset,horizontal);
            ctx->active_ui->desired_cursor = Cursor::Hand;
        }
    } else if (released_this_frame) {
        if (ctx->active_ui->mouse_captured && ctx->active_ui->pan_canvas_id == 0u) {
            if (ctx->mouse_capture_window == ctx->active_window) { SDL_CaptureMouse(false); ctx->mouse_capture_window = nullptr; }
            ctx->active_ui->mouse_captured = false;
        }
        if (ctx->active_ui->marquee_canvas_id != 0u) {
            if (WidgetState* mws = state_map_find(&ctx->active_ui->states, ctx->active_ui->marquee_canvas_id); mws && mws->cold) {
                WidgetCold* mc = mws->cold;
                if (ctx->active_ui->has_pointer) {
                    const Vec2 p0 = mc->canvas_marquee_start_world;
                    const Vec2 p1 = canvas_screen_to_world(mws, pointer_pos);
                    const float x0 = (p0.x < p1.x) ? p0.x : p1.x;
                    const float y0 = (p0.y < p1.y) ? p0.y : p1.y;
                    const float x1 = (p0.x > p1.x) ? p0.x : p1.x;
                    const float y1 = (p0.y > p1.y) ? p0.y : p1.y;
                    mc->canvas_marquee_world = Rect{ x0, y0, x1 - x0, y1 - y0 };
                }
                mc->canvas_marquee_active = false;
                if (mc->canvas_marquee_world.w >= 3.0f ||
                    mc->canvas_marquee_world.h >= 3.0f ||
                    ctx->active_ui->left_max_drag_dist_sq >= 9.0f)
                {
                    mc->canvas_marquee_finished = true;
                    ctx->active_ui->marquee_finished_canvas_id = ctx->active_ui->marquee_canvas_id;
                }
            }
            ctx->active_ui->marquee_canvas_id = 0u;
        }
        if (ctx->active_ui->drag_active) {
            ctx->active_ui->drag_active = false;
            ctx->active_ui->drag_drop_pending = true;
            ctx->active_ui->drag_accepted_this_frame = false;
        }
        if (ctx->active_ui->active_id != 0u) {
            const bool suppress_click =
                (ctx->active_ui->drag_drop_pending && ctx->active_ui->active_id == ctx->active_ui->drag_source_id) ||
                (ctx->active_ui->marquee_finished_canvas_id != 0u && ctx->active_ui->active_id == ctx->active_ui->marquee_finished_canvas_id);
            if (!suppress_click) {
                if (WidgetState* ws = state_map_find(&ctx->active_ui->states, ctx->active_ui->active_id)) {
                    const bool over_active = ws->is_text_span
                        ? ws->span_hovered
                        : pointer_over_id(ctx->active_ui, ctx->active_ui->active_id);
                    if (ws->is_user_element && over_active) {
                        ws->clicked_this_frame = true;
                    }
                }
            }
        }
        state_map_clear_flags(&ctx->active_ui->states, false, true);
        ctx->active_ui->last_released_drag_dist_sq = ctx->active_ui->left_max_drag_dist_sq;
        ctx->active_ui->active_id = 0u;
        ctx->active_ui->left_max_drag_dist_sq = 0.0f;
    }

    const bool pan_held = pointer_down || middle_down || (right_down && ctx->active_ui->right_drag_exceeded);
    if (ctx->active_ui->pan_canvas_id != 0u && pan_held) {
        if (!ctx->headless && ctx->active_window && !ctx->active_ui->mouse_captured) {
            if ((!ctx->mouse_capture_window || ctx->mouse_capture_window == ctx->active_window) && SDL_CaptureMouse(true)) {
                ctx->mouse_capture_window = ctx->active_window;
                ctx->active_ui->mouse_captured = true;
            }
        }
        if (WidgetState* cws = state_map_find(&ctx->active_ui->states, ctx->active_ui->pan_canvas_id); cws && cws->cold) {
            if (!pan_started_this_frame && ctx->active_ui->had_pointer && ctx->active_ui->has_pointer) {
                cws->cold->canvas_pan.x += pointer_pos.x - ctx->active_ui->prev_pointer_pos.x;
                cws->cold->canvas_pan.y += pointer_pos.y - ctx->active_ui->prev_pointer_pos.y;
            }
            ctx->active_ui->desired_cursor = Cursor::Move;
            request_redraw(ctx);
        } else {
            ctx->active_ui->pan_canvas_id = 0u;
        }
    } else if (ctx->active_ui->pan_canvas_id != 0u) {
        ctx->active_ui->pan_canvas_id = 0u;
        if (ctx->active_ui->mouse_captured && ctx->active_ui->active_id == 0u) {
            if (ctx->mouse_capture_window == ctx->active_window) { SDL_CaptureMouse(false); ctx->mouse_capture_window = nullptr; }
            ctx->active_ui->mouse_captured = false;
        }
    }

    // Apply scroll wheel impulses to scrollable/virtual containers, or zoom hovered interactive canvas.
    if (scroll.x != 0.0f || scroll.y != 0.0f) {
        WidgetState* target_scroll = nullptr;
        WidgetState* fallback_scroll = nullptr;
        WidgetState* target_canvas = nullptr;
        for (int32_t i = static_cast<int32_t>(over.size()) - 1; i >= 0; --i) {
            WidgetState* ws = state_map_find(&ctx->active_ui->states, over[static_cast<size_t>(i)]);
            if (!ws || !ws->is_user_element) {
                continue;
            }
            WidgetCold* wc = ws->cold;
            if (ws->is_multiline_input && wc && wc->edit_max_scroll_y>0) {
                wc->edit_scroll_y=clamp_range(wc->edit_scroll_y-scroll.y*48,0,wc->edit_max_scroll_y);
                wc->edit_reveal_caret=false;request_redraw(ctx);break;
            }
            if (ws->is_virtual_list) {
                if (!fallback_scroll) {
                    fallback_scroll = ws;
                }
                const bool can_vy = wc && wc->vscroll_max_y > 0.5 &&
                    ((scroll.y < 0.0f && wc->vscroll_pos_y < wc->vscroll_max_y - 0.5) ||
                     (scroll.y > 0.0f && wc->vscroll_pos_y > 0.5));
                if (can_vy) {
                    target_scroll = ws;
                    break;
                }
            } else if (ws->is_scroll_x || ws->is_scroll_y) {
                if (!fallback_scroll) {
                    fallback_scroll = ws;
                }
                const bool can_y = ws->is_scroll_y && ws->scroll_max.y > 0.5f &&
                    ((scroll.y < 0.0f && ws->scroll_pos.y < ws->scroll_max.y - 0.5f) ||
                     (scroll.y > 0.0f && ws->scroll_pos.y > 0.5f));
                const bool can_x = ws->is_scroll_x && ws->scroll_max.x > 0.5f &&
                    ((scroll.x < 0.0f && ws->scroll_pos.x < ws->scroll_max.x - 0.5f) ||
                     (scroll.x > 0.0f && ws->scroll_pos.x > 0.5f));
                if (can_y || can_x) {
                    target_scroll = ws;
                    break;
                }
            }
            if (ws->is_canvas && wc && wc->canvas_interactive) {
                target_canvas = ws;
                if (scroll.y != 0.0f) {
                    break;
                }
            } else if (!target_canvas && wc && wc->owner_canvas_id != 0u) {
                WidgetState* cws = state_map_find(&ctx->active_ui->states, wc->owner_canvas_id);
                if (cws && cws->is_canvas && cws->cold && cws->cold->canvas_interactive) {
                    target_canvas = cws;
                }
            }
        }
        if (target_scroll) {
            if (target_scroll->is_virtual_list) {
                if (WidgetCold* tc = ensure_widget_cold(target_scroll)) {
                    tc->vlist_pending_scroll = false;
                    if (scroll.y > 0.0f) {
                        tc->vlist_stuck_bottom = false;
                    }
                    if (ctx->active_ui->sim_dt <= 0.0f) {
                        tc->vscroll_pos_y = clamp_double(
                            tc->vscroll_pos_y - static_cast<double>(scroll.y) * 48.0,
                            0.0,
                            tc->vscroll_max_y);
                    } else {
                        tc->vscroll_vel_y += static_cast<double>(-scroll.y) * 560.0;
                    }
                }
            } else {
                if (ctx->active_ui->sim_dt <= 0.0f) {
                    if (target_scroll->is_scroll_x) {
                        target_scroll->scroll_pos.x = clamp_range(target_scroll->scroll_pos.x - scroll.x * 48.0f, 0.0f, target_scroll->scroll_max.x);
                    }
                    if (target_scroll->is_scroll_y) {
                        if (scroll.y > 0.0f) {
                            target_scroll->scroll_stuck_bottom = false;
                        }
                        target_scroll->scroll_pos.y = clamp_range(target_scroll->scroll_pos.y - scroll.y * 48.0f, 0.0f, target_scroll->scroll_max.y);
                        if (scroll.y < 0.0f && target_scroll->scroll_pos.y >= target_scroll->scroll_max.y - 2.0f) {
                            target_scroll->scroll_stuck_bottom = true;
                        }
                    }
                } else {
                    if (target_scroll->is_scroll_x) {
                        target_scroll->scroll_vel.x += (-scroll.x) * 560.0f;
                    }
                    if (target_scroll->is_scroll_y) {
                        if (scroll.y > 0.0f) {
                            target_scroll->scroll_stuck_bottom = false;
                        }
                        target_scroll->scroll_vel.y += (-scroll.y) * 560.0f;
                        if (scroll.y < 0.0f && target_scroll->scroll_vel.y >= -1.0f &&
                            target_scroll->scroll_pos.y >= target_scroll->scroll_max.y - 2.0f)
                        {
                            target_scroll->scroll_stuck_bottom = true;
                        }
                    }
                }
            }
            request_redraw(ctx);
        } else if (target_canvas && target_canvas->cold && scroll.y != 0.0f) {
            WidgetCold* cc = target_canvas->cold;
            const float old_z = (cc->canvas_zoom > 1e-4f) ? cc->canvas_zoom : 1.0f;
            const float factor = std::pow(1.12f, scroll.y);
            const float new_z = clamp_range(old_z * factor, cc->canvas_min_zoom, cc->canvas_max_zoom);
            const Rect vp = target_canvas->last_box_logical;
            if (vp.w > 0.0f && vp.h > 0.0f && ctx->active_ui->has_pointer) {
                const Vec2 local_ptr = Vec2{ pointer_pos.x - vp.x, pointer_pos.y - vp.y };
                const Vec2 world_under_ptr = (local_ptr - cc->canvas_pan) * (1.0f / old_z);
                cc->canvas_zoom = new_z;
                cc->canvas_pan = local_ptr - world_under_ptr * new_z;
            } else {
                cc->canvas_zoom = new_z;
            }
            request_redraw(ctx);
        } else if (fallback_scroll) {
            if (fallback_scroll->is_virtual_list) {
                if (WidgetCold* fc = ensure_widget_cold(fallback_scroll)) {
                    fc->vlist_pending_scroll = false;
                    if (scroll.y > 0.0f) {
                        fc->vlist_stuck_bottom = false;
                    }
                    if (ctx->active_ui->sim_dt <= 0.0f) {
                        fc->vscroll_pos_y = clamp_double(
                            fc->vscroll_pos_y - static_cast<double>(scroll.y) * 48.0,
                            0.0,
                            fc->vscroll_max_y);
                    } else {
                        fc->vscroll_vel_y += static_cast<double>(-scroll.y) * 560.0;
                    }
                }
            } else {
                if (ctx->active_ui->sim_dt <= 0.0f) {
                    if (fallback_scroll->is_scroll_x) {
                        fallback_scroll->scroll_pos.x = clamp_range(fallback_scroll->scroll_pos.x - scroll.x * 48.0f, 0.0f, fallback_scroll->scroll_max.x);
                    }
                    if (fallback_scroll->is_scroll_y) {
                        if (scroll.y > 0.0f) {
                            fallback_scroll->scroll_stuck_bottom = false;
                        }
                        fallback_scroll->scroll_pos.y = clamp_range(fallback_scroll->scroll_pos.y - scroll.y * 48.0f, 0.0f, fallback_scroll->scroll_max.y);
                        if (scroll.y < 0.0f && fallback_scroll->scroll_pos.y >= fallback_scroll->scroll_max.y - 2.0f) {
                            fallback_scroll->scroll_stuck_bottom = true;
                        }
                    }
                } else {
                    if (fallback_scroll->is_scroll_x) {
                        fallback_scroll->scroll_vel.x += (-scroll.x) * 560.0f;
                    }
                    if (fallback_scroll->is_scroll_y) {
                        if (scroll.y > 0.0f) {
                            fallback_scroll->scroll_stuck_bottom = false;
                        }
                        fallback_scroll->scroll_vel.y += (-scroll.y) * 560.0f;
                        if (scroll.y < 0.0f && fallback_scroll->scroll_vel.y >= -1.0f &&
                            fallback_scroll->scroll_pos.y >= fallback_scroll->scroll_max.y - 2.0f)
                        {
                            fallback_scroll->scroll_stuck_bottom = true;
                        }
                    }
                }
            }
            request_redraw(ctx);
        }
    }

    // Integrate frame-rate-independent inertial scroll damping
    if (ctx->active_ui->sim_dt > 0.0f && ctx->active_ui->states.entries) {
        constexpr float kScrollFriction = 10.5f;
        const float decay = std::exp(-kScrollFriction * ctx->active_ui->sim_dt);
        const float step_factor = (1.0f - decay) / kScrollFriction;
        for (uint32_t i = 0u; i < ctx->active_ui->states.capacity; ++i) {
            WidgetState& ws = ctx->active_ui->states.entries[i];
            if (ws.key == 0u || (!ws.is_scroll_x && !ws.is_scroll_y && !ws.is_virtual_list)) {
                continue;
            }
            bool moving = false;
            if (ws.is_scroll_x) {
                moving |= step_scroll_axis(&ws.scroll_pos.x, &ws.scroll_vel.x, ws.scroll_max.x, decay, step_factor);
            }
            if (ws.is_scroll_y) {
                const float vel_before = ws.scroll_vel.y;
                moving |= step_scroll_axis(&ws.scroll_pos.y, &ws.scroll_vel.y, ws.scroll_max.y, decay, step_factor);
                if (vel_before > 0.0f && ws.scroll_vel.y >= -1.0f &&
                    ws.scroll_pos.y >= ws.scroll_max.y - 2.0f)
                {
                    ws.scroll_stuck_bottom = true;
                }
            }
            if (ws.is_virtual_list && ws.cold) {
                moving |= step_scroll_axis_d(
                    &ws.cold->vscroll_pos_y,
                    &ws.cold->vscroll_vel_y,
                    ws.cold->vscroll_max_y,
                    static_cast<double>(decay),
                    static_cast<double>(step_factor));
            }
            if (moving) {
                ++ctx->active_ui->active_anim_count;
            }
        }
    }

    if (ctx->active_ui->applied_font_revision != ctx->atlas.font_revision) {
        layout_engine_reset_text_cache(ctx->active_ui->layout);
        ctx->active_ui->applied_font_revision = ctx->atlas.font_revision;
    }

    layout_begin(ctx->active_ui->layout, logical_w, logical_h);
}

static void resolve_gradient_theme_colors(GradientDesc* g, const Theme& thm) noexcept {
    if (g->kind == GradientKind::None) {
        return;
    }
    const uint8_t gc = g->stop_count < 8u ? g->stop_count : 8u;
    for (uint8_t gi = 0u; gi < gc; ++gi) {
        g->stops[gi].color = resolve_theme_color(
            g->stops[gi].color,
            gi == 0u ? thm.bg_surface : thm.bg_elevated);
    }
}

void begin_box(const BoxDesc& desc_in) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame) {
        return;
    }

    auto* build=ctx->frame_arena.alloc<BoxBuildStack>();
    if(build) {*build={ctx->active_ui->box_build_stack,ctx->active_ui->current_layer};ctx->active_ui->box_build_stack=build;}
    const float wscale = current_world_scale(ctx);
    BoxDesc desc = desc_in;
    desc.transition=valid_spring_spec(desc.transition);
    const auto positive_px = [](float px) noexcept {
        return std::isfinite(px) && px > 0.0f ? std::fmin(px, 65504.0f) : 0.0f;
    };
    desc.border_sides = {positive_px(desc.border_sides.left), positive_px(desc.border_sides.right),
                         positive_px(desc.border_sides.top), positive_px(desc.border_sides.bottom)};
    desc.border_dash_px = positive_px(desc.border_dash_px);
    desc.border_gap_px = positive_px(desc.border_gap_px);
    desc.border_beam=valid_border_beam(desc.border_beam);
    desc.scroll_fade_px=std::isfinite(desc.scroll_fade_px) && desc.scroll_fade_px>0 ? desc.scroll_fade_px : 0;
    desc.edge_fade = {positive_px(desc.edge_fade.left), positive_px(desc.edge_fade.right),
                      positive_px(desc.edge_fade.top), positive_px(desc.edge_fade.bottom)};
    desc.top_rim_alpha=std::isfinite(desc.top_rim_alpha) ? clamp_range(desc.top_rim_alpha,0,1) : 0;
    if(desc.border_beam.color.a && desc.border_beam.beam_active && desc.border_beam.speed!=0 && !ctx->reduced_motion && ctx->time_scale>0) ++ctx->active_ui->active_anim_count;
    if(!std::isfinite(desc.opacity)) desc.opacity=1;
    desc.opacity=std::fmin(1.f,std::fmax(0.f,desc.opacity));
    if(!std::isfinite(desc.hover_scale) || desc.hover_scale<=0) desc.hover_scale=1;
    if(!std::isfinite(desc.press_scale) || desc.press_scale<=0) desc.press_scale=1;
    desc.backdrop_saturation = std::isfinite(desc.backdrop_saturation) ? clamp_range(desc.backdrop_saturation, 0.0f, 2.5f) : 1.0f;
    desc.backdrop_brightness = std::isfinite(desc.backdrop_brightness) ? clamp_range(desc.backdrop_brightness, 0.0f, 2.0f) : 1.0f;
    desc.backdrop_noise = std::isfinite(desc.backdrop_noise) ? clamp_range(desc.backdrop_noise, 0.0f, 0.1f) : 0.0f;
    if (desc.shadow_count > 4u) desc.shadow_count = 4u;
    if (wscale != 1.0f) {
        desc.width = scale_sizing(desc.width, wscale);
        desc.height = scale_sizing(desc.height, wscale);
        desc.padding = scale_padding(desc.padding, wscale);
        desc.margin = scale_padding(desc.margin, wscale);
        desc.gap *= wscale;
        if (desc.row_gap >= 0) desc.row_gap *= wscale;
        desc.border_width *= wscale;
        desc.border_sides.left *= wscale;
        desc.border_sides.right *= wscale;
        desc.border_sides.top *= wscale;
        desc.border_sides.bottom *= wscale;
        desc.border_dash_px *= wscale;
        desc.border_gap_px *= wscale;
        desc.border_beam.width *= wscale;
        desc.border_beam.glow_px *= wscale;
        desc.scroll_fade_px *= wscale;
        desc.edge_fade = scale_padding(desc.edge_fade, wscale);
        desc.radius = scale_corners_uniform(desc.radius, wscale);
        desc.shadow_blur *= wscale;
        desc.shadow_spread *= wscale;
        desc.shadow_offset.x *= wscale;
        desc.shadow_offset.y *= wscale;
        for (uint8_t si = 0u; si < desc.shadow_count; ++si) {
            desc.shadows[si].blur_px *= wscale;
            desc.shadows[si].spread_px *= wscale;
            desc.shadows[si].offset.x *= wscale;
            desc.shadows[si].offset.y *= wscale;
        }
        desc.floating_offset.x *= wscale;
        desc.floating_offset.y *= wscale;
    }

    WidgetState* box_ws = nullptr;
    if (desc.id != 0u) {
        box_ws = state_map_get_or_insert(&ctx->active_ui->states, desc.id, ctx->active_ui->frame_index);
        if (box_ws) {
            box_ws->is_user_element = !desc.pointer_passthrough;
            box_ws->window_drag_passthrough = desc.window_drag_passthrough;
            box_ws->popup_owner=ctx->active_ui->popup_depth ? ctx->active_ui->popup_build_ids[ctx->active_ui->popup_depth-1] : 0;
            box_ws->is_focusable = desc.focusable;
            box_ws->focus_scope = ctx->active_ui->modal_scope && focus_scope_contains(*ctx->active_ui, ctx->active_ui->modal_scope) ? ctx->active_ui->modal_scope : ctx->active_ui->focus_scope;
            box_ws->show_scrollbar = desc.show_scrollbar;
            box_ws->is_scroll_x = desc.scroll_x;
            box_ws->is_scroll_y = desc.scroll_y;
            if (desc.scroll_y && desc.stick_to_bottom && !box_ws->scroll_stick_initialized) {
                box_ws->scroll_stuck_bottom = true;
                box_ws->scroll_stick_initialized = true;
            }
            box_ws->is_text_input_writable = false;
            if (ctx->active_ui->canvas_depth > 0u) {
                if (WidgetCold* bc = ensure_widget_cold(box_ws)) {
                    bc->owner_canvas_id = ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth - 1u].id;
                }
            } else if (box_ws->cold) {
                box_ws->cold->owner_canvas_id = 0u;
            }
        }
        if (desc.focusable && reserve_focus(ctx->active_ui)) {
            ctx->active_ui->focus_order[ctx->active_ui->focus_count++] = desc.id;
            if (ctx->active_ui->modal_needs_focus && focus_scope_contains(*ctx->active_ui, ctx->active_ui->modal_scope)) {
                ctx->active_ui->focus_id = desc.id;
                ctx->active_ui->modal_needs_focus = false;
                clear_ime_composition(ctx);
            }
        }
    }
    const uint32_t node_idx = layout_open(ctx->active_ui->layout, desc.id);
    if (node_idx == kLayoutInvalidNode) {
        return;
    }

    if (box_ws && (desc.scroll_x || desc.scroll_y)) {
        constexpr uint32_t kScrollActionMask =
            12u | a11y_action_bit(A11yAction::ScrollUp) | a11y_action_bit(A11yAction::ScrollDown);
        PendingA11yAction sc_cmd = {};
        float delta_x = 0.0f;
        float delta_y = 0.0f;
        const float page_y = std::fmax(40.0f, box_ws->viewport_dim.y * 0.85f);
        while (a11y_take_any(desc.id, kScrollActionMask, &sc_cmd)) {
            if (sc_cmd.action == A11yAction::ScrollUp || sc_cmd.action == A11yAction::ScrollDown) {
                const float step = (sc_cmd.number > 0.0f && std::isfinite(sc_cmd.number)) ? sc_cmd.number : page_y;
                delta_y += (sc_cmd.action == A11yAction::ScrollDown) ? step : -step;
            } else {
                const float d = (sc_cmd.action == A11yAction::Increment) ? 40.0f : -40.0f;
                if (desc.scroll_y) delta_y += d;
                else delta_x += d;
            }
        }
        if (delta_y != 0.0f && desc.scroll_y) {
            box_ws->scroll_pos.y = clamp_range(box_ws->scroll_pos.y + delta_y, 0.0f, box_ws->scroll_max.y);
            box_ws->scroll_vel.y = 0.0f;
            if (delta_y < 0.0f) {
                box_ws->scroll_stuck_bottom = false;
            } else if (box_ws->scroll_max.y <= 0.5f || box_ws->scroll_pos.y >= box_ws->scroll_max.y - 2.0f) {
                box_ws->scroll_stuck_bottom = true;
            }
            request_redraw(ctx);
        }
        if (delta_x != 0.0f && desc.scroll_x) {
            box_ws->scroll_pos.x = clamp_range(box_ws->scroll_pos.x + delta_x, 0.0f, box_ws->scroll_max.x);
            box_ws->scroll_vel.x = 0.0f;
            request_redraw(ctx);
        }
    }
    const Theme& thm = active_theme(ctx);
    BoxDesc eff_desc = desc;
    eff_desc.background = resolve_theme_color(eff_desc.background, thm.bg_surface);
    eff_desc.background_end = resolve_theme_color(eff_desc.background_end, thm.bg_elevated);
    eff_desc.border_color = resolve_theme_color(eff_desc.border_color, thm.border_subtle);
    eff_desc.shadow_color = resolve_theme_color(eff_desc.shadow_color, thm.shadow);
    for (uint8_t si = 0u; si < eff_desc.shadow_count; ++si) {
        eff_desc.shadows[si].color = resolve_theme_color(eff_desc.shadows[si].color, thm.shadow);
    }
    resolve_gradient_theme_colors(&eff_desc.gradient, thm);
    eff_desc.radius = resolve_theme_corners(eff_desc.radius, Corners::all(thm.radius_panel_px));

    if(box_ws && (desc.hover_background.a || desc.hover_border_color.a || desc.hover_scale!=1 || desc.press_scale!=1)) {
        advance_motion(ctx,&box_ws->hover_t,&box_ws->hover_vel,hovered(desc.id) ? 1.f : 0.f,desc.transition);
        advance_motion(ctx,&box_ws->press_t,&box_ws->press_vel,pressed(desc.id) ? 1.f : 0.f,desc.transition);
        if(desc.hover_background.a) eff_desc.background=Color::lerp_oklab(eff_desc.background,desc.hover_background,box_ws->hover_t);
        if(desc.hover_border_color.a) eff_desc.border_color=Color::lerp_oklab(eff_desc.border_color,desc.hover_border_color,box_ws->hover_t);
    }
    const bool focus_ring = eff_desc.focusable && keyboard_focus_visible(eff_desc.id);
    const bool is_scroll = eff_desc.scroll_x || eff_desc.scroll_y;
    const bool any_clip = eff_desc.clip_children || is_scroll || eff_desc.shrink_width || eff_desc.shrink_height;
    const HitboxBehavior eff_hitbox = eff_desc.pointer_passthrough ? HitboxBehavior::Passthrough : eff_desc.hitbox_behavior;
    const bool is_floating_blocker = (eff_desc.attach_to != AttachTo::None && eff_hitbox != HitboxBehavior::Passthrough);
    const bool has_beam=eff_desc.border_beam.color.a>0 || eff_desc.border_beam.base_color.a>0 || eff_desc.top_rim_alpha>0;
    const bool has_border = (eff_desc.border_color.a > 0u &&
        (eff_desc.border_width > 0.0f || has_border_sides(eff_desc))) || has_beam;
    const bool has_scrollbar = is_scroll && eff_desc.show_scrollbar;
    const bool has_multi_grad = (eff_desc.gradient.kind != GradientKind::None) && (eff_desc.gradient.stop_count >= 2u);
    const bool has_single_shadow = inset_shadow_active(
        eff_desc.shadow_color, eff_desc.shadow_blur, eff_desc.shadow_spread, eff_desc.shadow_offset);
    const bool has_multi_shadow = (eff_desc.shadow_count > 0u) || (eff_desc.elevation != Elevation::None);
    const bool has_visual =
        eff_desc.background.a > 0u ||
        eff_desc.background_end.a > 0u ||
        eff_desc.radial_gradient ||
        has_multi_grad ||
        has_single_shadow ||
        has_multi_shadow ||
        eff_desc.backdrop_blur > 0.0f ||
        has_border || focus_ring;

    if (ctx->active_ui->subtree_depth > 0u) {
        SubtreeMeta* sm = ctx->active_ui->subtree_stack[ctx->active_ui->subtree_depth - 1u].meta;
        if (sm) {
            if (desc.id != 0u) sm->has_interactive = true;
            if (eff_desc.attach_to != AttachTo::None || eff_desc.backdrop_blur > 0.0f || has_multi_grad ||
                eff_desc.opacity < 1.0f || eff_desc.animate_layout || eff_desc.hero_id != 0u || is_scroll)
            {
                sm->tainted = true;
            }
        }
    }

    BoxExtra* extra = nullptr;
    if (has_visual || any_clip || is_floating_blocker || eff_hitbox == HitboxBehavior::BlockMouse ||
        eff_desc.id || ctx->active_ui->current_layer || eff_desc.opacity<1 || eff_desc.animate_layout ||
        eff_desc.hero_id || eff_desc.hover_scale!=1 || eff_desc.press_scale!=1)
    {
        extra = ctx->frame_arena.alloc<BoxExtra>();
        if (extra) {
            extra->desc = eff_desc;
            extra->world_scale = wscale;
            if(eff_desc.opacity<1 || eff_desc.animate_layout || eff_desc.hero_id || eff_desc.hover_scale!=1 || eff_desc.press_scale!=1) {
                push_layer({.opacity=eff_desc.opacity});
                if(build && ctx->active_ui->current_layer!=build->previous_layer) const_cast<VisualLayerNode*>(ctx->active_ui->current_layer)->source_box=extra;
            }
            if (const VisualLayerNode* parent_layer = build ? build->previous_layer : ctx->active_ui->current_layer;
                parent_layer && !parent_layer->source_box && !parent_layer->pivot_box) {
                const_cast<VisualLayerNode*>(parent_layer)->pivot_box = extra;
            }
            extra->layer=ctx->active_ui->current_layer;
            extra->keyboard_focus_ring = focus_ring;
            extra->focus_ring_color = thm.focus_ring;
            extra->focus_ring_radius_px = thm.radius_small_px * 0.8f;
            extra->scrollbar_track = thm.scrollbar_track;
            extra->scrollbar_thumb = thm.scrollbar_thumb;
            extra->scrollbar_thumb_hover = thm.scrollbar_thumb_hover;
            extra->scrollbar_thumb_drag = thm.scrollbar_thumb_drag;
        }
    }

    const auto pack_grid_tracks = [&](std::span<const GridTrack> src, const LayoutGridTrack** out_ptr, uint16_t* out_cnt) noexcept {
        const size_t n = src.size() > 128u ? 128u : src.size();
        if (!n) return;
        auto* tracks = ctx->frame_arena.alloc<LayoutGridTrack>(n);
        if (!tracks) return;
        for (size_t i = 0; i < n; ++i) {
            const GridTrack& t = src[i];
            const float lo = std::isfinite(t.min_px) && t.min_px > 0.0f ? t.min_px * wscale : 0.0f;
            const float hi = std::isfinite(t.max_px) && t.max_px > 0.0f ? ((lo > t.max_px * wscale) ? lo : t.max_px * wscale) : 0.0f;
            const float value = std::isfinite(t.value) && t.value >= 0.0f ? t.value : 0.0f;
            tracks[i] = LayoutGridTrack{
                .kind = t.kind,
                .value = t.kind == SizingKind::Fixed ? value * wscale : value,
                .min_px = lo,
                .max_px = hi,
            };
        }
        *out_ptr = tracks;
        *out_cnt = static_cast<uint16_t>(n);
    };

    LayoutNodeConfig decl = {};
    decl.width = desc.width;
    decl.height = desc.height;
    decl.padding = desc.padding;
    decl.margin = desc.margin;
    decl.child_gap = std::isfinite(desc.gap) && desc.gap > 0.0f ? desc.gap : 0.0f;
    decl.cross_gap = std::isfinite(desc.row_gap) && desc.row_gap >= 0.0f ? desc.row_gap : decl.child_gap;
    decl.axis = desc.axis;
    decl.align_x = desc.align_x;
    decl.align_y = desc.align_y;
    decl.justify = desc.justify;
    decl.align_self = desc.align_self;
    decl.flex_grow = std::isfinite(desc.flex_grow) && desc.flex_grow > 0.0f ? desc.flex_grow : 0.0f;
    decl.flex_shrink = std::isfinite(desc.flex_shrink) && desc.flex_shrink >= 0.0f ? desc.flex_shrink : 1.0f;
    decl.aspect_ratio = std::isfinite(desc.aspect_ratio) && desc.aspect_ratio > 0.0f ? desc.aspect_ratio : 0.0f;
    decl.wrap = desc.wrap;
    decl.rtl = (ctx->effective_layout_direction == LayoutDirection::Rtl);
    decl.shrink_width = desc.shrink_width;
    decl.shrink_height = desc.shrink_height;
    decl.grid_columns = desc.grid_columns > 128u ? 128u : desc.grid_columns;
    decl.grid_col_span = desc.grid_col_span > 0u ? desc.grid_col_span : 1u;
    decl.grid_row_span = desc.grid_row_span > 0u ? desc.grid_row_span : 1u;
    pack_grid_tracks(desc.grid_tracks, &decl.grid_col_tracks, &decl.grid_col_track_count);
    pack_grid_tracks(desc.grid_row_tracks, &decl.grid_row_tracks, &decl.grid_row_track_count);
    decl.corner_radius = desc.radius;
    decl.hitbox_behavior = eff_hitbox;
    decl.user_data = extra;

    if ((has_visual || is_floating_blocker || eff_hitbox == HitboxBehavior::BlockMouse || desc.id || (extra && extra->layer)) && extra) {
        decl.emit_custom = true;
        decl.custom_data = extra;
    }
    if (desc.attach_to != AttachTo::None) {
        decl.attach_to = desc.attach_to;
        decl.attach_element_id = desc.attach_element_id;
        decl.anchor_element = desc.anchor_element;
        decl.anchor_parent = desc.anchor_parent;
        decl.floating_offset = desc.floating_offset;
        decl.z_index = desc.z_index;
        decl.clip_to_parent = desc.clip_to_parent;
        decl.clamp_to_viewport = desc.clamp_to_viewport;
        decl.flip_on_overflow = desc.flip_on_overflow;
    }
    if (any_clip) {
        decl.clip_x = desc.clip_children || desc.scroll_x || desc.shrink_width;
        decl.clip_y = desc.clip_children || desc.scroll_y || desc.shrink_height;
        decl.scroll_x = desc.scroll_x;
        decl.scroll_y = desc.scroll_y;
        if (is_scroll && box_ws) {
            if (desc.scroll_y && desc.stick_to_bottom && box_ws->scroll_stuck_bottom) {
                box_ws->scroll_pos.y = box_ws->scroll_max.y;
                box_ws->scroll_vel.y = 0.0f;
            }
            decl.child_offset = Vec2{ -box_ws->scroll_pos.x, -box_ws->scroll_pos.y };
        }
    }
    if ((has_border && (any_clip || has_segmented_border(eff_desc))) || (any_clip && has_scrollbar) || has_beam) {
        decl.emit_border = true;
    }

    layout_configure_open(ctx->active_ui->layout, decl);
    const uint32_t aid = ctx->active_ui->layout->nodes.data[node_idx].id;
    constexpr uint32_t kScrollBits =
        12u | a11y_action_bit(A11yAction::ScrollUp) | a11y_action_bit(A11yAction::ScrollDown);
    auto* semantic = a11y_emit(ctx, desc.id ? desc.id : aid, is_scroll ? A11yRole::ScrollArea : A11yRole::Group, {}, {}, 0, (is_scroll ? kScrollBits : 0u) | (desc.id ? 2u : 0u) | (desc.focusable ? 1u : 0u));
    if (semantic) { semantic->clay_id = aid; ctx->a11y_parent = semantic; }
}

void end_box() noexcept {
    if (!current_context() || !current_context()->in_frame) {
        return;
    }
    if (current_context()->a11y_parent) current_context()->a11y_parent = current_context()->a11y_parent->parent;
    layout_close(current_context()->active_ui->layout);
    auto& ui=*current_context()->active_ui;
    if(ui.box_build_stack) {ui.current_layer=ui.box_build_stack->previous_layer;ui.box_build_stack=ui.box_build_stack->parent;}
}

void box(const BoxDesc& desc) noexcept {
    begin_box(desc);
    end_box();
}

static void register_selectable_text(Context* ctx, uint32_t element_id, const char* full_utf8, uint32_t total_bytes, bool is_static_backing) noexcept {
    auto& ui = *ctx->active_ui;
    WidgetState* ws = state_map_get_or_insert(&ui.states, element_id, ui.frame_index);
    WidgetCold* c = ensure_widget_cold(ws);
    if (!c) return;
    ws->is_user_element = true; ws->is_text_span = true; ws->is_selectable_text = true; ws->is_focusable = true;
    c->edit_text_buffer = nullptr;
    ws->is_text_input_writable = false; ws->is_scroll_x = false; ws->is_scroll_y = false;
    c->owner_canvas_id = ui.canvas_depth ? ui.canvas_stack[ui.canvas_depth-1u].id : 0u;
    ws->focus_scope = ui.modal_scope && focus_scope_contains(ui,ui.modal_scope) ? ui.modal_scope : ui.focus_scope;
    const bool allowed = !ui.modal_scope || focus_scope_contains(ui,ui.modal_scope);
    if (allowed && reserve_focus(&ui)) ui.focus_order[ui.focus_count++] = element_id;
    c->sel_text_ptr = full_utf8; c->sel_text_len = total_bytes;
    c->sel_text_epoch = ctx->arena_epoch; c->sel_text_static = is_static_backing;
    c->edit_sel_anchor = utf8_clamp_boundary(full_utf8,total_bytes,c->edit_sel_anchor);
    c->edit_cursor = utf8_clamp_boundary(full_utf8,total_bytes,c->edit_cursor);
    if (ws->span_hovered || ui.active_id == element_id) set_cursor(Cursor::IBeam);
    if (ui.left_pressed_this_frame && ui.active_id != element_id) c->edit_sel_anchor = c->edit_cursor = 0u;
    if (ui.focus_id != element_id || !allowed) return;
    for (uint32_t i = 0; i < ui.frame_text_op_count; ++i) {
        const auto& op = ui.frame_text_ops[i];
        if (op.kind != TextEditOpKind::KeyPress) continue;
        const bool primary = (op.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0u;
        const bool alt = (op.mod & SDL_KMOD_ALT) != 0u;
        if (primary && !alt && op.key == SDLK_A) { c->edit_sel_anchor = 0; c->edit_cursor = total_bytes; request_redraw(ctx); }
        if (primary && !alt && op.key == SDLK_C) {
            const uint32_t a = c->edit_cursor < c->edit_sel_anchor ? c->edit_cursor : c->edit_sel_anchor;
            const uint32_t b = c->edit_cursor > c->edit_sel_anchor ? c->edit_cursor : c->edit_sel_anchor;
            if (b > a) { const Str slice = ctx->frame_arena.push_str(full_utf8+a,b-a); if (slice.chars) (void)set_clipboard_text(slice.chars); }
        }
        if (op.key == SDLK_ESCAPE) { c->edit_sel_anchor = c->edit_cursor; request_redraw(ctx); }
    }
}

void text(Str content, const TextDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || !content.chars || content.length == 0u || !std::isfinite(desc.line_height_px)) {
        return;
    }

    Str text_str = content;
    if (!content.is_static) {
        // Copy non-static transient strings into the frame arena so layout holds valid pointers until end_frame().
        const Str copied = ctx->frame_arena.push_str(content.chars, content.length);
        if (!copied.chars) {
            return;
        }
        text_str = copied;
    }

    const float wscale = current_world_scale(ctx);
    const float tscale = ctx->auto_sys_text_scale ? ctx->effective_text_scale : 1.0f;
    const float raw_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 16.0f;
    const float clamped_px = clamp_range(raw_px * wscale * tscale, 0.25f, 65535.0f);

    const Theme& thm = active_theme(ctx);
    const Color eff_color = resolve_theme_color(desc.color, thm.text_primary);
    const Color eff_sel_color = resolve_theme_color(desc.selection_color, thm.selection);

    LayoutTextConfig cfg = {};
    cfg.text_color = eff_color;
    cfg.font_id = (desc.font_id < kMaxFontSlots) ? desc.font_id : kFontSans;
    cfg.font_size_px = clamped_px;
    cfg.letter_spacing = std::isfinite(desc.letter_spacing) ? desc.letter_spacing * wscale : 0.0f;
    cfg.line_height = desc.line_height_px > 0.0f ? (desc.line_height_px * wscale * tscale) : 0.0f;
    cfg.ellipsis = desc.ellipsis;
    cfg.max_lines = desc.max_lines;
    cfg.wrap = desc.wrap;
    cfg.align_x = desc.align_x;
    cfg.opentype_flags = desc.opentype_flags;
    if (!desc.font_features.empty()) {
        const uint32_t fc = desc.font_features.size() > 16u ? 16u : static_cast<uint32_t>(desc.font_features.size());
        if (FontFeature* copied_feats = ctx->frame_arena.alloc_uninit<FontFeature>(fc)) {
            std::memcpy(copied_feats, desc.font_features.data(), size_t(fc) * sizeof(FontFeature));
            cfg.font_features = copied_feats;
            cfg.font_feature_count = fc;
        }
    }

    SelectableTextExtra* extra = nullptr;
    if ((desc.selectable && desc.id) || ctx->active_ui->current_layer) {
        extra = ctx->frame_arena.alloc<SelectableTextExtra>();
        if (!extra) return;
        extra->layer = ctx->active_ui->current_layer;
        extra->full_utf8 = text_str.chars; extra->total_bytes = content.length;
        extra->selectable_id = desc.selectable ? desc.id : 0u; extra->selection_color = eff_sel_color;
        cfg.user_data = extra;
        if (desc.selectable && desc.id) {
            register_selectable_text(ctx, desc.id, text_str.chars, content.length, content.is_static);
            if (ctx->active_ui->subtree_depth > 0u && ctx->active_ui->subtree_stack[ctx->active_ui->subtree_depth - 1u].meta) {
                ctx->active_ui->subtree_stack[ctx->active_ui->subtree_depth - 1u].meta->has_interactive = true;
            }
        }
    }
    const uint32_t cid = layout_open_text(ctx->active_ui->layout, text_str, cfg);
    if (cid != 0u) {
        auto* n = a11y_emit(ctx, desc.id ? desc.id : cid, A11yRole::StaticText, content);
        if (n) n->clay_id = cid;
        if (extra) extra->clay_elem_id = cid;
    }
}

void text_spans(std::span<const TextSpan> spans, const RichTextDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || spans.empty()) {
        return;
    }

    uint32_t valid_count = 0u;
    uint32_t total_bytes = 0u;
    uint32_t icon_count = 0u;
    for (const TextSpan& sp : spans) {
        const bool has_text = sp.text.chars && sp.text.length > 0u;
        const bool has_icon = is_icon_loaded(ctx, sp.icon_id);
        if (!has_text && !has_icon) continue;
        const uint32_t marker = has_icon ? 1u : 0u;
        const uint32_t text_len = has_text ? sp.text.length : 0u;
        if (text_len > 0x3FFFFFFFu || text_len + marker > 0x3FFFFFFFu - total_bytes) {
            return;
        }
        total_bytes += text_len + marker;
        if (has_icon) ++icon_count;
        ++valid_count;
    }
    if (valid_count == 0u || total_bytes == 0u) {
        return;
    }

    char* full_utf8 = ctx->frame_arena.alloc_uninit<char>(total_bytes + 1u);
    RichSpanInternal* int_spans = ctx->frame_arena.alloc_uninit<RichSpanInternal>(valid_count);
    RichTextExtra* rich = ctx->frame_arena.alloc<RichTextExtra>();
    const uint32_t text_bytes = total_bytes - icon_count;
    char* plain_utf8 = icon_count ? ctx->frame_arena.alloc_uninit<char>(text_bytes + 1u) : full_utf8;
    if (!full_utf8 || !int_spans || !rich || !plain_utf8) {
        return;
    }

    rich->layer=ctx->active_ui->current_layer;
    const Theme& thm = active_theme(ctx);
    const Color base_color = resolve_theme_color(desc.color, thm.text_primary);
    const Color eff_sel_color = resolve_theme_color(desc.selection_color, thm.selection);

    const float wscale = current_world_scale(ctx);
    const float tscale = ctx->auto_sys_text_scale ? ctx->effective_text_scale : 1.0f;
    const float base_px = clamp_range(
        ((desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f) * wscale * tscale,
        0.25f,
        4096.0f);

    uint32_t cursor = 0u;
    uint32_t text_cursor = 0u;
    uint32_t out_idx = 0u;
    float max_asc = 0.0f;
    float max_desc = 0.0f;
    float max_lh = 0.0f;
    uint32_t cfg_hash = 2166136261u;
    cfg_hash = fnv1a_32_step(cfg_hash, &ctx->atlas.font_revision, sizeof(ctx->atlas.font_revision));

    for (const TextSpan& sp : spans) {
        const bool has_text = sp.text.chars && sp.text.length > 0u;
        const bool has_icon = is_icon_loaded(ctx, sp.icon_id);
        if (!has_text && !has_icon) continue;
        const uint32_t b0 = cursor;
        const uint32_t text_start = text_cursor;
        // Clay wraps the marker with the start of the span. Copy and selection only see the text.
        if (has_icon) full_utf8[cursor++] = '\x01';
        if (has_text) {
            std::memcpy(full_utf8 + cursor, sp.text.chars, sp.text.length);
            if (icon_count) std::memcpy(plain_utf8 + text_cursor, sp.text.chars, sp.text.length);
            cursor += sp.text.length;
            text_cursor += sp.text.length;
        }
        const uint32_t b1 = cursor;

        const float raw_sz = (sp.font_size_px > 0.0f) ? (sp.font_size_px * wscale * tscale) : base_px;
        const float sp_px = clamp_range(raw_sz, 0.25f, 4096.0f);
        const uint16_t req_font = (sp.font_id != kInvalidFont) ? sp.font_id : desc.font_id;
        const uint16_t resolved_slot = ctx->atlas.font_valid
            ? resolve_font_slot_idx(&ctx->atlas, req_font)
            : kInvalidFont;
        const uint16_t final_font_id = (resolved_slot != kInvalidFont) ? resolved_slot : kFontSans;
        const float eff_ls = (desc.letter_spacing + sp.letter_spacing) * wscale;
        const float eff_pad = (sp.padding_x > 0.0f) ? (sp.padding_x * wscale) : 0.0f;
        const float eff_rad = (sp.bg_radius > 0.0f) ? (sp.bg_radius * wscale) : 0.0f;
        const float icon_size = has_icon ? (std::isfinite(sp.icon_size_px) && sp.icon_size_px > 0.0f
            ? std::fmin(sp.icon_size_px * wscale, 4096.0f) : 0.9f * sp_px) : 0.0f;
        const float icon_gap = has_icon && has_text && std::isfinite(sp.icon_gap_px) && sp.icon_gap_px > 0.0f
            ? sp.icon_gap_px * wscale : 0.0f;
        const float advance = icon_size + icon_gap;
        const float pad_y = std::isfinite(sp.padding_y) && sp.padding_y > 0.0f ? sp.padding_y * wscale : 0.0f;
        const Color tint = resolve_theme_color(sp.color, base_color);

        if (sp.id != 0u) {
            if (WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, sp.id, ctx->active_ui->frame_index)) {
                ws->is_user_element = true;
                ws->is_text_span = true;
            }
        }

        float sp_asc = sp_px * 0.8f;
        float sp_desc = sp_px * 0.2f;
        float sp_lh = sp_px * 1.2f;
        uint32_t metric_h = 0u;
        if (resolved_slot != kInvalidFont) {
            const FontSlot& fs = ctx->atlas.slots[resolved_slot];
            sp_asc = fs.ascent_em * sp_px;
            sp_desc = std::abs(fs.descent_em) * sp_px;
            sp_lh = fs.line_height_em * sp_px;
            if (sp_lh < sp_px) {
                sp_lh = sp_px;
            }
            metric_h = fs.metric_hash;
        }
        if (sp_asc > max_asc) max_asc = sp_asc;
        if (sp_desc > max_desc) max_desc = sp_desc;
        max_lh = std::fmax(max_lh, std::fmax(sp_lh, icon_size) + pad_y * 2.0f);

        cfg_hash = fnv1a_32_step(cfg_hash, &b0, sizeof(b0));
        cfg_hash = fnv1a_32_step(cfg_hash, &b1, sizeof(b1));
        cfg_hash = fnv1a_32_step(cfg_hash, &sp_px, sizeof(sp_px));
        cfg_hash = fnv1a_32_step(cfg_hash, &metric_h, sizeof(metric_h));
        cfg_hash = fnv1a_32_step(cfg_hash, &eff_ls, sizeof(eff_ls));
        cfg_hash = fnv1a_32_step(cfg_hash, &eff_pad, sizeof(eff_pad));

        cfg_hash = fnv1a_32_step(cfg_hash, &advance, sizeof(advance));
        cfg_hash = fnv1a_32_step(cfg_hash, &has_text, sizeof(has_text));

        const bool sp_has_wipe = sp.has_wipe && std::isfinite(sp.wipe_x);
        const Color sp_color_end = sp_has_wipe ? resolve_theme_color(sp.color_end, tint) : Color{};
        const float sp_wipe_x = sp_has_wipe ? sp.wipe_x * wscale : 0.0f;
        const float sp_wipe_feather = (sp_has_wipe && std::isfinite(sp.wipe_feather_px) && sp.wipe_feather_px > 0.0f) ? sp.wipe_feather_px * wscale : 0.0f;

        int_spans[out_idx++] = RichSpanInternal{
            .text_byte_start = text_start,
            .byte_start = b0,
            .byte_end = b1,
            .color = tint,
            .background = sp.background,
            .border_color = sp.border_color,
            .font_size_px = sp_px,
            .font_id = final_font_id,
            .underline = sp.underline,
            .strikethrough = sp.strikethrough,
            .letter_spacing = eff_ls,
            .padding_x = eff_pad,
            .bg_radius = eff_rad,
            .id = sp.id,
            .icon_id = has_icon ? sp.icon_id : kInvalidIcon,
            .icon_color = sp.icon_color.a ? resolve_theme_color(sp.icon_color, tint) : tint,
            .hover_background = sp.hover_background,
            .icon_size = icon_size,
            .icon_advance = advance,
            .padding_y = pad_y,
            .has_wipe = sp_has_wipe,
            .color_end = sp_color_end,
            .wipe_x = sp_wipe_x,
            .wipe_feather_px = sp_wipe_feather,
        };
    }
    full_utf8[total_bytes] = '\0';
    plain_utf8[text_bytes] = '\0';

    const float final_lh = (desc.line_height_px > 0.0f) ? (desc.line_height_px * wscale * tscale) : max_lh;
    cfg_hash = fnv1a_32_step(cfg_hash, &final_lh, sizeof(final_lh));

    rich->spans = int_spans;
    rich->span_count = out_idx;
    rich->full_utf8 = full_utf8;
    rich->total_bytes = total_bytes;
    rich->has_icon_markers = icon_count != 0u;
    rich->line_height_px = final_lh;
    rich->max_ascent_px = max_asc;
    rich->max_descent_px = max_desc;
    rich->align_x = desc.align_x;
    rich->config_hash = cfg_hash;
    if (desc.selectable && desc.id) {
        rich->selectable_id = desc.id; rich->selection_color = eff_sel_color;
        register_selectable_text(ctx,desc.id,plain_utf8,text_bytes,false);
    }

    LayoutTextConfig cfg = {};
    cfg.text_color = base_color;
    cfg.font_size_px = base_px;
    cfg.font_id = (desc.font_id < kMaxFontSlots) ? desc.font_id : kFontSans;
    cfg.line_height = final_lh;
    cfg.ellipsis = desc.ellipsis;
    cfg.max_lines = desc.max_lines;
    cfg.wrap = desc.wrap;
    cfg.align_x = desc.align_x;
    cfg.is_rich = true;
    cfg.rich_hash = cfg_hash;
    cfg.user_data = rich;

    if (ctx->active_ui->subtree_depth > 0u && ctx->active_ui->subtree_stack[ctx->active_ui->subtree_depth - 1u].meta) {
        bool any_interactive = (desc.selectable && desc.id != 0u);
        for (uint32_t i = 0; i < out_idx && !any_interactive; ++i) {
            if (int_spans[i].id != 0u) any_interactive = true;
        }
        if (any_interactive) ctx->active_ui->subtree_stack[ctx->active_ui->subtree_depth - 1u].meta->has_interactive = true;
    }

    const uint32_t cid = layout_open_text(ctx->active_ui->layout, Str{full_utf8, total_bytes, false}, cfg);
    if (cid != 0u) {
        rich->clay_elem_id = cid;
        auto* n = a11y_emit(ctx, desc.id ? desc.id : cid, A11yRole::StaticText, {plain_utf8, text_bytes});
        if (n) n->clay_id = cid;
    }
}

void image(const ImageDesc& desc_in) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame) {
        return;
    }

    const float wscale = current_world_scale(ctx);
    ImageDesc desc = desc_in;
    if (desc.sprite_id != kInvalidSprite && desc_in.aspect_ratio <= 0.0f &&
        desc_in.width.kind == SizingKind::Fit && desc_in.width.min == 0 && desc_in.width.max == 0 &&
        desc_in.height.kind == SizingKind::Fit && desc_in.height.min == 0 && desc_in.height.max == 0) {
        const Vec2 sz = sprite_size(ctx, desc.sprite_id);
        desc.width = Sizing::fixed(sz.x); desc.height = Sizing::fixed(sz.y);
    }
    if (wscale != 1.0f) {
        desc.width = scale_sizing(desc.width, wscale);
        desc.height = scale_sizing(desc.height, wscale);
        desc.radius = scale_corners_uniform(desc.radius, wscale);
        desc.border_width *= wscale;
    }

    if (desc.id != 0u && ctx->active_ui->canvas_depth > 0u) {
        if (WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, desc.id, ctx->active_ui->frame_index)) {
            ws->is_user_element = true;
            if (WidgetCold* c = ensure_widget_cold(ws)) {
                c->owner_canvas_id = ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth - 1u].id;
            }
        }
    }

    ImageExtra* extra = ctx->frame_arena.alloc<ImageExtra>();
    if (!extra) {
        return;
    }
    extra->layer=ctx->active_ui->current_layer;
    extra->desc = desc;

    if (layout_open(ctx->active_ui->layout, desc.id) == kLayoutInvalidNode) {
        return;
    }

    LayoutNodeConfig decl = {};
    decl.width = desc.width;
    decl.height = desc.height;
    decl.aspect_ratio = std::isfinite(desc.aspect_ratio) && desc.aspect_ratio > 0.0f ? desc.aspect_ratio : 0.0f;
    decl.grid_col_span = desc.grid_col_span > 0u ? desc.grid_col_span : 1u;
    decl.corner_radius = desc.radius;
    decl.emit_image = true;
    decl.image_data = extra;
    decl.user_data = extra;

    layout_configure_open(ctx->active_ui->layout, decl);
    layout_close(ctx->active_ui->layout);
}

void icon(const IconDesc& desc_in) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame) {
        return;
    }
    const float wscale = current_world_scale(ctx);
    IconDesc desc = desc_in;
    desc.color = resolve_theme_color(desc.color, active_theme(ctx).text_primary);
    desc.rotation_rad = std::isfinite(desc.rotation_rad) ? desc.rotation_rad : 0.0f;
    desc.scale = std::isfinite(desc.scale) ? std::fmax(0.0f, desc.scale) : 1.0f;
    desc.opacity = std::isfinite(desc.opacity) ? std::fmin(1.0f, std::fmax(0.0f, desc.opacity)) : 1.0f;
    const float layout_sz = ((desc.size_px > 0.0f) ? desc.size_px : 16.0f) * wscale;

    if (desc.id != 0u) {
        if (WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, desc.id, ctx->active_ui->frame_index)) {
            ws->is_user_element = true;
            ws->is_text_input_writable = false;
            if (ctx->active_ui->canvas_depth > 0u) {
                if (WidgetCold* c = ensure_widget_cold(ws)) {
                    c->owner_canvas_id = ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth - 1u].id;
                }
            } else if (ws->cold) {
                ws->cold->owner_canvas_id = 0u;
            }
        }
    }
    if (layout_open(ctx->active_ui->layout, desc.id) == kLayoutInvalidNode) {
        return;
    }

    BoxExtra* extra = ctx->frame_arena.alloc<BoxExtra>();
    LayoutNodeConfig decl = {};
    decl.width = Sizing::fixed(layout_sz);
    decl.height = Sizing::fixed(layout_sz);
    if (extra) {
        extra->layer = ctx->active_ui->current_layer;
        extra->kind = CustomCmdKind::Icon;
        extra->desc.id = desc.id;
        extra->icon_desc = desc;
        decl.emit_custom = true;
        decl.custom_data = extra;
        decl.user_data = extra;
    }
    layout_configure_open(ctx->active_ui->layout, decl);
    layout_close(ctx->active_ui->layout);
}

void draw_rect(const RectDrawDesc& desc) noexcept {
    if (desc.rect.w <= 0.0f || desc.rect.h <= 0.0f || !std::isfinite(desc.angle_rad) || !std::isfinite(desc.softness) || desc.softness < 0.5f) {
        return;
    }
    Context* ctx = current_context();
    if (DrawCmdNode* node = append_open_box_draw_cmd(ctx, desc.foreground)) {
        const Theme& thm = active_theme(ctx);
        RectDrawDesc eff = desc;
        eff.fill = resolve_theme_color(eff.fill, thm.bg_surface);
        eff.fill_end = resolve_theme_color(eff.fill_end, thm.bg_elevated);
        eff.border_color = resolve_theme_color(eff.border_color, thm.border_subtle);
        eff.radius = resolve_theme_corners(eff.radius, Corners::all(thm.radius_panel_px));
        resolve_gradient_theme_colors(&eff.gradient, thm);
        if (eff.gradient.kind != GradientKind::None && eff.gradient.stop_count >= 2u &&
            ctx->active_ui->subtree_depth > 0u)
        {
            if (SubtreeMeta* sm = ctx->active_ui->subtree_stack[ctx->active_ui->subtree_depth - 1u].meta) {
                sm->tainted = true;
            }
        }
        node->kind = DrawCmdKind::Rect;
        node->rect = eff;
    }
}

void draw_shadow_internal(
    Rect rect,
    Corners radius,
    Color color,
    float blur,
    Vec2 offset = {},
    bool foreground = false,
    bool continuous_corners = false) noexcept
{
    if (rect.w <= 0.0f || rect.h <= 0.0f || color.a == 0u || !std::isfinite(blur) || blur <= 0.0f) {
        return;
    }
    Context* ctx = current_context();
    if (DrawCmdNode* node = append_open_box_draw_cmd(ctx, foreground)) {
        node->kind = DrawCmdKind::Shadow;
        node->shadow_cmd = ShadowDrawCmd{
            .rect = rect,
            .radius = radius,
            .color = color,
            .blur = blur,
            .offset = offset,
            .continuous_corners = continuous_corners,
        };
    }
}

void draw_mesh_rect(const MeshRectDrawDesc& desc) noexcept {
    Context* ctx=current_context();
    if(!ctx || !ctx->in_frame || !std::isfinite(desc.rect.x) || !std::isfinite(desc.rect.y) ||
       !std::isfinite(desc.rect.w) || !std::isfinite(desc.rect.h) || desc.rect.w<=0 || desc.rect.h<=0 ||
       !std::isfinite(desc.grain_intensity) || desc.grain_intensity<0 || desc.grain_intensity>1 ||
       !std::isfinite(desc.warp_phase)) return;
    if(auto* node=append_open_box_draw_cmd(ctx,desc.foreground)) {
        node->kind=DrawCmdKind::Mesh;node->mesh=desc;
    }
}

void punch_hole(const PunchHoleDesc& desc) noexcept {
    Context* ctx=current_context();
    if(!ctx || !ctx->in_frame || !std::isfinite(desc.rect.x) || !std::isfinite(desc.rect.y) ||
       !std::isfinite(desc.rect.w) || !std::isfinite(desc.rect.h) || desc.rect.w<=0 || desc.rect.h<=0 ||
       !std::isfinite(desc.softness) || desc.softness<.5f) return;
    if(auto* node=append_open_box_draw_cmd(ctx,desc.foreground)) {node->kind=DrawCmdKind::Hole;node->hole=desc;}
}

void draw_border_beam(const BorderBeamDrawDesc& desc) noexcept {
    Context* ctx=current_context();
    if(!ctx || !ctx->in_frame || !std::isfinite(desc.rect.x) || !std::isfinite(desc.rect.y) ||
       !std::isfinite(desc.rect.w) || !std::isfinite(desc.rect.h) || desc.rect.w<=0 || desc.rect.h<=0) return;
    if(auto* node=append_open_box_draw_cmd(ctx,desc.foreground)) {
        node->kind=DrawCmdKind::Beam;node->beam=desc;
        node->beam.beam=valid_border_beam(desc.beam);
        node->beam.top_rim_alpha=std::isfinite(desc.top_rim_alpha) ? clamp_range(desc.top_rim_alpha,0,1) : 0;
        if(desc.beam.color.a && node->beam.beam.beam_active && node->beam.beam.speed!=0 && !ctx->reduced_motion && ctx->time_scale>0) ++ctx->active_ui->active_anim_count;
    }
}

void draw_line(const LineDesc& desc) noexcept {
    if (desc.color.a == 0u && desc.color_end.a == 0u && desc.glow_color.a == 0u) {
        return;
    }
    Context* ctx = current_context();
    if (DrawCmdNode* node = append_open_box_draw_cmd(ctx, desc.foreground)) {
        node->kind = DrawCmdKind::Line;
        node->line = desc;
    }
}

void draw_filled_path(const FilledPathDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || !std::isfinite(desc.offset.x) || !std::isfinite(desc.offset.y) || !std::isfinite(desc.scale) || desc.scale <= 0 ||
        (desc.commands.empty() && (!desc.svg_path_d.chars || desc.svg_path_d.length == 0))) return;
    for (const PathCmd& c : desc.commands) {
        if (uint8_t(c.verb) > uint8_t(PathVerb::Close) ||
            !std::isfinite(c.p1.x) || !std::isfinite(c.p1.y) || !std::isfinite(c.p2.x) ||
            !std::isfinite(c.p2.y) || !std::isfinite(c.p3.x) || !std::isfinite(c.p3.y)) return;
    }
    FilledPathDesc owned = desc;
    if (!desc.commands.empty()) {
        PathCmd* copy = ctx->frame_arena.alloc_uninit<PathCmd>(desc.commands.size());
        if (!copy) return;
        std::memcpy(copy, desc.commands.data(), desc.commands.size_bytes());
        owned.commands = {copy, desc.commands.size()};
    }
    if (desc.svg_path_d.length && !desc.svg_path_d.is_static) {
        owned.svg_path_d = ctx->frame_arena.push_str(desc.svg_path_d.chars, desc.svg_path_d.length);
        if (!owned.svg_path_d.chars) return;
    }
    if (DrawCmdNode* node = append_open_box_draw_cmd(ctx, desc.foreground)) {
        node->kind = DrawCmdKind::Fill;
        node->fill_cmd = owned;
    }
}

void draw_filled_polygon(const FilledPolygonDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || desc.points.size() < 3) return;
    for (Vec2 p : desc.points) if (!std::isfinite(p.x) || !std::isfinite(p.y)) return;
    // Convert caller-owned vertices immediately to owned commands.
    PathCmd* commands = ctx->frame_arena.alloc<PathCmd>(desc.points.size()+1);
    if (!commands) return;
    for (size_t i = 0; i < desc.points.size(); ++i)
        commands[i] = {.verb = i == 0 ? PathVerb::MoveTo : PathVerb::LineTo, .p1 = desc.points[i]};
    commands[desc.points.size()] = {.verb = PathVerb::Close};
    if (DrawCmdNode* node = append_open_box_draw_cmd(ctx, desc.foreground)) {
        node->kind = DrawCmdKind::Fill;
        node->fill_cmd = {.commands = {commands, desc.points.size()+1}, .fill = desc.fill,
            .fill_end = desc.fill_end, .gradient_dir = desc.gradient_dir, .radial_gradient = desc.radial_gradient,
            .stroke_color = desc.stroke_color, .stroke_width = desc.stroke_width, .foreground = desc.foreground};
    }
}

void draw_area_series(const AreaSeriesDesc& desc) noexcept {
    Context* ctx = current_context();
    const size_t n = desc.points.size();
    if (!ctx || !ctx->in_frame || n < 2 || !std::isfinite(desc.baseline_y)) return;
    for (Vec2 p : desc.points) if (!std::isfinite(p.x) || !std::isfinite(p.y)) return;
    PathCmd* cmds = ctx->frame_arena.alloc<PathCmd>(n+3);
    if (!cmds) return;
    cmds[0] = {.verb = PathVerb::MoveTo, .p1 = desc.points[0]};
    for (size_t i = 1; i < n; ++i) {
        const Vec2 a = desc.points[i-1], b = desc.points[i];
        const Vec2 prev = desc.points[i > 1 ? i-2 : 0], next = desc.points[i+1 < n ? i+1 : i];
        const Vec2 c1 = a + (b-prev)*(1.0f/6.0f), c2 = b - (next-a)*(1.0f/6.0f);
        cmds[i] = desc.smooth ? PathCmd{.verb = PathVerb::CubicTo, .p1 = c1, .p2 = c2, .p3 = b}
            : PathCmd{.verb = PathVerb::LineTo, .p1 = b};
    }
    cmds[n] = {.verb = PathVerb::LineTo, .p1 = {desc.points[n-1].x, desc.baseline_y}};
    cmds[n+1] = {.verb = PathVerb::LineTo, .p1 = {desc.points[0].x, desc.baseline_y}};
    cmds[n+2] = {.verb = PathVerb::Close};
    const Theme& thm = active_theme(ctx);
    const Color eff_fill_top = resolve_theme_color(desc.fill_top, thm.area_fill_top);
    const Color eff_fill_bottom = resolve_theme_color(desc.fill_bottom, thm.area_fill_bottom);
    const Color eff_line_color = resolve_theme_color(desc.line_color, thm.accent);
    if (eff_fill_top.a > 0u || eff_fill_bottom.a > 0u) {
        if (DrawCmdNode* node = append_open_box_draw_cmd(ctx, desc.foreground)) {
            node->kind = DrawCmdKind::Fill;
            node->fill_cmd = {.commands = {cmds,n+3}, .fill = eff_fill_top, .fill_end = eff_fill_bottom};
        }
    }
    // Stroke only the data series, sharing the exact interpolation controls with the fill.
    if (desc.line_thickness > 0.0f && eff_line_color.a > 0u) {
        for (size_t i = 1; i < n; ++i) {
            if (desc.smooth) draw_bezier({.p0 = desc.points[i-1], .p1 = cmds[i].p1, .p2 = cmds[i].p2,
                .p3 = cmds[i].p3, .cubic = true, .color_start = eff_line_color,
                .thickness = desc.line_thickness, .foreground = desc.foreground});
            else draw_line({.from = desc.points[i-1], .to = desc.points[i], .color = eff_line_color,
                .thickness = desc.line_thickness, .foreground = desc.foreground});
        }
    }
}

void draw_polyline(const PolylineDesc& desc) noexcept {
    const size_t n = desc.points.size();
    if (n < 2u || desc.color.a == 0u) {
        return;
    }
    Context* ctx = current_context();
    if (!ctx || n > (ctx->frame_arena.remaining() / sizeof(Vec2))) {
        return;
    }
    Vec2* copy = ctx->frame_arena.alloc_uninit<Vec2>(n);
    if (!copy) {
        return;
    }
    std::memcpy(copy, desc.points.data(), n * sizeof(Vec2));
    if (DrawCmdNode* node = append_open_box_draw_cmd(ctx, desc.foreground)) {
        node->kind = DrawCmdKind::Polyline;
        node->poly = DrawCmdPolyPayload{
            .points = copy,
            .count = static_cast<uint32_t>(n),
            .closed = desc.closed,
            .color = desc.color,
            .thickness = desc.thickness,
            .glow_color = desc.glow_color,
            .glow_px = desc.glow_px,
        };
    }
}

void draw_bezier(const BezierDesc& desc) noexcept {
    if (desc.color_start.a == 0u && desc.color_end.a == 0u && desc.glow_color.a == 0u) {
        return;
    }
    Context* ctx = current_context();
    if (DrawCmdNode* node = append_open_box_draw_cmd(ctx, desc.foreground)) {
        node->kind = DrawCmdKind::Bezier;
        node->bezier = desc;
    }
}

void draw_ring(const RingDesc& desc) noexcept {
    if (desc.radius <= 0.0f) {
        return;
    }
    Context* ctx = current_context();
    if (DrawCmdNode* node = append_open_box_draw_cmd(ctx, desc.foreground)) {
        node->kind = DrawCmdKind::Ring;
        node->ring = desc;
    }
}

void draw_icon(const IconDrawDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !std::isfinite(desc.angle_rad) || desc.icon_id >= kMaxVectorIcons || desc.rect.w <= 0.0f || desc.rect.h <= 0.0f) {
        return;
    }
    IconDrawDesc eff = desc;
    eff.color = resolve_theme_color(eff.color, active_theme(ctx).text_primary);
    if (eff.color.a == 0u) {
        return;
    }
    if (DrawCmdNode* node = append_open_box_draw_cmd(ctx, eff.foreground)) {
        node->kind = DrawCmdKind::Icon;
        node->icon = eff;
        if (std::fabs(node->icon.angle_rad) <= 1e-5f) node->icon.angle_rad = 0;
    }
}

FontMetrics font_metrics(uint16_t font_id, float size) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->atlas.font_valid || !std::isfinite(size) || size <= 0) return {};
    const uint16_t slot = resolve_font_slot_idx(&ctx->atlas,font_id);
    if (slot == kInvalidFont) return {};
    const auto& font = ctx->atlas.slots[slot];
    return {font.ascent_em*size,std::abs(font.descent_em)*size,font.line_height_em*size};
}

Vec2 measure_text(Str text, uint16_t font_id, float size, float letter_spacing) noexcept {
    Context* ctx = current_context();
    if (!ctx || size <= 0.0f) {
        return Vec2{};
    }
    return measure_multiline_text_metrics(ctx, text, font_id, size, letter_spacing, nullptr, nullptr);
}

void draw_after_layout(LayoutDrawFn callback, void* user_data, bool foreground) noexcept {
    Context* ctx=current_context();
    if (!ctx || !ctx->in_frame || !callback || ctx->layout_draw_target) return;
    Theme* cb_theme = ctx->frame_arena.alloc<Theme>();
    if (!cb_theme) return;
    *cb_theme = active_theme(ctx);
    if (DrawCmdNode* node=append_open_box_draw_cmd(ctx,foreground)) {
        node->kind = DrawCmdKind::Callback;
        node->cb = DrawCmdCallbackPayload{callback, user_data, cb_theme};
    }
}

void draw_text(const TextDrawDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !desc.text.chars || desc.text.length == 0u || !std::isfinite(desc.size) || desc.size <= 0.0f || !std::isfinite(desc.max_width_px) || desc.max_width_px < 0) {
        return;
    }
    TextDrawDesc copied = desc;
    copied.color = resolve_theme_color(desc.color, active_theme(ctx).text_primary);
    if (copied.has_wipe) {
        copied.color_end = resolve_theme_color(desc.color_end, active_theme(ctx).text_primary);
    }
    copied.shadow_color = resolve_theme_color(desc.shadow_color, active_theme(ctx).shadow);
    if (!std::isfinite(copied.shadow_offset.x)) copied.shadow_offset.x = 0.0f;
    if (!std::isfinite(copied.shadow_offset.y)) copied.shadow_offset.y = 0.0f;
    if (!std::isfinite(copied.shadow_blur) || copied.shadow_blur < 0.0f) copied.shadow_blur = 0.0f;
    if (copied.color.a == 0u && (!copied.has_wipe || copied.color_end.a == 0u) && copied.shadow_color.a == 0u) {
        return;
    }
    if (DrawCmdNode* node = append_open_box_draw_cmd(ctx, copied.foreground)) {
        if (!desc.text.is_static) {
            copied.text = ctx->frame_arena.push_str(desc.text.chars, desc.text.length);
            if (!copied.text.chars) {
                copied.text = {};
            }
        }
        node->kind = DrawCmdKind::Text;
        node->text_payload = DrawCmdTextPayload{.cmd = copied};
    }
}

void draw_image(const ImageDrawDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || (!desc.texture && desc.sprite_id == kInvalidSprite) || desc.tint.a == 0u || desc.rect.w <= 0.0f || desc.rect.h <= 0.0f) {
        return;
    }
    if (DrawCmdNode* node = append_open_box_draw_cmd(ctx, desc.foreground)) {
        node->kind = DrawCmdKind::Image;
        node->image_cmd = desc;
    }
}

SDL_HitTestResult SDLCALL etch_sdl_hit_test(SDL_Window* win, const SDL_Point* area, void* data) noexcept {
    const Context* ctx = static_cast<const Context*>(data);
    if (!ctx || !area) {
        return SDL_HITTEST_NORMAL;
    }
    const Vec2 logical_pos = window_to_logical_pos_for(
        ctx, win, static_cast<float>(area->x), static_cast<float>(area->y));
    switch (evaluate_window_hit(ctx, logical_pos, win)) {
        case WindowHit::Caption:           return SDL_HITTEST_DRAGGABLE;
        case WindowHit::ResizeTopLeft:     return SDL_HITTEST_RESIZE_TOPLEFT;
        case WindowHit::ResizeTop:         return SDL_HITTEST_RESIZE_TOP;
        case WindowHit::ResizeTopRight:    return SDL_HITTEST_RESIZE_TOPRIGHT;
        case WindowHit::ResizeRight:       return SDL_HITTEST_RESIZE_RIGHT;
        case WindowHit::ResizeBottomRight: return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        case WindowHit::ResizeBottom:      return SDL_HITTEST_RESIZE_BOTTOM;
        case WindowHit::ResizeBottomLeft:  return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        case WindowHit::ResizeLeft:        return SDL_HITTEST_RESIZE_LEFT;
        case WindowHit::Maximize:
        case WindowHit::Normal:
        case WindowHit::Client:
        default:                           return SDL_HITTEST_NORMAL;
    }
}

void set_window_chrome(const WindowChromeDesc& desc, SDL_Window* window) noexcept {
    Context* ctx = current_context();
    if (!ctx) {
        return;
    }
    WindowChromeDesc clean = desc;
    if (clean.border_thickness < 0.0f) {
        clean.border_thickness = 0.0f;
    }
    if (clean.corner_size < clean.border_thickness) {
        clean.corner_size = clean.border_thickness;
    }
    if (!std::isfinite(clean.traffic_light_inset_px) || clean.traffic_light_inset_px < 0.0f) {
        clean.traffic_light_inset_px = 0.0f;
    }
    WindowUiState* ui = window_ui(ctx, window ? window : ctx->active_window);
    if (!ui) return;
    ui->chrome_desc = clean;
    ui->redraw_budget = 2;
    if (!ctx->headless) {
        for (uint32_t i = 0u; i < ctx->claimed_window_cap; ++i) {
            if (!ctx->claimed_windows[i]) continue;
            ClaimedWindowSlot& slot = *ctx->claimed_windows[i];
            if (!slot.window || &slot.ui != ui) {
                continue;
            }
            if (clean.enabled) {
                if (SDL_SetWindowHitTest(slot.window, etch_sdl_hit_test, ctx)) {
                    slot.hit_test_attached = true;
                }
            } else if (slot.hit_test_attached) {
                SDL_SetWindowHitTest(slot.window, nullptr, nullptr);
                slot.hit_test_attached = false;
            }
            apply_platform_window_chrome(ctx, &slot);
        }
    }
    request_redraw(ctx);
}

void window_drag_region(Rect rect_logical) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame) {
        return;
    }
    push_chrome_rect(
        ctx->active_ui->pending_chrome_drag_rects,
        &ctx->active_ui->pending_chrome_drag_count,
        kMaxChromeDragRects,
        rect_logical);
}

void window_client_region(Rect rect_logical) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame) {
        return;
    }
    push_chrome_rect(
        ctx->active_ui->pending_chrome_client_rects,
        &ctx->active_ui->pending_chrome_client_count,
        kMaxChromeClientRects,
        rect_logical);
}

void window_maximize_button_region(Rect rect_logical) noexcept {
    Context* ctx=current_context();
    if (!ctx || !ctx->in_frame || !std::isfinite(rect_logical.x) || !std::isfinite(rect_logical.y) ||
        !std::isfinite(rect_logical.w) || !std::isfinite(rect_logical.h) || rect_logical.w<0 || rect_logical.h<0) return;
    ctx->active_ui->pending_chrome_maximize_rect=rect_logical;ctx->active_ui->pending_chrome_maximize_id=0;
}
void window_maximize_button_region(uint32_t element_id) noexcept {
    Context* ctx=current_context();
    if (!ctx || !ctx->in_frame) return;
    ctx->active_ui->pending_chrome_maximize_id=element_id;ctx->active_ui->pending_chrome_maximize_rect={};
}

WindowHit evaluate_window_hit(const Context* ctx, Vec2 logical_pos, SDL_Window* window) noexcept {
    const WindowUiState* ui = ctx ? window_ui(ctx, window ? window : ctx->active_window) : nullptr;
    if (!ui || !ui->chrome_desc.enabled) {
        return WindowHit::Normal;
    }
    SDL_Window* target = window ? window : ctx->active_window;
    float w = 0.0f;
    float h = 0.0f;
    bool maximized = false;
    bool fullscreen = false;
    if (target && (!ctx->headless || window != nullptr)) {
        int iw = 0;
        int ih = 0;
        SDL_GetWindowSize(target, &iw, &ih);
        const float density = SDL_GetWindowPixelDensity(target);
        const float dpi = (ui->dpi_scale > 0.0f) ? ui->dpi_scale : 1.0f;
        const float scale = ((density > 0.0f) ? density : 1.0f) / dpi;
        const Vec2 win_sz = {static_cast<float>(iw) * scale, static_cast<float>(ih) * scale};
        w = win_sz.x;
        h = win_sz.y;
        const SDL_WindowFlags wflags = SDL_GetWindowFlags(target);
        maximized = ((wflags & SDL_WINDOW_MAXIMIZED) != 0u) || win32_is_zoomed(target);
        fullscreen = (wflags & SDL_WINDOW_FULLSCREEN) != 0u;
    } else {
        const float dpi = (ui->dpi_scale > 0.0f) ? ui->dpi_scale : 1.0f;
        w = static_cast<float>(ui->viewport_w) / dpi;
        h = static_cast<float>(ui->viewport_h) / dpi;
    }
    if (w <= 0.0f || h <= 0.0f ||
        logical_pos.x < 0.0f || logical_pos.y < 0.0f ||
        logical_pos.x >= w || logical_pos.y >= h)
    {
        return WindowHit::Normal;
    }
    if (fullscreen) {
        return WindowHit::Client;
    }

    if (ui->chrome_desc.resizable && !maximized) {
        const float b = ui->chrome_desc.border_thickness;
        const float c = ui->chrome_desc.corner_size;
        const bool left_edge = logical_pos.x < b;
        const bool right_edge = logical_pos.x >= (w - b);
        const bool top_edge = logical_pos.y < b;
        const bool bottom_edge = logical_pos.y >= (h - b);
        const bool left_corner = logical_pos.x < c;
        const bool right_corner = logical_pos.x >= (w - c);
        const bool top_corner = logical_pos.y < c;
        const bool bottom_corner = logical_pos.y >= (h - c);
        if (left_edge || right_edge || top_edge || bottom_edge) {
            if (top_corner && left_corner) return WindowHit::ResizeTopLeft;
            if (top_corner && right_corner) return WindowHit::ResizeTopRight;
            if (bottom_corner && left_corner) return WindowHit::ResizeBottomLeft;
            if (bottom_corner && right_corner) return WindowHit::ResizeBottomRight;
            if (top_edge) return WindowHit::ResizeTop;
            if (bottom_edge) return WindowHit::ResizeBottom;
            if (left_edge) return WindowHit::ResizeLeft;
            if (right_edge) return WindowHit::ResizeRight;
        }
    }

    if (ui->chrome_desc.resizable && ui->chrome_maximize_rect.contains(logical_pos)) return WindowHit::Maximize;
    for (uint32_t i = 0u; i < ui->chrome_client_count; ++i) {
        if (ui->chrome_client_rects[i].contains(logical_pos)) {
            return WindowHit::Client;
        }
    }
    for (uint32_t i = 0u; i < ui->chrome_drag_count; ++i) {
        if (ui->chrome_drag_rects[i].contains(logical_pos)) {
            return WindowHit::Caption;
        }
    }
    return WindowHit::Client;
}

void minimize_window(SDL_Window* window) noexcept {
    Context* ctx = current_context();
    SDL_Window* target = window ? window : (ctx ? ctx->active_window : nullptr);
    if (target && (!ctx || !ctx->headless)) {
        SDL_MinimizeWindow(target);
        if (ctx) {
            request_redraw(ctx);
        }
    }
}

void maximize_window(SDL_Window* window) noexcept {
    Context* ctx = current_context();
    SDL_Window* target = window ? window : (ctx ? ctx->active_window : nullptr);
    if (target && (!ctx || !ctx->headless)) {
        SDL_MaximizeWindow(target);
        win32_maximize_window(target);
        if (ctx) {
            request_redraw(ctx);
        }
    }
}

void restore_window(SDL_Window* window) noexcept {
    Context* ctx = current_context();
    SDL_Window* target = window ? window : (ctx ? ctx->active_window : nullptr);
    if (target && (!ctx || !ctx->headless)) {
        SDL_RestoreWindow(target);
        win32_restore_window(target);
        if (ctx) {
            request_redraw(ctx);
        }
    }
}

bool is_window_maximized(const Context* ctx_arg, SDL_Window* window) noexcept {
    const Context* ctx = ctx_arg ? ctx_arg : current_context();
    SDL_Window* target = window ? window : (ctx ? ctx->active_window : nullptr);
    if (!target || (ctx && ctx->headless)) {
        return false;
    }
    return ((SDL_GetWindowFlags(target) & SDL_WINDOW_MAXIMIZED) != 0u) || win32_is_zoomed(target);
}

bool is_window_minimized(const Context* ctx_arg, SDL_Window* window) noexcept {
    const Context* ctx = ctx_arg ? ctx_arg : current_context();
    SDL_Window* target = window ? window : (ctx ? (ctx->active_window ? ctx->active_window : ctx->window) : nullptr);
    if (!target || (ctx && ctx->headless)) {
        return false;
    }
    return (SDL_GetWindowFlags(target) & SDL_WINDOW_MINIMIZED) != 0u;
}

bool begin_drag_payload(
    uint32_t source_id,
    uint32_t kind_hash,
    Str label,
    const void* data,
    uint32_t size,
    uint64_t u64,
    uint32_t u32) noexcept
{
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || !ctx->active_ui->prev_pointer_down || kind_hash == 0u ||
        size > (1u << 20) || (size > 0u && !data))
    {
        return false;
    }

    auto& ui = *ctx->active_ui;
    if (size > 64u && size > ui.drag_heap_cap) {
        uint32_t cap = ui.drag_heap_cap ? ui.drag_heap_cap : 128u; while(cap < size) cap *= 2u;
        void* p = std::realloc(ui.drag_heap, cap); if(!p)return false;
        ui.drag_heap=static_cast<uint8_t*>(p);ui.drag_heap_cap=cap;
    }
    ctx->active_ui->drag_source_id = source_id;
    ctx->active_ui->drag_kind_hash = kind_hash;
    ctx->active_ui->drag_u64 = u64;
    ctx->active_ui->drag_u32 = u32;
    ctx->active_ui->drag_size = size;
    if (size > 0u) {
        std::memcpy((ctx->active_ui->drag_size > 64u ? ctx->active_ui->drag_heap : ctx->active_ui->drag_inline), data, size);
    }

    uint32_t copy_len = 0u;
    if (label.chars && label.length > 0u) {
        const uint32_t raw_cap = (label.length < (kMaxDragLabelBytes - 1u))
            ? label.length
            : (kMaxDragLabelBytes - 1u);
        copy_len = utf8_clamp_boundary(label.chars, label.length, raw_cap);
        if (copy_len > 0u) {
            std::memcpy(ctx->active_ui->drag_label, label.chars, copy_len);
        }
    }
    ctx->active_ui->drag_label[copy_len] = '\0';
    ctx->active_ui->drag_label_len = copy_len;

    ctx->active_ui->drag_active = true;
    ctx->active_ui->drag_drop_pending = false;
    ctx->active_ui->drag_accepted_this_frame = false;
    request_redraw(ctx);
    return true;
}

bool is_dragging_payload(uint32_t kind_hash) noexcept {
    const Context* ctx = current_context();
    return ctx != nullptr && ctx->active_ui->drag_active && (kind_hash == 0u || ctx->active_ui->drag_kind_hash == kind_hash);
}

bool peek_drag_payload(DragPayloadView* out_view, uint32_t kind_hash) noexcept {
    const Context* ctx = current_context();
    if (!ctx || (!ctx->active_ui->drag_active && !(ctx->active_ui->drag_drop_pending && !ctx->active_ui->drag_accepted_this_frame))) {
        return false;
    }
    if (kind_hash != 0u && ctx->active_ui->drag_kind_hash != kind_hash) {
        return false;
    }
    if (out_view) {
        *out_view = DragPayloadView{
            .kind_hash = ctx->active_ui->drag_kind_hash,
            .label = (ctx->active_ui->drag_label_len > 0u) ? Str{ ctx->active_ui->drag_label, ctx->active_ui->drag_label_len, false } : Str{},
            .u64 = ctx->active_ui->drag_u64,
            .u32 = ctx->active_ui->drag_u32,
            .data = (ctx->active_ui->drag_size > 0u) ? (ctx->active_ui->drag_size > 64u ? ctx->active_ui->drag_heap : ctx->active_ui->drag_inline) : nullptr,
            .size = ctx->active_ui->drag_size,
        };
    }
    return true;
}

bool accept_drag_payload(uint32_t target_id, uint32_t kind_hash, DragPayloadView* out_view) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->active_ui->drag_drop_pending || ctx->active_ui->drag_accepted_this_frame || kind_hash == 0u) {
        return false;
    }
    if (ctx->active_ui->drag_kind_hash != kind_hash) {
        return false;
    }
    if (target_id != 0u) {
        const bool over_target = hovered(target_id) || element_bounds(target_id).contains(ctx->active_ui->pointer_pos);
        if (!over_target) {
            return false;
        }
    }
    if (out_view) {
        *out_view = DragPayloadView{
            .kind_hash = ctx->active_ui->drag_kind_hash,
            .label = (ctx->active_ui->drag_label_len > 0u) ? Str{ ctx->active_ui->drag_label, ctx->active_ui->drag_label_len, false } : Str{},
            .u64 = ctx->active_ui->drag_u64,
            .u32 = ctx->active_ui->drag_u32,
            .data = (ctx->active_ui->drag_size > 0u) ? (ctx->active_ui->drag_size > 64u ? ctx->active_ui->drag_heap : ctx->active_ui->drag_inline) : nullptr,
            .size = ctx->active_ui->drag_size,
        };
    }
    ctx->active_ui->drag_accepted_this_frame = true;
    return true;
}

bool set_clipboard_text(const char* utf8_text) noexcept {
    const char* src = utf8_text ? utf8_text : "";
    Context* ctx = current_context();
    bool fallback_ok = true;
    if (ctx) {
        const size_t len = std::strlen(src);
        const uint32_t need = static_cast<uint32_t>(len + 1u);
        if (need > ctx->clipboard_fallback_cap) {
            uint32_t new_cap = (ctx->clipboard_fallback_cap > 0u) ? ctx->clipboard_fallback_cap : 128u;
            while (new_cap < need) {
                new_cap *= 2u;
            }
            char* new_buf = static_cast<char*>(std::realloc(ctx->clipboard_fallback, new_cap));
            if (new_buf) {
                ctx->clipboard_fallback = new_buf;
                ctx->clipboard_fallback_cap = new_cap;
            } else {
                fallback_ok = false;
            }
        }
        if (ctx->clipboard_fallback && ctx->clipboard_fallback_cap >= need) {
            if (len > 0u) {
                std::memcpy(ctx->clipboard_fallback, src, len);
            }
            ctx->clipboard_fallback[len] = '\0';
        }
    }
    if (!ctx || !ctx->headless) {
        return SDL_SetClipboardText(src) && fallback_ok;
    }
    return fallback_ok;
}

Str get_clipboard_text(Arena* arena) noexcept {
    Context* ctx = current_context();
    Arena* dst_arena = arena ? arena : (ctx ? &ctx->frame_arena : nullptr);
    if (!dst_arena) {
        return Str{};
    }
    if (ctx && ctx->headless) {
        if (!ctx->clipboard_fallback || ctx->clipboard_fallback[0] == '\0') {
            return Str{};
        }
        return dst_arena->push_str(ctx->clipboard_fallback, std::strlen(ctx->clipboard_fallback));
    }
    char* sdl_clip = SDL_GetClipboardText();
    if (sdl_clip) {
        const size_t len = std::strlen(sdl_clip);
        const Str out = (len > 0u) ? dst_arena->push_str(sdl_clip, len) : Str{};
        SDL_free(sdl_clip);
        return out;
    }
    if (ctx && ctx->clipboard_fallback && ctx->clipboard_fallback[0] != '\0') {
        return dst_arena->push_str(ctx->clipboard_fallback, std::strlen(ctx->clipboard_fallback));
    }
    return Str{};
}

ClipboardImageData get_clipboard_image(Arena* arena) noexcept {
    Context* ctx = current_context();
    Arena* dst_arena = arena ? arena : (ctx ? &ctx->frame_arena : nullptr);
    if (!dst_arena || (ctx && ctx->headless)) {
        return ClipboardImageData{};
    }
    static constexpr const char* kMimes[] = { "image/png", "image/jpeg", "image/webp", "image/bmp" };
    for (const char* mime : kMimes) {
        if (!SDL_HasClipboardData(mime)) {
            continue;
        }
        size_t sz = 0;
        void* raw = SDL_GetClipboardData(mime, &sz);
        if (!raw) {
            continue;
        }
        ClipboardImageData out = {};
        if (sz > 0u && sz <= static_cast<size_t>(UINT32_MAX) && sz <= dst_arena->remaining()) {
            uint8_t* copy = dst_arena->alloc_uninit<uint8_t>(sz);
            if (copy) {
                std::memcpy(copy, raw, sz);
                out.bytes = copy;
                out.size_bytes = static_cast<uint32_t>(sz);
                out.mime_type = str_cstr(mime);
            }
        }
        SDL_free(raw);
        if (out.bytes) {
            return out;
        }
    }
    return ClipboardImageData{};
}

void set_text_selection(uint32_t element_id, uint32_t anchor_byte, uint32_t cursor_byte) noexcept {
    Context* ctx = current_context();
    if (!ctx || element_id == 0u) {
        return;
    }
    WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, element_id, ctx->active_ui->frame_index);
    WidgetCold* c = ensure_widget_cold(ws);
    if (!c) {
        return;
    }
    c->edit_initialized = true;
    const bool valid_backing = c->sel_text_ptr && (c->sel_text_static || c->sel_text_epoch == ctx->arena_epoch);
    c->edit_sel_anchor = valid_backing ? utf8_clamp_boundary(c->sel_text_ptr,c->sel_text_len,anchor_byte) : anchor_byte;
    c->edit_cursor = valid_backing ? utf8_clamp_boundary(c->sel_text_ptr,c->sel_text_len,cursor_byte) : cursor_byte;
    c->edit_preferred_x = -1.0f;
    request_redraw(ctx);
}

bool has_text_selection(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (!ctx) return false;
    auto* ws = state_map_find(&ctx->active_ui->states,element_id ? element_id : ctx->active_ui->focus_id);
    const WidgetCold* c = ws ? ws->cold : nullptr;
    if (c && c->edit_text_buffer && c->sel_text_epoch == ctx->arena_epoch) {
        const uint32_t n=text_buffer_byte_length(c->edit_text_buffer);
        return (c->edit_cursor<n?c->edit_cursor:n) != (c->edit_sel_anchor<n?c->edit_sel_anchor:n);
    }
    if (!c || !c->sel_text_ptr || (!c->sel_text_static && c->sel_text_epoch != ctx->arena_epoch)) return false;
    return utf8_clamp_boundary(c->sel_text_ptr,c->sel_text_len,c->edit_cursor) != utf8_clamp_boundary(c->sel_text_ptr,c->sel_text_len,c->edit_sel_anchor);
}
Str selected_text(uint32_t element_id, Arena* arena) noexcept {
    Context* ctx = current_context();
    if (!ctx || !has_text_selection(element_id)) return {};
    auto* ws = state_map_find(&ctx->active_ui->states,element_id ? element_id : ctx->active_ui->focus_id);
    const WidgetCold* cold = ws ? ws->cold : nullptr;
    if (!cold) return {};
    if(cold->edit_text_buffer && cold->sel_text_epoch == ctx->arena_epoch) {
        const uint32_t nbytes=text_buffer_byte_length(cold->edit_text_buffer);
        const uint32_t c=cold->edit_cursor<nbytes?cold->edit_cursor:nbytes,a=cold->edit_sel_anchor<nbytes?cold->edit_sel_anchor:nbytes;
        const uint32_t lo=c<a?c:a,hi=c>a?c:a;
        Arena* scratch=arena?arena:&ctx->frame_arena;
        char* p=scratch->alloc<char>(size_t(hi-lo)+1);if(!p)return {};
        const uint32_t n=text_buffer_copy_range(cold->edit_text_buffer,lo,hi,p,hi-lo);p[n]=0;return {p,n};
    }
    const uint32_t a = utf8_clamp_boundary(cold->sel_text_ptr,cold->sel_text_len,cold->edit_sel_anchor);
    const uint32_t b = utf8_clamp_boundary(cold->sel_text_ptr,cold->sel_text_len,cold->edit_cursor);
    const uint32_t lo = a < b ? a : b, hi = a > b ? a : b;
    return (arena ? arena : &ctx->frame_arena)->push_str(cold->sel_text_ptr+lo,hi-lo);
}
void clear_text_selection(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (!ctx) return;
    if (auto* ws = state_map_find(&ctx->active_ui->states,element_id ? element_id : ctx->active_ui->focus_id); ws && ws->cold) {
        ws->cold->edit_sel_anchor = ws->cold->edit_cursor; request_redraw(ctx);
    }
}

Rect ime_cursor_rect(const Context* ctx_arg) noexcept {
    const Context* ctx = ctx_arg ? ctx_arg : current_context();
    return ctx ? ctx->active_ui->ime_cursor_logical : Rect{};
}

void request_text_input(uint32_t element_id, Rect ime_cursor_rect) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || element_id == 0u || ctx->active_ui->focus_id != element_id) {
        return;
    }
    ctx->active_ui->ime_focused_id = element_id;
    ctx->active_ui->ime_focused_password = false;
    const bool valid_rect = std::isfinite(ime_cursor_rect.x) && std::isfinite(ime_cursor_rect.y) &&
                            std::isfinite(ime_cursor_rect.w) && std::isfinite(ime_cursor_rect.h) &&
                            ime_cursor_rect.w > 0.0f && ime_cursor_rect.h > 0.0f;
    ctx->active_ui->ime_cursor_logical = valid_rect ? ime_cursor_rect : Rect{};
    ctx->active_ui->ime_cursor_offset_win = 0;
}

Str typed_text(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || ctx->active_ui->frame_text_utf8_len == 0u) {
        return {};
    }
    if (element_id != 0u && ctx->active_ui->focus_id != element_id) {
        return {};
    }
    return ctx->frame_arena.push_str(ctx->active_ui->frame_text_utf8, ctx->active_ui->frame_text_utf8_len);
}

uint32_t text_input_menu(const TextInputDesc& d, uint32_t length, bool selection) noexcept {
    if (!d.context_menu || !d.id) return 0;
    const uint32_t popup=id_index(d.id,0xED1700u);
    if (right_clicked(d.id)) { request_focus(d.id);open_popup(popup); }
    uint32_t key=0;
    if (begin_popup({.id=popup,.trigger_id=d.id})) {
        if (menu_item({.id=id_index(popup,1),.label=str_lit("Cut"),.shortcut_hint=str_lit("Ctrl+X"),.disabled=d.read_only || d.password || !selection})) key=SDLK_X;
        if (menu_item({.id=id_index(popup,2),.label=str_lit("Copy"),.shortcut_hint=str_lit("Ctrl+C"),.disabled=d.password || !selection})) key=SDLK_C;
        if (menu_item({.id=id_index(popup,3),.label=str_lit("Paste"),.shortcut_hint=str_lit("Ctrl+V"),.disabled=d.read_only})) key=SDLK_V;
        if (menu_item({.id=id_index(popup,4),.label=str_lit("Select all"),.shortcut_hint=str_lit("Ctrl+A"),.disabled=length==0})) key=SDLK_A;
        end_popup();
    }
    if (key) request_focus(d.id);
    return key;
}

using ReadInputHighlight = TextInputHighlight (*)(const void*, size_t) noexcept;

static TextInputResult text_input_impl(const TextInputDesc& desc_in, const void* highlights,
    size_t highlight_count, ReadInputHighlight read_highlight) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame) {
        return TextInputResult{};
    }
    const Theme& thm = active_theme(ctx);
    TextInputDesc desc = desc_in;
    desc.text_color = resolve_theme_color(desc.text_color, desc.disabled ? thm.text_disabled : thm.text_body);
    desc.placeholder_color = resolve_theme_color(desc.placeholder_color, thm.text_disabled);
    desc.ghost_color = resolve_theme_color(desc.ghost_color, thm.text_disabled);
    desc.selection_color = resolve_theme_color(desc.selection_color, thm.selection);
    desc.caret_color = resolve_theme_color(desc.caret_color, thm.accent);
    desc.composition_color = resolve_theme_color(desc.composition_color, thm.warning);
    desc.background = resolve_theme_color(desc.background, desc.disabled ? thm.surface_disabled : thm.bg_input);
    desc.border_color = resolve_theme_color(desc.border_color, thm.border_input);
    desc.focus_border_color = resolve_theme_color(desc.focus_border_color, thm.accent);
    desc.hover_border_color = resolve_theme_color(
        desc.hover_border_color,
        Color::lerp_oklab(desc.border_color, desc.focus_border_color, 0.35f));
    desc.leading_icon_color = resolve_theme_color(
        desc.leading_icon_color,
        desc.disabled ? thm.text_disabled : thm.text_muted);
    desc.focus_glow_blur = std::isfinite(desc.focus_glow_blur) ? std::fmax(0.0f, desc.focus_glow_blur) : 10.0f;
    desc.radius = resolve_theme_corners(desc.radius, Corners::all(thm.radius_input_px));

    WidgetState* ws = nullptr;
    WidgetCold* cold = nullptr;
    if (desc.id != 0u) {
        ws = state_map_get_or_insert(&ctx->active_ui->states, desc.id, ctx->active_ui->frame_index);
        cold = ensure_widget_cold(ws);
        if (ws) {
            ws->is_user_element = true;
            ws->is_focusable = !desc.disabled;
            ws->focus_scope = ctx->active_ui->modal_scope && focus_scope_contains(*ctx->active_ui, ctx->active_ui->modal_scope) ? ctx->active_ui->modal_scope : ctx->active_ui->focus_scope;
            ws->is_scroll_x = false;
            ws->is_scroll_y = false;
            if (cold) {
                cold->owner_canvas_id = (ctx->active_ui->canvas_depth > 0u)
                    ? ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth - 1u].id
                    : 0u;
            }
        }
        if (desc.disabled) {
            if (ctx->active_ui->focus_id == desc.id) {
                ctx->active_ui->focus_id = 0u;
                clear_ime_composition(ctx);
            }
        } else if (reserve_focus(ctx->active_ui)) {
            ctx->active_ui->focus_order[ctx->active_ui->focus_count++] = desc.id;
            if (ctx->active_ui->modal_needs_focus && focus_scope_contains(*ctx->active_ui, ctx->active_ui->modal_scope)) {
                ctx->active_ui->focus_id = desc.id;
                ctx->active_ui->modal_needs_focus = false;
                clear_ime_composition(ctx);
            }
        }
    }

    char* buf = desc.buffer;
    const uint32_t cap = desc.capacity;
    uint32_t len = 0u;
    const bool writable = (buf != nullptr && cap > 1u && !desc.read_only && !desc.disabled);
    if (ws) {
        ws->is_text_input_writable = writable;
        ws->is_multiline_input = desc.multiline;
        ws->suppress_popup_keys = desc.suppress_popup_keys;
        if (cold) {
            cold->edit_text_buffer = nullptr;
            if (ws->pressed_this_frame || ctx->active_ui->frame_text_op_count || ctx->active_ui->ime_comp_len) cold->edit_reveal_caret = true;
        }
    }
    if (buf && cap > 0u) {
        while (len < (desc.read_only ? cap : cap - 1u) && buf[len] != '\0') {
            ++len;
        }
        if (!desc.read_only) buf[len] = '\0';
    }

    uint32_t cursor = len;
    uint32_t anchor = len;
    if (cold) {
        if (!cold->edit_initialized) {
            cold->edit_initialized = true;
            cold->edit_cursor = len;
            cold->edit_sel_anchor = len;
            cold->edit_preferred_x = -1.0f;
        }
        cursor = utf8_clamp_boundary(buf, len, cold->edit_cursor);
        anchor = utf8_clamp_boundary(buf, len, cold->edit_sel_anchor);
    }

    TextUndoSlot* undo_slot = nullptr;
    if (writable && desc.id != 0u) {
        undo_slot = undo_acquire_slot(ctx, desc.id, ctx->active_ui->frame_index);
        if (undo_slot && len <= 262144u) {
            const uint32_t cur_hash = (len > 0u) ? fnv1a_32_step(2166136261u, buf, len) : 2166136261u;
            if (undo_slot->count == 0u) {
                undo_push_snapshot(undo_slot, buf, len, cursor, anchor, ctx->active_ui->elapsed_seconds, 0u);
            } else {
                const TextUndoRecord& cur_rec = undo_slot->entries[(undo_slot->head + undo_slot->cur) % undo_slot->entries_cap];
                if (cur_rec.length != len || cur_rec.hash != cur_hash) {
                    undo_reset_slot(undo_slot);
                    undo_push_snapshot(undo_slot, buf, len, cursor, anchor, ctx->active_ui->elapsed_seconds, 0u);
                }
            }
        } else if (undo_slot) {
            undo_reset_slot(undo_slot);
        }
    }

    const uint32_t clear_id=id_index(desc.id,0xED1701u);
    const bool show_clear=desc.clear_button && writable && len>0;
    uint32_t menu_key=desc.disabled ? 0u : text_input_menu(desc,len,cursor!=anchor);
    ws=desc.id ? state_map_find(&ctx->active_ui->states,desc.id) : nullptr;
    cold = ensure_widget_cold(ws);
    if (show_clear && (clicked(clear_id) || a11y_take(clear_id,A11yAction::Press))) { request_focus(desc.id);anchor=0;cursor=len;menu_key=SDLK_DELETE; }
    const float orig_pad_left = desc.padding.left;
    const bool has_leading_icon = (desc.leading_icon_id != kInvalidIcon && is_icon_loaded(ctx, desc.leading_icon_id));
    if (has_leading_icon) {
        const float icon_sz = std::fmax(12.0f, std::fmin((desc.font_size_px > 0.0f ? desc.font_size_px : 14.0f), 18.0f));
        desc.padding.left += icon_sz + 8.0f;
    }
    if (show_clear) desc.padding.right += 24;
    bool is_focused = !desc.disabled && (desc.id != 0u && ctx->active_ui->focus_id == desc.id &&
        (!ctx->active_ui->modal_scope || focus_scope_contains(*ctx->active_ui, ctx->active_ui->modal_scope)));
    const bool is_hovered = !desc.disabled && (desc.id != 0u && pointer_over_id(ctx->active_ui, desc.id));
    if (!desc.disabled && (is_hovered || (desc.id != 0u && ctx->active_ui->active_id == desc.id))) {
        set_cursor(Cursor::IBeam);
    }
    if (ws) {
        if (desc.disabled || is_focused) {
            ws->hover_t = 0.0f;
            ws->hover_vel = 0.0f;
        } else {
            advance_motion(ctx, &ws->hover_t, &ws->hover_vel, is_hovered ? 1.0f : 0.0f, SpringSpec::snappy());
        }
    }

    if (is_focused && !desc.read_only) {
        ctx->active_ui->ime_focused_id = desc.id;
        ctx->active_ui->ime_focused_password = desc.password;
        if (desc.password && ctx->active_ui->ime_comp_len > 0u) {
            clear_ime_composition(ctx);
        }
    }

    const float active_wrap_w = (desc.multiline && desc.word_wrap)
        ? ((cold && cold->edit_wrap_width > 0.0f)
            ? cold->edit_wrap_width
            : ((ws && ws->last_box_logical.w > 0.0f)
                ? text_input_wrap_width(desc, compute_text_inner_geom(ctx, ws->last_box_logical, desc))
                : ((desc.width.kind == SizingKind::Fixed && desc.width.value > 0.0f)
                    ? text_input_wrap_width(desc, compute_text_inner_geom(ctx, Rect{ 0.0f, 0.0f, desc.width.value, 100.0f }, desc))
                    : 0.0f)))
        : 0.0f;
    if (cold && active_wrap_w <= 0.0f) {
        cold->edit_wrap_affinity = false;
    }

    const auto find_atomic_range = [&](uint32_t pos, bool include_start, bool include_end, uint32_t* out_s, uint32_t* out_e) noexcept -> bool {
        if (!highlights || highlight_count == 0u || len == 0u) return false;
        for (size_t hi = 0u; hi < highlight_count; ++hi) {
            const TextInputHighlight hr = read_highlight(highlights, hi);
            if (!hr.atomic || hr.byte_length == 0u || hr.byte_offset >= len) continue;
            const uint32_t s = utf8_clamp_boundary(buf, len, hr.byte_offset);
            uint32_t e = hr.byte_offset + std::min(hr.byte_length, len - hr.byte_offset);
            const uint32_t eb = utf8_clamp_boundary(buf, len, e);
            if (eb < e) e = utf8_next_boundary(buf, len, eb);
            if (e <= s) continue;
            const bool inside = (pos > s && pos < e) || (include_start && pos == s) || (include_end && pos == e);
            if (inside) {
                if (out_s) *out_s = s;
                if (out_e) *out_e = e;
                return true;
            }
        }
        return false;
    };

    // Pointer click / double-word / triple-line / drag selection
    if (!desc.disabled && ws && cold && ws->last_box_logical.w > 0.0f && desc.id != 0u) {
        const TextInnerGeom prev_geom = compute_text_inner_geom(ctx, ws->last_box_logical, desc);
        const float ptr_wrap_w = (active_wrap_w > 0.0f) ? active_wrap_w : text_input_wrap_width(desc, prev_geom);
        const float local_x = ctx->active_ui->pointer_pos.x - prev_geom.origin_x +
            ((ptr_wrap_w > 0.0f) ? 0.0f : cold->edit_scroll_x);
        const float local_y = ctx->active_ui->pointer_pos.y - prev_geom.origin_y + cold->edit_scroll_y;
        const Str cur_text = { buf ? buf : "", len };

        if (ws->pressed_this_frame) {
            ctx->active_ui->caret_blink_on = true;
            ctx->active_ui->caret_blink_cycles = 0u;
            ctx->active_ui->caret_blink_tick = SDL_GetTicks();
            bool hit_aff = false;
            uint32_t hit_byte = visual_point_to_byte(ctx, desc, cur_text, local_x, local_y, ptr_wrap_w, &hit_aff);
            if (hit_byte && hit_byte < len) {
                const uint32_t previous = utf8_prev_grapheme(buf, len, hit_byte);
                if (utf8_next_grapheme(buf, len, previous) != hit_byte) {
                    hit_byte = previous;
                    hit_aff = false;
                }
            }
            uint32_t as = 0u, ae = 0u;
            if (find_atomic_range(hit_byte, false, false, &as, &ae)) {
                hit_byte = ((hit_byte - as) <= (ae - hit_byte)) ? as : ae;
                hit_aff = false;
            }
            if (ctx->active_ui->last_mouse_clicks >= 3u) {
                if (desc.multiline) {
                    line_bounds_at(buf, len, hit_byte, &anchor, &cursor);
                } else {
                    anchor = 0u;
                    cursor = len;
                }
                cold->edit_wrap_affinity = false;
            } else if (ctx->active_ui->last_mouse_clicks == 2u) {
                utf8_word_bounds_at(buf, len, hit_byte, &anchor, &cursor);
                cold->edit_wrap_affinity = false;
            } else {
                const bool shift_held = (ctx->active_ui->frame_key_mods & Mod_Shift) != 0u;
                if (!shift_held) {
                    anchor = hit_byte;
                }
                cursor = hit_byte;
                cold->edit_wrap_affinity = (ptr_wrap_w > 0.0f) && hit_aff;
            }
            cold->edit_preferred_x = -1.0f;
        } else if (ctx->active_ui->active_id == desc.id && ws->pressed_down) {
            bool drag_aff = false;
            cursor = visual_point_to_byte(ctx, desc, cur_text, local_x, local_y, ptr_wrap_w, &drag_aff);
            if (cursor && cursor < len) {
                const uint32_t previous = utf8_prev_grapheme(buf, len, cursor);
                if (utf8_next_grapheme(buf, len, previous) != cursor) {
                    cursor = previous;
                    drag_aff = false;
                }
            }
            uint32_t as = 0u, ae = 0u;
            if (find_atomic_range(cursor, false, false, &as, &ae)) {
                cursor = (cursor >= anchor) ? ae : as;
                drag_aff = false;
            }
            cold->edit_wrap_affinity = (ptr_wrap_w > 0.0f) && drag_aff;
            cold->edit_preferred_x = -1.0f;
            request_redraw(ctx);
        }
    }

    bool changed = false;
    const auto record_edit = [&](uint8_t op_group) noexcept {
        changed = true;
        if (undo_slot) {
            if (len <= 262144u) {
                undo_push_snapshot(undo_slot, buf, len, cursor, anchor, ctx->active_ui->elapsed_seconds, op_group);
            } else {
                undo_reset_slot(undo_slot);
            }
        }
    };
    bool replaced_by_a11y = false;
    PendingA11yAction command = {};
    while (writable && a11y_take(desc.id, A11yAction::SetValue, &command)) {
        cursor = anchor = insert_utf8_filtered(buf, cap, &len, 0u, len, Str{command.text, command.length}, desc.multiline);
        record_edit(0u);
        if (cold) {
            cold->edit_preferred_x = -1.0f;
            cold->edit_wrap_affinity = false;
        }
        replaced_by_a11y = true;
    }
    if (replaced_by_a11y && is_focused) {
        cancel_ime_composition(ctx);
    }
    bool submitted = false;
    bool ghost_accepted = false;
    bool pasted_image = false;
    if (is_focused) {
        if (undo_slot && undo_slot->count > 0u) {
            TextUndoRecord& cur_rec = undo_slot->entries[(undo_slot->head + undo_slot->cur) % undo_slot->entries_cap];
            if (cur_rec.cursor != cursor || cur_rec.anchor != anchor) {
                undo_slot->last_op_group = 0u;
            }
            cur_rec.cursor = cursor;
            cur_rec.anchor = anchor;
        }
        const PendingTextOp menu_op={.kind=TextEditOpKind::KeyPress,.mod=static_cast<uint16_t>(menu_key==SDLK_DELETE ? 0 : SDL_KMOD_CTRL),.key=menu_key};
        for (uint32_t oi = 0u; oi < ctx->active_ui->frame_text_op_count + (menu_key ? 1u : 0u) && is_focused; ++oi) {
            const PendingTextOp& op = oi < ctx->active_ui->frame_text_op_count ? ctx->active_ui->frame_text_ops[oi] : menu_op;
            const uint32_t sel_min = (cursor < anchor) ? cursor : anchor;
            const uint32_t sel_max = (cursor < anchor) ? anchor : cursor;
            const bool has_sel = (sel_max > sel_min);

            if (op.kind == TextEditOpKind::InsertText) {
                if (writable && op.text_len > 0u) {
                    const Str ins = { ctx->active_ui->frame_text_utf8 + op.text_offset, op.text_len };
                    const uint32_t prev_len = len;
                    const uint32_t new_pos = insert_utf8_filtered(buf, cap, &len, anchor, cursor, ins, desc.multiline);
                    cursor = new_pos;
                    anchor = new_pos;
                    if (has_sel || len != prev_len) {
                        const uint8_t grp = (!has_sel && is_single_nonbreak_utf8_codepoint(ins)) ? 1u : 0u;
                        record_edit(grp);
                    }
                    if (cold) {
                        cold->edit_preferred_x = -1.0f;
                        cold->edit_wrap_affinity = false;
                    }
                }
                continue;
            }

            if (op.kind != TextEditOpKind::KeyPress) {
                continue;
            }

            const bool shift = (op.mod & SDL_KMOD_SHIFT) != 0u;
            const bool primary = (op.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0u;
            const bool alt = (op.mod & SDL_KMOD_ALT) != 0u;
            const bool word_mod = ((op.mod & SDL_KMOD_CTRL) != 0u) || ((op.mod & SDL_KMOD_ALT) != 0u);

            if (primary && !alt) {
                if (op.key == SDLK_A) {
                    anchor = 0u;
                    cursor = len;
                    if (cold) {
                        cold->edit_preferred_x = -1.0f;
                        cold->edit_wrap_affinity = false;
                    }
                    continue;
                }
                if (op.key == SDLK_Z) {
                    if (writable && undo_slot && undo_slot->count > 0u) {
                        if (!shift && undo_slot->cur > 0u) {
                            if (undo_restore(undo_slot, undo_slot->cur - 1u, buf, cap, &len, &cursor, &anchor)) {
                                changed = true;
                                if (cold) {
                                    cold->edit_preferred_x = -1.0f;
                                    cold->edit_wrap_affinity = false;
                                }
                            }
                        } else if (shift && (undo_slot->cur + 1u) < undo_slot->count) {
                            if (undo_restore(undo_slot, undo_slot->cur + 1u, buf, cap, &len, &cursor, &anchor)) {
                                changed = true;
                                if (cold) {
                                    cold->edit_preferred_x = -1.0f;
                                    cold->edit_wrap_affinity = false;
                                }
                            }
                        }
                    }
                    continue;
                }
                if (op.key == SDLK_Y && !shift) {
                    if (writable && undo_slot && (undo_slot->cur + 1u) < undo_slot->count) {
                        if (undo_restore(undo_slot, undo_slot->cur + 1u, buf, cap, &len, &cursor, &anchor)) {
                            changed = true;
                            if (cold) {
                                cold->edit_preferred_x = -1.0f;
                                cold->edit_wrap_affinity = false;
                            }
                        }
                    }
                    continue;
                }
                if (op.key == SDLK_C || op.key == SDLK_X) {
                    if (!desc.password && has_sel && buf) {
                        const uint32_t s_len = sel_max - sel_min;
                        const Str slice = ctx->frame_arena.push_str(buf + sel_min, s_len);
                        if (slice.chars) {
                            (void)set_clipboard_text(slice.chars);
                        }
                    }
                    if (op.key == SDLK_X && writable && has_sel) {
                        cursor = erase_text_range(buf, &len, anchor, cursor);
                        anchor = cursor;
                        record_edit(0u);
                        if (cold) {
                            cold->edit_preferred_x = -1.0f;
                            cold->edit_wrap_affinity = false;
                        }
                    }
                    continue;
                }
                if (op.key == SDLK_V) {
                    if (writable) {
                        const Str clip = get_clipboard_text(&ctx->frame_arena);
                        if (clip.chars && clip.length > 0u) {
                            const uint32_t prev_len = len;
                            const uint32_t new_pos = insert_utf8_filtered(
                                buf, cap, &len, anchor, cursor, clip, desc.multiline);
                            cursor = new_pos;
                            anchor = new_pos;
                            if (has_sel || len != prev_len) {
                                record_edit(0u);
                            }
                            if (cold) {
                                cold->edit_preferred_x = -1.0f;
                                cold->edit_wrap_affinity = false;
                            }
                        } else if (has_clipboard_mime(str_lit("image/png")) ||
                                   has_clipboard_mime(str_lit("image/bmp")) ||
                                   has_clipboard_mime(str_lit("image/jpeg")) ||
                                   has_clipboard_mime(str_lit("image/webp")) ||
                                   get_clipboard_image(&ctx->frame_arena).size_bytes > 0u) {
                            pasted_image = true;
                            request_redraw(ctx);
                        }
                    }
                    continue;
                }
            }

            switch (op.key) {
                case SDLK_LEFT:
                    if (has_sel && !shift && !word_mod) {
                        cursor = sel_min;
                        anchor = sel_min;
                    } else {
                        cursor = word_mod
                            ? utf8_word_left(buf, len, cursor)
                            : utf8_prev_grapheme(buf, len, cursor);
                        uint32_t as = 0u, ae = 0u;
                        if (find_atomic_range(cursor, false, false, &as, &ae)) {
                            cursor = as;
                        }
                        if (!shift) {
                            anchor = cursor;
                        }
                    }
                    if (cold) {
                        cold->edit_preferred_x = -1.0f;
                        cold->edit_wrap_affinity = false;
                    }
                    break;
                case SDLK_RIGHT:
                    if (has_sel && !shift && !word_mod) {
                        cursor = sel_max;
                        anchor = sel_max;
                    } else {
                        cursor = word_mod
                            ? utf8_word_right(buf, len, cursor)
                            : utf8_next_grapheme(buf, len, cursor);
                        uint32_t as = 0u, ae = 0u;
                        if (find_atomic_range(cursor, false, false, &as, &ae)) {
                            cursor = ae;
                        }
                        if (!shift) {
                            anchor = cursor;
                        }
                    }
                    if (cold) {
                        cold->edit_preferred_x = -1.0f;
                        cold->edit_wrap_affinity = false;
                    }
                    break;
                case SDLK_HOME: {
                    bool new_aff = false;
                    if (desc.multiline && !primary && active_wrap_w > 0.0f) {
                        text_wrapped_home_end(
                            ctx, desc, Str{ buf ? buf : "", len }, cursor,
                            cold ? cold->edit_wrap_affinity : false, false, active_wrap_w, &cursor, &new_aff);
                    } else if (desc.multiline && !primary) {
                        uint32_t ls = 0u;
                        uint32_t le = 0u;
                        line_bounds_at(buf, len, cursor, &ls, &le);
                        cursor = ls;
                    } else {
                        cursor = 0u;
                    }
                    if (!shift) {
                        anchor = cursor;
                    }
                    if (cold) {
                        cold->edit_preferred_x = -1.0f;
                        cold->edit_wrap_affinity = new_aff;
                    }
                    break;
                }
                case SDLK_END: {
                    bool new_aff = false;
                    if (desc.multiline && !primary && active_wrap_w > 0.0f) {
                        text_wrapped_home_end(
                            ctx, desc, Str{ buf ? buf : "", len }, cursor,
                            cold ? cold->edit_wrap_affinity : false, true, active_wrap_w, &cursor, &new_aff);
                    } else if (desc.multiline && !primary) {
                        uint32_t ls = 0u;
                        uint32_t le = 0u;
                        line_bounds_at(buf, len, cursor, &ls, &le);
                        cursor = le;
                    } else {
                        cursor = len;
                    }
                    if (!shift) {
                        anchor = cursor;
                    }
                    if (cold) {
                        cold->edit_preferred_x = -1.0f;
                        cold->edit_wrap_affinity = new_aff;
                    }
                    break;
                }
                case SDLK_UP:
                case SDLK_DOWN:
                    if (!desc.suppress_popup_keys && desc.multiline && cold) {
                        cold->edit_cursor = cursor;
                        cold->edit_sel_anchor = anchor;
                        move_caret_vertical(
                            ctx, desc, Str{ buf ? buf : "", len }, ws, (op.key == SDLK_UP) ? -1 : 1, shift, active_wrap_w);
                        cursor = cold->edit_cursor;
                        anchor = cold->edit_sel_anchor;
                    }
                    break;
                case SDLK_BACKSPACE:
                    if (writable) {
                        if (has_sel) {
                            cursor = erase_text_range(buf, &len, anchor, cursor);
                            anchor = cursor;
                            record_edit(0u);
                        } else if (cursor > 0u) {
                            uint32_t del_from = word_mod
                                ? utf8_word_left(buf, len, cursor)
                                : utf8_prev_grapheme(buf, len, cursor);
                            uint32_t del_to = cursor;
                            uint32_t as = 0u, ae = 0u;
                            if (find_atomic_range(cursor, false, true, &as, &ae)) {
                                del_from = as;
                                del_to = ae;
                            } else if (find_atomic_range(del_from, false, false, &as, &ae)) {
                                del_from = as;
                            }
                            if (del_from < del_to) {
                                cursor = erase_text_range(buf, &len, del_from, del_to);
                                anchor = cursor;
                                record_edit(word_mod ? 0u : 2u);
                            }
                        }
                        if (cold) {
                            cold->edit_preferred_x = -1.0f;
                            cold->edit_wrap_affinity = false;
                        }
                    }
                    break;
                case SDLK_DELETE:
                    if (writable) {
                        if (has_sel) {
                            cursor = erase_text_range(buf, &len, anchor, cursor);
                            anchor = cursor;
                            record_edit(0u);
                        } else if (cursor < len) {
                            uint32_t del_from = cursor;
                            uint32_t del_to = word_mod
                                ? utf8_word_right(buf, len, cursor)
                                : utf8_next_grapheme(buf, len, cursor);
                            uint32_t as = 0u, ae = 0u;
                            if (find_atomic_range(cursor, true, false, &as, &ae)) {
                                del_from = as;
                                del_to = ae;
                            } else if (find_atomic_range(del_to, false, false, &as, &ae)) {
                                del_to = ae;
                            }
                            if (del_to > del_from) {
                                cursor = erase_text_range(buf, &len, del_from, del_to);
                                anchor = cursor;
                                record_edit(word_mod ? 0u : 3u);
                            }
                        }
                        if (cold) {
                            cold->edit_preferred_x = -1.0f;
                            cold->edit_wrap_affinity = false;
                        }
                    }
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    if (desc.suppress_popup_keys) {
                        break;
                    }
                    if (desc.multiline && (desc.submit_on_enter ? (shift || alt) : !primary)) {
                        if (writable) {
                            const uint32_t prev_len = len;
                            const uint32_t new_pos = insert_utf8_filtered(
                                buf, cap, &len, anchor, cursor, str_lit("\n"), true);
                            cursor = new_pos;
                            anchor = new_pos;
                            if (has_sel || len != prev_len) {
                                record_edit(0u);
                            }
                            if (cold) {
                                cold->edit_preferred_x = -1.0f;
                                cold->edit_wrap_affinity = false;
                            }
                        }
                    } else {
                        submitted = true;
                    }
                    break;
                case SDLK_TAB:
                    if (desc.accept_ghost_on_tab && !shift && !primary && !alt && writable && !desc.password &&
                        desc.ghost_text.chars && desc.ghost_text.length > 0u && cursor == len && cursor == anchor)
                    {
                        ghost_accepted = true;
                        request_redraw(ctx);
                    }
                    break;
                case SDLK_ESCAPE:
                    if (has_sel) anchor = cursor;
                    else if (desc.escape_blurs) { request_focus(0);is_focused=false; }
                    if (cold) {
                        cold->edit_wrap_affinity = false;
                    }
                    break;
                default:
                    break;
            }
        }
    }

    const uint32_t clamped_cursor = utf8_clamp_boundary(buf, len, cursor);
    if (cold && clamped_cursor != cursor) {
        cold->edit_wrap_affinity = false;
    }
    cursor = clamped_cursor;
    anchor = utf8_clamp_boundary(buf, len, anchor);
    if (ws) {
        ws->accept_ghost_on_tab = desc.accept_ghost_on_tab && writable && !desc.password &&
            desc.ghost_text.chars && desc.ghost_text.length > 0u &&
            cursor == len && cursor == anchor && !ghost_accepted;
    }
    if (cold) {
        cold->edit_cursor = cursor;
        cold->edit_sel_anchor = anchor;
        if (active_wrap_w > 0.0f) {
            cold->edit_scroll_x = 0.0f;
        }
    }

    const Str committed_slice = (buf && len > 0u) ? ctx->frame_arena.push_str(buf, len) : Str{};
    if (cold) {
        cold->sel_text_ptr = committed_slice.chars;
        cold->sel_text_len = committed_slice.length;
        cold->sel_text_epoch = ctx->arena_epoch;
        cold->sel_text_static = false;
    }
    if (!is_focused && ctx->active_ui->ime_focused_id==desc.id) ctx->active_ui->ime_focused_id=0;
    const bool composing = is_focused && !desc.read_only && !desc.password && ctx->active_ui->ime_comp_len > 0u;
    Rect caret_rect = {};
    if (is_focused && ws && ws->last_box_logical.w > 0.0f) {
        const TextInnerGeom pre_geom = compute_text_inner_geom(ctx, ws->last_box_logical, desc);
        if (!desc.read_only && !desc.password) {
            update_text_scroll_and_ime(
                ctx, ws, desc, pre_geom, committed_slice, cursor, true, composing, nullptr, nullptr);
            caret_rect = ctx->active_ui->ime_cursor_logical;
        } else {
            float cx = 0.0f, cy = 0.0f;
            const float wrap_w = text_input_wrap_width(desc, pre_geom);
            measure_text_content_and_ime(
                ctx, desc, committed_slice, cursor, false,
                nullptr, nullptr, nullptr, nullptr, nullptr, &cx, &cy, wrap_w,
                (cold && wrap_w > 0.0f) ? cold->edit_wrap_affinity : false);
            const float sx = cold ? cold->edit_scroll_x : 0.0f;
            const float sy = cold ? cold->edit_scroll_y : 0.0f;
            caret_rect = { pre_geom.origin_x + cx - sx, pre_geom.origin_y + cy - sy, 2.0f, pre_geom.line_h };
        }
    }
    if (desc.multiline && desc.auto_grow) {
        const float min_h = (std::isfinite(desc.min_height_px) && desc.min_height_px > 0.0f) ? desc.min_height_px : 36.0f;
        const float max_h = (std::isfinite(desc.max_height_px) && desc.max_height_px >= min_h) ? desc.max_height_px : min_h;
        float content_h = text_input_line_height_logical(ctx, desc);
        float measure_wrap_w = active_wrap_w;
        if (measure_wrap_w <= 0.0f && desc.word_wrap && desc.width.kind == SizingKind::Fixed && desc.width.value > 0.0f) {
            const float inner_w = desc.width.value - desc.padding.left - desc.padding.right;
            if (inner_w > 8.0f) measure_wrap_w = inner_w;
        }
        measure_text_content_and_ime(
            ctx, desc, committed_slice, cursor, composing,
            nullptr, &content_h, nullptr, nullptr, nullptr, nullptr, nullptr,
            measure_wrap_w, (cold && measure_wrap_w > 0.0f) ? cold->edit_wrap_affinity : false);
        const float wanted_h = clamp_range(content_h + desc.padding.top + desc.padding.bottom, min_h, max_h);
        desc.height = Sizing::fixed(wanted_h);
    }

    TextInputExtra* ti_extra = ctx->frame_arena.alloc<TextInputExtra>();
    BoxExtra* extra = ctx->frame_arena.alloc<BoxExtra>();
    if (ti_extra && extra) {
        extra->layer = ctx->active_ui->current_layer;
        extra->kind = CustomCmdKind::TextInput;
        ti_extra->desc = desc;
        ti_extra->desc.highlights = {};
        if (highlight_count && committed_slice.length > 0u) {
            Arena& arena = ctx->frame_arena;
            const size_t capacity = arena.remaining() / sizeof(TextInputHighlight);
            TextInputHighlight* copy = highlight_count < capacity
                ? arena.alloc_uninit<TextInputHighlight>(highlight_count) : nullptr;
            if (copy) {
                uint32_t count = 0;
                const char* text = committed_slice.chars;
                const uint32_t bytes = committed_slice.length;
                for (size_t i = 0; i < highlight_count; ++i) {
                    TextInputHighlight range = read_highlight(highlights, i);
                    if (range.byte_offset >= bytes || range.byte_length == 0u ||
                        !(range.background.a || range.border_color.a || range.text_color.a)) continue;
                    uint32_t end = range.byte_offset + std::min(range.byte_length, bytes - range.byte_offset);
                    const uint32_t begin = utf8_clamp_boundary(text, bytes, range.byte_offset);
                    const uint32_t end_boundary = utf8_clamp_boundary(text, bytes, end);
                    if (end_boundary < end) end = utf8_next_boundary(text, bytes, end_boundary);
                    range.byte_offset = begin;
                    range.byte_length = end - begin;
                    range.radius_px = std::isfinite(range.radius_px) ? std::fmax(0.0f, range.radius_px) : 0.0f;
                    copy[count++] = range;
                }
                ti_extra->desc.highlights = {copy, count};
            }
        }
        if (desc.placeholder.chars && desc.placeholder.length > 0u && !desc.placeholder.is_static) {
            ti_extra->desc.placeholder = ctx->frame_arena.push_str(desc.placeholder.chars, desc.placeholder.length);
        }
        if (ghost_accepted) {
            ti_extra->desc.ghost_text = {};
        } else if (desc.ghost_text.chars && desc.ghost_text.length > 0u && !desc.ghost_text.is_static) {
            ti_extra->desc.ghost_text = ctx->frame_arena.push_str(desc.ghost_text.chars, desc.ghost_text.length);
        }
        ti_extra->committed_text = committed_slice;
        ti_extra->cursor_byte = cursor;
        ti_extra->sel_anchor_byte = anchor;
        ti_extra->is_focused = is_focused;
        ti_extra->hover_t = ws ? clamp_range(ws->hover_t, 0.0f, 1.0f) : 0.0f;
        ti_extra->leading_icon_pad_left = orig_pad_left;
        extra->desc.id = desc.id;
        extra->text_input = ti_extra;
    }

    const uint32_t ti_node_idx = layout_open(ctx->active_ui->layout, desc.id);
    uint32_t semantic_id = desc.id;
    if (ti_node_idx != kLayoutInvalidNode) {
        LayoutNodeConfig decl = {};
        decl.width = desc.width;
        decl.height = desc.height;
        decl.padding = desc.padding;
        decl.corner_radius = desc.radius;
        if (extra && ti_extra) {
            decl.emit_custom = true;
            decl.custom_data = extra;
            decl.user_data = extra;
        }
        layout_configure_open(ctx->active_ui->layout, decl);
        if (ctx->active_ui->subtree_depth > 0u && ctx->active_ui->subtree_stack[ctx->active_ui->subtree_depth - 1u].meta) {
            ctx->active_ui->subtree_stack[ctx->active_ui->subtree_depth - 1u].meta->tainted = true;
        }
        if (show_clear) {
            const bool ch = hovered(clear_id);
            const bool cp = pressed(clear_id);
            const float ht = spring(id_index(clear_id, 0xC1EA01u), ch ? 1.0f : 0.0f, SpringSpec::snappy());
            const float htc = clamp_range(ht, 0.0f, 1.0f);
            const Color pill_bg = cp
                ? thm.surface_active
                : (htc > 0.001f
                    ? Color::rgba8(thm.surface_hover.r, thm.surface_hover.g, thm.surface_hover.b,
                                   static_cast<uint8_t>(std::round(180.0f * htc)))
                    : Color{});
            begin_box({.id=clear_id,.width=Sizing::fixed(20),.height=Sizing::fixed(20),.align_y=AlignY::Center,
                .background=pill_bg,.radius=Corners::all(10.0f),
                .attach_to=AttachTo::Parent,.anchor_element=Anchor::CenterRight,.anchor_parent=Anchor::CenterRight,.floating_offset={-6,0}});
            if (ch) set_cursor(Cursor::Hand);
            const Color x_col = Color::lerp_oklab(thm.text_muted, desc.text_color, (ch || cp) ? 1.0f : htc);
            draw_line({.from={6.5f,6.5f},.to={13.5f,13.5f},.color=x_col,.thickness=1.5f});
            draw_line({.from={13.5f,6.5f},.to={6.5f,13.5f},.color=x_col,.thickness=1.5f});
            a11y_semantic(clear_id,A11yRole::Button,str_lit("Clear"),0,3u);
            end_box();
        }
        semantic_id = ctx->active_ui->layout->nodes.data[ti_node_idx].id;
        layout_close(ctx->active_ui->layout);
    }

    Str semantic_value={buf,len};
    if(desc.password) {
        uint32_t count=0; for(uint32_t i=0;i<len;++i) if((static_cast<unsigned char>(buf[i])&0xC0u)!=0x80u)++count;
        char* bullets=ctx->frame_arena.alloc<char>(size_t(count)*3+1);
        if(bullets) {for(uint32_t i=0;i<count;++i)std::memcpy(bullets+i*3,"\xE2\x80\xA2",3);semantic_value={bullets,count*3};} else semantic_value={};
    }
    a11y_emit(ctx,desc.id ? desc.id : semantic_id,A11yRole::TextInput,{},semantic_value,
        (desc.password?A11yState_Password:0u)|(desc.read_only?A11yState_ReadOnly:0u)|(desc.multiline?A11yState_Multiline:0u)|(desc.disabled?A11yState_Disabled:0u),
        desc.disabled ? 0u : (1u|(writable?128u:0u)));
    return TextInputResult{
        .changed = changed,
        .submitted = submitted,
        .focused = is_focused,
        .composing = composing,
        .byte_length = len,
        .cursor_byte = cursor,
        .selection_anchor_byte = anchor,
        .ime_cursor_rect = (is_focused && !desc.read_only && !desc.password) ? ctx->active_ui->ime_cursor_logical : Rect{},
        .ghost_accepted = ghost_accepted,
        .caret_rect = caret_rect,
        .pasted_image = pasted_image,
    };
}

TextInputResult text_input(const TextInputDesc& desc) noexcept {
    return text_input_impl(desc, desc.highlights.data(), desc.highlights.size(),
        [](const void* ranges, size_t i) noexcept { return static_cast<const TextInputHighlight*>(ranges)[i]; });
}

bool hovered(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (element_id == 0u || !ctx || !ctx->active_ui->layout) {
        return false;
    }
    if (ctx->active_ui->active_id != 0u && ctx->active_ui->active_id != element_id) {
        return false;
    }
    if (const WidgetState* ws = state_map_find(&ctx->active_ui->states, element_id)) {
        if (ws->is_text_span) {
            return ws->span_hovered;
        }
    }
    return pointer_over_id(ctx->active_ui, element_id);
}

uint32_t topmost_hovered_id() noexcept {
    const Context* ctx = current_context();
    return (ctx && ctx->active_ui) ? ctx->active_ui->topmost_hovered_id : 0u;
}

bool clicked_without_drag(uint32_t element_id, float max_drag_px) noexcept {
    if (!clicked(element_id)) return false;
    const Context* ctx = current_context();
    if (!ctx || !ctx->active_ui) return false;
    const float th = (std::isfinite(max_drag_px) && max_drag_px > 0.0f) ? max_drag_px : 0.0f;
    return ctx->active_ui->last_released_drag_dist_sq <= (th * th);
}

bool pressed(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || element_id == 0u) {
        return false;
    }
    const WidgetState* ws = state_map_find(&ctx->active_ui->states, element_id);
    return ws != nullptr && ws->pressed_down;
}

bool clicked(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || element_id == 0u) {
        return false;
    }
    const WidgetState* ws = state_map_find(&ctx->active_ui->states, element_id);
    return (ctx->in_frame && a11y_take(element_id,A11yAction::Press)) || (ws != nullptr && ws->clicked_this_frame);
}

bool active(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    return ctx != nullptr && element_id != 0u && ctx->active_ui->active_id == element_id;
}

uint32_t active_id() noexcept {
    Context* ctx = current_context();
    return ctx ? ctx->active_ui->active_id : 0u;
}

void set_active(uint32_t element_id) noexcept {
    if (Context* ctx = current_context()) {
        ctx->active_ui->active_id = element_id;
        request_redraw(ctx);
    }
}

bool focused(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    return ctx != nullptr && element_id != 0u && ctx->active_ui->focus_id == element_id;
}

uint32_t focused_id() noexcept {
    Context* ctx = current_context();
    return ctx ? ctx->active_ui->focus_id : 0u;
}

void request_focus(uint32_t element_id) noexcept {
    if (Context* ctx = current_context()) {
        if (element_id != ctx->active_ui->focus_id) {
            clear_ime_composition(ctx);
        }
        ctx->active_ui->focus_id = element_id;
        ctx->active_ui->caret_blink_on = true;
        ctx->active_ui->caret_blink_cycles = 0u;
        ctx->active_ui->caret_blink_tick = SDL_GetTicks();
        request_redraw(ctx);
    }
}

void set_cursor(Cursor cursor) noexcept {
    if (Context* ctx = current_context()) {
        ctx->active_ui->desired_cursor = cursor;
    }
}

bool element_geometry(uint32_t element_id, ElementGeometry* out_geometry) noexcept {
    const Context* ctx = current_context();
    if (!ctx || !element_id || !out_geometry) return false;
    const auto& ui = *ctx->active_ui;
    for (uint32_t i = ui.drop_hit_count; i > 0; --i) {
        const auto& hit = ui.drop_hits[i-1];
        if (hit.id == element_id) {
            *out_geometry = {hit.visual_bounds,hit.visible_bounds,hit.local_scale,hit.visual_opacity};
            return true;
        }
    }
    return false;
}
bool screen_to_element_local(uint32_t element_id, Vec2 screen_pos, Vec2* out_local) noexcept {
    if (!out_local || !std::isfinite(screen_pos.x) || !std::isfinite(screen_pos.y)) return false;
    ElementGeometry g;
    if (!element_geometry(element_id,&g) || g.local_scale.x <= 0 || g.local_scale.y <= 0) return false;
    *out_local = {(screen_pos.x-g.bounds.x)/g.local_scale.x,(screen_pos.y-g.bounds.y)/g.local_scale.y};
    return true;
}

Rect element_bounds(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->active_ui->layout || element_id == 0u) {
        return {};
    }
    if (const WidgetState* ws = state_map_find(&ctx->active_ui->states, element_id)) {
        if (ws->is_text_span) {
            bool found = false;
            float min_x = 0.0f;
            float min_y = 0.0f;
            float max_x = 0.0f;
            float max_y = 0.0f;
            for (uint32_t i = 0u; i < ctx->active_ui->span_hit_count; ++i) {
                const SpanHitRegion& r = ctx->active_ui->span_hits[i];
                if (r.id != element_id) {
                    continue;
                }
                if (!found) {
                    min_x = r.rect.x;
                    min_y = r.rect.y;
                    max_x = r.rect.x + r.rect.w;
                    max_y = r.rect.y + r.rect.h;
                    found = true;
                } else {
                    if (r.rect.x < min_x) min_x = r.rect.x;
                    if (r.rect.y < min_y) min_y = r.rect.y;
                    if (r.rect.x + r.rect.w > max_x) max_x = r.rect.x + r.rect.w;
                    if (r.rect.y + r.rect.h > max_y) max_y = r.rect.y + r.rect.h;
                }
            }
            return found ? Rect{ min_x, min_y, max_x - min_x, max_y - min_y } : Rect{};
        }
    }
    bool found = false;
    const Rect b = layout_engine_bounds(ctx->active_ui->layout, element_id, &found);
    return found ? b : Rect{};
}

bool begin_cached_subtree(uint32_t subtree_id, uint64_t content_hash) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || !ctx->active_ui || !ctx->active_ui->layout || subtree_id == 0u) {
        return true;
    }
    auto& ui = *ctx->active_ui;
    const bool cache_eligible =
        ctx->theme_depth == 0u &&
        ui.current_layer == nullptr &&
        ui.canvas_depth == 0u &&
        ui.subtree_depth == 0u;

    if (cache_eligible) {
        SubtreeCacheEntry* entry = subtree_cache_find(ui.subtree_cache.data, ui.subtree_cache.count, subtree_id);
        if (entry && entry->valid &&
            entry->content_hash == content_hash &&
            entry->theme_epoch == ctx->theme_epoch &&
            entry->font_revision == ctx->atlas.font_revision &&
            entry->atlas_uv_epoch == ctx->atlas.uv_epoch &&
            entry->dpi_scale == ui.dpi_scale &&
            entry->viewport_w == ui.viewport_w &&
            entry->viewport_h == ui.viewport_h)
        {
            bool interactive_hot = false;
            if (entry->has_interactive) {
                const Rect expanded = {
                    entry->logical_bounds.x - 2.0f,
                    entry->logical_bounds.y - 2.0f,
                    entry->logical_bounds.w + 4.0f,
                    entry->logical_bounds.h + 4.0f,
                };
                if ((ui.has_pointer && expanded.contains(ui.pointer_pos)) ||
                    (ui.had_pointer && expanded.contains(ui.prev_pointer_pos)) ||
                    ui.frame_scroll_delta.x != 0.0f || ui.frame_scroll_delta.y != 0.0f)
                {
                    interactive_hot = true;
                } else if (ui.active_id != 0u || ui.focus_id != 0u || ui.right_active_id != 0u) {
                    for (uint32_t i = 0; i < entry->hitbox_count; ++i) {
                        const uint32_t hid = entry->hitboxes[i].id;
                        if (hid != 0u && (hid == ui.active_id || hid == ui.focus_id || hid == ui.right_active_id)) {
                            interactive_hot = true;
                            break;
                        }
                    }
                }
            }
            if (!interactive_hot) {
                entry->last_used_frame = ui.frame_index;
                SubtreeMeta* meta = ctx->frame_arena.alloc<SubtreeMeta>();
                if (meta) {
                    meta->id = subtree_id;
                    meta->content_hash = content_hash;
                    meta->parent_a11y = ctx->a11y_parent;
                }
                const uint32_t node_idx = layout_open(ui.layout, id_index(subtree_id, 0xCA7Eu));
                if (node_idx != kLayoutInvalidNode) {
                    LayoutNodeConfig decl = {};
                    decl.width = Sizing::fixed(entry->logical_bounds.w);
                    decl.height = Sizing::fixed(entry->logical_bounds.h);
                    decl.cached_subtree_id = subtree_id;
                    decl.cached_subtree_hit = true;
                    decl.user_data = meta;
                    layout_configure_open(ui.layout, decl);
                    layout_close(ui.layout);
                    return false;
                }
            }
        }
    }

    if (!cache_eligible || ui.subtree_depth >= 16u) {
        if (ui.subtree_depth < 16u) {
            ui.subtree_stack[ui.subtree_depth++] = SubtreeCaptureFrame{.id = 0u, .meta = nullptr};
        }
        return true;
    }

    SubtreeMeta* meta = ctx->frame_arena.alloc<SubtreeMeta>();
    if (!meta) {
        ui.subtree_stack[ui.subtree_depth++] = SubtreeCaptureFrame{.id = 0u, .meta = nullptr};
        return true;
    }
    *meta = SubtreeMeta{
        .id = subtree_id,
        .content_hash = content_hash,
        .a11y_start = ctx->a11y_count,
        .a11y_end = ctx->a11y_count,
        .anim_count_start = ui.active_anim_count,
        .parent_a11y = ctx->a11y_parent,
        .tainted = false,
        .has_interactive = false,
    };
    const uint32_t node_idx = layout_open(ui.layout, id_index(subtree_id, 0xCA7Eu));
    if (node_idx == kLayoutInvalidNode) {
        ui.subtree_stack[ui.subtree_depth++] = SubtreeCaptureFrame{.id = 0u, .meta = nullptr};
        return true;
    }
    LayoutNodeConfig decl = {};
    decl.width = Sizing::fit();
    decl.height = Sizing::fit();
    decl.axis = Axis::Col;
    decl.cached_subtree_id = subtree_id;
    decl.cached_subtree_hit = false;
    decl.user_data = meta;
    layout_configure_open(ui.layout, decl);
    ui.subtree_stack[ui.subtree_depth++] = SubtreeCaptureFrame{.id = subtree_id, .meta = meta};
    return true;
}

void end_cached_subtree() noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || !ctx->active_ui || ctx->active_ui->subtree_depth == 0u) {
        return;
    }
    auto& ui = *ctx->active_ui;
    const SubtreeCaptureFrame frame = ui.subtree_stack[--ui.subtree_depth];
    if (frame.id == 0u || !frame.meta) {
        return;
    }
    frame.meta->a11y_end = ctx->a11y_count;
    if (ui.active_anim_count > frame.meta->anim_count_start) {
        frame.meta->tainted = true;
    }
    layout_close(ui.layout);
}

void invalidate_cached_subtree(uint32_t subtree_id, Context* ctx_arg) noexcept {
    Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (!ctx) return;
    const auto clear_ui = [subtree_id](WindowUiState& ui) noexcept {
        for (uint32_t i = 0; i < ui.subtree_cache.count; ++i) {
            SubtreeCacheEntry& e = ui.subtree_cache.data[i];
            if (subtree_id == 0u || e.id == subtree_id) e.valid = false;
        }
    };
    if (ctx->active_ui) clear_ui(*ctx->active_ui);
    if (ctx->headless_ui) clear_ui(*ctx->headless_ui);
    for (uint32_t i = 0; i < ctx->claimed_window_cap; ++i) {
        if (ctx->claimed_windows[i]) clear_ui(ctx->claimed_windows[i]->ui);
    }
}

void push_id(uint32_t scope_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->active_ui) return;
    auto& ui = *ctx->active_ui;
    const uint32_t seed = ui.id_stack_depth > 0u ? ui.id_stack[ui.id_stack_depth - 1u] : 0x811C9DC5u;
    const uint32_t mixed = id_index(seed, scope_id);
    if (ui.id_stack_depth < 32u) {
        ui.id_stack[ui.id_stack_depth++] = mixed;
    } else {
        ++ui.id_stack_overflow;
    }
}

void push_id(Str label) noexcept {
    uint32_t h = 0x811C9DC5u;
    if (label.chars && label.length > 0u) {
        h = fnv1a_32_step(h, label.chars, label.length);
    }
    push_id(h ? h : 1u);
}

void pop_id() noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->active_ui) return;
    auto& ui = *ctx->active_ui;
    if (ui.id_stack_overflow > 0u) {
        --ui.id_stack_overflow;
    } else if (ui.id_stack_depth > 0u) {
        --ui.id_stack_depth;
    }
}

uint32_t scoped_id(uint32_t local_id) noexcept {
    const Context* ctx = current_context();
    const uint32_t base = local_id ? local_id : 1u;
    if (!ctx || !ctx->active_ui || ctx->active_ui->id_stack_depth == 0u) {
        return base;
    }
    return id_index(ctx->active_ui->id_stack[ctx->active_ui->id_stack_depth - 1u], base);
}

uint32_t scoped_id(Str label) noexcept {
    uint32_t h = 0x811C9DC5u;
    if (label.chars && label.length > 0u) {
        h = fnv1a_32_step(h, label.chars, label.length);
    }
    return scoped_id(h ? h : 1u);
}

uint16_t key_mods() noexcept {
    const Context* ctx = current_context();
    return ctx ? ctx->active_ui->frame_key_mods : uint16_t(Mod_None);
}

bool key_pressed(uint32_t keycode, bool include_repeat) noexcept {
    const Context* ctx = current_context();
    if (!ctx || keycode == 0u) {
        return false;
    }
    for (uint32_t i = 0u; i < ctx->active_ui->frame_key_count; ++i) {
        const KeyEventEntry& ev = ctx->active_ui->frame_keys[i];
        if (!ev.consumed && ev.key == keycode && (include_repeat || !ev.repeat)) {
            return true;
        }
    }
    return false;
}

bool consume_key_pressed(uint32_t keycode, bool include_repeat) noexcept {
    Context* ctx = current_context();
    if (!ctx || keycode == 0u) {
        return false;
    }
    for (uint32_t i = 0u; i < ctx->active_ui->frame_key_count; ++i) {
        KeyEventEntry& ev = ctx->active_ui->frame_keys[i];
        if (!ev.consumed && ev.key == keycode && (include_repeat || !ev.repeat)) {
            ev.consumed = true;
            return true;
        }
    }
    return false;
}

uint32_t frame_key_count() noexcept {
    const Context* ctx = current_context();
    return (ctx && ctx->active_ui) ? ctx->active_ui->frame_key_count : 0u;
}

bool frame_key_at(uint32_t index, FrameKeyEvent* out_event) noexcept {
    const Context* ctx = current_context();
    if (!ctx || !ctx->active_ui || !out_event || index >= ctx->active_ui->frame_key_count) {
        return false;
    }
    const KeyEventEntry& ev = ctx->active_ui->frame_keys[index];
    *out_event = { ev.key, ev.mods, ev.repeat, ev.consumed };
    return true;
}

bool consume_frame_key_at(uint32_t index) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->active_ui || index >= ctx->active_ui->frame_key_count) {
        return false;
    }
    KeyEventEntry& ev = ctx->active_ui->frame_keys[index];
    if (ev.consumed) return false;
    ev.consumed = true;
    return true;
}

bool shortcut(uint16_t required_mods, uint32_t keycode, bool allow_when_typing) noexcept {
    Context* ctx = current_context();
    if (!ctx || keycode == 0u) {
        return false;
    }
    if (ctx->active_ui->modal_scope && !focus_scope_contains(*ctx->active_ui, ctx->active_ui->modal_scope)) return false;
    if (!allow_when_typing) {
        if (ctx->active_ui->ime_comp_len > 0u) {
            return false;
        }
        if (ctx->active_ui->focus_id != 0u) {
            const WidgetState* fws = state_map_find(&ctx->active_ui->states, ctx->active_ui->focus_id);
            if (fws && fws->is_text_input_writable && (fws->last_seen_frame + 1u >= ctx->active_ui->frame_index)) {
                return false;
            }
        }
    }
    constexpr uint16_t kModMask = static_cast<uint16_t>(Mod_Shift | Mod_Ctrl | Mod_Alt | Mod_Super);
    for (uint32_t i = 0u; i < ctx->active_ui->frame_key_count; ++i) {
        const KeyEventEntry& ev = ctx->active_ui->frame_keys[i];
        if (!ev.consumed && !ev.repeat && ev.key == keycode && (ev.mods & kModMask) == required_mods) {
            return true;
        }
    }
    return false;
}

bool consume_shortcut(uint16_t required_mods, uint32_t keycode, bool allow_when_typing) noexcept {
    Context* ctx = current_context();
    if (!ctx || keycode == 0u) {
        return false;
    }
    if (ctx->active_ui->modal_scope && !focus_scope_contains(*ctx->active_ui, ctx->active_ui->modal_scope)) return false;
    if (!allow_when_typing) {
        if (ctx->active_ui->ime_comp_len > 0u) {
            return false;
        }
        if (ctx->active_ui->focus_id != 0u) {
            const WidgetState* fws = state_map_find(&ctx->active_ui->states, ctx->active_ui->focus_id);
            if (fws && fws->is_text_input_writable && (fws->last_seen_frame + 1u >= ctx->active_ui->frame_index)) {
                return false;
            }
        }
    }
    constexpr uint16_t kModMask = static_cast<uint16_t>(Mod_Shift | Mod_Ctrl | Mod_Alt | Mod_Super);
    for (uint32_t i = 0u; i < ctx->active_ui->frame_key_count; ++i) {
        KeyEventEntry& ev = ctx->active_ui->frame_keys[i];
        if (!ev.consumed && !ev.repeat && ev.key == keycode && (ev.mods & kModMask) == required_mods) {
            ev.consumed = true;
            return true;
        }
    }
    return false;
}

Vec2 pointer_pos() noexcept {
    const Context* ctx = current_context();
    return ctx ? ctx->active_ui->pointer_pos : Vec2{-1.0f, -1.0f};
}

Vec2 pointer_delta() noexcept {
    const Context* ctx = current_context();
    return ctx ? ctx->active_ui->pointer_delta : Vec2{0.0f, 0.0f};
}

PointerState pointer_state(const Context* ctx_arg) noexcept {
    const Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (!ctx) {
        return {};
    }
    const WindowUiState* ui = ctx->active_ui
        ? ctx->active_ui
        : (ctx->headless ? ctx->headless_ui : window_ui(ctx, nullptr));
    return ui ? ui->pointer_state : PointerState{};
}

PointerGesturePhase pointer_gesture_phase(const Context* ctx_arg) noexcept {
    const Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (!ctx) {
        return PointerGesturePhase::None;
    }
    const WindowUiState* ui = ctx->active_ui
        ? ctx->active_ui
        : (ctx->headless ? ctx->headless_ui : window_ui(ctx, nullptr));
    return ui ? ui->gesture_phase : PointerGesturePhase::None;
}

uint32_t active_touch_points(TouchPoint* out_points, uint32_t max_points, const Context* ctx_arg) noexcept {
    const Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (!ctx) {
        return 0u;
    }
    const WindowUiState* ui = ctx->active_ui
        ? ctx->active_ui
        : (ctx->headless ? ctx->headless_ui : window_ui(ctx, nullptr));
    if (!ui) {
        return 0u;
    }
    const uint32_t total = ui->active_touch_count;
    if (!out_points || max_points == 0u) {
        return total;
    }
    const uint32_t copy_n = (total < max_points) ? total : max_points;
    if (copy_n > 0u) {
        std::memcpy(out_points, ui->active_touch_points, static_cast<size_t>(copy_n) * sizeof(TouchPoint));
    }
    return copy_n;
}

bool right_clicked(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || element_id == 0u) {
        return false;
    }
    const WidgetState* ws = state_map_find(&ctx->active_ui->states, element_id);
    return ws != nullptr && ws->right_clicked_this_frame;
}

bool right_clicked_anywhere() noexcept {
    const Context* ctx = current_context();
    return ctx != nullptr && ctx->active_ui->right_clicked_any_this_frame;
}

bool double_clicked(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || element_id == 0u) {
        return false;
    }
    const WidgetState* ws = state_map_find(&ctx->active_ui->states, element_id);
    return ws != nullptr && ws->double_clicked_this_frame;
}

bool dragging(uint32_t element_id, float threshold_px) noexcept {
    Context* ctx = current_context();
    if (!ctx || element_id == 0u || ctx->active_ui->active_id != element_id) {
        return false;
    }
    const WidgetState* ws = state_map_find(&ctx->active_ui->states, element_id);
    if (!ws || !ws->pressed_down) {
        return false;
    }
    const float th = (threshold_px > 0.0f) ? threshold_px : 0.0f;
    return ctx->active_ui->left_max_drag_dist_sq >= (th * th);
}

bool should_close_popover(uint32_t popover_id, uint32_t trigger_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->active_ui->layout || popover_id == 0u || ctx->active_ui->frame_index <= 1u) {
        return false;
    }
    const WidgetState* pop_ws = state_map_find(&ctx->active_ui->states, popover_id);
    if (!pop_ws) {
        return false;
    }
    const bool seen_prev_frame =
        (pop_ws->last_seen_frame == (ctx->active_ui->frame_index - 1u)) ||
        (pop_ws->last_seen_frame == ctx->active_ui->frame_index && pop_ws->prev_seen_frame == (ctx->active_ui->frame_index - 1u));
    if (!seen_prev_frame) {
        return false;
    }
    if (!ctx->active_ui->frame_escape_consumed_by_ime && consume_key_pressed(SDLK_ESCAPE, false)) {
        return true;
    }
    if (!ctx->active_ui->left_pressed_this_frame && !ctx->active_ui->right_pressed_this_frame) {
        return false;
    }
    const auto is_over_elem = [&](uint32_t id, const WidgetState* ws) noexcept -> bool {
        if (id == 0u) {
            return false;
        }
        if (pointer_over_id(ctx->active_ui, id)) {
            return true;
        }
        if (ws && (ws->last_seen_frame + 1u >= ctx->active_ui->frame_index) &&
            ws->last_box_logical.w > 0.0f && ws->last_box_logical.h > 0.0f &&
            ws->last_box_logical.contains(ctx->active_ui->pointer_pos))
        {
            return true;
        }
        return false;
    };
    if (is_over_elem(popover_id, pop_ws)) {
        return false;
    }
    if (trigger_id != 0u) {
        const WidgetState* trig_ws = state_map_find(&ctx->active_ui->states, trigger_id);
        if (is_over_elem(trigger_id, trig_ws)) {
            return false;
        }
    }
    return true;
}

std::span<const Str> dropped_files() noexcept {
    const Context* ctx = current_context();
    if (!ctx || ctx->active_ui->frame_drop_count == 0u) {
        return {};
    }
    return std::span<const Str>(ctx->active_ui->frame_drops, ctx->active_ui->frame_drop_count);
}

float dpi_scale(const Context* ctx_arg) noexcept {
    const Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (!ctx) {
        return 1.0f;
    }
    const WindowUiState* ui = ctx->active_ui ? ctx->active_ui : ctx->headless_ui;
    return (ui && std::isfinite(ui->dpi_scale) && ui->dpi_scale > 0.0f) ? ui->dpi_scale : 1.0f;
}

float hairline_px(const Context* ctx_arg) noexcept {
    return 1.0f / dpi_scale(ctx_arg);
}

float spring(uint32_t anim_id, float target, SpringSpec spec) noexcept {
    return spring(anim_id, target, target, spec);
}

float spring(uint32_t anim_id, float target, float initial_value, SpringSpec spec) noexcept {
    Context* ctx = current_context();
    if (!ctx || anim_id == 0u || !std::isfinite(target) || !std::isfinite(initial_value)) {
        return target;
    }
    spec=valid_spring_spec(spec);
    WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, anim_id, ctx->active_ui->frame_index);
    if (!ws) {
        return target;
    }
    if (!ws->spring_initialized) {
        ws->spring_pos = ctx->reduced_motion ? target : initial_value;
        ws->spring_vel = 0.0f;
        ws->spring_initialized = true;
        if (ws->spring_pos != target && ctx->time_scale>0) ++ctx->active_ui->active_anim_count;
        return ws->spring_pos;
    }

    advance_motion(ctx,&ws->spring_pos,&ws->spring_vel,target,spec);
    return ws->spring_pos;
}

Vec2 spring_vec2(uint32_t anim_id, Vec2 target, SpringSpec spec) noexcept {
    if (anim_id == 0u) {
        return target;
    }
    return Vec2{
        .x = spring(id_index(anim_id, 0x58u), target.x, spec),
        .y = spring(id_index(anim_id, 0x59u), target.y, spec),
    };
}

Color spring_color(uint32_t anim_id, Color target, SpringSpec spec) noexcept {
    Context* ctx = current_context();
    if (!ctx || anim_id == 0u) {
        return target;
    }
    const Vec4 tgt = color_to_oklab_internal(target);
    const float L = spring(id_index(anim_id, 0xC01001u), tgt.x, spec);
    const float a = spring(id_index(anim_id, 0xC01002u), tgt.y, spec);
    const float b = spring(id_index(anim_id, 0xC01003u), tgt.z, spec);
    const float alpha = spring(id_index(anim_id, 0xC01004u), tgt.w, spec);
    if (L == tgt.x && a == tgt.y && b == tgt.z && alpha == tgt.w) {
        return target;
    }
    return color_from_oklab_internal(Vec4{ L, a, b, alpha });
}

float spring_angle(uint32_t anim_id, float target_rad, SpringSpec spec) noexcept {
    Context* ctx = current_context();
    if (!std::isfinite(target_rad)) {
        return 0.0f;
    }
    if (!ctx || anim_id == 0u) {
        return target_rad;
    }
    spec = valid_spring_spec(spec);
    WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, anim_id, ctx->active_ui->frame_index);
    if (!ws) {
        return target_rad;
    }
    if (!ws->spring_initialized) {
        ws->spring_pos = target_rad;
        ws->spring_vel = 0.0f;
        ws->spring_initialized = true;
        return ws->spring_pos;
    }
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 6.28318530717958647692f;
    float diff = std::fmod(target_rad - ws->spring_pos, kTwoPi);
    if (diff > kPi)  diff -= kTwoPi;
    if (diff < -kPi) diff += kTwoPi;
    const float unwrapped_target = ws->spring_pos + diff;
    if (!advance_motion(ctx, &ws->spring_pos, &ws->spring_vel, unwrapped_target, spec)) {
        ws->spring_pos = target_rad;
    }
    return ws->spring_pos;
}

Vec2 scroll_delta() noexcept {
    const Context* ctx = current_context();
    return (ctx && ctx->in_frame) ? ctx->active_ui->frame_scroll_delta : Vec2{};
}

Vec2 scroll_max(uint32_t element_id) noexcept {
    const Context* ctx = current_context();
    if (!ctx || element_id == 0u) {
        return {};
    }
    const WidgetState* ws = state_map_find(&ctx->active_ui->states, element_id);
    if (!ws) {
        return {};
    }
    if (ws->is_virtual_list) {
        return Vec2{0.0f, ws->cold ? static_cast<float>(ws->cold->vscroll_max_y) : 0.0f};
    }
    return ws->scroll_max;
}

Vec2 scroll_offset(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || element_id == 0u) {
        return {};
    }
    const WidgetState* ws = state_map_find(&ctx->active_ui->states, element_id);
    return ws ? ws->scroll_pos : Vec2{};
}

void set_scroll_offset(uint32_t element_id, Vec2 offset, bool stop_momentum) noexcept {
    Context* ctx = current_context();
    if (!ctx || element_id == 0u) {
        return;
    }
    if (WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, element_id, ctx->active_ui->frame_index)) {
        ws->is_user_element = true;
        ws->scroll_pos = Vec2{
            (ws->scroll_max.x > 0.0f) ? clamp_range(offset.x, 0.0f, ws->scroll_max.x) : ((offset.x > 0.0f) ? offset.x : 0.0f),
            (ws->scroll_max.y > 0.0f) ? clamp_range(offset.y, 0.0f, ws->scroll_max.y) : ((offset.y > 0.0f) ? offset.y : 0.0f),
        };
        ws->scroll_stick_initialized = true;
        ws->scroll_stuck_bottom = (ws->scroll_max.y <= 0.5f) || (ws->scroll_pos.y >= ws->scroll_max.y - 2.0f);
        if (stop_momentum) {
            ws->scroll_vel = {};
        }
        request_redraw(ctx);
    }
}

void scroll_into_view(uint32_t scroll_container_id, uint32_t item_id, float margin_px) noexcept {
    Context* ctx = current_context();
    if (!ctx || scroll_container_id == 0u || item_id == 0u || !std::isfinite(margin_px)) {
        return;
    }
    const float m = (margin_px > 0.0f) ? margin_px : 0.0f;
    const Rect cr = element_bounds(scroll_container_id);
    const Rect ir = element_bounds(item_id);
    if (cr.w <= 0.0f || cr.h <= 0.0f || ir.w <= 0.0f || ir.h <= 0.0f) {
        return;
    }
    const WidgetState* ws = state_map_find(&ctx->active_ui->states, scroll_container_id);
    if (!ws) {
        return;
    }
    Vec2 next = ws->scroll_pos;
    bool moved = false;
    if (ws->scroll_max.y > 0.5f) {
        const float top_lim = cr.y + m;
        const float bot_lim = cr.y + cr.h - m;
        if (ir.y < top_lim) {
            next.y -= (top_lim - ir.y);
            moved = true;
        } else if (ir.y + ir.h > bot_lim) {
            next.y += (ir.y + ir.h) - bot_lim;
            moved = true;
        }
        next.y = clamp_range(next.y, 0.0f, ws->scroll_max.y);
    }
    if (ws->scroll_max.x > 0.5f) {
        const float left_lim = cr.x + m;
        const float right_lim = cr.x + cr.w - m;
        if (ir.x < left_lim) {
            next.x -= (left_lim - ir.x);
            moved = true;
        } else if (ir.x + ir.w > right_lim) {
            next.x += (ir.x + ir.w) - right_lim;
            moved = true;
        }
        next.x = clamp_range(next.x, 0.0f, ws->scroll_max.x);
    }
    if (moved) {
        set_scroll_offset(scroll_container_id, next, true);
    }
}

bool scroll_stuck_to_bottom(uint32_t element_id) noexcept {
    const Context* ctx = current_context();
    if (!ctx || element_id == 0u) {
        return false;
    }
    const WidgetState* ws = state_map_find(&ctx->active_ui->states, element_id);
    if (!ws) {
        return false;
    }
    if (ws->is_virtual_list) {
        return ws->cold ? ws->cold->vlist_stuck_bottom : true;
    }
    return (ws->is_scroll_y || ws->scroll_stick_initialized) ? ws->scroll_stuck_bottom : false;
}

void set_scroll_stuck_to_bottom(uint32_t element_id, bool stuck) noexcept {
    Context* ctx = current_context();
    if (!ctx || element_id == 0u) {
        return;
    }
    if (WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, element_id, ctx->active_ui->frame_index)) {
        ws->is_user_element = true;
        ws->scroll_stick_initialized = true;
        ws->scroll_stuck_bottom = stuck;
        if (stuck) {
            ws->scroll_pos.y = ws->scroll_max.y;
            ws->scroll_vel.y = 0.0f;
        }
        if (WidgetCold* vc = ensure_widget_cold(ws)) {
            vc->vlist_stick_initialized = true;
            vc->vlist_stuck_bottom = stuck;
            if (stuck) {
                vc->vscroll_pos_y = vc->vscroll_max_y;
                vc->vscroll_vel_y = 0.0;
                vc->vlist_pending_scroll = false;
            }
        }
        request_redraw(ctx);
    }
}

} // namespace etch
