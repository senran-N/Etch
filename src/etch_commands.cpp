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
    bool enabled = true;
    bool allow_when_typing = false;
};

struct CommandRegistry {
    static constexpr uint32_t kMaxRecent = 32u;
    CommandRecord* records = nullptr;
    uint32_t count = 0u;
    uint32_t cap = 0u;
    uint32_t recent[kMaxRecent] = {};
    uint32_t recent_count = 0u;
};

namespace {

constexpr uint16_t kCommandModMask = static_cast<uint16_t>(Mod_Shift | Mod_Ctrl | Mod_Alt | Mod_Super);

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
    if (!c || !c->commands) return 0u;
    CommandRegistry* reg = c->commands;
    Context* prev = current_context();
    if (prev != c) set_current_context(c);
    uint32_t triggered = 0u;
    for (uint32_t i = 0u; i < reg->count; ++i) {
        const CommandRecord& r = reg->records[i];
        if (!r.enabled || r.shortcut_keycode == 0u) continue;
        if (consume_shortcut(static_cast<uint16_t>(r.shortcut_mods & kCommandModMask),
                             r.shortcut_keycode, r.allow_when_typing)) {
            cmd_push_recent(reg, r.id);
            triggered = r.id;
            break;
        }
    }
    if (prev != c) set_current_context(prev);
    return triggered;
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
    };
    return true;
}

} // namespace etch
