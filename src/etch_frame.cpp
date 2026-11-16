// Frame composition, F12 HUD, focus scopes & OS drop injection.
// Compiled via src/etch_unity.cpp.

namespace etch {

void end_frame(Context* ctx_arg) noexcept {
    Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (!ctx || !ctx->in_frame || !ctx->active_ui->clay_ctx) {
        return;
    }
    ctx->theme_depth = 0u;
    ctx->theme_overflow = 0u;
    Clay_SetCurrentContext(ctx->active_ui->clay_ctx);

    while (ctx->active_ui->collapsible_depth) end_collapsible();
    while (ctx->active_ui->tree_depth) end_tree_node();
    const float dpi = ctx->active_ui->dpi_scale;
    const Rect root_clip = {
        0.0f,
        0.0f,
        static_cast<float>(ctx->active_ui->viewport_w),
        static_cast<float>(ctx->active_ui->viewport_h),
    };

    ClipStack clip_stack = {};
    clip_stack.entries[0] = ClipEntry{ .rect = root_clip, .radius = 0.0f };
    clip_stack.depth = 1u;

    ctx->active_ui->span_hit_count = 0u;
    ctx->active_ui->drop_hit_count = 0;
    Clay_RenderCommandArray cmds = Clay_EndLayout(0.0f);
    bool corrected_heights = false;
    for (auto* rec = ctx->active_ui->active_vlists; rec; rec = rec->next) {
        auto* cache = find_vlist(ctx->active_ui,rec->list_id);
        auto* entry = Clay__GetHashMapItem(make_clay_id(rec->list_id).id);
        if (!cache || entry == &Clay_LayoutElementHashMapItem_DEFAULT) continue;
        auto* list = entry->layoutElement;
        uint32_t count = rec->end_index-rec->first_index;
        if (count > static_cast<uint32_t>(list->children.length)) count = static_cast<uint32_t>(list->children.length);
        for (uint32_t i = 0; i < count; ++i) {
            auto* child = Clay_LayoutElementArray_Get(&Clay_GetCurrentContext()->layoutElements,list->children.elements[i]);
            float h = child->dimensions.height;
            if (std::isfinite(h) && h >= 1 && std::fabs(h-vlist_height(cache,rec->first_index+i)) > .25f) {
                update_vlist_height(ctx,cache,rec->first_index+i,clamp_range(h,1,16384)); corrected_heights = true;
            }
        }
    }
    if (corrected_heights) request_redraw(ctx);

    if (ctx->active_ui->active_id) {
        auto* ws = state_map_find(&ctx->active_ui->states,ctx->active_ui->active_id);
        if (ws && ws->is_selectable_text && (ws->pressed_this_frame || ws->pressed_down)) update_selectable_text_pointer(ctx,ws,cmds);
    }
    for (int32_t i = 0; i < cmds.length; ++i) {
        const Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(&cmds, i);
        if (!cmd) {
            continue;
        }
        const ClipEntry& cur_clip = clip_stack.entries[clip_stack.depth - 1u];
        ctx->render_clip_index=cur_clip.indexed ? cur_clip.shape : 0;
        const Rect raw_box_px = {
            .x = cmd->boundingBox.x * dpi,
            .y = cmd->boundingBox.y * dpi,
            .w = cmd->boundingBox.width * dpi,
            .h = cmd->boundingBox.height * dpi,
        };

        const Rect box_px = ctx->active_ui->pixel_snap && cmd->commandType != CLAY_RENDER_COMMAND_TYPE_TEXT ? snap_pixel_rect(raw_box_px) : raw_box_px;
        switch (cmd->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
                const BoxExtra* extra = static_cast<const BoxExtra*>(cmd->renderData.custom.customData);
                if (extra) {
                    if (extra->desc.id && reserve_span_hits(&ctx->active_ui->drop_hits, &ctx->active_ui->drop_hit_cap, ctx->active_ui->drop_hit_count, 512u)) {
                        ctx->active_ui->drop_hits[ctx->active_ui->drop_hit_count++] = SpanHitRegion{
                            .id = extra->desc.id, .clay_elem_id = extra->desc.id,
                            .rect = {cmd->boundingBox.x, cmd->boundingBox.y, cmd->boundingBox.width, cmd->boundingBox.height},
                            .paint_order = static_cast<uint32_t>(i),
                            .z_index = cmd->zIndex,
                            .clip_rect = {cur_clip.rect.x / dpi, cur_clip.rect.y / dpi, cur_clip.rect.w / dpi, cur_clip.rect.h / dpi}, .has_clip = true};
                    }
                    if (extra->code_editor) {
                        emit_code_editor_command(ctx, extra->code_editor, cmd->boundingBox, cur_clip, dpi);
                    } else if (extra->text_input) {
                        emit_text_input_command(ctx, extra->text_input, cmd->boundingBox, box_px, cur_clip, dpi);
                    } else if (extra->kind == CustomCmdKind::CanvasWire && extra->canvas_wire) {
                        emit_canvas_wire_command(ctx, extra->canvas_wire, cmd->boundingBox, cur_clip, dpi);
                    } else if (extra->kind == CustomCmdKind::Icon) {
                        emit_vector_icon_quad(ctx, extra->icon_desc.icon_id, raw_box_px, extra->icon_desc.color, cur_clip);
                    } else {
                        if (extra->desc.attach_to != AttachTo::None && !extra->desc.pointer_passthrough &&
                            reserve_span_hits(&ctx->active_ui->span_hits, &ctx->active_ui->span_hit_cap, ctx->active_ui->span_hit_count, 256u)) {
                            const uint32_t slot = ctx->active_ui->span_hit_count++;
                            ctx->active_ui->span_hits[slot] = SpanHitRegion{
                                .id = 0u,
                                .clay_elem_id = extra->desc.id,
                                .rect = Rect{
                                    cmd->boundingBox.x,
                                    cmd->boundingBox.y,
                                    cmd->boundingBox.width,
                                    cmd->boundingBox.height,
                                },
                                .paint_order = static_cast<uint32_t>(i),
                            .z_index = cmd->zIndex,
                                .clip_rect = Rect{
                                    cur_clip.rect.x / dpi,
                                    cur_clip.rect.y / dpi,
                                    cur_clip.rect.w / dpi,
                                    cur_clip.rect.h / dpi,
                                },
                                .has_clip = true,
                            };
                        }
                        emit_box_background_and_shadow(ctx, extra, box_px, cur_clip, dpi);
                        if (extra->keyboard_focus_ring) {
                            push_prim(ctx, Prim::make_rect(box_px, cur_clip.rect, {}, scale_corners_uniform(Corners::all(extra->focus_ring_radius_px), dpi),
                                extra->focus_ring_color, 2 * dpi, 1, cur_clip.radius));
                        }
                        if (extra->draw_head) {
                            ClipEntry draw_clip = cur_clip;
                            const bool box_clips =
                                extra->desc.clip_children || extra->desc.scroll_x || extra->desc.scroll_y || extra->desc.shrink_width || extra->desc.shrink_height || extra->is_canvas;
                            if (box_clips) {
                                draw_clip=make_box_clip(ctx,cur_clip,box_px,scale_corners(extra->desc.radius,dpi),extra->desc.continuous_corners);
                            }
                            emit_draw_list_command(ctx, extra->draw_head, raw_box_px, draw_clip, dpi);
                        }
                    }
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                const BoxExtra* extra = static_cast<const BoxExtra*>(cmd->userData);
                if (extra && extra->desc.id != 0u) {
                    if (extra->is_virtual_list) {
                        if (WidgetState* ws = state_map_find(&ctx->active_ui->states, extra->desc.id)) {
                            const double view_h = static_cast<double>(cmd->boundingBox.height);
                            if (std::fabs(ws->viewport_dim.x-cmd->boundingBox.width)>.5f || std::fabs(ws->viewport_dim.y-cmd->boundingBox.height)>.5f) request_redraw(ctx);
                            ws->viewport_dim = Vec2{ cmd->boundingBox.width, cmd->boundingBox.height };
                            ws->last_box_logical = Rect{
                                cmd->boundingBox.x,
                                cmd->boundingBox.y,
                                cmd->boundingBox.width,
                                cmd->boundingBox.height,
                            };
                            ws->vscroll_max_y = (ws->vscroll_total_h > view_h)
                                ? (ws->vscroll_total_h - view_h)
                                : 0.0;
                            ws->vscroll_pos_y = clamp_double(ws->vscroll_pos_y, 0.0, ws->vscroll_max_y);
                        }
                    } else if (extra->desc.scroll_x || extra->desc.scroll_y) {
                        if (WidgetState* ws = state_map_find(&ctx->active_ui->states, extra->desc.id)) {
                            const Clay_ScrollContainerData sc = Clay_GetScrollContainerData(make_clay_id(extra->desc.id));
                            if (sc.found) {
                                ws->viewport_dim = Vec2{ sc.scrollContainerDimensions.width, sc.scrollContainerDimensions.height };
                                ws->content_dim = Vec2{ sc.contentDimensions.width, sc.contentDimensions.height };
                                ws->scroll_max = Vec2{
                                    (sc.contentDimensions.width > sc.scrollContainerDimensions.width)
                                        ? (sc.contentDimensions.width - sc.scrollContainerDimensions.width) : 0.0f,
                                    (sc.contentDimensions.height > sc.scrollContainerDimensions.height)
                                        ? (sc.contentDimensions.height - sc.scrollContainerDimensions.height) : 0.0f,
                                };
                                ws->scroll_pos.x = clamp_range(ws->scroll_pos.x, 0.0f, ws->scroll_max.x);
                                ws->scroll_pos.y = clamp_range(ws->scroll_pos.y, 0.0f, ws->scroll_max.y);
                            } else {
                                ws->viewport_dim = Vec2{ cmd->boundingBox.width, cmd->boundingBox.height };
                                ws->content_dim = ws->viewport_dim;
                                ws->scroll_max = {};
                            }
                            ws->last_box_logical = Rect{
                                cmd->boundingBox.x,
                                cmd->boundingBox.y,
                                cmd->boundingBox.width,
                                cmd->boundingBox.height,
                            };
                        }
                    }
                }
                if (clip_stack.depth < kMaxClipDepth) {
                    clip_stack.entries[clip_stack.depth]=extra ? make_box_clip(ctx,cur_clip,box_px,scale_corners(extra->desc.radius,dpi),extra->desc.continuous_corners)
                        : ClipEntry{rect_intersect(cur_clip.rect,box_px),cur_clip.radius,cur_clip.shape,cur_clip.indexed};
                    if(extra && extra->is_canvas) {
                        ctx->active_ui->last_canvas_clip_px=clip_stack.entries[clip_stack.depth].rect;
                        ctx->active_ui->last_canvas_clip_radius_px=clip_stack.entries[clip_stack.depth].radius;
                    }
                    ++clip_stack.depth;
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
                if (clip_stack.depth > 1u) {
                    --clip_stack.depth;
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                const BoxExtra* extra = static_cast<const BoxExtra*>(cmd->userData);
                if (!extra) {
                    break;
                }
                if (extra->fg_draw_head) {
                    emit_draw_list_command(ctx, extra->fg_draw_head, raw_box_px, cur_clip, dpi);
                }
                const BoxDesc& d = extra->desc;
                const bool is_scroll = d.scroll_x || d.scroll_y || extra->is_virtual_list;
                const bool any_clip = d.clip_children || is_scroll || d.shrink_width || d.shrink_height;

                if ((d.scroll_x || d.scroll_y || extra->is_virtual_list) && d.show_scrollbar && d.id != 0u) {
                    if (const WidgetState* ws = state_map_find(&ctx->active_ui->states, d.id)) {
                        for(uint32_t axis=0;axis<2;++axis) {
                        const ScrollbarGeom sb = axis ? compute_hscrollbar_geom(ws) : compute_vscrollbar_geom(ws);
                        if (sb.visible) {
                            const uint32_t thumb_id = id_index(d.id,axis ? kScrollbarXTag : kScrollbarYTag);
                            const bool sb_active = (ctx->active_ui->active_id == thumb_id);
                            const bool sb_hover = (ctx->active_ui->active_id == 0u) && sb.hit_track.contains(ctx->active_ui->pointer_pos) &&
                                Clay_PointerOver(make_clay_id(d.id));
                            const Rect track_px = { sb.track.x * dpi, sb.track.y * dpi, sb.track.w * dpi, sb.track.h * dpi };
                            const Rect thumb_px = { sb.thumb.x * dpi, sb.thumb.y * dpi, sb.thumb.w * dpi, sb.thumb.h * dpi };
                            const Color track_col = extra->scrollbar_track;
                            const Color thumb_col = sb_active
                                ? extra->scrollbar_thumb_drag
                                : (sb_hover ? extra->scrollbar_thumb_hover : extra->scrollbar_thumb);
                            push_prim(
                                ctx,
                                Prim::make_rect(
                                    track_px,
                                    cur_clip.rect,
                                    track_col,
                                    Corners::all(0.5f * std::fmin(track_px.w,track_px.h)),
                                    {},
                                    0.0f,
                                    1.0f,
                                    cur_clip.radius));
                            push_prim(
                                ctx,
                                Prim::make_rect(
                                    thumb_px,
                                    cur_clip.rect,
                                    thumb_col,
                                    Corners::all(0.5f * std::fmin(thumb_px.w,thumb_px.h)),
                                    {},
                                    0.0f,
                                    1.0f,
                                    cur_clip.radius));
                        }
                        }
                    }
                }

                if (any_clip && d.border_width > 0.0f && d.border_color.a > 0u) {
                    // Clip against parent container (below our own SCISSOR_START) so border AA edge isn't halved.
                    const ClipEntry& parent_clip = (clip_stack.depth > 1u)
                        ? clip_stack.entries[clip_stack.depth - 2u]
                        : clip_stack.entries[0];
                    ctx->render_clip_index=parent_clip.indexed ? parent_clip.shape : 0;
                    Rect border_bounds=box_px;Corners border_corners=scale_corners(d.radius,dpi);
                    align_border_bounds(&border_bounds,&border_corners,d.border_width*dpi,d.border_align);
                    push_prim(
                        ctx,
                        continuous_prim(Prim::make_rect(
                            border_bounds,
                            parent_clip.rect,
                            Color::transparent(),
                            border_corners,
                            d.border_color,
                            d.border_width * dpi,
                            1.0f,
                            parent_clip.radius), d.continuous_corners));
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                emit_text_command(ctx, cmd, cur_clip, dpi);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
                const ImageExtra* extra = static_cast<const ImageExtra*>(cmd->renderData.image.imageData);
                if (extra && (extra->desc.sprite_id != kInvalidSprite || extra->desc.texture != nullptr)) {
                    const ImageDesc& idesc = extra->desc;
                    Vec2 uv0=idesc.uv_min, uv1=idesc.uv_max;
                    if (idesc.sprite_id != kInvalidSprite && !resolve_sprite_uv_rect(ctx,idesc.sprite_id,uv0,uv1,&uv0,&uv1)) break;
                    push_prim(
                        ctx,
                        Prim::make_texture(
                            box_px,
                            cur_clip.rect,
                            uv0,
                            uv1,
                            0u,
                            scale_corners(idesc.radius, dpi),
                            idesc.tint,
                            idesc.border_color,
                            idesc.border_width * dpi,
                            idesc.sprite_id != kInvalidSprite ? PrimFlag_Premultiplied : PrimFlag_None,
                            cur_clip.radius),
                        idesc.sprite_id != kInvalidSprite ? nullptr : idesc.texture);
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                // Fallback only if a raw Clay rectangle is ever emitted.
                const Clay_RectangleRenderData& rd = cmd->renderData.rectangle;
                const Color fill = Color::rgba8(
                    static_cast<uint8_t>(rd.backgroundColor.r + 0.5f),
                    static_cast<uint8_t>(rd.backgroundColor.g + 0.5f),
                    static_cast<uint8_t>(rd.backgroundColor.b + 0.5f),
                    static_cast<uint8_t>(rd.backgroundColor.a + 0.5f));
                const Corners cr = {
                    rd.cornerRadius.topLeft * dpi,
                    rd.cornerRadius.topRight * dpi,
                    rd.cornerRadius.bottomRight * dpi,
                    rd.cornerRadius.bottomLeft * dpi,
                };
                push_prim(ctx, Prim::make_rect(box_px, cur_clip.rect, fill, cr, {}, 0.0f, 1.0f, cur_clip.radius));
                break;
            }
            default:
                break;
        }
    }

    ctx->active_ui->frame_text_op_count = 0u;
    ctx->active_ui->frame_key_count = 0u;
    ctx->active_ui->last_mouse_clicks = 1u;

    if (ctx->active_ui->ime_focused_id == 0u && ctx->active_ui->ime_comp_len > 0u) {
        clear_ime_composition(ctx);
    }

    if (!ctx->headless && ctx->active_window && SDL_GetKeyboardFocus() == ctx->active_window) {
        if (ctx->active_ui->ime_focused_id != 0u) {
            if (ctx->ime_active_window != ctx->active_window) {
                if (ctx->ime_active_window) SDL_StopTextInput(ctx->ime_active_window);
                ctx->ime_active_window = ctx->active_window;
                ctx->active_ui->ime_input_started = false;
            }
            if (!ctx->active_ui->ime_input_started || ctx->active_ui->ime_focused_password != ctx->active_ui->applied_ime_password) {
                if (ctx->active_ui->ime_input_started) {
                    SDL_ClearComposition(ctx->active_window);
                    SDL_StopTextInput(ctx->active_window);
                    ctx->active_ui->ime_input_started = false;
                }
                bool started = false;
                if (ctx->active_ui->ime_focused_password) {
                    const SDL_PropertiesID props = SDL_CreateProperties();
                    if (props != 0u) {
                        SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER, SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_HIDDEN);
                        started = SDL_StartTextInputWithProperties(ctx->active_window, props);
                        SDL_DestroyProperties(props);
                    }
                } else {
                    started = SDL_StartTextInput(ctx->active_window);
                }
                if (started) {
                    ctx->active_ui->ime_input_started = true;
                    ctx->active_ui->applied_ime_password = ctx->active_ui->ime_focused_password;
                }
            } else if (ctx->active_ui->prev_ime_focused_id != 0u && ctx->active_ui->prev_ime_focused_id != ctx->active_ui->ime_focused_id) {
                SDL_ClearComposition(ctx->active_window);
            }

            const float wscale = logical_to_window_scale(ctx);
            const SDL_Rect ime_rect = {
                static_cast<int>(std::floor(ctx->active_ui->ime_cursor_logical.x * wscale + 0.5f)),
                static_cast<int>(std::floor(ctx->active_ui->ime_cursor_logical.y * wscale + 0.5f)),
                static_cast<int>(std::floor(ctx->active_ui->ime_cursor_logical.w * wscale + 0.5f)),
                static_cast<int>(std::floor(ctx->active_ui->ime_cursor_logical.h * wscale + 0.5f)),
            };
            const int ime_cursor = static_cast<int>(ctx->active_ui->ime_cursor_offset_win);
            if (ime_rect.w > 0 && ime_rect.h > 0 &&
                (ime_rect.x != ctx->active_ui->applied_ime_rect.x ||
                 ime_rect.y != ctx->active_ui->applied_ime_rect.y ||
                 ime_rect.w != ctx->active_ui->applied_ime_rect.w ||
                 ime_rect.h != ctx->active_ui->applied_ime_rect.h ||
                 ime_cursor != ctx->active_ui->applied_ime_cursor))
            {
                SDL_SetTextInputArea(ctx->active_window, &ime_rect, ime_cursor);
                ctx->active_ui->applied_ime_rect = ime_rect;
                ctx->active_ui->applied_ime_cursor = ime_cursor;
            }
        } else if (ctx->active_ui->ime_input_started && ctx->ime_active_window == ctx->active_window) {
            ctx->ime_active_window = nullptr;
            SDL_ClearComposition(ctx->active_window);
            SDL_StopTextInput(ctx->active_window);
            ctx->active_ui->ime_input_started = false;
            ctx->active_ui->applied_ime_password = false;
            ctx->active_ui->applied_ime_rect = SDL_Rect{-1, -1, 0, 0};
            ctx->active_ui->applied_ime_cursor = -1;
        }
    }

    if (!ctx->headless && ctx->active_window &&
        (SDL_GetMouseFocus() == ctx->active_window || ctx->mouse_capture_window == ctx->active_window) &&
        ctx->active_ui->desired_cursor != ctx->applied_cursor) {
        const uint32_t cidx = static_cast<uint32_t>(ctx->active_ui->desired_cursor);
        if (cidx < kCursorCount) {
            if (!ctx->system_cursors[cidx]) {
                ctx->system_cursors[cidx] = SDL_CreateSystemCursor(to_sdl_system_cursor(ctx->active_ui->desired_cursor));
            }
            if (ctx->system_cursors[cidx]) {
                SDL_SetCursor(ctx->system_cursors[cidx]);
                ctx->applied_cursor = ctx->active_ui->desired_cursor;
            }
        }
    }

    if (ctx->active_ui->active_anim_count > 0u) {
        ctx->active_ui->redraw_budget = 2u;
    }

    if (ctx->active_ui->states.entries) {
        for (uint32_t i = 0u; i < ctx->active_ui->states.capacity; ++i) {
            WidgetState& ws = ctx->active_ui->states.entries[i];
            if (ws.key == 0u || ws.last_seen_frame != ctx->active_ui->frame_index) {
                if (ws.last_seen_frame != ctx->active_ui->frame_index) ws.edit_text_buffer = nullptr;
                continue;
            }
            if (ws.is_user_element || ws.split_initialized || ws.is_canvas || ws.is_canvas_node || ws.is_virtual_list) {
                const Clay_ElementData ed = Clay_GetElementData(make_clay_id(ws.key));
                if (ed.found) {
                    ws.last_box_logical = Rect{
                        ed.boundingBox.x,
                        ed.boundingBox.y,
                        ed.boundingBox.width,
                        ed.boundingBox.height,
                    };
                    if(ws.edit_text_buffer) {ws.viewport_dim={ed.boundingBox.width,ed.boundingBox.height};ws.vscroll_max_y=std::fmax(0.0,ws.vscroll_total_h-ed.boundingBox.height);}
                }
            }
        }
    }

    for (uint32_t i = 0u; i < ctx->active_ui->pending_chrome_drag_count; ++i) {
        ctx->active_ui->chrome_drag_rects[i] = ctx->active_ui->pending_chrome_drag_rects[i];
    }
    ctx->active_ui->chrome_drag_count = ctx->active_ui->pending_chrome_drag_count;
    ctx->active_ui->pending_chrome_drag_count = 0u;

    uint32_t chrome_client_n = 0u;
    for (uint32_t i = 0u; i < ctx->active_ui->pending_chrome_client_count && chrome_client_n < kMaxChromeClientRects; ++i) {
        ctx->active_ui->chrome_client_rects[chrome_client_n++] = ctx->active_ui->pending_chrome_client_rects[i];
    }
    ctx->active_ui->pending_chrome_client_count = 0u;

    if (ctx->active_ui->chrome_desc.enabled && ctx->active_ui->chrome_drag_count > 0u) {
        for (uint32_t i = 0u; i < ctx->active_ui->span_hit_count && chrome_client_n < kMaxChromeClientRects; ++i) {
            const SpanHitRegion& r = ctx->active_ui->span_hits[i];
            if (r.id == 0u && r.rect.w > 0.0f && r.rect.h > 0.0f) {
                for (uint32_t d=0;d<ctx->active_ui->chrome_drag_count;++d) {
                    if (ctx->active_ui->chrome_drag_rects[d].intersects(r.rect)) {
                        ctx->active_ui->chrome_client_rects[chrome_client_n++] = r.rect;break;
                    }
                }
            }
        }
        if (ctx->active_ui->states.entries) {
            for (uint32_t i = 0u; i < ctx->active_ui->states.capacity && chrome_client_n < kMaxChromeClientRects; ++i) {
                const WidgetState& ws = ctx->active_ui->states.entries[i];
                if (ws.key == 0u || ws.last_seen_frame != ctx->active_ui->frame_index || !ws.is_user_element || ws.window_drag_passthrough) {
                    continue;
                }
                const Rect r = ws.last_box_logical;
                if (r.w <= 0.0f || r.h <= 0.0f) {
                    continue;
                }
                bool hits_drag = false;
                for (uint32_t d = 0u; d < ctx->active_ui->chrome_drag_count; ++d) {
                    if (ctx->active_ui->chrome_drag_rects[d].intersects(r)) {
                        hits_drag = true;
                        break;
                    }
                }
                if (hits_drag) {
                    ctx->active_ui->chrome_client_rects[chrome_client_n++] = r;
                }
            }
        }
    }
    ctx->active_ui->chrome_client_count = chrome_client_n;
    ctx->active_ui->chrome_maximize_rect = ctx->active_ui->pending_chrome_maximize_id
        ? element_bounds(ctx->active_ui->pending_chrome_maximize_id) : ctx->active_ui->pending_chrome_maximize_rect;
    ctx->active_ui->pending_chrome_maximize_id=0;ctx->active_ui->pending_chrome_maximize_rect={};

    const uint64_t end_ticks = SDL_GetPerformanceCounter();
    const uint64_t freq = SDL_GetPerformanceFrequency();
    const double layout_ms = (freq > 0u)
        ? (static_cast<double>(end_ticks - ctx->layout_start_ticks) * 1000.0 / static_cast<double>(freq))
        : 0.0;

    ctx->active_ui->layout_us_hist[ctx->active_ui->hud_hist_cursor] = static_cast<float>(layout_ms * 1000.0);
    ctx->active_ui->gpu_us_hist[ctx->active_ui->hud_hist_cursor] = static_cast<float>(ctx->stats.gpu_submit_ms * 1000.0);
    ctx->active_ui->hud_hist_cursor = (ctx->active_ui->hud_hist_cursor + 1u) % kHudHistorySamples;
    if (ctx->active_ui->hud_hist_count < kHudHistorySamples) {
        ++ctx->active_ui->hud_hist_count;
    }

    const uint32_t app_prim_count = ctx->prim_count;
    const uint32_t app_batch_count = ctx->batch_count;
    const uint32_t app_blur_count = ctx->blur_split_count;

    if (ctx->active_ui->debug_wireframe_open && app_prim_count > 0u) {
        const uint32_t step = (app_prim_count > 320u) ? ((app_prim_count + 319u) / 320u) : 1u;
        for (uint32_t pi = 0u; pi < app_prim_count; pi += step) {
            const Prim src = ctx->prims[pi];
            const uint32_t kind = src.style.x & 0xFu;
            if (kind == static_cast<uint32_t>(PrimKind::Shadow)) {
                continue;
            }
            if (src.bounds.z < 1.0f || src.bounds.w < 1.0f) {
                continue;
            }
            Color wc = Color::rgba8(56, 189, 248, 165);
            if (kind == static_cast<uint32_t>(PrimKind::Glyph) || kind == static_cast<uint32_t>(PrimKind::SlugGlyph)) {
                wc = Color::rgba8(251, 191, 36, 145);
            } else if (kind == static_cast<uint32_t>(PrimKind::Texture) || kind == static_cast<uint32_t>(PrimKind::BlurGlass)) {
                wc = Color::rgba8(244, 114, 182, 180);
            } else if (kind == static_cast<uint32_t>(PrimKind::BezierWire) ||
                       kind == static_cast<uint32_t>(PrimKind::RingArc)) {
                wc = Color::rgba8(52, 211, 153, 185);
            } else if (kind == static_cast<uint32_t>(PrimKind::CanvasGrid)) {
                wc = Color::rgba8(148, 163, 184, 110);
            }
            push_prim(
                ctx,
                Prim::make_rect(
                    Rect{ src.bounds.x, src.bounds.y, src.bounds.z, src.bounds.w },
                    root_clip,
                    Color::transparent(),
                    {},
                    wc,
                    1.0f * dpi,
                    1.0f,
                    0.0f),
                nullptr,
                0.0f);
        }
    }

    if (ctx->active_ui->drag_active && ctx->active_ui->has_pointer) {
        const float vp_w = static_cast<float>(ctx->active_ui->viewport_w) / dpi;
        const float vp_h = static_cast<float>(ctx->active_ui->viewport_h) / dpi;
        const Str lbl = (ctx->active_ui->drag_label_len > 0u)
            ? Str{ ctx->active_ui->drag_label, ctx->active_ui->drag_label_len, false }
            : str_lit("Item");
        const Vec2 tsz = measure_multiline_text_metrics(ctx, lbl, kFontSans, 12.0f, 0.0f, nullptr, nullptr);
        const float pill_w = clamp_range(tsz.x + 26.0f, 76.0f, 240.0f);
        const float pill_h = 26.0f;
        const float max_x = (vp_w - pill_w - 4.0f > 4.0f) ? (vp_w - pill_w - 4.0f) : 4.0f;
        const float max_y = (vp_h - pill_h - 4.0f > 4.0f) ? (vp_h - pill_h - 4.0f) : 4.0f;
        const float gx = clamp_range(ctx->active_ui->pointer_pos.x + 14.0f, 4.0f, max_x);
        const float gy = clamp_range(ctx->active_ui->pointer_pos.y + 14.0f, 4.0f, max_y);
        const ClipEntry pill_clip = { .rect = root_clip, .radius = 0.0f };

        push_prim(
            ctx,
            Prim::make_shadow(
                Rect{ gx * dpi, (gy + 3.0f) * dpi, pill_w * dpi, pill_h * dpi },
                root_clip,
                Color::rgba8(0, 0, 0, 145),
                Corners::all(8.0f * dpi),
                10.0f * dpi,
                0.0f));
        push_prim(
            ctx,
            Prim::make_rect(
                Rect{ gx * dpi, gy * dpi, pill_w * dpi, pill_h * dpi },
                root_clip,
                Color::rgba8(15, 23, 42, 236),
                Corners::all(8.0f * dpi),
                Color::rgba8(56, 189, 248, 195),
                1.0f * dpi,
                1.0f,
                0.0f));
        push_prim(
            ctx,
            Prim::make_rect(
                Rect{ (gx + 6.0f) * dpi, (gy + 6.0f) * dpi, 3.0f * dpi, (pill_h - 12.0f) * dpi },
                root_clip,
                Color::rgb_hex(0x38BDF8),
                Corners::all(1.5f * dpi)));
        emit_direct_text_px(
            ctx,
            lbl.chars,
            gx + 14.0f,
            gy + 6.0f,
            12.0f,
            Color::rgb_hex(0xF8FAFC),
            pill_clip,
            dpi);
    }

    const bool idle_now = !needs_redraw(ctx);
    if (ctx->active_ui->debug_hud_open) {
        const float vp_w = static_cast<float>(ctx->active_ui->viewport_w) / dpi;
        const float hud_w = (vp_w > 340.0f) ? 292.0f : ((vp_w > 180.0f) ? (vp_w - 20.0f) : 160.0f);
        const float hud_h = 154.0f;
        const float hud_x = (vp_w - hud_w - 14.0f > 4.0f) ? (vp_w - hud_w - 14.0f) : 4.0f;
        const float vp_h = static_cast<float>(ctx->active_ui->viewport_h) / dpi;
        float hud_y = 14.0f;
        if (ctx->active_ui->chrome_desc.enabled) {
            hud_y = 52.0f;
            for (uint32_t i = 0u; i < ctx->active_ui->chrome_drag_count; ++i) {
                const Rect r = ctx->active_ui->chrome_drag_rects[i];
                if (r.y + r.h + 8.0f > hud_y) hud_y = r.y + r.h + 8.0f;
            }
        }
        if (hud_y + hud_h > vp_h - 4.0f)
            hud_y = (vp_h > hud_h + 4.0f) ? (vp_h - hud_h - 4.0f) : 4.0f;
        const ClipEntry hud_clip = { .rect = root_clip, .radius = 0.0f };

        push_prim(
            ctx,
            Prim::make_shadow(
                Rect{ hud_x * dpi, (hud_y + 4.0f) * dpi, hud_w * dpi, hud_h * dpi },
                root_clip,
                Color::rgba8(0, 0, 0, 150),
                Corners::all(10.0f * dpi),
                14.0f * dpi,
                0.0f));
        push_prim(
            ctx,
            Prim::make_rect(
                Rect{ hud_x * dpi, hud_y * dpi, hud_w * dpi, hud_h * dpi },
                root_clip,
                Color::rgba8(9, 13, 22, 248),
                Corners::all(10.0f * dpi),
                Color::rgba8(56, 189, 248, 95),
                1.0f * dpi,
                1.0f,
                0.0f));
        push_prim(
            ctx,
            Prim::make_gradient_rect(
                Rect{ (hud_x + 10.0f) * dpi, (hud_y + 1.0f) * dpi, (hud_w - 20.0f) * dpi, 2.5f * dpi },
                root_clip,
                Color::rgb_hex(0x38BDF8),
                Color::rgb_hex(0x818CF8),
                Vec2{ 1.0f, 0.0f },
                Corners::all(1.25f * dpi)));

        emit_direct_text_px(
            ctx,
            "ETCH " ETCH_VERSION_STRING " INSPECTOR [F12]",
            hud_x + 12.0f,
            hud_y + 10.0f,
            12.0f,
            Color::rgb_hex(0xF8FAFC),
            hud_clip,
            dpi);
        if (ctx->active_ui->debug_wireframe_open) {
            emit_direct_text_px(
                ctx,
                "[F11 WIRE]",
                hud_x + hud_w - 72.0f,
                hud_y + 10.5f,
                11.0f,
                Color::rgb_hex(0xFBBF24),
                hud_clip,
                dpi);
        }

        char line[96] = {};
        std::snprintf(
            line,
            sizeof(line),
            "Layout: %.0f us   GPU: %.0f us",
            layout_ms * 1000.0,
            ctx->stats.gpu_submit_ms * 1000.0);
        emit_direct_text_px(ctx, line, hud_x + 12.0f, hud_y + 30.0f, 11.0f, Color::rgb_hex(0x38BDF8), hud_clip, dpi);

        std::snprintf(
            line,
            sizeof(line),
            "Batches: %u DC   Blur: %u pass",
            app_batch_count,
            app_blur_count);
        emit_direct_text_px(ctx, line, hud_x + 12.0f, hud_y + 47.0f, 11.0f, Color::rgb_hex(0xE2E8F0), hud_clip, dpi);

        std::snprintf(
            line,
            sizeof(line),
            "Prims: %u   Glyphs: %u   Curves: %u",
            app_prim_count,
            ctx->atlas.slug_glyph_count,
            ctx->atlas.slug_curve_count);
        emit_direct_text_px(ctx, line, hud_x + 12.0f, hud_y + 64.0f, 10.5f, Color::rgb_hex(0xCBD5E1), hud_clip, dpi);

        const uint32_t arena_kb = static_cast<uint32_t>((ctx->frame_arena.used() + 512u) / 1024u);
        const uint32_t arena_cap_kb = static_cast<uint32_t>(ctx->frame_arena.capacity / 1024u);
        std::snprintf(
            line,
            sizeof(line),
            "Arena: %u / %u KB   State: %s",
            arena_kb,
            arena_cap_kb,
            idle_now ? "SLEEP 0%" : "ACTIVE");
        emit_direct_text_px(
            ctx,
            line,
            hud_x + 12.0f,
            hud_y + 81.0f,
            10.5f,
            idle_now ? Color::rgb_hex(0x34D399) : Color::rgb_hex(0x94A3B8),
            hud_clip,
            dpi);

        const float chart_x = hud_x + 12.0f;
        const float chart_y = hud_y + 104.0f;
        const float chart_w = hud_w - 24.0f;
        const float chart_h = 38.0f;
        push_prim(
            ctx,
            Prim::make_rect(
                Rect{ chart_x * dpi, chart_y * dpi, chart_w * dpi, chart_h * dpi },
                root_clip,
                Color::rgba8(15, 23, 42, 215),
                Corners::all(5.0f * dpi),
                Color::rgba8(148, 163, 184, 34),
                1.0f * dpi));

        float peak_us = 250.0f;
        for (uint32_t hi = 0u; hi < ctx->active_ui->hud_hist_count; ++hi) {
            if (ctx->active_ui->layout_us_hist[hi] > peak_us) {
                peak_us = ctx->active_ui->layout_us_hist[hi];
            }
        }
        const uint32_t bar_slots = kHudHistorySamples;
        const float inner_w = chart_w - 8.0f;
        const float pitch = inner_w / static_cast<float>(bar_slots);
        const float bar_w = (pitch > 1.5f) ? (pitch - 0.8f) : pitch;
        const uint32_t n = ctx->active_ui->hud_hist_count;
        const uint32_t start = (n == kHudHistorySamples) ? ctx->active_ui->hud_hist_cursor : 0u;
        for (uint32_t bi = 0u; bi < n; ++bi) {
            const uint32_t idx = (start + bi) % kHudHistorySamples;
            const float us = ctx->active_ui->layout_us_hist[idx];
            const float norm = clamp_range(us / peak_us, 0.06f, 1.0f);
            const float bh = norm * (chart_h - 8.0f);
            const float bx = chart_x + 4.0f + static_cast<float>(bar_slots - n + bi) * pitch;
            const float by = chart_y + chart_h - 4.0f - bh;
            const Color bc = (us > 200.0f)
                ? Color::rgba8(251, 191, 36, 225)
                : Color::rgba8(56, 189, 248, 210);
            push_prim(
                ctx,
                Prim::make_rect(
                    Rect{ bx * dpi, by * dpi, bar_w * dpi, bh * dpi },
                    root_clip,
                    bc,
                    Corners::all(1.0f * dpi)));
        }
    }

    while (ctx->active_ui->modal_count && ctx->active_ui->modal_scopes[ctx->active_ui->modal_count - 1].seen != ctx->active_ui->frame_index) {
        const uint32_t restore = ctx->active_ui->modal_scopes[--ctx->active_ui->modal_count].restore;
        const WidgetState* prior = state_map_find(&ctx->active_ui->states, restore);
        ctx->active_ui->focus_id = prior && prior->is_focusable && prior->last_seen_frame == ctx->active_ui->frame_index ? prior->key : 0;
        ctx->active_ui->modal_scope = ctx->active_ui->modal_count ? ctx->active_ui->modal_scopes[ctx->active_ui->modal_count - 1].id : 0;
        ctx->active_ui->modal_needs_focus = false;
    }
    if (ctx->active_ui->drag_edit_id != 0u && ctx->active_ui->drag_edit_seen_frame != ctx->active_ui->frame_index) {
        ctx->active_ui->drag_edit_id = 0u;
        ctx->active_ui->drag_edit_len = 0u;
    }
    ctx->active_ui->drag_edit_pending_id = 0u;
    ctx->active_ui->frame_drop_count = 0;
    ctx->active_ui->frame_text_drop_count = 0;
    ctx->active_ui->has_delivered_drop_pos = false;
    // A window-only context consumes global invalidations through its primary pass.
    if (!ctx->headless && ctx->active_window == ctx->window) ctx->headless_ui.redraw_budget = 0;
    a11y_commit(ctx);
    ctx->in_frame = false;
    ctx->stats.sprite_count = ctx->atlas.sprite_live;
    ctx->stats.sprite_atlas_size = ctx->atlas.width;
    ctx->stats.prim_count = ctx->prim_count;
    ctx->stats.prim_capacity = ctx->max_prims;
    ctx->stats.draw_call_count = ctx->batch_count;
    ctx->stats.blur_pass_count = app_blur_count;
    ctx->stats.slug_glyph_count = ctx->atlas.slug_glyph_count;
    ctx->stats.slug_curve_count = ctx->atlas.slug_curve_count;
    ctx->stats.dynamic_slug_curve_count = ctx->dyn_slug_curve_count;
    ctx->stats.arena_used_bytes = ctx->frame_arena.used();
    ctx->stats.arena_peak_bytes = ctx->frame_arena.peak_offset;
    ctx->stats.layout_ms = layout_ms;
    ctx->stats.active_animations = ctx->active_ui->active_anim_count;
    ctx->stats.idle_sleeping = idle_now;
    ctx->stats.ime_cursor_offset_win = ctx->active_ui->ime_cursor_offset_win;
}

bool frame_in_progress(const Context* ctx) noexcept { return ctx && ctx->in_frame; }
SystemTheme system_theme() noexcept { return static_cast<SystemTheme>(SDL_GetSystemTheme()); }
bool system_dark_mode(bool fallback) noexcept {
    const SystemTheme theme = system_theme();
    return theme == SystemTheme::Unknown ? fallback : theme == SystemTheme::Dark;
}
bool needs_window_redraw(const Context* ctx, SDL_Window* window) noexcept {
    const WindowUiState* ui = ctx ? window_ui(ctx, window) : nullptr;
    return ui && (ui->redraw_budget || ui->active_anim_count);
}
void request_window_redraw(Context* ctx, SDL_Window* window) noexcept {
    WindowUiState* ui = ctx ? window_ui(ctx, window) : nullptr;
    if (ui) ui->redraw_budget = 2;
}
void process_event(Context* ctx, const SDL_Event& event) noexcept {
    if (!ctx) return;
    if (event.type == SDL_EVENT_SYSTEM_THEME_CHANGED || event.type == ctx->wake_event_type) {
        ctx->headless_ui.redraw_budget = 2;
        for (uint32_t i = 0; i < ctx->claimed_window_cap; ++i)
            if (ctx->claimed_windows[i]) ctx->claimed_windows[i]->ui.redraw_budget = 2;
        return;
    }
    SDL_WindowID id = 0;
    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION: id = event.motion.windowID; break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: id = event.button.windowID; break;
        case SDL_EVENT_MOUSE_WHEEL: id = event.wheel.windowID; break;
        case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: id = event.key.windowID; break;
        case SDL_EVENT_TEXT_EDITING: id = event.edit.windowID; break;
        case SDL_EVENT_TEXT_INPUT: id = event.text.windowID; break;
        case SDL_EVENT_DROP_BEGIN: case SDL_EVENT_DROP_POSITION: case SDL_EVENT_DROP_COMPLETE:
        case SDL_EVENT_DROP_FILE: case SDL_EVENT_DROP_TEXT: id = event.drop.windowID; break;
        default:
            if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) id = event.window.windowID;
            break;
    }
    SDL_Window* window = id ? SDL_GetWindowFromID(id) : ctx->window;
    if (id && !window) return;
    WindowUiState* ui = window_ui(ctx, window);
    if (!ui) return;
    WindowUiState* saved = ctx->active_ui;
    SDL_Window* saved_window = ctx->active_window;
    ctx->active_ui = ui; ctx->active_window = window;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) ui->keyboard_focus_active = false;
    if (event.type == SDL_EVENT_KEY_DOWN && (event.key.key == SDLK_TAB || event.key.key == SDLK_LEFT ||
        event.key.key == SDLK_RIGHT || event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)) ui->keyboard_focus_active = true;
    process_window_event(ctx, event);
    ctx->active_ui = saved; ctx->active_window = saved_window;
}
void begin_frame(Context* ctx, const FrameDesc& desc) noexcept {
    if (!ctx || ctx->in_frame) return;
    ctx->active_window = ctx->headless ? nullptr : ctx->window;
    ctx->active_ui = ctx->headless ? &ctx->headless_ui : window_ui(ctx, nullptr);
    begin_bound_frame(ctx, desc);
}
void begin_window_frame(Context* ctx, SDL_Window* window, float dt) noexcept {
    begin_window_frame(ctx, window, WindowFrameDesc{.dt = dt});
}
void begin_window_frame(Context* ctx, SDL_Window* window, const WindowFrameDesc& desc) noexcept {
    if (!ctx || !window || ctx->in_frame || !claim_window_swapchain(ctx, window)) return;
    ctx->active_window = window; ctx->active_ui = window_ui(ctx, window);
    ctx->active_ui->min_dpi_scale = (desc.min_scale > 0.0f && std::isfinite(desc.min_scale)) ? desc.min_scale : 0.0f;
    sync_desktop_window(window);
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    float scale = SDL_GetWindowDisplayScale(window);
    if (scale <= 0) scale = 1;
    if (scale < ctx->active_ui->min_dpi_scale) scale = ctx->active_ui->min_dpi_scale;
    begin_bound_frame(ctx, FrameDesc{.viewport_w = static_cast<uint32_t>(w > 0 ? w : 1),
        .viewport_h = static_cast<uint32_t>(h > 0 ? h : 1), .dpi_scale = scale, .dt_seconds = desc.dt, .pixel_snap = desc.pixel_snap});
}
void end_window_frame(Context* ctx) noexcept { end_frame(ctx); }
void begin_focus_scope(uint32_t scope_id, bool trap_tab) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || !scope_id || ctx->active_ui->focus_scope_depth == 32) return;
    WindowUiState& ui = *ctx->active_ui;
    ui.focus_scopes[ui.focus_scope_depth++] = ui.focus_scope;
    ui.focus_scope = scope_id;
    if (trap_tab) {
        uint32_t index = 0;
        while (index < ui.modal_count && ui.modal_scopes[index].id != scope_id) ++index;
        if (index == ui.modal_count) {
            if (ui.modal_count == 32) return;
            ui.modal_scopes[ui.modal_count++] = ModalFocusScope{scope_id, ui.focus_id, ui.frame_index};
            ui.focus_id = 0;
            ui.modal_needs_focus = true;
        }
        ui.modal_scopes[index].seen = ui.frame_index;
        ui.modal_scope = ui.modal_scopes[ui.modal_count - 1].id;
        ui.pending_tab_delta = 0;
    }
}
void end_focus_scope() noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || !ctx->active_ui->focus_scope_depth) return;
    WindowUiState& ui = *ctx->active_ui;
    ui.focus_scope = ui.focus_scopes[--ui.focus_scope_depth];
}
bool keyboard_focus_visible(uint32_t id) noexcept {
    Context* ctx = current_context();
    return ctx && ctx->active_ui->keyboard_focus_active && ctx->active_ui->focus_id &&
        (!id || ctx->active_ui->focus_id == id);
}
namespace {
void store_os_drop(WindowUiState& ui, Str value, bool text) noexcept {
    uint32_t& count = text ? ui.pending_text_drop_count : ui.pending_drop_count;
    uint32_t& capacity=text ? ui.pending_text_drop_cap : ui.pending_drop_cap;
    DropRecord*& records=text ? ui.pending_text_drop_records : ui.pending_drop_records;
    if(count==capacity) {
        const uint32_t next=capacity ? capacity*2 : 32;
        void* p=std::realloc(records,size_t(next)*sizeof(DropRecord));
        if(!p) {SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Etch drop metadata allocation failed");return;}
        records=static_cast<DropRecord*>(p);capacity=next;
    }
    const uint32_t needed=ui.pending_drop_bytes+value.length+1;
    if(needed>ui.pending_drop_capacity) {
        uint32_t next=ui.pending_drop_capacity ? ui.pending_drop_capacity : 4096;
        while(next<needed) next*=2;
        void* p=std::realloc(ui.pending_drop_buf,next);
        if(!p) {SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Etch drop payload allocation failed");return;}
        ui.pending_drop_buf=static_cast<char*>(p);ui.pending_drop_capacity=next;
    }
    const uint32_t offset=ui.pending_drop_bytes;
    std::memcpy(ui.pending_drop_buf+offset,value.chars,value.length);
    ui.pending_drop_buf[offset+value.length]='\0';
    records[count++]={offset,value.length};
    ui.pending_drop_bytes += value.length + 1;
    ui.redraw_budget = 2;
}
bool os_target(Context* ctx, uint32_t id, Vec2 pos) noexcept {
    WindowUiState& ui = *ctx->active_ui;
    int32_t target = -1;
    bool has_drop_hit = false;
    for (uint32_t i = 0; i < ui.drop_hit_count; ++i) {
        const SpanHitRegion& hit = ui.drop_hits[i];
        if (hit.id == id) has_drop_hit = true;
        if (hit.id == id && hit.rect.contains(pos) && (!hit.has_clip || hit.clip_rect.contains(pos))) target = static_cast<int32_t>(i);
    }
    if (target < 0) {
        if (has_drop_hit) return false;
        const WidgetState* ws = state_map_find(&ui.states, id);
        if (!ws || ws->last_seen_frame + 1u < ui.frame_index || !ws->last_box_logical.contains(pos)) return false;
        for (uint32_t i = 0; i < ui.span_hit_count; ++i) {
            const SpanHitRegion& hit = ui.span_hits[i];
            if (hit.id == 0 && hit.z_index >= 0 && hit.rect.contains(pos) &&
                (!hit.has_clip || hit.clip_rect.contains(pos))) return false;
        }
        return true;
    }
    const int16_t z = ui.drop_hits[target].z_index;
    for (uint32_t i = 0; i < ui.span_hit_count; ++i) {
        const SpanHitRegion& hit = ui.span_hits[i];
        if (hit.id == 0 && (hit.z_index > z || (hit.z_index == z && hit.paint_order > ui.drop_hits[target].paint_order)) && hit.rect.contains(pos) && (!hit.has_clip || hit.clip_rect.contains(pos))) return false;
    }
    return true;
}
}
void inject_drop_file(Context* ctx, Str value, SDL_Window* window) noexcept {
    WindowUiState* ui = ctx ? window_ui(ctx, window) : nullptr;
    if (ui && value.chars && value.length) store_os_drop(*ui, value, false);
}
void inject_drop_text(Context* ctx, Str value, SDL_Window* window) noexcept {
    WindowUiState* ui = ctx ? window_ui(ctx, window) : nullptr;
    if (ui && value.chars && value.length) store_os_drop(*ui, value, true);
}
void inject_os_drop_move(Context* ctx, Vec2 pos, SDL_Window* window) noexcept {
    WindowUiState* ui = ctx ? window_ui(ctx, window) : nullptr;
    if (!ui) return;
    ui->os_drop_pos = pos; ui->has_os_drop_pos = true; ui->os_dragging = true; ui->redraw_budget = 2;
}
void inject_os_drop_begin(Context* ctx, Vec2 pos, SDL_Window* window) noexcept {
    WindowUiState* ui = ctx ? window_ui(ctx, window) : nullptr;
    if (!ui) return;
    ui->pending_drop_count = 0;
    ui->pending_text_drop_count = 0;
    ui->pending_drop_bytes = 0;
    ui->has_delivered_drop_pos = false;
    inject_os_drop_move(ctx, pos, window);
}
void inject_os_drop_complete(Context* ctx, Vec2 pos, SDL_Window* window) noexcept {
    WindowUiState* ui = ctx ? window_ui(ctx, window) : nullptr;
    if (!ui) return;
    ui->os_dragging = false; ui->os_drop_pos = pos; ui->has_os_drop_pos = true;
    ui->delivered_drop_pos = pos; ui->has_delivered_drop_pos = true; ui->redraw_budget = 2;
}
bool is_os_dragging() noexcept { Context* ctx = current_context(); return ctx && ctx->active_ui->os_dragging; }
Vec2 os_drop_pos() noexcept { Context* ctx = current_context(); return ctx ? ctx->active_ui->os_drop_pos : Vec2{}; }
bool os_drag_hovering(uint32_t id) noexcept {
    Context* ctx = current_context();
    return ctx && id && ctx->active_ui->os_dragging && ctx->active_ui->has_os_drop_pos && os_target(ctx, id, ctx->active_ui->os_drop_pos);
}
std::span<const Str> dropped_files_on(uint32_t id) noexcept {
    Context* ctx = current_context();
    return ctx && id && ctx->active_ui->has_delivered_drop_pos && os_target(ctx, id, ctx->active_ui->delivered_drop_pos) ? dropped_files() : std::span<const Str>{};
}
std::span<const Str> dropped_texts() noexcept {
    Context* ctx = current_context();
    return ctx ? std::span<const Str>{ctx->active_ui->frame_text_drops, ctx->active_ui->frame_text_drop_count} : std::span<const Str>{};
}
std::span<const Str> dropped_texts_on(uint32_t id) noexcept {
    Context* ctx = current_context();
    return ctx && id && ctx->active_ui->has_delivered_drop_pos && os_target(ctx, id, ctx->active_ui->delivered_drop_pos) ? dropped_texts() : std::span<const Str>{};
}

} // namespace etch
