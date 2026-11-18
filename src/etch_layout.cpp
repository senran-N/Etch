// Clay element tree, vector icons, text shaping/rendering, widgets & scroll containers.
// Compiled via src/etch_unity.cpp.

namespace etch {

void emit_vector_icon_quad(
    Context* ctx,
    uint16_t icon_id,
    Rect target_px,
    Color color,
    const ClipEntry& clip, float angle_rad) noexcept
{
    if (icon_id >= kMaxVectorIcons || color.a == 0u || target_px.w <= 0.0f || target_px.h <= 0.0f) {
        return;
    }
    const VectorIconEntry& ic = ctx->atlas.icons[icon_id];
    if (!ic.loaded || !ic.has_outline || ic.view_size.x <= 0.0f || ic.view_size.y <= 0.0f) {
        return;
    }
    const float sx = target_px.w / ic.view_size.x;
    const float sy = target_px.h / ic.view_size.y;
    const float s = (sx < sy) ? sx : sy;
    const float w = ic.view_size.x * s;
    const float h = ic.view_size.y * s;
    const Rect quad = {
        .x = target_px.x + 0.5f * (target_px.w - w),
        .y = target_px.y + 0.5f * (target_px.h - h),
        .w = w,
        .h = h,
    };
    push_prim(
        ctx,
        Prim::make_slug_glyph(
            quad,
            clip.rect,
            color,
            ic.band_offset,
            ic.num_hbands,
            ic.num_vbands,
            clip.radius,
            PrimFlag_None, angle_rad));
}

DrawCmdNode* append_open_box_draw_cmd(Context* ctx, bool foreground) noexcept {
    if (!ctx || !ctx->in_frame || !ctx->active_ui->clay_ctx) {
        return nullptr;
    }
    BoxExtra* extra = ctx->layout_draw_target;
    if (!extra) {
        Clay_SetCurrentContext(ctx->active_ui->clay_ctx);
        if (ctx->active_ui->clay_ctx->openLayoutElementStack.length < 2) {
            return nullptr;
        }
        Clay_LayoutElement* open_elem = Clay__GetOpenLayoutElement();
        if (!open_elem || open_elem->isTextElement) {
            return nullptr;
        }
        extra = static_cast<BoxExtra*>(open_elem->config.userData);
        if (!extra) {
            extra = ctx->frame_arena.alloc<BoxExtra>();
            if (!extra) {
                return nullptr;
            }
            extra->layer = ctx->active_ui->current_layer;
            extra->kind = CustomCmdKind::Box;
            open_elem->config.userData = extra;
        }
    }
    if (!ctx->layout_draw_target && !foreground) {
        Clay_LayoutElement* open_elem = Clay__GetOpenLayoutElement();
        if (open_elem->config.custom.customData == nullptr) {
            open_elem->config.custom.customData = extra;
        }
    } else if (!ctx->layout_draw_target) {
        Clay_LayoutElement* open_elem = Clay__GetOpenLayoutElement();
        const Clay_BorderWidth& bw = open_elem->config.border.width;
        if (bw.left == 0u && bw.right == 0u && bw.top == 0u && bw.bottom == 0u && bw.betweenChildren == 0u) {
            open_elem->config.border.color = Clay_Color{255.0f, 255.0f, 255.0f, 1.0f};
            open_elem->config.border.width = Clay_BorderWidth{1u, 1u, 1u, 1u, 0u};
        }
    }
    DrawCmdNode* node = ctx->frame_arena.alloc<DrawCmdNode>();
    if (!node) {
        return nullptr;
    }
    *node = DrawCmdNode{};
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
    const float z = (cws->canvas_zoom > 1e-4f) ? cws->canvas_zoom : 1.0f;
    return Vec2{
        (screen.x - cws->last_box_logical.x - cws->canvas_pan.x) / z,
        (screen.y - cws->last_box_logical.y - cws->canvas_pan.y) / z,
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

void emit_draw_text_cmd(
    Context* ctx,
    const TextDrawDesc& tc,
    Vec2 origin,
    float scale,
    const ClipEntry& clip, const uint32_t* terminal_columns = nullptr, float terminal_cell_width = 0) noexcept
{
    if (!ctx || !ctx->atlas.font_valid || !tc.text.chars || tc.text.length == 0u || tc.size <= 0.0f || tc.color.a == 0u) {
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

        float pen_x = origin.x + start_x_logical * scale;
        const float base_y = origin.y + (start_y_logical + static_cast<float>(line_idx) * line_h_logical) * scale + asc_px;

        const ClipEntry text_clip = clip;
        const uint32_t first_text_prim = ctx->prim_count;
        float clip_left = origin.x + tc.pos.x * scale;
        if (tc.align_x == AlignX::Right) clip_left -= tc.max_width_px * scale;
        else if (tc.align_x == AlignX::Center) clip_left -= tc.max_width_px * scale * 0.5f;
        if (elided && tc.ellipsis) {
            emit_elided_line(ctx, tc.text.chars + pos, seg_len, tc.font_id, tc.size, tc.letter_spacing,
                pen_x / scale, base_y / scale, tc.max_width_px, text_clip, tc.color, scale);
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
                    pen_x = origin.x + (start_x_logical + float(terminal_columns[run_pos + g.cluster]) * terminal_cell_width) * scale;
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
                    tc.color);
                pen_x += g.x_advance_1px * phys_px + ls_px;
            }
            run_pos = run_end;
        }

        if (tc.max_width_px > 0) {
            for (uint32_t i = first_text_prim; i < ctx->prim_count; ++i) {
                Prim& p = ctx->prims[i];
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
    ctx->render_clip_index=clip.indexed ? clip.shape : 0;
    const Vec2 origin = { box_px.x, box_px.y };
    const auto* previous_layer=ctx->active_render_layer;
    for (const DrawCmdNode* node = head; node != nullptr; node = node->next) {
        ctx->active_render_layer=node->layer;
        const float scale = node->world_scale * dpi;
        switch (node->kind) {
            case DrawCmdKind::Callback: {
                BoxExtra target = {};
                ctx->layout_draw_target = &target;
                ctx->layout_draw_scale = node->world_scale;
                push_theme(*node->callback_theme);
                node->callback({0,0,box_px.w / scale,box_px.h / scale},node->callback_data);
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
                const bool has_grad = (rd.fill_end.pack_rgba8() != 0u) || rd.radial_gradient;
                if (std::fabs(rd.angle_rad) > 1e-5f) {
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
                if (!node->poly_points || node->poly_count < 2u || node->poly_color.a == 0u) {
                    break;
                }
                const float hw = 0.5f * ((node->poly_thickness * scale > 0.75f) ? (node->poly_thickness * scale) : 0.75f);
                const float glow = node->poly_glow_px * scale;
                const uint32_t segs = (node->poly_closed && node->poly_count >= 3u)
                    ? node->poly_count
                    : (node->poly_count - 1u);
                for (uint32_t i = 0u; i < segs; ++i) {
                    const Vec2 a = origin + node->poly_points[i] * scale;
                    const Vec2 b = origin + node->poly_points[(i + 1u) % node->poly_count] * scale;
                    const Vec2 mid = (a + b) * 0.5f;
                    push_prim(
                        ctx,
                        Prim::make_bezier_wire(
                            a, mid, b, clip.rect, node->poly_color, node->poly_color, hw, node->poly_glow_color, glow, clip.radius));
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
                emit_draw_text_cmd(ctx, node->text_cmd, origin, scale, clip, node->terminal_columns, node->terminal_cell_width);
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
                if (ctx->active_ui->pixel_snap) target_px = snap_pixel_rect(target_px);
                Vec2 uv_min = { im.uv_rect.x, im.uv_rect.y };
                Vec2 uv_max = { im.uv_rect.x + im.uv_rect.w, im.uv_rect.y + im.uv_rect.h };
                if (im.sprite_id != kInvalidSprite && !resolve_sprite_uv_rect(ctx, im.sprite_id, uv_min, uv_max, &uv_min, &uv_max)) break;
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
                        (im.sprite_id != kInvalidSprite || surface_texture_premultiplied(ctx, im.texture)) ? PrimFlag_Premultiplied : PrimFlag_None,
                        clip.radius),
                    im.sprite_id != kInvalidSprite ? nullptr : im.texture);
                break;
            }
        }
    }
    ctx->render_clip_index=previous_clip;
    ctx->active_render_layer=previous_layer;
}

// End internal unity-build helpers.

bool needs_redraw(const Context* ctx) noexcept {
    if (!ctx) return false;
    if (ctx->headless_ui.redraw_budget || ctx->headless_ui.active_anim_count) return true;
    for (uint32_t i = 0; i < ctx->claimed_window_cap; ++i) {
        if (!ctx->claimed_windows[i]) continue;
        const ClaimedWindowSlot& slot = *ctx->claimed_windows[i];
        if (slot.window && (slot.ui.redraw_budget || slot.ui.active_anim_count)) return true;
    }
    return shader_surface_timeout(ctx, nullptr, true, -1) == 0;
}

void request_redraw(Context* ctx_arg) noexcept {
    Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (ctx) {
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
    } else {
        ctx->active_ui->redraw_budget = 2u;
    }
}

bool wait_events(Context* ctx, SDL_Event* first_event_out, int32_t timeout_ms) noexcept {
    if (!first_event_out) {
        return false;
    }
    WindowUiState* caret_ui = ctx ? window_ui(ctx, ctx->ime_active_window) : nullptr;
    if (caret_ui && caret_ui->ime_focused_id != 0u && caret_ui->caret_blink_cycles < 20u) {
        const uint64_t now = SDL_GetTicks();
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
    if (needs_redraw(ctx)) return SDL_PollEvent(first_event_out);
    if (ctx) timeout_ms = shader_surface_timeout(ctx, nullptr, true, timeout_ms);
    if (timeout_ms < 0) {
        return SDL_WaitEvent(first_event_out);
    }
    return SDL_WaitEventTimeout(first_event_out, timeout_ms);
}

bool verify_caret_deadline() noexcept {
    Context* ctx=create_context({.headless=true});
    if(!ctx)return false;
    auto& ui=ctx->headless_ui;
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

static void process_window_event(Context* ctx, const SDL_Event& event) noexcept {
    if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
        if (SDL_Window* window = SDL_GetWindowFromID(event.window.windowID)) sync_desktop_window(window);
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
            request_redraw(ctx);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                ctx->active_ui->event_pointer_pos = window_to_logical_pos_for(
                    ctx, SDL_GetWindowFromID(event.button.windowID), event.button.x, event.button.y);
                ctx->active_ui->event_pointer_down = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                ctx->active_ui->has_event_pointer = true;
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
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
                ctx->active_ui->event_right_down = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
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
            if (event.wheel.mouse_x >= 0.0f && event.wheel.mouse_y >= 0.0f) {
                ctx->active_ui->event_pointer_pos = window_to_logical_pos_for(
                    ctx, SDL_GetWindowFromID(event.wheel.windowID), event.wheel.mouse_x, event.wheel.mouse_y);
                ctx->active_ui->has_event_pointer = true;
            }
            request_redraw(ctx);
            break;
        }
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
                        mws->canvas_marquee_active = false;
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
                if (event.key.key == SDLK_TAB && !(tab_ws && (tab_ws->edit_text_buffer || tab_ws->suppress_popup_keys))) {
                    ctx->active_ui->pending_tab_delta += ((event.key.mod & SDL_KMOD_SHIFT) != 0u) ? -1 : 1;
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
                    mws->canvas_marquee_active = false;
                }
                ctx->active_ui->marquee_canvas_id = 0u;
            }
            ctx->active_ui->drag_active = false;
            ctx->active_ui->drag_drop_pending = false;
            ctx->active_ui->drag_accepted_this_frame = false;
            ctx->active_ui->space_down = false;
            ctx->active_ui->active_id = 0u;
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
    if (!ctx || ctx->in_frame || !ctx->active_ui->clay_ctx) {
        return;
    }
    Clay_SetCurrentContext(ctx->active_ui->clay_ctx);
    set_current_context(ctx);
    ctx->in_frame = true;
    ctx->theme_depth = 0u;
    ctx->theme_overflow = 0u;
    ctx->active_ui->bloom_intensity = std::isfinite(desc.bloom_intensity) ? clamp_range(desc.bloom_intensity,0,8) : 0;
    ctx->active_ui->bloom_threshold = std::isfinite(desc.bloom_threshold) ? clamp_range(desc.bloom_threshold,0,1) : .7f;
    ctx->draw_list_window = ctx->active_window;
    ctx->active_ui->current_layer=nullptr;
    Clay_SetCullingEnabled(true);
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
    ctx->active_ui->elapsed_seconds += desc.dt_seconds*ctx->time_scale;

    if (ctx->active_ui->redraw_budget > 0u) {
        --ctx->active_ui->redraw_budget;
    }
    ctx->active_ui->sim_dt = (desc.dt_seconds <= 0.0f)
        ? 0.0f
        : ((desc.dt_seconds > 0.1f) ? (1.0f / 60.0f) : desc.dt_seconds);
    ctx->active_ui->sim_dt *= ctx->time_scale;
    ctx->active_ui->active_anim_count = 0u;
    ctx->active_ui->desired_cursor = Cursor::Arrow;

    ctx->active_ui->prev_ime_focused_id = ctx->active_ui->ime_focused_id;
    ctx->active_ui->ime_focused_id = 0u;
    ctx->active_ui->ime_focused_password = false;
    ctx->active_ui->frame_text_op_count = ctx->active_ui->pending_text_op_count;
    if (ctx->active_ui->frame_text_op_count > 0u) {
        std::memcpy(ctx->active_ui->frame_text_ops, ctx->active_ui->pending_text_ops, size_t(ctx->active_ui->frame_text_op_count) * sizeof(PendingTextOp));
    }
    ctx->active_ui->frame_text_utf8_len = ctx->active_ui->pending_text_utf8_len;
    if (ctx->active_ui->pending_text_utf8_len > 0u) {
        std::memcpy(ctx->active_ui->frame_text_utf8, ctx->active_ui->pending_text_utf8, ctx->active_ui->pending_text_utf8_len);
    }
    ctx->active_ui->pending_text_op_count = 0u;
    ctx->active_ui->pending_text_utf8_len = 0u;

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
            if (desc.key_pressed_code == SDLK_TAB && !(tab_ws && (tab_ws->edit_text_buffer || tab_ws->suppress_popup_keys))) {
                ctx->active_ui->pending_tab_delta += ((ctx->active_ui->frame_key_mods & Mod_Shift) != 0u) ? -1 : 1;
            } else if (ctx->active_ui->frame_text_op_count < kMaxTextOpsPerFrame) {
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
    ctx->clip_shape_count=1;ctx->render_clip_index=0;
    ctx->dyn_slug_curve_count = 0;
    ctx->dyn_slug_band_count = 0;
    ctx->prim_count = 0u;
    ctx->batch_count = 0u;
    ctx->blur_split_count = 0u;
    ctx->layer_has_bg_prims = true;
    ctx->active_ui->split_depth = 0u;
    ctx->active_ui->canvas_depth = 0u;
    ctx->active_ui->last_canvas_clip_px = {};
    ctx->active_ui->last_canvas_clip_radius_px = 0.0f;
    ctx->active_ui->pending_chrome_drag_count = 0u;
    ctx->active_ui->pending_chrome_client_count = 0u;
    ctx->active_ui->drag_drop_pending = false;
    ctx->active_ui->drag_accepted_this_frame = false;
    if (ctx->active_ui->marquee_finished_canvas_id != 0u) {
        if (WidgetState* mws = state_map_find(&ctx->active_ui->states, ctx->active_ui->marquee_finished_canvas_id)) {
            mws->canvas_marquee_finished = false;
            mws->canvas_marquee_world = {};
        }
        ctx->active_ui->marquee_finished_canvas_id = 0u;
    }

    const float dpi = (desc.dpi_scale > 0.0f) ? desc.dpi_scale : 1.0f;
    ctx->active_ui->viewport_w = (desc.viewport_w > 0u) ? desc.viewport_w : 1u;
    ctx->active_ui->viewport_h = (desc.viewport_h > 0u) ? desc.viewport_h : 1u;
    ctx->active_ui->dpi_scale = dpi;
    ctx->active_ui->pixel_snap = desc.pixel_snap;
    if (std::fabs(dpi - ctx->active_ui->applied_dpi_scale) > 1e-4f) {
        ctx->active_ui->applied_dpi_scale = dpi;
        ctx->active_ui->applied_ime_rect = {-1, -1, 0, 0};
        ctx->active_ui->applied_ime_cursor = -1;
        if (ctx->active_ui->redraw_budget < 1u) {
            ctx->active_ui->redraw_budget = 1u;
        }
    }

    const Clay_Dimensions logical_dims = {
        .width = static_cast<float>(ctx->active_ui->viewport_w) / dpi,
        .height = static_cast<float>(ctx->active_ui->viewport_h) / dpi,
    };
    Clay_SetLayoutDimensions(logical_dims);

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

    const bool pointer_down = desc.pointer_down || ctx->active_ui->event_pointer_down;
    const bool pressed_this_frame = pointer_down && !ctx->active_ui->prev_pointer_down;
    const bool released_this_frame = !pointer_down && ctx->active_ui->prev_pointer_down;
    ctx->active_ui->prev_pointer_down = pointer_down;
    ctx->active_ui->left_pressed_this_frame = pressed_this_frame;
    if (pressed_this_frame) ctx->active_ui->keyboard_focus_active = false;
    if (pressed_this_frame) {
        if (desc.pointer_clicks > 0u) {
            ctx->active_ui->last_mouse_clicks = desc.pointer_clicks;
        } else if (!ctx->active_ui->event_pointer_down) {
            ctx->active_ui->last_mouse_clicks = 1u;
        }
    }

    const bool middle_down = ctx->active_ui->event_middle_down;
    const bool middle_pressed_this_frame = middle_down && !ctx->active_ui->prev_pan_button_down;
    ctx->active_ui->prev_pan_button_down = middle_down;

    const bool right_down = desc.right_pointer_down || ctx->active_ui->event_right_down;
    const bool right_pressed_this_frame = right_down && !ctx->active_ui->prev_right_down;
    const bool right_released_this_frame = !right_down && ctx->active_ui->prev_right_down;
    ctx->active_ui->prev_right_down = right_down;
    ctx->active_ui->right_pressed_this_frame = right_pressed_this_frame;
    if (right_pressed_this_frame || middle_pressed_this_frame) ctx->active_ui->keyboard_focus_active = false;
    ctx->active_ui->right_clicked_any_this_frame = false;

    const Vec2 scroll = (desc.scroll_delta.x != 0.0f || desc.scroll_delta.y != 0.0f)
        ? desc.scroll_delta
        : ctx->active_ui->event_scroll_delta;
    ctx->active_ui->frame_scroll_delta = scroll;
    ctx->active_ui->event_scroll_delta = {};

    // Update Clay hit-test results against the previous frame's tree BEFORE Clay_BeginLayout() resets it.
    Clay_SetPointerState(ctx->active_ui->has_pointer ? Clay_Vector2{pointer_pos.x, pointer_pos.y} : Clay_Vector2{-FLT_MAX,-FLT_MAX}, pointer_down);
    Clay_UpdateScrollContainers(false, Clay_Vector2{0.0f, 0.0f}, 0.0f);

    state_map_clear_flags(&ctx->active_ui->states, true, false);
    const Clay_ElementIdArray over = Clay_GetPointerOverIds();
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
                        : Clay_PointerOver(make_clay_id(ctx->active_ui->right_active_id));
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
            for (int32_t i = over.length - 1; i >= 0; --i) {
                WidgetState* ws = state_map_find(&ctx->active_ui->states, over.internalArray[i].id);
                if (!ws || !ws->is_user_element) continue;
                for(uint32_t axis=0;axis<2;++axis) {
                    const bool horizontal=axis==1;
                    const ScrollbarGeom sb=horizontal ? compute_hscrollbar_geom(ws) : compute_vscrollbar_geom(ws);
                    if(!sb.visible || !sb.hit_track.contains(pointer_pos)) continue;
                    ctx->active_ui->active_id=id_index(ws->key,horizontal ? kScrollbarXTag : kScrollbarYTag);
                    ws->scroll_vel={};ws->vscroll_vel_y=0;
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
                for (int32_t i = over.length - 1; i >= 0; --i) {
                    const uint32_t oid = over.internalArray[i].id;
                    WidgetState* ws = state_map_find(&ctx->active_ui->states, oid);
                    if (ws && ws->is_user_element) {
                        if (top_hit == 0u) {
                            top_hit = oid;
                            ws->pressed_down = true;
                            ws->pressed_this_frame = true;
                            if (ctx->active_ui->last_mouse_clicks >= 2u) {
                                ws->double_clicked_this_frame = true;
                            }
                            if (ws->is_canvas && ws->canvas_interactive) {
                                if (ws->canvas_marquee_enabled) {
                                    if (ctx->active_ui->marquee_canvas_id == 0u) {
                                        ctx->active_ui->marquee_canvas_id = oid;
                                        const Vec2 start_w = canvas_screen_to_world(ws, pointer_pos);
                                        ws->canvas_marquee_start_world = start_w;
                                        ws->canvas_marquee_world = Rect{ start_w.x, start_w.y, 0.0f, 0.0f };
                                        ws->canvas_marquee_active = true;
                                        ws->canvas_marquee_finished = false;
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
            if (WidgetState* mws = state_map_find(&ctx->active_ui->states, ctx->active_ui->marquee_canvas_id)) {
                const Vec2 p0 = mws->canvas_marquee_start_world;
                const Vec2 p1 = canvas_screen_to_world(mws, pointer_pos);
                const float x0 = (p0.x < p1.x) ? p0.x : p1.x;
                const float y0 = (p0.y < p1.y) ? p0.y : p1.y;
                const float x1 = (p0.x > p1.x) ? p0.x : p1.x;
                const float y1 = (p0.y > p1.y) ? p0.y : p1.y;
                mws->canvas_marquee_world = Rect{ x0, y0, x1 - x0, y1 - y0 };
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
            if (WidgetState* mws = state_map_find(&ctx->active_ui->states, ctx->active_ui->marquee_canvas_id)) {
                if (ctx->active_ui->has_pointer) {
                    const Vec2 p0 = mws->canvas_marquee_start_world;
                    const Vec2 p1 = canvas_screen_to_world(mws, pointer_pos);
                    const float x0 = (p0.x < p1.x) ? p0.x : p1.x;
                    const float y0 = (p0.y < p1.y) ? p0.y : p1.y;
                    const float x1 = (p0.x > p1.x) ? p0.x : p1.x;
                    const float y1 = (p0.y > p1.y) ? p0.y : p1.y;
                    mws->canvas_marquee_world = Rect{ x0, y0, x1 - x0, y1 - y0 };
                }
                mws->canvas_marquee_active = false;
                if (mws->canvas_marquee_world.w >= 3.0f ||
                    mws->canvas_marquee_world.h >= 3.0f ||
                    ctx->active_ui->left_max_drag_dist_sq >= 9.0f)
                {
                    mws->canvas_marquee_finished = true;
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
                        : Clay_PointerOver(make_clay_id(ctx->active_ui->active_id));
                    if (ws->is_user_element && over_active) {
                        ws->clicked_this_frame = true;
                    }
                }
            }
        }
        state_map_clear_flags(&ctx->active_ui->states, false, true);
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
        if (WidgetState* cws = state_map_find(&ctx->active_ui->states, ctx->active_ui->pan_canvas_id)) {
            if (!pan_started_this_frame && ctx->active_ui->had_pointer && ctx->active_ui->has_pointer) {
                cws->canvas_pan.x += pointer_pos.x - ctx->active_ui->prev_pointer_pos.x;
                cws->canvas_pan.y += pointer_pos.y - ctx->active_ui->prev_pointer_pos.y;
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
        for (int32_t i = over.length - 1; i >= 0; --i) {
            WidgetState* ws = state_map_find(&ctx->active_ui->states, over.internalArray[i].id);
            if (!ws || !ws->is_user_element) {
                continue;
            }
            if (ws->is_multiline_input && ws->edit_max_scroll_y>0) {
                ws->edit_scroll_y=clamp_range(ws->edit_scroll_y-scroll.y*48,0,ws->edit_max_scroll_y);
                ws->edit_reveal_caret=false;request_redraw(ctx);break;
            }
            if (ws->is_virtual_list) {
                if (!fallback_scroll) {
                    fallback_scroll = ws;
                }
                const bool can_vy = ws->vscroll_max_y > 0.5 &&
                    ((scroll.y < 0.0f && ws->vscroll_pos_y < ws->vscroll_max_y - 0.5) ||
                     (scroll.y > 0.0f && ws->vscroll_pos_y > 0.5));
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
            if (ws->is_canvas && ws->canvas_interactive) {
                target_canvas = ws;
                if (scroll.y != 0.0f) {
                    break;
                }
            } else if (!target_canvas && ws->owner_canvas_id != 0u) {
                WidgetState* cws = state_map_find(&ctx->active_ui->states, ws->owner_canvas_id);
                if (cws && cws->is_canvas && cws->canvas_interactive) {
                    target_canvas = cws;
                }
            }
        }
        if (target_scroll) {
            if (target_scroll->is_virtual_list) {
                target_scroll->vlist_pending_scroll = false;
                if (ctx->active_ui->sim_dt <= 0.0f) {
                    target_scroll->vscroll_pos_y = clamp_double(
                        target_scroll->vscroll_pos_y - static_cast<double>(scroll.y) * 48.0,
                        0.0,
                        target_scroll->vscroll_max_y);
                } else {
                    target_scroll->vscroll_vel_y += static_cast<double>(-scroll.y) * 560.0;
                }
            } else {
                if (ctx->active_ui->sim_dt <= 0.0f) {
                    if (target_scroll->is_scroll_x) {
                        target_scroll->scroll_pos.x = clamp_range(target_scroll->scroll_pos.x - scroll.x * 48.0f, 0.0f, target_scroll->scroll_max.x);
                    }
                    if (target_scroll->is_scroll_y) {
                        target_scroll->scroll_pos.y = clamp_range(target_scroll->scroll_pos.y - scroll.y * 48.0f, 0.0f, target_scroll->scroll_max.y);
                    }
                } else {
                    if (target_scroll->is_scroll_x) {
                        target_scroll->scroll_vel.x += (-scroll.x) * 560.0f;
                    }
                    if (target_scroll->is_scroll_y) {
                        target_scroll->scroll_vel.y += (-scroll.y) * 560.0f;
                    }
                }
            }
            request_redraw(ctx);
        } else if (target_canvas && scroll.y != 0.0f) {
            const float old_z = (target_canvas->canvas_zoom > 1e-4f) ? target_canvas->canvas_zoom : 1.0f;
            const float factor = std::pow(1.12f, scroll.y);
            const float new_z = clamp_range(old_z * factor, target_canvas->canvas_min_zoom, target_canvas->canvas_max_zoom);
            const Rect vp = target_canvas->last_box_logical;
            if (vp.w > 0.0f && vp.h > 0.0f && ctx->active_ui->has_pointer) {
                const Vec2 local_ptr = Vec2{ pointer_pos.x - vp.x, pointer_pos.y - vp.y };
                const Vec2 world_under_ptr = (local_ptr - target_canvas->canvas_pan) * (1.0f / old_z);
                target_canvas->canvas_zoom = new_z;
                target_canvas->canvas_pan = local_ptr - world_under_ptr * new_z;
            } else {
                target_canvas->canvas_zoom = new_z;
            }
            request_redraw(ctx);
        } else if (fallback_scroll) {
            if (fallback_scroll->is_virtual_list) {
                fallback_scroll->vlist_pending_scroll = false;
                if (ctx->active_ui->sim_dt <= 0.0f) {
                    fallback_scroll->vscroll_pos_y = clamp_double(
                        fallback_scroll->vscroll_pos_y - static_cast<double>(scroll.y) * 48.0,
                        0.0,
                        fallback_scroll->vscroll_max_y);
                } else {
                    fallback_scroll->vscroll_vel_y += static_cast<double>(-scroll.y) * 560.0;
                }
            } else {
                if (ctx->active_ui->sim_dt <= 0.0f) {
                    if (fallback_scroll->is_scroll_x) {
                        fallback_scroll->scroll_pos.x = clamp_range(fallback_scroll->scroll_pos.x - scroll.x * 48.0f, 0.0f, fallback_scroll->scroll_max.x);
                    }
                    if (fallback_scroll->is_scroll_y) {
                        fallback_scroll->scroll_pos.y = clamp_range(fallback_scroll->scroll_pos.y - scroll.y * 48.0f, 0.0f, fallback_scroll->scroll_max.y);
                    }
                } else {
                    if (fallback_scroll->is_scroll_x) {
                        fallback_scroll->scroll_vel.x += (-scroll.x) * 560.0f;
                    }
                    if (fallback_scroll->is_scroll_y) {
                        fallback_scroll->scroll_vel.y += (-scroll.y) * 560.0f;
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
                moving |= step_scroll_axis(&ws.scroll_pos.y, &ws.scroll_vel.y, ws.scroll_max.y, decay, step_factor);
            }
            if (ws.is_virtual_list) {
                moving |= step_scroll_axis_d(
                    &ws.vscroll_pos_y,
                    &ws.vscroll_vel_y,
                    ws.vscroll_max_y,
                    static_cast<double>(decay),
                    static_cast<double>(step_factor));
            }
            if (moving) {
                ++ctx->active_ui->active_anim_count;
            }
        }
    }

    if (ctx->active_ui->applied_font_revision != ctx->atlas.font_revision) {
        Clay_ResetMeasureTextCache();
        ctx->active_ui->applied_font_revision = ctx->atlas.font_revision;
    }

    Clay_BeginLayout();
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
    desc.top_rim_alpha=std::isfinite(desc.top_rim_alpha) ? clamp_range(desc.top_rim_alpha,0,1) : 0;
    if(desc.border_beam.color.a && desc.border_beam.beam_active && desc.border_beam.speed!=0 && !ctx->reduced_motion && ctx->time_scale>0) ++ctx->active_ui->active_anim_count;
    if(!std::isfinite(desc.opacity)) desc.opacity=1;
    desc.opacity=std::fmin(1.f,std::fmax(0.f,desc.opacity));
    if(!std::isfinite(desc.hover_scale) || desc.hover_scale<=0) desc.hover_scale=1;
    if(!std::isfinite(desc.press_scale) || desc.press_scale<=0) desc.press_scale=1;
    if (wscale != 1.0f) {
        desc.width = scale_sizing(desc.width, wscale);
        desc.height = scale_sizing(desc.height, wscale);
        desc.padding = scale_padding(desc.padding, wscale);
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
        desc.radius = scale_corners_uniform(desc.radius, wscale);
        desc.shadow_blur *= wscale;
        desc.shadow_spread *= wscale;
        desc.shadow_offset.x *= wscale;
        desc.shadow_offset.y *= wscale;
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
            box_ws->is_text_input_writable = false;
            box_ws->owner_canvas_id = (ctx->active_ui->canvas_depth > 0u)
                ? ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth - 1u].id
                : 0u;
        }
        if (desc.focusable && reserve_focus(ctx->active_ui)) {
            ctx->active_ui->focus_order[ctx->active_ui->focus_count++] = desc.id;
            if (ctx->active_ui->modal_needs_focus && focus_scope_contains(*ctx->active_ui, ctx->active_ui->modal_scope)) {
                ctx->active_ui->focus_id = desc.id;
                ctx->active_ui->modal_needs_focus = false;
                clear_ime_composition(ctx);
            }
        }
        Clay__OpenElementWithId(make_clay_id(desc.id));
    } else {
        Clay__OpenElement();
    }

    if(box_ws && (desc.scroll_x || desc.scroll_y)) {
        PendingA11yAction sc_cmd = {};
        float direction = 0.0f;
        while(a11y_take_any(desc.id,12u,&sc_cmd)) direction += (sc_cmd.action==A11yAction::Increment)?1.0f:-1.0f;
        if(direction != 0.0f) {
            if(desc.scroll_y)box_ws->scroll_pos.y=clamp_range(box_ws->scroll_pos.y+direction*40,0,box_ws->scroll_max.y);
            else box_ws->scroll_pos.x=clamp_range(box_ws->scroll_pos.x+direction*40,0,box_ws->scroll_max.x);
            request_redraw(ctx);
        }
    }
    const Theme& thm = active_theme(ctx);
    BoxDesc eff_desc = desc;
    eff_desc.background = resolve_theme_color(eff_desc.background, thm.bg_surface);
    eff_desc.background_end = resolve_theme_color(eff_desc.background_end, thm.bg_elevated);
    eff_desc.border_color = resolve_theme_color(eff_desc.border_color, thm.border_subtle);
    eff_desc.shadow_color = resolve_theme_color(eff_desc.shadow_color, thm.shadow);
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
    const bool is_floating_blocker = (eff_desc.attach_to != AttachTo::None && !eff_desc.pointer_passthrough);
    const bool has_beam=eff_desc.border_beam.color.a>0 || eff_desc.border_beam.base_color.a>0 || eff_desc.top_rim_alpha>0;
    const bool has_border = (eff_desc.border_color.a > 0u &&
        (eff_desc.border_width > 0.0f || has_border_sides(eff_desc))) || has_beam;
    const bool has_scrollbar = is_scroll && eff_desc.show_scrollbar;
    const bool has_visual =
        eff_desc.background.a > 0u ||
        eff_desc.background_end.a > 0u ||
        eff_desc.radial_gradient ||
        (eff_desc.shadow_color.a > 0u && eff_desc.shadow_blur > 0.0f) ||
        eff_desc.backdrop_blur > 0.0f ||
        has_border || focus_ring;

    BoxExtra* extra = nullptr;
    if (has_visual || any_clip || is_floating_blocker || eff_desc.id || ctx->active_ui->current_layer || eff_desc.opacity<1 || eff_desc.animate_layout || eff_desc.hero_id || eff_desc.hover_scale!=1 || eff_desc.press_scale!=1) {
        extra = ctx->frame_arena.alloc<BoxExtra>();
        if (extra) {
            extra->desc = eff_desc;
            extra->world_scale = wscale;
            if(eff_desc.opacity<1 || eff_desc.animate_layout || eff_desc.hero_id || eff_desc.hover_scale!=1 || eff_desc.press_scale!=1) {
                push_layer({.opacity=eff_desc.opacity});
                if(build && ctx->active_ui->current_layer!=build->previous_layer) const_cast<VisualLayerNode*>(ctx->active_ui->current_layer)->source_box=extra;
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

    Clay_ElementDeclaration decl = {};
    decl.layout.sizing.width = to_clay_sizing(desc.width);
    decl.layout.sizing.height = to_clay_sizing(desc.height);
    decl.layout.padding = Clay_Padding{
        .left = to_u16_round(desc.padding.left),
        .right = to_u16_round(desc.padding.right),
        .top = to_u16_round(desc.padding.top),
        .bottom = to_u16_round(desc.padding.bottom),
    };
    decl.aspectRatio.aspectRatio = std::isfinite(desc.aspect_ratio) && desc.aspect_ratio > 0 ? desc.aspect_ratio : 0;
    decl.layout.wrap = desc.wrap;
    decl.layout.shrinkWidth = desc.shrink_width;
    decl.layout.shrinkHeight = desc.shrink_height;
    decl.layout.crossChildGap = to_u16_round(std::isfinite(desc.row_gap) && desc.row_gap >= 0 ? desc.row_gap : desc.gap);
    decl.layout.gridColumns = desc.grid_columns > 128 ? 128 : desc.grid_columns;
    decl.layout.gridColSpan = desc.grid_col_span > 0 ? desc.grid_col_span : 1;
    const size_t track_count = desc.grid_tracks.size() > 128 ? 128 : desc.grid_tracks.size();
    if (track_count) {
        auto* tracks = ctx->frame_arena.alloc<Clay__GridTrack>(track_count);
        if (tracks) {
            for (size_t i = 0; i < track_count; ++i) {
                const GridTrack& t = desc.grid_tracks[i];
                const uint8_t kind = t.kind == SizingKind::Fixed ? 2 : t.kind == SizingKind::Percent ? 3 : t.kind == SizingKind::Fit ? 0 : 1;
                const float lo = std::isfinite(t.min_px) && t.min_px > 0 ? t.min_px * wscale : 0;
                const float hi = std::isfinite(t.max_px) && t.max_px > 0 ? ((lo > t.max_px * wscale) ? lo : t.max_px * wscale) : 0;
                const float value = std::isfinite(t.value) && t.value >= 0 ? t.value : 0;
                tracks[i] = {kind, kind == 2 ? value * wscale : value, lo, hi};
            }
            decl.layout.gridTracks = tracks;
            decl.layout.gridTrackCount = static_cast<uint16_t>(track_count);
        }
    }
    decl.layout.childGap = to_u16_round(desc.gap);
    decl.layout.childAlignment = Clay_ChildAlignment{
        .x = to_clay_align_x(desc.align_x),
        .y = to_clay_align_y(desc.align_y),
    };
    decl.layout.layoutDirection = to_clay_axis(desc.axis);
    decl.cornerRadius = to_clay_corners(desc.radius);
    decl.userData = extra;

    if ((has_visual || is_floating_blocker || desc.id || (extra && extra->layer)) && extra) {
        // Emit via CUSTOM command so shadows + gradients + borders + floating blockers render/occlude uniformly.
        decl.custom.customData = extra;
    }
    if (desc.attach_to != AttachTo::None) {
        decl.floating.attachTo = to_clay_attach_to(desc.attach_to);
        decl.floating.parentId = desc.attach_element_id;
        decl.floating.attachPoints = Clay_FloatingAttachPoints{
            .element = to_clay_attach_point(desc.anchor_element),
            .parent = to_clay_attach_point(desc.anchor_parent),
        };
        decl.floating.offset = Clay_Vector2{ desc.floating_offset.x, desc.floating_offset.y };
        decl.floating.zIndex = desc.z_index;
        decl.floating.pointerCaptureMode = desc.pointer_passthrough
            ? CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH
            : CLAY_POINTER_CAPTURE_MODE_CAPTURE;
        decl.floating.clipTo = desc.clip_to_parent
            ? CLAY_CLIP_TO_ATTACHED_PARENT
            : CLAY_CLIP_TO_NONE;
        decl.floating.clampToViewport = desc.clamp_to_viewport;
    }
    if (any_clip) {
        decl.clip.horizontal = desc.clip_children || desc.scroll_x || desc.shrink_width;
        decl.clip.vertical = desc.clip_children || desc.scroll_y || desc.shrink_height;
        if (is_scroll && box_ws) {
            decl.clip.childOffset = Clay_Vector2{ -box_ws->scroll_pos.x, -box_ws->scroll_pos.y };
        }
    }
    if ((has_border && (any_clip || has_segmented_border(eff_desc))) || (any_clip && has_scrollbar) || has_beam) {
        // Ask Clay to emit a border command after children for scrollbars and material rims.
        decl.border.color = Clay_Color{255.0f, 255.0f, 255.0f, 1.0f};
        decl.border.width = Clay_BorderWidth{1u, 1u, 1u, 1u, 0u};
    }

    Clay__ConfigureOpenElement(decl);
    const uint32_t aid = Clay__GetOpenLayoutElement()->id;
    auto* semantic = a11y_emit(ctx, desc.id ? desc.id : aid, is_scroll ? A11yRole::ScrollArea : A11yRole::Group, {}, {}, 0, (is_scroll?12u:0u)|(desc.id?2u:0u)|(desc.focusable?1u:0u));
    if (semantic) { semantic->clay_id = aid; ctx->a11y_parent = semantic; }
}

void end_box() noexcept {
    if (!current_context() || !current_context()->in_frame) {
        return;
    }
    if (current_context()->a11y_parent) current_context()->a11y_parent = current_context()->a11y_parent->parent;
    Clay__CloseElement();
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
    if (!ws) return;
    ws->is_user_element = true; ws->is_text_span = true; ws->is_selectable_text = true; ws->is_focusable = true;
    ws->edit_text_buffer = nullptr;
    ws->is_text_input_writable = false; ws->is_scroll_x = false; ws->is_scroll_y = false;
    ws->owner_canvas_id = ui.canvas_depth ? ui.canvas_stack[ui.canvas_depth-1u].id : 0u;
    ws->focus_scope = ui.modal_scope && focus_scope_contains(ui,ui.modal_scope) ? ui.modal_scope : ui.focus_scope;
    const bool allowed = !ui.modal_scope || focus_scope_contains(ui,ui.modal_scope);
    if (allowed && reserve_focus(&ui)) ui.focus_order[ui.focus_count++] = element_id;
    ws->sel_text_ptr = full_utf8; ws->sel_text_len = total_bytes;
    ws->sel_text_epoch = ctx->arena_epoch; ws->sel_text_static = is_static_backing;
    ws->edit_sel_anchor = utf8_clamp_boundary(full_utf8,total_bytes,ws->edit_sel_anchor);
    ws->edit_cursor = utf8_clamp_boundary(full_utf8,total_bytes,ws->edit_cursor);
    if (ws->span_hovered || ui.active_id == element_id) set_cursor(Cursor::IBeam);
    if (ui.left_pressed_this_frame && ui.active_id != element_id) ws->edit_sel_anchor = ws->edit_cursor = 0u;
    if (ui.focus_id != element_id || !allowed) return;
    for (uint32_t i = 0; i < ui.frame_text_op_count; ++i) {
        const auto& op = ui.frame_text_ops[i];
        if (op.kind != TextEditOpKind::KeyPress) continue;
        const bool primary = (op.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0u;
        const bool alt = (op.mod & SDL_KMOD_ALT) != 0u;
        if (primary && !alt && op.key == SDLK_A) { ws->edit_sel_anchor = 0; ws->edit_cursor = total_bytes; request_redraw(ctx); }
        if (primary && !alt && op.key == SDLK_C) {
            const uint32_t a = ws->edit_cursor < ws->edit_sel_anchor ? ws->edit_cursor : ws->edit_sel_anchor;
            const uint32_t b = ws->edit_cursor > ws->edit_sel_anchor ? ws->edit_cursor : ws->edit_sel_anchor;
            if (b > a) { const Str slice = ctx->frame_arena.push_str(full_utf8+a,b-a); if (slice.chars) (void)set_clipboard_text(slice.chars); }
        }
        if (op.key == SDLK_ESCAPE) { ws->edit_sel_anchor = ws->edit_cursor; request_redraw(ctx); }
    }
}

void text(Str content, const TextDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || !content.chars || content.length == 0u || !std::isfinite(desc.line_height_px)) {
        return;
    }

    Clay_String clay_str = {
        .isStaticallyAllocated = content.is_static,
        .length = static_cast<int32_t>(content.length),
        .chars = content.chars,
    };
    if (!content.is_static) {
        // Copy non-static transient strings into the frame arena so Clay holds valid pointers until end_frame().
        const Str copied = ctx->frame_arena.push_str(content.chars, content.length);
        if (!copied.chars) {
            return;
        }
        clay_str.chars = copied.chars;
        clay_str.length = static_cast<int32_t>(copied.length);
    }

    const float wscale = current_world_scale(ctx);
    const float raw_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 16.0f;
    const float clamped_px = clamp_range(raw_px * wscale, 0.25f, 65535.0f);
    const uint32_t size_q8 = static_cast<uint32_t>(clamped_px * 256.0f + 0.5f);
    uint32_t int_px = size_q8 >> 8;
    uint32_t frac_px = size_q8 & 0xFFu;
    if (int_px > 0xFFFFu) {
        int_px = 0xFFFFu;
        frac_px = 0xFFu;
    }

    const Theme& thm = active_theme(ctx);
    const Color eff_color = resolve_theme_color(desc.color, thm.text_primary);
    const Color eff_sel_color = resolve_theme_color(desc.selection_color, thm.selection);

    Clay_TextElementConfig cfg = {};
    cfg.textColor = to_clay_color(eff_color);
    const uint16_t slot_bits = (desc.font_id < kMaxFontSlots) ? desc.font_id : kFontSans;
    cfg.fontId = static_cast<uint16_t>((slot_bits & 0x7Fu) | ((frac_px & 0xFFu) << 7));
    cfg.fontSize = static_cast<uint16_t>(int_px);
    cfg.letterSpacing = to_u16_round(desc.letter_spacing * wscale);
    cfg.lineHeight = desc.line_height_px > 0.0f ? to_u16_round(desc.line_height_px * wscale) : 0u;
    cfg.ellipsis = desc.ellipsis;
    cfg.maxLines = desc.max_lines;
    cfg.wrapMode = desc.wrap ? CLAY_TEXT_WRAP_WORDS : CLAY_TEXT_WRAP_NONE;
    cfg.textAlignment = desc.align_x == AlignX::Center ? CLAY_TEXT_ALIGN_CENTER : (desc.align_x == AlignX::Right ? CLAY_TEXT_ALIGN_RIGHT : CLAY_TEXT_ALIGN_LEFT);

    SelectableTextExtra* extra = nullptr;
    if ((desc.selectable && desc.id) || ctx->active_ui->current_layer) {
        extra = ctx->frame_arena.alloc<SelectableTextExtra>();
        if (!extra) return;
        extra->layer=ctx->active_ui->current_layer;
        extra->full_utf8 = clay_str.chars; extra->total_bytes = content.length;
        extra->selectable_id = desc.selectable ? desc.id : 0; extra->selection_color = eff_sel_color;
        cfg.userData = extra;
        if(desc.selectable && desc.id) register_selectable_text(ctx,desc.id,clay_str.chars,content.length,content.is_static);
    }
    Clay_Context* cctx = Clay_GetCurrentContext();
    const int32_t prev_len = cctx->layoutElements.length;
    Clay__OpenTextElement(clay_str, cfg);
    if (cctx->layoutElements.length > prev_len) {
        const uint32_t cid = cctx->layoutElements.internalArray[cctx->layoutElements.length-1].id;
        auto* n = a11y_emit(ctx,desc.id ? desc.id : cid,A11yRole::StaticText,content); if(n)n->clay_id=cid;
    }
    if (extra && cctx->layoutElements.length > prev_len) extra->clay_elem_id = cctx->layoutElements.internalArray[cctx->layoutElements.length-1].id;
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
    const float base_px = clamp_range(
        ((desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f) * wscale,
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

        const float raw_sz = (sp.font_size_px > 0.0f) ? (sp.font_size_px * wscale) : base_px;
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
        };
    }
    full_utf8[total_bytes] = '\0';
    plain_utf8[text_bytes] = '\0';

    const float final_lh = (desc.line_height_px > 0.0f) ? (desc.line_height_px * wscale) : max_lh;
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

    Clay_TextElementConfig cfg = {};
    cfg.userData = rich;
    cfg.textColor = to_clay_color(base_color);
    cfg.fontId = static_cast<uint16_t>((cfg_hash & 0x7FFFu) | 0x8000u);
    cfg.fontSize = static_cast<uint16_t>(cfg_hash >> 16);
    cfg.letterSpacing = 0u;
    cfg.lineHeight = 0u;
    cfg.ellipsis = desc.ellipsis;
    cfg.maxLines = desc.max_lines;
    cfg.wrapMode = desc.wrap ? CLAY_TEXT_WRAP_WORDS : CLAY_TEXT_WRAP_NONE;
    cfg.textAlignment = (desc.align_x == AlignX::Center)
        ? CLAY_TEXT_ALIGN_CENTER
        : ((desc.align_x == AlignX::Right) ? CLAY_TEXT_ALIGN_RIGHT : CLAY_TEXT_ALIGN_LEFT);

    Clay_String clay_str = {
        .isStaticallyAllocated = false,
        .length = static_cast<int32_t>(total_bytes),
        .chars = full_utf8,
    };
    Clay_Context* cctx = Clay_GetCurrentContext();
    const int32_t prev_elem_len = cctx ? cctx->layoutElements.length : 0;
    Clay__OpenTextElement(clay_str, cfg);
    if (cctx && cctx->layoutElements.length > prev_elem_len) {
        rich->clay_elem_id = cctx->layoutElements.internalArray[cctx->layoutElements.length - 1].id;
        auto* n=a11y_emit(ctx,desc.id ? desc.id : rich->clay_elem_id,A11yRole::StaticText,{plain_utf8,text_bytes}); if(n)n->clay_id=rich->clay_elem_id;
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
            ws->owner_canvas_id = ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth - 1u].id;
        }
    }

    ImageExtra* extra = ctx->frame_arena.alloc<ImageExtra>();
    if (!extra) {
        return;
    }
    extra->layer=ctx->active_ui->current_layer;
    extra->desc = desc;

    if (desc.id != 0u) {
        Clay__OpenElementWithId(make_clay_id(desc.id));
    } else {
        Clay__OpenElement();
    }

    Clay_ElementDeclaration decl = {};
    decl.layout.sizing.width = to_clay_sizing(desc.width);
    decl.layout.sizing.height = to_clay_sizing(desc.height);
    decl.aspectRatio.aspectRatio = std::isfinite(desc.aspect_ratio) && desc.aspect_ratio > 0 ? desc.aspect_ratio : 0;
    decl.layout.gridColSpan = desc.grid_col_span > 0 ? desc.grid_col_span : 1;
    decl.cornerRadius = to_clay_corners(desc.radius);
    decl.image.imageData = extra;
    decl.userData = extra;

    Clay__ConfigureOpenElement(decl);
    Clay__CloseElement();
}

void icon(const IconDesc& desc_in) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame) {
        return;
    }
    const float wscale = current_world_scale(ctx);
    IconDesc desc = desc_in;
    desc.color = resolve_theme_color(desc.color, active_theme(ctx).text_primary);
    const float layout_sz = ((desc.size_px > 0.0f) ? desc.size_px : 16.0f) * wscale;

    if (desc.id != 0u) {
        if (WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, desc.id, ctx->active_ui->frame_index)) {
            ws->is_user_element = true;
            ws->is_text_input_writable = false;
            ws->owner_canvas_id = (ctx->active_ui->canvas_depth > 0u)
                ? ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth - 1u].id
                : 0u;
        }
        Clay__OpenElementWithId(make_clay_id(desc.id));
    } else {
        Clay__OpenElement();
    }

    BoxExtra* extra = ctx->frame_arena.alloc<BoxExtra>();
    Clay_ElementDeclaration decl = {};
    decl.layout.sizing.width = CLAY_SIZING_FIXED(layout_sz);
    decl.layout.sizing.height = CLAY_SIZING_FIXED(layout_sz);
    if (extra) {
        extra->layer = ctx->active_ui->current_layer;
        extra->kind = CustomCmdKind::Icon;
        extra->icon_desc = desc;
        decl.custom.customData = extra;
        decl.userData = extra;
    }
    Clay__ConfigureOpenElement(decl);
    Clay__CloseElement();
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
        node->kind = DrawCmdKind::Rect;
        node->rect = eff;
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
        node->poly_points = copy;
        node->poly_count = static_cast<uint32_t>(n);
        node->poly_closed = desc.closed;
        node->poly_color = desc.color;
        node->poly_thickness = desc.thickness;
        node->poly_glow_color = desc.glow_color;
        node->poly_glow_px = desc.glow_px;
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
    if (DrawCmdNode* node=append_open_box_draw_cmd(ctx,foreground)) {
        node->callback_theme=ctx->frame_arena.alloc<Theme>();
        if (!node->callback_theme) return;
        *node->callback_theme=active_theme(ctx);
        node->kind=DrawCmdKind::Callback;node->callback=callback;node->callback_data=user_data;
    }
}

void draw_text(const TextDrawDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !desc.text.chars || desc.text.length == 0u || !std::isfinite(desc.size) || desc.size <= 0.0f || !std::isfinite(desc.max_width_px) || desc.max_width_px < 0) {
        return;
    }
    TextDrawDesc copied = desc;
    copied.color = resolve_theme_color(desc.color, active_theme(ctx).text_primary);
    if (copied.color.a == 0u) {
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
        node->text_cmd = copied;
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
        maximized = (wflags & SDL_WINDOW_MAXIMIZED) != 0u;
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
    return (SDL_GetWindowFlags(target) & SDL_WINDOW_MAXIMIZED) != 0u;
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

void set_text_selection(uint32_t element_id, uint32_t anchor_byte, uint32_t cursor_byte) noexcept {
    Context* ctx = current_context();
    if (!ctx || element_id == 0u) {
        return;
    }
    WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, element_id, ctx->active_ui->frame_index);
    if (!ws) {
        return;
    }
    ws->edit_initialized = true;
    const bool valid_backing = ws->sel_text_ptr && (ws->sel_text_static || ws->sel_text_epoch == ctx->arena_epoch);
    ws->edit_sel_anchor = valid_backing ? utf8_clamp_boundary(ws->sel_text_ptr,ws->sel_text_len,anchor_byte) : anchor_byte;
    ws->edit_cursor = valid_backing ? utf8_clamp_boundary(ws->sel_text_ptr,ws->sel_text_len,cursor_byte) : cursor_byte;
    ws->edit_preferred_x = -1.0f;
    request_redraw(ctx);
}

bool has_text_selection(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (!ctx) return false;
    auto* ws = state_map_find(&ctx->active_ui->states,element_id ? element_id : ctx->active_ui->focus_id);
    if (ws && ws->edit_text_buffer && ws->sel_text_epoch == ctx->arena_epoch) {
        const uint32_t n=text_buffer_byte_length(ws->edit_text_buffer);
        return (ws->edit_cursor<n?ws->edit_cursor:n) != (ws->edit_sel_anchor<n?ws->edit_sel_anchor:n);
    }
    if (!ws || !ws->sel_text_ptr || (!ws->sel_text_static && ws->sel_text_epoch != ctx->arena_epoch)) return false;
    return utf8_clamp_boundary(ws->sel_text_ptr,ws->sel_text_len,ws->edit_cursor) != utf8_clamp_boundary(ws->sel_text_ptr,ws->sel_text_len,ws->edit_sel_anchor);
}
Str selected_text(uint32_t element_id, Arena* arena) noexcept {
    Context* ctx = current_context();
    if (!ctx || !has_text_selection(element_id)) return {};
    auto* ws = state_map_find(&ctx->active_ui->states,element_id ? element_id : ctx->active_ui->focus_id);
    if(ws->edit_text_buffer && ws->sel_text_epoch == ctx->arena_epoch) {
        const uint32_t nbytes=text_buffer_byte_length(ws->edit_text_buffer);
        const uint32_t c=ws->edit_cursor<nbytes?ws->edit_cursor:nbytes,a=ws->edit_sel_anchor<nbytes?ws->edit_sel_anchor:nbytes;
        const uint32_t lo=c<a?c:a,hi=c>a?c:a;
        Arena* scratch=arena?arena:&ctx->frame_arena;
        char* p=scratch->alloc<char>(size_t(hi-lo)+1);if(!p)return {};
        const uint32_t n=text_buffer_copy_range(ws->edit_text_buffer,lo,hi,p,hi-lo);p[n]=0;return {p,n};
    }
    const uint32_t a = utf8_clamp_boundary(ws->sel_text_ptr,ws->sel_text_len,ws->edit_sel_anchor);
    const uint32_t b = utf8_clamp_boundary(ws->sel_text_ptr,ws->sel_text_len,ws->edit_cursor);
    const uint32_t lo = a < b ? a : b, hi = a > b ? a : b;
    return (arena ? arena : &ctx->frame_arena)->push_str(ws->sel_text_ptr+lo,hi-lo);
}
void clear_text_selection(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (!ctx) return;
    if (auto* ws = state_map_find(&ctx->active_ui->states,element_id ? element_id : ctx->active_ui->focus_id)) {
        ws->edit_sel_anchor = ws->edit_cursor; request_redraw(ctx);
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
    desc.text_color = resolve_theme_color(desc.text_color, thm.text_body);
    desc.placeholder_color = resolve_theme_color(desc.placeholder_color, thm.text_disabled);
    desc.selection_color = resolve_theme_color(desc.selection_color, thm.selection);
    desc.caret_color = resolve_theme_color(desc.caret_color, thm.accent);
    desc.composition_color = resolve_theme_color(desc.composition_color, thm.warning);
    desc.background = resolve_theme_color(desc.background, thm.bg_input);
    desc.border_color = resolve_theme_color(desc.border_color, thm.border_input);
    desc.focus_border_color = resolve_theme_color(desc.focus_border_color, thm.accent);
    desc.radius = resolve_theme_corners(desc.radius, Corners::all(thm.radius_input_px));

    WidgetState* ws = nullptr;
    if (desc.id != 0u) {
        ws = state_map_get_or_insert(&ctx->active_ui->states, desc.id, ctx->active_ui->frame_index);
        if (ws) {
            ws->is_user_element = true;
            ws->is_focusable = true;
            ws->focus_scope = ctx->active_ui->modal_scope && focus_scope_contains(*ctx->active_ui, ctx->active_ui->modal_scope) ? ctx->active_ui->modal_scope : ctx->active_ui->focus_scope;
            ws->is_scroll_x = false;
            ws->is_scroll_y = false;
            ws->owner_canvas_id = (ctx->active_ui->canvas_depth > 0u)
                ? ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth - 1u].id
                : 0u;
        }
        if (reserve_focus(ctx->active_ui)) {
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
    const bool writable = (buf != nullptr && cap > 1u && !desc.read_only);
    if (ws) {
        ws->edit_text_buffer = nullptr;
        ws->is_text_input_writable = writable;
        ws->is_multiline_input = desc.multiline;
        ws->suppress_popup_keys = desc.suppress_popup_keys;
        if (ws->pressed_this_frame || ctx->active_ui->frame_text_op_count || ctx->active_ui->ime_comp_len) ws->edit_reveal_caret = true;
    }
    if (buf && cap > 0u) {
        while (len < (desc.read_only ? cap : cap - 1u) && buf[len] != '\0') {
            ++len;
        }
        if (!desc.read_only) buf[len] = '\0';
    }

    uint32_t cursor = len;
    uint32_t anchor = len;
    if (ws) {
        if (!ws->edit_initialized) {
            ws->edit_initialized = true;
            ws->edit_cursor = len;
            ws->edit_sel_anchor = len;
            ws->edit_preferred_x = -1.0f;
        }
        cursor = utf8_clamp_boundary(buf, len, ws->edit_cursor);
        anchor = utf8_clamp_boundary(buf, len, ws->edit_sel_anchor);
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
    uint32_t menu_key=text_input_menu(desc,len,cursor!=anchor);
    ws=desc.id ? state_map_find(&ctx->active_ui->states,desc.id) : nullptr;
    if (show_clear && (clicked(clear_id) || a11y_take(clear_id,A11yAction::Press))) { request_focus(desc.id);anchor=0;cursor=len;menu_key=SDLK_DELETE; }
    if (show_clear) desc.padding.right += 24;
    bool is_focused = (desc.id != 0u && ctx->active_ui->focus_id == desc.id &&
        (!ctx->active_ui->modal_scope || focus_scope_contains(*ctx->active_ui, ctx->active_ui->modal_scope)));
    const bool is_hovered = (desc.id != 0u && Clay_PointerOver(make_clay_id(desc.id)));
    if (is_hovered || (desc.id != 0u && ctx->active_ui->active_id == desc.id)) {
        set_cursor(Cursor::IBeam);
    }

    if (is_focused && !desc.read_only) {
        ctx->active_ui->ime_focused_id = desc.id;
        ctx->active_ui->ime_focused_password = desc.password;
        if (desc.password && ctx->active_ui->ime_comp_len > 0u) {
            clear_ime_composition(ctx);
        }
    }

    const float active_wrap_w = (desc.multiline && desc.word_wrap)
        ? ((ws && ws->edit_wrap_width > 0.0f)
            ? ws->edit_wrap_width
            : ((ws && ws->last_box_logical.w > 0.0f)
                ? text_input_wrap_width(desc, compute_text_inner_geom(ws->last_box_logical, desc))
                : ((desc.width.kind == SizingKind::Fixed && desc.width.value > 0.0f)
                    ? text_input_wrap_width(desc, compute_text_inner_geom(Rect{ 0.0f, 0.0f, desc.width.value, 100.0f }, desc))
                    : 0.0f)))
        : 0.0f;
    if (ws && active_wrap_w <= 0.0f) {
        ws->edit_wrap_affinity = false;
    }

    // Pointer click / double-word / triple-line / drag selection
    if (ws && ws->last_box_logical.w > 0.0f && desc.id != 0u) {
        const TextInnerGeom prev_geom = compute_text_inner_geom(ws->last_box_logical, desc);
        const float ptr_wrap_w = (active_wrap_w > 0.0f) ? active_wrap_w : text_input_wrap_width(desc, prev_geom);
        const float local_x = ctx->active_ui->pointer_pos.x - prev_geom.origin_x +
            ((ptr_wrap_w > 0.0f) ? 0.0f : ws->edit_scroll_x);
        const float local_y = ctx->active_ui->pointer_pos.y - prev_geom.origin_y + ws->edit_scroll_y;
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
            if (ctx->active_ui->last_mouse_clicks >= 3u) {
                if (desc.multiline) {
                    line_bounds_at(buf, len, hit_byte, &anchor, &cursor);
                } else {
                    anchor = 0u;
                    cursor = len;
                }
                ws->edit_wrap_affinity = false;
            } else if (ctx->active_ui->last_mouse_clicks == 2u) {
                utf8_word_bounds_at(buf, len, hit_byte, &anchor, &cursor);
                ws->edit_wrap_affinity = false;
            } else {
                const bool shift_held = (ctx->active_ui->frame_key_mods & Mod_Shift) != 0u;
                if (!shift_held) {
                    anchor = hit_byte;
                }
                cursor = hit_byte;
                ws->edit_wrap_affinity = (ptr_wrap_w > 0.0f) && hit_aff;
            }
            ws->edit_preferred_x = -1.0f;
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
            ws->edit_wrap_affinity = (ptr_wrap_w > 0.0f) && drag_aff;
            ws->edit_preferred_x = -1.0f;
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
        if (ws) {
            ws->edit_preferred_x = -1.0f;
            ws->edit_wrap_affinity = false;
        }
        replaced_by_a11y = true;
    }
    if (replaced_by_a11y && is_focused) {
        cancel_ime_composition(ctx);
    }
    bool submitted = false;
    if (is_focused) {
        if (undo_slot && undo_slot->count > 0u) {
            TextUndoRecord& cur_rec = undo_slot->entries[(undo_slot->head + undo_slot->cur) % undo_slot->entries_cap];
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
                    if (ws) {
                        ws->edit_preferred_x = -1.0f;
                        ws->edit_wrap_affinity = false;
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
                    if (ws) {
                        ws->edit_preferred_x = -1.0f;
                        ws->edit_wrap_affinity = false;
                    }
                    continue;
                }
                if (op.key == SDLK_Z) {
                    if (writable && undo_slot && undo_slot->count > 0u) {
                        if (!shift && undo_slot->cur > 0u) {
                            if (undo_restore(undo_slot, undo_slot->cur - 1u, buf, cap, &len, &cursor, &anchor)) {
                                changed = true;
                                if (ws) {
                                    ws->edit_preferred_x = -1.0f;
                                    ws->edit_wrap_affinity = false;
                                }
                            }
                        } else if (shift && (undo_slot->cur + 1u) < undo_slot->count) {
                            if (undo_restore(undo_slot, undo_slot->cur + 1u, buf, cap, &len, &cursor, &anchor)) {
                                changed = true;
                                if (ws) {
                                    ws->edit_preferred_x = -1.0f;
                                    ws->edit_wrap_affinity = false;
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
                            if (ws) {
                                ws->edit_preferred_x = -1.0f;
                                ws->edit_wrap_affinity = false;
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
                        if (ws) {
                            ws->edit_preferred_x = -1.0f;
                            ws->edit_wrap_affinity = false;
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
                            if (ws) {
                                ws->edit_preferred_x = -1.0f;
                                ws->edit_wrap_affinity = false;
                            }
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
                        if (!shift) {
                            anchor = cursor;
                        }
                    }
                    if (ws) {
                        ws->edit_preferred_x = -1.0f;
                        ws->edit_wrap_affinity = false;
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
                        if (!shift) {
                            anchor = cursor;
                        }
                    }
                    if (ws) {
                        ws->edit_preferred_x = -1.0f;
                        ws->edit_wrap_affinity = false;
                    }
                    break;
                case SDLK_HOME: {
                    bool new_aff = false;
                    if (desc.multiline && !primary && active_wrap_w > 0.0f) {
                        text_wrapped_home_end(
                            ctx, desc, Str{ buf ? buf : "", len }, cursor,
                            ws ? ws->edit_wrap_affinity : false, false, active_wrap_w, &cursor, &new_aff);
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
                    if (ws) {
                        ws->edit_preferred_x = -1.0f;
                        ws->edit_wrap_affinity = new_aff;
                    }
                    break;
                }
                case SDLK_END: {
                    bool new_aff = false;
                    if (desc.multiline && !primary && active_wrap_w > 0.0f) {
                        text_wrapped_home_end(
                            ctx, desc, Str{ buf ? buf : "", len }, cursor,
                            ws ? ws->edit_wrap_affinity : false, true, active_wrap_w, &cursor, &new_aff);
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
                    if (ws) {
                        ws->edit_preferred_x = -1.0f;
                        ws->edit_wrap_affinity = new_aff;
                    }
                    break;
                }
                case SDLK_UP:
                case SDLK_DOWN:
                    if (!desc.suppress_popup_keys && desc.multiline && ws) {
                        ws->edit_cursor = cursor;
                        ws->edit_sel_anchor = anchor;
                        move_caret_vertical(
                            ctx, desc, Str{ buf ? buf : "", len }, ws, (op.key == SDLK_UP) ? -1 : 1, shift, active_wrap_w);
                        cursor = ws->edit_cursor;
                        anchor = ws->edit_sel_anchor;
                    }
                    break;
                case SDLK_BACKSPACE:
                    if (writable) {
                        if (has_sel) {
                            cursor = erase_text_range(buf, &len, anchor, cursor);
                            anchor = cursor;
                            record_edit(0u);
                        } else if (cursor > 0u) {
                            const uint32_t del_from = word_mod
                                ? utf8_word_left(buf, len, cursor)
                                : utf8_prev_grapheme(buf, len, cursor);
                            if (del_from < cursor) {
                                cursor = erase_text_range(buf, &len, del_from, cursor);
                                anchor = cursor;
                                record_edit(word_mod ? 0u : 2u);
                            }
                        }
                        if (ws) {
                            ws->edit_preferred_x = -1.0f;
                            ws->edit_wrap_affinity = false;
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
                            const uint32_t del_to = word_mod
                                ? utf8_word_right(buf, len, cursor)
                                : utf8_next_grapheme(buf, len, cursor);
                            if (del_to > cursor) {
                                cursor = erase_text_range(buf, &len, cursor, del_to);
                                anchor = cursor;
                                record_edit(word_mod ? 0u : 2u);
                            }
                        }
                        if (ws) {
                            ws->edit_preferred_x = -1.0f;
                            ws->edit_wrap_affinity = false;
                        }
                    }
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    if (desc.suppress_popup_keys) {
                        break;
                    }
                    if (desc.multiline && !primary) {
                        if (writable) {
                            const uint32_t prev_len = len;
                            const uint32_t new_pos = insert_utf8_filtered(
                                buf, cap, &len, anchor, cursor, str_lit("\n"), true);
                            cursor = new_pos;
                            anchor = new_pos;
                            if (has_sel || len != prev_len) {
                                record_edit(0u);
                            }
                            if (ws) {
                                ws->edit_preferred_x = -1.0f;
                                ws->edit_wrap_affinity = false;
                            }
                        }
                    } else {
                        submitted = true;
                    }
                    break;
                case SDLK_ESCAPE:
                    if (has_sel) anchor = cursor;
                    else if (desc.escape_blurs) { request_focus(0);is_focused=false; }
                    if (ws) {
                        ws->edit_wrap_affinity = false;
                    }
                    break;
                default:
                    break;
            }
        }
    }

    const uint32_t clamped_cursor = utf8_clamp_boundary(buf, len, cursor);
    if (ws && clamped_cursor != cursor) {
        ws->edit_wrap_affinity = false;
    }
    cursor = clamped_cursor;
    anchor = utf8_clamp_boundary(buf, len, anchor);
    if (ws) {
        ws->edit_cursor = cursor;
        ws->edit_sel_anchor = anchor;
        if (active_wrap_w > 0.0f) {
            ws->edit_scroll_x = 0.0f;
        }
    }

    const Str committed_slice = (buf && len > 0u) ? ctx->frame_arena.push_str(buf, len) : Str{};
    if (ws) {
        ws->sel_text_ptr = committed_slice.chars;
        ws->sel_text_len = committed_slice.length;
        ws->sel_text_epoch = ctx->arena_epoch;
        ws->sel_text_static = false;
    }
    if (!is_focused && ctx->active_ui->ime_focused_id==desc.id) ctx->active_ui->ime_focused_id=0;
    const bool composing = is_focused && !desc.read_only && !desc.password && ctx->active_ui->ime_comp_len > 0u;
    if (is_focused && ws && ws->last_box_logical.w > 0.0f && !desc.read_only && !desc.password) {
        const TextInnerGeom pre_geom = compute_text_inner_geom(ws->last_box_logical, desc);
        update_text_scroll_and_ime(
            ctx, ws, desc, pre_geom, committed_slice, cursor, true, composing, nullptr, nullptr);
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
        ti_extra->committed_text = committed_slice;
        ti_extra->cursor_byte = cursor;
        ti_extra->sel_anchor_byte = anchor;
        ti_extra->is_focused = is_focused;
        extra->text_input = ti_extra;
    }

    if (desc.id != 0u) {
        Clay__OpenElementWithId(make_clay_id(desc.id));
    } else {
        Clay__OpenElement();
    }

    Clay_ElementDeclaration decl = {};
    decl.layout.sizing.width = to_clay_sizing(desc.width);
    decl.layout.sizing.height = to_clay_sizing(desc.height);
    decl.layout.padding = Clay_Padding{
        .left = to_u16_round(desc.padding.left),
        .right = to_u16_round(desc.padding.right),
        .top = to_u16_round(desc.padding.top),
        .bottom = to_u16_round(desc.padding.bottom),
    };
    decl.cornerRadius = to_clay_corners(desc.radius);
    if (extra && ti_extra) {
        decl.custom.customData = extra;
        decl.userData = extra;
    }

    Clay__ConfigureOpenElement(decl);
    if (show_clear) {
        begin_box({.id=clear_id,.width=Sizing::fixed(20),.height=Sizing::fixed(20),.align_y=AlignY::Center,
            .attach_to=AttachTo::Parent,.anchor_element=Anchor::CenterRight,.anchor_parent=Anchor::CenterRight,.floating_offset={-4,0}});
        if (hovered(clear_id)) set_cursor(Cursor::Hand);
        const Color color=desc.text_color;
        draw_line({.from={6,6},.to={14,14},.color=color,.thickness=1.5f});
        draw_line({.from={14,6},.to={6,14},.color=color,.thickness=1.5f});
        a11y_semantic(clear_id,A11yRole::Button,str_lit("Clear"),0,3u);
        end_box();
    }
    const uint32_t semantic_id=Clay__GetOpenLayoutElement()->id;
    Clay__CloseElement();

    Str semantic_value={buf,len};
    if(desc.password) {
        uint32_t count=0; for(uint32_t i=0;i<len;++i) if((static_cast<unsigned char>(buf[i])&0xC0u)!=0x80u)++count;
        char* bullets=ctx->frame_arena.alloc<char>(size_t(count)*3+1);
        if(bullets) {for(uint32_t i=0;i<count;++i)std::memcpy(bullets+i*3,"\xE2\x80\xA2",3);semantic_value={bullets,count*3};} else semantic_value={};
    }
    a11y_emit(ctx,desc.id ? desc.id : semantic_id,A11yRole::TextInput,{},semantic_value,(desc.password?A11yState_Password:0u)|(desc.read_only?A11yState_ReadOnly:0u)|(desc.multiline?A11yState_Multiline:0u),1u|(writable?128u:0u));
    return TextInputResult{
        .changed = changed,
        .submitted = submitted,
        .focused = is_focused,
        .composing = composing,
        .byte_length = len,
        .cursor_byte = cursor,
        .selection_anchor_byte = anchor,
        .ime_cursor_rect = (is_focused && !desc.read_only && !desc.password) ? ctx->active_ui->ime_cursor_logical : Rect{},
    };
}

TextInputResult text_input(const TextInputDesc& desc) noexcept {
    return text_input_impl(desc, desc.highlights.data(), desc.highlights.size(),
        [](const void* ranges, size_t i) noexcept { return static_cast<const TextInputHighlight*>(ranges)[i]; });
}

bool hovered(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (element_id == 0u || !ctx || !ctx->active_ui->clay_ctx) {
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
    Clay_SetCurrentContext(ctx->active_ui->clay_ctx);
    return Clay_PointerOver(make_clay_id(element_id));
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
    if (!ctx || !ctx->active_ui->clay_ctx || element_id == 0u) {
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
    Clay_SetCurrentContext(ctx->active_ui->clay_ctx);
    const Clay_ElementData ed = Clay_GetElementData(make_clay_id(element_id));
    if (!ed.found) {
        return {};
    }
    return Rect{
        ed.boundingBox.x,
        ed.boundingBox.y,
        ed.boundingBox.width,
        ed.boundingBox.height,
    };
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
        if (ev.key == keycode && (include_repeat || !ev.repeat)) {
            return true;
        }
    }
    return false;
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
        if (!ev.repeat && ev.key == keycode && (ev.mods & kModMask) == required_mods) {
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
    if (!ctx || !ctx->active_ui->clay_ctx || popover_id == 0u || ctx->active_ui->frame_index <= 1u) {
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
    if (!ctx->active_ui->frame_escape_consumed_by_ime && key_pressed(SDLK_ESCAPE, false)) {
        return true;
    }
    if (!ctx->active_ui->left_pressed_this_frame && !ctx->active_ui->right_pressed_this_frame) {
        return false;
    }
    Clay_SetCurrentContext(ctx->active_ui->clay_ctx);
    const auto is_over_elem = [&](uint32_t id, const WidgetState* ws) noexcept -> bool {
        if (id == 0u) {
            return false;
        }
        if (Clay_PointerOver(make_clay_id(id))) {
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
        return Vec2{0.0f, static_cast<float>(ws->vscroll_max_y)};
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
        if (stop_momentum) {
            ws->scroll_vel = {};
        }
        request_redraw(ctx);
    }
}

} // namespace etch
