// Arena memory pool, cross-frame StateMap, and internal Context state definitions.
// Compiled via src/etch_unity.cpp.

#if defined(_WIN32)
#    include <malloc.h>
#else
#    include <cstdlib>
#endif

extern "C" {
typedef struct EtchContext EtchContext;
typedef float (*EtchCVirtualItemHeightFn)(uint32_t index, void* user_data);
typedef void (*EtchCWindowRefreshFn)(EtchContext* ctx, struct SDL_Window* window, float dt_seconds, void* user_data);
}

namespace etch {

inline constexpr uint32_t kMinPrimCap = 64u;
inline constexpr uint32_t kMaxPrimHardCap = 1u << 22;
inline constexpr uint32_t kMaxCollapsibleDepth = 32u;
inline constexpr uint32_t kAtlasSize = 16u;
inline constexpr uint32_t kSlugGlyphTableCapacity = 4096u;
inline constexpr uint32_t kMaxSlugBands = 8u;
inline constexpr uint32_t kInitialSlugCurveCap = 4096u;
inline constexpr uint32_t kInitialSlugBandCap = 8192u;
inline constexpr uint32_t kDefaultStateCapacity = 1024u;
inline constexpr uint32_t kMaxClipDepth = 64u;
inline constexpr uint32_t kMaxCachedPipelines = 4u;
inline constexpr uint32_t kMaxSplitDepth = 32u;
inline constexpr uint32_t kMaxCanvasDepth = 8u;
inline constexpr uint32_t kHudHistorySamples = 64u;
inline constexpr uint32_t kCursorCount = 10u;
inline constexpr uint32_t kMaxTextOpsPerFrame = 32u;
inline constexpr uint32_t kMaxPendingTextBytes = 1024u;
inline constexpr uint32_t kMaxImeCompBytes = 256u;
inline constexpr uint32_t kMaxKeyEventsPerFrame = 32u;
inline constexpr uint32_t kMaxChromeDragRects = 16u;
inline constexpr uint32_t kMaxChromeClientRects = 128u;
inline constexpr uint32_t kMaxDragLabelBytes = 64u;

struct WidgetState {
    uint32_t key = 0; // 0 marks an empty hash slot
    uint32_t last_seen_frame = 0;
    uint32_t prev_seen_frame = 0;
    bool     is_user_element = false;
    bool     is_focusable = false;
    bool window_drag_passthrough = false;
    uint32_t focus_scope = 0;
    uint32_t popup_owner = 0;
    bool     pressed_down = false;
    bool     pressed_this_frame = false;
    bool     clicked_this_frame = false;
    bool     right_clicked_this_frame = false;
    bool     double_clicked_this_frame = false;
    bool     is_text_input_writable = false;
    bool     is_multiline_input = false, edit_reveal_caret = true;
    bool     suppress_popup_keys = false;
    bool     accept_ghost_on_tab = false;
    float    edit_max_scroll_y = 0;
    float hover_t=0, hover_vel=0, press_t=0, press_vel=0;
    Vec2 terminal_cursor = {}, terminal_velocity = {};
    bool terminal_initialized = false;
    float sheet_pos = 0, sheet_velocity = 0, sheet_last_pointer = 0;
    uint32_t sheet_snap = 0;
    bool sheet_initialized = false, sheet_dragging = false;
    Rect anim_rect = {};
    Vec2 anim_vel_pos = {}, anim_vel_size = {};
    uint32_t anim_seen_frame=0;
    bool presence_initialized = false;
    float presence_pos = 0, presence_vel = 0;
    uint32_t presence_frame = 0;
    bool     spring_initialized = false;
    float    spring_pos = 0.0f;
    float    spring_vel = 0.0f;
    bool show_scrollbar = true;
    bool     is_scroll_x = false;
    bool     is_scroll_y = false;
    Vec2     scroll_pos = {};
    Vec2     scroll_vel = {};
    Vec2     scroll_max = {};
    Vec2     viewport_dim = {};
    Vec2     content_dim = {};
    Rect     last_box_logical = {};
    const TextBuffer* edit_text_buffer = nullptr;
    bool editor_reveal = false;
    bool     editor_scroll_pending = false;
    uint8_t  editor_scroll_align = 0u;
    uint32_t editor_scroll_line = 0u;
    bool editor_show_scrollbar = true;
    bool     edit_initialized = false;
    uint32_t edit_cursor = 0;
    uint32_t edit_sel_anchor = 0;
    float    edit_scroll_x = 0.0f;
    float    edit_scroll_y = 0.0f;
    float    edit_preferred_x = -1.0f;
    bool     edit_wrap_affinity = false;
    float    edit_wrap_width = 0.0f;
    uint64_t edit_wrap_version = 0u;
    uint32_t edit_wrap_lines = 0u;
    uint32_t edit_wrap_tab_width = 0u;
    float    edit_wrap_font_size = 0.0f;
    uint16_t edit_wrap_font_id = kInvalidFont;
    uint32_t edit_wrap_font_revision = 0;

    bool is_selectable_text = false;
    const char* sel_text_ptr = nullptr;
    uint32_t sel_text_len = 0;
    uint64_t sel_text_epoch = 0;
    bool sel_text_static = false;
    bool collapsible_initialized = false;
    bool collapsible_open = false;
    float collapsible_content_h = 0.0f;
    bool tree_initialized = false;
    bool tree_open = false;
    uint32_t table_focus_row = UINT32_MAX, table_anchor_row = UINT32_MAX;
    bool table_initialized = false;
    bool table_sort_descending = false;
    bool table_sort_changed = false;
    uint32_t table_sort_column = 0;
    bool column_override = false;
    float column_width = 0.0f;
    float column_grab = 0.0f;
    bool column_resizing = false;
    // Split Pane state
    bool     split_initialized = false;
    float    split_ratio = 0.5f;
    float    split_grab_offset = 0.0f;

    // Virtual List state (double-precision scroll prevents float32 ULP degradation past 16.7M px)
    bool     is_virtual_list = false;
    bool     vlist_pending_scroll = false;
    bool     vlist_stick_initialized = false;
    bool     vlist_stuck_bottom = true;
    uint32_t vlist_built_frame = 0;
    uint32_t vlist_pending_scroll_row = 0;
    float    vlist_pending_scroll_item_h = 0.0f;
    AlignY   vlist_pending_scroll_align = AlignY::Top;
    double   vscroll_pos_y = 0.0;
    double   vscroll_vel_y = 0.0;
    double   vscroll_max_y = 0.0;
    double   vscroll_total_h = 0.0;
    float    vlist_stride = 28.0f;

    // Infinite 2D Canvas & Node Drag state
    bool     is_canvas = false;
    bool     canvas_initialized = false;
    bool     canvas_interactive = true;
    bool     canvas_marquee_enabled = false;
    bool     canvas_marquee_active = false;
    bool     canvas_marquee_finished = false;
    bool     is_canvas_node = false;
    Vec2     canvas_marquee_start_world = {};
    Rect     canvas_marquee_world = {};
    uint32_t owner_canvas_id = 0;
    Vec2     canvas_pan = {};
    float    canvas_zoom = 1.0f;
    float    canvas_min_zoom = 0.15f;
    float    canvas_max_zoom = 8.0f;
    Vec2     drag_grab = {};
    bool     is_text_span = false;
    bool     span_hovered = false;
    float    vscrollbar_right_edge_x = 0.0f;
};

struct StateMap {
    WidgetState* entries = nullptr;
    uint32_t     capacity = 0; // Power of two
    uint32_t     count = 0;
};

struct alignas(16) SlugCurveGpu {
    uint32_t p1_xy = 0; // f16x2 normalized [0,1] Y-UP
    uint32_t p2_xy = 0; // f16x2 normalized [0,1] Y-UP
    uint32_t p3_xy = 0; // f16x2 normalized [0,1] Y-UP
    uint32_t pad0 = 0;
};
static_assert(sizeof(SlugCurveGpu) == 16);
static_assert(sizeof(SlugCurveGpu) % 16 == 0);

struct alignas(16) SlugBandEntryGpu {
    uint32_t x = 0; // band header: count; curve item: curve_idx
    uint32_t y = 0; // band header: rel_offset; curve item: 0
    uint32_t pad0 = 0, pad1 = 0;
};
static_assert(sizeof(SlugBandEntryGpu) == 16);
static_assert(sizeof(SlugBandEntryGpu) % 16 == 0);

struct SlugGlyphEntry {
    uint64_t key = 0; // Packed (slot_idx + 1, generation, glyph_id); 0 = empty
    float    min_x_em = 0.0f;
    float    min_y_em = 0.0f;
    float    max_x_em = 0.0f;
    float    max_y_em = 0.0f;
    uint32_t band_offset = 0;
    uint16_t num_hbands = 0;
    uint16_t num_vbands = 0;
    bool     has_outline = false;
};

struct ShapedGlyph {
    uint32_t glyph_id = 0;
    uint32_t cluster = 0;
    uint16_t font_slot = 0;
    uint8_t is_rtl = 0;
    uint8_t flags = 0;
    uint32_t cluster_end = 0;
    float    x_advance_1px = 0.0f;
    float    x_offset_em = 0.0f;
    float    y_offset_em = 0.0f;
};

static_assert(sizeof(ShapedGlyph) == 28);

struct FontSlot {
    stbtt_fontinfo font_info = {};
    uint8_t*       font_file_data = nullptr;
    size_t         font_file_size = 0;
    bool           font_file_mapped = false;
    int32_t        font_ascent = 0;
    int32_t        font_descent = 0;
    int32_t        font_line_gap = 0;
    float          scale_1px = 0.0f;
    float          ascent_em = 0.8f;
    float          descent_em = 0.2f;
    float          line_height_em = 1.2f;
    hb_blob_t* colr_blob = nullptr;
    hb_blob_t* cpal_blob = nullptr;
    const uint8_t* colr_data = nullptr;
    const uint8_t* cpal_data = nullptr;
    uint32_t colr_len = 0, cpal_len = 0;
    uint16_t colr_num_base = 0, colr_num_layers = 0, cpal_num_palette_entries = 0;
    uint32_t colr_base_off = 0, colr_layer_off = 0, cpal_color_records_off = 0;
    bool has_colr = false;
    uint32_t upem = 0, num_glyphs = 0;
    bool has_outlines = false, has_sbix = false, has_cbdt = false, has_color_emoji = false;
    hb_blob_t* sbix_blob = nullptr;
    hb_blob_t* cblc_blob = nullptr;
    hb_blob_t* cbdt_blob = nullptr;
    const uint8_t* sbix_data = nullptr;
    const uint8_t* cblc_data = nullptr;
    const uint8_t* cbdt_data = nullptr;
    uint32_t sbix_len = 0, cblc_len = 0, cbdt_len = 0;
    hb_blob_t*     hb_blob = nullptr;
    hb_face_t*     hb_face = nullptr;
    hb_font_t*     hb_font = nullptr;
    uint32_t       bullet_glyph_id = 0;
    float          bullet_advance_em = 0.0f;
    float          space_advance_em = 0.28f;
    uint64_t       source_hash = 0;
    uint32_t       metric_hash = 0;
    uint8_t        generation = 0;
    bool           loaded = false;
    bool tabular_numbers = false;
    char           font_file_path[512] = {};
    int32_t        face_index = 0;
    void*          dwrite_face = nullptr;
    bool           dwrite_probed = false;
};

struct VectorIconEntry {
    uint32_t band_offset = 0;
    uint16_t num_hbands = 0;
    uint16_t num_vbands = 0;
    Vec2     view_size = {24.0f, 24.0f};
    bool     loaded = false;
    bool     has_outline = false;
};

struct SpriteSlot {
    uint32_t x = 0, y = 0, width = 0, height = 0, alloc_w = 0, alloc_h = 0;
    uint32_t next_free = 0;
    uint16_t generation = 1;
    bool live = false, srgb = true, premultiplied = false;
};
struct EmojiBitmapEntry {
    uint64_t key = 0;
    uint32_t sprite_id = kInvalidSprite, ppem = 0, width_px = 0, height_px = 0;
    uint32_t tile_x = 0, tile_y = 0;
    float bearing_x = 0, bearing_y_top = 0;
    bool has_bitmap = false;
};
struct FontAtlas {
    Arena* scratch = nullptr;
    SlugGlyphEntry*   slug_glyphs = nullptr; // Open-addressed table (power-of-2 capacity)
    uint32_t          slug_glyph_cap = 0;
    uint32_t          slug_glyph_count = 0;
    SlugCurveGpu*     slug_curves = nullptr;
    uint32_t          slug_curve_count = 0;
    uint32_t          slug_curve_cap = 0;
    SlugBandEntryGpu* slug_bands = nullptr;
    uint32_t          slug_band_count = 0;
    uint32_t          slug_band_cap = 0;
    uint32_t          uploaded_slug_curves = 0;
    uint32_t          uploaded_slug_bands = 0;

    uint8_t*          pixels = nullptr; // kAtlasSize * kAtlasSize * 4 solid white fallback
    SDL_GPUTexture*   gpu_tex = nullptr;
    uint32_t width = kAtlasSize, height = kAtlasSize, gpu_w = 0, gpu_h = 0;
    uint32_t dirty_y0 = 0, dirty_y1 = kAtlasSize;
    bool dirty = true;
    uint32_t shelf_x = 0, shelf_y = 8, shelf_h = 0;
    SpriteSlot* sprites = nullptr;
    uint32_t sprite_cap = 0, sprite_used = 1, sprite_live = 0, sprite_free = 0;
    EmojiBitmapEntry* emoji_bitmaps = nullptr;
    uint32_t emoji_cap = 0, emoji_count = 0;
    EmojiBitmapEntry* hinted_glyphs = nullptr;
    uint32_t hinted_cap = 0, hinted_count = 0;
    // Dedicated linear R8G8B8A8_UNORM atlas for 3-channel LCD ClearType RGB subpixel coverage.
    uint8_t* subpixel_pixels = nullptr;
    SDL_GPUTexture* subpixel_gpu_tex = nullptr;
    uint32_t subpixel_w = 0, subpixel_h = 0, subpixel_gpu_w = 0, subpixel_gpu_h = 0;
    uint32_t subpixel_dirty_y0 = UINT32_MAX, subpixel_dirty_y1 = 0;
    uint32_t subpixel_shelf_x = 0, subpixel_shelf_y = 2, subpixel_shelf_h = 0;
    bool subpixel_dirty = false;
    void* dwrite_factory = nullptr;
    void* dwrite_module = nullptr;
    bool dwrite_probed = false;
    bool hinted_small_text = false;

    FontSlot          slots[kMaxFontSlots] = {};
    uint16_t          fallback_order[kMaxFontSlots] = {};
    uint32_t          fallback_count = 0;
    uint32_t          font_revision = 1;
    bool              font_valid = false;
    bool              bold_probed = false;
    bool              mono_probed = false;
    uint8_t weighted_probed = 0;
    bool              fallbacks_probed = false;
    bool              emoji_fallback_probed = false;

    VectorIconEntry   icons[kMaxVectorIcons] = {};

    hb_buffer_t*      hb_buf = nullptr;
};

enum class GpuPipelineKind : uint8_t {
    Uber = 0,
    QuadOpaque = 1,
    QuadAlpha = 2,
    SubpixelText = 3,
    UberPunch = 4,
    UberAdditive = 5,
};

struct DrawBatch {
    uint32_t        first_prim = 0;
    uint32_t        prim_count = 0;
    SDL_GPUTexture* ext_textures[8] = {}; // Slots 1..8; slot 0 is always FontAtlas::gpu_tex
    uint32_t        ext_count = 0;
    uint32_t        blend_rgba8 = 0; // Packed sRGB RGBA8 text color for SubpixelText CONSTANT_COLOR
    GpuPipelineKind pipeline = GpuPipelineKind::Uber;
    bool is_punch_hole = false;
    bool is_additive = false;
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

enum class CustomCmdKind : uint8_t {
    Box = 0,
    TextInput = 1,
    CanvasGrid = 2,
    CanvasWire = 3,
    Icon = 4,
    CodeEditor = 5,
};

enum class DrawCmdKind : uint8_t {
    Rect = 0,
    Line = 1,
    Polyline = 2,
    Bezier = 3,
    Ring = 4,
    Icon = 5,
    Text = 6,
    Image = 7,
    Fill = 8,
    Callback = 9,
    Mesh = 10,
    Hole = 11,
    Beam = 12,
};

struct BoxExtra;
struct VisualLayerNode {
    const VisualLayerNode* parent = nullptr;
    BoxExtra* source_box = nullptr;
    BoxExtra* pivot_box = nullptr;
    LayerDesc local = {};
    mutable float resolved_opacity = 1;
    mutable Vec2 resolved_scale = {1,1}, resolved_offset_px = {};
    mutable Vec2 inherited_scale = {1,1}, inherited_offset_px = {};
    mutable float resolved_blur_px = 0.0f;
    mutable const VisualLayerNode* resolved_blur_root = nullptr;
    mutable bool resolved_plus_lighter = false;
    mutable bool resolved_animating = false;
    mutable uint32_t resolved_frame = 0;
};
struct BoxBuildStack {
    BoxBuildStack* parent = nullptr;
    const VisualLayerNode* previous_layer = nullptr;
};
struct DrawCmdNode {
    const VisualLayerNode* layer = nullptr;
    DrawCmdNode*  next = nullptr;
    DrawCmdKind   kind = DrawCmdKind::Rect;
    float         world_scale = 1.0f;
    RectDrawDesc  rect = {};
    MeshRectDrawDesc mesh = {};
    PunchHoleDesc hole = {};
    BorderBeamDrawDesc beam = {};
    LineDesc      line = {};
    const Vec2*   poly_points = nullptr;
    uint32_t      poly_count = 0;
    bool          poly_closed = false;
    Color         poly_color = {};
    float         poly_thickness = 1.5f;
    Color         poly_glow_color = {};
    float         poly_glow_px = 0.0f;
    BezierDesc    bezier = {};
    RingDesc      ring = {};
    IconDrawDesc  icon = {};
    TextDrawDesc  text_cmd = {};
    const uint32_t* terminal_columns = nullptr;
    float terminal_cell_width = 0;
    ImageDrawDesc image_cmd = {};
    FilledPathDesc fill_cmd = {};
    LayoutDrawFn callback = nullptr;
    void* callback_data = nullptr;
    Theme* callback_theme = nullptr;
};

SDL_HitTestResult SDLCALL etch_sdl_hit_test(SDL_Window* win, const SDL_Point* area, void* data) noexcept;

struct CanvasGridExtra {
    uint32_t canvas_id = 0;
    Vec2     pan = {};
    float    zoom = 1.0f;
    float    grid_step_world = 24.0f;
    uint32_t major_subdiv = 4u;
    Color    minor_color = {};
    Color    major_color = {};
};

struct CanvasWireExtra {
    uint32_t canvas_id = 0;
    Vec2     from_world = {};
    Vec2     to_world = {};
    Color    color_start = {};
    Color    color_end = {};
    Color    glow_color = {};
    float    thickness_world = 2.2f;
    float    glow_world = 0.0f;
    float    tangent_bias = 0.45f;
};

struct CodeEditorExtra;
struct BoxExtra {
    float world_scale = 1;
    bool geometry_recorded = false;
    const VisualLayerNode* layer = nullptr;
    Rect layout_rect_logical = {};
    CodeEditorExtra* code_editor = nullptr;
    CustomCmdKind    kind = CustomCmdKind::Box;
    BoxDesc          desc = {};
    TextInputExtra*  text_input = nullptr;
    CanvasGridExtra* canvas_grid = nullptr;
    CanvasWireExtra* canvas_wire = nullptr;
    IconDesc         icon_desc = {};
    DrawCmdNode*     draw_head = nullptr;
    DrawCmdNode*     draw_tail = nullptr;
    DrawCmdNode*     fg_draw_head = nullptr;
    DrawCmdNode*     fg_draw_tail = nullptr;
    bool             keyboard_focus_ring = false;
    bool             is_virtual_list = false;
    bool             is_canvas = false;
    Color            focus_ring_color = {};
    float            focus_ring_radius_px = 0.0f;
    Color            scrollbar_track = {};
    Color            scrollbar_thumb = {};
    Color            scrollbar_thumb_hover = {};
    Color            scrollbar_thumb_drag = {};
};

struct SplitStackEntry {
    uint32_t id = 0;
    Axis     axis = Axis::Row;
    float    ratio = 0.5f;
    float    divider_size_px = 6.0f;
    float    divider_hit_px = 8.0f;
    float    min_first_px = 120.0f;
    float    min_second_px = 120.0f;
    Color    divider_color = {};
    Color    divider_hover_color = {};
    Color    divider_active_color = {};
    Color    grip_hover_color = {};
    Color    grip_idle_color = {};
    bool     in_second = false;
};

struct CanvasStackEntry {
    uint32_t    id = 0;
    CanvasDesc  desc = {};
    CanvasState state = {};
    bool        in_node = false;
};

struct ImageExtra {
    const VisualLayerNode* layer = nullptr;
    ImageDesc desc = {};
};

struct RichSpanInternal {
    uint32_t text_byte_start = 0;
    uint32_t byte_start = 0;
    uint32_t byte_end = 0;
    Color    color = Color::white();
    Color    background = {};
    Color    border_color = {};
    float    font_size_px = 14.0f;
    uint16_t font_id = kFontSans;
    bool     underline = false;
    bool     strikethrough = false;
    float    letter_spacing = 0.0f;
    float    padding_x = 0.0f;
    float    bg_radius = 4.0f;
    uint32_t id = 0;
    uint16_t icon_id = kInvalidIcon;
    Color icon_color = {};
    Color hover_background = {};
    float icon_size = 0.0f;
    float icon_advance = 0.0f;
    float padding_y = 0.0f;
    bool has_wipe = false;
    Color color_end = {};
    float wipe_x = 0.0f;
    float wipe_feather_px = 0.0f;
    uint32_t layout_text_start() const noexcept { return byte_start + (icon_id != kInvalidIcon ? 1u : 0u); }
};

struct SelectableTextExtra {
    const VisualLayerNode* layer = nullptr;
    const char* full_utf8 = nullptr;
    uint32_t total_bytes = 0;
    uint32_t selectable_id = 0;
    uint32_t clay_elem_id = 0;
    Color selection_color = {};
};

struct RichTextExtra {
    const VisualLayerNode* layer = nullptr;
    uint32_t selectable_id = 0;
    Color selection_color = {};
    RichSpanInternal* spans = nullptr;
    uint32_t          span_count = 0;
    const char*       full_utf8 = nullptr;
    uint32_t          total_bytes = 0;
    bool              has_icon_markers = false;
    float             line_height_px = 0.0f;
    float             max_ascent_px = 0.0f;
    float             max_descent_px = 0.0f;
    AlignX            align_x = AlignX::Left;
    uint32_t          config_hash = 0;
    uint32_t          clay_elem_id = 0;
};

struct SpanHitRegion {
    Rect visual_bounds = {}, visible_bounds = {};
    Vec2 local_scale = {1,1};
    float visual_opacity = 1;
    uint32_t id = 0; // 0 marks a floating overlay blocker rect
    uint32_t clay_elem_id = 0;
    Rect     rect = {};
    uint32_t paint_order = 0;
    int16_t  z_index = 0;
    Rect     clip_rect = {};
    bool     has_clip = false;
};

struct alignas(16) ClipShapeGpu {
    Vec4 bounds = {}, radii = {};
    Uint4 meta = {}; // Parent index, continuous corners, packed f16 top/bottom and left/right fade widths.
};
static_assert(sizeof(ClipShapeGpu)==48 && sizeof(ClipShapeGpu)%16==0);

struct ClipEntry {
    Rect  rect = {};

    float radius = 0.0f;
    uint32_t shape = 0;
    bool indexed = false;
};

// Shared internal entry points; definitions precede the editor in the unity TU.
uint32_t utf8_clamp_boundary(const char*, uint32_t, uint32_t) noexcept;
uint32_t utf8_prev_grapheme(const char*, uint32_t, uint32_t) noexcept;
uint32_t utf8_next_grapheme(const char*, uint32_t, uint32_t) noexcept;
uint32_t utf8_word_left(const char*, uint32_t, uint32_t) noexcept;
uint32_t utf8_word_right(const char*, uint32_t, uint32_t) noexcept;
void utf8_word_bounds_at(const char*, uint32_t, uint32_t, uint32_t*, uint32_t*) noexcept;
using LineClusterVisitor = void (*)(void*, uint32_t, uint32_t, float, float, bool) noexcept;
void measure_line_segment_x(Context*, uint16_t, const char*, uint32_t, uint32_t, uint32_t,
    float, float, bool, bool, float*, float*, float*, uint32_t = 4u,
    LineClusterVisitor = nullptr, void* = nullptr) noexcept;
void emit_utf8_glyph_run(Context*, uint16_t, const char*, uint32_t, float, float, float,
    float, Color, float, float, float, float, const ClipEntry&, float, bool, bool, float*, float*, uint32_t = 4u,
    std::span<const TextInputHighlight> = {}, uint32_t = 0u) noexcept;
void clear_ime_composition(Context*) noexcept;
struct ScrollbarGeom;
ScrollbarGeom compute_vscrollbar_geom(const WidgetState*) noexcept;
void emit_code_editor_command(Context*, const CodeEditorExtra*, Clay_BoundingBox, const ClipEntry&, float) noexcept;
void optimize_render_batches(Context*) noexcept;

struct ClipStack {
    ClipEntry entries[kMaxClipDepth] = {};
    uint32_t  depth = 0;
    uint32_t  overflow = 0;
};

struct CachedPipeline {
    SDL_GPUTextureFormat     format = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
};

struct BlurSplit {
    uint32_t split_batch = 0; // First batch in this glass layer (prior batches render into scene before blur)
    uint32_t split_prim = 0;  // First logical prim in this glass layer
    float    blur_radius_px = 0.0f;
    Rect     union_bounds = {};
};

inline constexpr uint32_t kMaxFilterSlices = 8u;
inline constexpr uint32_t kMaxFilterLevels = 5u; // 0: 1x, 1: 1/2, 2: 1/4, 3: 1/8, 4: 1/16

struct FilterSliceTargets {
    SDL_GPUTexture* levels[kMaxFilterLevels] = {};
};

struct FilterBlurGroup {
    const VisualLayerNode* blur_root = nullptr;
    uint32_t first_batch = 0;
    uint32_t end_batch = 0;
    uint32_t slice = 0;
    uint32_t max_level = 3;
    float    blur_px = 0.0f;
    bool     plus_lighter = false;
    Rect     raw_bounds = {};
    bool     has_bounds = false;
    Rect     padded_bounds = {};
    ClipEntry ancestor_clip = {};
    uint32_t ancestor_clip_index = 0;
    uint32_t ancestor_clip_depth = 1;
};

inline SDL_GPUTexture* filter_tex_sentinel(uint32_t slice, uint32_t level) noexcept {
    static uint8_t s_filter_tex_tags[kMaxFilterSlices][kMaxFilterLevels] = {};
    return reinterpret_cast<SDL_GPUTexture*>(&s_filter_tex_tags[slice][level]);
}

struct KeyEventEntry {
    uint32_t key = 0;
    uint16_t mods = 0;
    bool     repeat = false;
    bool     consumed = false;
};

struct TextUndoRecord {
    uint32_t offset = 0;
    uint32_t length = 0;
    uint32_t cursor = 0;
    uint32_t anchor = 0;
    uint32_t hash = 0;
};

struct TextUndoSlot {
    uint32_t       widget_id = 0;
    uint32_t       last_used_frame = 0;
    TextUndoRecord* entries = nullptr; uint32_t entries_cap = 0;
    uint32_t       head = 0;
    uint32_t       count = 0;
    uint32_t       cur = 0;
    uint32_t       write_pos = 0;
    uint32_t       bytes_used = 0;
    float          last_coalesce_sec = -10.0f;
    uint8_t        last_op_group = 0; // 0 = none/discrete, 1 = char burst, 2 = delete burst
    char* bytes = nullptr; uint32_t bytes_cap = 0;
};

inline SDL_GPUTexture* blur_tex_sentinel() noexcept {
    static uint8_t s_blur_tex_tag = 0;
    return reinterpret_cast<SDL_GPUTexture*>(&s_blur_tex_tag);
}

inline SDL_GPUTexture* subpixel_tex_sentinel() noexcept {
    static uint8_t s_subpixel_tex_tag = 0;
    return reinterpret_cast<SDL_GPUTexture*>(&s_subpixel_tex_tag);
}

inline constexpr uint32_t kMaxTreeDepth = 32u;
struct DockNode {
    bool used = false; uint32_t parent = 0; uint32_t first = 0; uint32_t second = 0;
    uint32_t split_id = 0; Axis axis = Axis::Row;
    float min_first = 120.0f; float min_second = 120.0f;
    uint32_t active = 0;
};
struct DockTabSlot {
    uint32_t id = 0; uint32_t leaf = 0; char title_inline[48] = {}; char* title_heap = nullptr; uint32_t title_cap = 0;
    uint16_t icon_id = kInvalidIcon; bool closable = false; bool open = false;
};
struct DockSpaceSlot {
    uint32_t id = 0; uint32_t root = 0; uint32_t split_seq = 0;
    DockNode* nodes = nullptr; uint32_t node_cap = 0; DockTabSlot* tabs = nullptr; uint32_t tab_cap = 0;
};
struct DockTraverseEntry { uint32_t node_idx = 0; uint8_t phase = 0; bool split_pushed = false; };
struct DockCursor {
    DockSpaceSlot* space = nullptr; DockSpaceDesc desc = {};
    DockTraverseEntry* stack = nullptr; uint32_t traverse_cap = 0; uint32_t depth = 0;
    bool pane_open = false; uint32_t move_pane = 0; uint32_t target = 0; DockZone zone = DockZone::None; bool reorder = false; bool insert_after = false;
};
struct TableMetadata {
    uint32_t id = 0; uint32_t count = 0;
    uint32_t column_ids[kMaxTableColumns] = {};
    float widths[kMaxTableColumns] = {};
    float min_widths[kMaxTableColumns] = {};
    float max_widths[kMaxTableColumns] = {};
};
struct TableStackEntry {
    TableDesc desc = {}; TableColumnDesc columns[kMaxTableColumns] = {};
    GridTrack tracks[kMaxTableColumns] = {};
    float col_widths[kMaxTableColumns] = {}; uint32_t count = 0;
    uint32_t cursor_row = 0; uint32_t end_row = 0; uint32_t col_cursor = 0;
    bool row_open = false; bool cell_open = false;
};
struct ModalFocusScope { uint32_t id = 0; uint32_t restore = 0; uint32_t seen = 0; };

inline constexpr uint32_t kVarListChunkSize = 256u;
struct VarVirtualListCache {
    uint32_t list_id = 0, item_count = 0, chunk_count = 0;
    float default_height = 28, gap_px = 0;
    float** chunks = nullptr;
    double* fenwick = nullptr;
    double total_stride_sum = 0;
    uint32_t last_anchor_row = 0;
    float measured_width = 0.0f;
    bool dirty = false;
    bool uncapped_heights = false;
    VirtualItemHeightFn measure_fn = nullptr;
    EtchCVirtualItemHeightFn c_measure_fn = nullptr;
    void* measure_user_data = nullptr;
};
struct ActiveVarListRecord {
    uint32_t list_id, first_index, end_index;
    ActiveVarListRecord* next;
};
void free_vlist(VarVirtualListCache* c) noexcept {
    for (uint32_t i = 0; i < c->chunk_count; ++i) std::free(c->chunks[i]);
    std::free(c->chunks); std::free(c->fenwick);
    *c = {};
}

struct DesktopPopup { uint32_t id = 0; Vec2 anchor = {}; uint32_t opened_frame = 0; int selection = 0; uint32_t parent_id = 0; };
struct DropRecord { uint32_t offset, length; };
struct PendingA11yAction { uint32_t id = 0; A11yAction action = {}; bool consumed = false; uint8_t age = 0; float number = 0; uint32_t length = 0; char text[1024] = {}; };
struct A11yBuild { A11yNode node = {}; A11yBuild* next = nullptr; A11yBuild* parent = nullptr; uint32_t index = 0, clay_id = 0; };
struct A11yPatch { A11yOverrideDesc desc = {}; A11yPatch* next = nullptr; };
struct A11yIndexEntry { uint32_t id = 0; A11yBuild* build = nullptr; A11yPatch* patches = nullptr; A11yPatch* tail = nullptr; };
struct WindowUiState {
    const VisualLayerNode* current_layer = nullptr;
    BoxBuildStack* box_build_stack = nullptr;
    Arena a11y_arena = {}; A11yNode* a11y_nodes = nullptr; uint32_t a11y_node_count = 0;
    PendingA11yAction a11y_actions[16] = {}; uint32_t a11y_action_count = 0;
    PendingA11yAction frame_a11y_actions[16] = {}; uint32_t frame_a11y_action_count = 0;

    uint32_t scrub_id = 0; float scrub_value = 0;
    uint32_t drag_edit_id = 0; uint32_t drag_edit_seen_frame = 0;
    char drag_edit_buf[64] = {}; uint32_t drag_edit_len = 0;
    uint32_t drag_edit_pending_id = 0;
    char drag_edit_pending_buf[64] = {};
    uint32_t modal_depth = 0;
    uint32_t prev_top_modal_id = 0; uint32_t top_modal_id = 0;
    uint32_t id_stack[32] = {}; uint32_t id_stack_depth = 0; uint32_t id_stack_overflow = 0;
    uint8_t sheet_depth = 0; bool sheet_focus_scoped[16] = {};
    DesktopPopup popups[16] = {}; uint32_t popup_count = 0; uint32_t popup_depth = 0; uint32_t popup_build_ids[16] = {};
    TabBarDesc tab_bars[8] = {}; uint32_t tab_bar_depth = 0;
    uint32_t menu_bar_id = 0; uint32_t menu_owner = 0; uint32_t menu_open_id = 0;
    uint32_t tooltip_target = 0; float tooltip_start = 0;
    uint32_t tooltip_prev_winner = 0; uint32_t tooltip_frame_winner = 0; uint32_t tooltip_frame_rank = 0;
    uint64_t tooltip_start_tick = 0; uint64_t tooltip_wake_tick = 0;

    VarVirtualListCache* vlists = nullptr;
    uint32_t vlist_cap = 0, vlist_count = 0;
    ActiveVarListRecord* active_vlists = nullptr;
    uint32_t active_vlist_count = 0;
    ModalFocusScope modal_scopes[32] = {};
    uint32_t modal_count = 0;
    void* clay_mem = nullptr;
    Clay_Context* clay_ctx = nullptr;
    StateMap states = {};
    SDL_GPUTexture* scene_tex = nullptr;
    SDL_GPUTexture* blur_ping_tex = nullptr;
    SDL_GPUTexture* blur_pong_tex = nullptr;
    uint32_t blur_rt_w = 0;
    uint32_t blur_rt_h = 0;
    FilterSliceTargets filter_slices[kMaxFilterSlices] = {};
    uint32_t filter_rt_w = 0;
    uint32_t filter_rt_h = 0;
    uint32_t filter_idle_frames = 0;
    bool keyboard_focus_active = false;
    uint32_t focus_scope = 0;
    uint32_t focus_scopes[32] = {};
    uint32_t focus_scope_depth = 0;
    uint32_t modal_scope = 0;
    bool modal_needs_focus = false;
    bool os_dragging = false;
    bool has_os_drop_pos = false;
    Vec2 os_drop_pos = {};
    Vec2 delivered_drop_pos = {};
    bool has_delivered_drop_pos = false;
    DropRecord* pending_text_drop_records = nullptr; uint32_t pending_text_drop_cap = 0;
    uint32_t pending_text_drop_count = 0;
    Str* frame_text_drops = nullptr;
    uint32_t frame_text_drop_count = 0;
    // Per-frame & input tracking
    uint32_t   viewport_w = 1280u;
    uint32_t   viewport_h = 720u;
    float      dpi_scale = 1.0f;
    bool       pixel_snap = true;
    float      min_dpi_scale = 0.0f;
    float      applied_dpi_scale = 1.0f;
    float bloom_intensity = 0, bloom_threshold = .7f;
    float      elapsed_seconds = 0.0f;
    uint32_t   frame_index = 0;
    uint32_t   prev_a11y_focus_id = 0u;

    Vec2       event_pointer_pos = {-1.0f, -1.0f};
    bool       has_event_pointer = false;
    bool       event_pointer_down = false;
    bool       event_right_down = false;
    bool       event_middle_down = false;
    bool       event_left_pressed_edge = false;
    bool       event_left_released_edge = false;
    bool       event_right_pressed_edge = false;
    bool       event_right_released_edge = false;
    Vec2       event_scroll_delta = {};
    Vec2       frame_scroll_delta = {};
    bool       prev_pointer_down = false;
    bool       prev_right_down = false;
    bool       prev_pan_button_down = false;
    Vec2       prev_pointer_pos = {-1.0f, -1.0f};
    bool       has_pointer = false, had_pointer = false;
    Vec2       pointer_delta = {};
    bool       space_down = false;

    uint32_t    active_id = 0;
    uint32_t    focus_id = 0;
    uint32_t    pan_canvas_id = 0;
    uint32_t    marquee_canvas_id = 0;
    uint32_t    marquee_finished_canvas_id = 0;
    bool        mouse_captured = false;
    Vec2        pointer_pos = {-1.0f, -1.0f};
    bool        left_pressed_this_frame = false;
    Vec2        left_press_origin = {-1.0f, -1.0f};
    float       left_max_drag_dist_sq = 0.0f;
    bool        right_pressed_this_frame = false;
    bool        right_clicked_any_this_frame = false;
    bool        right_drag_exceeded = false;
    uint32_t    right_active_id = 0;
    Vec2        right_press_origin = {-1.0f, -1.0f};
    float       scrollbar_drag_grab_offset = 0.0f;
    uint32_t* focus_order = nullptr;
    uint32_t focus_cap = 0;
    uint32_t    focus_count = 0;
    uint32_t* prev_focus_order = nullptr;
    uint32_t    prev_focus_count = 0;
    int32_t     pending_tab_delta = 0;
    Cursor      desired_cursor = Cursor::Arrow;
    uint32_t    redraw_budget = 2u;
    uint32_t    active_anim_count = 0u;
    float       sim_dt = 0.0f;

    DockSpaceSlot* dock_spaces = nullptr; uint32_t dock_space_cap = 0; uint32_t dock_space_count = 0;
    DockCursor dock_cursor = {};
    TableMetadata* tables = nullptr; uint32_t table_cap = 0; uint32_t table_count = 0;
    TableStackEntry table_stack[8] = {};
    uint32_t table_depth = 0;
    uint32_t tree_depth = 0;
    uint32_t collapsible_depth = 0;
    SplitStackEntry  split_stack[kMaxSplitDepth] = {};
    uint32_t         split_depth = 0;
    CanvasStackEntry canvas_stack[kMaxCanvasDepth] = {};
    uint32_t         canvas_depth = 0;
    Rect             last_canvas_clip_px = {};
    float            last_canvas_clip_radius_px = 0.0f;

    bool        debug_hud_open = false;
    bool        debug_wireframe_open = false;
    float       layout_us_hist[kHudHistorySamples] = {};
    float       gpu_us_hist[kHudHistorySamples] = {};
    uint32_t    hud_hist_cursor = 0;
    uint32_t    hud_hist_count = 0;

    uint8_t       last_mouse_clicks = 1u;
    KeyEventEntry pending_keys[kMaxKeyEventsPerFrame] = {};
    uint32_t      pending_key_count = 0;
    KeyEventEntry frame_keys[kMaxKeyEventsPerFrame] = {};
    uint32_t      frame_key_count = 0;
    uint16_t      event_key_mods = 0;
    uint16_t      frame_key_mods = 0;
    bool          pending_escape_consumed_by_ime = false;
    bool          frame_escape_consumed_by_ime = false;

    DropRecord* pending_drop_records = nullptr; uint32_t pending_drop_cap = 0;
    uint32_t      pending_drop_count = 0;
    char*         pending_drop_buf = nullptr; uint32_t pending_drop_capacity = 0;
    uint32_t      pending_drop_bytes = 0;
    Str*          frame_drops = nullptr;
    uint32_t      frame_drop_count = 0;
    char*         frame_drop_buf = nullptr;

    TextUndoSlot* undo_pool = nullptr; uint32_t undo_cap = 0;

    PendingTextOp pending_text_ops[kMaxTextOpsPerFrame] = {};
    uint32_t      pending_text_op_count = 0;
    char          pending_text_utf8[kMaxPendingTextBytes] = {};
    uint32_t      pending_text_utf8_len = 0;

    PendingTextOp frame_text_ops[kMaxTextOpsPerFrame] = {};
    uint32_t      frame_text_op_count = 0;
    char          frame_text_utf8[kMaxPendingTextBytes] = {};
    uint32_t      frame_text_utf8_len = 0;

    char          ime_comp_utf8[kMaxImeCompBytes] = {};
    uint32_t      ime_comp_len = 0;
    uint32_t      ime_comp_cursor_byte = 0;
    uint32_t      ime_comp_sel_len_byte = 0;
    bool          pending_ime_comp_edited = false;
    bool          frame_ime_comp_edited = false;
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
    uint64_t      caret_blink_tick = 0;

    uint32_t      applied_font_revision = 0;
    SpanHitRegion* drop_hits = nullptr;
    uint32_t drop_hit_cap = 0;
    uint32_t drop_hit_count = 0;
    SpanHitRegion* span_hits = nullptr;
    uint32_t span_hit_cap = 0;
    uint32_t      span_hit_count = 0;

    // Frameless Custom Window Chrome (double-buffered for SDL_HitTest callback safety)
    WindowChromeDesc chrome_desc = {};
    Rect chrome_maximize_rect = {}, pending_chrome_maximize_rect = {};
    uint32_t pending_chrome_maximize_id = 0;
    Rect             chrome_drag_rects[kMaxChromeDragRects] = {};
    uint32_t         chrome_drag_count = 0;
    Rect             chrome_client_rects[kMaxChromeClientRects] = {};
    uint32_t         chrome_client_count = 0;
    Rect             pending_chrome_drag_rects[kMaxChromeDragRects] = {};
    uint32_t         pending_chrome_drag_count = 0;
    Rect             pending_chrome_client_rects[kMaxChromeClientRects] = {};
    uint32_t         pending_chrome_client_count = 0;

    // Zero-heap in-app Drag-and-Drop
    bool             drag_active = false;
    bool             drag_drop_pending = false;
    bool             drag_accepted_this_frame = false;
    uint32_t         drag_source_id = 0;
    uint32_t         drag_kind_hash = 0;
    uint64_t         drag_u64 = 0;
    uint32_t         drag_u32 = 0;
    uint32_t         drag_size = 0;
    uint8_t drag_inline[64] = {};
    uint8_t* drag_heap = nullptr; uint32_t drag_heap_cap = 0;
    uint32_t         drag_label_len = 0;
    char             drag_label[kMaxDragLabelBytes] = {};

    // Transient Early-Z depth target for main render pass.
    SDL_GPUTexture*      depth_tex = nullptr;
    uint32_t             depth_w = 0, depth_h = 0;
    SDL_GPUTextureFormat depth_fmt = SDL_GPU_TEXTUREFORMAT_INVALID;
};

struct ClaimedWindowSlot {
    SDL_Window* window = nullptr;
    bool hit_test_attached = false;
    bool macos_chrome_applied = false;
    bool chrome_subclass_attached = false;
    uintptr_t native_chrome_style = 0; bool native_frame_applied = false;
    Context* chrome_context = nullptr;
    bool native_max_press = false, native_max_hover = false;
    void* os_a11y = nullptr;
    WindowUiState ui = {};
};

struct ShaderSurfaceSlot {
    SDL_GPUShader* shader = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUTexture* texture = nullptr;
    SDL_GPUTexture* retired_tex = nullptr;
    SDL_GPUTexture* prev_texture = nullptr;
    SDL_GPUTexture* retired_prev_tex = nullptr;
    SDL_Window* window = nullptr;
    ShaderSurfaceEvalDesc eval = {};
    uint64_t last_render_tick = 0;
    uint64_t output_epoch = 0;
    uint64_t pending_output_epoch = 0;
    uint64_t sampled_epochs[4] = {};
    uint64_t pending_sampled_epochs[4] = {};
    uint32_t generation = 1, seen_frame = 0, num_samplers = 0;
    float max_fps = 60;
    bool continuous_time = false, sampler_mirror_repeat = false, enable_feedback = false, owner_bound = false, initialized = false, dirty = true, recorded = false, swapped_this_frame = false;
};

struct Context {
    ShaderSurfaceSlot shader_surfaces[64] = {};
    const VisualLayerNode* active_render_layer = nullptr;
    float time_scale = 1;
    bool reduced_motion = false;
    A11yIndexEntry* a11y_index = nullptr; uint32_t a11y_index_cap = 0, a11y_index_count = 0;
    A11yBuild* a11y_first = nullptr; A11yBuild* a11y_last = nullptr; A11yBuild* a11y_parent = nullptr;
    A11yPatch* a11y_patches = nullptr; uint32_t a11y_count = 0;

    WindowUiState headless_ui = {};
    WindowUiState* active_ui = nullptr;
    SDL_Window* active_window = nullptr;
    SDL_Window* draw_list_window = nullptr;
    SDL_Window* mouse_capture_window = nullptr;
    SDL_Window* ime_active_window = nullptr;
    bool in_frame = false;
    ClipShapeGpu* clip_shapes = nullptr;
    uint32_t clip_shape_count = 1, clip_shape_capacity = 0, render_clip_index = 0, render_clip_depth = 1;
    bool clip_shape_sentinel_uploaded = false;
    SDL_GPUBuffer* clip_shape_gpu = nullptr; SDL_GPUTransferBuffer* clip_shape_transfer = nullptr;
    uint32_t clip_shape_gpu_capacity = 0, clip_shape_transfer_capacity = 0;
    BoxExtra* layout_draw_target = nullptr;
    float layout_draw_scale = 1;
    uint32_t wake_event_type = 0;
    SDL_Cursor* system_cursors[kCursorCount] = {};
    Cursor applied_cursor = Cursor::Arrow;

    uint64_t arena_epoch = 1u;
    Arena         frame_arena = {};
    FontAtlas     atlas = {};

    struct ShapeCacheEntry {
        uint64_t hash = 0;
        uint32_t text_off = 0;
        uint32_t text_len = 0;
        uint32_t glyph_off = 0;
        uint32_t glyph_count = 0;
        uint32_t font_revision = 0;
        uint16_t font_slot = 0;
        uint8_t  is_rtl = 0;
        uint8_t  used = 0;
    };
    struct ShapeCacheBank {
        ShapeCacheEntry* entries = nullptr;
        ShapedGlyph*     glyphs = nullptr;
        char*            text = nullptr;
        uint32_t         entry_count = 0;
        uint32_t         glyph_used = 0;
        uint32_t         text_used = 0;
    };
    struct ShapeCache {
        static constexpr uint32_t kEntryCap = 2048u;
        static constexpr uint32_t kGlyphCap = 32768u;
        static constexpr uint32_t kTextCap  = 131072u;
        ShapeCacheBank banks[2] = {};
        uint32_t       active_bank = 0;
        uint32_t       font_revision = 0;
        bool           initialized = false;
    };
    ShapeCache    shape_cache = {};

    Prim*         prims = nullptr;
    uint32_t      max_prims = 0;
    uint32_t      prim_count = 0;
    DrawBatch* batches = nullptr;
    uint32_t batch_cap = 0;
    uint32_t      batch_count = 0;
    BlurSplit* blur_splits = nullptr;
    uint32_t blur_split_cap = 0;
    uint32_t      blur_split_count = 0;
    Prim*         filter_prims = nullptr;
    uint32_t      filter_prim_cap = 0;
    uint32_t      filter_prim_count = 0;
    DrawBatch*    filter_batches = nullptr;
    uint32_t      filter_batch_cap = 0;
    uint32_t      filter_batch_count = 0;
    FilterBlurGroup* filter_groups = nullptr;
    uint32_t      filter_group_cap = 0;
    uint32_t      filter_group_count = 0;
    FilterBlurGroup* active_filter_group = nullptr;
    bool          layer_has_bg_prims = false;
    uint64_t      blur_dirty_grid[4] = {};
    bool          blur_dirty_valid = false;

    // Optimized spatially-reordered GPU submission buffers (keeps ctx->prims in logical emission order).
    Prim*         gpu_prims = nullptr;
    uint32_t*     gpu_prim_depths = nullptr;
    uint32_t      gpu_prim_cap = 0;
    DrawBatch*    gpu_batches = nullptr;
    uint32_t      gpu_batch_cap = 0;
    uint32_t      gpu_batch_count = 0;
    BlurSplit*    gpu_blur_splits = nullptr;
    uint32_t      gpu_blur_split_cap = 0;
    uint32_t      gpu_blur_split_count = 0;
    bool          gpu_use_early_z = false;
    bool          supports_d32_depth = true;

    // GPU objects
    char gpu_driver[32] = {};
    bool enable_gpu_debug = false;
    bool gpu_init_failed = false;
    SDL_GPUDevice*           device = nullptr;
    SDL_Window*              window = nullptr;
    ClaimedWindowSlot** claimed_windows = nullptr;
    uint32_t claimed_window_cap = 0;
    uint32_t max_layout_elements = 16384u;
    uint32_t                 claimed_window_count = 0;
    bool                     owns_video_init = false;
    bool                     headless = false;
    SDL_GPUShader*           vs_shader = nullptr;
    SDL_GPUShader*           ps_shader = nullptr;
    SDL_GPUShader*           quad_vs_shader = nullptr;
    SDL_GPUShader*           quad_ps_shader = nullptr;
    SDL_GPUShader*           subpixel_vs_shader = nullptr;
    SDL_GPUShader*           subpixel_ps_shader = nullptr;
    SDL_GPUShader*           kawase_vs_shader = nullptr;
    SDL_GPUShader*           kawase_ps_shader = nullptr;
    SDL_GPUSampler*          linear_sampler = nullptr;
    SDL_GPUSampler*          nearest_sampler = nullptr;
    SDL_GPUSampler*          mirror_linear_sampler = nullptr;
    SDL_GPUTexture*          surface_fallback_tex = nullptr;
    uint64_t                 shader_surface_epoch = 0;
    CachedPipeline           pipelines[kMaxCachedPipelines] = {};
    uint32_t                 pipeline_count = 0;
    CachedPipeline           uber_depth_pipelines[kMaxCachedPipelines] = {};
    uint32_t                 uber_depth_pipeline_count = 0;
    CachedPipeline           quad_opaque_pipelines[kMaxCachedPipelines] = {};
    uint32_t                 quad_opaque_pipeline_count = 0;
    CachedPipeline           quad_alpha_pipelines[kMaxCachedPipelines] = {};
    uint32_t                 quad_alpha_pipeline_count = 0;
    CachedPipeline           quad_alpha_depth_pipelines[kMaxCachedPipelines] = {};
    uint32_t                 quad_alpha_depth_pipeline_count = 0;
    CachedPipeline           subpixel_pipelines[kMaxCachedPipelines] = {};
    uint32_t                 subpixel_pipeline_count = 0;
    CachedPipeline           subpixel_depth_pipelines[kMaxCachedPipelines] = {};
    uint32_t                 subpixel_depth_pipeline_count = 0;
    CachedPipeline punch_pipelines[kMaxCachedPipelines] = {};
    uint32_t punch_pipeline_count = 0;
    CachedPipeline additive_pipelines[kMaxCachedPipelines] = {};
    uint32_t additive_pipeline_count = 0;
    CachedPipeline additive_depth_pipelines[kMaxCachedPipelines] = {};
    uint32_t additive_depth_pipeline_count = 0;
    SDL_GPUGraphicsPipeline* kawase_pipe = nullptr;
    SDL_GPUGraphicsPipeline* filter_kawase_pipe = nullptr;
    SDL_GPUGraphicsPipeline* bloom_pipe = nullptr;
    CachedPipeline           present_pipelines[kMaxCachedPipelines] = {};
    uint32_t                 present_pipeline_count = 0;
    uint32_t prim_gpu_cap = 0;
    uint32_t prim_tbuf_cap = 0;
    SDL_GPUBuffer*           prim_gpu_buf = nullptr;
    SDL_GPUTransferBuffer*   prim_transfer_buf = nullptr;
    SDL_GPUBuffer*           prim_depth_gpu_buf = nullptr;
    SDL_GPUTransferBuffer*   prim_depth_transfer_buf = nullptr;
    uint32_t                 prim_depth_gpu_cap = 0;
    uint32_t                 prim_depth_tbuf_cap = 0;
    SDL_GPUTransferBuffer*   atlas_transfer_buf = nullptr;
    SDL_GPUTransferBuffer*   subpixel_transfer_buf = nullptr;
    uint32_t                 subpixel_tbuf_size = 0;
    SDL_GPUCommandBuffer*    texture_upload_cmd = nullptr;
    SDL_GPUTransferBuffer*   texture_upload_buf = nullptr; uint32_t texture_upload_cap = 0;
    uint32_t atlas_tbuf_size = 0;
    SDL_GPUBuffer*           slug_curve_gpu_buf = nullptr;
    uint32_t                 slug_curve_gpu_cap = 0;
    SDL_GPUBuffer*           slug_band_gpu_buf = nullptr;
    uint32_t                 slug_band_gpu_cap = 0;
    SDL_GPUTransferBuffer*   slug_curve_tbuf = nullptr;
    uint32_t                 slug_curve_tbuf_cap = 0;
    SDL_GPUTransferBuffer*   slug_band_tbuf = nullptr;
    uint32_t                 slug_band_tbuf_cap = 0;

    // Reusable offscreen target & readback buffer for render_headless_png
    SDL_GPUTexture*          headless_tex = nullptr;
    uint32_t                 headless_w = 0;
    uint32_t                 headless_h = 0;
    SDL_GPUTransferBuffer*   headless_dl_buf = nullptr;
    uint32_t                 headless_dl_size = 0;

    char*         clipboard_fallback = nullptr;
    uint32_t      clipboard_fallback_cap = 0;
    struct DialogSharedQueue* dialog_queue = nullptr;

    uint64_t   layout_start_ticks = 0;
    SlugCurveGpu* dyn_slug_curves = nullptr;
    uint32_t dyn_slug_curve_count = 0;
    uint32_t dyn_slug_curve_cap = 0;
    SlugBandEntryGpu* dyn_slug_bands = nullptr;
    uint32_t dyn_slug_band_count = 0;
    uint32_t dyn_slug_band_cap = 0;
    SDL_GPUBuffer* dyn_slug_curve_gpu_buf = nullptr;
    uint32_t dyn_slug_curve_gpu_cap = 0;
    SDL_GPUTransferBuffer* dyn_slug_curve_tbuf = nullptr;
    uint32_t dyn_slug_curve_tbuf_cap = 0;
    SDL_GPUBuffer* dyn_slug_band_gpu_buf = nullptr;
    uint32_t dyn_slug_band_gpu_cap = 0;
    SDL_GPUTransferBuffer* dyn_slug_band_tbuf = nullptr;
    uint32_t dyn_slug_band_tbuf_cap = 0;
    FrameStats stats = {};
    static constexpr uint32_t kMaxThemeStack = 16u;
    Theme base_theme = {};
    Theme theme_stack[kMaxThemeStack] = {};
    uint32_t theme_depth = 0u;
    uint32_t theme_overflow = 0u;
    WindowRefreshFn refresh_fn = nullptr;
    void* refresh_user_data = nullptr;
    EtchCWindowRefreshFn c_refresh_fn = nullptr;
    void* c_refresh_user_data = nullptr;
    uint64_t last_refresh_ns = 0u;
    bool event_watch_attached = false;
    bool in_refresh_callback = false;
    bool enable_os_a11y = true;
};

namespace {

thread_local Context* g_current_context = nullptr;

WindowUiState* window_ui(Context* ctx, SDL_Window* window) noexcept {
    SDL_Window* target = window ? window : ctx->window;
    for (uint32_t i = 0; i < ctx->claimed_window_cap; ++i)
        if (target && ctx->claimed_windows[i] && ctx->claimed_windows[i]->window == target) return &ctx->claimed_windows[i]->ui;
    return window ? nullptr : &ctx->headless_ui;
}
const WindowUiState* window_ui(const Context* ctx, SDL_Window* window) noexcept {
    return window_ui(const_cast<Context*>(ctx), window);
}
bool ensure_batch_cap_array(DrawBatch** batches, uint32_t* cap, uint32_t count) noexcept {
    if (count < *cap) return true;
    if (*cap == 65536u) return false;
    const uint32_t next = *cap ? *cap * 2u : 256u;
    void* p = std::realloc(*batches, size_t(next) * sizeof(DrawBatch));
    if (!p) return false;
    *batches = static_cast<DrawBatch*>(p); *cap = next; return true;
}
bool ensure_batch_cap(Context* ctx) noexcept {
    return ensure_batch_cap_array(&ctx->batches, &ctx->batch_cap, ctx->batch_count);
}
bool reserve_span_hits(SpanHitRegion** hits, uint32_t* cap, uint32_t count, uint32_t initial) noexcept {
    if (count < *cap) return true;
    const uint32_t next = *cap ? *cap * 2u : initial;
    void* p = std::realloc(*hits, size_t(next) * sizeof(SpanHitRegion));
    if (!p) return false;
    *hits = static_cast<SpanHitRegion*>(p); *cap = next; return true;
}
bool reserve_focus(WindowUiState* ui) noexcept {
    if (ui->focus_count < ui->focus_cap) return true;
    const uint32_t cap = ui->focus_cap ? ui->focus_cap * 2u : 256u;
    // Keep both allocations valid even when the second reserve fails.
    void* p = std::realloc(ui->focus_order, size_t(cap) * sizeof(uint32_t));
    if (!p) return false;
    ui->focus_order = static_cast<uint32_t*>(p);
    p = std::realloc(ui->prev_focus_order, size_t(cap) * sizeof(uint32_t));
    if (!p) return false;
    ui->prev_focus_order = static_cast<uint32_t*>(p); ui->focus_cap = cap; return true;
}
bool claim_window_swapchain(Context* ctx, SDL_Window* win) noexcept;

bool focus_scope_contains(const WindowUiState& ui, uint32_t scope) noexcept {
    if (ui.focus_scope == scope) return true;
    for (uint32_t i = 0; i < ui.focus_scope_depth; ++i)
        if (ui.focus_scopes[i] == scope) return true;
    return false;
}

constexpr uint32_t hash_u32(uint32_t x) noexcept {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

constexpr uint64_t hash_u64(uint64_t x) noexcept {
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x;
}

inline uint32_t fnv1a_32_step(uint32_t h, const void* data, size_t len) noexcept {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

inline uint64_t fnv1a_64(const void* data, size_t len) noexcept {
    uint64_t h = 14695981039346656037ULL;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

inline uint32_t next_utf8_codepoint(const char*& p, const char* end) noexcept {
    if (p >= end) {
        return 0u;
    }
    const uint8_t b0 = static_cast<uint8_t>(*p++);
    if (b0 < 0x80u) {
        return b0;
    }
    if ((b0 & 0xE0u) == 0xC0u) {
        if (p < end) {
            const uint8_t b1 = static_cast<uint8_t>(*p);
            if ((b1 & 0xC0u) == 0x80u) {
                ++p;
                const uint32_t cp = (uint32_t(b0 & 0x1Fu) << 6) | uint32_t(b1 & 0x3Fu);
                return (cp >= 0x80u) ? cp : 0xFFFDu;
            }
        }
        return 0xFFFDu;
    }
    if ((b0 & 0xF0u) == 0xE0u) {
        if ((end - p) >= 2) {
            const uint8_t b1 = static_cast<uint8_t>(p[0]);
            const uint8_t b2 = static_cast<uint8_t>(p[1]);
            if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u) {
                p += 2;
                const uint32_t cp =
                    (uint32_t(b0 & 0x0Fu) << 12) | (uint32_t(b1 & 0x3Fu) << 6) | uint32_t(b2 & 0x3Fu);
                return (cp >= 0x800u && (cp < 0xD800u || cp > 0xDFFFu)) ? cp : 0xFFFDu;
            }
        }
        return 0xFFFDu;
    }
    if ((b0 & 0xF8u) == 0xF0u) {
        if ((end - p) >= 3) {
            const uint8_t b1 = static_cast<uint8_t>(p[0]);
            const uint8_t b2 = static_cast<uint8_t>(p[1]);
            const uint8_t b3 = static_cast<uint8_t>(p[2]);
            if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u && (b3 & 0xC0u) == 0x80u) {
                p += 3;
                const uint32_t cp = (uint32_t(b0 & 0x07u) << 18) | (uint32_t(b1 & 0x3Fu) << 12) |
                                    (uint32_t(b2 & 0x3Fu) << 6) | uint32_t(b3 & 0x3Fu);
                return (cp >= 0x10000u && cp <= 0x10FFFFu) ? cp : 0xFFFDu;
            }
        }
        return 0xFFFDu;
    }
    return 0xFFFDu;
}

inline uint32_t count_utf8_cps(const char* beg, uint32_t len) noexcept {
    const char* p = beg;
    const char* end = beg + len;
    uint32_t n = 0u;
    while (p < end) {
        (void)next_utf8_codepoint(p, end);
        ++n;
    }
    return n;
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

bool state_is_persistent(const WidgetState& ws) noexcept {
    return ws.is_scroll_x || ws.is_scroll_y || ws.is_virtual_list ||
           ws.split_initialized || ws.tree_initialized || ws.collapsible_initialized ||
           ws.edit_initialized || ws.table_initialized || ws.column_override ||
           ws.canvas_initialized || ws.sheet_initialized || ws.spring_initialized ||
           ws.presence_initialized || ws.terminal_initialized;
}

bool state_map_prune(
    StateMap* map,
    uint32_t frame_index,
    uint32_t keep_a,
    uint32_t keep_b,
    uint32_t keep_c,
    uint32_t keep_d,
    bool allow_shrink) noexcept
{
    if (!map || !map->entries || map->capacity == 0u || frame_index < 120u) {
        return false;
    }
    const uint32_t cutoff = frame_index - 120u;
    uint32_t live = 0u;
    uint32_t evictable = 0u;
    for (uint32_t i = 0u; i < map->capacity; ++i) {
        const WidgetState& ws = map->entries[i];
        if (ws.key == 0u) continue;
        const bool keep = ws.last_seen_frame > cutoff || state_is_persistent(ws) ||
            ws.pressed_down || ws.key == keep_a || ws.key == keep_b || ws.key == keep_c || ws.key == keep_d;
        if (keep) ++live; else ++evictable;
    }
    if (evictable == 0u || (evictable * 4u < map->count && !allow_shrink)) {
        return false;
    }
    uint32_t new_cap = map->capacity;
    if (allow_shrink) {
        while (new_cap > kDefaultStateCapacity && live * 8u < new_cap) {
            new_cap >>= 1u;
        }
    }
    if ((live + 1u) * 4u >= new_cap * 3u) {
        new_cap <<= 1u;
    }
    WidgetState* next = static_cast<WidgetState*>(std::calloc(new_cap, sizeof(WidgetState)));
    if (!next) {
        return false;
    }
    const uint32_t mask = new_cap - 1u;
    uint32_t written = 0u;
    for (uint32_t i = 0u; i < map->capacity; ++i) {
        const WidgetState& ws = map->entries[i];
        if (ws.key == 0u) continue;
        const bool keep = ws.last_seen_frame > cutoff || state_is_persistent(ws) ||
            ws.pressed_down || ws.key == keep_a || ws.key == keep_b || ws.key == keep_c || ws.key == keep_d;
        if (!keep) continue;
        uint32_t idx = hash_u32(ws.key) & mask;
        while (next[idx].key != 0u) idx = (idx + 1u) & mask;
        next[idx] = ws;
        ++written;
    }
    std::free(map->entries);
    map->entries = next;
    map->capacity = new_cap;
    map->count = written;
    return true;
}

WidgetState* state_map_get_or_insert(StateMap* map, uint32_t key, uint32_t frame_index) noexcept {
    if (!map || key == 0u) {
        return nullptr;
    }
    if (!map->entries && !state_map_init(map, kDefaultStateCapacity)) {
        return nullptr;
    }
    if ((map->count + 1u) * 4u >= map->capacity * 3u) {
        if (!state_map_prune(map, frame_index, 0u, 0u, 0u, 0u, false) ||
            (map->count + 1u) * 4u >= map->capacity * 3u)
        {
            if (!state_map_rehash(map, map->capacity * 2u)) {
                return nullptr;
            }
        }
    }
    const uint32_t mask = map->capacity - 1u;
    uint32_t idx = hash_u32(key) & mask;
    for (;;) {
        WidgetState& slot = map->entries[idx];
        if (slot.key == key) {
            if (slot.last_seen_frame != frame_index) {
                slot.prev_seen_frame = slot.last_seen_frame;
                slot.last_seen_frame = frame_index;
            }
            return &slot;
        }
        if (slot.key == 0u) {
            slot = WidgetState{
                .key = key,
                .last_seen_frame = frame_index,
                .prev_seen_frame = 0u,
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
            slot.right_clicked_this_frame = false;
            slot.double_clicked_this_frame = false;
            slot.span_hovered = false;
            slot.suppress_popup_keys = false;
            slot.accept_ghost_on_tab = false;
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
    if (ctx && ctx->active_ui->clay_ctx) {
        Clay_SetCurrentContext(ctx->active_ui->clay_ctx);
    }
}

Arena* frame_arena(Context* ctx) noexcept {
    return ctx ? &ctx->frame_arena : nullptr;
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

namespace etch { namespace {
void a11y_begin(Context* ctx) noexcept;
bool a11y_commit_snapshot(Context* ctx) noexcept;
void a11y_finish_actions(Context* ctx, bool snapshot_updated) noexcept;
A11yIndexEntry* a11y_index(Context* ctx, uint32_t id, bool insert = false) noexcept;
A11yBuild* a11y_emit(Context* ctx, uint32_t id, A11yRole role, Str label = {}, Str value = {}, uint32_t states = 0, uint32_t actions = 0) noexcept;
void a11y_semantic(uint32_t id, A11yRole role, Str label = {}, uint32_t states = 0, uint32_t actions = 0, float value = 0, float lo = 0, float hi = 0) noexcept;
bool a11y_take_any(uint32_t id, uint32_t mask, PendingA11yAction* out = nullptr) noexcept;
bool a11y_take(uint32_t id, A11yAction action, PendingA11yAction* out = nullptr) noexcept;
void a11y_toggle(uint32_t id, bool* open) noexcept;
void os_a11y_attach_window(Context* ctx, ClaimedWindowSlot* slot) noexcept;
void os_a11y_detach_window(Context* ctx, ClaimedWindowSlot* slot) noexcept;
void os_a11y_notify_commit(Context* ctx) noexcept;
void apply_platform_window_chrome(Context* ctx, ClaimedWindowSlot* slot) noexcept;
void apply_win32_chrome(Context* ctx, ClaimedWindowSlot* slot) noexcept;
void detach_win32_chrome(ClaimedWindowSlot* slot) noexcept;
bool win32_find_system_font_path(Str family_name, FontWeight weight, char* out_path, size_t out_cap) noexcept;
void sync_window_event_watch(Context* ctx) noexcept;
void detach_window_event_watch(Context* ctx) noexcept;
bool sync_window_metrics_from_os(WindowUiState* ui, SDL_Window* window) noexcept;
int32_t shader_surface_timeout(const Context* ctx, SDL_Window* window, bool all_windows, int32_t timeout) noexcept;
bool surface_texture_premultiplied(const Context* ctx, SDL_GPUTexture* texture) noexcept;
bool record_shader_surfaces(Context* ctx, SDL_GPUCommandBuffer* cmd) noexcept;
void complete_shader_surfaces(Context* ctx, SDL_Window* window, bool submitted) noexcept;
void detach_shader_surface_window(Context* ctx, SDL_Window* window) noexcept;
void release_shader_surfaces(Context* ctx) noexcept;
} }
