// Declarative Clay layout adapter, UTF-8 + stb_truetype FontAtlas, and Prim batch lowering.
// Compiled via src/etch_unity.cpp.

#include <cmath>

namespace etch {

namespace {

constexpr Clay_ElementId make_clay_id(uint32_t element_id) noexcept {
    return Clay_ElementId{
        .id = element_id,
        .offset = 0u,
        .baseId = element_id,
        .stringId = Clay_String{ .isStaticallyAllocated = true, .length = 0, .chars = "" },
    };
}

inline Clay_Color to_clay_color(Color c) noexcept {
    return Clay_Color{
        static_cast<float>(c.r),
        static_cast<float>(c.g),
        static_cast<float>(c.b),
        static_cast<float>(c.a),
    };
}

constexpr Clay_SizingAxis to_clay_sizing(const Sizing& s) noexcept {
    Clay_SizingAxis out = {};
    switch (s.kind) {
        case SizingKind::Fit:
            out.type = CLAY__SIZING_TYPE_FIT;
            out.size.minMax = Clay_SizingMinMax{ s.min, s.max };
            break;
        case SizingKind::Grow:
            out.type = CLAY__SIZING_TYPE_GROW;
            out.size.minMax = Clay_SizingMinMax{ s.min, s.max };
            break;
        case SizingKind::Fixed:
            out.type = CLAY__SIZING_TYPE_FIXED;
            out.size.minMax = Clay_SizingMinMax{ s.value, s.value };
            break;
        case SizingKind::Percent:
            out.type = CLAY__SIZING_TYPE_PERCENT;
            out.size.percent = (s.value < 0.0f) ? 0.0f : ((s.value > 1.0f) ? 1.0f : s.value);
            break;
    }
    return out;
}

constexpr Clay_LayoutDirection to_clay_axis(Axis axis) noexcept {
    return (axis == Axis::Col) ? CLAY_TOP_TO_BOTTOM : CLAY_LEFT_TO_RIGHT;
}

constexpr Clay_LayoutAlignmentX to_clay_align_x(AlignX ax) noexcept {
    switch (ax) {
        case AlignX::Left:   return CLAY_ALIGN_X_LEFT;
        case AlignX::Center: return CLAY_ALIGN_X_CENTER;
        case AlignX::Right:  return CLAY_ALIGN_X_RIGHT;
    }
    return CLAY_ALIGN_X_LEFT;
}

constexpr Clay_LayoutAlignmentY to_clay_align_y(AlignY ay) noexcept {
    switch (ay) {
        case AlignY::Top:    return CLAY_ALIGN_Y_TOP;
        case AlignY::Center: return CLAY_ALIGN_Y_CENTER;
        case AlignY::Bottom: return CLAY_ALIGN_Y_BOTTOM;
    }
    return CLAY_ALIGN_Y_TOP;
}

inline uint16_t to_u16_round(float v) noexcept {
    if (v <= 0.0f) {
        return 0u;
    }
    if (v >= 65535.0f) {
        return 65535u;
    }
    return static_cast<uint16_t>(v + 0.5f);
}

inline Clay_CornerRadius to_clay_corners(const Corners& c) noexcept {
    // Note: Clay_CornerRadius puts bottomLeft before bottomRight.
    return Clay_CornerRadius{
        .topLeft = c.tl,
        .topRight = c.tr,
        .bottomLeft = c.bl,
        .bottomRight = c.br,
    };
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

WidgetState* resolve_canvas_owner(Context* ctx, const Clay_ElementIdArray& over) noexcept {
    if (!ctx) {
        return nullptr;
    }
    for (int32_t i = over.length - 1; i >= 0; --i) {
        WidgetState* ws = state_map_find(&ctx->active_ui->states, over.internalArray[i].id);
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

constexpr Clay_FloatingAttachPointType to_clay_attach_point(Anchor a) noexcept {
    switch (a) {
        case Anchor::TopLeft:      return CLAY_ATTACH_POINT_LEFT_TOP;
        case Anchor::TopCenter:    return CLAY_ATTACH_POINT_CENTER_TOP;
        case Anchor::TopRight:     return CLAY_ATTACH_POINT_RIGHT_TOP;
        case Anchor::CenterLeft:   return CLAY_ATTACH_POINT_LEFT_CENTER;
        case Anchor::Center:       return CLAY_ATTACH_POINT_CENTER_CENTER;
        case Anchor::CenterRight:  return CLAY_ATTACH_POINT_RIGHT_CENTER;
        case Anchor::BottomLeft:   return CLAY_ATTACH_POINT_LEFT_BOTTOM;
        case Anchor::BottomCenter: return CLAY_ATTACH_POINT_CENTER_BOTTOM;
        case Anchor::BottomRight:  return CLAY_ATTACH_POINT_RIGHT_BOTTOM;
    }
    return CLAY_ATTACH_POINT_LEFT_TOP;
}

constexpr Clay_FloatingAttachToElement to_clay_attach_to(AttachTo at) noexcept {
    switch (at) {
        case AttachTo::None:    return CLAY_ATTACH_TO_NONE;
        case AttachTo::Parent:  return CLAY_ATTACH_TO_PARENT;
        case AttachTo::Element: return CLAY_ATTACH_TO_ELEMENT_WITH_ID;
        case AttachTo::Root:    return CLAY_ATTACH_TO_ROOT;
    }
    return CLAY_ATTACH_TO_NONE;
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
    if (!ws || (!ws->is_scroll_y && !ws->is_virtual_list)) {
        return g;
    }
    double scroll_y = 0.0;
    double scroll_max_y = 0.0;
    double content_h = 0.0;
    if (ws->is_virtual_list) {
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
    const float thumb_x = b.x + b.w - kSbMargin - kSbThumbWidth;
    const float track_y = b.y + kSbMargin;

    g.visible = true;
    g.hit_track = Rect{ b.x + b.w - kSbMargin - kSbHitWidth, track_y, kSbHitWidth + kSbMargin, track_h };
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
    float* out_total_w) noexcept
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
            const float adv = ((cp == '\t' && !mask_bullets) ? 4.0f * cp_w : cp_w) + next_spacing();
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
            (void)next_utf8_codepoint(p, end);
            const float adv = bullet_adv + next_spacing();
            if (cp_off < stop_a) xa += adv;
            if (cp_off < stop_b) xb += adv;
            total_w += adv;
        }
        if (out_xa) *out_xa = xa;
        if (out_xb) *out_xb = xb;
        if (out_total_w) *out_total_w = total_w;
        return;
    }

    const auto cluster_stop_dx = [&](uint32_t stop, uint32_t abs_c0, uint32_t abs_c1, float cluster_adv, bool is_rtl) noexcept -> float {
        if (is_rtl) {
            if (stop <= abs_c0) return cluster_adv;
            if (stop >= abs_c1) return 0.0f;
            const uint32_t n_tot = count_utf8_cps(seg + abs_c0, abs_c1 - abs_c0);
            const uint32_t n_pre = count_utf8_cps(seg + abs_c0, stop - abs_c0);
            return cluster_adv * (1.0f - static_cast<float>(n_pre) / static_cast<float>(n_tot > 0u ? n_tot : 1u));
        }
        if (stop >= abs_c1) return cluster_adv;
        if (stop <= abs_c0) return 0.0f;
        const uint32_t n_tot = count_utf8_cps(seg + abs_c0, abs_c1 - abs_c0);
        const uint32_t n_pre = count_utf8_cps(seg + abs_c0, stop - abs_c0);
        return cluster_adv * (static_cast<float>(n_pre) / static_cast<float>(n_tot > 0u ? n_tot : 1u));
    };

    uint32_t pos = 0u;
    while (pos < seg_len) {
        if (seg[pos] == '\t') {
            const float adv = 4.0f * primary.space_advance_em * base_px + next_spacing();
            if (pos < stop_a) xa += adv;
            if (pos < stop_b) xb += adv;
            total_w += adv;
            ++pos;
            continue;
        }
        uint32_t run_end = pos;
        while (run_end < seg_len && seg[run_end] != '\t') {
            ++run_end;
        }
        const uint32_t run_len = run_end - pos;
        bool is_rtl = false;
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
            const uint32_t c1 = is_rtl
                ? ((gi > 0u && glyphs[gi - 1u].cluster > c0) ? glyphs[gi - 1u].cluster : run_len)
                : ((gi_end < gcount && glyphs[gi_end].cluster > c0) ? glyphs[gi_end].cluster : run_len);
            const uint32_t abs_c0 = pos + c0;
            const uint32_t abs_c1 = pos + c1;
            if (out_xa) xa += cluster_stop_dx(stop_a, abs_c0, abs_c1, cluster_adv, is_rtl);
            if (out_xb) xb += cluster_stop_dx(stop_b, abs_c0, abs_c1, cluster_adv, is_rtl);
            total_w += cluster_adv;
            gi = gi_end;
        }
        pos = run_end;
    }
    if (out_xa) *out_xa = xa;
    if (out_xb) *out_xb = xb;
    if (out_total_w) *out_total_w = total_w;
}

constexpr float decode_clay_font_px(uint16_t font_size, uint16_t font_id) noexcept {
    const uint32_t frac = (font_id >> 7) & 0xFFu;
    if (font_size == 0u && frac == 0u) {
        return 16.0f;
    }
    return static_cast<float>(font_size) + static_cast<float>(frac) * (1.0f / 256.0f);
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

Clay_Dimensions measure_rich_slice(
    Context* ctx,
    const RichTextExtra* rich,
    Clay_StringSlice slice) noexcept
{
    if (!slice.chars || slice.length <= 0 || !rich || !rich->spans || rich->span_count == 0u) {
        return Clay_Dimensions{ .width = 0.0f, .height = rich ? rich->line_height_px : 16.0f };
    }
    const char* base = rich->full_utf8;
    const char* base_end = rich->full_utf8 + rich->total_bytes;
    const char* slice_end = slice.chars + slice.length;
    if (!base || slice.chars < base || slice_end > base_end) {
        const RichSpanInternal& s0 = rich->spans[0];
        const float w = measure_rich_span_segment(
            ctx, s0, slice.chars, static_cast<uint32_t>(slice.length), true);
        return Clay_Dimensions{ .width = w, .height = rich->line_height_px };
    }

    const uint32_t s0 = static_cast<uint32_t>(slice.chars - base);
    const uint32_t s1 = s0 + static_cast<uint32_t>(slice.length);
    float total_w = 0.0f;
    for (uint32_t i = 0u; i < rich->span_count; ++i) {
        const RichSpanInternal& sp = rich->spans[i];
        if (sp.byte_end <= s0) {
            continue;
        }
        if (sp.byte_start >= s1) {
            break;
        }
        const uint32_t ov0 = (s0 > sp.byte_start) ? s0 : sp.byte_start;
        const uint32_t ov1 = (s1 < sp.byte_end) ? s1 : sp.byte_end;
        const bool is_last_seg = (ov1 == s1);
        total_w += measure_rich_span_segment(ctx, sp, base + ov0, ov1 - ov0, is_last_seg);
        if (ov0 == sp.byte_start) {
            total_w += sp.padding_x;
        }
        if (ov1 == sp.byte_end) {
            total_w += sp.padding_x;
        }
    }
    return Clay_Dimensions{ .width = total_w, .height = rich->line_height_px };
}

Clay_Dimensions etch_measure_text(
    Clay_StringSlice text,
    Clay_TextElementConfig* config,
    void* user_data) noexcept
{
    Context* ctx = static_cast<Context*>(user_data);
    const size_t arena_mark = ctx ? ctx->frame_arena.mark() : 0u;
    Clay_Dimensions dims = {};
    if (config && config->userData != nullptr) {
        dims = measure_rich_slice(ctx, static_cast<const RichTextExtra*>(config->userData), text);
    } else {
        const uint16_t font_id = config ? static_cast<uint16_t>(config->fontId & 0x7Fu) : kFontSans;
        const float logical_font_px = config ? decode_clay_font_px(config->fontSize, config->fontId) : 16.0f;
        const float letter_spacing = config ? static_cast<float>(config->letterSpacing) : 0.0f;
        if (!text.chars || text.length <= 0) {
            dims = Clay_Dimensions{ .width = 0.0f, .height = logical_font_px };
        } else {
            float width_logical = 0.0f;
            measure_line_segment_x(
                ctx,
                font_id,
                text.chars,
                static_cast<uint32_t>(text.length),
                0u,
                0u,
                logical_font_px,
                letter_spacing,
                false,
                true,
                nullptr,
                nullptr,
                &width_logical);
            dims = Clay_Dimensions{
                .width = width_logical,
                .height = logical_font_px,
            };
        }
    }
    if (ctx) {
        ctx->frame_arena.rewind(arena_mark);
    }
    return dims;
}

void etch_clay_error_handler(Clay_ErrorData error) noexcept {
    SDL_LogError(
        SDL_LOG_CATEGORY_APPLICATION,
        "Etch Clay error (%d): %.*s",
        static_cast<int>(error.errorType),
        error.errorText.length,
        error.errorText.chars ? error.errorText.chars : "");
}

void push_prim(
    Context* ctx,
    const Prim& prim,
    SDL_GPUTexture* ext_tex = nullptr,
    float cull_pad_px = 2.0f) noexcept
{
    if (!ctx || !ctx->prims || ctx->prim_count >= ctx->max_prims) {
        return;
    }
    // CPU-side offscreen clip culling: drop primitives completely outside their active clip rect.
    if (prim.clip.z <= prim.clip.x || prim.clip.w <= prim.clip.y ||
        prim.bounds.z <= 0.0f || prim.bounds.w <= 0.0f)
    {
        return;
    }
    if ((prim.bounds.x + prim.bounds.z + cull_pad_px <= prim.clip.x) ||
        (prim.bounds.x - cull_pad_px >= prim.clip.z) ||
        (prim.bounds.y + prim.bounds.w + cull_pad_px <= prim.clip.y) ||
        (prim.bounds.y - cull_pad_px >= prim.clip.w))
    {
        return;
    }
    if (ctx->batch_count == 0u) {
        ctx->batches[0] = DrawBatch{ .first_prim = 0u, .prim_count = 0u, .ext_textures = {}, .ext_count = 0u };
        ctx->batch_count = 1u;
    }

    DrawBatch* cur = &ctx->batches[ctx->batch_count - 1u];
    uint16_t tex_slot = 0u;

    if (ext_tex != nullptr) {
        bool found = false;
        for (uint32_t i = 0u; i < cur->ext_count; ++i) {
            if (cur->ext_textures[i] == ext_tex) {
                tex_slot = static_cast<uint16_t>(i + 1u);
                found = true;
                break;
            }
        }
        if (!found) {
            if (cur->ext_count < 3u) {
                cur->ext_textures[cur->ext_count] = ext_tex;
                tex_slot = static_cast<uint16_t>(cur->ext_count + 1u);
                ++cur->ext_count;
            } else {
                if (ctx->batch_count >= kMaxBatches) {
                    return;
                }
                cur = &ctx->batches[ctx->batch_count++];
                *cur = DrawBatch{
                    .first_prim = ctx->prim_count,
                    .prim_count = 0u,
                    .ext_textures = {ext_tex, nullptr, nullptr},
                    .ext_count = 1u,
                };
                tex_slot = 1u;
            }
        }
    }

    Prim final_p = prim;
    final_p.style.x = (final_p.style.x & ~(0x3FFu << 10)) | ((static_cast<uint32_t>(tex_slot) & 0x3FFu) << 10);
    ctx->prims[ctx->prim_count++] = final_p;
    ++cur->prim_count;

    const uint32_t pkind = final_p.style.x & 0xFu;
    if (pkind != static_cast<uint32_t>(PrimKind::Shadow) &&
        pkind != static_cast<uint32_t>(PrimKind::BlurGlass))
    {
        ctx->layer_has_bg_prims = true;
    }
}

inline Corners scale_corners(Corners c, float dpi) noexcept {
    return Corners{ c.tl * dpi, c.tr * dpi, c.br * dpi, c.bl * dpi };
}

bool begin_blur_layer(Context* ctx, Rect box_px, float radius_px) noexcept {
    if (!ctx->layer_has_bg_prims && ctx->blur_split_count > 0u) {
        BlurSplit& cur = ctx->blur_splits[ctx->blur_split_count - 1u];
        const bool overlaps =
            box_px.x < (cur.union_bounds.x + cur.union_bounds.w) &&
            (box_px.x + box_px.w) > cur.union_bounds.x &&
            box_px.y < (cur.union_bounds.y + cur.union_bounds.h) &&
            (box_px.y + box_px.h) > cur.union_bounds.y;
        if (!overlaps) {
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
    if (ctx->blur_split_count >= kMaxBlurSplits) {
        return false;
    }
    if (ctx->batch_count > 0u && ctx->batches[ctx->batch_count - 1u].prim_count > 0u) {
        if (ctx->batch_count >= kMaxBatches) {
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
        .blur_radius_px = radius_px,
        .union_bounds = box_px,
    };
    ctx->layer_has_bg_prims = false;
    return true;
}

void emit_canvas_grid_command(
    Context* ctx,
    const CanvasGridExtra* cg,
    Rect box_px,
    const ClipEntry& clip,
    float dpi) noexcept;

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
    const Corners rad_px = scale_corners(d.radius, dpi);
    const bool has_grad = (d.background_end.a > 0u) || d.radial_gradient;
    const bool box_visible =
        clip.rect.w > 0.0f && clip.rect.h > 0.0f &&
        box_px.w > 0.0f && box_px.h > 0.0f &&
        (box_px.x + box_px.w) > clip.rect.x && box_px.x < (clip.rect.x + clip.rect.w) &&
        (box_px.y + box_px.h) > clip.rect.y && box_px.y < (clip.rect.y + clip.rect.h);
    const bool use_blur =
        (d.backdrop_blur > 0.0f) && !has_grad && box_visible &&
        begin_blur_layer(ctx, box_px, d.backdrop_blur * dpi);

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
            Prim::make_shadow(
                shadow_rect,
                clip.rect,
                d.shadow_color,
                rad_px,
                blur_px,
                clip.radius),
            nullptr,
            2.0f * blur_px + 2.0f);
    }

    const bool has_fill = (d.background.a > 0u) || has_grad;
    const bool any_clip = d.clip_children || d.scroll_x || d.scroll_y;
    const bool draw_border_here = !any_clip && d.border_width > 0.0f && d.border_color.a > 0u;
    if (has_fill || draw_border_here || use_blur) {
        const Color border_col = draw_border_here ? d.border_color : Color{};
        const float border_w = draw_border_here ? (d.border_width * dpi) : 0.0f;

        if (use_blur) {
            push_prim(
                ctx,
                Prim::make_blur_glass(
                    box_px,
                    clip.rect,
                    d.background,
                    rad_px,
                    border_col,
                    border_w,
                    0u,
                    clip.radius),
                blur_tex_sentinel());
        } else if (has_grad) {
            push_prim(
                ctx,
                Prim::make_gradient_rect(
                    box_px,
                    clip.rect,
                    d.background,
                    d.background_end,
                    d.gradient_dir,
                    rad_px,
                    border_col,
                    border_w,
                    clip.radius,
                    d.radial_gradient));
        } else {
            push_prim(
                ctx,
                Prim::make_rect(
                    box_px,
                    clip.rect,
                    d.background,
                    rad_px,
                    border_col,
                    border_w,
                    1.0f,
                    clip.radius));
        }
    }

    if (extra->is_canvas && extra->canvas_grid && box_visible) {
        const Rect canvas_clip_rect = rect_intersect(clip.rect, box_px);
        float canvas_clip_r = min4f(rad_px.tl, rad_px.tr, rad_px.br, rad_px.bl);
        if (canvas_clip_r <= 0.0f &&
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

void emit_slug_glyph_quad(
    Context* ctx,
    uint16_t slot_idx,
    uint32_t glyph_id,
    float phys_px,
    float pen_x_px,
    float baseline_y_px,
    float x_offset_em,
    float y_offset_em,
    const ClipEntry& clip_px,
    Color tint) noexcept
{
    const SlugGlyphEntry ge = slug_get_glyph(&ctx->atlas, slot_idx, glyph_id);
    if (!ge.has_outline) {
        return;
    }
    const float scale_px = ctx->atlas.slots[slot_idx].scale_1px * phys_px;
    const float box_x = pen_x_px + (x_offset_em + ge.min_x_em) * scale_px;
    const float box_y = baseline_y_px - (y_offset_em + ge.max_y_em) * scale_px;
    const float box_w = (ge.max_x_em - ge.min_x_em) * scale_px;
    const float box_h = (ge.max_y_em - ge.min_y_em) * scale_px;
    if (box_w <= 0.0f || box_h <= 0.0f) {
        return;
    }
    push_prim(
        ctx,
        Prim::make_slug_glyph(
            Rect{ box_x, box_y, box_w, box_h },
            clip_px.rect,
            tint,
            ge.band_offset,
            ge.num_hbands,
            ge.num_vbands,
            clip_px.radius,
            PrimFlag_StemDarken));
}

void emit_rich_text_command(
    Context* ctx,
    const Clay_RenderCommand* cmd,
    const RichTextExtra* rich,
    const ClipEntry& clip,
    float dpi) noexcept
{
    const Clay_TextRenderData& td = cmd->renderData.text;
    const char* chars = td.stringContents.chars;
    const int32_t len = td.stringContents.length;
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
    const float top_lead = (content_h > 0.0f) ? (0.5f * (cmd->boundingBox.height - content_h)) : 0.0f;
    const float line_top_px = cmd->boundingBox.y * dpi;
    const float line_bot_px = (cmd->boundingBox.y + cmd->boundingBox.height) * dpi;
    const float baseline_y_px = (cmd->boundingBox.y + top_lead + rich->max_ascent_px) * dpi;

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
        clip.rect.x / dpi,
        clip.rect.y / dpi,
        clip.rect.w / dpi,
        clip.rect.h / dpi,
    };

    float pen_logical = cmd->boundingBox.x;
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
        const float text_x0 = pen_logical;

        const uint16_t slot_idx = can_shape ? resolve_font_slot_idx(&ctx->atlas, sp.font_id) : kInvalidFont;
        const bool has_slot = (slot_idx != kInvalidFont);
        uint32_t glyph_idx = 0u;
        const auto next_spacing = [&]() noexcept -> float {
            const float s = (!is_last_on_line || glyph_idx > 0u) ? sp.letter_spacing : 0.0f;
            ++glyph_idx;
            return s;
        };

        if (!has_slot) {
            const float cp_w = sp.font_size_px * 0.55f;
            const char* p = base + ov0;
            const char* end = base + ov1;
            while (p < end) {
                const uint32_t cp = next_utf8_codepoint(p, end);
                pen_logical += ((cp == '\t') ? 4.0f * cp_w : cp_w) + next_spacing();
            }
        } else {
            const FontSlot& primary = ctx->atlas.slots[slot_idx];
            uint32_t pos = ov0;
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

        if (sp.id != 0u && seg_x1 > seg_x0 && ctx->active_ui->span_hit_count < kMaxSpanHitsPerFrame) {
            ctx->active_ui->span_hits[ctx->active_ui->span_hit_count++] = SpanHitRegion{
                .id = sp.id,
                .clay_elem_id = rich->clay_elem_id,
                .rect = Rect{ seg_x0, cmd->boundingBox.y, seg_x1 - seg_x0, cmd->boundingBox.height },
                .z_index = cmd->zIndex,
                .clip_rect = clip_logical,
                .has_clip = true,
            };
        }
    }

    // Pass 1: Emit inline pill backgrounds + borders BEFORE all glyphs on this line.
    for (uint32_t i = 0u; i < seg_count; ++i) {
        const RichLineSeg& sg = segs[i];
        const RichSpanInternal* sp = sg.sp;
        if (sp->background.a == 0u && sp->border_color.a == 0u) {
            continue;
        }
        const float w_px = sg.x1_px - sg.x0_px;
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
        const float pad_y_px = 1.5f * dpi;
        float top_px = baseline_y_px - sp_asc_px - pad_y_px;
        float bot_px = baseline_y_px + sp_desc_px + pad_y_px;
        if (top_px < line_top_px) top_px = line_top_px;
        if (bot_px > line_bot_px) bot_px = line_bot_px;
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
                clip.rect,
                sp->background,
                Corners{ rl, rr, rr, rl },
                sp->border_color,
                bw,
                1.0f,
                clip.radius));
    }

    // Pass 2: Emit Slug glyphs, underline, and strikethrough decorations.
    for (uint32_t i = 0u; i < chunk_count; ++i) {
        const RichLineChunk& ch = chunks[i];
        const RichSpanInternal* sp = ch.sp;
        const float phys_px = sp->font_size_px * dpi;
        const float ls_px = sp->letter_spacing * dpi;
        float gx = ch.start_x_px;
        for (uint32_t gi = 0u; gi < static_cast<uint32_t>(ch.glyphs.size()); ++gi) {
            const ShapedGlyph& g = ch.glyphs[gi];
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
                clip,
                sp->color);
            gx += g.x_advance_1px * phys_px + (ch.trim_last_spacing ? 0.0f : ls_px);
        }
    }

    for (uint32_t i = 0u; i < seg_count; ++i) {
        const RichLineSeg& sg = segs[i];
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
                    clip.rect,
                    sp->color));
        }
        if (sp->strikethrough) {
            const float sy = baseline_y_px - sp->font_size_px * 0.28f * dpi;
            push_prim(
                ctx,
                Prim::make_rect(
                    Rect{ sg.text_x0_px, sy, sg.text_w_px, thick_px },
                    clip.rect,
                    sp->color));
        }
    }

    ctx->frame_arena.rewind(arena_mark);
}

void emit_text_command(
    Context* ctx,
    const Clay_RenderCommand* cmd,
    const ClipEntry& clip,
    float dpi) noexcept
{
    if (cmd->userData != nullptr) {
        emit_rich_text_command(ctx, cmd, static_cast<const RichTextExtra*>(cmd->userData), clip, dpi);
        return;
    }

    const Clay_TextRenderData& td = cmd->renderData.text;
    const char* chars = td.stringContents.chars;
    const int32_t len = td.stringContents.length;
    if (!chars || len <= 0 || !ctx->atlas.font_valid) {
        return;
    }

    const uint16_t primary_slot = resolve_font_slot_idx(&ctx->atlas, static_cast<uint16_t>(td.fontId & 0x7Fu));
    if (primary_slot == kInvalidFont) {
        return;
    }
    const FontSlot& primary = ctx->atlas.slots[primary_slot];

    const float logical_font_px = decode_clay_font_px(td.fontSize, td.fontId);
    const float phys_px = logical_font_px * dpi;
    const float ascender_px = primary.ascent_em * phys_px;
    const float line_top_px = cmd->boundingBox.y * dpi;
    const float baseline_y = line_top_px + ascender_px;
    const float letter_spacing_px = static_cast<float>(td.letterSpacing) * dpi;

    const Color tint = Color::rgba8(
        static_cast<uint8_t>(td.textColor.r + 0.5f),
        static_cast<uint8_t>(td.textColor.g + 0.5f),
        static_cast<uint8_t>(td.textColor.b + 0.5f),
        static_cast<uint8_t>(td.textColor.a + 0.5f));

    float pen_x = cmd->boundingBox.x * dpi;
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
        if (ws.key != 0u && (ws.is_scroll_y || ws.is_virtual_list) && id_index(ws.key, kScrollbarYTag) == thumb_active_id) {
            return &ws;
        }
    }
    return nullptr;
}

void apply_scrollbar_drag(WidgetState* ws, float pointer_y, float grab_offset) noexcept {
    if (!ws) {
        return;
    }
    const ScrollbarGeom sb = compute_vscrollbar_geom(ws);
    if (sb.visible && sb.travel_h > 0.0f) {
        const float rel = (pointer_y - grab_offset) - sb.track.y;
        const float ratio = clamp_range(rel / sb.travel_h, 0.0f, 1.0f);
        if (ws->is_virtual_list) {
            ws->vscroll_pos_y = static_cast<double>(ratio) * ws->vscroll_max_y;
            ws->vscroll_vel_y = 0.0;
        } else {
            ws->scroll_pos.y = ratio * ws->scroll_max.y;
            ws->scroll_vel.y = 0.0f;
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
    Clay_BoundingBox fallback_box_logical,
    const ClipEntry& clip,
    float dpi) noexcept
{
    if (!cw) {
        return;
    }
    const WidgetState* cws = (cw->canvas_id != 0u) ? state_map_find(&ctx->active_ui->states, cw->canvas_id) : nullptr;
    const Vec2 pan = cws ? cws->canvas_pan : Vec2{};
    const float zoom = (cws && cws->canvas_zoom > 1e-4f) ? cws->canvas_zoom : 1.0f;

    Rect canvas_logical = {
        fallback_box_logical.x,
        fallback_box_logical.y,
        fallback_box_logical.width,
        fallback_box_logical.height,
    };
    if (cw->canvas_id != 0u) {
        const Clay_ElementData ed = Clay_GetElementData(make_clay_id(cw->canvas_id));
        if (ed.found) {
            canvas_logical = Rect{ ed.boundingBox.x, ed.boundingBox.y, ed.boundingBox.width, ed.boundingBox.height };
        }
    }

    const Vec2 p0_px = (Vec2{ canvas_logical.x, canvas_logical.y } + pan + cw->from_world * zoom) * dpi;
    const Vec2 p1_px = (Vec2{ canvas_logical.x, canvas_logical.y } + pan + cw->to_world * zoom) * dpi;
    const float dx = std::fabs(p1_px.x - p0_px.x);
    const float dy = std::fabs(p1_px.y - p0_px.y);
    const float handle = clamp_range(
        ((dx > 40.0f * dpi) ? dx : (dy * 0.55f + 40.0f * dpi)) * cw->tangent_bias,
        24.0f * dpi,
        260.0f * dpi);
    const Vec2 mid_px = (p0_px + p1_px) * 0.5f;
    const Vec2 q0_ctrl = { p0_px.x + handle, p0_px.y };
    const Vec2 q1_ctrl = { p1_px.x - handle, p1_px.y };
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
    for (int32_t i = static_cast<int32_t>(ctx->active_ui->span_hit_count) - 1; i >= 0; --i) {
        const SpanHitRegion& r = ctx->active_ui->span_hits[i];
        if (!r.rect.contains(pointer_pos)) {
            continue;
        }
        if (r.has_clip && !r.clip_rect.contains(pointer_pos)) {
            continue;
        }
        if (r.id != 0u && r.clay_elem_id != 0u && !Clay_PointerOver(make_clay_id(r.clay_elem_id))) {
            continue;
        }
        return r.id; // Either floating blocker (0u) or top clickable span id
    }
    return 0u;
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

uint32_t utf8_codepoint_to_byte_offset(const char* s, uint32_t len, int32_t cp_count) noexcept {
    if (!s || len == 0u || cp_count <= 0) {
        return 0u;
    }
    const char* p = s;
    const char* end = s + len;
    int32_t count = 0;
    while (p < end && count < cp_count) {
        (void)next_utf8_codepoint(p, end);
        ++count;
    }
    return static_cast<uint32_t>(p - s);
}

uint8_t utf8_char_class(uint32_t cp) noexcept {
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') {
        return 0u;
    }
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') || cp == '_') {
        return 1u;
    }
    if (cp >= 0x80u) {
        return 2u;
    }
    return 3u;
}

uint32_t utf8_word_left(const char* s, uint32_t len, uint32_t pos) noexcept {
    pos = utf8_clamp_boundary(s, len, pos);
    if (pos == 0u) {
        return 0u;
    }
    uint32_t cur = pos;
    while (cur > 0u) {
        const uint32_t prev = utf8_prev_boundary(s, len, cur);
        if (utf8_char_class(utf8_decode_at(s, len, prev)) != 0u) {
            break;
        }
        cur = prev;
    }
    if (cur == 0u) {
        return 0u;
    }
    const uint32_t first_prev = utf8_prev_boundary(s, len, cur);
    const uint8_t target_cls = utf8_char_class(utf8_decode_at(s, len, first_prev));
    cur = first_prev;
    while (cur > 0u) {
        const uint32_t prev = utf8_prev_boundary(s, len, cur);
        if (utf8_char_class(utf8_decode_at(s, len, prev)) != target_cls) {
            break;
        }
        cur = prev;
    }
    return cur;
}

uint32_t utf8_word_right(const char* s, uint32_t len, uint32_t pos) noexcept {
    pos = utf8_clamp_boundary(s, len, pos);
    if (pos >= len) {
        return len;
    }
    uint32_t cur = pos;
    while (cur < len && utf8_char_class(utf8_decode_at(s, len, cur)) == 0u) {
        cur = utf8_next_boundary(s, len, cur);
    }
    if (cur >= len) {
        return len;
    }
    const uint8_t target_cls = utf8_char_class(utf8_decode_at(s, len, cur));
    while (cur < len && utf8_char_class(utf8_decode_at(s, len, cur)) == target_cls) {
        cur = utf8_next_boundary(s, len, cur);
    }
    return cur;
}

void utf8_word_bounds_at(const char* s, uint32_t len, uint32_t pos, uint32_t* out_start, uint32_t* out_end) noexcept {
    if (!out_start || !out_end) {
        return;
    }
    if (!s || len == 0u) {
        *out_start = 0u;
        *out_end = 0u;
        return;
    }
    pos = utf8_clamp_boundary(s, len, pos);
    if (pos >= len && pos > 0u) {
        pos = utf8_prev_boundary(s, len, pos);
    }
    const uint32_t cp = utf8_decode_at(s, len, pos);
    if (cp == '\n') {
        *out_start = pos;
        *out_end = utf8_next_boundary(s, len, pos);
        return;
    }
    const uint8_t cls = utf8_char_class(cp);
    uint32_t a = pos;
    while (a > 0u) {
        const uint32_t prev = utf8_prev_boundary(s, len, a);
        const uint32_t pcp = utf8_decode_at(s, len, prev);
        if (pcp == '\n' || utf8_char_class(pcp) != cls) {
            break;
        }
        a = prev;
    }
    uint32_t b = utf8_next_boundary(s, len, pos);
    while (b < len) {
        const uint32_t ncp = utf8_decode_at(s, len, b);
        if (ncp == '\n' || utf8_char_class(ncp) != cls) {
            break;
        }
        b = utf8_next_boundary(s, len, b);
    }
    *out_start = a;
    *out_end = b;
}

void line_bounds_at(const char* s, uint32_t len, uint32_t pos, uint32_t* out_start, uint32_t* out_end) noexcept {
    if (!out_start || !out_end) {
        return;
    }
    if (!s || len == 0u) {
        *out_start = 0u;
        *out_end = 0u;
        return;
    }
    pos = utf8_clamp_boundary(s, len, pos);
    uint32_t a = pos;
    while (a > 0u && s[a - 1u] != '\n') {
        --a;
    }
    uint32_t b = pos;
    while (b < len && s[b] != '\n') {
        ++b;
    }
    *out_start = a;
    *out_end = b;
}

uint32_t erase_text_range(char* buf, uint32_t* io_len, uint32_t a, uint32_t b) noexcept {
    if (!buf || !io_len) {
        return 0u;
    }
    uint32_t len = *io_len;
    const uint32_t lo = utf8_clamp_boundary(buf, len, (a < b) ? a : b);
    const uint32_t hi = utf8_clamp_boundary(buf, len, (a < b) ? b : a);
    if (hi > lo) {
        std::memmove(buf + lo, buf + hi, size_t(len - hi));
        len -= (hi - lo);
        buf[len] = '\0';
        *io_len = len;
    }
    return lo;
}

uint32_t insert_utf8_filtered(
    char* buf,
    uint32_t cap,
    uint32_t* io_len,
    uint32_t sel_a,
    uint32_t sel_b,
    Str ins,
    bool multiline) noexcept
{
    if (!buf || cap == 0u || !io_len) {
        return 0u;
    }
    const uint32_t pos = erase_text_range(buf, io_len, sel_a, sel_b);
    uint32_t cur_len = *io_len;
    if (cur_len >= cap) {
        cur_len = cap - 1u;
        buf[cur_len] = '\0';
        *io_len = cur_len;
    }
    if (!ins.chars || ins.length == 0u) {
        return pos;
    }

    const uint32_t avail = (cap > cur_len + 1u) ? (cap - 1u - cur_len) : 0u;
    if (avail == 0u) {
        return pos;
    }

    // Pass 1: count filtered bytes respecting UTF-8 codepoint boundaries within avail.
    const char* p = ins.chars;
    const char* end = ins.chars + ins.length;
    uint32_t write_bytes = 0u;
    while (p < end) {
        const char* cp_start = p;
        const uint32_t cp = next_utf8_codepoint(p, end);
        if (cp == '\r' || cp == 0x7Fu) {
            continue;
        }
        if (cp < 0x20u && cp != '\n' && cp != '\t') {
            continue;
        }
        const uint32_t seq_bytes = (!multiline && (cp == '\n' || cp == '\t'))
            ? 1u
            : static_cast<uint32_t>(p - cp_start);
        if (write_bytes + seq_bytes > avail) {
            break;
        }
        write_bytes += seq_bytes;
    }
    if (write_bytes == 0u) {
        return pos;
    }

    const uint32_t tail_len = cur_len - pos;
    if (tail_len > 0u) {
        std::memmove(buf + pos + write_bytes, buf + pos, tail_len);
    }

    // Pass 2: materialize filtered bytes directly into target slice.
    p = ins.chars;
    uint32_t dst_off = 0u;
    while (p < end && dst_off < write_bytes) {
        const char* cp_start = p;
        const uint32_t cp = next_utf8_codepoint(p, end);
        if (cp == '\r' || cp == 0x7Fu) {
            continue;
        }
        if (cp < 0x20u && cp != '\n' && cp != '\t') {
            continue;
        }
        if (!multiline && (cp == '\n' || cp == '\t')) {
            buf[pos + dst_off++] = ' ';
        } else {
            const uint32_t seq_bytes = static_cast<uint32_t>(p - cp_start);
            if (dst_off + seq_bytes > write_bytes) {
                break;
            }
            std::memcpy(buf + pos + dst_off, cp_start, seq_bytes);
            dst_off += seq_bytes;
        }
    }

    cur_len += write_bytes;
    buf[cur_len] = '\0';
    *io_len = cur_len;
    return pos + write_bytes;
}

void push_pending_text_op(Context* ctx, PendingTextOp op, Str utf8) noexcept {
    if (!ctx || ctx->active_ui->pending_text_op_count >= kMaxTextOpsPerFrame) {
        return;
    }
    if (utf8.chars && utf8.length > 0u) {
        const uint32_t avail = kMaxPendingTextBytes - ctx->active_ui->pending_text_utf8_len;
        const uint32_t max_copy = (utf8.length < avail) ? utf8.length : avail;
        const uint32_t copy_len = utf8_clamp_boundary(utf8.chars, utf8.length, max_copy);
        if (copy_len == 0u) {
            return;
        }
        op.text_offset = static_cast<uint16_t>(ctx->active_ui->pending_text_utf8_len);
        op.text_len = static_cast<uint16_t>(copy_len);
        std::memcpy(ctx->active_ui->pending_text_utf8 + ctx->active_ui->pending_text_utf8_len, utf8.chars, copy_len);
        ctx->active_ui->pending_text_utf8_len += copy_len;
    }
    ctx->active_ui->pending_text_ops[ctx->active_ui->pending_text_op_count++] = op;
}

void clear_ime_composition(Context* ctx) noexcept {
    if (!ctx) {
        return;
    }
    ctx->active_ui->ime_comp_len = 0u;
    ctx->active_ui->ime_comp_utf8[0] = '\0';
    ctx->active_ui->ime_comp_cursor_byte = 0u;
    ctx->active_ui->ime_comp_sel_len_byte = 0u;
}

struct TextInnerGeom {
    Rect  clip_logical = {};
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    float line_h = 14.0f;
    float line_adv = 14.0f;
};

TextInnerGeom compute_text_inner_geom(Rect box_logical, const TextInputDesc& desc) noexcept {
    const float base_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f;
    const float line_adv = desc.multiline ? std::floor(base_px * 1.35f + 0.5f) : base_px;
    const float ix = box_logical.x + desc.padding.left;
    const float iy = box_logical.y + desc.padding.top;
    const float iw = (box_logical.w > desc.padding.left + desc.padding.right)
        ? (box_logical.w - desc.padding.left - desc.padding.right) : 0.0f;
    const float ih = (box_logical.h > desc.padding.top + desc.padding.bottom)
        ? (box_logical.h - desc.padding.top - desc.padding.bottom) : 0.0f;
    const float oy = desc.multiline
        ? iy
        : (iy + 0.5f * ((ih > base_px) ? (ih - base_px) : 0.0f));
    const float clip_pad_top = (desc.padding.top > 2.0f) ? 2.0f : desc.padding.top;
    const float clip_pad_bot = (desc.padding.bottom > 3.0f) ? 3.0f : desc.padding.bottom;
    return TextInnerGeom{
        .clip_logical = Rect{ ix, iy - clip_pad_top, iw, ih + clip_pad_top + clip_pad_bot },
        .origin_x = ix,
        .origin_y = oy,
        .line_h = base_px,
        .line_adv = line_adv,
    };
}

struct TextVisualPos {
    float    x = 0.0f;
    float    y = 0.0f;
    uint32_t line_idx = 0u;
};

TextVisualPos text_byte_to_visual(Context* ctx, const TextInputDesc& desc, Str text, uint32_t byte_pos) noexcept {
    const float base_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f;
    const float line_adv = desc.multiline ? std::floor(base_px * 1.35f + 0.5f) : base_px;
    TextVisualPos out = {};
    if (!text.chars || text.length == 0u) {
        return out;
    }
    const uint32_t clamped = utf8_clamp_boundary(text.chars, text.length, byte_pos);
    if (!desc.multiline) {
        measure_line_segment_x(
            ctx, desc.font_id, text.chars, text.length, clamped, 0u,
            base_px, desc.letter_spacing, desc.password, false, &out.x, nullptr, nullptr);
        return out;
    }

    uint32_t line_start = 0u;
    for (;;) {
        uint32_t line_end = line_start;
        while (line_end < text.length && text.chars[line_end] != '\n') {
            ++line_end;
        }
        if (clamped <= line_end) {
            measure_line_segment_x(
                ctx, desc.font_id, text.chars + line_start, line_end - line_start, clamped - line_start, 0u,
                base_px, desc.letter_spacing, desc.password, false, &out.x, nullptr, nullptr);
            return out;
        }
        out.y += line_adv;
        ++out.line_idx;
        line_start = line_end + 1u;
    }
}

uint32_t visual_point_to_byte(Context* ctx, const TextInputDesc& desc, Str text, float local_x, float local_y) noexcept {
    if (!text.chars || text.length == 0u) {
        return 0u;
    }
    const float base_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f;
    const float line_adv = desc.multiline ? std::floor(base_px * 1.35f + 0.5f) : base_px;
    const uint32_t target_line = desc.multiline
        ? static_cast<uint32_t>((local_y > 0.0f) ? std::floor(local_y / line_adv) : 0.0f)
        : 0u;

    uint32_t line_start = 0u;
    uint32_t line_end = text.length;
    if (desc.multiline) {
        uint32_t cur_line = 0u;
        while (cur_line < target_line && line_start < text.length) {
            if (text.chars[line_start] == '\n') {
                ++cur_line;
            }
            ++line_start;
        }
        if (cur_line < target_line) {
            return text.length;
        }
        line_end = line_start;
        while (line_end < text.length && text.chars[line_end] != '\n') {
            ++line_end;
        }
    }

    const char* seg = text.chars + line_start;
    const uint32_t seg_len = line_end - line_start;
    float pen_x = 0.0f;

    const uint16_t slot_idx = (ctx && ctx->atlas.font_valid) ? resolve_font_slot_idx(&ctx->atlas, desc.font_id) : kInvalidFont;
    if (!ctx || !ctx->atlas.font_valid || slot_idx == kInvalidFont || desc.password) {
        const float cp_adv = (!ctx || !ctx->atlas.font_valid || slot_idx == kInvalidFont)
            ? (base_px * 0.55f)
            : (ctx->atlas.slots[slot_idx].bullet_advance_em * base_px);
        const char* p = seg;
        const char* end = seg + seg_len;
        while (p < end) {
            const char* cp_start = p;
            const uint32_t cp = next_utf8_codepoint(p, end);
            const float adv = ((cp == '\t' && !desc.password) ? 4.0f * cp_adv : cp_adv) + desc.letter_spacing;
            if (local_x < pen_x + 0.5f * adv) {
                return line_start + static_cast<uint32_t>(cp_start - seg);
            }
            pen_x += adv;
        }
        return line_end;
    }

    const FontSlot& primary = ctx->atlas.slots[slot_idx];
    bool last_run_rtl = false;
    uint32_t last_run_start = 0u;
    uint32_t pos = 0u;
    while (pos < seg_len) {
        if (seg[pos] == '\t') {
            const float adv = 4.0f * primary.space_advance_em * base_px + desc.letter_spacing;
            if (local_x < pen_x + 0.5f * adv) {
                return line_start + pos;
            }
            pen_x += adv;
            last_run_rtl = false;
            last_run_start = pos;
            ++pos;
            continue;
        }
        uint32_t run_end = pos;
        while (run_end < seg_len && seg[run_end] != '\t') {
            ++run_end;
        }
        const uint32_t run_len = run_end - pos;
        bool is_rtl = false;
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
                cluster_adv += glyphs[k].x_advance_1px * base_px + desc.letter_spacing;
            }
            const uint32_t c1 = is_rtl
                ? ((gi > 0u && glyphs[gi - 1u].cluster > c0) ? glyphs[gi - 1u].cluster : run_len)
                : ((gi_end < gcount && glyphs[gi_end].cluster > c0) ? glyphs[gi_end].cluster : run_len);
            if (local_x < pen_x + cluster_adv && c1 > c0) {
                const uint32_t n_cp = count_utf8_cps(seg + pos + c0, c1 - c0);
                if (is_rtl) {
                    const float rel_x = local_x - pen_x;
                    const uint32_t slot = (cluster_adv > 0.0f && rel_x > 0.0f)
                        ? static_cast<uint32_t>(std::floor((rel_x / cluster_adv) * static_cast<float>(n_cp) + 0.5f))
                        : 0u;
                    const uint32_t clamped_slot = (slot < n_cp) ? slot : n_cp;
                    const uint32_t target_k = n_cp - clamped_slot;
                    const char* cp_p = seg + pos + c0;
                    const char* cl_end = seg + pos + c1;
                    for (uint32_t k = 0u; k < target_k && cp_p < cl_end; ++k) {
                        (void)next_utf8_codepoint(cp_p, cl_end);
                    }
                    return line_start + static_cast<uint32_t>(cp_p - seg);
                }
                const char* cp_p = seg + pos + c0;
                const char* cl_end = seg + pos + c1;
                for (uint32_t k = 0u; k < n_cp && cp_p < cl_end; ++k) {
                    const uint32_t cp_off = static_cast<uint32_t>(cp_p - seg);
                    (void)next_utf8_codepoint(cp_p, cl_end);
                    const float mid = pen_x + ((static_cast<float>(k) + 0.5f) / static_cast<float>(n_cp)) * cluster_adv;
                    if (local_x < mid) {
                        return line_start + cp_off;
                    }
                }
                return line_start + pos + c1;
            }
            pen_x += cluster_adv;
            gi = gi_end;
        }
        last_run_rtl = is_rtl;
        last_run_start = pos;
        pos = run_end;
    }
    return last_run_rtl ? (line_start + last_run_start) : line_end;
}

void move_caret_vertical(
    Context* ctx,
    const TextInputDesc& desc,
    Str text,
    WidgetState* ws,
    int32_t delta,
    bool extend_selection) noexcept
{
    if (!ws || !desc.multiline) {
        return;
    }
    const TextVisualPos cur = text_byte_to_visual(ctx, desc, text, ws->edit_cursor);
    if (ws->edit_preferred_x < 0.0f) {
        ws->edit_preferred_x = cur.x;
    }
    const float base_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f;
    const float line_adv = std::floor(base_px * 1.35f + 0.5f);
    const TextVisualPos total_end = text_byte_to_visual(ctx, desc, text, text.length);

    if (delta < 0) {
        if (cur.line_idx == 0u) {
            ws->edit_cursor = 0u;
        } else {
            const uint32_t target_line = cur.line_idx - 1u;
            ws->edit_cursor = visual_point_to_byte(
                ctx,
                desc,
                text,
                ws->edit_preferred_x,
                (static_cast<float>(target_line) + 0.5f) * line_adv);
        }
    } else if (delta > 0) {
        if (cur.line_idx >= total_end.line_idx) {
            ws->edit_cursor = text.length;
        } else {
            const uint32_t target_line = cur.line_idx + 1u;
            ws->edit_cursor = visual_point_to_byte(
                ctx,
                desc,
                text,
                ws->edit_preferred_x,
                (static_cast<float>(target_line) + 0.5f) * line_adv);
        }
    }
    if (!extend_selection) {
        ws->edit_sel_anchor = ws->edit_cursor;
    }
}

void measure_text_content_and_ime(
    Context* ctx,
    const TextInputDesc& desc,
    Str committed,
    uint32_t cursor_byte,
    bool composing,
    float* out_total_w,
    float* out_total_h,
    float* out_comp_x,
    float* out_comp_y,
    float* out_comp_w,
    float* out_caret_x,
    float* out_caret_y) noexcept
{
    const float base_px = (desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f;
    const float line_adv = desc.multiline ? std::floor(base_px * 1.35f + 0.5f) : base_px;
    const char* s = committed.chars ? committed.chars : "";
    const uint32_t len = committed.length;
    const uint32_t c_byte = utf8_clamp_boundary(s, len, cursor_byte);

    float pen_x = 0.0f;
    float pen_y = 0.0f;
    float max_w = 0.0f;
    float cx = 0.0f;
    float cy = 0.0f;
    float cw = 0.0f;
    float caret_dx = 0.0f;

    if (composing && ctx && ctx->active_ui->ime_comp_len > 0u) {
        const auto walk_slice = [&](const char* beg, uint32_t slice_len, bool mask) noexcept {
            uint32_t pos = 0u;
            while (pos <= slice_len) {
                uint32_t seg_end = pos;
                while (seg_end < slice_len && !(desc.multiline && beg[seg_end] == '\n')) {
                    ++seg_end;
                }
                if (seg_end > pos) {
                    float seg_w = 0.0f;
                    measure_line_segment_x(
                        ctx, desc.font_id, beg + pos, seg_end - pos, 0u, 0u,
                        base_px, desc.letter_spacing, mask, false, nullptr, nullptr, &seg_w);
                    pen_x += seg_w;
                }
                if (pen_x > max_w) {
                    max_w = pen_x;
                }
                if (seg_end >= slice_len) {
                    break;
                }
                pen_x = 0.0f;
                pen_y += line_adv;
                pos = seg_end + 1u;
            }
        };

        walk_slice(s, c_byte, desc.password);
        cx = pen_x;
        cy = pen_y;
        measure_line_segment_x(
            ctx, desc.font_id, ctx->active_ui->ime_comp_utf8, ctx->active_ui->ime_comp_len, ctx->active_ui->ime_comp_cursor_byte, 0u,
            base_px, desc.letter_spacing, false, false, &caret_dx, nullptr, &cw);
        pen_x += cw;
        if (pen_x > max_w) {
            max_w = pen_x;
        }
        if (c_byte < len) {
            walk_slice(s + c_byte, len - c_byte, desc.password);
        }
    } else if (!desc.multiline) {
        measure_line_segment_x(
            ctx, desc.font_id, s, len, c_byte, 0u,
            base_px, desc.letter_spacing, desc.password, false, &cx, nullptr, &max_w);
    } else {
        uint32_t line_start = 0u;
        for (;;) {
            uint32_t line_end = line_start;
            while (line_end < len && s[line_end] != '\n') {
                ++line_end;
            }
            float line_w = 0.0f;
            if (c_byte >= line_start && c_byte <= line_end) {
                cy = pen_y;
                measure_line_segment_x(
                    ctx, desc.font_id, s + line_start, line_end - line_start, c_byte - line_start, 0u,
                    base_px, desc.letter_spacing, desc.password, false, &cx, nullptr, &line_w);
            } else {
                measure_line_segment_x(
                    ctx, desc.font_id, s + line_start, line_end - line_start, 0u, 0u,
                    base_px, desc.letter_spacing, desc.password, false, nullptr, nullptr, &line_w);
            }
            if (line_w > max_w) {
                max_w = line_w;
            }
            if (line_end >= len) {
                break;
            }
            pen_y += line_adv;
            line_start = line_end + 1u;
        }
    }

    if (out_total_w) *out_total_w = max_w;
    if (out_total_h) *out_total_h = pen_y + base_px;
    if (out_comp_x) *out_comp_x = cx;
    if (out_comp_y) *out_comp_y = cy;
    if (out_comp_w) *out_comp_w = cw;
    if (out_caret_x) *out_caret_x = cx + caret_dx;
    if (out_caret_y) *out_caret_y = cy;
}

void update_text_scroll_and_ime(
    Context* ctx,
    WidgetState* ws,
    const TextInputDesc& desc,
    const TextInnerGeom& geom,
    Str committed,
    uint32_t cursor_byte,
    bool is_focused,
    bool composing,
    float* out_scroll_x,
    float* out_scroll_y) noexcept
{
    float total_w = 0.0f;
    float total_h = 0.0f;
    float comp_x = 0.0f;
    float comp_y = 0.0f;
    float comp_w = 0.0f;
    float caret_x = 0.0f;
    float caret_y = 0.0f;
    measure_text_content_and_ime(
        ctx, desc, committed, cursor_byte, composing,
        &total_w, &total_h, &comp_x, &comp_y, &comp_w, &caret_x, &caret_y);

    const float view_w = geom.clip_logical.w;
    const float view_h = geom.clip_logical.h;
    float sx = ws ? ws->edit_scroll_x : 0.0f;
    float sy = ws ? ws->edit_scroll_y : 0.0f;

    if (view_w > 0.0f) {
        const float max_sx = (total_w + 6.0f > view_w) ? (total_w + 6.0f - view_w) : 0.0f;
        if (is_focused) {
            if (caret_x + 4.0f > sx + view_w) {
                sx = (caret_x + 4.0f) - view_w;
            }
            if (caret_x - 4.0f < sx) {
                sx = (caret_x > 4.0f) ? (caret_x - 4.0f) : 0.0f;
            }
        }
        sx = clamp_range(sx, 0.0f, max_sx);
    } else {
        sx = 0.0f;
    }

    if (desc.multiline && view_h > 0.0f) {
        const float max_sy = (total_h > view_h) ? (total_h - view_h) : 0.0f;
        if (is_focused) {
            if (caret_y + geom.line_h > sy + view_h) {
                sy = (caret_y + geom.line_h) - view_h;
            }
            if (caret_y < sy) {
                sy = caret_y;
            }
        }
        sy = clamp_range(sy, 0.0f, max_sy);
    } else {
        sy = 0.0f;
    }

    if (ws) {
        ws->edit_scroll_x = sx;
        ws->edit_scroll_y = sy;
    }
    if (out_scroll_x) *out_scroll_x = sx;
    if (out_scroll_y) *out_scroll_y = sy;

    if (ctx && is_focused && !desc.read_only && !desc.password) {
        const float win_scale = logical_to_window_scale(ctx);
        if (composing && comp_w > 0.0f) {
            ctx->active_ui->ime_cursor_logical = Rect{
                .x = geom.origin_x + comp_x - sx,
                .y = geom.origin_y + comp_y - sy,
                .w = (comp_w > 2.0f) ? comp_w : 2.0f,
                .h = geom.line_h,
            };
            ctx->active_ui->ime_cursor_offset_win = static_cast<int32_t>(std::floor((caret_x - comp_x) * win_scale + 0.5f));
        } else {
            ctx->active_ui->ime_cursor_logical = Rect{
                .x = geom.origin_x + caret_x - sx,
                .y = geom.origin_y + caret_y - sy,
                .w = 2.0f,
                .h = geom.line_h,
            };
            ctx->active_ui->ime_cursor_offset_win = 0;
        }
    }
}

void emit_utf8_glyph_run(
    Context* ctx,
    uint16_t font_id,
    const char* s,
    uint32_t len,
    float font_size_px,
    float letter_spacing,
    float ascender_px,
    float line_adv_logical,
    Color tint,
    float origin_x,
    float origin_y,
    float scroll_x,
    float scroll_y,
    const ClipEntry& clip_px,
    float dpi,
    bool multiline,
    bool mask_bullets,
    float* io_pen_x,
    float* io_pen_y) noexcept
{
    if (!ctx || !ctx->atlas.font_valid || !s || len == 0u || !io_pen_x || !io_pen_y) {
        return;
    }
    const uint16_t primary_slot = resolve_font_slot_idx(&ctx->atlas, font_id);
    if (primary_slot == kInvalidFont) {
        return;
    }
    const FontSlot& primary = ctx->atlas.slots[primary_slot];
    const float base_px = (font_size_px > 0.0f) ? font_size_px : 14.0f;
    const float phys_px = base_px * dpi;

    if (mask_bullets) {
        const float bullet_adv_logical = primary.bullet_advance_em * base_px + letter_spacing;
        const char* p = s;
        const char* end = s + len;
        while (p < end) {
            const uint32_t cp = next_utf8_codepoint(p, end);
            if (cp == '\n') {
                if (multiline) {
                    *io_pen_x = 0.0f;
                    *io_pen_y += line_adv_logical;
                }
                continue;
            }
            const float pen_x_px = (origin_x + *io_pen_x - scroll_x) * dpi;
            const float base_y_px = (origin_y + *io_pen_y - scroll_y) * dpi + ascender_px;
            emit_slug_glyph_quad(
                ctx, primary_slot, primary.bullet_glyph_id, phys_px, pen_x_px, base_y_px, 0.0f, 0.0f, clip_px, tint);
            *io_pen_x += bullet_adv_logical;
        }
        return;
    }

    uint32_t pos = 0u;
    while (pos < len) {
        const char ch = s[pos];
        if (ch == '\n') {
            if (multiline) {
                *io_pen_x = 0.0f;
                *io_pen_y += line_adv_logical;
            }
            ++pos;
            continue;
        }
        if (ch == '\t') {
            *io_pen_x += 4.0f * primary.space_advance_em * base_px + letter_spacing;
            ++pos;
            continue;
        }
        uint32_t run_end = pos;
        while (run_end < len && s[run_end] != '\t' && s[run_end] != '\n') {
            ++run_end;
        }
        const std::span<const ShapedGlyph> glyphs = shape_utf8_run(ctx, primary_slot, s + pos, run_end - pos);
        for (const ShapedGlyph& g : glyphs) {
            const float pen_x_px = (origin_x + *io_pen_x - scroll_x) * dpi;
            const float base_y_px = (origin_y + *io_pen_y - scroll_y) * dpi + ascender_px;
            emit_slug_glyph_quad(
                ctx,
                g.font_slot,
                g.glyph_id,
                phys_px,
                pen_x_px,
                base_y_px,
                g.x_offset_em,
                g.y_offset_em,
                clip_px,
                tint);
            *io_pen_x += g.x_advance_1px * base_px + letter_spacing;
        }
        pos = run_end;
    }
}

void emit_text_input_command(
    Context* ctx,
    const TextInputExtra* ti,
    Clay_BoundingBox bounds_logical,
    Rect box_px,
    const ClipEntry& outer_clip,
    float dpi) noexcept
{
    if (!ctx || !ti) {
        return;
    }
    const TextInputDesc& desc = ti->desc;
    WidgetState* ws = (desc.id != 0u) ? state_map_find(&ctx->active_ui->states, desc.id) : nullptr;
    const Rect box_logical = { bounds_logical.x, bounds_logical.y, bounds_logical.width, bounds_logical.height };
    if (ws) {
        ws->last_box_logical = box_logical;
    }

    const TextInnerGeom geom = compute_text_inner_geom(box_logical, desc);
    const Corners rad_px = scale_corners(desc.radius, dpi);
    const bool is_focused = ti->is_focused;
    const bool composing = is_focused && !desc.read_only && !desc.password && ctx->active_ui->ime_comp_len > 0u;

    if (is_focused && desc.focus_border_color.a > 0u) {
        const Color glow_col = Color::rgba8(desc.focus_border_color.r, desc.focus_border_color.g, desc.focus_border_color.b, 80u);
        push_prim(
            ctx,
            Prim::make_shadow(box_px, outer_clip.rect, glow_col, rad_px, 10.0f * dpi, outer_clip.radius),
            nullptr,
            20.0f * dpi + 2.0f);
    }

    const Color border_col = is_focused ? desc.focus_border_color : desc.border_color;
    const float border_w = (is_focused ? desc.focus_border_width : desc.border_width) * dpi;
    push_prim(
        ctx,
        Prim::make_rect(box_px, outer_clip.rect, desc.background, rad_px, border_col, border_w, 1.0f, outer_clip.radius));

    float scroll_x = 0.0f;
    float scroll_y = 0.0f;
    update_text_scroll_and_ime(
        ctx, ws, desc, geom, ti->committed_text, ti->cursor_byte, is_focused, composing, &scroll_x, &scroll_y);

    const Rect inner_px = {
        geom.clip_logical.x * dpi,
        geom.clip_logical.y * dpi,
        geom.clip_logical.w * dpi,
        geom.clip_logical.h * dpi,
    };
    const ClipEntry inner_clip = {
        .rect = rect_intersect(outer_clip.rect, inner_px),
        .radius = 0.0f,
    };

    const float base_px = geom.line_h;
    const float phys_px = base_px * dpi;
    const uint16_t slot_idx = ctx->atlas.font_valid ? resolve_font_slot_idx(&ctx->atlas, desc.font_id) : kInvalidFont;
    const float ascender_px = (slot_idx != kInvalidFont)
        ? (ctx->atlas.slots[slot_idx].ascent_em * phys_px)
        : (phys_px * 0.8f);

    const char* s = ti->committed_text.chars ? ti->committed_text.chars : "";
    const uint32_t len = ti->committed_text.length;
    const uint32_t c_byte = utf8_clamp_boundary(s, len, ti->cursor_byte);
    const uint32_t a_byte = utf8_clamp_boundary(s, len, ti->sel_anchor_byte);
    const uint32_t sel_min = (c_byte < a_byte) ? c_byte : a_byte;
    const uint32_t sel_max = (c_byte < a_byte) ? a_byte : c_byte;

    // 1. Emit per-line selection highlight quads
    if (sel_max > sel_min && desc.selection_color.a > 0u) {
        const Color sel_col = is_focused
            ? desc.selection_color
            : Color::rgba8(desc.selection_color.r, desc.selection_color.g, desc.selection_color.b, static_cast<uint8_t>(desc.selection_color.a * 45u / 100u));
        uint32_t pos = 0u;
        float line_y = 0.0f;
        while (pos <= len) {
            uint32_t ls = 0u;
            uint32_t le = 0u;
            if (desc.multiline) {
                line_bounds_at(s, len, pos, &ls, &le);
            } else {
                ls = 0u;
                le = len;
            }
            const uint32_t hs = (sel_min > ls) ? sel_min : ls;
            const uint32_t he = (sel_max < le) ? sel_max : le;
            const bool spans_nl = desc.multiline && sel_max > le && sel_min <= le;
            if (he > hs || spans_nl) {
                float x0 = 0.0f;
                float x1 = 0.0f;
                measure_line_segment_x(
                    ctx, desc.font_id, s + ls, le - ls, hs - ls, he - ls,
                    base_px, desc.letter_spacing, desc.password, false, &x0, &x1, nullptr);
                float sx0 = (x0 < x1) ? x0 : x1;
                float sx1 = (x0 < x1) ? x1 : x0;
                if (spans_nl) {
                    sx1 += 6.0f;
                }
                const Rect sel_px = {
                    (geom.origin_x + sx0 - scroll_x) * dpi,
                    (geom.origin_y + line_y - scroll_y - 1.0f) * dpi,
                    ((sx1 > sx0) ? (sx1 - sx0) : 2.0f) * dpi,
                    (geom.line_h + 2.0f) * dpi,
                };
                push_prim(ctx, Prim::make_rect(sel_px, inner_clip.rect, sel_col, Corners::all(2.0f * dpi), {}, 0.0f, 1.0f, 0.0f));
            }
            if (!desc.multiline || le >= len) {
                break;
            }
            pos = le + 1u;
            line_y += geom.line_adv;
        }
    }

    // 2. Emit placeholder or committed text + inline IME preedit composition
    float caret_vx = 0.0f;
    float caret_vy = 0.0f;
    if (len == 0u && !composing) {
        if (desc.placeholder.chars && desc.placeholder.length > 0u) {
            float px = 0.0f;
            float py = 0.0f;
            emit_utf8_glyph_run(
                ctx, desc.font_id, desc.placeholder.chars, desc.placeholder.length,
                base_px, desc.letter_spacing, ascender_px, geom.line_adv,
                desc.placeholder_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
                inner_clip, dpi, desc.multiline, false, &px, &py);
        }
    } else if (composing) {
        float pen_x = 0.0f;
        float pen_y = 0.0f;
        emit_utf8_glyph_run(
            ctx, desc.font_id, s, c_byte, base_px, desc.letter_spacing, ascender_px, geom.line_adv,
            desc.text_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
            inner_clip, dpi, desc.multiline, desc.password, &pen_x, &pen_y);

        const float comp_start_x = pen_x;
        const float comp_start_y = pen_y;
        float comp_w = 0.0f;
        float caret_dx = 0.0f;
        float clause_end_dx = 0.0f;
        uint32_t cl_end = ctx->active_ui->ime_comp_cursor_byte + ctx->active_ui->ime_comp_sel_len_byte;
        if (cl_end > ctx->active_ui->ime_comp_len) {
            cl_end = ctx->active_ui->ime_comp_len;
        }
        measure_line_segment_x(
            ctx, desc.font_id, ctx->active_ui->ime_comp_utf8, ctx->active_ui->ime_comp_len,
            ctx->active_ui->ime_comp_cursor_byte, cl_end,
            base_px, desc.letter_spacing, false, false,
            &caret_dx, &clause_end_dx, &comp_w);
        caret_vx = comp_start_x + caret_dx;
        caret_vy = comp_start_y;

        if (ctx->active_ui->ime_comp_sel_len_byte > 0u) {
            const float cl_left = (caret_dx < clause_end_dx) ? caret_dx : clause_end_dx;
            const float cl_w = (caret_dx < clause_end_dx) ? (clause_end_dx - caret_dx) : (caret_dx - clause_end_dx);
            if (cl_w > 0.0f) {
                const Rect clause_bg = {
                    (geom.origin_x + comp_start_x + cl_left - scroll_x) * dpi,
                    (geom.origin_y + comp_start_y - scroll_y - 1.0f) * dpi,
                    cl_w * dpi,
                    (geom.line_h + 2.0f) * dpi,
                };
                const Color clause_col = Color::rgba8(
                    desc.composition_color.r, desc.composition_color.g, desc.composition_color.b, 48u);
                push_prim(ctx, Prim::make_rect(clause_bg, inner_clip.rect, clause_col, Corners::all(2.0f * dpi)));
            }
        }

        emit_utf8_glyph_run(
            ctx, desc.font_id, ctx->active_ui->ime_comp_utf8, ctx->active_ui->ime_comp_len, base_px, desc.letter_spacing, ascender_px, geom.line_adv,
            desc.composition_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
            inner_clip, dpi, false, false, &pen_x, &pen_y);

        if (comp_w > 0.0f) {
            const Rect uline = {
                (geom.origin_x + comp_start_x - scroll_x) * dpi,
                (geom.origin_y + comp_start_y + geom.line_h - scroll_y + 0.5f) * dpi,
                comp_w * dpi,
                (1.5f * dpi > 1.0f) ? (1.5f * dpi) : 1.0f,
            };
            push_prim(ctx, Prim::make_rect(uline, inner_clip.rect, desc.composition_color));
        }

        if (c_byte < len) {
            emit_utf8_glyph_run(
                ctx, desc.font_id, s + c_byte, len - c_byte, base_px, desc.letter_spacing, ascender_px, geom.line_adv,
                desc.text_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
                inner_clip, dpi, desc.multiline, desc.password, &pen_x, &pen_y);
        }
    } else {
        float pen_x = 0.0f;
        float pen_y = 0.0f;
        emit_utf8_glyph_run(
            ctx, desc.font_id, s, len, base_px, desc.letter_spacing, ascender_px, geom.line_adv,
            desc.text_color, geom.origin_x, geom.origin_y, scroll_x, scroll_y,
            inner_clip, dpi, desc.multiline, desc.password, &pen_x, &pen_y);
        const TextVisualPos cpos = text_byte_to_visual(ctx, desc, ti->committed_text, c_byte);
        caret_vx = cpos.x;
        caret_vy = cpos.y;
    }

    // 3. Emit SDF caret
    if (is_focused && ctx->active_ui->caret_blink_on && desc.caret_color.a > 0u) {
        const float cw_px = (2.0f * dpi > 1.5f) ? (2.0f * dpi) : 1.5f;
        const Rect caret_px = {
            std::floor((geom.origin_x + caret_vx - scroll_x) * dpi + 0.5f),
            (geom.origin_y + caret_vy - scroll_y - 1.0f) * dpi,
            cw_px,
            (geom.line_h + 2.0f) * dpi,
        };
        push_prim(ctx, Prim::make_rect(caret_px, inner_clip.rect, desc.caret_color, Corners::all(0.5f * cw_px)));
    }
}

constexpr uint16_t sdl_mod_to_etch(uint16_t sdl_mod) noexcept {
    uint16_t out = Mod_None;
    if ((sdl_mod & SDL_KMOD_SHIFT) != 0u) out = static_cast<uint16_t>(out | Mod_Shift);
    if ((sdl_mod & SDL_KMOD_CTRL)  != 0u) out = static_cast<uint16_t>(out | Mod_Ctrl);
    if ((sdl_mod & SDL_KMOD_ALT)   != 0u) out = static_cast<uint16_t>(out | Mod_Alt);
    if ((sdl_mod & SDL_KMOD_GUI)   != 0u) out = static_cast<uint16_t>(out | Mod_Super);
    return out;
}

constexpr uint16_t etch_mod_to_sdl(uint16_t emod) noexcept {
    uint16_t out = 0u;
    if ((emod & Mod_Shift) != 0u) out = static_cast<uint16_t>(out | SDL_KMOD_SHIFT);
    if ((emod & Mod_Ctrl)  != 0u) out = static_cast<uint16_t>(out | SDL_KMOD_CTRL);
    if ((emod & Mod_Alt)   != 0u) out = static_cast<uint16_t>(out | SDL_KMOD_ALT);
    if ((emod & Mod_Super) != 0u) out = static_cast<uint16_t>(out | SDL_KMOD_GUI);
    return out;
}

WidgetState* find_topmost_user_element(
    Context* ctx,
    Clay_ElementIdArray over,
    uint32_t hovered_span_id) noexcept
{
    if (hovered_span_id != 0u) {
        if (WidgetState* sws = state_map_find(&ctx->active_ui->states, hovered_span_id)) {
            if (sws->is_user_element) {
                return sws;
            }
        }
    }
    for (int32_t i = over.length - 1; i >= 0; --i) {
        WidgetState* ws = state_map_find(&ctx->active_ui->states, over.internalArray[i].id);
        if (ws && ws->is_user_element) {
            return ws;
        }
    }
    return nullptr;
}

bool is_single_nonbreak_utf8_codepoint(Str ins) noexcept {
    if (!ins.chars || ins.length == 0u || ins.length > 4u) {
        return false;
    }
    const char* p = ins.chars;
    const char* end = ins.chars + ins.length;
    const uint32_t cp = next_utf8_codepoint(p, end);
    if (p != end) {
        return false;
    }
    return cp != ' ' && cp != '\n' && cp != '\t';
}

void undo_reset_slot(TextUndoSlot* slot) noexcept {
    slot->head = 0u;
    slot->count = 0u;
    slot->cur = 0u;
    slot->write_pos = 0u;
    slot->bytes_used = 0u;
    slot->last_coalesce_sec = -10.0f;
    slot->last_op_group = 0u;
}

TextUndoSlot* undo_acquire_slot(Context* ctx, uint32_t widget_id, uint32_t frame_index) noexcept {
    TextUndoSlot* lru = &ctx->active_ui->undo_pool[0];
    for (uint32_t i = 0u; i < kTextUndoSlots; ++i) {
        TextUndoSlot* s = &ctx->active_ui->undo_pool[i];
        if (s->widget_id == widget_id) {
            s->last_used_frame = frame_index;
            return s;
        }
        if (s->widget_id == 0u) {
            s->widget_id = widget_id;
            s->last_used_frame = frame_index;
            undo_reset_slot(s);
            return s;
        }
        if (s->last_used_frame < lru->last_used_frame) {
            lru = s;
        }
    }
    lru->widget_id = widget_id;
    lru->last_used_frame = frame_index;
    undo_reset_slot(lru);
    return lru;
}

void undo_pop_tail(TextUndoSlot* slot) noexcept {
    const uint32_t tail_idx = (slot->head + slot->count - 1u) % kTextUndoMaxEntries;
    const TextUndoRecord& tail = slot->entries[tail_idx];
    slot->write_pos = tail.offset;
    slot->bytes_used = static_cast<uint16_t>(slot->bytes_used - tail.length);
    --slot->count;
}

void undo_push_snapshot(
    TextUndoSlot* slot,
    const char* buf,
    uint32_t len,
    uint32_t cursor,
    uint32_t anchor,
    float elapsed_sec,
    uint8_t op_group) noexcept
{
    if (len > kTextUndoByteCap) {
        undo_reset_slot(slot);
        return;
    }
    while (slot->count > 0u && slot->count > slot->cur + 1u) {
        undo_pop_tail(slot);
    }
    const float dt = elapsed_sec - slot->last_coalesce_sec;
    if (op_group != 0u && op_group == slot->last_op_group &&
        slot->count >= 2u && dt > 0.0f && dt <= 0.30f)
    {
        undo_pop_tail(slot);
    }
    while (slot->count > 0u &&
           (slot->count >= kTextUndoMaxEntries || uint32_t(slot->bytes_used) + len > kTextUndoByteCap))
    {
        const TextUndoRecord& oldest = slot->entries[slot->head];
        slot->bytes_used = static_cast<uint16_t>(slot->bytes_used - oldest.length);
        slot->head = (slot->head + 1u) % kTextUndoMaxEntries;
        --slot->count;
    }
    if (slot->count == 0u) {
        slot->head = 0u;
        slot->write_pos = 0u;
        slot->bytes_used = 0u;
    }
    const uint32_t hash = (len > 0u && buf) ? fnv1a_32_step(2166136261u, buf, len) : 2166136261u;
    const uint16_t start_off = slot->write_pos;
    if (len > 0u && buf) {
        const uint32_t first = kTextUndoByteCap - start_off;
        if (len <= first) {
            std::memcpy(slot->bytes + start_off, buf, len);
        } else {
            std::memcpy(slot->bytes + start_off, buf, first);
            std::memcpy(slot->bytes, buf + first, len - first);
        }
    }
    const uint32_t ins_idx = (slot->head + slot->count) % kTextUndoMaxEntries;
    slot->entries[ins_idx] = TextUndoRecord{
        .offset = start_off,
        .length = static_cast<uint16_t>(len),
        .cursor = cursor,
        .anchor = anchor,
        .hash = hash,
    };
    slot->write_pos = static_cast<uint16_t>((uint32_t(start_off) + len) % kTextUndoByteCap);
    slot->bytes_used = static_cast<uint16_t>(slot->bytes_used + len);
    slot->cur = slot->count;
    ++slot->count;
    slot->last_coalesce_sec = elapsed_sec;
    slot->last_op_group = op_group;
}

bool undo_restore(
    TextUndoSlot* slot,
    uint32_t target_cur,
    char* buf,
    uint32_t cap,
    uint32_t* io_len,
    uint32_t* io_cursor,
    uint32_t* io_anchor) noexcept
{
    if (!slot || !buf || cap == 0u || target_cur >= slot->count) {
        return false;
    }
    const TextUndoRecord& rec = slot->entries[(slot->head + target_cur) % kTextUndoMaxEntries];
    const uint32_t max_copy = cap - 1u;
    const uint32_t to_copy = (rec.length <= max_copy) ? rec.length : max_copy;
    if (to_copy > 0u) {
        const uint32_t first = kTextUndoByteCap - rec.offset;
        if (to_copy <= first) {
            std::memcpy(buf, slot->bytes + rec.offset, to_copy);
        } else {
            std::memcpy(buf, slot->bytes + rec.offset, first);
            std::memcpy(buf + first, slot->bytes, to_copy - first);
        }
    }
    uint32_t clamped_len = to_copy;
    if (clamped_len > 0u && to_copy < rec.length) {
        const uint32_t lead = utf8_prev_boundary(buf, to_copy, to_copy);
        const uint8_t lb = static_cast<uint8_t>(buf[lead]);
        const uint32_t cp_bytes = (lb < 0x80u) ? 1u
            : ((lb & 0xE0u) == 0xC0u) ? 2u
            : ((lb & 0xF0u) == 0xE0u) ? 3u
            : (((lb & 0xF8u) == 0xF0u) ? 4u : 1u);
        if (lead + cp_bytes > to_copy) {
            clamped_len = lead;
        }
    }
    buf[clamped_len] = '\0';
    *io_len = clamped_len;
    *io_cursor = utf8_clamp_boundary(buf, clamped_len, rec.cursor);
    *io_anchor = utf8_clamp_boundary(buf, clamped_len, rec.anchor);
    slot->cur = target_cur;
    slot->last_op_group = 0u;
    return true;
}

void emit_vector_icon_quad(
    Context* ctx,
    uint16_t icon_id,
    Rect target_px,
    Color color,
    const ClipEntry& clip) noexcept
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
            PrimFlag_None));
}

DrawCmdNode* append_open_box_draw_cmd(Context* ctx, bool foreground) noexcept {
    if (!ctx || !ctx->in_frame || !ctx->active_ui->clay_ctx) {
        return nullptr;
    }
    Clay_SetCurrentContext(ctx->active_ui->clay_ctx);
    if (ctx->active_ui->clay_ctx->openLayoutElementStack.length < 2) {
        return nullptr;
    }
    Clay_LayoutElement* open_elem = Clay__GetOpenLayoutElement();
    if (!open_elem || open_elem->isTextElement) {
        return nullptr;
    }
    BoxExtra* extra = static_cast<BoxExtra*>(open_elem->config.userData);
    if (!extra) {
        extra = ctx->frame_arena.alloc<BoxExtra>();
        if (!extra) {
            return nullptr;
        }
        extra->kind = CustomCmdKind::Box;
        open_elem->config.userData = extra;
    }
    if (!foreground) {
        if (open_elem->config.custom.customData == nullptr) {
            open_elem->config.custom.customData = extra;
        }
    } else {
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
    node->world_scale = current_world_scale(ctx);
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
    const ClipEntry& clip) noexcept
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
        if (tc.align_x != AlignX::Left && seg_len > 0u) {
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

        float start_x_logical = tc.pos.x;
        if (tc.align_x == AlignX::Center) {
            start_x_logical -= line_w_logical * 0.5f;
        } else if (tc.align_x == AlignX::Right) {
            start_x_logical -= line_w_logical;
        }

        float pen_x = origin.x + start_x_logical * scale;
        const float base_y = origin.y + (start_y_logical + static_cast<float>(line_idx) * line_h_logical) * scale + asc_px;

        uint32_t run_pos = pos;
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
            for (const ShapedGlyph& g : glyphs) {
                emit_slug_glyph_quad(
                    ctx,
                    g.font_slot,
                    g.glyph_id,
                    phys_px,
                    pen_x,
                    base_y,
                    g.x_offset_em,
                    g.y_offset_em,
                    clip,
                    tc.color);
                pen_x += g.x_advance_1px * phys_px + ls_px;
            }
            run_pos = run_end;
        }

        ++line_idx;
        if (end >= tc.text.length) {
            break;
        }
        pos = end + 1u;
    }
}

void emit_draw_list_command(
    Context* ctx,
    const DrawCmdNode* head,
    Rect box_px,
    const ClipEntry& clip,
    float dpi) noexcept
{
    const Vec2 origin = { box_px.x, box_px.y };
    for (const DrawCmdNode* node = head; node != nullptr; node = node->next) {
        const float scale = node->world_scale * dpi;
        switch (node->kind) {
            case DrawCmdKind::Rect: {
                const RectDrawDesc& rd = node->rect;
                const Rect r_px = {
                    .x = origin.x + rd.rect.x * scale,
                    .y = origin.y + rd.rect.y * scale,
                    .w = rd.rect.w * scale,
                    .h = rd.rect.h * scale,
                };
                const Corners rad_px = scale_corners(rd.radius, scale);
                const float bw_px = rd.border_width * scale;
                const bool has_grad = (rd.fill_end.a > 0u) || rd.radial_gradient;
                if (has_grad) {
                    push_prim(
                        ctx,
                        Prim::make_gradient_rect(
                            r_px,
                            clip.rect,
                            rd.fill,
                            rd.fill_end,
                            rd.gradient_dir,
                            rad_px,
                            rd.border_color,
                            bw_px,
                            clip.radius,
                            rd.radial_gradient));
                } else if (rd.fill.a > 0u || (bw_px > 0.0f && rd.border_color.a > 0u)) {
                    push_prim(
                        ctx,
                        Prim::make_rect(
                            r_px,
                            clip.rect,
                            rd.fill,
                            rad_px,
                            rd.border_color,
                            bw_px,
                            1.0f,
                            clip.radius));
                }
                break;
            }
            case DrawCmdKind::Line: {
                const LineDesc& ld = node->line;
                const Vec2 a = origin + ld.from * scale;
                const Vec2 b = origin + ld.to * scale;
                const Vec2 mid = (a + b) * 0.5f;
                const float hw = 0.5f * ((ld.thickness * scale > 0.75f) ? (ld.thickness * scale) : 0.75f);
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
                emit_vector_icon_quad(ctx, ic.icon_id, target_px, ic.color, clip);
                break;
            }
            case DrawCmdKind::Text: {
                emit_draw_text_cmd(ctx, node->text_cmd, origin, scale, clip);
                break;
            }
            case DrawCmdKind::Image: {
                const ImageDrawDesc& im = node->image_cmd;
                if (!im.texture || im.tint.a == 0u || im.rect.w <= 0.0f || im.rect.h <= 0.0f) {
                    break;
                }
                const Rect target_px = {
                    .x = origin.x + im.rect.x * scale,
                    .y = origin.y + im.rect.y * scale,
                    .w = im.rect.w * scale,
                    .h = im.rect.h * scale,
                };
                const Vec2 uv_min = { im.uv_rect.x, im.uv_rect.y };
                const Vec2 uv_max = { im.uv_rect.x + im.uv_rect.w, im.uv_rect.y + im.uv_rect.h };
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
                        PrimFlag_None,
                        clip.radius),
                    im.texture);
                break;
            }
        }
    }
}

} // namespace

bool needs_redraw(const Context* ctx) noexcept {
    if (!ctx) return false;
    if (ctx->headless_ui.redraw_budget || ctx->headless_ui.active_anim_count) return true;
    for (uint32_t i = 0; i < kMaxClaimedWindows; ++i) {
        const ClaimedWindowSlot& slot = ctx->claimed_windows[i];
        if (slot.window && (slot.ui.redraw_budget || slot.ui.active_anim_count)) return true;
    }
    return false;
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
    if (needs_redraw(ctx)) {
        return SDL_PollEvent(first_event_out);
    }
    WindowUiState* caret_ui = ctx ? window_ui(ctx, ctx->ime_active_window) : nullptr;
    if (ctx && caret_ui->ime_focused_id != 0u && caret_ui->caret_blink_cycles < 20u && (timeout_ms < 0 || timeout_ms > 500)) {
        if (SDL_WaitEventTimeout(first_event_out, 500)) {
            return true;
        }
        caret_ui->caret_blink_on = !caret_ui->caret_blink_on;
        ++caret_ui->caret_blink_cycles;
        if (caret_ui->caret_blink_cycles >= 20u) {
            caret_ui->caret_blink_on = true;
        }
        caret_ui->redraw_budget = 2;
        return false;
    }
    if (timeout_ms < 0) {
        return SDL_WaitEvent(first_event_out);
    }
    return SDL_WaitEventTimeout(first_event_out, timeout_ms);
}

static void process_window_event(Context* ctx, const SDL_Event& event) noexcept {
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
            if (ctx->active_ui->ime_comp_len > 0u) {
                if (event.key.key == SDLK_ESCAPE) {
                    clear_ime_composition(ctx);
                    ctx->active_ui->pending_escape_consumed_by_ime = true;
                    if (!ctx->headless && ctx->active_window) {
                        SDL_ClearComposition(ctx->active_window);
                    }
                }
            } else {
                if (event.key.key == SDLK_TAB) {
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
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
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
    ctx->draw_list_window = ctx->active_window;
    ctx->active_ui->focus_scope_depth = 0;
    ctx->active_ui->focus_scope = 0;
    ctx->layout_start_ticks = SDL_GetPerformanceCounter();
    ++ctx->active_ui->frame_index;
    ctx->active_ui->elapsed_seconds += desc.dt_seconds;

    if (ctx->active_ui->redraw_budget > 0u) {
        --ctx->active_ui->redraw_budget;
    }
    ctx->active_ui->sim_dt = (desc.dt_seconds <= 0.0f)
        ? 0.0f
        : ((desc.dt_seconds > 0.1f) ? (1.0f / 60.0f) : desc.dt_seconds);
    ctx->active_ui->active_anim_count = 0u;
    ctx->active_ui->desired_cursor = Cursor::Arrow;

    ctx->active_ui->prev_ime_focused_id = ctx->active_ui->ime_focused_id;
    ctx->active_ui->ime_focused_id = 0u;
    ctx->active_ui->ime_focused_password = false;
    ctx->active_ui->frame_text_op_count = ctx->active_ui->pending_text_op_count;
    if (ctx->active_ui->frame_text_op_count > 0u) {
        std::memcpy(ctx->active_ui->frame_text_ops, ctx->active_ui->pending_text_ops, size_t(ctx->active_ui->frame_text_op_count) * sizeof(PendingTextOp));
    }
    if (ctx->active_ui->pending_text_utf8_len > 0u) {
        std::memcpy(ctx->active_ui->frame_text_utf8, ctx->active_ui->pending_text_utf8, ctx->active_ui->pending_text_utf8_len);
    }
    ctx->active_ui->pending_text_op_count = 0u;
    ctx->active_ui->pending_text_utf8_len = 0u;

    uint16_t kmods = static_cast<uint16_t>(desc.key_mods | ctx->active_ui->event_key_mods);
    ctx->active_ui->frame_key_mods = kmods;
    ctx->active_ui->frame_escape_consumed_by_ime = ctx->active_ui->pending_escape_consumed_by_ime;
    ctx->active_ui->pending_escape_consumed_by_ime = false;

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
            if (desc.key_pressed_code == SDLK_TAB) {
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

    ctx->active_ui->frame_drop_count = 0;
    ctx->active_ui->frame_text_drop_count = 0;
    if (!ctx->active_ui->os_dragging &&
        (ctx->active_ui->pending_drop_count > 0 || ctx->active_ui->pending_text_drop_count > 0)) {
        ctx->active_ui->frame_drop_count = ctx->active_ui->pending_drop_count;
        if (ctx->active_ui->pending_drop_bytes > 0u) {
            std::memcpy(ctx->active_ui->frame_drop_buf, ctx->active_ui->pending_drop_buf, ctx->active_ui->pending_drop_bytes);
            for (uint32_t i = 0u; i < ctx->active_ui->frame_drop_count; ++i) {
                ctx->active_ui->frame_drops[i] = Str{
                    ctx->active_ui->frame_drop_buf + ctx->active_ui->pending_drop_offsets[i],
                    ctx->active_ui->pending_drop_lengths[i],
                };
            }
        }
        ctx->active_ui->frame_text_drop_count = ctx->active_ui->pending_text_drop_count;
        for (uint32_t i = 0; i < ctx->active_ui->frame_text_drop_count; ++i)
            ctx->active_ui->frame_text_drops[i] = Str{ctx->active_ui->frame_drop_buf + ctx->active_ui->pending_text_drop_offsets[i], ctx->active_ui->pending_text_drop_lengths[i]};
        ctx->active_ui->pending_text_drop_count = 0;
        ctx->active_ui->pending_drop_count = 0u;
        ctx->active_ui->pending_drop_bytes = 0u;
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
        request_redraw(ctx);
    }

    ctx->frame_arena.reset();
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

    const Clay_Dimensions logical_dims = {
        .width = static_cast<float>(ctx->active_ui->viewport_w) / dpi,
        .height = static_cast<float>(ctx->active_ui->viewport_h) / dpi,
    };
    Clay_SetLayoutDimensions(logical_dims);

    ctx->active_ui->prev_pointer_pos = ctx->active_ui->pointer_pos;
    Vec2 pointer_pos = desc.pointer_pos;
    if (pointer_pos.x < 0.0f && pointer_pos.y < 0.0f && ctx->active_ui->has_event_pointer) {
        pointer_pos = ctx->active_ui->event_pointer_pos;
    }
    ctx->active_ui->pointer_pos = pointer_pos;
    if (ctx->active_ui->prev_pointer_pos.x >= 0.0f && ctx->active_ui->prev_pointer_pos.y >= 0.0f &&
        pointer_pos.x >= 0.0f && pointer_pos.y >= 0.0f)
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
    ctx->active_ui->event_scroll_delta = {};

    // Update Clay hit-test results against the previous frame's tree BEFORE Clay_BeginLayout() resets it.
    Clay_SetPointerState(Clay_Vector2{pointer_pos.x, pointer_pos.y}, pointer_down);
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
        ctx->active_ui->right_press_origin.x >= 0.0f && pointer_pos.x >= 0.0f)
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
        if (!drag_exceeded && ctx->active_ui->right_press_origin.x >= 0.0f && pointer_pos.x >= 0.0f) {
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
                if (!ws || !ws->is_user_element || (!ws->is_scroll_y && !ws->is_virtual_list)) {
                    continue;
                }
                const ScrollbarGeom sb = compute_vscrollbar_geom(ws);
                if (sb.visible && sb.hit_track.contains(pointer_pos)) {
                    ctx->active_ui->active_id = id_index(ws->key, kScrollbarYTag);
                    ws->scroll_vel.y = 0.0f;
                    ws->vscroll_vel_y = 0.0;
                    if (pointer_pos.y >= sb.thumb.y && pointer_pos.y <= (sb.thumb.y + sb.thumb.h)) {
                        ctx->active_ui->scrollbar_drag_grab_offset = pointer_pos.y - sb.thumb.y;
                    } else {
                        ctx->active_ui->scrollbar_drag_grab_offset = 0.5f * sb.thumb.h;
                        apply_scrollbar_drag(ws, pointer_pos.y, ctx->active_ui->scrollbar_drag_grab_offset);
                    }
                    request_redraw(ctx);
                    handled_scrollbar = true;
                    break;
                }
            }
            if (!handled_scrollbar) {
                uint32_t top_hit = 0u;
                uint32_t top_focus = 0u;
                if (hovered_span_id != 0u) {
                    if (WidgetState* sws = state_map_find(&ctx->active_ui->states, hovered_span_id)) {
                        if (sws->is_user_element) {
                            top_hit = hovered_span_id;
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
        if (ctx->active_ui->left_press_origin.x >= 0.0f && pointer_pos.x >= 0.0f) {
            const float dx = pointer_pos.x - ctx->active_ui->left_press_origin.x;
            const float dy = pointer_pos.y - ctx->active_ui->left_press_origin.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 > ctx->active_ui->left_max_drag_dist_sq) {
                ctx->active_ui->left_max_drag_dist_sq = d2;
            }
        }
        if (ctx->active_ui->marquee_canvas_id != 0u && pointer_pos.x >= 0.0f && pointer_pos.y >= 0.0f) {
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
            apply_scrollbar_drag(sb_ws, pointer_pos.y, ctx->active_ui->scrollbar_drag_grab_offset);
            ctx->active_ui->desired_cursor = Cursor::Hand;
        }
    } else if (released_this_frame) {
        if (ctx->active_ui->mouse_captured && ctx->active_ui->pan_canvas_id == 0u) {
            if (ctx->mouse_capture_window == ctx->active_window) { SDL_CaptureMouse(false); ctx->mouse_capture_window = nullptr; }
            ctx->active_ui->mouse_captured = false;
        }
        if (ctx->active_ui->marquee_canvas_id != 0u) {
            if (WidgetState* mws = state_map_find(&ctx->active_ui->states, ctx->active_ui->marquee_canvas_id)) {
                if (pointer_pos.x >= 0.0f && pointer_pos.y >= 0.0f) {
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
            if (!pan_started_this_frame && ctx->active_ui->prev_pointer_pos.x >= 0.0f && pointer_pos.x >= 0.0f) {
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
            if (vp.w > 0.0f && vp.h > 0.0f && pointer_pos.x >= 0.0f && pointer_pos.y >= 0.0f) {
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

    const float wscale = current_world_scale(ctx);
    BoxDesc desc = desc_in;
    if (wscale != 1.0f) {
        desc.width = scale_sizing(desc.width, wscale);
        desc.height = scale_sizing(desc.height, wscale);
        desc.padding = scale_padding(desc.padding, wscale);
        desc.gap *= wscale;
        desc.border_width *= wscale;
        desc.radius = scale_corners_uniform(desc.radius, wscale);
        desc.shadow_blur *= wscale;
        desc.shadow_offset.x *= wscale;
        desc.shadow_offset.y *= wscale;
        desc.floating_offset.x *= wscale;
        desc.floating_offset.y *= wscale;
    }

    WidgetState* box_ws = nullptr;
    if (desc.id != 0u) {
        box_ws = state_map_get_or_insert(&ctx->active_ui->states, desc.id, ctx->active_ui->frame_index);
        if (box_ws) {
            box_ws->is_user_element = true;
            box_ws->is_focusable = desc.focusable;
            box_ws->focus_scope = ctx->active_ui->modal_scope && focus_scope_contains(*ctx->active_ui, ctx->active_ui->modal_scope) ? ctx->active_ui->modal_scope : ctx->active_ui->focus_scope;
            box_ws->is_scroll_x = desc.scroll_x;
            box_ws->is_scroll_y = desc.scroll_y;
            box_ws->is_text_input_writable = false;
            box_ws->owner_canvas_id = (ctx->active_ui->canvas_depth > 0u)
                ? ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth - 1u].id
                : 0u;
        }
        if (desc.focusable && ctx->active_ui->focus_count < kMaxFocusablePerFrame) {
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

    const bool focus_ring = desc.focusable && keyboard_focus_visible(desc.id);
    const bool is_scroll = desc.scroll_x || desc.scroll_y;
    const bool any_clip = desc.clip_children || is_scroll;
    const bool is_floating_blocker = (desc.attach_to != AttachTo::None && !desc.pointer_passthrough);
    const bool has_border = (desc.border_width > 0.0f && desc.border_color.a > 0u);
    const bool has_scrollbar = is_scroll && desc.show_scrollbar;
    const bool has_visual =
        desc.background.a > 0u ||
        desc.background_end.a > 0u ||
        desc.radial_gradient ||
        (desc.shadow_color.a > 0u && desc.shadow_blur > 0.0f) ||
        desc.backdrop_blur > 0.0f ||
        has_border || focus_ring;

    BoxExtra* extra = nullptr;
    if (has_visual || any_clip || is_floating_blocker || desc.id) {
        extra = ctx->frame_arena.alloc<BoxExtra>();
        if (extra) {
            extra->desc = desc;
            extra->keyboard_focus_ring = focus_ring;
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
    decl.layout.childGap = to_u16_round(desc.gap);
    decl.layout.childAlignment = Clay_ChildAlignment{
        .x = to_clay_align_x(desc.align_x),
        .y = to_clay_align_y(desc.align_y),
    };
    decl.layout.layoutDirection = to_clay_axis(desc.axis);
    decl.cornerRadius = to_clay_corners(desc.radius);
    decl.userData = extra;

    if ((has_visual || is_floating_blocker || desc.id) && extra) {
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
        decl.clip.horizontal = desc.clip_children || desc.scroll_x;
        decl.clip.vertical = desc.clip_children || desc.scroll_y;
        if (is_scroll && box_ws) {
            decl.clip.childOffset = Clay_Vector2{ -box_ws->scroll_pos.x, -box_ws->scroll_pos.y };
        }
        if (has_border || has_scrollbar) {
            // Ask Clay to emit CLAY_RENDER_COMMAND_TYPE_BORDER after children for scrollbar + crisp outer rim.
            decl.border.color = Clay_Color{255.0f, 255.0f, 255.0f, 1.0f};
            decl.border.width = Clay_BorderWidth{1u, 1u, 1u, 1u, 0u};
        }
    }

    Clay__ConfigureOpenElement(decl);
}

void end_box() noexcept {
    if (!current_context() || !current_context()->in_frame) {
        return;
    }
    Clay__CloseElement();
}

void box(const BoxDesc& desc) noexcept {
    begin_box(desc);
    end_box();
}

void text(Str content, const TextDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || !content.chars || content.length == 0u) {
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

    Clay_TextElementConfig cfg = {};
    cfg.textColor = to_clay_color(desc.color);
    const uint16_t slot_bits = (desc.font_id < kMaxFontSlots) ? desc.font_id : kFontSans;
    cfg.fontId = static_cast<uint16_t>((slot_bits & 0x7Fu) | ((frac_px & 0xFFu) << 7));
    cfg.fontSize = static_cast<uint16_t>(int_px);
    cfg.letterSpacing = to_u16_round(desc.letter_spacing * wscale);
    cfg.lineHeight = 0u;
    cfg.wrapMode = desc.wrap ? CLAY_TEXT_WRAP_WORDS : CLAY_TEXT_WRAP_NONE;

    Clay__OpenTextElement(clay_str, cfg);
}

void text_spans(std::span<const TextSpan> spans, const RichTextDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || spans.empty()) {
        return;
    }

    uint32_t valid_count = 0u;
    uint32_t total_bytes = 0u;
    for (const TextSpan& sp : spans) {
        if (!sp.text.chars || sp.text.length == 0u) {
            continue;
        }
        if (sp.text.length > (0x3FFFFFFFu - total_bytes)) {
            return;
        }
        total_bytes += sp.text.length;
        ++valid_count;
    }
    if (valid_count == 0u || total_bytes == 0u) {
        return;
    }

    char* full_utf8 = ctx->frame_arena.alloc_uninit<char>(total_bytes + 1u);
    RichSpanInternal* int_spans = ctx->frame_arena.alloc_uninit<RichSpanInternal>(valid_count);
    RichTextExtra* rich = ctx->frame_arena.alloc<RichTextExtra>();
    if (!full_utf8 || !int_spans || !rich) {
        return;
    }

    const float wscale = current_world_scale(ctx);
    const float base_px = clamp_range(
        ((desc.font_size_px > 0.0f) ? desc.font_size_px : 14.0f) * wscale,
        0.25f,
        4096.0f);

    uint32_t cursor = 0u;
    uint32_t out_idx = 0u;
    float max_asc = 0.0f;
    float max_desc = 0.0f;
    float max_lh = 0.0f;
    uint32_t cfg_hash = 2166136261u;
    cfg_hash = fnv1a_32_step(cfg_hash, &ctx->atlas.font_revision, sizeof(ctx->atlas.font_revision));

    for (const TextSpan& sp : spans) {
        if (!sp.text.chars || sp.text.length == 0u) {
            continue;
        }
        const uint32_t b0 = cursor;
        std::memcpy(full_utf8 + cursor, sp.text.chars, sp.text.length);
        cursor += sp.text.length;
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
        if (sp_lh > max_lh) max_lh = sp_lh;

        cfg_hash = fnv1a_32_step(cfg_hash, &b0, sizeof(b0));
        cfg_hash = fnv1a_32_step(cfg_hash, &b1, sizeof(b1));
        cfg_hash = fnv1a_32_step(cfg_hash, &sp_px, sizeof(sp_px));
        cfg_hash = fnv1a_32_step(cfg_hash, &metric_h, sizeof(metric_h));
        cfg_hash = fnv1a_32_step(cfg_hash, &eff_ls, sizeof(eff_ls));
        cfg_hash = fnv1a_32_step(cfg_hash, &eff_pad, sizeof(eff_pad));

        int_spans[out_idx++] = RichSpanInternal{
            .byte_start = b0,
            .byte_end = b1,
            .color = sp.color,
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
        };
    }
    full_utf8[total_bytes] = '\0';

    const float final_lh = (desc.line_height_px > 0.0f) ? (desc.line_height_px * wscale) : max_lh;
    cfg_hash = fnv1a_32_step(cfg_hash, &final_lh, sizeof(final_lh));

    rich->spans = int_spans;
    rich->span_count = out_idx;
    rich->full_utf8 = full_utf8;
    rich->total_bytes = total_bytes;
    rich->line_height_px = final_lh;
    rich->max_ascent_px = max_asc;
    rich->max_descent_px = max_desc;
    rich->align_x = desc.align_x;
    rich->config_hash = cfg_hash;

    Clay_TextElementConfig cfg = {};
    cfg.userData = rich;
    cfg.textColor = to_clay_color(desc.color);
    cfg.fontId = static_cast<uint16_t>((cfg_hash & 0x7FFFu) | 0x8000u);
    cfg.fontSize = static_cast<uint16_t>(cfg_hash >> 16);
    cfg.letterSpacing = 0u;
    cfg.lineHeight = 0u;
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
    }
}

void image(const ImageDesc& desc_in) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame) {
        return;
    }

    const float wscale = current_world_scale(ctx);
    ImageDesc desc = desc_in;
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
    extra->desc = desc;

    if (desc.id != 0u) {
        Clay__OpenElementWithId(make_clay_id(desc.id));
    } else {
        Clay__OpenElement();
    }

    Clay_ElementDeclaration decl = {};
    decl.layout.sizing.width = to_clay_sizing(desc.width);
    decl.layout.sizing.height = to_clay_sizing(desc.height);
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
        extra->kind = CustomCmdKind::Icon;
        extra->icon_desc = desc;
        decl.custom.customData = extra;
        decl.userData = extra;
    }
    Clay__ConfigureOpenElement(decl);
    Clay__CloseElement();
}

void draw_rect(const RectDrawDesc& desc) noexcept {
    if (desc.rect.w <= 0.0f || desc.rect.h <= 0.0f) {
        return;
    }
    Context* ctx = current_context();
    if (DrawCmdNode* node = append_open_box_draw_cmd(ctx, desc.foreground)) {
        node->kind = DrawCmdKind::Rect;
        node->rect = desc;
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
    if (desc.icon_id >= kMaxVectorIcons || desc.color.a == 0u || desc.rect.w <= 0.0f || desc.rect.h <= 0.0f) {
        return;
    }
    Context* ctx = current_context();
    if (DrawCmdNode* node = append_open_box_draw_cmd(ctx, desc.foreground)) {
        node->kind = DrawCmdKind::Icon;
        node->icon = desc;
    }
}

Vec2 measure_text(Str text, uint16_t font_id, float size, float letter_spacing) noexcept {
    Context* ctx = current_context();
    if (!ctx || size <= 0.0f) {
        return Vec2{};
    }
    return measure_multiline_text_metrics(ctx, text, font_id, size, letter_spacing, nullptr, nullptr);
}

void draw_text(const TextDrawDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !desc.text.chars || desc.text.length == 0u || desc.size <= 0.0f || desc.color.a == 0u) {
        return;
    }
    if (DrawCmdNode* node = append_open_box_draw_cmd(ctx, desc.foreground)) {
        TextDrawDesc copied = desc;
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
    if (!ctx || !desc.texture || desc.tint.a == 0u || desc.rect.w <= 0.0f || desc.rect.h <= 0.0f) {
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
    WindowUiState* ui = window_ui(ctx, window ? window : ctx->active_window);
    if (!ui) return;
    ui->chrome_desc = clean;
    ui->redraw_budget = 2;
    if (!ctx->headless) {
        for (uint32_t i = 0u; i < kMaxClaimedWindows; ++i) {
            ClaimedWindowSlot& slot = ctx->claimed_windows[i];
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

WindowHit evaluate_window_hit(const Context* ctx, Vec2 logical_pos, SDL_Window* window) noexcept {
    const WindowUiState* ui = ctx ? window_ui(ctx, window ? window : ctx->active_window) : nullptr;
    if (!ui || !ui->chrome_desc.enabled) {
        return WindowHit::Normal;
    }
    SDL_Window* target = window ? window : ctx->active_window;
    float w = 0.0f;
    float h = 0.0f;
    bool maximized = false;
    if (target && !ctx->headless) {
        int iw = 0;
        int ih = 0;
        SDL_GetWindowSize(target, &iw, &ih);
        const float scale = SDL_GetWindowPixelDensity(target) / ui->dpi_scale;
        const Vec2 win_sz = {static_cast<float>(iw) * scale, static_cast<float>(ih) * scale};
        w = win_sz.x;
        h = win_sz.y;
        maximized = (SDL_GetWindowFlags(target) & SDL_WINDOW_MAXIMIZED) != 0u;
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
        size > kMaxDragPayloadBytes || (size > 0u && !data))
    {
        return false;
    }

    ctx->active_ui->drag_source_id = source_id;
    ctx->active_ui->drag_kind_hash = kind_hash;
    ctx->active_ui->drag_u64 = u64;
    ctx->active_ui->drag_u32 = u32;
    ctx->active_ui->drag_size = size;
    if (size > 0u) {
        std::memcpy(ctx->active_ui->drag_data, data, size);
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
            .data = (ctx->active_ui->drag_size > 0u) ? ctx->active_ui->drag_data : nullptr,
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
            .data = (ctx->active_ui->drag_size > 0u) ? ctx->active_ui->drag_data : nullptr,
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
    ws->edit_sel_anchor = anchor_byte;
    ws->edit_cursor = cursor_byte;
    ws->edit_preferred_x = -1.0f;
    request_redraw(ctx);
}

Rect ime_cursor_rect(const Context* ctx_arg) noexcept {
    const Context* ctx = ctx_arg ? ctx_arg : current_context();
    return ctx ? ctx->active_ui->ime_cursor_logical : Rect{};
}

TextInputResult text_input(const TextInputDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame) {
        return TextInputResult{};
    }

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
        if (ctx->active_ui->focus_count < kMaxFocusablePerFrame) {
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
        ws->is_text_input_writable = writable;
    }
    if (buf && cap > 0u) {
        while (len + 1u < cap && buf[len] != '\0') {
            ++len;
        }
        buf[len] = '\0';
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
        if (len <= kTextUndoByteCap) {
            const uint32_t cur_hash = (len > 0u) ? fnv1a_32_step(2166136261u, buf, len) : 2166136261u;
            if (undo_slot->count == 0u) {
                undo_push_snapshot(undo_slot, buf, len, cursor, anchor, ctx->active_ui->elapsed_seconds, 0u);
            } else {
                const TextUndoRecord& cur_rec = undo_slot->entries[(undo_slot->head + undo_slot->cur) % kTextUndoMaxEntries];
                if (cur_rec.length != len || cur_rec.hash != cur_hash) {
                    undo_reset_slot(undo_slot);
                    undo_push_snapshot(undo_slot, buf, len, cursor, anchor, ctx->active_ui->elapsed_seconds, 0u);
                }
            }
        } else {
            undo_reset_slot(undo_slot);
        }
    }

    const bool is_focused = (desc.id != 0u && ctx->active_ui->focus_id == desc.id &&
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

    // Pointer click / double-word / triple-line / drag selection
    if (ws && ws->last_box_logical.w > 0.0f && desc.id != 0u) {
        const TextInnerGeom prev_geom = compute_text_inner_geom(ws->last_box_logical, desc);
        const float local_x = ctx->active_ui->pointer_pos.x - prev_geom.origin_x + ws->edit_scroll_x;
        const float local_y = ctx->active_ui->pointer_pos.y - prev_geom.origin_y + ws->edit_scroll_y;
        const Str cur_text = { buf ? buf : "", len };

        if (ws->pressed_this_frame) {
            ctx->active_ui->caret_blink_on = true;
            ctx->active_ui->caret_blink_cycles = 0u;
            const uint32_t hit_byte = visual_point_to_byte(ctx, desc, cur_text, local_x, local_y);
            if (ctx->active_ui->last_mouse_clicks >= 3u) {
                if (desc.multiline) {
                    line_bounds_at(buf, len, hit_byte, &anchor, &cursor);
                } else {
                    anchor = 0u;
                    cursor = len;
                }
            } else if (ctx->active_ui->last_mouse_clicks == 2u) {
                utf8_word_bounds_at(buf, len, hit_byte, &anchor, &cursor);
            } else {
                const bool shift_held = (ctx->active_ui->frame_key_mods & Mod_Shift) != 0u;
                if (!shift_held) {
                    anchor = hit_byte;
                }
                cursor = hit_byte;
            }
            ws->edit_preferred_x = -1.0f;
        } else if (ctx->active_ui->active_id == desc.id && ws->pressed_down) {
            cursor = visual_point_to_byte(ctx, desc, cur_text, local_x, local_y);
            ws->edit_preferred_x = -1.0f;
            request_redraw(ctx);
        }
    }

    bool changed = false;
    bool submitted = false;
    if (is_focused) {
        if (undo_slot && undo_slot->count > 0u) {
            TextUndoRecord& cur_rec = undo_slot->entries[(undo_slot->head + undo_slot->cur) % kTextUndoMaxEntries];
            cur_rec.cursor = cursor;
            cur_rec.anchor = anchor;
        }
        const auto record_edit = [&](uint8_t op_group) noexcept {
            changed = true;
            if (undo_slot) {
                if (len <= kTextUndoByteCap) {
                    undo_push_snapshot(undo_slot, buf, len, cursor, anchor, ctx->active_ui->elapsed_seconds, op_group);
                } else {
                    undo_reset_slot(undo_slot);
                }
            }
        };
        for (uint32_t oi = 0u; oi < ctx->active_ui->frame_text_op_count; ++oi) {
            const PendingTextOp& op = ctx->active_ui->frame_text_ops[oi];
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
                                }
                            }
                        } else if (shift && (undo_slot->cur + 1u) < undo_slot->count) {
                            if (undo_restore(undo_slot, undo_slot->cur + 1u, buf, cap, &len, &cursor, &anchor)) {
                                changed = true;
                                if (ws) {
                                    ws->edit_preferred_x = -1.0f;
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
                            : utf8_prev_boundary(buf, len, cursor);
                        if (!shift) {
                            anchor = cursor;
                        }
                    }
                    if (ws) {
                        ws->edit_preferred_x = -1.0f;
                    }
                    break;
                case SDLK_RIGHT:
                    if (has_sel && !shift && !word_mod) {
                        cursor = sel_max;
                        anchor = sel_max;
                    } else {
                        cursor = word_mod
                            ? utf8_word_right(buf, len, cursor)
                            : utf8_next_boundary(buf, len, cursor);
                        if (!shift) {
                            anchor = cursor;
                        }
                    }
                    if (ws) {
                        ws->edit_preferred_x = -1.0f;
                    }
                    break;
                case SDLK_HOME: {
                    uint32_t ls = 0u;
                    uint32_t le = 0u;
                    if (desc.multiline && !primary) {
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
                    }
                    break;
                }
                case SDLK_END: {
                    uint32_t ls = 0u;
                    uint32_t le = 0u;
                    if (desc.multiline && !primary) {
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
                    }
                    break;
                }
                case SDLK_UP:
                case SDLK_DOWN:
                    if (desc.multiline && ws) {
                        ws->edit_cursor = cursor;
                        ws->edit_sel_anchor = anchor;
                        move_caret_vertical(
                            ctx, desc, Str{ buf ? buf : "", len }, ws, (op.key == SDLK_UP) ? -1 : 1, shift);
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
                                : utf8_prev_boundary(buf, len, cursor);
                            if (del_from < cursor) {
                                cursor = erase_text_range(buf, &len, del_from, cursor);
                                anchor = cursor;
                                record_edit(word_mod ? 0u : 2u);
                            }
                        }
                        if (ws) {
                            ws->edit_preferred_x = -1.0f;
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
                                : utf8_next_boundary(buf, len, cursor);
                            if (del_to > cursor) {
                                cursor = erase_text_range(buf, &len, cursor, del_to);
                                anchor = cursor;
                                record_edit(word_mod ? 0u : 2u);
                            }
                        }
                        if (ws) {
                            ws->edit_preferred_x = -1.0f;
                        }
                    }
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
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
                            }
                        }
                    } else {
                        submitted = true;
                    }
                    break;
                case SDLK_ESCAPE:
                    if (has_sel) {
                        anchor = cursor;
                    }
                    break;
                default:
                    break;
            }
        }
    }

    cursor = utf8_clamp_boundary(buf, len, cursor);
    anchor = utf8_clamp_boundary(buf, len, anchor);
    if (ws) {
        ws->edit_cursor = cursor;
        ws->edit_sel_anchor = anchor;
    }

    const Str committed_slice = (buf && len > 0u) ? ctx->frame_arena.push_str(buf, len) : Str{};
    const bool composing = is_focused && !desc.read_only && !desc.password && ctx->active_ui->ime_comp_len > 0u;
    if (is_focused && ws && ws->last_box_logical.w > 0.0f && !desc.read_only && !desc.password) {
        const TextInnerGeom pre_geom = compute_text_inner_geom(ws->last_box_logical, desc);
        update_text_scroll_and_ime(
            ctx, ws, desc, pre_geom, committed_slice, cursor, true, composing, nullptr, nullptr);
    }

    TextInputExtra* ti_extra = ctx->frame_arena.alloc<TextInputExtra>();
    BoxExtra* extra = ctx->frame_arena.alloc<BoxExtra>();
    if (ti_extra && extra) {
        extra->kind = CustomCmdKind::TextInput;
        ti_extra->desc = desc;
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
    Clay__CloseElement();

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
    return ws != nullptr && ws->clicked_this_frame;
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
        request_redraw(ctx);
    }
}

void set_cursor(Cursor cursor) noexcept {
    if (Context* ctx = current_context()) {
        ctx->active_ui->desired_cursor = cursor;
    }
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
    return ctx ? ctx->active_ui->frame_key_mods : Mod_None;
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
    Context* ctx = current_context();
    if (!ctx || anim_id == 0u) {
        return target;
    }
    WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, anim_id, ctx->active_ui->frame_index);
    if (!ws) {
        return target;
    }
    if (!ws->spring_initialized) {
        ws->spring_pos = target;
        ws->spring_vel = 0.0f;
        ws->spring_initialized = true;
        return target;
    }

    const float x0 = ws->spring_pos - target;
    const float v0 = ws->spring_vel;
    if (std::fabs(x0) <= 0.0005f && std::fabs(v0) <= 0.005f) {
        ws->spring_pos = target;
        ws->spring_vel = 0.0f;
        return target;
    }

    const float dt = ctx->active_ui->sim_dt;
    if (dt <= 0.0f) {
        ++ctx->active_ui->active_anim_count;
        return ws->spring_pos;
    }

    const float response = (spec.response < 0.01f) ? 0.01f : spec.response;
    const float zeta = (spec.damping_ratio < 0.05f) ? 0.05f : spec.damping_ratio;
    const float omega = 6.28318530718f / response;

    float x1 = 0.0f;
    float v1 = 0.0f;
    if (std::fabs(zeta - 1.0f) < 1e-3f) {
        const float e = std::exp(-omega * dt);
        const float c1 = x0;
        const float c2 = v0 + omega * x0;
        const float linear = c1 + c2 * dt;
        x1 = linear * e;
        v1 = (c2 - omega * linear) * e;
    } else if (zeta < 1.0f) {
        const float omega_d = omega * std::sqrt(1.0f - zeta * zeta);
        const float e = std::exp(-zeta * omega * dt);
        const float c1 = x0;
        const float c2 = (v0 + zeta * omega * x0) / omega_d;
        const float s = std::sin(omega_d * dt);
        const float c = std::cos(omega_d * dt);
        x1 = e * (c1 * c + c2 * s);
        v1 = e * ((c2 * omega_d - c1 * zeta * omega) * c - (c1 * omega_d + c2 * zeta * omega) * s);
    } else {
        const float sq = std::sqrt(zeta * zeta - 1.0f);
        const float r1 = -omega * (zeta - sq);
        const float r2 = -omega * (zeta + sq);
        const float e1 = std::exp(r1 * dt);
        const float e2 = std::exp(r2 * dt);
        const float c2 = (v0 - r1 * x0) / (r2 - r1);
        const float c1 = x0 - c2;
        x1 = c1 * e1 + c2 * e2;
        v1 = c1 * r1 * e1 + c2 * r2 * e2;
    }

    if (std::fabs(x1) <= 0.0005f && std::fabs(v1) <= 0.005f) {
        ws->spring_pos = target;
        ws->spring_vel = 0.0f;
        return target;
    }

    ws->spring_pos = target + x1;
    ws->spring_vel = v1;
    ++ctx->active_ui->active_anim_count;
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

SplitResult begin_split(const SplitDesc& desc) noexcept {
    Context* ctx = current_context();
    const float fallback_ratio = clamp_range(desc.default_ratio, 0.02f, 0.98f);
    if (!ctx || !ctx->in_frame || desc.id == 0u || ctx->active_ui->split_depth >= kMaxSplitDepth) {
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

    const float ratio = ws_split->split_ratio;
    ctx->active_ui->split_stack[ctx->active_ui->split_depth++] = SplitStackEntry{
        .id = desc.id,
        .axis = desc.axis,
        .ratio = ratio,
        .divider_size_px = div_sz,
        .min_first_px = desc.min_first_px,
        .min_second_px = desc.min_second_px,
        .divider_color = desc.divider_color,
        .divider_hover_color = desc.divider_hover_color,
        .divider_active_color = desc.divider_active_color,
        .in_second = false,
    };

    begin_box(BoxDesc{
        .id = desc.id,
        .axis = desc.axis,
        .width = desc.width,
        .height = desc.height,
    });

    const Sizing first_primary = (avail > 1.0f)
        ? Sizing::fixed(avail * ratio)
        : Sizing::percent(clamp_range(ratio, 0.02f, 0.95f));
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
        : (hovered_div ? Color::rgba8(226, 232, 240, 220) : Color::rgba8(148, 163, 184, 110));

    begin_box(BoxDesc{
        .id = div_id,
        .axis = se.axis,
        .width = (se.axis == Axis::Row) ? Sizing::fixed(se.divider_size_px) : Sizing::grow(),
        .height = (se.axis == Axis::Col) ? Sizing::fixed(se.divider_size_px) : Sizing::grow(),
        .align_x = AlignX::Center,
        .align_y = AlignY::Center,
        .background = div_col,
    });
    box(BoxDesc{
        .width = Sizing::fixed((se.axis == Axis::Row) ? 2.0f : 28.0f),
        .height = Sizing::fixed((se.axis == Axis::Col) ? 2.0f : 28.0f),
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

VirtualListSlice begin_virtual_list(const VirtualListDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || desc.id == 0u) {
        return VirtualListSlice{};
    }
    WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, desc.id, ctx->active_ui->frame_index);
    if (!ws) {
        return VirtualListSlice{};
    }

    const float item_h = (desc.item_height_px > 1.0f) ? desc.item_height_px : 1.0f;
    const uint16_t gap_u16 = to_u16_round((desc.row_gap_px > 0.0f) ? desc.row_gap_px : 0.0f);
    const float gap_px = static_cast<float>(gap_u16);
    const double stride = static_cast<double>(item_h) + static_cast<double>(gap_px);

    ws->is_user_element = true;
    ws->is_scroll_x = false;
    ws->is_scroll_y = false;
    ws->is_virtual_list = true;
    ws->vlist_stride = static_cast<float>(stride);
    ws->owner_canvas_id = (ctx->active_ui->canvas_depth > 0u)
        ? ctx->active_ui->canvas_stack[ctx->active_ui->canvas_depth - 1u].id
        : 0u;

    const double pad_v = static_cast<double>(desc.padding.top) + static_cast<double>(desc.padding.bottom);
    const double items_h = (desc.item_count > 0u)
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

    const double first_row_top = static_cast<double>(first_idx) * stride;
    const float child_offset_y = static_cast<float>(first_row_top - ws->vscroll_pos_y);

    BoxExtra* extra = ctx->frame_arena.alloc<BoxExtra>();
    if (extra) {
        extra->kind = CustomCmdKind::Box;
        extra->is_virtual_list = true;
        extra->desc = BoxDesc{
            .id = desc.id,
            .axis = Axis::Col,
            .width = desc.width,
            .height = desc.height,
            .padding = desc.padding,
            .gap = gap_px,
            .background = desc.background,
            .border_color = desc.border_color,
            .border_width = desc.border_width,
            .radius = desc.radius,
            .clip_children = true,
            .show_scrollbar = desc.show_scrollbar,
        };
    }

    Clay__OpenElementWithId(make_clay_id(desc.id));

    const bool has_border = (desc.border_width > 0.0f && desc.border_color.a > 0u);
    const bool has_visual = (desc.background.a > 0u) || has_border;

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
    decl.cornerRadius = to_clay_corners(desc.radius);
    decl.userData = extra;
    if (has_visual && extra) {
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

    return VirtualListSlice{
        .first_index = first_idx,
        .end_index = end_idx,
        .total_count = desc.item_count,
        .scroll_y = ws->vscroll_pos_y,
        .max_scroll_y = max_scroll,
        .viewport_height = static_cast<float>(view_h),
    };
}

void end_virtual_list() noexcept {
    if (!current_context() || !current_context()->in_frame) {
        return;
    }
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
    const float item_h = (item_height_px > 1.0f) ? item_height_px : 1.0f;
    const double stride = (ws->vlist_stride >= item_h)
        ? static_cast<double>(ws->vlist_stride)
        : static_cast<double>(item_h);
    const double view_h = (ws->viewport_dim.y > 1.0f)
        ? static_cast<double>(ws->viewport_dim.y)
        : (static_cast<double>(ctx->active_ui->viewport_h) / static_cast<double>(ctx->active_ui->dpi_scale));
    double target_y = static_cast<double>(row_index) * stride;
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

CanvasState begin_canvas(const CanvasDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || desc.id == 0u || ctx->active_ui->canvas_depth >= kMaxCanvasDepth) {
        return CanvasState{};
    }
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
        extra->kind = CustomCmdKind::Box;
        extra->is_canvas = true;
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
    cw->canvas_id = ce.id;
    cw->from_world = desc.from_world;
    cw->to_world = desc.to_world;
    cw->color_start = desc.color_start;
    cw->color_end = desc.color_end;
    cw->glow_color = desc.glow_color;
    cw->thickness_world = desc.thickness_world;
    cw->glow_world = desc.glow_world;
    cw->tangent_bias = desc.tangent_bias;
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

    begin_box(BoxDesc{
        .id = desc.id,
        .axis = desc.axis,
        .width = scale_sizing(desc.width_world, z),
        .height = scale_sizing(desc.height_world, z),
        .padding = scale_padding(desc.padding_world, z),
        .gap = desc.gap_world * z,
        .background = desc.background,
        .background_end = desc.background_end,
        .gradient_dir = desc.gradient_dir,
        .border_color = desc.border_color,
        .border_width = desc.border_width_world * z,
        .radius = scale_corners_uniform(desc.radius_world, z),
        .shadow_color = desc.shadow_color,
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
DockSpaceSlot* dock_slot(Context* ctx, uint32_t id) noexcept {
    for (auto& s : ctx->active_ui->dock_spaces) if (s.id == id) return &s;
    return nullptr;
}
DockTabSlot* dock_tab(DockSpaceSlot* s, uint32_t id) noexcept {
    for (auto& t : s->tabs) if (t.id == id) return &t;
    return nullptr;
}
uint32_t dock_visible(DockSpaceSlot* s, uint32_t leaf) noexcept {
    const uint32_t active = s->nodes[leaf - 1u].active;
    for (auto& t : s->tabs) if (t.leaf == leaf && t.open && t.id == active) return active;
    for (auto& t : s->tabs) if (t.leaf == leaf && t.open) return t.id;
    return 0;
}
void dock_reparent(DockSpaceSlot* s, uint32_t from, uint32_t to) noexcept {
    for (auto& t : s->tabs) if (t.leaf == from) t.leaf = to;
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
    for (auto& t : s->tabs) if (t.leaf == leaf) t.leaf = destination;
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
    if (!s) for (auto& slot : ctx->active_ui->dock_spaces) if (!slot.id) { s = &slot; break; }
    if (!s) return 0;
    *s = {}; s->id = id; s->root = 1; s->nodes[0].used = true; return 1;
}
bool dock_split_node(uint32_t id, uint32_t node, Axis axis, float ratio,
    uint32_t* out_a, uint32_t* out_b, uint32_t split_id, float min_a, float min_b) noexcept {
    Context* ctx = current_context();
    if (!ctx || !id || !node || node > kMaxDockNodes || ctx->active_ui->dock_cursor.space || !std::isfinite(ratio)) return false;
    auto* s = dock_slot(ctx, id); if (!s || !s->nodes[node - 1u].used || s->nodes[node - 1u].first) return false;
    uint32_t free_nodes[2] = {}; uint32_t count = 0;
    for (uint32_t i = 0; i < kMaxDockNodes && count < 2; ++i) if (!s->nodes[i].used) free_nodes[count++] = i + 1;
    if (count != 2) return false;
    auto& n = s->nodes[node - 1u]; const uint32_t a = free_nodes[0], b = free_nodes[1];
    s->nodes[a - 1u] = n; s->nodes[a - 1u].parent = node;
    s->nodes[b - 1u] = {}; s->nodes[b - 1u].used = true; s->nodes[b - 1u].parent = node;
    dock_reparent(s, node, a);
    n.first = a; n.second = b; n.axis = axis; n.active = 0;
    n.split_id = split_id ? split_id : id_index(id, kDockSplitTagBase + s->split_seq++);
    n.min_first = min_a; n.min_second = min_b;
    set_split_ratio(n.split_id, ratio);
    if (out_a) *out_a = a; if (out_b) *out_b = b; return true;
}
bool dock_add_pane(uint32_t id, uint32_t leaf, const DockPaneDesc& pane) noexcept {
    Context* ctx = current_context();
    if (!ctx || !id || !pane.id || !leaf || leaf > kMaxDockNodes || (pane.title.length && !pane.title.chars) || ctx->active_ui->dock_cursor.space) return false;
    auto* s = dock_slot(ctx, id); if (!s || !s->nodes[leaf - 1u].used || s->nodes[leaf - 1u].first || dock_tab(s, pane.id)) return false;
    for (auto& t : s->tabs) if (!t.id) {
        t.id = pane.id; t.leaf = leaf; t.icon_id = pane.icon_id; t.closable = pane.closable; t.open = pane.default_open;
        uint32_t len = pane.title.length < 47u ? pane.title.length : 47u;
        if (len < pane.title.length) while (len && (static_cast<uint8_t>(pane.title.chars[len]) & 0xC0u) == 0x80u) --len;
        if (len) std::memcpy(t.title, pane.title.chars, len); t.title[len] = 0;
        s->nodes[leaf - 1u].active = dock_visible(s, leaf); return true;
    }
    return false;
}
uint32_t dock_find_pane_leaf(uint32_t id, uint32_t pane) noexcept {
    Context* ctx = current_context(); if (!ctx || !id || !pane) return 0;
    auto* s = dock_slot(ctx, id); auto* t = s ? dock_tab(s, pane) : nullptr; return t ? t->leaf : 0;
}
uint32_t dock_active_pane(uint32_t id, uint32_t leaf) noexcept {
    Context* ctx = current_context(); if (!ctx || !id || !leaf || leaf > kMaxDockNodes) return 0;
    auto* s = dock_slot(ctx, id); return s && s->nodes[leaf - 1u].used ? dock_visible(s, leaf) : 0;
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
    if (zone != DockZone::Center) {
        uint32_t count = 0; for (auto& n : s->nodes) if (!n.used) ++count;
        if (count < 2) return false;
    }
    const uint32_t source = t->leaf;
    if (zone == DockZone::Center) { t->leaf = dst->leaf; t->open = true; s->nodes[t->leaf - 1u].active = pane; }
    else {
        uint32_t a = 0, b = 0;
        const bool before = zone == DockZone::Left || zone == DockZone::Top;
        if (!dock_split_node(id, dst->leaf, (zone == DockZone::Left || zone == DockZone::Right) ? Axis::Row : Axis::Col,
            ratio, &a, &b)) return false;
        // Existing tabs were moved into first; swap leaf contents for a leading insertion.
        if (before) { for (auto& tab : s->tabs) if (tab.leaf == a) tab.leaf = b; s->nodes[b - 1u].active = target; }
        t->leaf = before ? a : b; t->open = true; s->nodes[t->leaf - 1u].active = pane;
    }
    // The source may have become an internal split if docking within its own leaf.
    dock_collapse(s, source); request_redraw(ctx); return true;
}
void begin_dock_space(const DockSpaceDesc& desc) noexcept {
    Context* ctx = current_context(); if (!ctx || !ctx->in_frame || !desc.id || ctx->active_ui->dock_cursor.space) return;
    if (!dock_space_initialized(desc.id)) dock_reset_root(desc.id);
    auto* s = dock_slot(ctx, desc.id); if (!s) return;
    auto& c = ctx->active_ui->dock_cursor; c = {}; c.space = s; c.desc = desc; c.depth = 1; c.stack[0].node_idx = s->root;
    begin_box({ .id = desc.id, .axis = Axis::Col, .width = desc.width, .height = desc.height, .background = desc.background });
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
        for (auto& t : s->tabs) if (t.leaf == leaf && t.open) {
            const uint32_t tab_id = id_index(t.id, 0xD300u), close_id = id_index(t.id, 0xD301u);
            if (clicked(tab_id)) n.active = t.id;
            if (dragging(tab_id, 4.0f)) begin_drag_payload(tab_id, kInternalDockDragKind, Str{t.title}, nullptr, 0, 0, t.id);
            begin_box({ .id = tab_id, .height = Sizing::grow(), .padding = Padding::xy(10, 0), .gap = 6,
                .align_y = AlignY::Center, .background = n.active == t.id ? d.tab_active_bg : (hovered(tab_id) ? d.tab_hover_bg : Color{}), .focusable = true });
            if (n.active == t.id) {
                const float tab_w = element_bounds(tab_id).w;
                draw_line({
                    .from = {2.0f, d.tab_bar_height_px - 1.5f},
                    .to = {(tab_w > 4.0f) ? (tab_w - 2.0f) : 58.0f, d.tab_bar_height_px - 1.5f},
                    .color = d.tab_accent,
                    .thickness = 2.0f,
                });
            }
            if (t.icon_id != kInvalidIcon) icon({ .icon_id = t.icon_id, .size_px = 14 });
            text(Str{t.title}, { .font_size_px = 12.5f, .wrap = false });
            if (t.closable) {
                begin_box({ .id = close_id, .width = Sizing::fixed(14), .height = Sizing::fixed(20), .align_y = AlignY::Center });
                text(str_lit("x"), { .font_size_px = 12 }); end_box();
                if (clicked(close_id)) { t.open = false; c.move_pane = t.id; c.target = 0; }
            }
            end_box();
        }
        end_box(); n.active = dock_visible(s, leaf);
        DragPayloadView payload = {};
        const Rect r = element_bounds(leaf_id); const Vec2 p = ctx->active_ui->pointer_pos;
        if (peek_drag_payload(&payload, kInternalDockDragKind) && p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h) {
            const float x = (p.x - r.x) / r.w, y = (p.y - r.y) / r.h;
            DockZone z = DockZone::Center;
            if (x < .28f || x > .72f || y < .28f || y > .72f) {
                const float dx = x < .5f ? x : 1-x, dy = y < .5f ? y : 1-y;
                z = dx < dy ? (x < .5f ? DockZone::Left : DockZone::Right) : (y < .5f ? DockZone::Top : DockZone::Bottom);
            }
            Rect hi = {0,0,r.w,r.h};
            if (z == DockZone::Left || z == DockZone::Right) { hi.w *= .5f; if (z == DockZone::Right) hi.x = hi.w; }
            if (z == DockZone::Top || z == DockZone::Bottom) { hi.h *= .5f; if (z == DockZone::Bottom) hi.y = hi.h; }
            draw_rect({ .rect = hi, .fill = Color::rgba8(56,189,248,65), .foreground = true });
            if (accept_drag_payload(leaf_id, kInternalDockDragKind, &payload)) { c.move_pane = payload.u32; c.target = n.active; c.zone = z; }
        }
        const float leaf_h = element_bounds(leaf_id).h;
        begin_box({ .axis = Axis::Col, .width = Sizing::percent(1), .height = Sizing::fixed(leaf_h > d.tab_bar_height_px ? leaf_h-d.tab_bar_height_px : 1) }); c.pane_open = true;
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
    auto* s = c.space; c = {};
    if (pane && target) dock_dock_pane(id, pane, target, zone);
    else if (pane) { auto* t = dock_tab(s, pane); dock_collapse(s, t->leaf); request_redraw(ctx); }
}
namespace {
TableMetadata* table_metadata(Context* ctx, uint32_t id) noexcept {
    for (auto& m : ctx->active_ui->tables) if (m.id == id) return &m;
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
TableResult begin_table(const TableDesc& desc) noexcept {
    Context* ctx = current_context();
    if (!ctx || !ctx->in_frame || !desc.id || desc.columns.empty() || desc.columns.size() > kMaxTableColumns || ctx->active_ui->table_depth == 8 ||
        !std::isfinite(desc.row_height_px) || desc.row_height_px <= 0) return {};
    for (const auto& col : desc.columns) if (!std::isfinite(col.width) || !std::isfinite(col.min_width) || !std::isfinite(col.max_width) || col.min_width < 0 || col.max_width < col.min_width) return {};
    auto* metadata = table_metadata(ctx, desc.id);
    if (!metadata) for (auto& slot : ctx->active_ui->tables) if (!slot.id) { metadata = &slot; metadata->id = desc.id; break; }
    if (!metadata) return {};
    auto& t = ctx->active_ui->table_stack[ctx->active_ui->table_depth++]; t = {}; t.desc = desc; t.count = static_cast<uint32_t>(desc.columns.size());
    for (uint32_t c = 0; c < t.count; ++c) t.columns[c] = desc.columns[c];
    t.desc.columns = {t.columns, t.count};
    auto* ws = state_map_get_or_insert(&ctx->active_ui->states, desc.id, ctx->active_ui->frame_index);
    if (!ws->table_initialized) {
        ws->table_initialized = true; ws->table_sort_column = desc.default_sort_column < t.count ? desc.default_sort_column : 0;
        ws->table_sort_descending = desc.default_sort_descending;
    }
    TableResult result = { .sort_column = ws->table_sort_column, .sort_descending = ws->table_sort_descending, .sort_changed = ws->table_sort_changed };
    ws->table_sort_changed = false;
    const uint32_t outer_id = id_index(desc.id, kTableOuterTag);
    const float gutter = desc.show_scrollbar ? 10.0f : 0;
    float viewport = element_bounds(id_index(desc.id,0x7402u)).w;
    if (viewport <= 0) viewport = desc.width.kind == SizingKind::Fixed ? desc.width.value : static_cast<float>(ctx->active_ui->viewport_w) / ctx->active_ui->dpi_scale;
    float remaining = viewport - desc.padding.left - desc.padding.right - gutter;
    float weights = 0; bool resizing = false;
    for (uint32_t c = 0; c < t.count; ++c) {
        const auto& col = t.columns[c]; const uint32_t cid = table_col_id(desc, c), rid = id_index(cid, 0x7600u);
        state_map_get_or_insert(&ctx->active_ui->states, rid, ctx->active_ui->frame_index);
        auto* cw = state_map_get_or_insert(&ctx->active_ui->states, cid, ctx->active_ui->frame_index);
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
            remaining -= t.col_widths[c];
        } else weights += col.width > 0 ? col.width : 1;
    }
    // Water-fill stretch columns, respecting each column's declared bounds.
    bool resolved[kMaxTableColumns] = {};
    for (uint32_t pass = 0; pass <= t.count; ++pass) {
        bool clamped = false;
        for (uint32_t c = 0; c < t.count; ++c) {
            const auto& col = t.columns[c]; auto* cw = state_map_find(&ctx->active_ui->states, table_col_id(desc, c));
            if (resolved[c] || cw->column_override || col.sizing != TableColumnSizing::Stretch) continue;
            const float weight = col.width > 0 ? col.width : 1;
            const float width = weights > 0 ? remaining * weight / weights : col.min_width;
            if (pass == t.count || width < col.min_width || width > col.max_width) {
                t.col_widths[c] = clamp_range(width, col.min_width, col.max_width);
                remaining -= t.col_widths[c]; weights -= weight; resolved[c] = true; clamped = true;
            } else t.col_widths[c] = width;
        }
        if (!clamped) break;
    }
    begin_box({ .id = id_index(desc.id,0x7402u), .axis = Axis::Col, .width = desc.width.kind == SizingKind::Grow ? Sizing::percent(1) : desc.width, .height = desc.height });
    begin_box({ .id = outer_id, .axis = Axis::Col, .width = desc.width, .height = desc.height,
        .background = desc.background, .border_color = desc.border_color, .border_width = desc.border_width, .radius = desc.radius, .clip_children = true });
    begin_box({ .id = id_index(desc.id, kTableHeaderTag), .width = Sizing::grow(), .height = Sizing::fixed(desc.header_height_px),
        .padding = {desc.padding.left, desc.padding.right + gutter, 0, 0}, .background = desc.header_background });
    for (uint32_t c = 0; c < t.count; ++c) {
        const auto& col = t.columns[c]; const uint32_t cid = table_col_id(desc, c), hid = id_index(cid, 0x7601u), rid = id_index(cid, 0x7600u);
        auto* cw = state_map_find(&ctx->active_ui->states, cid); cw->column_width = t.col_widths[c];
        if (col.sortable && clicked(hid) && !resizing) {
            result.sort_descending = result.sort_column == c ? !result.sort_descending : true;
            result.sort_column = c; result.sort_changed = true;
        }
        begin_box({ .id = hid, .width = Sizing::fixed(t.col_widths[c]), .height = Sizing::grow(),
            .padding = Padding::xy(desc.cell_padding_x,0), .align_x = col.align_x, .align_y = AlignY::Center, .clip_children = true, .focusable = true });
        text(col.title, { .font_size_px = 12, .wrap = false });
        draw_line({ .from = {t.col_widths[c]-1,0}, .to = {t.col_widths[c]-1,desc.header_height_px}, .color = desc.border_color });
        if (result.sort_column == c) {
            const float x = t.col_widths[c] - 14, y = desc.header_height_px * .5f;
            const float dir = result.sort_descending ? 3.0f : -3.0f;
            draw_line({ .from = {x-3,y-dir}, .to = {x,y+dir}, .color = Color::rgb_hex(0x38BDF8) });
            draw_line({ .from = {x,y+dir}, .to = {x+3,y-dir}, .color = Color::rgb_hex(0x38BDF8) });
        }
        if (col.resizable) box({ .id = rid, .width = Sizing::fixed(6), .height = Sizing::fixed(desc.header_height_px),
            .attach_to = AttachTo::Parent, .anchor_element = Anchor::TopRight, .anchor_parent = Anchor::TopRight, .floating_offset = {0,0} });
        end_box();
    }
    end_box();
    ws = state_map_find(&ctx->active_ui->states, desc.id);
    ws->table_sort_column = result.sort_column; ws->table_sort_descending = result.sort_descending; metadata->count = t.count;
    for (uint32_t c = 0; c < t.count; ++c) { metadata->widths[c] = t.col_widths[c]; metadata->column_ids[c] = table_col_id(desc,c); metadata->min_widths[c] = t.columns[c].min_width; metadata->max_widths[c] = t.columns[c].max_width; }
    const float outer_h = element_bounds(outer_id).h;
    const float table_h = outer_h > 0 ? outer_h : (desc.height.kind == SizingKind::Fixed ? desc.height.value : 1);
    // Clay clipping permits child overflow. Bound the viewport independently of visible row content.
    begin_box({ .axis = Axis::Col, .width = Sizing::grow(), .height = Sizing::fixed(table_h > desc.header_height_px ? table_h-desc.header_height_px : 1) });
    result.slice = begin_virtual_list({ .id = desc.id, .item_count = desc.item_count, .item_height_px = desc.row_height_px,
        .row_gap_px = desc.row_gap_px, .overscan_rows = desc.overscan_rows, .width = Sizing::grow(), .height = Sizing::grow(),
        .padding = desc.padding, .background = Color::transparent(), .border_width = 0, .show_scrollbar = desc.show_scrollbar });
    // Rows own the horizontal insets, including the scrollbar gutter. Keep the list's vertical padding.
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
    begin_box({ .id = rid, .width = Sizing::grow(), .height = Sizing::fixed(d.row_height_px),
        .padding = {d.padding.left, d.padding.right + (d.show_scrollbar ? 10.0f : 0.0f), 0, 0},
        .background = bg.a ? bg : (hovered(rid) ? d.hover_row_color : ((row & 1u) ? d.zebra_color : Color{})),
        .border_color = border, .border_width = border.a ? 1.0f : 0.0f });
    t.col_cursor = 0; t.row_open = true; return true;
}
bool table_next_column() noexcept {
    Context* ctx = current_context(); if (!ctx || !ctx->in_frame || !ctx->active_ui->table_depth) return false;
    auto& t = ctx->active_ui->table_stack[ctx->active_ui->table_depth - 1]; if (!t.row_open) return false;
    if (t.cell_open) { end_box(); t.cell_open = false; }
    if (t.col_cursor == t.count) return false;
    const uint32_t c = t.col_cursor++;
    begin_box({ .id = id_index(id_index(t.desc.id, 0x100000u + t.cursor_row - 1),c), .width = Sizing::fixed(t.col_widths[c]), .height = Sizing::grow(),
        .padding = Padding::xy(t.desc.cell_padding_x,0), .align_x = t.columns[c].align_x, .align_y = AlignY::Center, .clip_children = true });
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
TreeNodeResult begin_tree_node(const TreeNodeDesc& d) noexcept {
    Context* ctx = current_context(); if (!ctx || !ctx->in_frame || !d.id || ctx->active_ui->tree_depth >= kMaxTreeDepth) return {};
    const uint32_t chev = id_index(d.id,kTreeChevronTag);
    auto* ws = state_map_get_or_insert(&ctx->active_ui->states,d.id,ctx->active_ui->frame_index);
    if (!ws->tree_initialized) { ws->tree_initialized = true; ws->tree_open = d.default_open; }
    TreeNodeResult r = { .clicked = clicked(d.id), .double_clicked = double_clicked(d.id), .hovered = hovered(d.id) };
    bool open = ws->tree_open;
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
        begin_box({ .id = id_index(d.id,0x7EE1u), .axis = Axis::Col, .width = Sizing::grow(), .height = Sizing::fit() });
        if (d.show_indent_guide) {
            const float x = static_cast<float>(ctx->active_ui->tree_depth)*d.indent_px+8;
            draw_line({ .from = {x,0}, .to = {x,element_bounds(id_index(d.id,0x7EE1u)).h}, .color = Color::rgba8(148,163,184,40) });
        }
        ++ctx->active_ui->tree_depth;
    }
    return r;
}
void end_tree_node() noexcept {
    Context* ctx = current_context(); if (!ctx || !ctx->in_frame || !ctx->active_ui->tree_depth) return;
    --ctx->active_ui->tree_depth; end_box();
}

void end_frame(Context* ctx_arg) noexcept {
    Context* ctx = ctx_arg ? ctx_arg : current_context();
    if (!ctx || !ctx->in_frame || !ctx->active_ui->clay_ctx) {
        return;
    }
    Clay_SetCurrentContext(ctx->active_ui->clay_ctx);

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
    for (int32_t i = 0; i < cmds.length; ++i) {
        const Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(&cmds, i);
        if (!cmd) {
            continue;
        }
        const ClipEntry& cur_clip = clip_stack.entries[clip_stack.depth - 1u];
        const Rect box_px = {
            .x = cmd->boundingBox.x * dpi,
            .y = cmd->boundingBox.y * dpi,
            .w = cmd->boundingBox.width * dpi,
            .h = cmd->boundingBox.height * dpi,
        };

        switch (cmd->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
                const BoxExtra* extra = static_cast<const BoxExtra*>(cmd->renderData.custom.customData);
                if (extra) {
                    if (extra->desc.id && ctx->active_ui->drop_hit_count < ETCH_COUNTOF(ctx->active_ui->drop_hits)) {
                        ctx->active_ui->drop_hits[ctx->active_ui->drop_hit_count++] = SpanHitRegion{
                            .id = extra->desc.id, .clay_elem_id = extra->desc.id,
                            .rect = {cmd->boundingBox.x, cmd->boundingBox.y, cmd->boundingBox.width, cmd->boundingBox.height},
                            .paint_order = static_cast<uint32_t>(i),
                            .z_index = cmd->zIndex,
                            .clip_rect = {cur_clip.rect.x / dpi, cur_clip.rect.y / dpi, cur_clip.rect.w / dpi, cur_clip.rect.h / dpi}, .has_clip = true};
                    }
                    if (extra->text_input) {
                        emit_text_input_command(ctx, extra->text_input, cmd->boundingBox, box_px, cur_clip, dpi);
                    } else if (extra->kind == CustomCmdKind::CanvasWire && extra->canvas_wire) {
                        emit_canvas_wire_command(ctx, extra->canvas_wire, cmd->boundingBox, cur_clip, dpi);
                    } else if (extra->kind == CustomCmdKind::Icon) {
                        emit_vector_icon_quad(ctx, extra->icon_desc.icon_id, box_px, extra->icon_desc.color, cur_clip);
                    } else {
                        if (extra->desc.attach_to != AttachTo::None && !extra->desc.pointer_passthrough) {
                            const uint32_t slot = (ctx->active_ui->span_hit_count < kMaxSpanHitsPerFrame)
                                ? ctx->active_ui->span_hit_count++
                                : (kMaxSpanHitsPerFrame - 1u);
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
                        if (extra->keyboard_focus_ring)
                            push_prim(ctx, Prim::make_rect(box_px, cur_clip.rect, {}, scale_corners_uniform(Corners::all(4), dpi),
                                Color::rgb_hex(0x60A5FA), 2 * dpi, 1, cur_clip.radius));
                        if (extra->draw_head) {
                            ClipEntry draw_clip = cur_clip;
                            const bool box_clips =
                                extra->desc.clip_children || extra->desc.scroll_x || extra->desc.scroll_y || extra->is_canvas;
                            if (box_clips) {
                                const Rect clipped_rect = rect_intersect(cur_clip.rect, box_px);
                                const Corners& c = extra->desc.radius;
                                float cr = min4f(c.tl, c.tr, c.br, c.bl) * dpi;
                                if (cr <= 0.0f &&
                                    clipped_rect.x == cur_clip.rect.x &&
                                    clipped_rect.y == cur_clip.rect.y &&
                                    clipped_rect.w == cur_clip.rect.w &&
                                    clipped_rect.h == cur_clip.rect.h)
                                {
                                    cr = cur_clip.radius;
                                }
                                draw_clip = ClipEntry{ .rect = clipped_rect, .radius = cr };
                            }
                            emit_draw_list_command(ctx, extra->draw_head, box_px, draw_clip, dpi);
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
                    const Rect clipped_rect = rect_intersect(cur_clip.rect, box_px);
                    float next_radius = 0.0f;
                    if (extra) {
                        const Corners& c = extra->desc.radius;
                        next_radius = min4f(c.tl, c.tr, c.br, c.bl) * dpi;
                        if (extra->is_canvas) {
                            ctx->active_ui->last_canvas_clip_px = clipped_rect;
                            ctx->active_ui->last_canvas_clip_radius_px = next_radius;
                        }
                    } else if (clipped_rect.x == ctx->active_ui->last_canvas_clip_px.x &&
                               clipped_rect.y == ctx->active_ui->last_canvas_clip_px.y &&
                               clipped_rect.w == ctx->active_ui->last_canvas_clip_px.w &&
                               clipped_rect.h == ctx->active_ui->last_canvas_clip_px.h)
                    {
                        next_radius = ctx->active_ui->last_canvas_clip_radius_px;
                    }
                    if (next_radius <= 0.0f &&
                        clipped_rect.x == cur_clip.rect.x &&
                        clipped_rect.y == cur_clip.rect.y &&
                        clipped_rect.w == cur_clip.rect.w &&
                        clipped_rect.h == cur_clip.rect.h)
                    {
                        next_radius = cur_clip.radius;
                    }
                    clip_stack.entries[clip_stack.depth++] = ClipEntry{
                        .rect = clipped_rect,
                        .radius = next_radius,
                    };
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
                    emit_draw_list_command(ctx, extra->fg_draw_head, box_px, cur_clip, dpi);
                }
                const BoxDesc& d = extra->desc;
                const bool is_scroll = d.scroll_x || d.scroll_y || extra->is_virtual_list;
                const bool any_clip = d.clip_children || is_scroll;

                if ((d.scroll_y || extra->is_virtual_list) && d.show_scrollbar && d.id != 0u) {
                    if (const WidgetState* ws = state_map_find(&ctx->active_ui->states, d.id)) {
                        const ScrollbarGeom sb = compute_vscrollbar_geom(ws);
                        if (sb.visible) {
                            const uint32_t thumb_id = id_index(d.id, kScrollbarYTag);
                            const bool sb_active = (ctx->active_ui->active_id == thumb_id);
                            const bool sb_hover = (ctx->active_ui->active_id == 0u) && sb.hit_track.contains(ctx->active_ui->pointer_pos) &&
                                Clay_PointerOver(make_clay_id(d.id));
                            const Rect track_px = { sb.track.x * dpi, sb.track.y * dpi, sb.track.w * dpi, sb.track.h * dpi };
                            const Rect thumb_px = { sb.thumb.x * dpi, sb.thumb.y * dpi, sb.thumb.w * dpi, sb.thumb.h * dpi };
                            const Color track_col = Color::rgba8(255, 255, 255, 18);
                            const Color thumb_col = sb_active
                                ? Color::rgba8(96, 165, 250, 230)
                                : (sb_hover ? Color::rgba8(148, 163, 184, 195) : Color::rgba8(148, 163, 184, 125));
                            push_prim(
                                ctx,
                                Prim::make_rect(
                                    track_px,
                                    cur_clip.rect,
                                    track_col,
                                    Corners::all(0.5f * track_px.w),
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
                                    Corners::all(0.5f * thumb_px.w),
                                    {},
                                    0.0f,
                                    1.0f,
                                    cur_clip.radius));
                        }
                    }
                }

                if (any_clip && d.border_width > 0.0f && d.border_color.a > 0u) {
                    // Clip against parent container (below our own SCISSOR_START) so border AA edge isn't halved.
                    const ClipEntry& parent_clip = (clip_stack.depth > 1u)
                        ? clip_stack.entries[clip_stack.depth - 2u]
                        : clip_stack.entries[0];
                    push_prim(
                        ctx,
                        Prim::make_rect(
                            box_px,
                            parent_clip.rect,
                            Color::transparent(),
                            scale_corners(d.radius, dpi),
                            d.border_color,
                            d.border_width * dpi,
                            1.0f,
                            parent_clip.radius));
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                emit_text_command(ctx, cmd, cur_clip, dpi);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
                const ImageExtra* extra = static_cast<const ImageExtra*>(cmd->renderData.image.imageData);
                if (extra && extra->desc.texture != nullptr) {
                    const ImageDesc& idesc = extra->desc;
                    push_prim(
                        ctx,
                        Prim::make_texture(
                            box_px,
                            cur_clip.rect,
                            idesc.uv_min,
                            idesc.uv_max,
                            0u,
                            scale_corners(idesc.radius, dpi),
                            idesc.tint,
                            idesc.border_color,
                            idesc.border_width * dpi,
                            PrimFlag_None,
                            cur_clip.radius),
                        idesc.texture);
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
                ctx->active_ui->chrome_client_rects[chrome_client_n++] = r.rect;
            }
        }
        if (ctx->active_ui->states.entries) {
            for (uint32_t i = 0u; i < ctx->active_ui->states.capacity && chrome_client_n < kMaxChromeClientRects; ++i) {
                const WidgetState& ws = ctx->active_ui->states.entries[i];
                if (ws.key == 0u || ws.last_seen_frame != ctx->active_ui->frame_index || !ws.is_focusable) {
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

    if (ctx->active_ui->drag_active && ctx->active_ui->pointer_pos.x >= 0.0f && ctx->active_ui->pointer_pos.y >= 0.0f) {
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
        const float hud_y = 14.0f;
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
                Color::rgba8(9, 13, 22, 236),
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
            "ETCH 1.5 INSPECTOR   [F12]",
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
    ctx->active_ui->frame_drop_count = 0;
    ctx->active_ui->frame_text_drop_count = 0;
    ctx->active_ui->has_delivered_drop_pos = false;
    // A window-only context consumes global invalidations through its primary pass.
    if (!ctx->headless && ctx->active_window == ctx->window) ctx->headless_ui.redraw_budget = 0;
    ctx->in_frame = false;
    ctx->stats.prim_count = ctx->prim_count;
    ctx->stats.draw_call_count = ctx->batch_count;
    ctx->stats.blur_pass_count = app_blur_count;
    ctx->stats.slug_glyph_count = ctx->atlas.slug_glyph_count;
    ctx->stats.slug_curve_count = ctx->atlas.slug_curve_count;
    ctx->stats.arena_used_bytes = ctx->frame_arena.used();
    ctx->stats.arena_peak_bytes = ctx->frame_arena.peak_offset;
    ctx->stats.layout_ms = layout_ms;
    ctx->stats.active_animations = ctx->active_ui->active_anim_count;
    ctx->stats.idle_sleeping = idle_now;
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
        for (uint32_t i = 0; i < kMaxClaimedWindows; ++i)
            if (ctx->claimed_windows[i].window) ctx->claimed_windows[i].ui.redraw_budget = 2;
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
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    float scale = SDL_GetWindowDisplayScale(window);
    if (scale <= 0) scale = 1;
    if (scale < desc.min_scale) scale = desc.min_scale;
    begin_bound_frame(ctx, FrameDesc{.viewport_w = static_cast<uint32_t>(w > 0 ? w : 1),
        .viewport_h = static_cast<uint32_t>(h > 0 ? h : 1), .dpi_scale = scale, .dt_seconds = desc.dt});
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
    if (count == kMaxDroppedFiles || value.length >= kMaxDroppedPathBytes - ui.pending_drop_bytes) return;
    const uint32_t offset = ui.pending_drop_bytes;
    std::memcpy(ui.pending_drop_buf + offset, value.chars, value.length);
    ui.pending_drop_buf[offset + value.length] = '\0';
    (text ? ui.pending_text_drop_offsets : ui.pending_drop_offsets)[count] = static_cast<uint16_t>(offset);
    (text ? ui.pending_text_drop_lengths : ui.pending_drop_lengths)[count++] = static_cast<uint16_t>(value.length);
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
