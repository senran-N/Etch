// Arena memory pool, cross-frame StateMap, and internal Context state definitions.
// Compiled via src/etch_unity.cpp.

#if defined(_WIN32)
#    include <malloc.h>
#else
#    include <cstdlib>
#endif

namespace etch {

inline constexpr uint32_t kAtlasSize = 16u;
inline constexpr uint32_t kSlugGlyphTableCapacity = 4096u;
inline constexpr uint32_t kMaxSlugBands = 8u;
inline constexpr uint32_t kInitialSlugCurveCap = 4096u;
inline constexpr uint32_t kInitialSlugBandCap = 8192u;
inline constexpr uint32_t kDefaultStateCapacity = 1024u;
inline constexpr uint32_t kMaxBatches = 256u;
inline constexpr uint32_t kMaxClipDepth = 64u;
inline constexpr uint32_t kMaxCachedPipelines = 4u;
inline constexpr uint32_t kMaxBlurSplits = 4u;
inline constexpr uint32_t kMaxSplitDepth = 32u;
inline constexpr uint32_t kMaxCanvasDepth = 8u;
inline constexpr uint32_t kHudHistorySamples = 64u;
inline constexpr uint32_t kMaxFocusablePerFrame = 256u;
inline constexpr uint32_t kMaxSpanHitsPerFrame = 256u;
inline constexpr uint32_t kCursorCount = 10u;
inline constexpr uint32_t kMaxTextOpsPerFrame = 32u;
inline constexpr uint32_t kMaxPendingTextBytes = 1024u;
inline constexpr uint32_t kMaxImeCompBytes = 256u;
inline constexpr uint32_t kMaxKeyEventsPerFrame = 32u;
inline constexpr uint32_t kMaxDroppedFiles = 16u;
inline constexpr uint32_t kMaxDroppedPathBytes = 2048u;
inline constexpr uint32_t kTextUndoSlots = 8u;
inline constexpr uint32_t kTextUndoMaxEntries = 32u;
inline constexpr uint32_t kTextUndoByteCap = 8192u;
inline constexpr uint32_t kMaxClaimedWindows = 8u;
inline constexpr uint32_t kMaxChromeDragRects = 16u;
inline constexpr uint32_t kMaxChromeClientRects = 128u;
inline constexpr uint32_t kMaxDragPayloadBytes = 64u;
inline constexpr uint32_t kMaxDragLabelBytes = 64u;

struct WidgetState {
    uint32_t key = 0; // 0 marks an empty hash slot
    uint32_t last_seen_frame = 0;
    uint32_t prev_seen_frame = 0;
    bool     is_user_element = false;
    bool     is_focusable = false;
    bool     pressed_down = false;
    bool     pressed_this_frame = false;
    bool     clicked_this_frame = false;
    bool     right_clicked_this_frame = false;
    bool     double_clicked_this_frame = false;
    bool     is_text_input_writable = false;
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

    bool tree_initialized = false;
    bool tree_open = false;
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

struct SlugBandEntryGpu {
    uint32_t x = 0; // band header: count; curve item: curve_idx
    uint32_t y = 0; // band header: rel_offset; curve item: 0
};
static_assert(sizeof(SlugBandEntryGpu) == 8);

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
    uint16_t pad0 = 0;
    float    x_advance_1px = 0.0f;
    float    x_offset_em = 0.0f;
    float    y_offset_em = 0.0f;
};

struct FontSlot {
    stbtt_fontinfo font_info = {};
    uint8_t*       font_file_data = nullptr;
    size_t         font_file_size = 0;
    int32_t        font_ascent = 0;
    int32_t        font_descent = 0;
    int32_t        font_line_gap = 0;
    float          scale_1px = 0.0f;
    float          ascent_em = 0.8f;
    float          descent_em = 0.2f;
    float          line_height_em = 1.2f;
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
};

struct VectorIconEntry {
    uint32_t band_offset = 0;
    uint16_t num_hbands = 0;
    uint16_t num_vbands = 0;
    Vec2     view_size = {24.0f, 24.0f};
    bool     loaded = false;
    bool     has_outline = false;
};

struct FontAtlas {
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
    bool              dirty = false;

    FontSlot          slots[kMaxFontSlots] = {};
    uint16_t          fallback_order[kMaxFontSlots] = {};
    uint32_t          fallback_count = 0;
    uint32_t          font_revision = 1;
    bool              font_valid = false;

    VectorIconEntry   icons[kMaxVectorIcons] = {};

    hb_buffer_t*      hb_buf = nullptr;
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

enum class CustomCmdKind : uint8_t {
    Box = 0,
    TextInput = 1,
    CanvasGrid = 2,
    CanvasWire = 3,
    Icon = 4,
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
};

struct DrawCmdNode {
    DrawCmdNode*  next = nullptr;
    DrawCmdKind   kind = DrawCmdKind::Rect;
    float         world_scale = 1.0f;
    RectDrawDesc  rect = {};
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
    ImageDrawDesc image_cmd = {};
};

struct ClaimedWindowSlot {
    SDL_Window* window = nullptr;
    bool        hit_test_attached = false;
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

struct BoxExtra {
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
    bool             is_virtual_list = false;
    bool             is_canvas = false;
};

struct SplitStackEntry {
    uint32_t id = 0;
    Axis     axis = Axis::Row;
    float    ratio = 0.5f;
    float    divider_size_px = 6.0f;
    float    min_first_px = 120.0f;
    float    min_second_px = 120.0f;
    Color    divider_color = {};
    Color    divider_hover_color = {};
    Color    divider_active_color = {};
    bool     in_second = false;
};

struct CanvasStackEntry {
    uint32_t    id = 0;
    CanvasDesc  desc = {};
    CanvasState state = {};
    bool        in_node = false;
};

struct ImageExtra {
    ImageDesc desc = {};
};

struct RichSpanInternal {
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
};

struct RichTextExtra {
    RichSpanInternal* spans = nullptr;
    uint32_t          span_count = 0;
    const char*       full_utf8 = nullptr;
    uint32_t          total_bytes = 0;
    float             line_height_px = 0.0f;
    float             max_ascent_px = 0.0f;
    float             max_descent_px = 0.0f;
    AlignX            align_x = AlignX::Left;
    uint32_t          config_hash = 0;
    uint32_t          clay_elem_id = 0;
};

struct SpanHitRegion {
    uint32_t id = 0; // 0 marks a floating overlay blocker rect
    uint32_t clay_elem_id = 0;
    Rect     rect = {};
    int16_t  z_index = 0;
    Rect     clip_rect = {};
    bool     has_clip = false;
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

struct BlurSplit {
    uint32_t split_batch = 0; // First batch in this glass layer (prior batches render into scene before blur)
    float    blur_radius_px = 0.0f;
    Rect     union_bounds = {};
};

struct KeyEventEntry {
    uint32_t key = 0;
    uint16_t mods = 0;
    bool     repeat = false;
};

struct TextUndoRecord {
    uint16_t offset = 0;
    uint16_t length = 0;
    uint32_t cursor = 0;
    uint32_t anchor = 0;
    uint32_t hash = 0;
};

struct TextUndoSlot {
    uint32_t       widget_id = 0;
    uint32_t       last_used_frame = 0;
    TextUndoRecord entries[kTextUndoMaxEntries] = {};
    uint32_t       head = 0;
    uint32_t       count = 0;
    uint32_t       cur = 0;
    uint16_t       write_pos = 0;
    uint16_t       bytes_used = 0;
    float          last_coalesce_sec = -10.0f;
    uint8_t        last_op_group = 0; // 0 = none/discrete, 1 = char burst, 2 = delete burst
    char           bytes[kTextUndoByteCap] = {};
};

inline SDL_GPUTexture* blur_tex_sentinel() noexcept {
    static uint8_t s_blur_tex_tag = 0;
    return reinterpret_cast<SDL_GPUTexture*>(&s_blur_tex_tag);
}


inline constexpr uint32_t kMaxDockSpaces = 4u;
inline constexpr uint32_t kMaxDockNodes = 32u;
inline constexpr uint32_t kMaxDockTabs = 32u;
inline constexpr uint32_t kMaxDockTraverseDepth = 32u;
inline constexpr uint32_t kMaxTreeDepth = 32u;
struct DockNode {
    bool used = false; uint32_t parent = 0; uint32_t first = 0; uint32_t second = 0;
    uint32_t split_id = 0; Axis axis = Axis::Row;
    float min_first = 120.0f; float min_second = 120.0f;
    uint32_t active = 0;
};
struct DockTabSlot {
    uint32_t id = 0; uint32_t leaf = 0; char title[48] = {};
    uint16_t icon_id = kInvalidIcon; bool closable = false; bool open = false;
};
struct DockSpaceSlot {
    uint32_t id = 0; uint32_t root = 0; uint32_t split_seq = 0;
    DockNode nodes[kMaxDockNodes] = {}; DockTabSlot tabs[kMaxDockTabs] = {};
};
struct DockTraverseEntry { uint32_t node_idx = 0; uint8_t phase = 0; };
struct DockCursor {
    DockSpaceSlot* space = nullptr; DockSpaceDesc desc = {};
    DockTraverseEntry stack[kMaxDockTraverseDepth] = {}; uint32_t depth = 0;
    bool pane_open = false; uint32_t move_pane = 0; uint32_t target = 0; DockZone zone = DockZone::None;
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
    float col_widths[kMaxTableColumns] = {}; uint32_t count = 0;
    uint32_t cursor_row = 0; uint32_t end_row = 0; uint32_t col_cursor = 0;
    bool row_open = false; bool cell_open = false;
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
    BlurSplit     blur_splits[kMaxBlurSplits] = {};
    uint32_t      blur_split_count = 0;
    bool          layer_has_bg_prims = false;

    // GPU objects
    SDL_GPUDevice*           device = nullptr;
    SDL_Window*              window = nullptr;
    ClaimedWindowSlot        claimed_windows[kMaxClaimedWindows] = {};
    uint32_t                 claimed_window_count = 0;
    bool                     owns_video_init = false;
    bool                     headless = false;
    SDL_GPUShader*           vs_shader = nullptr;
    SDL_GPUShader*           ps_shader = nullptr;
    SDL_GPUShader*           kawase_vs_shader = nullptr;
    SDL_GPUShader*           kawase_ps_shader = nullptr;
    SDL_GPUSampler*          linear_sampler = nullptr;
    CachedPipeline           pipelines[kMaxCachedPipelines] = {};
    uint32_t                 pipeline_count = 0;
    SDL_GPUGraphicsPipeline* kawase_pipe = nullptr;
    CachedPipeline           present_pipelines[kMaxCachedPipelines] = {};
    uint32_t                 present_pipeline_count = 0;
    SDL_GPUBuffer*           prim_gpu_buf = nullptr;
    SDL_GPUTransferBuffer*   prim_transfer_buf = nullptr;
    SDL_GPUTransferBuffer*   atlas_transfer_buf = nullptr;
    SDL_GPUBuffer*           slug_curve_gpu_buf = nullptr;
    uint32_t                 slug_curve_gpu_cap = 0;
    SDL_GPUBuffer*           slug_band_gpu_buf = nullptr;
    uint32_t                 slug_band_gpu_cap = 0;
    SDL_GPUTransferBuffer*   slug_curve_tbuf = nullptr;
    uint32_t                 slug_curve_tbuf_cap = 0;
    SDL_GPUTransferBuffer*   slug_band_tbuf = nullptr;
    uint32_t                 slug_band_tbuf_cap = 0;

    // Offscreen scene + dual-Kawase pyramid textures (R8G8B8A8_UNORM_SRGB)
    SDL_GPUTexture*          scene_tex = nullptr;
    SDL_GPUTexture*          blur_half_tex = nullptr;
    SDL_GPUTexture*          blur_quarter_tex = nullptr;
    uint32_t                 blur_rt_w = 0;
    uint32_t                 blur_rt_h = 0;

    // Reusable offscreen target & readback buffer for render_headless_png
    SDL_GPUTexture*          headless_tex = nullptr;
    uint32_t                 headless_w = 0;
    uint32_t                 headless_h = 0;
    SDL_GPUTransferBuffer*   headless_dl_buf = nullptr;
    uint32_t                 headless_dl_size = 0;

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
    bool       event_right_down = false;
    bool       event_middle_down = false;
    Vec2       event_scroll_delta = {};
    bool       prev_pointer_down = false;
    bool       prev_right_down = false;
    bool       prev_pan_button_down = false;
    Vec2       prev_pointer_pos = {-1.0f, -1.0f};
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

    DockSpaceSlot dock_spaces[kMaxDockSpaces] = {};
    DockCursor dock_cursor = {};
    TableMetadata tables[16] = {};
    TableStackEntry table_stack[8] = {};
    uint32_t table_depth = 0;
    uint32_t tree_depth = 0;
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

    uint16_t      pending_drop_offsets[kMaxDroppedFiles] = {};
    uint16_t      pending_drop_lengths[kMaxDroppedFiles] = {};
    uint32_t      pending_drop_count = 0;
    char          pending_drop_buf[kMaxDroppedPathBytes] = {};
    uint32_t      pending_drop_bytes = 0;
    Str           frame_drops[kMaxDroppedFiles] = {};
    uint32_t      frame_drop_count = 0;
    char          frame_drop_buf[kMaxDroppedPathBytes] = {};

    TextUndoSlot  undo_pool[kTextUndoSlots] = {};

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

    uint32_t      applied_font_revision = 0;
    SpanHitRegion span_hits[kMaxSpanHitsPerFrame] = {};
    uint32_t      span_hit_count = 0;

    // Frameless Custom Window Chrome (double-buffered for SDL_HitTest callback safety)
    WindowChromeDesc chrome_desc = {};
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
    uint8_t          drag_data[kMaxDragPayloadBytes] = {};
    uint32_t         drag_label_len = 0;
    char             drag_label[kMaxDragLabelBytes] = {};

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
