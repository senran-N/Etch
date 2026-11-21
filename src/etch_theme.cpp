// Semantic theme presets and scoped theme token resolution.
// Compiled via src/etch_unity.cpp.

namespace etch {

namespace {

bool color_eq(Color a, Color b) noexcept {
    return a.pack_rgba8() == b.pack_rgba8();
}

bool theme_equals(const Theme& a, const Theme& b) noexcept {
    return a.id == b.id && color_eq(a.on_primary,b.on_primary) && color_eq(a.on_error,b.on_error) &&
        color_eq(a.bg_canvas, b.bg_canvas) &&
        color_eq(a.bg_surface, b.bg_surface) &&
        color_eq(a.bg_elevated, b.bg_elevated) &&
        color_eq(a.bg_chrome, b.bg_chrome) &&
        color_eq(a.bg_popup, b.bg_popup) &&
        color_eq(a.bg_tooltip, b.bg_tooltip) &&
        color_eq(a.bg_input, b.bg_input) &&
        color_eq(a.bg_table, b.bg_table) &&
        color_eq(a.bg_node_canvas, b.bg_node_canvas) &&
        color_eq(a.surface_control, b.surface_control) &&
        color_eq(a.surface_hover, b.surface_hover) &&
        color_eq(a.surface_active, b.surface_active) &&
        color_eq(a.surface_disabled, b.surface_disabled) &&
        color_eq(a.surface_subtle, b.surface_subtle) &&
        color_eq(a.surface_subtle_hover, b.surface_subtle_hover) &&
        color_eq(a.surface_subtle_body, b.surface_subtle_body) &&
        color_eq(a.surface_selected, b.surface_selected) &&
        color_eq(a.text_primary, b.text_primary) &&
        color_eq(a.text_secondary, b.text_secondary) &&
        color_eq(a.text_body, b.text_body) &&
        color_eq(a.text_code, b.text_code) &&
        color_eq(a.text_muted, b.text_muted) &&
        color_eq(a.text_disabled, b.text_disabled) &&
        color_eq(a.accent, b.accent) &&
        color_eq(a.accent_secondary, b.accent_secondary) &&
        color_eq(a.accent_primary, b.accent_primary) &&
        color_eq(a.focus_ring, b.focus_ring) &&
        color_eq(a.danger, b.danger) &&
        color_eq(a.warning, b.warning) &&
        color_eq(a.border_subtle, b.border_subtle) &&
        color_eq(a.border_line, b.border_line) &&
        color_eq(a.border_input, b.border_input) &&
        color_eq(a.border_node, b.border_node) &&
        color_eq(a.border_control, b.border_control) &&
        color_eq(a.border_tooltip, b.border_tooltip) &&
        color_eq(a.selection, b.selection) &&
        color_eq(a.selection_hover, b.selection_hover) &&
        color_eq(a.zebra_row, b.zebra_row) &&
        color_eq(a.active_line, b.active_line) &&
        color_eq(a.area_fill_top, b.area_fill_top) &&
        color_eq(a.area_fill_bottom, b.area_fill_bottom) &&
        color_eq(a.divider_hover, b.divider_hover) &&
        color_eq(a.divider_thumb_hover, b.divider_thumb_hover) &&
        color_eq(a.grid_minor, b.grid_minor) &&
        color_eq(a.grid_major, b.grid_major) &&
        color_eq(a.canvas_border, b.canvas_border) &&
        color_eq(a.marquee_fill, b.marquee_fill) &&
        color_eq(a.marquee_border, b.marquee_border) &&
        color_eq(a.backdrop, b.backdrop) &&
        color_eq(a.shadow, b.shadow) &&
        color_eq(a.scrollbar_track, b.scrollbar_track) &&
        color_eq(a.scrollbar_thumb, b.scrollbar_thumb) &&
        color_eq(a.scrollbar_thumb_hover, b.scrollbar_thumb_hover) &&
        color_eq(a.scrollbar_thumb_drag, b.scrollbar_thumb_drag) &&
        color_eq(a.syntax_comment, b.syntax_comment) &&
        color_eq(a.syntax_string, b.syntax_string) &&
        color_eq(a.syntax_number, b.syntax_number) &&
        color_eq(a.syntax_keyword, b.syntax_keyword) &&
        a.radius_small_px == b.radius_small_px &&
        a.radius_control_px == b.radius_control_px &&
        a.radius_table_px == b.radius_table_px &&
        a.radius_input_px == b.radius_input_px &&
        a.radius_panel_px == b.radius_panel_px &&
        a.radius_pill_px == b.radius_pill_px;
}

const Theme& active_theme(const Context* ctx) noexcept {
    static const Theme kFallbackDark = Theme::dark();
    if (!ctx) {
        return kFallbackDark;
    }
    return (ctx->theme_depth > 0u) ? ctx->theme_stack[ctx->theme_depth - 1u] : ctx->base_theme;
}

} // namespace

Theme Theme::light() noexcept {
    return Theme{
        .id = ThemeId::Light,
        .bg_canvas = Color::rgb_hex(0xF8FAFC),
        .bg_surface = Color::rgb_hex(0xFFFFFF),
        .bg_elevated = Color::rgb_hex(0xF1F5F9),
        .bg_chrome = Color::rgb_hex(0xE2E8F0),
        .bg_popup = Color::rgb_hex(0xFFFFFF),
        .bg_tooltip = Color::rgb_hex(0xFFFFFF),
        .bg_input = Color::rgb_hex(0xFFFFFF),
        .bg_table = Color::rgb_hex(0xFFFFFF),
        .bg_node_canvas = Color::rgb_hex(0xF1F5F9),
        .surface_control = Color::rgb_hex(0xE2E8F0),
        .surface_hover = Color::rgb_hex(0xCBD5E1),
        .surface_active = Color::rgb_hex(0xFFFFFF),
        .surface_disabled = Color::rgb_hex(0xF1F5F9),
        .surface_subtle = Color::rgba8(226, 232, 240, 210),
        .surface_subtle_hover = Color::rgba8(203, 213, 225, 235),
        .surface_subtle_body = Color::rgba8(248, 250, 252, 220),
        .surface_selected = Color::rgb_hex(0xDBEAFE),
        .text_primary = Color::rgb_hex(0x0F172A),
        .text_secondary = Color::rgb_hex(0x1E293B),
        .text_body = Color::rgb_hex(0x0F172A),
        .text_code = Color::rgb_hex(0x334155),
        .text_muted = Color::rgb_hex(0x64748B),
        .text_disabled = Color::rgb_hex(0x94A3B8),
        .accent = Color::rgb_hex(0x0284C7),
        .accent_secondary = Color::rgb_hex(0x6366F1),
        .accent_primary = Color::rgb_hex(0x0369A1),
        .focus_ring = Color::rgb_hex(0x2563EB),
        .danger = Color::rgb_hex(0xDC2626),
        .warning = Color::rgb_hex(0xD97706),
        .border_subtle = Color::rgba8(15, 23, 42, 32),
        .border_line = Color::rgba8(15, 23, 42, 28),
        .border_input = Color::rgba8(15, 23, 42, 48),
        .border_node = Color::rgba8(15, 23, 42, 42),
        .border_control = Color::rgb_hex(0xCBD5E1),
        .border_tooltip = Color::rgb_hex(0x94A3B8),
        .selection = Color::rgba8(2, 132, 199, 72),
        .selection_hover = Color::rgba8(2, 132, 199, 24),
        .zebra_row = Color::rgba8(15, 23, 42, 10),
        .active_line = Color::rgba8(15, 23, 42, 12),
        .area_fill_top = Color::rgba8(2, 132, 199, 76),
        .area_fill_bottom = Color::rgba8(2, 132, 199, 0),
        .divider_hover = Color::rgba8(2, 132, 199, 165),
        .divider_thumb_hover = Color::rgba8(15, 23, 42, 180),
        .grid_minor = Color::rgba8(15, 23, 42, 22),
        .grid_major = Color::rgba8(15, 23, 42, 42),
        .canvas_border = Color::rgba8(15, 23, 42, 36),
        .marquee_fill = Color::rgba8(2, 132, 199, 40),
        .marquee_border = Color::rgba8(2, 132, 199, 210),
        .backdrop = Color::rgba8(15, 23, 42, 110),
        .shadow = Color::rgba8(15, 23, 42, 50),
        .scrollbar_track = Color::rgba8(15, 23, 42, 16),
        .scrollbar_thumb = Color::rgba8(100, 116, 139, 120),
        .scrollbar_thumb_hover = Color::rgba8(71, 85, 105, 185),
        .scrollbar_thumb_drag = Color::rgba8(2, 132, 199, 220),
        .syntax_comment = Color::rgb_hex(0x64748B),
        .syntax_string = Color::rgb_hex(0x15803D),
        .syntax_number = Color::rgb_hex(0x7E22CE),
        .syntax_keyword = Color::rgb_hex(0x0369A1),
    };
}

Theme Theme::high_contrast() noexcept {
    return Theme{
        .id = ThemeId::HighContrast,
        .bg_canvas = Color::rgb_hex(0x000000),
        .bg_surface = Color::rgb_hex(0x000000),
        .bg_elevated = Color::rgb_hex(0x0A0A0A),
        .bg_chrome = Color::rgb_hex(0x000000),
        .bg_popup = Color::rgb_hex(0x000000),
        .bg_tooltip = Color::rgb_hex(0x000000),
        .bg_input = Color::rgb_hex(0x000000),
        .bg_table = Color::rgb_hex(0x000000),
        .bg_node_canvas = Color::rgb_hex(0x000000),
        .surface_control = Color::rgb_hex(0x141414),
        .surface_hover = Color::rgb_hex(0x262626),
        .surface_active = Color::rgb_hex(0x1F2937),
        .surface_disabled = Color::rgb_hex(0x0D0D0D),
        .surface_subtle = Color::rgb_hex(0x121212),
        .surface_subtle_hover = Color::rgb_hex(0x262626),
        .surface_subtle_body = Color::rgb_hex(0x050505),
        .surface_selected = Color::rgb_hex(0x083344),
        .text_primary = Color::white(),
        .text_secondary = Color::white(),
        .text_body = Color::white(),
        .text_code = Color::white(),
        .text_muted = Color::rgb_hex(0xE5E7EB),
        .text_disabled = Color::rgb_hex(0x9CA3AF),
        .accent = Color::rgb_hex(0x22D3EE),
        .accent_secondary = Color::rgb_hex(0xFACC15),
        .accent_primary = Color::rgb_hex(0x22D3EE),
        .focus_ring = Color::rgb_hex(0xFACC15),
        .danger = Color::rgb_hex(0xDC2626),
        .warning = Color::rgb_hex(0xFACC15),
        .border_subtle = Color::rgba8(255, 255, 255, 190),
        .border_line = Color::rgba8(255, 255, 255, 180),
        .border_input = Color::rgba8(255, 255, 255, 220),
        .border_node = Color::rgba8(255, 255, 255, 230),
        .border_control = Color::white(),
        .border_tooltip = Color::white(),
        .selection = Color::rgba8(34, 211, 238, 128),
        .selection_hover = Color::rgba8(34, 211, 238, 56),
        .zebra_row = Color::rgba8(255, 255, 255, 24),
        .active_line = Color::rgba8(255, 255, 255, 32),
        .area_fill_top = Color::rgba8(34, 211, 238, 128),
        .area_fill_bottom = Color::rgba8(34, 211, 238, 0),
        .divider_hover = Color::rgb_hex(0xFACC15),
        .divider_thumb_hover = Color::white(),
        .grid_minor = Color::rgba8(255, 255, 255, 56),
        .grid_major = Color::rgba8(255, 255, 255, 110),
        .canvas_border = Color::white(),
        .marquee_fill = Color::rgba8(34, 211, 238, 80),
        .marquee_border = Color::rgb_hex(0xFACC15),
        .backdrop = Color::rgba8(0, 0, 0, 210),
        .shadow = Color::rgba8(0, 0, 0, 200),
        .scrollbar_track = Color::rgba8(255, 255, 255, 36),
        .scrollbar_thumb = Color::rgba8(255, 255, 255, 195),
        .scrollbar_thumb_hover = Color::rgb_hex(0x22D3EE),
        .scrollbar_thumb_drag = Color::rgb_hex(0xFACC15),
        .syntax_comment = Color::rgb_hex(0x9CA3AF),
        .syntax_string = Color::rgb_hex(0x86EFAC),
        .syntax_number = Color::rgb_hex(0xFDE047),
        .syntax_keyword = Color::rgb_hex(0x67E8F9),
        .radius_small_px = 2.0f,
        .radius_control_px = 2.0f,
        .radius_table_px = 2.0f,
        .radius_input_px = 2.0f,
        .radius_panel_px = 2.0f,
        .radius_pill_px = 999.0f,
        .on_primary = Color::rgba8(9,13,22,255),
    };
}

Theme Theme::industrial() noexcept {
    return Theme{
        .id = ThemeId::Industrial,
        .bg_canvas = Color::rgb_hex(0x121316),
        .bg_surface = Color::rgb_hex(0x181A1F),
        .bg_elevated = Color::rgb_hex(0x20232A),
        .bg_chrome = Color::rgb_hex(0x0E1013),
        .bg_popup = Color::rgb_hex(0x181A1F),
        .bg_tooltip = Color::rgb_hex(0x121418),
        .bg_input = Color::rgb_hex(0x101216),
        .bg_table = Color::rgb_hex(0x14161B),
        .bg_node_canvas = Color::rgb_hex(0x101216),
        .surface_control = Color::rgb_hex(0x2A2E37),
        .surface_hover = Color::rgb_hex(0x373C47),
        .surface_active = Color::rgb_hex(0x22262F),
        .surface_disabled = Color::rgb_hex(0x1A1C22),
        .surface_subtle = Color::rgba8(38, 42, 50, 215),
        .surface_subtle_hover = Color::rgba8(55, 60, 71, 235),
        .surface_subtle_body = Color::rgba8(20, 22, 27, 200),
        .surface_selected = Color::rgb_hex(0x3B2F18),
        .text_primary = Color::rgb_hex(0xF3F4F6),
        .text_secondary = Color::rgb_hex(0xE5E7EB),
        .text_body = Color::rgb_hex(0xF3F4F6),
        .text_code = Color::rgb_hex(0xD1D5DB),
        .text_muted = Color::rgb_hex(0x9CA3AF),
        .text_disabled = Color::rgb_hex(0x6B7280),
        .accent = Color::rgb_hex(0xF59E0B),
        .accent_secondary = Color::rgb_hex(0xFB923C),
        .accent_primary = Color::rgb_hex(0xF59E0B),
        .focus_ring = Color::rgb_hex(0xFBBF24),
        .danger = Color::rgb_hex(0xDC2626),
        .warning = Color::rgb_hex(0xFBBF24),
        .border_subtle = Color::rgba8(156, 163, 175, 48),
        .border_line = Color::rgba8(156, 163, 175, 44),
        .border_input = Color::rgba8(156, 163, 175, 68),
        .border_node = Color::rgba8(156, 163, 175, 72),
        .border_control = Color::rgb_hex(0x374151),
        .border_tooltip = Color::rgb_hex(0x4B5563),
        .selection = Color::rgba8(245, 158, 11, 88),
        .selection_hover = Color::rgba8(245, 158, 11, 28),
        .zebra_row = Color::rgba8(255, 255, 255, 12),
        .active_line = Color::rgba8(245, 158, 11, 20),
        .area_fill_top = Color::rgba8(245, 158, 11, 96),
        .area_fill_bottom = Color::rgba8(245, 158, 11, 0),
        .divider_hover = Color::rgba8(245, 158, 11, 180),
        .divider_thumb_hover = Color::rgb_hex(0xE5E7EB),
        .grid_minor = Color::rgba8(156, 163, 175, 34),
        .grid_major = Color::rgba8(156, 163, 175, 56),
        .canvas_border = Color::rgba8(156, 163, 175, 64),
        .marquee_fill = Color::rgba8(245, 158, 11, 44),
        .marquee_border = Color::rgba8(251, 191, 36, 220),
        .backdrop = Color::rgba8(0, 0, 0, 165),
        .shadow = Color::rgba8(0, 0, 0, 155),
        .scrollbar_track = Color::rgba8(255, 255, 255, 18),
        .scrollbar_thumb = Color::rgba8(156, 163, 175, 130),
        .scrollbar_thumb_hover = Color::rgba8(245, 158, 11, 190),
        .scrollbar_thumb_drag = Color::rgb_hex(0xFBBF24),
        .syntax_comment = Color::rgb_hex(0x6B7280),
        .syntax_string = Color::rgb_hex(0x84CC16),
        .syntax_number = Color::rgb_hex(0xFBBF24),
        .syntax_keyword = Color::rgb_hex(0xFB923C),
        .radius_small_px = 2.0f,
        .radius_control_px = 2.0f,
        .radius_table_px = 3.0f,
        .radius_input_px = 2.0f,
        .radius_panel_px = 3.0f,
        .radius_pill_px = 4.0f,
        .on_primary = Color::rgba8(9,13,22,255),
    };
}

void set_theme(const Theme& t, Context* ctx) noexcept {
    Context* target = ctx ? ctx : current_context();
    if (!target) {
        return;
    }
    if (!theme_equals(target->base_theme, t)) {
        target->base_theme = t;
        ++target->theme_epoch;
        if (target->headless || target->claimed_window_count == 0u) {
            target->headless_ui.redraw_budget = 2u;
        }
        for (uint32_t i = 0; i < target->claimed_window_cap; ++i) {
            if (ClaimedWindowSlot* slot = target->claimed_windows[i]) {
                slot->ui.redraw_budget = 2u;
                apply_platform_window_chrome(target, slot);
            }
        }
    }
}

const Theme& theme(const Context* ctx) noexcept {
    const Context* target = ctx ? ctx : current_context();
    return active_theme(target);
}

void push_theme(const Theme& t) noexcept {
    Context* ctx = current_context();
    if (!ctx) {
        return;
    }
    if (ctx->theme_depth < Context::kMaxThemeStack) {
        ctx->theme_stack[ctx->theme_depth++] = t;
    } else {
        ++ctx->theme_overflow;
    }
}

void pop_theme() noexcept {
    Context* ctx = current_context();
    if (!ctx) {
        return;
    }
    if (ctx->theme_overflow > 0u) {
        --ctx->theme_overflow;
    } else if (ctx->theme_depth > 0u) {
        --ctx->theme_depth;
    }
}

} // namespace etch
