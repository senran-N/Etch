// Declarative Clay layout adapter, UTF-8 + stb_truetype FontAtlas, and Prim batch lowering.
// Compiled via src/etch_unity.cpp.

#include <cmath>

namespace etch {

// Internal unity-build helpers have etch namespace linkage.

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
    uint32_t tab_width = 4u) noexcept
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

    uint32_t pos = 0u;
    while (pos < seg_len) {
        if (seg[pos] == '\t') {
            const float adv = static_cast<float>(tab_width) * primary.space_advance_em * base_px + next_spacing();
            if (pos < stop_a) xa = total_w + adv;
            if (pos < stop_b) xb = total_w + adv;
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
    if (config && (config->fontId & 0x8000u) != 0u && config->userData != nullptr) {
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

// Preserve subpixel primitives when rounding would collapse either dimension.
Rect snap_pixel_rect(Rect r) noexcept {
    const float x = std::round(r.x), y = std::round(r.y);
    const float w = std::round(r.x + r.w) - x, h = std::round(r.y + r.h) - y;
    return w > 0 && h > 0 ? Rect{x, y, w, h} : r;
}

ClipEntry make_box_clip(Context* ctx,const ClipEntry& parent,Rect bounds,Corners radii,bool continuous) noexcept {
    if(const auto* layer=ctx->active_render_layer) {
        resolve_layer_chain(ctx,layer,ctx->active_ui->dpi_scale);
        const auto scale=layer->resolved_scale,offset=layer->resolved_offset_px;
        bounds={bounds.x*scale.x+offset.x,bounds.y*scale.y+offset.y,bounds.w*scale.x,bounds.h*scale.y};
        const float k=.5f*(scale.x+scale.y);
        radii={radii.tl*k,radii.tr*k,radii.br*k,radii.bl*k};
    }

    ClipEntry result={rect_intersect(parent.rect,bounds),0,parent.shape,parent.indexed || parent.shape!=0};
    const float radius=min4f(radii.tl,radii.tr,radii.br,radii.bl);
    result.radius=continuous ? -radius : radius;
    if(radii.tl==0 && radii.tr==0 && radii.br==0 && radii.bl==0) {
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
    ctx->clip_shapes[index]={{bounds.x,bounds.y,bounds.w,bounds.h},{radii.tl,radii.tr,radii.br,radii.bl},{parent.shape,continuous ? 1u : 0u,0,0}};
    result.shape=index;
    result.indexed=parent.shape!=0 || radii.tl!=radii.tr || radii.tr!=radii.br || radii.br!=radii.bl;
    return result;
}

void push_prim(
    Context* ctx,
    Prim prim,
    SDL_GPUTexture* ext_tex = nullptr,
    float cull_pad_px = 2.0f) noexcept
{
    if (!ctx || !ctx->prims) return;
    if (ctx->prim_count >= ctx->max_prims) {
        if (ctx->max_prims >= kMaxPrimHardCap) return;
        uint32_t cap = ctx->max_prims >= kMinPrimCap ? ctx->max_prims : kMinPrimCap;
        cap = cap > kMaxPrimHardCap / 2u ? kMaxPrimHardCap : cap * 2u;
        Prim* grown = static_cast<Prim*>(std::realloc(ctx->prims, static_cast<size_t>(cap) * sizeof(Prim)));
        if (!grown) return;
        ctx->prims = grown;
        ctx->max_prims = cap;
    }
    const auto* layer=ctx->active_render_layer;
    if(layer) {
        resolve_layer_chain(ctx,layer,ctx->active_ui->dpi_scale);
        const Vec2 scale=layer->resolved_scale, offset=layer->resolved_offset_px;
        prim.bounds={prim.bounds.x*scale.x+offset.x,prim.bounds.y*scale.y+offset.y,prim.bounds.z*scale.x,prim.bounds.w*scale.y};
        const auto fade=[&](uint32_t c) noexcept {return (c&0xFFFFFFu) | (uint32_t(float(c>>24)*layer->resolved_opacity+.5f)<<24);};
        const PrimKind kind=static_cast<PrimKind>(prim.style.x&15u);
        if(kind==PrimKind::BlurGlass) prim.payload.x=std::bit_cast<uint32_t>(1.f-layer->resolved_opacity);
        else prim.style.y=fade(prim.style.y);
        if(kind==PrimKind::Rect || kind==PrimKind::RotatedRect || kind==PrimKind::BezierWire || kind==PrimKind::RingArc || kind==PrimKind::Texture || kind==PrimKind::BlurGlass || kind==PrimKind::CanvasGrid) prim.style.z=fade(prim.style.z);
        if(kind==PrimKind::BezierWire || kind==PrimKind::RingArc) prim.payload.w=fade(prim.payload.w);
        if(kind==PrimKind::SlugFill) prim.payload.z=fade(prim.payload.z);
        if((kind==PrimKind::Rect || kind==PrimKind::RotatedRect) && (prim.style.x & ((PrimFlag_LinearGrad|PrimFlag_RadialGrad)<<4))) prim.payload.x=fade(prim.payload.x);
        const float metric_scale=.5f*(scale.x+scale.y);
        const auto scaled_pair=[metric_scale](uint32_t pair) noexcept {
            return pack_f16x2(unpack_f16_cpu(uint16_t(pair))*metric_scale,unpack_f16_cpu(uint16_t(pair>>16))*metric_scale);
        };
        if(kind==PrimKind::BezierWire || kind==PrimKind::RingArc || kind==PrimKind::Shadow) prim.style.w=scaled_pair(prim.style.w);
        if(kind==PrimKind::RingArc) prim.payload.z=scaled_pair(prim.payload.z);
        if(kind==PrimKind::Rect || kind==PrimKind::RotatedRect || kind==PrimKind::Shadow || kind==PrimKind::Texture || kind==PrimKind::BlurGlass) {
            const float k=.5f*(scale.x+scale.y);
            const auto scale_pair=[k](uint32_t pair) noexcept {return pack_f16x2(unpack_f16_cpu(uint16_t(pair))*k,unpack_f16_cpu(uint16_t(pair>>16))*k);};
            prim.payload.z=scale_pair(prim.payload.z);prim.payload.w=scale_pair(prim.payload.w);
            if(kind==PrimKind::Rect || kind==PrimKind::BlurGlass || kind==PrimKind::RotatedRect) prim.style.w=(prim.style.w&0xFFFF0000u) | pack_f16(unpack_f16_cpu(uint16_t(prim.style.w))*k);
        }

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
    if(ctx->render_clip_index) {
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
            if (cur->ext_count < 8u) {
                cur->ext_textures[cur->ext_count] = ext_tex;
                tex_slot = static_cast<uint16_t>(cur->ext_count + 1u);
                ++cur->ext_count;
            } else {
                if (!ensure_batch_cap(ctx)) {
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
    final_p.style.x = (final_p.style.x & ~(0xFu << 10)) | ((static_cast<uint32_t>(tex_slot) & 0xFu) << 10);
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
    const bool draw_border_here = !any_clip && d.border_width > 0.0f && d.border_color.a > 0u;
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
    Color tint, bool color_layer = false) noexcept
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
            color_layer ? PrimFlag_None : PrimFlag_StemDarken));
}

void emit_slug_glyph_quad(Context* ctx, uint16_t slot_idx, uint32_t glyph_id, float phys_px, float pen_x_px,
    float baseline_y_px, float x_offset_em, float y_offset_em, const ClipEntry& clip_px, Color tint) noexcept {
    const auto& slot=ctx->atlas.slots[slot_idx]; const auto layers=slug_get_colr_layers(slot,glyph_id);
    if (!layers.count) {
        if(slot.has_sbix || slot.has_cbdt) {
            const auto entry=get_or_cache_bitmap_emoji(ctx,slot_idx,glyph_id,phys_px);
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
        emit_slug_outline_quad(ctx,slot_idx,glyph_id,phys_px,pen_x_px,baseline_y_px,x_offset_em,y_offset_em,clip_px,tint);return;
    }
    for(uint32_t i=0;i<layers.count;++i) {
        const auto* p=slot.colr_data+slot.colr_layer_off+(layers.first+i)*4u;
        const uint16_t glyph=ot_u16(p),palette=ot_u16(p+2); Color color=tint;
        if(palette!=0xFFFFu) {
            if(palette>=slot.cpal_num_palette_entries)continue;
            const auto* c=slot.cpal_data+slot.cpal_color_records_off+uint32_t(palette)*4u;
            color=Color::rgba8(c[2],c[1],c[0],static_cast<uint8_t>((uint32_t(c[3])*tint.a+127u)/255u));
        }
        emit_slug_outline_quad(ctx,slot_idx,glyph,phys_px,pen_x_px,baseline_y_px,x_offset_em,y_offset_em,clip_px,color,true);
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
    float start, float baseline, float max_width, const ClipEntry& clip, Color color, float dpi) noexcept {
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
        for(uint32_t k=i;k<j;++k){const auto& g=glyphs[k];emit_slug_glyph_quad(ctx,g.font_slot,g.glyph_id,size*dpi,pen*dpi,baseline*dpi,g.x_offset_em,g.y_offset_em,clip,color);pen+=g.x_advance_1px*size+spacing;}
        i=j;
    }
    for(const auto& g:dots){emit_slug_glyph_quad(ctx,g.font_slot,g.glyph_id,size*dpi,pen*dpi,baseline*dpi,g.x_offset_em,g.y_offset_em,clip,color);pen+=g.x_advance_1px*size;}
    return pen;
}

uint32_t utf8_clamp_boundary(const char*, uint32_t, uint32_t) noexcept;

struct SelectionLine {
    const char* base = nullptr;
    uint32_t start = 0, end = 0, id = 0, clay_id = 0;
    Color color = {};
    const RichTextExtra* rich = nullptr;
};
SelectionLine selection_line(const Clay_RenderCommand* cmd) noexcept {
    SelectionLine line = {};
    if (!cmd->userData) return line;
    if ((cmd->renderData.text.fontId & 0x8000u) != 0u) {
        const auto* r = static_cast<const RichTextExtra*>(cmd->userData);
        line.base = r->full_utf8; line.id = r->selectable_id;
        line.color = r->selection_color; line.clay_id = r->clay_elem_id; line.rich = r;
    } else {
        const auto* r = static_cast<const SelectableTextExtra*>(cmd->userData);
        line.base = r->full_utf8; line.id = r->selectable_id;
        line.color = r->selection_color; line.clay_id = r->clay_elem_id;
    }
    line.start = static_cast<uint32_t>(cmd->renderData.text.stringContents.chars - line.base);
    line.end = line.start + static_cast<uint32_t>(cmd->renderData.text.stringContents.length);
    return line;
}

// Visit visual cluster edges once; ligatures and RTL runs retain their shaped boundaries.
void selection_line_geometry(Context* ctx, const Clay_RenderCommand* cmd, const SelectionLine& line,
    uint32_t a, uint32_t b, float pointer_x, float* xa, float* xb, uint32_t* hit,
    const ClipEntry* selection_clip = nullptr, float dpi = 1.0f) noexcept {
    float pen = cmd->boundingBox.x;
    float best = 1.0e30f;
    bool has_xa = false;
    bool has_xb = false;
    *xa = pen; *xb = pen; *hit = line.start;
    const auto edge = [&](uint32_t byte, float x) noexcept {
        if (byte == a) { *xa = x; has_xa = true; }
        if (byte == b) { *xb = x; has_xb = true; }
        const float dist = std::abs(x - pointer_x);
        if (dist < best) { best = dist; *hit = byte; }
    };
    const auto segment = [&](uint32_t start, uint32_t end, uint16_t font, float size, float spacing, bool trim) noexcept {
        const uint16_t slot = resolve_font_slot_idx(&ctx->atlas, font);
        uint32_t pos = start;
        uint32_t glyph_index = 0;
        const auto advance_spacing = [&]() noexcept { return (!trim || glyph_index++ > 0u) ? spacing : 0.0f; };
        while (pos < end) {
            if (line.base[pos] == '\t' || slot == kInvalidFont) {
                edge(pos, pen);
                const float start_pen = pen;
                const bool tab = line.base[pos] == '\t';
                const char* cp = line.base + pos;
                (void)next_utf8_codepoint(cp, line.base + end);
                pen += (tab ? 4.0f : 1.0f) * (slot == kInvalidFont ? 0.55f : ctx->atlas.slots[slot].space_advance_em) * size + advance_spacing();
                const uint32_t next_pos = static_cast<uint32_t>(cp - line.base);
                if (selection_clip && a < next_pos && b > pos) {
                    push_prim(ctx, Prim::make_rect({start_pen * dpi, cmd->boundingBox.y * dpi,
                        (pen - start_pen) * dpi, cmd->boundingBox.height * dpi},
                        selection_clip->rect, line.color, {}, {}, 0, 1, selection_clip->radius));
                }
                pos = next_pos; edge(pos, pen); continue;
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
                edge(pos + (rtl ? c1 : c0), pen);
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
                if (selection_clip && c1 > c0 && a < pos+c1 && b > pos+c0) {
                    const uint32_t lo = a > pos+c0 ? a : pos+c0, hi = b < pos+c1 ? b : pos+c1;
                    const uint32_t count = count_utf8_cps(line.base+pos+c0,c1-c0);
                    if (count > 0u) {
                        const float f0 = static_cast<float>(count_utf8_cps(line.base+pos+c0,lo-pos-c0))/static_cast<float>(count);
                        const float f1 = static_cast<float>(count_utf8_cps(line.base+pos+c0,hi-pos-c0))/static_cast<float>(count);
                        const float x = cluster_x+(pen-cluster_x)*(rtl ? 1.0f-f1 : f0);
                        push_prim(ctx,Prim::make_rect({x*dpi,cmd->boundingBox.y*dpi,(pen-cluster_x)*(f1-f0)*dpi,cmd->boundingBox.height*dpi},
                            selection_clip->rect,line.color,{}, {},0,1,selection_clip->radius));
                    }
                }
                selection_stop(a,xa,&has_xa);
                selection_stop(b,xb,&has_xb);
                edge(pos + (rtl ? c0 : c1), pen);
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
            if (lo == sp.byte_start) pen += sp.padding_x;
            segment(lo, hi, sp.font_id, sp.font_size_px, sp.letter_spacing, hi == line.end);
            if (hi == sp.byte_end) pen += sp.padding_x;
        }
    } else {
        const auto& td = cmd->renderData.text;
        segment(line.start, line.end, static_cast<uint16_t>(td.fontId & 0x7Fu),
            decode_clay_font_px(td.fontSize, td.fontId), static_cast<float>(td.letterSpacing), false);
    }
    if (!has_xa && a >= line.end) *xa = pen;
    if (!has_xb && b >= line.end) *xb = pen;
}

void emit_selection_line(Context* ctx, const Clay_RenderCommand* cmd, const ClipEntry& clip, float dpi) noexcept {
    const SelectionLine line = selection_line(cmd);
    if (!line.id) return;
    if (reserve_span_hits(&ctx->active_ui->span_hits, &ctx->active_ui->span_hit_cap, ctx->active_ui->span_hit_count, 256u)) {
        ctx->active_ui->span_hits[ctx->active_ui->span_hit_count++] = SpanHitRegion{
            .id = line.id, .clay_elem_id = line.clay_id,
            .rect = {cmd->boundingBox.x, cmd->boundingBox.y, cmd->boundingBox.width, cmd->boundingBox.height},
            .z_index = cmd->zIndex, .clip_rect = {clip.rect.x/dpi,clip.rect.y/dpi,clip.rect.w/dpi,clip.rect.h/dpi}, .has_clip = true };
    }
    const auto* ws = state_map_find(&ctx->active_ui->states, line.id);
    if (!ws) return;
    const uint32_t a = ws->edit_cursor < ws->edit_sel_anchor ? ws->edit_cursor : ws->edit_sel_anchor;
    const uint32_t b = ws->edit_cursor > ws->edit_sel_anchor ? ws->edit_cursor : ws->edit_sel_anchor;
    if (a == b || a >= line.end || b <= line.start) return;
    float xa, xb; uint32_t hit;
    const size_t mark = ctx->frame_arena.mark();
    selection_line_geometry(ctx, cmd, line, a < line.start ? line.start : a, b > line.end ? line.end : b, 0, &xa, &xb, &hit, &clip, dpi);
    ctx->frame_arena.rewind(mark);

}

void emit_rich_text_command(
    Context* ctx,
    const Clay_RenderCommand* cmd,
    const RichTextExtra* rich,
    const ClipEntry& clip,
    float dpi) noexcept
{
    ClipEntry effective_clip = clip;
    if(cmd->renderData.text.truncated) {
        const float width=cmd->renderData.text.maxWidth>0?cmd->renderData.text.maxWidth:cmd->boundingBox.width;
        const auto* layer=ctx->active_render_layer;
        const float sx=layer ? layer->resolved_scale.x : 1.f;
        const float ox=layer ? layer->resolved_offset_px.x : 0.f;
        effective_clip.rect=rect_intersect(clip.rect,Rect{cmd->boundingBox.x*dpi*sx+ox,clip.rect.y,width*dpi*sx,clip.rect.h});
    }
    emit_selection_line(ctx, cmd, effective_clip, dpi);
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
        effective_clip.rect.x / dpi,
        effective_clip.rect.y / dpi,
        effective_clip.rect.w / dpi,
        effective_clip.rect.h / dpi,
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

        if (sp.id != 0u && seg_x1 > seg_x0 && reserve_span_hits(&ctx->active_ui->span_hits, &ctx->active_ui->span_hit_cap, ctx->active_ui->span_hit_count, 256u)) {
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

    float visible_end_px=cmd->boundingBox.x*dpi;
    const RichSpanInternal* ellipsis_span=&rich->spans[0];
    const float full_limit_px=(cmd->boundingBox.x+(td.maxWidth>0?td.maxWidth:cmd->boundingBox.width))*dpi;
    float text_limit_px=full_limit_px;
    if(td.truncated) {
        bool stop=false;
        for(uint32_t ci=0;ci<chunk_count && !stop;++ci) {
            const auto& ch=chunks[ci];const auto& sp=*ch.sp;
            const Str dots=ellipsis_text(ctx,sp.font_id);float reserved=0;
            for(const auto& g:shape_utf8_run(ctx,sp.font_id,dots.chars,dots.length)) reserved+=g.x_advance_1px*sp.font_size_px*dpi;
            float pen=ch.start_x_px;
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
        if (sp->background.a == 0u && sp->border_color.a == 0u) {
            continue;
        }
        const float w_px = (td.truncated ? std::fmin(sg.x1_px,visible_end_px) : sg.x1_px) - sg.x0_px;
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
                effective_clip.rect,
                sp->background,
                Corners{ rl, rr, rr, rl },
                sp->border_color,
                bw,
                1.0f,
                effective_clip.radius));
    }

    if(td.truncated) visible_end_px=cmd->boundingBox.x*dpi;
    bool elided=false;
    // Pass 2: Emit Slug glyphs, underline, and strikethrough decorations.
    for (uint32_t i = 0u; i < chunk_count; ++i) {
        const RichLineChunk& ch = chunks[i];
        const RichSpanInternal* sp = ch.sp;
        const float phys_px = sp->font_size_px * dpi;
        const float ls_px = sp->letter_spacing * dpi;
        float gx = ch.start_x_px;
        for (uint32_t gi = 0u; gi < static_cast<uint32_t>(ch.glyphs.size()); ++gi) {
            const ShapedGlyph& g = ch.glyphs[gi];
            if(td.truncated && (gi==0 || ch.glyphs[gi-1].cluster!=g.cluster)) {
                float advance=0;
                for(uint32_t j=gi;j<ch.glyphs.size()&&ch.glyphs[j].cluster==g.cluster;++j)advance+=ch.glyphs[j].x_advance_1px*phys_px+ls_px;
                if(elided || gx+advance>text_limit_px+0.01f){elided=true;break;}
            }
            if(td.truncated && elided)break;
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
                sp->color);
            gx += g.x_advance_1px * phys_px + (ch.trim_last_spacing ? 0.0f : ls_px);
            visible_end_px=gx;
        }
    }

    if(td.truncated) {
        const Str dots=ellipsis_text(ctx,ellipsis_span->font_id);
        float pen=visible_end_px;
        for(const auto& g:shape_utf8_run(ctx,ellipsis_span->font_id,dots.chars,dots.length)) {
            emit_slug_glyph_quad(ctx,g.font_slot,g.glyph_id,ellipsis_span->font_size_px*dpi,pen,baseline_y_px,g.x_offset_em,g.y_offset_em,effective_clip,ellipsis_span->color);
            pen+=g.x_advance_1px*ellipsis_span->font_size_px*dpi;
        }
    }
    for (uint32_t i = 0u; i < seg_count; ++i) {
        RichLineSeg sg = segs[i];
        if(td.truncated && sg.text_x0_px+sg.text_w_px>visible_end_px)sg.text_w_px=visible_end_px-sg.text_x0_px;
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
    const Clay_RenderCommand* cmd,
    const ClipEntry& clip,
    float dpi) noexcept
{
    if ((cmd->renderData.text.fontId & 0x8000u) != 0u && cmd->userData != nullptr) {
        emit_rich_text_command(ctx, cmd, static_cast<const RichTextExtra*>(cmd->userData), clip, dpi);
        return;
    }

    emit_selection_line(ctx, cmd, clip, dpi);
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

    if(td.truncated) {
        ClipEntry local_clip=clip;
        const float width=td.maxWidth>0?td.maxWidth:cmd->boundingBox.width;
        const auto* layer=ctx->active_render_layer;
        const float sx=layer ? layer->resolved_scale.x : 1.f;
        const float ox=layer ? layer->resolved_offset_px.x : 0.f;
        local_clip.rect=rect_intersect(clip.rect,Rect{cmd->boundingBox.x*dpi*sx+ox,clip.rect.y,width*dpi*sx,clip.rect.h});
        emit_elided_line(ctx,chars,static_cast<uint32_t>(len),primary_slot,logical_font_px,static_cast<float>(td.letterSpacing),
            cmd->boundingBox.x,baseline_y/dpi,width,local_clip,tint,dpi);return;
    }
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

} // namespace etch
