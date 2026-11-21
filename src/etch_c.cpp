// Pure C99 ABI facade over Etch's Orthodox C++20 core.
// Compiled via src/etch_unity.cpp.

#include "etch/etch_c.h"

namespace {

static_assert(sizeof(EtchVec2) == sizeof(etch::Vec2) && alignof(EtchVec2) == alignof(etch::Vec2));
static_assert(sizeof(EtchVec4) == sizeof(etch::Vec4) && alignof(EtchVec4) == alignof(etch::Vec4));
static_assert(sizeof(EtchUint4) == sizeof(etch::Uint4) && alignof(EtchUint4) == alignof(etch::Uint4));
static_assert(sizeof(EtchColor) == sizeof(etch::Color) && alignof(EtchColor) == alignof(etch::Color));
static_assert(sizeof(EtchPrim) == 64 && sizeof(EtchPrim) == sizeof(etch::Prim) && alignof(EtchPrim) == alignof(etch::Prim));
static_assert(sizeof(EtchGlobalsUniform) == 32 && sizeof(EtchGlobalsUniform) == sizeof(etch::GlobalsUniform) &&
    alignof(EtchGlobalsUniform) == alignof(etch::GlobalsUniform));

inline etch::Context* to_ctx(EtchContext* c) noexcept { return reinterpret_cast<etch::Context*>(c); }
inline const etch::Context* to_ctx(const EtchContext* c) noexcept { return reinterpret_cast<const etch::Context*>(c); }
inline EtchContext* from_ctx(etch::Context* c) noexcept { return reinterpret_cast<EtchContext*>(c); }
inline etch::TextBuffer* to_tb(EtchTextBuffer* b) noexcept { return reinterpret_cast<etch::TextBuffer*>(b); }
inline const etch::TextBuffer* to_tb(const EtchTextBuffer* b) noexcept { return reinterpret_cast<const etch::TextBuffer*>(b); }
inline EtchTextBuffer* from_tb(etch::TextBuffer* b) noexcept { return reinterpret_cast<EtchTextBuffer*>(b); }

inline etch::Vec2 to_cpp(EtchVec2 v) noexcept { return {v.x, v.y}; }
inline EtchVec2 from_cpp(etch::Vec2 v) noexcept { return {v.x, v.y}; }
inline etch::Vec4 to_cpp(EtchVec4 v) noexcept { return {v.x, v.y, v.z, v.w}; }
inline etch::Rect to_cpp(EtchRect r) noexcept { return {r.x, r.y, r.w, r.h}; }
inline EtchRect from_cpp(etch::Rect r) noexcept { return {r.x, r.y, r.w, r.h}; }
inline etch::Color to_cpp(EtchColor c) noexcept { return {c.r, c.g, c.b, c.a}; }
inline EtchColor from_cpp(etch::Color c) noexcept { return {c.r, c.g, c.b, c.a}; }
inline etch::Corners to_cpp(EtchCorners c) noexcept { return {c.tl, c.tr, c.br, c.bl}; }
inline EtchCorners from_cpp(etch::Corners c) noexcept { return {c.tl, c.tr, c.br, c.bl}; }
inline etch::Padding to_cpp(EtchPadding p) noexcept { return {p.left, p.right, p.top, p.bottom}; }
inline EtchPadding from_cpp(etch::Padding p) noexcept { return {p.left, p.right, p.top, p.bottom}; }
inline etch::Str to_cpp(EtchStr s) noexcept { return {s.chars, s.length, s.is_static}; }
inline EtchStr from_cpp(etch::Str s) noexcept { return {s.chars, s.length, s.is_static}; }
inline etch::Sizing to_cpp(EtchSizing s) noexcept { return {static_cast<etch::SizingKind>(s.kind), s.value, s.min, s.max}; }
inline EtchSizing from_cpp(etch::Sizing s) noexcept { return {static_cast<EtchSizingKind>(s.kind), s.value, s.min, s.max}; }
inline etch::SpringSpec to_cpp(EtchSpringSpec s) noexcept { return {s.response, s.damping_ratio}; }
inline EtchSpringSpec from_cpp(etch::SpringSpec s) noexcept { return {s.response, s.damping_ratio}; }
inline etch::BorderBeamStyle to_cpp(EtchBorderBeamStyle s) noexcept { return {to_cpp(s.color),to_cpp(s.base_color),s.width,s.angle_rad,s.sweep_fraction,s.glow_px,s.speed,s.softness,s.beam_active,s.symmetric_dual}; }
inline EtchBorderBeamStyle from_cpp(etch::BorderBeamStyle s) noexcept { return {from_cpp(s.color),from_cpp(s.base_color),s.width,s.angle_rad,s.sweep_fraction,s.glow_px,s.speed,s.softness,s.beam_active,s.symmetric_dual}; }

inline etch::GridTrack to_cpp(const EtchGridTrack& t) noexcept {
    return {static_cast<etch::SizingKind>(t.kind), t.value, t.min_px, t.max_px};
}

inline etch::TextSpan to_cpp(const EtchTextSpan& s) noexcept {
    return {to_cpp(s.text), to_cpp(s.color), to_cpp(s.background), to_cpp(s.border_color),
            s.font_size_px, s.font_id, s.underline, s.strikethrough, s.letter_spacing,
            s.padding_x, s.bg_radius, s.id, s.icon_id, to_cpp(s.icon_color), s.icon_size_px,
            s.icon_gap_px, s.padding_y, to_cpp(s.hover_background),
            s.has_wipe, to_cpp(s.color_end), s.wipe_x, s.wipe_feather_px};
}

inline etch::PathCmd to_cpp(const EtchPathCmd& c) noexcept {
    return {static_cast<etch::PathVerb>(c.verb), to_cpp(c.p1), to_cpp(c.p2), to_cpp(c.p3)};
}

inline etch::TableColumnDesc to_cpp(const EtchTableColumnDesc& c) noexcept {
    return {c.id, to_cpp(c.title), static_cast<etch::TableColumnSizing>(c.sizing),
            c.width, c.min_width, c.max_width, static_cast<etch::AlignX>(c.align_x),
            c.resizable, c.sortable};
}

inline etch::Vec2 to_cpp_vec2_ref(const EtchVec2& v) noexcept { return {v.x, v.y}; }
inline etch::Str to_cpp_str_ref(const EtchStr& s) noexcept { return {s.chars, s.length, s.is_static}; }

template <typename CppT, typename CT>
std::span<const CppT> convert_frame_span(const CT* src, uint32_t count, CppT (*conv)(const CT&) noexcept) noexcept {
    if (!src || count == 0u) return {};
    etch::Context* ctx = etch::current_context();
    if (!ctx || !ctx->in_frame) return {};
    CppT* dst = ctx->frame_arena.alloc<CppT>(count);
    if (!dst) return {};
    for (uint32_t i = 0u; i < count; ++i) dst[i] = conv(src[i]);
    return {dst, count};
}

inline etch::Arena to_cpp_arena(const EtchArena* a) noexcept {
    return a ? etch::Arena{a->base, a->capacity, a->offset, a->peak_offset} : etch::Arena{};
}

inline void sync_c_arena(EtchArena* dst, const etch::Arena& src) noexcept {
    if (!dst) return;
    dst->base = src.base;
    dst->capacity = src.capacity;
    dst->offset = src.offset;
    dst->peak_offset = src.peak_offset;
}

inline EtchArena from_cpp_arena(const etch::Arena& a) noexcept {
    return {a.base, a.capacity, a.offset, a.peak_offset};
}

#define ETCH_THEME_COLOR_FIELDS(X) \
    X(bg_canvas) X(bg_surface) X(bg_elevated) X(bg_chrome) X(bg_popup) X(bg_tooltip) \
    X(bg_input) X(bg_table) X(bg_node_canvas) X(surface_control) X(surface_hover) X(surface_active) \
    X(surface_disabled) X(surface_subtle) X(surface_subtle_hover) X(surface_subtle_body) \
    X(surface_selected) X(text_primary) X(text_secondary) X(text_body) X(text_code) \
    X(text_muted) X(text_disabled) X(accent) X(accent_secondary) X(accent_primary) X(focus_ring) \
    X(danger) X(warning) X(border_subtle) X(border_line) X(border_input) X(border_node) \
    X(border_control) X(border_tooltip) X(selection) X(selection_hover) X(zebra_row) \
    X(active_line) X(area_fill_top) X(area_fill_bottom) X(divider_hover) X(divider_thumb_hover) \
    X(grid_minor) X(grid_major) X(canvas_border) X(marquee_fill) X(marquee_border) \
    X(backdrop) X(shadow) X(scrollbar_track) X(scrollbar_thumb) X(scrollbar_thumb_hover) \
    X(scrollbar_thumb_drag) X(syntax_comment) X(syntax_string) X(syntax_number) X(syntax_keyword) X(on_primary) X(on_error)

#define ETCH_THEME_RADIUS_FIELDS(X) \
    X(radius_small_px) X(radius_control_px) X(radius_table_px) \
    X(radius_input_px) X(radius_panel_px) X(radius_pill_px)

inline etch::Theme to_cpp(const EtchTheme& t) noexcept {
    etch::Theme out{};
    out.id = static_cast<etch::ThemeId>(t.id);
#define ETCH_CP_TO_CPP(f) out.f = to_cpp(t.f);
#define ETCH_SC_TO_CPP(f) out.f = t.f;
    ETCH_THEME_COLOR_FIELDS(ETCH_CP_TO_CPP)
    ETCH_THEME_RADIUS_FIELDS(ETCH_SC_TO_CPP)
#undef ETCH_CP_TO_CPP
#undef ETCH_SC_TO_CPP
    return out;
}

inline EtchTheme from_cpp(const etch::Theme& t) noexcept {
    EtchTheme out{};
    out.id = static_cast<EtchThemeId>(t.id);
#define ETCH_CP_FROM_CPP(f) out.f = from_cpp(t.f);
#define ETCH_SC_FROM_CPP(f) out.f = t.f;
    ETCH_THEME_COLOR_FIELDS(ETCH_CP_FROM_CPP)
    ETCH_THEME_RADIUS_FIELDS(ETCH_SC_FROM_CPP)
#undef ETCH_CP_FROM_CPP
#undef ETCH_SC_FROM_CPP
    return out;
}

inline EtchA11yNode from_cpp(const etch::A11yNode& n) noexcept {
    return {n.id, n.parent_id, n.first_child_idx, n.next_sibling_idx,
            static_cast<EtchA11yRole>(n.role), n.state_flags, n.actions_mask,
            from_cpp(n.bounds), from_cpp(n.label), from_cpp(n.value_text),
            n.value_num, n.min_value, n.max_value};
}

inline etch::BoxDesc to_cpp(const EtchBoxDesc& d) noexcept {
    etch::BoxDesc out{};
    out.id = d.id; out.axis = static_cast<etch::Axis>(d.axis);
    out.width = to_cpp(d.width); out.height = to_cpp(d.height); out.padding = to_cpp(d.padding);
    out.gap = d.gap; out.align_x = static_cast<etch::AlignX>(d.align_x);
    out.align_y = static_cast<etch::AlignY>(d.align_y);
    out.background = to_cpp(d.background); out.background_end = to_cpp(d.background_end);
    out.gradient_dir = to_cpp(d.gradient_dir); out.radial_gradient = d.radial_gradient;
    out.border_color = to_cpp(d.border_color); out.border_width = d.border_width;
    out.radius = to_cpp(d.radius); out.shadow_color = to_cpp(d.shadow_color);
    out.shadow_spread = d.shadow_spread; out.shadow_blur = d.shadow_blur; out.shadow_offset = to_cpp(d.shadow_offset);
    out.opacity=d.opacity; out.hover_background=to_cpp(d.hover_background); out.hover_border_color=to_cpp(d.hover_border_color);
    out.hover_scale=d.hover_scale;out.press_scale=d.press_scale;out.animate_layout=d.animate_layout;out.hero_id=d.hero_id;out.transition=to_cpp(d.transition);
    out.border_beam=to_cpp(d.border_beam);out.top_rim_alpha=d.top_rim_alpha;out.scroll_fade_px=d.scroll_fade_px;
    out.border_align=static_cast<etch::BorderAlign>(d.border_align); out.window_drag_passthrough = d.window_drag_passthrough; out.continuous_corners = d.continuous_corners; out.shrink_width = d.shrink_width; out.shrink_height = d.shrink_height; out.clip_children = d.clip_children; out.scroll_x = d.scroll_x; out.scroll_y = d.scroll_y;
    out.show_scrollbar = d.show_scrollbar; out.focusable = d.focusable;
    out.attach_to = static_cast<etch::AttachTo>(d.attach_to);
    out.attach_element_id = d.attach_element_id;
    out.anchor_element = static_cast<etch::Anchor>(d.anchor_element);
    out.anchor_parent = static_cast<etch::Anchor>(d.anchor_parent);
    out.floating_offset = to_cpp(d.floating_offset); out.z_index = d.z_index;
    out.pointer_passthrough = d.pointer_passthrough; out.clip_to_parent = d.clip_to_parent;
    out.backdrop_blur = d.backdrop_blur; out.clamp_to_viewport = d.clamp_to_viewport;
    out.wrap = d.wrap; out.row_gap = d.row_gap; out.aspect_ratio = d.aspect_ratio;
    out.grid_columns = d.grid_columns;
    out.grid_tracks = convert_frame_span<etch::GridTrack, EtchGridTrack>(d.grid_tracks, d.grid_track_count, to_cpp);
    out.grid_col_span = d.grid_col_span;
    out.border_sides = to_cpp(d.border_sides);
    out.border_dash_px = d.border_dash_px;
    out.border_gap_px = d.border_gap_px;
    out.edge_fade = to_cpp(d.edge_fade);
    return out;
}

inline etch::TextDesc to_cpp(const EtchTextDesc& d) noexcept {
    return {to_cpp(d.color), d.font_size_px, d.font_id, d.letter_spacing, d.wrap, d.id,
            d.selectable, to_cpp(d.selection_color), d.line_height_px,
            static_cast<etch::AlignX>(d.align_x), d.ellipsis, d.max_lines};
}

inline etch::RichTextDesc to_cpp(const EtchRichTextDesc& d) noexcept {
    return {to_cpp(d.color), d.font_size_px, d.font_id, d.line_height_px, d.letter_spacing,
            static_cast<etch::AlignX>(d.align_x), d.wrap, d.id, d.selectable,
            to_cpp(d.selection_color), d.ellipsis, d.max_lines};
}

void c_window_refresh_trampoline(etch::Context* ctx, struct SDL_Window* win, float dt_seconds, void* user_data) noexcept {
    (void)user_data;
    if (ctx && ctx->c_refresh_fn) {
        ctx->c_refresh_fn(from_ctx(ctx), win, dt_seconds, ctx->c_refresh_user_data);
    }
}

struct CTokenizerThunk {
    EtchSyntaxTokenizeFn fn;
    void* user_data;
};

uint32_t c_syntax_tokenizer_trampoline(
    uint32_t line_index,
    etch::Str line,
    etch::SyntaxToken* out_tokens,
    uint32_t max_tokens,
    void* user_data) noexcept
{
    const auto* thunk = static_cast<const CTokenizerThunk*>(user_data);
    if (!thunk || !thunk->fn || !out_tokens || max_tokens == 0u) return 0u;
    EtchSyntaxToken local[256];
    EtchSyntaxToken* c_tokens=max_tokens>256 ? etch::current_context()->frame_arena.alloc<EtchSyntaxToken>(max_tokens) : local;
    if(!c_tokens)return 0;
    const uint32_t cap=max_tokens;
    const uint32_t count = thunk->fn(line_index, from_cpp(line), c_tokens, cap, thunk->user_data);
    const uint32_t n = (count < cap) ? count : cap;
    for (uint32_t i = 0u; i < n; ++i) {
        out_tokens[i] = {c_tokens[i].byte_start, c_tokens[i].byte_end,
                         to_cpp(c_tokens[i].color), c_tokens[i].font_id};
    }
    return n;
}

struct CCodeLineDecorateThunk {
    EtchCodeLineDecorateFn fn;
    void* user_data;
};

bool c_code_line_decorate_trampoline(
    uint32_t line_index,
    etch::CodeLineDecoration* out_dec,
    void* user_data) noexcept
{
    const auto* thunk = static_cast<const CCodeLineDecorateThunk*>(user_data);
    if (!thunk || !thunk->fn || !out_dec) return false;
    EtchCodeLineDecoration c_dec = {};
    c_dec.gutter_tag_color = from_cpp(etch::Color::theme_auto());
    if (!thunk->fn(line_index, &c_dec, thunk->user_data)) return false;
    out_dec->row_bg = to_cpp(c_dec.row_bg);
    out_dec->gutter_bar_color = to_cpp(c_dec.gutter_bar_color);
    out_dec->gutter_tag = to_cpp(c_dec.gutter_tag);
    out_dec->gutter_tag_color = to_cpp(c_dec.gutter_tag_color);
    return true;
}

inline EtchVirtualListSlice from_cpp(const etch::VirtualListSlice& s) noexcept {
    return {s.first_index, s.end_index, s.total_count, s.scroll_y, s.max_scroll_y, s.viewport_height, s.at_bottom};
}

inline EtchCanvasState from_cpp(const etch::CanvasState& s) noexcept {
    return {from_cpp(s.pan), s.zoom, from_cpp(s.viewport), s.panning,
            s.marquee_active, s.marquee_finished, from_cpp(s.marquee_world)};
}

} // namespace

/* ============================================================================
 * Descriptor Default Factories
 * ============================================================================ */

EtchContextDesc etch_context_desc_default(void) {
    const etch::ContextDesc d{};
    return {d.max_layout_elements, d.window, d.frame_arena_bytes, d.max_prims_per_frame,
            d.font_path, d.font_size_px, d.enable_gpu_debug, d.headless, d.gpu_driver,
            nullptr, nullptr, d.enable_os_a11y, d.respect_reduced_motion};
}

EtchFrameDesc etch_frame_desc_default(void) {
    const etch::FrameDesc d{};
    return {d.viewport_w, d.viewport_h, d.dpi_scale, d.dt_seconds, from_cpp(d.pointer_pos),
            d.pointer_down, from_cpp(d.scroll_delta), d.right_pointer_down, d.pointer_clicks,
            d.key_mods, d.key_pressed_code, d.pixel_snap, d.bloom_intensity, d.bloom_threshold};
}

EtchWindowFrameDesc etch_window_frame_desc_default(void) {
    const etch::WindowFrameDesc d{};
    return {d.dt, d.min_scale, d.pixel_snap,d.bloom_intensity,d.bloom_threshold};
}

EtchBoxDesc etch_box_desc_default(void) {
    const etch::BoxDesc d{};
    return {d.id, static_cast<EtchAxis>(d.axis), from_cpp(d.width), from_cpp(d.height),
            from_cpp(d.padding), d.gap, static_cast<EtchAlignX>(d.align_x),
            static_cast<EtchAlignY>(d.align_y), from_cpp(d.background), from_cpp(d.background_end),
            from_cpp(d.gradient_dir), d.radial_gradient, from_cpp(d.border_color), d.border_width,
            from_cpp(d.radius), from_cpp(d.shadow_color), d.shadow_blur, from_cpp(d.shadow_offset),
            d.clip_children, d.scroll_x, d.scroll_y, d.show_scrollbar, d.focusable,
            static_cast<EtchAttachTo>(d.attach_to), d.attach_element_id,
            static_cast<EtchAnchor>(d.anchor_element), static_cast<EtchAnchor>(d.anchor_parent),
            from_cpp(d.floating_offset), d.z_index, d.pointer_passthrough, d.clip_to_parent,
            d.backdrop_blur, d.clamp_to_viewport, d.wrap, d.row_gap, d.aspect_ratio,
            d.grid_columns, nullptr, 0u, d.grid_col_span, d.shrink_width, d.shrink_height, d.continuous_corners, d.shadow_spread, d.window_drag_passthrough,static_cast<EtchBorderAlign>(d.border_align),d.opacity,from_cpp(d.hover_background),from_cpp(d.hover_border_color),d.hover_scale,d.press_scale,d.animate_layout,d.hero_id,from_cpp(d.transition),from_cpp(d.border_beam),d.top_rim_alpha,d.scroll_fade_px,
            from_cpp(d.border_sides), d.border_dash_px, d.border_gap_px, from_cpp(d.edge_fade)};
}

EtchTextDesc etch_text_desc_default(void) {
    const etch::TextDesc d{};
    return {from_cpp(d.color), d.font_size_px, d.font_id, d.letter_spacing, d.wrap, d.id,
            d.selectable, from_cpp(d.selection_color), d.line_height_px,
            static_cast<EtchAlignX>(d.align_x), d.ellipsis, d.max_lines};
}

EtchTextSpan etch_text_span_default(void) {
    const etch::TextSpan d{};
    return {from_cpp(d.text), from_cpp(d.color), from_cpp(d.background), from_cpp(d.border_color),
            d.font_size_px, d.font_id, d.underline, d.strikethrough, d.letter_spacing,
            d.padding_x, d.bg_radius, d.id, d.icon_id, from_cpp(d.icon_color), d.icon_size_px,
            d.icon_gap_px, d.padding_y, from_cpp(d.hover_background),
            d.has_wipe, from_cpp(d.color_end), d.wipe_x, d.wipe_feather_px};
}

EtchRichTextDesc etch_rich_text_desc_default(void) {
    const etch::RichTextDesc d{};
    return {from_cpp(d.color), d.font_size_px, d.font_id, d.line_height_px, d.letter_spacing,
            static_cast<EtchAlignX>(d.align_x), d.wrap, d.id, d.selectable,
            from_cpp(d.selection_color), d.ellipsis, d.max_lines};
}

EtchCollapsibleDesc etch_collapsible_desc_default(void) {
    const etch::CollapsibleDesc d{};
    return {d.id, from_cpp(d.title), d.icon_id, d.default_open, d.header_height_px, d.font_size_px,
            d.font_id, from_cpp(d.header_padding), from_cpp(d.body_padding), d.body_gap,
            from_cpp(d.radius), from_cpp(d.header_bg), from_cpp(d.header_hover_bg),
            from_cpp(d.body_bg), from_cpp(d.border_color), d.border_width,
            from_cpp(d.text_color), from_cpp(d.icon_color), from_cpp(d.spring_spec)};
}

EtchImageDesc etch_image_desc_default(void) {
    const etch::ImageDesc d{};
    return {d.id, d.texture, d.sprite_id, from_cpp(d.width), from_cpp(d.height),
            from_cpp(d.uv_min), from_cpp(d.uv_max), from_cpp(d.radius), from_cpp(d.tint),
            from_cpp(d.border_color), d.border_width, d.aspect_ratio, d.grid_col_span, d.continuous_corners};
}

EtchIconDesc etch_icon_desc_default(void) {
    const etch::IconDesc d{};
    return {d.id, d.icon_id, d.size_px, from_cpp(d.color)};
}

EtchRectDrawDesc etch_rect_draw_desc_default(void) {
    const etch::RectDrawDesc d{};
    return {from_cpp(d.rect), from_cpp(d.fill), from_cpp(d.fill_end), from_cpp(d.gradient_dir),
            d.radial_gradient, from_cpp(d.radius), from_cpp(d.border_color), d.border_width,
            d.foreground, d.angle_rad, d.softness, d.continuous_corners,static_cast<EtchBorderAlign>(d.border_align)};
}

EtchLineDesc etch_line_draw_desc_default(void) {
    const etch::LineDesc d{};
    return {from_cpp(d.from), from_cpp(d.to), from_cpp(d.color), from_cpp(d.color_end),
            d.thickness, from_cpp(d.glow_color), d.glow_px, d.foreground};
}

EtchPolylineDesc etch_polyline_desc_default(void) {
    const etch::PolylineDesc d{};
    return {nullptr, 0u, from_cpp(d.color), d.thickness, from_cpp(d.glow_color),
            d.glow_px, d.closed, d.foreground};
}

EtchFilledPolygonDesc etch_filled_polygon_desc_default(void) {
    const etch::FilledPolygonDesc d{};
    return {nullptr, 0u, from_cpp(d.fill), from_cpp(d.fill_end), from_cpp(d.gradient_dir),
            d.radial_gradient, from_cpp(d.stroke_color), d.stroke_width, d.foreground};
}

EtchFilledPathDesc etch_filled_path_desc_default(void) {
    const etch::FilledPathDesc d{};
    return {nullptr, 0u, from_cpp(d.svg_path_d), from_cpp(d.offset), d.scale, from_cpp(d.fill),
            from_cpp(d.fill_end), from_cpp(d.gradient_dir), d.radial_gradient,
            from_cpp(d.stroke_color), d.stroke_width, d.foreground};
}

EtchAreaSeriesDesc etch_area_series_desc_default(void) {
    const etch::AreaSeriesDesc d{};
    return {nullptr, 0u, d.baseline_y, from_cpp(d.fill_top), from_cpp(d.fill_bottom),
            from_cpp(d.line_color), d.line_thickness, d.smooth, d.foreground};
}

EtchBezierDesc etch_bezier_desc_default(void) {
    const etch::BezierDesc d{};
    return {from_cpp(d.p0), from_cpp(d.p1), from_cpp(d.p2), from_cpp(d.p3), d.cubic,
            from_cpp(d.color_start), from_cpp(d.color_end), d.thickness,
            from_cpp(d.glow_color), d.glow_px, d.foreground};
}

EtchRingDesc etch_ring_desc_default(void) {
    const etch::RingDesc d{};
    return {from_cpp(d.center), d.radius, d.thickness, d.start_angle_rad, d.sweep_angle_rad,
            from_cpp(d.color), from_cpp(d.color_end), from_cpp(d.track_color),
            from_cpp(d.glow_color), d.glow_px, d.round_caps, d.foreground};
}

EtchIconDrawDesc etch_icon_draw_desc_default(void) {
    const etch::IconDrawDesc d{};
    return {d.icon_id, from_cpp(d.rect), from_cpp(d.color), d.foreground, d.angle_rad};
}

EtchTextDrawDesc etch_text_draw_desc_default(void) {
    const etch::TextDrawDesc d{};
    return {from_cpp(d.text), from_cpp(d.pos), d.font_id, d.size, from_cpp(d.color),
            d.letter_spacing, static_cast<EtchAlignX>(d.align_x),
            static_cast<EtchAlignY>(d.align_y), d.foreground, d.max_width_px, d.ellipsis,
            d.has_wipe, from_cpp(d.color_end), d.wipe_x, d.wipe_feather_px,
            from_cpp(d.shadow_color), from_cpp(d.shadow_offset), d.shadow_blur};
}

EtchImageDrawDesc etch_image_draw_desc_default(void) {
    const etch::ImageDrawDesc d{};
    return {d.texture, d.sprite_id, from_cpp(d.rect), from_cpp(d.uv_rect),
            from_cpp(d.tint), d.radius, d.foreground, d.continuous_corners};
}

EtchTextureDesc etch_texture_desc_default(void) {
    const etch::TextureDesc d{};
    return {d.rgba_pixels, d.width, d.height, d.row_pitch_bytes, d.srgb,
            d.debug_name, d.premultiplied_alpha};
}

EtchWindowChromeDesc etch_window_chrome_desc_default(void) {
    const etch::WindowChromeDesc d{};
    return {d.enabled, d.resizable, d.border_thickness, d.corner_size,
            d.macos_traffic_lights, d.traffic_light_inset_px, static_cast<EtchWindowBackdrop>(d.backdrop)};
}

EtchCodeEditorDesc etch_code_editor_desc_default(void) {
    const etch::CodeEditorDesc d{};
    return {d.id, nullptr, from_cpp(d.width), from_cpp(d.height), from_cpp(d.padding),
            d.font_size_px, d.font_id, d.line_height_px, d.show_line_numbers, d.gutter_width_px,
            d.tab_width, d.read_only, d.show_scrollbar, d.word_wrap, etch_tokenize_cpp_hlsl,
            nullptr, from_cpp(d.background), from_cpp(d.gutter_background),
            from_cpp(d.active_line_color), from_cpp(d.selection_color), from_cpp(d.text_color),
            from_cpp(d.gutter_text_color), from_cpp(d.gutter_active_text_color),
            from_cpp(d.caret_color), from_cpp(d.composition_color), from_cpp(d.border_color),
            from_cpp(d.focus_border_color), d.border_width, d.focus_border_width,
            from_cpp(d.radius), d.scroll_fade_px, nullptr, nullptr, d.gutter_tag_width_px};
}

EtchTextInputHighlight etch_text_input_highlight_default(void) {
    const etch::TextInputHighlight h{};
    return {h.byte_offset, h.byte_length, from_cpp(h.text_color), from_cpp(h.background),
            from_cpp(h.border_color), h.radius_px};
}

EtchTextInputDesc etch_text_input_desc_default(void) {
    const etch::TextInputDesc d{};
    return {d.id, d.buffer, d.capacity, from_cpp(d.placeholder), from_cpp(d.width),
            from_cpp(d.height), from_cpp(d.padding), d.font_size_px, d.font_id,
            d.letter_spacing, from_cpp(d.text_color), from_cpp(d.placeholder_color),
            from_cpp(d.selection_color), from_cpp(d.caret_color), from_cpp(d.composition_color),
            from_cpp(d.background), from_cpp(d.border_color), from_cpp(d.focus_border_color),
            d.border_width, d.focus_border_width, from_cpp(d.radius), d.multiline, d.word_wrap,
            d.read_only, d.password, d.context_menu, d.clear_button, d.escape_blurs, d.suppress_popup_keys, nullptr, 0u,
            from_cpp(d.ghost_text), from_cpp(d.ghost_color), d.accept_ghost_on_tab, d.submit_on_enter};
}

EtchButtonDesc etch_button_desc_default(void) {
    const etch::ButtonDesc d{};
    return {d.id, from_cpp(d.label), d.icon_id, static_cast<EtchButtonVariant>(d.variant),
            from_cpp(d.width), from_cpp(d.height), from_cpp(d.padding), d.font_size_px,
            d.font_id, from_cpp(d.radius), d.disabled, from_cpp(d.shortcut_hint), static_cast<EtchAlignX>(d.align_x)};
}

EtchCheckboxDesc etch_checkbox_desc_default(void) {
    const etch::CheckboxDesc d{};
    return {d.id, from_cpp(d.label), d.checked, d.indeterminate, d.disabled,
            d.box_size_px, d.font_size_px, d.font_id, from_cpp(d.accent)};
}

EtchToggleSwitchDesc etch_toggle_switch_desc_default(void) {
    const etch::ToggleSwitchDesc d{};
    return {d.id, from_cpp(d.label), d.value, d.disabled, d.track_width_px,
            d.track_height_px, d.font_size_px, d.font_id, from_cpp(d.on_color),
            from_cpp(d.off_color)};
}

EtchProgressBarDesc etch_progress_bar_desc_default(void) {
    const etch::ProgressBarDesc d{};
    return {d.id, d.fraction, from_cpp(d.overlay_text), from_cpp(d.width),
            from_cpp(d.height), from_cpp(d.radius), from_cpp(d.track_color),
            from_cpp(d.fill_color), from_cpp(d.text_color), d.font_size_px};
}

EtchTabBarDesc etch_tab_bar_desc_default(void) {
    const etch::TabBarDesc d{};
    return {d.id, d.active_tab, from_cpp(d.width), d.height_px, d.font_size_px,
            from_cpp(d.background), from_cpp(d.active_bg), from_cpp(d.hover_bg),
            from_cpp(d.accent), from_cpp(d.text_color), from_cpp(d.active_text_color)};
}

EtchTabItemDesc etch_tab_item_desc_default(void) {
    const etch::TabItemDesc d{};
    return {d.id, from_cpp(d.label), d.icon_id, d.closable, d.disabled};
}

EtchSliderDesc etch_slider_desc_default(void) {
    const etch::SliderDesc d{};
    return {d.id, from_cpp(d.label), d.value, d.min_value, d.max_value, d.step, d.format,
            from_cpp(d.width), d.height_px, d.disabled, from_cpp(d.background),
            from_cpp(d.fill_color), from_cpp(d.text_color)};
}

EtchDragValueDesc etch_drag_value_desc_default(void) {
    const etch::DragValueDesc d{};
    return {d.id, from_cpp(d.label), d.value, d.min_value, d.max_value, d.speed, d.step,
            d.format, from_cpp(d.width), d.height_px, d.disabled, from_cpp(d.background),
            from_cpp(d.fill_color), from_cpp(d.text_color)};
}

EtchTooltipDesc etch_tooltip_desc_default(void) {
    const etch::TooltipDesc d{};
    return {from_cpp(d.text), d.delay_seconds, d.max_width_px, d.font_size_px, d.z_index};
}

EtchModalDesc etch_modal_desc_default(void) {
    const etch::ModalDesc d{};
    return {d.id, d.open, from_cpp(d.title), d.width_px, from_cpp(d.height),
            d.dismiss_on_backdrop, d.dismiss_on_escape, from_cpp(d.backdrop_color),
            from_cpp(d.background), from_cpp(d.border_color), from_cpp(d.radius), d.z_index};
}

EtchComboBoxDesc etch_combo_box_desc_default(void) {
    const etch::ComboBoxDesc d{};
    return {d.id, from_cpp(d.label), d.selected_index, nullptr, 0u, from_cpp(d.width),
            d.height_px, d.max_popup_height_px, d.disabled, from_cpp(d.background),
            from_cpp(d.hover_bg), from_cpp(d.text_color)};
}

EtchPopupDesc etch_popup_desc_default(void) {
    const etch::PopupDesc d{};
    return {d.id, d.trigger_id, from_cpp(d.width), from_cpp(d.height), from_cpp(d.padding),
            d.gap, from_cpp(d.background), from_cpp(d.border_color), from_cpp(d.radius), d.z_index,d.attach_to_trigger,d.match_trigger_width,d.scroll_y};
}

EtchMenuBarDesc etch_menu_bar_desc_default(void) {
    const etch::MenuBarDesc d{};
    return {d.id, from_cpp(d.width), d.height_px, from_cpp(d.background)};
}

EtchMenuDesc etch_menu_desc_default(void) {
    const etch::MenuDesc d{};
    return {d.id, from_cpp(d.label), d.disabled};
}

EtchMenuItemDesc etch_menu_item_desc_default(void) {
    const etch::MenuItemDesc d{};
    return {d.id, from_cpp(d.label), from_cpp(d.shortcut_hint), d.icon_id, d.checked,
            d.disabled, d.separator_after};
}

EtchSplitDesc etch_split_desc_default(void) {
    const etch::SplitDesc d{};
    return {d.id, static_cast<EtchAxis>(d.axis), from_cpp(d.width), from_cpp(d.height),
            d.default_ratio, d.min_first_px, d.min_second_px, d.divider_size_px,
            from_cpp(d.divider_color), from_cpp(d.divider_hover_color),
            from_cpp(d.divider_active_color), d.divider_hit_px, d.keyboard_step_px};
}

EtchVirtualListDesc etch_virtual_list_desc_default(void) {
    const etch::VirtualListDesc d{};
    return {d.id, d.item_count, d.item_height_px, d.row_gap_px, d.overscan_rows,
            from_cpp(d.width), from_cpp(d.height), from_cpp(d.padding), from_cpp(d.background),
            from_cpp(d.border_color), d.border_width, from_cpp(d.radius), d.show_scrollbar,
            nullptr, d.measure_user_data, d.variable_height, d.scroll_fade_px, d.stick_to_bottom};
}

EtchDockPaneDesc etch_dock_pane_desc_default(void) {
    const etch::DockPaneDesc d{};
    return {d.id, from_cpp(d.title), d.icon_id, d.closable, d.default_open};
}

EtchDockSpaceDesc etch_dock_space_desc_default(void) {
    const etch::DockSpaceDesc d{};
    return {d.id, from_cpp(d.width), from_cpp(d.height), d.tab_bar_height_px, d.divider_size_px,
            from_cpp(d.background), from_cpp(d.pane_background), from_cpp(d.pane_border_color),
            d.pane_border_width, from_cpp(d.pane_radius), from_cpp(d.tab_bar_bg),
            from_cpp(d.tab_active_bg), from_cpp(d.tab_hover_bg), from_cpp(d.tab_accent)};
}

EtchTableColumnDesc etch_table_column_desc_default(void) {
    const etch::TableColumnDesc d{};
    return {d.id, from_cpp(d.title), static_cast<EtchTableColumnSizing>(d.sizing),
            d.width, d.min_width, d.max_width, static_cast<EtchAlignX>(d.align_x),
            d.resizable, d.sortable};
}

EtchTableDesc etch_table_desc_default(void) {
    const etch::TableDesc d{};
    return {d.id, nullptr, 0u, d.item_count, d.header_height_px, d.row_height_px, d.row_gap_px,
            d.overscan_rows, d.default_sort_column, d.default_sort_descending, d.cell_padding_x,
            from_cpp(d.width), from_cpp(d.height), from_cpp(d.padding), from_cpp(d.background),
            from_cpp(d.header_background), from_cpp(d.zebra_color), from_cpp(d.hover_row_color),
            from_cpp(d.border_color), d.border_width, from_cpp(d.radius), d.show_scrollbar, d.show_column_lines, d.header_font_size_px, d.selected_row, from_cpp(d.selected_row_color),d.horizontal_scroll,d.keyboard_navigation,{d.selection.focused_row,d.selection.anchor_row,d.selection.selected_words,d.selection.word_count},d.scroll_fade_px};
}

EtchTreeNodeDesc etch_tree_node_desc_default(void) {
    const etch::TreeNodeDesc d{};
    return {d.id, from_cpp(d.label), d.icon_id, from_cpp(d.icon_color), from_cpp(d.text_color),
            from_cpp(d.selected_bg), from_cpp(d.selected_accent), from_cpp(d.hover_bg),
            d.row_height_px, d.font_size_px, d.indent_px, d.font_id, d.default_open,
            d.selected, d.leaf, d.show_indent_guide};
}

EtchCanvasDesc etch_canvas_desc_default(void) {
    const etch::CanvasDesc d{};
    return {d.id, from_cpp(d.width), from_cpp(d.height), from_cpp(d.default_pan), d.default_zoom,
            d.min_zoom, d.max_zoom, d.grid_step_world, d.major_grid_subdiv, from_cpp(d.background),
            from_cpp(d.minor_grid_color), from_cpp(d.major_grid_color), from_cpp(d.border_color),
            d.border_width, from_cpp(d.radius), d.show_grid, d.interactive, d.marquee_select,
            from_cpp(d.marquee_fill), from_cpp(d.marquee_border)};
}

EtchCanvasWireDesc etch_canvas_wire_desc_default(void) {
    const etch::CanvasWireDesc d{};
    return {from_cpp(d.from_world), from_cpp(d.to_world), from_cpp(d.color_start),
            from_cpp(d.color_end), from_cpp(d.glow_color), d.thickness_world, d.glow_world,
            d.tangent_bias, d.z_index};
}

EtchCanvasNodeDesc etch_canvas_node_desc_default(void) {
    const etch::CanvasNodeDesc d{};
    return {d.id, nullptr, from_cpp(d.pos_value), from_cpp(d.width_world), from_cpp(d.height_world),
            from_cpp(d.padding_world), d.gap_world, static_cast<EtchAxis>(d.axis),
            from_cpp(d.background), from_cpp(d.background_end), from_cpp(d.gradient_dir),
            from_cpp(d.border_color), d.border_width_world, from_cpp(d.radius_world),
            from_cpp(d.shadow_color), d.shadow_blur_world, from_cpp(d.shadow_offset_world),
            d.z_index, d.draggable};
}

/* ============================================================================
 * Arena, Theme & System API
 * ============================================================================ */

EtchArena etch_arena_create(size_t capacity_bytes) {
    return from_cpp_arena(etch::arena_create(capacity_bytes));
}

EtchArena etch_arena_from_buffer(void* buffer, size_t capacity_bytes) {
    return from_cpp_arena(etch::arena_from_buffer(buffer, capacity_bytes));
}

void etch_arena_destroy(EtchArena* arena) {
    if (!arena) return;
    etch::Arena a = to_cpp_arena(arena);
    etch::arena_destroy(&a);
    sync_c_arena(arena, a);
}

void etch_arena_reset(EtchArena* arena) {
    if (!arena) return;
    arena->offset = 0u;
}

size_t etch_arena_mark(const EtchArena* arena) {
    return arena ? arena->offset : 0u;
}

void etch_arena_rewind(EtchArena* arena, size_t saved_mark) {
    if (!arena || saved_mark > arena->offset) return;
    arena->offset = saved_mark;
}

void* etch_arena_alloc_bytes(EtchArena* arena, size_t size, size_t align) {
    if (!arena || align == 0u || (align & (align - 1u)) != 0u) return nullptr;
    etch::Arena a = to_cpp_arena(arena);
    void* p = a.alloc_bytes(size, align);
    sync_c_arena(arena, a);
    return p;
}

EtchStr etch_arena_push_str(EtchArena* arena, const char* s, size_t len) {
    if (!arena) return {};
    etch::Arena a = to_cpp_arena(arena);
    const etch::Str out = a.push_str(s, len);
    sync_c_arena(arena, a);
    return from_cpp(out);
}

void* etch_frame_arena_alloc(EtchContext* ctx, size_t size, size_t align) {
    etch::Context* c = to_ctx(ctx);
    if (!c || align == 0u || (align & (align - 1u)) != 0u) return nullptr;
    return c->frame_arena.alloc_bytes(size, align);
}

EtchStr etch_frame_arena_push_str(EtchContext* ctx, const char* s, size_t len) {
    etch::Context* c = to_ctx(ctx);
    return c ? from_cpp(c->frame_arena.push_str(s, len)) : EtchStr{};
}

EtchSystemTheme etch_system_theme(void) { return static_cast<EtchSystemTheme>(etch::system_theme()); }
bool etch_system_dark_mode(bool default_when_unknown) { return etch::system_dark_mode(default_when_unknown); }
EtchTheme etch_theme_dark(void) { return from_cpp(etch::Theme::dark()); }
EtchTheme etch_theme_light(void) { return from_cpp(etch::Theme::light()); }
EtchColor etch_color_oklch(float L, float C, float hue_deg, float alpha) { return from_cpp(etch::Color::oklch(L,C,hue_deg,alpha)); }
EtchColor etch_color_lerp_oklab(EtchColor a, EtchColor b, float t) { return from_cpp(etch::Color::lerp_oklab(to_cpp(a),to_cpp(b),t)); }
EtchTheme etch_theme_from_seed(EtchColor seed, bool dark_mode, float chroma_scale) { return from_cpp(etch::Theme::from_seed(to_cpp(seed),dark_mode,chroma_scale)); }
EtchSpringSpec etch_spring_spec_snappy(void) { return from_cpp(etch::SpringSpec::snappy()); }
EtchSpringSpec etch_spring_spec_fluid(void) { return from_cpp(etch::SpringSpec::fluid()); }
EtchSpringSpec etch_spring_spec_bouncy(void) { return from_cpp(etch::SpringSpec::bouncy()); }
EtchSpringSpec etch_spring_spec_gentle(void) { return from_cpp(etch::SpringSpec::gentle()); }
EtchTheme etch_theme_high_contrast(void) { return from_cpp(etch::Theme::high_contrast()); }
EtchTheme etch_theme_industrial(void) { return from_cpp(etch::Theme::industrial()); }
void etch_set_theme(const EtchTheme* theme, EtchContext* ctx) { if (theme) etch::set_theme(to_cpp(*theme), to_ctx(ctx)); }
EtchTheme etch_get_theme(const EtchContext* ctx) { return from_cpp(etch::theme(to_ctx(ctx))); }
void etch_push_theme(const EtchTheme* theme) { if (theme) etch::push_theme(to_cpp(*theme)); }
void etch_pop_theme(void) { etch::pop_theme(); }

/* ============================================================================
 * Context Lifecycle, Fonts, Icons, Events, Frames & Window Management
 * ============================================================================ */

EtchContext* etch_create_context(const EtchContextDesc* desc) {
    etch::ContextDesc cpp_desc{};
    if (desc) {
        cpp_desc = {desc->max_layout_elements, desc->window, desc->frame_arena_bytes,
                    desc->max_prims_per_frame, desc->font_path, desc->font_size_px,
                    desc->enable_gpu_debug, desc->headless, desc->gpu_driver,
                    nullptr, nullptr, desc->enable_os_a11y, desc->respect_reduced_motion};
    }
    etch::Context* ctx = etch::create_context(cpp_desc);
    if (ctx && desc && desc->on_window_refresh) {
        etch_set_window_refresh_callback(from_ctx(ctx), desc->on_window_refresh, desc->refresh_user_data);
    }
    return from_ctx(ctx);
}

void etch_destroy_context(EtchContext* ctx) { etch::destroy_context(to_ctx(ctx)); }
EtchContext* etch_current_context(void) { return from_ctx(etch::current_context()); }
void etch_set_current_context(EtchContext* ctx) { etch::set_current_context(to_ctx(ctx)); }
struct SDL_GPUDevice* etch_gpu_device(EtchContext* ctx) { return etch::gpu_device(to_ctx(ctx)); }

uint16_t etch_load_system_font(EtchContext* ctx, EtchStr family_name, EtchFontWeight weight, uint16_t slot_hint) {
    return etch::load_system_font(to_ctx(ctx), to_cpp(family_name), static_cast<etch::FontWeight>(weight), slot_hint);
}

uint16_t etch_load_font_file(EtchContext* ctx, const char* path, uint16_t slot_hint, int32_t face_index) {
    return etch::load_font_file(to_ctx(ctx), path, slot_hint, face_index);
}

uint16_t etch_load_font_memory(EtchContext* ctx, const void* data, size_t size_bytes, uint16_t slot_hint, int32_t face_index) {
    return etch::load_font_memory(to_ctx(ctx), data, size_bytes, slot_hint, face_index);
}

bool etch_set_font_tabular_numbers(EtchContext* ctx, uint16_t font_id, bool enabled) {
    return etch::set_font_tabular_numbers(to_ctx(ctx), font_id, enabled);
}
void etch_set_hinted_small_text(EtchContext* ctx, bool enabled) {
    etch::set_hinted_small_text(to_ctx(ctx), enabled);
}
bool etch_hinted_small_text(const EtchContext* ctx) {
    return etch::hinted_small_text(to_ctx(ctx));
}
bool etch_is_font_loaded(const EtchContext* ctx, uint16_t font_id) {
    return etch::is_font_loaded(to_ctx(ctx), font_id);
}

EtchSvgStrokeDesc etch_svg_stroke_desc_default(void) {
    const etch::SvgStrokeDesc d{};
    return {{d.view_box.x, d.view_box.y, d.view_box.z, d.view_box.w}, d.stroke_width, static_cast<EtchStrokeCap>(d.cap),
            static_cast<EtchStrokeJoin>(d.join), d.miter_limit, d.slot_hint};
}

uint16_t etch_register_svg_stroke_icon(EtchContext* ctx, EtchStr path, const EtchSvgStrokeDesc* d) {
    if (!d) return etch::register_svg_stroke_icon(to_ctx(ctx), to_cpp(path));
    if (d->cap < ETCH_STROKE_CAP_BUTT || d->cap > ETCH_STROKE_CAP_SQUARE ||
        d->join < ETCH_STROKE_JOIN_MITER || d->join > ETCH_STROKE_JOIN_BEVEL) return ETCH_INVALID_ICON;
    return etch::register_svg_stroke_icon(to_ctx(ctx), to_cpp(path),
        {to_cpp(d->view_box), d->stroke_width, static_cast<etch::StrokeCap>(d->cap),
         static_cast<etch::StrokeJoin>(d->join), d->miter_limit, d->slot_hint});
}

uint16_t etch_register_svg_icon(EtchContext* ctx, EtchStr svg_path_d, EtchVec4 view_box, uint16_t slot_hint) {
    return etch::register_svg_icon(to_ctx(ctx), to_cpp(svg_path_d), to_cpp(view_box), slot_hint);
}

bool etch_is_icon_loaded(const EtchContext* ctx, uint16_t icon_id) {
    return etch::is_icon_loaded(to_ctx(ctx), icon_id);
}

void etch_process_event(EtchContext* ctx, const union SDL_Event* event) {
    if (ctx && event) etch::process_event(to_ctx(ctx), *event);
}

bool etch_needs_redraw(const EtchContext* ctx) { return etch::needs_redraw(to_ctx(ctx)); }
void etch_request_redraw(EtchContext* ctx) { etch::request_redraw(to_ctx(ctx)); }
void etch_wake_up(EtchContext* ctx) { etch::wake_up(to_ctx(ctx)); }
bool etch_wait_events(EtchContext* ctx, union SDL_Event* first_event_out, int32_t timeout_ms) {
    return etch::wait_events(to_ctx(ctx), first_event_out, timeout_ms);
}

void etch_begin_frame(EtchContext* ctx, const EtchFrameDesc* desc) {
    if (!ctx) return;
    etch::FrameDesc f{};
    if (desc) {
        f = {desc->viewport_w, desc->viewport_h, desc->dpi_scale, desc->dt_seconds,
             to_cpp(desc->pointer_pos), desc->pointer_down, to_cpp(desc->scroll_delta),
             desc->right_pointer_down, desc->pointer_clicks, desc->key_mods, desc->key_pressed_code, desc->pixel_snap, desc->bloom_intensity, desc->bloom_threshold};
    }
    etch::begin_frame(to_ctx(ctx), f);
}

void etch_end_frame(EtchContext* ctx) { etch::end_frame(to_ctx(ctx)); }
bool etch_frame_in_progress(const EtchContext* ctx) { return etch::frame_in_progress(to_ctx(ctx)); }

void etch_begin_window_frame(EtchContext* ctx, struct SDL_Window* window, const EtchWindowFrameDesc* desc) {
    const etch::WindowFrameDesc w = desc ? etch::WindowFrameDesc{desc->dt, desc->min_scale, desc->pixel_snap,desc->bloom_intensity,desc->bloom_threshold} : etch::WindowFrameDesc{};
    etch::begin_window_frame(to_ctx(ctx), window, w);
}

void etch_end_window_frame(EtchContext* ctx) { etch::end_window_frame(to_ctx(ctx)); }
bool etch_needs_window_redraw(const EtchContext* ctx, struct SDL_Window* window) { return etch::needs_window_redraw(to_ctx(ctx), window); }
void etch_request_window_redraw(EtchContext* ctx, struct SDL_Window* window) { etch::request_window_redraw(to_ctx(ctx), window); }

uint32_t etch_frame_prim_count(const EtchContext* ctx) { return static_cast<uint32_t>(etch::frame_prims(to_ctx(ctx)).size()); }
bool etch_get_frame_prim(const EtchContext* ctx, uint32_t index, EtchPrim* out_prim) {
    const auto p = etch::frame_prims(to_ctx(ctx));
    if (!out_prim || index >= p.size()) return false;
    std::memcpy(out_prim, &p[index], sizeof(EtchPrim));
    return true;
}

EtchFrameStats etch_frame_stats(const EtchContext* ctx) {
    const etch::FrameStats s = etch::frame_stats(to_ctx(ctx));
    return {s.a11y_node_count, s.prim_count, s.prim_capacity, s.draw_call_count, s.blur_pass_count,
            s.slug_glyph_count, s.slug_curve_count, s.dynamic_slug_curve_count,
            s.arena_used_bytes, s.arena_peak_bytes, s.layout_ms, s.gpu_submit_ms,
            s.active_animations, s.idle_sleeping, s.ime_cursor_offset_win,
            s.sprite_count, s.sprite_atlas_size};
}

bool etch_render_window(EtchContext* ctx, EtchColor clear_color, struct SDL_Window* window_override) {
    return etch::render_window(to_ctx(ctx), to_cpp(clear_color), window_override);
}
void etch_release_window(EtchContext* ctx, struct SDL_Window* window) { etch::release_window(to_ctx(ctx), window); }
uint32_t etch_claimed_window_count(const EtchContext* ctx) { return etch::claimed_window_count(to_ctx(ctx)); }
bool etch_render_headless_png(EtchContext* ctx, uint32_t width, uint32_t height, const char* out_png_path, EtchColor clear_color) {
    return etch::render_headless_png(to_ctx(ctx), width, height, out_png_path, to_cpp(clear_color));
}

void etch_set_window_refresh_callback(EtchContext* ctx_arg, EtchWindowRefreshFn fn, void* user_data) {
    etch::Context* ctx = ctx_arg ? to_ctx(ctx_arg) : etch::current_context();
    if (!ctx) return;
    if (fn) {
        etch::set_window_refresh_callback(ctx, c_window_refresh_trampoline, ctx);
        ctx->c_refresh_fn = fn;
        ctx->c_refresh_user_data = user_data;
    } else {
        etch::set_window_refresh_callback(ctx, nullptr, nullptr);
    }
}

void etch_set_window_chrome(const EtchWindowChromeDesc* desc, struct SDL_Window* window) {
    if (!desc) return;
    etch::set_window_chrome({desc->enabled, desc->resizable, desc->border_thickness, desc->corner_size,
                             desc->macos_traffic_lights, desc->traffic_light_inset_px, static_cast<etch::WindowBackdrop>(desc->backdrop)}, window);
}
void etch_window_drag_region(EtchRect rect_logical) { etch::window_drag_region(to_cpp(rect_logical)); }
void etch_window_maximize_button_region(EtchRect rect_logical) { etch::window_maximize_button_region(to_cpp(rect_logical)); }
void etch_window_maximize_button_element(uint32_t element_id) { etch::window_maximize_button_region(element_id); }
void etch_window_client_region(EtchRect rect_logical) { etch::window_client_region(to_cpp(rect_logical)); }
EtchWindowHit etch_evaluate_window_hit(const EtchContext* ctx, EtchVec2 logical_pos, struct SDL_Window* window) {
    return static_cast<EtchWindowHit>(etch::evaluate_window_hit(to_ctx(ctx), to_cpp(logical_pos), window));
}
void etch_minimize_window(struct SDL_Window* window) { etch::minimize_window(window); }
void etch_maximize_window(struct SDL_Window* window) { etch::maximize_window(window); }
void etch_restore_window(struct SDL_Window* window) { etch::restore_window(window); }
bool etch_is_window_maximized(const EtchContext* ctx, struct SDL_Window* window) { return etch::is_window_maximized(to_ctx(ctx), window); }
bool etch_is_window_minimized(const EtchContext* ctx, struct SDL_Window* window) { return etch::is_window_minimized(to_ctx(ctx), window); }
bool etch_set_window_fullscreen(bool fullscreen, struct SDL_Window* window) { return etch::set_window_fullscreen(fullscreen, window); }
bool etch_is_window_fullscreen(const EtchContext* ctx, struct SDL_Window* window) { return etch::is_window_fullscreen(to_ctx(ctx), window); }
EtchPadding etch_window_safe_insets(const EtchContext* ctx, struct SDL_Window* window) { return from_cpp(etch::window_safe_insets(to_ctx(ctx), window)); }

/* ============================================================================
 * Accessibility (A11y) Snapshot, Actions & Native OS Bridge
 * ============================================================================ */

void etch_set_a11y_override(const EtchA11yOverrideDesc* desc) {
    if (!desc) return;
    etch::set_a11y_override({desc->element_id, static_cast<etch::A11yRole>(desc->role),
                             to_cpp(desc->label), to_cpp(desc->value_text),
                             desc->add_states, desc->clear_states, desc->actions_mask,
                             desc->has_role, desc->has_label, desc->has_value_text,
                             desc->has_actions_mask});
}

uint32_t etch_a11y_node_count(const EtchContext* ctx, struct SDL_Window* window) {
    return static_cast<uint32_t>(etch::frame_a11y_nodes(to_ctx(ctx), window).size());
}

bool etch_get_a11y_node(uint32_t index, EtchA11yNode* out_node, const EtchContext* ctx, struct SDL_Window* window) {
    const auto nodes = etch::frame_a11y_nodes(to_ctx(ctx), window);
    if (!out_node || index >= nodes.size()) return false;
    *out_node = from_cpp(nodes[index]);
    return true;
}

bool etch_find_a11y_node(uint32_t element_id, EtchA11yNode* out_node, const EtchContext* ctx, struct SDL_Window* window) {
    if (!out_node) return false;
    const etch::A11yNode* n = etch::find_a11y_node(element_id, to_ctx(ctx), window);
    if (!n) return false;
    *out_node = from_cpp(*n);
    return true;
}

bool etch_perform_a11y_action(uint32_t element_id, EtchA11yAction action, float numeric_value, EtchStr text_value, EtchContext* ctx, struct SDL_Window* window) {
    return etch::perform_a11y_action(element_id, static_cast<etch::A11yAction>(action), numeric_value,
                                     to_cpp(text_value), to_ctx(ctx), window);
}
const char* etch_a11y_role_name(EtchA11yRole role) { return etch::a11y_role_name(static_cast<etch::A11yRole>(role)); }
void etch_set_os_a11y_enabled(EtchContext* ctx, bool enabled) { etch::set_os_a11y_enabled(to_ctx(ctx), enabled); }
bool etch_os_a11y_enabled(const EtchContext* ctx) { return etch::os_a11y_enabled(to_ctx(ctx)); }
bool etch_os_a11y_active(const EtchContext* ctx, struct SDL_Window* window) { return etch::os_a11y_active(to_ctx(ctx), window); }

/* ============================================================================
 * Layout Boxes, Text, Collapsibles, Images, Icons & 2D Draw Commands
 * ============================================================================ */

void etch_begin_box(const EtchBoxDesc* desc) { etch::begin_box(desc ? to_cpp(*desc) : etch::BoxDesc{}); }
void etch_end_box(void) { etch::end_box(); }
void etch_box(const EtchBoxDesc* desc) { etch::box(desc ? to_cpp(*desc) : etch::BoxDesc{}); }
void etch_text(EtchStr content, const EtchTextDesc* desc) { etch::text(to_cpp(content), desc ? to_cpp(*desc) : etch::TextDesc{}); }
void etch_text_spans(const EtchTextSpan* spans, uint32_t span_count, const EtchRichTextDesc* desc) {
    etch::text_spans(convert_frame_span<etch::TextSpan, EtchTextSpan>(spans, span_count, to_cpp),
                     desc ? to_cpp(*desc) : etch::RichTextDesc{});
}

EtchCollapsibleResult etch_begin_collapsible(const EtchCollapsibleDesc* desc) {
    if (!desc) return {};
    const etch::CollapsibleResult r = etch::begin_collapsible({
        desc->id, to_cpp(desc->title), desc->icon_id, desc->default_open, desc->header_height_px,
        desc->font_size_px, desc->font_id, to_cpp(desc->header_padding), to_cpp(desc->body_padding),
        desc->body_gap, to_cpp(desc->radius), to_cpp(desc->header_bg), to_cpp(desc->header_hover_bg),
        to_cpp(desc->body_bg), to_cpp(desc->border_color), desc->border_width,
        to_cpp(desc->text_color), to_cpp(desc->icon_color), to_cpp(desc->spring_spec)});
    return {r.open, r.visible, r.toggled, r.header_hovered, r.anim_t};
}
void etch_end_collapsible(void) { etch::end_collapsible(); }
bool etch_is_collapsible_open(uint32_t element_id) { return etch::is_collapsible_open(element_id); }
void etch_set_collapsible_open(uint32_t element_id, bool open) { etch::set_collapsible_open(element_id, open); }

void etch_image(const EtchImageDesc* desc) {
    if (!desc) return;
    etch::image({desc->id, desc->texture, desc->sprite_id, to_cpp(desc->width), to_cpp(desc->height),
                 to_cpp(desc->uv_min), to_cpp(desc->uv_max), to_cpp(desc->radius), to_cpp(desc->tint),
                 to_cpp(desc->border_color), desc->border_width, desc->aspect_ratio, desc->grid_col_span,
                 desc->continuous_corners});
}
void etch_icon(const EtchIconDesc* desc) {
    if (!desc) return;
    etch::icon({desc->id, desc->icon_id, desc->size_px, to_cpp(desc->color)});
}

void etch_draw_rect(const EtchRectDrawDesc* desc) {
    if (!desc) return;
    etch::draw_rect({to_cpp(desc->rect), to_cpp(desc->fill), to_cpp(desc->fill_end),
                     to_cpp(desc->gradient_dir), desc->radial_gradient, to_cpp(desc->radius),
                     to_cpp(desc->border_color), desc->border_width, desc->foreground, desc->angle_rad, desc->softness, desc->continuous_corners,static_cast<etch::BorderAlign>(desc->border_align)});
}
EtchMeshRectDrawDesc etch_mesh_rect_draw_desc_default(void) {
    EtchMeshRectDrawDesc d={};d.oklab=true;return d;
}
void etch_draw_mesh_rect(const EtchMeshRectDrawDesc* desc) {
    if(!desc) return;
    etch::draw_mesh_rect({to_cpp(desc->rect),to_cpp(desc->color_tl),to_cpp(desc->color_tr),
        to_cpp(desc->color_br),to_cpp(desc->color_bl),to_cpp(desc->radius),desc->oklab,
        desc->grain_intensity,desc->warp_phase,desc->continuous_corners,desc->foreground});
}
EtchPunchHoleDesc etch_punch_hole_desc_default(void) {
    EtchPunchHoleDesc d={};d.softness=1;return d;
}
void etch_punch_hole(const EtchPunchHoleDesc* desc) {
    if(desc) etch::punch_hole({to_cpp(desc->rect),to_cpp(desc->radius),desc->softness,desc->continuous_corners,desc->foreground});
}
EtchBorderBeamStyle etch_border_beam_style_default(void) { return from_cpp(etch::BorderBeamStyle{}); }
EtchBorderBeamDrawDesc etch_border_beam_draw_desc_default(void) {
    EtchBorderBeamDrawDesc d={};d.beam=etch_border_beam_style_default();return d;
}
void etch_draw_border_beam(const EtchBorderBeamDrawDesc* d) {
    if(d) etch::draw_border_beam({to_cpp(d->rect),to_cpp(d->radius),to_cpp(d->beam),d->top_rim_alpha,d->continuous_corners,d->foreground});
}
void etch_draw_line(const EtchLineDesc* desc) {
    if (!desc) return;
    etch::draw_line({to_cpp(desc->from), to_cpp(desc->to), to_cpp(desc->color), to_cpp(desc->color_end),
                     desc->thickness, to_cpp(desc->glow_color), desc->glow_px, desc->foreground});
}
void etch_draw_filled_polygon(const EtchFilledPolygonDesc* desc) {
    if (!desc) return;
    etch::draw_filled_polygon({convert_frame_span<etch::Vec2, EtchVec2>(desc->points, desc->point_count, to_cpp_vec2_ref),
                               to_cpp(desc->fill), to_cpp(desc->fill_end), to_cpp(desc->gradient_dir),
                               desc->radial_gradient, to_cpp(desc->stroke_color), desc->stroke_width,
                               desc->foreground});
}
void etch_draw_filled_path(const EtchFilledPathDesc* desc) {
    if (!desc) return;
    etch::draw_filled_path({convert_frame_span<etch::PathCmd, EtchPathCmd>(desc->commands, desc->command_count, to_cpp),
                            to_cpp(desc->svg_path_d), to_cpp(desc->offset), desc->scale,
                            to_cpp(desc->fill), to_cpp(desc->fill_end), to_cpp(desc->gradient_dir),
                            desc->radial_gradient, to_cpp(desc->stroke_color), desc->stroke_width,
                            desc->foreground});
}
void etch_draw_area_series(const EtchAreaSeriesDesc* desc) {
    if (!desc) return;
    etch::draw_area_series({convert_frame_span<etch::Vec2, EtchVec2>(desc->points, desc->point_count, to_cpp_vec2_ref),
                            desc->baseline_y, to_cpp(desc->fill_top), to_cpp(desc->fill_bottom),
                            to_cpp(desc->line_color), desc->line_thickness, desc->smooth, desc->foreground});
}
void etch_draw_polyline(const EtchPolylineDesc* desc) {
    if (!desc) return;
    etch::draw_polyline({convert_frame_span<etch::Vec2, EtchVec2>(desc->points, desc->point_count, to_cpp_vec2_ref),
                         to_cpp(desc->color), desc->thickness, to_cpp(desc->glow_color),
                         desc->glow_px, desc->closed, desc->foreground});
}
void etch_draw_bezier(const EtchBezierDesc* desc) {
    if (!desc) return;
    etch::draw_bezier({to_cpp(desc->p0), to_cpp(desc->p1), to_cpp(desc->p2), to_cpp(desc->p3),
                       desc->cubic, to_cpp(desc->color_start), to_cpp(desc->color_end),
                       desc->thickness, to_cpp(desc->glow_color), desc->glow_px, desc->foreground});
}
void etch_draw_ring(const EtchRingDesc* desc) {
    if (!desc) return;
    etch::draw_ring({to_cpp(desc->center), desc->radius, desc->thickness, desc->start_angle_rad,
                     desc->sweep_angle_rad, to_cpp(desc->color), to_cpp(desc->color_end),
                     to_cpp(desc->track_color), to_cpp(desc->glow_color), desc->glow_px,
                     desc->round_caps, desc->foreground});
}
void etch_draw_icon(const EtchIconDrawDesc* desc) {
    if (!desc) return;
    etch::draw_icon({desc->icon_id, to_cpp(desc->rect), to_cpp(desc->color), desc->foreground, desc->angle_rad});
}
EtchFontMetrics etch_font_metrics(uint16_t font_id,float size) {
    const auto metrics=etch::font_metrics(font_id,size);
    return {metrics.ascent,metrics.descent,metrics.line_height};
}
EtchVec2 etch_measure_text(EtchStr text, uint16_t font_id, float size, float letter_spacing) {
    return from_cpp(etch::measure_text(to_cpp(text), font_id, size, letter_spacing));
}
struct CLayoutDraw { EtchLayoutDrawFn callback; void* data; };
void etch_draw_after_layout(EtchLayoutDrawFn callback, void* user_data, bool foreground) {
    auto* ctx=etch::current_context();
    if (!ctx || !ctx->in_frame || !callback || ctx->layout_draw_target) return;
    auto* call=ctx->frame_arena.alloc<CLayoutDraw>();
    if (!call) return;
    *call={callback,user_data};
    etch::draw_after_layout([](etch::Rect bounds,void* data) noexcept {
        const auto& c=*static_cast<const CLayoutDraw*>(data);c.callback(from_cpp(bounds),c.data);
    },call,foreground);
}
void etch_draw_text(const EtchTextDrawDesc* desc) {
    if (!desc) return;
    etch::draw_text({to_cpp(desc->text), to_cpp(desc->pos), desc->font_id, desc->size,
                     to_cpp(desc->color), desc->letter_spacing,
                     static_cast<etch::AlignX>(desc->align_x),
                     static_cast<etch::AlignY>(desc->align_y), desc->foreground, desc->max_width_px, desc->ellipsis,
                     desc->has_wipe, to_cpp(desc->color_end), desc->wipe_x, desc->wipe_feather_px,
                     to_cpp(desc->shadow_color), to_cpp(desc->shadow_offset), desc->shadow_blur});
}
void etch_draw_image(const EtchImageDrawDesc* desc) {
    if (!desc) return;
    etch::draw_image({desc->texture, desc->sprite_id, to_cpp(desc->rect), to_cpp(desc->uv_rect),
                      to_cpp(desc->tint), desc->radius, desc->foreground, desc->continuous_corners});
}

/* ============================================================================
 * Dynamic Sprites & External GPU Textures
 * ============================================================================ */

uint32_t etch_create_sprite_rgba(EtchContext* ctx, const EtchTextureDesc* desc) {
    if (!desc) return ETCH_INVALID_SPRITE;
    return etch::create_sprite_rgba(to_ctx(ctx), {desc->rgba_pixels, desc->width, desc->height,
                                                  desc->row_pitch_bytes, desc->srgb,
                                                  desc->debug_name, desc->premultiplied_alpha});
}
bool etch_update_sprite_rgba(EtchContext* ctx, uint32_t sprite_id, uint32_t width, uint32_t height, const void* rgba_pixels, uint32_t row_pitch_bytes) {
    return etch::update_sprite_rgba(to_ctx(ctx), sprite_id, width, height, rgba_pixels, row_pitch_bytes);
}
uint32_t etch_load_sprite_file(EtchContext* ctx, const char* path, bool srgb) { return etch::load_sprite_file(to_ctx(ctx), path, srgb); }
uint32_t etch_load_sprite_memory(EtchContext* ctx, const void* data, size_t size_bytes, bool srgb) { return etch::load_sprite_memory(to_ctx(ctx), data, size_bytes, srgb); }
void etch_destroy_sprite(EtchContext* ctx, uint32_t sprite_id) { etch::destroy_sprite(to_ctx(ctx), sprite_id); }
EtchVec2 etch_sprite_size(const EtchContext* ctx, uint32_t sprite_id) { return from_cpp(etch::sprite_size(to_ctx(ctx), sprite_id)); }
uint32_t etch_sprite_count(const EtchContext* ctx) { return etch::sprite_count(to_ctx(ctx)); }

EtchLoadedTexture etch_create_texture_rgba(EtchContext* ctx, const EtchTextureDesc* desc) {
    if (!desc) return {};
    const etch::LoadedTexture t = etch::create_texture_rgba(to_ctx(ctx), {
        desc->rgba_pixels, desc->width, desc->height, desc->row_pitch_bytes, desc->srgb,
        desc->debug_name, desc->premultiplied_alpha});
    return {t.texture, t.width, t.height};
}
bool etch_flush_texture_uploads(EtchContext* ctx) {return etch::flush_texture_uploads(reinterpret_cast<etch::Context*>(ctx));}
bool etch_update_texture_region_rgba(EtchContext* ctx, struct SDL_GPUTexture* texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* rgba_pixels, uint32_t row_pitch_bytes) {
    return etch::update_texture_region_rgba(to_ctx(ctx),texture,x,y,width,height,rgba_pixels,row_pitch_bytes);
}
bool etch_update_texture_rgba(EtchContext* ctx, struct SDL_GPUTexture* texture, uint32_t width, uint32_t height, const void* rgba_pixels, uint32_t row_pitch_bytes) {
    return etch::update_texture_rgba(to_ctx(ctx), texture, width, height, rgba_pixels, row_pitch_bytes);
}
EtchLoadedTexture etch_load_texture_file(EtchContext* ctx, const char* path, bool srgb) {
    const etch::LoadedTexture t = etch::load_texture_file(to_ctx(ctx), path, srgb);
    return {t.texture, t.width, t.height};
}
EtchLoadedTexture etch_load_texture_memory(EtchContext* ctx, const void* data, size_t size_bytes, bool srgb) {
    const etch::LoadedTexture t = etch::load_texture_memory(to_ctx(ctx), data, size_bytes, srgb);
    return {t.texture, t.width, t.height};
}
void etch_destroy_loaded_texture(EtchContext* ctx, EtchLoadedTexture* tex) {
    if (!tex) return;
    etch::LoadedTexture t = {tex->texture, tex->width, tex->height};
    etch::destroy_texture(to_ctx(ctx), &t);
    *tex = {t.texture, t.width, t.height};
}
void etch_destroy_gpu_texture(EtchContext* ctx, struct SDL_GPUTexture* tex) { etch::destroy_texture(to_ctx(ctx), tex); }

/* ============================================================================
 * Text Editing, Selection, Clipboard, IME & Code Editor
 * ============================================================================ */

EtchTextInputResult etch_text_input(const EtchTextInputDesc* desc) {
    if (!desc) return {};
    const etch::TextInputResult r = etch::text_input_impl({
        desc->id, desc->buffer, desc->capacity, to_cpp(desc->placeholder), to_cpp(desc->width),
        to_cpp(desc->height), to_cpp(desc->padding), desc->font_size_px, desc->font_id,
        desc->letter_spacing, to_cpp(desc->text_color), to_cpp(desc->placeholder_color),
        to_cpp(desc->selection_color), to_cpp(desc->caret_color), to_cpp(desc->composition_color),
        to_cpp(desc->background), to_cpp(desc->border_color), to_cpp(desc->focus_border_color),
        desc->border_width, desc->focus_border_width, to_cpp(desc->radius), desc->multiline,
        desc->word_wrap, desc->read_only, desc->password, desc->context_menu, desc->clear_button, desc->escape_blurs, desc->suppress_popup_keys,
        {}, to_cpp(desc->ghost_text), to_cpp(desc->ghost_color), desc->accept_ghost_on_tab, desc->submit_on_enter,
    }, desc->highlights, desc->highlights ? desc->highlight_count : 0u,
        [](const void* ranges, size_t i) noexcept {
            const auto& h = static_cast<const EtchTextInputHighlight*>(ranges)[i];
            return etch::TextInputHighlight{h.byte_offset, h.byte_length, to_cpp(h.text_color),
                                            to_cpp(h.background), to_cpp(h.border_color), h.radius_px};
        });
    return {r.changed, r.submitted, r.focused, r.composing, r.byte_length,
            r.cursor_byte, r.selection_anchor_byte, from_cpp(r.ime_cursor_rect), r.ghost_accepted};
}
void etch_set_text_selection(uint32_t element_id, uint32_t anchor_byte, uint32_t cursor_byte) {
    etch::set_text_selection(element_id, anchor_byte, cursor_byte);
}
bool etch_has_text_selection(uint32_t element_id) { return etch::has_text_selection(element_id); }
EtchStr etch_selected_text(uint32_t element_id, EtchArena* arena) {
    if (!arena) return from_cpp(etch::selected_text(element_id, nullptr));
    etch::Arena a = to_cpp_arena(arena);
    const etch::Str out = etch::selected_text(element_id, &a);
    sync_c_arena(arena, a);
    return from_cpp(out);
}
void etch_clear_text_selection(uint32_t element_id) { etch::clear_text_selection(element_id); }
EtchRect etch_ime_cursor_rect(const EtchContext* ctx) { return from_cpp(etch::ime_cursor_rect(to_ctx(ctx))); }
void etch_request_text_input(uint32_t element_id, EtchRect ime_cursor_rect) { etch::request_text_input(element_id, to_cpp(ime_cursor_rect)); }
EtchStr etch_typed_text(uint32_t element_id) { return from_cpp(etch::typed_text(element_id)); }
bool etch_set_clipboard_text(const char* utf8_text) { return etch::set_clipboard_text(utf8_text); }
EtchStr etch_get_clipboard_text(EtchArena* arena) {
    if (!arena) return from_cpp(etch::get_clipboard_text(nullptr));
    etch::Arena a = to_cpp_arena(arena);
    const etch::Str out = etch::get_clipboard_text(&a);
    sync_c_arena(arena, a);
    return from_cpp(out);
}
EtchClipboardImageData etch_get_clipboard_image(EtchArena* arena) {
    if (!arena) {
        const etch::ClipboardImageData d = etch::get_clipboard_image(nullptr);
        return {d.bytes, d.size_bytes, from_cpp(d.mime_type)};
    }
    etch::Arena a = to_cpp_arena(arena);
    const etch::ClipboardImageData d = etch::get_clipboard_image(&a);
    sync_c_arena(arena, a);
    return {d.bytes, d.size_bytes, from_cpp(d.mime_type)};
}

bool etch_open_folder_dialog(EtchContext* ctx, struct SDL_Window* parent_window, const char* default_location, uint32_t request_tag, bool allow_many) {
    return etch::open_folder_dialog(to_ctx(ctx), parent_window, default_location, request_tag, allow_many);
}

bool etch_open_file_dialog(EtchContext* ctx, struct SDL_Window* parent_window, const EtchFileDialogFilter* filters, uint32_t filter_count, const char* default_location, uint32_t request_tag, bool allow_many) {
    etch::Context* c = to_ctx(ctx);
    c = c ? c : etch::current_context();
    if (!c) return false;
    etch::FileDialogFilter stack_filters[16];
    etch::FileDialogFilter* heap_filters = nullptr;
    etch::FileDialogFilter* dst = stack_filters;
    const uint32_t n = (filters && filter_count > 0u) ? filter_count : 0u;
    if (n > 16u) {
        heap_filters = static_cast<etch::FileDialogFilter*>(SDL_calloc(n, sizeof(etch::FileDialogFilter)));
        if (!heap_filters) return false;
        dst = heap_filters;
    }
    for (uint32_t i = 0; i < n; ++i) {
        dst[i] = {filters[i].name, filters[i].pattern};
    }
    const bool ok = etch::open_file_dialog(c, parent_window, std::span<const etch::FileDialogFilter>(dst, n), default_location, request_tag, allow_many);
    SDL_free(heap_filters);
    return ok;
}

bool etch_open_save_file_dialog(EtchContext* ctx, struct SDL_Window* parent_window, const EtchFileDialogFilter* filters, uint32_t filter_count, const char* default_location, uint32_t request_tag) {
    etch::Context* c = to_ctx(ctx);
    c = c ? c : etch::current_context();
    if (!c) return false;
    etch::FileDialogFilter stack_filters[16];
    etch::FileDialogFilter* heap_filters = nullptr;
    etch::FileDialogFilter* dst = stack_filters;
    const uint32_t n = (filters && filter_count > 0u) ? filter_count : 0u;
    if (n > 16u) {
        heap_filters = static_cast<etch::FileDialogFilter*>(SDL_calloc(n, sizeof(etch::FileDialogFilter)));
        if (!heap_filters) return false;
        dst = heap_filters;
    }
    for (uint32_t i = 0; i < n; ++i) {
        dst[i] = {filters[i].name, filters[i].pattern};
    }
    const bool ok = etch::open_save_file_dialog(c, parent_window, std::span<const etch::FileDialogFilter>(dst, n), default_location, request_tag);
    SDL_free(heap_filters);
    return ok;
}

EtchFileDialogResult etch_poll_file_dialog(EtchContext* ctx, EtchArena* arena) {
    etch::Context* c = to_ctx(ctx);
    c = c ? c : etch::current_context();
    if (!c) return {false, false, false, 0u, -1, nullptr, 0u};
    etch::Arena a = arena ? to_cpp_arena(arena) : c->frame_arena;
    const etch::FileDialogResult r = etch::poll_file_dialog(c, &a);
    EtchStr* c_paths = nullptr;
    const uint32_t cnt = static_cast<uint32_t>(r.paths.size());
    if (cnt > 0u) {
        c_paths = a.alloc<EtchStr>(cnt);
        if (c_paths) {
            for (uint32_t i = 0; i < cnt; ++i) {
                c_paths[i] = from_cpp(r.paths[i]);
            }
        }
    }
    if (arena) sync_c_arena(arena, a);
    else c->frame_arena = a;
    return {r.ready, r.cancelled, r.error, r.request_tag, r.selected_filter, c_paths, c_paths ? cnt : 0u};
}

EtchTextBuffer* etch_create_text_buffer(EtchStr initial_utf8, uint32_t initial_cap) {
    auto* tb = static_cast<etch::TextBuffer*>(std::calloc(1u, sizeof(etch::TextBuffer)));
    if (!tb) return nullptr;
    if (!etch::init_text_buffer(tb, to_cpp(initial_utf8), initial_cap ? initial_cap : 4096u)) {
        std::free(tb);
        return nullptr;
    }
    return from_tb(tb);
}
void etch_destroy_text_buffer(EtchTextBuffer* buf) {
    if (!buf) return;
    etch::TextBuffer* tb = to_tb(buf);
    etch::free_text_buffer(tb);
    std::free(tb);
}
bool etch_text_buffer_set(EtchTextBuffer* buf, EtchStr utf8) { return etch::text_buffer_set(to_tb(buf), to_cpp(utf8)); }
bool etch_text_buffer_replace(EtchTextBuffer* buf, uint32_t byte_start, uint32_t byte_end, EtchStr utf8,
    bool record_undo, uint32_t before_cursor, uint32_t before_anchor,
    uint32_t after_cursor, uint32_t after_anchor, uint8_t op_group, double timestamp_s)
{
    return etch::text_buffer_replace(to_tb(buf), byte_start, byte_end, to_cpp(utf8), record_undo,
                                     before_cursor, before_anchor, after_cursor, after_anchor,
                                     op_group, timestamp_s);
}
uint32_t etch_text_buffer_byte_length(const EtchTextBuffer* buf) { return etch::text_buffer_byte_length(to_tb(buf)); }
uint32_t etch_text_buffer_line_count(const EtchTextBuffer* buf) { return etch::text_buffer_line_count(to_tb(buf)); }
uint32_t etch_text_buffer_line_start(const EtchTextBuffer* buf, uint32_t line) { return etch::text_buffer_line_start(to_tb(buf), line); }
uint32_t etch_text_buffer_line_end(const EtchTextBuffer* buf, uint32_t line) { return etch::text_buffer_line_end(to_tb(buf), line); }
EtchStr etch_text_buffer_line(const EtchTextBuffer* buf, uint32_t line, EtchArena* scratch) {
    if (!scratch) return from_cpp(etch::text_buffer_line(to_tb(buf), line, nullptr));
    etch::Arena a = to_cpp_arena(scratch);
    const etch::Str out = etch::text_buffer_line(to_tb(buf), line, &a);
    sync_c_arena(scratch, a);
    return from_cpp(out);
}
uint32_t etch_text_buffer_copy_range(const EtchTextBuffer* buf, uint32_t byte_start, uint32_t byte_end, char* out_buf, uint32_t cap) {
    return etch::text_buffer_copy_range(to_tb(buf), byte_start, byte_end, out_buf, cap);
}
EtchTextLineCol etch_text_buffer_line_col(const EtchTextBuffer* buf, uint32_t byte_offset) {
    const etch::TextLineCol lc = etch::text_buffer_line_col(to_tb(buf), byte_offset);
    return {lc.line, lc.col_byte};
}
uint32_t etch_text_buffer_byte_offset(const EtchTextBuffer* buf, uint32_t line, uint32_t col_byte) {
    return etch::text_buffer_byte_offset(to_tb(buf), line, col_byte);
}
bool etch_text_buffer_undo(EtchTextBuffer* buf, uint32_t* out_cursor, uint32_t* out_anchor) {
    return etch::text_buffer_undo(to_tb(buf), out_cursor, out_anchor);
}
bool etch_text_buffer_redo(EtchTextBuffer* buf, uint32_t* out_cursor, uint32_t* out_anchor) {
    return etch::text_buffer_redo(to_tb(buf), out_cursor, out_anchor);
}
void etch_text_buffer_clear_undo(EtchTextBuffer* buf) { etch::text_buffer_clear_undo(to_tb(buf)); }

uint32_t etch_tokenize_cpp_hlsl(uint32_t line_index, EtchStr line, EtchSyntaxToken* out_tokens, uint32_t max_tokens, void* user_data) {
    (void)line_index;(void)user_data;
    if (!out_tokens || max_tokens == 0u) return 0u;
    return etch::tokenize_cpp_hlsl_emit(to_cpp(line),out_tokens,max_tokens,etch::active_theme(etch::current_context()),
        [](void* ptr,uint32_t i,uint32_t a,uint32_t z,etch::Color color) noexcept {
            static_cast<EtchSyntaxToken*>(ptr)[i]={a,z,from_cpp(color),etch::kFontMono};
        });
}

EtchCodeEditorResult etch_code_editor(const EtchCodeEditorDesc* desc) {
    if (!desc) return {};
    etch::SyntaxTokenizeFn tok_fn = nullptr;
    void* tok_ud = nullptr;
    if (desc->tokenizer == etch_tokenize_cpp_hlsl) {
        tok_fn = etch::tokenize_cpp_hlsl;
        tok_ud = desc->tokenizer_user_data;
    } else if (desc->tokenizer) {
        etch::Context* ctx = etch::current_context();
        CTokenizerThunk* thunk = (ctx && ctx->in_frame) ? ctx->frame_arena.alloc<CTokenizerThunk>() : nullptr;
        if (thunk) {
            thunk->fn = desc->tokenizer;
            thunk->user_data = desc->tokenizer_user_data;
            tok_fn = c_syntax_tokenizer_trampoline;
            tok_ud = thunk;
        }
    }
    etch::CodeLineDecorateFn dec_fn = nullptr;
    void* dec_ud = nullptr;
    if (desc->line_decorator) {
        etch::Context* ctx = etch::current_context();
        CCodeLineDecorateThunk* dthunk = (ctx && ctx->in_frame) ? ctx->frame_arena.alloc<CCodeLineDecorateThunk>() : nullptr;
        if (dthunk) {
            dthunk->fn = desc->line_decorator;
            dthunk->user_data = desc->line_decorator_user_data;
            dec_fn = c_code_line_decorate_trampoline;
            dec_ud = dthunk;
        }
    }
    const etch::CodeEditorResult r = etch::code_editor({
        desc->id, to_tb(desc->buffer), to_cpp(desc->width), to_cpp(desc->height),
        to_cpp(desc->padding), desc->font_size_px, desc->font_id, desc->line_height_px,
        desc->show_line_numbers, desc->gutter_width_px, desc->tab_width, desc->read_only,
        desc->show_scrollbar, desc->word_wrap, tok_fn, tok_ud, to_cpp(desc->background),
        to_cpp(desc->gutter_background), to_cpp(desc->active_line_color), to_cpp(desc->selection_color),
        to_cpp(desc->text_color), to_cpp(desc->gutter_text_color),
        to_cpp(desc->gutter_active_text_color), to_cpp(desc->caret_color),
        to_cpp(desc->composition_color), to_cpp(desc->border_color),
        to_cpp(desc->focus_border_color), desc->border_width, desc->focus_border_width,
        to_cpp(desc->radius), desc->scroll_fade_px, dec_fn, dec_ud, desc->gutter_tag_width_px});
    return {r.changed, r.focused, r.composing, r.cursor_byte, r.selection_anchor_byte,
            r.cursor_line, r.cursor_col_byte, r.first_visible_line, r.end_visible_line,
            r.total_lines, from_cpp(r.ime_cursor_rect), r.hovered_line, r.clicked_gutter_line};
}
void etch_code_editor_set_cursor(uint32_t element_id, uint32_t anchor_byte, uint32_t cursor_byte, bool scroll_into_view) {
    etch::code_editor_set_cursor(element_id, anchor_byte, cursor_byte, scroll_into_view);
}
void etch_code_editor_scroll_to_line(uint32_t element_id, uint32_t line_index, EtchAlignY align) {
    etch::code_editor_scroll_to_line(element_id, line_index, static_cast<etch::AlignY>(align));
}

/* ============================================================================
 * Desktop Widgets
 * ============================================================================ */

bool etch_button(const EtchButtonDesc* desc) {
    if (!desc) return false;
    return etch::button({desc->id, to_cpp(desc->label), desc->icon_id,
                         static_cast<etch::ButtonVariant>(desc->variant), to_cpp(desc->width),
                         to_cpp(desc->height), to_cpp(desc->padding), desc->font_size_px,
                         desc->font_id, to_cpp(desc->radius), desc->disabled, to_cpp(desc->shortcut_hint), static_cast<etch::AlignX>(desc->align_x)});
}
bool etch_checkbox(const EtchCheckboxDesc* desc) {
    if (!desc) return false;
    return etch::checkbox({desc->id, to_cpp(desc->label), desc->checked, desc->indeterminate,
                           desc->disabled, desc->box_size_px, desc->font_size_px, desc->font_id,
                           to_cpp(desc->accent)});
}
bool etch_toggle_switch(const EtchToggleSwitchDesc* desc) {
    if (!desc) return false;
    return etch::toggle_switch({desc->id, to_cpp(desc->label), desc->value, desc->disabled,
                                desc->track_width_px, desc->track_height_px, desc->font_size_px,
                                desc->font_id, to_cpp(desc->on_color), to_cpp(desc->off_color)});
}
void etch_progress_bar(const EtchProgressBarDesc* desc) {
    if (!desc) return;
    etch::progress_bar({desc->id, desc->fraction, to_cpp(desc->overlay_text), to_cpp(desc->width),
                        to_cpp(desc->height), to_cpp(desc->radius), to_cpp(desc->track_color),
                        to_cpp(desc->fill_color), to_cpp(desc->text_color), desc->font_size_px});
}
bool etch_begin_tab_bar(const EtchTabBarDesc* desc) {
    if (!desc) return false;
    return etch::begin_tab_bar({desc->id, desc->active_tab, to_cpp(desc->width), desc->height_px,
                                desc->font_size_px, to_cpp(desc->background), to_cpp(desc->active_bg),
                                to_cpp(desc->hover_bg), to_cpp(desc->accent),
                                to_cpp(desc->text_color), to_cpp(desc->active_text_color)});
}
EtchTabItemResult etch_tab_item(const EtchTabItemDesc* desc) {
    if (!desc) return {};
    const etch::TabItemResult r = etch::tab_item({desc->id, to_cpp(desc->label), desc->icon_id, desc->closable, desc->disabled});
    return {r.selected, r.clicked, r.closed};
}
void etch_end_tab_bar(void) { etch::end_tab_bar(); }
bool etch_slider(const EtchSliderDesc* desc) {
    if (!desc) return false;
    return etch::slider({desc->id, to_cpp(desc->label), desc->value, desc->min_value, desc->max_value,
                         desc->step, desc->format, to_cpp(desc->width), desc->height_px,
                         desc->disabled, to_cpp(desc->background), to_cpp(desc->fill_color),
                         to_cpp(desc->text_color)});
}
bool etch_drag_value(const EtchDragValueDesc* desc) {
    if (!desc) return false;
    return etch::drag_value({desc->id, to_cpp(desc->label), desc->value, desc->min_value, desc->max_value,
                             desc->speed, desc->step, desc->format, to_cpp(desc->width),
                             desc->height_px, desc->disabled, to_cpp(desc->background),
                             to_cpp(desc->fill_color), to_cpp(desc->text_color)});
}
void etch_set_tooltip(uint32_t target_id, const EtchTooltipDesc* desc) {
    if (!desc) return;
    etch::set_tooltip(target_id, {to_cpp(desc->text), desc->delay_seconds, desc->max_width_px,
                                  desc->font_size_px, desc->z_index});
}
void etch_set_tooltip_text(uint32_t target_id, EtchStr text) { etch::set_tooltip(target_id, to_cpp(text)); }
bool etch_begin_modal(const EtchModalDesc* desc) {
    if (!desc) return false;
    return etch::begin_modal({desc->id, desc->open, to_cpp(desc->title), desc->width_px, to_cpp(desc->height),
                              desc->dismiss_on_backdrop, desc->dismiss_on_escape, to_cpp(desc->backdrop_color),
                              to_cpp(desc->background), to_cpp(desc->border_color), to_cpp(desc->radius),
                              desc->z_index});
}
void etch_end_modal(void) { etch::end_modal(); }
bool etch_combo_box(const EtchComboBoxDesc* desc) {
    if (!desc) return false;
    return etch::combo_box({desc->id, to_cpp(desc->label), desc->selected_index,
                            convert_frame_span<etch::Str, EtchStr>(desc->items, desc->item_count, to_cpp_str_ref),
                            to_cpp(desc->width), desc->height_px, desc->max_popup_height_px,
                            desc->disabled, to_cpp(desc->background), to_cpp(desc->hover_bg),
                            to_cpp(desc->text_color)});
}
void etch_open_popup(uint32_t popup_id, EtchVec2 anchor_pos) { etch::open_popup(popup_id, to_cpp(anchor_pos)); }
void etch_close_popup(uint32_t popup_id) { etch::close_popup(popup_id); }
bool etch_is_popup_open(uint32_t popup_id) { return etch::is_popup_open(popup_id); }
bool etch_begin_popup(const EtchPopupDesc* desc) {
    if (!desc) return false;
    return etch::begin_popup({desc->id, desc->trigger_id, to_cpp(desc->width), to_cpp(desc->height),
                              to_cpp(desc->padding), desc->gap, to_cpp(desc->background),
                              to_cpp(desc->border_color), to_cpp(desc->radius), desc->z_index,desc->attach_to_trigger,desc->match_trigger_width,desc->scroll_y});
}
void etch_end_popup(void) { etch::end_popup(); }
bool etch_begin_context_menu(uint32_t popup_id, uint32_t target_id) { return etch::begin_context_menu(popup_id, target_id); }
void etch_end_context_menu(void) { etch::end_context_menu(); }
bool etch_begin_menu_bar(const EtchMenuBarDesc* desc) {
    if (!desc) return false;
    return etch::begin_menu_bar({desc->id, to_cpp(desc->width), desc->height_px, to_cpp(desc->background)});
}
void etch_end_menu_bar(void) { etch::end_menu_bar(); }
bool etch_begin_menu(const EtchMenuDesc* desc) {
    if (!desc) return false;
    return etch::begin_menu({desc->id, to_cpp(desc->label), desc->disabled});
}
void etch_end_menu(void) { etch::end_menu(); }
bool etch_menu_item(const EtchMenuItemDesc* desc) {
    if (!desc) return false;
    return etch::menu_item({desc->id, to_cpp(desc->label), to_cpp(desc->shortcut_hint),
                            desc->icon_id, desc->checked, desc->disabled, desc->separator_after});
}
void etch_menu_separator(void) { etch::menu_separator(); }

/* ============================================================================
 * Interaction, Input, OS Drops, Drag Payload, Springs & Containers
 * ============================================================================ */

bool etch_hovered(uint32_t element_id) { return etch::hovered(element_id); }
bool etch_pressed(uint32_t element_id) { return etch::pressed(element_id); }
bool etch_clicked(uint32_t element_id) { return etch::clicked(element_id); }
bool etch_active(uint32_t element_id) { return etch::active(element_id); }
uint32_t etch_active_id(void) { return etch::active_id(); }
void etch_set_active(uint32_t element_id) { etch::set_active(element_id); }
bool etch_focused(uint32_t element_id) { return etch::focused(element_id); }
uint32_t etch_focused_id(void) { return etch::focused_id(); }
void etch_request_focus(uint32_t element_id) { etch::request_focus(element_id); }
void etch_set_cursor(EtchCursor cursor) { etch::set_cursor(static_cast<etch::Cursor>(cursor)); }
bool etch_element_geometry(uint32_t element_id, EtchElementGeometry* out_geometry) {
    if (!out_geometry) return false;
    etch::ElementGeometry g;
    if (!etch::element_geometry(element_id,&g)) return false;
    *out_geometry = {from_cpp(g.bounds),from_cpp(g.visible_bounds),from_cpp(g.local_scale),g.opacity};
    return true;
}
bool etch_screen_to_element_local(uint32_t element_id, EtchVec2 screen_pos, EtchVec2* out_local) {
    if (!out_local) return false;
    etch::Vec2 p;
    if (!etch::screen_to_element_local(element_id,to_cpp(screen_pos),&p)) return false;
    *out_local = from_cpp(p);
    return true;
}
EtchRect etch_element_bounds(uint32_t element_id) { return from_cpp(etch::element_bounds(element_id)); }

void etch_push_id(uint32_t scope_id) { etch::push_id(scope_id); }
void etch_push_id_str(EtchStr label) { etch::push_id(to_cpp(label)); }
void etch_pop_id(void) { etch::pop_id(); }
uint32_t etch_scoped_id(uint32_t local_id) { return etch::scoped_id(local_id); }
uint32_t etch_scoped_id_str(EtchStr label) { return etch::scoped_id(to_cpp(label)); }

uint16_t etch_key_mods(void) { return etch::key_mods(); }
bool etch_key_pressed(uint32_t keycode, bool include_repeat) { return etch::key_pressed(keycode, include_repeat); }
bool etch_consume_key_pressed(uint32_t keycode, bool include_repeat) { return etch::consume_key_pressed(keycode, include_repeat); }
bool etch_shortcut(uint16_t required_mods, uint32_t keycode, bool allow_when_typing) {
    return etch::shortcut(required_mods, keycode, allow_when_typing);
}
bool etch_consume_shortcut(uint16_t required_mods, uint32_t keycode, bool allow_when_typing) {
    return etch::consume_shortcut(required_mods, keycode, allow_when_typing);
}

EtchVec2 etch_pointer_pos(void) { return from_cpp(etch::pointer_pos()); }
EtchVec2 etch_pointer_delta(void) { return from_cpp(etch::pointer_delta()); }
bool etch_right_clicked(uint32_t element_id) { return etch::right_clicked(element_id); }
bool etch_right_clicked_anywhere(void) { return etch::right_clicked_anywhere(); }
bool etch_double_clicked(uint32_t element_id) { return etch::double_clicked(element_id); }
bool etch_dragging(uint32_t element_id, float threshold_px) { return etch::dragging(element_id, threshold_px); }
bool etch_should_close_popover(uint32_t popover_id, uint32_t trigger_id) { return etch::should_close_popover(popover_id, trigger_id); }

uint32_t etch_dropped_file_count(void) { return static_cast<uint32_t>(etch::dropped_files().size()); }
EtchStr etch_dropped_file_at(uint32_t index) {
    const auto s = etch::dropped_files();
    return (index < s.size()) ? from_cpp(s[index]) : EtchStr{};
}
bool etch_is_os_dragging(void) { return etch::is_os_dragging(); }
EtchVec2 etch_os_drop_pos(void) { return from_cpp(etch::os_drop_pos()); }
bool etch_os_drag_hovering(uint32_t element_id) { return etch::os_drag_hovering(element_id); }
uint32_t etch_dropped_file_on_count(uint32_t element_id) { return static_cast<uint32_t>(etch::dropped_files_on(element_id).size()); }
EtchStr etch_dropped_file_on_at(uint32_t element_id, uint32_t index) {
    const auto s = etch::dropped_files_on(element_id);
    return (index < s.size()) ? from_cpp(s[index]) : EtchStr{};
}
uint32_t etch_dropped_text_count(void) { return static_cast<uint32_t>(etch::dropped_texts().size()); }
EtchStr etch_dropped_text_at(uint32_t index) {
    const auto s = etch::dropped_texts();
    return (index < s.size()) ? from_cpp(s[index]) : EtchStr{};
}
uint32_t etch_dropped_text_on_count(uint32_t element_id) { return static_cast<uint32_t>(etch::dropped_texts_on(element_id).size()); }
EtchStr etch_dropped_text_on_at(uint32_t element_id, uint32_t index) {
    const auto s = etch::dropped_texts_on(element_id);
    return (index < s.size()) ? from_cpp(s[index]) : EtchStr{};
}
void etch_begin_focus_scope(uint32_t scope_id, bool trap_tab) { etch::begin_focus_scope(scope_id, trap_tab); }
void etch_end_focus_scope(void) { etch::end_focus_scope(); }
bool etch_keyboard_focus_visible(uint32_t element_id) { return etch::keyboard_focus_visible(element_id); }

bool etch_begin_drag_payload(uint32_t source_id, uint32_t kind_hash, EtchStr label, const void* data, uint32_t size, uint64_t u64, uint32_t u32) {
    return etch::begin_drag_payload(source_id, kind_hash, to_cpp(label), data, size, u64, u32);
}
bool etch_is_dragging_payload(uint32_t kind_hash) { return etch::is_dragging_payload(kind_hash); }
bool etch_peek_drag_payload(EtchDragPayloadView* out_view, uint32_t kind_hash) {
    if (!out_view) return etch::peek_drag_payload(nullptr, kind_hash);
    etch::DragPayloadView v{};
    if (!etch::peek_drag_payload(&v, kind_hash)) return false;
    *out_view = {v.kind_hash, from_cpp(v.label), v.u64, v.u32, v.data, v.size};
    return true;
}
bool etch_accept_drag_payload(uint32_t target_id, uint32_t kind_hash, EtchDragPayloadView* out_view) {
    if (!out_view) return etch::accept_drag_payload(target_id, kind_hash, nullptr);
    etch::DragPayloadView v{};
    if (!etch::accept_drag_payload(target_id, kind_hash, &v)) return false;
    *out_view = {v.kind_hash, from_cpp(v.label), v.u64, v.u32, v.data, v.size};
    return true;
}

struct SDL_Window* etch_create_desktop_window(const char* title, float w, float h, uint64_t flags) { return etch::create_desktop_window(title, w, h, flags); }
struct SDL_Window* etch_create_desktop_window_on_display(const char* title,float w,float h,uint32_t display,uint64_t flags) {return etch::create_desktop_window_on_display(title,w,h,display,flags);}
struct SDL_Window* etch_create_desktop_window_at_pointer(const char* title,float w,float h,uint64_t flags) {return etch::create_desktop_window_at_pointer(title,w,h,flags);}
bool etch_set_desktop_window_min_size(struct SDL_Window* window,float w,float h) {return etch::set_desktop_window_min_size(window,w,h);}
float etch_spring_from(uint32_t anim_id, float target, float initial_value, EtchSpringSpec spec) { return etch::spring(anim_id, target, initial_value, to_cpp(spec)); }
float etch_spring(uint32_t anim_id, float target, EtchSpringSpec spec) { return etch::spring(anim_id, target, to_cpp(spec)); }
EtchVec2 etch_spring_vec2(uint32_t anim_id, EtchVec2 target, EtchSpringSpec spec) { return from_cpp(etch::spring_vec2(anim_id, to_cpp(target), to_cpp(spec))); }
EtchVec2 etch_scroll_delta(void) { return from_cpp(etch::scroll_delta()); }
EtchVec2 etch_scroll_max(uint32_t element_id) { return from_cpp(etch::scroll_max(element_id)); }
EtchVec2 etch_scroll_offset(uint32_t element_id) { return from_cpp(etch::scroll_offset(element_id)); }
void etch_set_scroll_offset(uint32_t element_id, EtchVec2 offset, bool stop_momentum) { etch::set_scroll_offset(element_id, to_cpp(offset), stop_momentum); }

EtchSplitResult etch_begin_split(const EtchSplitDesc* desc) {
    if (!desc) return {};
    const etch::SplitResult r = etch::begin_split({
        desc->id, static_cast<etch::Axis>(desc->axis), to_cpp(desc->width), to_cpp(desc->height),
        desc->default_ratio, desc->min_first_px, desc->min_second_px, desc->divider_size_px,
        to_cpp(desc->divider_color), to_cpp(desc->divider_hover_color), to_cpp(desc->divider_active_color), desc->divider_hit_px, desc->keyboard_step_px});
    return {r.ratio, r.dragging};
}
void etch_split_second(void) { etch::split_second(); }
void etch_end_split(void) { etch::end_split(); }
float etch_split_ratio(uint32_t split_id) { return etch::split_ratio(split_id); }
void etch_set_split_ratio(uint32_t split_id, float ratio) { etch::set_split_ratio(split_id, ratio); }

EtchVirtualListSlice etch_begin_virtual_list(const EtchVirtualListDesc* desc) {
    if (!desc) return {};
    const etch::VirtualListDesc cpp_desc = {
        desc->id, desc->item_count, desc->item_height_px, desc->row_gap_px, desc->overscan_rows,
        to_cpp(desc->width), to_cpp(desc->height), to_cpp(desc->padding), to_cpp(desc->background),
        to_cpp(desc->border_color), desc->border_width, to_cpp(desc->radius), desc->show_scrollbar,
        nullptr, desc->measure_user_data, desc->variable_height, desc->scroll_fade_px, desc->stick_to_bottom};
    return from_cpp(etch::begin_virtual_list_with_c_measure(cpp_desc, desc->measure_height_fn));
}
void etch_end_virtual_list(void) { etch::end_virtual_list(); }
void etch_virtual_list_set_item_height(uint32_t list_id, uint32_t row_index, float height_px) { etch::virtual_list_set_item_height(list_id, row_index, height_px); }
void etch_virtual_list_invalidate_heights(uint32_t list_id) { etch::virtual_list_invalidate_heights(list_id); }
double etch_virtual_list_scroll_offset(uint32_t list_id) { return etch::virtual_list_scroll_offset(list_id); }
void etch_set_virtual_list_scroll_offset(uint32_t list_id, double offset_y, bool stop_momentum) { etch::set_virtual_list_scroll_offset(list_id, offset_y, stop_momentum); }
void etch_virtual_list_scroll_to_row(uint32_t list_id, uint32_t row_index, float item_height_px, EtchAlignY align) {
    etch::virtual_list_scroll_to_row(list_id, row_index, item_height_px, static_cast<etch::AlignY>(align));
}
bool etch_virtual_list_is_at_bottom(uint32_t list_id) { return etch::virtual_list_is_at_bottom(list_id); }
void etch_virtual_list_scroll_to_bottom(uint32_t list_id, bool stop_momentum) { etch::virtual_list_scroll_to_bottom(list_id, stop_momentum); }

bool etch_dock_space_initialized(uint32_t dock_id) { return etch::dock_space_initialized(dock_id); }
uint32_t etch_dock_reset_root(uint32_t dock_id) { return etch::dock_reset_root(dock_id); }
bool etch_dock_split_node(uint32_t dock_id, uint32_t node_idx, EtchAxis axis, float default_ratio,
    uint32_t* out_first_node, uint32_t* out_second_node, uint32_t split_widget_id,
    float min_first_px, float min_second_px)
{
    return etch::dock_split_node(dock_id, node_idx, static_cast<etch::Axis>(axis), default_ratio,
                                 out_first_node, out_second_node, split_widget_id, min_first_px, min_second_px);
}
bool etch_dock_add_pane(uint32_t dock_id, uint32_t leaf_node_idx, const EtchDockPaneDesc* pane) {
    if (!pane) return false;
    return etch::dock_add_pane(dock_id, leaf_node_idx, {pane->id, to_cpp(pane->title), pane->icon_id, pane->closable, pane->default_open});
}
bool etch_dock_dock_pane(uint32_t dock_id, uint32_t pane_id, uint32_t target_pane_id, EtchDockZone zone, float split_ratio) {
    return etch::dock_dock_pane(dock_id, pane_id, target_pane_id, static_cast<etch::DockZone>(zone), split_ratio);
}
bool etch_dock_reorder_pane(uint32_t dock_id, uint32_t pane_id, uint32_t target_pane_id, bool insert_after) {
    return etch::dock_reorder_pane(dock_id, pane_id, target_pane_id, insert_after);
}
void etch_dock_activate_pane(uint32_t dock_id, uint32_t pane_id) { etch::dock_activate_pane(dock_id, pane_id); }
uint32_t etch_dock_active_pane(uint32_t dock_id, uint32_t leaf_node_idx) { return etch::dock_active_pane(dock_id, leaf_node_idx); }
uint32_t etch_dock_find_pane_leaf(uint32_t dock_id, uint32_t pane_id) { return etch::dock_find_pane_leaf(dock_id, pane_id); }
bool etch_dock_is_pane_open(uint32_t dock_id, uint32_t pane_id) { return etch::dock_is_pane_open(dock_id, pane_id); }
void etch_dock_set_pane_open(uint32_t dock_id, uint32_t pane_id, bool open) { etch::dock_set_pane_open(dock_id, pane_id, open); }
void etch_begin_dock_space(const EtchDockSpaceDesc* desc) {
    if (!desc) return;
    etch::begin_dock_space({
        desc->id, to_cpp(desc->width), to_cpp(desc->height), desc->tab_bar_height_px, desc->divider_size_px,
        to_cpp(desc->background), to_cpp(desc->pane_background), to_cpp(desc->pane_border_color),
        desc->pane_border_width, to_cpp(desc->pane_radius), to_cpp(desc->tab_bar_bg),
        to_cpp(desc->tab_active_bg), to_cpp(desc->tab_hover_bg), to_cpp(desc->tab_accent)});
}
uint32_t etch_next_dock_pane(void) { return etch::next_dock_pane(); }
void etch_end_dock_space(void) { etch::end_dock_space(); }

bool etch_table_row_selected(const EtchTableSelection* s,uint32_t row) {
    return s && etch::table_row_selected({s->focused_row,s->anchor_row,s->selected_words,s->word_count},row);
}
EtchTableResult etch_begin_table(const EtchTableDesc* desc) {
    if (!desc) return {};
    const etch::TableResult r = etch::begin_table({
        desc->id,
        convert_frame_span<etch::TableColumnDesc, EtchTableColumnDesc>(desc->columns, desc->column_count, to_cpp),
        desc->item_count, desc->header_height_px, desc->row_height_px, desc->row_gap_px,
        desc->overscan_rows, desc->default_sort_column, desc->default_sort_descending, desc->cell_padding_x,
        to_cpp(desc->width), to_cpp(desc->height), to_cpp(desc->padding), to_cpp(desc->background),
        to_cpp(desc->header_background), to_cpp(desc->zebra_color), to_cpp(desc->hover_row_color),
        to_cpp(desc->border_color), desc->border_width, to_cpp(desc->radius), desc->show_scrollbar, desc->show_column_lines, desc->header_font_size_px, desc->selected_row, to_cpp(desc->selected_row_color),desc->horizontal_scroll,desc->keyboard_navigation,{desc->selection.focused_row,desc->selection.anchor_row,desc->selection.selected_words,desc->selection.word_count},desc->scroll_fade_px});
    return {from_cpp(r.slice), r.sort_column, r.sort_descending, r.sort_changed};
}
bool etch_table_next_row(uint32_t* out_row_index, EtchColor row_bg_override, EtchColor row_border_override) {
    return etch::table_next_row(out_row_index, to_cpp(row_bg_override), to_cpp(row_border_override));
}
EtchTableRowResult etch_table_row_result(uint32_t table_id, uint32_t row_index) {
    const auto r=etch::table_row_result(table_id,row_index);
    return {r.id,r.hovered,r.pressed,r.clicked,r.double_clicked,r.right_clicked};
}
bool etch_table_next_column(void) { return etch::table_next_column(); }
void etch_end_table(void) { etch::end_table(); }
float etch_table_column_width(uint32_t table_id, uint32_t col_index) { return etch::table_column_width(table_id, col_index); }
void etch_set_table_column_width(uint32_t table_id, uint32_t col_index, float width_px) { etch::set_table_column_width(table_id, col_index, width_px); }
void etch_set_table_sort(uint32_t table_id, uint32_t col_index, bool descending) { etch::set_table_sort(table_id, col_index, descending); }
void etch_table_scroll_to_row(uint32_t table_id, uint32_t row_index, float row_height_px, EtchAlignY align) {
    etch::table_scroll_to_row(table_id, row_index, row_height_px, static_cast<etch::AlignY>(align));
}

EtchTreeNodeResult etch_begin_tree_node(const EtchTreeNodeDesc* desc) {
    if (!desc) return {};
    const etch::TreeNodeResult r = etch::begin_tree_node({
        desc->id, to_cpp(desc->label), desc->icon_id, to_cpp(desc->icon_color), to_cpp(desc->text_color),
        to_cpp(desc->selected_bg), to_cpp(desc->selected_accent), to_cpp(desc->hover_bg),
        desc->row_height_px, desc->font_size_px, desc->indent_px, desc->font_id,
        desc->default_open, desc->selected, desc->leaf, desc->show_indent_guide});
    return {r.open, r.clicked, r.double_clicked, r.toggled, r.hovered};
}
void etch_end_tree_node(void) { etch::end_tree_node(); }
bool etch_is_tree_open(uint32_t node_id) { return etch::is_tree_open(node_id); }
void etch_set_tree_open(uint32_t node_id, bool open) { etch::set_tree_open(node_id, open); }

EtchCanvasState etch_begin_canvas(const EtchCanvasDesc* desc) {
    if (!desc) return {};
    return from_cpp(etch::begin_canvas({
        desc->id, to_cpp(desc->width), to_cpp(desc->height), to_cpp(desc->default_pan), desc->default_zoom,
        desc->min_zoom, desc->max_zoom, desc->grid_step_world, desc->major_grid_subdiv,
        to_cpp(desc->background), to_cpp(desc->minor_grid_color), to_cpp(desc->major_grid_color),
        to_cpp(desc->border_color), desc->border_width, to_cpp(desc->radius), desc->show_grid,
        desc->interactive, desc->marquee_select, to_cpp(desc->marquee_fill), to_cpp(desc->marquee_border)}));
}
void etch_end_canvas(void) { etch::end_canvas(); }
void etch_canvas_wire(const EtchCanvasWireDesc* desc) {
    if (!desc) return;
    etch::canvas_wire({to_cpp(desc->from_world), to_cpp(desc->to_world), to_cpp(desc->color_start),
                       to_cpp(desc->color_end), to_cpp(desc->glow_color), desc->thickness_world,
                       desc->glow_world, desc->tangent_bias, desc->z_index});
}
EtchCanvasNodeResult etch_begin_canvas_node(const EtchCanvasNodeDesc* desc) {
    if (!desc) return {};
    etch::Vec2 wp_tmp = desc->world_pos ? to_cpp(*desc->world_pos) : to_cpp(desc->pos_value);
    const etch::CanvasNodeResult r = etch::begin_canvas_node({
        desc->id, desc->world_pos ? &wp_tmp : nullptr, to_cpp(desc->pos_value),
        to_cpp(desc->width_world), to_cpp(desc->height_world), to_cpp(desc->padding_world),
        desc->gap_world, static_cast<etch::Axis>(desc->axis), to_cpp(desc->background),
        to_cpp(desc->background_end), to_cpp(desc->gradient_dir), to_cpp(desc->border_color),
        desc->border_width_world, to_cpp(desc->radius_world), to_cpp(desc->shadow_color),
        desc->shadow_blur_world, to_cpp(desc->shadow_offset_world), desc->z_index, desc->draggable});
    if (desc->world_pos) *desc->world_pos = from_cpp(wp_tmp);
    return {r.hovered, r.pressed, r.dragging, r.clicked, from_cpp(r.drag_delta_world)};
}
void etch_end_canvas_node(void) { etch::end_canvas_node(); }
EtchCanvasState etch_canvas_state(uint32_t canvas_id) { return from_cpp(etch::canvas_state(canvas_id)); }
void etch_set_canvas_view(uint32_t canvas_id, EtchVec2 pan, float zoom) { etch::set_canvas_view(canvas_id, to_cpp(pan), zoom); }

void etch_set_debug_hud_open(EtchContext* ctx, bool open) { etch::set_debug_hud_open(to_ctx(ctx), open); }
bool etch_debug_hud_open(const EtchContext* ctx) { return etch::debug_hud_open(to_ctx(ctx)); }
void etch_set_debug_wireframe(EtchContext* ctx, bool enabled) { etch::set_debug_wireframe(to_ctx(ctx), enabled); }
bool etch_debug_wireframe(const EtchContext* ctx) { return etch::debug_wireframe(to_ctx(ctx)); }

extern "C" EtchPresenceResult etch_presence(uint32_t id, bool mounted, EtchSpringSpec spec) {
    const auto r=etch::presence(id,mounted,to_cpp(spec)); return {r.visible,r.opacity,r.exiting};
}
extern "C" void etch_set_time_scale(EtchContext* ctx, float scale) { etch::set_time_scale(to_ctx(ctx),scale); }

extern "C" EtchLayerDesc etch_layer_desc_default(void) {
    const etch::LayerDesc d;
    return {d.opacity,from_cpp(d.translate),from_cpp(d.scale),from_cpp(d.pivot_norm),from_cpp(d.pivot_px),d.animating,d.blur_px,d.blend_plus_lighter};
}
extern "C" void etch_push_layer(const EtchLayerDesc* d) {
    etch::push_layer(d ? etch::LayerDesc{d->opacity,to_cpp(d->translate),to_cpp(d->scale),to_cpp(d->pivot_norm),to_cpp(d->pivot_px),d->animating,d->blur_px,d->blend_plus_lighter} : etch::LayerDesc{});
}
extern "C" void etch_pop_layer(void) {etch::pop_layer();}

EtchShaderSurfaceCreateDesc etch_shader_surface_create_desc_default(void) {
    EtchShaderSurfaceCreateDesc d = {};
    d.spirv.entrypoint = d.dxil.entrypoint = d.msl.entrypoint = "ps_main";
    d.max_fps = 60;
    return d;
}
EtchShaderSurfaceEvalDesc etch_shader_surface_eval_desc_default(void) {
    EtchShaderSurfaceEvalDesc d = {}; d.width = d.height = 1; d.enabled = true; return d;
}
EtchShaderSurfaceId etch_create_shader_surface(EtchContext* ctx, const EtchShaderSurfaceCreateDesc* d) {
    if (!d) return 0;
    return etch::create_shader_surface(to_ctx(ctx), {{d->spirv.data,d->spirv.size,d->spirv.entrypoint},
        {d->dxil.data,d->dxil.size,d->dxil.entrypoint}, {d->msl.data,d->msl.size,d->msl.entrypoint},
        d->continuous_time, d->max_fps, d->num_samplers, d->sampler_mirror_repeat, d->enable_feedback});
}
void etch_destroy_shader_surface(EtchContext* ctx, EtchShaderSurfaceId id) { etch::destroy_shader_surface(to_ctx(ctx),id); }
SDL_GPUTexture* etch_eval_shader_surface(EtchContext* ctx, EtchShaderSurfaceId id, const EtchShaderSurfaceEvalDesc* d) {
    if (!d) return nullptr;
    etch::ShaderSurfaceEvalDesc eval = {}; eval.width=d->width; eval.height=d->height; eval.revision=d->revision; eval.enabled=d->enabled;
    for (uint32_t i=0;i<4;++i) { eval.params[i]=to_cpp(d->params[i]); eval.textures[i]=d->textures[i]; }
    return etch::eval_shader_surface(to_ctx(ctx),id,eval);
}

EtchTerminalGridDesc etch_terminal_grid_desc_default(void) {
    const etch::TerminalGridDesc d;
    return {d.id,nullptr,0,d.columns,d.rows,d.first_row,d.cell_width_px,d.line_height_px,d.font_size_px,d.font_id,
        from_cpp(d.foreground),from_cpp(d.background),d.cursor_column,d.cursor_row,from_cpp(d.cursor_color),from_cpp(d.cursor_motion)};
}
EtchTerminalCell etch_terminal_cell_default(void) {
    return {' ',{ETCH_TERMINAL_DEFAULT,0},{ETCH_TERMINAL_DEFAULT,0},1,false,false,false};
}
namespace {
etch::TerminalCell read_c_terminal_cell(const void* data,size_t index) noexcept {
    const auto& c=static_cast<const EtchTerminalCell*>(data)[index];
    return {c.codepoint,{static_cast<etch::TerminalColorMode>(c.foreground.mode),c.foreground.value},
        {static_cast<etch::TerminalColorMode>(c.background.mode),c.background.value},c.width,c.bold,c.underline,c.inverse};
}
}
void etch_terminal_grid(const EtchTerminalGridDesc* d) {
    if(!d || !d->cells) return;
    const etch::TerminalGridDesc desc={.id=d->id,.columns=d->columns,.rows=d->rows,.first_row=d->first_row,
        .cell_width_px=d->cell_width_px,.line_height_px=d->line_height_px,.font_size_px=d->font_size_px,.font_id=d->font_id,
        .foreground=to_cpp(d->foreground),.background=to_cpp(d->background),.cursor_column=d->cursor_column,
        .cursor_row=d->cursor_row,.cursor_color=to_cpp(d->cursor_color),.cursor_motion=to_cpp(d->cursor_motion)};
    if(etch::terminal_grid_valid(desc,d->cell_count)) etch::terminal_grid_run(desc,d->cells,d->cell_count,read_c_terminal_cell);
}
EtchSheetDesc etch_sheet_desc_default(void) {
    const etch::SheetDesc d;
    EtchSheetDesc result={d.id,d.open,static_cast<EtchSheetEdge>(d.edge),d.extent_px,{},d.snap_count,nullptr,
        from_cpp(d.background),from_cpp(d.motion),d.z_index,nullptr,d.dismiss_on_backdrop,d.dismiss_on_escape,
        d.show_handle,from_cpp(d.backdrop_color),from_cpp(d.border_color),d.border_width,
        d.corner_radius_px,d.inset_px,d.backdrop_blur,d.top_inset_px,from_cpp(d.shadow_color),d.shadow_blur,from_cpp(d.shadow_offset)};
    for(uint32_t i=0;i<8;++i) result.snap_points[i]=d.snap_points[i];
    return result;
}
bool etch_begin_sheet(const EtchSheetDesc* d) {
    if(!d) return false;
    etch::SheetDesc desc={.id=d->id,.open=d->open,.edge=static_cast<etch::SheetEdge>(d->edge),.extent_px=d->extent_px,
        .snap_count=d->snap_count,.snap_index=d->snap_index,.background=to_cpp(d->background),.motion=to_cpp(d->motion),.z_index=d->z_index,
        .open_state=d->open_state,.dismiss_on_backdrop=d->dismiss_on_backdrop,.dismiss_on_escape=d->dismiss_on_escape,
        .show_handle=d->show_handle,.backdrop_color=to_cpp(d->backdrop_color),.border_color=to_cpp(d->border_color),
        .border_width=d->border_width,.corner_radius_px=d->corner_radius_px,.inset_px=d->inset_px,.backdrop_blur=d->backdrop_blur,
        .top_inset_px=d->top_inset_px,.shadow_color=to_cpp(d->shadow_color),.shadow_blur=d->shadow_blur,.shadow_offset=to_cpp(d->shadow_offset)};
    for(uint32_t i=0;i<8;++i) desc.snap_points[i]=d->snap_points[i];
    return etch::begin_sheet(desc);
}
void etch_end_sheet(void) {etch::end_sheet();}
