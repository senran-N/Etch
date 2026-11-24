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
    InputRecordRing* ring = ctx->input_record_ring;
    if (ring->count < InputRecordRing::kCapacity) {
        const uint32_t tail = (ring->head + ring->count) % InputRecordRing::kCapacity;
        ring->items[tail] = input;
        ++ring->count;
    } else {
        ring->items[ring->head] = input;
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

} // namespace etch
