// Owned layout adapter, UTF-8 + HarfBuzz/stb_truetype FontAtlas, and Prim batch lowering.
// Compiled via src/etch_unity.cpp.

#include <cmath>

namespace etch {

// Internal unity-build helpers have etch namespace linkage.

inline uint16_t to_u16_round(float v) noexcept {
    if (v <= 0.0f) {
        return 0u;
    }
    if (v >= 65535.0f) {
        return 65535u;
    }
    return static_cast<uint16_t>(v + 0.5f);
}

constexpr Rect rect_intersect(Rect a, Rect b) noexcept {
    const float x0 = (a.x > b.x) ? a.x : b.x;
    const float y0 = (a.y > b.y) ? a.y : b.y;
    const float ax1 = a.x + a.w;
    const float ay1 = a.y + a.h;
    const float bx1 = b.x + b.w;
    const float by1 = b.y + b.h;
    const float x1 = (ax1 < bx1) ? ax1 : bx1;
    const float y1 = (ay1 < by1) ? ay1 : by1;
    return Rect{
        .x = x0,
        .y = y0,
        .w = (x1 > x0) ? (x1 - x0) : 0.0f,
        .h = (y1 > y0) ? (y1 - y0) : 0.0f,
    };
}

constexpr float min4f(float a, float b, float c, float d) noexcept {
    const float ab = (a < b) ? a : b;
    const float cd = (c < d) ? c : d;
    return (ab < cd) ? ab : cd;
}

constexpr float clamp_range(float v, float lo, float hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

constexpr double clamp_double(double v, double lo, double hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

constexpr Padding scale_padding(const Padding& p, float scale) noexcept {
    return Padding{
        .left = p.left * scale,
        .right = p.right * scale,
        .top = p.top * scale,
        .bottom = p.bottom * scale,
    };
}

constexpr Corners scale_corners_uniform(const Corners& c, float scale) noexcept {
    return Corners{
        .tl = c.tl * scale,
        .tr = c.tr * scale,
        .br = c.br * scale,
        .bl = c.bl * scale,
    };
}

constexpr Sizing scale_sizing(const Sizing& s, float scale) noexcept {
    return Sizing{
        .kind = s.kind,
        .value = (s.kind == SizingKind::Fixed) ? (s.value * scale) : s.value,
        .min = s.min * scale,
        .max = s.max * scale,
    };
}

inline float current_world_scale(const Context* ctx) noexcept {
    if (ctx && ctx->active_ui->canvas_depth > 0u) {
        const CanvasStackEntry& ce = ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth - 1u];
        if (ce.in_node && ce.state.zoom > 1e-4f) {
            return ce.state.zoom;
        }
    }
    return 1.0f;
}

WidgetState* resolve_canvas_owner(Context* ctx, std::span<const uint32_t> over) noexcept {
    if (!ctx) {
        return nullptr;
    }
    for (int32_t i = static_cast<int32_t>(over.size()) - 1; i >= 0; --i) {
        WidgetState* ws = state_map_find(&ctx->active_ui->states, over[static_cast<size_t>(i)]);
        if (!ws || !ws->is_user_element) {
            continue;
        }
        if (ws->is_canvas && ws->canvas_interactive) {
            return ws;
        }
        if (ws->owner_canvas_id != 0u) {
            WidgetState* cws = state_map_find(&ctx->active_ui->states, ws->owner_canvas_id);
            if (cws && cws->is_canvas && cws->canvas_interactive) {
                return cws;
            }
        }
    }
    return nullptr;
}

constexpr SDL_SystemCursor to_sdl_system_cursor(Cursor c) noexcept {
    switch (c) {
        case Cursor::Arrow:      return SDL_SYSTEM_CURSOR_DEFAULT;
        case Cursor::Hand:       return SDL_SYSTEM_CURSOR_POINTER;
        case Cursor::IBeam:      return SDL_SYSTEM_CURSOR_TEXT;
        case Cursor::ResizeEW:   return SDL_SYSTEM_CURSOR_EW_RESIZE;
        case Cursor::ResizeNS:   return SDL_SYSTEM_CURSOR_NS_RESIZE;
        case Cursor::ResizeNWSE: return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
        case Cursor::ResizeNESW: return SDL_SYSTEM_CURSOR_NESW_RESIZE;
        case Cursor::Move:       return SDL_SYSTEM_CURSOR_MOVE;
        case Cursor::NotAllowed: return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
        case Cursor::Crosshair:  return SDL_SYSTEM_CURSOR_CROSSHAIR;
    }
    return SDL_SYSTEM_CURSOR_DEFAULT;
}

inline constexpr uint32_t kScrollbarXTag   = 0x53435258u;
inline constexpr uint32_t kScrollbarYTag   = 0x53435259u;
inline constexpr uint32_t kSplitDividerTag = 0x53504C54u;
inline constexpr float    kSbMargin      = 4.0f;
inline constexpr float    kSbHitWidth    = 12.0f;
inline constexpr float    kSbThumbWidth  = 6.0f;
inline constexpr float    kSbMinThumbH   = 24.0f;

struct ScrollbarGeom {
    bool  visible = false;
    Rect  hit_track = {};
    Rect  track = {};
    Rect  thumb = {};
    float travel_h = 0.0f;
};

ScrollbarGeom compute_vscrollbar_geom(const WidgetState* ws) noexcept {
    ScrollbarGeom g = {};
    if (!ws || !ws->show_scrollbar || (!ws->is_scroll_y && !ws->is_virtual_list && !ws->is_multiline_input)) {
        return g;
    }
    if(ws->edit_text_buffer && !ws->editor_show_scrollbar)return g;
    double scroll_y = 0.0;
    double scroll_max_y = 0.0;
    double content_h = 0.0;
    if(ws->is_multiline_input) {
        scroll_y=ws->edit_scroll_y;scroll_max_y=ws->edit_max_scroll_y;
        content_h=ws->edit_max_scroll_y+ws->viewport_dim.y;
    } else if (ws->is_virtual_list) {
        scroll_y = ws->vscroll_pos_y;
        scroll_max_y = ws->vscroll_max_y;
        content_h = ws->vscroll_total_h;
    } else {
        scroll_y = static_cast<double>(ws->scroll_pos.y);
        scroll_max_y = static_cast<double>(ws->scroll_max.y);
        content_h = static_cast<double>(ws->content_dim.y);
    }
    if (scroll_max_y <= 0.5 || content_h <= 0.5) {
        return g;
    }
    const Rect& b = ws->last_box_logical;
    const float track_h = b.h - 2.0f * kSbMargin;
    if (b.w <= kSbHitWidth + 4.0f || track_h <= kSbMinThumbH) {
        return g;
    }
    const double view_h = static_cast<double>(ws->viewport_dim.y);
    const double visible_ratio = (view_h < content_h) ? (view_h / content_h) : 1.0;
    float thumb_h = static_cast<float>(static_cast<double>(track_h) * visible_ratio);
    if (thumb_h < kSbMinThumbH) thumb_h = kSbMinThumbH;
    if (thumb_h > track_h) thumb_h = track_h;
    const float travel = track_h - thumb_h;
    double r_d = (scroll_max_y > 0.0) ? (scroll_y / scroll_max_y) : 0.0;
    if (r_d < 0.0) r_d = 0.0;
    if (r_d > 1.0) r_d = 1.0;
    const float ratio = static_cast<float>(r_d);
    const float right_edge = (ws->vscrollbar_right_edge_x > b.x + kSbHitWidth + 4.0f && ws->vscrollbar_right_edge_x < b.x + b.w)
        ? ws->vscrollbar_right_edge_x
        : (b.x + b.w);
    const float thumb_x = right_edge - kSbMargin - kSbThumbWidth;
    const float track_y = b.y + kSbMargin;

    g.visible = true;
    g.hit_track = Rect{ right_edge - kSbMargin - kSbHitWidth, track_y, kSbHitWidth + kSbMargin, track_h };
    g.track = Rect{ thumb_x, track_y, kSbThumbWidth, track_h };
    g.thumb = Rect{ thumb_x, track_y + travel * ratio, kSbThumbWidth, thumb_h };
    g.travel_h = travel;
    return g;
}

bool step_scroll_axis(float* pos, float* vel, float max_val, float decay, float step_factor) noexcept {
    if (!pos || !vel || std::fabs(*vel) <= 1.0f) {
        if (vel) *vel = 0.0f;
        return false;
    }
    *pos += (*vel) * step_factor;
    *vel *= decay;
    if (*pos <= 0.0f) {
        *pos = 0.0f;
        *vel = 0.0f;
        return false;
    }
    if (*pos >= max_val) {
        *pos = max_val;
        *vel = 0.0f;
        return false;
    }
    if (std::fabs(*vel) <= 1.0f) {
        *vel = 0.0f;
        return false;
    }
    return true;
}

bool step_scroll_axis_d(double* pos, double* vel, double max_val, double decay, double step_factor) noexcept {
    if (!pos || !vel || std::fabs(*vel) <= 1.0) {
        if (vel) *vel = 0.0;
        return false;
    }
    *pos += (*vel) * step_factor;
    *vel *= decay;
    if (*pos <= 0.0) {
        *pos = 0.0;
        *vel = 0.0;
        return false;
    }
    if (*pos >= max_val) {
        *pos = max_val;
        *vel = 0.0;
        return false;
    }
    if (std::fabs(*vel) <= 1.0) {
        *vel = 0.0;
        return false;
    }
    return true;
}

// Measures a single-line UTF-8 segment (no '\n') in logical px.
// Computes total line width and, optionally, the x offsets at `stop_a` and `stop_b` relative to `seg`.
ScrollbarGeom compute_hscrollbar_geom(const WidgetState* ws) noexcept {
    ScrollbarGeom g={};
    if(!ws->show_scrollbar || !ws->is_scroll_x || ws->scroll_max.x<=.5f) return g;
    const Rect b=ws->last_box_logical;
    const float track=b.w-2*kSbMargin-(ws->is_scroll_y && ws->scroll_max.y>.5f ? kSbHitWidth : 0);
    if(b.h<=kSbHitWidth+4 || track<=kSbMinThumbH) return g;
    const float thumb=clamp_range(track*ws->viewport_dim.x/ws->content_dim.x,kSbMinThumbH,track);
    const float travel=track-thumb,y=b.y+b.h-kSbMargin-kSbThumbWidth;
    g.visible=true;g.track={b.x+kSbMargin,y,track,kSbThumbWidth};
    g.hit_track={g.track.x,b.y+b.h-kSbMargin-kSbHitWidth,track,kSbHitWidth+kSbMargin};
    g.thumb={g.track.x+travel*ws->scroll_pos.x/ws->scroll_max.x,y,thumb,kSbThumbWidth};g.travel_h=travel;
    return g;
}

void measure_line_segment_x(
    Context* ctx,
    uint16_t font_id,
    const char* seg,
    uint32_t seg_len,
    uint32_t stop_a,
    uint32_t stop_b,
    float base_px,
    float letter_spacing,
    bool mask_bullets,
    bool spacing_between_glyphs_only,
    float* out_xa,
    float* out_xb,
    float* out_total_w,
    uint32_t tab_width,
    LineClusterVisitor visit,
    void* visit_data) noexcept
{
    uint32_t glyph_idx = 0u;
    const auto next_spacing = [&]() noexcept -> float {
        const float sp = (!spacing_between_glyphs_only || glyph_idx > 0u) ? letter_spacing : 0.0f;
        ++glyph_idx;
        return sp;
    };

    float total_w = 0.0f;
    float xa = 0.0f;
    float xb = 0.0f;

    const uint16_t slot_idx = (ctx && ctx->atlas.font_valid) ? resolve_font_slot_idx(&ctx->atlas, font_id) : kInvalidFont;
    if (!ctx || !ctx->atlas.font_valid || slot_idx == kInvalidFont) {
        const float cp_w = base_px * 0.55f;
        const char* p = seg;
        const char* end = seg + seg_len;
        while (p < end) {
            const uint32_t cp_off = static_cast<uint32_t>(p - seg);
            const uint32_t cp = next_utf8_codepoint(p, end);
            const float adv = ((cp == '\t' && !mask_bullets) ? static_cast<float>(tab_width) * cp_w : cp_w) + next_spacing();
            if (visit) {
                if (cp == '\n') continue;
                visit(visit_data, cp_off, static_cast<uint32_t>(p - seg), total_w, adv, false);
            }
            if (cp_off < stop_a) xa += adv;
            if (cp_off < stop_b) xb += adv;
            total_w += adv;
        }
        if (out_xa) *out_xa = xa;
        if (out_xb) *out_xb = xb;
        if (out_total_w) *out_total_w = total_w;
        return;
    }

    const FontSlot& primary = ctx->atlas.slots[slot_idx];
    if (mask_bullets) {
        const float bullet_adv = primary.bullet_advance_em * base_px;
        const char* p = seg;
        const char* end = seg + seg_len;
        while (p < end) {
            const uint32_t cp_off = static_cast<uint32_t>(p - seg);
            const uint32_t cp = next_utf8_codepoint(p, end);
            const float adv = bullet_adv + next_spacing();
            if (visit) {
                if (cp == '\n') continue;
                visit(visit_data, cp_off, static_cast<uint32_t>(p - seg), total_w, adv, false);
            }
            if (cp_off < stop_a) xa += adv;
            if (cp_off < stop_b) xb += adv;
            total_w += adv;
        }
        if (out_xa) *out_xa = xa;
        if (out_xb) *out_xb = xb;
        if (out_total_w) *out_total_w = total_w;
        return;
    }

    uint32_t pos = 0u;
    while (pos < seg_len) {
        if (seg[pos] == '\t') {
            const float adv = static_cast<float>(tab_width) * primary.space_advance_em * base_px + next_spacing();
            if (visit) visit(visit_data, pos, pos + 1u, total_w, adv, false);
            if (pos < stop_a) xa = total_w + adv;
            if (pos < stop_b) xb = total_w + adv;
            total_w += adv;
            ++pos;
            continue;
        }
        uint32_t run_end = pos;
        if (visit && seg[pos] == '\n') { ++pos; continue; }
        while (run_end < seg_len && seg[run_end] != '\t' && (!visit || seg[run_end] != '\n')) {
            ++run_end;
        }
        const uint32_t run_len = run_end - pos;
        bool is_rtl = false;
        const size_t run_mark = ctx->frame_arena.mark();
        const std::span<const ShapedGlyph> glyphs = shape_utf8_run(ctx, slot_idx, seg + pos, run_len, &is_rtl);
        const uint32_t gcount = static_cast<uint32_t>(glyphs.size());
        uint32_t gi = 0u;
        while (gi < gcount) {
            const uint32_t c0 = glyphs[gi].cluster;
            uint32_t gi_end = gi + 1u;
            while (gi_end < gcount && glyphs[gi_end].cluster == c0) {
                ++gi_end;
            }
            float cluster_adv = 0.0f;
            for (uint32_t k = gi; k < gi_end; ++k) {
                cluster_adv += glyphs[k].x_advance_1px * base_px + next_spacing();
            }
            is_rtl = glyphs[gi].is_rtl != 0;
            const uint32_t c1 = glyphs[gi].cluster_end;
            const uint32_t abs_c0 = pos + c0;
            const uint32_t abs_c1 = pos + c1;
            const auto stop_x = [&](uint32_t stop, float* x) noexcept {
                if (stop < abs_c0 || stop > abs_c1) return;
                const uint32_t count = count_utf8_cps(seg + abs_c0, abs_c1 - abs_c0);
                const uint32_t before = count_utf8_cps(seg + abs_c0, stop - abs_c0);
                const float fraction = count ? static_cast<float>(before) / static_cast<float>(count) : 0;
                *x = total_w + cluster_adv * (is_rtl ? 1.0f-fraction : fraction);
            };
            if (visit) visit(visit_data, abs_c0, abs_c1, total_w, cluster_adv, is_rtl);
            if (out_xa) stop_x(stop_a, &xa);
            if (out_xb) stop_x(stop_b, &xb);
            total_w += cluster_adv;
            gi = gi_end;
        }
        ctx->frame_arena.rewind(run_mark);
        pos = run_end;
    }
    if (out_xa) *out_xa = xa;
    if (out_xb) *out_xb = xb;
    if (out_total_w) *out_total_w = total_w;
}

float measure_rich_span_segment(
    Context* ctx,
    const RichSpanInternal& sp,
    const char* seg,
    uint32_t len,
    bool trim_last_spacing) noexcept
{
    if (len == 0u) {
        return 0.0f;
    }
    float w = 0.0f;
    measure_line_segment_x(
        ctx,
        sp.font_id,
        seg,
        len,
        0u,
        0u,
        sp.font_size_px,
        sp.letter_spacing,
        false,
        trim_last_spacing,
        nullptr,
        nullptr,
        &w);
    return w;
}

Vec2 measure_rich_slice(
    Context* ctx,
    const RichTextExtra* rich,
    Str slice) noexcept
{
    if (!slice.chars || slice.length == 0u || !rich || !rich->spans || rich->span_count == 0u) {
        return Vec2{ 0.0f, rich ? rich->line_height_px : 16.0f };
    }
    const char* base = rich->full_utf8;
    const char* base_end = rich->full_utf8 + rich->total_bytes;
    const char* slice_end = slice.chars + slice.length;
    if (!base || slice.chars < base || slice_end > base_end) {
        const RichSpanInternal& s0 = rich->spans[0];
        const float w = measure_rich_span_segment(ctx, s0, slice.chars, slice.length, true);
        return Vec2{ w, rich->line_height_px };
    }

    const uint32_t s0 = static_cast<uint32_t>(slice.chars - base);
    const uint32_t s1 = s0 + slice.length;
    float total_w = 0.0f;
    for (uint32_t i = 0u; i < rich->span_count; ++i) {
        const RichSpanInternal& sp = rich->spans[i];
        if (sp.byte_end <= s0) continue;
        if (sp.byte_start >= s1) break;
        const uint32_t ov0 = (s0 > sp.byte_start) ? s0 : sp.byte_start;
        const uint32_t ov1 = (s1 < sp.byte_end) ? s1 : sp.byte_end;
        const bool is_last_seg = (ov1 == s1);
        const uint32_t text_start = std::max(ov0, sp.layout_text_start());
        if (ov1 > text_start)
            total_w += measure_rich_span_segment(ctx, sp, base + text_start, ov1 - text_start, is_last_seg);
        if (ov0 == sp.byte_start) total_w += sp.padding_x + sp.icon_advance;
        if (ov1 == sp.byte_end) total_w += sp.padding_x;
    }
    return Vec2{ total_w, rich->line_height_px };
}

Vec2 etch_measure_text(Str text, const LayoutTextConfig& config, void* user_data) noexcept {
    Context* ctx = static_cast<Context*>(user_data);
    const size_t arena_mark = ctx ? ctx->frame_arena.mark() : 0u;
    Vec2 dims = {};
    if (config.is_rich && config.user_data != nullptr) {
        dims = measure_rich_slice(ctx, static_cast<const RichTextExtra*>(config.user_data), text);
    } else {
        const uint16_t font_id = config.font_id;
        const float logical_font_px = config.font_size_px > 0.0f ? config.font_size_px : 16.0f;
        const float metric_h = scaled_font_span_px(ctx, font_id, logical_font_px);
        const float letter_spacing = config.letter_spacing;
        if (!text.chars || text.length == 0u) {
            dims = Vec2{ 0.0f, metric_h };
        } else {
            float width_logical = 0.0f;
            measure_line_segment_x(
                ctx, font_id, text.chars, text.length, 0u, 0u,
                logical_font_px, letter_spacing, false, true,
                nullptr, nullptr, &width_logical);
            dims = Vec2{ width_logical, metric_h };
        }
    }
    if (ctx) ctx->frame_arena.rewind(arena_mark);
    return dims;
}

bool ensure_window_ui_clay(Context* ctx, WindowUiState* ui, float logical_w, float logical_h) noexcept {
    if (!ctx || !ui) return false;
    if (ui->layout) return true;
    if (!ui->states.entries && !state_map_init(&ui->states, kDefaultStateCapacity)) {
        return false;
    }
    ui->layout = layout_engine_create(etch_measure_text, ctx);
    if (!ui->layout) return false;
    const float w = (logical_w > 0.0f) ? logical_w : 1280.0f;
    const float h = (logical_h > 0.0f) ? logical_h : 720.0f;
    layout_engine_set_viewport(ui->layout, w, h);
    return true;
}

// Preserve subpixel primitives when rounding would collapse either dimension.
// For thin hairlines/dividers (<= 2.5px), snap thickness independently so 1px borders stay uniform across fractional DPI.
Rect snap_pixel_rect(Rect r) noexcept {
    const float x = std::round(r.x), y = std::round(r.y);
    const float w = (r.w > 0.0f && r.w <= 2.5f) ? std::fmax(1.0f, std::round(r.w)) : (std::round(r.x + r.w) - x);
    const float h = (r.h > 0.0f && r.h <= 2.5f) ? std::fmax(1.0f, std::round(r.h)) : (std::round(r.y + r.h) - y);
    return w > 0 && h > 0 ? Rect{x, y, w, h} : r;
}

ClipEntry make_box_clip(Context* ctx,const ClipEntry& parent,Rect bounds,Corners radii,bool continuous,Vec4 fade = {}) noexcept {
    if(const auto* layer=ctx->active_render_layer) {
        resolve_layer_chain(ctx,layer,ctx->active_ui->dpi_scale);
        const auto scale=layer->resolved_scale,offset=layer->resolved_offset_px;
        bounds={bounds.x*scale.x+offset.x,bounds.y*scale.y+offset.y,bounds.w*scale.x,bounds.h*scale.y};
        const float k=.5f*(scale.x+scale.y);
        radii={radii.tl*k,radii.tr*k,radii.br*k,radii.bl*k};
        fade={fade.x*scale.y,fade.y*scale.y,fade.z*scale.x,fade.w*scale.x};
    }

    fade={fade.x>.25f ? fade.x : 0,fade.y>.25f ? fade.y : 0,fade.z>.25f ? fade.z : 0,fade.w>.25f ? fade.w : 0};
    ClipEntry result={rect_intersect(parent.rect,bounds),0,parent.shape,parent.indexed || parent.shape!=0};
    const float radius=min4f(radii.tl,radii.tr,radii.br,radii.bl);
    result.radius=continuous ? -radius : radius;
    if(radii.tl==0 && radii.tr==0 && radii.br==0 && radii.bl==0 && fade.x==0 && fade.y==0 && fade.z==0 && fade.w==0) {
        if(result.rect.x==parent.rect.x && result.rect.y==parent.rect.y && result.rect.w==parent.rect.w && result.rect.h==parent.rect.h) result.radius=parent.radius;
        return result;
    }
    if(ctx->clip_shape_count==ctx->clip_shape_capacity || ctx->clip_shape_capacity==0) {
        const uint32_t capacity=ctx->clip_shape_capacity ? std::min(65536u,ctx->clip_shape_capacity*2) : 128;
        if(ctx->clip_shape_count>=65536) {SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Rounded clip shape limit exceeded");return result;}
        auto* grown=static_cast<ClipShapeGpu*>(std::realloc(ctx->clip_shapes,size_t(capacity)*sizeof(ClipShapeGpu)));
        if(!grown) return result;
        ctx->clip_shapes=grown;ctx->clip_shape_capacity=capacity;ctx->clip_shapes[0]={};
    }
    const uint32_t index=ctx->clip_shape_count++;
    ctx->clip_shapes[index]={{bounds.x,bounds.y,bounds.w,bounds.h},{radii.tl,radii.tr,radii.br,radii.bl},
        {parent.shape,continuous ? 1u : 0u,pack_f16x2(fade.x,fade.y),pack_f16x2(fade.z,fade.w)}};
    result.shape=index;
    result.indexed=parent.shape!=0 || radii.tl!=radii.tr || radii.tr!=radii.br || radii.br!=radii.bl || fade.x>0 || fade.y>0 || fade.z>0 || fade.w>0;
    return result;
}

Vec4 box_scroll_fade(Context* ctx,const BoxExtra* extra,float dpi) noexcept {
    Vec4 fade = {
        (std::isfinite(extra->desc.edge_fade.top) && extra->desc.edge_fade.top > 0.0f) ? extra->desc.edge_fade.top * dpi : 0.0f,
        (std::isfinite(extra->desc.edge_fade.bottom) && extra->desc.edge_fade.bottom > 0.0f) ? extra->desc.edge_fade.bottom * dpi : 0.0f,
        (std::isfinite(extra->desc.edge_fade.left) && extra->desc.edge_fade.left > 0.0f) ? extra->desc.edge_fade.left * dpi : 0.0f,
        (std::isfinite(extra->desc.edge_fade.right) && extra->desc.edge_fade.right > 0.0f) ? extra->desc.edge_fade.right * dpi : 0.0f,
    };
    if(extra->desc.scroll_fade_px<=0 || !extra->desc.id) return fade;
    const auto* ws=state_map_find(&ctx->active_ui->states,extra->desc.id);
    if(!ws) return fade;
    const double pos=extra->is_virtual_list ? ws->vscroll_pos_y : ws->scroll_pos.y;
    double max=extra->is_virtual_list ? ws->vscroll_max_y : ws->scroll_max.y;
    float max_x=ws->scroll_max.x;
    if((extra->desc.scroll_y || extra->desc.scroll_x) && !extra->is_virtual_list) {
        const LayoutScrollData sc = layout_engine_scroll_data(ctx->active_ui->layout, extra->desc.id);
        if(sc.found) {
            max = std::fmax(0.0f, sc.content_dim.y - sc.viewport_dim.y);
            max_x = std::fmax(0.0f, sc.content_dim.x - sc.viewport_dim.x);
        }
    }
    return {
        std::fmax(fade.x, extra->is_virtual_list || extra->desc.scroll_y ? float(std::fmin(extra->desc.scroll_fade_px,pos))*dpi : 0.0f),
        std::fmax(fade.y, extra->is_virtual_list || extra->desc.scroll_y ? float(std::fmin(extra->desc.scroll_fade_px,std::fmax(0.0,max-pos)))*dpi : 0.0f),
        std::fmax(fade.z, extra->desc.scroll_x ? std::fmin(extra->desc.scroll_fade_px,ws->scroll_pos.x)*dpi : 0.0f),
        std::fmax(fade.w, extra->desc.scroll_x ? std::fmin(extra->desc.scroll_fade_px,std::fmax(0.f,max_x-ws->scroll_pos.x))*dpi : 0.0f),
    };
}

bool append_prim_to_batch(
    Prim** prims,
    uint32_t* prim_count,
    uint32_t* prim_cap,
    uint32_t hard_cap,
    DrawBatch** batches,
    uint32_t* batch_count,
    uint32_t* batch_cap,
    Prim prim,
    SDL_GPUTexture* ext_tex,
    SDL_GPUTexture* ext_tex2,
    bool hole,
    bool additive,
    bool force_new_batch) noexcept
{
    if (*prim_count >= *prim_cap) {
        if (*prim_cap >= hard_cap) return false;
        uint32_t cap = *prim_cap >= kMinPrimCap ? *prim_cap : kMinPrimCap;
        cap = cap > hard_cap / 2u ? hard_cap : cap * 2u;
        Prim* grown = static_cast<Prim*>(std::realloc(*prims, static_cast<size_t>(cap) * sizeof(Prim)));
        if (!grown) return false;
        *prims = grown;
        *prim_cap = cap;
    }

    if (*batch_count == 0u || force_new_batch) {
        if (!ensure_batch_cap_array(batches, batch_cap, *batch_count)) return false;
        (*batches)[(*batch_count)++] = DrawBatch{
            .first_prim = *prim_count,
            .prim_count = 0u,
            .ext_textures = {},
            .ext_count = 0u,
            .is_punch_hole = hole,
            .is_additive = additive,
        };
    }

    DrawBatch* cur = &(*batches)[*batch_count - 1u];
    if ((cur->is_punch_hole != hole || cur->is_additive != additive) && cur->prim_count > 0u) {
        if (!ensure_batch_cap_array(batches, batch_cap, *batch_count)) return false;
        cur = &(*batches)[(*batch_count)++];
        *cur = DrawBatch{
            .first_prim = *prim_count,
            .prim_count = 0u,
            .ext_textures = {},
            .ext_count = 0u,
            .is_punch_hole = hole,
            .is_additive = additive,
        };
    }
    cur->is_punch_hole = hole;
    cur->is_additive = additive;

    const auto find_ext_slot = [](const DrawBatch* b, SDL_GPUTexture* tex) noexcept -> uint16_t {
        if (!tex) return 0u;
        for (uint32_t i = 0u; i < b->ext_count; ++i) {
            if (b->ext_textures[i] == tex) return static_cast<uint16_t>(i + 1u);
        }
        return 0u;
    };

    uint16_t tex_slot = find_ext_slot(cur, ext_tex);
    uint16_t slot2 = find_ext_slot(cur, ext_tex2);
    uint32_t needed = 0u;
    if (ext_tex && tex_slot == 0u) ++needed;
    if (ext_tex2 && ext_tex2 != ext_tex && slot2 == 0u) ++needed;

    if (cur->ext_count + needed > 8u) {
        if (!ensure_batch_cap_array(batches, batch_cap, *batch_count)) return false;
        cur = &(*batches)[(*batch_count)++];
        *cur = DrawBatch{
            .first_prim = *prim_count,
            .prim_count = 0u,
            .ext_textures = {},
            .ext_count = 0u,
            .is_punch_hole = hole,
            .is_additive = additive,
        };
        tex_slot = 0u;
        slot2 = 0u;
    }

    if (ext_tex) {
        if (tex_slot == 0u) {
            cur->ext_textures[cur->ext_count] = ext_tex;
            tex_slot = static_cast<uint16_t>(cur->ext_count + 1u);
            ++cur->ext_count;
        }
    }
    if (ext_tex2) {
        slot2 = find_ext_slot(cur, ext_tex2);
        if (slot2 == 0u) {
            cur->ext_textures[cur->ext_count] = ext_tex2;
            slot2 = static_cast<uint16_t>(cur->ext_count + 1u);
            ++cur->ext_count;
        }
    }

    prim.style.x = (prim.style.x & ~(0xFu << 10)) | ((static_cast<uint32_t>(tex_slot) & 0xFu) << 10);
    if (ext_tex2) {
        prim.payload.y = static_cast<uint32_t>(slot2);
    }
    (*prims)[(*prim_count)++] = prim;
    ++cur->prim_count;
    return true;
}

void push_prim(
    Context* ctx,
    Prim prim,
    SDL_GPUTexture* ext_tex = nullptr,
    float cull_pad_px = 2.0f,
    SDL_GPUTexture* ext_tex2 = nullptr,
    bool force_additive = false) noexcept
{
    if (!ctx || !ctx->prims) return;
    const auto* layer=ctx->active_render_layer;
    if(layer) {
        resolve_layer_chain(ctx,layer,ctx->active_ui->dpi_scale);
        const Vec2 scale=layer->resolved_scale, offset=layer->resolved_offset_px;
        prim.bounds={prim.bounds.x*scale.x+offset.x,prim.bounds.y*scale.y+offset.y,prim.bounds.z*scale.x,prim.bounds.w*scale.y};
        const auto fade=[&](uint32_t c) noexcept {return (c&0xFFFFFFu) | (uint32_t(float(c>>24)*layer->resolved_opacity+.5f)<<24);};
        const PrimKind kind=static_cast<PrimKind>(prim.style.x&15u);
        if(kind==PrimKind::BlurGlass) prim.payload.x=std::bit_cast<uint32_t>(1.f-layer->resolved_opacity);
        else if(kind==PrimKind::BorderBeam) {
            prim.style.y=fade(prim.style.y);prim.style.z=fade(prim.style.z);
            prim.style.w=(prim.style.w&0xFFFF0000u)|pack_f16(unpack_f16_cpu(uint16_t(prim.style.w))*layer->resolved_opacity);
        }
        else prim.style.y=fade(prim.style.y);
        if(kind==PrimKind::Rect || kind==PrimKind::RotatedRect || kind==PrimKind::BezierWire || kind==PrimKind::RingArc || kind==PrimKind::Texture || kind==PrimKind::BlurGlass || kind==PrimKind::CanvasGrid || kind==PrimKind::MeshGrad) prim.style.z=fade(prim.style.z);
        if((kind==PrimKind::SlugGlyph || kind==PrimKind::Glyph) && (prim.style.x & (uint32_t(PrimFlag_LinearGrad)<<4))) {
            prim.style.z=fade(prim.style.z);
            const float x0=unpack_f16_cpu(uint16_t(prim.payload.w))*scale.x;
            const float x1=unpack_f16_cpu(uint16_t(prim.payload.w>>16))*scale.x;
            prim.payload.w=pack_f16x2(clamp_range(x0,-32768.0f,32768.0f),clamp_range(x1,-32768.0f,32768.0f));
        }
        if(kind==PrimKind::BezierWire || kind==PrimKind::RingArc) prim.payload.w=fade(prim.payload.w);
        if(kind==PrimKind::MeshGrad) {prim.payload.x=fade(prim.payload.x);prim.payload.y=fade(prim.payload.y);}
        if(kind==PrimKind::SlugFill) prim.payload.z=fade(prim.payload.z);
        if((kind==PrimKind::Rect || kind==PrimKind::RotatedRect) && (prim.style.x & ((PrimFlag_LinearGrad|PrimFlag_RadialGrad)<<4))) prim.payload.x=fade(prim.payload.x);
        const float metric_scale=.5f*(scale.x+scale.y);
        const auto scaled_pair=[metric_scale](uint32_t pair) noexcept {
            return pack_f16x2(unpack_f16_cpu(uint16_t(pair))*metric_scale,unpack_f16_cpu(uint16_t(pair>>16))*metric_scale);
        };
        if(kind==PrimKind::BezierWire || kind==PrimKind::RingArc || kind==PrimKind::Shadow) prim.style.w=scaled_pair(prim.style.w);
        if(kind==PrimKind::RingArc) prim.payload.z=scaled_pair(prim.payload.z);
        if(kind==PrimKind::BorderBeam) prim.payload.y=scaled_pair(prim.payload.y);
        if(kind==PrimKind::Rect || kind==PrimKind::RotatedRect || kind==PrimKind::Shadow || kind==PrimKind::Texture || kind==PrimKind::BlurGlass || kind==PrimKind::MeshGrad || kind==PrimKind::PunchHole || kind==PrimKind::BorderBeam) {
            const float k=.5f*(scale.x+scale.y);
            const auto scale_pair=[k](uint32_t pair) noexcept {return pack_f16x2(unpack_f16_cpu(uint16_t(pair))*k,unpack_f16_cpu(uint16_t(pair>>16))*k);};
            prim.payload.z=scale_pair(prim.payload.z);prim.payload.w=scale_pair(prim.payload.w);
            if(kind==PrimKind::Rect || kind==PrimKind::BlurGlass || kind==PrimKind::RotatedRect) prim.style.w=(prim.style.w&0xFFFF0000u) | pack_f16(unpack_f16_cpu(uint16_t(prim.style.w))*k);
        }
        cull_pad_px *= std::fmax(scale.x, scale.y);

    }
    // Raster-only snapping preserves logical layout and text positioning.
    const PrimKind snap_kind=static_cast<PrimKind>(prim.style.x&15u);
    const bool snap_rect=snap_kind==PrimKind::Rect && unpack_f16_cpu(static_cast<uint16_t>(prim.style.w>>16))<=1;
    const bool snap_image=(snap_kind==PrimKind::Texture && (prim.style.x & (uint32_t(PrimFlag_TextClip)<<4))==0) || snap_kind==PrimKind::BlurGlass;
    if (ctx->active_ui->pixel_snap && !(layer && layer->resolved_animating) && (snap_rect || snap_image)) {
        const Rect r = snap_pixel_rect({prim.bounds.x, prim.bounds.y, prim.bounds.z, prim.bounds.w});
        prim.bounds = {r.x, r.y, r.w, r.h};
        const float width = unpack_f16_cpu(static_cast<uint16_t>(prim.style.w));
        if (snap_rect && width > 0) prim.style.w = (prim.style.w & 0xFFFF0000u) | pack_f16(std::fmax(1.0f, std::round(width)));
    }
    FilterBlurGroup* fg = ctx->active_filter_group;
    if (fg && ctx->render_clip_depth <= fg->ancestor_clip_depth) {
        prim.style.x &= 0x3FFFu;
        prim.style.x &= ~(uint32_t(PrimFlag_RoundedClip) << 4);
        prim.clip = {0.0f, 0.0f, static_cast<float>(ctx->active_ui->viewport_w), static_cast<float>(ctx->active_ui->viewport_h)};
    } else if (ctx->render_clip_index) {
        const uint32_t index=ctx->render_clip_index;
        prim.style.x=(prim.style.x & 0x3FFFu) | (1u<<18) | ((index&0xFFFu)<<20) | ((index>>12)<<14);
    }
    // CPU-side offscreen clip culling: drop primitives completely outside their active clip rect.
    if (prim.clip.z <= prim.clip.x || prim.clip.w <= prim.clip.y ||
        prim.bounds.z <= 0.0f || prim.bounds.w <= 0.0f)
    {
        return;
    }
    float min_x = prim.bounds.x, min_y = prim.bounds.y;
    float max_x = min_x + prim.bounds.z, max_y = min_y + prim.bounds.w;
    const PrimKind kind = static_cast<PrimKind>(prim.style.x & 15u);
    if(kind==PrimKind::BorderBeam) cull_pad_px=std::fmax(std::fmax(unpack_f16_cpu(uint16_t(prim.payload.y>>16)),1.f)+2,
        unpack_f16_cpu(uint16_t(prim.style.w>>16))*.5f+1);
    if(kind==PrimKind::PunchHole) cull_pad_px=std::fmax(cull_pad_px,unpack_f16_cpu(uint16_t(prim.style.w>>16))*.5f+1);
    if (kind == PrimKind::RotatedRect || (kind == PrimKind::SlugGlyph && prim.payload.z != 0u)) {
        const float angle = kind == PrimKind::RotatedRect
            ? unpack_f16_cpu(static_cast<uint16_t>(prim.style.w >> 16))
            : std::bit_cast<float>(prim.payload.z);
        const float hx = 0.5f * prim.bounds.z, hy = 0.5f * prim.bounds.w;
        const float cx = prim.bounds.x + hx, cy = prim.bounds.y + hy;
        const float ac = std::fabs(std::cos(angle)), as = std::fabs(std::sin(angle));
        const float ex = ac * hx + as * hy, ey = as * hx + ac * hy;
        min_x = cx - ex; min_y = cy - ey;
        max_x = cx + ex; max_y = cy + ey;
    }
    if ((max_x + cull_pad_px <= prim.clip.x) ||
        (min_x - cull_pad_px >= prim.clip.z) ||
        (max_y + cull_pad_px <= prim.clip.y) ||
        (min_y - cull_pad_px >= prim.clip.w))
    {
        return;
    }

    if (ctx->prim_count + ctx->filter_prim_count >= kMaxPrimHardCap) {
        return;
    }

    const bool hole = (prim.style.x & 15u) == uint32_t(PrimKind::PunchHole);
    if (fg) {
        const float bx0 = min_x - cull_pad_px;
        const float by0 = min_y - cull_pad_px;
        const float bx1 = max_x + cull_pad_px;
        const float by1 = max_y + cull_pad_px;
        if (!fg->has_bounds) {
            fg->raw_bounds = {bx0, by0, bx1 - bx0, by1 - by0};
            fg->has_bounds = true;
        } else {
            const float gx0 = std::fmin(fg->raw_bounds.x, bx0);
            const float gy0 = std::fmin(fg->raw_bounds.y, by0);
            const float gx1 = std::fmax(fg->raw_bounds.x + fg->raw_bounds.w, bx1);
            const float gy1 = std::fmax(fg->raw_bounds.y + fg->raw_bounds.h, by1);
            fg->raw_bounds = {gx0, gy0, gx1 - gx0, gy1 - gy0};
        }
        if (layer) {
            fg->blur_px = std::fmax(fg->blur_px, layer->resolved_blur_px);
        }
        const bool child_additive = !hole && (force_additive || (layer && layer->resolved_plus_lighter && !fg->blur_root->resolved_plus_lighter));
        const bool need_new = (ctx->filter_batch_count == fg->first_batch);
        (void)append_prim_to_batch(
            &ctx->filter_prims, &ctx->filter_prim_count, &ctx->filter_prim_cap, kMaxPrimHardCap,
            &ctx->filter_batches, &ctx->filter_batch_count, &ctx->filter_batch_cap,
            prim, ext_tex, ext_tex2, hole, child_additive, need_new);
        return;
    }

    const bool additive = !hole && (force_additive || (layer && layer->resolved_plus_lighter));
    if (!append_prim_to_batch(
            &ctx->prims, &ctx->prim_count, &ctx->max_prims, kMaxPrimHardCap,
            &ctx->batches, &ctx->batch_count, &ctx->batch_cap,
            prim, ext_tex, ext_tex2, hole, additive, false))
    {
        return;
    }

    const uint32_t pkind = prim.style.x & 0xFu;
    if (pkind != static_cast<uint32_t>(PrimKind::Shadow)) {
        if (pkind != static_cast<uint32_t>(PrimKind::BlurGlass)) {
            ctx->layer_has_bg_prims = true;
        }
        if (ctx->blur_split_count > 0u) {
            const float vw = static_cast<float>(ctx->active_ui->viewport_w > 0u ? ctx->active_ui->viewport_w : 1u);
            const float vh = static_cast<float>(ctx->active_ui->viewport_h > 0u ? ctx->active_ui->viewport_h : 1u);
            const float dx0 = std::fmax(min_x, prim.clip.x);
            const float dy0 = std::fmax(min_y, prim.clip.y);
            const float dx1 = std::fmin(max_x, prim.clip.z);
            const float dy1 = std::fmin(max_y, prim.clip.w);
            if (dx1 > dx0 && dy1 > dy0) {
                const uint32_t cx0 = static_cast<uint32_t>(clamp_range(std::floor(dx0 * 16.0f / vw), 0.0f, 15.0f));
                const uint32_t cy0 = static_cast<uint32_t>(clamp_range(std::floor(dy0 * 16.0f / vh), 0.0f, 15.0f));
                const uint32_t cx1 = static_cast<uint32_t>(clamp_range(std::floor((dx1 - 1e-3f) * 16.0f / vw), 0.0f, 15.0f));
                const uint32_t cy1 = static_cast<uint32_t>(clamp_range(std::floor((dy1 - 1e-3f) * 16.0f / vh), 0.0f, 15.0f));
                for (uint32_t ty = cy0; ty <= cy1; ++ty) {
                    for (uint32_t tx = cx0; tx <= cx1; ++tx) {
                        const uint32_t bit = ty * 16u + tx;
                        ctx->blur_dirty_grid[bit >> 6] |= (1ULL << (bit & 63u));
                    }
                }
                ctx->blur_dirty_valid = true;
            }
        }
    }
}

void begin_filter_group(Context* ctx, const VisualLayerNode* blur_root, const ClipEntry& ancestor_clip, uint32_t clip_depth) noexcept {
    if (!ctx || !blur_root || ctx->active_filter_group) return;
    if (ctx->filter_group_count == ctx->filter_group_cap) {
        const uint32_t cap = ctx->filter_group_cap ? ctx->filter_group_cap * 2u : 16u;
        void* p = std::realloc(ctx->filter_groups, size_t(cap) * sizeof(FilterBlurGroup));
        if (!p) return;
        ctx->filter_groups = static_cast<FilterBlurGroup*>(p);
        ctx->filter_group_cap = cap;
    }
    FilterBlurGroup& g = ctx->filter_groups[ctx->filter_group_count++];
    g = FilterBlurGroup{
        .blur_root = blur_root,
        .first_batch = ctx->filter_batch_count,
        .end_batch = ctx->filter_batch_count,
        .slice = 0u,
        .max_level = 3u,
        .blur_px = blur_root->resolved_blur_px,
        .plus_lighter = blur_root->resolved_plus_lighter,
        .raw_bounds = {},
        .has_bounds = false,
        .padded_bounds = {},
        .ancestor_clip = ancestor_clip,
        .ancestor_clip_index = ancestor_clip.indexed ? ancestor_clip.shape : 0u,
        .ancestor_clip_depth = clip_depth,
    };
    ctx->active_filter_group = &g;
}

void finalize_filter_group(Context* ctx) noexcept {
    if (!ctx || !ctx->active_filter_group) return;
    FilterBlurGroup& g = *ctx->active_filter_group;
    ctx->active_filter_group = nullptr;
    g.end_batch = ctx->filter_batch_count;
    if (!g.has_bounds || g.end_batch <= g.first_batch || g.raw_bounds.w <= 0.0f || g.raw_bounds.h <= 0.0f) {
        --ctx->filter_group_count;
        return;
    }
    const float pad = std::ceil(g.blur_px * 2.5f + 8.0f);
    const float vp_w = static_cast<float>(ctx->active_ui->viewport_w);
    const float vp_h = static_cast<float>(ctx->active_ui->viewport_h);
    const float px0 = std::fmax(0.0f, std::floor(g.raw_bounds.x - pad));
    const float py0 = std::fmax(0.0f, std::floor(g.raw_bounds.y - pad));
    const float px1 = std::fmin(vp_w, std::ceil(g.raw_bounds.x + g.raw_bounds.w + pad));
    const float py1 = std::fmin(vp_h, std::ceil(g.raw_bounds.y + g.raw_bounds.h + pad));
    g.padded_bounds = {px0, py0, px1 - px0, py1 - py0};
    const Rect clipped = rect_intersect(g.padded_bounds, g.ancestor_clip.rect);
    if (clipped.w <= 0.0f || clipped.h <= 0.0f) {
        ctx->filter_batch_count = g.first_batch;
        --ctx->filter_group_count;
        return;
    }

    uint32_t chosen_slice = 0u;
    bool found_clean = false;
    float best_overlap = 1.0e30f;
    for (uint32_t s = 0u; s < kMaxFilterSlices; ++s) {
        float overlap_area = 0.0f;
        for (uint32_t i = 0u; i + 1u < ctx->filter_group_count; ++i) {
            const FilterBlurGroup& prev = ctx->filter_groups[i];
            if (prev.slice != s) continue;
            const Rect inter = rect_intersect(g.padded_bounds, prev.padded_bounds);
            overlap_area += inter.w * inter.h;
        }
        if (overlap_area <= 0.0f) {
            chosen_slice = s;
            found_clean = true;
            break;
        }
        if (overlap_area < best_overlap) {
            best_overlap = overlap_area;
            chosen_slice = s;
        }
    }
    (void)found_clean;
    g.slice = chosen_slice;

    const float b = clamp_range(g.blur_px, 0.0f, 64.0f);
    uint32_t lo = 0u, hi = 1u;
    float blend_t = 0.0f;
    if (b <= 2.0f) {
        lo = 0u; hi = 1u; blend_t = b * 0.5f;
    } else if (b <= 5.0f) {
        lo = 1u; hi = 2u; blend_t = (b - 2.0f) / 3.0f;
    } else if (b <= 11.0f) {
        lo = 2u; hi = 3u; blend_t = (b - 5.0f) / 6.0f;
    } else {
        lo = 3u; hi = 4u; blend_t = clamp_range((b - 11.0f) / 13.0f, 0.0f, 1.0f);
    }
    g.max_level = hi;

    const VisualLayerNode* saved_layer = ctx->active_render_layer;
    const uint32_t saved_clip = ctx->render_clip_index;
    ctx->active_render_layer = nullptr;
    ctx->render_clip_index = g.ancestor_clip_index;
    push_prim(
        ctx,
        Prim::make_layer_blur(g.padded_bounds, g.ancestor_clip.rect, blend_t, 0u, g.ancestor_clip.radius),
        filter_tex_sentinel(g.slice, lo),
        0.0f,
        filter_tex_sentinel(g.slice, hi),
        g.plus_lighter);
    ctx->active_render_layer = saved_layer;
    ctx->render_clip_index = saved_clip;
}

inline Corners scale_corners(Corners c, float dpi) noexcept {
    return Corners{ c.tl * dpi, c.tr * dpi, c.br * dpi, c.bl * dpi };
}

bool begin_blur_layer(Context* ctx, Rect box_px, float radius_px) noexcept {
    if (ctx->active_filter_group) return false;
    if (ctx->blur_split_count > 0u) {
        BlurSplit& cur = ctx->blur_splits[ctx->blur_split_count - 1u];
        const float max_r = std::fmax(radius_px, cur.blur_radius_px);
        const bool radius_compatible = std::fabs(radius_px - cur.blur_radius_px) <= std::fmax(2.0f, 0.25f * max_r);
        if (radius_compatible) {
            bool dirty_overlap = false;
            if (ctx->blur_dirty_valid) {
                const float vw = static_cast<float>(ctx->active_ui->viewport_w > 0u ? ctx->active_ui->viewport_w : 1u);
                const float vh = static_cast<float>(ctx->active_ui->viewport_h > 0u ? ctx->active_ui->viewport_h : 1u);
                const uint32_t cx0 = static_cast<uint32_t>(clamp_range(std::floor(box_px.x * 16.0f / vw), 0.0f, 15.0f));
                const uint32_t cy0 = static_cast<uint32_t>(clamp_range(std::floor(box_px.y * 16.0f / vh), 0.0f, 15.0f));
                const uint32_t cx1 = static_cast<uint32_t>(clamp_range(std::floor((box_px.x + box_px.w - 1e-3f) * 16.0f / vw), 0.0f, 15.0f));
                const uint32_t cy1 = static_cast<uint32_t>(clamp_range(std::floor((box_px.y + box_px.h - 1e-3f) * 16.0f / vh), 0.0f, 15.0f));
                bool tile_hit = false;
                for (uint32_t ty = cy0; ty <= cy1 && !tile_hit; ++ty) {
                    for (uint32_t tx = cx0; tx <= cx1; ++tx) {
                        const uint32_t bit = ty * 16u + tx;
                        if ((ctx->blur_dirty_grid[bit >> 6] & (1ULL << (bit & 63u))) != 0ULL) {
                            tile_hit = true;
                            break;
                        }
                    }
                }
                if (tile_hit) {
                    const float bx0 = box_px.x, by0 = box_px.y;
                    const float bx1 = box_px.x + box_px.w, by1 = box_px.y + box_px.h;
                    const uint32_t count = ctx->prim_count - cur.split_prim;
                    if (count > 2048u) {
                        dirty_overlap = true;
                    } else {
                        for (uint32_t pi = cur.split_prim; pi < ctx->prim_count; ++pi) {
                            const Prim& p = ctx->prims[pi];
                            if ((p.style.x & 0xFu) == static_cast<uint32_t>(PrimKind::Shadow)) continue;
                            const float px0 = std::fmax(p.bounds.x, p.clip.x);
                            const float py0 = std::fmax(p.bounds.y, p.clip.y);
                            const float px1 = std::fmin(p.bounds.x + p.bounds.z, p.clip.z);
                            const float py1 = std::fmin(p.bounds.y + p.bounds.w, p.clip.w);
                            if (px1 > bx0 && px0 < bx1 && py1 > by0 && py0 < by1) {
                                dirty_overlap = true;
                                break;
                            }
                        }
                    }
                }
            }
            if (!dirty_overlap) {
                const float x0 = (box_px.x < cur.union_bounds.x) ? box_px.x : cur.union_bounds.x;
                const float y0 = (box_px.y < cur.union_bounds.y) ? box_px.y : cur.union_bounds.y;
                const float rx1 = box_px.x + box_px.w;
                const float cx1 = cur.union_bounds.x + cur.union_bounds.w;
                const float by1 = box_px.y + box_px.h;
                const float cy1 = cur.union_bounds.y + cur.union_bounds.h;
                const float x1 = (rx1 > cx1) ? rx1 : cx1;
                const float y1 = (by1 > cy1) ? by1 : cy1;
                cur.union_bounds = Rect{ x0, y0, x1 - x0, y1 - y0 };
                if (radius_px > cur.blur_radius_px) {
                    cur.blur_radius_px = radius_px;
                }
                return true;
            }
        }
    }
    if (ctx->blur_split_count == ctx->blur_split_cap) {
        if (ctx->blur_split_cap == 256u) return false;
        const uint32_t cap = ctx->blur_split_cap ? ctx->blur_split_cap * 2u : 8u;
        void* p = std::realloc(ctx->blur_splits, size_t(cap) * sizeof(BlurSplit));
        if (!p) return false;
        ctx->blur_splits = static_cast<BlurSplit*>(p); ctx->blur_split_cap = cap;
    }
    if (ctx->batch_count > 0u && ctx->batches[ctx->batch_count - 1u].prim_count > 0u) {
        if (!ensure_batch_cap(ctx)) {
            return false;
        }
        ctx->batches[ctx->batch_count++] = DrawBatch{
            .first_prim = ctx->prim_count,
            .prim_count = 0u,
            .ext_textures = {},
            .ext_count = 0u,
        };
    }
    const uint32_t split_batch = (ctx->batch_count > 0u) ? (ctx->batch_count - 1u) : 0u;
    ctx->blur_splits[ctx->blur_split_count++] = BlurSplit{
        .split_batch = split_batch,
        .split_prim = ctx->prim_count,
        .blur_radius_px = radius_px,
        .union_bounds = box_px,
    };
    ctx->layer_has_bg_prims = false;
    ctx->blur_dirty_valid = false;
    ctx->blur_dirty_grid[0] = ctx->blur_dirty_grid[1] = ctx->blur_dirty_grid[2] = ctx->blur_dirty_grid[3] = 0ULL;
    return true;
}

void emit_canvas_grid_command(
    Context* ctx,
    const CanvasGridExtra* cg,
    Rect box_px,
    const ClipEntry& clip,
    float dpi) noexcept;

void align_border_bounds(Rect* bounds,Corners* corners,float width,BorderAlign align) noexcept {
    const float spread=align==BorderAlign::Outside ? width : align==BorderAlign::Center ? width*.5f : 0;
    if(spread==0) return;
    bounds->x-=spread;bounds->y-=spread;bounds->w+=2*spread;bounds->h+=2*spread;
    // Square corners remain square; rounded outlines preserve the inner contour.
    if(corners->tl>0) corners->tl+=spread;
    if(corners->tr>0) corners->tr+=spread;
    if(corners->br>0) corners->br+=spread;
    if(corners->bl>0) corners->bl+=spread;
}

Prim continuous_prim(Prim prim, bool enabled) noexcept {
    if (enabled) prim.style.x |= uint32_t(PrimFlag_Continuous) << 4;
    return prim;
}

BorderBeamStyle valid_border_beam(BorderBeamStyle s) noexcept {
    if(!std::isfinite(s.width) || s.width<0) s.width=1.5f;
    if(!std::isfinite(s.angle_rad)) s.angle_rad=0;
    if(!std::isfinite(s.sweep_fraction) || s.sweep_fraction<=.02f || s.sweep_fraction>1) s.sweep_fraction=.25f;
    if(!std::isfinite(s.glow_px) || s.glow_px<0) s.glow_px=0;
    if(!std::isfinite(s.speed)) s.speed=0;
    if(!std::isfinite(s.softness) || s.softness<.5f) s.softness=1;
    return s;
}

void emit_border_beam(Context* ctx,Rect bounds,const ClipEntry& clip,Corners radius,
    const BorderBeamStyle& beam,float rim,bool continuous,float scale) noexcept {
    float angle=beam.angle_rad;
    if(beam.beam_active && !ctx->reduced_motion) angle+=ctx->active_ui->elapsed_seconds*beam.speed;
    angle=std::fmod(angle,6.283185307f);
    push_prim(ctx,Prim::make_border_beam(bounds,clip.rect,beam.color,beam.base_color,radius,
        beam.width*scale,angle,beam.sweep_fraction,beam.glow_px*scale,rim,beam.softness,beam.symmetric_dual,continuous,clip.radius));
}

bool has_border_sides(const BoxDesc& d) noexcept {
    return d.border_sides.left > 0.0f || d.border_sides.right > 0.0f ||
           d.border_sides.top > 0.0f || d.border_sides.bottom > 0.0f;
}

bool has_segmented_border(const BoxDesc& d) noexcept {
    return has_border_sides(d) || d.border_dash_px > 0.0f;
}

void emit_segmented_border(Context* ctx, const BoxDesc& d, Rect box, const ClipEntry& parent, float dpi) noexcept {
    const bool sides = has_border_sides(d);
    const float spread = d.border_align == BorderAlign::Outside ? 1.0f :
                         d.border_align == BorderAlign::Center ? 0.5f : 0.0f;
    const Padding w = sides ? Padding{d.border_sides.left * dpi, d.border_sides.right * dpi,
                                     d.border_sides.top * dpi, d.border_sides.bottom * dpi}
                            : Padding::all(d.border_width * dpi);
    const Rect outer{box.x - w.left * spread, box.y - w.top * spread,
                     box.w + (w.left + w.right) * spread, box.h + (w.top + w.bottom) * spread};
    Corners r = scale_corners(d.radius, dpi);
    if (sides) {
        if (r.tl > 0.0f) r.tl += std::fmin(w.top, w.left) * spread;
        if (r.tr > 0.0f) r.tr += std::fmin(w.top, w.right) * spread;
        if (r.br > 0.0f) r.br += std::fmin(w.bottom, w.right) * spread;
        if (r.bl > 0.0f) r.bl += std::fmin(w.bottom, w.left) * spread;
    } else if (spread > 0.0f) {
        if (r.tl > 0.0f) r.tl += w.top * spread;
        if (r.tr > 0.0f) r.tr += w.top * spread;
        if (r.br > 0.0f) r.br += w.top * spread;
        if (r.bl > 0.0f) r.bl += w.top * spread;
    }

    const bool rounded = r.tl > 0 || r.tr > 0 || r.br > 0 || r.bl > 0;
    const bool clip_corner_arcs = !sides && rounded &&
        (w.top < 1.0f || (r.tl > 0 && r.tl <= w.top * 0.5f + 0.5f) ||
         (r.tr > 0 && r.tr <= w.top * 0.5f + 0.5f) ||
         (r.br > 0 && r.br <= w.top * 0.5f + 0.5f) ||
         (r.bl > 0 && r.bl <= w.top * 0.5f + 0.5f));
    const ClipEntry clip = (sides && rounded) || clip_corner_arcs
        ? make_box_clip(ctx, parent, outer, r, d.continuous_corners) : parent;
    const uint32_t saved_clip = ctx->render_clip_index;
    ctx->render_clip_index = (clip.indexed || clip.shape != parent.shape) ? clip.shape : 0u;
    const float dash = d.border_dash_px * dpi;
    const float gap = d.border_gap_px > 0.0f ? d.border_gap_px * dpi : dash;
    const float period = std::fmax(0.5f, dash + gap);
    const float painted = dash > 0.0f && period <= 0.5f ? dash / (dash + gap) * period : dash;
    Rect visible = clip.rect;
    if (const auto* layer = ctx->active_render_layer) {
        const Vec2 scale = layer->resolved_scale, offset = layer->resolved_offset_px;
        visible = {(visible.x - offset.x) / scale.x, (visible.y - offset.y) / scale.y,
                   visible.w / scale.x, visible.h / scale.y};
    }
    float phase = 0.0f;
    const auto segments = [&](float length, float first, float last, auto emit) noexcept {
        if (length <= 0.0f) return;
        if (dash <= 0.0f) {
            emit(0.0f, length);
            return;
        }
        if (last > first && std::isfinite(length)) {
            float start = first - std::fmod(std::fmod(first, period) + phase, period);
            while (start < last) {
                const float a = std::fmax(0.0f, start), b = std::fmin(length, start + painted);
                if (b > a) emit(a, b);
                const float next = start + period;
                if (next <= start) break;
                start = next;
            }
        }
        phase = std::fmod(phase + std::fmod(length, period), period);
    };
    const auto strip = [&](Rect bounds, bool horizontal, bool reverse) noexcept {
        if (bounds.w <= 0.0f || bounds.h <= 0.0f) return;
        const float length = horizontal ? bounds.w : bounds.h;
        const Rect overlap = rect_intersect(bounds, visible);
        const float visible_offset = horizontal ? overlap.x - bounds.x : overlap.y - bounds.y;
        const float visible_len = overlap.w > 0.0f && overlap.h > 0.0f ? (horizontal ? overlap.w : overlap.h) : 0.0f;
        const float start = reverse ? length - visible_offset - visible_len : visible_offset;
        segments(length, start, start + visible_len, [&](float a, float b) noexcept {
            Rect piece = bounds;
            const float offset = reverse ? length - b : a;
            if (horizontal) { piece.x += offset; piece.w = b - a; }
            else { piece.y += offset; piece.h = b - a; }
            push_prim(ctx, Prim::make_rect(piece, clip.rect, d.border_color, {}, {}, 0, 1, clip.radius));
        });
    };
    const float x = outer.x, y = outer.y, right = x + outer.w, bottom = y + outer.h;
    const float top_w = std::fmin(w.top, outer.h);
    const float bottom_w = std::fmin(w.bottom, outer.h - top_w);
    if (sides) {
        strip({x, y, outer.w, top_w}, true, false);
        strip({right - std::fmin(w.right, outer.w), y + top_w, std::fmin(w.right, outer.w), outer.h - top_w - bottom_w}, false, false);
        strip({x, bottom - bottom_w, outer.w, bottom_w}, true, true);
        const float left_w = std::fmin(w.left, std::fmax(0.0f, outer.w - w.right));
        strip({x, y + top_w, left_w, outer.h - top_w - bottom_w}, false, true);
    } else {
        constexpr float quarter = 1.57079632679489661923f;
        const float half = w.top * 0.5f;
        const float max_r = std::fmin(outer.w, outer.h) * 0.5f;
        const auto outer_radius = [&](float radius) noexcept { return std::fmin(radius, max_r); };
        const float tl = outer_radius(r.tl), tr = outer_radius(r.tr);
        const float br = outer_radius(r.br), bl = outer_radius(r.bl);
        const auto arc = [&](Vec2 center, float radius, float angle) noexcept {
            if (radius == 0.0f) return;
            // RingArc's SDF resolves radii and half widths down to half a physical pixel.
            const float corner_half = std::fmax(0.5f, std::fmin(half, radius - 0.5f));
            const float center_r = std::fmax(0.5f, radius - corner_half);
            const float length = quarter * center_r;
            const Rect arc_bounds{center.x - radius, center.y - radius, 2.0f * radius, 2.0f * radius};
            const Rect overlap = rect_intersect(visible, arc_bounds);
            const float last = overlap.w > 0.0f && overlap.h > 0.0f ? length : 0.0f;
            segments(length, 0.0f, last, [&](float a, float b) noexcept {
                if ((b - a) / center_r < 1e-4f) return;
                push_prim(ctx, Prim::make_ring_arc(center, center_r, corner_half, angle + a / center_r,
                    (b - a) / center_r, clip.rect, d.border_color, d.border_color, false, {}, 0, clip.radius));
            });
        };
        strip({x + tl, y, outer.w - tl - tr, top_w}, true, false);
        arc({right - tr, y + tr}, tr, -quarter);
        strip({right - w.right, y + (tr > 0 ? tr : top_w), w.right,
               outer.h - (tr > 0 ? tr : top_w) - (br > 0 ? br : bottom_w)}, false, false);
        arc({right - br, bottom - br}, br, 0.0f);
        strip({x + bl, bottom - bottom_w, outer.w - bl - br, bottom_w}, true, true);
        arc({x + bl, bottom - bl}, bl, quarter);
        strip({x, y + (tl > 0 ? tl : top_w), w.left,
               outer.h - (tl > 0 ? tl : top_w) - (bl > 0 ? bl : bottom_w)}, false, true);
        arc({x + tl, y + tl}, tl, 2.0f * quarter);
    }
    ctx->render_clip_index = saved_clip;
}

void emit_box_background_and_shadow(
    Context* ctx,
    const BoxExtra* extra,
    Rect box_px,
    const ClipEntry& clip,
    float dpi) noexcept
{
    if (!extra) {
        return;
    }
    const BoxDesc& d = extra->desc;
    Corners rad_px = scale_corners(d.radius, dpi);
    const bool has_grad = (d.background_end.a > 0u) || d.radial_gradient;
    const Rect visual_box=layer_rect(ctx,box_px);
    const bool box_visible = rect_intersect(clip.rect,visual_box).w>0 && rect_intersect(clip.rect,visual_box).h>0;
    const bool use_blur =
        (d.backdrop_blur > 0.0f) && !has_grad && box_visible &&
        begin_blur_layer(ctx, visual_box, d.backdrop_blur * dpi);

    if (d.shadow_color.a > 0u && d.shadow_blur > 0.0f) {
        const float blur_px = d.shadow_blur * dpi;
        const Rect shadow_rect = {
            .x = box_px.x + d.shadow_offset.x * dpi,
            .y = box_px.y + d.shadow_offset.y * dpi,
            .w = box_px.w,
            .h = box_px.h,
        };
        push_prim(
            ctx,
            continuous_prim(Prim::make_shadow(
                shadow_rect,
                clip.rect,
                d.shadow_color,
                rad_px,
                blur_px,
                clip.radius, {}, d.shadow_spread * dpi), d.continuous_corners),
            nullptr,
            2.0f * blur_px + 2.0f);
    }

    const bool has_fill = (d.background.a > 0u) || has_grad;
    const bool any_clip = d.clip_children || d.scroll_x || d.scroll_y || d.shrink_width || d.shrink_height;
    const bool draw_border_here = !any_clip && !has_segmented_border(d) && d.border_width > 0.0f && d.border_color.a > 0u;
    if(draw_border_here) align_border_bounds(&box_px,&rad_px,d.border_width*dpi,d.border_align);
    if (has_fill || draw_border_here || use_blur) {
        const Color border_col = draw_border_here ? d.border_color : Color{};
        const float border_w = draw_border_here ? (d.border_width * dpi) : 0.0f;

        if (use_blur) {
            push_prim(
                ctx,
                continuous_prim(Prim::make_blur_glass(
                    box_px,
                    clip.rect,
                    d.background,
                    rad_px,
                    border_col,
                    border_w,
                    0u,
                    clip.radius), d.continuous_corners),
                blur_tex_sentinel());
        } else if (has_grad) {
            push_prim(
                ctx,
                continuous_prim(Prim::make_gradient_rect(
                    box_px,
                    clip.rect,
                    d.background,
                    d.background_end,
                    d.gradient_dir,
                    rad_px,
                    border_col,
                    border_w,
                    clip.radius,
                    d.radial_gradient), d.continuous_corners));
        } else {
            push_prim(
                ctx,
                continuous_prim(Prim::make_rect(
                    box_px,
                    clip.rect,
                    d.background,
                    rad_px,
                    border_col,
                    border_w,
                    1.0f,
                    clip.radius), d.continuous_corners));
        }
    }

    if (extra->is_canvas && extra->canvas_grid && box_visible) {
        const Rect canvas_clip_rect = rect_intersect(clip.rect, layer_rect(ctx,box_px));
        float canvas_clip_r = min4f(rad_px.tl, rad_px.tr, rad_px.br, rad_px.bl);
        if (d.continuous_corners) canvas_clip_r = -canvas_clip_r;
        if (canvas_clip_r == 0.0f &&
            canvas_clip_rect.x == clip.rect.x &&
            canvas_clip_rect.y == clip.rect.y &&
            canvas_clip_rect.w == clip.rect.w &&
            canvas_clip_rect.h == clip.rect.h)
        {
            canvas_clip_r = clip.radius;
        }
        emit_canvas_grid_command(
            ctx,
            extra->canvas_grid,
            box_px,
            ClipEntry{ .rect = canvas_clip_rect, .radius = canvas_clip_r },
            dpi);
    }
}

struct TextWipeParams {
    bool enabled = false;
    Color color_end = {};
    float x0_px = 0.0f;
    float x1_px = 0.0f;
};

void emit_slug_outline_quad(
    Context* ctx,
    uint16_t slot_idx,
    uint32_t glyph_id,
    float phys_px,
    float pen_x_px,
    float baseline_y_px,
    float x_offset_em,
    float y_offset_em,
    const ClipEntry& clip_px,
    Color tint, bool color_layer = false, const TextWipeParams* wipe = nullptr) noexcept
{
    const bool snap_glyph = ctx->active_ui->pixel_snap &&
        !(ctx->active_render_layer && ctx->active_render_layer->resolved_animating);
    if (snap_glyph) {
        baseline_y_px = std::round(baseline_y_px);
    }
    const float scale_px = ctx->atlas.slots[slot_idx].scale_1px * phys_px;
    const bool allow_hinted = ctx->atlas.hinted_small_text && !color_layer && phys_px <= 24.0f && snap_glyph &&
        !(ctx->active_render_layer && (std::fabs(ctx->active_render_layer->resolved_scale.x - 1.0f) > 1e-3f ||
                                       std::fabs(ctx->active_render_layer->resolved_scale.y - 1.0f) > 1e-3f));
    if (allow_hinted) {
        const float raw_x = pen_x_px + x_offset_em * scale_px;
        const float qx = std::round(raw_x * 4.0f);
        const float floor_x = std::floor(qx * 0.25f);
        const uint8_t x_phase = static_cast<uint8_t>(static_cast<int32_t>(qx) - static_cast<int32_t>(floor_x) * 4) & 3u;
        const EmojiBitmapEntry he = get_or_cache_hinted_glyph(ctx, slot_idx, glyph_id, phys_px, x_phase);
        if (he.has_bitmap && he.width_px > 0u && he.height_px > 0u && ctx->atlas.subpixel_w > 0u && ctx->atlas.subpixel_h > 0u) {
            const float inv_aw = 1.0f / static_cast<float>(ctx->atlas.subpixel_w);
            const float inv_ah = 1.0f / static_cast<float>(ctx->atlas.subpixel_h);
            const Vec2 uv0 = { static_cast<float>(he.tile_x) * inv_aw, static_cast<float>(he.tile_y) * inv_ah };
            const Vec2 uv1 = { static_cast<float>(he.tile_x + he.width_px) * inv_aw, static_cast<float>(he.tile_y + he.height_px) * inv_ah };
            const float gx = floor_x + he.bearing_x;
            const float gy = std::round(baseline_y_px - y_offset_em * scale_px) - he.bearing_y_top;
            const float gw = static_cast<float>(he.width_px);
            const float gh = static_cast<float>(he.height_px);
            Prim p = Prim::make_glyph(
                Rect{ gx, gy, gw, gh },
                clip_px.rect,
                tint,
                uv0,
                uv1,
                0u,
                clip_px.radius,
                PrimFlag_StemDarken);
            bool has_wipe_grad = false;
            if (wipe && wipe->enabled) {
                const float x0_rel = wipe->x0_px - gx;
                const float x1_rel = wipe->x1_px - gx;
                if (x1_rel <= -1.0f) {
                    p.style.y = wipe->color_end.pack_rgba8();
                } else if (x0_rel < gw + 1.0f) {
                    p.style.x |= static_cast<uint32_t>(PrimFlag_LinearGrad) << 4;
                    p.style.z = wipe->color_end.pack_rgba8();
                    p.payload.w = pack_f16x2(clamp_range(x0_rel, -32768.0f, 32768.0f),
                                             clamp_range(x1_rel, -32768.0f, 32768.0f));
                    has_wipe_grad = true;
                }
            }
            const bool is_additive = ctx->active_render_layer && ctx->active_render_layer->resolved_plus_lighter;
            const bool use_uber_glyph = (ctx->active_filter_group != nullptr) || has_wipe_grad || is_additive;
            push_prim(ctx, p, use_uber_glyph ? subpixel_tex_sentinel() : nullptr);
            return;
        }
    }
    const SlugGlyphEntry ge = slug_get_glyph(&ctx->atlas, slot_idx, glyph_id);
    if (!ge.has_outline) {
        return;
    }
    const float box_x = pen_x_px + (x_offset_em + ge.min_x_em) * scale_px;
    const float box_y = baseline_y_px - (y_offset_em + ge.max_y_em) * scale_px;
    const float box_w = (ge.max_x_em - ge.min_x_em) * scale_px;
    const float box_h = (ge.max_y_em - ge.min_y_em) * scale_px;
    if (box_w <= 0.0f || box_h <= 0.0f) {
        return;
    }
    Prim p = Prim::make_slug_glyph(
        Rect{ box_x, box_y, box_w, box_h },
        clip_px.rect,
        tint,
        ge.band_offset,
        ge.num_hbands,
        ge.num_vbands,
        clip_px.radius,
        color_layer ? PrimFlag_None : PrimFlag_StemDarken);
    if (wipe && wipe->enabled && !color_layer) {
        const float x0_rel = wipe->x0_px - box_x;
        const float x1_rel = wipe->x1_px - box_x;
        if (x1_rel <= -1.0f) {
            p.style.y = wipe->color_end.pack_rgba8();
        } else if (x0_rel < box_w + 1.0f) {
            p.style.x |= static_cast<uint32_t>(PrimFlag_LinearGrad) << 4;
            p.style.z = wipe->color_end.pack_rgba8();
            p.payload.w = pack_f16x2(clamp_range(x0_rel, -32768.0f, 32768.0f),
                                     clamp_range(x1_rel, -32768.0f, 32768.0f));
        }
    }
    push_prim(ctx, p);
}

void emit_slug_glyph_quad(Context* ctx, uint16_t slot_idx, uint32_t glyph_id, float phys_px, float pen_x_px,
    float baseline_y_px, float x_offset_em, float y_offset_em, const ClipEntry& clip_px, Color tint,
    const TextWipeParams* wipe = nullptr) noexcept {
    const auto& slot=ctx->atlas.slots[slot_idx]; const auto layers=slug_get_colr_layers(slot,glyph_id);
    if (!layers.count) {
        if(slot.has_sbix || slot.has_cbdt) {
            const auto entry=get_or_cache_bitmap_emoji(ctx,slot_idx,glyph_id,phys_px * slot.scale_adjust);
            if(entry.has_bitmap && entry.sprite_id!=kInvalidSprite) {
                const float scale_px=slot.scale_1px*phys_px;
                const float strike_scale=float(slot.upem)*scale_px/float(entry.ppem);
                const Rect quad={pen_x_px+x_offset_em*scale_px+entry.bearing_x*strike_scale,
                    baseline_y_px-y_offset_em*scale_px-entry.bearing_y_top*strike_scale,
                    float(entry.width_px)*strike_scale,float(entry.height_px)*strike_scale};
                Vec2 uv0{},uv1{};
                if(resolve_sprite_uv_rect(ctx,entry.sprite_id,{0,0},{1,1},&uv0,&uv1))
                    push_prim(ctx,Prim::make_texture(quad,clip_px.rect,uv0,uv1,0u,{},Color::rgba8(255,255,255,tint.a),{},0.0f,PrimFlag_Premultiplied,clip_px.radius),nullptr);
                return;
            }
        }
        emit_slug_outline_quad(ctx,slot_idx,glyph_id,phys_px,pen_x_px,baseline_y_px,x_offset_em,y_offset_em,clip_px,tint,false,wipe);return;
    }
    for(uint32_t i=0;i<layers.count;++i) {
        const auto* p=slot.colr_data+slot.colr_layer_off+(layers.first+i)*4u;
        const uint16_t glyph=ot_u16(p),palette=ot_u16(p+2); Color color=tint;
        if(palette!=0xFFFFu) {
            if(palette>=slot.cpal_num_palette_entries)continue;
            const auto* c=slot.cpal_data+slot.cpal_color_records_off+uint32_t(palette)*4u;
            color=Color::rgba8(c[2],c[1],c[0],static_cast<uint8_t>((uint32_t(c[3])*tint.a+127u)/255u));
        }
        emit_slug_outline_quad(ctx,slot_idx,glyph,phys_px,pen_x_px,baseline_y_px,x_offset_em,y_offset_em,clip_px,color,true,nullptr);
    }
}

Str ellipsis_text(Context* ctx, uint16_t font) noexcept {
    hb_codepoint_t glyph=0;
    const uint16_t slot=resolve_font_slot_idx(&ctx->atlas,font);
    if(slot!=kInvalidFont && hb_font_get_nominal_glyph(ctx->atlas.slots[slot].hb_font,0x2026,&glyph))return str_lit("…");
    if (!ctx->atlas.fallbacks_probed) ensure_system_fallbacks_loaded(&ctx->atlas);
    for(uint32_t i=0;i<ctx->atlas.fallback_count;++i)
        if(hb_font_get_nominal_glyph(ctx->atlas.slots[ctx->atlas.fallback_order[i]].hb_font,0x2026,&glyph))return str_lit("…");
    return str_lit("...");
}
// Emit whole visual clusters, reserving room for the trailing marker.
float emit_elided_line(Context* ctx, const char* chars, uint32_t len, uint16_t font, float size, float spacing,
    float start, float baseline, float max_width, const ClipEntry& clip, Color color, float dpi,
    const TextWipeParams* wipe = nullptr) noexcept {
    const Str marker=ellipsis_text(ctx,font);
    const auto dots=shape_utf8_run(ctx,font,marker.chars,marker.length);
    float dots_w=0;for(const auto& g:dots)dots_w+=g.x_advance_1px*size;
    const float available=max_width>dots_w?max_width-dots_w:0;
    const auto glyphs=shape_utf8_run(ctx,font,chars,len);
    float pen=start;
    for(uint32_t i=0;i<glyphs.size();) {
        uint32_t j=i+1;while(j<glyphs.size()&&glyphs[j].cluster==glyphs[i].cluster)++j;
        float advance=0;for(uint32_t k=i;k<j;++k)advance+=glyphs[k].x_advance_1px*size+spacing;
        if(pen-start+advance>available+0.01f)break;
        for(uint32_t k=i;k<j;++k){const auto& g=glyphs[k];emit_slug_glyph_quad(ctx,g.font_slot,g.glyph_id,size*dpi,pen*dpi,baseline*dpi,g.x_offset_em,g.y_offset_em,clip,color,wipe);pen+=g.x_advance_1px*size+spacing;}
        i=j;
    }
    for(const auto& g:dots){emit_slug_glyph_quad(ctx,g.font_slot,g.glyph_id,size*dpi,pen*dpi,baseline*dpi,g.x_offset_em,g.y_offset_em,clip,color,wipe);pen+=g.x_advance_1px*size;}
    return pen;
}

uint32_t utf8_clamp_boundary(const char*, uint32_t, uint32_t) noexcept;

struct SelectionLine {
    const char* base = nullptr;
    uint32_t start = 0, end = 0, id = 0, clay_id = 0;
    Color color = {};
    const RichTextExtra* rich = nullptr;
};
inline uint32_t find_parent_hitbox_for_node(const Context* ctx, uint32_t node_idx) noexcept {
    if (!ctx || !ctx->active_ui->layout || !ctx->active_ui->active_node_to_hitbox) return UINT32_MAX;
    const LayoutEngine* le = ctx->active_ui->layout;
    uint32_t p = node_idx < le->nodes.count ? le->nodes.data[node_idx].parent_idx : kLayoutInvalidNode;
    while (p < ctx->active_ui->active_node_to_hitbox_count) {
        const uint32_t hb = ctx->active_ui->active_node_to_hitbox[p];
        if (hb != UINT32_MAX) return hb;
        if (le->nodes.data[p].is_floating) break;
        p = le->nodes.data[p].parent_idx;
    }
    return UINT32_MAX;
}

inline void push_span_hitbox(Context* ctx, uint32_t span_id, uint32_t layout_elem_id, uint32_t node_idx,
                             Rect rect_logical, const ClipEntry& clip, int16_t z_index, float dpi) noexcept {
    const Rect raw_px = {rect_logical.x * dpi, rect_logical.y * dpi, rect_logical.w * dpi, rect_logical.h * dpi};
    const Rect vis_px = layer_rect(ctx, raw_px);
    const Rect vis_logical = {vis_px.x / dpi, vis_px.y / dpi, vis_px.w / dpi, vis_px.h / dpi};
    const Rect clip_logical = {clip.rect.x / dpi, clip.rect.y / dpi, clip.rect.w / dpi, clip.rect.h / dpi};
    const Rect visible = rect_intersect(vis_logical, clip_logical);
    ctx->active_ui->hitboxes.push(HitboxEntry{
        .id = span_id,
        .layout_elem_id = layout_elem_id,
        .parent_idx = find_parent_hitbox_for_node(ctx, node_idx),
        .layout_rect = rect_logical,
        .visual_rect = vis_logical,
        .visible_rect = visible,
        .clip_rect = clip_logical,
        .clip_radius = std::fabs(clip.radius) / dpi,
        .paint_order = ctx->active_ui->hitboxes.count,
        .z_index = z_index,
        .behavior = HitboxBehavior::Normal,
        .is_text_span = true,
    });
}

SelectionLine selection_line(const LayoutCmd* cmd) noexcept {
    SelectionLine line = {};
    if (!cmd->user_data) return line;
    if (cmd->text_cfg.is_rich) {
        const auto* r = static_cast<const RichTextExtra*>(cmd->user_data);
        line.base = r->full_utf8; line.id = r->selectable_id;
        line.color = r->selection_color; line.clay_id = r->clay_elem_id; line.rich = r;
    } else {
        const auto* r = static_cast<const SelectableTextExtra*>(cmd->user_data);
        line.base = r->full_utf8; line.id = r->selectable_id;
        line.color = r->selection_color; line.clay_id = r->clay_elem_id;
    }
    line.start = static_cast<uint32_t>(cmd->text.chars - line.base);
    line.end = line.start + cmd->text.length;
    return line;
}

// Visit visual cluster edges once; ligatures and RTL runs retain their shaped boundaries.
static uint32_t selection_text_offset(const SelectionLine& line, uint32_t layout_offset) noexcept {
    if (!line.rich || !line.rich->has_icon_markers) return layout_offset;
    const RichSpanInternal* spans = line.rich->spans;
    uint32_t lo = 0, hi = line.rich->span_count - 1u;
    while (lo < hi) {
        const uint32_t middle = lo + (hi - lo) / 2u;
        if (layout_offset >= spans[middle].byte_end) lo = middle + 1;
        else hi = middle;
    }
    const RichSpanInternal& span = spans[lo];
    return span.text_byte_start + std::max(layout_offset, span.layout_text_start()) - span.layout_text_start();
}

void selection_line_geometry(Context* ctx, const LayoutCmd* cmd, const SelectionLine& line,
    uint32_t a, uint32_t b, float pointer_x, float* xa, float* xb, uint32_t* hit,
    const ClipEntry* selection_clip = nullptr, float dpi = 1.0f) noexcept {
    float pen = cmd->bounds.x;
    float best = 1.0e30f;
    bool has_xa = false;
    bool has_xb = false;
    *xa = pen; *xb = pen; *hit = selection_text_offset(line, line.start);
    const auto edge = [&](uint32_t byte, float x) noexcept {
        if (byte == a) { *xa = x; has_xa = true; }
        if (byte == b) { *xb = x; has_xb = true; }
        const float dist = std::abs(x - pointer_x);
        if (dist < best) { best = dist; *hit = byte; }
    };
    const auto segment = [&](uint32_t start, uint32_t end, uint16_t font, float size, float spacing, bool trim,
                             uint32_t marker_bytes) noexcept {
        const uint32_t byte_a = a + marker_bytes, byte_b = b + marker_bytes;
        const auto text_edge = [&](uint32_t byte, float x) noexcept { edge(byte - marker_bytes, x); };
        const uint16_t slot = resolve_font_slot_idx(&ctx->atlas, font);
        uint32_t pos = start;
        uint32_t glyph_index = 0;
        const auto advance_spacing = [&]() noexcept { return (!trim || glyph_index++ > 0u) ? spacing : 0.0f; };
        while (pos < end) {
            if (line.base[pos] == '\t' || slot == kInvalidFont) {
                text_edge(pos, pen);
                const float start_pen = pen;
                const bool tab = line.base[pos] == '\t';
                const char* cp = line.base + pos;
                (void)next_utf8_codepoint(cp, line.base + end);
                pen += (tab ? 4.0f : 1.0f) * (slot == kInvalidFont ? 0.55f : ctx->atlas.slots[slot].space_advance_em) * size + advance_spacing();
                const uint32_t next_pos = static_cast<uint32_t>(cp - line.base);
                if (selection_clip && byte_a < next_pos && byte_b > pos) {
                    push_prim(ctx, Prim::make_rect({start_pen * dpi, cmd->bounds.y * dpi,
                        (pen - start_pen) * dpi, cmd->bounds.h * dpi},
                        selection_clip->rect, line.color, {}, {}, 0, 1, selection_clip->radius));
                }
                pos = next_pos; text_edge(pos, pen); continue;
            }
            uint32_t run_end = pos;
            while (run_end < end && line.base[run_end] != '\t') ++run_end;
            bool rtl = false;
            const auto glyphs = shape_utf8_run(ctx, slot, line.base + pos, run_end - pos, &rtl);
            for (uint32_t i = 0; i < glyphs.size();) {
                const uint32_t c0 = glyphs[i].cluster;
                uint32_t j = i + 1u;
                while (j < glyphs.size() && glyphs[j].cluster == c0) ++j;
                rtl = glyphs[i].is_rtl != 0;
                const uint32_t c1 = glyphs[i].cluster_end;
                const float cluster_x = pen;
                text_edge(pos + (rtl ? c1 : c0), pen);
                for (uint32_t k = i; k < j; ++k) pen += glyphs[k].x_advance_1px * size + advance_spacing();
                const auto selection_stop = [&](uint32_t stop, float* x, bool* has_x) noexcept {
                    if (stop > pos+c0 && stop < pos+c1) {
                        const uint32_t count = count_utf8_cps(line.base+pos+c0,c1-c0);
                        const uint32_t before = count_utf8_cps(line.base+pos+c0,stop-pos-c0);
                        const float fraction = static_cast<float>(before)/static_cast<float>(count);
                        *x = cluster_x+(pen-cluster_x)*(rtl ? 1.0f-fraction : fraction);
                        *has_x = true;
                    }
                };
                if (selection_clip && c1 > c0 && byte_a < pos+c1 && byte_b > pos+c0) {
                    const uint32_t lo = std::max(byte_a, pos+c0), hi = std::min(byte_b, pos+c1);
                    const uint32_t count = count_utf8_cps(line.base+pos+c0,c1-c0);
                    if (count > 0u) {
                        const float f0 = static_cast<float>(count_utf8_cps(line.base+pos+c0,lo-pos-c0))/static_cast<float>(count);
                        const float f1 = static_cast<float>(count_utf8_cps(line.base+pos+c0,hi-pos-c0))/static_cast<float>(count);
                        const float x = cluster_x+(pen-cluster_x)*(rtl ? 1.0f-f1 : f0);
                        push_prim(ctx,Prim::make_rect({x*dpi,cmd->bounds.y*dpi,(pen-cluster_x)*(f1-f0)*dpi,cmd->bounds.h*dpi},
                            selection_clip->rect,line.color,{}, {},0,1,selection_clip->radius));
                    }
                }
                selection_stop(byte_a,xa,&has_xa);
                selection_stop(byte_b,xb,&has_xb);
                text_edge(pos + (rtl ? c0 : c1), pen);
                i = j;
            }
            pos = run_end;
        }
    };
    if (line.rich) {
        for (uint32_t i = 0; i < line.rich->span_count; ++i) {
            const auto& sp = line.rich->spans[i];
            const uint32_t lo = sp.byte_start > line.start ? sp.byte_start : line.start;
            const uint32_t hi = sp.byte_end < line.end ? sp.byte_end : line.end;
            if (hi <= lo) continue;
            if (lo == sp.byte_start) {
                if (sp.icon_advance > 0.0f) edge(sp.text_byte_start, pen);
                pen += sp.padding_x + sp.icon_advance;
            }
            const uint32_t start = std::max(lo, sp.layout_text_start());
            if (start < hi)
                segment(start, hi, sp.font_id, sp.font_size_px, sp.letter_spacing, hi == line.end,
                        sp.layout_text_start() - sp.text_byte_start);
            if (hi == sp.byte_end) pen += sp.padding_x;
        }
    } else {
        const auto& td = cmd->text_cfg;
        segment(line.start, line.end, td.font_id, td.font_size_px, td.letter_spacing, false, 0u);
    }
    const uint32_t text_end = selection_text_offset(line, line.end);
    if (!has_xa && a >= text_end) *xa = pen;
    if (!has_xb && b >= text_end) *xb = pen;
}

void emit_selection_line(Context* ctx, const LayoutCmd* cmd, const ClipEntry& clip, float dpi) noexcept {
    const SelectionLine line = selection_line(cmd);
    if (!line.id) return;
    if (reserve_span_hits(&ctx->active_ui->span_hits, &ctx->active_ui->span_hit_cap, ctx->active_ui->span_hit_count, 256u)) {
        ctx->active_ui->span_hits[ctx->active_ui->span_hit_count++] = SpanHitRegion{
            .id = line.id, .clay_elem_id = line.clay_id,
            .rect = cmd->bounds,
            .z_index = cmd->z_index, .clip_rect = {clip.rect.x/dpi,clip.rect.y/dpi,clip.rect.w/dpi,clip.rect.h/dpi}, .has_clip = true };
    }
    push_span_hitbox(ctx, line.id, line.clay_id, cmd->node_idx, cmd->bounds, clip, cmd->z_index, dpi);
    const auto* ws = state_map_find(&ctx->active_ui->states, line.id);
    if (!ws) return;
    const uint32_t a = ws->edit_cursor < ws->edit_sel_anchor ? ws->edit_cursor : ws->edit_sel_anchor;
    const uint32_t b = ws->edit_cursor > ws->edit_sel_anchor ? ws->edit_cursor : ws->edit_sel_anchor;
    if (a == b || a >= selection_text_offset(line, line.end) ||
        b <= selection_text_offset(line, line.start)) return;
    float xa, xb; uint32_t hit;
    const size_t mark = ctx->frame_arena.mark();
    selection_line_geometry(ctx, cmd, line, a, b, 0, &xa, &xb, &hit, &clip, dpi);
    ctx->frame_arena.rewind(mark);
}

void emit_vector_icon_quad(Context* ctx, uint16_t icon_id, Rect target_px, Color color,
                           const ClipEntry& clip, float angle_rad = 0.0f,
                           Color secondary_color = Color::theme_auto(),
                           float opacity = 1.0f, bool allow_snap = true) noexcept;

void emit_rich_text_command(
    Context* ctx,
    const LayoutCmd* cmd,
    const RichTextExtra* rich,
    const ClipEntry& clip,
    float dpi) noexcept
{
    ClipEntry effective_clip = clip;
    if(cmd->text_truncated) {
        const float width=cmd->text_max_width>0?cmd->text_max_width:cmd->bounds.w;
        const auto* layer=ctx->active_render_layer;
        const float sx=layer ? layer->resolved_scale.x : 1.f;
        const float ox=layer ? layer->resolved_offset_px.x : 0.f;
        effective_clip.rect=rect_intersect(clip.rect,Rect{cmd->bounds.x*dpi*sx+ox,clip.rect.y,width*dpi*sx,clip.rect.h});
    }
    if (!rich->has_icon_markers) emit_selection_line(ctx, cmd, effective_clip, dpi);
    const char* chars = cmd->text.chars;
    const int32_t len = static_cast<int32_t>(cmd->text.length);
    if (!rich || !rich->full_utf8 || !rich->spans || rich->span_count == 0u || !chars || len <= 0) {
        return;
    }
    const char* base = rich->full_utf8;
    const char* base_end = base + rich->total_bytes;
    if (chars < base || (chars + len) > base_end) {
        return;
    }

    const uint32_t line_start = static_cast<uint32_t>(chars - base);
    const uint32_t line_end = line_start + static_cast<uint32_t>(len);
    const float content_h = rich->max_ascent_px + rich->max_descent_px;
    const float top_lead = (content_h > 0.0f) ? (0.5f * (cmd->bounds.h - content_h)) : 0.0f;
    const float line_top_px = cmd->bounds.y * dpi;
    const float line_bot_px = (cmd->bounds.y + cmd->bounds.h) * dpi;
    const bool snap_text = ctx->active_ui->pixel_snap &&
        !(ctx->active_render_layer && ctx->active_render_layer->resolved_animating);
    const float raw_baseline_y = (cmd->bounds.y + top_lead + rich->max_ascent_px) * dpi;
    const float baseline_y_px = snap_text ? std::round(raw_baseline_y) : raw_baseline_y;

    struct RichLineSeg {
        const RichSpanInternal* sp = nullptr;
        float x0_px = 0.0f;
        float x1_px = 0.0f;
        float text_x0_px = 0.0f;
        float text_w_px = 0.0f;
        bool has_left_edge = false;
        bool has_right_edge = false;
    };
    struct RichLineChunk {
        const RichSpanInternal* sp = nullptr;
        std::span<const ShapedGlyph> glyphs = {};
        bool trim_last_spacing = false;
        bool first_glyph_has_ls = false;
        float start_x_px = 0.0f;
        float span_text_x0_px = 0.0f;
    };

    const size_t arena_mark = ctx->frame_arena.mark();
    constexpr uint32_t kStackSegs = 32u;
    constexpr uint32_t kStackChunks = 64u;
    RichLineSeg stack_segs[kStackSegs];
    RichLineChunk stack_chunks[kStackChunks];
    RichLineSeg* segs = (rich->span_count <= kStackSegs)
        ? stack_segs
        : ctx->frame_arena.alloc_uninit<RichLineSeg>(rich->span_count);
    if (!segs) {
        ctx->frame_arena.rewind(arena_mark);
        return;
    }
    RichLineChunk* chunks = stack_chunks;
    uint32_t chunk_cap = kStackChunks;
    uint32_t seg_count = 0u;
    uint32_t chunk_count = 0u;

    const auto push_chunk = [&](const RichLineChunk& ch) noexcept {
        if (chunk_count >= chunk_cap) {
            const uint32_t nc = chunk_cap * 2u;
            RichLineChunk* g = ctx->frame_arena.alloc_uninit<RichLineChunk>(nc);
            if (!g) return;
            std::memcpy(g, chunks, size_t(chunk_count) * sizeof(RichLineChunk));
            chunks = g;
            chunk_cap = nc;
        }
        chunks[chunk_count++] = ch;
    };

    const bool can_shape = ctx->atlas.font_valid;
    const Rect clip_logical = {
        effective_clip.rect.x / dpi,
        effective_clip.rect.y / dpi,
        effective_clip.rect.w / dpi,
        effective_clip.rect.h / dpi,
    };

    float pen_logical = cmd->bounds.x;
    for (uint32_t si = 0u; si < rich->span_count; ++si) {
        const RichSpanInternal& sp = rich->spans[si];
        if (sp.byte_end <= line_start) {
            continue;
        }
        if (sp.byte_start >= line_end) {
            break;
        }
        const uint32_t ov0 = (line_start > sp.byte_start) ? line_start : sp.byte_start;
        const uint32_t ov1 = (line_end < sp.byte_end) ? line_end : sp.byte_end;
        const bool has_left = (ov0 == sp.byte_start);
        const bool has_right = (ov1 == sp.byte_end);
        const bool is_last_on_line = (ov1 == line_end);

        const float seg_x0 = pen_logical;
        if (has_left) {
            pen_logical += sp.padding_x;
        }
        if (has_left && sp.icon_size > 0.0f) {
            push_chunk(RichLineChunk{.sp = &sp, .start_x_px = pen_logical * dpi, .span_text_x0_px = pen_logical * dpi});
            pen_logical += sp.icon_advance;
        }
        const float text_x0 = pen_logical;

        const uint16_t slot_idx = can_shape ? resolve_font_slot_idx(&ctx->atlas, sp.font_id) : kInvalidFont;
        const bool has_slot = (slot_idx != kInvalidFont);
        uint32_t glyph_idx = 0u;
        const auto next_spacing = [&]() noexcept -> float {
            const float s = (!is_last_on_line || glyph_idx > 0u) ? sp.letter_spacing : 0.0f;
            ++glyph_idx;
            return s;
        };

        const uint32_t text_start = std::max(ov0, sp.layout_text_start());
        if (!has_slot) {
            const float cp_w = sp.font_size_px * 0.55f;
            const char* p = base + text_start;
            const char* end = base + ov1;
            while (p < end) {
                const uint32_t cp = next_utf8_codepoint(p, end);
                pen_logical += ((cp == '\t') ? 4.0f * cp_w : cp_w) + next_spacing();
            }
        } else {
            const FontSlot& primary = ctx->atlas.slots[slot_idx];
            uint32_t pos = text_start;
            while (pos < ov1) {
                if (base[pos] == '\t') {
                    uint32_t t_end = pos + 1u;
                    while (t_end < ov1 && base[t_end] == '\t') {
                        ++t_end;
                    }
                    const uint32_t tc = t_end - pos;
                    for (uint32_t k = 0u; k < tc; ++k) {
                        pen_logical += 4.0f * primary.space_advance_em * sp.font_size_px + next_spacing();
                    }
                    pos = t_end;
                    continue;
                }
                uint32_t run_end = pos;
                while (run_end < ov1 && base[run_end] != '\t') {
                    ++run_end;
                }
                const float chunk_start_x = pen_logical;
                const bool first_has_ls = (!is_last_on_line || glyph_idx > 0u);
                const std::span<const ShapedGlyph> glyphs =
                    shape_utf8_run(ctx, slot_idx, base + pos, run_end - pos);
                for (const ShapedGlyph& g : glyphs) {
                    pen_logical += g.x_advance_1px * sp.font_size_px + next_spacing();
                }
                if (!glyphs.empty()) {
                    push_chunk(RichLineChunk{
                        .sp = &sp,
                        .glyphs = glyphs,
                        .trim_last_spacing = is_last_on_line,
                        .first_glyph_has_ls = first_has_ls,
                        .start_x_px = chunk_start_x * dpi,
                        .span_text_x0_px = text_x0 * dpi,
                    });
                }
                pos = run_end;
            }
        }

        const float text_x1 = pen_logical;
        if (has_right) {
            pen_logical += sp.padding_x;
        }
        const float seg_x1 = pen_logical;

        segs[seg_count++] = RichLineSeg{
            .sp = &sp,
            .x0_px = seg_x0 * dpi,
            .x1_px = seg_x1 * dpi,
            .text_x0_px = text_x0 * dpi,
            .text_w_px = (text_x1 > text_x0) ? ((text_x1 - text_x0) * dpi) : 0.0f,
            .has_left_edge = has_left,
            .has_right_edge = has_right,
        };

    }

    float visible_end_px=cmd->bounds.x*dpi;
    const RichSpanInternal* ellipsis_span=&rich->spans[0];
    const float full_limit_px=(cmd->bounds.x+(cmd->text_max_width>0?cmd->text_max_width:cmd->bounds.w))*dpi;
    float text_limit_px=full_limit_px;
    if(cmd->text_truncated) {
        bool stop=false;
        for(uint32_t ci=0;ci<chunk_count && !stop;++ci) {
            const auto& ch=chunks[ci];const auto& sp=*ch.sp;
            const Str dots=ellipsis_text(ctx,sp.font_id);float reserved=0;
            for(const auto& g:shape_utf8_run(ctx,sp.font_id,dots.chars,dots.length)) reserved+=g.x_advance_1px*sp.font_size_px*dpi;
            float pen=ch.start_x_px;
            if (ch.glyphs.empty()) {
                const float end = pen + sp.icon_size * dpi;
                if (end + reserved > full_limit_px + 0.01f) break;
                visible_end_px = end;
                ellipsis_span = &sp;
                continue;
            }
            for(uint32_t gi=0;gi<ch.glyphs.size();) {
                const uint32_t first=gi;float next=pen;
                do {
                    if(ch.trim_last_spacing && (ch.first_glyph_has_ls || gi>0))next+=sp.letter_spacing*dpi;
                    next+=ch.glyphs[gi].x_advance_1px*sp.font_size_px*dpi+(ch.trim_last_spacing?0:sp.letter_spacing*dpi);++gi;
                } while(gi<ch.glyphs.size() && ch.glyphs[gi].cluster==ch.glyphs[first].cluster);
                if(next+reserved>full_limit_px+.01f) {stop=true;break;}
                pen=next;visible_end_px=pen;ellipsis_span=&sp;
            }
        }
        text_limit_px=visible_end_px;
    }
    // Pass 1: Emit inline pill backgrounds + borders BEFORE all glyphs on this line.
    for (uint32_t i = 0u; i < seg_count; ++i) {
        const RichLineSeg& sg = segs[i];
        const RichSpanInternal* sp = sg.sp;
        const Color background = sp->id && sp->hover_background.a && hovered(sp->id)
            ? sp->hover_background : sp->background;
        if (background.a == 0u && sp->border_color.a == 0u) {
            continue;
        }
        const float w_px = (cmd->text_truncated ? std::fmin(sg.x1_px,visible_end_px) : sg.x1_px) - sg.x0_px;
        if (w_px <= 0.0f) {
            continue;
        }
        const uint16_t slot_idx = can_shape ? resolve_font_slot_idx(&ctx->atlas, sp->font_id) : kInvalidFont;
        const float sp_asc_px = (slot_idx != kInvalidFont)
            ? (ctx->atlas.slots[slot_idx].ascent_em * sp->font_size_px * dpi)
            : (sp->font_size_px * 0.8f * dpi);
        const float sp_desc_px = (slot_idx != kInvalidFont)
            ? (std::abs(ctx->atlas.slots[slot_idx].descent_em) * sp->font_size_px * dpi)
            : (sp->font_size_px * 0.2f * dpi);
        const float pad_y_px = 0.5f * dpi;
        float top_px = baseline_y_px - sp_asc_px - pad_y_px;
        float bot_px = baseline_y_px + sp_desc_px + pad_y_px;
        if (top_px < line_top_px) top_px = line_top_px;
        if (bot_px > line_bot_px) bot_px = line_bot_px;
        if (sg.has_left_edge && sp->icon_size > 0.0f) {
            const float center_y = baseline_y_px - 0.36f * (sp->font_size_px * dpi);
            top_px = std::fmin(top_px, center_y - sp->icon_size * dpi * 0.5f - pad_y_px);
            bot_px = std::fmax(bot_px, center_y + sp->icon_size * dpi * 0.5f + pad_y_px);
        }
        top_px -= sp->padding_y * dpi;
        bot_px += sp->padding_y * dpi;
        const float h_px = (bot_px > top_px) ? (bot_px - top_px) : (line_bot_px - line_top_px);
        const float r_full = sp->bg_radius * dpi;
        const float r_join = (r_full < 2.0f * dpi) ? r_full : (2.0f * dpi);
        const float rl = sg.has_left_edge ? r_full : r_join;
        const float rr = sg.has_right_edge ? r_full : r_join;
        const float bw = (sp->border_color.a > 0u) ? (1.0f * dpi) : 0.0f;
        push_prim(
            ctx,
            Prim::make_rect(
                Rect{ sg.x0_px, top_px, w_px, h_px },
                effective_clip.rect,
                background,
                Corners{ rl, rr, rr, rl },
                sp->border_color,
                bw,
                1.0f,
                effective_clip.radius));
    }

    if (rich->has_icon_markers) emit_selection_line(ctx, cmd, effective_clip, dpi);
    for (uint32_t i = 0u; i < seg_count; ++i) {
        const RichLineSeg& sg = segs[i];
        const RichSpanInternal& sp = *sg.sp;
        if (sp.id != 0u && sg.x1_px > sg.x0_px) {
            const Rect span_rect = {sg.x0_px / dpi, cmd->bounds.y - sp.padding_y, (sg.x1_px - sg.x0_px) / dpi,
                                    cmd->bounds.h + sp.padding_y * 2.0f};
            if (reserve_span_hits(&ctx->active_ui->span_hits, &ctx->active_ui->span_hit_cap, ctx->active_ui->span_hit_count, 256u)) {
                ctx->active_ui->span_hits[ctx->active_ui->span_hit_count++] = SpanHitRegion{
                    .id = sp.id,
                    .clay_elem_id = rich->clay_elem_id,
                    .rect = span_rect,
                    .z_index = cmd->z_index,
                    .clip_rect = clip_logical,
                    .has_clip = true,
                };
            }
            push_span_hitbox(ctx, sp.id, rich->clay_elem_id, cmd->node_idx, span_rect, effective_clip, cmd->z_index, dpi);
        }
    }

    if(cmd->text_truncated) visible_end_px=cmd->bounds.x*dpi;
    bool elided=false;
    // Pass 2: Emit Slug glyphs, underline, and strikethrough decorations.
    for (uint32_t i = 0u; i < chunk_count; ++i) {
        const RichLineChunk& ch = chunks[i];
        const RichSpanInternal* sp = ch.sp;
        const float phys_px = sp->font_size_px * dpi;
        const float ls_px = sp->letter_spacing * dpi;
        float gx = ch.start_x_px;
        TextWipeParams wipe = {};
        if (sp->has_wipe) {
            const float center_px = ch.span_text_x0_px + sp->wipe_x * dpi;
            const float half_f = 0.5f * sp->wipe_feather_px * dpi;
            wipe = {true, sp->color_end, center_px - half_f, center_px + half_f};
        }
        if (ch.glyphs.empty()) {
            const float size = sp->icon_size * dpi;
            if (cmd->text_truncated && (elided || gx + size > text_limit_px + 0.01f)) break;
            const float icon_y = baseline_y_px - 0.36f * phys_px - size * 0.5f;
            emit_vector_icon_quad(ctx, sp->icon_id, {gx, icon_y, size, size},
                                  sp->icon_color, effective_clip);
            visible_end_px = gx + size;
            ellipsis_span = sp;
            continue;
        }
        for (uint32_t gi = 0u; gi < static_cast<uint32_t>(ch.glyphs.size()); ++gi) {
            const ShapedGlyph& g = ch.glyphs[gi];
            if(cmd->text_truncated && (gi==0 || ch.glyphs[gi-1].cluster!=g.cluster)) {
                float advance=0;
                for(uint32_t j=gi;j<ch.glyphs.size()&&ch.glyphs[j].cluster==g.cluster;++j)advance+=ch.glyphs[j].x_advance_1px*phys_px+ls_px;
                if(elided || gx+advance>text_limit_px+0.01f){elided=true;break;}
            }
            if(cmd->text_truncated && elided)break;
            ellipsis_span=sp;
            if (ch.trim_last_spacing && (ch.first_glyph_has_ls || gi > 0u)) {
                gx += ls_px;
            }
            emit_slug_glyph_quad(
                ctx,
                g.font_slot,
                g.glyph_id,
                phys_px,
                gx,
                baseline_y_px,
                g.x_offset_em,
                g.y_offset_em,
                effective_clip,
                sp->color,
                wipe.enabled ? &wipe : nullptr);
            gx += g.x_advance_1px * phys_px + (ch.trim_last_spacing ? 0.0f : ls_px);
            visible_end_px=gx;
        }
    }

    if(cmd->text_truncated) {
        const Str dots=ellipsis_text(ctx,ellipsis_span->font_id);
        float pen=visible_end_px;
        TextWipeParams wipe = {};
        if (ellipsis_span->has_wipe) {
            float dots_span_x0 = cmd->bounds.x * dpi;
            for (uint32_t i = 0u; i < seg_count; ++i) {
                if (segs[i].sp == ellipsis_span) { dots_span_x0 = segs[i].text_x0_px; break; }
            }
            const float center_px = dots_span_x0 + ellipsis_span->wipe_x * dpi;
            const float half_f = 0.5f * ellipsis_span->wipe_feather_px * dpi;
            wipe = {true, ellipsis_span->color_end, center_px - half_f, center_px + half_f};
        }
        for(const auto& g:shape_utf8_run(ctx,ellipsis_span->font_id,dots.chars,dots.length)) {
            emit_slug_glyph_quad(ctx,g.font_slot,g.glyph_id,ellipsis_span->font_size_px*dpi,pen,baseline_y_px,g.x_offset_em,g.y_offset_em,effective_clip,ellipsis_span->color,wipe.enabled ? &wipe : nullptr);
            pen+=g.x_advance_1px*ellipsis_span->font_size_px*dpi;
        }
    }
    for (uint32_t i = 0u; i < seg_count; ++i) {
        RichLineSeg sg = segs[i];
        if(cmd->text_truncated && sg.text_x0_px+sg.text_w_px>visible_end_px)sg.text_w_px=visible_end_px-sg.text_x0_px;
        const RichSpanInternal* sp = sg.sp;
        if (sg.text_w_px <= 0.0f) {
            continue;
        }
        const float thick_px = (sp->font_size_px * 0.075f * dpi > 1.0f)
            ? std::round(sp->font_size_px * 0.075f * dpi)
            : 1.0f;
        if (sp->underline) {
            const float uy = baseline_y_px + ((sp->font_size_px * 0.12f * dpi > 1.0f * dpi)
                ? (sp->font_size_px * 0.12f * dpi)
                : (1.0f * dpi));
            push_prim(
                ctx,
                Prim::make_rect(
                    Rect{ sg.text_x0_px, uy, sg.text_w_px, thick_px },
                    effective_clip.rect,
                    sp->color));
        }
        if (sp->strikethrough) {
            const float sy = baseline_y_px - sp->font_size_px * 0.28f * dpi;
            push_prim(
                ctx,
                Prim::make_rect(
                    Rect{ sg.text_x0_px, sy, sg.text_w_px, thick_px },
                    effective_clip.rect,
                    sp->color));
        }
    }

    ctx->frame_arena.rewind(arena_mark);
}

void emit_text_command(
    Context* ctx,
    const LayoutCmd* cmd,
    const ClipEntry& clip,
    float dpi) noexcept
{
    if (cmd->text_cfg.is_rich && cmd->user_data != nullptr) {
        emit_rich_text_command(ctx, cmd, static_cast<const RichTextExtra*>(cmd->user_data), clip, dpi);
        return;
    }

    emit_selection_line(ctx, cmd, clip, dpi);
    const LayoutTextConfig& td = cmd->text_cfg;
    const char* chars = cmd->text.chars;
    const int32_t len = static_cast<int32_t>(cmd->text.length);
    if (!chars || len <= 0 || !ctx->atlas.font_valid) {
        return;
    }

    const uint16_t primary_slot = resolve_font_slot_idx(&ctx->atlas, td.font_id);
    if (primary_slot == kInvalidFont) {
        return;
    }
    const FontSlot& primary = ctx->atlas.slots[primary_slot];

    const float logical_font_px = td.font_size_px > 0.0f ? td.font_size_px : 16.0f;
    const float phys_px = logical_font_px * dpi;
    const float ascender_px = primary.ascent_em * phys_px;
    const float line_top_px = cmd->bounds.y * dpi;
    const bool snap_text = ctx->active_ui->pixel_snap &&
        !(ctx->active_render_layer && ctx->active_render_layer->resolved_animating);
    const float baseline_y = snap_text ? std::round(line_top_px + ascender_px) : (line_top_px + ascender_px);
    const float letter_spacing_px = td.letter_spacing * dpi;

    const Color tint = td.text_color;

    if(cmd->text_truncated) {
        ClipEntry local_clip=clip;
        const float width=cmd->text_max_width>0?cmd->text_max_width:cmd->bounds.w;
        const auto* layer=ctx->active_render_layer;
        const float sx=layer ? layer->resolved_scale.x : 1.f;
        const float ox=layer ? layer->resolved_offset_px.x : 0.f;
        local_clip.rect=rect_intersect(clip.rect,Rect{cmd->bounds.x*dpi*sx+ox,clip.rect.y,width*dpi*sx,clip.rect.h});
        emit_elided_line(ctx,chars,static_cast<uint32_t>(len),primary_slot,logical_font_px,td.letter_spacing,
            cmd->bounds.x,baseline_y/dpi,width,local_clip,tint,dpi);return;
    }
    float pen_x = snap_text ? std::round(cmd->bounds.x * dpi) : (cmd->bounds.x * dpi);
    const uint32_t ulen = static_cast<uint32_t>(len);
    uint32_t pos = 0u;
    while (pos < ulen) {
        if (chars[pos] == '\t') {
            pen_x += 4.0f * primary.space_advance_em * phys_px + letter_spacing_px;
            ++pos;
            continue;
        }
        uint32_t run_end = pos;
        while (run_end < ulen && chars[run_end] != '\t') {
            ++run_end;
        }
        const std::span<const ShapedGlyph> glyphs = shape_utf8_run(ctx, primary_slot, chars + pos, run_end - pos);
        for (const ShapedGlyph& g : glyphs) {
            emit_slug_glyph_quad(
                ctx,
                g.font_slot,
                g.glyph_id,
                phys_px,
                pen_x,
                baseline_y,
                g.x_offset_em,
                g.y_offset_em,
                clip,
                tint);
            pen_x += g.x_advance_1px * phys_px + letter_spacing_px;
        }
        pos = run_end;
    }
}

WidgetState* find_scrollbar_owner(Context* ctx, uint32_t thumb_active_id) noexcept {
    if (!ctx || !ctx->active_ui->states.entries || thumb_active_id == 0u) {
        return nullptr;
    }
    for (uint32_t i = 0u; i < ctx->active_ui->states.capacity; ++i) {
        WidgetState& ws = ctx->active_ui->states.entries[i];
        if (ws.key != 0u && (((ws.is_scroll_y || ws.is_virtual_list || ws.is_multiline_input) && id_index(ws.key, kScrollbarYTag) == thumb_active_id) || (ws.is_scroll_x && id_index(ws.key,kScrollbarXTag)==thumb_active_id))) {
            return &ws;
        }
    }
    return nullptr;
}

void apply_scrollbar_drag(WidgetState* ws, float pointer_y, float grab_offset, bool horizontal = false) noexcept {
    if (!ws) {
        return;
    }
    const ScrollbarGeom sb = horizontal ? compute_hscrollbar_geom(ws) : compute_vscrollbar_geom(ws);
    if (sb.visible && sb.travel_h > 0.0f) {
        const float rel = (pointer_y - grab_offset) - (horizontal ? sb.track.x : sb.track.y);
        const float ratio = clamp_range(rel / sb.travel_h, 0.0f, 1.0f);
        if(horizontal) {
            ws->scroll_pos.x=ratio*ws->scroll_max.x;ws->scroll_vel.x=0;
        } else if(ws->is_multiline_input) {
            ws->edit_scroll_y=ratio*ws->edit_max_scroll_y;ws->edit_reveal_caret=false;
        } else if (ws->is_virtual_list) {
            ws->vscroll_pos_y = static_cast<double>(ratio) * ws->vscroll_max_y;
            ws->vscroll_vel_y = 0.0;
            ws->vlist_pending_scroll = false;
            ws->vlist_stuck_bottom = (ws->vscroll_max_y <= 0.5) || (ws->vscroll_pos_y >= ws->vscroll_max_y - 2.0);
        } else {
            ws->scroll_pos.y = ratio * ws->scroll_max.y;
            ws->scroll_vel.y = 0.0f;
            ws->scroll_stuck_bottom = (ws->scroll_max.y <= 0.5f) || (ws->scroll_pos.y >= ws->scroll_max.y - 2.0f);
        }
    }
}

void emit_canvas_grid_command(
    Context* ctx,
    const CanvasGridExtra* cg,
    Rect box_px,
    const ClipEntry& clip,
    float dpi) noexcept
{
    if (!cg) {
        return;
    }
    const float world_step = (cg->grid_step_world > 2.0f) ? cg->grid_step_world : 24.0f;
    const uint32_t subdiv = (cg->major_subdiv >= 2u) ? cg->major_subdiv : 4u;
    float minor_px = world_step * cg->zoom * dpi;
    while (minor_px < 12.0f * dpi) {
        minor_px *= static_cast<float>(subdiv);
    }
    while (minor_px > 64.0f * dpi) {
        minor_px /= static_cast<float>(subdiv);
    }
    const float major_px = minor_px * static_cast<float>(subdiv);
    const float fade_t = clamp_range((minor_px / dpi - 10.0f) / 14.0f, 0.25f, 1.0f);

    const float ox = cg->pan.x * dpi;
    const float oy = cg->pan.y * dpi;
    const auto wrap_phase = [](float offset, float step) noexcept -> float {
        float m = std::fmod(offset, step);
        if (m < 0.0f) {
            m += step;
        }
        return m;
    };
    const Vec2 phase_px = { wrap_phase(ox, major_px), wrap_phase(oy, major_px) };
    const float dot_r = clamp_range(1.25f * dpi, 0.8f, 2.5f);

    push_prim(
        ctx,
        Prim::make_canvas_grid(
            box_px,
            clip.rect,
            phase_px,
            minor_px,
            major_px,
            Vec2{ ox, oy },
            cg->minor_color,
            cg->major_color,
            dot_r,
            fade_t,
            clip.radius));
}

void emit_canvas_wire_command(
    Context* ctx,
    const CanvasWireExtra* cw,
    Rect fallback_box_logical,
    const ClipEntry& clip,
    float dpi) noexcept
{
    if (!cw) {
        return;
    }
    const WidgetState* cws = (cw->canvas_id != 0u) ? state_map_find(&ctx->active_ui->states, cw->canvas_id) : nullptr;
    const Vec2 pan = cws ? cws->canvas_pan : Vec2{};
    const float zoom = (cws && cws->canvas_zoom > 1e-4f) ? cws->canvas_zoom : 1.0f;

    Rect canvas_logical = fallback_box_logical;
    if (cw->canvas_id != 0u) {
        bool found = false;
        const Rect ed = layout_engine_bounds(ctx->active_ui->layout, cw->canvas_id, &found);
        if (found) canvas_logical = ed;
    }

    const Vec2 p0_px = (Vec2{ canvas_logical.x, canvas_logical.y } + pan + cw->from_world * zoom) * dpi;
    const Vec2 p1_px = (Vec2{ canvas_logical.x, canvas_logical.y } + pan + cw->to_world * zoom) * dpi;
    const float dx = std::fabs(p1_px.x - p0_px.x);
    const float dy = std::fabs(p1_px.y - p0_px.y);
    const Vec2 mid_px = (p0_px + p1_px) * 0.5f;
    Vec2 q0_ctrl = {};
    Vec2 q1_ctrl = {};
    if (cw->axis == Axis::Col) {
        const float sign_y = (p1_px.y >= p0_px.y) ? 1.0f : -1.0f;
        const float handle = clamp_range(
            ((dy > 16.0f * dpi) ? dy : (dx * 0.45f + 16.0f * dpi)) * cw->tangent_bias,
            10.0f * dpi,
            220.0f * dpi);
        q0_ctrl = { p0_px.x, p0_px.y + sign_y * handle };
        q1_ctrl = { p1_px.x, p1_px.y - sign_y * handle };
    } else {
        const float handle = clamp_range(
            ((dx > 40.0f * dpi) ? dx : (dy * 0.55f + 40.0f * dpi)) * cw->tangent_bias,
            24.0f * dpi,
            260.0f * dpi);
        q0_ctrl = { p0_px.x + handle, p0_px.y };
        q1_ctrl = { p1_px.x - handle, p1_px.y };
    }
    const Color c_mid = Color::lerp(cw->color_start, cw->color_end, 0.5f);
    const float half_w_px = 0.5f * ((cw->thickness_world * zoom > 0.75f) ? (cw->thickness_world * zoom) : 0.75f) * dpi;
    const float glow_px = cw->glow_world * zoom * dpi;

    push_prim(
        ctx,
        Prim::make_bezier_wire(
            p0_px, q0_ctrl, mid_px, clip.rect, cw->color_start, c_mid, half_w_px, cw->glow_color, glow_px, clip.radius));
    push_prim(
        ctx,
        Prim::make_bezier_wire(
            mid_px, q1_ctrl, p1_px, clip.rect, c_mid, cw->color_end, half_w_px, cw->glow_color, glow_px, clip.radius));
}

void emit_direct_text_px(
    Context* ctx,
    const char* s,
    float x_logical,
    float y_logical,
    float font_px,
    Color tint,
    const ClipEntry& clip,
    float dpi) noexcept
{
    if (!ctx || !ctx->atlas.font_valid || !s || s[0] == '\0') {
        return;
    }
    const uint16_t primary_slot = resolve_font_slot_idx(&ctx->atlas, kFontSans);
    if (primary_slot == kInvalidFont) {
        return;
    }
    const float phys_px = font_px * dpi;
    const float ascender_px = ctx->atlas.slots[primary_slot].ascent_em * phys_px;
    float pen_x = x_logical * dpi;
    const float base_y = y_logical * dpi + ascender_px;
    const std::span<const ShapedGlyph> glyphs = shape_utf8_run(ctx, primary_slot, s, static_cast<uint32_t>(std::strlen(s)));
    for (const ShapedGlyph& g : glyphs) {
        emit_slug_glyph_quad(ctx, g.font_slot, g.glyph_id, phys_px, pen_x, base_y, g.x_offset_em, g.y_offset_em, clip, tint);
        pen_x += g.x_advance_1px * phys_px;
    }
}

uint32_t hit_test_span_regions(const Context* ctx, Vec2 pointer_pos) noexcept {
    if (!ctx || pointer_pos.x < 0.0f || pointer_pos.y < 0.0f) {
        return 0u;
    }
    return ctx->active_ui->hovered_span_id;
}

Vec2 window_to_logical_pos_for(const Context* ctx, SDL_Window* win, float wx, float wy) noexcept {
    const WindowUiState* ui = ctx ? (win ? window_ui(ctx, win) : ctx->active_ui) : nullptr;
    SDL_Window* target = win ? win : (ctx ? ctx->active_window : nullptr);
    if (!ui || !target || ui->dpi_scale <= 0.0f) {
        return Vec2{wx, wy};
    }
    float density = SDL_GetWindowPixelDensity(target);
    if (density <= 0.0f) {
        density = 1.0f;
    }
    const float scale = density / ui->dpi_scale;
    return Vec2{wx * scale, wy * scale};
}

Vec2 window_to_logical_pos(const Context* ctx, float wx, float wy) noexcept {
    return window_to_logical_pos_for(ctx, ctx ? ctx->active_window : nullptr, wx, wy);
}

float logical_to_window_scale(const Context* ctx) noexcept {
    if (!ctx || ctx->headless || !ctx->active_window || ctx->active_ui->dpi_scale <= 0.0f) {
        return 1.0f;
    }
    float density = SDL_GetWindowPixelDensity(ctx->active_window);
    if (density <= 0.0f) {
        density = 1.0f;
    }
    return ctx->active_ui->dpi_scale / density;
}

uint32_t utf8_clamp_boundary(const char* s, uint32_t len, uint32_t pos) noexcept {
    if (!s || len == 0u) {
        return 0u;
    }
    if (pos > len) {
        pos = len;
    }
    while (pos > 0u && pos < len && (static_cast<uint8_t>(s[pos]) & 0xC0u) == 0x80u) {
        --pos;
    }
    return pos;
}

uint32_t utf8_prev_boundary(const char* s, uint32_t len, uint32_t pos) noexcept {
    pos = utf8_clamp_boundary(s, len, pos);
    if (pos == 0u) {
        return 0u;
    }
    --pos;
    while (pos > 0u && (static_cast<uint8_t>(s[pos]) & 0xC0u) == 0x80u) {
        --pos;
    }
    return pos;
}

uint32_t utf8_next_boundary(const char* s, uint32_t len, uint32_t pos) noexcept {
    pos = utf8_clamp_boundary(s, len, pos);
    if (!s || pos >= len) {
        return len;
    }
    const char* p = s + pos;
    (void)next_utf8_codepoint(p, s + len);
    return static_cast<uint32_t>(p - s);
}

uint32_t utf8_decode_at(const char* s, uint32_t len, uint32_t pos) noexcept {
    if (!s || pos >= len) {
        return 0u;
    }
    const char* p = s + pos;
    return next_utf8_codepoint(p, s + len);
}

} // namespace etch
