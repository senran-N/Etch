// Perceptual color conversion; matrices from Bjorn Ottosson's Oklab reference.
// https://bottosson.github.io/posts/oklab/ (public domain).
// Compiled via src/etch_unity.cpp or standalone when ETCH_UNITY_BUILD=OFF.
#include "etch/etch.h"
#include <cmath>

namespace etch {
namespace {
struct PerceptualColor { float L, a, b; };
float color_linear(float s) noexcept {
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}
float color_encoded(float v) noexcept {
    return v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}
PerceptualColor color_to_lab(Color color) noexcept {
    const float r=color_linear(color.r/255.f), g=color_linear(color.g/255.f), b=color_linear(color.b/255.f);
    const float l=std::cbrt(.4122214708f*r+.5363325363f*g+.0514459929f*b);
    const float m=std::cbrt(.2119034982f*r+.6806995451f*g+.1073969566f*b);
    const float s=std::cbrt(.0883024619f*r+.2817188376f*g+.6299787005f*b);
    return {.2104542553f*l+.7936177850f*m-.0040720468f*s,
            1.9779984951f*l-2.4285922050f*m+.4505937099f*s,
            .0259040371f*l+.7827717662f*m-.8086757660f*s};
}
Color color_from_lab_raw(PerceptualColor c, float alpha, bool* in_gamut_out = nullptr) noexcept {
    float l=c.L+.3963377774f*c.a+.2158037573f*c.b;
    float m=c.L-.1055613458f*c.a-.0638541728f*c.b;
    float s=c.L-.0894841775f*c.a-1.2914855480f*c.b;
    l=l*l*l; m=m*m*m; s=s*s*s;
    const float r=4.0767416621f*l-3.3077115913f*m+.2309699292f*s;
    const float g=-1.2684380046f*l+2.6097574011f*m-.3413193965f*s;
    const float b=-.0041960863f*l-.7034186147f*m+1.7076147010f*s;
    if (in_gamut_out) {
        *in_gamut_out = (r >= -1e-4f && r <= 1.0001f && g >= -1e-4f && g <= 1.0001f && b >= -1e-4f && b <= 1.0001f);
    }
    return Color::rgba(color_encoded(r), color_encoded(g), color_encoded(b), alpha);
}
Color color_from_lab(PerceptualColor c, float alpha) noexcept {
    bool in_gamut = false;
    const Color raw = color_from_lab_raw(c, alpha, &in_gamut);
    if (in_gamut || (c.a == 0.0f && c.b == 0.0f)) return raw;
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 10; ++i) {
        const float mid = 0.5f * (lo + hi);
        bool ok = false;
        (void)color_from_lab_raw({c.L, c.a * mid, c.b * mid}, alpha, &ok);
        if (ok) lo = mid; else hi = mid;
    }
    return color_from_lab_raw({c.L, c.a * lo, c.b * lo}, alpha);
}
Color seed_token(Color original, float hue, float chroma) noexcept {
    const auto lab=color_to_lab(original);
    const float original_chroma=std::sqrt(lab.a*lab.a+lab.b*lab.b);
    return Color::oklch(lab.L, std::fmin(original_chroma,chroma),hue,original.a/255.f);
}
Color harmonize_syntax_token(Color original, float seed_hue, float chroma_scale) noexcept {
    const auto lab = color_to_lab(original);
    const float c = std::sqrt(lab.a * lab.a + lab.b * lab.b);
    const float orig_hue = std::atan2(lab.b, lab.a) * 57.29577951308232f;
    const float delta = std::fmod(seed_hue - orig_hue + 540.0f, 360.0f) - 180.0f;
    const float syn_c = (chroma_scale <= 0.0f) ? 0.0f : std::fmin(c * std::fmin(chroma_scale, 1.35f), 0.24f);
    return Color::oklch(lab.L, syn_c, orig_hue + delta * 0.15f, original.a / 255.0f);
}
Color readable_on(Color background) noexcept {
    const float y=.2126f*color_linear(background.r/255.f)+.7152f*color_linear(background.g/255.f)+.0722f*color_linear(background.b/255.f);
    return y > .179f ? Color::black() : Color::white();
}
} // namespace
Color Color::oklch(float L, float C, float hue_deg, float alpha) noexcept {
    if (!std::isfinite(L) || !std::isfinite(C) || !std::isfinite(hue_deg) || !std::isfinite(alpha)) return {};
    L=std::fmin(1.f,std::fmax(0.f,L)); C=std::fmin(1.f,std::fmax(0.f,C));
    const float angle=std::fmod(hue_deg,360.f)*.017453292519943295f;
    return color_from_lab({L,C*std::cos(angle),C*std::sin(angle)},alpha);
}
Color Color::lerp_oklab(Color a, Color b, float t) noexcept {
    if (!std::isfinite(t)) return a;
    if (t<=0) return a;
    if (t>=1) return b;
    const auto x=color_to_lab(a), y=color_to_lab(b);
    return color_from_lab({x.L+(y.L-x.L)*t,x.a+(y.a-x.a)*t,x.b+(y.b-x.b)*t},(a.a+(b.a-a.a)*t)/255.f);
}
Theme Theme::from_seed(Color seed, bool dark_mode, float chroma_scale) noexcept {
    if (!std::isfinite(chroma_scale)) chroma_scale=1;
    chroma_scale=std::fmin(4.f,std::fmax(0.f,chroma_scale));
    const auto lab=color_to_lab(seed);
    const float hue=std::atan2(lab.b,lab.a)*57.29577951308232f;
    const float chroma=std::sqrt(lab.a*lab.a+lab.b*lab.b)*chroma_scale;
    Theme result=dark_mode ? Theme::dark() : Theme::light();
    result.bg_canvas=seed_token(result.bg_canvas,hue,chroma);
    result.bg_surface=seed_token(result.bg_surface,hue,chroma);
    result.bg_elevated=seed_token(result.bg_elevated,hue,chroma);
    result.bg_chrome=seed_token(result.bg_chrome,hue,chroma);
    result.bg_popup=seed_token(result.bg_popup,hue,chroma);
    result.bg_tooltip=seed_token(result.bg_tooltip,hue,chroma);
    result.bg_input=seed_token(result.bg_input,hue,chroma);
    result.bg_table=seed_token(result.bg_table,hue,chroma);
    result.bg_node_canvas=seed_token(result.bg_node_canvas,hue,chroma);
    result.surface_control=seed_token(result.surface_control,hue,chroma);
    result.surface_hover=seed_token(result.surface_hover,hue,chroma);
    result.surface_active=seed_token(result.surface_active,hue,chroma);
    result.surface_disabled=seed_token(result.surface_disabled,hue,chroma);
    result.surface_subtle=seed_token(result.surface_subtle,hue,chroma);
    result.surface_subtle_hover=seed_token(result.surface_subtle_hover,hue,chroma);
    result.surface_subtle_body=seed_token(result.surface_subtle_body,hue,chroma);
    result.surface_selected=seed_token(result.surface_selected,hue,chroma);
    result.text_primary=seed_token(result.text_primary,hue,chroma);
    result.text_secondary=seed_token(result.text_secondary,hue,chroma);
    result.text_body=seed_token(result.text_body,hue,chroma);
    result.text_code=seed_token(result.text_code,hue,chroma);
    result.text_muted=seed_token(result.text_muted,hue,chroma);
    result.text_disabled=seed_token(result.text_disabled,hue,chroma);
    result.accent=seed_token(result.accent,hue,chroma);
    result.accent_secondary=seed_token(result.accent_secondary,hue,chroma);
    result.accent_primary=seed_token(result.accent_primary,hue,chroma);
    result.focus_ring=seed_token(result.focus_ring,hue,chroma);
    result.border_subtle=seed_token(result.border_subtle,hue,chroma);
    result.border_line=seed_token(result.border_line,hue,chroma);
    result.border_input=seed_token(result.border_input,hue,chroma);
    result.border_node=seed_token(result.border_node,hue,chroma);
    result.border_control=seed_token(result.border_control,hue,chroma);
    result.border_tooltip=seed_token(result.border_tooltip,hue,chroma);
    result.selection=seed_token(result.selection,hue,chroma);
    result.selection_hover=seed_token(result.selection_hover,hue,chroma);
    result.zebra_row=seed_token(result.zebra_row,hue,chroma);
    result.active_line=seed_token(result.active_line,hue,chroma);
    result.area_fill_top=seed_token(result.area_fill_top,hue,chroma);
    result.area_fill_bottom=seed_token(result.area_fill_bottom,hue,chroma);
    result.divider_hover=seed_token(result.divider_hover,hue,chroma);
    result.divider_thumb_hover=seed_token(result.divider_thumb_hover,hue,chroma);
    result.grid_minor=seed_token(result.grid_minor,hue,chroma);
    result.grid_major=seed_token(result.grid_major,hue,chroma);
    result.canvas_border=seed_token(result.canvas_border,hue,chroma);
    result.marquee_fill=seed_token(result.marquee_fill,hue,chroma);
    result.marquee_border=seed_token(result.marquee_border,hue,chroma);
    result.backdrop=seed_token(result.backdrop,hue,chroma);
    result.shadow=seed_token(result.shadow,hue,chroma);
    result.scrollbar_track=seed_token(result.scrollbar_track,hue,chroma);
    result.scrollbar_thumb=seed_token(result.scrollbar_thumb,hue,chroma);
    result.scrollbar_thumb_hover=seed_token(result.scrollbar_thumb_hover,hue,chroma);
    result.scrollbar_thumb_drag=seed_token(result.scrollbar_thumb_drag,hue,chroma);
    result.syntax_comment=seed_token(result.syntax_comment,hue,chroma);
    result.syntax_string=harmonize_syntax_token(result.syntax_string,hue,chroma_scale);
    result.syntax_number=harmonize_syntax_token(result.syntax_number,hue,chroma_scale);
    result.syntax_keyword=seed_token(result.syntax_keyword,hue,chroma);
    result.text_primary=readable_on(result.bg_canvas);
    result.on_primary=readable_on(result.accent_primary);
    result.on_error=readable_on(result.danger);
    return result;
}
Vec4 color_to_oklab_internal(Color c) noexcept {
    const auto lab = color_to_lab(c);
    return {lab.L, lab.a, lab.b, c.a / 255.0f};
}
Color color_from_oklab_internal(Vec4 laba) noexcept {
    return color_from_lab({laba.x, laba.y, laba.z}, laba.w);
}
Theme Theme::lerp_oklab(const Theme& a, const Theme& b, float t) noexcept {
    if (!std::isfinite(t) || t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    const auto lc = [t](Color ca, Color cb) noexcept -> Color {
        return Color::lerp_oklab(ca, cb, t);
    };
    const auto lf = [t](float fa, float fb) noexcept -> float {
        return fa + (fb - fa) * t;
    };
    Theme r = {};
    r.id = (a.id == b.id) ? a.id : ((t < 0.5f) ? a.id : b.id);
    r.bg_canvas = lc(a.bg_canvas, b.bg_canvas);
    r.bg_surface = lc(a.bg_surface, b.bg_surface);
    r.bg_elevated = lc(a.bg_elevated, b.bg_elevated);
    r.bg_chrome = lc(a.bg_chrome, b.bg_chrome);
    r.bg_popup = lc(a.bg_popup, b.bg_popup);
    r.bg_tooltip = lc(a.bg_tooltip, b.bg_tooltip);
    r.bg_input = lc(a.bg_input, b.bg_input);
    r.bg_table = lc(a.bg_table, b.bg_table);
    r.bg_node_canvas = lc(a.bg_node_canvas, b.bg_node_canvas);
    r.surface_control = lc(a.surface_control, b.surface_control);
    r.surface_hover = lc(a.surface_hover, b.surface_hover);
    r.surface_active = lc(a.surface_active, b.surface_active);
    r.surface_disabled = lc(a.surface_disabled, b.surface_disabled);
    r.surface_subtle = lc(a.surface_subtle, b.surface_subtle);
    r.surface_subtle_hover = lc(a.surface_subtle_hover, b.surface_subtle_hover);
    r.surface_subtle_body = lc(a.surface_subtle_body, b.surface_subtle_body);
    r.surface_selected = lc(a.surface_selected, b.surface_selected);
    r.text_primary = lc(a.text_primary, b.text_primary);
    r.text_secondary = lc(a.text_secondary, b.text_secondary);
    r.text_body = lc(a.text_body, b.text_body);
    r.text_code = lc(a.text_code, b.text_code);
    r.text_muted = lc(a.text_muted, b.text_muted);
    r.text_disabled = lc(a.text_disabled, b.text_disabled);
    r.accent = lc(a.accent, b.accent);
    r.accent_secondary = lc(a.accent_secondary, b.accent_secondary);
    r.accent_primary = lc(a.accent_primary, b.accent_primary);
    r.focus_ring = lc(a.focus_ring, b.focus_ring);
    r.danger = lc(a.danger, b.danger);
    r.warning = lc(a.warning, b.warning);
    r.border_subtle = lc(a.border_subtle, b.border_subtle);
    r.border_line = lc(a.border_line, b.border_line);
    r.border_input = lc(a.border_input, b.border_input);
    r.border_node = lc(a.border_node, b.border_node);
    r.border_control = lc(a.border_control, b.border_control);
    r.border_tooltip = lc(a.border_tooltip, b.border_tooltip);
    r.selection = lc(a.selection, b.selection);
    r.selection_hover = lc(a.selection_hover, b.selection_hover);
    r.zebra_row = lc(a.zebra_row, b.zebra_row);
    r.active_line = lc(a.active_line, b.active_line);
    r.area_fill_top = lc(a.area_fill_top, b.area_fill_top);
    r.area_fill_bottom = lc(a.area_fill_bottom, b.area_fill_bottom);
    r.divider_hover = lc(a.divider_hover, b.divider_hover);
    r.divider_thumb_hover = lc(a.divider_thumb_hover, b.divider_thumb_hover);
    r.grid_minor = lc(a.grid_minor, b.grid_minor);
    r.grid_major = lc(a.grid_major, b.grid_major);
    r.canvas_border = lc(a.canvas_border, b.canvas_border);
    r.marquee_fill = lc(a.marquee_fill, b.marquee_fill);
    r.marquee_border = lc(a.marquee_border, b.marquee_border);
    r.backdrop = lc(a.backdrop, b.backdrop);
    r.shadow = lc(a.shadow, b.shadow);
    r.scrollbar_track = lc(a.scrollbar_track, b.scrollbar_track);
    r.scrollbar_thumb = lc(a.scrollbar_thumb, b.scrollbar_thumb);
    r.scrollbar_thumb_hover = lc(a.scrollbar_thumb_hover, b.scrollbar_thumb_hover);
    r.scrollbar_thumb_drag = lc(a.scrollbar_thumb_drag, b.scrollbar_thumb_drag);
    r.syntax_comment = lc(a.syntax_comment, b.syntax_comment);
    r.syntax_string = lc(a.syntax_string, b.syntax_string);
    r.syntax_number = lc(a.syntax_number, b.syntax_number);
    r.syntax_keyword = lc(a.syntax_keyword, b.syntax_keyword);
    r.radius_small_px = lf(a.radius_small_px, b.radius_small_px);
    r.radius_control_px = lf(a.radius_control_px, b.radius_control_px);
    r.radius_table_px = lf(a.radius_table_px, b.radius_table_px);
    r.radius_input_px = lf(a.radius_input_px, b.radius_input_px);
    r.radius_panel_px = lf(a.radius_panel_px, b.radius_panel_px);
    r.radius_pill_px = lf(a.radius_pill_px, b.radius_pill_px);
    r.on_primary = lc(a.on_primary, b.on_primary);
    r.on_error = lc(a.on_error, b.on_error);
    return r;
}
} // namespace etch
