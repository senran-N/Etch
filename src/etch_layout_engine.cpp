// Owned Orthodox C++20 2-Pass Flexbox + 2D Grid + Anchor Layout Engine,
// Inverse-Transformed True Occlusion Hitbox Tree & Dirty Subtree Cache.
// Compiled via src/etch_unity.cpp (zero third_party/clay.h dependency).

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace etch {

inline constexpr uint32_t kLayoutInvalidNode = UINT32_MAX;
inline constexpr float kLayoutMaxFloat = 1.0e20f;
inline constexpr float kLayoutEpsilon = 0.01f;

template <typename T>
struct PodBuf {
    T* data = nullptr;
    uint32_t count = 0;
    uint32_t cap = 0;

    void clear() noexcept { count = 0; }

    void destroy() noexcept {
        std::free(data);
        data = nullptr;
        count = 0;
        cap = 0;
    }

    bool reserve(uint32_t needed) noexcept {
        if (needed <= cap) return true;
        uint32_t next = cap ? cap : 64u;
        while (next < needed) {
            if (next > UINT32_MAX / 2u) { next = needed; break; }
            next *= 2u;
        }
        auto* p = static_cast<T*>(std::realloc(data, size_t(next) * sizeof(T)));
        if (!p) return false;
        data = p;
        cap = next;
        return true;
    }

    T* push_uninit() noexcept {
        if (count >= cap && !reserve(count + 1u)) return nullptr;
        return &data[count++];
    }

    bool push(const T& v) noexcept {
        T* slot = push_uninit();
        if (!slot) return false;
        *slot = v;
        return true;
    }
};

struct LayoutGridTrack {
    SizingKind kind = SizingKind::Grow;
    float value = 1.0f;
    float min_px = 0.0f;
    float max_px = 0.0f;
};

struct LayoutTextConfig {
    Color text_color = {};
    float font_size_px = 16.0f;
    float letter_spacing = 0.0f;
    float line_height = 0.0f;
    uint16_t font_id = 0;
    uint16_t max_lines = 0;
    AlignX align_x = AlignX::Left;
    bool wrap = true;
    bool ellipsis = false;
    bool is_rich = false;
    uint32_t rich_hash = 0;
    uint32_t opentype_flags = 0u;
    const FontFeature* font_features = nullptr;
    uint32_t font_feature_count = 0u;
    void* user_data = nullptr;
};

struct LayoutNodeConfig {
    Sizing width = Sizing::fit();
    Sizing height = Sizing::fit();
    Padding padding = {};
    Padding margin = {};
    float child_gap = 0.0f;
    float cross_gap = 0.0f;
    Axis axis = Axis::Row;
    AlignX align_x = AlignX::Left;
    AlignY align_y = AlignY::Top;
    Justify justify = Justify::Start;
    AlignSelf align_self = AlignSelf::Auto;
    float flex_grow = 0.0f;
    float flex_shrink = 1.0f;
    float aspect_ratio = 0.0f;
    bool wrap = false;
    bool shrink_width = false;
    bool shrink_height = false;
    bool percent_after_fixed = false;
    float percent_min_first = 0.0f;
    float percent_min_second = 0.0f;
    uint16_t grid_columns = 0;
    const LayoutGridTrack* grid_col_tracks = nullptr;
    uint16_t grid_col_track_count = 0;
    const LayoutGridTrack* grid_row_tracks = nullptr;
    uint16_t grid_row_track_count = 0;
    uint16_t grid_col_span = 1;
    uint16_t grid_row_span = 1;
    bool clip_x = false;
    bool clip_y = false;
    bool scroll_x = false;
    bool scroll_y = false;
    Vec2 child_offset = {};
    AttachTo attach_to = AttachTo::None;
    uint32_t attach_element_id = 0;
    Anchor anchor_element = Anchor::TopLeft;
    Anchor anchor_parent = Anchor::BottomLeft;
    Vec2 floating_offset = {};
    int16_t z_index = 0;
    bool clip_to_parent = false;
    bool clamp_to_viewport = false;
    bool flip_on_overflow = false;
    HitboxBehavior hitbox_behavior = HitboxBehavior::Normal;
    Corners corner_radius = {};
    bool emit_custom = false;
    bool emit_border = false;
    bool emit_image = false;
    void* user_data = nullptr;
    void* custom_data = nullptr;
    void* image_data = nullptr;
    uint32_t cached_subtree_id = 0;
    bool cached_subtree_hit = false;
};

struct LayoutWrappedLine {
    Str line = {};
    float width = 0.0f;
    float height = 0.0f;
    float max_width = 0.0f;
    bool truncated = false;
};

struct LayoutNode {
    uint32_t id = 0;
    uint32_t parent_idx = kLayoutInvalidNode;
    uint32_t first_child = kLayoutInvalidNode;
    uint32_t last_child = kLayoutInvalidNode;
    uint32_t next_sibling = kLayoutInvalidNode;
    uint32_t child_count = 0;
    uint32_t auto_child_seq = 0;
    float width = 0.0f;
    float height = 0.0f;
    float min_w = 0.0f;
    float min_h = 0.0f;
    Rect bounds = {};
    Vec2 content_size = {};
    int32_t cell_row = 0;
    int32_t cell_col = 0;
    int32_t cell_col_span = 1;
    int32_t cell_row_span = 1;
    float cell_x = 0.0f;
    float cell_y = 0.0f;
    float cell_w = 0.0f;
    float cell_h = 0.0f;
    bool is_text = false;
    bool is_floating = false;
    bool positioned = false;
    bool sized_x = false;
    bool sized_y = false;
    uint32_t text_cache_idx = UINT32_MAX;
    uint32_t wrapped_start = 0;
    uint32_t wrapped_count = 0;
    Str text = {};
    Vec2 text_preferred = {};
    LayoutTextConfig text_cfg = {};
    LayoutNodeConfig cfg = {};
};

struct LayoutRoot {
    uint32_t node_idx = 0;
    uint32_t parent_idx = kLayoutInvalidNode;
    uint32_t order = 0;
    int16_t z_index = 0;
};

enum class LayoutCmdKind : uint8_t {
    Custom = 0,
    Border,
    Text,
    Image,
    ScissorStart,
    ScissorEnd,
    SubtreeBegin,
    SubtreeEnd,
    SubtreeReplay,
};

struct LayoutCmd {
    LayoutCmdKind kind = LayoutCmdKind::Custom;
    uint32_t id = 0;
    uint32_t node_idx = 0;
    int16_t z_index = 0;
    Rect bounds = {};
    void* user_data = nullptr;
    void* custom_data = nullptr;
    Str text = {};
    LayoutTextConfig text_cfg = {};
    float text_max_width = 0.0f;
    bool text_truncated = false;
};

struct LayoutBoundsEntry {
    uint32_t id = 0;
    uint32_t node_idx = kLayoutInvalidNode;
    Rect bounds = {};
    Vec2 content_size = {};
    bool has_scroll = false;
};

struct LayoutScrollData {
    bool found = false;
    Vec2 viewport_dim = {};
    Vec2 content_dim = {};
};

struct LayoutMeasuredWord {
    uint32_t start = 0;
    uint32_t length = 0;
    float width = 0.0f;
};

struct LayoutTextCacheEntry {
    uint64_t hash = 0;
    uint32_t word_start = 0;
    uint32_t word_count = 0;
    Vec2 unwrapped = {};
    float min_width = 0.0f;
    bool contains_newlines = false;
    bool occupied = false;
};

using LayoutMeasureTextFn = Vec2 (*)(Str slice, const LayoutTextConfig& cfg, void* user_data) noexcept;

struct LayoutEngine {
    Vec2 viewport = {1280.0f, 720.0f};
    bool culling_enabled = true;
    LayoutMeasureTextFn measure_text_fn = nullptr;
    void* measure_text_user = nullptr;

    PodBuf<LayoutNode> nodes = {};
    PodBuf<uint32_t> open_stack = {};
    PodBuf<LayoutRoot> roots = {};
    PodBuf<LayoutRoot> root_scratch = {};
    PodBuf<LayoutWrappedLine> wrapped_lines = {};
    PodBuf<LayoutCmd> cmds = {};
    PodBuf<uint32_t> text_nodes = {};
    PodBuf<uint32_t> aspect_nodes = {};
    PodBuf<uint32_t> bfs_queue = {};
    PodBuf<uint32_t> post_order = {};
    PodBuf<uint32_t> scratch_children = {};
    PodBuf<float> scratch_f = {};
    PodBuf<uint8_t> scratch_u8 = {};

    // Current-frame and last-completed-frame bounds/scroll hash tables (power-of-two)
    LayoutBoundsEntry* cur_map = nullptr;
    uint32_t cur_map_cap = 0;
    uint32_t cur_map_count = 0;

    LayoutBoundsEntry* prev_map = nullptr;
    uint32_t prev_map_cap = 0;
    uint32_t prev_map_count = 0;

    // Persistent text word measurement cache across frames (reset on font_revision change)
    LayoutTextCacheEntry* text_cache = nullptr;
    uint32_t text_cache_cap = 0;
    uint32_t text_cache_count = 0;
    PodBuf<LayoutMeasuredWord> cached_words = {};
};

// True Occlusion Hitbox Tree entry (built in paint order during end_frame).
struct HitboxEntry {
    uint32_t id = 0;
    uint32_t layout_elem_id = 0;
    uint32_t parent_idx = UINT32_MAX;
    uint32_t floating_root_id = 0;
    Rect layout_rect = {};
    Rect visual_rect = {};
    Rect visible_rect = {};
    Rect clip_rect = {};
    Vec2 local_scale = {1.0f, 1.0f};
    float visual_opacity = 1.0f;
    float rotation_rad = 0.0f;
    float clip_radius = 0.0f;
    uint32_t paint_order = 0;
    int16_t z_index = 0;
    HitboxBehavior behavior = HitboxBehavior::Normal;
    bool is_floating_root = false;
    bool is_opaque_surface = false;
    bool is_text_span = false;
};

// Dirty Subtree Caching records (O(1) replay of Prims + Hitboxes + A11y).
struct CachedPrimRecord {
    Prim prim = {};
    SDL_GPUTexture* ext1 = nullptr;
    SDL_GPUTexture* ext2 = nullptr;
    uint8_t blend_mode = 0;
    uint8_t aux_flags = 0;
    bool is_punch = false;
};

struct CachedA11yRecord {
    A11yNode node = {};
    uint32_t parent_rel = UINT32_MAX;
    uint32_t label_off = 0;
    uint32_t value_off = 0;
};

struct SubtreeMeta {
    uint32_t id = 0;
    uint64_t content_hash = 0;
    uint32_t a11y_start = 0;
    uint32_t a11y_end = 0;
    uint32_t anim_count_start = 0;
    void* parent_a11y = nullptr;
    bool tainted = false;
    bool has_interactive = false;
};

struct SubtreeCacheEntry {
    uint32_t id = 0;
    uint64_t content_hash = 0;
    uint32_t theme_epoch = 0;
    uint32_t font_revision = 0;
    uint32_t atlas_uv_epoch = 0;
    float dpi_scale = 1.0f;
    uint32_t viewport_w = 0;
    uint32_t viewport_h = 0;
    Rect logical_bounds = {};
    Vec2 origin_px = {};
    CachedPrimRecord* prims = nullptr;
    uint32_t prim_count = 0;
    uint32_t prim_cap = 0;
    HitboxEntry* hitboxes = nullptr;
    uint32_t hitbox_count = 0;
    uint32_t hitbox_cap = 0;
    CachedA11yRecord* a11y_nodes = nullptr;
    uint32_t a11y_count = 0;
    uint32_t a11y_cap = 0;
    char* a11y_strings = nullptr;
    uint32_t a11y_str_len = 0;
    uint32_t a11y_str_cap = 0;
    uint32_t last_used_frame = 0;
    bool has_interactive = false;
    bool valid = false;
};

struct SubtreeCaptureFrame {
    uint32_t id = 0;
    SubtreeMeta* meta = nullptr;
};

// ============================================================================
// Internal hash & geometry helpers
// ============================================================================

inline uint32_t layout_hash_u32(uint32_t x) noexcept {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x ? x : 1u;
}

inline uint32_t layout_auto_id(uint32_t parent_id, uint32_t child_index) noexcept {
    uint32_t h = 2166136261u;
    h = (h ^ parent_id) * 16777619u;
    h = (h ^ (child_index + 1u)) * 16777619u;
    return layout_hash_u32(h);
}

inline float layout_minf(float a, float b) noexcept { return a < b ? a : b; }
inline float layout_maxf(float a, float b) noexcept { return a > b ? a : b; }
inline float layout_clampf(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

inline float layout_sizing_min(const Sizing& s) noexcept {
    if (s.kind == SizingKind::Fixed) return s.value > 0.0f ? s.value : 0.0f;
    return s.min > 0.0f ? s.min : 0.0f;
}

inline float layout_sizing_max(const Sizing& s) noexcept {
    if (s.kind == SizingKind::Fixed) return s.value > 0.0f ? s.value : 0.0f;
    return s.max > 0.0f ? s.max : kLayoutMaxFloat;
}

inline void layout_bounds_map_clear(LayoutBoundsEntry* map, uint32_t cap) noexcept {
    if (map && cap) std::fill_n(map, cap, LayoutBoundsEntry{});
}

inline bool layout_bounds_map_reserve(LayoutBoundsEntry** map, uint32_t* cap, uint32_t* count, uint32_t needed) noexcept {
    if (*cap > 0u && needed * 2u <= *cap) return true;
    uint32_t next = *cap ? (*cap * 2u) : 256u;
    while (needed * 2u > next) next *= 2u;
    auto* fresh = static_cast<LayoutBoundsEntry*>(std::calloc(next, sizeof(LayoutBoundsEntry)));
    if (!fresh) return false;
    const uint32_t mask = next - 1u;
    for (uint32_t i = 0; i < *cap; ++i) {
        if ((*map)[i].id == 0u) continue;
        uint32_t slot = layout_hash_u32((*map)[i].id) & mask;
        while (fresh[slot].id != 0u) slot = (slot + 1u) & mask;
        fresh[slot] = (*map)[i];
    }
    std::free(*map);
    *map = fresh;
    *cap = next;
    (void)count;
    return true;
}

inline void layout_bounds_map_put(
    LayoutBoundsEntry** map,
    uint32_t* cap,
    uint32_t* count,
    const LayoutBoundsEntry& entry) noexcept
{
    if (entry.id == 0u) return;
    if (!layout_bounds_map_reserve(map, cap, count, *count + 1u)) return;
    const uint32_t mask = *cap - 1u;
    uint32_t slot = layout_hash_u32(entry.id) & mask;
    while ((*map)[slot].id != 0u && (*map)[slot].id != entry.id) {
        slot = (slot + 1u) & mask;
    }
    if ((*map)[slot].id == 0u) ++(*count);
    (*map)[slot] = entry;
}

inline const LayoutBoundsEntry* layout_bounds_map_find(const LayoutBoundsEntry* map, uint32_t cap, uint32_t id) noexcept {
    if (!map || cap == 0u || id == 0u) return nullptr;
    const uint32_t mask = cap - 1u;
    uint32_t slot = layout_hash_u32(id) & mask;
    for (uint32_t probe = 0u; probe < cap; ++probe) {
        const LayoutBoundsEntry& e = map[slot];
        if (e.id == 0u) return nullptr;
        if (e.id == id) return &e;
        slot = (slot + 1u) & mask;
    }
    return nullptr;
}

inline LayoutBoundsEntry* layout_bounds_map_find_mut(LayoutBoundsEntry* map, uint32_t cap, uint32_t id) noexcept {
    return const_cast<LayoutBoundsEntry*>(layout_bounds_map_find(map, cap, id));
}

// ============================================================================
// Lifecycle
// ============================================================================

inline void layout_engine_reset_text_cache(LayoutEngine* le) noexcept {
    if (!le) return;
    if (le->text_cache && le->text_cache_cap) {
        std::fill_n(le->text_cache, le->text_cache_cap, LayoutTextCacheEntry{});
    }
    le->text_cache_count = 0;
    le->cached_words.clear();
}

inline LayoutEngine* layout_engine_create(LayoutMeasureTextFn measure_fn, void* user_data) noexcept {
    auto* le = static_cast<LayoutEngine*>(std::calloc(1u, sizeof(LayoutEngine)));
    if (!le) return nullptr;
    le->viewport = {1280.0f, 720.0f};
    le->culling_enabled = true;
    le->measure_text_fn = measure_fn;
    le->measure_text_user = user_data;
    le->nodes.reserve(256u);
    le->open_stack.reserve(64u);
    le->roots.reserve(32u);
    le->wrapped_lines.reserve(128u);
    le->cmds.reserve(256u);
    le->cached_words.reserve(512u);
    return le;
}

inline void layout_engine_destroy(LayoutEngine* le) noexcept {
    if (!le) return;
    le->nodes.destroy();
    le->open_stack.destroy();
    le->roots.destroy();
    le->root_scratch.destroy();
    le->wrapped_lines.destroy();
    le->cmds.destroy();
    le->text_nodes.destroy();
    le->aspect_nodes.destroy();
    le->bfs_queue.destroy();
    le->post_order.destroy();
    le->scratch_children.destroy();
    le->scratch_f.destroy();
    le->scratch_u8.destroy();
    std::free(le->cur_map);
    std::free(le->prev_map);
    std::free(le->text_cache);
    le->cached_words.destroy();
    std::free(le);
}

inline void layout_engine_set_viewport(LayoutEngine* le, float w, float h) noexcept {
    if (!le) return;
    le->viewport = {w > 0.0f ? w : 1.0f, h > 0.0f ? h : 1.0f};
}

inline Rect layout_engine_bounds(const LayoutEngine* le, uint32_t id, bool* found_out = nullptr) noexcept {
    if (found_out) *found_out = false;
    if (!le || id == 0u) return {};
    if (const LayoutBoundsEntry* e = layout_bounds_map_find(le->prev_map, le->prev_map_cap, id)) {
        if (found_out) *found_out = true;
        return e->bounds;
    }
    return {};
}

inline LayoutScrollData layout_engine_scroll_data(const LayoutEngine* le, uint32_t id) noexcept {
    LayoutScrollData out = {};
    if (!le || id == 0u) return out;
    if (const LayoutBoundsEntry* e = layout_bounds_map_find(le->prev_map, le->prev_map_cap, id)) {
        if (e->has_scroll) {
            out.found = true;
            out.viewport_dim = {e->bounds.w, e->bounds.h};
            out.content_dim = e->content_size;
        }
    }
    return out;
}

inline LayoutNode* layout_engine_open_node(LayoutEngine* le) noexcept {
    if (!le || le->open_stack.count == 0u) return nullptr;
    const uint32_t idx = le->open_stack.data[le->open_stack.count - 1u];
    return idx < le->nodes.count ? &le->nodes.data[idx] : nullptr;
}

inline LayoutNode* layout_engine_find_cur_node(LayoutEngine* le, uint32_t id) noexcept {
    if (!le || id == 0u) return nullptr;
    if (const LayoutBoundsEntry* e = layout_bounds_map_find(le->cur_map, le->cur_map_cap, id)) {
        if (e->node_idx < le->nodes.count) return &le->nodes.data[e->node_idx];
    }
    return nullptr;
}

// ============================================================================
// Text Measurement & CJK + Kinsoku Tokenization Cache
// ============================================================================

inline uint64_t layout_hash_text_key(Str text, const LayoutTextConfig& cfg) noexcept {
    uint64_t h = 14695981039346656037ull;
    const auto mix = [&](const void* p, size_t n) noexcept {
        const auto* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    if (text.chars && text.length) mix(text.chars, text.length);
    mix(&cfg.font_id, sizeof(cfg.font_id));
    mix(&cfg.font_size_px, sizeof(cfg.font_size_px));
    mix(&cfg.letter_spacing, sizeof(cfg.letter_spacing));
    mix(&cfg.line_height, sizeof(cfg.line_height));
    const uint8_t flags = (cfg.wrap ? 1u : 0u) | (cfg.is_rich ? 2u : 0u);
    mix(&flags, 1u);
    if (cfg.is_rich) mix(&cfg.rich_hash, sizeof(cfg.rich_hash));
    if (cfg.opentype_flags != 0u) mix(&cfg.opentype_flags, sizeof(cfg.opentype_flags));
    if (cfg.font_features && cfg.font_feature_count > 0u) {
        mix(&cfg.font_feature_count, sizeof(cfg.font_feature_count));
        mix(cfg.font_features, size_t(cfg.font_feature_count) * sizeof(FontFeature));
    }
    return h ? h : 1ull;
}

inline bool layout_is_kinsoku_no_end(uint32_t cp) noexcept {
    return cp == 0xFF08u || cp == 0x300Au || cp == 0x300Cu || cp == 0x300Eu ||
           cp == 0x3010u || cp == 0x3008u || cp == 0x3014u || cp == 0x2018u || cp == 0x201Cu;
}

inline bool layout_is_kinsoku_no_start(uint32_t cp) noexcept {
    return cp == 0xFF0Cu || cp == 0x3002u || cp == 0x3001u || cp == 0xFF1Bu || cp == 0xFF1Au ||
           cp == 0xFF01u || cp == 0xFF1Fu || cp == 0xFF09u || cp == 0x300Bu || cp == 0x300Du ||
           cp == 0x300Fu || cp == 0x3011u || cp == 0x3009u || cp == 0x3015u || cp == 0x2019u || cp == 0x201Du;
}

inline bool layout_is_cjk_breakable(uint32_t cp) noexcept {
    return (cp >= 0x2E80u && cp <= 0x9FFFu) || (cp >= 0xAC00u && cp <= 0xD7AFu) ||
           (cp >= 0xF900u && cp <= 0xFAFFu) || (cp >= 0xFF01u && cp <= 0xFF60u) ||
           (cp >= 0x20000u && cp <= 0x3FFFFu);
}

inline uint32_t layout_measure_text_cached(LayoutEngine* le, Str text, const LayoutTextConfig& cfg) noexcept {
    if (!le || !le->measure_text_fn) return UINT32_MAX;
    if (le->text_cache_count > 16384u || le->cached_words.count > 131072u) {
        layout_engine_reset_text_cache(le);
    }
    if (le->text_cache_cap == 0u || (le->text_cache_count + 1u) * 2u > le->text_cache_cap) {
        const uint32_t next_cap = le->text_cache_cap ? (le->text_cache_cap * 2u) : 512u;
        auto* fresh = static_cast<LayoutTextCacheEntry*>(std::calloc(next_cap, sizeof(LayoutTextCacheEntry)));
        if (!fresh) return UINT32_MAX;
        const uint32_t mask = next_cap - 1u;
        for (uint32_t i = 0; i < le->text_cache_cap; ++i) {
            if (!le->text_cache[i].occupied) continue;
            uint32_t s = static_cast<uint32_t>(le->text_cache[i].hash) & mask;
            while (fresh[s].occupied) s = (s + 1u) & mask;
            fresh[s] = le->text_cache[i];
        }
        std::free(le->text_cache);
        le->text_cache = fresh;
        le->text_cache_cap = next_cap;
    }

    const uint64_t key = layout_hash_text_key(text, cfg);
    const uint32_t mask = le->text_cache_cap - 1u;
    uint32_t slot = static_cast<uint32_t>(key) & mask;
    while (le->text_cache[slot].occupied) {
        if (le->text_cache[slot].hash == key) return slot;
        slot = (slot + 1u) & mask;
    }

    LayoutTextCacheEntry entry = {};
    entry.hash = key;
    entry.occupied = true;
    entry.word_start = le->cached_words.count;

    if (!cfg.wrap) {
        uint32_t len = 0;
        while (len < text.length && text.chars[len] != '\n') ++len;
        entry.unwrapped = le->measure_text_fn(Str{text.chars, len, text.is_static}, cfg, le->measure_text_user);
        entry.min_width = 0.0f;
        entry.contains_newlines = len < text.length;
        entry.word_count = 0;
        le->text_cache[slot] = entry;
        ++le->text_cache_count;
        return slot;
    }

    const Vec2 space_dims = le->measure_text_fn(Str{" ", 1u, true}, cfg, le->measure_text_user);
    float max_line_w = 0.0f;
    float cur_line_w = 0.0f;
    float max_h = 0.0f;
    uint32_t start = 0;
    uint32_t end = 0;

    const auto push_word = [&](uint32_t ws, uint32_t wl, float ww) noexcept {
        le->cached_words.push(LayoutMeasuredWord{ws, wl, ww});
        ++entry.word_count;
    };

    while (end < text.length) {
        const unsigned char ub = static_cast<unsigned char>(text.chars[end]);
        const char ch = text.chars[end];
        if (ch == ' ' || ch == '\n') {
            const uint32_t len = end - start;
            Vec2 dims = {};
            if (len > 0u) {
                dims = le->measure_text_fn(Str{text.chars + start, len, false}, cfg, le->measure_text_user);
                max_h = layout_maxf(max_h, dims.y);
            }
            entry.min_width = layout_maxf(entry.min_width, dims.x);
            if (ch == ' ') {
                float sw = space_dims.x;
                if (cfg.is_rich) {
                    const Vec2 sdims = le->measure_text_fn(Str{text.chars + end, 1u, false}, cfg, le->measure_text_user);
                    sw = sdims.x;
                    max_h = layout_maxf(max_h, sdims.y);
                } else {
                    max_h = layout_maxf(max_h, space_dims.y);
                }
                dims.x += sw;
                push_word(start, len + 1u, dims.x);
                cur_line_w += dims.x;
            } else {
                if (len > 0u) {
                    push_word(start, len, dims.x);
                    cur_line_w += dims.x;
                }
                push_word(end, 0u, 0.0f); // explicit newline sentinel
                max_line_w = layout_maxf(max_line_w, cur_line_w);
                cur_line_w = 0.0f;
                entry.contains_newlines = true;
            }
            start = end + 1u;
            ++end;
            continue;
        }
        if (ub >= 0xE0u) {
            uint32_t cp_len = 1u;
            uint32_t cp = ub;
            if ((ub & 0xF0u) == 0xE0u && end + 2u < text.length) {
                cp_len = 3u;
                cp = (uint32_t(ub & 0x0Fu) << 12) |
                     (uint32_t(static_cast<unsigned char>(text.chars[end + 1u]) & 0x3Fu) << 6) |
                     uint32_t(static_cast<unsigned char>(text.chars[end + 2u]) & 0x3Fu);
            } else if ((ub & 0xF8u) == 0xF0u && end + 3u < text.length) {
                cp_len = 4u;
                cp = (uint32_t(ub & 0x07u) << 18) |
                     (uint32_t(static_cast<unsigned char>(text.chars[end + 1u]) & 0x3Fu) << 12) |
                     (uint32_t(static_cast<unsigned char>(text.chars[end + 2u]) & 0x3Fu) << 6) |
                     uint32_t(static_cast<unsigned char>(text.chars[end + 3u]) & 0x3Fu);
            }
            if (layout_is_cjk_breakable(cp)) {
                const bool no_end = layout_is_kinsoku_no_end(cp);
                const bool no_start = layout_is_kinsoku_no_start(cp);
                if (end > start && !no_start) {
                    bool prev_opener = false;
                    if (end - start == 3u && (static_cast<unsigned char>(text.chars[start]) & 0xF0u) == 0xE0u) {
                        const uint32_t pcp = (uint32_t(static_cast<unsigned char>(text.chars[start]) & 0x0Fu) << 12) |
                            (uint32_t(static_cast<unsigned char>(text.chars[start + 1u]) & 0x3Fu) << 6) |
                            uint32_t(static_cast<unsigned char>(text.chars[start + 2u]) & 0x3Fu);
                        prev_opener = layout_is_kinsoku_no_end(pcp);
                    }
                    if (!prev_opener) {
                        const Vec2 dims = le->measure_text_fn(Str{text.chars + start, end - start, false}, cfg, le->measure_text_user);
                        max_h = layout_maxf(max_h, dims.y);
                        entry.min_width = layout_maxf(entry.min_width, dims.x);
                        push_word(start, end - start, dims.x);
                        cur_line_w += dims.x;
                        start = end;
                    }
                }
                end += cp_len;
                if (!no_end) {
                    bool next_no_start = false;
                    if (end < text.length) {
                        const unsigned char nb = static_cast<unsigned char>(text.chars[end]);
                        if (nb == ',' || nb == '.' || nb == '!' || nb == '?' || nb == ';' || nb == ':' || nb == ')' || nb == ']' || nb == '}') {
                            next_no_start = true;
                        } else if ((nb & 0xF0u) == 0xE0u && end + 2u < text.length) {
                            const uint32_t ncp = (uint32_t(nb & 0x0Fu) << 12) |
                                (uint32_t(static_cast<unsigned char>(text.chars[end + 1u]) & 0x3Fu) << 6) |
                                uint32_t(static_cast<unsigned char>(text.chars[end + 2u]) & 0x3Fu);
                            next_no_start = layout_is_kinsoku_no_start(ncp);
                        }
                    }
                    if (!next_no_start && end > start) {
                        const Vec2 dims = le->measure_text_fn(Str{text.chars + start, end - start, false}, cfg, le->measure_text_user);
                        max_h = layout_maxf(max_h, dims.y);
                        entry.min_width = layout_maxf(entry.min_width, dims.x);
                        push_word(start, end - start, dims.x);
                        cur_line_w += dims.x;
                        start = end;
                    }
                }
                continue;
            }
            end += cp_len;
            continue;
        }
        ++end;
    }

    if (end > start) {
        const Vec2 dims = le->measure_text_fn(Str{text.chars + start, end - start, false}, cfg, le->measure_text_user);
        max_h = layout_maxf(max_h, dims.y);
        entry.min_width = layout_maxf(entry.min_width, dims.x);
        push_word(start, end - start, dims.x);
        cur_line_w += dims.x;
    }
    max_line_w = layout_maxf(max_line_w, cur_line_w);
    if (max_h <= 0.0f) max_h = space_dims.y > 0.0f ? space_dims.y : cfg.font_size_px;
    entry.unwrapped = {max_line_w, max_h};
    le->text_cache[slot] = entry;
    ++le->text_cache_count;
    return slot;
}

// ============================================================================
// Frame Begin & Tree Construction (Pass 1 Intrinsic Bottom-Up on Close)
// ============================================================================

inline uint32_t layout_grid_col_count(const LayoutNode& n) noexcept {
    return n.cfg.grid_col_track_count ? n.cfg.grid_col_track_count : n.cfg.grid_columns;
}

inline bool layout_is_rows(const LayoutNode& n) noexcept {
    return layout_grid_col_count(n) > 0u || (n.cfg.wrap && n.cfg.axis == Axis::Row);
}

inline void layout_update_aspect_box(LayoutNode& n) noexcept {
    if (!n.is_text && n.cfg.aspect_ratio > 0.0f) {
        if (n.width == 0.0f && n.height != 0.0f) {
            n.width = n.height * n.cfg.aspect_ratio;
        } else if (n.width != 0.0f && n.height == 0.0f) {
            n.height = n.width / n.cfg.aspect_ratio;
        }
    }
}

inline uint32_t layout_place_grid_children(LayoutEngine* le, LayoutNode& p, uint32_t col_count) noexcept {
    bool any_row_span = false;
    for (uint32_t ci = p.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
        if (!le->nodes.data[ci].is_text && le->nodes.data[ci].cfg.grid_row_span > 1u) {
            any_row_span = true;
            break;
        }
    }
    if (!any_row_span) {
        uint32_t row = 0, col = 0;
        for (uint32_t ci = p.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
            LayoutNode& ch = le->nodes.data[ci];
            uint32_t span = ch.is_text ? 1u : (ch.cfg.grid_col_span ? ch.cfg.grid_col_span : 1u);
            if (span > col_count) span = col_count;
            if (col + span > col_count) { ++row; col = 0; }
            ch.cell_row = static_cast<int32_t>(row);
            ch.cell_col = static_cast<int32_t>(col);
            ch.cell_col_span = static_cast<int32_t>(span);
            ch.cell_row_span = 1;
            col += span;
        }
        return p.child_count > 0u ? (row + 1u) : 0u;
    }

    // 2D occupancy grid when any child spans multiple rows.
    le->scratch_u8.clear();
    uint32_t occ_rows = 0;
    const auto ensure_rows = [&](uint32_t needed_rows) noexcept -> bool {
        if (needed_rows <= occ_rows) return true;
        const uint32_t needed_cells = needed_rows * col_count;
        if (!le->scratch_u8.reserve(needed_cells)) return false;
        std::memset(le->scratch_u8.data + occ_rows * col_count, 0, size_t(needed_cells - occ_rows * col_count));
        le->scratch_u8.count = needed_cells;
        occ_rows = needed_rows;
        return true;
    };

    uint32_t cur_row = 0, cur_col = 0, max_row_end = 0;
    for (uint32_t ci = p.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
        LayoutNode& ch = le->nodes.data[ci];
        uint32_t cspan = ch.is_text ? 1u : (ch.cfg.grid_col_span ? ch.cfg.grid_col_span : 1u);
        uint32_t rspan = ch.is_text ? 1u : (ch.cfg.grid_row_span ? ch.cfg.grid_row_span : 1u);
        if (cspan > col_count) cspan = col_count;
        for (;;) {
            if (cur_col + cspan > col_count) { ++cur_row; cur_col = 0; }
            if (!ensure_rows(cur_row + rspan)) break;
            bool free_slot = true;
            for (uint32_t r = cur_row; r < cur_row + rspan && free_slot; ++r) {
                for (uint32_t c = cur_col; c < cur_col + cspan; ++c) {
                    if (le->scratch_u8.data[r * col_count + c]) { free_slot = false; break; }
                }
            }
            if (free_slot) break;
            ++cur_col;
        }
        ch.cell_row = static_cast<int32_t>(cur_row);
        ch.cell_col = static_cast<int32_t>(cur_col);
        ch.cell_col_span = static_cast<int32_t>(cspan);
        ch.cell_row_span = static_cast<int32_t>(rspan);
        for (uint32_t r = cur_row; r < cur_row + rspan && r < occ_rows; ++r) {
            for (uint32_t c = cur_col; c < cur_col + cspan; ++c) {
                le->scratch_u8.data[r * col_count + c] = 1u;
            }
        }
        if (cur_row + rspan > max_row_end) max_row_end = cur_row + rspan;
        cur_col += cspan;
    }
    return max_row_end;
}

inline void layout_solve_grid_tracks(
    uint32_t n,
    const LayoutGridTrack* tracks,
    uint32_t track_count,
    float avail,
    bool intrinsic,
    float* out_sizes) noexcept
{
    bool active[128] = {};
    float weights[128] = {}, mins[128] = {}, maxs[128] = {};
    for (uint32_t i = 0; i < n; ++i) {
        LayoutGridTrack t = {};
        if (i < track_count && tracks) t = tracks[i];
        mins[i] = t.min_px > 0.0f ? t.min_px : 0.0f;
        maxs[i] = t.max_px > 0.0f ? t.max_px : kLayoutMaxFloat;
        active[i] = (t.kind == SizingKind::Grow) && !intrinsic;
        weights[i] = t.value > 0.0f ? t.value : 1.0f;
        float base = out_sizes[i];
        if (t.kind == SizingKind::Fixed) base = t.value;
        else if (t.kind == SizingKind::Percent && !intrinsic) base = avail * t.value;
        out_sizes[i] = layout_minf(maxs[i], layout_maxf(mins[i], base));
    }
    if (intrinsic) return;
    for (uint32_t pass = 0; pass <= n; ++pass) {
        float remaining = avail;
        float weight = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            if (active[i]) weight += weights[i];
            else remaining -= out_sizes[i];
        }
        if (weight <= 0.0f) break;
        bool froze = false;
        float bounded_sum = 0.0f;
        const float pos_rem = layout_maxf(0.0f, remaining);
        for (uint32_t i = 0; i < n; ++i) {
            if (!active[i]) continue;
            const float v = pos_rem * weights[i] / weight;
            out_sizes[i] = layout_minf(maxs[i], layout_maxf(mins[i], v));
            bounded_sum += out_sizes[i];
        }
        for (uint32_t i = 0; i < n; ++i) {
            if (!active[i]) continue;
            const float v = pos_rem * weights[i] / weight;
            if ((bounded_sum > remaining && v < mins[i]) || (bounded_sum < remaining && v > maxs[i])) {
                active[i] = false;
                froze = true;
            }
        }
        if (!froze) break;
    }
}

inline void layout_compute_grid_columns(LayoutEngine* le, LayoutNode& p, float* widths, bool intrinsic) noexcept {
    const uint32_t n = layout_grid_col_count(p);
    const float avail = layout_maxf(0.0f, p.width - p.cfg.padding.left - p.cfg.padding.right - float(n > 1u ? n - 1u : 0u) * p.cfg.child_gap);
    for (uint32_t c = 0; c < n; ++c) widths[c] = 0.0f;
    (void)layout_place_grid_children(le, p, n);
    for (uint32_t ci = p.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
        const LayoutNode& ch = le->nodes.data[ci];
        if (ch.cell_col_span == 1) {
            const uint32_t c = static_cast<uint32_t>(ch.cell_col);
            const bool fit_col = (c >= p.cfg.grid_col_track_count) ||
                (p.cfg.grid_col_tracks[c].kind == SizingKind::Fit) ||
                (intrinsic && p.cfg.grid_col_tracks[c].kind == SizingKind::Grow);
            if (fit_col) {
                const float outer_w = ch.width + ch.cfg.margin.left + ch.cfg.margin.right;
                if (outer_w > widths[c]) widths[c] = outer_w;
            }
        }
    }
    layout_solve_grid_tracks(n, p.cfg.grid_col_tracks, p.cfg.grid_col_track_count, avail, intrinsic, widths);
}

inline void layout_begin(LayoutEngine* le, float viewport_w, float viewport_h) noexcept {
    if (!le) return;
    layout_engine_set_viewport(le, viewport_w, viewport_h);
    le->nodes.clear();
    le->open_stack.clear();
    le->roots.clear();
    le->wrapped_lines.clear();
    le->cmds.clear();
    le->text_nodes.clear();
    le->aspect_nodes.clear();
    layout_bounds_map_clear(le->cur_map, le->cur_map_cap);
    le->cur_map_count = 0;

    // Allocate synthetic Root node 0.
    LayoutNode* root = le->nodes.push_uninit();
    if (!root) return;
    *root = LayoutNode{};
    root->id = id("EtchRoot");
    root->cfg.width = Sizing::fixed(le->viewport.x);
    root->cfg.height = Sizing::fixed(le->viewport.y);
    root->width = le->viewport.x;
    root->height = le->viewport.y;
    root->min_w = le->viewport.x;
    root->min_h = le->viewport.y;
    le->open_stack.push(0u);
    le->roots.push(LayoutRoot{.node_idx = 0u, .parent_idx = kLayoutInvalidNode, .order = 0u, .z_index = 0});
    layout_bounds_map_put(&le->cur_map, &le->cur_map_cap, &le->cur_map_count, LayoutBoundsEntry{.id = root->id, .node_idx = 0u});
}

inline uint32_t layout_open(LayoutEngine* le, uint32_t explicit_id) noexcept {
    if (!le || le->open_stack.count == 0u) return kLayoutInvalidNode;
    const uint32_t parent_idx = le->open_stack.data[le->open_stack.count - 1u];
    const uint32_t idx = le->nodes.count;
    LayoutNode* n = le->nodes.push_uninit();
    if (!n) return kLayoutInvalidNode;
    *n = LayoutNode{};
    const uint32_t parent_id = le->nodes.data[parent_idx].id;
    const uint32_t sib_ord = le->nodes.data[parent_idx].auto_child_seq++;
    n->id = explicit_id ? explicit_id : layout_auto_id(parent_id, sib_ord);
    n->parent_idx = parent_idx;
    le->open_stack.push(idx);
    layout_bounds_map_put(&le->cur_map, &le->cur_map_cap, &le->cur_map_count, LayoutBoundsEntry{.id = n->id, .node_idx = idx});
    return idx;
}

inline void layout_configure_open(LayoutEngine* le, const LayoutNodeConfig& cfg) noexcept {
    LayoutNode* n = layout_engine_open_node(le);
    if (!n) return;
    const uint32_t idx = le->open_stack.data[le->open_stack.count - 1u];
    n->cfg = cfg;
    if (n->cfg.attach_to != AttachTo::None) {
        n->is_floating = true;
        le->roots.push(LayoutRoot{
            .node_idx = idx,
            .parent_idx = n->parent_idx,
            .order = le->roots.count,
            .z_index = n->cfg.z_index,
        });
    } else if (n->parent_idx != kLayoutInvalidNode) {
        LayoutNode& p = le->nodes.data[n->parent_idx];
        if (p.last_child == kLayoutInvalidNode) p.first_child = idx;
        else le->nodes.data[p.last_child].next_sibling = idx;
        p.last_child = idx;
        ++p.child_count;
    }
    if (n->cfg.aspect_ratio > 0.0f) {
        le->aspect_nodes.push(idx);
    }
}

inline void layout_close(LayoutEngine* le) noexcept {
    if (!le || le->open_stack.count == 0u) return;
    const uint32_t idx = le->open_stack.data[--le->open_stack.count];
    LayoutNode& n = le->nodes.data[idx];
    const LayoutNodeConfig& c = n.cfg;
    const float pad_x = c.padding.left + c.padding.right;
    const float pad_y = c.padding.top + c.padding.bottom;

    n.width = pad_x;
    n.height = pad_y;
    n.min_w = pad_x;
    n.min_h = pad_y;

    const uint32_t col_count = layout_grid_col_count(n);
    if (col_count > 0u) {
        float widths[128] = {};
        layout_compute_grid_columns(le, n, widths, true);
        float w = pad_x;
        for (uint32_t ci = 0; ci < col_count; ++ci) w += widths[ci] + (ci ? c.child_gap : 0.0f);
        n.width = w;
        n.min_w = c.clip_x ? pad_x : w;
        for (uint32_t ci = n.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
            const LayoutNode& ch = le->nodes.data[ci];
            const float ch_h = ch.height + ch.cfg.margin.top + ch.cfg.margin.bottom;
            const float ch_mh = ch.min_h + ch.cfg.margin.top + ch.cfg.margin.bottom;
            n.height = layout_maxf(n.height, ch_h + pad_y);
            if (!c.clip_y) n.min_h = layout_maxf(n.min_h, ch_mh + pad_y);
        }
    } else if (c.axis == Axis::Row) {
        for (uint32_t ci = n.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
            const LayoutNode& ch = le->nodes.data[ci];
            const float mx = ch.cfg.margin.left + ch.cfg.margin.right;
            const float my = ch.cfg.margin.top + ch.cfg.margin.bottom;
            n.width += ch.width + mx;
            n.height = layout_maxf(n.height, ch.height + my + pad_y);
            if (!c.clip_x) {
                n.min_w = c.wrap ? layout_maxf(n.min_w, pad_x + ch.min_w + mx) : (n.min_w + ch.min_w + mx);
            }
            if (!c.clip_y) {
                n.min_h = layout_maxf(n.min_h, ch.min_h + my + pad_y);
            }
        }
        const float gaps = float(n.child_count > 1u ? n.child_count - 1u : 0u) * c.child_gap;
        n.width += gaps;
        if (!c.clip_x && !c.wrap) n.min_w += gaps;
    } else {
        for (uint32_t ci = n.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
            const LayoutNode& ch = le->nodes.data[ci];
            const float mx = ch.cfg.margin.left + ch.cfg.margin.right;
            const float my = ch.cfg.margin.top + ch.cfg.margin.bottom;
            n.width = layout_maxf(n.width, ch.width + mx + pad_x);
            n.height += ch.height + my;
            if (!c.clip_x) n.min_w = layout_maxf(n.min_w, ch.min_w + mx + pad_x);
            if (!c.clip_y) n.min_h += ch.min_h + my;
        }
        const float gaps = float(n.child_count > 1u ? n.child_count - 1u : 0u) * c.child_gap;
        n.height += gaps;
        if (!c.clip_y) n.min_h += gaps;
    }

    if (c.shrink_width && (c.width.kind == SizingKind::Fit || c.width.kind == SizingKind::Grow)) {
        n.min_w = pad_x;
    }
    if (c.shrink_height && (c.height.kind == SizingKind::Fit || c.height.kind == SizingKind::Grow)) {
        n.min_h = pad_y;
    }

    if (c.width.kind == SizingKind::Fixed) {
        n.width = c.width.value > 0.0f ? c.width.value : 0.0f;
        n.min_w = n.width;
    } else if (c.width.kind != SizingKind::Percent) {
        const float lo = layout_sizing_min(c.width);
        const float hi = layout_sizing_max(c.width);
        n.width = layout_clampf(n.width, lo, hi);
        n.min_w = layout_clampf(n.min_w, lo, hi);
    } else {
        n.width = 0.0f;
        n.min_w = 0.0f;
    }

    if (c.height.kind == SizingKind::Fixed) {
        n.height = c.height.value > 0.0f ? c.height.value : 0.0f;
        n.min_h = n.height;
    } else if (c.height.kind != SizingKind::Percent) {
        const float lo = layout_sizing_min(c.height);
        const float hi = layout_sizing_max(c.height);
        n.height = layout_clampf(n.height, lo, hi);
        n.min_h = layout_clampf(n.min_h, lo, hi);
    } else {
        n.height = 0.0f;
        n.min_h = 0.0f;
    }

    layout_update_aspect_box(n);
}

inline uint32_t layout_open_text(LayoutEngine* le, Str text, const LayoutTextConfig& cfg) noexcept {
    if (!le || le->open_stack.count == 0u) return 0u;
    const uint32_t parent_idx = le->open_stack.data[le->open_stack.count - 1u];
    const uint32_t idx = le->nodes.count;
    LayoutNode* n = le->nodes.push_uninit();
    if (!n) return 0u;
    *n = LayoutNode{};
    LayoutNode& parent = le->nodes.data[parent_idx];
    n->id = layout_auto_id(parent.id, parent.auto_child_seq++);
    n->parent_idx = parent_idx;
    n->is_text = true;
    n->text = text;
    n->text_cfg = cfg;
    n->cfg.width = Sizing::fit();
    n->cfg.height = Sizing::fit();

    if (parent.last_child == kLayoutInvalidNode) parent.first_child = idx;
    else le->nodes.data[parent.last_child].next_sibling = idx;
    parent.last_child = idx;
    ++parent.child_count;

    n->text_cache_idx = layout_measure_text_cached(le, text, cfg);
    Vec2 unwrapped = {0.0f, cfg.font_size_px};
    float min_w = 0.0f;
    if (n->text_cache_idx < le->text_cache_cap && le->text_cache[n->text_cache_idx].occupied) {
        const auto& ce = le->text_cache[n->text_cache_idx];
        unwrapped = ce.unwrapped;
        min_w = ce.min_width;
    } else if (le->measure_text_fn) {
        unwrapped.y = le->measure_text_fn(Str{}, cfg, le->measure_text_user).y;
    }
    const float line_h = cfg.line_height > 0.0f ? cfg.line_height : unwrapped.y;
    n->text_preferred = unwrapped;
    n->width = unwrapped.x;
    n->height = line_h;
    n->min_w = (cfg.ellipsis || cfg.max_lines > 0u) ? 0.0f : min_w;
    n->min_h = line_h;

    le->text_nodes.push(idx);
    layout_bounds_map_put(&le->cur_map, &le->cur_map_cap, &le->cur_map_count, LayoutBoundsEntry{.id = n->id, .node_idx = idx});
    return n->id;
}

// ============================================================================
// Pass 2 & Pass 5: Grid / Wrap / Flexbox Axis Solvers
// ============================================================================

inline void layout_size_rows_x(LayoutEngine* le, LayoutNode& p) noexcept {
    const LayoutNodeConfig& c = p.cfg;
    const float avail = layout_maxf(0.0f, p.width - c.padding.left - c.padding.right);
    const uint32_t col_count = layout_grid_col_count(p);
    if (col_count > 0u) {
        float widths[128] = {};
        layout_compute_grid_columns(le, p, widths, false);
        for (uint32_t ci = p.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
            LayoutNode& ch = le->nodes.data[ci];
            float x = 0.0f, w = 0.0f;
            for (int32_t k = 0; k < ch.cell_col; ++k) x += widths[k] + c.child_gap;
            for (int32_t k = 0; k < ch.cell_col_span; ++k) w += widths[ch.cell_col + k];
            w += float(ch.cell_col_span - 1) * c.child_gap;
            ch.cell_x = x;
            ch.cell_w = w;
            const float mx = ch.cfg.margin.left + ch.cfg.margin.right;
            const float inner_cell = layout_maxf(0.0f, w - mx);
            if (ch.is_text) {
                ch.width = inner_cell;
            } else if (ch.cfg.width.kind == SizingKind::Percent) {
                ch.width = inner_cell * ch.cfg.width.value;
            } else if (ch.cfg.width.kind == SizingKind::Fixed) {
                ch.width = ch.cfg.width.value;
            } else {
                ch.width = layout_clampf(inner_cell, layout_sizing_min(ch.cfg.width), layout_sizing_max(ch.cfg.width));
            }
            layout_update_aspect_box(ch);
        }
        return;
    }

    // Flex-wrap row partitioning.
    le->scratch_children.clear();
    int32_t row = 0;
    float used = 0.0f;
    bool first = true;
    for (uint32_t ci = p.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
        LayoutNode& ch = le->nodes.data[ci];
        le->scratch_children.push(ci);
        if (!ch.is_text && ch.cfg.width.kind == SizingKind::Percent) {
            ch.width = layout_maxf(ch.min_w, avail * ch.cfg.width.value);
            layout_update_aspect_box(ch);
        }
        const float outer_w = ch.width + ch.cfg.margin.left + ch.cfg.margin.right;
        const float next = used + (first ? 0.0f : c.child_gap) + outer_w;
        if (!first && next > avail + kLayoutEpsilon) {
            ++row;
            used = 0.0f;
            first = true;
        }
        ch.cell_row = row;
        used += (first ? 0.0f : c.child_gap) + outer_w;
        first = false;
    }

    const uint32_t n = le->scratch_children.count;
    for (uint32_t start = 0; start < n;) {
        uint32_t end = start + 1u;
        const int32_t r_id = le->nodes.data[le->scratch_children.data[start]].cell_row;
        while (end < n && le->nodes.data[le->scratch_children.data[end]].cell_row == r_id) ++end;
        const uint32_t live = end - start;
        for (uint32_t pass = 0; pass <= live; ++pass) {
            float sum = 0.0f;
            for (uint32_t i = start; i < end; ++i) {
                const LayoutNode& ch = le->nodes.data[le->scratch_children.data[i]];
                sum += ch.width + ch.cfg.margin.left + ch.cfg.margin.right;
            }
            const float extra = avail - sum - float(live > 1u ? live - 1u : 0u) * c.child_gap;
            if (extra < 0.0f && c.scroll_x) break;
            if (std::fabs(extra) < kLayoutEpsilon) break;
            float total_w = 0.0f;
            for (uint32_t i = start; i < end; ++i) {
                const LayoutNode& ch = le->nodes.data[le->scratch_children.data[i]];
                if (extra > 0.0f) {
                    if (!ch.is_text && ch.cfg.width.kind == SizingKind::Grow && ch.width < layout_sizing_max(ch.cfg.width) - kLayoutEpsilon) {
                        total_w += ch.cfg.flex_grow > 0.0f ? ch.cfg.flex_grow : 1.0f;
                    }
                } else {
                    const bool can_shrink = ch.is_text || (ch.cfg.width.kind != SizingKind::Fixed && ch.cfg.width.kind != SizingKind::Percent && ch.cfg.flex_shrink > 0.0f);
                    if (can_shrink && ch.width > ch.min_w + kLayoutEpsilon) {
                        total_w += ch.is_text ? 1.0f : ch.cfg.flex_shrink;
                    }
                }
            }
            if (total_w <= 0.0f) break;
            for (uint32_t i = start; i < end; ++i) {
                LayoutNode& ch = le->nodes.data[le->scratch_children.data[i]];
                if (extra > 0.0f) {
                    if (!ch.is_text && ch.cfg.width.kind == SizingKind::Grow) {
                        const float wt = ch.cfg.flex_grow > 0.0f ? ch.cfg.flex_grow : 1.0f;
                        ch.width = layout_clampf(ch.width + extra * wt / total_w, ch.min_w, layout_sizing_max(ch.cfg.width));
                        layout_update_aspect_box(ch);
                    }
                } else {
                    const bool can_shrink = ch.is_text || (ch.cfg.width.kind != SizingKind::Fixed && ch.cfg.width.kind != SizingKind::Percent && ch.cfg.flex_shrink > 0.0f);
                    if (can_shrink) {
                        const float wt = ch.is_text ? 1.0f : ch.cfg.flex_shrink;
                        const float hi = ch.is_text ? kLayoutMaxFloat : layout_sizing_max(ch.cfg.width);
                        ch.width = layout_clampf(ch.width + extra * wt / total_w, ch.min_w, hi);
                        layout_update_aspect_box(ch);
                    }
                }
            }
        }
        start = end;
    }
}

inline Vec2 layout_row_metrics(LayoutEngine* le, LayoutNode& p, bool stretch_y) noexcept {
    const LayoutNodeConfig& c = p.cfg;
    const uint32_t col_count = layout_grid_col_count(p);
    if (col_count > 0u) {
        uint32_t max_row_end = 0;
        for (uint32_t ci = p.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
            const LayoutNode& ch = le->nodes.data[ci];
            const uint32_t rend = static_cast<uint32_t>(ch.cell_row + ch.cell_row_span);
            if (rend > max_row_end) max_row_end = rend;
        }
        if (c.grid_row_track_count > max_row_end) max_row_end = c.grid_row_track_count;
        if (max_row_end == 0u) return {};

        le->scratch_f.clear();
        if (!le->scratch_f.reserve(max_row_end * 2u)) return {};
        float* row_h = le->scratch_f.data;
        float* row_y = le->scratch_f.data + max_row_end;
        for (uint32_t r = 0; r < max_row_end; ++r) row_h[r] = 0.0f;

        // Single-row items set base intrinsic row heights.
        for (uint32_t ci = p.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
            const LayoutNode& ch = le->nodes.data[ci];
            if (ch.cell_row_span == 1) {
                const uint32_t r = static_cast<uint32_t>(ch.cell_row);
                const float outer_h = ch.height + ch.cfg.margin.top + ch.cfg.margin.bottom;
                const bool fit_row = (r >= c.grid_row_track_count) ||
                    (c.grid_row_tracks[r].kind == SizingKind::Fit) ||
                    (!stretch_y && c.grid_row_tracks[r].kind == SizingKind::Grow);
                if (fit_row && outer_h > row_h[r]) row_h[r] = outer_h;
            }
        }
        // Solve explicit row tracks if provided (supporting px/fr/percent/fit + min/max).
        if (c.grid_row_track_count > 0u) {
            const uint32_t solve_n = max_row_end < 128u ? max_row_end : 128u;
            const float avail_h = layout_maxf(0.0f, p.height - c.padding.top - c.padding.bottom - float(solve_n > 1u ? solve_n - 1u : 0u) * c.cross_gap);
            layout_solve_grid_tracks(solve_n, c.grid_row_tracks, c.grid_row_track_count, avail_h, !stretch_y, row_h);
        }
        // Expand auto rows if a multi-row spanning item is taller than its spanned tracks.
        for (uint32_t ci = p.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
            const LayoutNode& ch = le->nodes.data[ci];
            if (ch.cell_row_span <= 1) continue;
            const uint32_t r0 = static_cast<uint32_t>(ch.cell_row);
            const uint32_t r1 = r0 + static_cast<uint32_t>(ch.cell_row_span);
            float spanned = float(ch.cell_row_span - 1) * c.cross_gap;
            for (uint32_t r = r0; r < r1 && r < max_row_end; ++r) spanned += row_h[r];
            const float need = ch.height + ch.cfg.margin.top + ch.cfg.margin.bottom;
            if (need > spanned) {
                const float add = (need - spanned) / float(ch.cell_row_span);
                for (uint32_t r = r0; r < r1 && r < max_row_end; ++r) row_h[r] += add;
            }
        }
        float y = 0.0f;
        for (uint32_t r = 0; r < max_row_end; ++r) {
            row_y[r] = y;
            y += row_h[r] + (r + 1u < max_row_end ? c.cross_gap : 0.0f);
        }
        float max_w = 0.0f;
        for (uint32_t ci = p.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
            LayoutNode& ch = le->nodes.data[ci];
            const uint32_t r0 = static_cast<uint32_t>(ch.cell_row);
            const uint32_t r1 = r0 + static_cast<uint32_t>(ch.cell_row_span);
            float cell_h = float(ch.cell_row_span - 1) * c.cross_gap;
            for (uint32_t r = r0; r < r1 && r < max_row_end; ++r) cell_h += row_h[r];
            ch.cell_y = r0 < max_row_end ? row_y[r0] : 0.0f;
            ch.cell_h = cell_h;
            if (stretch_y && !ch.is_text && (ch.cfg.height.kind == SizingKind::Grow || ch.cfg.align_self == AlignSelf::Stretch)) {
                const float my = ch.cfg.margin.top + ch.cfg.margin.bottom;
                ch.height = layout_clampf(layout_maxf(0.0f, cell_h - my), layout_sizing_min(ch.cfg.height), layout_sizing_max(ch.cfg.height));
            }
            max_w = layout_maxf(max_w, ch.cell_x + ch.cell_w);
        }
        return {max_w, y};
    }

    // Flex-wrap row metrics.
    le->scratch_children.clear();
    for (uint32_t ci = p.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
        le->scratch_children.push(ci);
    }
    const uint32_t n = le->scratch_children.count;
    float y = 0.0f;
    float max_w = 0.0f;
    for (uint32_t start = 0; start < n;) {
        uint32_t end = start + 1u;
        const int32_t r_id = le->nodes.data[le->scratch_children.data[start]].cell_row;
        while (end < n && le->nodes.data[le->scratch_children.data[end]].cell_row == r_id) ++end;
        float row_h = 0.0f, row_w = 0.0f;
        const uint32_t live = end - start;
        for (uint32_t i = start; i < end; ++i) {
            const LayoutNode& ch = le->nodes.data[le->scratch_children.data[i]];
            row_h = layout_maxf(row_h, ch.height + ch.cfg.margin.top + ch.cfg.margin.bottom);
            row_w += ch.width + ch.cfg.margin.left + ch.cfg.margin.right + (i > start ? c.child_gap : 0.0f);
        }
        const float avail_w = layout_maxf(0.0f, p.width - c.padding.left - c.padding.right);
        const float extra = layout_maxf(0.0f, avail_w - row_w);
        float x = 0.0f;
        float gap = c.child_gap;
        if (c.justify != Justify::Start) {
            if (c.justify == Justify::Center) x = extra * 0.5f;
            else if (c.justify == Justify::End) x = extra;
            else if (c.justify == Justify::SpaceBetween && live > 1u) gap += extra / float(live - 1u);
            else if (c.justify == Justify::SpaceAround && live > 0u) { const float s = extra / float(live); x = s * 0.5f; gap += s; }
            else if (c.justify == Justify::SpaceEvenly && live > 0u) { const float s = extra / float(live + 1u); x = s; gap += s; }
        } else {
            x = c.align_x == AlignX::Left ? 0.0f : (c.align_x == AlignX::Center ? extra * 0.5f : extra);
        }
        for (uint32_t i = start; i < end; ++i) {
            LayoutNode& ch = le->nodes.data[le->scratch_children.data[i]];
            if (stretch_y && !ch.is_text && (ch.cfg.height.kind == SizingKind::Grow || ch.cfg.align_self == AlignSelf::Stretch)) {
                const float my = ch.cfg.margin.top + ch.cfg.margin.bottom;
                ch.height = layout_clampf(layout_maxf(0.0f, row_h - my), layout_sizing_min(ch.cfg.height), layout_sizing_max(ch.cfg.height));
            }
            const float outer_w = ch.width + ch.cfg.margin.left + ch.cfg.margin.right;
            ch.cell_x = x;
            ch.cell_w = outer_w;
            ch.cell_y = y;
            ch.cell_h = row_h;
            x += outer_w + gap;
        }
        max_w = layout_maxf(max_w, row_w);
        y += row_h + (end < n ? c.cross_gap : 0.0f);
        start = end;
    }
    return {max_w, y};
}

inline void layout_size_axis_tree(LayoutEngine* le, uint32_t root_idx, bool x_axis) noexcept {
    le->bfs_queue.clear();
    le->bfs_queue.push(root_idx);
    uint32_t head = 0;
    while (head < le->bfs_queue.count) {
        const uint32_t p_idx = le->bfs_queue.data[head++];
        LayoutNode& p = le->nodes.data[p_idx];
        const LayoutNodeConfig& pc = p.cfg;
        if (x_axis) p.sized_x = true;
        else p.sized_y = true;
        if (p.is_text || p.child_count == 0u) continue;

        const float parent_size = x_axis ? p.width : p.height;
        const float parent_pad = x_axis ? (pc.padding.left + pc.padding.right) : (pc.padding.top + pc.padding.bottom);
        const bool along_axis = (x_axis && pc.axis == Axis::Row) || (!x_axis && pc.axis == Axis::Col);
        const bool parent_scrolls = x_axis ? pc.scroll_x : pc.scroll_y;
        const float child_gaps = float(p.child_count > 1u ? p.child_count - 1u : 0u) * pc.child_gap;

        if (layout_is_rows(p)) {
            if (x_axis) {
                layout_size_rows_x(le, p);
            } else {
                const float inner_h = layout_maxf(0.0f, parent_size - parent_pad);
                for (uint32_t ci = p.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
                    LayoutNode& ch = le->nodes.data[ci];
                    if (!ch.is_text && ch.cfg.height.kind == SizingKind::Percent) {
                        ch.height = layout_maxf(ch.min_h, inner_h * ch.cfg.height.value);
                        layout_update_aspect_box(ch);
                    }
                }
                (void)layout_row_metrics(le, p, true);
            }
            for (uint32_t ci = p.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
                le->bfs_queue.push(ci);
            }
            continue;
        }

        // 1. Resolve Percent children and accumulate inner content size.
        float inner_content = 0.0f;
        float fixed_siblings_sum = 0.0f;
        if (along_axis && pc.percent_after_fixed) {
            for (uint32_t ci = p.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
                const LayoutNode& sib = le->nodes.data[ci];
                const Sizing& ss = x_axis ? sib.cfg.width : sib.cfg.height;
                if (!sib.is_text && ss.kind == SizingKind::Fixed) {
                    fixed_siblings_sum += x_axis ? sib.width : sib.height;
                }
            }
        }

        le->scratch_children.clear();
        for (uint32_t ci = p.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
            LayoutNode& ch = le->nodes.data[ci];
            const Sizing& cs = x_axis ? ch.cfg.width : ch.cfg.height;
            const float margin_ax = x_axis ? (ch.cfg.margin.left + ch.cfg.margin.right) : (ch.cfg.margin.top + ch.cfg.margin.bottom);
            float& ch_dim = x_axis ? ch.width : ch.height;

            if (!ch.is_text && cs.kind == SizingKind::Percent) {
                float pct_space = parent_size - parent_pad - (along_axis ? child_gaps : 0.0f);
                if (along_axis && pc.percent_after_fixed) pct_space -= fixed_siblings_sum;
                pct_space = layout_maxf(0.0f, pct_space);
                ch_dim = pct_space * cs.value;
                if (along_axis && pc.percent_after_fixed) {
                    float lo = layout_minf(pc.percent_min_first, pct_space);
                    float hi = layout_maxf(0.0f, pct_space - pc.percent_min_second);
                    if (lo > hi) lo = hi = (lo + hi) * 0.5f;
                    ch_dim = layout_clampf(ch_dim, lo, hi);
                }
                layout_update_aspect_box(ch);
            }

            if (along_axis) inner_content += ch_dim + margin_ax;
            else inner_content = layout_maxf(inner_content, ch_dim + margin_ax);

            const bool resizable = !ch.is_text
                ? (cs.kind != SizingKind::Percent && cs.kind != SizingKind::Fixed)
                : (x_axis && (ch.text_cfg.wrap || ch.text_cfg.ellipsis || ch.text_cfg.max_lines > 0u));
            if (resizable) le->scratch_children.push(ci);
        }

        if (along_axis) {
            inner_content += child_gaps;
            float size_to_distribute = parent_size - parent_pad - inner_content;
            if (size_to_distribute < -kLayoutEpsilon) {
                // Compress children when overflowing (a scrolling axis preserves natural child size unless opt-in shrink).
                if (parent_scrolls) {
                    uint32_t kept = 0;
                    for (uint32_t i = 0; i < le->scratch_children.count; ++i) {
                        const uint32_t ci = le->scratch_children.data[i];
                        const LayoutNode& ch = le->nodes.data[ci];
                        const bool opt_in = x_axis
                            ? ((ch.is_text && ch.text_cfg.ellipsis) || ch.cfg.shrink_width)
                            : ch.cfg.shrink_height;
                        if (opt_in) le->scratch_children.data[kept++] = ci;
                    }
                    le->scratch_children.count = kept;
                }
                // Filter out flex_shrink <= 0 non-text items.
                bool weighted_shrink = false;
                uint32_t kept_s = 0;
                for (uint32_t i = 0; i < le->scratch_children.count; ++i) {
                    const uint32_t ci = le->scratch_children.data[i];
                    const LayoutNode& ch = le->nodes.data[ci];
                    if (!ch.is_text && ch.cfg.flex_shrink <= 0.0f) continue;
                    if (!ch.is_text && std::fabs(ch.cfg.flex_shrink - 1.0f) > 1e-4f) weighted_shrink = true;
                    le->scratch_children.data[kept_s++] = ci;
                }
                le->scratch_children.count = kept_s;

                if (weighted_shrink) {
                    for (uint32_t pass = 0; pass <= le->scratch_children.count && size_to_distribute < -kLayoutEpsilon; ++pass) {
                        float total_w = 0.0f;
                        for (uint32_t i = 0; i < le->scratch_children.count; ++i) {
                            const LayoutNode& ch = le->nodes.data[le->scratch_children.data[i]];
                            const float cur = x_axis ? ch.width : ch.height;
                            const float lo = x_axis ? ch.min_w : ch.min_h;
                            if (cur > lo + kLayoutEpsilon) total_w += ch.is_text ? 1.0f : ch.cfg.flex_shrink;
                        }
                        if (total_w <= 0.0f) break;
                        const float rem = -size_to_distribute;
                        for (uint32_t i = 0; i < le->scratch_children.count; ++i) {
                            LayoutNode& ch = le->nodes.data[le->scratch_children.data[i]];
                            float& cur = x_axis ? ch.width : ch.height;
                            const float lo = x_axis ? ch.min_w : ch.min_h;
                            if (cur <= lo + kLayoutEpsilon) continue;
                            const float wt = ch.is_text ? 1.0f : ch.cfg.flex_shrink;
                            const float delta = layout_minf(cur - lo, rem * wt / total_w);
                            cur -= delta;
                            size_to_distribute += delta;
                        }
                    }
                } else {
                    // Largest-first compression (preserves Clay behavior for uniform shrink).
                    while (size_to_distribute < -kLayoutEpsilon && le->scratch_children.count > 0u) {
                        float largest = 0.0f;
                        float second = 0.0f;
                        float width_to_add = size_to_distribute;
                        for (uint32_t i = 0; i < le->scratch_children.count; ++i) {
                            const LayoutNode& ch = le->nodes.data[le->scratch_children.data[i]];
                            const float cur = x_axis ? ch.width : ch.height;
                            if (std::fabs(cur - largest) < kLayoutEpsilon) continue;
                            if (cur > largest) { second = largest; largest = cur; }
                            else if (cur > second) { second = cur; }
                        }
                        if (second > 0.0f) width_to_add = second - largest;
                        uint32_t match_cnt = 0;
                        for (uint32_t i = 0; i < le->scratch_children.count; ++i) {
                            const LayoutNode& ch = le->nodes.data[le->scratch_children.data[i]];
                            const float cur = x_axis ? ch.width : ch.height;
                            if (std::fabs(cur - largest) < kLayoutEpsilon) ++match_cnt;
                        }
                        if (match_cnt == 0u) break;
                        const float per = layout_maxf(width_to_add, size_to_distribute / float(match_cnt));
                        uint32_t next_cnt = 0;
                        for (uint32_t i = 0; i < le->scratch_children.count; ++i) {
                            const uint32_t ci = le->scratch_children.data[i];
                            LayoutNode& ch = le->nodes.data[ci];
                            float& cur = x_axis ? ch.width : ch.height;
                            const float lo = x_axis ? ch.min_w : ch.min_h;
                            bool keep = true;
                            if (std::fabs(cur - largest) < kLayoutEpsilon) {
                                const float prev = cur;
                                cur += per;
                                if (cur <= lo + kLayoutEpsilon) { cur = lo; keep = false; }
                                size_to_distribute -= (cur - prev);
                            }
                            if (keep) le->scratch_children.data[next_cnt++] = ci;
                        }
                        if (next_cnt == le->scratch_children.count && std::fabs(per) < 1e-5f) break;
                        le->scratch_children.count = next_cnt;
                    }
                }
            } else if (size_to_distribute > kLayoutEpsilon) {
                // Expand Grow children.
                uint32_t grow_cnt = 0;
                bool has_explicit_weights = false;
                float first_wt = -1.0f;
                for (uint32_t i = 0; i < le->scratch_children.count; ++i) {
                    const uint32_t ci = le->scratch_children.data[i];
                    const LayoutNode& ch = le->nodes.data[ci];
                    const Sizing& cs = x_axis ? ch.cfg.width : ch.cfg.height;
                    if (!ch.is_text && cs.kind == SizingKind::Grow) {
                        le->scratch_children.data[grow_cnt++] = ci;
                        if (ch.cfg.flex_grow > 0.0f) {
                            if (first_wt < 0.0f) first_wt = ch.cfg.flex_grow;
                            else if (std::fabs(ch.cfg.flex_grow - first_wt) > 1e-4f) has_explicit_weights = true;
                        }
                    }
                }
                if (first_wt > 0.0f) {
                    // Check if any sibling left flex_grow==0 while another specified != 1.0
                    for (uint32_t i = 0; i < grow_cnt; ++i) {
                        const float wt = le->nodes.data[le->scratch_children.data[i]].cfg.flex_grow;
                        const float eff = wt > 0.0f ? wt : 1.0f;
                        if (std::fabs(eff - first_wt) > 1e-4f) has_explicit_weights = true;
                    }
                }
                le->scratch_children.count = grow_cnt;

                if (has_explicit_weights) {
                    for (uint32_t pass = 0; pass <= grow_cnt && size_to_distribute > kLayoutEpsilon; ++pass) {
                        float total_w = 0.0f;
                        for (uint32_t i = 0; i < grow_cnt; ++i) {
                            const LayoutNode& ch = le->nodes.data[le->scratch_children.data[i]];
                            const Sizing& cs = x_axis ? ch.cfg.width : ch.cfg.height;
                            const float cur = x_axis ? ch.width : ch.height;
                            if (cur < layout_sizing_max(cs) - kLayoutEpsilon) {
                                total_w += ch.cfg.flex_grow > 0.0f ? ch.cfg.flex_grow : 1.0f;
                            }
                        }
                        if (total_w <= 0.0f) break;
                        const float rem = size_to_distribute;
                        for (uint32_t i = 0; i < grow_cnt; ++i) {
                            LayoutNode& ch = le->nodes.data[le->scratch_children.data[i]];
                            const Sizing& cs = x_axis ? ch.cfg.width : ch.cfg.height;
                            float& cur = x_axis ? ch.width : ch.height;
                            const float hi = layout_sizing_max(cs);
                            if (cur >= hi - kLayoutEpsilon) continue;
                            const float wt = ch.cfg.flex_grow > 0.0f ? ch.cfg.flex_grow : 1.0f;
                            const float delta = layout_minf(hi - cur, rem * wt / total_w);
                            cur += delta;
                            size_to_distribute -= delta;
                        }
                    }
                } else {
                    // Water-filling equalization (matches Clay Grow distribution).
                    while (size_to_distribute > kLayoutEpsilon && le->scratch_children.count > 0u) {
                        float smallest = kLayoutMaxFloat;
                        float second = kLayoutMaxFloat;
                        for (uint32_t i = 0; i < le->scratch_children.count; ++i) {
                            const LayoutNode& ch = le->nodes.data[le->scratch_children.data[i]];
                            const float cur = x_axis ? ch.width : ch.height;
                            if (std::fabs(cur - smallest) < kLayoutEpsilon) continue;
                            if (cur < smallest) { second = smallest; smallest = cur; }
                            else if (cur < second) { second = cur; }
                        }
                        uint32_t match_cnt = 0;
                        for (uint32_t i = 0; i < le->scratch_children.count; ++i) {
                            const LayoutNode& ch = le->nodes.data[le->scratch_children.data[i]];
                            const float cur = x_axis ? ch.width : ch.height;
                            if (std::fabs(cur - smallest) < kLayoutEpsilon) ++match_cnt;
                        }
                        if (match_cnt == 0u) break;
                        const float cap_add = second < kLayoutMaxFloat * 0.5f ? (second - smallest) : size_to_distribute;
                        const float per = layout_minf(cap_add, size_to_distribute / float(match_cnt));
                        uint32_t next_cnt = 0;
                        for (uint32_t i = 0; i < le->scratch_children.count; ++i) {
                            const uint32_t ci = le->scratch_children.data[i];
                            LayoutNode& ch = le->nodes.data[ci];
                            const Sizing& cs = x_axis ? ch.cfg.width : ch.cfg.height;
                            float& cur = x_axis ? ch.width : ch.height;
                            const float hi = layout_sizing_max(cs);
                            bool keep = true;
                            if (std::fabs(cur - smallest) < kLayoutEpsilon) {
                                const float prev = cur;
                                cur += per;
                                if (cur >= hi - kLayoutEpsilon) { cur = hi; keep = false; }
                                size_to_distribute -= (cur - prev);
                            }
                            if (keep) le->scratch_children.data[next_cnt++] = ci;
                        }
                        if (next_cnt == le->scratch_children.count && per < 1e-5f) break;
                        le->scratch_children.count = next_cnt;
                    }
                }
            }
        } else {
            // Cross-axis sizing.
            for (uint32_t ci = p.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
                LayoutNode& ch = le->nodes.data[ci];
                const Sizing& cs = x_axis ? ch.cfg.width : ch.cfg.height;
                if (!ch.is_text && (cs.kind == SizingKind::Fixed || cs.kind == SizingKind::Percent)) continue;
                const bool opt_in_shrink = x_axis
                    ? ((ch.is_text && ch.text_cfg.ellipsis) || ch.cfg.shrink_width)
                    : ch.cfg.shrink_height;
                const float margin_ax = x_axis ? (ch.cfg.margin.left + ch.cfg.margin.right) : (ch.cfg.margin.top + ch.cfg.margin.bottom);
                float max_sz = layout_maxf(0.0f, parent_size - parent_pad - margin_ax);
                if (parent_scrolls && !opt_in_shrink) {
                    max_sz = layout_maxf(max_sz, inner_content - margin_ax);
                }
                float& cur = x_axis ? ch.width : ch.height;
                const float lo = x_axis ? ch.min_w : ch.min_h;
                if (!ch.is_text && (cs.kind == SizingKind::Grow || ch.cfg.align_self == AlignSelf::Stretch)) {
                    cur = layout_clampf(max_sz, layout_sizing_min(cs), layout_sizing_max(cs));
                } else if (cur > max_sz && (ch.is_text || ch.cfg.flex_shrink > 0.0f)) {
                    const bool can_compress = !ch.is_text || (x_axis && (ch.text_cfg.wrap || ch.text_cfg.ellipsis || ch.text_cfg.max_lines > 0u));
                    if (can_compress) cur = layout_maxf(lo, max_sz);
                }
            }
        }

        for (uint32_t ci = p.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
            le->bfs_queue.push(ci);
        }
    }
}

// ============================================================================
// Pass 3: Text Wrapping
// ============================================================================

inline void layout_wrap_all_text(LayoutEngine* le) noexcept {
    const float default_space_fallback = 4.0f;
    for (uint32_t ti = 0; ti < le->text_nodes.count; ++ti) {
        LayoutNode& n = le->nodes.data[le->text_nodes.data[ti]];
        const LayoutTextConfig& tc = n.text_cfg;
        n.wrapped_start = le->wrapped_lines.count;
        n.wrapped_count = 0;
        const float line_h = tc.line_height > 0.0f ? tc.line_height : n.text_preferred.y;
        const bool single_line = !tc.wrap;
        const uint32_t line_limit = single_line ? 1u : tc.max_lines;

        const LayoutTextCacheEntry* ce = (n.text_cache_idx < le->text_cache_cap && le->text_cache[n.text_cache_idx].occupied)
            ? &le->text_cache[n.text_cache_idx] : nullptr;

        if (single_line) {
            uint32_t len = 0;
            while (len < n.text.length && n.text.chars[len] != '\n') ++len;
            const float mw = ce ? ce->unwrapped.x : n.text_preferred.x;
            const bool trunc = tc.ellipsis && (mw > n.width + kLayoutEpsilon || len < n.text.length);
            le->wrapped_lines.push(LayoutWrappedLine{
                .line = {n.text.chars, len, n.text.is_static},
                .width = layout_minf(mw, n.width),
                .height = line_h,
                .max_width = n.width,
                .truncated = trunc,
            });
            n.wrapped_count = 1u;
            n.height = line_h;
            continue;
        }

        if (!ce || (!ce->contains_newlines && n.text_preferred.x <= n.width + kLayoutEpsilon)) {
            le->wrapped_lines.push(LayoutWrappedLine{
                .line = n.text,
                .width = n.width,
                .height = line_h,
                .max_width = n.width,
                .truncated = false,
            });
            n.wrapped_count = 1u;
            n.height = line_h;
            continue;
        }

        const float space_w = le->measure_text_fn
            ? le->measure_text_fn(Str{" ", 1u, true}, tc, le->measure_text_user).x
            : default_space_fallback;

        float line_w = 0.0f;
        uint32_t line_start = 0;
        uint32_t line_len = 0;
        const uint32_t w_end = ce->word_start + ce->word_count;

        for (uint32_t wi = ce->word_start; wi < w_end;) {
            if (line_limit > 0u && n.wrapped_count >= line_limit) {
                LayoutWrappedLine& last = le->wrapped_lines.data[n.wrapped_start + line_limit - 1u];
                const LayoutMeasuredWord& rem = le->cached_words.data[wi];
                const bool has_more = rem.length > 0u || (wi + 1u) < w_end;
                if (tc.ellipsis && has_more) last.truncated = true;
                line_len = 0;
                break;
            }
            const LayoutMeasuredWord& w = le->cached_words.data[wi];
            if (line_len == 0u && w.length > 0u && line_w + w.width > n.width) {
                le->wrapped_lines.push(LayoutWrappedLine{
                    .line = {n.text.chars + w.start, w.length, false},
                    .width = w.width,
                    .height = line_h,
                    .max_width = n.width,
                    .truncated = false,
                });
                ++n.wrapped_count;
                line_start = w.start + w.length;
                line_w = 0.0f;
                ++wi;
            } else if (w.length == 0u || line_w + w.width > n.width) {
                const bool trailing_space = (line_len > 0u) && (n.text.chars[line_start + line_len - 1u] == ' ');
                float tsw = space_w;
                if (trailing_space && tc.is_rich && le->measure_text_fn) {
                    tsw = le->measure_text_fn(Str{n.text.chars + line_start + line_len - 1u, 1u, false}, tc, le->measure_text_user).x;
                }
                le->wrapped_lines.push(LayoutWrappedLine{
                    .line = {n.text.chars + line_start, line_len - (trailing_space ? 1u : 0u), false},
                    .width = layout_maxf(0.0f, line_w - (trailing_space ? tsw : 0.0f)),
                    .height = line_h,
                    .max_width = n.width,
                    .truncated = false,
                });
                ++n.wrapped_count;
                if (line_len == 0u || w.length == 0u) {
                    line_start = w.start + (w.length == 0u ? 1u : w.length);
                    ++wi;
                } else {
                    line_start = w.start;
                }
                line_len = 0;
                line_w = 0.0f;
            } else {
                if (line_len == 0u) line_start = w.start;
                line_w += w.width + tc.letter_spacing;
                line_len += w.length;
                ++wi;
            }
        }

        if (line_len > 0u) {
            le->wrapped_lines.push(LayoutWrappedLine{
                .line = {n.text.chars + line_start, line_len, false},
                .width = layout_maxf(0.0f, line_w - tc.letter_spacing),
                .height = line_h,
                .max_width = n.width,
                .truncated = false,
            });
            ++n.wrapped_count;
        }
        if (line_limit > 0u && n.wrapped_count > line_limit) {
            le->wrapped_lines.count -= (n.wrapped_count - line_limit);
            n.wrapped_count = line_limit;
            if (tc.ellipsis) le->wrapped_lines.data[n.wrapped_start + line_limit - 1u].truncated = true;
        }
        for (uint32_t li = 0; li < n.wrapped_count; ++li) {
            LayoutWrappedLine& wl = le->wrapped_lines.data[n.wrapped_start + li];
            wl.max_width = n.width;
            if (tc.ellipsis && wl.width > wl.max_width + kLayoutEpsilon) wl.truncated = true;
            if (wl.truncated) wl.width = wl.max_width;
        }
        n.height = line_h * float(n.wrapped_count > 0u ? n.wrapped_count : 1u);
        n.min_h = n.height;
    }
}

// ============================================================================
// Pass 4: Bottom-Up Y Height Propagation After Text Wrapping
// ============================================================================

inline void layout_propagate_y_up(LayoutEngine* le, uint32_t root_idx) noexcept {
    // Iterative post-order traversal using bfs_queue + post_order.
    le->bfs_queue.clear();
    le->post_order.clear();
    le->bfs_queue.push(root_idx);
    while (le->bfs_queue.count > 0u) {
        const uint32_t idx = le->bfs_queue.data[--le->bfs_queue.count];
        le->post_order.push(idx);
        const LayoutNode& n = le->nodes.data[idx];
        for (uint32_t ci = n.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
            le->bfs_queue.push(ci);
        }
    }
    for (uint32_t k = le->post_order.count; k > 0u; --k) {
        const uint32_t idx = le->post_order.data[k - 1u];
        LayoutNode& n = le->nodes.data[idx];
        if (n.is_text) continue;
        const LayoutNodeConfig& c = n.cfg;
        if (c.height.kind == SizingKind::Fixed || c.height.kind == SizingKind::Percent) continue;
        const float pad_y = c.padding.top + c.padding.bottom;
        const float lo = layout_sizing_min(c.height);
        const float hi = layout_sizing_max(c.height);

        if (layout_is_rows(n)) {
            const Vec2 content = layout_row_metrics(le, n, false);
            n.height = layout_clampf(content.y + pad_y, lo, hi);
            if (!c.clip_y && !c.shrink_height) n.min_h = n.height;
        } else if (c.axis == Axis::Row) {
            float max_h = pad_y;
            float max_mh = pad_y;
            for (uint32_t ci = n.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
                const LayoutNode& ch = le->nodes.data[ci];
                const float my = ch.cfg.margin.top + ch.cfg.margin.bottom;
                max_h = layout_maxf(max_h, ch.height + my + pad_y);
                max_mh = layout_maxf(max_mh, ch.min_h + my + pad_y);
            }
            n.height = layout_clampf(max_h, lo, hi);
            if (!c.clip_y && !c.shrink_height) n.min_h = layout_clampf(max_mh, lo, hi);
        } else {
            float sum_h = pad_y;
            float sum_mh = pad_y;
            for (uint32_t ci = n.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
                const LayoutNode& ch = le->nodes.data[ci];
                const float my = ch.cfg.margin.top + ch.cfg.margin.bottom;
                sum_h += ch.height + my;
                sum_mh += ch.min_h + my;
            }
            const float gaps = float(n.child_count > 1u ? n.child_count - 1u : 0u) * c.child_gap;
            sum_h += gaps;
            sum_mh += gaps;
            n.height = layout_clampf(sum_h, lo, hi);
            if (!c.clip_y && !c.shrink_height) n.min_h = layout_clampf(sum_mh, lo, hi);
        }
        if (c.aspect_ratio > 0.0f) {
            n.height = n.width / c.aspect_ratio;
        }
    }
}

// ============================================================================
// Pass 6: 2-Phase Positioning (Zero 1-Frame Lag + Flip-On-Overflow)
// ============================================================================

inline Vec2 layout_anchor_point(Rect r, Anchor a) noexcept {
    float x = r.x;
    float y = r.y;
    switch (a) {
        case Anchor::TopLeft:      x = r.x;              y = r.y; break;
        case Anchor::TopCenter:    x = r.x + r.w * 0.5f; y = r.y; break;
        case Anchor::TopRight:     x = r.x + r.w;        y = r.y; break;
        case Anchor::CenterLeft:   x = r.x;              y = r.y + r.h * 0.5f; break;
        case Anchor::Center:       x = r.x + r.w * 0.5f; y = r.y + r.h * 0.5f; break;
        case Anchor::CenterRight:  x = r.x + r.w;        y = r.y + r.h * 0.5f; break;
        case Anchor::BottomLeft:   x = r.x;              y = r.y + r.h; break;
        case Anchor::BottomCenter: x = r.x + r.w * 0.5f; y = r.y + r.h; break;
        case Anchor::BottomRight:  x = r.x + r.w;        y = r.y + r.h; break;
    }
    return {x, y};
}

inline Anchor layout_flip_anchor_y(Anchor a) noexcept {
    switch (a) {
        case Anchor::TopLeft:      return Anchor::BottomLeft;
        case Anchor::TopCenter:    return Anchor::BottomCenter;
        case Anchor::TopRight:     return Anchor::BottomRight;
        case Anchor::BottomLeft:   return Anchor::TopLeft;
        case Anchor::BottomCenter: return Anchor::TopCenter;
        case Anchor::BottomRight:  return Anchor::TopRight;
        default: return a;
    }
}

inline Anchor layout_flip_anchor_x(Anchor a) noexcept {
    switch (a) {
        case Anchor::TopLeft:     return Anchor::TopRight;
        case Anchor::CenterLeft:  return Anchor::CenterRight;
        case Anchor::BottomLeft:  return Anchor::BottomRight;
        case Anchor::TopRight:    return Anchor::TopLeft;
        case Anchor::CenterRight: return Anchor::CenterLeft;
        case Anchor::BottomRight: return Anchor::BottomLeft;
        default: return a;
    }
}

inline void layout_position_subtree(LayoutEngine* le, uint32_t root_idx, Vec2 root_pos) noexcept {
    le->nodes.data[root_idx].bounds = {root_pos.x, root_pos.y, le->nodes.data[root_idx].width, le->nodes.data[root_idx].height};
    le->nodes.data[root_idx].positioned = true;
    le->bfs_queue.clear();
    le->bfs_queue.push(root_idx);
    uint32_t head = 0;

    while (head < le->bfs_queue.count) {
        const uint32_t idx = le->bfs_queue.data[head++];
        LayoutNode& n = le->nodes.data[idx];
        const LayoutNodeConfig& c = n.cfg;
        const bool has_scroll = c.clip_x || c.clip_y;

        if (!n.is_text && n.child_count > 0u) {
            if (layout_is_rows(n)) {
                const Vec2 content = layout_row_metrics(le, n, false);
                n.content_size = {content.x + c.padding.left + c.padding.right, content.y + c.padding.top + c.padding.bottom};
                for (uint32_t ci = n.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
                    LayoutNode& ch = le->nodes.data[ci];
                    const float mx = ch.cfg.margin.left + ch.cfg.margin.right;
                    const float my = ch.cfg.margin.top + ch.cfg.margin.bottom;
                    const float dx = layout_maxf(0.0f, ch.cell_w - ch.width - mx);
                    const float dy = layout_maxf(0.0f, ch.cell_h - ch.height - my);
                    const float ax = c.align_x == AlignX::Left ? 0.0f : (c.align_x == AlignX::Center ? dx * 0.5f : dx);
                    float ay = c.align_y == AlignY::Top ? 0.0f : (c.align_y == AlignY::Center ? dy * 0.5f : dy);
                    if (!ch.is_text && ch.cfg.align_self != AlignSelf::Auto) {
                        if (ch.cfg.align_self == AlignSelf::Start || ch.cfg.align_self == AlignSelf::Stretch) ay = 0.0f;
                        else if (ch.cfg.align_self == AlignSelf::Center) ay = dy * 0.5f;
                        else if (ch.cfg.align_self == AlignSelf::End) ay = dy;
                    }
                    ch.bounds = {
                        n.bounds.x + c.padding.left + ch.cell_x + c.child_offset.x + ch.cfg.margin.left + ax,
                        n.bounds.y + c.padding.top + ch.cell_y + c.child_offset.y + ch.cfg.margin.top + ay,
                        ch.width,
                        ch.height,
                    };
                    ch.positioned = true;
                    le->bfs_queue.push(ci);
                }
            } else {
                const bool is_row = (c.axis == Axis::Row);
                float content_w = 0.0f, content_h = 0.0f;
                for (uint32_t ci = n.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
                    const LayoutNode& ch = le->nodes.data[ci];
                    const float ow = ch.width + ch.cfg.margin.left + ch.cfg.margin.right;
                    const float oh = ch.height + ch.cfg.margin.top + ch.cfg.margin.bottom;
                    if (is_row) { content_w += ow; content_h = layout_maxf(content_h, oh); }
                    else        { content_w = layout_maxf(content_w, ow); content_h += oh; }
                }
                const float total_gaps = float(n.child_count > 1u ? n.child_count - 1u : 0u) * c.child_gap;
                if (is_row) content_w += total_gaps;
                else content_h += total_gaps;
                n.content_size = {content_w + c.padding.left + c.padding.right, content_h + c.padding.top + c.padding.bottom};

                const float inner_w = layout_maxf(0.0f, n.width - c.padding.left - c.padding.right);
                const float inner_h = layout_maxf(0.0f, n.height - c.padding.top - c.padding.bottom);
                const float main_extra = layout_maxf(0.0f, (is_row ? inner_w : inner_h) - (is_row ? content_w : content_h));

                float main_offset = 0.0f;
                float step_gap = c.child_gap;
                if (c.justify != Justify::Start) {
                    if (c.justify == Justify::Center) main_offset = main_extra * 0.5f;
                    else if (c.justify == Justify::End) main_offset = main_extra;
                    else if (c.justify == Justify::SpaceBetween && n.child_count > 1u) step_gap += main_extra / float(n.child_count - 1u);
                    else if (c.justify == Justify::SpaceAround && n.child_count > 0u) {
                        const float s = main_extra / float(n.child_count);
                        main_offset = s * 0.5f;
                        step_gap += s;
                    } else if (c.justify == Justify::SpaceEvenly && n.child_count > 0u) {
                        const float s = main_extra / float(n.child_count + 1u);
                        main_offset = s;
                        step_gap += s;
                    }
                } else {
                    if (is_row) {
                        if (c.align_x == AlignX::Center) main_offset = main_extra * 0.5f;
                        else if (c.align_x == AlignX::Right) main_offset = main_extra;
                    } else {
                        if (c.align_y == AlignY::Center) main_offset = main_extra * 0.5f;
                        else if (c.align_y == AlignY::Bottom) main_offset = main_extra;
                    }
                }

                float cursor = main_offset;
                for (uint32_t ci = n.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
                    LayoutNode& ch = le->nodes.data[ci];
                    const float ow = ch.width + ch.cfg.margin.left + ch.cfg.margin.right;
                    const float oh = ch.height + ch.cfg.margin.top + ch.cfg.margin.bottom;
                    if (is_row) {
                        const float cross_extra = layout_maxf(0.0f, inner_h - oh);
                        float cy = c.align_y == AlignY::Top ? 0.0f : (c.align_y == AlignY::Center ? cross_extra * 0.5f : cross_extra);
                        if (!ch.is_text && ch.cfg.align_self != AlignSelf::Auto) {
                            if (ch.cfg.align_self == AlignSelf::Start || ch.cfg.align_self == AlignSelf::Stretch) cy = 0.0f;
                            else if (ch.cfg.align_self == AlignSelf::Center) cy = cross_extra * 0.5f;
                            else if (ch.cfg.align_self == AlignSelf::End) cy = cross_extra;
                        }
                        ch.bounds = {
                            n.bounds.x + c.padding.left + cursor + c.child_offset.x + ch.cfg.margin.left,
                            n.bounds.y + c.padding.top + cy + c.child_offset.y + ch.cfg.margin.top,
                            ch.width,
                            ch.height,
                        };
                        cursor += ow + step_gap;
                    } else {
                        const float cross_extra = layout_maxf(0.0f, inner_w - ow);
                        float cx = c.align_x == AlignX::Left ? 0.0f : (c.align_x == AlignX::Center ? cross_extra * 0.5f : cross_extra);
                        if (!ch.is_text && ch.cfg.align_self != AlignSelf::Auto) {
                            if (ch.cfg.align_self == AlignSelf::Start || ch.cfg.align_self == AlignSelf::Stretch) cx = 0.0f;
                            else if (ch.cfg.align_self == AlignSelf::Center) cx = cross_extra * 0.5f;
                            else if (ch.cfg.align_self == AlignSelf::End) cx = cross_extra;
                        }
                        ch.bounds = {
                            n.bounds.x + c.padding.left + cx + c.child_offset.x + ch.cfg.margin.left,
                            n.bounds.y + c.padding.top + cursor + c.child_offset.y + ch.cfg.margin.top,
                            ch.width,
                            ch.height,
                        };
                        cursor += oh + step_gap;
                    }
                    ch.positioned = true;
                    le->bfs_queue.push(ci);
                }
            }
        } else {
            n.content_size = {n.width, n.height};
        }

        if (LayoutBoundsEntry* entry = layout_bounds_map_find_mut(le->cur_map, le->cur_map_cap, n.id)) {
            entry->bounds = n.bounds;
            entry->content_size = n.content_size;
            entry->has_scroll = has_scroll;
        }
    }
}

inline Rect layout_resolve_floating_target_rect(LayoutEngine* le, const LayoutRoot& r) noexcept {
    const LayoutNode& n = le->nodes.data[r.node_idx];
    if (n.cfg.attach_to == AttachTo::Root) {
        return {0.0f, 0.0f, le->viewport.x, le->viewport.y};
    }
    if (n.cfg.attach_to == AttachTo::Parent && r.parent_idx < le->nodes.count) {
        return le->nodes.data[r.parent_idx].bounds;
    }
    if (n.cfg.attach_to == AttachTo::Element && n.cfg.attach_element_id != 0u) {
        if (const LayoutBoundsEntry* e = layout_bounds_map_find(le->cur_map, le->cur_map_cap, n.cfg.attach_element_id)) {
            if (e->node_idx < le->nodes.count && le->nodes.data[e->node_idx].positioned) {
                return le->nodes.data[e->node_idx].bounds;
            }
        }
        if (const LayoutBoundsEntry* pe = layout_bounds_map_find(le->prev_map, le->prev_map_cap, n.cfg.attach_element_id)) {
            return pe->bounds;
        }
    }
    return {0.0f, 0.0f, le->viewport.x, le->viewport.y};
}

inline Vec2 layout_resolve_floating_target_size(LayoutEngine* le, const LayoutRoot& r) noexcept {
    const LayoutNode& n = le->nodes.data[r.node_idx];
    if (n.cfg.attach_to == AttachTo::Root) return le->viewport;
    if (n.cfg.attach_to == AttachTo::Parent && r.parent_idx < le->nodes.count) {
        return {le->nodes.data[r.parent_idx].width, le->nodes.data[r.parent_idx].height};
    }
    if (n.cfg.attach_to == AttachTo::Element && n.cfg.attach_element_id != 0u) {
        if (const LayoutBoundsEntry* e = layout_bounds_map_find(le->cur_map, le->cur_map_cap, n.cfg.attach_element_id)) {
            if (e->node_idx < le->nodes.count) {
                return {le->nodes.data[e->node_idx].width, le->nodes.data[e->node_idx].height};
            }
        }
    }
    return le->viewport;
}

// Stable O(F log F) iterative bottom-up merge sort on floating roots by (z_index, order).
inline void layout_sort_roots(LayoutEngine* le) noexcept {
    const uint32_t n = le->roots.count;
    if (n <= 2u) return;
    if (!le->root_scratch.reserve(n)) return;
    LayoutRoot* src = le->roots.data;
    LayoutRoot* dst = le->root_scratch.data;
    // Keep root 0 (main tree) in place at index 0; sort roots [1..n) or sort all by z_index cleanly.
    // Note: roots with z_index < 0 can sort behind root 0 (z_index == 0), so sort all [0..n)!
    bool swapped = false;
    for (uint32_t width = 1u; width < n; width *= 2u) {
        for (uint32_t left = 0u; left < n; left += 2u * width) {
            const uint32_t mid = layout_minf(float(left + width), float(n)) < float(n) ? (left + width) : n;
            const uint32_t right = (left + 2u * width < n) ? (left + 2u * width) : n;
            uint32_t i = left, j = mid, k = left;
            while (i < mid && j < right) {
                if (src[i].z_index <= src[j].z_index) dst[k++] = src[i++];
                else dst[k++] = src[j++];
            }
            while (i < mid) dst[k++] = src[i++];
            while (j < right) dst[k++] = src[j++];
        }
        LayoutRoot* tmp = src;
        src = dst;
        dst = tmp;
        swapped = !swapped;
    }
    if (swapped) {
        std::memcpy(le->roots.data, le->root_scratch.data, size_t(n) * sizeof(LayoutRoot));
    }
}

// ============================================================================
// Pass 7: Command Emission with Per-Layer Clip Culling
// ============================================================================

inline bool layout_rects_overlap(Rect a, Rect b) noexcept {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

inline Rect layout_rect_intersect(Rect a, Rect b) noexcept {
    const float x0 = layout_maxf(a.x, b.x);
    const float y0 = layout_maxf(a.y, b.y);
    const float x1 = layout_minf(a.x + a.w, b.x + b.w);
    const float y1 = layout_minf(a.y + a.h, b.y + b.h);
    return {x0, y0, x1 > x0 ? (x1 - x0) : 0.0f, y1 > y0 ? (y1 - y0) : 0.0f};
}

inline void layout_emit_subtree_cmds(
    LayoutEngine* le,
    uint32_t idx,
    int16_t z_index,
    Rect active_clip,
    bool clipped_parent) noexcept
{
    const LayoutNode& n = le->nodes.data[idx];
    const LayoutNodeConfig& c = n.cfg;

    // Cached subtree hit replays in one single command without walking descendants.
    if (c.cached_subtree_id != 0u && c.cached_subtree_hit) {
        le->cmds.push(LayoutCmd{
            .kind = LayoutCmdKind::SubtreeReplay,
            .id = c.cached_subtree_id,
            .node_idx = idx,
            .z_index = z_index,
            .bounds = n.bounds,
            .user_data = c.user_data,
        });
        return;
    }

    // Per-layer clip culling: skip leaf/clipped-subtree render commands completely outside active clip,
    // except named scroll/text-selection/cached-capture nodes whose metadata must still run.
    const bool cullable = le->culling_enabled && clipped_parent &&
        !c.clip_x && !c.clip_y && c.cached_subtree_id == 0u &&
        (n.is_text ? (n.text_cfg.user_data == nullptr) : (n.child_count == 0u && !c.emit_custom && !c.emit_border));
    if (cullable && !layout_rects_overlap(n.bounds, active_clip)) {
        return;
    }

    if (c.cached_subtree_id != 0u) {
        le->cmds.push(LayoutCmd{
            .kind = LayoutCmdKind::SubtreeBegin,
            .id = c.cached_subtree_id,
            .node_idx = idx,
            .z_index = z_index,
            .bounds = n.bounds,
            .user_data = c.user_data,
        });
    }

    if (n.is_text) {
        for (uint32_t li = 0; li < n.wrapped_count; ++li) {
            const LayoutWrappedLine& wl = le->wrapped_lines.data[n.wrapped_start + li];
            const float dx = n.text_cfg.align_x == AlignX::Center
                ? 0.5f * layout_maxf(0.0f, n.bounds.w - wl.width)
                : (n.text_cfg.align_x == AlignX::Right ? layout_maxf(0.0f, n.bounds.w - wl.width) : 0.0f);
            const Rect line_box = {
                n.bounds.x + dx,
                n.bounds.y + float(li) * wl.height,
                wl.width,
                wl.height,
            };
            if (le->culling_enabled && clipped_parent && n.text_cfg.user_data == nullptr && !layout_rects_overlap(line_box, active_clip)) {
                continue;
            }
            le->cmds.push(LayoutCmd{
                .kind = LayoutCmdKind::Text,
                .id = layout_auto_id(n.id, li),
                .node_idx = idx,
                .z_index = z_index,
                .bounds = line_box,
                .user_data = n.text_cfg.user_data,
                .text = wl.line,
                .text_cfg = n.text_cfg,
                .text_max_width = wl.max_width,
                .text_truncated = wl.truncated,
            });
        }
        return;
    }

    if (c.emit_custom) {
        le->cmds.push(LayoutCmd{
            .kind = LayoutCmdKind::Custom,
            .id = n.id,
            .node_idx = idx,
            .z_index = z_index,
            .bounds = n.bounds,
            .user_data = c.user_data,
            .custom_data = c.custom_data,
        });
    }
    if (c.emit_image) {
        le->cmds.push(LayoutCmd{
            .kind = LayoutCmdKind::Image,
            .id = n.id,
            .node_idx = idx,
            .z_index = z_index,
            .bounds = n.bounds,
            .user_data = c.user_data,
            .custom_data = c.image_data,
        });
    }

    const bool has_scissor = c.clip_x || c.clip_y;
    Rect child_clip = active_clip;
    if (has_scissor) {
        le->cmds.push(LayoutCmd{
            .kind = LayoutCmdKind::ScissorStart,
            .id = n.id,
            .node_idx = idx,
            .z_index = z_index,
            .bounds = n.bounds,
            .user_data = c.user_data,
        });
        child_clip = layout_rect_intersect(active_clip, n.bounds);
    }

    for (uint32_t ci = n.first_child; ci != kLayoutInvalidNode; ci = le->nodes.data[ci].next_sibling) {
        layout_emit_subtree_cmds(le, ci, z_index, child_clip, clipped_parent || has_scissor);
    }

    if (has_scissor) {
        le->cmds.push(LayoutCmd{
            .kind = LayoutCmdKind::ScissorEnd,
            .id = n.id,
            .node_idx = idx,
            .z_index = z_index,
            .bounds = n.bounds,
            .user_data = c.user_data,
        });
    }

    if (c.emit_border) {
        le->cmds.push(LayoutCmd{
            .kind = LayoutCmdKind::Border,
            .id = n.id,
            .node_idx = idx,
            .z_index = z_index,
            .bounds = n.bounds,
            .user_data = c.user_data,
        });
    }

    if (c.cached_subtree_id != 0u) {
        le->cmds.push(LayoutCmd{
            .kind = LayoutCmdKind::SubtreeEnd,
            .id = c.cached_subtree_id,
            .node_idx = idx,
            .z_index = z_index,
            .bounds = n.bounds,
            .user_data = c.user_data,
        });
    }
}

inline std::span<const LayoutCmd> layout_end(LayoutEngine* le) noexcept {
    if (!le || le->nodes.count == 0u) return {};
    while (le->open_stack.count > 0u) layout_close(le);

    // Pass 2: Top-down X sizing (root 0 first, then floating roots so AttachTo::Element Percent resolves on Frame 0!).
    layout_size_axis_tree(le, 0u, true);
    for (uint32_t ri = 1u; ri < le->roots.count; ++ri) {
        const LayoutRoot& r = le->roots.data[ri];
        LayoutNode& rn = le->nodes.data[r.node_idx];
        const Vec2 target_sz = layout_resolve_floating_target_size(le, r);
        if (rn.cfg.width.kind == SizingKind::Percent) {
            rn.width = target_sz.x * rn.cfg.width.value;
        } else if (rn.cfg.width.kind == SizingKind::Grow) {
            rn.width = layout_clampf(target_sz.x, layout_sizing_min(rn.cfg.width), layout_sizing_max(rn.cfg.width));
        }
        layout_size_axis_tree(le, r.node_idx, true);
    }

    // Pass 3: Wrap all text elements with their resolved widths.
    layout_wrap_all_text(le);

    // Pass 4: Aspect ratio height + bottom-up Y propagation.
    for (uint32_t i = 0; i < le->aspect_nodes.count; ++i) {
        LayoutNode& an = le->nodes.data[le->aspect_nodes.data[i]];
        if (an.cfg.height.kind == SizingKind::Fixed) continue;
        an.height = an.width / an.cfg.aspect_ratio;
        an.cfg.height.max = an.height;
    }
    for (uint32_t ri = 0; ri < le->roots.count; ++ri) {
        layout_propagate_y_up(le, le->roots.data[ri].node_idx);
    }

    // Pass 5: Top-down Y sizing.
    layout_size_axis_tree(le, 0u, false);
    for (uint32_t ri = 1u; ri < le->roots.count; ++ri) {
        const LayoutRoot& r = le->roots.data[ri];
        LayoutNode& rn = le->nodes.data[r.node_idx];
        const Vec2 target_sz = layout_resolve_floating_target_size(le, r);
        if (rn.cfg.height.kind == SizingKind::Percent) {
            rn.height = target_sz.y * rn.cfg.height.value;
        } else if (rn.cfg.height.kind == SizingKind::Grow) {
            rn.height = layout_clampf(target_sz.y, layout_sizing_min(rn.cfg.height), layout_sizing_max(rn.cfg.height));
        }
        layout_size_axis_tree(le, r.node_idx, false);
    }
    for (uint32_t i = 0; i < le->aspect_nodes.count; ++i) {
        LayoutNode& an = le->nodes.data[le->aspect_nodes.data[i]];
        if (an.cfg.width.kind == SizingKind::Fit) {
            an.width = an.height * an.cfg.aspect_ratio;
        }
    }

    // Pass 6a: Position main tree (root 0) first -> ALL flow nodes have exact current-frame bounds!
    layout_position_subtree(le, 0u, {0.0f, 0.0f});

    // Pass 6b: Position floating roots in declaration order (resolving AttachTo::Parent/Element on Frame 0!).
    for (uint32_t ri = 1u; ri < le->roots.count; ++ri) {
        const LayoutRoot& r = le->roots.data[ri];
        LayoutNode& rn = le->nodes.data[r.node_idx];
        const Rect target = layout_resolve_floating_target_rect(le, r);
        Anchor ap = rn.cfg.anchor_parent;
        Anchor ae = rn.cfg.anchor_element;
        const auto compute_pos = [&](Anchor p_anc, Anchor e_anc, Vec2 off) noexcept -> Vec2 {
            const Vec2 tp = layout_anchor_point(target, p_anc);
            const Vec2 ep = layout_anchor_point({0.0f, 0.0f, rn.width, rn.height}, e_anc);
            return {tp.x - ep.x + off.x, tp.y - ep.y + off.y};
        };
        Vec2 pos = compute_pos(ap, ae, rn.cfg.floating_offset);
        if (rn.cfg.flip_on_overflow && rn.cfg.attach_to != AttachTo::Root) {
            if ((pos.y + rn.height > le->viewport.y && (ap == Anchor::BottomLeft || ap == Anchor::BottomCenter || ap == Anchor::BottomRight)) ||
                (pos.y < 0.0f && (ap == Anchor::TopLeft || ap == Anchor::TopCenter || ap == Anchor::TopRight))) {
                const Anchor fap = layout_flip_anchor_y(ap);
                const Anchor fae = layout_flip_anchor_y(ae);
                const Vec2 fpos = compute_pos(fap, fae, {rn.cfg.floating_offset.x, -rn.cfg.floating_offset.y});
                if (fpos.y >= 0.0f && fpos.y + rn.height <= le->viewport.y) {
                    ap = fap;
                    ae = fae;
                    pos.y = fpos.y;
                }
            }
            if ((pos.x + rn.width > le->viewport.x && (ap == Anchor::TopRight || ap == Anchor::CenterRight || ap == Anchor::BottomRight)) ||
                (pos.x < 0.0f && (ap == Anchor::TopLeft || ap == Anchor::CenterLeft || ap == Anchor::BottomLeft))) {
                const Anchor fap = layout_flip_anchor_x(ap);
                const Anchor fae = layout_flip_anchor_x(ae);
                const Vec2 fpos = compute_pos(fap, fae, {-rn.cfg.floating_offset.x, rn.cfg.floating_offset.y});
                if (fpos.x >= 0.0f && fpos.x + rn.width <= le->viewport.x) {
                    pos.x = fpos.x;
                }
            }
        }
        if (rn.cfg.clamp_to_viewport) {
            constexpr float kMargin = 6.0f;
            const float max_x = layout_maxf(kMargin, le->viewport.x - rn.width - kMargin);
            const float max_y = layout_maxf(kMargin, le->viewport.y - rn.height - kMargin);
            pos.x = layout_clampf(pos.x, kMargin, max_x);
            pos.y = layout_clampf(pos.y, kMargin, max_y);
        }
        layout_position_subtree(le, r.node_idx, pos);
    }

    // Pass 7: O(F log F) stable sort by z_index + emit render commands.
    layout_sort_roots(le);
    const Rect vp_rect = {0.0f, 0.0f, le->viewport.x, le->viewport.y};
    for (uint32_t ri = 0; ri < le->roots.count; ++ri) {
        const LayoutRoot& r = le->roots.data[ri];
        const LayoutNode& rn = le->nodes.data[r.node_idx];
        Rect root_clip = vp_rect;
        bool pushed_parent_clip = false;
        if (rn.is_floating && rn.cfg.clip_to_parent && r.parent_idx < le->nodes.count) {
            const Rect pbounds = le->nodes.data[r.parent_idx].bounds;
            le->cmds.push(LayoutCmd{
                .kind = LayoutCmdKind::ScissorStart,
                .id = le->nodes.data[r.parent_idx].id,
                .node_idx = r.parent_idx,
                .z_index = r.z_index,
                .bounds = pbounds,
                .user_data = le->nodes.data[r.parent_idx].cfg.user_data,
            });
            root_clip = layout_rect_intersect(vp_rect, pbounds);
            pushed_parent_clip = true;
        }
        layout_emit_subtree_cmds(le, r.node_idx, r.z_index, root_clip, pushed_parent_clip);
        if (pushed_parent_clip) {
            le->cmds.push(LayoutCmd{
                .kind = LayoutCmdKind::ScissorEnd,
                .id = le->nodes.data[r.parent_idx].id,
                .node_idx = r.parent_idx,
                .z_index = r.z_index,
                .bounds = le->nodes.data[r.parent_idx].bounds,
                .user_data = le->nodes.data[r.parent_idx].cfg.user_data,
            });
        }
    }

    // Publish current-frame bounds/scroll map into prev_map for queries after end_frame() and next frame!
    if (le->cur_map_cap > 0u) {
        if (le->prev_map_cap < le->cur_map_cap) {
            auto* fresh = static_cast<LayoutBoundsEntry*>(std::malloc(size_t(le->cur_map_cap) * sizeof(LayoutBoundsEntry)));
            if (fresh) {
                std::free(le->prev_map);
                le->prev_map = fresh;
                le->prev_map_cap = le->cur_map_cap;
            }
        }
        if (le->prev_map && le->prev_map_cap >= le->cur_map_cap) {
            if (le->prev_map_cap > le->cur_map_cap) {
                layout_bounds_map_clear(le->prev_map, le->prev_map_cap);
                le->prev_map_count = 0;
                for (uint32_t i = 0; i < le->cur_map_cap; ++i) {
                    if (le->cur_map[i].id != 0u) {
                        layout_bounds_map_put(&le->prev_map, &le->prev_map_cap, &le->prev_map_count, le->cur_map[i]);
                    }
                }
            } else {
                std::memcpy(le->prev_map, le->cur_map, size_t(le->cur_map_cap) * sizeof(LayoutBoundsEntry));
                le->prev_map_count = le->cur_map_count;
            }
        }
    }

    return {le->cmds.data, le->cmds.count};
}

// ============================================================================
// Inverse-Transformed True Occlusion Hitbox Tree
// ============================================================================

inline bool hitbox_rounded_contains(Rect r, float radius, Vec2 pt) noexcept {
    if (!r.contains(pt)) return false;
    if (radius <= 0.5f) return true;
    const float rad = layout_minf(radius, 0.5f * layout_minf(r.w, r.h));
    const float cx = r.x + r.w * 0.5f;
    const float cy = r.y + r.h * 0.5f;
    const float qx = std::fabs(pt.x - cx) - (r.w * 0.5f - rad);
    const float qy = std::fabs(pt.y - cy) - (r.h * 0.5f - rad);
    if (qx <= 0.0f || qy <= 0.0f) return true;
    return (qx * qx + qy * qy) <= (rad * rad + 0.5f);
}

inline void hitbox_run_hit_test(
    const HitboxEntry* hitboxes,
    uint32_t count,
    Vec2 pointer_pos,
    bool has_pointer,
    PodBuf<uint32_t>* out_over_ids,
    uint32_t* out_topmost_id,
    uint32_t* out_hovered_span_id) noexcept
{
    out_over_ids->clear();
    *out_topmost_id = 0u;
    *out_hovered_span_id = 0u;
    if (!has_pointer || !hitboxes || count == 0u) return;

    // Walk hitboxes in reverse paint order (topmost visual element first).
    uint32_t winner_idx = UINT32_MAX;
    for (uint32_t i = count; i > 0u; --i) {
        const uint32_t idx = i - 1u;
        const HitboxEntry& h = hitboxes[idx];
        if (h.behavior == HitboxBehavior::Passthrough) continue;
        if (!hitbox_rounded_contains(h.clip_rect, h.clip_radius, pointer_pos)) continue;
        Vec2 local_pt = pointer_pos;
        if (std::fabs(h.rotation_rad) > 1e-4f) {
            const float cx = h.visual_rect.x + h.visual_rect.w * 0.5f;
            const float cy = h.visual_rect.y + h.visual_rect.h * 0.5f;
            const float px = pointer_pos.x - cx;
            const float py = pointer_pos.y - cy;
            const float c = std::cos(h.rotation_rad);
            const float s = std::sin(h.rotation_rad);
            local_pt = {cx + c * px + s * py, cy - s * px + c * py};
        }
        if (!h.visual_rect.contains(local_pt)) continue;

        if (h.is_text_span) {
            if (*out_hovered_span_id == 0u) *out_hovered_span_id = h.id;
            if (*out_topmost_id == 0u && h.id != 0u) *out_topmost_id = h.id;
            winner_idx = idx;
            break;
        }
        // Any interactive ID, opaque surface, floating overlay, or explicit BlockMouse claims the topmost hit
        // and occludes overlapping non-ancestor siblings behind it.
        if (h.behavior == HitboxBehavior::BlockMouse || h.is_floating_root || h.id != 0u || h.is_opaque_surface) {
            winner_idx = idx;
            break;
        }
    }

    if (winner_idx == UINT32_MAX) return;

    // Collect winner + its true ancestor chain (within its floating layer) so ancestor buttons/scrollbars/canvases stay hovered
    // while overlapping non-ancestor siblings & lower overlays are strictly occluded!
    uint32_t chain[128];
    uint32_t chain_len = 0;
    uint32_t cur = winner_idx;
    uint32_t guard = 0;
    while (cur < count && chain_len < 128u && guard++ < 256u) {
        const HitboxEntry& h = hitboxes[cur];
        if (h.behavior != HitboxBehavior::Passthrough) {
            if (h.id != 0u && !h.is_text_span) {
                chain[chain_len++] = h.id;
                if (*out_topmost_id == 0u) *out_topmost_id = h.id;
            }
            if (h.is_floating_root) break;
        }
        cur = h.parent_idx;
    }

    // Push bottom-to-top (ancestors first, topmost child last) so existing reverse loops see topmost first.
    for (uint32_t i = chain_len; i > 0u; --i) {
        out_over_ids->push(chain[i - 1u]);
    }
}

// ============================================================================
// Dirty Subtree Cache Helpers
// ============================================================================

inline void subtree_cache_entry_destroy(SubtreeCacheEntry* e) noexcept {
    if (!e) return;
    std::free(e->prims);
    std::free(e->hitboxes);
    std::free(e->a11y_nodes);
    std::free(e->a11y_strings);
    *e = SubtreeCacheEntry{};
}

inline SubtreeCacheEntry* subtree_cache_find(SubtreeCacheEntry* entries, uint32_t count, uint32_t id) noexcept {
    if (!entries || id == 0u) return nullptr;
    for (uint32_t i = 0; i < count; ++i) {
        if (entries[i].id == id) return &entries[i];
    }
    return nullptr;
}

inline SubtreeCacheEntry* subtree_cache_get_or_create(PodBuf<SubtreeCacheEntry>* pool, uint32_t id) noexcept {
    if (!pool || id == 0u) return nullptr;
    if (SubtreeCacheEntry* e = subtree_cache_find(pool->data, pool->count, id)) return e;
    for (uint32_t i = 0; i < pool->count; ++i) {
        if (pool->data[i].id == 0u) {
            pool->data[i] = SubtreeCacheEntry{.id = id};
            return &pool->data[i];
        }
    }
    SubtreeCacheEntry* slot = pool->push_uninit();
    if (!slot) return nullptr;
    *slot = SubtreeCacheEntry{.id = id};
    return slot;
}

inline void subtree_cache_gc(PodBuf<SubtreeCacheEntry>* pool, uint32_t current_frame) noexcept {
    if (!pool) return;
    for (uint32_t i = 0; i < pool->count; ++i) {
        SubtreeCacheEntry& e = pool->data[i];
        if (e.id != 0u && current_frame > e.last_used_frame + 2u) {
            subtree_cache_entry_destroy(&e);
        }
    }
}

inline void subtree_cache_destroy_all(PodBuf<SubtreeCacheEntry>* pool) noexcept {
    if (!pool) return;
    for (uint32_t i = 0; i < pool->count; ++i) subtree_cache_entry_destroy(&pool->data[i]);
    pool->destroy();
}

} // namespace etch
