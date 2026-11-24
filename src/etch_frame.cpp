// Frame composition, F12 HUD, focus scopes & OS drop injection.
// Compiled via src/etch_unity.cpp.

namespace etch {

void automation_record_frame_input(Context* ctx, const RecordedFrameInput& input) noexcept;

namespace {
bool locale_is_rtl(const char* locale) noexcept {
    if (!locale || locale[0] == '\0') return false;
    char lang[8] = {};
    uint32_t len = 0u;
    while (locale[len] != '\0' && locale[len] != '-' && locale[len] != '_' &&
           locale[len] != '.' && locale[len] != '@' && len < 7u)
    {
        char c = locale[len];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        lang[len++] = c;
    }
    lang[len] = '\0';
    if (len != 2u) return false;
    return (lang[0] == 'a' && lang[1] == 'r') ||
           (lang[0] == 'h' && lang[1] == 'e') ||
           (lang[0] == 'i' && lang[1] == 'w') ||
           (lang[0] == 'f' && lang[1] == 'a') ||
           (lang[0] == 'u' && lang[1] == 'r') ||
           (lang[0] == 'y' && lang[1] == 'i') ||
           (lang[0] == 'p' && lang[1] == 's') ||
           (lang[0] == 's' && lang[1] == 'd');
}

#if defined(_WIN32)
float query_win32_text_scale() noexcept {
    using RegGetValueWFn = LONG(WINAPI*)(HKEY, LPCWSTR, LPCWSTR, DWORD, LPDWORD, PVOID, LPDWORD);
    static RegGetValueWFn fn = nullptr;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        HMODULE mod = GetModuleHandleW(L"advapi32.dll");
        if (!mod) mod = LoadLibraryW(L"advapi32.dll");
        if (mod) {
            fn = reinterpret_cast<RegGetValueWFn>(reinterpret_cast<void*>(GetProcAddress(mod, "RegGetValueW")));
        }
    }
    if (!fn) return 1.0f;
    static constexpr DWORD kRrfRtRegDword = 0x00000010u;
    DWORD val = 100u;
    DWORD size = static_cast<DWORD>(sizeof(val));
    const LONG st = fn(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Accessibility",
        L"TextScaleFactor",
        kRrfRtRegDword,
        nullptr,
        &val,
        &size);
    if (st == ERROR_SUCCESS && val >= 100u && val <= 225u) {
        return static_cast<float>(val) / 100.0f;
    }
    return 1.0f;
}
#endif
} // namespace

void end_frame(Context* ctx_arg) noexcept {
    Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (!ctx || !ctx->in_frame || !ctx->active_ui->layout) {
        return;
    }
    ctx->theme_depth = 0u;
    ctx->theme_overflow = 0u;
    ctx->active_ui->id_stack_depth = 0u;
    ctx->active_ui->id_stack_overflow = 0u;

    while (ctx->active_ui->subtree_depth) end_cached_subtree();
    while (ctx->active_ui->collapsible_depth) end_collapsible();
    while (ctx->active_ui->collapse_depth) end_collapse();
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
    LayoutEngine* le = ctx->active_ui->layout;
    const std::span<const LayoutCmd> cmds = layout_end(le);

    // Resolve bounds before walking commands: floating descendants can render before ancestors.
    for (size_t i = 0; i < cmds.size(); ++i) {
        const LayoutCmd& cmd = cmds[i];
        if (cmd.kind == LayoutCmdKind::Custom) {
            auto* extra = static_cast<BoxExtra*>(cmd.custom_data);
            if (extra) extra->layout_rect_logical = cmd.bounds;
        }
    }
    bool corrected_heights = false;
    for (auto* rec = ctx->active_ui->active_vlists; rec; rec = rec->next) {
        // Lock variable row heights while actively dragging this list's vertical scrollbar thumb to prevent thumb jitter.
        if (ctx->active_ui->active_id == id_index(rec->list_id, kScrollbarYTag)) continue;
        auto* cache = find_vlist(ctx->active_ui, rec->list_id);
        LayoutNode* list = layout_engine_find_cur_node(le, rec->list_id);
        if (!cache || !list) continue;
        uint32_t count = rec->end_index - rec->first_index;
        if (count > list->child_count) count = list->child_count;
        uint32_t ci = list->first_child;
        for (uint32_t i = 0; i < count && ci != kLayoutInvalidNode; ++i, ci = le->nodes.data[ci].next_sibling) {
            const float h = le->nodes.data[ci].height;
            if (std::isfinite(h) && h >= 1 && std::fabs(h - vlist_height(cache, rec->first_index + i)) > .25f) {
                update_vlist_height(ctx, cache, rec->first_index + i, clamp_range(h, 1, 16384));
                corrected_heights = true;
            }
        }
    }
    if (corrected_heights) request_redraw(ctx);

    if (ctx->active_ui->active_id) {
        auto* ws = state_map_find(&ctx->active_ui->states, ctx->active_ui->active_id);
        if (ws && ws->is_selectable_text && (ws->pressed_this_frame || ws->pressed_down)) update_selectable_text_pointer(ctx, ws, cmds);
    }

    ctx->active_ui->hitboxes.clear();
    const uint32_t node_count = le->nodes.count;
    ctx->active_ui->active_node_to_hitbox = node_count ? ctx->frame_arena.alloc_uninit<uint32_t>(node_count) : nullptr;
    ctx->active_ui->active_node_to_hitbox_count = ctx->active_ui->active_node_to_hitbox ? node_count : 0u;
    if (ctx->active_ui->active_node_to_hitbox) {
        std::memset(ctx->active_ui->active_node_to_hitbox, 0xFF, size_t(node_count) * sizeof(uint32_t));
    }

    struct ActiveSubtreeCap {
        uint32_t id = 0;
        SubtreeMeta* meta = nullptr;
        uint32_t prim_start = 0;
        uint32_t hitbox_start = 0;
        uint32_t blur_start = 0;
        uint32_t clip_shape_start = 0;
        uint32_t grad_stop_start = 0;
        uint32_t filter_group_start = 0;
        bool had_layer = false;
    };
    ActiveSubtreeCap cap_stack[16] = {};
    uint32_t cap_depth = 0u;

    for (size_t i = 0; i < cmds.size(); ++i) {
        const LayoutCmd* cmd = &cmds[i];
        const ClipEntry& cur_clip = clip_stack.entries[clip_stack.depth - 1u];
        ctx->render_clip_index = cur_clip.indexed ? cur_clip.shape : 0;
        ctx->render_clip_shape = cur_clip.shape;
        ctx->render_clip_depth = clip_stack.depth + clip_stack.overflow;
        const Rect raw_box_px = {
            .x = cmd->bounds.x * dpi,
            .y = cmd->bounds.y * dpi,
            .w = cmd->bounds.w * dpi,
            .h = cmd->bounds.h * dpi,
        };

        ctx->active_render_layer = nullptr;
        switch (cmd->kind) {
            case LayoutCmdKind::Custom:
            case LayoutCmdKind::Border:
            case LayoutCmdKind::ScissorStart: {
                const auto* extra = static_cast<const BoxExtra*>(cmd->user_data);
                if (extra) ctx->active_render_layer = extra->layer;
                break;
            }
            case LayoutCmdKind::Text:
                if (cmd->user_data) {
                    ctx->active_render_layer = cmd->text_cfg.is_rich
                        ? static_cast<const RichTextExtra*>(cmd->user_data)->layer
                        : static_cast<const SelectableTextExtra*>(cmd->user_data)->layer;
                }
                break;
            case LayoutCmdKind::Image: {
                const auto* extra = static_cast<const ImageExtra*>(cmd->custom_data);
                if (extra) ctx->active_render_layer = extra->layer;
                break;
            }
            default: break;
        }
        if (ctx->active_render_layer) {
            resolve_layer_chain(ctx, ctx->active_render_layer, dpi);
            for (uint32_t ci = 0u; ci < cap_depth; ++ci) {
                cap_stack[ci].had_layer = true;
            }
        }
        if (cmd->kind == LayoutCmdKind::ScissorEnd) {
            if (ctx->active_filter_group && clip_stack.overflow == 0u && clip_stack.depth <= ctx->active_filter_group->ancestor_clip_depth) {
                finalize_filter_group(ctx);
            }
        } else if (cmd->kind != LayoutCmdKind::ScissorStart &&
                   cmd->kind != LayoutCmdKind::SubtreeBegin &&
                   cmd->kind != LayoutCmdKind::SubtreeEnd &&
                   cmd->kind != LayoutCmdKind::SubtreeReplay)
        {
            const VisualLayerNode* cmd_blur_root = ctx->active_render_layer ? ctx->active_render_layer->resolved_blur_root : nullptr;
            if (ctx->active_filter_group && ctx->active_filter_group->blur_root != cmd_blur_root) {
                finalize_filter_group(ctx);
            }
            if (cmd_blur_root && !ctx->active_filter_group) {
                begin_filter_group(ctx, cmd_blur_root, cur_clip, clip_stack.depth + clip_stack.overflow);
            }
        }
        const Rect box_px = ctx->active_ui->pixel_snap && !ctx->active_render_layer && cmd->kind != LayoutCmdKind::Text ? snap_pixel_rect(raw_box_px) : raw_box_px;
        switch (cmd->kind) {
            case LayoutCmdKind::SubtreeBegin: {
                if (cap_depth < 16u) {
                    cap_stack[cap_depth++] = ActiveSubtreeCap{
                        .id = cmd->id,
                        .meta = static_cast<SubtreeMeta*>(cmd->user_data),
                        .prim_start = ctx->prim_count,
                        .hitbox_start = ctx->active_ui->hitboxes.count,
                        .blur_start = ctx->blur_split_count,
                        .clip_shape_start = ctx->clip_shape_count,
                        .grad_stop_start = ctx->grad_stop_count,
                        .filter_group_start = ctx->filter_group_count,
                    };
                }
                break;
            }
            case LayoutCmdKind::SubtreeEnd: {
                if (cap_depth == 0u) break;
                const ActiveSubtreeCap cap = cap_stack[--cap_depth];
                SubtreeMeta* meta = cap.meta;
                bool tainted = !meta || meta->tainted ||
                    ctx->blur_split_count != cap.blur_start ||
                    ctx->clip_shape_count != cap.clip_shape_start ||
                    ctx->grad_stop_count != cap.grad_stop_start ||
                    ctx->filter_group_count != cap.filter_group_start ||
                    ctx->active_filter_group != nullptr ||
                    cap.had_layer ||
                    cur_clip.indexed;
                const uint32_t p_end = ctx->prim_count;
                if (!tainted && p_end >= cap.prim_start) {
                    uint32_t bi = 0u;
                    for (uint32_t pi = cap.prim_start; pi < p_end; ++pi) {
                        const Prim& p = ctx->prims[pi];
                        const PrimKind pk = static_cast<PrimKind>(p.style.x & 0xFu);
                        if (pk == PrimKind::BlurGlass || pk == PrimKind::LayerBlur ||
                            pk == PrimKind::PunchHole || pk == PrimKind::SlugFill ||
                            pk == PrimKind::BezierWire || pk == PrimKind::CanvasGrid ||
                            (p.style.x & (1u << 18)) != 0u)
                        {
                            tainted = true;
                            break;
                        }
                        while (bi < ctx->batch_count && pi >= ctx->batches[bi].first_prim + ctx->batches[bi].prim_count) {
                            ++bi;
                        }
                        if (bi < ctx->batch_count && (ctx->batches[bi].is_punch_hole || ctx->batches[bi].mask_tex != nullptr)) {
                            tainted = true;
                            break;
                        }
                    }
                }
                if (tainted) {
                    if (SubtreeCacheEntry* existing = subtree_cache_find(ctx->active_ui->subtree_cache.data, ctx->active_ui->subtree_cache.count, cap.id)) {
                        existing->valid = false;
                    }
                    break;
                }
                SubtreeCacheEntry* entry = subtree_cache_get_or_create(&ctx->active_ui->subtree_cache, cap.id);
                if (!entry) break;
                const uint32_t p_cnt = p_end - cap.prim_start;
                if (p_cnt > entry->prim_cap) {
                    auto* fresh = static_cast<CachedPrimRecord*>(std::realloc(entry->prims, size_t(p_cnt) * sizeof(CachedPrimRecord)));
                    if (!fresh) { entry->valid = false; break; }
                    entry->prims = fresh;
                    entry->prim_cap = p_cnt;
                }
                uint32_t bi = 0u;
                for (uint32_t k = 0; k < p_cnt; ++k) {
                    const uint32_t pi = cap.prim_start + k;
                    while (bi < ctx->batch_count && pi >= ctx->batches[bi].first_prim + ctx->batches[bi].prim_count) {
                        ++bi;
                    }
                    const Prim& p = ctx->prims[pi];
                    const uint32_t tex_slot = (p.style.x >> 10) & 0xFu;
                    SDL_GPUTexture* ext1 = nullptr;
                    uint8_t blend_mode = 0u;
                    if (bi < ctx->batch_count) {
                        const DrawBatch& b = ctx->batches[bi];
                        if (tex_slot >= 1u && tex_slot <= b.ext_count) ext1 = b.ext_textures[tex_slot - 1u];
                        blend_mode = b.blend_mode;
                    }
                    const uint8_t aux_flags = (ctx->prim_aux_flags && pi < ctx->prim_aux_cap)
                        ? ctx->prim_aux_flags[pi]
                        : blend_mode;
                    entry->prims[k] = CachedPrimRecord{
                        .prim = p,
                        .ext1 = ext1,
                        .ext2 = nullptr,
                        .blend_mode = blend_mode,
                        .aux_flags = aux_flags,
                        .is_punch = false,
                    };
                }
                entry->prim_count = p_cnt;

                const uint32_t hb_end = ctx->active_ui->hitboxes.count;
                const uint32_t hb_cnt = hb_end >= cap.hitbox_start ? (hb_end - cap.hitbox_start) : 0u;
                if (hb_cnt > entry->hitbox_cap) {
                    auto* fresh = static_cast<HitboxEntry*>(std::realloc(entry->hitboxes, size_t(hb_cnt) * sizeof(HitboxEntry)));
                    if (!fresh) { entry->valid = false; break; }
                    entry->hitboxes = fresh;
                    entry->hitbox_cap = hb_cnt;
                }
                for (uint32_t k = 0; k < hb_cnt; ++k) {
                    HitboxEntry h = ctx->active_ui->hitboxes.data[cap.hitbox_start + k];
                    h.parent_idx = (h.parent_idx >= cap.hitbox_start && h.parent_idx < hb_end)
                        ? (h.parent_idx - cap.hitbox_start)
                        : UINT32_MAX;
                    entry->hitboxes[k] = h;
                }
                entry->hitbox_count = hb_cnt;

                const uint32_t a_start = meta->a11y_start;
                const uint32_t a_end = meta->a11y_end;
                const uint32_t a_cnt = a_end >= a_start ? (a_end - a_start) : 0u;
                if (a_cnt > entry->a11y_cap) {
                    auto* fresh = static_cast<CachedA11yRecord*>(std::realloc(entry->a11y_nodes, size_t(a_cnt) * sizeof(CachedA11yRecord)));
                    if (!fresh) { entry->valid = false; break; }
                    entry->a11y_nodes = fresh;
                    entry->a11y_cap = a_cnt;
                }
                uint32_t total_str = 0u;
                uint32_t out_a = 0u;
                if (a_cnt > 0u) {
                    for (A11yBuild* b = ctx->a11y_first; b; b = b->next) {
                        if (b->index < a_start || b->index >= a_end) continue;
                        total_str += b->node.label.length + 1u + b->node.value_text.length + 1u;
                    }
                    if (total_str > entry->a11y_str_cap) {
                        auto* fresh = static_cast<char*>(std::realloc(entry->a11y_strings, size_t(total_str)));
                        if (!fresh) { entry->valid = false; break; }
                        entry->a11y_strings = fresh;
                        entry->a11y_str_cap = total_str;
                    }
                    uint32_t str_off = 0u;
                    for (A11yBuild* b = ctx->a11y_first; b && out_a < a_cnt; b = b->next) {
                        if (b->index < a_start || b->index >= a_end) continue;
                        CachedA11yRecord rec = {};
                        rec.node = b->node;
                        bool found = false;
                        const Rect bb = layout_engine_bounds(le, b->clay_id, &found);
                        if (found) rec.node.bounds = bb;
                        rec.parent_rel = (b->parent && b->parent->index >= a_start && b->parent->index < a_end)
                            ? (b->parent->index - a_start)
                            : UINT32_MAX;
                        rec.label_off = str_off;
                        if (b->node.label.chars && b->node.label.length > 0u) {
                            std::memcpy(entry->a11y_strings + str_off, b->node.label.chars, b->node.label.length);
                            str_off += b->node.label.length;
                        }
                        entry->a11y_strings[str_off++] = '\0';
                        rec.value_off = str_off;
                        if (b->node.value_text.chars && b->node.value_text.length > 0u) {
                            std::memcpy(entry->a11y_strings + str_off, b->node.value_text.chars, b->node.value_text.length);
                            str_off += b->node.value_text.length;
                        }
                        entry->a11y_strings[str_off++] = '\0';
                        entry->a11y_nodes[out_a++] = rec;
                    }
                    entry->a11y_str_len = str_off;
                }
                entry->a11y_count = out_a;

                entry->content_hash = meta->content_hash;
                entry->theme_epoch = ctx->theme_epoch;
                entry->font_revision = ctx->atlas.font_revision;
                entry->atlas_uv_epoch = ctx->atlas.uv_epoch;
                entry->dpi_scale = dpi;
                entry->viewport_w = ctx->active_ui->viewport_w;
                entry->viewport_h = ctx->active_ui->viewport_h;
                entry->logical_bounds = cmd->bounds;
                entry->origin_px = {cmd->bounds.x * dpi, cmd->bounds.y * dpi};
                entry->has_interactive = meta->has_interactive;
                entry->last_used_frame = ctx->active_ui->frame_index;
                entry->valid = true;
                break;
            }
            case LayoutCmdKind::SubtreeReplay: {
                SubtreeCacheEntry* entry = subtree_cache_find(ctx->active_ui->subtree_cache.data, ctx->active_ui->subtree_cache.count, cmd->id);
                if (!entry || !entry->valid) break;
                const float dx_logical = cmd->bounds.x - entry->logical_bounds.x;
                const float dy_logical = cmd->bounds.y - entry->logical_bounds.y;
                const float dx_px = cmd->bounds.x * dpi - entry->origin_px.x;
                const float dy_px = cmd->bounds.y * dpi - entry->origin_px.y;
                const float c_x0 = cur_clip.rect.x;
                const float c_y0 = cur_clip.rect.y;
                const float c_x1 = cur_clip.rect.x + cur_clip.rect.w;
                const float c_y1 = cur_clip.rect.y + cur_clip.rect.h;
                for (uint32_t k = 0; k < entry->prim_count; ++k) {
                    const CachedPrimRecord& rec = entry->prims[k];
                    Prim p = rec.prim;
                    p.bounds.x += dx_px;
                    p.bounds.y += dy_px;
                    p.clip.x = std::fmax(p.clip.x + dx_px, c_x0);
                    p.clip.y = std::fmax(p.clip.y + dy_px, c_y0);
                    p.clip.z = std::fmin(p.clip.z + dx_px, c_x1);
                    p.clip.w = std::fmin(p.clip.w + dy_px, c_y1);
                    const PrimKind pk = static_cast<PrimKind>(p.style.x & 0xFu);
                    if ((pk == PrimKind::Glyph || pk == PrimKind::SlugGlyph || pk == PrimKind::Texture) &&
                        (p.style.x & (uint32_t(PrimFlag_TextClip) << 4)) != 0u)
                    {
                        p.style.z = std::bit_cast<uint32_t>(std::bit_cast<float>(p.style.z) + dx_px);
                        p.style.w = std::bit_cast<uint32_t>(std::bit_cast<float>(p.style.w) + dx_px);
                    }
                    push_prim(
                        ctx,
                        p,
                        rec.ext1,
                        2.0f,
                        rec.ext2,
                        static_cast<BlendMode>(rec.blend_mode),
                        (rec.aux_flags & kPrimAuxHollowBorder) != 0u);
                }
                const uint32_t outer_parent_hb = find_parent_hitbox_for_node(ctx, cmd->node_idx);
                const uint32_t hb_base = ctx->active_ui->hitboxes.count;
                const Rect cur_clip_logical = {c_x0 / dpi, c_y0 / dpi, cur_clip.rect.w / dpi, cur_clip.rect.h / dpi};
                for (uint32_t k = 0; k < entry->hitbox_count; ++k) {
                    HitboxEntry h = entry->hitboxes[k];
                    h.layout_rect.x += dx_logical;
                    h.layout_rect.y += dy_logical;
                    h.visual_rect.x += dx_logical;
                    h.visual_rect.y += dy_logical;
                    h.clip_rect.x += dx_logical;
                    h.clip_rect.y += dy_logical;
                    h.clip_rect = rect_intersect(h.clip_rect, cur_clip_logical);
                    h.visible_rect = rect_intersect(h.visual_rect, h.clip_rect);
                    h.parent_idx = (h.parent_idx == UINT32_MAX) ? outer_parent_hb : (hb_base + h.parent_idx);
                    h.paint_order = ctx->active_ui->hitboxes.count;
                    ctx->active_ui->hitboxes.push(h);
                }
                if (entry->a11y_count > 0u) {
                    const SubtreeMeta* meta = static_cast<const SubtreeMeta*>(cmd->user_data);
                    A11yBuild* outer_parent = meta ? static_cast<A11yBuild*>(meta->parent_a11y) : ctx->a11y_first;
                    A11yBuild* saved_parent = ctx->a11y_parent;
                    A11yBuild** replayed = ctx->frame_arena.alloc<A11yBuild*>(entry->a11y_count);
                    for (uint32_t k = 0; k < entry->a11y_count; ++k) {
                        const CachedA11yRecord& rec = entry->a11y_nodes[k];
                        const Str lbl = rec.node.label.length > 0u
                            ? Str{entry->a11y_strings + rec.label_off, rec.node.label.length, false}
                            : Str{};
                        const Str val = rec.node.value_text.length > 0u
                            ? Str{entry->a11y_strings + rec.value_off, rec.node.value_text.length, false}
                            : Str{};
                        ctx->a11y_parent = (rec.parent_rel != UINT32_MAX && replayed && rec.parent_rel < k && replayed[rec.parent_rel])
                            ? replayed[rec.parent_rel]
                            : outer_parent;
                        if (A11yBuild* nb = a11y_emit(ctx, rec.node.id, rec.node.role, lbl, val, rec.node.state_flags, rec.node.actions_mask)) {
                            nb->node.bounds = {
                                rec.node.bounds.x + dx_logical,
                                rec.node.bounds.y + dy_logical,
                                rec.node.bounds.w,
                                rec.node.bounds.h,
                            };
                            nb->node.value_num = rec.node.value_num;
                            nb->node.min_value = rec.node.min_value;
                            nb->node.max_value = rec.node.max_value;
                            nb->has_explicit_bounds = true;
                            if (replayed) replayed[k] = nb;
                        }
                    }
                    ctx->a11y_parent = saved_parent;
                }
                ++ctx->active_ui->cached_subtree_hits;
                break;
            }
            case LayoutCmdKind::Custom: {
                BoxExtra* extra = static_cast<BoxExtra*>(cmd->custom_data);
                if (extra) {
                    const Rect visual = layer_rect(ctx, raw_box_px);
                    const Rect visible = rect_intersect(visual, cur_clip.rect);
                    const auto* layer = ctx->active_render_layer;
                    if (extra->kind != CustomCmdKind::CanvasWire) {
                        const LayoutNode& node = le->nodes.data[cmd->node_idx];
                        const bool opaque_surface =
                            extra->desc.background.a > 0u || extra->desc.background_end.a > 0u ||
                            extra->desc.radial_gradient ||
                            (extra->desc.gradient.kind != GradientKind::None && extra->desc.gradient.stop_count >= 2u) ||
                            extra->desc.backdrop_blur > 0.0f ||
                            extra->code_editor != nullptr || extra->text_input != nullptr ||
                            extra->kind == CustomCmdKind::Icon;
                        const uint32_t hb_idx = ctx->active_ui->hitboxes.count;
                        ctx->active_ui->hitboxes.push(HitboxEntry{
                            .id = extra->desc.id,
                            .layout_elem_id = node.id,
                            .parent_idx = find_parent_hitbox_for_node(ctx, cmd->node_idx),
                            .layout_rect = cmd->bounds,
                            .visual_rect = {visual.x / dpi, visual.y / dpi, visual.w / dpi, visual.h / dpi},
                            .visible_rect = {visible.x / dpi, visible.y / dpi, visible.w / dpi, visible.h / dpi},
                            .clip_rect = {cur_clip.rect.x / dpi, cur_clip.rect.y / dpi, cur_clip.rect.w / dpi, cur_clip.rect.h / dpi},
                            .local_scale = {extra->world_scale * (layer ? layer->resolved_scale.x : 1.0f), extra->world_scale * (layer ? layer->resolved_scale.y : 1.0f)},
                            .visual_opacity = layer ? layer->resolved_opacity : 1.0f,
                            .rotation_rad = layer ? layer->resolved_rotation_rad : 0.0f,
                            .clip_radius = std::fabs(cur_clip.radius) / dpi,
                            .paint_order = hb_idx,
                            .z_index = cmd->z_index,
                            .behavior = node.cfg.hitbox_behavior,
                            .is_floating_root = node.is_floating,
                            .is_opaque_surface = opaque_surface,
                            .is_text_span = false,
                        });
                        if (ctx->active_ui->active_node_to_hitbox && cmd->node_idx < ctx->active_ui->active_node_to_hitbox_count) {
                            ctx->active_ui->active_node_to_hitbox[cmd->node_idx] = hb_idx;
                        }
                    }
                    if (extra->desc.id && reserve_span_hits(&ctx->active_ui->drop_hits, &ctx->active_ui->drop_hit_cap, ctx->active_ui->drop_hit_count, 512u)) {
                        extra->geometry_recorded = true;
                        ctx->active_ui->drop_hits[ctx->active_ui->drop_hit_count++] = SpanHitRegion{
                            .visual_bounds = {visual.x/dpi,visual.y/dpi,visual.w/dpi,visual.h/dpi},
                            .visible_bounds = {visible.x/dpi,visible.y/dpi,visible.w/dpi,visible.h/dpi},
                            .local_scale = {extra->world_scale*(layer ? layer->resolved_scale.x : 1),extra->world_scale*(layer ? layer->resolved_scale.y : 1)},
                            .visual_opacity = layer ? layer->resolved_opacity : 1,
                            .id = extra->desc.id, .clay_elem_id = extra->desc.id,
                            .rect = cmd->bounds,
                            .paint_order = static_cast<uint32_t>(i),
                            .z_index = cmd->z_index,
                            .clip_rect = {cur_clip.rect.x / dpi, cur_clip.rect.y / dpi, cur_clip.rect.w / dpi, cur_clip.rect.h / dpi}, .has_clip = true};
                    }
                    if (extra->code_editor) {
                        emit_code_editor_command(ctx, extra->code_editor, cmd->bounds, cur_clip, dpi);
                    } else if (extra->text_input) {
                        emit_text_input_command(ctx, extra->text_input, cmd->bounds, box_px, cur_clip, dpi);
                    } else if (extra->kind == CustomCmdKind::CanvasWire && extra->canvas_wire) {
                        emit_canvas_wire_command(ctx, extra->canvas_wire, cmd->bounds, cur_clip, dpi);
                    } else if (extra->kind == CustomCmdKind::Icon) {
                        const IconDesc& idesc = extra->icon_desc;
                        const float sc = std::isfinite(idesc.scale) ? idesc.scale : 1.0f;
                        if (sc > 1e-4f) {
                            Rect icon_px = raw_box_px;
                            const bool scaled = std::fabs(sc - 1.0f) > 1e-4f;
                            if (scaled) {
                                const float nw = icon_px.w * sc;
                                const float nh = icon_px.h * sc;
                                icon_px.x += 0.5f * (icon_px.w - nw);
                                icon_px.y += 0.5f * (icon_px.h - nh);
                                icon_px.w = nw;
                                icon_px.h = nh;
                            }
                            const float rot = std::isfinite(idesc.rotation_rad) ? idesc.rotation_rad : 0.0f;
                            const float op = std::isfinite(idesc.opacity) ? idesc.opacity : 1.0f;
                            emit_vector_icon_quad(ctx, idesc.icon_id, icon_px, idesc.color, cur_clip, rot, idesc.secondary_color, op, !scaled);
                        }
                    } else {
                        if (extra->desc.attach_to != AttachTo::None && !extra->desc.pointer_passthrough &&
                            extra->desc.hitbox_behavior != HitboxBehavior::Passthrough &&
                            reserve_span_hits(&ctx->active_ui->span_hits, &ctx->active_ui->span_hit_cap, ctx->active_ui->span_hit_count, 256u)) {
                            const uint32_t slot = ctx->active_ui->span_hit_count++;
                            ctx->active_ui->span_hits[slot] = SpanHitRegion{
                                .id = 0u,
                                .clay_elem_id = extra->desc.id,
                                .rect = cmd->bounds,
                                .paint_order = static_cast<uint32_t>(i),
                                .z_index = cmd->z_index,
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
                            push_prim(
                                ctx,
                                Prim::make_rect(box_px, cur_clip.rect, {}, scale_corners_uniform(Corners::all(extra->focus_ring_radius_px), dpi),
                                    extra->focus_ring_color, 2 * dpi, 1, cur_clip.radius),
                                nullptr,
                                2.0f,
                                nullptr,
                                BlendMode::Normal,
                                true);
                        }
                        if (extra->draw_head) {
                            ClipEntry draw_clip = cur_clip;
                            const bool box_clips =
                                extra->desc.clip_children || extra->desc.scroll_x || extra->desc.scroll_y || extra->desc.shrink_width || extra->desc.shrink_height || extra->is_canvas;
                            const uint32_t saved_clip_depth = ctx->render_clip_depth;
                            if (box_clips) {
                                const ClipEntry& draw_base = (ctx->active_filter_group && clip_stack.overflow == 0u &&
                                    clip_stack.depth == ctx->active_filter_group->ancestor_clip_depth)
                                    ? clip_stack.entries[0] : cur_clip;
                                draw_clip=make_box_clip(ctx,draw_base,box_px,scale_corners(extra->desc.radius,dpi),extra->desc.continuous_corners,box_scroll_fade(ctx,extra,dpi));
                                ++ctx->render_clip_depth;
                            }
                            emit_draw_list_command(ctx, extra->draw_head, raw_box_px, draw_clip, dpi);
                            ctx->render_clip_depth = saved_clip_depth;
                        }
                    }
                }
                break;
            }
            case LayoutCmdKind::ScissorStart: {
                const BoxExtra* extra = static_cast<const BoxExtra*>(cmd->user_data);
                if (extra && extra->desc.id != 0u) {
                    if (extra->is_virtual_list) {
                        if (WidgetState* ws = state_map_find(&ctx->active_ui->states, extra->desc.id)) {
                            const double view_h = static_cast<double>(cmd->bounds.h);
                            if (std::fabs(ws->viewport_dim.x - cmd->bounds.w) > .5f || std::fabs(ws->viewport_dim.y - cmd->bounds.h) > .5f) request_redraw(ctx);
                            ws->viewport_dim = Vec2{ cmd->bounds.w, cmd->bounds.h };
                            ws->last_box_logical = cmd->bounds;
                            if (WidgetCold* c = ensure_widget_cold(ws)) {
                                c->vscroll_max_y = (c->vscroll_total_h > view_h)
                                    ? (c->vscroll_total_h - view_h)
                                    : 0.0;
                                c->vscroll_pos_y = clamp_double(c->vscroll_pos_y, 0.0, c->vscroll_max_y);
                            }
                        }
                    } else if (extra->desc.scroll_x || extra->desc.scroll_y) {
                        if (WidgetState* ws = state_map_find(&ctx->active_ui->states, extra->desc.id)) {
                            const LayoutScrollData sc = layout_engine_scroll_data(le, extra->desc.id);
                            if (sc.found) {
                                ws->viewport_dim = sc.viewport_dim;
                                ws->content_dim = sc.content_dim;
                                ws->scroll_max = Vec2{
                                    (sc.content_dim.x > sc.viewport_dim.x) ? (sc.content_dim.x - sc.viewport_dim.x) : 0.0f,
                                    (sc.content_dim.y > sc.viewport_dim.y) ? (sc.content_dim.y - sc.viewport_dim.y) : 0.0f,
                                };
                                ws->scroll_pos.x = clamp_range(ws->scroll_pos.x, 0.0f, ws->scroll_max.x);
                                if (extra->desc.scroll_y && extra->desc.stick_to_bottom && ws->scroll_stuck_bottom) {
                                    const float prev_y = ws->scroll_pos.y;
                                    ws->scroll_pos.y = ws->scroll_max.y;
                                    if (std::fabs(ws->scroll_pos.y - prev_y) > 0.5f) {
                                        ws->scroll_vel.y = 0.0f;
                                        request_redraw(ctx);
                                    }
                                } else {
                                    ws->scroll_pos.y = clamp_range(ws->scroll_pos.y, 0.0f, ws->scroll_max.y);
                                }
                            } else {
                                ws->viewport_dim = Vec2{ cmd->bounds.w, cmd->bounds.h };
                                ws->content_dim = ws->viewport_dim;
                                ws->scroll_max = {};
                            }
                            ws->last_box_logical = cmd->bounds;
                        }
                    }
                }
                if (clip_stack.depth < kMaxClipDepth) {
                    const ClipEntry& clip_base = (ctx->active_filter_group && clip_stack.overflow == 0u &&
                        clip_stack.depth == ctx->active_filter_group->ancestor_clip_depth)
                        ? clip_stack.entries[0] : cur_clip;
                    const Vec4 fade=extra ? box_scroll_fade(ctx,extra,dpi) : Vec4{};
                    clip_stack.entries[clip_stack.depth]=extra ? make_box_clip(ctx,clip_base,box_px,scale_corners(extra->desc.radius,dpi),extra->desc.continuous_corners,fade)
                        : ClipEntry{rect_intersect(clip_base.rect,box_px),clip_base.radius,clip_base.shape,clip_base.indexed};
                    if(extra && extra->is_canvas) {
                        ctx->active_ui->last_canvas_clip_px=clip_stack.entries[clip_stack.depth].rect;
                        ctx->active_ui->last_canvas_clip_radius_px=clip_stack.entries[clip_stack.depth].radius;
                    }
                    ++clip_stack.depth;
                } else {
                    ++clip_stack.overflow;
                }
                break;
            }
            case LayoutCmdKind::ScissorEnd: {
                if (clip_stack.overflow > 0u) {
                    --clip_stack.overflow;
                } else if (clip_stack.depth > 1u) {
                    --clip_stack.depth;
                }
                break;
            }
            case LayoutCmdKind::Border: {
                const BoxExtra* extra = static_cast<const BoxExtra*>(cmd->user_data);
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
                    // Retain the container's geometry and ancestor masks, excluding its content fade.
                    const ClipEntry& parent_clip=clip_stack.depth>1u ? clip_stack.entries[clip_stack.depth-2u] : cur_clip;
                    const bool has_fade = d.scroll_fade_px > 0 || d.edge_fade.left > 0 || d.edge_fade.right > 0 || d.edge_fade.top > 0 || d.edge_fade.bottom > 0;
                    const ClipEntry scrollbar_clip = has_fade
                        ? make_box_clip(ctx,parent_clip,box_px,scale_corners(d.radius,dpi),d.continuous_corners) : cur_clip;
                    ctx->render_clip_index=scrollbar_clip.indexed ? scrollbar_clip.shape : 0;
                    ctx->render_clip_shape=scrollbar_clip.shape;
                    if (const WidgetState* ws = state_map_find(&ctx->active_ui->states, d.id)) {
                        for (uint32_t axis = 0u; axis < 2u; ++axis) {
                            const bool horizontal = (axis != 0u);
                            const ScrollbarGeom sb = horizontal ? compute_hscrollbar_geom(ws) : compute_vscrollbar_geom(ws);
                            emit_scrollbar_overlay(
                                ctx, d.id, sb, horizontal, scrollbar_clip,
                                extra->scrollbar_track, extra->scrollbar_thumb,
                                extra->scrollbar_thumb_hover, extra->scrollbar_thumb_drag, dpi);
                        }
                    }
                }

                if(d.border_beam.color.a>0 || d.border_beam.base_color.a>0 || d.top_rim_alpha>0) {
                    const ClipEntry& parent=any_clip && clip_stack.depth>1u ? clip_stack.entries[clip_stack.depth-2u] : cur_clip;
                    ctx->render_clip_index=parent.indexed ? parent.shape : 0;
                    ctx->render_clip_shape=parent.shape;
                    emit_border_beam(ctx,box_px,parent,scale_corners(d.radius,dpi),d.border_beam,d.top_rim_alpha,d.continuous_corners,dpi);
                }
                if (has_segmented_border(d) && d.border_color.a > 0u &&
                    (d.border_width > 0.0f || has_border_sides(d))) {
                    const ClipEntry& parent = any_clip && clip_stack.depth > 1u && clip_stack.overflow == 0u
                        ? clip_stack.entries[clip_stack.depth - 2u] : cur_clip;
                    if (any_clip && ctx->render_clip_depth > 1u) --ctx->render_clip_depth;
                    emit_segmented_border(ctx, d, box_px, parent, dpi);
                } else if (any_clip && d.border_width > 0.0f && d.border_color.a > 0u) {
                    // Clip against parent container (below our own SCISSOR_START) so border AA edge isn't halved.
                    const ClipEntry& parent_clip = (clip_stack.depth > 1u && clip_stack.overflow == 0u)
                        ? clip_stack.entries[clip_stack.depth - 2u]
                        : (clip_stack.overflow > 0u ? cur_clip : clip_stack.entries[0]);
                    ctx->render_clip_index=parent_clip.indexed ? parent_clip.shape : 0;
                    ctx->render_clip_shape=parent_clip.shape;
                    if (ctx->render_clip_depth > 1u) --ctx->render_clip_depth;
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
                            parent_clip.radius), d.continuous_corners),
                        nullptr,
                        2.0f,
                        nullptr,
                        d.blend_mode,
                        true);
                }
                break;
            }
            case LayoutCmdKind::Text: {
                emit_text_command(ctx, cmd, cur_clip, dpi);
                break;
            }
            case LayoutCmdKind::Image: {
                const ImageExtra* extra = static_cast<const ImageExtra*>(cmd->custom_data);
                if (extra) {
                    const ImageDesc& idesc = extra->desc;
                    const auto* layer = ctx->active_render_layer;
                    if (idesc.id != 0u) {
                        const Rect visual = layer_rect(ctx, raw_box_px);
                        const Rect visible = rect_intersect(visual, cur_clip.rect);
                        const uint32_t hb_idx = ctx->active_ui->hitboxes.count;
                        ctx->active_ui->hitboxes.push(HitboxEntry{
                            .id = idesc.id,
                            .layout_elem_id = le->nodes.data[cmd->node_idx].id,
                            .parent_idx = find_parent_hitbox_for_node(ctx, cmd->node_idx),
                            .layout_rect = cmd->bounds,
                            .visual_rect = {visual.x / dpi, visual.y / dpi, visual.w / dpi, visual.h / dpi},
                            .visible_rect = {visible.x / dpi, visible.y / dpi, visible.w / dpi, visible.h / dpi},
                            .clip_rect = {cur_clip.rect.x / dpi, cur_clip.rect.y / dpi, cur_clip.rect.w / dpi, cur_clip.rect.h / dpi},
                            .local_scale = {layer ? layer->resolved_scale.x : 1.0f, layer ? layer->resolved_scale.y : 1.0f},
                            .visual_opacity = layer ? layer->resolved_opacity : 1.0f,
                            .rotation_rad = 0.0f,
                            .clip_radius = std::fabs(cur_clip.radius) / dpi,
                            .paint_order = hb_idx,
                            .z_index = cmd->z_index,
                            .behavior = HitboxBehavior::Normal,
                            .is_opaque_surface = true,
                        });
                        if (ctx->active_ui->active_node_to_hitbox && cmd->node_idx < ctx->active_ui->active_node_to_hitbox_count) {
                            ctx->active_ui->active_node_to_hitbox[cmd->node_idx] = hb_idx;
                        }
                    }
                    if (idesc.sprite_id != kInvalidSprite || idesc.texture != nullptr) {
                        Rect img_px = box_px;
                        Vec2 uv0 = idesc.uv_min, uv1 = idesc.uv_max;
                        apply_image_fit(ctx, idesc.fit, idesc.source_size, idesc.sprite_id, &img_px, &uv0, &uv1);
                        if (ctx->active_ui->pixel_snap && !layer && idesc.fit == ImageFit::Contain) {
                            img_px = snap_pixel_rect(img_px);
                        }
                        if (idesc.sprite_id != kInvalidSprite && !resolve_sprite_uv_rect(ctx, idesc.sprite_id, uv0, uv1, &uv0, &uv1)) break;
                        const uint8_t img_flags = static_cast<uint8_t>(
                            ((idesc.sprite_id != kInvalidSprite || surface_texture_premultiplied(ctx, idesc.texture)) ? PrimFlag_Premultiplied : PrimFlag_None) |
                            (idesc.continuous_corners ? PrimFlag_LinearGrad : PrimFlag_None));
                        push_prim(
                            ctx,
                            Prim::make_texture(
                                img_px,
                                cur_clip.rect,
                                uv0,
                                uv1,
                                0u,
                                scale_corners(idesc.radius, dpi),
                                idesc.tint,
                                idesc.border_color,
                                idesc.border_width * dpi,
                                img_flags,
                                cur_clip.radius),
                            idesc.sprite_id != kInvalidSprite ? nullptr : idesc.texture,
                            2.0f,
                            nullptr,
                            idesc.blend_mode);
                    }
                }
                break;
            }
            default:
                break;
        }
    }
    finalize_filter_group(ctx);

    // Retain coordinate mapping for viewport-culled boxes without emitting primitives or disabling culling.
    for (uint32_t i = 0; i < le->nodes.count; ++i) {
        const LayoutNode& element = le->nodes.data[i];
        if (element.is_text || !element.cfg.emit_custom || !element.cfg.custom_data) continue;
        auto* extra = static_cast<BoxExtra*>(element.cfg.custom_data);
        if (!extra->desc.id || extra->geometry_recorded || !element.positioned) continue;
        const Rect& r = element.bounds;
        ctx->active_render_layer = extra->layer;
        const Rect visual = layer_rect(ctx, {r.x * dpi, r.y * dpi, r.w * dpi, r.h * dpi});
        const auto* layer = extra->layer;
        if (reserve_span_hits(&ctx->active_ui->drop_hits, &ctx->active_ui->drop_hit_cap, ctx->active_ui->drop_hit_count, 512u)) {
            ctx->active_ui->drop_hits[ctx->active_ui->drop_hit_count++] = SpanHitRegion{
                .visual_bounds = {visual.x / dpi, visual.y / dpi, visual.w / dpi, visual.h / dpi},
                .local_scale = {extra->world_scale * (layer ? layer->resolved_scale.x : 1.0f), extra->world_scale * (layer ? layer->resolved_scale.y : 1.0f)},
                .visual_opacity = layer ? layer->resolved_opacity : 1.0f,
                .id = extra->desc.id};
        }
    }
    ctx->active_render_layer = nullptr;
    ctx->active_ui->active_node_to_hitbox = nullptr;
    ctx->active_ui->active_node_to_hitbox_count = 0u;
    subtree_cache_gc(&ctx->active_ui->subtree_cache, ctx->active_ui->frame_index);
    ctx->active_ui->frame_text_op_count = 0u;
    ctx->active_ui->frame_text_utf8_len = 0u;
    ctx->active_ui->frame_key_count = 0u;
    ctx->active_ui->frame_scroll_delta = {};
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
            int win_w = static_cast<int>(std::floor(
                static_cast<float>(ctx->active_ui->viewport_w) / ctx->active_ui->dpi_scale * wscale + 0.5f));
            int win_h = static_cast<int>(std::floor(
                static_cast<float>(ctx->active_ui->viewport_h) / ctx->active_ui->dpi_scale * wscale + 0.5f));
            if (win_w < 1) win_w = 1;
            if (win_h < 1) win_h = 1;
            int ix = static_cast<int>(std::floor(ctx->active_ui->ime_cursor_logical.x * wscale + 0.5f));
            int iy = static_cast<int>(std::floor(ctx->active_ui->ime_cursor_logical.y * wscale + 0.5f));
            int iw = static_cast<int>(std::floor(ctx->active_ui->ime_cursor_logical.w * wscale + 0.5f));
            int ih = static_cast<int>(std::floor(ctx->active_ui->ime_cursor_logical.h * wscale + 0.5f));
            if (iw > win_w) iw = win_w;
            if (ih > win_h) ih = win_h;
            if (ix < 0) ix = 0; else if (ix + iw > win_w) ix = (win_w > iw) ? (win_w - iw) : 0;
            if (iy < 0) iy = 0; else if (iy + ih > win_h) iy = (win_h > ih) ? (win_h - ih) : 0;
            const SDL_Rect ime_rect = { ix, iy, iw, ih };
            int ime_cursor = static_cast<int>(ctx->active_ui->ime_cursor_offset_win);
            if (ime_cursor < 0) ime_cursor = 0;
            else if (iw > 0 && ime_cursor > iw) ime_cursor = iw;
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
                if (ws.last_seen_frame != ctx->active_ui->frame_index && ws.cold) ws.cold->edit_text_buffer = nullptr;
                continue;
            }
            const bool split_init = ws.cold && ws.cold->split_initialized;
            if (ws.is_user_element || split_init || ws.is_canvas || ws.is_canvas_node || ws.is_virtual_list) {
                bool found = false;
                const Rect bb = layout_engine_bounds(le, ws.key, &found);
                if (found) {
                    ws.last_box_logical = bb;
                    if (ws.cold && ws.cold->edit_text_buffer) {
                        ws.viewport_dim = {bb.w, bb.h};
                        ws.cold->vscroll_max_y = std::fmax(0.0, ws.cold->vscroll_total_h - bb.h);
                    }
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
            "Prims: %u Glyphs: %u Curves: %u | F10 %.1fx",
            app_prim_count,
            ctx->atlas.slug_glyph_count,
            ctx->atlas.slug_curve_count, double(ctx->time_scale));
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
    ctx->active_ui->pointer_state.pinch_delta = 0.0f;
    ctx->active_ui->pointer_state.precise_scroll = false;
    if (ctx->active_ui->gesture_phase == PointerGesturePhase::End ||
        ctx->active_ui->gesture_phase == PointerGesturePhase::Cancel)
    {
        ctx->active_ui->gesture_phase = PointerGesturePhase::None;
    }
    if (ctx->active_ui->states.count > 128u &&
        (ctx->active_ui->states.count * 4u >= ctx->active_ui->states.capacity * 3u ||
         (ctx->active_ui->frame_index & 127u) == 0u))
    {
        state_map_prune(
            &ctx->active_ui->states,
            ctx->active_ui->frame_index,
            ctx->active_ui->focus_id,
            ctx->active_ui->active_id,
            ctx->active_ui->right_active_id,
            ctx->active_ui->ime_focused_id,
            true);
    }
    if (ctx->active_ui->tooltip_frame_winner != ctx->active_ui->tooltip_prev_winner &&
        ctx->active_ui->tooltip_frame_winner != 0u &&
        ctx->active_ui->redraw_budget == 0u)
    {
        ctx->active_ui->redraw_budget = 1u;
    }
    // A window-only context consumes global invalidations through its primary pass.
    if (!ctx->headless && ctx->active_window == ctx->window && ctx->headless_ui) ctx->headless_ui->redraw_budget = 0;
    const bool a11y_ok = a11y_commit_snapshot(ctx);
    ctx->active_render_layer=nullptr;
    ctx->active_ui->current_layer=nullptr;
    ctx->active_ui->box_build_stack=nullptr;
    optimize_render_batches(ctx);
    ctx->in_frame = false;
    ctx->stats.sprite_count = ctx->atlas.sprite_live;
    ctx->stats.sprite_atlas_size = ctx->atlas.width;
    ctx->stats.prim_count = ctx->prim_count;
    ctx->stats.prim_capacity = ctx->max_prims;
    ctx->stats.draw_call_count = ctx->gpu_batch_count;
    ctx->stats.blur_pass_count = app_blur_count;
    ctx->stats.slug_glyph_count = ctx->atlas.slug_glyph_count;
    ctx->stats.slug_curve_count = ctx->atlas.slug_curve_count;
    ctx->stats.dynamic_slug_curve_count = ctx->dyn_slug_curve_count;
    ctx->stats.arena_used_bytes = ctx->frame_arena.used();
    ctx->stats.arena_peak_bytes = ctx->frame_arena.peak_offset;
    ctx->stats.layout_ms = layout_ms;
    ctx->stats.active_animations = ctx->active_ui->active_anim_count;
    ctx->stats.cached_subtree_hits = ctx->active_ui->cached_subtree_hits;
    ctx->stats.idle_sleeping = idle_now;
    ctx->stats.ime_cursor_offset_win = ctx->active_ui->ime_cursor_offset_win;
    if (a11y_ok) os_a11y_notify_commit(ctx);
    a11y_finish_actions(ctx, a11y_ok);
}

bool frame_in_progress(const Context* ctx) noexcept { return ctx && ctx->in_frame; }
SystemTheme system_theme() noexcept { return static_cast<SystemTheme>(SDL_GetSystemTheme()); }
bool system_dark_mode(bool fallback) noexcept {
    const SystemTheme theme = system_theme();
    return theme == SystemTheme::Unknown ? fallback : theme == SystemTheme::Dark;
}
SystemPreferences system_preferences(const Context* ctx) noexcept {
    const Context* c = ctx ? ctx : current_context();
    if (c && c->has_sys_prefs_override) {
        return c->sys_prefs_override;
    }
    SystemPreferences prefs = {};
    prefs.theme = system_theme();
    prefs.text_scale = 1.0f;
#if defined(_WIN32)
    HIGHCONTRASTW hc = {};
    hc.cbSize = static_cast<UINT>(sizeof(hc));
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, static_cast<UINT>(sizeof(hc)), &hc, 0u)) {
        prefs.high_contrast = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0u;
    }
    BOOL anim = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0u, &anim, 0u)) {
        prefs.reduced_motion = (anim == FALSE);
    }
    prefs.text_scale = query_win32_text_scale();
    wchar_t wloc[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(wloc, LOCALE_NAME_MAX_LENGTH) > 0) {
        const int written = WideCharToMultiByte(
            CP_UTF8, 0u, wloc, -1, prefs.locale, static_cast<int>(sizeof(prefs.locale)), nullptr, nullptr);
        if (written <= 0) {
            prefs.locale[0] = '\0';
        } else {
            prefs.locale[sizeof(prefs.locale) - 1u] = '\0';
        }
    }
#else
    prefs.reduced_motion = (c != nullptr) ? c->reduced_motion : false;
    const char* env_loc = SDL_getenv("LC_ALL");
    if (!env_loc || env_loc[0] == '\0') env_loc = SDL_getenv("LANG");
    if (env_loc && env_loc[0] != '\0' && std::strcmp(env_loc, "C") != 0 && std::strcmp(env_loc, "POSIX") != 0) {
        uint32_t j = 0u;
        while (env_loc[j] != '\0' && env_loc[j] != '.' && env_loc[j] != '@' &&
               j < static_cast<uint32_t>(sizeof(prefs.locale) - 1u))
        {
            prefs.locale[j] = (env_loc[j] == '_') ? '-' : env_loc[j];
            ++j;
        }
        prefs.locale[j] = '\0';
    }
#endif
    if (prefs.locale[0] == '\0') {
        std::memcpy(prefs.locale, "en-US", 6u);
    }
    prefs.layout_direction = locale_is_rtl(prefs.locale) ? LayoutDirection::Rtl : LayoutDirection::Ltr;
    return prefs;
}
namespace {
void sync_system_preferences(Context* c) noexcept {
    const SystemPreferences prefs = system_preferences(c);
    const float raw_scale = std::isfinite(prefs.text_scale) ? prefs.text_scale : 1.0f;
    const float next_scale = c->auto_sys_text_scale ? clamp_range(raw_scale, 0.75f, 2.5f) : 1.0f;
    const LayoutDirection next_dir = c->auto_sys_rtl ? prefs.layout_direction : LayoutDirection::Ltr;
    const bool layout_changed =
        (std::fabs(c->effective_text_scale - next_scale) > 1e-4f) ||
        (c->effective_layout_direction != next_dir);
    c->effective_text_scale = next_scale;
    c->effective_layout_direction = next_dir;
    if (layout_changed) {
        invalidate_cached_subtree(0u, c);
        request_redraw(c);
    }
    if (c->auto_sys_theme) {
        if (prefs.high_contrast) {
            set_theme(Theme::high_contrast(), c);
        } else if (prefs.theme == SystemTheme::Dark) {
            set_theme(Theme::dark(), c);
        } else if (prefs.theme == SystemTheme::Light) {
            set_theme(Theme::light(), c);
        }
    }
}
} // namespace

void apply_system_preferences(Context* ctx, bool auto_theme, bool auto_text_scale, bool auto_rtl) noexcept {
    Context* c = ctx ? ctx : current_context();
    if (!c) return;
    c->auto_sys_theme = auto_theme;
    c->auto_sys_text_scale = auto_text_scale;
    c->auto_sys_rtl = auto_rtl;
    sync_system_preferences(c);
}

float active_text_scale(const Context* ctx) noexcept {
    const Context* c = ctx ? ctx : current_context();
    return c ? c->effective_text_scale : 1.0f;
}

LayoutDirection active_layout_direction(const Context* ctx) noexcept {
    const Context* c = ctx ? ctx : current_context();
    return c ? c->effective_layout_direction : LayoutDirection::Ltr;
}

void set_system_preferences_override(Context* ctx, const SystemPreferences* override_prefs) noexcept {
    Context* c = ctx ? ctx : current_context();
    if (!c) return;
    if (!override_prefs) {
        c->has_sys_prefs_override = false;
        c->sys_prefs_override = {};
    } else {
        SystemPreferences p = *override_prefs;
        p.locale[sizeof(p.locale) - 1u] = '\0';
        if (!std::isfinite(p.text_scale) || p.text_scale <= 0.0f) {
            p.text_scale = 1.0f;
        }
        if (p.layout_direction == LayoutDirection::Ltr && locale_is_rtl(p.locale)) {
            p.layout_direction = LayoutDirection::Rtl;
        }
        c->sys_prefs_override = p;
        c->has_sys_prefs_override = true;
    }
    if (c->auto_sys_theme || c->auto_sys_text_scale || c->auto_sys_rtl) {
        sync_system_preferences(c);
    }
    request_redraw(c);
}
bool needs_window_redraw(const Context* ctx, SDL_Window* window) noexcept {
    const WindowUiState* ui = ctx ? window_ui(ctx, window) : nullptr;
    return ui && (ui->redraw_budget || ui->active_anim_count || shader_surface_timeout(ctx, window, false, -1) == 0);
}
void request_window_redraw(Context* ctx, SDL_Window* window) noexcept {
    WindowUiState* ui = ctx ? window_ui(ctx, window) : nullptr;
    if (ui) ui->redraw_budget = 2;
}
void process_event(Context* ctx, const SDL_Event& event) noexcept {
    if (!ctx) return;
    if (event.type == SDL_EVENT_SYSTEM_THEME_CHANGED ||
        event.type == SDL_EVENT_LOCALE_CHANGED ||
        event.type == ctx->wake_event_type)
    {
        if ((event.type == SDL_EVENT_SYSTEM_THEME_CHANGED || event.type == SDL_EVENT_LOCALE_CHANGED) &&
            (ctx->auto_sys_theme || ctx->auto_sys_text_scale || ctx->auto_sys_rtl))
        {
            sync_system_preferences(ctx);
        }
        if (ctx->headless || ctx->claimed_window_count == 0u) {
            if (WindowUiState* hui = ensure_headless_ui(ctx)) hui->redraw_budget = 2;
        }
        for (uint32_t i = 0; i < ctx->claimed_window_cap; ++i)
            if (ctx->claimed_windows[i]) ctx->claimed_windows[i]->ui.redraw_budget = 2;
        return;
    }
    SDL_WindowID id = 0;
    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION: id = event.motion.windowID; break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: id = event.button.windowID; break;
        case SDL_EVENT_MOUSE_WHEEL: id = event.wheel.windowID; break;
        case SDL_EVENT_PEN_PROXIMITY_IN: case SDL_EVENT_PEN_PROXIMITY_OUT: id = event.pproximity.windowID; break;
        case SDL_EVENT_PEN_DOWN: case SDL_EVENT_PEN_UP: id = event.ptouch.windowID; break;
        case SDL_EVENT_PEN_MOTION: id = event.pmotion.windowID; break;
        case SDL_EVENT_PEN_BUTTON_DOWN: case SDL_EVENT_PEN_BUTTON_UP: id = event.pbutton.windowID; break;
        case SDL_EVENT_PEN_AXIS: id = event.paxis.windowID; break;
        case SDL_EVENT_FINGER_DOWN: case SDL_EVENT_FINGER_UP: case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_CANCELED: id = event.tfinger.windowID; break;
        case SDL_EVENT_PINCH_BEGIN: case SDL_EVENT_PINCH_UPDATE: case SDL_EVENT_PINCH_END: id = event.pinch.windowID; break;
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
    if (event.type == SDL_EVENT_WINDOW_DESTROYED && !ctx->in_frame) {
        if (window) release_window(ctx, window);
        return;
    }
    WindowUiState* ui = window_ui(ctx, window);
    if (!ui) return;
    WindowUiState* saved = ctx->active_ui;
    SDL_Window* saved_window = ctx->active_window;
    ctx->active_ui = ui; ctx->active_window = window;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_PEN_DOWN ||
        event.type == SDL_EVENT_FINGER_DOWN)
    {
        ui->keyboard_focus_active = false;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && (event.key.key == SDLK_TAB || event.key.key == SDLK_LEFT ||
        event.key.key == SDLK_RIGHT || event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)) ui->keyboard_focus_active = true;
    process_window_event(ctx, event);
    ctx->active_ui = saved; ctx->active_window = saved_window;
}
void begin_frame(Context* ctx, const FrameDesc& desc) noexcept {
    if (!ctx || ctx->in_frame) return;
    ctx->active_window = ctx->headless ? nullptr : ctx->window;
    ctx->active_ui = ctx->headless ? ensure_headless_ui(ctx) : window_ui(ctx, nullptr);
    if (!ctx->active_ui) return;
    begin_bound_frame(ctx, desc);
    if (ctx->in_frame && ctx->automation_recording) {
        const WindowUiState* ui = ctx->active_ui;
        const uint32_t key_code = (desc.key_pressed_code != 0u)
            ? desc.key_pressed_code
            : ((ui->frame_key_count > 0u) ? ui->frame_keys[0].key : 0u);
        const RecordedFrameInput rec = {
            .dt_seconds = desc.dt_seconds,
            .pointer_pos = ui->pointer_pos,
            .pointer_down = ui->prev_pointer_down,
            .right_pointer_down = ui->prev_right_down,
            .scroll_delta = ui->frame_scroll_delta,
            .key_mods = ui->frame_key_mods,
            .key_pressed_code = key_code,
        };
        automation_record_frame_input(ctx, rec);
    }
}
void begin_window_frame(Context* ctx, SDL_Window* window, float dt) noexcept {
    begin_window_frame(ctx, window, WindowFrameDesc{.dt = dt});
}
void begin_window_frame(Context* ctx, SDL_Window* window, const WindowFrameDesc& desc) noexcept {
    if (!ctx || !window || ctx->in_frame || !claim_window_swapchain(ctx, window)) return;
    ctx->active_window = window; ctx->active_ui = window_ui(ctx, window);
    ctx->active_ui->min_dpi_scale = (desc.min_scale > 0.0f && std::isfinite(desc.min_scale)) ? desc.min_scale : 0.0f;
    sync_desktop_window(window, ctx);
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    float scale = SDL_GetWindowDisplayScale(window);
    if (scale <= 0) scale = 1;
    if (scale < ctx->active_ui->min_dpi_scale) scale = ctx->active_ui->min_dpi_scale;
    begin_bound_frame(ctx, FrameDesc{.viewport_w = static_cast<uint32_t>(w > 0 ? w : 1),
        .viewport_h = static_cast<uint32_t>(h > 0 ? h : 1), .dpi_scale = scale, .dt_seconds = desc.dt, .pixel_snap = desc.pixel_snap,.bloom_intensity=desc.bloom_intensity,.bloom_threshold=desc.bloom_threshold});
    if (ctx->in_frame && ctx->automation_recording) {
        const WindowUiState* ui = ctx->active_ui;
        const RecordedFrameInput rec = {
            .dt_seconds = desc.dt,
            .pointer_pos = ui->pointer_pos,
            .pointer_down = ui->prev_pointer_down,
            .right_pointer_down = ui->prev_right_down,
            .scroll_delta = ui->frame_scroll_delta,
            .key_mods = ui->frame_key_mods,
            .key_pressed_code = (ui->frame_key_count > 0u) ? ui->frame_keys[0].key : 0u,
        };
        automation_record_frame_input(ctx, rec);
    }
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
