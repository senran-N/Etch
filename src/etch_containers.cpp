// Splits, popovers, docking, tables, trees, menus & selection.
// Compiled via src/etch_unity.cpp.

namespace etch {

SplitResult begin_split(const SplitDesc& desc) noexcept {
    Context* ctx = current_context();
    const float fallback_ratio = clamp_range(desc.default_ratio, 0.02f, 0.98f);
    if (!ctx || !ctx->in_frame || desc.id == 0u || ctx->active_ui->split_depth >= kMaxSplitDepth ||
        !std::isfinite(desc.default_ratio) || !std::isfinite(desc.divider_size_px) || !std::isfinite(desc.divider_hit_px) ||
        !std::isfinite(desc.keyboard_step_px) || desc.keyboard_step_px<0 || !std::isfinite(desc.min_first_px) || !std::isfinite(desc.min_second_px) || desc.min_first_px<0 || desc.min_second_px<0) {
        return SplitResult{ .ratio = fallback_ratio, .dragging = false };
    }

    const uint32_t div_id = id_index(desc.id, kSplitDividerTag);
    state_map_get_or_insert(&ctx->active_ui->states, desc.id, ctx->active_ui->frame_index);
    WidgetState* ws_div = state_map_get_or_insert(&ctx->active_ui->states, div_id, ctx->active_ui->frame_index);
    WidgetState* ws_split = state_map_find(&ctx->active_ui->states, desc.id);
    if (!ws_split || !ws_div) {
        return SplitResult{ .ratio = fallback_ratio, .dragging = false };
    }

    ws_split->is_user_element = true;
    ws_split->owner_canvas_id = (ctx->active_ui->canvas_depth > 0u)
        ? ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth - 1u].id
        : 0u;
    if (!ws_split->split_initialized) {
        ws_split->split_ratio = fallback_ratio;
        ws_split->split_initialized = true;
    }

    const float div_sz = (desc.divider_size_px > 1.0f) ? desc.divider_size_px : 1.0f;
    const float total_span = (desc.axis == Axis::Row) ? ws_split->last_box_logical.w : ws_split->last_box_logical.h;
    const float avail = total_span - div_sz;
    const bool dragging_div = (ctx->active_ui->active_id == div_id);
    const bool hovered_div = (ctx->active_ui->active_id == 0u) && Clay_PointerOver(make_clay_id(div_id));

    if (ws_div->pressed_this_frame) {
        if (ctx->active_ui->last_mouse_clicks >= 2u) {
            ws_split->split_ratio = fallback_ratio;
            ws_split->split_grab_offset = 0.0f;
            request_redraw(ctx);
        } else if (avail > 1.0f) {
            const float origin = (desc.axis == Axis::Row) ? ws_split->last_box_logical.x : ws_split->last_box_logical.y;
            const float ptr_axis = (desc.axis == Axis::Row) ? ctx->active_ui->pointer_pos.x : ctx->active_ui->pointer_pos.y;
            const float div_start = origin + avail * ws_split->split_ratio;
            ws_split->split_grab_offset = ptr_axis - div_start;
        }
    } else if (dragging_div && ctx->active_ui->prev_pointer_down && avail > 1.0f) {
        const float origin = (desc.axis == Axis::Row) ? ws_split->last_box_logical.x : ws_split->last_box_logical.y;
        const float ptr_axis = (desc.axis == Axis::Row) ? ctx->active_ui->pointer_pos.x : ctx->active_ui->pointer_pos.y;
        const float new_first = (ptr_axis - ws_split->split_grab_offset) - origin;
        ws_split->split_ratio = clamp_range(new_first / avail, 0.0f, 1.0f);
        request_redraw(ctx);
    }

    PendingA11yAction action={};
    while(a11y_take_any(div_id,140u,&action)) {
        const float step=avail>1 ? desc.keyboard_step_px/avail : .01f;
        if(action.action==A11yAction::SetValue) ws_split->split_ratio=action.number;
        else ws_split->split_ratio+=action.action==A11yAction::Increment ? step : -step;
        request_redraw(ctx);
    }

    if(focused(div_id) && avail>1) {
        float delta=0;
        if(key_pressed(desc.axis==Axis::Row ? SDLK_LEFT : SDLK_UP,true)) delta=-desc.keyboard_step_px;
        if(key_pressed(desc.axis==Axis::Row ? SDLK_RIGHT : SDLK_DOWN,true)) delta=desc.keyboard_step_px;
        if(delta!=0) {ws_split->split_ratio+=delta/avail;request_redraw(ctx);}
        if(key_pressed(SDLK_HOME)) ws_split->split_ratio=0;
        if(key_pressed(SDLK_END)) ws_split->split_ratio=1;
    }

    if (avail > 1.0f) {
        float min_r = desc.min_first_px / avail;
        float max_r = 1.0f - (desc.min_second_px / avail);
        if (min_r > max_r) {
            const float mid = 0.5f * (min_r + max_r);
            min_r = mid;
            max_r = mid;
        }
        ws_split->split_ratio = clamp_range(
            ws_split->split_ratio,
            clamp_range(min_r, 0.0f, 1.0f),
            clamp_range(max_r, 0.0f, 1.0f));
    } else {
        ws_split->split_ratio = clamp_range(ws_split->split_ratio, 0.02f, 0.98f);
    }

    if (dragging_div || hovered_div) {
        ctx->active_ui->desired_cursor = (desc.axis == Axis::Row) ? Cursor::ResizeEW : Cursor::ResizeNS;
    }

    const Theme& thm = active_theme(ctx);
    const float ratio = ws_split->split_ratio;
    ctx->active_ui->split_stack[ctx->active_ui->split_depth++] = SplitStackEntry{
        .id = desc.id,
        .axis = desc.axis,
        .ratio = ratio,
        .divider_size_px = div_sz,
        .divider_hit_px = std::fmax(div_sz,desc.divider_hit_px),
        .min_first_px = desc.min_first_px,
        .min_second_px = desc.min_second_px,
        .divider_color = resolve_theme_color(desc.divider_color, thm.border_subtle),
        .divider_hover_color = resolve_theme_color(desc.divider_hover_color, thm.divider_hover),
        .divider_active_color = resolve_theme_color(desc.divider_active_color, thm.accent),
        .grip_hover_color = thm.divider_thumb_hover,
        .grip_idle_color = thm.scrollbar_thumb,
        .in_second = false,
    };

    begin_box(BoxDesc{
        .id = desc.id,
        .axis = desc.axis,
        .width = desc.width,
        .height = desc.height,
    });

    Clay__GetOpenLayoutElement()->config.layout.percentAfterFixed = true;
    Clay__GetOpenLayoutElement()->config.layout.percentMinFirst = desc.min_first_px;
    Clay__GetOpenLayoutElement()->config.layout.percentMinSecond = desc.min_second_px;
    const Sizing first_primary = Sizing::percent(ratio);
    begin_box(BoxDesc{
        .axis = Axis::Col,
        .width = (desc.axis == Axis::Row) ? first_primary : Sizing::grow(),
        .height = (desc.axis == Axis::Col) ? first_primary : Sizing::grow(),
        .clip_children = true,
    });
    begin_box(BoxDesc{
        .axis = Axis::Col,
        .width = Sizing::percent(1.0f),
        .height = Sizing::percent(1.0f),
    });

    return SplitResult{ .ratio = ratio, .dragging = dragging_div };
}

void split_second() noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || ctx->active_ui->split_depth == 0u) {
        return;
    }
    SplitStackEntry& se = ctx->active_ui->split_stack[ctx->active_ui->split_depth - 1u];
    if (se.in_second) {
        return;
    }
    end_box();
    end_box();

    const uint32_t div_id = id_index(se.id, kSplitDividerTag);
    const bool dragging_div = (ctx->active_ui->active_id == div_id);
    const bool hovered_div = (ctx->active_ui->active_id == 0u) && Clay_PointerOver(make_clay_id(div_id));
    const Color div_col = dragging_div
        ? se.divider_active_color
        : (hovered_div ? se.divider_hover_color : se.divider_color);
    const Color grip_col = dragging_div
        ? Color::white()
        : (hovered_div ? se.grip_hover_color : se.grip_idle_color);

    begin_box(BoxDesc{
        .axis = se.axis,
        .width = (se.axis == Axis::Row) ? Sizing::fixed(se.divider_size_px) : Sizing::grow(),
        .height = (se.axis == Axis::Col) ? Sizing::fixed(se.divider_size_px) : Sizing::grow(),
        .align_x = AlignX::Center,
        .align_y = AlignY::Center,
        .background = div_col,
    });
    box({.id=div_id,.width=se.axis==Axis::Row ? Sizing::fixed(se.divider_hit_px) : Sizing::grow(),
        .height=se.axis==Axis::Col ? Sizing::fixed(se.divider_hit_px) : Sizing::grow(),.focusable=true,
        .attach_to=AttachTo::Parent,.anchor_element=Anchor::Center,.anchor_parent=Anchor::Center,.z_index=2,.clip_to_parent=false});
    a11y_semantic(div_id,A11yRole::Slider,str_lit("Pane divider"),0,141u,se.ratio,0,1);
    box(BoxDesc{
        .width = Sizing::fixed((se.axis == Axis::Row) ? std::fmin(2.0f,se.divider_size_px) : 28.0f),
        .height = Sizing::fixed((se.axis == Axis::Col) ? std::fmin(2.0f,se.divider_size_px) : 28.0f),
        .background = grip_col,
        .radius = Corners::all(1.0f),
    });
    end_box();

    begin_box(BoxDesc{
        .axis = Axis::Col,
        .width = Sizing::grow(),
        .height = Sizing::grow(),
        .clip_children = true,
    });
    begin_box(BoxDesc{
        .axis = Axis::Col,
        .width = Sizing::percent(1.0f),
        .height = Sizing::percent(1.0f),
    });
    se.in_second = true;
}

void end_split() noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || ctx->active_ui->split_depth == 0u) {
        return;
    }
    --ctx->active_ui->split_depth;
    end_box();
    end_box();
    end_box();
}

float split_ratio(uint32_t split_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || split_id == 0u) {
        return 0.5f;
    }
    const WidgetState* ws = state_map_find(&ctx->active_ui->states, split_id);
    return (ws && ws->split_initialized) ? ws->split_ratio : 0.5f;
}

void set_split_ratio(uint32_t split_id, float ratio) noexcept {
    Context* ctx = current_context();
    if (!ctx || split_id == 0u) {
        return;
    }
    if (WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, split_id, ctx->active_ui->frame_index)) {
        ws->is_user_element = true;
        ws->split_initialized = true;
        ws->split_ratio = clamp_range(ratio, 0.02f, 0.98f);
        request_redraw(ctx);
    }
}

namespace {
VarVirtualListCache* find_vlist(WindowUiState* ui, uint32_t id) noexcept {
    for (uint32_t i = 0; i < ui->vlist_count; ++i) if (ui->vlists[i].list_id == id) return &ui->vlists[i];
    return nullptr;
}
void fenwick_add(VarVirtualListCache* c, uint32_t chunk, double delta) noexcept {
    for (uint32_t i = chunk + 1; i <= c->chunk_count; i += i & (0u-i)) c->fenwick[i] += delta;
    c->total_stride_sum += delta;
}
double fenwick_prefix(const VarVirtualListCache* c, uint32_t count) noexcept {
    double v = 0; for (uint32_t i = count; i; i -= i & (0u-i)) v += c->fenwick[i]; return v;
}
void reset_vlist_heights(VarVirtualListCache* c) noexcept {
    for (uint32_t i = 0; i < c->chunk_count; ++i) { std::free(c->chunks[i]); c->chunks[i] = nullptr; }
    std::memset(c->fenwick, 0, (size_t(c->chunk_count)+1)*sizeof(double)); c->total_stride_sum = 0;
    for (uint32_t i = 0; i < c->chunk_count; ++i) {
        uint32_t count = c->item_count - i*kVarListChunkSize; if (count > kVarListChunkSize) count = kVarListChunkSize;
        fenwick_add(c,i,double(count)*(double(c->default_height)+c->gap_px));
    }
}
VarVirtualListCache* prepare_vlist_raw(
    WindowUiState* ui,
    uint32_t list_id,
    uint32_t item_count,
    float h,
    float gap,
    VirtualItemHeightFn measure_fn,
    void* user_data,
    bool uncapped = false,
    EtchCVirtualItemHeightFn c_measure_fn = nullptr) noexcept
{
    auto* c = find_vlist(ui, list_id);
    if (!c) {
        for (uint32_t i = 0; i < ui->vlist_count; ++i) if (ui->vlists[i].list_id == 0) { c = &ui->vlists[i]; break; }
    }
    if (!c) {
        if (ui->vlist_count == ui->vlist_cap) {
            uint32_t cap = ui->vlist_cap ? ui->vlist_cap * 2 : 4;
            void* mem = std::realloc(ui->vlists, size_t(cap) * sizeof(VarVirtualListCache)); if (!mem) return nullptr;
            ui->vlists = static_cast<VarVirtualListCache*>(mem); ui->vlist_cap = cap;
        }
        c = &ui->vlists[ui->vlist_count++]; *c = {};
    }
    if (!uncapped && c->list_id != 0 && c->item_count != item_count && c->default_height == h && c->gap_px == gap && c->measure_fn == measure_fn && c->c_measure_fn == c_measure_fn && c->measure_user_data == user_data && c->uncapped_heights == uncapped) {
        const uint32_t count = item_count / 256 + (item_count % 256 != 0);
        auto** chunks = c->chunks;
        auto* tree = c->fenwick;
        if (count != c->chunk_count) {
            chunks = static_cast<float**>(std::calloc(count ? count : 1, sizeof(float*)));
            tree = static_cast<double*>(std::calloc(size_t(count) + 1, sizeof(double)));
            if (!chunks || !tree) { std::free(chunks); std::free(tree); return nullptr; }
            for (uint32_t i = 0; i < c->chunk_count; ++i) {
                if (i < count) chunks[i] = c->chunks[i]; else std::free(c->chunks[i]);
            }
            std::free(c->chunks); std::free(c->fenwick);
        } else std::memset(tree, 0, (size_t(count) + 1) * sizeof(double));
        const uint32_t old_count = c->item_count;
        c->chunks = chunks; c->fenwick = tree; c->chunk_count = count; c->item_count = item_count; c->total_stride_sum = 0;
        for (uint32_t chunk = 0; chunk < count; ++chunk) {
            const uint32_t end = std::min(item_count, (chunk + 1) * 256);
            double sum = chunks[chunk] ? 0 : double(end - chunk * 256) * (double(h) + gap);
            for (uint32_t row = chunk * 256; chunks[chunk] && row < end; ++row) {
                float height = h;
                if (row >= old_count) {
                    height = measure_fn ? measure_fn(row, user_data) : (c_measure_fn ? c_measure_fn(row, user_data) : h);
                    height = std::isfinite(height) ? clamp_range(height, 1, 16384) : h;
                    chunks[chunk][row % 256] = height;
                } else height = chunks[chunk][row % 256];
                sum += double(height) + gap;
            }
            fenwick_add(c, chunk, sum);
        }
    }
    if (c->list_id == 0 || c->item_count != item_count || c->default_height != h || c->gap_px != gap || c->measure_fn != measure_fn || c->c_measure_fn != c_measure_fn || c->measure_user_data != user_data || c->uncapped_heights != uncapped) {
        free_vlist(c); c->list_id = list_id; c->item_count = item_count; c->chunk_count = item_count / 256 + (item_count % 256 != 0);
        c->default_height = h; c->gap_px = gap; c->measure_fn = measure_fn; c->c_measure_fn = c_measure_fn; c->measure_user_data = user_data; c->uncapped_heights = uncapped;
        c->chunks = static_cast<float**>(std::calloc(c->chunk_count ? c->chunk_count : 1, sizeof(float*)));
        c->fenwick = static_cast<double*>(std::calloc(size_t(c->chunk_count) + 1, sizeof(double)));
        if (!c->chunks || !c->fenwick) { std::free(c->chunks); std::free(c->fenwick); *c = {}; return nullptr; }
        reset_vlist_heights(c);
    }
    if (c->dirty) {
        if (c->measure_fn || c->c_measure_fn) {
            for (uint32_t chunk = 0; chunk < c->chunk_count; ++chunk) {
                if (!c->chunks[chunk]) continue;
                const uint32_t end = std::min(c->item_count, (chunk + 1) * 256);
                for (uint32_t row = chunk * 256; row < end; ++row) {
                    float measured = c->measure_fn ? c->measure_fn(row, c->measure_user_data) : c->c_measure_fn(row, c->measure_user_data);
                    measured = std::isfinite(measured) ? (c->uncapped_heights ? std::fmax(measured, 1.f) : clamp_range(measured, 1, 16384)) : c->default_height;
                    const float old = c->chunks[chunk][row % 256];
                    c->chunks[chunk][row % 256] = measured;
                    fenwick_add(c, chunk, double(measured) - old);
                }
            }
        } else reset_vlist_heights(c);
        c->dirty = false;
    }
    return c;
}
VarVirtualListCache* prepare_vlist(
    WindowUiState* ui,
    const VirtualListDesc& d,
    float h,
    float gap,
    EtchCVirtualItemHeightFn c_measure_fn = nullptr) noexcept
{
    return prepare_vlist_raw(ui, d.id, d.item_count, h, gap, d.measure_height_fn, d.measure_user_data, false, c_measure_fn);
}
bool vlist_ensure_chunk(VarVirtualListCache* c, uint32_t chunk) noexcept {
    if (c->chunks[chunk]) return true;
    auto* heights = static_cast<float*>(std::malloc(256*sizeof(float))); if (!heights) return false;
    uint32_t count = c->item_count - chunk*256; if (count > 256) count = 256;
    double delta = 0;
    for (uint32_t i = 0; i < count; ++i) {
        float h = c->measure_fn
            ? c->measure_fn(chunk*256+i,c->measure_user_data)
            : (c->c_measure_fn ? c->c_measure_fn(chunk*256+i,c->measure_user_data) : c->default_height);
        h = std::isfinite(h) ? (c->uncapped_heights ? std::fmax(h, 1.0f) : clamp_range(h, 1.0f, 16384.0f)) : c->default_height;
        heights[i] = h; delta += double(h)-c->default_height;
    }
    c->chunks[chunk] = heights; fenwick_add(c,chunk,delta); return true;
}
float vlist_height(VarVirtualListCache* c, uint32_t row) noexcept {
    return vlist_ensure_chunk(c,row/256) ? c->chunks[row/256][row%256] : c->default_height;
}
// Read the current estimates without materializing any sparse chunks.
double vlist_current_row_top(const VarVirtualListCache* c, uint32_t row) noexcept {
    if (row >= c->item_count) return c->total_stride_sum;
    const uint32_t chunk = row / 256;
    double y = fenwick_prefix(c, chunk);
    for (uint32_t i = chunk * 256; i < row; ++i)
        y += double(c->chunks[chunk] ? c->chunks[chunk][i % 256] : c->default_height) + c->gap_px;
    return y;
}
double vlist_row_top(VarVirtualListCache* c, uint32_t row) noexcept {
    if (row == c->item_count) return c->total_stride_sum;
    vlist_height(c,row);
    double y = fenwick_prefix(c,row/256);
    for (uint32_t i = row/256*256; i < row; ++i) y += double(vlist_height(c,i))+c->gap_px;
    return y;
}
uint32_t vlist_find_first_at_y(VarVirtualListCache* c, double y) noexcept {
    uint32_t chunk = 0;
    double prefix = 0;
    for (;;) {
        chunk = 0; prefix = 0; uint32_t bit = 1;
        while (bit <= c->chunk_count/2) bit *= 2;
        for (; bit; bit >>= 1) { uint32_t next = chunk+bit; if (next <= c->chunk_count && prefix+c->fenwick[next] <= y) { chunk = next; prefix += c->fenwick[next]; } }
        if (chunk == c->chunk_count) return c->item_count-1;
        if (c->chunks[chunk] || !vlist_ensure_chunk(c,chunk)) break;
    }
    uint32_t row = chunk*256, end = row+256; if (end > c->item_count) end = c->item_count;
    vlist_height(c,row);
    while (row+1 < end && prefix+double(vlist_height(c,row))+c->gap_px <= y) { prefix += double(vlist_height(c,row))+c->gap_px; ++row; }
    return row;
}
void update_vlist_height(Context* ctx, VarVirtualListCache* c, uint32_t row, float h) noexcept {
    auto* ws = state_map_find(&ctx->active_ui->states,c->list_id);
    const double anchor_before = (ws && c->last_anchor_row < c->item_count) ? vlist_current_row_top(c, c->last_anchor_row) : 0.0;
    const double previous_total = c->total_stride_sum;
    float old = vlist_height(c,row); if (!c->chunks[row/256]) return;
    const double measured_delta = c->total_stride_sum-previous_total;
    double delta = double(h)-old; c->chunks[row/256][row%256] = h; fenwick_add(c,row/256,delta);
    if (ws) {
        ws->vscroll_total_h += measured_delta+delta; ws->vscroll_max_y = ws->vscroll_total_h > ws->viewport_dim.y ? ws->vscroll_total_h-ws->viewport_dim.y : 0;
        if (row < c->last_anchor_row && ws->vscroll_pos_y > 0.0 && c->last_anchor_row < c->item_count)
            ws->vscroll_pos_y = clamp_double(ws->vscroll_pos_y + (vlist_current_row_top(c, c->last_anchor_row) - anchor_before), 0.0, ws->vscroll_max_y);
    }
}
} // namespace

// Shared sparse Fenwick list helpers used by both virtual lists and wrapped code editors.
VarVirtualListCache* editor_vlist_find(WindowUiState* ui, uint32_t id) noexcept { return find_vlist(ui, id); }
VarVirtualListCache* editor_vlist_prepare(WindowUiState* ui, uint32_t id, uint32_t count, float h, VirtualItemHeightFn fn, void* ud) noexcept {
    if (auto* existing = find_vlist(ui, id)) {
        existing->measure_user_data = ud;
    }
    return prepare_vlist_raw(ui, id, count, h, 0.0f, fn, ud, true);
}
void editor_vlist_free(VarVirtualListCache* c) noexcept { free_vlist(c); }
void editor_vlist_reset(VarVirtualListCache* c) noexcept { if (c) reset_vlist_heights(c); }
float editor_vlist_height(VarVirtualListCache* c, uint32_t row) noexcept { return vlist_height(c, row); }
double editor_vlist_row_top(VarVirtualListCache* c, uint32_t row) noexcept { return vlist_row_top(c, row); }
double editor_vlist_current_row_top(const VarVirtualListCache* c, uint32_t row) noexcept { return vlist_current_row_top(c, row); }
float editor_vlist_current_height(const VarVirtualListCache* c, uint32_t row) noexcept {
    if (!c || row >= c->item_count) return c ? c->default_height : 20.0f;
    const uint32_t chunk = row / kVarListChunkSize;
    return (c->chunks && c->chunks[chunk]) ? c->chunks[chunk][row % kVarListChunkSize] : c->default_height;
}
uint32_t editor_vlist_find_first_at_y(VarVirtualListCache* c, double y) noexcept { return vlist_find_first_at_y(c, y); }
void vlist_set_row_height_direct(VarVirtualListCache* c, uint32_t row, float h) noexcept {
    if (!c || row >= c->item_count || !c->chunks) return;
    const float clamped = std::isfinite(h) ? (c->uncapped_heights ? std::fmax(h, 1.0f) : clamp_range(h, 1.0f, 16384.0f)) : c->default_height;
    if (!c->chunks[row / kVarListChunkSize] && !vlist_ensure_chunk(c, row / kVarListChunkSize)) return;
    const float old = c->chunks[row / kVarListChunkSize][row % kVarListChunkSize];
    if (std::fabs(clamped - old) <= 0.01f) return;
    c->chunks[row / kVarListChunkSize][row % kVarListChunkSize] = clamped;
    fenwick_add(c, row / kVarListChunkSize, double(clamped) - double(old));
}

void virtual_list_set_item_height(uint32_t id, uint32_t row, float height) noexcept {
    Context* ctx = current_context(); if (!ctx || !id || !std::isfinite(height) || height < 1) return;
    auto* c = find_vlist(ctx->active_ui,id); if (!c || row >= c->item_count) return;
    if (c->dirty) {
        const double previous_total = c->total_stride_sum;
        reset_vlist_heights(c); c->dirty = false;
        if (auto* ws = state_map_find(&ctx->active_ui->states,id)) {
            ws->vscroll_total_h += c->total_stride_sum-previous_total;
            ws->vscroll_max_y = std::fmax(ws->vscroll_total_h-ws->viewport_dim.y,0.0);
        }
    }
    update_vlist_height(ctx,c,row,clamp_range(height,1,16384)); request_redraw(ctx);
}
void virtual_list_invalidate_heights(uint32_t id) noexcept {
    Context* ctx = current_context(); if (!ctx || !id) return;
    auto* c = find_vlist(ctx->active_ui,id); if (!c) return;
    if (!c->measure_fn && !c->c_measure_fn) {
        const double previous_total = c->total_stride_sum;
        reset_vlist_heights(c);
        if (auto* ws = state_map_find(&ctx->active_ui->states,id)) {
            ws->vscroll_total_h += c->total_stride_sum-previous_total;
            ws->vscroll_max_y = std::fmax(ws->vscroll_total_h-ws->viewport_dim.y,0.0);
        }
        request_redraw(ctx); return;
    }
    c->dirty = true; request_redraw(ctx);
}

VirtualListSlice begin_virtual_list_with_c_measure(const VirtualListDesc& desc, EtchCVirtualItemHeightFn c_measure_fn) noexcept;

VirtualListSlice begin_virtual_list(const VirtualListDesc& desc) noexcept {
    return begin_virtual_list_with_c_measure(desc, nullptr);
}

VirtualListSlice begin_virtual_list_with_c_measure(const VirtualListDesc& desc, EtchCVirtualItemHeightFn c_measure_fn) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || desc.id == 0u) {
        return VirtualListSlice{};
    }
    WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, desc.id, ctx->active_ui->frame_index);
    if (!ws) {
        return VirtualListSlice{};
    }

    const float item_h = std::isfinite(desc.item_height_px) ? clamp_range(desc.item_height_px,1,16384) : 28;
    const uint16_t gap_u16 = to_u16_round((std::isfinite(desc.row_gap_px) && desc.row_gap_px > 0.0f) ? desc.row_gap_px : 0.0f);
    const float gap_px = static_cast<float>(gap_u16);
    const double stride = static_cast<double>(item_h) + static_cast<double>(gap_px);

    auto* cache = (desc.variable_height || desc.measure_height_fn || c_measure_fn) ? prepare_vlist(ctx->active_ui,desc,item_h,gap_px,c_measure_fn) : nullptr;
    if (!desc.variable_height && !desc.measure_height_fn && !c_measure_fn) { auto* stale = find_vlist(ctx->active_ui,desc.id); if (stale) free_vlist(stale); }

    ws->is_user_element = true;
    ws->is_scroll_x = false;
    ws->is_scroll_y = false;
    ws->is_virtual_list = true;
    ws->show_scrollbar = desc.show_scrollbar;
    ws->vlist_stride = static_cast<float>(stride);
    ws->owner_canvas_id = (ctx->active_ui->canvas_depth > 0u)
        ? ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth - 1u].id
        : 0u;

    const double pad_v = static_cast<double>(desc.padding.top) + static_cast<double>(desc.padding.bottom);
    const double items_h = cache ? cache->total_stride_sum - (desc.item_count ? gap_px : 0) : (desc.item_count > 0u)
        ? (static_cast<double>(desc.item_count) * static_cast<double>(item_h) +
           static_cast<double>(desc.item_count - 1u) * static_cast<double>(gap_px))
        : 0.0;
    const double total_h = items_h + pad_v;

    double view_h = static_cast<double>(ws->last_box_logical.h);
    if (view_h <= 0.5) {
        if (desc.height.kind == SizingKind::Fixed && desc.height.value > 0.0f) {
            view_h = static_cast<double>(desc.height.value);
        } else {
            view_h = static_cast<double>(ctx->active_ui->viewport_h) / static_cast<double>(ctx->active_ui->dpi_scale);
        }
    }
    const double max_scroll = (total_h > view_h) ? (total_h - view_h) : 0.0;
    ws->vscroll_total_h = total_h;
    ws->vscroll_max_y = max_scroll;
    PendingA11yAction lv_cmd = {};
    while(a11y_take_any(desc.id,12u,&lv_cmd)) {
        ws->vscroll_pos_y += (lv_cmd.action==A11yAction::Increment) ? 40.0 : -40.0;
        request_redraw(ctx);
    }
    ws->vscroll_pos_y = clamp_double(ws->vscroll_pos_y, 0.0, max_scroll);
    ws->viewport_dim.y = static_cast<float>(view_h);

    const double content_scroll_y = (ws->vscroll_pos_y > static_cast<double>(desc.padding.top))
        ? (ws->vscroll_pos_y - static_cast<double>(desc.padding.top))
        : 0.0;
    const double inner_view_h = (view_h > pad_v) ? (view_h - pad_v) : view_h;

    uint32_t first_idx = 0u;
    uint32_t end_idx = 0u;
    if (desc.item_count > 0u && stride > 0.0) {
        const double raw_first = std::floor(content_scroll_y / stride);
        const double raw_last = std::floor((content_scroll_y + inner_view_h) / stride) + 1.0;
        const int64_t os = static_cast<int64_t>(desc.overscan_rows);
        int64_t fi = static_cast<int64_t>(raw_first) - os;
        int64_t ei = static_cast<int64_t>(raw_last) + os;
        if (fi < 0) fi = 0;
        if (ei > static_cast<int64_t>(desc.item_count)) ei = static_cast<int64_t>(desc.item_count);
        if (fi > ei) fi = ei;
        first_idx = static_cast<uint32_t>(fi);
        end_idx = static_cast<uint32_t>(ei);
    }

    double first_row_top = static_cast<double>(first_idx) * stride;
    if (cache && desc.item_count) {
        double adj_content_scroll_y = content_scroll_y;
        for (uint32_t pass = 0; pass < 2; ++pass) {
            uint32_t anchor = vlist_find_first_at_y(cache, adj_content_scroll_y);
            cache->last_anchor_row = anchor;
            const double anchor_top_before = vlist_current_row_top(cache, anchor);
            first_idx = anchor > desc.overscan_rows ? anchor - desc.overscan_rows : 0;
            for (uint32_t chunk = first_idx / 256; chunk <= anchor / 256; ++chunk)
                vlist_ensure_chunk(cache, chunk);
            const double shift = (adj_content_scroll_y > 0.0) ? (vlist_current_row_top(cache, anchor) - anchor_top_before) : 0.0;
            if (shift != 0.0) {
                adj_content_scroll_y = (adj_content_scroll_y + shift > 0.0) ? (adj_content_scroll_y + shift) : 0.0;
                ws->vscroll_pos_y = (ws->vscroll_pos_y + shift > 0.0) ? (ws->vscroll_pos_y + shift) : 0.0;
            }
            first_row_top = vlist_row_top(cache, first_idx);
            double y = first_row_top;
            end_idx = first_idx;
            while (end_idx < desc.item_count && y < adj_content_scroll_y + inner_view_h) {
                y += double(vlist_height(cache, end_idx)) + gap_px;
                ++end_idx;
            }
            uint32_t extra = desc.item_count - end_idx;
            if (extra > desc.overscan_rows) extra = desc.overscan_rows;
            end_idx += extra;
            ws->vscroll_total_h = cache->total_stride_sum - (desc.item_count ? gap_px : 0) + pad_v;
            ws->vscroll_max_y = ws->vscroll_total_h > view_h ? ws->vscroll_total_h - view_h : 0;
            const double before_clamp = ws->vscroll_pos_y;
            ws->vscroll_pos_y = clamp_double(ws->vscroll_pos_y, 0.0, ws->vscroll_max_y);
            // Shrinking estimates near the bottom can move the viewport before this slice.
            if (ws->vscroll_pos_y == before_clamp || ws->vscroll_pos_y - desc.padding.top >= first_row_top) break;
            adj_content_scroll_y = ws->vscroll_pos_y > desc.padding.top ? ws->vscroll_pos_y - desc.padding.top : 0.0;
        }
        auto* rec = ctx->frame_arena.alloc<ActiveVarListRecord>();
        if (rec) { *rec = {desc.id,first_idx,end_idx,ctx->active_ui->active_vlists}; ctx->active_ui->active_vlists = rec; ++ctx->active_ui->active_vlist_count; }
        cache->dirty = false;
    }
    const float child_offset_y = static_cast<float>(first_row_top - ws->vscroll_pos_y);

    const Theme& thm = active_theme(ctx);
    const Color eff_bg = resolve_theme_color(desc.background, thm.bg_surface);
    const Color eff_border = resolve_theme_color(desc.border_color, thm.border_subtle);
    const Corners eff_radius = resolve_theme_corners(desc.radius, Corners::all(thm.radius_panel_px));

    BoxExtra* extra = ctx->frame_arena.alloc<BoxExtra>();
    if (extra) {
        extra->layer = ctx->active_ui->current_layer;
        extra->kind = CustomCmdKind::Box;
        extra->is_virtual_list = true;
        extra->world_scale = 1;
        extra->scrollbar_track = thm.scrollbar_track;
        extra->scrollbar_thumb = thm.scrollbar_thumb;
        extra->scrollbar_thumb_hover = thm.scrollbar_thumb_hover;
        extra->scrollbar_thumb_drag = thm.scrollbar_thumb_drag;
        extra->desc = BoxDesc{
            .id = desc.id,
            .axis = Axis::Col,
            .width = desc.width,
            .height = desc.height,
            .padding = desc.padding,
            .gap = gap_px,
            .background = eff_bg,
            .border_color = eff_border,
            .border_width = desc.border_width,
            .radius = eff_radius,
            .clip_children = true,
            .show_scrollbar = desc.show_scrollbar,
            .scroll_fade_px = std::isfinite(desc.scroll_fade_px) && desc.scroll_fade_px>0 ? desc.scroll_fade_px*current_world_scale(ctx) : 0,
        };
    }

    Clay__OpenElementWithId(make_clay_id(desc.id));

    const bool has_border = (desc.border_width > 0.0f && eff_border.a > 0u);

    Clay_ElementDeclaration decl = {};
    decl.layout.sizing.width = to_clay_sizing(desc.width);
    decl.layout.sizing.height = to_clay_sizing(desc.height);
    decl.layout.padding = Clay_Padding{
        .left = to_u16_round(desc.padding.left),
        .right = to_u16_round(desc.padding.right),
        .top = to_u16_round(desc.padding.top),
        .bottom = to_u16_round(desc.padding.bottom),
    };
    decl.layout.childGap = gap_u16;
    decl.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    decl.cornerRadius = to_clay_corners(eff_radius);
    decl.userData = extra;
    if (extra) {
        decl.custom.customData = extra;
    }
    decl.clip.horizontal = true;
    decl.clip.vertical = true;
    decl.clip.childOffset = Clay_Vector2{ 0.0f, child_offset_y };
    if (has_border || desc.show_scrollbar) {
        decl.border.color = Clay_Color{255.0f, 255.0f, 255.0f, 1.0f};
        decl.border.width = Clay_BorderWidth{1u, 1u, 1u, 1u, 0u};
    }

    Clay__ConfigureOpenElement(decl);

    ctx->a11y_parent=a11y_emit(ctx,desc.id,A11yRole::List,{}, {},0,12u);
    return VirtualListSlice{
        .first_index = first_idx,
        .end_index = end_idx,
        .total_count = desc.item_count,
        .scroll_y = ws->vscroll_pos_y,
        .max_scroll_y = ws->vscroll_max_y,
        .viewport_height = static_cast<float>(view_h),
    };
}

void end_virtual_list() noexcept {
    if (!current_context() || !current_context()->in_frame) {
        return;
    }
    if(current_context()->a11y_parent)current_context()->a11y_parent=current_context()->a11y_parent->parent;
    Clay__CloseElement();
}

double virtual_list_scroll_offset(uint32_t list_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || list_id == 0u) {
        return 0.0;
    }
    const WidgetState* ws = state_map_find(&ctx->active_ui->states, list_id);
    return ws ? ws->vscroll_pos_y : 0.0;
}

void set_virtual_list_scroll_offset(uint32_t list_id, double offset_y, bool stop_momentum) noexcept {
    Context* ctx = current_context();
    if (!ctx || list_id == 0u) {
        return;
    }
    if (WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, list_id, ctx->active_ui->frame_index)) {
        ws->is_user_element = true;
        ws->is_virtual_list = true;
        double clamped = (offset_y > 0.0) ? offset_y : 0.0;
        if (ws->vscroll_max_y > 0.0) {
            clamped = clamp_double(clamped, 0.0, ws->vscroll_max_y);
        }
        ws->vscroll_pos_y = clamped;
        if (stop_momentum) {
            ws->vscroll_vel_y = 0.0;
        }
        request_redraw(ctx);
    }
}

void virtual_list_scroll_to_row(
    uint32_t list_id,
    uint32_t row_index,
    float item_height_px,
    AlignY align) noexcept
{
    Context* ctx = current_context();
    if (!ctx || list_id == 0u) {
        return;
    }
    WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, list_id, ctx->active_ui->frame_index);
    if (!ws) {
        return;
    }
    auto* cache = find_vlist(ctx->active_ui,list_id);
    const double previous_total = cache ? cache->total_stride_sum : 0;
    if (cache && cache->dirty) { reset_vlist_heights(cache); cache->dirty = false; }
    if (cache && cache->item_count == 0) return;
    if (cache && row_index >= cache->item_count) row_index = cache->item_count-1;
    const float item_h = cache ? vlist_height(cache,row_index) : (std::isfinite(item_height_px) && item_height_px > 1 ? item_height_px : 1);
    const double stride = (ws->vlist_stride >= item_h)
        ? static_cast<double>(ws->vlist_stride)
        : static_cast<double>(item_h);
    const double view_h = (ws->viewport_dim.y > 1.0f)
        ? static_cast<double>(ws->viewport_dim.y)
        : (static_cast<double>(ctx->active_ui->viewport_h) / static_cast<double>(ctx->active_ui->dpi_scale));
    double target_y = cache ? vlist_row_top(cache,row_index) : static_cast<double>(row_index) * stride;
    if (cache) {
        ws->vscroll_total_h += cache->total_stride_sum-previous_total;
        ws->vscroll_max_y = ws->vscroll_total_h > view_h ? ws->vscroll_total_h-view_h : 0;
    }
    if (align == AlignY::Center) {
        target_y -= 0.5 * (view_h - static_cast<double>(item_h));
    } else if (align == AlignY::Bottom) {
        target_y -= (view_h - static_cast<double>(item_h));
    }
    if (target_y < 0.0) {
        target_y = 0.0;
    }
    if (ws->vscroll_max_y > 0.0 && target_y > ws->vscroll_max_y) {
        target_y = ws->vscroll_max_y;
    }
    ws->is_user_element = true;
    ws->is_virtual_list = true;
    ws->vscroll_pos_y = target_y;
    ws->vscroll_vel_y = 0.0;
    request_redraw(ctx);
}

CanvasState begin_canvas(const CanvasDesc& desc_in) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || desc_in.id == 0u || ctx->active_ui->canvas_depth >= kMaxCanvasDepth) {
        return CanvasState{};
    }
    const Theme& thm = active_theme(ctx);
    CanvasDesc desc = desc_in;
    desc.background = resolve_theme_color(desc.background, thm.bg_node_canvas);
    desc.minor_grid_color = resolve_theme_color(desc.minor_grid_color, thm.grid_minor);
    desc.major_grid_color = resolve_theme_color(desc.major_grid_color, thm.grid_major);
    desc.border_color = resolve_theme_color(desc.border_color, thm.canvas_border);
    desc.radius = resolve_theme_corners(desc.radius, Corners::all(thm.radius_panel_px));
    desc.marquee_fill = resolve_theme_color(desc.marquee_fill, thm.marquee_fill);
    desc.marquee_border = resolve_theme_color(desc.marquee_border, thm.marquee_border);

    WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, desc.id, ctx->active_ui->frame_index);
    if (!ws) {
        return CanvasState{};
    }

    const float min_z = (desc.min_zoom > 0.02f) ? desc.min_zoom : 0.02f;
    const float max_z = (desc.max_zoom > min_z) ? desc.max_zoom : min_z;
    if (!ws->canvas_initialized) {
        const float def_z = (desc.default_zoom > 1e-4f) ? desc.default_zoom : 1.0f;
        ws->canvas_pan = desc.default_pan;
        ws->canvas_zoom = clamp_range(def_z, min_z, max_z);
        ws->canvas_initialized = true;
    }
    ws->is_user_element = true;
    ws->is_canvas = true;
    ws->canvas_interactive = desc.interactive;
    ws->canvas_marquee_enabled = desc.marquee_select;
    if (!desc.marquee_select) {
        ws->canvas_marquee_active = false;
        ws->canvas_marquee_finished = false;
    }
    ws->canvas_min_zoom = min_z;
    ws->canvas_max_zoom = max_z;
    ws->canvas_zoom = clamp_range(ws->canvas_zoom, min_z, max_z);
    ws->owner_canvas_id = (ctx->active_ui->canvas_depth > 0u)
        ? ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth - 1u].id
        : 0u;

    const bool panning = (ctx->active_ui->pan_canvas_id == desc.id);
    if (panning) {
        ctx->active_ui->desired_cursor = Cursor::Move;
    }

    const CanvasState cs = {
        .pan = ws->canvas_pan,
        .zoom = ws->canvas_zoom,
        .viewport = ws->last_box_logical,
        .panning = panning,
        .marquee_active = ws->canvas_marquee_active,
        .marquee_finished = ws->canvas_marquee_finished,
        .marquee_world = ws->canvas_marquee_world,
    };

    ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth++] = CanvasStackEntry{
        .id = desc.id,
        .desc = desc,
        .state = cs,
        .in_node = false,
    };

    BoxExtra* extra = ctx->frame_arena.alloc<BoxExtra>();
    if (extra) {
        extra->layer = ctx->active_ui->current_layer;
        extra->kind = CustomCmdKind::Box;
        extra->is_canvas = true;
        extra->world_scale = 1;
        extra->desc = BoxDesc{
            .id = desc.id,
            .axis = Axis::Col,
            .width = desc.width,
            .height = desc.height,
            .background = desc.background,
            .radius = desc.radius,
            .clip_children = true,
        };
        if (desc.show_grid) {
            CanvasGridExtra* cg = ctx->frame_arena.alloc<CanvasGridExtra>();
            if (cg) {
                cg->pan = ws->canvas_pan;
                cg->zoom = ws->canvas_zoom;
                cg->grid_step_world = desc.grid_step_world;
                cg->major_subdiv = desc.major_grid_subdiv;
                cg->minor_color = desc.minor_grid_color;
                cg->major_color = desc.major_grid_color;
                extra->canvas_grid = cg;
            }
        }
    }

    Clay__OpenElementWithId(make_clay_id(desc.id));

    Clay_ElementDeclaration decl = {};
    decl.layout.sizing.width = to_clay_sizing(desc.width);
    decl.layout.sizing.height = to_clay_sizing(desc.height);
    decl.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    decl.cornerRadius = to_clay_corners(desc.radius);
    decl.userData = extra;
    if (extra) {
        decl.custom.customData = extra;
    }
    decl.clip.horizontal = true;
    decl.clip.vertical = true;

    Clay__ConfigureOpenElement(decl);
    ctx->a11y_parent=a11y_emit(ctx,desc.id,A11yRole::Canvas);
    return cs;
}

void end_canvas() noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || ctx->active_ui->canvas_depth == 0u) {
        return;
    }
    const CanvasStackEntry ce = ctx->active_ui->canvas_stack[--ctx->active_ui->canvas_depth];
    if (ce.state.marquee_active && ce.state.marquee_world.w > 0.0f && ce.state.marquee_world.h > 0.0f) {
        const Vec2 tl_local = ce.state.world_to_local(Vec2{ ce.state.marquee_world.x, ce.state.marquee_world.y });
        box(BoxDesc{
            .width = Sizing::fixed(ce.state.marquee_world.w * ce.state.zoom),
            .height = Sizing::fixed(ce.state.marquee_world.h * ce.state.zoom),
            .background = ce.desc.marquee_fill,
            .border_color = ce.desc.marquee_border,
            .border_width = 1.0f,
            .radius = Corners::all(2.0f),
            .attach_to = AttachTo::Parent,
            .anchor_element = Anchor::TopLeft,
            .anchor_parent = Anchor::TopLeft,
            .floating_offset = tl_local,
            .z_index = 7,
            .pointer_passthrough = true,
            .clip_to_parent = true,
        });
    }
    if (ce.desc.border_width > 0.0f && ce.desc.border_color.a > 0u) {
        box(BoxDesc{
            .width = Sizing::grow(),
            .height = Sizing::grow(),
            .border_color = ce.desc.border_color,
            .border_width = ce.desc.border_width,
            .radius = ce.desc.radius,
            .attach_to = AttachTo::Parent,
            .anchor_element = Anchor::TopLeft,
            .anchor_parent = Anchor::TopLeft,
            .z_index = 8,
            .pointer_passthrough = true,
            .clip_to_parent = false,
        });
    }
    if(ctx->a11y_parent)ctx->a11y_parent=ctx->a11y_parent->parent;
    Clay__CloseElement();
}

void canvas_wire(const CanvasWireDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || ctx->active_ui->canvas_depth == 0u) {
        return;
    }
    const CanvasStackEntry& ce = ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth - 1u];
    BoxExtra* extra = ctx->frame_arena.alloc<BoxExtra>();
    CanvasWireExtra* cw = ctx->frame_arena.alloc<CanvasWireExtra>();
    if (!extra || !cw) {
        return;
    }
    const Theme& thm = active_theme(ctx);
    cw->canvas_id = ce.id;
    cw->from_world = desc.from_world;
    cw->to_world = desc.to_world;
    cw->color_start = resolve_theme_color(desc.color_start, thm.accent);
    cw->color_end = resolve_theme_color(desc.color_end, thm.accent_secondary);
    cw->glow_color = desc.glow_color;
    cw->thickness_world = desc.thickness_world;
    cw->glow_world = desc.glow_world;
    cw->tangent_bias = desc.tangent_bias;
    extra->layer = ctx->active_ui->current_layer;
    extra->kind = CustomCmdKind::CanvasWire;
    extra->canvas_wire = cw;

    Clay__OpenElement();
    Clay_ElementDeclaration decl = {};
    decl.layout.sizing.width = CLAY_SIZING_FIXED(0.0f);
    decl.layout.sizing.height = CLAY_SIZING_FIXED(0.0f);
    decl.custom.customData = extra;
    decl.userData = extra;
    decl.floating.attachTo = CLAY_ATTACH_TO_PARENT;
    decl.floating.attachPoints = Clay_FloatingAttachPoints{
        .element = CLAY_ATTACH_POINT_CENTER_CENTER,
        .parent = CLAY_ATTACH_POINT_CENTER_CENTER,
    };
    decl.floating.zIndex = desc.z_index;
    decl.floating.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH;
    decl.floating.clipTo = CLAY_CLIP_TO_ATTACHED_PARENT;
    Clay__ConfigureOpenElement(decl);
    Clay__CloseElement();
}

CanvasNodeResult begin_canvas_node(const CanvasNodeDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || ctx->active_ui->canvas_depth == 0u || desc.id == 0u) {
        return CanvasNodeResult{};
    }
    CanvasStackEntry& ce = ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth - 1u];
    WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, desc.id, ctx->active_ui->frame_index);
    if (!ws) {
        return CanvasNodeResult{};
    }

    ws->is_user_element = true;
    ws->is_canvas_node = true;
    ws->owner_canvas_id = ce.id;

    Vec2 node_pos = desc.world_pos ? *desc.world_pos : desc.pos_value;
    const bool hovered_node = Clay_PointerOver(make_clay_id(desc.id));
    const bool can_drag = desc.draggable && ce.desc.interactive;
    const bool dragging = can_drag && (ctx->active_ui->active_id == desc.id) && ctx->active_ui->prev_pointer_down;

    Vec2 drag_delta_world = {};
    if (can_drag && ws->pressed_this_frame) {
        const Vec2 ptr_world = ce.state.screen_to_world(ctx->active_ui->pointer_pos);
        ws->drag_grab = node_pos - ptr_world;
        ctx->active_ui->desired_cursor = Cursor::Move;
    } else if (dragging) {
        const float inv_z = (ce.state.zoom > 1e-4f) ? (1.0f / ce.state.zoom) : 1.0f;
        drag_delta_world = ctx->active_ui->pointer_delta * inv_z;
        const Vec2 ptr_world = ce.state.screen_to_world(ctx->active_ui->pointer_pos);
        node_pos = ptr_world + ws->drag_grab;
        if (desc.world_pos) {
            *desc.world_pos = node_pos;
        }
        ctx->active_ui->desired_cursor = Cursor::Move;
    }

    const float z = ce.state.zoom;
    const Vec2 local_offset = ce.state.world_to_local(node_pos);
    const bool pressed_now = ws->pressed_down;
    const bool clicked_now = ws->clicked_this_frame;
    ce.in_node = false;

    const Theme& thm = active_theme(ctx);
    const Color eff_bg = resolve_theme_color(desc.background, thm.bg_elevated);
    const Color eff_bg_end = resolve_theme_color(desc.background_end, thm.bg_surface);
    const Color eff_border = resolve_theme_color(desc.border_color, thm.border_node);
    const Corners eff_radius = resolve_theme_corners(desc.radius_world, Corners::all(thm.radius_panel_px));
    const Color eff_shadow = resolve_theme_color(desc.shadow_color, thm.shadow);

    begin_box(BoxDesc{
        .id = desc.id,
        .axis = desc.axis,
        .width = scale_sizing(desc.width_world, z),
        .height = scale_sizing(desc.height_world, z),
        .padding = scale_padding(desc.padding_world, z),
        .gap = desc.gap_world * z,
        .background = eff_bg,
        .background_end = eff_bg_end,
        .gradient_dir = desc.gradient_dir,
        .border_color = eff_border,
        .border_width = desc.border_width_world * z,
        .radius = scale_corners_uniform(eff_radius, z),
        .shadow_color = eff_shadow,
        .shadow_blur = desc.shadow_blur_world * z,
        .shadow_offset = Vec2{ desc.shadow_offset_world.x * z, desc.shadow_offset_world.y * z },
        .attach_to = AttachTo::Parent,
        .anchor_element = Anchor::TopLeft,
        .anchor_parent = Anchor::TopLeft,
        .floating_offset = local_offset,
        .z_index = desc.z_index,
        .pointer_passthrough = false,
        .clip_to_parent = true,
    });
    if (auto* extra = static_cast<BoxExtra*>(Clay__GetOpenLayoutElement()->config.userData)) extra->world_scale = z;
    ce.in_node = true;

    return CanvasNodeResult{
        .hovered = hovered_node,
        .pressed = pressed_now,
        .dragging = dragging,
        .clicked = clicked_now,
        .drag_delta_world = drag_delta_world,
    };
}

void end_canvas_node() noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || ctx->active_ui->canvas_depth == 0u) {
        return;
    }
    ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth - 1u].in_node = false;
    end_box();
}

CanvasState canvas_state(uint32_t canvas_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || canvas_id == 0u) {
        return CanvasState{};
    }
    const WidgetState* ws = state_map_find(&ctx->active_ui->states, canvas_id);
    if (!ws || !ws->canvas_initialized) {
        return CanvasState{};
    }
    return CanvasState{
        .pan = ws->canvas_pan,
        .zoom = ws->canvas_zoom,
        .viewport = ws->last_box_logical,
        .panning = (ctx->active_ui->pan_canvas_id == canvas_id),
        .marquee_active = ws->canvas_marquee_active,
        .marquee_finished = ws->canvas_marquee_finished,
        .marquee_world = ws->canvas_marquee_world,
    };
}

void set_canvas_view(uint32_t canvas_id, Vec2 pan, float zoom) noexcept {
    Context* ctx = current_context();
    if (!ctx || canvas_id == 0u) {
        return;
    }
    if (WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, canvas_id, ctx->active_ui->frame_index)) {
        const float min_z = (ws->canvas_min_zoom > 0.02f) ? ws->canvas_min_zoom : 0.02f;
        const float max_z = (ws->canvas_max_zoom > min_z) ? ws->canvas_max_zoom : 16.0f;
        ws->canvas_pan = pan;
        ws->canvas_zoom = clamp_range(zoom, min_z, max_z);
        ws->canvas_initialized = true;
        request_redraw(ctx);
    }
}

void set_debug_hud_open(Context* ctx_arg, bool open) noexcept {
    Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (ctx && ctx->active_ui->debug_hud_open != open) {
        ctx->active_ui->debug_hud_open = open;
        request_redraw(ctx);
    }
}

bool debug_hud_open(const Context* ctx_arg) noexcept {
    const Context* ctx = ctx_arg ? ctx_arg : current_context();
    return ctx != nullptr && ctx->active_ui->debug_hud_open;
}

void set_debug_wireframe(Context* ctx_arg, bool enabled) noexcept {
    Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (ctx && ctx->active_ui->debug_wireframe_open != enabled) {
        ctx->active_ui->debug_wireframe_open = enabled;
        request_redraw(ctx);
    }
}

bool debug_wireframe(const Context* ctx_arg) noexcept {
    const Context* ctx = ctx_arg ? ctx_arg : current_context();
    return ctx != nullptr && ctx->active_ui->debug_wireframe_open;
}

namespace {
constexpr uint32_t kDockSplitTagBase = 0xD100u;
constexpr uint32_t kInternalDockDragKind = 0xD0C4u;
constexpr uint32_t kTableOuterTag = 0x7400u;
constexpr uint32_t kTableHeaderTag = 0x7401u;
constexpr uint32_t kTableColTagBase = 0x7500u;
constexpr uint32_t kTreeChevronTag = 0x7EE0u;
Str dock_tab_title(const DockTabSlot& tab) noexcept { return Str{tab.title_heap ? tab.title_heap : tab.title_inline}; }
bool grow_dock_nodes(DockSpaceSlot* s) noexcept {
    const uint32_t cap = s->node_cap ? s->node_cap * 2u : 32u;
    void* p = std::realloc(s->nodes, size_t(cap) * sizeof(DockNode));
    if (!p) return false;
    s->nodes = static_cast<DockNode*>(p);
    for (uint32_t i = s->node_cap; i < cap; ++i) s->nodes[i] = {};
    s->node_cap = cap; return true;
}
DockSpaceSlot* dock_slot(Context* ctx, uint32_t id) noexcept {
    for (auto& s : std::span(ctx->active_ui->dock_spaces, ctx->active_ui->dock_space_count)) if (s.id == id) return &s;
    return nullptr;
}
DockTabSlot* dock_tab(DockSpaceSlot* s, uint32_t id) noexcept {
    for (auto& t : std::span(s->tabs, s->tab_cap)) if (t.id == id) return &t;
    return nullptr;
}
uint32_t dock_visible(DockSpaceSlot* s, uint32_t leaf) noexcept {
    const uint32_t active = s->nodes[leaf - 1u].active;
    for (auto& t : std::span(s->tabs, s->tab_cap)) if (t.leaf == leaf && t.open && t.id == active) return active;
    for (auto& t : std::span(s->tabs, s->tab_cap)) if (t.leaf == leaf && t.open) return t.id;
    return 0;
}
void dock_reparent(DockSpaceSlot* s, uint32_t from, uint32_t to) noexcept {
    for (auto& t : std::span(s->tabs, s->tab_cap)) if (t.leaf == from) t.leaf = to;
    auto& n = s->nodes[to - 1u];
    if (n.first) { s->nodes[n.first - 1u].parent = to; s->nodes[n.second - 1u].parent = to; }
}
void dock_collapse(DockSpaceSlot* s, uint32_t leaf) noexcept {
    auto& n = s->nodes[leaf - 1u];
    if (n.first || dock_visible(s, leaf) || !n.parent) return;
    const uint32_t parent = n.parent;
    auto& p = s->nodes[parent - 1u];
    const uint32_t sibling = p.first == leaf ? p.second : p.first;
    const uint32_t grand = p.parent;
    // Closed tabs remain addressable and reopen in the promoted sibling subtree.
    uint32_t destination = sibling;
    while (s->nodes[destination - 1u].first) destination = s->nodes[destination - 1u].first;
    for (auto& t : std::span(s->tabs, s->tab_cap)) if (t.leaf == leaf) t.leaf = destination;
    p = s->nodes[sibling - 1u]; p.parent = grand;
    dock_reparent(s, sibling, parent);
    s->nodes[sibling - 1u] = {}; n = {};
}
}
bool dock_space_initialized(uint32_t id) noexcept {
    Context* ctx = current_context(); return ctx && id && dock_slot(ctx, id);
}
uint32_t dock_reset_root(uint32_t id) noexcept {
    Context* ctx = current_context(); if (!ctx || !id || ctx->active_ui->dock_cursor.space) return 0;
    DockSpaceSlot* s = dock_slot(ctx, id);
    auto& ui = *ctx->active_ui;
    if (!s) {
        if (ui.dock_space_count == ui.dock_space_cap) {
            const uint32_t cap = ui.dock_space_cap ? ui.dock_space_cap * 2u : 4u;
            void* p = std::realloc(ui.dock_spaces, size_t(cap) * sizeof(DockSpaceSlot));
            if (!p) return 0;
            ui.dock_spaces = static_cast<DockSpaceSlot*>(p); ui.dock_space_cap = cap;
        }
        s = &ui.dock_spaces[ui.dock_space_count++]; *s = {};
    }
    for (auto& tab : std::span(s->tabs, s->tab_cap)) { std::free(tab.title_heap); tab = {}; }
    for (auto& n : std::span(s->nodes, s->node_cap)) n = {};
    if (!s->node_cap && !grow_dock_nodes(s)) return 0;
    s->id = id; s->root = 1; s->split_seq = 0; s->nodes[0].used = true; return 1;
}
bool dock_split_node(uint32_t id, uint32_t node, Axis axis, float ratio,
    uint32_t* out_a, uint32_t* out_b, uint32_t split_id, float min_a, float min_b) noexcept {
    Context* ctx = current_context();
    if (!ctx || !id || !node || ctx->active_ui->dock_cursor.space || !std::isfinite(ratio)) return false;
    auto* s = dock_slot(ctx, id); if (!s || node > s->node_cap || !s->nodes[node - 1u].used || s->nodes[node - 1u].first) return false;
    uint32_t free_nodes[2] = {}; uint32_t count = 0;
    for (uint32_t i = 0; i < s->node_cap && count < 2; ++i) if (!s->nodes[i].used) free_nodes[count++] = i + 1;
    if (count != 2) {
        const uint32_t old = s->node_cap;
        if (!grow_dock_nodes(s)) return false;
        for (uint32_t i = old; count < 2; ++i) free_nodes[count++] = i + 1u;
    }
    auto& n = s->nodes[node - 1u]; const uint32_t a = free_nodes[0], b = free_nodes[1];
    s->nodes[a - 1u] = n; s->nodes[a - 1u].parent = node;
    s->nodes[b - 1u] = {}; s->nodes[b - 1u].used = true; s->nodes[b - 1u].parent = node;
    dock_reparent(s, node, a);
    n.first = a; n.second = b; n.axis = axis; n.active = 0;
    n.split_id = split_id ? split_id : id_index(id, kDockSplitTagBase + s->split_seq++);
    n.min_first = min_a; n.min_second = min_b;
    set_split_ratio(n.split_id, ratio);
    if (out_a) { *out_a = a; } if (out_b) { *out_b = b; } return true;
}
bool dock_add_pane(uint32_t id, uint32_t leaf, const DockPaneDesc& pane) noexcept {
    Context* ctx = current_context();
    if (!ctx || !id || !pane.id || !leaf || (pane.title.length && !pane.title.chars) || ctx->active_ui->dock_cursor.space) return false;
    auto* s = dock_slot(ctx, id); if (!s || leaf > s->node_cap || !s->nodes[leaf - 1u].used || s->nodes[leaf - 1u].first || dock_tab(s, pane.id)) return false;
    DockTabSlot* target = nullptr;
    for (auto& t : std::span(s->tabs, s->tab_cap)) if (!t.id) { target = &t; break; }
    if (!target) {
        const uint32_t cap = s->tab_cap ? s->tab_cap * 2u : 32u;
        void* p = std::realloc(s->tabs, size_t(cap) * sizeof(DockTabSlot));
        if (!p) return false;
        s->tabs = static_cast<DockTabSlot*>(p);
        for (uint32_t i = s->tab_cap; i < cap; ++i) s->tabs[i] = {};
        target = &s->tabs[s->tab_cap]; s->tab_cap = cap;
    }
    auto& t = *target;
    const uint32_t len = pane.title.length;
    if (len >= sizeof(t.title_inline)) {
        uint32_t cap = 64; while (cap <= len) cap *= 2u;
        t.title_heap = static_cast<char*>(std::calloc(cap, 1));
        if (!t.title_heap) return false;
        t.title_cap = cap;
    }
    char* title = t.title_heap ? t.title_heap : t.title_inline;
    if (len) { std::memcpy(title, pane.title.chars, len); } title[len] = 0;
    t.id = pane.id; t.leaf = leaf; t.icon_id = pane.icon_id; t.closable = pane.closable; t.open = pane.default_open;
    s->nodes[leaf - 1u].active = dock_visible(s, leaf); return true;
}

uint32_t dock_find_pane_leaf(uint32_t id, uint32_t pane) noexcept {
    Context* ctx = current_context(); if (!ctx || !id || !pane) return 0;
    auto* s = dock_slot(ctx, id); auto* t = s ? dock_tab(s, pane) : nullptr; return t ? t->leaf : 0;
}
uint32_t dock_active_pane(uint32_t id, uint32_t leaf) noexcept {
    Context* ctx = current_context(); if (!ctx || !id || !leaf) return 0;
    auto* s = dock_slot(ctx, id); return s && leaf <= s->node_cap && s->nodes[leaf - 1u].used ? dock_visible(s, leaf) : 0;
}
bool dock_is_pane_open(uint32_t id, uint32_t pane) noexcept {
    Context* ctx = current_context(); if (!ctx || !id || !pane) return false;
    auto* s = dock_slot(ctx, id); auto* t = s ? dock_tab(s, pane) : nullptr; return t && t->open;
}
void dock_activate_pane(uint32_t id, uint32_t pane) noexcept {
    Context* ctx = current_context(); if (!ctx || !id || !pane) return;
    auto* s = dock_slot(ctx, id); auto* t = s ? dock_tab(s, pane) : nullptr;
    if (t && t->open) { s->nodes[t->leaf - 1u].active = pane; request_redraw(ctx); }
}
void dock_set_pane_open(uint32_t id, uint32_t pane, bool open) noexcept {
    Context* ctx = current_context(); if (!ctx || !id || !pane || ctx->active_ui->dock_cursor.space) return;
    auto* s = dock_slot(ctx, id); auto* t = s ? dock_tab(s, pane) : nullptr;
    if (t) { t->open = open; if (open) s->nodes[t->leaf - 1u].active = pane; else dock_collapse(s, t->leaf); request_redraw(ctx); }
}
bool dock_dock_pane(uint32_t id, uint32_t pane, uint32_t target, DockZone zone, float ratio) noexcept {
    Context* ctx = current_context();
    if (!ctx || !id || !pane || !target || pane == target || zone < DockZone::Center || zone > DockZone::Bottom || !std::isfinite(ratio) || ctx->active_ui->dock_cursor.space) return false;
    auto* s = dock_slot(ctx, id); auto* t = s ? dock_tab(s, pane) : nullptr; auto* dst = s ? dock_tab(s, target) : nullptr;
    if (!t || !dst || !dst->open) return false;
    const uint32_t source = t->leaf;
    if (zone == DockZone::Center) { t->leaf = dst->leaf; t->open = true; s->nodes[t->leaf - 1u].active = pane; }
    else {
        uint32_t a = 0, b = 0;
        const bool before = zone == DockZone::Left || zone == DockZone::Top;
        if (!dock_split_node(id, dst->leaf, (zone == DockZone::Left || zone == DockZone::Right) ? Axis::Row : Axis::Col,
            ratio, &a, &b)) return false;
        // Existing tabs were moved into first; swap leaf contents for a leading insertion.
        if (before) { for (auto& tab : std::span(s->tabs, s->tab_cap)) if (tab.leaf == a) tab.leaf = b; s->nodes[b - 1u].active = target; }
        t->leaf = before ? a : b; t->open = true; s->nodes[t->leaf - 1u].active = pane;
    }
    // The source may have become an internal split if docking within its own leaf.
    dock_collapse(s, source); request_redraw(ctx); return true;
}
bool dock_reorder_pane(uint32_t id, uint32_t pane, uint32_t target, bool after) noexcept {
    Context* ctx = current_context();
    if (!ctx || !id || !pane || !target || pane == target || ctx->active_ui->dock_cursor.space) return false;
    auto* s = dock_slot(ctx,id); auto* src = s ? dock_tab(s,pane) : nullptr; auto* dst = s ? dock_tab(s,target) : nullptr;
    if (!src || !dst || !src->open || !dst->open) return false;
    const uint32_t old_leaf = src->leaf, new_leaf = dst->leaf;
    uint32_t from = uint32_t(src-s->tabs), to = uint32_t(dst-s->tabs);
    if (from < to) { if (!after) --to; } else if (after) ++to;
    DockTabSlot moving = *src; moving.leaf = new_leaf;
    if (from < to) std::memmove(s->tabs+from,s->tabs+from+1,size_t(to-from)*sizeof(DockTabSlot));
    else if (from > to) std::memmove(s->tabs+to+1,s->tabs+to,size_t(from-to)*sizeof(DockTabSlot));
    s->tabs[to] = moving; s->nodes[new_leaf-1].active = pane;
    dock_collapse(s,old_leaf); request_redraw(ctx); return true;
}
void begin_dock_space(const DockSpaceDesc& desc_in) noexcept {
    Context* ctx = current_context(); if (!ctx || !ctx->in_frame || !desc_in.id || ctx->active_ui->dock_cursor.space) return;
    const Theme& thm = active_theme(ctx);
    DockSpaceDesc desc = desc_in;
    desc.background = resolve_theme_color(desc.background, thm.bg_canvas);
    desc.pane_background = resolve_theme_color(desc.pane_background, thm.bg_surface);
    desc.pane_border_color = resolve_theme_color(desc.pane_border_color, thm.border_subtle);
    desc.pane_radius = resolve_theme_corners(desc.pane_radius, Corners::all(thm.radius_panel_px));
    desc.tab_bar_bg = resolve_theme_color(desc.tab_bar_bg, thm.bg_chrome);
    desc.tab_active_bg = resolve_theme_color(desc.tab_active_bg, thm.surface_active);
    desc.tab_hover_bg = resolve_theme_color(desc.tab_hover_bg, thm.bg_popup);
    desc.tab_accent = resolve_theme_color(desc.tab_accent, thm.accent);
    if (!dock_space_initialized(desc.id)) dock_reset_root(desc.id);
    auto* s = dock_slot(ctx, desc.id); if (!s) return;
    auto& c = ctx->active_ui->dock_cursor;
    if (c.traverse_cap < s->node_cap) {
        void* p = std::realloc(c.stack, size_t(s->node_cap) * sizeof(DockTraverseEntry));
        if (!p) return;
        c.stack = static_cast<DockTraverseEntry*>(p); c.traverse_cap = s->node_cap;
    }
    c.space = s; c.desc = desc; c.depth = 1; c.stack[0] = {s->root, 0};
    begin_box({ .id = desc.id, .axis = Axis::Col, .width = desc.width, .height = desc.height, .background = desc.background });
    a11y_semantic(desc.id,A11yRole::Region);
}
uint32_t next_dock_pane() noexcept {
    Context* ctx = current_context(); if (!ctx || !ctx->in_frame || !ctx->active_ui->dock_cursor.space) return 0;
    auto& c = ctx->active_ui->dock_cursor; auto* s = c.space; const auto& d = c.desc;
    if (c.pane_open) { end_box(); end_box(); c.pane_open = false; }
    while (c.depth) {
        auto& e = c.stack[c.depth - 1]; auto& n = s->nodes[e.node_idx - 1];
        if (n.first) {
            if (e.phase == 0) {
                begin_split({ .id = n.split_id, .axis = n.axis, .default_ratio = split_ratio(n.split_id),
                    .min_first_px = n.min_first, .min_second_px = n.min_second, .divider_size_px = d.divider_size_px });
                e.phase = 1; c.stack[c.depth++] = {n.first, 0};
            } else if (e.phase == 1) { split_second(); e.phase = 2; c.stack[c.depth++] = {n.second, 0}; }
            else { end_split(); --c.depth; }
            continue;
        }
        const uint32_t leaf = e.node_idx; --c.depth; n.active = dock_visible(s, leaf); if (!n.active) continue;
        const uint32_t leaf_id = id_index(d.id, 0xD200u + leaf);
        begin_box({ .id = leaf_id, .axis = Axis::Col, .width = Sizing::percent(1), .height = Sizing::percent(1),
            .background = d.pane_background, .border_color = d.pane_border_color, .border_width = d.pane_border_width,
            .radius = d.pane_radius, .clip_children = true });
        begin_box({ .width = Sizing::grow(), .height = Sizing::fixed(d.tab_bar_height_px), .background = d.tab_bar_bg });
        for (auto& t : std::span(s->tabs, s->tab_cap)) if (t.leaf == leaf && t.open) {
            const uint32_t tab_id = id_index(t.id, 0xD300u), close_id = id_index(t.id, 0xD301u);
            if (clicked(tab_id) && !clicked(close_id)) n.active = t.id;
            if (dragging(tab_id, 4.0f)) begin_drag_payload(tab_id, kInternalDockDragKind, dock_tab_title(t), nullptr, 0, 0, t.id);
            begin_box({ .id = tab_id, .height = Sizing::grow(), .padding = Padding::xy(10, 0), .gap = 6,
                .align_y = AlignY::Center, .background = n.active == t.id ? d.tab_active_bg : (hovered(tab_id) ? d.tab_hover_bg : Color{}), .focusable = true });
            if (n.active == t.id) {
                auto* color=ctx->frame_arena.alloc<Color>();
                if(color) {*color=d.tab_accent;draw_after_layout([](Rect r,void* data) noexcept {
                    draw_line({.from={2,r.h-1.5f},.to={r.w-2,r.h-1.5f},.color=*static_cast<Color*>(data),.thickness=2});
                },color);}
            }
            if (t.icon_id != kInvalidIcon) icon({ .icon_id = t.icon_id, .size_px = 14 });
            text(dock_tab_title(t), { .font_size_px = 12.5f, .wrap = false });
            if (t.closable) {
                begin_box({ .id = close_id, .width = Sizing::fixed(14), .height = Sizing::fixed(20), .align_y = AlignY::Center });
                if(hovered(close_id))set_cursor(Cursor::Hand);
                const Color color=hovered(close_id)?active_theme(ctx).text_primary:active_theme(ctx).text_muted;
                draw_line({.from={3,6},.to={11,14},.color=color,.thickness=1.5f});
                draw_line({.from={11,6},.to={3,14},.color=color,.thickness=1.5f});end_box();
                if (clicked(close_id)) { t.open = false; c.move_pane = t.id; c.target = 0; }
            }
            DragPayloadView tab_payload = {};
            if (peek_drag_payload(&tab_payload,kInternalDockDragKind) && tab_payload.u32 != t.id && hovered(tab_id)) {
                const Rect tr = element_bounds(tab_id); const bool after = pointer_pos().x > tr.x+tr.w*.5f;
                const float ix = after ? tr.w-1 : 1;
                draw_line({.from={ix,2},.to={ix,d.tab_bar_height_px-2},.color=d.tab_accent,.thickness=2,.foreground=true});
                if (accept_drag_payload(tab_id,kInternalDockDragKind,&tab_payload)) {
                    c.move_pane=tab_payload.u32; c.target=t.id; c.reorder=true; c.insert_after=after;
                }
            }
            end_box();
        }
        end_box(); n.active = dock_visible(s, leaf);
        DragPayloadView payload = {};
        const Rect r = element_bounds(leaf_id); const Vec2 p = ctx->active_ui->pointer_pos;
        if (!c.reorder && peek_drag_payload(&payload, kInternalDockDragKind) && p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h) {
            const float x = (p.x - r.x) / r.w, y = (p.y - r.y) / r.h;
            DockZone z = DockZone::Center;
            if (x < .28f || x > .72f || y < .28f || y > .72f) {
                const float dx = x < .5f ? x : 1-x, dy = y < .5f ? y : 1-y;
                z = dx < dy ? (x < .5f ? DockZone::Left : DockZone::Right) : (y < .5f ? DockZone::Top : DockZone::Bottom);
            }
            uint32_t target = n.active;
            if (target == payload.u32) {
                target = 0;
                for (const auto& tab : std::span(s->tabs,s->tab_cap))
                    if (tab.open && tab.leaf == leaf && tab.id != payload.u32) { target=tab.id; break; }
            }
            if (!target) z=DockZone::Center;
            const float cx=r.w*.5f, cy=r.h*.5f;
            const Vec2 centers[]={{cx,cy},{cx-32,cy},{cx+32,cy},{cx,cy-32},{cx,cy+32}};
            const DockZone zones[]={DockZone::Center,DockZone::Left,DockZone::Right,DockZone::Top,DockZone::Bottom};
            for (uint32_t j=0;j<5;++j) {
                if (j && !target) continue;
                const Rect hit={centers[j].x-13,centers[j].y-13,26,26};
                if (hit.contains({p.x-r.x,p.y-r.y})) z=zones[j];
            }
            Rect hi = {0,0,r.w,r.h};
            if (z == DockZone::Left || z == DockZone::Right) { hi.w *= .5f; if (z == DockZone::Right) hi.x = hi.w; }
            if (z == DockZone::Top || z == DockZone::Bottom) { hi.h *= .5f; if (z == DockZone::Bottom) hi.y = hi.h; }
            const Theme& thm = active_theme(ctx);
            const Color drop_tint = Color::rgba8(d.tab_accent.r, d.tab_accent.g, d.tab_accent.b, 65u);
            const Color drop_active = Color::rgba8(d.tab_accent.r, d.tab_accent.g, d.tab_accent.b, 220u);
            draw_rect({ .rect = hi, .fill = drop_tint, .foreground = true });
            for (uint32_t j=0;j<5;++j) {
                if (j && !target) continue;
                draw_rect({.rect={centers[j].x-13,centers[j].y-13,26,26},
                    .fill=zones[j]==z ? drop_active : thm.bg_tooltip,
                    .radius=Corners::all(3),.border_color=d.tab_accent,.border_width=1,.foreground=true});
                draw_line({.from={centers[j].x-5,centers[j].y},.to={centers[j].x+5,centers[j].y},.color=Color::white(),.foreground=true});
                if (j==0 || j>=3) draw_line({.from={centers[j].x,centers[j].y-5},.to={centers[j].x,centers[j].y+5},.foreground=true});
            }
            if (target && accept_drag_payload(leaf_id, kInternalDockDragKind, &payload)) { c.move_pane = payload.u32; c.target = target; c.zone = z; }
        }
        begin_box({ .axis = Axis::Col, .width = Sizing::percent(1), .height = Sizing::grow(), .shrink_height = true }); c.pane_open = true;
        if (n.active) return n.active;
        end_box(); end_box(); c.pane_open = false;
    }
    return 0;
}
void end_dock_space() noexcept {
    Context* ctx = current_context(); if (!ctx || !ctx->in_frame || !ctx->active_ui->dock_cursor.space) return;
    auto& c = ctx->active_ui->dock_cursor;
    if (c.pane_open) { end_box(); end_box(); }
    while (c.depth) { auto& e = c.stack[--c.depth]; if (e.phase) end_split(); }
    end_box();
    const uint32_t id = c.desc.id, pane = c.move_pane, target = c.target; const DockZone zone = c.zone;
    auto* s = c.space; c.space = nullptr; c.pane_open = false; c.move_pane = 0; c.target = 0;
    const bool reorder=c.reorder, after=c.insert_after; c.reorder=false; c.insert_after=false;
    if (pane && target && reorder) dock_reorder_pane(id,pane,target,after);
    else if (pane && target) dock_dock_pane(id, pane, target, zone);
    else if (pane) { auto* t = dock_tab(s, pane); dock_collapse(s, t->leaf); request_redraw(ctx); }
}
namespace {
TableMetadata* table_metadata(Context* ctx, uint32_t id) noexcept {
    for (auto& m : std::span(ctx->active_ui->tables, ctx->active_ui->table_count)) if (m.id == id) return &m;
    return nullptr;
}
uint32_t table_col_id(const TableDesc& d, uint32_t c) noexcept {
    return d.columns[c].id ? d.columns[c].id : id_index(d.id, kTableColTagBase + c);
}
void table_close_row(TableStackEntry& t) noexcept {
    if (t.cell_open) { end_box(); t.cell_open = false; }
    if (t.row_open) { end_box(); t.row_open = false; }
}
}
struct TableHeaderPaint {
    uint32_t table_id, column;
    bool lines, sorted, descending;
    Color border, accent;
};
void paint_table_header(Rect bounds, void* data) noexcept {
    const auto& p=*static_cast<const TableHeaderPaint*>(data);
    Context* ctx=current_context();
    auto* meta=table_metadata(ctx,p.table_id);
    meta->widths[p.column]=bounds.w;
    state_map_find(&ctx->active_ui->states,meta->column_ids[p.column])->column_width=bounds.w;
    if (p.lines) draw_line({.from={bounds.w-1,0},.to={bounds.w-1,bounds.h},.color=p.border});
    if (p.sorted) {
        const float x=bounds.w-14,y=bounds.h*.5f,dir=p.descending?3.0f:-3.0f;
        draw_line({.from={x-3,y-dir},.to={x,y+dir},.color=p.accent});
        draw_line({.from={x,y+dir},.to={x+3,y-dir},.color=p.accent});
    }
}
bool table_row_selected(const TableSelection& s,uint32_t row) noexcept {
    if(s.selected_words && row/64<s.word_count) return (s.selected_words[row/64] & (uint64_t(1)<<(row%64)))!=0;
    return s.focused_row && *s.focused_row==row;
}
namespace {
void table_select_row(const TableDesc& d,uint32_t row,bool extend,bool toggle) noexcept {
    auto* ws=state_map_find(&current_context()->active_ui->states,d.id);
    const TableSelection& s=d.selection;
    uint32_t& focus=s.focused_row ? *s.focused_row : ws->table_focus_row;
    uint32_t& anchor=s.anchor_row ? *s.anchor_row : ws->table_anchor_row;
    if(anchor>=d.item_count || !extend) anchor=row;
    focus=row;
    if(d.selected_row) *d.selected_row=row;
    if(s.selected_words) {
        if(!extend && !toggle) std::memset(s.selected_words,0,size_t(s.word_count)*sizeof(uint64_t));
        if(extend) {
            if(!toggle) std::memset(s.selected_words,0,size_t(s.word_count)*sizeof(uint64_t));
            const uint32_t first=std::min(row,anchor),last=std::max(row,anchor);
            for(uint32_t i=first;i<=last;++i) s.selected_words[i/64]|=uint64_t(1)<<(i%64);
        } else if(toggle) s.selected_words[row/64]^=uint64_t(1)<<(row%64);
        else s.selected_words[row/64]|=uint64_t(1)<<(row%64);
    }
    request_redraw(current_context());
}
}
TableResult begin_table(const TableDesc& desc_in) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || !desc_in.id || desc_in.columns.empty() || desc_in.columns.size() > kMaxTableColumns || ctx->active_ui->table_depth == 8 ||
        !std::isfinite(desc_in.header_font_size_px) || desc_in.header_font_size_px <= 0 || !std::isfinite(desc_in.row_height_px) || desc_in.row_height_px <= 0) return {};
    if(desc_in.selection.selected_words && desc_in.selection.word_count < (uint64_t(desc_in.item_count)+63)/64) return {};
    for (const auto& col : desc_in.columns) if (!std::isfinite(col.width) || !std::isfinite(col.min_width) || !std::isfinite(col.max_width) || col.min_width < 0 || col.max_width < col.min_width) return {};
    const Theme& thm = active_theme(ctx);
    TableDesc desc = desc_in;
    desc.row_height_px=clamp_range(desc.row_height_px,1,16384);
    desc.row_gap_px=std::isfinite(desc.row_gap_px) && desc.row_gap_px>0 ? float(to_u16_round(desc.row_gap_px)) : 0;
    desc.background = resolve_theme_color(desc.background, thm.bg_table);
    desc.header_background = resolve_theme_color(desc.header_background, thm.bg_elevated);
    desc.zebra_color = resolve_theme_color(desc.zebra_color, thm.zebra_row);
    desc.selected_row_color = resolve_theme_color(desc.selected_row_color, thm.surface_selected);
    desc.hover_row_color = resolve_theme_color(desc.hover_row_color, thm.selection_hover);
    desc.border_color = resolve_theme_color(desc.border_color, thm.border_line);
    desc.radius = resolve_theme_corners(desc.radius, Corners::all(thm.radius_table_px));
    auto* metadata = table_metadata(ctx, desc.id);
    if (!metadata) {
        auto& ui = *ctx->active_ui;
        if (ui.table_count == ui.table_cap) {
            const uint32_t cap = ui.table_cap ? ui.table_cap * 2u : 16u;
            void* p = std::realloc(ui.tables, size_t(cap) * sizeof(TableMetadata));
            if (!p) return {};
            ui.tables = static_cast<TableMetadata*>(p); ui.table_cap = cap;
        }
        metadata = &ui.tables[ui.table_count++]; *metadata = {}; metadata->id = desc.id;
    }
    if (!metadata) return {};
    auto& t = ctx->active_ui->table_stack[ctx->active_ui->table_depth++]; t = {}; t.desc = desc; t.count = static_cast<uint32_t>(desc.columns.size());
    for (uint32_t c = 0; c < t.count; ++c) t.columns[c] = desc.columns[c];
    t.desc.columns = {t.columns, t.count};
    auto* ws = state_map_get_or_insert(&ctx->active_ui->states, desc.id, ctx->active_ui->frame_index);
    if (!ws->table_initialized || ws->table_sort_column >= t.count) {
        ws->table_initialized = true; ws->table_sort_column = desc.default_sort_column < t.count ? desc.default_sort_column : 0;
        ws->table_sort_descending = desc.default_sort_descending;
    }
    TableResult result = { .sort_column = ws->table_sort_column, .sort_descending = ws->table_sort_descending, .sort_changed = ws->table_sort_changed };
    ws->table_sort_changed = false;
    if(desc.keyboard_navigation && focused(desc.id) && desc.item_count) {
        const uint32_t current=desc.selection.focused_row ? *desc.selection.focused_row : desc.selected_row ? *desc.selected_row : ws->table_focus_row;
        uint32_t next=current<desc.item_count ? current : 0;
        const uint32_t page=uint32_t(std::fmax(1,std::floor(ws->viewport_dim.y/(desc.row_height_px+desc.row_gap_px))));
        bool moved=false;
        if(key_pressed(SDLK_DOWN,true)) {if(current<desc.item_count && next+1<desc.item_count) ++next;moved=true;}
        if(key_pressed(SDLK_UP,true)) {if(next>0) --next;moved=true;}
        if(key_pressed(SDLK_PAGEDOWN,true)) {next+=std::min(page,desc.item_count-1-next);moved=true;}
        if(key_pressed(SDLK_PAGEUP,true)) {next-=std::min(page,next);moved=true;}
        if(key_pressed(SDLK_HOME)) {next=0;moved=true;}
        if(key_pressed(SDLK_END)) {next=desc.item_count-1;moved=true;}
        if(moved) {
            table_select_row(desc,next,(key_mods()&Mod_Shift)!=0,(key_mods()&Mod_Primary)!=0);
            const double top=double(next)*(desc.row_height_px+desc.row_gap_px);
            if(top<ws->vscroll_pos_y || top+desc.row_height_px>ws->vscroll_pos_y+ws->viewport_dim.y)
                table_scroll_to_row(desc.id,next,desc.row_height_px,top<ws->vscroll_pos_y ? AlignY::Top : AlignY::Bottom);
        }
        if((key_mods()&Mod_Primary) && key_pressed(SDLK_A) && desc.selection.selected_words) {
            const uint32_t words=desc.item_count/64u+(desc.item_count%64u!=0);
            std::memset(desc.selection.selected_words,0xFF,size_t(words)*sizeof(uint64_t));
            if(desc.item_count%64) desc.selection.selected_words[words-1]&=(uint64_t(1)<<(desc.item_count%64))-1;
            request_redraw(ctx);
        }
    }
    const uint32_t outer_id = id_index(desc.id, kTableOuterTag);
    const float gutter = desc.show_scrollbar ? 10.0f : 0;
    bool resizing = false;
    for (uint32_t c = 0; c < t.count; ++c) {
        const auto& col = t.columns[c]; const uint32_t cid = table_col_id(desc, c), rid = id_index(cid, 0x7600u);
        state_map_get_or_insert(&ctx->active_ui->states, rid, ctx->active_ui->frame_index);
        auto* cw = state_map_get_or_insert(&ctx->active_ui->states, cid, ctx->active_ui->frame_index);
        if (metadata->count != 0 && metadata->count != t.count && col.id == 0) cw->column_override = false;
        const bool active = ctx->active_ui->active_id == rid;
        const bool ended = cw->column_resizing && !active;
        cw->column_resizing = active;
        if (col.resizable) {
            if (double_clicked(rid)) { cw->column_override = false; request_redraw(ctx); }
            else if (state_map_find(&ctx->active_ui->states, rid)->pressed_this_frame) { cw->column_grab = ctx->active_ui->pointer_pos.x - (cw->column_width > 0 ? cw->column_width : col.width); }
            else if (active && ctx->active_ui->prev_pointer_down) {
                cw->column_width = clamp_range(ctx->active_ui->pointer_pos.x - cw->column_grab, col.min_width, col.max_width);
                cw->column_override = true; request_redraw(ctx);
            }
            if (active || hovered(rid)) ctx->active_ui->desired_cursor = Cursor::ResizeEW;
        }
        resizing = resizing || active || ended;
        if (cw->column_override || col.sizing == TableColumnSizing::Fixed) {
            t.col_widths[c] = clamp_range(cw->column_override ? cw->column_width : col.width, col.min_width, col.max_width);
            t.tracks[c] = GridTrack::px(t.col_widths[c]);
        } else {
            t.tracks[c] = GridTrack::fr(col.width > 0 ? col.width : 1,col.min_width,col.max_width);
            t.col_widths[c] = cw->column_width > 0 ? cw->column_width : col.min_width;
        }
    }
    float content_min=desc.padding.left+desc.padding.right+gutter;
    for(uint32_t c=0;c<t.count;++c) content_min+=t.tracks[c].kind==SizingKind::Fixed ? t.col_widths[c] : t.columns[c].min_width;
    if(!desc.horizontal_scroll) content_min=0;
    begin_box({ .id = id_index(desc.id,0x7402u), .axis = Axis::Col, .width = desc.width.kind == SizingKind::Grow ? Sizing::percent(1) : desc.width, .height = desc.height, .shrink_height = true });
    begin_box({ .id = outer_id, .axis = Axis::Col, .width = desc.width, .height = desc.height,
        .padding={0,0,0,desc.horizontal_scroll ? 12.0f : 0.0f},
        .background = desc.background, .border_color = desc.border_color, .border_width = desc.border_width, .radius = desc.radius,
        .clip_children = true, .scroll_x=desc.horizontal_scroll,.show_scrollbar=desc.show_scrollbar, .shrink_height = true });
    begin_box({ .id = id_index(desc.id, kTableHeaderTag), .width = Sizing::grow(content_min), .height = Sizing::fixed(desc.header_height_px),
        .padding = {desc.padding.left, desc.padding.right + gutter, 0, 0}, .background = desc.header_background, .grid_tracks = {t.tracks,t.count}, .shrink_width = true });
    for (uint32_t c = 0; c < t.count; ++c) {
        const auto& col = t.columns[c]; const uint32_t cid = table_col_id(desc, c), hid = id_index(cid, 0x7601u), rid = id_index(cid, 0x7600u);
        auto* cw = state_map_find(&ctx->active_ui->states, cid); cw->column_width = t.col_widths[c];
        if (col.sortable && clicked(hid) && !resizing) {
            result.sort_descending = result.sort_column == c ? !result.sort_descending : true;
            result.sort_column = c; result.sort_changed = true;
        }
        begin_box({ .id = hid, .width = Sizing::grow(), .height = Sizing::grow(),
            .padding = {desc.cell_padding_x, desc.cell_padding_x + (col.sortable && result.sort_column == c ? 18.0f : 0.0f), 0, 0}, .align_x = col.align_x, .align_y = AlignY::Center, .clip_children = true, .focusable = true });
        text(col.title, { .font_size_px = desc.header_font_size_px, .wrap = false, .ellipsis = true });
        auto* paint=ctx->frame_arena.alloc<TableHeaderPaint>();
        if (paint) {
            *paint={desc.id,c,desc.show_column_lines,col.sortable && result.sort_column==c,result.sort_descending,desc.border_color,thm.accent};
            draw_after_layout(paint_table_header,paint);
        }
        if (col.resizable) box({ .id = rid, .width = Sizing::fixed(6), .height = Sizing::fixed(desc.header_height_px),
            .attach_to = AttachTo::Parent, .anchor_element = Anchor::TopRight, .anchor_parent = Anchor::TopRight, .floating_offset = {0,0} });
        end_box();
    }
    end_box();
    ws = state_map_find(&ctx->active_ui->states, desc.id);
    ws->table_sort_column = result.sort_column; ws->table_sort_descending = result.sort_descending; metadata->count = t.count;
    for (uint32_t c = 0; c < t.count; ++c) { metadata->widths[c] = t.col_widths[c]; metadata->column_ids[c] = table_col_id(desc,c); metadata->min_widths[c] = t.columns[c].min_width; metadata->max_widths[c] = t.columns[c].max_width; }
    begin_box({ .axis = Axis::Col, .width = Sizing::grow(content_min), .height = Sizing::grow(), .shrink_width = true, .shrink_height=true });
    result.slice = begin_virtual_list({ .id = desc.id, .item_count = desc.item_count, .item_height_px = desc.row_height_px,
        .row_gap_px = desc.row_gap_px, .overscan_rows = desc.overscan_rows, .width = Sizing::grow(), .height = Sizing::grow(),
        .padding = desc.padding, .background = Color::transparent(), .border_width = 0, .show_scrollbar = desc.show_scrollbar, .scroll_fade_px=desc.scroll_fade_px });
    Clay__GetOpenLayoutElement()->config.layout.shrinkWidth = true;
    // The list viewport must shrink inside the clipped table body. Its rows
    // describe scrollable content, not a minimum height for the table.
    Clay__GetOpenLayoutElement()->config.layout.shrinkHeight = true;
    // Rows own the horizontal insets, including the scrollbar gutter. Keep the list's vertical padding.
    ws=state_map_find(&ctx->active_ui->states,desc.id);
    ws->is_focusable=desc.keyboard_navigation;
    if(desc.keyboard_navigation && reserve_focus(ctx->active_ui)) ctx->active_ui->focus_order[ctx->active_ui->focus_count++]=desc.id;
    a11y_semantic(desc.id,A11yRole::Table,{},0,desc.keyboard_navigation ? 1u : 0u);
    Clay__GetOpenLayoutElement()->config.layout.padding.left = 0;
    Clay__GetOpenLayoutElement()->config.layout.padding.right = 0;
    t.cursor_row = result.slice.first_index; t.end_row = result.slice.end_index;
    return result;
}
bool table_next_row(uint32_t* out, Color bg, Color border) noexcept {
    Context* ctx = current_context(); if (!ctx || !ctx->in_frame || !ctx->active_ui->table_depth) return false;
    auto& t = ctx->active_ui->table_stack[ctx->active_ui->table_depth - 1]; table_close_row(t);
    if (t.cursor_row >= t.end_row) return false;
    const auto& d = t.desc; const uint32_t row = t.cursor_row++, rid = id_index(d.id, 0x100000u + row);
    if (out) *out = row;
    if(clicked(rid)) {
        table_select_row(d,row,(key_mods()&Mod_Shift)!=0,(key_mods()&Mod_Primary)!=0);
        if(d.keyboard_navigation) request_focus(d.id);
    }
    Color row_color = bg.a ? bg : ((row & 1u) ? d.zebra_color : Color{});
    const bool selected=(d.selection.selected_words || d.selection.focused_row) ? table_row_selected(d.selection,row)
        : d.selected_row && *d.selected_row==row;
    if(selected) row_color=d.selected_row_color;
    if (hovered(rid)) {
        const Color under = row_color.a ? row_color : d.background;
        row_color = Color::blend_over(under,d.hover_row_color);
    }
    begin_box({ .id = rid, .width = Sizing::grow(), .height = Sizing::fixed(d.row_height_px),
        .padding = {d.padding.left, d.padding.right + (d.show_scrollbar ? 10.0f : 0.0f), 0, 0},
        .background = row_color,
        .border_color = border, .border_width = border.a ? 1.0f : 0.0f, .grid_tracks = {t.tracks,t.count}, .shrink_width = true });
    t.col_cursor = 0; t.row_open = true; return true;
}
TableRowResult table_row_result(uint32_t table_id, uint32_t row_index) noexcept {
    if (!table_id || row_index > UINT32_MAX - 0x100000u) return {};
    const uint32_t rid = id_index(table_id, 0x100000u + row_index);
    return {rid, hovered(rid), pressed(rid), clicked(rid), double_clicked(rid), right_clicked(rid)};
}
bool table_next_column() noexcept {
    Context* ctx = current_context(); if (!ctx || !ctx->in_frame || !ctx->active_ui->table_depth) return false;
    auto& t = ctx->active_ui->table_stack[ctx->active_ui->table_depth - 1]; if (!t.row_open) return false;
    if (t.cell_open) { end_box(); t.cell_open = false; }
    if (t.col_cursor == t.count) return false;
    const uint32_t c = t.col_cursor++;
    begin_box({ .id = id_index(id_index(t.desc.id, 0x100000u + t.cursor_row - 1),c), .width = Sizing::grow(), .height = Sizing::grow(),
        .padding = Padding::xy(t.desc.cell_padding_x,0), .align_x = t.columns[c].align_x, .align_y = AlignY::Center, .clip_children = true, .pointer_passthrough = true });
    t.cell_open = true; return true;
}
void end_table() noexcept {
    Context* ctx = current_context(); if (!ctx || !ctx->in_frame || !ctx->active_ui->table_depth) return;
    auto& t = ctx->active_ui->table_stack[--ctx->active_ui->table_depth]; table_close_row(t); end_virtual_list(); end_box(); end_box(); end_box();
}
float table_column_width(uint32_t id, uint32_t col) noexcept {
    Context* ctx = current_context(); if (!ctx || !id || col >= kMaxTableColumns) return 0;
    const auto* metadata = table_metadata(ctx,id); return metadata && col < metadata->count ? metadata->widths[col] : 0;
}
void set_table_column_width(uint32_t id, uint32_t col, float width) noexcept {
    Context* ctx = current_context(); if (!ctx || !id || col >= kMaxTableColumns || !std::isfinite(width) || width <= 0) return;
    auto* metadata = table_metadata(ctx,id); if (!metadata || col >= metadata->count) return;
    width = clamp_range(width,metadata->min_widths[col],metadata->max_widths[col]); metadata->widths[col] = width;
    const uint32_t cid = metadata->column_ids[col]; auto* cw = state_map_get_or_insert(&ctx->active_ui->states,cid,ctx->active_ui->frame_index);
    cw->column_width = width; cw->column_override = true; request_redraw(ctx);
}
void set_table_sort(uint32_t id, uint32_t col, bool descending) noexcept {
    Context* ctx = current_context(); if (!ctx || !id || col >= kMaxTableColumns) return;
    auto* ws = state_map_get_or_insert(&ctx->active_ui->states,id,ctx->active_ui->frame_index);
    const auto* metadata = table_metadata(ctx,id);
    if (metadata && col >= metadata->count) return;
    ws->table_sort_changed = !ws->table_initialized || ws->table_sort_column != col || ws->table_sort_descending != descending;
    ws->table_initialized = true; ws->table_sort_column = col; ws->table_sort_descending = descending; request_redraw(ctx);
}
void table_scroll_to_row(uint32_t id, uint32_t row, float height, AlignY align) noexcept { virtual_list_scroll_to_row(id,row,height,align); }
bool is_tree_open(uint32_t id) noexcept {
    Context* ctx = current_context(); if (!ctx || !id) return false;
    const auto* ws = state_map_find(&ctx->active_ui->states,id); return ws && ws->tree_open;
}
void set_tree_open(uint32_t id, bool open) noexcept {
    Context* ctx = current_context(); if (!ctx || !id) return;
    auto* ws = state_map_get_or_insert(&ctx->active_ui->states,id,ctx->active_ui->frame_index); ws->tree_initialized = true; ws->tree_open = open; request_redraw(ctx);
}
TreeNodeResult begin_tree_node(const TreeNodeDesc& d_in) noexcept {
    Context* ctx = current_context(); if (!ctx || !ctx->in_frame || !d_in.id || ctx->active_ui->tree_depth >= kMaxTreeDepth) return {};
    const Theme& thm = active_theme(ctx);
    TreeNodeDesc d = d_in;
    d.icon_color = resolve_theme_color(d.icon_color, thm.accent);
    d.text_color = resolve_theme_color(d.text_color, thm.text_secondary);
    d.selected_bg = resolve_theme_color(d.selected_bg, thm.surface_selected);
    d.selected_accent = resolve_theme_color(d.selected_accent, thm.accent);
    d.hover_bg = resolve_theme_color(d.hover_bg, thm.surface_active);
    const uint32_t chev = id_index(d.id,kTreeChevronTag);
    auto* ws = state_map_get_or_insert(&ctx->active_ui->states,d.id,ctx->active_ui->frame_index);
    if (!ws->tree_initialized) { ws->tree_initialized = true; ws->tree_open = d.default_open; }
    PendingA11yAction tree_act = {};
    const bool got_act = a11y_take_any(d.id, d.leaf ? 2u : 50u, &tree_act);
    const bool semantic_press = got_act && tree_act.action == A11yAction::Press;
    TreeNodeResult r = { .clicked = semantic_press || ws->clicked_this_frame, .double_clicked = double_clicked(d.id), .hovered = hovered(d.id) };
    bool open = ws->tree_open;
    if (got_act && !d.leaf) {
        const bool next = (tree_act.action == A11yAction::Press) ? !open : (tree_act.action == A11yAction::Expand);
        if (next != open) { open = next; r.toggled = true; }
    }
    if (!d.leaf) {
        if (clicked(chev) || r.double_clicked) { open = !open; r.toggled = true; }
        if (focused(d.id)) {
            if (key_pressed(SDLK_RIGHT) && !open) { open = true; r.toggled = true; }
            if (key_pressed(SDLK_LEFT) && open) { open = false; r.toggled = true; }
        }
    }
    ws->tree_open = open;
    begin_box({ .id = d.id, .width = Sizing::grow(), .height = Sizing::fixed(d.row_height_px),
        .padding = {static_cast<float>(ctx->active_ui->tree_depth)*d.indent_px,0,0,0}, .gap = 4,
        .align_y = AlignY::Center, .background = d.selected ? d.selected_bg : (r.hovered ? d.hover_bg : Color{}), .focusable = true });
    a11y_semantic(d.id,A11yRole::TreeItem,d.label,(!d.leaf && open?A11yState_Expanded:0u)|(d.selected?A11yState_Selected:0u),d.leaf?3u:51u);
    if (d.selected) draw_line({ .from = {1,2}, .to = {1,d.row_height_px-2}, .color = d.selected_accent, .thickness = 2 });
    begin_box({ .id = chev, .width = Sizing::fixed(16), .height = Sizing::fixed(d.row_height_px) });
    if (!d.leaf) {
        const float y = d.row_height_px*.5f;
        draw_line({ .from = open ? Vec2{4,y-2} : Vec2{6,y-4}, .to = open ? Vec2{8,y+2} : Vec2{10,y}, .color = d.text_color });
        draw_line({ .from = open ? Vec2{8,y+2} : Vec2{10,y}, .to = open ? Vec2{12,y-2} : Vec2{6,y+4}, .color = d.text_color });
    }
    end_box();
    if (d.icon_id != kInvalidIcon) icon({ .icon_id = d.icon_id, .size_px = 14, .color = d.icon_color });
    text(d.label,{ .color = d.text_color, .font_size_px = d.font_size_px, .font_id = d.font_id, .wrap = false }); end_box();
    r.open = !d.leaf && open;
    if (r.open) {
        if(auto* entry=a11y_index(ctx,d.id))ctx->a11y_parent=entry->build;
        begin_box({ .id = id_index(d.id,0x7EE1u), .axis = Axis::Col, .width = Sizing::grow(), .height = Sizing::fit() });
        if (d.show_indent_guide) {
            const float x = static_cast<float>(ctx->active_ui->tree_depth)*d.indent_px+8;
            struct Guide {float x;Color color;};auto* guide=ctx->frame_arena.alloc<Guide>();
            if(guide){*guide={x,thm.grid_minor};draw_after_layout([](Rect r,void* data) noexcept {
                const auto& g=*static_cast<Guide*>(data);draw_line({.from={g.x,0},.to={g.x,r.h},.color=g.color});
            },guide);}
        }
        ++ctx->active_ui->tree_depth;
    }
    return r;
}
void end_tree_node() noexcept {
    Context* ctx = current_context(); if (!ctx || !ctx->in_frame || !ctx->active_ui->tree_depth) return;
    --ctx->active_ui->tree_depth; end_box();
    if(ctx->a11y_parent)ctx->a11y_parent=ctx->a11y_parent->parent;
}

bool is_collapsible_open(uint32_t element_id) noexcept {
    Context* ctx = current_context();
    if (!ctx || !element_id) return false;
    auto* ws = state_map_find(&ctx->active_ui->states,element_id);
    return ws && ws->collapsible_open;
}
void set_collapsible_open(uint32_t element_id, bool open) noexcept {
    Context* ctx = current_context();
    if (!ctx || !element_id) return;
    if (auto* ws = state_map_get_or_insert(&ctx->active_ui->states,element_id,ctx->active_ui->frame_index)) {
        ws->collapsible_initialized = true; ws->collapsible_open = open; request_redraw(ctx);
    }
}
CollapsibleResult begin_collapsible(const CollapsibleDesc& d_in) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || !d_in.id || ctx->active_ui->collapsible_depth >= kMaxCollapsibleDepth) return {};
    const Theme& thm = active_theme(ctx);
    CollapsibleDesc d = d_in;
    d.radius = resolve_theme_corners(d.radius, Corners::all(thm.radius_control_px));
    d.header_bg = resolve_theme_color(d.header_bg, thm.surface_subtle);
    d.header_hover_bg = resolve_theme_color(d.header_hover_bg, thm.surface_subtle_hover);
    d.body_bg = resolve_theme_color(d.body_bg, thm.surface_subtle_body);
    d.border_color = resolve_theme_color(d.border_color, thm.border_subtle);
    d.text_color = resolve_theme_color(d.text_color, thm.text_secondary);
    d.icon_color = resolve_theme_color(d.icon_color, thm.accent);
    auto* ws = state_map_get_or_insert(&ctx->active_ui->states,d.id,ctx->active_ui->frame_index);
    if (!ws) return {};
    if (!ws->collapsible_initialized) { ws->collapsible_initialized = true; ws->collapsible_open = d.default_open; }
    a11y_toggle(d.id,&ws->collapsible_open);
    const uint32_t hdr_id = id_index(d.id,0xC011u);
    const bool toggle = clicked(hdr_id) || (focused(hdr_id) && (key_pressed(SDLK_RETURN) || key_pressed(SDLK_KP_ENTER) || key_pressed(SDLK_SPACE)));
    if (toggle) { ws->collapsible_open = !ws->collapsible_open; request_redraw(ctx); }
    const bool open = ws->collapsible_open;
    float content_h = ws->collapsible_content_h;
    const uint32_t clip_id = id_index(d.id,0xC013u), content_id = id_index(d.id,0xC014u);
    Clay_SetCurrentContext(ctx->active_ui->clay_ctx);
    const Clay_ScrollContainerData sc = Clay_GetScrollContainerData(make_clay_id(clip_id));
    if (sc.found && sc.contentDimensions.height > 0.5f) content_h = sc.contentDimensions.height;
    else if (element_bounds(content_id).h > 0.5f) content_h = element_bounds(content_id).h;
    ws->collapsible_content_h = content_h;
    // spring/begin_box may rehash StateMap: retain only values across those calls.
    const float t = clamp_range(spring(id_index(d.id,0xC012u),open ? 1.0f : 0.0f,d.spring_spec),0,1);
    const bool visible = open || t > 0.001f;
    const bool hov = hovered(hdr_id);
    if (hov) set_cursor(Cursor::Hand);
    begin_box({.id=d.id,.axis=Axis::Col,.width=Sizing::grow(),.height=Sizing::fit(),
        .background=visible ? d.body_bg : Color{},.border_color=d.border_color,.border_width=d.border_width,.radius=d.radius,.clip_children=true});
    a11y_semantic(d.id,A11yRole::Region,d.title,open?A11yState_Expanded:0u,51u);
    begin_box({.id=hdr_id,.axis=Axis::Row,.width=Sizing::grow(),.height=Sizing::fixed(d.header_height_px),
        .padding=d.header_padding,.gap=6,.align_y=AlignY::Center,.background=hov ? d.header_hover_bg : d.header_bg,.focusable=true});
    begin_box({.width=Sizing::fixed(14),.height=Sizing::fixed(d.header_height_px)});
    const float angle = t*1.57079633f, c=std::cos(angle), sn=std::sin(angle);
    Vec2 pts[3] = {};
    constexpr Vec2 chevron[3] = {{-2,-4},{2,0},{-2,4}};
    for (uint32_t i=0;i<3;++i) pts[i]={7+chevron[i].x*c-chevron[i].y*sn,d.header_height_px*.5f+chevron[i].x*sn+chevron[i].y*c};
    draw_polyline({.points=pts,.color=d.icon_color,.thickness=1.5f});
    end_box();
    if (d.icon_id != kInvalidIcon) icon({.icon_id=d.icon_id,.size_px=14,.color=d.icon_color});
    text(d.title,{.color=d.text_color,.font_size_px=d.font_size_px,.font_id=d.font_id,.wrap=false});
    end_box();
    CollapsibleResult result = {.open=open,.visible=visible,.toggled=toggle,.header_hovered=hov,.anim_t=t};
    if (!visible) { end_box(); return result; }
    Sizing clip_sizing = Sizing::fit();
    if (!(open && t >= .999f)) {
        clip_sizing = Sizing::fixed(content_h > .5f ? content_h*t : 0);
        if (content_h <= .5f) request_redraw(ctx);
    }
    begin_box({.id=clip_id,.axis=Axis::Col,.width=Sizing::grow(),.height=clip_sizing,.clip_children=true});
    begin_box({.id=content_id,.axis=Axis::Col,.width=Sizing::grow(),.height=Sizing::fit(),.padding=d.body_padding,.gap=d.body_gap});
    ++ctx->active_ui->collapsible_depth;
    return result;
}
void end_collapsible() noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || !ctx->active_ui->collapsible_depth) return;
    --ctx->active_ui->collapsible_depth;
    end_box(); end_box(); end_box();
}

static void update_selectable_text_pointer(Context* ctx, WidgetState* ws, Clay_RenderCommandArray cmds) noexcept {
    const Clay_RenderCommand* best = nullptr;
    float best_y = 1.0e30f, best_x = 1.0e30f;
    const Vec2 p = ctx->active_ui->pointer_pos;
    for (int32_t i = 0; i < cmds.length; ++i) {
        const auto* cmd = Clay_RenderCommandArray_Get(&cmds,i);
        if (cmd->commandType != CLAY_RENDER_COMMAND_TYPE_TEXT || !cmd->userData || selection_line(cmd).id != ws->key) continue;
        const auto& bb = cmd->boundingBox;
        const float dy = p.y < bb.y ? bb.y-p.y : (p.y > bb.y+bb.height ? p.y-bb.y-bb.height : 0);
        const float dx = p.x < bb.x ? bb.x-p.x : (p.x > bb.x+bb.width ? p.x-bb.x-bb.width : 0);
        if (dy < best_y || (dy == best_y && dx < best_x)) { best = cmd; best_y = dy; best_x = dx; }
    }
    if (!best) return;
    float xa, xb; uint32_t hit;
    const size_t mark = ctx->frame_arena.mark();
    selection_line_geometry(ctx,best,selection_line(best),0,0,p.x,&xa,&xb,&hit);
    ctx->frame_arena.rewind(mark);
    hit = utf8_clamp_boundary(ws->sel_text_ptr,ws->sel_text_len,hit);
    if (ws->pressed_this_frame) {
        if (ctx->active_ui->last_mouse_clicks >= 3u) {
            if (std::memchr(ws->sel_text_ptr,'\n',ws->sel_text_len)) line_bounds_at(ws->sel_text_ptr,ws->sel_text_len,hit,&ws->edit_sel_anchor,&ws->edit_cursor);
            else { ws->edit_sel_anchor = 0; ws->edit_cursor = ws->sel_text_len; }
        } else if (ctx->active_ui->last_mouse_clicks == 2u) utf8_word_bounds_at(ws->sel_text_ptr,ws->sel_text_len,hit,&ws->edit_sel_anchor,&ws->edit_cursor);
        else { if (!(ctx->active_ui->frame_key_mods & Mod_Shift)) ws->edit_sel_anchor = hit; ws->edit_cursor = hit; }
    } else ws->edit_cursor = hit;
    request_redraw(ctx);
}

} // namespace etch
