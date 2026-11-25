// Pure C99 ABI facade over Etch's Orthodox C++20 core.
// Compiled via src/etch_unity.cpp or standalone when ETCH_UNITY_BUILD=OFF.

#include "etch/etch.h"
#include "etch/etch_c.h"
#include "etch_c_internal.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace {

#define ETCH_ASSERT_LAYOUT_COMPAT(CT, CppT) \
    static_assert(sizeof(CT) == sizeof(CppT) && alignof(CT) == alignof(CppT)); \
    static_assert(std::is_standard_layout_v<CT> && std::is_standard_layout_v<CppT>); \
    static_assert(std::is_trivially_copyable_v<CT> && std::is_trivially_copyable_v<CppT>)

#define ETCH_ASSERT_FIELD_COMPAT(CT, CppT, field) \
    static_assert(offsetof(CT, field) == offsetof(CppT, field) && sizeof(CT::field) == sizeof(CppT::field))

ETCH_ASSERT_LAYOUT_COMPAT(EtchVec2, etch::Vec2);
ETCH_ASSERT_FIELD_COMPAT(EtchVec2, etch::Vec2, x);
ETCH_ASSERT_FIELD_COMPAT(EtchVec2, etch::Vec2, y);

ETCH_ASSERT_LAYOUT_COMPAT(EtchVec4, etch::Vec4);
ETCH_ASSERT_FIELD_COMPAT(EtchVec4, etch::Vec4, x);
ETCH_ASSERT_FIELD_COMPAT(EtchVec4, etch::Vec4, y);
ETCH_ASSERT_FIELD_COMPAT(EtchVec4, etch::Vec4, z);
ETCH_ASSERT_FIELD_COMPAT(EtchVec4, etch::Vec4, w);

ETCH_ASSERT_LAYOUT_COMPAT(EtchUint4, etch::Uint4);
ETCH_ASSERT_FIELD_COMPAT(EtchUint4, etch::Uint4, x);
ETCH_ASSERT_FIELD_COMPAT(EtchUint4, etch::Uint4, y);
ETCH_ASSERT_FIELD_COMPAT(EtchUint4, etch::Uint4, z);
ETCH_ASSERT_FIELD_COMPAT(EtchUint4, etch::Uint4, w);

ETCH_ASSERT_LAYOUT_COMPAT(EtchRect, etch::Rect);
ETCH_ASSERT_FIELD_COMPAT(EtchRect, etch::Rect, x);
ETCH_ASSERT_FIELD_COMPAT(EtchRect, etch::Rect, y);
ETCH_ASSERT_FIELD_COMPAT(EtchRect, etch::Rect, w);
ETCH_ASSERT_FIELD_COMPAT(EtchRect, etch::Rect, h);

ETCH_ASSERT_LAYOUT_COMPAT(EtchColor, etch::Color);
ETCH_ASSERT_FIELD_COMPAT(EtchColor, etch::Color, r);
ETCH_ASSERT_FIELD_COMPAT(EtchColor, etch::Color, g);
ETCH_ASSERT_FIELD_COMPAT(EtchColor, etch::Color, b);
ETCH_ASSERT_FIELD_COMPAT(EtchColor, etch::Color, a);

ETCH_ASSERT_LAYOUT_COMPAT(EtchCorners, etch::Corners);
ETCH_ASSERT_FIELD_COMPAT(EtchCorners, etch::Corners, tl);
ETCH_ASSERT_FIELD_COMPAT(EtchCorners, etch::Corners, tr);
ETCH_ASSERT_FIELD_COMPAT(EtchCorners, etch::Corners, br);
ETCH_ASSERT_FIELD_COMPAT(EtchCorners, etch::Corners, bl);

ETCH_ASSERT_LAYOUT_COMPAT(EtchPadding, etch::Padding);
ETCH_ASSERT_FIELD_COMPAT(EtchPadding, etch::Padding, left);
ETCH_ASSERT_FIELD_COMPAT(EtchPadding, etch::Padding, right);
ETCH_ASSERT_FIELD_COMPAT(EtchPadding, etch::Padding, top);
ETCH_ASSERT_FIELD_COMPAT(EtchPadding, etch::Padding, bottom);

static_assert(sizeof(EtchPrim) == 64);
ETCH_ASSERT_LAYOUT_COMPAT(EtchPrim, etch::Prim);
ETCH_ASSERT_FIELD_COMPAT(EtchPrim, etch::Prim, bounds);
ETCH_ASSERT_FIELD_COMPAT(EtchPrim, etch::Prim, clip);
ETCH_ASSERT_FIELD_COMPAT(EtchPrim, etch::Prim, payload);
ETCH_ASSERT_FIELD_COMPAT(EtchPrim, etch::Prim, style);

static_assert(sizeof(EtchGlobalsUniform) == 32);
ETCH_ASSERT_LAYOUT_COMPAT(EtchGlobalsUniform, etch::GlobalsUniform);
ETCH_ASSERT_FIELD_COMPAT(EtchGlobalsUniform, etch::GlobalsUniform, viewport_size);
ETCH_ASSERT_FIELD_COMPAT(EtchGlobalsUniform, etch::GlobalsUniform, inv_viewport_size);
ETCH_ASSERT_FIELD_COMPAT(EtchGlobalsUniform, etch::GlobalsUniform, dpi_scale);
ETCH_ASSERT_FIELD_COMPAT(EtchGlobalsUniform, etch::GlobalsUniform, time_seconds);
ETCH_ASSERT_FIELD_COMPAT(EtchGlobalsUniform, etch::GlobalsUniform, frame_index);
ETCH_ASSERT_FIELD_COMPAT(EtchGlobalsUniform, etch::GlobalsUniform, flags);

ETCH_ASSERT_LAYOUT_COMPAT(EtchStr, etch::Str);
ETCH_ASSERT_FIELD_COMPAT(EtchStr, etch::Str, chars);
ETCH_ASSERT_FIELD_COMPAT(EtchStr, etch::Str, length);
ETCH_ASSERT_FIELD_COMPAT(EtchStr, etch::Str, is_static);

ETCH_ASSERT_LAYOUT_COMPAT(EtchFileDialogFilter, etch::FileDialogFilter);
ETCH_ASSERT_FIELD_COMPAT(EtchFileDialogFilter, etch::FileDialogFilter, name);
ETCH_ASSERT_FIELD_COMPAT(EtchFileDialogFilter, etch::FileDialogFilter, pattern);

ETCH_ASSERT_LAYOUT_COMPAT(EtchTextInputHighlight, etch::TextInputHighlight);
ETCH_ASSERT_FIELD_COMPAT(EtchTextInputHighlight, etch::TextInputHighlight, byte_offset);
ETCH_ASSERT_FIELD_COMPAT(EtchTextInputHighlight, etch::TextInputHighlight, byte_length);
ETCH_ASSERT_FIELD_COMPAT(EtchTextInputHighlight, etch::TextInputHighlight, text_color);
ETCH_ASSERT_FIELD_COMPAT(EtchTextInputHighlight, etch::TextInputHighlight, background);
ETCH_ASSERT_FIELD_COMPAT(EtchTextInputHighlight, etch::TextInputHighlight, border_color);
ETCH_ASSERT_FIELD_COMPAT(EtchTextInputHighlight, etch::TextInputHighlight, radius_px);

ETCH_ASSERT_LAYOUT_COMPAT(EtchTextSpan, etch::TextSpan);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, text);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, color);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, background);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, border_color);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, font_size_px);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, font_id);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, underline);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, strikethrough);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, letter_spacing);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, padding_x);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, bg_radius);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, id);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, icon_id);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, icon_color);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, icon_size_px);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, icon_gap_px);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, padding_y);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, hover_background);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, has_wipe);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, color_end);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, wipe_x);
ETCH_ASSERT_FIELD_COMPAT(EtchTextSpan, etch::TextSpan, wipe_feather_px);

ETCH_ASSERT_LAYOUT_COMPAT(EtchArena, etch::Arena);
ETCH_ASSERT_FIELD_COMPAT(EtchArena, etch::Arena, base);
ETCH_ASSERT_FIELD_COMPAT(EtchArena, etch::Arena, capacity);
ETCH_ASSERT_FIELD_COMPAT(EtchArena, etch::Arena, offset);
ETCH_ASSERT_FIELD_COMPAT(EtchArena, etch::Arena, peak_offset);

ETCH_ASSERT_LAYOUT_COMPAT(EtchFontFeature, etch::FontFeature);
ETCH_ASSERT_FIELD_COMPAT(EtchFontFeature, etch::FontFeature, tag);
ETCH_ASSERT_FIELD_COMPAT(EtchFontFeature, etch::FontFeature, value);

ETCH_ASSERT_LAYOUT_COMPAT(EtchFontAxisValue, etch::FontAxisValue);
ETCH_ASSERT_FIELD_COMPAT(EtchFontAxisValue, etch::FontAxisValue, tag);
ETCH_ASSERT_FIELD_COMPAT(EtchFontAxisValue, etch::FontAxisValue, value);

#undef ETCH_ASSERT_FIELD_COMPAT
#undef ETCH_ASSERT_LAYOUT_COMPAT

// Note: EtchGridTrack, EtchTableColumnDesc, EtchPathCmd, and EtchTerminalCell embed C enums (int, 4B)
// whereas their C++ counterparts use enum class : uint8_t (1B), so they keep safe conversion/reader paths.

inline bool validate_c_struct_size(uint32_t sz, size_t expected) noexcept {
    (void)expected;
    return sz == 0u || sz >= sizeof(uint32_t);
}

template <typename T>
inline const T* resolve_c_desc(const T* src, T* scratch, T (*make_default)()) noexcept {
    static_assert(std::is_trivially_copyable_v<T>);
    const uint32_t sz = src->struct_size;
    if (!validate_c_struct_size(sz, sizeof(T))) {
        return nullptr;
    }
    if (sz == 0u || sz >= sizeof(T)) {
        return src;
    }
    *scratch = make_default();
    std::memcpy(scratch, src, sz);
    scratch->struct_size = static_cast<uint32_t>(sizeof(T));
    return scratch;
}

template <typename CppT, typename CT>
inline std::span<const CppT> view_compatible_span(const CT* src, uint32_t count) noexcept {
    static_assert(sizeof(CT) == sizeof(CppT) && alignof(CT) == alignof(CppT));
    static_assert(std::is_standard_layout_v<CT> && std::is_standard_layout_v<CppT>);
    static_assert(std::is_trivially_copyable_v<CT> && std::is_trivially_copyable_v<CppT>);
    if (!src || count == 0u) return {};
    return {reinterpret_cast<const CppT*>(src), count};
}

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
inline etch::Str to_cpp(EtchStr s) noexcept {
    if (!s.chars && s.length != 0u) {
        return {};
    }
    return {s.chars, s.length, s.is_static};
}
inline EtchStr from_cpp(etch::Str s) noexcept { return {s.chars, s.length, s.is_static}; }
inline etch::Sizing to_cpp(EtchSizing s) noexcept { return {static_cast<etch::SizingKind>(s.kind), s.value, s.min, s.max}; }
inline EtchSizing from_cpp(etch::Sizing s) noexcept { return {static_cast<EtchSizingKind>(s.kind), s.value, s.min, s.max}; }
inline etch::SpringSpec to_cpp(EtchSpringSpec s) noexcept { return {s.response, s.damping_ratio}; }
inline EtchSpringSpec from_cpp(etch::SpringSpec s) noexcept { return {s.response, s.damping_ratio}; }
inline etch::BorderBeamStyle to_cpp(EtchBorderBeamStyle s) noexcept { return {to_cpp(s.color),to_cpp(s.base_color),s.width,s.angle_rad,s.sweep_fraction,s.glow_px,s.speed,s.softness,s.beam_active,s.symmetric_dual}; }
inline EtchBorderBeamStyle from_cpp(etch::BorderBeamStyle s) noexcept { return {from_cpp(s.color),from_cpp(s.base_color),s.width,s.angle_rad,s.sweep_fraction,s.glow_px,s.speed,s.softness,s.beam_active,s.symmetric_dual}; }
inline etch::ShadowLayer to_cpp(const EtchShadowLayer& s) noexcept {
    return {to_cpp(s.offset), s.blur_px, s.spread_px, to_cpp(s.color), s.inset};
}
inline EtchShadowLayer from_cpp(const etch::ShadowLayer& s) noexcept {
    return {from_cpp(s.offset), s.blur_px, s.spread_px, from_cpp(s.color), s.inset};
}
inline etch::GradientDesc to_cpp(const EtchGradientDesc& g) noexcept {
    etch::GradientDesc out{};
    out.kind = (static_cast<uint32_t>(g.kind) <= static_cast<uint32_t>(ETCH_GRADIENT_CONIC))
        ? static_cast<etch::GradientKind>(g.kind)
        : etch::GradientKind::None;
    out.oklab = g.oklab;
    out.dir = to_cpp(g.dir);
    out.center = to_cpp(g.center);
    out.angle_rad = g.angle_rad;
    out.stop_count = g.stop_count > 8u ? 8u : g.stop_count;
    for (uint32_t i = 0u; i < 8u; ++i) {
        out.stops[i] = {to_cpp(g.stops[i].color), g.stops[i].position};
    }
    return out;
}
inline EtchGradientDesc from_cpp(const etch::GradientDesc& g) noexcept {
    EtchGradientDesc out{};
    out.kind = static_cast<EtchGradientKind>(g.kind);
    out.oklab = g.oklab;
    out.dir = from_cpp(g.dir);
    out.center = from_cpp(g.center);
    out.angle_rad = g.angle_rad;
    out.stop_count = g.stop_count;
    for (uint32_t i = 0u; i < 8u; ++i) {
        out.stops[i] = {from_cpp(g.stops[i].color), g.stops[i].position};
    }
    return out;
}
inline etch::EasingSpec to_cpp(EtchEasingSpec s) noexcept {
    const etch::EasingKind kind = (static_cast<uint32_t>(s.kind) <= static_cast<uint32_t>(ETCH_EASING_CUBIC_BEZIER))
        ? static_cast<etch::EasingKind>(s.kind)
        : etch::EasingKind::EaseOut;
    return {kind, s.x1, s.y1, s.x2, s.y2};
}
inline EtchEasingSpec from_cpp(etch::EasingSpec s) noexcept {
    return {static_cast<EtchEasingKind>(s.kind), s.x1, s.y1, s.x2, s.y2};
}
inline etch::ImageFit to_cpp(EtchImageFit f) noexcept {
    return (static_cast<uint32_t>(f) <= static_cast<uint32_t>(ETCH_IMAGE_FIT_COVER))
        ? static_cast<etch::ImageFit>(f)
        : etch::ImageFit::Fill;
}
inline etch::BlendMode to_cpp(EtchBlendMode m) noexcept {
    return (static_cast<uint32_t>(m) <= static_cast<uint32_t>(ETCH_BLEND_SCREEN))
        ? static_cast<etch::BlendMode>(m)
        : etch::BlendMode::Normal;
}
inline etch::AlphaMaskDesc to_cpp(const EtchAlphaMaskDesc& m) noexcept {
    etch::AlphaMaskDesc out{};
    out.kind = (static_cast<uint32_t>(m.kind) <= static_cast<uint32_t>(ETCH_ALPHA_MASK_TEXTURE))
        ? static_cast<etch::AlphaMaskKind>(m.kind)
        : etch::AlphaMaskKind::None;
    out.invert = m.invert;
    out.angle_deg = m.angle_deg;
    out.start_t = m.start_t;
    out.end_t = m.end_t;
    out.alpha_start = m.alpha_start;
    out.alpha_end = m.alpha_end;
    out.center_norm = to_cpp(m.center_norm);
    out.radius_norm = to_cpp(m.radius_norm);
    out.texture = m.texture;
    out.uv = to_cpp(m.uv);
    out.radius = to_cpp(m.radius);
    out.continuous_corners = m.continuous_corners;
    return out;
}
inline EtchAlphaMaskDesc from_cpp(const etch::AlphaMaskDesc& m) noexcept {
    EtchAlphaMaskDesc out{};
    out.kind = static_cast<EtchAlphaMaskKind>(m.kind);
    out.invert = m.invert;
    out.angle_deg = m.angle_deg;
    out.start_t = m.start_t;
    out.end_t = m.end_t;
    out.alpha_start = m.alpha_start;
    out.alpha_end = m.alpha_end;
    out.center_norm = from_cpp(m.center_norm);
    out.radius_norm = from_cpp(m.radius_norm);
    out.texture = m.texture;
    out.uv = from_cpp(m.uv);
    out.radius = from_cpp(m.radius);
    out.continuous_corners = m.continuous_corners;
    return out;
}

inline etch::GridTrack to_cpp(const EtchGridTrack& t) noexcept {
    return {static_cast<etch::SizingKind>(t.kind), t.value, t.min_px, t.max_px};
}

inline etch::PathCmd to_cpp(const EtchPathCmd& c) noexcept {
    return {static_cast<etch::PathVerb>(c.verb), to_cpp(c.p1), to_cpp(c.p2), to_cpp(c.p3)};
}

inline etch::TableColumnDesc to_cpp(const EtchTableColumnDesc& c) noexcept {
    return {c.id, to_cpp(c.title), static_cast<etch::TableColumnSizing>(c.sizing),
            c.width, c.min_width, c.max_width, static_cast<etch::AlignX>(c.align_x),
            c.resizable, c.sortable};
}

template <typename CppT, typename CT>
std::span<const CppT> convert_frame_span(const CT* src, uint32_t count, CppT (*conv)(const CT&) noexcept) noexcept {
    if (!src || count == 0u) return {};
    etch::Context* ctx = etch::current_context();
    if (!etch::frame_in_progress(ctx)) return {};
    etch::Arena* arena = etch::frame_arena(ctx);
    CppT* dst = arena ? arena->alloc<CppT>(count) : nullptr;
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
    out.justify = static_cast<etch::Justify>(d.justify);
    out.align_self = static_cast<etch::AlignSelf>(d.align_self);
    out.flex_grow = d.flex_grow;
    out.flex_shrink = d.flex_shrink;
    out.margin = to_cpp(d.margin);
    out.grid_row_tracks = convert_frame_span<etch::GridTrack, EtchGridTrack>(d.grid_row_tracks, d.grid_row_track_count, to_cpp);
    out.grid_row_span = d.grid_row_span ? d.grid_row_span : 1u;
    out.flip_on_overflow = d.flip_on_overflow;
    out.hitbox_behavior = static_cast<etch::HitboxBehavior>(d.hitbox_behavior);
    out.stick_to_bottom = d.stick_to_bottom;
    for (uint32_t i = 0u; i < 4u; ++i) {
        out.shadows[i] = to_cpp(d.shadows[i]);
    }
    out.shadow_count = d.shadow_count > 4u ? 4u : d.shadow_count;
    out.elevation = (static_cast<uint32_t>(d.elevation) <= static_cast<uint32_t>(ETCH_ELEVATION_XL))
        ? static_cast<etch::Elevation>(d.elevation)
        : etch::Elevation::None;
    out.shadow_inset = d.shadow_inset;
    out.gradient = to_cpp(d.gradient);
    out.backdrop_saturation = d.backdrop_saturation;
    out.backdrop_brightness = d.backdrop_brightness;
    out.backdrop_noise = d.backdrop_noise;
    out.blend_mode = to_cpp(d.blend_mode);
    return out;
}

inline etch::TextDesc to_cpp(const EtchTextDesc& d) noexcept {
    return {to_cpp(d.color), d.font_size_px, d.font_id, d.letter_spacing, d.wrap, d.id,
            d.selectable, to_cpp(d.selection_color), d.line_height_px,
            static_cast<etch::AlignX>(d.align_x), d.ellipsis, d.max_lines,
            d.opentype_flags,
            view_compatible_span<etch::FontFeature>(d.font_features, d.font_features ? d.font_feature_count : 0u)};
}

inline etch::RichTextDesc to_cpp(const EtchRichTextDesc& d) noexcept {
    return {to_cpp(d.color), d.font_size_px, d.font_id, d.line_height_px, d.letter_spacing,
            static_cast<etch::AlignX>(d.align_x), d.wrap, d.id, d.selectable,
            to_cpp(d.selection_color), d.ellipsis, d.max_lines};
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
    etch::Arena* arena = (max_tokens > 256u) ? etch::frame_arena(etch::current_context()) : nullptr;
    EtchSyntaxToken* c_tokens = (max_tokens > 256u) ? (arena ? arena->alloc<EtchSyntaxToken>(max_tokens) : nullptr) : local;
    if (!c_tokens) return 0u;
    const uint32_t cap = max_tokens;
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
    out_dec->has_display_line_number = c_dec.has_display_line_number;
    out_dec->display_line_number = c_dec.display_line_number;
    out_dec->hide_line_number = c_dec.hide_line_number;
    return true;
}

struct CLayoutDraw {
    EtchLayoutDrawFn callback;
    void* data;
};

etch::TerminalCell read_c_terminal_cell(const void* data, size_t index) noexcept {
    const auto& c = static_cast<const EtchTerminalCell*>(data)[index];
    return {c.codepoint, {static_cast<etch::TerminalColorMode>(c.foreground.mode), c.foreground.value},
            {static_cast<etch::TerminalColorMode>(c.background.mode), c.background.value},
            c.width, c.bold, c.underline, c.inverse,
            c.italic, c.dim, c.strikethrough,
            static_cast<etch::TerminalUnderlineStyle>(c.underline_style),
            {static_cast<etch::TerminalColorMode>(c.underline_color.mode), c.underline_color.value},
            c.hyperlink_id, c.grapheme_utf8, c.grapheme_len};
}

inline EtchVirtualListSlice from_cpp(const etch::VirtualListSlice& s) noexcept {
    return {s.first_index, s.end_index, s.total_count, s.scroll_y, s.max_scroll_y, s.viewport_height, s.at_bottom};
}

inline EtchCanvasState from_cpp(const etch::CanvasState& s) noexcept {
    return {from_cpp(s.pan), s.zoom, from_cpp(s.viewport), s.panning,
            s.marquee_active, s.marquee_finished, from_cpp(s.marquee_world)};
}

} // namespace

ETCH_API uint32_t etch_abi_version(void) {
    return ETCH_ABI_VERSION;
}

/* ============================================================================
 * Descriptor Default Factories
 * ============================================================================ */

ETCH_API EtchContextDesc etch_context_desc_default(void) {
    const etch::ContextDesc d{};
    return {static_cast<uint32_t>(sizeof(EtchContextDesc)),
            d.max_layout_elements, d.window, d.frame_arena_bytes, d.max_prims_per_frame,
            d.font_path, d.font_size_px, d.enable_gpu_debug, d.headless, d.gpu_driver,
            nullptr, nullptr, d.enable_os_a11y, d.respect_reduced_motion, 0u,
            d.prefer_high_performance_gpu, d.lcd_subpixel_text};
}

ETCH_API EtchFrameDesc etch_frame_desc_default(void) {
    const etch::FrameDesc d{};
    return {static_cast<uint32_t>(sizeof(EtchFrameDesc)),
            d.viewport_w, d.viewport_h, d.dpi_scale, d.dt_seconds, from_cpp(d.pointer_pos),
            d.pointer_down, from_cpp(d.scroll_delta), d.right_pointer_down, d.pointer_clicks,
            d.key_mods, d.key_pressed_code, d.pixel_snap, d.bloom_intensity, d.bloom_threshold};
}

ETCH_API EtchWindowFrameDesc etch_window_frame_desc_default(void) {
    const etch::WindowFrameDesc d{};
    return {static_cast<uint32_t>(sizeof(EtchWindowFrameDesc)),
            d.dt, d.min_scale, d.pixel_snap, d.bloom_intensity, d.bloom_threshold};
}

ETCH_API EtchShadowLayer etch_shadow_layer_default(void) {
    return from_cpp(etch::ShadowLayer{});
}

ETCH_API EtchGradientDesc etch_gradient_desc_default(void) {
    return from_cpp(etch::GradientDesc{});
}

ETCH_API EtchAlphaMaskDesc etch_alpha_mask_desc_default(void) {
    return from_cpp(etch::AlphaMaskDesc{});
}

ETCH_API EtchBoxDesc etch_box_desc_default(void) {
    const etch::BoxDesc d{};
    return {static_cast<uint32_t>(sizeof(EtchBoxDesc)),
            d.id, static_cast<EtchAxis>(d.axis), from_cpp(d.width), from_cpp(d.height),
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
            from_cpp(d.border_sides), d.border_dash_px, d.border_gap_px, from_cpp(d.edge_fade),
            static_cast<EtchJustify>(d.justify), static_cast<EtchAlignSelf>(d.align_self),
            d.flex_grow, d.flex_shrink, from_cpp(d.margin), nullptr, 0u, d.grid_row_span,
            d.flip_on_overflow, static_cast<EtchHitboxBehavior>(d.hitbox_behavior),
            d.stick_to_bottom, 0u,
            {from_cpp(d.shadows[0]), from_cpp(d.shadows[1]), from_cpp(d.shadows[2]), from_cpp(d.shadows[3])},
            d.shadow_count, static_cast<EtchElevation>(d.elevation), d.shadow_inset,
            from_cpp(d.gradient), d.backdrop_saturation, d.backdrop_brightness, d.backdrop_noise,
            static_cast<EtchBlendMode>(d.blend_mode)};
}

ETCH_API EtchTextDesc etch_text_desc_default(void) {
    const etch::TextDesc d{};
    return {static_cast<uint32_t>(sizeof(EtchTextDesc)),
            from_cpp(d.color), d.font_size_px, d.font_id, d.letter_spacing, d.wrap, d.id,
            d.selectable, from_cpp(d.selection_color), d.line_height_px,
            static_cast<EtchAlignX>(d.align_x), d.ellipsis, d.max_lines,
            d.opentype_flags, nullptr, 0u};
}

ETCH_API EtchTextSpan etch_text_span_default(void) {
    const etch::TextSpan d{};
    return {from_cpp(d.text), from_cpp(d.color), from_cpp(d.background), from_cpp(d.border_color),
            d.font_size_px, d.font_id, d.underline, d.strikethrough, d.letter_spacing,
            d.padding_x, d.bg_radius, d.id, d.icon_id, from_cpp(d.icon_color), d.icon_size_px,
            d.icon_gap_px, d.padding_y, from_cpp(d.hover_background),
            d.has_wipe, from_cpp(d.color_end), d.wipe_x, d.wipe_feather_px};
}

ETCH_API EtchRichTextDesc etch_rich_text_desc_default(void) {
    const etch::RichTextDesc d{};
    return {static_cast<uint32_t>(sizeof(EtchRichTextDesc)),
            from_cpp(d.color), d.font_size_px, d.font_id, d.line_height_px, d.letter_spacing,
            static_cast<EtchAlignX>(d.align_x), d.wrap, d.id, d.selectable,
            from_cpp(d.selection_color), d.ellipsis, d.max_lines};
}

ETCH_API EtchCollapsibleDesc etch_collapsible_desc_default(void) {
    const etch::CollapsibleDesc d{};
    return {d.id, from_cpp(d.title), d.icon_id, d.default_open, d.header_height_px, d.font_size_px,
            d.font_id, from_cpp(d.header_padding), from_cpp(d.body_padding), d.body_gap,
            from_cpp(d.radius), from_cpp(d.header_bg), from_cpp(d.header_hover_bg),
            from_cpp(d.body_bg), from_cpp(d.border_color), d.border_width,
            from_cpp(d.text_color), from_cpp(d.icon_color), from_cpp(d.spring_spec)};
}

ETCH_API EtchCollapseDesc etch_collapse_desc_default(void) {
    const etch::CollapseDesc d{};
    return {static_cast<uint32_t>(sizeof(EtchCollapseDesc)),
            d.id, d.open, from_cpp(d.spring), d.fade_content, from_cpp(d.width)};
}

ETCH_API EtchImageDesc etch_image_desc_default(void) {
    const etch::ImageDesc d{};
    return {d.id, d.texture, d.sprite_id, from_cpp(d.width), from_cpp(d.height),
            from_cpp(d.uv_min), from_cpp(d.uv_max), from_cpp(d.radius), from_cpp(d.tint),
            from_cpp(d.border_color), d.border_width, d.aspect_ratio, d.grid_col_span,
            d.continuous_corners, static_cast<EtchImageFit>(d.fit), from_cpp(d.source_size),
            static_cast<EtchBlendMode>(d.blend_mode)};
}

ETCH_API EtchIconDesc etch_icon_desc_default(void) {
    const etch::IconDesc d{};
    return {d.id, d.icon_id, d.size_px, from_cpp(d.color), d.rotation_rad, d.scale, d.opacity,
            from_cpp(d.secondary_color)};
}

ETCH_API EtchRectDrawDesc etch_rect_draw_desc_default(void) {
    const etch::RectDrawDesc d{};
    return {from_cpp(d.rect), from_cpp(d.fill), from_cpp(d.fill_end), from_cpp(d.gradient_dir),
            d.radial_gradient, from_cpp(d.radius), from_cpp(d.border_color), d.border_width,
            d.foreground, d.angle_rad, d.softness, d.continuous_corners,
            static_cast<EtchBorderAlign>(d.border_align), from_cpp(d.gradient)};
}

ETCH_API EtchLineDesc etch_line_draw_desc_default(void) {
    const etch::LineDesc d{};
    return {from_cpp(d.from), from_cpp(d.to), from_cpp(d.color), from_cpp(d.color_end),
            d.thickness, from_cpp(d.glow_color), d.glow_px, d.foreground};
}

ETCH_API EtchPolylineDesc etch_polyline_desc_default(void) {
    const etch::PolylineDesc d{};
    return {nullptr, 0u, from_cpp(d.color), d.thickness, from_cpp(d.glow_color),
            d.glow_px, d.closed, d.foreground};
}

ETCH_API EtchFilledPolygonDesc etch_filled_polygon_desc_default(void) {
    const etch::FilledPolygonDesc d{};
    return {nullptr, 0u, from_cpp(d.fill), from_cpp(d.fill_end), from_cpp(d.gradient_dir),
            d.radial_gradient, from_cpp(d.stroke_color), d.stroke_width, d.foreground};
}

ETCH_API EtchFilledPathDesc etch_filled_path_desc_default(void) {
    const etch::FilledPathDesc d{};
    return {nullptr, 0u, from_cpp(d.svg_path_d), from_cpp(d.offset), d.scale, from_cpp(d.fill),
            from_cpp(d.fill_end), from_cpp(d.gradient_dir), d.radial_gradient,
            from_cpp(d.stroke_color), d.stroke_width, d.foreground};
}

ETCH_API EtchAreaSeriesDesc etch_area_series_desc_default(void) {
    const etch::AreaSeriesDesc d{};
    return {nullptr, 0u, d.baseline_y, from_cpp(d.fill_top), from_cpp(d.fill_bottom),
            from_cpp(d.line_color), d.line_thickness, d.smooth, d.foreground};
}

ETCH_API EtchBezierDesc etch_bezier_desc_default(void) {
    const etch::BezierDesc d{};
    return {from_cpp(d.p0), from_cpp(d.p1), from_cpp(d.p2), from_cpp(d.p3), d.cubic,
            from_cpp(d.color_start), from_cpp(d.color_end), d.thickness,
            from_cpp(d.glow_color), d.glow_px, d.foreground};
}

ETCH_API EtchRingDesc etch_ring_desc_default(void) {
    const etch::RingDesc d{};
    return {from_cpp(d.center), d.radius, d.thickness, d.start_angle_rad, d.sweep_angle_rad,
            from_cpp(d.color), from_cpp(d.color_end), from_cpp(d.track_color),
            from_cpp(d.glow_color), d.glow_px, d.round_caps, d.foreground};
}

ETCH_API EtchIconDrawDesc etch_icon_draw_desc_default(void) {
    const etch::IconDrawDesc d{};
    return {d.icon_id, from_cpp(d.rect), from_cpp(d.color), d.foreground, d.angle_rad};
}

ETCH_API EtchTextDrawDesc etch_text_draw_desc_default(void) {
    const etch::TextDrawDesc d{};
    return {from_cpp(d.text), from_cpp(d.pos), d.font_id, d.size, from_cpp(d.color),
            d.letter_spacing, static_cast<EtchAlignX>(d.align_x),
            static_cast<EtchAlignY>(d.align_y), d.foreground, d.max_width_px, d.ellipsis,
            d.has_wipe, from_cpp(d.color_end), d.wipe_x, d.wipe_feather_px,
            from_cpp(d.shadow_color), from_cpp(d.shadow_offset), d.shadow_blur};
}

ETCH_API EtchImageDrawDesc etch_image_draw_desc_default(void) {
    const etch::ImageDrawDesc d{};
    return {d.texture, d.sprite_id, from_cpp(d.rect), from_cpp(d.uv_rect),
            from_cpp(d.tint), d.radius, d.foreground, d.continuous_corners,
            static_cast<EtchImageFit>(d.fit), from_cpp(d.source_size)};
}

ETCH_API EtchTextureDesc etch_texture_desc_default(void) {
    const etch::TextureDesc d{};
    return {d.rgba_pixels, d.width, d.height, d.row_pitch_bytes, d.srgb,
            d.debug_name, d.premultiplied_alpha, d.generate_mipmaps};
}

ETCH_API EtchSvgRasterDesc etch_svg_raster_desc_default(void) {
    const etch::SvgRasterDesc d{};
    return {static_cast<uint32_t>(sizeof(EtchSvgRasterDesc)),
            d.width_px, d.height_px, d.scale, d.srgb, d.generate_mipmaps};
}

ETCH_API EtchWindowChromeDesc etch_window_chrome_desc_default(void) {
    const etch::WindowChromeDesc d{};
    return {static_cast<uint32_t>(sizeof(EtchWindowChromeDesc)),
            d.enabled, d.resizable, d.border_thickness, d.corner_size,
            d.macos_traffic_lights, d.traffic_light_inset_px, static_cast<EtchWindowBackdrop>(d.backdrop)};
}

ETCH_API EtchCodeEditorDesc etch_code_editor_desc_default(void) {
    const etch::CodeEditorDesc d{};
    return {static_cast<uint32_t>(sizeof(EtchCodeEditorDesc)),
            d.id, nullptr, from_cpp(d.width), from_cpp(d.height), from_cpp(d.padding),
            d.font_size_px, d.font_id, d.line_height_px, d.show_line_numbers, d.gutter_width_px,
            d.tab_width, d.read_only, d.show_scrollbar, d.word_wrap, etch_tokenize_cpp_hlsl,
            nullptr, from_cpp(d.background), from_cpp(d.gutter_background),
            from_cpp(d.active_line_color), from_cpp(d.selection_color), from_cpp(d.text_color),
            from_cpp(d.gutter_text_color), from_cpp(d.gutter_active_text_color),
            from_cpp(d.caret_color), from_cpp(d.composition_color), from_cpp(d.border_color),
            from_cpp(d.focus_border_color), d.border_width, d.focus_border_width,
            from_cpp(d.radius), d.scroll_fade_px, nullptr, nullptr, d.gutter_tag_width_px,
            d.auto_height, d.show_overview_ruler, d.show_sticky_header, from_cpp(d.sticky_header_text),
            from_cpp(d.language), d.highlight_matching_brackets, d.show_indent_guides, d.show_diagnostic_tooltips, d.show_minimap};
}

ETCH_API EtchTextInputHighlight etch_text_input_highlight_default(void) {
    const etch::TextInputHighlight h{};
    return {h.byte_offset, h.byte_length, from_cpp(h.text_color), from_cpp(h.background),
            from_cpp(h.border_color), h.radius_px, h.atomic};
}

ETCH_API EtchTextInputDesc etch_text_input_desc_default(void) {
    const etch::TextInputDesc d{};
    return {static_cast<uint32_t>(sizeof(EtchTextInputDesc)),
            d.id, d.buffer, d.capacity, from_cpp(d.placeholder), from_cpp(d.width),
            from_cpp(d.height), from_cpp(d.padding), d.font_size_px, d.font_id,
            d.letter_spacing, from_cpp(d.text_color), from_cpp(d.placeholder_color),
            from_cpp(d.selection_color), from_cpp(d.caret_color), from_cpp(d.composition_color),
            from_cpp(d.background), from_cpp(d.border_color), from_cpp(d.focus_border_color),
            d.border_width, d.focus_border_width, from_cpp(d.radius), d.multiline, d.word_wrap,
            d.read_only, d.password, d.context_menu, d.clear_button, d.escape_blurs, d.suppress_popup_keys, nullptr, 0u,
            from_cpp(d.ghost_text), from_cpp(d.ghost_color), d.accept_ghost_on_tab, d.submit_on_enter,
            from_cpp(d.hover_border_color), d.focus_glow_blur, d.leading_icon_id,
            from_cpp(d.leading_icon_color), d.continuous_corners, d.disabled,
            d.auto_grow, d.min_height_px, d.max_height_px};
}

ETCH_API EtchButtonDesc etch_button_desc_default(void) {
    const etch::ButtonDesc d{};
    return {d.id, from_cpp(d.label), d.icon_id, static_cast<EtchButtonVariant>(d.variant),
            from_cpp(d.width), from_cpp(d.height), from_cpp(d.padding), d.font_size_px,
            d.font_id, from_cpp(d.radius), d.disabled, from_cpp(d.shortcut_hint), static_cast<EtchAlignX>(d.align_x)};
}

ETCH_API EtchCheckboxDesc etch_checkbox_desc_default(void) {
    const etch::CheckboxDesc d{};
    return {d.id, from_cpp(d.label), d.checked, d.indeterminate, d.disabled,
            d.box_size_px, d.font_size_px, d.font_id, from_cpp(d.accent)};
}

ETCH_API EtchToggleSwitchDesc etch_toggle_switch_desc_default(void) {
    const etch::ToggleSwitchDesc d{};
    return {d.id, from_cpp(d.label), d.value, d.disabled, d.track_width_px,
            d.track_height_px, d.font_size_px, d.font_id, from_cpp(d.on_color),
            from_cpp(d.off_color), from_cpp(d.thumb_color), from_cpp(d.border_color),
            from_cpp(d.track_radius), from_cpp(d.thumb_radius), d.thumb_padding_px, d.border_width};
}

ETCH_API EtchProgressBarDesc etch_progress_bar_desc_default(void) {
    const etch::ProgressBarDesc d{};
    return {d.id, d.fraction, from_cpp(d.overlay_text), from_cpp(d.width),
            from_cpp(d.height), from_cpp(d.radius), from_cpp(d.track_color),
            from_cpp(d.fill_color), from_cpp(d.text_color), d.font_size_px};
}

ETCH_API EtchTabBarDesc etch_tab_bar_desc_default(void) {
    const etch::TabBarDesc d{};
    return {d.id, d.active_tab, from_cpp(d.width), d.height_px, d.font_size_px,
            from_cpp(d.background), from_cpp(d.active_bg), from_cpp(d.hover_bg),
            from_cpp(d.accent), from_cpp(d.text_color), from_cpp(d.active_text_color)};
}

ETCH_API EtchTabItemDesc etch_tab_item_desc_default(void) {
    const etch::TabItemDesc d{};
    return {d.id, from_cpp(d.label), d.icon_id, d.closable, d.disabled};
}

ETCH_API EtchSliderDesc etch_slider_desc_default(void) {
    const etch::SliderDesc d{};
    return {d.id, from_cpp(d.label), d.value, d.min_value, d.max_value, d.step, d.format,
            from_cpp(d.width), d.height_px, d.disabled, from_cpp(d.background),
            from_cpp(d.fill_color), from_cpp(d.text_color), static_cast<EtchSliderStyle>(d.style),
            from_cpp(d.track_color), from_cpp(d.thumb_color), d.track_height_px,
            d.thumb_size_px, d.show_value};
}

ETCH_API EtchDragValueDesc etch_drag_value_desc_default(void) {
    const etch::DragValueDesc d{};
    return {d.id, from_cpp(d.label), d.value, d.min_value, d.max_value, d.speed, d.step,
            d.format, from_cpp(d.width), d.height_px, d.disabled, from_cpp(d.background),
            from_cpp(d.fill_color), from_cpp(d.text_color)};
}

ETCH_API EtchTooltipDesc etch_tooltip_desc_default(void) {
    const etch::TooltipDesc d{};
    return {from_cpp(d.text), d.delay_seconds, d.max_width_px, d.font_size_px, d.z_index};
}

ETCH_API EtchModalDesc etch_modal_desc_default(void) {
    const etch::ModalDesc d{};
    return {d.id, d.open, from_cpp(d.title), d.width_px, from_cpp(d.height),
            d.dismiss_on_backdrop, d.dismiss_on_escape, from_cpp(d.backdrop_color),
            from_cpp(d.background), from_cpp(d.border_color), from_cpp(d.radius), d.z_index};
}

ETCH_API EtchComboBoxDesc etch_combo_box_desc_default(void) {
    const etch::ComboBoxDesc d{};
    return {d.id, from_cpp(d.label), d.selected_index, nullptr, 0u, from_cpp(d.width),
            d.height_px, d.max_popup_height_px, d.disabled, from_cpp(d.background),
            from_cpp(d.hover_bg), from_cpp(d.text_color), from_cpp(d.border_color),
            from_cpp(d.focus_border_color), d.border_width};
}

ETCH_API EtchPopupDesc etch_popup_desc_default(void) {
    const etch::PopupDesc d{};
    return {d.id, d.trigger_id, from_cpp(d.width), from_cpp(d.height), from_cpp(d.padding),
            d.gap, from_cpp(d.background), from_cpp(d.border_color), from_cpp(d.radius), d.z_index,
            d.attach_to_trigger, d.match_trigger_width, d.scroll_y, d.backdrop_blur,
            d.shadow_blur, from_cpp(d.shadow_color), d.flip_on_overflow};
}

ETCH_API EtchMenuBarDesc etch_menu_bar_desc_default(void) {
    const etch::MenuBarDesc d{};
    return {d.id, from_cpp(d.width), d.height_px, from_cpp(d.background)};
}

ETCH_API EtchMenuDesc etch_menu_desc_default(void) {
    const etch::MenuDesc d{};
    return {d.id, from_cpp(d.label), d.disabled};
}

ETCH_API EtchMenuItemDesc etch_menu_item_desc_default(void) {
    const etch::MenuItemDesc d{};
    return {d.id, from_cpp(d.label), from_cpp(d.shortcut_hint), d.icon_id, d.checked,
            d.disabled, d.separator_after};
}

ETCH_API EtchSplitDesc etch_split_desc_default(void) {
    const etch::SplitDesc d{};
    return {static_cast<uint32_t>(sizeof(EtchSplitDesc)),
            d.id, static_cast<EtchAxis>(d.axis), from_cpp(d.width), from_cpp(d.height),
            d.default_ratio, d.min_first_px, d.min_second_px, d.divider_size_px,
            from_cpp(d.divider_color), from_cpp(d.divider_hover_color),
            from_cpp(d.divider_active_color), d.divider_hit_px, d.keyboard_step_px,
            static_cast<EtchSplitSizing>(d.sizing), d.extent_px, d.default_extent_px,
            d.show_grip, from_cpp(d.grip_color), d.hover_highlight_px};
}

ETCH_API EtchVirtualListDesc etch_virtual_list_desc_default(void) {
    const etch::VirtualListDesc d{};
    return {static_cast<uint32_t>(sizeof(EtchVirtualListDesc)),
            d.id, d.item_count, d.item_height_px, d.row_gap_px, d.overscan_rows,
            from_cpp(d.width), from_cpp(d.height), from_cpp(d.padding), from_cpp(d.background),
            from_cpp(d.border_color), d.border_width, from_cpp(d.radius), d.show_scrollbar,
            nullptr, d.measure_user_data, d.variable_height, d.scroll_fade_px, d.stick_to_bottom};
}

ETCH_API EtchDockPaneDesc etch_dock_pane_desc_default(void) {
    const etch::DockPaneDesc d{};
    return {d.id, from_cpp(d.title), d.icon_id, d.closable, d.default_open,
            d.pinned, d.dirty, from_cpp(d.badge_color)};
}

ETCH_API EtchDockSpaceDesc etch_dock_space_desc_default(void) {
    const etch::DockSpaceDesc d{};
    return {d.id, from_cpp(d.width), from_cpp(d.height), d.tab_bar_height_px, d.divider_size_px,
            from_cpp(d.background), from_cpp(d.pane_background), from_cpp(d.pane_border_color),
            d.pane_border_width, from_cpp(d.pane_radius), from_cpp(d.tab_bar_bg),
            from_cpp(d.tab_active_bg), from_cpp(d.tab_hover_bg), from_cpp(d.tab_accent)};
}

ETCH_API EtchTableColumnDesc etch_table_column_desc_default(void) {
    const etch::TableColumnDesc d{};
    return {d.id, from_cpp(d.title), static_cast<EtchTableColumnSizing>(d.sizing),
            d.width, d.min_width, d.max_width, static_cast<EtchAlignX>(d.align_x),
            d.resizable, d.sortable};
}

ETCH_API EtchTableDesc etch_table_desc_default(void) {
    const etch::TableDesc d{};
    return {static_cast<uint32_t>(sizeof(EtchTableDesc)),
            d.id, nullptr, 0u, d.item_count, d.header_height_px, d.row_height_px, d.row_gap_px,
            d.overscan_rows, d.default_sort_column, d.default_sort_descending, d.cell_padding_x,
            from_cpp(d.width), from_cpp(d.height), from_cpp(d.padding), from_cpp(d.background),
            from_cpp(d.header_background), from_cpp(d.zebra_color), from_cpp(d.hover_row_color),
            from_cpp(d.border_color), d.border_width, from_cpp(d.radius), d.show_scrollbar, d.show_column_lines, d.header_font_size_px, d.selected_row, from_cpp(d.selected_row_color),d.horizontal_scroll,d.keyboard_navigation,{d.selection.focused_row,d.selection.anchor_row,d.selection.selected_words,d.selection.word_count},d.scroll_fade_px};
}

ETCH_API EtchTreeNodeDesc etch_tree_node_desc_default(void) {
    const etch::TreeNodeDesc d{};
    return {d.id, from_cpp(d.label), d.icon_id, from_cpp(d.icon_color), from_cpp(d.text_color),
            from_cpp(d.selected_bg), from_cpp(d.selected_accent), from_cpp(d.hover_bg),
            d.row_height_px, d.font_size_px, d.indent_px, d.font_id, d.default_open,
            d.selected, d.leaf, d.show_indent_guide};
}

ETCH_API EtchCanvasDesc etch_canvas_desc_default(void) {
    const etch::CanvasDesc d{};
    return {static_cast<uint32_t>(sizeof(EtchCanvasDesc)),
            d.id, from_cpp(d.width), from_cpp(d.height), from_cpp(d.default_pan), d.default_zoom,
            d.min_zoom, d.max_zoom, d.grid_step_world, d.major_grid_subdiv, from_cpp(d.background),
            from_cpp(d.minor_grid_color), from_cpp(d.major_grid_color), from_cpp(d.border_color),
            d.border_width, from_cpp(d.radius), d.show_grid, d.interactive, d.marquee_select,
            from_cpp(d.marquee_fill), from_cpp(d.marquee_border)};
}

ETCH_API EtchCanvasWireDesc etch_canvas_wire_desc_default(void) {
    const etch::CanvasWireDesc d{};
    return {from_cpp(d.from_world), from_cpp(d.to_world), from_cpp(d.color_start),
            from_cpp(d.color_end), from_cpp(d.glow_color), d.thickness_world, d.glow_world,
            d.tangent_bias, d.z_index, static_cast<EtchAxis>(d.axis)};
}

ETCH_API EtchCanvasNodeDesc etch_canvas_node_desc_default(void) {
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

ETCH_API EtchArena etch_arena_create(size_t capacity_bytes) {
    return from_cpp_arena(etch::arena_create(capacity_bytes));
}

ETCH_API EtchArena etch_arena_from_buffer(void* buffer, size_t capacity_bytes) {
    return from_cpp_arena(etch::arena_from_buffer(buffer, capacity_bytes));
}

ETCH_API void etch_arena_destroy(EtchArena* arena) {
    if (!arena) return;
    etch::Arena a = to_cpp_arena(arena);
    etch::arena_destroy(&a);
    sync_c_arena(arena, a);
}

ETCH_API void etch_arena_reset(EtchArena* arena) {
    if (!arena) return;
    arena->offset = 0u;
}

ETCH_API size_t etch_arena_mark(const EtchArena* arena) {
    return arena ? arena->offset : 0u;
}

ETCH_API void etch_arena_rewind(EtchArena* arena, size_t saved_mark) {
    if (!arena || saved_mark > arena->offset) return;
    arena->offset = saved_mark;
}

ETCH_API void* etch_arena_alloc_bytes(EtchArena* arena, size_t size, size_t align) {
    if (!arena || align == 0u || (align & (align - 1u)) != 0u) return nullptr;
    etch::Arena a = to_cpp_arena(arena);
    void* p = a.alloc_bytes(size, align);
    sync_c_arena(arena, a);
    return p;
}

ETCH_API EtchStr etch_arena_push_str(EtchArena* arena, const char* s, size_t len) {
    if (!arena) return {};
    etch::Arena a = to_cpp_arena(arena);
    const etch::Str out = a.push_str(s, len);
    sync_c_arena(arena, a);
    return from_cpp(out);
}

ETCH_API void* etch_frame_arena_alloc(EtchContext* ctx, size_t size, size_t align) {
    etch::Arena* a = etch::frame_arena(to_ctx(ctx));
    if (!a || align == 0u || (align & (align - 1u)) != 0u) return nullptr;
    return a->alloc_bytes(size, align);
}

ETCH_API EtchStr etch_frame_arena_push_str(EtchContext* ctx, const char* s, size_t len) {
    etch::Arena* a = etch::frame_arena(to_ctx(ctx));
    return a ? from_cpp(a->push_str(s, len)) : EtchStr{};
}

ETCH_API EtchSystemTheme etch_system_theme(void) { return static_cast<EtchSystemTheme>(etch::system_theme()); }
ETCH_API bool etch_system_dark_mode(bool default_when_unknown) { return etch::system_dark_mode(default_when_unknown); }
ETCH_API EtchTheme etch_theme_dark(void) { return from_cpp(etch::Theme::dark()); }
ETCH_API EtchTheme etch_theme_light(void) { return from_cpp(etch::Theme::light()); }
ETCH_API EtchColor etch_color_oklch(float L, float C, float hue_deg, float alpha) { return from_cpp(etch::Color::oklch(L,C,hue_deg,alpha)); }
ETCH_API EtchColor etch_color_lerp_oklab(EtchColor a, EtchColor b, float t) { return from_cpp(etch::Color::lerp_oklab(to_cpp(a),to_cpp(b),t)); }
ETCH_API EtchTheme etch_theme_from_seed(EtchColor seed, bool dark_mode, float chroma_scale) { return from_cpp(etch::Theme::from_seed(to_cpp(seed),dark_mode,chroma_scale)); }
ETCH_API EtchTheme etch_theme_lerp_oklab(const EtchTheme* a, const EtchTheme* b, float t) {
    const etch::Theme ta = a ? to_cpp(*a) : etch::Theme::dark();
    const etch::Theme tb = b ? to_cpp(*b) : etch::Theme::dark();
    return from_cpp(etch::Theme::lerp_oklab(ta, tb, t));
}
ETCH_API EtchSpringSpec etch_spring_spec_snappy(void) { return from_cpp(etch::SpringSpec::snappy()); }
ETCH_API EtchSpringSpec etch_spring_spec_fluid(void) { return from_cpp(etch::SpringSpec::fluid()); }
ETCH_API EtchSpringSpec etch_spring_spec_bouncy(void) { return from_cpp(etch::SpringSpec::bouncy()); }
ETCH_API EtchSpringSpec etch_spring_spec_gentle(void) { return from_cpp(etch::SpringSpec::gentle()); }
ETCH_API EtchEasingSpec etch_easing_linear(void) { return from_cpp(etch::EasingSpec::linear()); }
ETCH_API EtchEasingSpec etch_easing_in(void) { return from_cpp(etch::EasingSpec::ease_in()); }
ETCH_API EtchEasingSpec etch_easing_out(void) { return from_cpp(etch::EasingSpec::ease_out()); }
ETCH_API EtchEasingSpec etch_easing_in_out(void) { return from_cpp(etch::EasingSpec::ease_in_out()); }
ETCH_API EtchEasingSpec etch_easing_cubic_bezier(float x1, float y1, float x2, float y2) {
    return from_cpp(etch::EasingSpec::cubic_bezier(x1, y1, x2, y2));
}
ETCH_API float etch_eval_easing(EtchEasingSpec spec, float t) {
    return etch::eval_easing(to_cpp(spec), t);
}
ETCH_API float etch_tween(uint32_t id, float target, float duration_sec, EtchEasingSpec spec, float delay_sec) {
    return etch::tween(id, target, duration_sec, to_cpp(spec), delay_sec);
}
ETCH_API float etch_stagger_delay(uint32_t index, float step_sec, float max_delay_sec) {
    return etch::stagger_delay(index, step_sec, max_delay_sec);
}
ETCH_API EtchTheme etch_theme_high_contrast(void) { return from_cpp(etch::Theme::high_contrast()); }
ETCH_API EtchTheme etch_theme_industrial(void) { return from_cpp(etch::Theme::industrial()); }
ETCH_API void etch_set_theme(const EtchTheme* theme, EtchContext* ctx) { if (theme) etch::set_theme(to_cpp(*theme), to_ctx(ctx)); }
ETCH_API EtchTheme etch_get_theme(const EtchContext* ctx) { return from_cpp(etch::theme(to_ctx(ctx))); }
ETCH_API void etch_push_theme(const EtchTheme* theme) { if (theme) etch::push_theme(to_cpp(*theme)); }
ETCH_API void etch_pop_theme(void) { etch::pop_theme(); }

/* ============================================================================
 * Context Lifecycle, Fonts, Icons, Events, Frames & Window Management
 * ============================================================================ */

ETCH_API EtchContext* etch_create_context(const EtchContextDesc* desc) {
    etch::ContextDesc cpp_desc{};
    EtchContextDesc scratch{};
    const EtchContextDesc* d = nullptr;
    if (desc) {
        d = resolve_c_desc(desc, &scratch, etch_context_desc_default);
        if (!d) return nullptr;
        cpp_desc = {d->max_layout_elements, d->window, d->frame_arena_bytes,
                    d->max_prims_per_frame, d->font_path, d->font_size_px,
                    d->enable_gpu_debug, d->headless, d->gpu_driver,
                    nullptr, nullptr, d->enable_os_a11y, d->respect_reduced_motion,
                    d->prefer_high_performance_gpu, d->lcd_subpixel_text};
    }
    etch::Context* ctx = etch::create_context(cpp_desc);
    if (ctx && d && d->on_window_refresh) {
        etch_set_window_refresh_callback(from_ctx(ctx), d->on_window_refresh, d->refresh_user_data);
    }
    return from_ctx(ctx);
}

ETCH_API void etch_destroy_context(EtchContext* ctx) { etch::destroy_context(to_ctx(ctx)); }
ETCH_API EtchContext* etch_current_context(void) { return from_ctx(etch::current_context()); }
ETCH_API void etch_set_current_context(EtchContext* ctx) { etch::set_current_context(to_ctx(ctx)); }
ETCH_API struct SDL_GPUDevice* etch_gpu_device(EtchContext* ctx) { return etch::gpu_device(to_ctx(ctx)); }
ETCH_API EtchGpuStatus etch_gpu_status(const EtchContext* ctx) {
    return static_cast<EtchGpuStatus>(etch::gpu_status(to_ctx(ctx)));
}
ETCH_API const char* etch_gpu_driver_name(const EtchContext* ctx) {
    return etch::gpu_driver_name(to_ctx(ctx));
}
ETCH_API bool etch_retry_gpu_initialization(EtchContext* ctx, const char* driver) {
    return etch::retry_gpu_initialization(to_ctx(ctx), driver);
}
ETCH_API bool etch_recreate_gpu_device(EtchContext* ctx, const char* driver) {
    return etch::recreate_gpu_device(to_ctx(ctx), driver);
}
ETCH_API void etch_mark_gpu_device_lost(EtchContext* ctx) {
    etch::mark_gpu_device_lost(to_ctx(ctx));
}
ETCH_API uint64_t etch_gpu_device_generation(const EtchContext* ctx) {
    return etch::gpu_device_generation(to_ctx(ctx));
}
ETCH_API void etch_set_gpu_device_reset_callback(EtchContext* ctx, EtchGpuDeviceResetFn fn, void* user_data) {
    etch::c_bridge_set_gpu_device_reset(to_ctx(ctx), fn, user_data);
}
ETCH_API bool etch_begin_async_readback(EtchContext* ctx, uint32_t width, uint32_t height, EtchColor clear_color) {
    return etch::begin_async_readback(to_ctx(ctx), width, height, to_cpp(clear_color));
}
ETCH_API int32_t etch_poll_async_readback(EtchContext* ctx, bool (*receive)(const uint8_t*, uint32_t, uint32_t, uint32_t, void*), void* user_data) {
    return etch::poll_async_readback(to_ctx(ctx), receive, user_data);
}
ETCH_API void etch_set_trace_enabled(EtchContext* ctx, bool enabled) { etch::set_trace_enabled(to_ctx(ctx), enabled); }
ETCH_API bool etch_trace_enabled(const EtchContext* ctx) { return etch::trace_enabled(to_ctx(ctx)); }
ETCH_API uint32_t etch_trace_sample_count(const EtchContext* ctx) { return etch::trace_sample_count(to_ctx(ctx)); }
ETCH_API bool etch_trace_get_sample(const EtchContext* ctx, uint32_t index, EtchFrameTraceSample* out_sample) {
    if (!out_sample) return false;
    etch::FrameTraceSample s = {};
    if (!etch::trace_get_sample(to_ctx(ctx), index, &s)) return false;
    *out_sample = {s.frame_index, s.layout_us, s.text_shape_us, s.prim_build_us, s.gpu_upload_us, s.gpu_submit_us,
                   s.ui_draw_calls, s.total_draw_calls, s.blur_passes, s.filter_passes, s.prim_count,
                   s.atlas_evictions, s.shape_cache_hits, s.shape_cache_misses,
                   s.working_set_bytes, s.private_commit_bytes, s.estimated_vram_bytes};
    return true;
}
ETCH_API void etch_trace_clear(EtchContext* ctx) { etch::trace_clear(to_ctx(ctx)); }
ETCH_API uint32_t etch_trace_export_json(const EtchContext* ctx, char* out_buf, uint32_t cap) {
    return etch::trace_export_json(to_ctx(ctx), out_buf, cap);
}
ETCH_API bool etch_trace_compute_percentiles(const EtchContext* ctx, EtchTracePercentiles* out_summary) {
    if (!out_summary) return false;
    etch::TracePercentiles p = {};
    if (!etch::trace_compute_percentiles(to_ctx(ctx), &p)) return false;
    *out_summary = {p.sample_count, p.layout_p50_us, p.layout_p95_us, p.layout_p99_us,
                    p.gpu_p50_us, p.gpu_p95_us, p.gpu_p99_us, p.peak_working_set_bytes, p.peak_vram_bytes};
    return true;
}
ETCH_API EtchSystemPreferences etch_system_preferences(const EtchContext* ctx) {
    const etch::SystemPreferences p = etch::system_preferences(to_ctx(ctx));
    EtchSystemPreferences out = {};
    out.theme = static_cast<EtchSystemTheme>(p.theme);
    out.high_contrast = p.high_contrast;
    out.reduced_motion = p.reduced_motion;
    out.text_scale = p.text_scale;
    out.layout_direction = static_cast<EtchLayoutDirection>(p.layout_direction);
    std::memcpy(out.locale, p.locale, sizeof(out.locale));
    return out;
}
ETCH_API void etch_set_system_preferences_override(EtchContext* ctx, const EtchSystemPreferences* override_prefs) {
    if (!override_prefs) {
        etch::set_system_preferences_override(to_ctx(ctx), nullptr);
        return;
    }
    etch::SystemPreferences p = {};
    p.theme = static_cast<etch::SystemTheme>(override_prefs->theme);
    p.high_contrast = override_prefs->high_contrast;
    p.reduced_motion = override_prefs->reduced_motion;
    p.text_scale = override_prefs->text_scale;
    p.layout_direction = static_cast<etch::LayoutDirection>(override_prefs->layout_direction);
    std::memcpy(p.locale, override_prefs->locale, sizeof(p.locale));
    p.locale[sizeof(p.locale) - 1u] = '\0';
    etch::set_system_preferences_override(to_ctx(ctx), &p);
}
ETCH_API void etch_apply_system_preferences(EtchContext* ctx, bool auto_theme, bool auto_text_scale, bool auto_rtl) {
    etch::apply_system_preferences(to_ctx(ctx), auto_theme, auto_text_scale, auto_rtl);
}
ETCH_API float etch_active_text_scale(const EtchContext* ctx) {
    return etch::active_text_scale(to_ctx(ctx));
}
ETCH_API EtchLayoutDirection etch_active_layout_direction(const EtchContext* ctx) {
    return static_cast<EtchLayoutDirection>(etch::active_layout_direction(to_ctx(ctx)));
}
ETCH_API EtchPointerState etch_pointer_state(const EtchContext* ctx) {
    const etch::PointerState p = etch::pointer_state(to_ctx(ctx));
    return {static_cast<EtchPointerKind>(p.kind), p.pointer_id, p.active_touches,
            p.pressure, p.tilt_x, p.tilt_y, p.pinch_delta, p.precise_scroll, p.pen_eraser};
}
ETCH_API EtchPointerGesturePhase etch_pointer_gesture_phase(const EtchContext* ctx) {
    return static_cast<EtchPointerGesturePhase>(etch::pointer_gesture_phase(to_ctx(ctx)));
}
ETCH_API uint32_t etch_active_touch_points(EtchTouchPoint* out_points, uint32_t max_points, const EtchContext* ctx) {
    static_assert(sizeof(EtchTouchPoint) == sizeof(etch::TouchPoint));
    return etch::active_touch_points(reinterpret_cast<etch::TouchPoint*>(out_points), max_points, to_ctx(ctx));
}

ETCH_API uint16_t etch_load_system_font(EtchContext* ctx, EtchStr family_name, EtchFontWeight weight, uint16_t slot_hint) {
    return etch::load_system_font(to_ctx(ctx), to_cpp(family_name), static_cast<etch::FontWeight>(weight), slot_hint);
}

ETCH_API uint16_t etch_load_font_file(EtchContext* ctx, const char* path, uint16_t slot_hint, int32_t face_index) {
    return etch::load_font_file(to_ctx(ctx), path, slot_hint, face_index);
}

ETCH_API uint16_t etch_load_font_memory(EtchContext* ctx, const void* data, size_t size_bytes, uint16_t slot_hint, int32_t face_index) {
    return etch::load_font_memory(to_ctx(ctx), data, size_bytes, slot_hint, face_index);
}

ETCH_API uint16_t etch_create_font_variation(EtchContext* ctx, uint16_t base_font_id, const EtchFontAxisValue* axes, uint32_t axis_count, const EtchFontFeature* features, uint32_t feature_count) {
    return etch::create_font_variation(
        to_ctx(ctx),
        base_font_id,
        view_compatible_span<etch::FontAxisValue>(axes, axes ? axis_count : 0u),
        view_compatible_span<etch::FontFeature>(features, features ? feature_count : 0u));
}

ETCH_API bool etch_set_font_tabular_numbers(EtchContext* ctx, uint16_t font_id, bool enabled) {
    return etch::set_font_tabular_numbers(to_ctx(ctx), font_id, enabled);
}
ETCH_API bool etch_set_font_fallback(EtchContext* ctx, uint16_t primary_font_id, uint16_t fallback_font_id) {
    return etch::set_font_fallback(to_ctx(ctx), primary_font_id, fallback_font_id);
}
ETCH_API bool etch_set_font_scale_adjust(EtchContext* ctx, uint16_t font_id, float scale_factor) {
    return etch::set_font_scale_adjust(to_ctx(ctx), font_id, scale_factor);
}
ETCH_API float etch_font_scale_adjust(const EtchContext* ctx, uint16_t font_id) {
    return etch::font_scale_adjust(to_ctx(ctx), font_id);
}
ETCH_API void etch_set_hinted_small_text(EtchContext* ctx, bool enabled) {
    etch::set_hinted_small_text(to_ctx(ctx), enabled);
}
ETCH_API bool etch_hinted_small_text(const EtchContext* ctx) {
    return etch::hinted_small_text(to_ctx(ctx));
}
ETCH_API void etch_set_lcd_subpixel_text(EtchContext* ctx, bool enabled) {
    etch::set_lcd_subpixel_text(to_ctx(ctx), enabled);
}
ETCH_API bool etch_lcd_subpixel_text(const EtchContext* ctx) {
    return etch::lcd_subpixel_text(to_ctx(ctx));
}
ETCH_API bool etch_is_font_loaded(const EtchContext* ctx, uint16_t font_id) {
    return etch::is_font_loaded(to_ctx(ctx), font_id);
}

ETCH_API EtchSvgStrokeDesc etch_svg_stroke_desc_default(void) {
    const etch::SvgStrokeDesc d{};
    return {{d.view_box.x, d.view_box.y, d.view_box.z, d.view_box.w}, d.stroke_width, static_cast<EtchStrokeCap>(d.cap),
            static_cast<EtchStrokeJoin>(d.join), d.miter_limit, d.slot_hint};
}

ETCH_API EtchSvgIconLayer etch_svg_icon_layer_default(void) {
    const etch::SvgIconLayer l{};
    return {from_cpp(l.path_d), l.stroked, etch_svg_stroke_desc_default(), l.color_slot, l.opacity};
}

ETCH_API uint16_t etch_register_svg_stroke_icon(EtchContext* ctx, EtchStr path, const EtchSvgStrokeDesc* d) {
    if (!d) return etch::register_svg_stroke_icon(to_ctx(ctx), to_cpp(path));
    if (d->cap < ETCH_STROKE_CAP_BUTT || d->cap > ETCH_STROKE_CAP_SQUARE ||
        d->join < ETCH_STROKE_JOIN_MITER || d->join > ETCH_STROKE_JOIN_BEVEL) return ETCH_INVALID_ICON;
    return etch::register_svg_stroke_icon(to_ctx(ctx), to_cpp(path),
        {to_cpp(d->view_box), d->stroke_width, static_cast<etch::StrokeCap>(d->cap),
         static_cast<etch::StrokeJoin>(d->join), d->miter_limit, d->slot_hint});
}

ETCH_API uint16_t etch_register_svg_icon(EtchContext* ctx, EtchStr svg_path_d, EtchVec4 view_box, uint16_t slot_hint) {
    return etch::register_svg_icon(to_ctx(ctx), to_cpp(svg_path_d), to_cpp(view_box), slot_hint);
}

ETCH_API uint16_t etch_register_svg_layered_icon(EtchContext* ctx, const EtchSvgIconLayer* layers, uint32_t layer_count, EtchVec4 view_box, uint16_t slot_hint) {
    if (!layers || layer_count == 0u || layer_count > 4u) return ETCH_INVALID_ICON;
    etch::SvgIconLayer cpp_layers[4] = {};
    for (uint32_t i = 0u; i < layer_count; ++i) {
        const EtchSvgIconLayer& l = layers[i];
        if (l.stroked && (l.stroke.cap < ETCH_STROKE_CAP_BUTT || l.stroke.cap > ETCH_STROKE_CAP_SQUARE ||
                          l.stroke.join < ETCH_STROKE_JOIN_MITER || l.stroke.join > ETCH_STROKE_JOIN_BEVEL)) {
            return ETCH_INVALID_ICON;
        }
        cpp_layers[i] = {
            to_cpp(l.path_d),
            l.stroked,
            {to_cpp(l.stroke.view_box), l.stroke.stroke_width, static_cast<etch::StrokeCap>(l.stroke.cap),
             static_cast<etch::StrokeJoin>(l.stroke.join), l.stroke.miter_limit, l.stroke.slot_hint},
            l.color_slot,
            l.opacity,
        };
    }
    return etch::register_svg_layered_icon(
        to_ctx(ctx),
        std::span<const etch::SvgIconLayer>(cpp_layers, layer_count),
        to_cpp(view_box),
        slot_hint);
}

ETCH_API uint16_t etch_register_svg_duotone_icon(EtchContext* ctx, EtchStr fill_path_d, EtchStr stroke_path_d, const EtchSvgStrokeDesc* stroke_desc, float fill_opacity) {
    etch::SvgStrokeDesc s{};
    if (stroke_desc) {
        if (stroke_desc->cap < ETCH_STROKE_CAP_BUTT || stroke_desc->cap > ETCH_STROKE_CAP_SQUARE ||
            stroke_desc->join < ETCH_STROKE_JOIN_MITER || stroke_desc->join > ETCH_STROKE_JOIN_BEVEL) {
            return ETCH_INVALID_ICON;
        }
        s = {to_cpp(stroke_desc->view_box), stroke_desc->stroke_width, static_cast<etch::StrokeCap>(stroke_desc->cap),
             static_cast<etch::StrokeJoin>(stroke_desc->join), stroke_desc->miter_limit, stroke_desc->slot_hint};
    }
    return etch::register_svg_duotone_icon(to_ctx(ctx), to_cpp(fill_path_d), to_cpp(stroke_path_d), s, fill_opacity);
}

ETCH_API bool etch_is_icon_loaded(const EtchContext* ctx, uint16_t icon_id) {
    return etch::is_icon_loaded(to_ctx(ctx), icon_id);
}

ETCH_API void etch_process_event(EtchContext* ctx, const union SDL_Event* event) {
    if (ctx && event) etch::process_event(to_ctx(ctx), *event);
}

ETCH_API bool etch_needs_redraw(const EtchContext* ctx) { return etch::needs_redraw(to_ctx(ctx)); }
ETCH_API void etch_request_redraw(EtchContext* ctx) { etch::request_redraw(to_ctx(ctx)); }
ETCH_API void etch_wake_up(EtchContext* ctx) { etch::wake_up(to_ctx(ctx)); }
ETCH_API bool etch_wait_events(EtchContext* ctx, union SDL_Event* first_event_out, int32_t timeout_ms) {
    return etch::wait_events(to_ctx(ctx), first_event_out, timeout_ms);
}

ETCH_API void etch_begin_frame(EtchContext* ctx, const EtchFrameDesc* desc) {
    if (!ctx) return;
    etch::FrameDesc f{};
    EtchFrameDesc scratch{};
    if (desc) {
        const EtchFrameDesc* d = resolve_c_desc(desc, &scratch, etch_frame_desc_default);
        if (!d) return;
        f = {d->viewport_w, d->viewport_h, d->dpi_scale, d->dt_seconds,
             to_cpp(d->pointer_pos), d->pointer_down, to_cpp(d->scroll_delta),
             d->right_pointer_down, d->pointer_clicks, d->key_mods, d->key_pressed_code, d->pixel_snap, d->bloom_intensity, d->bloom_threshold};
    }
    etch::begin_frame(to_ctx(ctx), f);
}

ETCH_API void etch_end_frame(EtchContext* ctx) { etch::end_frame(to_ctx(ctx)); }
ETCH_API bool etch_frame_in_progress(const EtchContext* ctx) { return etch::frame_in_progress(to_ctx(ctx)); }

ETCH_API void etch_begin_window_frame(EtchContext* ctx, struct SDL_Window* window, const EtchWindowFrameDesc* desc) {
    etch::WindowFrameDesc w{};
    EtchWindowFrameDesc scratch{};
    if (desc) {
        const EtchWindowFrameDesc* d = resolve_c_desc(desc, &scratch, etch_window_frame_desc_default);
        if (!d) return;
        w = {d->dt, d->min_scale, d->pixel_snap, d->bloom_intensity, d->bloom_threshold};
    }
    etch::begin_window_frame(to_ctx(ctx), window, w);
}

ETCH_API void etch_end_window_frame(EtchContext* ctx) { etch::end_window_frame(to_ctx(ctx)); }
ETCH_API bool etch_needs_window_redraw(const EtchContext* ctx, struct SDL_Window* window) { return etch::needs_window_redraw(to_ctx(ctx), window); }
ETCH_API void etch_request_window_redraw(EtchContext* ctx, struct SDL_Window* window) { etch::request_window_redraw(to_ctx(ctx), window); }

ETCH_API uint32_t etch_frame_prim_count(const EtchContext* ctx) { return static_cast<uint32_t>(etch::frame_prims(to_ctx(ctx)).size()); }
ETCH_API bool etch_get_frame_prim(const EtchContext* ctx, uint32_t index, EtchPrim* out_prim) {
    const auto p = etch::frame_prims(to_ctx(ctx));
    if (!out_prim || index >= p.size()) return false;
    std::memcpy(out_prim, &p[index], sizeof(EtchPrim));
    return true;
}

ETCH_API EtchFrameStats etch_frame_stats(const EtchContext* ctx) {
    const etch::FrameStats s = etch::frame_stats(to_ctx(ctx));
    return {s.a11y_node_count, s.prim_count, s.prim_capacity, s.draw_call_count, s.blur_pass_count,
            s.slug_glyph_count, s.slug_curve_count, s.dynamic_slug_curve_count,
            s.arena_used_bytes, s.arena_peak_bytes, s.layout_ms, s.gpu_submit_ms,
            s.active_animations, s.idle_sleeping, s.ime_cursor_offset_win,
            s.sprite_count, s.sprite_atlas_size, s.cached_subtree_hits};
}

ETCH_API bool etch_render_window(EtchContext* ctx, EtchColor clear_color, struct SDL_Window* window_override) {
    return etch::render_window(to_ctx(ctx), to_cpp(clear_color), window_override);
}
ETCH_API EtchRenderTargetDesc etch_render_target_desc_default(void) {
    EtchRenderTargetDesc desc = {};
    desc.struct_size = sizeof(desc);
    desc.clear_color = etch_color_theme_auto();
    return desc;
}
ETCH_API bool etch_render_to_texture(EtchContext* ctx, const EtchRenderTargetDesc* input) {
    if (!input) return false;
    EtchRenderTargetDesc scratch = {};
    const auto* desc = resolve_c_desc(input, &scratch, etch_render_target_desc_default);
    if (!desc) return false;
    return etch::render_to_texture(to_ctx(ctx), {
        .texture = desc->texture, .width = desc->width, .height = desc->height,
        .sdl_texture_format = desc->sdl_texture_format, .clear_color = to_cpp(desc->clear_color),
    });
}
ETCH_API void etch_release_window(EtchContext* ctx, struct SDL_Window* window) { etch::release_window(to_ctx(ctx), window); }
ETCH_API uint32_t etch_claimed_window_count(const EtchContext* ctx) { return etch::claimed_window_count(to_ctx(ctx)); }
ETCH_API EtchReadPixelsDesc etch_read_pixels_desc_default(void) {
    EtchReadPixelsDesc desc = {};
    desc.struct_size = sizeof(desc);
    desc.clear_color = etch_color_theme_auto();
    return desc;
}
ETCH_API bool etch_render_headless_pixels(EtchContext* ctx, const EtchReadPixelsDesc* input) {
    if (!input) return false;
    EtchReadPixelsDesc scratch = {};
    const auto* desc = resolve_c_desc(input, &scratch, etch_read_pixels_desc_default);
    if (!desc) return false;
    return etch::render_headless_pixels(to_ctx(ctx), {
        .frame_width = desc->frame_width, .frame_height = desc->frame_height,
        .x = desc->x, .y = desc->y, .width = desc->width, .height = desc->height,
        .clear_color = to_cpp(desc->clear_color), .receive = desc->receive, .user_data = desc->user_data,
    });
}
ETCH_API bool etch_render_headless_png(EtchContext* ctx, uint32_t width, uint32_t height, const char* out_png_path, EtchColor clear_color) {
    return etch::render_headless_png(to_ctx(ctx), width, height, out_png_path, to_cpp(clear_color));
}

ETCH_API void etch_set_window_refresh_callback(EtchContext* ctx_arg, EtchWindowRefreshFn fn, void* user_data) {
    etch::Context* ctx = ctx_arg ? to_ctx(ctx_arg) : etch::current_context();
    etch::c_bridge_set_window_refresh(ctx, fn, user_data);
}

ETCH_API void etch_set_window_chrome(const EtchWindowChromeDesc* desc, struct SDL_Window* window) {
    if (!desc) return;
    EtchWindowChromeDesc scratch{};
    const EtchWindowChromeDesc* d = resolve_c_desc(desc, &scratch, etch_window_chrome_desc_default);
    if (!d) return;
    etch::set_window_chrome({d->enabled, d->resizable, d->border_thickness, d->corner_size,
                             d->macos_traffic_lights, d->traffic_light_inset_px, static_cast<etch::WindowBackdrop>(d->backdrop)}, window);
}
ETCH_API void etch_window_drag_region(EtchRect rect_logical) { etch::window_drag_region(to_cpp(rect_logical)); }
ETCH_API void etch_window_maximize_button_region(EtchRect rect_logical) { etch::window_maximize_button_region(to_cpp(rect_logical)); }
ETCH_API void etch_window_maximize_button_element(uint32_t element_id) { etch::window_maximize_button_region(element_id); }
ETCH_API void etch_window_client_region(EtchRect rect_logical) { etch::window_client_region(to_cpp(rect_logical)); }
ETCH_API EtchWindowHit etch_evaluate_window_hit(const EtchContext* ctx, EtchVec2 logical_pos, struct SDL_Window* window) {
    return static_cast<EtchWindowHit>(etch::evaluate_window_hit(to_ctx(ctx), to_cpp(logical_pos), window));
}
ETCH_API void etch_minimize_window(struct SDL_Window* window) { etch::minimize_window(window); }
ETCH_API void etch_maximize_window(struct SDL_Window* window) { etch::maximize_window(window); }
ETCH_API void etch_restore_window(struct SDL_Window* window) { etch::restore_window(window); }
ETCH_API bool etch_is_window_maximized(const EtchContext* ctx, struct SDL_Window* window) { return etch::is_window_maximized(to_ctx(ctx), window); }
ETCH_API bool etch_is_window_minimized(const EtchContext* ctx, struct SDL_Window* window) { return etch::is_window_minimized(to_ctx(ctx), window); }
ETCH_API bool etch_set_window_fullscreen(bool fullscreen, struct SDL_Window* window) { return etch::set_window_fullscreen(fullscreen, window); }
ETCH_API bool etch_is_window_fullscreen(const EtchContext* ctx, struct SDL_Window* window) { return etch::is_window_fullscreen(to_ctx(ctx), window); }
ETCH_API EtchPadding etch_window_safe_insets(const EtchContext* ctx, struct SDL_Window* window) { return from_cpp(etch::window_safe_insets(to_ctx(ctx), window)); }

/* ============================================================================
 * Accessibility (A11y) Snapshot, Actions & Native OS Bridge
 * ============================================================================ */

ETCH_API void etch_set_a11y_override(const EtchA11yOverrideDesc* desc) {
    if (!desc) return;
    etch::set_a11y_override({desc->element_id, static_cast<etch::A11yRole>(desc->role),
                             to_cpp(desc->label), to_cpp(desc->value_text),
                             desc->add_states, desc->clear_states, desc->actions_mask,
                             desc->has_role, desc->has_label, desc->has_value_text,
                             desc->has_actions_mask});
}

ETCH_API void etch_emit_a11y_node(uint32_t element_id, EtchA11yRole role, EtchStr label, EtchStr value_text,
    uint32_t state_flags, uint32_t actions_mask, float value_num, EtchRect bounds) {
    etch::emit_a11y_node(element_id, static_cast<etch::A11yRole>(role), to_cpp(label), to_cpp(value_text),
                         state_flags, actions_mask, value_num, to_cpp(bounds));
}

ETCH_API void etch_a11y_announce(EtchStr message, EtchA11yLiveMode priority, EtchContext* ctx, struct SDL_Window* window) {
    etch::announce_a11y(to_cpp(message), static_cast<etch::A11yLiveMode>(priority), to_ctx(ctx), window);
}

ETCH_API EtchStr etch_last_a11y_announcement(EtchA11yLiveMode* out_priority, uint64_t* out_seq, const EtchContext* ctx, struct SDL_Window* window) {
    etch::A11yLiveMode prio = etch::A11yLiveMode::Off;
    const etch::Str s = etch::last_a11y_announcement(&prio, out_seq, to_ctx(ctx), window);
    if (out_priority) *out_priority = static_cast<EtchA11yLiveMode>(prio);
    return from_cpp(s);
}

ETCH_API uint32_t etch_a11y_node_count(const EtchContext* ctx, struct SDL_Window* window) {
    return static_cast<uint32_t>(etch::frame_a11y_nodes(to_ctx(ctx), window).size());
}

ETCH_API bool etch_get_a11y_node(uint32_t index, EtchA11yNode* out_node, const EtchContext* ctx, struct SDL_Window* window) {
    const auto nodes = etch::frame_a11y_nodes(to_ctx(ctx), window);
    if (!out_node || index >= nodes.size()) return false;
    *out_node = from_cpp(nodes[index]);
    return true;
}

ETCH_API bool etch_find_a11y_node(uint32_t element_id, EtchA11yNode* out_node, const EtchContext* ctx, struct SDL_Window* window) {
    if (!out_node) return false;
    const etch::A11yNode* n = etch::find_a11y_node(element_id, to_ctx(ctx), window);
    if (!n) return false;
    *out_node = from_cpp(*n);
    return true;
}

ETCH_API bool etch_perform_a11y_action(uint32_t element_id, EtchA11yAction action, float numeric_value, EtchStr text_value, EtchContext* ctx, struct SDL_Window* window) {
    return etch::perform_a11y_action(element_id, static_cast<etch::A11yAction>(action), numeric_value,
                                     to_cpp(text_value), to_ctx(ctx), window);
}
ETCH_API const char* etch_a11y_role_name(EtchA11yRole role) { return etch::a11y_role_name(static_cast<etch::A11yRole>(role)); }
ETCH_API void etch_set_os_a11y_enabled(EtchContext* ctx, bool enabled) { etch::set_os_a11y_enabled(to_ctx(ctx), enabled); }
ETCH_API bool etch_os_a11y_enabled(const EtchContext* ctx) { return etch::os_a11y_enabled(to_ctx(ctx)); }
ETCH_API bool etch_os_a11y_active(const EtchContext* ctx, struct SDL_Window* window) { return etch::os_a11y_active(to_ctx(ctx), window); }

/* ============================================================================
 * Layout Boxes, Text, Collapsibles, Images, Icons & 2D Draw Commands
 * ============================================================================ */

ETCH_API void etch_begin_box(const EtchBoxDesc* desc) {
    if (!desc) { etch::begin_box(etch::BoxDesc{}); return; }
    EtchBoxDesc scratch{};
    const EtchBoxDesc* d = resolve_c_desc(desc, &scratch, etch_box_desc_default);
    if (!d) return;
    etch::begin_box(to_cpp(*d));
}
ETCH_API void etch_end_box(void) { etch::end_box(); }
ETCH_API void etch_box(const EtchBoxDesc* desc) {
    if (!desc) { etch::box(etch::BoxDesc{}); return; }
    EtchBoxDesc scratch{};
    const EtchBoxDesc* d = resolve_c_desc(desc, &scratch, etch_box_desc_default);
    if (!d) return;
    etch::box(to_cpp(*d));
}
ETCH_API void etch_text(EtchStr content, const EtchTextDesc* desc) {
    if (!desc) { etch::text(to_cpp(content), etch::TextDesc{}); return; }
    EtchTextDesc scratch{};
    const EtchTextDesc* d = resolve_c_desc(desc, &scratch, etch_text_desc_default);
    if (!d) return;
    etch::text(to_cpp(content), to_cpp(*d));
}
ETCH_API void etch_text_spans(const EtchTextSpan* spans, uint32_t span_count, const EtchRichTextDesc* desc) {
    etch::RichTextDesc rdesc{};
    EtchRichTextDesc scratch{};
    if (desc) {
        const EtchRichTextDesc* d = resolve_c_desc(desc, &scratch, etch_rich_text_desc_default);
        if (!d) return;
        rdesc = to_cpp(*d);
    }
    etch::text_spans(view_compatible_span<etch::TextSpan>(spans, span_count), rdesc);
}

ETCH_API EtchCollapsibleResult etch_begin_collapsible(const EtchCollapsibleDesc* desc) {
    if (!desc) return {};
    const etch::CollapsibleResult r = etch::begin_collapsible({
        desc->id, to_cpp(desc->title), desc->icon_id, desc->default_open, desc->header_height_px,
        desc->font_size_px, desc->font_id, to_cpp(desc->header_padding), to_cpp(desc->body_padding),
        desc->body_gap, to_cpp(desc->radius), to_cpp(desc->header_bg), to_cpp(desc->header_hover_bg),
        to_cpp(desc->body_bg), to_cpp(desc->border_color), desc->border_width,
        to_cpp(desc->text_color), to_cpp(desc->icon_color), to_cpp(desc->spring_spec)});
    return {r.open, r.visible, r.toggled, r.header_hovered, r.anim_t};
}
ETCH_API void etch_end_collapsible(void) { etch::end_collapsible(); }
ETCH_API bool etch_is_collapsible_open(uint32_t element_id) { return etch::is_collapsible_open(element_id); }
ETCH_API void etch_set_collapsible_open(uint32_t element_id, bool open) { etch::set_collapsible_open(element_id, open); }

ETCH_API bool etch_begin_collapse(const EtchCollapseDesc* desc) {
    if (!desc) return false;
    EtchCollapseDesc scratch{};
    const EtchCollapseDesc* d = resolve_c_desc(desc, &scratch, etch_collapse_desc_default);
    if (!d) return false;
    return etch::begin_collapse({d->id, d->open, to_cpp(d->spring), d->fade_content, to_cpp(d->width)});
}
ETCH_API void etch_end_collapse(void) { etch::end_collapse(); }

ETCH_API void etch_image(const EtchImageDesc* desc) {
    if (!desc) return;
    etch::image({desc->id, desc->texture, desc->sprite_id, to_cpp(desc->width), to_cpp(desc->height),
                 to_cpp(desc->uv_min), to_cpp(desc->uv_max), to_cpp(desc->radius), to_cpp(desc->tint),
                 to_cpp(desc->border_color), desc->border_width, desc->aspect_ratio, desc->grid_col_span,
                 desc->continuous_corners, to_cpp(desc->fit), to_cpp(desc->source_size),
                 to_cpp(desc->blend_mode)});
}
ETCH_API void etch_icon(const EtchIconDesc* desc) {
    if (!desc) return;
    etch::icon({desc->id, desc->icon_id, desc->size_px, to_cpp(desc->color),
                desc->rotation_rad, desc->scale, desc->opacity, to_cpp(desc->secondary_color)});
}

ETCH_API void etch_draw_rect(const EtchRectDrawDesc* desc) {
    if (!desc) return;
    etch::draw_rect({to_cpp(desc->rect), to_cpp(desc->fill), to_cpp(desc->fill_end),
                     to_cpp(desc->gradient_dir), desc->radial_gradient, to_cpp(desc->radius),
                     to_cpp(desc->border_color), desc->border_width, desc->foreground, desc->angle_rad,
                     desc->softness, desc->continuous_corners,
                     static_cast<etch::BorderAlign>(desc->border_align), to_cpp(desc->gradient)});
}
ETCH_API EtchMeshRectDrawDesc etch_mesh_rect_draw_desc_default(void) {
    EtchMeshRectDrawDesc d={};d.oklab=true;return d;
}
ETCH_API void etch_draw_mesh_rect(const EtchMeshRectDrawDesc* desc) {
    if(!desc) return;
    etch::draw_mesh_rect({to_cpp(desc->rect),to_cpp(desc->color_tl),to_cpp(desc->color_tr),
        to_cpp(desc->color_br),to_cpp(desc->color_bl),to_cpp(desc->radius),desc->oklab,
        desc->grain_intensity,desc->warp_phase,desc->continuous_corners,desc->foreground});
}
ETCH_API EtchPunchHoleDesc etch_punch_hole_desc_default(void) {
    EtchPunchHoleDesc d={};d.softness=1;return d;
}
ETCH_API void etch_punch_hole(const EtchPunchHoleDesc* desc) {
    if(desc) etch::punch_hole({to_cpp(desc->rect),to_cpp(desc->radius),desc->softness,desc->continuous_corners,desc->foreground});
}
ETCH_API EtchBorderBeamStyle etch_border_beam_style_default(void) { return from_cpp(etch::BorderBeamStyle{}); }
ETCH_API EtchBorderBeamDrawDesc etch_border_beam_draw_desc_default(void) {
    EtchBorderBeamDrawDesc d={};d.beam=etch_border_beam_style_default();return d;
}
ETCH_API void etch_draw_border_beam(const EtchBorderBeamDrawDesc* d) {
    if(d) etch::draw_border_beam({to_cpp(d->rect),to_cpp(d->radius),to_cpp(d->beam),d->top_rim_alpha,d->continuous_corners,d->foreground});
}
ETCH_API void etch_draw_line(const EtchLineDesc* desc) {
    if (!desc) return;
    etch::draw_line({to_cpp(desc->from), to_cpp(desc->to), to_cpp(desc->color), to_cpp(desc->color_end),
                     desc->thickness, to_cpp(desc->glow_color), desc->glow_px, desc->foreground});
}
ETCH_API void etch_draw_filled_polygon(const EtchFilledPolygonDesc* desc) {
    if (!desc) return;
    etch::draw_filled_polygon({view_compatible_span<etch::Vec2>(desc->points, desc->point_count),
                               to_cpp(desc->fill), to_cpp(desc->fill_end), to_cpp(desc->gradient_dir),
                               desc->radial_gradient, to_cpp(desc->stroke_color), desc->stroke_width,
                               desc->foreground});
}
ETCH_API void etch_draw_filled_path(const EtchFilledPathDesc* desc) {
    if (!desc) return;
    etch::draw_filled_path({convert_frame_span<etch::PathCmd, EtchPathCmd>(desc->commands, desc->command_count, to_cpp),
                            to_cpp(desc->svg_path_d), to_cpp(desc->offset), desc->scale,
                            to_cpp(desc->fill), to_cpp(desc->fill_end), to_cpp(desc->gradient_dir),
                            desc->radial_gradient, to_cpp(desc->stroke_color), desc->stroke_width,
                            desc->foreground});
}
ETCH_API void etch_draw_area_series(const EtchAreaSeriesDesc* desc) {
    if (!desc) return;
    etch::draw_area_series({view_compatible_span<etch::Vec2>(desc->points, desc->point_count),
                            desc->baseline_y, to_cpp(desc->fill_top), to_cpp(desc->fill_bottom),
                            to_cpp(desc->line_color), desc->line_thickness, desc->smooth, desc->foreground});
}
ETCH_API void etch_draw_polyline(const EtchPolylineDesc* desc) {
    if (!desc) return;
    etch::draw_polyline({view_compatible_span<etch::Vec2>(desc->points, desc->point_count),
                         to_cpp(desc->color), desc->thickness, to_cpp(desc->glow_color),
                         desc->glow_px, desc->closed, desc->foreground});
}
ETCH_API void etch_draw_bezier(const EtchBezierDesc* desc) {
    if (!desc) return;
    etch::draw_bezier({to_cpp(desc->p0), to_cpp(desc->p1), to_cpp(desc->p2), to_cpp(desc->p3),
                       desc->cubic, to_cpp(desc->color_start), to_cpp(desc->color_end),
                       desc->thickness, to_cpp(desc->glow_color), desc->glow_px, desc->foreground});
}
ETCH_API void etch_draw_ring(const EtchRingDesc* desc) {
    if (!desc) return;
    etch::draw_ring({to_cpp(desc->center), desc->radius, desc->thickness, desc->start_angle_rad,
                     desc->sweep_angle_rad, to_cpp(desc->color), to_cpp(desc->color_end),
                     to_cpp(desc->track_color), to_cpp(desc->glow_color), desc->glow_px,
                     desc->round_caps, desc->foreground});
}
ETCH_API void etch_draw_icon(const EtchIconDrawDesc* desc) {
    if (!desc) return;
    etch::draw_icon({desc->icon_id, to_cpp(desc->rect), to_cpp(desc->color), desc->foreground, desc->angle_rad});
}
ETCH_API EtchFontMetrics etch_font_metrics(uint16_t font_id,float size) {
    const auto metrics=etch::font_metrics(font_id,size);
    return {metrics.ascent,metrics.descent,metrics.line_height};
}
ETCH_API EtchVec2 etch_measure_text(EtchStr text, uint16_t font_id, float size, float letter_spacing) {
    return from_cpp(etch::measure_text(to_cpp(text), font_id, size, letter_spacing));
}
ETCH_API void etch_draw_after_layout(EtchLayoutDrawFn callback, void* user_data, bool foreground) {
    etch::Context* ctx = etch::current_context();
    if (!callback || !etch::c_bridge_can_draw_after_layout(ctx)) return;
    etch::Arena* arena = etch::frame_arena(ctx);
    CLayoutDraw* call = arena ? arena->alloc<CLayoutDraw>() : nullptr;
    if (!call) return;
    *call = {callback, user_data};
    etch::draw_after_layout([](etch::Rect bounds, void* data) noexcept {
        const auto& c = *static_cast<const CLayoutDraw*>(data);
        c.callback(from_cpp(bounds), c.data);
    }, call, foreground);
}
ETCH_API void etch_draw_text(const EtchTextDrawDesc* desc) {
    if (!desc) return;
    etch::draw_text({to_cpp(desc->text), to_cpp(desc->pos), desc->font_id, desc->size,
                     to_cpp(desc->color), desc->letter_spacing,
                     static_cast<etch::AlignX>(desc->align_x),
                     static_cast<etch::AlignY>(desc->align_y), desc->foreground, desc->max_width_px, desc->ellipsis,
                     desc->has_wipe, to_cpp(desc->color_end), desc->wipe_x, desc->wipe_feather_px,
                     to_cpp(desc->shadow_color), to_cpp(desc->shadow_offset), desc->shadow_blur});
}
ETCH_API void etch_draw_image(const EtchImageDrawDesc* desc) {
    if (!desc) return;
    etch::draw_image({desc->texture, desc->sprite_id, to_cpp(desc->rect), to_cpp(desc->uv_rect),
                      to_cpp(desc->tint), desc->radius, desc->foreground, desc->continuous_corners,
                      to_cpp(desc->fit), to_cpp(desc->source_size)});
}

/* ============================================================================
 * Dynamic Sprites & External GPU Textures
 * ============================================================================ */

ETCH_API uint32_t etch_create_sprite_rgba(EtchContext* ctx, const EtchTextureDesc* desc) {
    if (!desc) return ETCH_INVALID_SPRITE;
    return etch::create_sprite_rgba(to_ctx(ctx), {desc->rgba_pixels, desc->width, desc->height,
                                                  desc->row_pitch_bytes, desc->srgb,
                                                  desc->debug_name, desc->premultiplied_alpha,
                                                  desc->generate_mipmaps});
}
ETCH_API bool etch_update_sprite_rgba(EtchContext* ctx, uint32_t sprite_id, uint32_t width, uint32_t height, const void* rgba_pixels, uint32_t row_pitch_bytes) {
    return etch::update_sprite_rgba(to_ctx(ctx), sprite_id, width, height, rgba_pixels, row_pitch_bytes);
}
ETCH_API uint32_t etch_load_sprite_file(EtchContext* ctx, const char* path, bool srgb) { return etch::load_sprite_file(to_ctx(ctx), path, srgb); }
ETCH_API uint32_t etch_load_sprite_memory(EtchContext* ctx, const void* data, size_t size_bytes, bool srgb) { return etch::load_sprite_memory(to_ctx(ctx), data, size_bytes, srgb); }
ETCH_API void etch_destroy_sprite(EtchContext* ctx, uint32_t sprite_id) { etch::destroy_sprite(to_ctx(ctx), sprite_id); }
ETCH_API EtchVec2 etch_sprite_size(const EtchContext* ctx, uint32_t sprite_id) { return from_cpp(etch::sprite_size(to_ctx(ctx), sprite_id)); }
ETCH_API uint32_t etch_sprite_count(const EtchContext* ctx) { return etch::sprite_count(to_ctx(ctx)); }

ETCH_API EtchLoadedTexture etch_create_texture_rgba(EtchContext* ctx, const EtchTextureDesc* desc) {
    if (!desc) return {};
    const etch::LoadedTexture t = etch::create_texture_rgba(to_ctx(ctx), {
        desc->rgba_pixels, desc->width, desc->height, desc->row_pitch_bytes, desc->srgb,
        desc->debug_name, desc->premultiplied_alpha, desc->generate_mipmaps});
    return {t.texture, t.width, t.height};
}
ETCH_API bool etch_flush_texture_uploads(EtchContext* ctx) { return etch::flush_texture_uploads(to_ctx(ctx)); }
ETCH_API bool etch_update_texture_region_rgba(EtchContext* ctx, struct SDL_GPUTexture* texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* rgba_pixels, uint32_t row_pitch_bytes) {
    return etch::update_texture_region_rgba(to_ctx(ctx),texture,x,y,width,height,rgba_pixels,row_pitch_bytes);
}
ETCH_API bool etch_update_texture_rgba(EtchContext* ctx, struct SDL_GPUTexture* texture, uint32_t width, uint32_t height, const void* rgba_pixels, uint32_t row_pitch_bytes) {
    return etch::update_texture_rgba(to_ctx(ctx), texture, width, height, rgba_pixels, row_pitch_bytes);
}
ETCH_API EtchLoadedTexture etch_load_texture_file(EtchContext* ctx, const char* path, bool srgb) {
    const etch::LoadedTexture t = etch::load_texture_file(to_ctx(ctx), path, srgb);
    return {t.texture, t.width, t.height};
}
ETCH_API EtchLoadedTexture etch_load_texture_memory(EtchContext* ctx, const void* data, size_t size_bytes, bool srgb) {
    const etch::LoadedTexture t = etch::load_texture_memory(to_ctx(ctx), data, size_bytes, srgb);
    return {t.texture, t.width, t.height};
}
ETCH_API EtchLoadedTexture etch_load_svg_file(EtchContext* ctx, const char* path, const EtchSvgRasterDesc* desc) {
    EtchSvgRasterDesc scratch{};
    const EtchSvgRasterDesc* d = desc ? resolve_c_desc(desc, &scratch, etch_svg_raster_desc_default) : nullptr;
    etch::SvgRasterDesc cpp_desc{};
    if (d) {
        cpp_desc.width_px = d->width_px;
        cpp_desc.height_px = d->height_px;
        cpp_desc.scale = d->scale;
        cpp_desc.srgb = d->srgb;
        cpp_desc.generate_mipmaps = d->generate_mipmaps;
    }
    const etch::LoadedTexture t = etch::load_svg_file(to_ctx(ctx), path, cpp_desc);
    return {t.texture, t.width, t.height};
}
ETCH_API EtchLoadedTexture etch_load_svg_memory(EtchContext* ctx, const void* data, size_t size_bytes, const EtchSvgRasterDesc* desc) {
    EtchSvgRasterDesc scratch{};
    const EtchSvgRasterDesc* d = desc ? resolve_c_desc(desc, &scratch, etch_svg_raster_desc_default) : nullptr;
    etch::SvgRasterDesc cpp_desc{};
    if (d) {
        cpp_desc.width_px = d->width_px;
        cpp_desc.height_px = d->height_px;
        cpp_desc.scale = d->scale;
        cpp_desc.srgb = d->srgb;
        cpp_desc.generate_mipmaps = d->generate_mipmaps;
    }
    const etch::LoadedTexture t = etch::load_svg_memory(to_ctx(ctx), data, size_bytes, cpp_desc);
    return {t.texture, t.width, t.height};
}
ETCH_API bool etch_register_svg_font_file(const char* family, bool bold, bool italic, const char* path) {
    return etch::register_svg_font_file(family, bold, italic, path);
}
ETCH_API bool etch_register_svg_font_memory(const char* family, bool bold, bool italic, const void* data, size_t size_bytes) {
    return etch::register_svg_font_memory(family, bold, italic, data, size_bytes);
}
ETCH_API void etch_destroy_loaded_texture(EtchContext* ctx, EtchLoadedTexture* tex) {
    if (!tex) return;
    etch::LoadedTexture t = {tex->texture, tex->width, tex->height};
    etch::destroy_texture(to_ctx(ctx), &t);
    *tex = {t.texture, t.width, t.height};
}
ETCH_API void etch_destroy_gpu_texture(EtchContext* ctx, struct SDL_GPUTexture* tex) { etch::destroy_texture(to_ctx(ctx), tex); }
ETCH_API bool etch_decode_image_memory(const void* data, size_t size_bytes, EtchDecodedImage* out_image) {
    if (!out_image) return false;
    etch::DecodedImage img = {};
    if (!etch::decode_image_memory(data, size_bytes, &img)) return false;
    *out_image = {img.rgba, img.width, img.height};
    return true;
}
ETCH_API bool etch_rasterize_svg_memory(const void* data, size_t size_bytes, const EtchSvgRasterDesc* desc, EtchDecodedImage* out_image) {
    if (!out_image) return false;
    EtchSvgRasterDesc scratch{};
    const EtchSvgRasterDesc* d = desc ? resolve_c_desc(desc, &scratch, etch_svg_raster_desc_default) : nullptr;
    etch::SvgRasterDesc cpp_desc{};
    if (d) { cpp_desc = {d->width_px, d->height_px, d->scale, d->srgb, d->generate_mipmaps}; }
    etch::DecodedImage img = {};
    if (!etch::rasterize_svg_memory(data, size_bytes, cpp_desc, &img)) return false;
    *out_image = {img.rgba, img.width, img.height};
    return true;
}
ETCH_API void etch_free_decoded_image(EtchDecodedImage* image) {
    if (!image) return;
    etch::DecodedImage img = {image->rgba, image->width, image->height};
    etch::free_decoded_image(&img);
    *image = {};
}

/* ============================================================================
 * Text Editing, Selection, Clipboard, IME & Code Editor
 * ============================================================================ */

ETCH_API EtchTextInputResult etch_text_input(const EtchTextInputDesc* desc) {
    if (!desc) return {};
    EtchTextInputDesc scratch{};
    const EtchTextInputDesc* d = resolve_c_desc(desc, &scratch, etch_text_input_desc_default);
    if (!d) return {};
    const etch::TextInputResult r = etch::text_input({
        d->id, d->buffer, d->capacity, to_cpp(d->placeholder), to_cpp(d->width),
        to_cpp(d->height), to_cpp(d->padding), d->font_size_px, d->font_id,
        d->letter_spacing, to_cpp(d->text_color), to_cpp(d->placeholder_color),
        to_cpp(d->selection_color), to_cpp(d->caret_color), to_cpp(d->composition_color),
        to_cpp(d->background), to_cpp(d->border_color), to_cpp(d->focus_border_color),
        d->border_width, d->focus_border_width, to_cpp(d->radius), d->multiline,
        d->word_wrap, d->read_only, d->password, d->context_menu, d->clear_button, d->escape_blurs, d->suppress_popup_keys,
        view_compatible_span<etch::TextInputHighlight>(d->highlights, d->highlights ? d->highlight_count : 0u),
        to_cpp(d->ghost_text), to_cpp(d->ghost_color), d->accept_ghost_on_tab, d->submit_on_enter,
        to_cpp(d->hover_border_color), d->focus_glow_blur, d->leading_icon_id,
        to_cpp(d->leading_icon_color), d->continuous_corners, d->disabled,
        d->auto_grow, d->min_height_px, d->max_height_px,
    });
    return {r.changed, r.submitted, r.focused, r.composing, r.byte_length,
            r.cursor_byte, r.selection_anchor_byte, from_cpp(r.ime_cursor_rect), r.ghost_accepted,
            from_cpp(r.caret_rect), r.pasted_image};
}
ETCH_API void etch_set_text_selection(uint32_t element_id, uint32_t anchor_byte, uint32_t cursor_byte) {
    etch::set_text_selection(element_id, anchor_byte, cursor_byte);
}
ETCH_API bool etch_has_text_selection(uint32_t element_id) { return etch::has_text_selection(element_id); }
ETCH_API EtchStr etch_selected_text(uint32_t element_id, EtchArena* arena) {
    if (!arena) return from_cpp(etch::selected_text(element_id, nullptr));
    etch::Arena a = to_cpp_arena(arena);
    const etch::Str out = etch::selected_text(element_id, &a);
    sync_c_arena(arena, a);
    return from_cpp(out);
}
ETCH_API void etch_clear_text_selection(uint32_t element_id) { etch::clear_text_selection(element_id); }
ETCH_API EtchRect etch_ime_cursor_rect(const EtchContext* ctx) { return from_cpp(etch::ime_cursor_rect(to_ctx(ctx))); }
ETCH_API void etch_request_text_input(uint32_t element_id, EtchRect ime_cursor_rect) { etch::request_text_input(element_id, to_cpp(ime_cursor_rect)); }
ETCH_API EtchStr etch_typed_text(uint32_t element_id) { return from_cpp(etch::typed_text(element_id)); }
ETCH_API bool etch_set_clipboard_text(const char* utf8_text) { return etch::set_clipboard_text(utf8_text); }
ETCH_API EtchStr etch_get_clipboard_text(EtchArena* arena) {
    if (!arena) return from_cpp(etch::get_clipboard_text(nullptr));
    etch::Arena a = to_cpp_arena(arena);
    const etch::Str out = etch::get_clipboard_text(&a);
    sync_c_arena(arena, a);
    return from_cpp(out);
}
ETCH_API EtchClipboardImageData etch_get_clipboard_image(EtchArena* arena) {
    if (!arena) {
        const etch::ClipboardImageData d = etch::get_clipboard_image(nullptr);
        return {d.bytes, d.size_bytes, from_cpp(d.mime_type)};
    }
    etch::Arena a = to_cpp_arena(arena);
    const etch::ClipboardImageData d = etch::get_clipboard_image(&a);
    sync_c_arena(arena, a);
    return {d.bytes, d.size_bytes, from_cpp(d.mime_type)};
}

ETCH_API bool etch_open_folder_dialog(EtchContext* ctx, struct SDL_Window* parent_window, const char* default_location, uint32_t request_tag, bool allow_many) {
    return etch::open_folder_dialog(to_ctx(ctx), parent_window, default_location, request_tag, allow_many);
}

ETCH_API bool etch_open_file_dialog(EtchContext* ctx, struct SDL_Window* parent_window, const EtchFileDialogFilter* filters, uint32_t filter_count, const char* default_location, uint32_t request_tag, bool allow_many) {
    etch::Context* c = to_ctx(ctx);
    c = c ? c : etch::current_context();
    if (!c) return false;
    return etch::open_file_dialog(c, parent_window, view_compatible_span<etch::FileDialogFilter>(filters, filter_count),
                                  default_location, request_tag, allow_many);
}

ETCH_API bool etch_open_save_file_dialog(EtchContext* ctx, struct SDL_Window* parent_window, const EtchFileDialogFilter* filters, uint32_t filter_count, const char* default_location, uint32_t request_tag) {
    etch::Context* c = to_ctx(ctx);
    c = c ? c : etch::current_context();
    if (!c) return false;
    return etch::open_save_file_dialog(c, parent_window, view_compatible_span<etch::FileDialogFilter>(filters, filter_count),
                                       default_location, request_tag);
}

ETCH_API EtchFileDialogResult etch_poll_file_dialog(EtchContext* ctx, EtchArena* arena) {
    etch::Context* c = to_ctx(ctx);
    c = c ? c : etch::current_context();
    if (!c) return {false, false, false, 0u, -1, nullptr, 0u};
    etch::Arena local_arena = arena ? to_cpp_arena(arena) : etch::Arena{};
    etch::Arena* target_arena = arena ? &local_arena : etch::frame_arena(c);
    const etch::FileDialogResult r = etch::poll_file_dialog(c, target_arena);
    if (arena) sync_c_arena(arena, local_arena);
    const uint32_t cnt = static_cast<uint32_t>(r.paths.size());
    const EtchStr* c_paths = (cnt > 0u) ? reinterpret_cast<const EtchStr*>(r.paths.data()) : nullptr;
    return {r.ready, r.cancelled, r.error, r.request_tag, r.selected_filter, c_paths, cnt};
}

ETCH_API EtchDesktopNotificationDesc etch_desktop_notification_desc_default(void) {
    return {static_cast<uint32_t>(sizeof(EtchDesktopNotificationDesc)), {nullptr, 0u, false}, {nullptr, 0u, false}, 0u, false};
}
ETCH_API bool etch_open_url(EtchStr url) { return etch::open_url(to_cpp(url)); }
ETCH_API bool etch_flash_window(struct SDL_Window* window, EtchWindowFlashKind kind) {
    return etch::flash_window(window, static_cast<etch::WindowFlashKind>(kind));
}
ETCH_API bool etch_set_window_progress(struct SDL_Window* window, EtchWindowProgressState state, float value_0_to_1) {
    return etch::set_window_progress(window, static_cast<etch::WindowProgressState>(state), value_0_to_1);
}
ETCH_API bool etch_system_beep(void) { return etch::system_beep(); }
ETCH_API bool etch_notify_desktop(EtchContext* ctx, const EtchDesktopNotificationDesc* desc) {
    if (!desc) return false;
    EtchDesktopNotificationDesc scratch{};
    const EtchDesktopNotificationDesc* d = resolve_c_desc(desc, &scratch, etch_desktop_notification_desc_default);
    if (!d) return false;
    return etch::notify_desktop(to_ctx(ctx), {to_cpp(d->title), to_cpp(d->body), d->tag, d->silent});
}
ETCH_API uint32_t etch_desktop_notification_count(const EtchContext* ctx) {
    return etch::desktop_notification_count(to_ctx(ctx));
}
ETCH_API EtchTray* etch_tray_create(EtchContext* ctx, EtchStr tooltip) {
    return reinterpret_cast<EtchTray*>(etch::create_tray(to_ctx(ctx), to_cpp(tooltip)));
}
ETCH_API void etch_tray_destroy(EtchTray* tray) {
    etch::destroy_tray(reinterpret_cast<etch::Tray*>(tray));
}
ETCH_API bool etch_tray_set_tooltip(EtchTray* tray, EtchStr tooltip) {
    return etch::tray_set_tooltip(reinterpret_cast<etch::Tray*>(tray), to_cpp(tooltip));
}
ETCH_API bool etch_tray_set_icon_rgba(EtchTray* tray, const uint8_t* rgba_pixels, uint32_t width, uint32_t height) {
    return etch::tray_set_icon_rgba(reinterpret_cast<etch::Tray*>(tray), rgba_pixels, width, height);
}
namespace {
uint32_t count_c_tray_items(const EtchTrayItemDesc* items, uint32_t count, uint32_t depth) noexcept {
    if (!items || count == 0u || depth > 4u) return 0u;
    uint32_t total = count;
    for (uint32_t i = 0u; i < count; ++i) {
        if (items[i].submenu_items && items[i].submenu_count > 0u) {
            total += count_c_tray_items(items[i].submenu_items, items[i].submenu_count, depth + 1u);
        }
    }
    return total;
}
std::span<const etch::TrayItemDesc> convert_c_tray_items(
    const EtchTrayItemDesc* src, uint32_t count,
    etch::TrayItemDesc* pool, uint32_t* used, uint32_t cap, uint32_t depth) noexcept
{
    if (!src || count == 0u || depth > 4u || *used + count > cap) return {};
    const uint32_t base = *used;
    *used += count;
    for (uint32_t i = 0u; i < count; ++i) {
        const EtchTrayItemDesc& s = src[i];
        std::span<const etch::TrayItemDesc> sub = {};
        if (s.submenu_items && s.submenu_count > 0u) {
            sub = convert_c_tray_items(s.submenu_items, s.submenu_count, pool, used, cap, depth + 1u);
        }
        pool[base + i] = {s.id, to_cpp(s.label), s.checked, s.checkbox, s.disabled, s.separator, sub};
    }
    return std::span<const etch::TrayItemDesc>(pool + base, count);
}
uint32_t count_c_menu_items(const EtchMenuBarItemDesc* items, uint32_t count, uint32_t depth) noexcept {
    if (!items || count == 0u || depth > 4u) return 0u;
    uint32_t total = count;
    for (uint32_t i = 0u; i < count; ++i) {
        if (items[i].children && items[i].child_count > 0u) {
            total += count_c_menu_items(items[i].children, items[i].child_count, depth + 1u);
        }
    }
    return total;
}
std::span<const etch::MenuBarItemDesc> convert_c_menu_items(
    const EtchMenuBarItemDesc* src, uint32_t count,
    etch::MenuBarItemDesc* pool, uint32_t* used, uint32_t cap, uint32_t depth) noexcept
{
    if (!src || count == 0u || depth > 4u || *used + count > cap) return {};
    const uint32_t base = *used;
    *used += count;
    for (uint32_t i = 0u; i < count; ++i) {
        const EtchMenuBarItemDesc& s = src[i];
        std::span<const etch::MenuBarItemDesc> ch = {};
        if (s.children && s.child_count > 0u) {
            ch = convert_c_menu_items(s.children, s.child_count, pool, used, cap, depth + 1u);
        }
        pool[base + i] = {s.id, to_cpp(s.label), to_cpp(s.shortcut), s.separator, s.disabled, s.checked, ch};
    }
    return std::span<const etch::MenuBarItemDesc>(pool + base, count);
}
} // namespace
ETCH_API bool etch_tray_set_menu(EtchTray* tray, const EtchTrayItemDesc* items, uint32_t item_count) {
    if (item_count == 0u) {
        return etch::tray_set_menu(reinterpret_cast<etch::Tray*>(tray), {});
    }
    if (!items || item_count > 64u) return false;
    const uint32_t total = count_c_tray_items(items, item_count, 0u);
    if (total == 0u || total > 256u) return false;
    etch::TrayItemDesc pool[256] = {};
    uint32_t used = 0u;
    const auto span = convert_c_tray_items(items, item_count, pool, &used, total, 0u);
    return etch::tray_set_menu(reinterpret_cast<etch::Tray*>(tray), span);
}
ETCH_API uint32_t etch_tray_poll_clicked_item(EtchTray* tray) {
    return etch::tray_poll_clicked_item(reinterpret_cast<etch::Tray*>(tray));
}
ETCH_API bool etch_set_native_menu_bar(EtchContext* ctx, struct SDL_Window* window, const EtchMenuBarItemDesc* menus, uint32_t menu_count) {
    if (menu_count == 0u) {
        return etch::set_native_menu_bar(to_ctx(ctx), window, {});
    }
    if (!menus || menu_count > 32u) return false;
    const uint32_t total = count_c_menu_items(menus, menu_count, 0u);
    if (total == 0u || total > 512u) return false;
    auto* pool = static_cast<etch::MenuBarItemDesc*>(std::calloc(total, sizeof(etch::MenuBarItemDesc)));
    if (!pool) return false;
    uint32_t used = 0u;
    const auto span = convert_c_menu_items(menus, menu_count, pool, &used, total, 0u);
    const bool ok = etch::set_native_menu_bar(to_ctx(ctx), window, span);
    std::free(pool);
    return ok;
}
ETCH_API uint32_t etch_poll_native_menu_item(EtchContext* ctx) {
    return etch::poll_native_menu_item(to_ctx(ctx));
}

ETCH_API EtchTextBuffer* etch_create_text_buffer(EtchStr initial_utf8, uint32_t initial_cap) {
    auto* tb = static_cast<etch::TextBuffer*>(std::calloc(1u, sizeof(etch::TextBuffer)));
    if (!tb) return nullptr;
    if (!etch::init_text_buffer(tb, to_cpp(initial_utf8), initial_cap ? initial_cap : 4096u)) {
        std::free(tb);
        return nullptr;
    }
    return from_tb(tb);
}
ETCH_API void etch_destroy_text_buffer(EtchTextBuffer* buf) {
    if (!buf) return;
    etch::TextBuffer* tb = to_tb(buf);
    etch::free_text_buffer(tb);
    std::free(tb);
}
ETCH_API bool etch_text_buffer_set(EtchTextBuffer* buf, EtchStr utf8) { return etch::text_buffer_set(to_tb(buf), to_cpp(utf8)); }
ETCH_API bool etch_text_buffer_replace(EtchTextBuffer* buf, uint32_t byte_start, uint32_t byte_end, EtchStr utf8,
    bool record_undo, uint32_t before_cursor, uint32_t before_anchor,
    uint32_t after_cursor, uint32_t after_anchor, uint8_t op_group, double timestamp_s)
{
    return etch::text_buffer_replace(to_tb(buf), byte_start, byte_end, to_cpp(utf8), record_undo,
                                     before_cursor, before_anchor, after_cursor, after_anchor,
                                     op_group, timestamp_s);
}
ETCH_API uint32_t etch_text_buffer_byte_length(const EtchTextBuffer* buf) { return etch::text_buffer_byte_length(to_tb(buf)); }
ETCH_API bool etch_text_buffer_apply_edits(EtchTextBuffer* buf, uint64_t revision, const EtchTextEdit* edits, uint32_t count) {
    return etch::apply_text_edits(to_tb(buf), revision, edits, count, [](const void* data, size_t i) noexcept {
        const auto& e = static_cast<const EtchTextEdit*>(data)[i];
        return etch::TextEdit{e.byte_start, e.byte_end, to_cpp(e.text)};
    });
}
ETCH_API bool etch_text_buffer_set_diagnostics(EtchTextBuffer* buf, uint64_t revision,
                                                const EtchTextDiagnostic* diagnostics, uint32_t count) {
    if (!diagnostics && count) return false;
    etch::TextDiagnostic* converted = count
        ? static_cast<etch::TextDiagnostic*>(std::malloc(size_t(count) * sizeof(etch::TextDiagnostic))) : nullptr;
    if (count && !converted) return false;
    for (uint32_t i = 0; i < count; ++i) {
        converted[i] = {diagnostics[i].byte_start, diagnostics[i].byte_end,
                        static_cast<etch::TextDiagnosticSeverity>(diagnostics[i].severity),
                        to_cpp(diagnostics[i].message)};
    }
    const bool ok = etch::text_buffer_set_diagnostics(to_tb(buf), revision, {converted, count});
    std::free(converted);
    return ok;
}
ETCH_API void etch_text_buffer_clear_diagnostics(EtchTextBuffer* buf) {
    etch::text_buffer_clear_diagnostics(to_tb(buf));
}
ETCH_API EtchTextSearchOptions etch_text_search_options_default(void) {
    return {static_cast<uint32_t>(sizeof(EtchTextSearchOptions)), false, false, false};
}
ETCH_API bool etch_text_buffer_find(const EtchTextBuffer* buf, EtchStr needle, uint32_t start_byte,
    bool case_sensitive, bool whole_word, EtchTextSearchMatch* out_match) {
    etch::TextSearchMatch m = {};
    if (!etch::text_buffer_find(to_tb(buf), to_cpp(needle), start_byte, case_sensitive, whole_word, &m)) return false;
    if (out_match) { out_match->byte_start = m.byte_start; out_match->byte_end = m.byte_end; }
    return true;
}
ETCH_API uint32_t etch_text_buffer_find_all(const EtchTextBuffer* buf, EtchStr needle,
    bool case_sensitive, bool whole_word, EtchTextSearchMatch* out_matches, uint32_t max_matches) {
    static_assert(sizeof(EtchTextSearchMatch) == sizeof(etch::TextSearchMatch));
    return etch::text_buffer_find_all(to_tb(buf), to_cpp(needle), case_sensitive, whole_word,
        reinterpret_cast<etch::TextSearchMatch*>(out_matches), max_matches);
}
ETCH_API bool etch_text_buffer_find_ex(const EtchTextBuffer* buf, EtchStr pattern, uint32_t start_byte,
    const EtchTextSearchOptions* opts, EtchTextSearchMatch* out_match) {
    EtchTextSearchOptions scratch{};
    const EtchTextSearchOptions* o = opts ? resolve_c_desc(opts, &scratch, etch_text_search_options_default) : nullptr;
    const etch::TextSearchOptions cpp_opts = o ? etch::TextSearchOptions{o->case_sensitive, o->whole_word, o->regex} : etch::TextSearchOptions{};
    etch::TextSearchMatch m = {};
    if (!etch::text_buffer_find_ex(to_tb(buf), to_cpp(pattern), start_byte, cpp_opts, &m)) return false;
    if (out_match) { out_match->byte_start = m.byte_start; out_match->byte_end = m.byte_end; }
    return true;
}
ETCH_API uint32_t etch_text_buffer_find_all_ex(const EtchTextBuffer* buf, EtchStr pattern,
    const EtchTextSearchOptions* opts, EtchTextSearchMatch* out_matches, uint32_t max_matches) {
    static_assert(sizeof(EtchTextSearchMatch) == sizeof(etch::TextSearchMatch));
    EtchTextSearchOptions scratch{};
    const EtchTextSearchOptions* o = opts ? resolve_c_desc(opts, &scratch, etch_text_search_options_default) : nullptr;
    const etch::TextSearchOptions cpp_opts = o ? etch::TextSearchOptions{o->case_sensitive, o->whole_word, o->regex} : etch::TextSearchOptions{};
    return etch::text_buffer_find_all_ex(to_tb(buf), to_cpp(pattern), cpp_opts,
        reinterpret_cast<etch::TextSearchMatch*>(out_matches), max_matches);
}
ETCH_API uint32_t etch_text_buffer_replace_all(EtchTextBuffer* buf, EtchStr pattern, EtchStr replacement,
    const EtchTextSearchOptions* opts) {
    EtchTextSearchOptions scratch{};
    const EtchTextSearchOptions* o = opts ? resolve_c_desc(opts, &scratch, etch_text_search_options_default) : nullptr;
    const etch::TextSearchOptions cpp_opts = o ? etch::TextSearchOptions{o->case_sensitive, o->whole_word, o->regex} : etch::TextSearchOptions{};
    return etch::text_buffer_replace_all(to_tb(buf), to_cpp(pattern), to_cpp(replacement), cpp_opts);
}
ETCH_API uint32_t etch_text_buffer_line_count(const EtchTextBuffer* buf) { return etch::text_buffer_line_count(to_tb(buf)); }
ETCH_API uint32_t etch_text_buffer_line_start(const EtchTextBuffer* buf, uint32_t line) { return etch::text_buffer_line_start(to_tb(buf), line); }
ETCH_API uint32_t etch_text_buffer_line_end(const EtchTextBuffer* buf, uint32_t line) { return etch::text_buffer_line_end(to_tb(buf), line); }
ETCH_API EtchStr etch_text_buffer_line(const EtchTextBuffer* buf, uint32_t line, EtchArena* scratch) {
    if (!scratch) return from_cpp(etch::text_buffer_line(to_tb(buf), line, nullptr));
    etch::Arena a = to_cpp_arena(scratch);
    const etch::Str out = etch::text_buffer_line(to_tb(buf), line, &a);
    sync_c_arena(scratch, a);
    return from_cpp(out);
}
ETCH_API uint32_t etch_text_buffer_copy_range(const EtchTextBuffer* buf, uint32_t byte_start, uint32_t byte_end, char* out_buf, uint32_t cap) {
    return etch::text_buffer_copy_range(to_tb(buf), byte_start, byte_end, out_buf, cap);
}
ETCH_API EtchTextLineCol etch_text_buffer_line_col(const EtchTextBuffer* buf, uint32_t byte_offset) {
    const etch::TextLineCol lc = etch::text_buffer_line_col(to_tb(buf), byte_offset);
    return {lc.line, lc.col_byte};
}
ETCH_API uint32_t etch_text_buffer_byte_offset(const EtchTextBuffer* buf, uint32_t line, uint32_t col_byte) {
    return etch::text_buffer_byte_offset(to_tb(buf), line, col_byte);
}
ETCH_API EtchTextUtf16Position etch_text_buffer_utf16_position(const EtchTextBuffer* buf, uint32_t byte_offset) {
    const auto p = etch::text_buffer_utf16_position(to_tb(buf), byte_offset);
    return {p.line, p.character};
}
ETCH_API uint32_t etch_text_buffer_byte_from_utf16(const EtchTextBuffer* buf, EtchTextUtf16Position position) {
    return etch::text_buffer_byte_from_utf16(to_tb(buf), {position.line, position.character});
}
ETCH_API uint64_t etch_text_buffer_revision(const EtchTextBuffer* buf) { return etch::text_buffer_revision(to_tb(buf)); }
ETCH_API bool etch_text_buffer_undo(EtchTextBuffer* buf, uint32_t* out_cursor, uint32_t* out_anchor) {
    return etch::text_buffer_undo(to_tb(buf), out_cursor, out_anchor);
}
ETCH_API bool etch_text_buffer_redo(EtchTextBuffer* buf, uint32_t* out_cursor, uint32_t* out_anchor) {
    return etch::text_buffer_redo(to_tb(buf), out_cursor, out_anchor);
}
ETCH_API void etch_text_buffer_clear_undo(EtchTextBuffer* buf) { etch::text_buffer_clear_undo(to_tb(buf)); }

ETCH_API uint32_t etch_tokenize_cpp_hlsl(uint32_t line_index, EtchStr line, EtchSyntaxToken* out_tokens, uint32_t max_tokens, void* user_data) {
    (void)line_index;(void)user_data;
    if (!out_tokens || max_tokens == 0u) return 0u;
    return etch::c_bridge_tokenize_cpp_hlsl(to_cpp(line), out_tokens, max_tokens,
        [](void* ptr, uint32_t i, uint32_t a, uint32_t z, etch::Color color) noexcept {
            static_cast<EtchSyntaxToken*>(ptr)[i] = {a, z, from_cpp(color), etch::kFontMono};
        });
}
ETCH_API uint32_t etch_tokenize_language(EtchStr language, uint32_t line_index, EtchStr line, EtchSyntaxToken* out_tokens, uint32_t max_tokens) {
    (void)line_index;
    if (!out_tokens || max_tokens == 0u) return 0u;
    return etch::c_bridge_tokenize_language(to_cpp(language), to_cpp(line), out_tokens, max_tokens,
        [](void* ptr, uint32_t i, uint32_t a, uint32_t z, etch::Color color) noexcept {
            static_cast<EtchSyntaxToken*>(ptr)[i] = {a, z, from_cpp(color), etch::kFontMono};
        });
}
ETCH_API uint32_t etch_tokenize_language_stateful(EtchStr language, EtchStr line, uint8_t in_state, uint8_t* out_state, EtchSyntaxToken* out_tokens, uint32_t max_tokens) {
    if (!out_tokens && max_tokens > 0u) return 0u;
    return etch::c_bridge_tokenize_language_stateful(to_cpp(language), to_cpp(line), in_state, out_state, out_tokens, max_tokens,
        [](void* ptr, uint32_t i, uint32_t a, uint32_t z, etch::Color color) noexcept {
            static_cast<EtchSyntaxToken*>(ptr)[i] = {a, z, from_cpp(color), etch::kFontMono};
        });
}

ETCH_API EtchCodeViewZone etch_code_view_zone_default(void) {
    const etch::CodeViewZone z{};
    return {z.zone_id, z.after_line, z.height_px, z.above_line, from_cpp(z.fill_color)};
}
ETCH_API EtchCodeInlayHint etch_code_inlay_hint_default(void) {
    const etch::CodeInlayHint h{};
    return {h.line_index, h.col_byte, from_cpp(h.text), from_cpp(h.color),
            from_cpp(h.background), from_cpp(h.border_color), h.radius_px};
}
ETCH_API EtchCodeInlineHighlight etch_code_inline_highlight_default(void) {
    const etch::CodeInlineHighlight h{};
    return {h.line_index, h.start_col_byte, h.end_col_byte,
            from_cpp(h.background), from_cpp(h.border_color), h.radius_px};
}
ETCH_API EtchCodeOverviewMarker etch_code_overview_marker_default(void) {
    const etch::CodeOverviewMarker m{};
    return {m.line_index, from_cpp(m.color), m.lane};
}
ETCH_API EtchCodeEditorResult etch_code_editor(const EtchCodeEditorDesc* desc) {
    if (!desc) return {};
    EtchCodeEditorDesc scratch{};
    const EtchCodeEditorDesc* d = resolve_c_desc(desc, &scratch, etch_code_editor_desc_default);
    if (!d) return {};
    etch::SyntaxTokenizeFn tok_fn = nullptr;
    void* tok_ud = nullptr;
    if (d->tokenizer == etch_tokenize_cpp_hlsl) {
        tok_fn = etch::tokenize_cpp_hlsl;
        tok_ud = d->tokenizer_user_data;
    } else if (d->tokenizer) {
        etch::Context* ctx = etch::current_context();
        etch::Arena* arena = etch::frame_in_progress(ctx) ? etch::frame_arena(ctx) : nullptr;
        CTokenizerThunk* thunk = arena ? arena->alloc<CTokenizerThunk>() : nullptr;
        if (thunk) {
            thunk->fn = d->tokenizer;
            thunk->user_data = d->tokenizer_user_data;
            tok_fn = c_syntax_tokenizer_trampoline;
            tok_ud = thunk;
        }
    }
    etch::CodeLineDecorateFn dec_fn = nullptr;
    void* dec_ud = nullptr;
    if (d->line_decorator) {
        etch::Context* ctx = etch::current_context();
        etch::Arena* arena = etch::frame_in_progress(ctx) ? etch::frame_arena(ctx) : nullptr;
        CCodeLineDecorateThunk* dthunk = arena ? arena->alloc<CCodeLineDecorateThunk>() : nullptr;
        if (dthunk) {
            dthunk->fn = d->line_decorator;
            dthunk->user_data = d->line_decorator_user_data;
            dec_fn = c_code_line_decorate_trampoline;
            dec_ud = dthunk;
        }
    }
    const etch::CodeEditorResult r = etch::code_editor({
        d->id, to_tb(d->buffer), to_cpp(d->width), to_cpp(d->height),
        to_cpp(d->padding), d->font_size_px, d->font_id, d->line_height_px,
        d->show_line_numbers, d->gutter_width_px, d->tab_width, d->read_only,
        d->show_scrollbar, d->word_wrap, tok_fn, tok_ud, to_cpp(d->background),
        to_cpp(d->gutter_background), to_cpp(d->active_line_color), to_cpp(d->selection_color),
        to_cpp(d->text_color), to_cpp(d->gutter_text_color),
        to_cpp(d->gutter_active_text_color), to_cpp(d->caret_color),
        to_cpp(d->composition_color), to_cpp(d->border_color),
        to_cpp(d->focus_border_color), d->border_width, d->focus_border_width,
        to_cpp(d->radius), d->scroll_fade_px, dec_fn, dec_ud, d->gutter_tag_width_px,
        d->auto_height, d->show_overview_ruler, d->show_sticky_header, to_cpp(d->sticky_header_text),
        to_cpp(d->language), d->highlight_matching_brackets, d->show_indent_guides, d->show_diagnostic_tooltips, d->show_minimap});
    return {r.changed, r.focused, r.composing, r.cursor_byte, r.selection_anchor_byte,
            r.cursor_line, r.cursor_col_byte, r.first_visible_line, r.end_visible_line,
            r.total_lines, from_cpp(r.ime_cursor_rect), r.hovered_line, r.clicked_gutter_line,
            r.gutter_drag_start_line, r.gutter_drag_end_line, r.gutter_dragging};
}
ETCH_API void etch_code_editor_set_cursor(uint32_t element_id, uint32_t anchor_byte, uint32_t cursor_byte, bool scroll_into_view) {
    etch::code_editor_set_cursor(element_id, anchor_byte, cursor_byte, scroll_into_view);
}
ETCH_API void etch_code_editor_scroll_to_line(uint32_t element_id, uint32_t line_index, EtchAlignY align) {
    etch::code_editor_scroll_to_line(element_id, line_index, static_cast<etch::AlignY>(align));
}
ETCH_API void etch_code_editor_set_inline_completion(uint32_t editor_id, EtchStr ghost_text) {
    etch::code_editor_set_inline_completion(editor_id, to_cpp(ghost_text));
}
ETCH_API bool etch_code_editor_completion_accepted(uint32_t editor_id) {
    return etch::code_editor_completion_accepted(editor_id);
}
ETCH_API bool etch_code_editor_completion_dismissed(uint32_t editor_id) {
    return etch::code_editor_completion_dismissed(editor_id);
}
ETCH_API void etch_code_editor_set_search_highlights(uint32_t editor_id, EtchStr query, bool case_sensitive) {
    etch::code_editor_set_search_highlights(editor_id, to_cpp(query), case_sensitive);
}
ETCH_API bool etch_code_editor_set_fold(uint32_t editor_id, uint32_t start_line, uint32_t end_line, bool collapsed) {
    return etch::code_editor_set_fold(editor_id, start_line, end_line, collapsed);
}
ETCH_API bool etch_code_editor_is_line_folded(uint32_t editor_id, uint32_t line) {
    return etch::code_editor_is_line_folded(editor_id, line);
}
ETCH_API bool etch_code_editor_add_cursor(uint32_t editor_id, uint32_t byte_offset) {
    return etch::code_editor_add_cursor(editor_id, byte_offset);
}
ETCH_API void etch_code_editor_clear_extra_cursors(uint32_t editor_id) {
    etch::code_editor_clear_extra_cursors(editor_id);
}
ETCH_API uint32_t etch_code_editor_cursor_count(uint32_t editor_id) {
    return etch::code_editor_cursor_count(editor_id);
}
ETCH_API EtchVec2 etch_code_editor_scroll_offset(uint32_t editor_id) {
    return from_cpp(etch::code_editor_scroll_offset(editor_id));
}
ETCH_API void etch_code_editor_set_scroll_offset(uint32_t editor_id, EtchVec2 offset, bool stop_momentum) {
    etch::code_editor_set_scroll_offset(editor_id, to_cpp(offset), stop_momentum);
}
ETCH_API bool etch_code_editor_set_view_zones(uint32_t editor_id, const EtchCodeViewZone* zones, uint32_t count) {
    if (!zones && count > 0u) return false;
    if (count == 0u) { etch::code_editor_clear_view_zones(editor_id); return true; }
    static_assert(sizeof(EtchCodeViewZone) == sizeof(etch::CodeViewZone));
    return etch::code_editor_set_view_zones(editor_id,
        std::span<const etch::CodeViewZone>(reinterpret_cast<const etch::CodeViewZone*>(zones), count));
}
ETCH_API void etch_code_editor_clear_view_zones(uint32_t editor_id) {
    etch::code_editor_clear_view_zones(editor_id);
}
ETCH_API bool etch_code_editor_get_view_zone_bounds(uint32_t editor_id, uint32_t zone_id, EtchRect* out_bounds, EtchRect* out_viewport_clip) {
    etch::Rect b = {}, vp = {};
    if (!etch::code_editor_get_view_zone_bounds(editor_id, zone_id, &b, &vp)) return false;
    if (out_bounds) *out_bounds = from_cpp(b);
    if (out_viewport_clip) *out_viewport_clip = from_cpp(vp);
    return true;
}
ETCH_API bool etch_code_editor_begin_view_zone(uint32_t editor_id, uint32_t zone_id) {
    return etch::code_editor_begin_view_zone(editor_id, zone_id);
}
ETCH_API void etch_code_editor_end_view_zone(void) {
    etch::code_editor_end_view_zone();
}
ETCH_API bool etch_code_editor_set_inlay_hints(uint32_t editor_id, const EtchCodeInlayHint* hints, uint32_t count) {
    if (!hints && count > 0u) return false;
    if (count == 0u) { etch::code_editor_clear_inlay_hints(editor_id); return true; }
    static_assert(sizeof(EtchCodeInlayHint) == sizeof(etch::CodeInlayHint));
    return etch::code_editor_set_inlay_hints(editor_id,
        std::span<const etch::CodeInlayHint>(reinterpret_cast<const etch::CodeInlayHint*>(hints), count));
}
ETCH_API void etch_code_editor_clear_inlay_hints(uint32_t editor_id) {
    etch::code_editor_clear_inlay_hints(editor_id);
}
ETCH_API bool etch_code_editor_set_inline_highlights(uint32_t editor_id, const EtchCodeInlineHighlight* highlights, uint32_t count) {
    if (!highlights && count > 0u) return false;
    if (count == 0u) { etch::code_editor_clear_inline_highlights(editor_id); return true; }
    static_assert(sizeof(EtchCodeInlineHighlight) == sizeof(etch::CodeInlineHighlight));
    return etch::code_editor_set_inline_highlights(editor_id,
        std::span<const etch::CodeInlineHighlight>(reinterpret_cast<const etch::CodeInlineHighlight*>(highlights), count));
}
ETCH_API void etch_code_editor_clear_inline_highlights(uint32_t editor_id) {
    etch::code_editor_clear_inline_highlights(editor_id);
}
ETCH_API bool etch_code_editor_set_overview_markers(uint32_t editor_id, const EtchCodeOverviewMarker* markers, uint32_t count) {
    if (!markers && count > 0u) return false;
    if (count == 0u) { etch::code_editor_clear_overview_markers(editor_id); return true; }
    static_assert(sizeof(EtchCodeOverviewMarker) == sizeof(etch::CodeOverviewMarker));
    return etch::code_editor_set_overview_markers(editor_id,
        std::span<const etch::CodeOverviewMarker>(reinterpret_cast<const etch::CodeOverviewMarker*>(markers), count));
}
ETCH_API void etch_code_editor_clear_overview_markers(uint32_t editor_id) {
    etch::code_editor_clear_overview_markers(editor_id);
}

/* ============================================================================
 * Desktop Widgets
 * ============================================================================ */

ETCH_API bool etch_button(const EtchButtonDesc* desc) {
    if (!desc) return false;
    return etch::button({desc->id, to_cpp(desc->label), desc->icon_id,
                         static_cast<etch::ButtonVariant>(desc->variant), to_cpp(desc->width),
                         to_cpp(desc->height), to_cpp(desc->padding), desc->font_size_px,
                         desc->font_id, to_cpp(desc->radius), desc->disabled, to_cpp(desc->shortcut_hint), static_cast<etch::AlignX>(desc->align_x)});
}
ETCH_API bool etch_checkbox(const EtchCheckboxDesc* desc) {
    if (!desc) return false;
    return etch::checkbox({desc->id, to_cpp(desc->label), desc->checked, desc->indeterminate,
                           desc->disabled, desc->box_size_px, desc->font_size_px, desc->font_id,
                           to_cpp(desc->accent)});
}
ETCH_API bool etch_toggle_switch(const EtchToggleSwitchDesc* desc) {
    if (!desc) return false;
    return etch::toggle_switch({desc->id, to_cpp(desc->label), desc->value, desc->disabled,
                                desc->track_width_px, desc->track_height_px, desc->font_size_px,
                                desc->font_id, to_cpp(desc->on_color), to_cpp(desc->off_color),
                                to_cpp(desc->thumb_color), to_cpp(desc->border_color),
                                to_cpp(desc->track_radius), to_cpp(desc->thumb_radius),
                                desc->thumb_padding_px, desc->border_width});
}
ETCH_API void etch_progress_bar(const EtchProgressBarDesc* desc) {
    if (!desc) return;
    etch::progress_bar({desc->id, desc->fraction, to_cpp(desc->overlay_text), to_cpp(desc->width),
                        to_cpp(desc->height), to_cpp(desc->radius), to_cpp(desc->track_color),
                        to_cpp(desc->fill_color), to_cpp(desc->text_color), desc->font_size_px});
}
ETCH_API bool etch_begin_tab_bar(const EtchTabBarDesc* desc) {
    if (!desc) return false;
    return etch::begin_tab_bar({desc->id, desc->active_tab, to_cpp(desc->width), desc->height_px,
                                desc->font_size_px, to_cpp(desc->background), to_cpp(desc->active_bg),
                                to_cpp(desc->hover_bg), to_cpp(desc->accent),
                                to_cpp(desc->text_color), to_cpp(desc->active_text_color)});
}
ETCH_API EtchTabItemResult etch_tab_item(const EtchTabItemDesc* desc) {
    if (!desc) return {};
    const etch::TabItemResult r = etch::tab_item({desc->id, to_cpp(desc->label), desc->icon_id, desc->closable, desc->disabled});
    return {r.selected, r.clicked, r.closed};
}
ETCH_API void etch_end_tab_bar(void) { etch::end_tab_bar(); }
ETCH_API bool etch_slider(const EtchSliderDesc* desc) {
    if (!desc) return false;
    if (desc->style < ETCH_SLIDER_TRACK || desc->style > ETCH_SLIDER_COMPACT_BAR) return false;
    return etch::slider({desc->id, to_cpp(desc->label), desc->value, desc->min_value, desc->max_value,
                         desc->step, desc->format, to_cpp(desc->width), desc->height_px,
                         desc->disabled, to_cpp(desc->background), to_cpp(desc->fill_color),
                         to_cpp(desc->text_color), static_cast<etch::SliderStyle>(desc->style),
                         to_cpp(desc->track_color), to_cpp(desc->thumb_color), desc->track_height_px,
                         desc->thumb_size_px, desc->show_value});
}
ETCH_API bool etch_drag_value(const EtchDragValueDesc* desc) {
    if (!desc) return false;
    return etch::drag_value({desc->id, to_cpp(desc->label), desc->value, desc->min_value, desc->max_value,
                             desc->speed, desc->step, desc->format, to_cpp(desc->width),
                             desc->height_px, desc->disabled, to_cpp(desc->background),
                             to_cpp(desc->fill_color), to_cpp(desc->text_color)});
}
ETCH_API void etch_set_tooltip(uint32_t target_id, const EtchTooltipDesc* desc) {
    if (!desc) return;
    etch::set_tooltip(target_id, {to_cpp(desc->text), desc->delay_seconds, desc->max_width_px,
                                  desc->font_size_px, desc->z_index});
}
ETCH_API void etch_set_tooltip_text(uint32_t target_id, EtchStr text) { etch::set_tooltip(target_id, to_cpp(text)); }
ETCH_API bool etch_begin_modal(const EtchModalDesc* desc) {
    if (!desc) return false;
    return etch::begin_modal({desc->id, desc->open, to_cpp(desc->title), desc->width_px, to_cpp(desc->height),
                              desc->dismiss_on_backdrop, desc->dismiss_on_escape, to_cpp(desc->backdrop_color),
                              to_cpp(desc->background), to_cpp(desc->border_color), to_cpp(desc->radius),
                              desc->z_index});
}
ETCH_API void etch_end_modal(void) { etch::end_modal(); }
ETCH_API bool etch_combo_box(const EtchComboBoxDesc* desc) {
    if (!desc) return false;
    return etch::combo_box({desc->id, to_cpp(desc->label), desc->selected_index,
                            view_compatible_span<etch::Str>(desc->items, desc->item_count),
                            to_cpp(desc->width), desc->height_px, desc->max_popup_height_px,
                            desc->disabled, to_cpp(desc->background), to_cpp(desc->hover_bg),
                            to_cpp(desc->text_color), to_cpp(desc->border_color),
                            to_cpp(desc->focus_border_color), desc->border_width});
}
ETCH_API void etch_open_popup(uint32_t popup_id, EtchVec2 anchor_pos) { etch::open_popup(popup_id, to_cpp(anchor_pos)); }
ETCH_API void etch_close_popup(uint32_t popup_id) { etch::close_popup(popup_id); }
ETCH_API bool etch_is_popup_open(uint32_t popup_id) { return etch::is_popup_open(popup_id); }
ETCH_API bool etch_begin_popup(const EtchPopupDesc* desc) {
    if (!desc) return false;
    return etch::begin_popup({desc->id, desc->trigger_id, to_cpp(desc->width), to_cpp(desc->height),
                              to_cpp(desc->padding), desc->gap, to_cpp(desc->background),
                              to_cpp(desc->border_color), to_cpp(desc->radius), desc->z_index,
                              desc->attach_to_trigger, desc->match_trigger_width, desc->scroll_y,
                              desc->backdrop_blur, desc->shadow_blur, to_cpp(desc->shadow_color),
                              desc->flip_on_overflow});
}
ETCH_API void etch_end_popup(void) { etch::end_popup(); }
ETCH_API bool etch_begin_context_menu(uint32_t popup_id, uint32_t target_id) { return etch::begin_context_menu(popup_id, target_id); }
ETCH_API void etch_end_context_menu(void) { etch::end_context_menu(); }
ETCH_API bool etch_begin_menu_bar(const EtchMenuBarDesc* desc) {
    if (!desc) return false;
    return etch::begin_menu_bar({desc->id, to_cpp(desc->width), desc->height_px, to_cpp(desc->background)});
}
ETCH_API void etch_end_menu_bar(void) { etch::end_menu_bar(); }
ETCH_API bool etch_begin_menu(const EtchMenuDesc* desc) {
    if (!desc) return false;
    return etch::begin_menu({desc->id, to_cpp(desc->label), desc->disabled});
}
ETCH_API void etch_end_menu(void) { etch::end_menu(); }
ETCH_API bool etch_menu_item(const EtchMenuItemDesc* desc) {
    if (!desc) return false;
    return etch::menu_item({desc->id, to_cpp(desc->label), to_cpp(desc->shortcut_hint),
                            desc->icon_id, desc->checked, desc->disabled, desc->separator_after});
}
ETCH_API void etch_menu_separator(void) { etch::menu_separator(); }

/* ============================================================================
 * Interaction, Input, OS Drops, Drag Payload, Springs & Containers
 * ============================================================================ */

ETCH_API bool etch_hovered(uint32_t element_id) { return etch::hovered(element_id); }
ETCH_API bool etch_pressed(uint32_t element_id) { return etch::pressed(element_id); }
ETCH_API bool etch_clicked(uint32_t element_id) { return etch::clicked(element_id); }
ETCH_API bool etch_active(uint32_t element_id) { return etch::active(element_id); }
ETCH_API uint32_t etch_active_id(void) { return etch::active_id(); }
ETCH_API void etch_set_active(uint32_t element_id) { etch::set_active(element_id); }
ETCH_API bool etch_focused(uint32_t element_id) { return etch::focused(element_id); }
ETCH_API uint32_t etch_focused_id(void) { return etch::focused_id(); }
ETCH_API void etch_request_focus(uint32_t element_id) { etch::request_focus(element_id); }
ETCH_API void etch_set_cursor(EtchCursor cursor) { etch::set_cursor(static_cast<etch::Cursor>(cursor)); }
ETCH_API bool etch_element_geometry(uint32_t element_id, EtchElementGeometry* out_geometry) {
    if (!out_geometry) return false;
    etch::ElementGeometry g;
    if (!etch::element_geometry(element_id,&g)) return false;
    *out_geometry = {from_cpp(g.bounds),from_cpp(g.visible_bounds),from_cpp(g.local_scale),g.opacity};
    return true;
}
ETCH_API bool etch_screen_to_element_local(uint32_t element_id, EtchVec2 screen_pos, EtchVec2* out_local) {
    if (!out_local) return false;
    etch::Vec2 p;
    if (!etch::screen_to_element_local(element_id,to_cpp(screen_pos),&p)) return false;
    *out_local = from_cpp(p);
    return true;
}
ETCH_API EtchRect etch_element_bounds(uint32_t element_id) { return from_cpp(etch::element_bounds(element_id)); }

ETCH_API void etch_push_id(uint32_t scope_id) { etch::push_id(scope_id); }
ETCH_API void etch_push_id_str(EtchStr label) { etch::push_id(to_cpp(label)); }
ETCH_API void etch_pop_id(void) { etch::pop_id(); }
ETCH_API uint32_t etch_scoped_id(uint32_t local_id) { return etch::scoped_id(local_id); }
ETCH_API uint32_t etch_scoped_id_str(EtchStr label) { return etch::scoped_id(to_cpp(label)); }

ETCH_API uint16_t etch_key_mods(void) { return etch::key_mods(); }
ETCH_API bool etch_key_pressed(uint32_t keycode, bool include_repeat) { return etch::key_pressed(keycode, include_repeat); }
ETCH_API bool etch_consume_key_pressed(uint32_t keycode, bool include_repeat) { return etch::consume_key_pressed(keycode, include_repeat); }
ETCH_API uint32_t etch_frame_key_count(void) { return etch::frame_key_count(); }
ETCH_API bool etch_frame_key_at(uint32_t index, EtchFrameKeyEvent* out_event) {
    if (!out_event) return false;
    etch::FrameKeyEvent ev{};
    if (!etch::frame_key_at(index, &ev)) return false;
    *out_event = {ev.keycode, ev.mods, ev.repeat, ev.consumed};
    return true;
}
ETCH_API bool etch_consume_frame_key_at(uint32_t index) { return etch::consume_frame_key_at(index); }
ETCH_API bool etch_shortcut(uint16_t required_mods, uint32_t keycode, bool allow_when_typing) {
    return etch::shortcut(required_mods, keycode, allow_when_typing);
}
ETCH_API bool etch_consume_shortcut(uint16_t required_mods, uint32_t keycode, bool allow_when_typing) {
    return etch::consume_shortcut(required_mods, keycode, allow_when_typing);
}

ETCH_API EtchVec2 etch_pointer_pos(void) { return from_cpp(etch::pointer_pos()); }
ETCH_API EtchVec2 etch_pointer_delta(void) { return from_cpp(etch::pointer_delta()); }
ETCH_API bool etch_right_clicked(uint32_t element_id) { return etch::right_clicked(element_id); }
ETCH_API bool etch_right_clicked_anywhere(void) { return etch::right_clicked_anywhere(); }
ETCH_API bool etch_double_clicked(uint32_t element_id) { return etch::double_clicked(element_id); }
ETCH_API bool etch_dragging(uint32_t element_id, float threshold_px) { return etch::dragging(element_id, threshold_px); }
ETCH_API bool etch_clicked_without_drag(uint32_t element_id, float max_drag_px) { return etch::clicked_without_drag(element_id, max_drag_px); }
ETCH_API uint32_t etch_topmost_hovered_id(void) { return etch::topmost_hovered_id(); }
ETCH_API bool etch_begin_cached_subtree(uint32_t id, uint64_t content_hash) { return etch::begin_cached_subtree(id, content_hash); }
ETCH_API void etch_end_cached_subtree(void) { etch::end_cached_subtree(); }
ETCH_API void etch_invalidate_cached_subtree(uint32_t id, EtchContext* ctx) { etch::invalidate_cached_subtree(id, to_ctx(ctx)); }
ETCH_API bool etch_should_close_popover(uint32_t popover_id, uint32_t trigger_id) { return etch::should_close_popover(popover_id, trigger_id); }

ETCH_API uint32_t etch_dropped_file_count(void) { return static_cast<uint32_t>(etch::dropped_files().size()); }
ETCH_API EtchStr etch_dropped_file_at(uint32_t index) {
    const auto s = etch::dropped_files();
    return (index < s.size()) ? from_cpp(s[index]) : EtchStr{};
}
ETCH_API bool etch_is_os_dragging(void) { return etch::is_os_dragging(); }
ETCH_API EtchVec2 etch_os_drop_pos(void) { return from_cpp(etch::os_drop_pos()); }
ETCH_API bool etch_os_drag_hovering(uint32_t element_id) { return etch::os_drag_hovering(element_id); }
ETCH_API uint32_t etch_dropped_file_on_count(uint32_t element_id) { return static_cast<uint32_t>(etch::dropped_files_on(element_id).size()); }
ETCH_API EtchStr etch_dropped_file_on_at(uint32_t element_id, uint32_t index) {
    const auto s = etch::dropped_files_on(element_id);
    return (index < s.size()) ? from_cpp(s[index]) : EtchStr{};
}
ETCH_API uint32_t etch_dropped_text_count(void) { return static_cast<uint32_t>(etch::dropped_texts().size()); }
ETCH_API EtchStr etch_dropped_text_at(uint32_t index) {
    const auto s = etch::dropped_texts();
    return (index < s.size()) ? from_cpp(s[index]) : EtchStr{};
}
ETCH_API uint32_t etch_dropped_text_on_count(uint32_t element_id) { return static_cast<uint32_t>(etch::dropped_texts_on(element_id).size()); }
ETCH_API EtchStr etch_dropped_text_on_at(uint32_t element_id, uint32_t index) {
    const auto s = etch::dropped_texts_on(element_id);
    return (index < s.size()) ? from_cpp(s[index]) : EtchStr{};
}
ETCH_API void etch_begin_focus_scope(uint32_t scope_id, bool trap_tab) { etch::begin_focus_scope(scope_id, trap_tab); }
ETCH_API void etch_end_focus_scope(void) { etch::end_focus_scope(); }
ETCH_API bool etch_keyboard_focus_visible(uint32_t element_id) { return etch::keyboard_focus_visible(element_id); }

ETCH_API bool etch_begin_drag_payload(uint32_t source_id, uint32_t kind_hash, EtchStr label, const void* data, uint32_t size, uint64_t u64, uint32_t u32) {
    return etch::begin_drag_payload(source_id, kind_hash, to_cpp(label), data, size, u64, u32);
}
ETCH_API bool etch_is_dragging_payload(uint32_t kind_hash) { return etch::is_dragging_payload(kind_hash); }
ETCH_API bool etch_peek_drag_payload(EtchDragPayloadView* out_view, uint32_t kind_hash) {
    if (!out_view) return etch::peek_drag_payload(nullptr, kind_hash);
    etch::DragPayloadView v{};
    if (!etch::peek_drag_payload(&v, kind_hash)) return false;
    *out_view = {v.kind_hash, from_cpp(v.label), v.u64, v.u32, v.data, v.size};
    return true;
}
ETCH_API bool etch_accept_drag_payload(uint32_t target_id, uint32_t kind_hash, EtchDragPayloadView* out_view) {
    if (!out_view) return etch::accept_drag_payload(target_id, kind_hash, nullptr);
    etch::DragPayloadView v{};
    if (!etch::accept_drag_payload(target_id, kind_hash, &v)) return false;
    *out_view = {v.kind_hash, from_cpp(v.label), v.u64, v.u32, v.data, v.size};
    return true;
}

ETCH_API struct SDL_Window* etch_create_desktop_window(const char* title, float w, float h, uint64_t flags) { return etch::create_desktop_window(title, w, h, flags); }
ETCH_API struct SDL_Window* etch_create_desktop_window_on_display(const char* title,float w,float h,uint32_t display,uint64_t flags) {return etch::create_desktop_window_on_display(title,w,h,display,flags);}
ETCH_API struct SDL_Window* etch_create_desktop_window_at_pointer(const char* title,float w,float h,uint64_t flags) {return etch::create_desktop_window_at_pointer(title,w,h,flags);}
ETCH_API bool etch_set_desktop_window_min_size(struct SDL_Window* window,float w,float h) {return etch::set_desktop_window_min_size(window,w,h);}
ETCH_API float etch_dpi_scale(const EtchContext* ctx) { return etch::dpi_scale(to_ctx(ctx)); }
ETCH_API float etch_hairline_px(const EtchContext* ctx) { return etch::hairline_px(to_ctx(ctx)); }
ETCH_API float etch_spring_from(uint32_t anim_id, float target, float initial_value, EtchSpringSpec spec) { return etch::spring(anim_id, target, initial_value, to_cpp(spec)); }
ETCH_API float etch_spring(uint32_t anim_id, float target, EtchSpringSpec spec) { return etch::spring(anim_id, target, to_cpp(spec)); }
ETCH_API EtchVec2 etch_spring_vec2(uint32_t anim_id, EtchVec2 target, EtchSpringSpec spec) { return from_cpp(etch::spring_vec2(anim_id, to_cpp(target), to_cpp(spec))); }
ETCH_API EtchColor etch_spring_color(uint32_t anim_id, EtchColor target, EtchSpringSpec spec) { return from_cpp(etch::spring_color(anim_id, to_cpp(target), to_cpp(spec))); }
ETCH_API float etch_spring_angle(uint32_t anim_id, float target_rad, EtchSpringSpec spec) { return etch::spring_angle(anim_id, target_rad, to_cpp(spec)); }
ETCH_API EtchVec2 etch_scroll_delta(void) { return from_cpp(etch::scroll_delta()); }
ETCH_API EtchVec2 etch_scroll_max(uint32_t element_id) { return from_cpp(etch::scroll_max(element_id)); }
ETCH_API EtchVec2 etch_scroll_offset(uint32_t element_id) { return from_cpp(etch::scroll_offset(element_id)); }
ETCH_API void etch_set_scroll_offset(uint32_t element_id, EtchVec2 offset, bool stop_momentum) { etch::set_scroll_offset(element_id, to_cpp(offset), stop_momentum); }
ETCH_API void etch_scroll_into_view(uint32_t scroll_container_id, uint32_t item_id, float margin_px) { etch::scroll_into_view(scroll_container_id, item_id, margin_px); }
ETCH_API bool etch_scroll_stuck_to_bottom(uint32_t element_id) { return etch::scroll_stuck_to_bottom(element_id); }
ETCH_API void etch_set_scroll_stuck_to_bottom(uint32_t element_id, bool stuck) { etch::set_scroll_stuck_to_bottom(element_id, stuck); }

ETCH_API EtchSplitResult etch_begin_split(const EtchSplitDesc* desc) {
    if (!desc) return {};
    EtchSplitDesc scratch{};
    const EtchSplitDesc* d = resolve_c_desc(desc, &scratch, etch_split_desc_default);
    if (!d) return {};
    if (d->sizing < ETCH_SPLIT_RATIO || d->sizing > ETCH_SPLIT_SECOND_FIXED_PX) return {};
    const etch::SplitResult r = etch::begin_split({
        d->id, static_cast<etch::Axis>(d->axis), to_cpp(d->width), to_cpp(d->height),
        d->default_ratio, d->min_first_px, d->min_second_px, d->divider_size_px,
        to_cpp(d->divider_color), to_cpp(d->divider_hover_color), to_cpp(d->divider_active_color),
        d->divider_hit_px, d->keyboard_step_px, static_cast<etch::SplitSizing>(d->sizing),
        d->extent_px, d->default_extent_px, d->show_grip, to_cpp(d->grip_color), d->hover_highlight_px});
    return {r.ratio, r.dragging};
}
ETCH_API void etch_split_second(void) { etch::split_second(); }
ETCH_API void etch_end_split(void) { etch::end_split(); }
ETCH_API float etch_split_ratio(uint32_t split_id) { return etch::split_ratio(split_id); }
ETCH_API void etch_set_split_ratio(uint32_t split_id, float ratio) { etch::set_split_ratio(split_id, ratio); }

ETCH_API EtchVirtualListSlice etch_begin_virtual_list(const EtchVirtualListDesc* desc) {
    if (!desc) return {};
    EtchVirtualListDesc scratch{};
    const EtchVirtualListDesc* d = resolve_c_desc(desc, &scratch, etch_virtual_list_desc_default);
    if (!d) return {};
    const etch::VirtualListDesc cpp_desc = {
        d->id, d->item_count, d->item_height_px, d->row_gap_px, d->overscan_rows,
        to_cpp(d->width), to_cpp(d->height), to_cpp(d->padding), to_cpp(d->background),
        to_cpp(d->border_color), d->border_width, to_cpp(d->radius), d->show_scrollbar,
        nullptr, d->measure_user_data, d->variable_height, d->scroll_fade_px, d->stick_to_bottom};
    return from_cpp(etch::begin_virtual_list_with_c_measure(cpp_desc, d->measure_height_fn));
}
ETCH_API void etch_end_virtual_list(void) { etch::end_virtual_list(); }
ETCH_API void etch_virtual_list_set_item_height(uint32_t list_id, uint32_t row_index, float height_px) { etch::virtual_list_set_item_height(list_id, row_index, height_px); }
ETCH_API void etch_virtual_list_invalidate_heights(uint32_t list_id) { etch::virtual_list_invalidate_heights(list_id); }
ETCH_API double etch_virtual_list_scroll_offset(uint32_t list_id) { return etch::virtual_list_scroll_offset(list_id); }
ETCH_API void etch_set_virtual_list_scroll_offset(uint32_t list_id, double offset_y, bool stop_momentum) { etch::set_virtual_list_scroll_offset(list_id, offset_y, stop_momentum); }
ETCH_API void etch_virtual_list_scroll_to_row(uint32_t list_id, uint32_t row_index, float item_height_px, EtchAlignY align) {
    etch::virtual_list_scroll_to_row(list_id, row_index, item_height_px, static_cast<etch::AlignY>(align));
}
ETCH_API bool etch_virtual_list_is_at_bottom(uint32_t list_id) { return etch::virtual_list_is_at_bottom(list_id); }
ETCH_API void etch_virtual_list_scroll_to_bottom(uint32_t list_id, bool stop_momentum) { etch::virtual_list_scroll_to_bottom(list_id, stop_momentum); }

ETCH_API bool etch_dock_space_initialized(uint32_t dock_id) { return etch::dock_space_initialized(dock_id); }
ETCH_API uint32_t etch_dock_reset_root(uint32_t dock_id) { return etch::dock_reset_root(dock_id); }
ETCH_API bool etch_dock_split_node(uint32_t dock_id, uint32_t node_idx, EtchAxis axis, float default_ratio,
    uint32_t* out_first_node, uint32_t* out_second_node, uint32_t split_widget_id,
    float min_first_px, float min_second_px)
{
    return etch::dock_split_node(dock_id, node_idx, static_cast<etch::Axis>(axis), default_ratio,
                                 out_first_node, out_second_node, split_widget_id, min_first_px, min_second_px);
}
ETCH_API bool etch_dock_add_pane(uint32_t dock_id, uint32_t leaf_node_idx, const EtchDockPaneDesc* pane) {
    if (!pane) return false;
    return etch::dock_add_pane(dock_id, leaf_node_idx, {pane->id, to_cpp(pane->title), pane->icon_id, pane->closable, pane->default_open,
        pane->pinned, pane->dirty, to_cpp(pane->badge_color)});
}
ETCH_API bool etch_dock_dock_pane(uint32_t dock_id, uint32_t pane_id, uint32_t target_pane_id, EtchDockZone zone, float split_ratio) {
    return etch::dock_dock_pane(dock_id, pane_id, target_pane_id, static_cast<etch::DockZone>(zone), split_ratio);
}
ETCH_API bool etch_dock_reorder_pane(uint32_t dock_id, uint32_t pane_id, uint32_t target_pane_id, bool insert_after) {
    return etch::dock_reorder_pane(dock_id, pane_id, target_pane_id, insert_after);
}
ETCH_API void etch_dock_activate_pane(uint32_t dock_id, uint32_t pane_id) { etch::dock_activate_pane(dock_id, pane_id); }
ETCH_API uint32_t etch_dock_active_pane(uint32_t dock_id, uint32_t leaf_node_idx) { return etch::dock_active_pane(dock_id, leaf_node_idx); }
ETCH_API uint32_t etch_dock_find_pane_leaf(uint32_t dock_id, uint32_t pane_id) { return etch::dock_find_pane_leaf(dock_id, pane_id); }
ETCH_API bool etch_dock_is_pane_open(uint32_t dock_id, uint32_t pane_id) { return etch::dock_is_pane_open(dock_id, pane_id); }
ETCH_API void etch_dock_set_pane_open(uint32_t dock_id, uint32_t pane_id, bool open) { etch::dock_set_pane_open(dock_id, pane_id, open); }
ETCH_API void etch_begin_dock_space(const EtchDockSpaceDesc* desc) {
    if (!desc) return;
    etch::begin_dock_space({
        desc->id, to_cpp(desc->width), to_cpp(desc->height), desc->tab_bar_height_px, desc->divider_size_px,
        to_cpp(desc->background), to_cpp(desc->pane_background), to_cpp(desc->pane_border_color),
        desc->pane_border_width, to_cpp(desc->pane_radius), to_cpp(desc->tab_bar_bg),
        to_cpp(desc->tab_active_bg), to_cpp(desc->tab_hover_bg), to_cpp(desc->tab_accent)});
}
ETCH_API uint32_t etch_next_dock_pane(void) { return etch::next_dock_pane(); }
ETCH_API void etch_end_dock_space(void) { etch::end_dock_space(); }
ETCH_API bool etch_dock_detach_pane(uint32_t dock_id, uint32_t pane_id) { return etch::dock_detach_pane(dock_id, pane_id); }
ETCH_API bool etch_dock_poll_tear_off(EtchDockTearOffEvent* out_event) {
    if (!out_event) return false;
    etch::DockTearOffEvent ev = {};
    if (!etch::dock_poll_tear_off(&ev)) return false;
    *out_event = {ev.dock_id, ev.pane_id, from_cpp(ev.screen_pos), from_cpp(ev.title)};
    return true;
}
ETCH_API bool etch_dock_transfer_pane(uint32_t src_dock_id, uint32_t dst_dock_id, uint32_t pane_id,
    uint32_t target_pane_id, EtchDockZone zone, float split_ratio,
    struct SDL_Window* src_window, struct SDL_Window* dst_window) {
    return etch::dock_transfer_pane(src_dock_id, dst_dock_id, pane_id, target_pane_id,
        static_cast<etch::DockZone>(zone), split_ratio, src_window, dst_window);
}
ETCH_API uint32_t etch_dock_save_layout_json(uint32_t dock_id, char* out_buf, uint32_t cap) {
    return etch::dock_save_layout_json(dock_id, out_buf, cap);
}
ETCH_API bool etch_dock_load_layout_json(uint32_t dock_id, EtchStr json) {
    return etch::dock_load_layout_json(dock_id, to_cpp(json));
}
ETCH_API uint32_t etch_dock_redock_window_panes(struct SDL_Window* closing_window, uint32_t src_dock_id,
    uint32_t dst_dock_id, uint32_t target_pane_id, EtchDockZone zone, struct SDL_Window* dst_window) {
    return etch::dock_redock_window_panes(closing_window, src_dock_id, dst_dock_id, target_pane_id,
        static_cast<etch::DockZone>(zone), dst_window);
}
ETCH_API void etch_dock_set_pane_pinned(uint32_t dock_id, uint32_t pane_id, bool pinned) {
    etch::dock_set_pane_pinned(dock_id, pane_id, pinned);
}
ETCH_API bool etch_dock_is_pane_pinned(uint32_t dock_id, uint32_t pane_id) {
    return etch::dock_is_pane_pinned(dock_id, pane_id);
}
ETCH_API void etch_dock_set_pane_dirty(uint32_t dock_id, uint32_t pane_id, bool dirty) {
    etch::dock_set_pane_dirty(dock_id, pane_id, dirty);
}
ETCH_API void etch_dock_set_pane_badge(uint32_t dock_id, uint32_t pane_id, EtchColor badge_color) {
    etch::dock_set_pane_badge(dock_id, pane_id, to_cpp(badge_color));
}
ETCH_API bool etch_dock_pane_right_clicked(uint32_t dock_id, uint32_t pane_id) {
    return etch::dock_pane_right_clicked(dock_id, pane_id);
}
ETCH_API uint32_t etch_dock_focused_leaf(uint32_t dock_id) {
    return etch::dock_focused_leaf(dock_id);
}

ETCH_API bool etch_begin_mime_drag_payload(uint32_t source_id, EtchStr label, const EtchMimeDragEntry* entries, uint32_t count) {
    if (!entries && count) return false;
    static_assert(sizeof(EtchMimeDragEntry) == sizeof(etch::MimeDragEntry));
    return etch::begin_mime_drag_payload(source_id, to_cpp(label),
        std::span<const etch::MimeDragEntry>(reinterpret_cast<const etch::MimeDragEntry*>(entries), count));
}
ETCH_API uint32_t etch_mime_drag_format_count(void) { return etch::mime_drag_format_count(); }
ETCH_API bool etch_peek_mime_drag_entry(EtchStr mime_type, EtchMimeDragEntry* out_entry) {
    etch::MimeDragEntry e = {};
    if (!etch::peek_mime_drag_entry(to_cpp(mime_type), &e)) return false;
    if (out_entry) *out_entry = {from_cpp(e.mime_type), e.bytes, e.size};
    return true;
}
ETCH_API bool etch_accept_mime_drag_payload(uint32_t target_id, EtchStr mime_type, EtchMimeDragEntry* out_entry) {
    etch::MimeDragEntry e = {};
    if (!etch::accept_mime_drag_payload(target_id, to_cpp(mime_type), &e)) return false;
    if (out_entry) *out_entry = {from_cpp(e.mime_type), e.bytes, e.size};
    return true;
}
ETCH_API bool etch_set_clipboard_mime(const EtchMimeDragEntry* entries, uint32_t count) {
    if (!entries && count) return false;
    static_assert(sizeof(EtchMimeDragEntry) == sizeof(etch::MimeDragEntry));
    return etch::set_clipboard_mime(
        std::span<const etch::MimeDragEntry>(reinterpret_cast<const etch::MimeDragEntry*>(entries), count));
}
ETCH_API EtchClipboardMimeData etch_get_clipboard_mime(EtchStr mime_type, EtchArena* arena) {
    const etch::ClipboardMimeData d = etch::get_clipboard_mime(to_cpp(mime_type),
        arena ? reinterpret_cast<etch::Arena*>(arena) : nullptr);
    return {d.bytes, d.size_bytes};
}
ETCH_API bool etch_has_clipboard_mime(EtchStr mime_type) {
    return etch::has_clipboard_mime(to_cpp(mime_type));
}

ETCH_API EtchCommandDesc etch_command_desc_default(void) {
    EtchCommandDesc d = {};
    d.struct_size = static_cast<uint32_t>(sizeof(EtchCommandDesc));
    d.enabled = true;
    return d;
}
ETCH_API bool etch_register_command(EtchContext* ctx, const EtchCommandDesc* desc) {
    if (!desc) return false;
    EtchCommandDesc scratch{};
    const EtchCommandDesc* d = resolve_c_desc(desc, &scratch, etch_command_desc_default);
    if (!d) return false;
    return etch::register_command(to_ctx(ctx), {d->id, to_cpp(d->name), to_cpp(d->title), to_cpp(d->category),
        d->shortcut_mods, d->shortcut_keycode, d->enabled, d->allow_when_typing,
        d->scope_id, d->chord_mods, d->chord_keycode});
}
ETCH_API bool etch_unregister_command(EtchContext* ctx, uint32_t command_id) { return etch::unregister_command(to_ctx(ctx), command_id); }
ETCH_API bool etch_set_command_enabled(EtchContext* ctx, uint32_t command_id, bool enabled) { return etch::set_command_enabled(to_ctx(ctx), command_id, enabled); }
ETCH_API bool etch_is_command_enabled(const EtchContext* ctx, uint32_t command_id) { return etch::is_command_enabled(to_ctx(ctx), command_id); }
ETCH_API bool etch_rebind_command_shortcut(EtchContext* ctx, uint32_t command_id, uint16_t mods, uint32_t keycode,
    uint16_t chord_mods, uint32_t chord_keycode) {
    return etch::rebind_command_shortcut(to_ctx(ctx), command_id, mods, keycode, chord_mods, chord_keycode);
}
ETCH_API uint32_t etch_find_command_shortcut_conflict(const EtchContext* ctx, uint16_t mods, uint32_t keycode, uint32_t ignore_id) {
    return etch::find_command_shortcut_conflict(to_ctx(ctx), mods, keycode, ignore_id);
}
ETCH_API uint32_t etch_poll_triggered_command(EtchContext* ctx) { return etch::poll_triggered_command(to_ctx(ctx)); }
ETCH_API bool etch_dispatch_command(EtchContext* ctx, uint32_t command_id) { return etch::dispatch_command(to_ctx(ctx), command_id); }
ETCH_API uint32_t etch_recent_command_count(const EtchContext* ctx) { return etch::recent_command_count(to_ctx(ctx)); }
ETCH_API uint32_t etch_recent_command_at(const EtchContext* ctx, uint32_t index) { return etch::recent_command_at(to_ctx(ctx), index); }
ETCH_API uint32_t etch_command_count(const EtchContext* ctx) { return etch::command_count(to_ctx(ctx)); }
ETCH_API bool etch_get_command(const EtchContext* ctx, uint32_t index, EtchCommandDesc* out_desc) {
    if (!out_desc) return false;
    etch::CommandDesc c = {};
    if (!etch::get_command(to_ctx(ctx), index, &c)) return false;
    *out_desc = {static_cast<uint32_t>(sizeof(EtchCommandDesc)), c.id, from_cpp(c.name), from_cpp(c.title),
        from_cpp(c.category), c.shortcut_mods, c.shortcut_keycode, c.enabled, c.allow_when_typing,
        c.scope_id, c.chord_mods, c.chord_keycode};
    return true;
}
ETCH_API void etch_push_command_scope(uint32_t scope_id) { etch::push_command_scope(scope_id); }
ETCH_API void etch_pop_command_scope(void) { etch::pop_command_scope(); }
ETCH_API bool etch_push_focus_restore_point(EtchContext* ctx) { return etch::push_focus_restore_point(to_ctx(ctx)); }
ETCH_API uint32_t etch_pop_focus_restore_point(EtchContext* ctx) { return etch::pop_focus_restore_point(to_ctx(ctx)); }
ETCH_API EtchFuzzyMatchResult etch_fuzzy_match(EtchStr pattern, EtchStr candidate, uint32_t* out_positions, uint32_t max_positions) {
    const etch::FuzzyMatchResult r = etch::fuzzy_match(to_cpp(pattern), to_cpp(candidate), out_positions, max_positions);
    return {r.matched, r.score, r.match_count};
}

ETCH_API uint32_t etch_automation_find_nodes(const EtchContext* ctx, const EtchAutomationQuery* query,
    uint32_t* out_ids, uint32_t max_ids, struct SDL_Window* window) {
    if (!query) return 0u;
    return etch::automation_find_nodes(to_ctx(ctx), {static_cast<etch::A11yRole>(query->role),
        to_cpp(query->label_contains), query->required_states, query->forbidden_states}, out_ids, max_ids, window);
}
ETCH_API bool etch_automation_assert_node(const EtchContext* ctx, const EtchAutomationQuery* query,
    EtchA11yNode* out_node, struct SDL_Window* window) {
    if (!query) return false;
    etch::A11yNode n = {};
    if (!etch::automation_assert_node(to_ctx(ctx), {static_cast<etch::A11yRole>(query->role),
        to_cpp(query->label_contains), query->required_states, query->forbidden_states}, &n, window)) return false;
    if (out_node) *out_node = from_cpp(n);
    return true;
}
ETCH_API float etch_time_scale(const EtchContext* ctx) { return etch::time_scale(to_ctx(ctx)); }
ETCH_API void etch_automation_set_recording(EtchContext* ctx, bool enabled) { etch::automation_set_recording(to_ctx(ctx), enabled); }
ETCH_API uint32_t etch_automation_recorded_count(const EtchContext* ctx) { return etch::automation_recorded_count(to_ctx(ctx)); }
ETCH_API bool etch_automation_get_recorded(const EtchContext* ctx, uint32_t index, EtchRecordedFrameInput* out_input) {
    if (!out_input) return false;
    etch::RecordedFrameInput r = {};
    if (!etch::automation_get_recorded(to_ctx(ctx), index, &r)) return false;
    EtchRecordedFrameInput out = {};
    out.dt_seconds = r.dt_seconds;
    out.pointer_pos = from_cpp(r.pointer_pos);
    out.pointer_down = r.pointer_down;
    out.right_pointer_down = r.right_pointer_down;
    out.scroll_delta = from_cpp(r.scroll_delta);
    out.key_mods = r.key_mods;
    out.key_pressed_code = r.key_pressed_code;
    std::memcpy(out.typed_utf8, r.typed_utf8, sizeof(out.typed_utf8));
    out.typed_utf8_len = r.typed_utf8_len;
    *out_input = out;
    return true;
}
ETCH_API void etch_automation_clear_recording(EtchContext* ctx) { etch::automation_clear_recording(to_ctx(ctx)); }
ETCH_API bool etch_automation_replay_frame(EtchContext* ctx, uint32_t index, uint32_t viewport_w, uint32_t viewport_h, float dpi_scale) {
    return etch::automation_replay_frame(to_ctx(ctx), index, viewport_w, viewport_h, dpi_scale);
}
ETCH_API uint32_t etch_automation_export_failure_report_json(const EtchContext* ctx, char* out_buf, uint32_t cap, struct SDL_Window* window) {
    return etch::automation_export_failure_report_json(to_ctx(ctx), out_buf, cap, window);
}

ETCH_API bool etch_table_row_selected(const EtchTableSelection* s,uint32_t row) {
    return s && etch::table_row_selected({s->focused_row,s->anchor_row,s->selected_words,s->word_count},row);
}
ETCH_API EtchTableResult etch_begin_table(const EtchTableDesc* desc) {
    if (!desc) return {};
    EtchTableDesc scratch{};
    const EtchTableDesc* d = resolve_c_desc(desc, &scratch, etch_table_desc_default);
    if (!d) return {};
    const etch::TableResult r = etch::begin_table({
        d->id,
        convert_frame_span<etch::TableColumnDesc, EtchTableColumnDesc>(d->columns, d->column_count, to_cpp),
        d->item_count, d->header_height_px, d->row_height_px, d->row_gap_px,
        d->overscan_rows, d->default_sort_column, d->default_sort_descending, d->cell_padding_x,
        to_cpp(d->width), to_cpp(d->height), to_cpp(d->padding), to_cpp(d->background),
        to_cpp(d->header_background), to_cpp(d->zebra_color), to_cpp(d->hover_row_color),
        to_cpp(d->border_color), d->border_width, to_cpp(d->radius), d->show_scrollbar, d->show_column_lines, d->header_font_size_px, d->selected_row, to_cpp(d->selected_row_color),d->horizontal_scroll,d->keyboard_navigation,{d->selection.focused_row,d->selection.anchor_row,d->selection.selected_words,d->selection.word_count},d->scroll_fade_px});
    return {from_cpp(r.slice), r.sort_column, r.sort_descending, r.sort_changed};
}
ETCH_API bool etch_table_next_row(uint32_t* out_row_index, EtchColor row_bg_override, EtchColor row_border_override) {
    return etch::table_next_row(out_row_index, to_cpp(row_bg_override), to_cpp(row_border_override));
}
ETCH_API EtchTableRowResult etch_table_row_result(uint32_t table_id, uint32_t row_index) {
    const auto r=etch::table_row_result(table_id,row_index);
    return {r.id,r.hovered,r.pressed,r.clicked,r.double_clicked,r.right_clicked};
}
ETCH_API bool etch_table_next_column(void) { return etch::table_next_column(); }
ETCH_API void etch_end_table(void) { etch::end_table(); }
ETCH_API float etch_table_column_width(uint32_t table_id, uint32_t col_index) { return etch::table_column_width(table_id, col_index); }
ETCH_API void etch_set_table_column_width(uint32_t table_id, uint32_t col_index, float width_px) { etch::set_table_column_width(table_id, col_index, width_px); }
ETCH_API void etch_set_table_sort(uint32_t table_id, uint32_t col_index, bool descending) { etch::set_table_sort(table_id, col_index, descending); }
ETCH_API void etch_table_scroll_to_row(uint32_t table_id, uint32_t row_index, float row_height_px, EtchAlignY align) {
    etch::table_scroll_to_row(table_id, row_index, row_height_px, static_cast<etch::AlignY>(align));
}
ETCH_API void etch_table_set_frozen_columns(uint32_t table_id, uint32_t frozen_count) {
    etch::table_set_frozen_columns(table_id, frozen_count);
}
ETCH_API uint32_t etch_table_frozen_columns(uint32_t table_id) {
    return etch::table_frozen_columns(table_id);
}
ETCH_API bool etch_table_set_column_order(uint32_t table_id, const uint32_t* order, uint32_t count) {
    if (!order && count) return false;
    return etch::table_set_column_order(table_id, std::span<const uint32_t>(order, count));
}
ETCH_API uint32_t etch_table_get_column_order(uint32_t table_id, uint32_t* out_order, uint32_t cap) {
    return etch::table_get_column_order(table_id, out_order, cap);
}

ETCH_API EtchTreeNodeResult etch_begin_tree_node(const EtchTreeNodeDesc* desc) {
    if (!desc) return {};
    const etch::TreeNodeResult r = etch::begin_tree_node({
        desc->id, to_cpp(desc->label), desc->icon_id, to_cpp(desc->icon_color), to_cpp(desc->text_color),
        to_cpp(desc->selected_bg), to_cpp(desc->selected_accent), to_cpp(desc->hover_bg),
        desc->row_height_px, desc->font_size_px, desc->indent_px, desc->font_id,
        desc->default_open, desc->selected, desc->leaf, desc->show_indent_guide});
    return {r.open, r.clicked, r.double_clicked, r.toggled, r.hovered};
}
ETCH_API void etch_end_tree_node(void) { etch::end_tree_node(); }
ETCH_API bool etch_is_tree_open(uint32_t node_id) { return etch::is_tree_open(node_id); }
ETCH_API void etch_set_tree_open(uint32_t node_id, bool open) { etch::set_tree_open(node_id, open); }

ETCH_API EtchCanvasState etch_begin_canvas(const EtchCanvasDesc* desc) {
    if (!desc) return {};
    EtchCanvasDesc scratch{};
    const EtchCanvasDesc* d = resolve_c_desc(desc, &scratch, etch_canvas_desc_default);
    if (!d) return {};
    return from_cpp(etch::begin_canvas({
        d->id, to_cpp(d->width), to_cpp(d->height), to_cpp(d->default_pan), d->default_zoom,
        d->min_zoom, d->max_zoom, d->grid_step_world, d->major_grid_subdiv,
        to_cpp(d->background), to_cpp(d->minor_grid_color), to_cpp(d->major_grid_color),
        to_cpp(d->border_color), d->border_width, to_cpp(d->radius), d->show_grid,
        d->interactive, d->marquee_select, to_cpp(d->marquee_fill), to_cpp(d->marquee_border)}));
}
ETCH_API void etch_end_canvas(void) { etch::end_canvas(); }
ETCH_API void etch_canvas_wire(const EtchCanvasWireDesc* desc) {
    if (!desc) return;
    etch::canvas_wire({to_cpp(desc->from_world), to_cpp(desc->to_world), to_cpp(desc->color_start),
                       to_cpp(desc->color_end), to_cpp(desc->glow_color), desc->thickness_world,
                       desc->glow_world, desc->tangent_bias, desc->z_index,
                       static_cast<etch::Axis>(desc->axis)});
}
ETCH_API EtchCanvasNodeResult etch_begin_canvas_node(const EtchCanvasNodeDesc* desc) {
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
ETCH_API void etch_end_canvas_node(void) { etch::end_canvas_node(); }
ETCH_API EtchCanvasState etch_canvas_state(uint32_t canvas_id) { return from_cpp(etch::canvas_state(canvas_id)); }
ETCH_API void etch_set_canvas_view(uint32_t canvas_id, EtchVec2 pan, float zoom) { etch::set_canvas_view(canvas_id, to_cpp(pan), zoom); }

ETCH_API void etch_set_debug_hud_open(EtchContext* ctx, bool open) { etch::set_debug_hud_open(to_ctx(ctx), open); }
ETCH_API bool etch_debug_hud_open(const EtchContext* ctx) { return etch::debug_hud_open(to_ctx(ctx)); }
ETCH_API void etch_set_debug_wireframe(EtchContext* ctx, bool enabled) { etch::set_debug_wireframe(to_ctx(ctx), enabled); }
ETCH_API bool etch_debug_wireframe(const EtchContext* ctx) { return etch::debug_wireframe(to_ctx(ctx)); }

ETCH_API EtchPresenceResult etch_presence(uint32_t id, bool mounted, EtchSpringSpec spec) {
    const auto r=etch::presence(id,mounted,to_cpp(spec)); return {r.visible,r.opacity,r.exiting};
}
ETCH_API void etch_set_time_scale(EtchContext* ctx, float scale) { etch::set_time_scale(to_ctx(ctx),scale); }
ETCH_API void etch_set_ui_zoom(EtchContext* ctx, float zoom) { etch::set_ui_zoom(to_ctx(ctx), zoom); }
ETCH_API float etch_ui_zoom(const EtchContext* ctx) { return etch::ui_zoom(to_ctx(ctx)); }

ETCH_API EtchLayerDesc etch_layer_desc_default(void) {
    const etch::LayerDesc d{};
    return {static_cast<uint32_t>(sizeof(EtchLayerDesc)),
            d.opacity, from_cpp(d.translate), from_cpp(d.scale), from_cpp(d.pivot_norm),
            from_cpp(d.pivot_px), d.animating, d.blur_px, d.blend_plus_lighter, d.rotation_rad,
            static_cast<EtchBlendMode>(d.blend_mode), from_cpp(d.alpha_mask)};
}
ETCH_API void etch_push_layer(const EtchLayerDesc* desc) {
    if (!desc) { etch::push_layer(etch::LayerDesc{}); return; }
    EtchLayerDesc scratch{};
    const EtchLayerDesc* d = resolve_c_desc(desc, &scratch, etch_layer_desc_default);
    if (!d) return;
    etch::push_layer({d->opacity, to_cpp(d->translate), to_cpp(d->scale), to_cpp(d->pivot_norm),
                      to_cpp(d->pivot_px), d->animating, d->blur_px, d->blend_plus_lighter, d->rotation_rad,
                      to_cpp(d->blend_mode), to_cpp(d->alpha_mask)});
}
ETCH_API void etch_pop_layer(void) {etch::pop_layer();}

ETCH_API EtchShaderSurfaceCreateDesc etch_shader_surface_create_desc_default(void) {
    EtchShaderSurfaceCreateDesc d = {};
    d.struct_size = static_cast<uint32_t>(sizeof(EtchShaderSurfaceCreateDesc));
    d.spirv.entrypoint = d.dxil.entrypoint = d.msl.entrypoint = "ps_main";
    d.max_fps = 60;
    return d;
}
ETCH_API EtchShaderSurfaceEvalDesc etch_shader_surface_eval_desc_default(void) {
    EtchShaderSurfaceEvalDesc d = {};
    d.struct_size = static_cast<uint32_t>(sizeof(EtchShaderSurfaceEvalDesc));
    d.width = d.height = 1;
    d.enabled = true;
    return d;
}
ETCH_API EtchShaderSurfaceId etch_create_shader_surface(EtchContext* ctx, const EtchShaderSurfaceCreateDesc* desc) {
    if (!desc) return 0;
    EtchShaderSurfaceCreateDesc scratch{};
    const EtchShaderSurfaceCreateDesc* d = resolve_c_desc(desc, &scratch, etch_shader_surface_create_desc_default);
    if (!d) return 0;
    return etch::create_shader_surface(to_ctx(ctx), {{d->spirv.data,d->spirv.size,d->spirv.entrypoint},
        {d->dxil.data,d->dxil.size,d->dxil.entrypoint}, {d->msl.data,d->msl.size,d->msl.entrypoint},
        d->continuous_time, d->max_fps, d->num_samplers, d->sampler_mirror_repeat, d->enable_feedback});
}
ETCH_API void etch_destroy_shader_surface(EtchContext* ctx, EtchShaderSurfaceId id) { etch::destroy_shader_surface(to_ctx(ctx),id); }
ETCH_API SDL_GPUTexture* etch_eval_shader_surface(EtchContext* ctx, EtchShaderSurfaceId id, const EtchShaderSurfaceEvalDesc* desc) {
    if (!desc) return nullptr;
    EtchShaderSurfaceEvalDesc scratch{};
    const EtchShaderSurfaceEvalDesc* d = resolve_c_desc(desc, &scratch, etch_shader_surface_eval_desc_default);
    if (!d) return nullptr;
    etch::ShaderSurfaceEvalDesc eval = {}; eval.width=d->width; eval.height=d->height; eval.revision=d->revision; eval.enabled=d->enabled;
    for (uint32_t i=0;i<4;++i) { eval.params[i]=to_cpp(d->params[i]); eval.textures[i]=d->textures[i]; }
    return etch::eval_shader_surface(to_ctx(ctx),id,eval);
}

ETCH_API EtchTerminalGridDesc etch_terminal_grid_desc_default(void) {
    const etch::TerminalGridDesc d;
    return {d.id,nullptr,0,d.columns,d.rows,d.first_row,d.cell_width_px,d.line_height_px,d.font_size_px,d.font_id,
        from_cpp(d.foreground),from_cpp(d.background),d.cursor_column,d.cursor_row,from_cpp(d.cursor_color),from_cpp(d.cursor_motion),
        static_cast<EtchTerminalCursorShape>(d.cursor_shape),d.cursor_visible,d.focusable,d.auto_fit_container,d.font_italic_id,
        d.selection_start_col,d.selection_start_row,d.selection_end_col,d.selection_end_row,d.selection_block,
        from_cpp(d.selection_color),d.hovered_hyperlink_id,nullptr,0u,
        nullptr,0u,from_cpp(d.search_match_color),from_cpp(d.search_active_color),d.interactive_selection,
        nullptr,0u,d.capture_keyboard_input,d.bracketed_paste,d.mouse_reporting};
}
ETCH_API EtchTerminalCell etch_terminal_cell_default(void) {
    return {' ',{ETCH_TERMINAL_DEFAULT,0},{ETCH_TERMINAL_DEFAULT,0},1,false,false,false,
            false,false,false,ETCH_TERMINAL_UNDERLINE_NONE,{ETCH_TERMINAL_DEFAULT,0},0u,nullptr,0u};
}
ETCH_API EtchTerminalGridResult etch_terminal_grid_ex(const EtchTerminalGridDesc* d) {
    if(!d || !d->cells) return {};
    static_assert(sizeof(EtchTerminalInlineImage) == sizeof(etch::TerminalInlineImage));
    static_assert(sizeof(EtchTerminalSearchMatch) == sizeof(etch::TerminalSearchMatch));
    const etch::TerminalGridDesc desc={.id=d->id,.columns=d->columns,.rows=d->rows,.first_row=d->first_row,
        .cell_width_px=d->cell_width_px,.line_height_px=d->line_height_px,.font_size_px=d->font_size_px,.font_id=d->font_id,
        .foreground=to_cpp(d->foreground),.background=to_cpp(d->background),.cursor_column=d->cursor_column,
        .cursor_row=d->cursor_row,.cursor_color=to_cpp(d->cursor_color),.cursor_motion=to_cpp(d->cursor_motion),
        .cursor_shape=static_cast<etch::TerminalCursorShape>(d->cursor_shape),.cursor_visible=d->cursor_visible,
        .focusable=d->focusable,.auto_fit_container=d->auto_fit_container,.font_italic_id=d->font_italic_id,
        .selection_start_col=d->selection_start_col,.selection_start_row=d->selection_start_row,
        .selection_end_col=d->selection_end_col,.selection_end_row=d->selection_end_row,
        .selection_block=d->selection_block,.selection_color=to_cpp(d->selection_color),
        .hovered_hyperlink_id=d->hovered_hyperlink_id,
        .inline_images=(d->inline_images && d->inline_image_count > 0u)
            ? std::span<const etch::TerminalInlineImage>(reinterpret_cast<const etch::TerminalInlineImage*>(d->inline_images), d->inline_image_count)
            : std::span<const etch::TerminalInlineImage>{},
        .search_matches=(d->search_matches && d->search_match_count > 0u)
            ? std::span<const etch::TerminalSearchMatch>(reinterpret_cast<const etch::TerminalSearchMatch*>(d->search_matches), d->search_match_count)
            : std::span<const etch::TerminalSearchMatch>{},
        .search_match_color=to_cpp(d->search_match_color),
        .search_active_color=to_cpp(d->search_active_color),
        .interactive_selection=d->interactive_selection,
        .ansi_palette=view_compatible_span<etch::Color>(d->ansi_palette, d->ansi_palette_count),
        .capture_keyboard_input=d->capture_keyboard_input,
        .bracketed_paste=d->bracketed_paste,
        .mouse_reporting=d->mouse_reporting};
    const etch::TerminalGridResult r = etch::c_bridge_terminal_grid(desc, d->cells, d->cell_count, read_c_terminal_cell);
    return {r.focused,r.hovered,r.clicked,r.right_clicked,r.hovered_col,r.hovered_row,
            r.clicked_col,r.clicked_row,r.hovered_hyperlink_id,r.clicked_hyperlink_id,
            r.visible_columns,r.visible_rows,from_cpp(r.ime_cursor_rect),
            r.has_selection,r.sel_start_col,r.sel_start_row,r.sel_end_col,r.sel_end_row,
            r.selection_changed,r.wheel_delta_y,
            from_cpp(r.typed_text),r.key_pressed,r.key_mods,r.key_repeat,from_cpp(r.pasted_text),
            r.mouse_moved,r.mouse_down,r.mouse_released,r.mouse_button,
            from_cpp(r.hovered_url),from_cpp(r.clicked_url)};
}
ETCH_API void etch_terminal_grid(const EtchTerminalGridDesc* d) {
    (void)etch_terminal_grid_ex(d);
}
ETCH_API uint32_t etch_terminal_encode_key(uint32_t keycode, uint16_t mods, bool app_cursor, char* out_buf, uint32_t cap) {
    return etch::terminal_encode_key(keycode, mods, app_cursor, out_buf, cap);
}
ETCH_API uint32_t etch_terminal_encode_mouse_sgr(uint8_t button, int32_t col, int32_t row, uint16_t mods, bool pressed, bool motion, char* out_buf, uint32_t cap) {
    return etch::terminal_encode_mouse_sgr(button, col, row, mods, pressed, motion, out_buf, cap);
}
ETCH_API EtchSheetDesc etch_sheet_desc_default(void) {
    const etch::SheetDesc d;
    EtchSheetDesc result={static_cast<uint32_t>(sizeof(EtchSheetDesc)),
        d.id,d.open,static_cast<EtchSheetEdge>(d.edge),d.extent_px,{},d.snap_count,nullptr,
        from_cpp(d.background),from_cpp(d.motion),d.z_index,nullptr,d.dismiss_on_backdrop,d.dismiss_on_escape,
        d.show_handle,from_cpp(d.backdrop_color),from_cpp(d.border_color),d.border_width,
        d.corner_radius_px,d.inset_px,d.backdrop_blur,d.top_inset_px,from_cpp(d.shadow_color),d.shadow_blur,from_cpp(d.shadow_offset)};
    for(uint32_t i=0;i<8;++i) result.snap_points[i]=d.snap_points[i];
    return result;
}
ETCH_API bool etch_begin_sheet(const EtchSheetDesc* desc) {
    if(!desc) return false;
    EtchSheetDesc scratch{};
    const EtchSheetDesc* d = resolve_c_desc(desc, &scratch, etch_sheet_desc_default);
    if (!d) return false;
    etch::SheetDesc cpp_desc={.id=d->id,.open=d->open,.edge=static_cast<etch::SheetEdge>(d->edge),.extent_px=d->extent_px,
        .snap_count=d->snap_count,.snap_index=d->snap_index,.background=to_cpp(d->background),.motion=to_cpp(d->motion),.z_index=d->z_index,
        .open_state=d->open_state,.dismiss_on_backdrop=d->dismiss_on_backdrop,.dismiss_on_escape=d->dismiss_on_escape,
        .show_handle=d->show_handle,.backdrop_color=to_cpp(d->backdrop_color),.border_color=to_cpp(d->border_color),
        .border_width=d->border_width,.corner_radius_px=d->corner_radius_px,.inset_px=d->inset_px,.backdrop_blur=d->backdrop_blur,
        .top_inset_px=d->top_inset_px,.shadow_color=to_cpp(d->shadow_color),.shadow_blur=d->shadow_blur,.shadow_offset=to_cpp(d->shadow_offset)};
    for(uint32_t i=0;i<8;++i) cpp_desc.snap_points[i]=d->snap_points[i];
    return etch::begin_sheet(cpp_desc);
}
ETCH_API void etch_end_sheet(void) {etch::end_sheet();}
