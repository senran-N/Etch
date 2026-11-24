// Semantic UI automation queries, deterministic clock & frame input recording (#47).
// Compiled via src/etch_unity.cpp.

namespace etch {

struct InputRecordRing {
    static constexpr uint32_t kCapacity = 1024u;
    RecordedFrameInput items[kCapacity] = {};
    uint32_t head = 0u;
    uint32_t count = 0u;
};

namespace {

char auto_ascii_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

bool auto_contains_ci(Str hay, Str needle) noexcept {
    if (needle.length == 0u) return true;
    if (!hay.chars || !needle.chars || needle.length > hay.length) return false;
    const uint32_t limit = hay.length - needle.length;
    const char n0 = auto_ascii_lower(needle.chars[0]);
    for (uint32_t i = 0u; i <= limit; ++i) {
        if (auto_ascii_lower(hay.chars[i]) != n0) continue;
        bool match = true;
        for (uint32_t j = 1u; j < needle.length; ++j) {
            if (auto_ascii_lower(hay.chars[i + j]) != auto_ascii_lower(needle.chars[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool auto_node_matches(const A11yNode& node, const AutomationQuery& query) noexcept {
    if (query.role != A11yRole::None && node.role != query.role) return false;
    if (query.label_contains.length > 0u && !auto_contains_ci(node.label, query.label_contains)) return false;
    if ((node.state_flags & query.required_states) != query.required_states) return false;
    if ((node.state_flags & query.forbidden_states) != 0u) return false;
    return true;
}

struct AutoJsonWriter {
    char* buf = nullptr;
    uint32_t cap = 0u;
    uint32_t total = 0u;

    void append_raw(const char* src, uint32_t len) noexcept {
        if (buf && cap > 1u && total < cap - 1u) {
            const uint32_t avail = (cap - 1u) - total;
            const uint32_t copy_n = (len < avail) ? len : avail;
            if (copy_n > 0u) std::memcpy(buf + total, src, copy_n);
        }
        total += len;
    }

    void append_char(char ch) noexcept {
        append_raw(&ch, 1u);
    }

    void append_cstr(const char* src) noexcept {
        append_raw(src, static_cast<uint32_t>(std::strlen(src)));
    }

    void append_u32(uint32_t v) noexcept {
        char tmp[16];
        const int n = std::snprintf(tmp, sizeof(tmp), "%u", v);
        if (n > 0) append_raw(tmp, static_cast<uint32_t>(n));
    }

    void append_float(float v, const char* fmt) noexcept {
        const double d = std::isfinite(v) ? static_cast<double>(v) : 0.0;
        char tmp[32];
        const int n = std::snprintf(tmp, sizeof(tmp), fmt, d);
        if (n > 0) {
            const uint32_t clamped = static_cast<uint32_t>(
                static_cast<size_t>(n) < sizeof(tmp) ? static_cast<size_t>(n) : (sizeof(tmp) - 1u));
            append_raw(tmp, clamped);
        }
    }

    void append_escaped(const char* chars, uint32_t len) noexcept {
        append_char('"');
        if (chars && len > 0u) {
            for (uint32_t i = 0u; i < len; ++i) {
                const unsigned char ch = static_cast<unsigned char>(chars[i]);
                switch (ch) {
                    case '"':  append_raw("\\\"", 2u); break;
                    case '\\': append_raw("\\\\", 2u); break;
                    case '\b': append_raw("\\b", 2u); break;
                    case '\f': append_raw("\\f", 2u); break;
                    case '\n': append_raw("\\n", 2u); break;
                    case '\r': append_raw("\\r", 2u); break;
                    case '\t': append_raw("\\t", 2u); break;
                    default:
                        if (ch < 0x20u) {
                            char esc[7];
                            std::snprintf(esc, sizeof(esc), "\\u%04X", static_cast<unsigned int>(ch));
                            append_raw(esc, 6u);
                        } else {
                            const char out_c = static_cast<char>(ch);
                            append_raw(&out_c, 1u);
                        }
                        break;
                }
            }
        }
        append_char('"');
    }

    void finish() noexcept {
        if (buf && cap > 0u) {
            const uint32_t term = (total < cap - 1u) ? total : (cap - 1u);
            buf[term] = '\0';
        }
    }
};

} // namespace

void free_automation_recording(Context* ctx) noexcept {
    if (!ctx) return;
    std::free(ctx->input_record_ring);
    ctx->input_record_ring = nullptr;
    ctx->automation_recording = false;
}

void automation_record_frame_input(Context* ctx, const RecordedFrameInput& input) noexcept {
    if (!ctx || !ctx->automation_recording) return;
    if (!ctx->input_record_ring) {
        ctx->input_record_ring = static_cast<InputRecordRing*>(std::calloc(1u, sizeof(InputRecordRing)));
        if (!ctx->input_record_ring) return;
    }
    RecordedFrameInput recorded = input;
    if (ctx->active_ui && ctx->active_ui->frame_text_utf8 &&
        ctx->active_ui->frame_text_utf8_len > 0u && recorded.typed_utf8_len == 0u) {
        const WindowUiState* ui = ctx->active_ui;
        const uint32_t raw_cap = (ui->frame_text_utf8_len < 63u) ? ui->frame_text_utf8_len : 63u;
        const uint32_t copy_len = utf8_clamp_boundary(ui->frame_text_utf8, ui->frame_text_utf8_len, raw_cap);
        if (copy_len > 0u) {
            std::memcpy(recorded.typed_utf8, ui->frame_text_utf8, copy_len);
        }
        recorded.typed_utf8[copy_len] = '\0';
        recorded.typed_utf8_len = copy_len;
    } else {
        const uint32_t clamped = (recorded.typed_utf8_len < 63u) ? recorded.typed_utf8_len : 63u;
        recorded.typed_utf8[clamped] = '\0';
        recorded.typed_utf8_len = clamped;
    }
    InputRecordRing* ring = ctx->input_record_ring;
    if (ring->count < InputRecordRing::kCapacity) {
        const uint32_t tail = (ring->head + ring->count) % InputRecordRing::kCapacity;
        ring->items[tail] = recorded;
        ++ring->count;
    } else {
        ring->items[ring->head] = recorded;
        ring->head = (ring->head + 1u) % InputRecordRing::kCapacity;
    }
}

uint32_t automation_find_nodes(
    const Context* ctx,
    const AutomationQuery& query,
    uint32_t* out_ids,
    uint32_t max_ids,
    SDL_Window* window) noexcept
{
    const Context* c = ctx ? ctx : current_context();
    if (!c) return 0u;
    if (query.label_contains.length > 0u && !query.label_contains.chars) return 0u;
    const std::span<const A11yNode> nodes = frame_a11y_nodes(c, window);
    uint32_t found = 0u;
    for (const A11yNode& node : nodes) {
        if (!auto_node_matches(node, query)) continue;
        if (out_ids) {
            if (found >= max_ids) break;
            out_ids[found++] = node.id;
        } else {
            ++found;
        }
    }
    return found;
}

bool automation_assert_node(
    const Context* ctx,
    const AutomationQuery& query,
    A11yNode* out_node,
    SDL_Window* window) noexcept
{
    const Context* c = ctx ? ctx : current_context();
    if (!c) return false;
    if (query.label_contains.length > 0u && !query.label_contains.chars) return false;
    const std::span<const A11yNode> nodes = frame_a11y_nodes(c, window);
    for (const A11yNode& node : nodes) {
        if (!auto_node_matches(node, query)) continue;
        if (out_node) *out_node = node;
        return true;
    }
    return false;
}

// Note: set_time_scale(Context*, float) is currently defined in src/etch_motion.cpp.
float time_scale(const Context* ctx) noexcept {
    const Context* c = ctx ? ctx : current_context();
    return c ? c->time_scale : 1.0f;
}

void automation_set_recording(Context* ctx, bool enabled) noexcept {
    Context* c = ctx ? ctx : current_context();
    if (!c) return;
    c->automation_recording = enabled;
}

uint32_t automation_recorded_count(const Context* ctx) noexcept {
    const Context* c = ctx ? ctx : current_context();
    return (c && c->input_record_ring) ? c->input_record_ring->count : 0u;
}

bool automation_get_recorded(const Context* ctx, uint32_t index, RecordedFrameInput* out_input) noexcept {
    const Context* c = ctx ? ctx : current_context();
    if (!c || !c->input_record_ring || !out_input || index >= c->input_record_ring->count) return false;
    const InputRecordRing* ring = c->input_record_ring;
    *out_input = ring->items[(ring->head + index) % InputRecordRing::kCapacity];
    return true;
}

void automation_clear_recording(Context* ctx) noexcept {
    Context* c = ctx ? ctx : current_context();
    if (!c || !c->input_record_ring) return;
    c->input_record_ring->head = 0u;
    c->input_record_ring->count = 0u;
}

// Opens a frame via begin_frame() populated from recorded sample `index` (and injects
// any recorded typed_utf8 text via process_event). Recording is temporarily paused so
// replay never self-appends. Caller builds its immediate-mode UI and calls end_frame().
bool automation_replay_frame(
    Context* ctx,
    uint32_t index,
    uint32_t viewport_w,
    uint32_t viewport_h,
    float dpi_scale) noexcept
{
    Context* c = ctx ? ctx : current_context();
    if (!c || c->in_frame || !c->input_record_ring || index >= c->input_record_ring->count) return false;
    const InputRecordRing* ring = c->input_record_ring;
    const RecordedFrameInput rec = ring->items[(ring->head + index) % InputRecordRing::kCapacity];

    const bool prev_recording = c->automation_recording;
    c->automation_recording = false;

    if (rec.typed_utf8_len > 0u) {
        char txt[64] = {};
        const uint32_t len = (rec.typed_utf8_len < 63u) ? rec.typed_utf8_len : 63u;
        std::memcpy(txt, rec.typed_utf8, len);
        txt[len] = '\0';
        SDL_Event ev = {};
        ev.type = SDL_EVENT_TEXT_INPUT;
        ev.text.text = txt;
        process_event(c, ev);
    }

    FrameDesc desc = {};
    desc.viewport_w = viewport_w > 0u ? viewport_w : 1u;
    desc.viewport_h = viewport_h > 0u ? viewport_h : 1u;
    desc.dpi_scale = (std::isfinite(dpi_scale) && dpi_scale > 0.0f) ? dpi_scale : 1.0f;
    desc.dt_seconds = std::isfinite(rec.dt_seconds) ? rec.dt_seconds : 0.0f;
    desc.pointer_pos = rec.pointer_pos;
    desc.pointer_down = rec.pointer_down;
    desc.right_pointer_down = rec.right_pointer_down;
    desc.scroll_delta = rec.scroll_delta;
    desc.key_mods = rec.key_mods;
    desc.key_pressed_code = rec.key_pressed_code;

    begin_frame(c, desc);
    c->automation_recording = prev_recording;
    return c->in_frame;
}

uint32_t automation_export_failure_report_json(
    const Context* ctx,
    char* out_buf,
    uint32_t cap,
    SDL_Window* window) noexcept
{
    const Context* c = ctx ? ctx : current_context();
    if (!c) {
        if (out_buf && cap > 0u) out_buf[0] = '\0';
        return 0u;
    }

    AutoJsonWriter w{out_buf, cap, 0u};
    w.append_cstr("{\"recorded_frames\":[");
    const uint32_t rec_count = automation_recorded_count(c);
    for (uint32_t i = 0u; i < rec_count; ++i) {
        RecordedFrameInput r = {};
        if (!automation_get_recorded(c, i, &r)) continue;
        if (i > 0u) w.append_char(',');
        w.append_cstr("{\"dt\":");
        w.append_float(r.dt_seconds, "%.6f");
        w.append_cstr(",\"pointer\":[");
        w.append_float(r.pointer_pos.x, "%.2f");
        w.append_char(',');
        w.append_float(r.pointer_pos.y, "%.2f");
        w.append_cstr("],\"pointer_down\":");
        w.append_cstr(r.pointer_down ? "true" : "false");
        w.append_cstr(",\"right_pointer_down\":");
        w.append_cstr(r.right_pointer_down ? "true" : "false");
        w.append_cstr(",\"scroll\":[");
        w.append_float(r.scroll_delta.x, "%.2f");
        w.append_char(',');
        w.append_float(r.scroll_delta.y, "%.2f");
        w.append_cstr("],\"key_mods\":");
        w.append_u32(static_cast<uint32_t>(r.key_mods));
        w.append_cstr(",\"key\":");
        w.append_u32(r.key_pressed_code);
        w.append_cstr(",\"typed_utf8\":");
        const uint32_t tlen = (r.typed_utf8_len < 63u) ? r.typed_utf8_len : 63u;
        w.append_escaped(r.typed_utf8, tlen);
        w.append_char('}');
    }

    w.append_cstr("],\"a11y_nodes\":[");
    const std::span<const A11yNode> nodes = frame_a11y_nodes(c, window);
    for (size_t i = 0u; i < nodes.size(); ++i) {
        const A11yNode& n = nodes[i];
        if (i > 0u) w.append_char(',');
        w.append_cstr("{\"id\":");
        w.append_u32(n.id);
        w.append_cstr(",\"role\":");
        const char* role = a11y_role_name(n.role);
        w.append_escaped(role, static_cast<uint32_t>(std::strlen(role)));
        w.append_cstr(",\"label\":");
        w.append_escaped(n.label.chars, n.label.length);
        w.append_cstr(",\"bounds\":[");
        w.append_float(n.bounds.x, "%.2f");
        w.append_char(',');
        w.append_float(n.bounds.y, "%.2f");
        w.append_char(',');
        w.append_float(n.bounds.w, "%.2f");
        w.append_char(',');
        w.append_float(n.bounds.h, "%.2f");
        w.append_cstr("],\"states\":");
        w.append_u32(n.state_flags);
        w.append_char('}');
    }
    w.append_cstr("]}");
    w.finish();
    return w.total;
}

} // namespace etch
