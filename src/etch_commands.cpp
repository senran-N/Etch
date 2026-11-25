// Application command registry, shortcut dispatch & MRU history (#31).
// Compiled via src/etch_unity.cpp.

namespace etch {

struct CommandRecord {
    uint32_t id = 0u;
    char* name = nullptr;
    uint32_t name_len = 0u;
    char* title = nullptr;
    uint32_t title_len = 0u;
    char* category = nullptr;
    uint32_t category_len = 0u;
    uint16_t shortcut_mods = 0u;
    uint32_t shortcut_keycode = 0u;
    uint32_t scope_id = 0u;
    uint16_t chord_mods = 0u;
    uint32_t chord_keycode = 0u;
    bool enabled = true;
    bool allow_when_typing = false;
};

struct CommandRegistry {
    static constexpr uint32_t kMaxRecent = 32u;
    static constexpr uint64_t kChordTimeoutMs = 1500u;
    CommandRecord* records = nullptr;
    uint32_t count = 0u;
    uint32_t cap = 0u;
    uint32_t recent[kMaxRecent] = {};
    uint32_t recent_count = 0u;
    uint16_t pending_chord_mods = 0u;
    uint32_t pending_chord_keycode = 0u;
    uint64_t pending_chord_tick = 0u;
};

namespace {

constexpr uint16_t kCommandModMask = static_cast<uint16_t>(Mod_Shift | Mod_Ctrl | Mod_Alt | Mod_Super);

bool cmd_is_modifier_key(uint32_t keycode) noexcept {
    return keycode == SDLK_LCTRL || keycode == SDLK_RCTRL ||
           keycode == SDLK_LSHIFT || keycode == SDLK_RSHIFT ||
           keycode == SDLK_LALT || keycode == SDLK_RALT ||
           keycode == SDLK_LGUI || keycode == SDLK_RGUI;
}

void cmd_clear_pending_chord(CommandRegistry* reg) noexcept {
    reg->pending_chord_mods = 0u;
    reg->pending_chord_keycode = 0u;
    reg->pending_chord_tick = 0u;
}

char* cmd_dup_str(Str s, uint32_t* out_len) noexcept {
    if (!s.chars || s.length == 0u) {
        *out_len = 0u;
        return nullptr;
    }
    char* dst = static_cast<char*>(std::malloc(size_t(s.length) + 1u));
    if (!dst) {
        *out_len = 0u;
        return nullptr;
    }
    std::memcpy(dst, s.chars, s.length);
    dst[s.length] = '\0';
    *out_len = s.length;
    return dst;
}

void cmd_free_record(CommandRecord* r) noexcept {
    std::free(r->name);
    std::free(r->title);
    std::free(r->category);
    *r = {};
}

CommandRegistry* cmd_ensure_registry(Context* ctx) noexcept {
    if (!ctx->commands) {
        ctx->commands = static_cast<CommandRegistry*>(std::calloc(1u, sizeof(CommandRegistry)));
    }
    return ctx->commands;
}

CommandRecord* cmd_find(CommandRegistry* reg, uint32_t command_id, uint32_t* out_idx = nullptr) noexcept {
    for (uint32_t i = 0u; i < reg->count; ++i) {
        if (reg->records[i].id == command_id) {
            if (out_idx) *out_idx = i;
            return &reg->records[i];
        }
    }
    return nullptr;
}

const CommandRecord* cmd_find_const(const CommandRegistry* reg, uint32_t command_id) noexcept {
    for (uint32_t i = 0u; i < reg->count; ++i) {
        if (reg->records[i].id == command_id) return &reg->records[i];
    }
    return nullptr;
}

void cmd_push_recent(CommandRegistry* reg, uint32_t command_id) noexcept {
    const uint32_t shift = reg->recent_count < CommandRegistry::kMaxRecent
        ? reg->recent_count
        : (CommandRegistry::kMaxRecent - 1u);
    if (shift > 0u) {
        std::memmove(reg->recent + 1u, reg->recent, size_t(shift) * sizeof(uint32_t));
    }
    reg->recent[0] = command_id;
    if (reg->recent_count < CommandRegistry::kMaxRecent) {
        ++reg->recent_count;
    }
}

} // namespace

void free_command_registry(Context* ctx) noexcept {
    if (!ctx || !ctx->commands) return;
    CommandRegistry* reg = ctx->commands;
    for (uint32_t i = 0u; i < reg->count; ++i) {
        cmd_free_record(&reg->records[i]);
    }
    std::free(reg->records);
    std::free(reg);
    ctx->commands = nullptr;
}

bool register_command(Context* ctx, const CommandDesc& desc) noexcept {
    Context* c = ctx ? ctx : current_context();
    if (!c || desc.id == 0u) return false;
    if ((desc.name.length > 0u && !desc.name.chars) ||
        (desc.title.length > 0u && !desc.title.chars) ||
        (desc.category.length > 0u && !desc.category.chars)) {
        return false;
    }

    CommandRegistry* reg = cmd_ensure_registry(c);
    if (!reg) return false;

    uint32_t name_len = 0u;
    uint32_t title_len = 0u;
    uint32_t category_len = 0u;
    char* name = cmd_dup_str(desc.name, &name_len);
    char* title = cmd_dup_str(desc.title, &title_len);
    char* category = cmd_dup_str(desc.category, &category_len);
    if ((desc.name.length > 0u && !name) ||
        (desc.title.length > 0u && !title) ||
        (desc.category.length > 0u && !category)) {
        std::free(name);
        std::free(title);
        std::free(category);
        return false;
    }

    CommandRecord* slot = cmd_find(reg, desc.id);
    if (!slot) {
        if (reg->count == reg->cap) {
            const uint32_t next_cap = reg->cap ? (reg->cap * 2u) : 16u;
            void* mem = std::realloc(reg->records, size_t(next_cap) * sizeof(CommandRecord));
            if (!mem) {
                std::free(name);
                std::free(title);
                std::free(category);
                return false;
            }
            reg->records = static_cast<CommandRecord*>(mem);
            reg->cap = next_cap;
        }
        slot = &reg->records[reg->count++];
        *slot = {};
    } else {
        cmd_free_record(slot);
    }

    slot->id = desc.id;
    slot->name = name;
    slot->name_len = name_len;
    slot->title = title;
    slot->title_len = title_len;
    slot->category = category;
    slot->category_len = category_len;
    slot->shortcut_mods = static_cast<uint16_t>(desc.shortcut_mods & kCommandModMask);
    slot->shortcut_keycode = desc.shortcut_keycode;
    slot->scope_id = desc.scope_id;
    slot->chord_mods = static_cast<uint16_t>(desc.chord_mods & kCommandModMask);
    slot->chord_keycode = desc.chord_keycode;
    slot->enabled = desc.enabled;
    slot->allow_when_typing = desc.allow_when_typing;
    return true;
}

bool unregister_command(Context* ctx, uint32_t command_id) noexcept {
    Context* c = ctx ? ctx : current_context();
    if (!c || !c->commands || command_id == 0u) return false;
    CommandRegistry* reg = c->commands;
    uint32_t idx = 0u;
    CommandRecord* rec = cmd_find(reg, command_id, &idx);
    if (!rec) return false;
    cmd_free_record(rec);
    const uint32_t tail = reg->count - idx - 1u;
    if (tail > 0u) {
        std::memmove(reg->records + idx, reg->records + idx + 1u, size_t(tail) * sizeof(CommandRecord));
    }
    --reg->count;
    cmd_clear_pending_chord(reg);
    return true;
}

bool set_command_enabled(Context* ctx, uint32_t command_id, bool enabled) noexcept {
    Context* c = ctx ? ctx : current_context();
    if (!c || !c->commands || command_id == 0u) return false;
    CommandRecord* rec = cmd_find(c->commands, command_id);
    if (!rec) return false;
    rec->enabled = enabled;
    return true;
}

bool is_command_enabled(const Context* ctx, uint32_t command_id) noexcept {
    const Context* c = ctx ? ctx : current_context();
    if (!c || !c->commands || command_id == 0u) return false;
    const CommandRecord* rec = cmd_find_const(c->commands, command_id);
    return rec && rec->enabled;
}

bool rebind_command_shortcut(
    Context* ctx,
    uint32_t command_id,
    uint16_t mods,
    uint32_t keycode,
    uint16_t chord_mods,
    uint32_t chord_keycode) noexcept
{
    Context* c = ctx ? ctx : current_context();
    if (!c || !c->commands || command_id == 0u) return false;
    CommandRegistry* reg = c->commands;
    CommandRecord* rec = cmd_find(reg, command_id);
    if (!rec) return false;
    rec->shortcut_mods = static_cast<uint16_t>(mods & kCommandModMask);
    rec->shortcut_keycode = keycode;
    rec->chord_mods = static_cast<uint16_t>(chord_mods & kCommandModMask);
    rec->chord_keycode = chord_keycode;
    cmd_clear_pending_chord(reg);
    return true;
}

uint32_t find_command_shortcut_conflict(
    const Context* ctx,
    uint16_t mods,
    uint32_t keycode,
    uint32_t ignore_id) noexcept
{
    const Context* c = ctx ? ctx : current_context();
    if (!c || !c->commands || keycode == 0u) return 0u;
    const uint16_t norm_mods = static_cast<uint16_t>(mods & kCommandModMask);
    const CommandRegistry* reg = c->commands;
    for (uint32_t i = 0u; i < reg->count; ++i) {
        const CommandRecord& r = reg->records[i];
        if (r.id != ignore_id && r.shortcut_keycode == keycode &&
            (r.shortcut_mods & kCommandModMask) == norm_mods) {
            return r.id;
        }
    }
    return 0u;
}

uint32_t poll_triggered_command(Context* ctx) noexcept {
    Context* c = ctx ? ctx : current_context();
    if (!c || !c->commands || !c->active_ui) return 0u;
    WindowUiState* ui = c->active_ui;
    if (ui->ime_comp_len > 0u) return 0u;

    CommandRegistry* reg = c->commands;
    const uint64_t now_ms = SDL_GetTicks();
    if (reg->pending_chord_keycode != 0u &&
        (now_ms < reg->pending_chord_tick || (now_ms - reg->pending_chord_tick) > CommandRegistry::kChordTimeoutMs)) {
        cmd_clear_pending_chord(reg);
    }

    const WidgetState* focused_ws = (ui->focus_id != 0u) ? state_map_find(&ui->states, ui->focus_id) : nullptr;
    const bool typing = focused_ws != nullptr && focused_ws->is_text_input_writable &&
                        (focused_ws->last_seen_frame + 1u >= ui->frame_index);
    const bool modal_in_scope = (ui->modal_scope == 0u) ||
                                focus_scope_contains(*ui, ui->modal_scope) ||
                                (focused_ws != nullptr && focused_ws->focus_scope == ui->modal_scope);

    // Specificity rank: -1 = no match, 0 = global, 1..N = outer-to-inner command/focus scope, 1000 = direct focus_id match.
    const auto scope_rank = [&](uint32_t scope_id) noexcept -> int32_t {
        if (scope_id == 0u) return modal_in_scope ? 0 : -1;
        if (ui->modal_scope != 0u && scope_id != ui->modal_scope && !modal_in_scope) return -1;
        if (ui->focus_id == scope_id) return 1000;
        for (int32_t d = static_cast<int32_t>(ui->command_scope_depth) - 1; d >= 0; --d) {
            if (ui->command_scopes[d] == scope_id) return 100 + d;
        }
        if (ui->focus_scope == scope_id) return 50 + static_cast<int32_t>(ui->focus_scope_depth);
        for (int32_t d = static_cast<int32_t>(ui->focus_scope_depth) - 1; d >= 0; --d) {
            if (ui->focus_scopes[d] == scope_id) return 50 + d;
        }
        if (focused_ws != nullptr && focused_ws->focus_scope == scope_id) return 10;
        return -1;
    };

    for (uint32_t ki = 0u; ki < ui->frame_key_count; ++ki) {
        KeyEventEntry& ev = ui->frame_keys[ki];
        if (ev.consumed || ev.repeat || ev.key == 0u || cmd_is_modifier_key(ev.key)) continue;
        const uint16_t ev_mods = static_cast<uint16_t>(ev.mods & kCommandModMask);

        if (reg->pending_chord_keycode != 0u) {
            uint32_t best_chord_id = 0u;
            int32_t best_chord_rank = -1;
            for (uint32_t i = 0u; i < reg->count; ++i) {
                const CommandRecord& r = reg->records[i];
                if (!r.enabled || r.chord_keycode == 0u) continue;
                if (typing && !r.allow_when_typing) continue;
                const int32_t rk = scope_rank(r.scope_id);
                if (rk < 0) continue;
                if (r.shortcut_mods == reg->pending_chord_mods &&
                    r.shortcut_keycode == reg->pending_chord_keycode &&
                    r.chord_mods == ev_mods &&
                    r.chord_keycode == ev.key) {
                    if (rk > best_chord_rank) {
                        best_chord_rank = rk;
                        best_chord_id = r.id;
                    }
                }
            }
            cmd_clear_pending_chord(reg);
            if (best_chord_id != 0u) {
                ev.consumed = true;
                cmd_push_recent(reg, best_chord_id);
                return best_chord_id;
            }
        }

        uint32_t best_single_id = 0u;
        int32_t best_single_rank = -1;
        int32_t best_prefix_rank = -1;

        for (uint32_t i = 0u; i < reg->count; ++i) {
            const CommandRecord& r = reg->records[i];
            if (!r.enabled || r.shortcut_keycode == 0u) continue;
            if (typing && !r.allow_when_typing) continue;
            const int32_t rk = scope_rank(r.scope_id);
            if (rk < 0) continue;
            if (r.shortcut_mods != ev_mods || r.shortcut_keycode != ev.key) continue;

            if (r.chord_keycode != 0u) {
                if (rk > best_prefix_rank) best_prefix_rank = rk;
            } else {
                if (rk > best_single_rank) {
                    best_single_rank = rk;
                    best_single_id = r.id;
                }
            }
        }

        if (best_prefix_rank >= 0 && best_prefix_rank >= best_single_rank) {
            reg->pending_chord_mods = ev_mods;
            reg->pending_chord_keycode = ev.key;
            reg->pending_chord_tick = now_ms;
            ev.consumed = true;
            return 0u;
        }
        if (best_single_id != 0u) {
            ev.consumed = true;
            cmd_push_recent(reg, best_single_id);
            return best_single_id;
        }
    }
    return 0u;
}

bool dispatch_command(Context* ctx, uint32_t command_id) noexcept {
    Context* c = ctx ? ctx : current_context();
    if (!c || !c->commands || command_id == 0u) return false;
    CommandRecord* rec = cmd_find(c->commands, command_id);
    if (!rec || !rec->enabled) return false;
    cmd_push_recent(c->commands, rec->id);
    return true;
}

uint32_t recent_command_count(const Context* ctx) noexcept {
    const Context* c = ctx ? ctx : current_context();
    return (c && c->commands) ? c->commands->recent_count : 0u;
}

uint32_t recent_command_at(const Context* ctx, uint32_t index) noexcept {
    const Context* c = ctx ? ctx : current_context();
    if (!c || !c->commands || index >= c->commands->recent_count) return 0u;
    return c->commands->recent[index];
}

uint32_t command_count(const Context* ctx) noexcept {
    const Context* c = ctx ? ctx : current_context();
    return (c && c->commands) ? c->commands->count : 0u;
}

bool get_command(const Context* ctx, uint32_t index, CommandDesc* out_desc) noexcept {
    const Context* c = ctx ? ctx : current_context();
    if (!c || !c->commands || !out_desc || index >= c->commands->count) return false;
    const CommandRecord& r = c->commands->records[index];
    *out_desc = CommandDesc{
        .id = r.id,
        .name = r.name_len > 0u ? Str{r.name, r.name_len, false} : Str{},
        .title = r.title_len > 0u ? Str{r.title, r.title_len, false} : Str{},
        .category = r.category_len > 0u ? Str{r.category, r.category_len, false} : Str{},
        .shortcut_mods = r.shortcut_mods,
        .shortcut_keycode = r.shortcut_keycode,
        .enabled = r.enabled,
        .allow_when_typing = r.allow_when_typing,
        .scope_id = r.scope_id,
        .chord_mods = r.chord_mods,
        .chord_keycode = r.chord_keycode,
    };
    return true;
}

void push_command_scope(uint32_t scope_id) noexcept {
    Context* c = current_context();
    if (!c || !c->active_ui || scope_id == 0u || c->active_ui->command_scope_depth >= 32u) return;
    c->active_ui->command_scopes[c->active_ui->command_scope_depth++] = scope_id;
}

void pop_command_scope() noexcept {
    Context* c = current_context();
    if (!c || !c->active_ui || c->active_ui->command_scope_depth == 0u) return;
    --c->active_ui->command_scope_depth;
}

bool push_focus_restore_point(Context* ctx) noexcept {
    Context* c = ctx ? ctx : current_context();
    if (!c || !c->active_ui || c->active_ui->focus_restore_depth >= 16u) return false;
    c->active_ui->focus_restore_stack[c->active_ui->focus_restore_depth++] = c->active_ui->focus_id;
    return true;
}

uint32_t pop_focus_restore_point(Context* ctx) noexcept {
    Context* c = ctx ? ctx : current_context();
    if (!c || !c->active_ui || c->active_ui->focus_restore_depth == 0u) return 0u;
    const uint32_t saved = c->active_ui->focus_restore_stack[--c->active_ui->focus_restore_depth];
    if (saved != 0u) {
        Context* prev = current_context();
        set_current_context(c);
        request_focus(saved);
        set_current_context(prev);
    }
    return saved;
}

FuzzyMatchResult fuzzy_match(Str pattern, Str candidate, uint32_t* out_positions, uint32_t max_positions) noexcept {
    if (pattern.length == 0u) return {true, 0, 0u};
    if (!pattern.chars || !candidate.chars || candidate.length < pattern.length) return {};

    const auto fold = [](char ch) noexcept -> unsigned char {
        return (ch >= 'A' && ch <= 'Z') ? static_cast<unsigned char>(ch + ('a' - 'A')) : static_cast<unsigned char>(ch);
    };
    const auto is_sep = [](char ch) noexcept -> bool {
        return ch == '/' || ch == '\\' || ch == '_' || ch == '-' || ch == '.' || ch == ' ' || ch == ':';
    };
    const auto is_upper = [](char ch) noexcept -> bool { return ch >= 'A' && ch <= 'Z'; };
    const auto is_lower = [](char ch) noexcept -> bool { return ch >= 'a' && ch <= 'z'; };

    // Fast greedy pass with word-boundary lookahead so camelCase/path initials beat mid-word hits.
    uint32_t pi = 0u;
    uint32_t ci = 0u;
    int32_t score = 0;
    uint32_t matched = 0u;
    uint32_t prev_match = UINT32_MAX;
    uint32_t consecutive = 0u;

    while (pi < pattern.length && ci < candidate.length) {
        const unsigned char pc = fold(pattern.chars[pi]);
        uint32_t chosen = UINT32_MAX;
        int32_t chosen_bonus = -10000;

        for (uint32_t scan = ci; scan < candidate.length; ++scan) {
            if (fold(candidate.chars[scan]) != pc) continue;
            // Verify the remaining pattern still fits after `scan`.
            if (candidate.length - scan < pattern.length - pi) break;
            int32_t b = 0;
            const bool is_consec = (prev_match != UINT32_MAX && scan == prev_match + 1u);
            const bool at_start = (scan == 0u);
            const bool after_sep = (scan > 0u && is_sep(candidate.chars[scan - 1u]));
            const bool camel = (scan > 0u && is_upper(candidate.chars[scan]) && is_lower(candidate.chars[scan - 1u]));
            if (at_start) b += 24;
            if (after_sep) b += 20;
            if (camel) b += 18;
            if (is_consec) b += 16 + static_cast<int32_t>(consecutive < 8u ? consecutive * 3u : 24u);
            if (candidate.chars[scan] == pattern.chars[pi]) b += 2;
            const uint32_t gap = (prev_match == UINT32_MAX) ? scan : (scan - prev_match - 1u);
            b -= static_cast<int32_t>(gap < 32u ? gap : 32u);
            if (chosen == UINT32_MAX || b > chosen_bonus) {
                chosen = scan;
                chosen_bonus = b;
                // Stop scanning early once we hit an optimal consecutive or boundary match.
                if (is_consec || at_start || after_sep || camel) break;
            }
        }
        if (chosen == UINT32_MAX) return {};
        if (prev_match != UINT32_MAX && chosen == prev_match + 1u) ++consecutive;
        else consecutive = 0u;
        score += 10 + chosen_bonus;
        if (out_positions && matched < max_positions) {
            out_positions[matched] = chosen;
        }
        ++matched;
        prev_match = chosen;
        ci = chosen + 1u;
        ++pi;
    }
    if (pi < pattern.length) return {};
    // Slightly prefer tighter candidates.
    const uint32_t excess = candidate.length - pattern.length;
    score -= static_cast<int32_t>(excess < 64u ? excess : 64u);
    return {true, score, matched};
}

} // namespace etch
