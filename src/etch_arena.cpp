// Arena memory pool, cross-frame StateMap, and internal Context state definitions.
// Compiled via src/etch_unity.cpp.

#if defined(_WIN32)
#    include <malloc.h>
#else
#    include <cstdlib>
#endif

namespace etch {

inline constexpr uint32_t kAtlasSize = 1024u;
inline constexpr uint32_t kGlyphTableCapacity = 4096u;
inline constexpr uint32_t kDefaultStateCapacity = 1024u;
inline constexpr uint32_t kMaxBatches = 256u;
inline constexpr uint32_t kMaxClipDepth = 64u;
inline constexpr uint32_t kMaxCachedPipelines = 4u;
inline constexpr uint32_t kMaxFocusablePerFrame = 256u;
inline constexpr uint32_t kCursorCount = 9u;
inline constexpr uint32_t kMaxTextOpsPerFrame = 32u;
inline constexpr uint32_t kMaxPendingTextBytes = 1024u;
inline constexpr uint32_t kMaxImeCompBytes = 256u;

struct WidgetState {
    uint32_t key = 0; // 0 marks an empty hash slot
    uint32_t last_seen_frame = 0;
    bool     is_user_element = false;
    bool     is_focusable = false;
    bool     pressed_down = false;
    bool     pressed_this_frame = false;
    bool     clicked_this_frame = false;
    bool     spring_initialized = false;
    float    spring_pos = 0.0f;
    float    spring_vel = 0.0f;
    bool     is_scroll_x = false;
    bool     is_scroll_y = false;
    Vec2     scroll_pos = {};
    Vec2     scroll_vel = {};
    Vec2     scroll_max = {};
    Vec2     viewport_dim = {};
    Vec2     content_dim = {};
    Rect     last_box_logical = {};
    bool     edit_initialized = false;
    uint32_t edit_cursor = 0;
    uint32_t edit_sel_anchor = 0;
    float    edit_scroll_x = 0.0f;
    float    edit_scroll_y = 0.0f;
    float    edit_preferred_x = -1.0f;
};

struct StateMap {
    WidgetState* entries = nullptr;
    uint32_t     capacity = 0; // Power of two
    uint32_t     count = 0;
};

struct GlyphEntry {
    uint64_t key = 0; // (uint64_t(codepoint) << 16) | size_q2; 0 = empty
    float    u0 = 0.0f;
    float    v0 = 0.0f;
    float    u1 = 0.0f;
    float    v1 = 0.0f;
    float    xoff = 0.0f;
    float    yoff = 0.0f;
    float    width = 0.0f;
    float    height = 0.0f;
    float    advance = 0.0f;
    bool     visible = false;
};

struct FontAtlas {
    GlyphEntry*     glyphs = nullptr; // Open-addressed table of kGlyphTableCapacity slots
    uint8_t*        pixels = nullptr; // kAtlasSize * kAtlasSize * 4 RGBA8
    uint8_t*        raster_scratch = nullptr; // 256 * 256 reusable 8-bit bitmap scratch
    SDL_GPUTexture* gpu_tex = nullptr;
    uint32_t       shelf_x = 6u;
    uint32_t       shelf_y = 0u;
    uint32_t       shelf_h = 0u;
    uint32_t       dirty_y0 = 0u;
    uint32_t       dirty_y1 = 0u;
    bool           dirty = false;

    stbtt_fontinfo font_info = {};
    uint8_t*       font_file_data = nullptr;
    size_t         font_file_size = 0;
    int32_t        font_ascent = 0;
    int32_t        font_descent = 0;
    int32_t        font_line_gap = 0;
    bool           font_valid = false;
};

struct DrawBatch {
    uint32_t        first_prim = 0;
    uint32_t        prim_count = 0;
    SDL_GPUTexture* ext_textures[3] = {}; // Slots 1..3; slot 0 is always FontAtlas::gpu_tex
    uint32_t        ext_count = 0;
};

enum class TextEditOpKind : uint8_t {
    KeyPress = 0,
    InsertText = 1,
};

struct PendingTextOp {
    TextEditOpKind kind = TextEditOpKind::KeyPress;
    uint8_t        pad0 = 0;
    uint16_t       mod = 0;
    uint32_t       key = 0;
    uint16_t       text_offset = 0;
    uint16_t       text_len = 0;
};

struct TextInputExtra {
    TextInputDesc desc = {};
    Str           committed_text = {};
    uint32_t      cursor_byte = 0;
    uint32_t      sel_anchor_byte = 0;
    bool          is_focused = false;
};

struct BoxExtra {
    BoxDesc         desc = {};
    TextInputExtra* text_input = nullptr;
};

struct ImageExtra {
    ImageDesc desc = {};
};

struct ClipEntry {
    Rect  rect = {};
    float radius = 0.0f;
};

struct ClipStack {
    ClipEntry entries[kMaxClipDepth] = {};
    uint32_t  depth = 0;
};

struct CachedPipeline {
    SDL_GPUTextureFormat     format = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
};

struct Context {
    Arena         frame_arena = {};
    void*         clay_mem = nullptr;
    Clay_Context* clay_ctx = nullptr;
    StateMap      states = {};
    FontAtlas     atlas = {};

    Prim*         prims = nullptr;
    uint32_t      max_prims = 0;
    uint32_t      prim_count = 0;
    DrawBatch     batches[kMaxBatches] = {};
    uint32_t      batch_count = 0;

    // GPU objects
    SDL_GPUDevice*         device = nullptr;
    SDL_Window*            window = nullptr;
    bool                   owns_video_init = false;
    bool                   headless = false;
    SDL_GPUShader*         vs_shader = nullptr;
    SDL_GPUShader*         ps_shader = nullptr;
    SDL_GPUSampler*        linear_sampler = nullptr;
    CachedPipeline         pipelines[kMaxCachedPipelines] = {};
    uint32_t               pipeline_count = 0;
    SDL_GPUBuffer*         prim_gpu_buf = nullptr;
    SDL_GPUTransferBuffer* prim_transfer_buf = nullptr;
    SDL_GPUTransferBuffer* atlas_transfer_buf = nullptr;

    // Reusable offscreen target & readback buffer for render_headless_png
    SDL_GPUTexture*        headless_tex = nullptr;
    uint32_t               headless_w = 0;
    uint32_t               headless_h = 0;
    SDL_GPUTransferBuffer* headless_dl_buf = nullptr;
    uint32_t               headless_dl_size = 0;

    // Per-frame & input tracking
    uint32_t   viewport_w = 1280u;
    uint32_t   viewport_h = 720u;
    float      dpi_scale = 1.0f;
    float      elapsed_seconds = 0.0f;
    uint32_t   frame_index = 0;
    bool       in_frame = false;

    Vec2       event_pointer_pos = {-1.0f, -1.0f};
    bool       has_event_pointer = false;
    bool       event_pointer_down = false;
    Vec2       event_scroll_delta = {};
    bool       prev_pointer_down = false;

    uint32_t    active_id = 0;
    uint32_t    focus_id = 0;
    bool        mouse_captured = false;
    Vec2        pointer_pos = {-1.0f, -1.0f};
    float       scrollbar_drag_grab_offset = 0.0f;
    uint32_t    focus_order[kMaxFocusablePerFrame] = {};
    uint32_t    focus_count = 0;
    uint32_t    prev_focus_order[kMaxFocusablePerFrame] = {};
    uint32_t    prev_focus_count = 0;
    int32_t     pending_tab_delta = 0;
    Cursor      desired_cursor = Cursor::Arrow;
    Cursor      applied_cursor = Cursor::Arrow;
    SDL_Cursor* system_cursors[kCursorCount] = {};
    uint32_t    redraw_budget = 2u;
    uint32_t    active_anim_count = 0u;
    uint32_t    wake_event_type = 0u;
    float       sim_dt = 0.0f;

    uint8_t       last_mouse_clicks = 1u;
    PendingTextOp pending_text_ops[kMaxTextOpsPerFrame] = {};
    uint32_t      pending_text_op_count = 0;
    char          pending_text_utf8[kMaxPendingTextBytes] = {};
    uint32_t      pending_text_utf8_len = 0;

    PendingTextOp frame_text_ops[kMaxTextOpsPerFrame] = {};
    uint32_t      frame_text_op_count = 0;
    char          frame_text_utf8[kMaxPendingTextBytes] = {};

    char          ime_comp_utf8[kMaxImeCompBytes] = {};
    uint32_t      ime_comp_len = 0;
    uint32_t      ime_comp_cursor_byte = 0;
    uint32_t      ime_comp_sel_len_byte = 0;
    uint32_t      ime_focused_id = 0;
    uint32_t      prev_ime_focused_id = 0;
    bool          ime_focused_password = false;
    bool          applied_ime_password = false;
    bool          ime_input_started = false;
    Rect          ime_cursor_logical = {};
    int32_t       ime_cursor_offset_win = 0;
    SDL_Rect      applied_ime_rect = {-1, -1, 0, 0};
    int32_t       applied_ime_cursor = -1;

    bool          caret_blink_on = true;
    uint32_t      caret_blink_cycles = 0;

    char*         clipboard_fallback = nullptr;
    uint32_t      clipboard_fallback_cap = 0;

    uint64_t   layout_start_ticks = 0;
    FrameStats stats = {};
};

namespace {

thread_local Context* g_current_context = nullptr;

constexpr uint32_t hash_u32(uint32_t x) noexcept {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

bool state_map_init(StateMap* map, uint32_t capacity) noexcept {
    if (!map || capacity == 0 || (capacity & (capacity - 1u)) != 0u) {
        return false;
    }
    map->entries = static_cast<WidgetState*>(std::calloc(capacity, sizeof(WidgetState)));
    if (!map->entries) {
        return false;
    }
    map->capacity = capacity;
    map->count = 0;
    return true;
}

void state_map_destroy(StateMap* map) noexcept {
    if (!map) {
        return;
    }
    std::free(map->entries);
    map->entries = nullptr;
    map->capacity = 0;
    map->count = 0;
}

bool state_map_rehash(StateMap* map, uint32_t new_cap) noexcept {
    WidgetState* next = static_cast<WidgetState*>(std::calloc(new_cap, sizeof(WidgetState)));
    if (!next) {
        return false;
    }
    const uint32_t mask = new_cap - 1u;
    for (uint32_t i = 0; i < map->capacity; ++i) {
        if (map->entries[i].key != 0u) {
            uint32_t idx = hash_u32(map->entries[i].key) & mask;
            while (next[idx].key != 0u) {
                idx = (idx + 1u) & mask;
            }
            next[idx] = map->entries[i];
        }
    }
    std::free(map->entries);
    map->entries = next;
    map->capacity = new_cap;
    return true;
}

WidgetState* state_map_find(StateMap* map, uint32_t key) noexcept {
    if (!map || !map->entries || key == 0u || map->capacity == 0u) {
        return nullptr;
    }
    const uint32_t mask = map->capacity - 1u;
    uint32_t idx = hash_u32(key) & mask;
    for (;;) {
        WidgetState& slot = map->entries[idx];
        if (slot.key == key) {
            return &slot;
        }
        if (slot.key == 0u) {
            return nullptr;
        }
        idx = (idx + 1u) & mask;
    }
}

WidgetState* state_map_get_or_insert(StateMap* map, uint32_t key, uint32_t frame_index) noexcept {
    if (!map || key == 0u) {
        return nullptr;
    }
    if (!map->entries && !state_map_init(map, kDefaultStateCapacity)) {
        return nullptr;
    }
    if ((map->count + 1u) * 4u >= map->capacity * 3u) {
        if (!state_map_rehash(map, map->capacity * 2u)) {
            return nullptr;
        }
    }
    const uint32_t mask = map->capacity - 1u;
    uint32_t idx = hash_u32(key) & mask;
    for (;;) {
        WidgetState& slot = map->entries[idx];
        if (slot.key == key) {
            slot.last_seen_frame = frame_index;
            return &slot;
        }
        if (slot.key == 0u) {
            slot = WidgetState{
                .key = key,
                .last_seen_frame = frame_index,
            };
            ++map->count;
            return &slot;
        }
        idx = (idx + 1u) & mask;
    }
}

void state_map_clear_flags(StateMap* map, bool clear_frame_edges, bool clear_pressed) noexcept {
    if (!map || !map->entries) {
        return;
    }
    for (uint32_t i = 0; i < map->capacity; ++i) {
        WidgetState& slot = map->entries[i];
        if (clear_frame_edges) {
            slot.pressed_this_frame = false;
            slot.clicked_this_frame = false;
        }
        if (clear_pressed) {
            slot.pressed_down = false;
        }
    }
}

} // namespace

Arena arena_create(size_t capacity_bytes) noexcept {
    if (capacity_bytes == 0 || capacity_bytes > (SIZE_MAX - 63u)) {
        return {};
    }
    const size_t aligned_cap = (capacity_bytes + 63u) & ~size_t(63u);
#if defined(_WIN32)
    void* mem = _aligned_malloc(aligned_cap, 64u);
#else
    void* mem = std::aligned_alloc(64u, aligned_cap);
#endif
    if (!mem) {
        return {};
    }
    return Arena{
        .base = static_cast<uint8_t*>(mem),
        .capacity = aligned_cap,
        .offset = 0,
        .peak_offset = 0,
    };
}

void arena_destroy(Arena* arena) noexcept {
    if (!arena) {
        return;
    }
    if (arena->base) {
#if defined(_WIN32)
        _aligned_free(arena->base);
#else
        std::free(arena->base);
#endif
    }
    *arena = {};
}

Context* current_context() noexcept {
    return g_current_context;
}

void set_current_context(Context* ctx) noexcept {
    g_current_context = ctx;
    if (ctx && ctx->clay_ctx) {
        Clay_SetCurrentContext(ctx->clay_ctx);
    }
}

Arena* frame_arena(Context* ctx) noexcept {
    return ctx ? &ctx->frame_arena : nullptr;
}

SDL_GPUDevice* gpu_device(Context* ctx) noexcept {
    return ctx ? ctx->device : nullptr;
}

std::span<const Prim> frame_prims(const Context* ctx) noexcept {
    if (!ctx || !ctx->prims || ctx->prim_count == 0u) {
        return {};
    }
    return std::span<const Prim>(ctx->prims, ctx->prim_count);
}

FrameStats frame_stats(const Context* ctx) noexcept {
    return ctx ? ctx->stats : FrameStats{};
}

} // namespace etch
