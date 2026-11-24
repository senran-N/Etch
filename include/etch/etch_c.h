/* Etch - Pure-GPU UI Engine (Pure C99 ABI Header) */
#ifndef ETCH_C_H
#define ETCH_C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef ETCH_VERSION_MAJOR
#define ETCH_VERSION_MAJOR 0
#define ETCH_VERSION_MINOR 29
#define ETCH_VERSION_PATCH 0
#define ETCH_VERSION_STR_DETAIL(x) #x
#define ETCH_VERSION_STR(x) ETCH_VERSION_STR_DETAIL(x)
#define ETCH_VERSION_STRING ETCH_VERSION_STR(ETCH_VERSION_MAJOR) "." ETCH_VERSION_STR(ETCH_VERSION_MINOR) "." ETCH_VERSION_STR(ETCH_VERSION_PATCH)
#endif

#ifndef ETCH_ABI_VERSION
#define ETCH_ABI_VERSION 1u
#endif

#ifndef ETCH_API
#  if defined(ETCH_SHARED)
#    if defined(_WIN32)
#      if defined(ETCH_BUILD_DLL)
#        define ETCH_API __declspec(dllexport)
#      else
#        define ETCH_API __declspec(dllimport)
#      endif
#    elif defined(__GNUC__) || defined(__clang__)
#      define ETCH_API __attribute__((visibility("default")))
#    else
#      define ETCH_API
#    endif
#  else
#    define ETCH_API
#  endif
#endif

#if defined(__cplusplus)
#define ETCH_C_ALIGN16 alignas(16)
#define ETCH_C_INLINE inline
#define ETCH_STR_LIT(lit) EtchStr{(lit), (uint32_t)(sizeof(lit) - 1u), true}
#elif defined(_MSC_VER)
#define ETCH_C_ALIGN16 __declspec(align(16))
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
#define ETCH_C_INLINE __inline
#else
#define ETCH_C_INLINE inline
#endif
#define ETCH_STR_LIT(lit) (EtchStr){(lit), (uint32_t)(sizeof(lit) - 1u), true}
#elif defined(__GNUC__) || defined(__clang__)
#define ETCH_C_ALIGN16 __attribute__((__aligned__(16)))
#define ETCH_C_INLINE inline
#define ETCH_STR_LIT(lit) (EtchStr){(lit), (uint32_t)(sizeof(lit) - 1u), true}
#else
#define ETCH_C_ALIGN16
#define ETCH_C_INLINE inline
#define ETCH_STR_LIT(lit) (EtchStr){(lit), (uint32_t)(sizeof(lit) - 1u), true}
#endif

/* Tag-only SDL3 forward declarations to coexist cleanly with <SDL3/SDL.h>. */
struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUTexture;
union SDL_Event;

#ifdef __cplusplus
extern "C" {
#endif

ETCH_API uint32_t etch_abi_version(void);

/* ============================================================================
 * Constants
 * ============================================================================ */

enum {
    ETCH_FONT_SANS = 0u,
    ETCH_FONT_BOLD = 1u,
    ETCH_FONT_MONO = 2u,
    ETCH_FONT_MEDIUM = 14u, ETCH_FONT_SEMIBOLD = 15u, ETCH_FONT_ITALIC = 16u,
    ETCH_MAX_FONT_SLOTS = 64u,
    ETCH_INVALID_FONT = 0xFFFFu,
    ETCH_MAX_VECTOR_ICONS = 1024u,
    ETCH_INVALID_ICON = 0xFFFFu,
    ETCH_INVALID_SPRITE = 0u,
    ETCH_MAX_TABLE_COLUMNS = 16u
};

typedef enum EtchKeyMod {
    ETCH_MOD_NONE  = 0u,
    ETCH_MOD_SHIFT = 1u << 0,
    ETCH_MOD_CTRL  = 1u << 1,
    ETCH_MOD_ALT   = 1u << 2,
    ETCH_MOD_SUPER = 1u << 3,
#if defined(__APPLE__)
    ETCH_MOD_PRIMARY = ETCH_MOD_SUPER
#else
    ETCH_MOD_PRIMARY = ETCH_MOD_CTRL
#endif
} EtchKeyMod;

typedef enum EtchPrimKind {
    ETCH_PRIM_RECT = 0,
    ETCH_PRIM_SHADOW = 1,
    ETCH_PRIM_GLYPH = 2,
    ETCH_PRIM_TEXTURE = 3,
    ETCH_PRIM_SLUG_GLYPH = 4,
    ETCH_PRIM_BLUR_GLASS = 5,
    ETCH_PRIM_CANVAS_GRID = 6,
    ETCH_PRIM_BEZIER_WIRE = 7,
    ETCH_PRIM_RING_ARC = 8,
    ETCH_PRIM_SLUG_FILL = 9,
    ETCH_PRIM_ROTATED_RECT = 10,
    ETCH_PRIM_MESH_GRAD = 11,
    ETCH_PRIM_BORDER_BEAM = 12,
    ETCH_PRIM_PUNCH_HOLE = 13,
    ETCH_PRIM_LAYER_BLUR = 14
} EtchPrimKind;

typedef enum EtchPrimFlags {
    ETCH_PRIM_FLAG_NONE          = 0,
    ETCH_PRIM_FLAG_LINEAR_GRAD   = 1 << 0,
    ETCH_PRIM_FLAG_RADIAL_GRAD   = 1 << 1,
    ETCH_PRIM_FLAG_ROUNDED_CLIP  = 1 << 2,
    ETCH_PRIM_FLAG_PREMULTIPLIED = 1 << 3,
    ETCH_PRIM_FLAG_STEM_DARKEN   = 1 << 4,
    ETCH_PRIM_FLAG_ROUND_CAP     = 1 << 5
} EtchPrimFlags;

typedef enum EtchA11yRole {
    ETCH_A11Y_NONE = 0,
    ETCH_A11Y_WINDOW,
    ETCH_A11Y_GROUP,
    ETCH_A11Y_REGION,
    ETCH_A11Y_TOOLBAR,
    ETCH_A11Y_MENU_BAR,
    ETCH_A11Y_MENU,
    ETCH_A11Y_MENU_ITEM,
    ETCH_A11Y_TAB_LIST,
    ETCH_A11Y_TAB,
    ETCH_A11Y_DIALOG,
    ETCH_A11Y_SCROLL_AREA,
    ETCH_A11Y_LIST,
    ETCH_A11Y_LIST_ITEM,
    ETCH_A11Y_TABLE,
    ETCH_A11Y_TREE,
    ETCH_A11Y_TREE_ITEM,
    ETCH_A11Y_STATIC_TEXT,
    ETCH_A11Y_BUTTON,
    ETCH_A11Y_CHECKBOX,
    ETCH_A11Y_SWITCH,
    ETCH_A11Y_SLIDER,
    ETCH_A11Y_SPIN_BUTTON,
    ETCH_A11Y_PROGRESS_BAR,
    ETCH_A11Y_COMBO_BOX,
    ETCH_A11Y_TEXT_INPUT,
    ETCH_A11Y_CODE_EDITOR,
    ETCH_A11Y_CANVAS,
    ETCH_A11Y_TOOLTIP,
    ETCH_A11Y_HEADING,
    ETCH_A11Y_LINK,
    ETCH_A11Y_DOCUMENT,
    ETCH_A11Y_MATH,
    ETCH_A11Y_FIGURE,
    ETCH_A11Y_ALERT,
    ETCH_A11Y_STATUS
} EtchA11yRole;

typedef enum EtchA11yStateFlags {
    ETCH_A11Y_STATE_NONE      = 0u,
    ETCH_A11Y_STATE_FOCUSED   = 1u << 0,
    ETCH_A11Y_STATE_HOVERED   = 1u << 1,
    ETCH_A11Y_STATE_PRESSED   = 1u << 2,
    ETCH_A11Y_STATE_CHECKED   = 1u << 3,
    ETCH_A11Y_STATE_SELECTED  = 1u << 4,
    ETCH_A11Y_STATE_EXPANDED  = 1u << 5,
    ETCH_A11Y_STATE_DISABLED  = 1u << 6,
    ETCH_A11Y_STATE_READ_ONLY = 1u << 7,
    ETCH_A11Y_STATE_MODAL     = 1u << 8,
    ETCH_A11Y_STATE_MULTILINE = 1u << 9,
    ETCH_A11Y_STATE_PASSWORD  = 1u << 10,
    ETCH_A11Y_STATE_LIVE_POLITE    = 1u << 11,
    ETCH_A11Y_STATE_LIVE_ASSERTIVE = 1u << 12,
    ETCH_A11Y_STATE_BUSY           = 1u << 13,
    ETCH_A11Y_STATE_INVALID        = 1u << 14
} EtchA11yStateFlags;

typedef enum EtchA11yLiveMode {
    ETCH_A11Y_LIVE_OFF       = 0,
    ETCH_A11Y_LIVE_POLITE    = 1,
    ETCH_A11Y_LIVE_ASSERTIVE = 2
} EtchA11yLiveMode;

typedef enum EtchA11yAction {
    ETCH_A11Y_ACT_FOCUS = 0,
    ETCH_A11Y_ACT_PRESS,
    ETCH_A11Y_ACT_INCREMENT,
    ETCH_A11Y_ACT_DECREMENT,
    ETCH_A11Y_ACT_EXPAND,
    ETCH_A11Y_ACT_COLLAPSE,
    ETCH_A11Y_ACT_DISMISS,
    ETCH_A11Y_ACT_SET_VALUE,
    ETCH_A11Y_ACT_SCROLL_UP,
    ETCH_A11Y_ACT_SCROLL_DOWN
} EtchA11yAction;

typedef enum EtchSystemTheme {
    ETCH_SYSTEM_THEME_UNKNOWN = 0,
    ETCH_SYSTEM_THEME_LIGHT   = 1,
    ETCH_SYSTEM_THEME_DARK    = 2
} EtchSystemTheme;

typedef enum EtchLayoutDirection {
    ETCH_LAYOUT_LTR = 0,
    ETCH_LAYOUT_RTL = 1
} EtchLayoutDirection;

typedef struct EtchSystemPreferences {
    EtchSystemTheme theme;
    bool high_contrast;
    bool reduced_motion;
    float text_scale;
    EtchLayoutDirection layout_direction;
    char locale[32];
} EtchSystemPreferences;

typedef enum EtchPointerKind {
    ETCH_POINTER_MOUSE = 0,
    ETCH_POINTER_PEN   = 1,
    ETCH_POINTER_TOUCH = 2
} EtchPointerKind;

typedef enum EtchPointerGesturePhase {
    ETCH_GESTURE_NONE   = 0,
    ETCH_GESTURE_BEGIN  = 1,
    ETCH_GESTURE_UPDATE = 2,
    ETCH_GESTURE_END    = 3,
    ETCH_GESTURE_CANCEL = 4
} EtchPointerGesturePhase;

typedef struct EtchPointerState {
    EtchPointerKind kind;
    uint32_t pointer_id;
    uint32_t active_touches;
    float pressure;
    float tilt_x;
    float tilt_y;
    float pinch_delta;
    bool precise_scroll;
    bool pen_eraser;
} EtchPointerState;

typedef enum EtchThemeId {
    ETCH_THEME_DARK          = 0,
    ETCH_THEME_LIGHT         = 1,
    ETCH_THEME_HIGH_CONTRAST = 2,
    ETCH_THEME_INDUSTRIAL    = 3,
    ETCH_THEME_CUSTOM        = 4
} EtchThemeId;

typedef enum EtchSizingKind {
    ETCH_SIZING_FIT     = 0,
    ETCH_SIZING_GROW    = 1,
    ETCH_SIZING_FIXED   = 2,
    ETCH_SIZING_PERCENT = 3
} EtchSizingKind;

typedef enum EtchAxis {
    ETCH_AXIS_ROW = 0,
    ETCH_AXIS_COL = 1
} EtchAxis;

typedef enum EtchAlignX {
    ETCH_ALIGN_X_LEFT   = 0,
    ETCH_ALIGN_X_CENTER = 1,
    ETCH_ALIGN_X_RIGHT  = 2
} EtchAlignX;

typedef enum EtchAlignY {
    ETCH_ALIGN_Y_TOP    = 0,
    ETCH_ALIGN_Y_CENTER = 1,
    ETCH_ALIGN_Y_BOTTOM = 2
} EtchAlignY;

typedef enum EtchJustify {
    ETCH_JUSTIFY_START         = 0,
    ETCH_JUSTIFY_CENTER        = 1,
    ETCH_JUSTIFY_END           = 2,
    ETCH_JUSTIFY_SPACE_BETWEEN = 3,
    ETCH_JUSTIFY_SPACE_AROUND  = 4,
    ETCH_JUSTIFY_SPACE_EVENLY  = 5
} EtchJustify;

typedef enum EtchAlignSelf {
    ETCH_ALIGN_SELF_AUTO    = 0,
    ETCH_ALIGN_SELF_START   = 1,
    ETCH_ALIGN_SELF_CENTER  = 2,
    ETCH_ALIGN_SELF_END     = 3,
    ETCH_ALIGN_SELF_STRETCH = 4
} EtchAlignSelf;

typedef enum EtchHitboxBehavior {
    ETCH_HITBOX_NORMAL      = 0,
    ETCH_HITBOX_BLOCK_MOUSE = 1,
    ETCH_HITBOX_PASSTHROUGH = 2
} EtchHitboxBehavior;

typedef enum EtchCursor {
    ETCH_CURSOR_ARROW = 0,
    ETCH_CURSOR_HAND,
    ETCH_CURSOR_IBEAM,
    ETCH_CURSOR_RESIZE_EW,
    ETCH_CURSOR_RESIZE_NS,
    ETCH_CURSOR_RESIZE_NWSE,
    ETCH_CURSOR_RESIZE_NESW,
    ETCH_CURSOR_MOVE,
    ETCH_CURSOR_NOT_ALLOWED,
    ETCH_CURSOR_CROSSHAIR
} EtchCursor;

typedef enum EtchAnchor {
    ETCH_ANCHOR_TOP_LEFT = 0,
    ETCH_ANCHOR_TOP_CENTER,
    ETCH_ANCHOR_TOP_RIGHT,
    ETCH_ANCHOR_CENTER_LEFT,
    ETCH_ANCHOR_CENTER,
    ETCH_ANCHOR_CENTER_RIGHT,
    ETCH_ANCHOR_BOTTOM_LEFT,
    ETCH_ANCHOR_BOTTOM_CENTER,
    ETCH_ANCHOR_BOTTOM_RIGHT
} EtchAnchor;

typedef enum EtchAttachTo {
    ETCH_ATTACH_NONE = 0,
    ETCH_ATTACH_PARENT,
    ETCH_ATTACH_ELEMENT,
    ETCH_ATTACH_ROOT
} EtchAttachTo;

typedef enum EtchPathVerb {
    ETCH_PATH_MOVE_TO  = 0,
    ETCH_PATH_LINE_TO  = 1,
    ETCH_PATH_QUAD_TO  = 2,
    ETCH_PATH_CUBIC_TO = 3,
    ETCH_PATH_CLOSE    = 4
} EtchPathVerb;

typedef enum EtchWindowHit {
    ETCH_WIN_HIT_NORMAL = 0,
    ETCH_WIN_HIT_CLIENT = 1,
    ETCH_WIN_HIT_CAPTION = 2,
    ETCH_WIN_HIT_RESIZE_TOP_LEFT = 3,
    ETCH_WIN_HIT_RESIZE_TOP = 4,
    ETCH_WIN_HIT_RESIZE_TOP_RIGHT = 5,
    ETCH_WIN_HIT_RESIZE_RIGHT = 6,
    ETCH_WIN_HIT_RESIZE_BOTTOM_RIGHT = 7,
    ETCH_WIN_HIT_RESIZE_BOTTOM = 8,
    ETCH_WIN_HIT_RESIZE_BOTTOM_LEFT = 9,
    ETCH_WIN_HIT_RESIZE_LEFT = 10,
    ETCH_WIN_HIT_MAXIMIZE = 11
} EtchWindowHit;

typedef enum EtchButtonVariant {
    ETCH_BUTTON_DEFAULT = 0,
    ETCH_BUTTON_PRIMARY = 1,
    ETCH_BUTTON_GHOST   = 2,
    ETCH_BUTTON_DANGER  = 3
} EtchButtonVariant;

typedef enum EtchDockZone {
    ETCH_DOCK_NONE   = 0,
    ETCH_DOCK_CENTER = 1,
    ETCH_DOCK_LEFT   = 2,
    ETCH_DOCK_RIGHT  = 3,
    ETCH_DOCK_TOP    = 4,
    ETCH_DOCK_BOTTOM = 5
} EtchDockZone;

typedef enum EtchTableColumnSizing {
    ETCH_TABLE_COL_FIXED   = 0,
    ETCH_TABLE_COL_STRETCH = 1
} EtchTableColumnSizing;

/* ============================================================================
 * Opaque Handles & Callbacks
 * ============================================================================ */

typedef struct EtchContext EtchContext;
typedef struct EtchTextBuffer EtchTextBuffer;

typedef void (*EtchWindowRefreshFn)(EtchContext* ctx, struct SDL_Window* window, float dt_seconds, void* user_data);
typedef float (*EtchVirtualItemHeightFn)(uint32_t index, void* user_data);

/* ============================================================================
 * Fundamental POD Structs & GPU Contracts
 * ============================================================================ */

typedef struct EtchVec2 {
    float x;
    float y;
} EtchVec2;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

typedef struct ETCH_C_ALIGN16 EtchVec4 {
    float x;
    float y;
    float z;
    float w;
} EtchVec4;

typedef uint32_t EtchShaderSurfaceId;
typedef struct EtchShaderSurfaceCode {
    const void* data;
    size_t size;
    const char* entrypoint;
} EtchShaderSurfaceCode;
typedef struct EtchShaderSurfaceCreateDesc {
    uint32_t struct_size;
    EtchShaderSurfaceCode spirv, dxil, msl;
    bool continuous_time;
    float max_fps;
    uint32_t num_samplers;
    bool sampler_mirror_repeat;
    bool enable_feedback;
} EtchShaderSurfaceCreateDesc;
typedef struct EtchShaderSurfaceEvalDesc {
    uint32_t struct_size;
    uint32_t width, height;
    uint64_t revision;
    EtchVec4 params[4];
    struct SDL_GPUTexture* textures[4];
    bool enabled;
} EtchShaderSurfaceEvalDesc;
ETCH_API EtchShaderSurfaceCreateDesc etch_shader_surface_create_desc_default(void);
ETCH_API EtchShaderSurfaceEvalDesc etch_shader_surface_eval_desc_default(void);
ETCH_API EtchShaderSurfaceId etch_create_shader_surface(EtchContext*, const EtchShaderSurfaceCreateDesc*);
ETCH_API void etch_destroy_shader_surface(EtchContext*, EtchShaderSurfaceId);
ETCH_API struct SDL_GPUTexture* etch_eval_shader_surface(EtchContext*, EtchShaderSurfaceId, const EtchShaderSurfaceEvalDesc*);
typedef struct ETCH_C_ALIGN16 EtchUint4 {
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint32_t w;
} EtchUint4;

typedef struct EtchRect {
    float x;
    float y;
    float w;
    float h;
} EtchRect;

typedef struct EtchColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} EtchColor;

typedef struct EtchCorners {
    float tl;
    float tr;
    float br;
    float bl;
} EtchCorners;

typedef struct EtchPadding {
    float left;
    float right;
    float top;
    float bottom;
} EtchPadding;

typedef struct EtchStr {
    const char* chars;
    uint32_t length;
    bool is_static;
} EtchStr;

typedef struct EtchArena {
    uint8_t* base;
    size_t capacity;
    size_t offset;
    size_t peak_offset;
} EtchArena;

typedef struct ETCH_C_ALIGN16 EtchPrim {
    EtchVec4 bounds;
    EtchVec4 clip;
    EtchUint4 payload;
    EtchUint4 style;
} EtchPrim;

typedef struct ETCH_C_ALIGN16 EtchGlobalsUniform {
    EtchVec2 viewport_size;
    EtchVec2 inv_viewport_size;
    float dpi_scale;
    float time_seconds;
    uint32_t frame_index;
    uint32_t flags;
} EtchGlobalsUniform;

#ifdef _MSC_VER
#pragma warning(pop)
#endif

typedef char etch_c_chk_vec4[(sizeof(EtchVec4) == 16) ? 1 : -1];
typedef char etch_c_chk_uint4[(sizeof(EtchUint4) == 16) ? 1 : -1];
typedef char etch_c_chk_prim[(sizeof(EtchPrim) == 64) ? 1 : -1];
typedef char etch_c_chk_globals[(sizeof(EtchGlobalsUniform) == 32) ? 1 : -1];

/* ============================================================================
 * Inline Helpers (C99 / C++20 Compatible)
 * ============================================================================ */

static ETCH_C_INLINE uint32_t etch_cstr_len(const char* s) {
    uint32_t n = 0u;
    if (s) {
        while (s[n] != '\0') {
            ++n;
        }
    }
    return n;
}

static ETCH_C_INLINE EtchStr etch_str_cstr(const char* s) {
    EtchStr r;
    r.chars = s;
    r.length = etch_cstr_len(s);
    r.is_static = false;
    return r;
}

static ETCH_C_INLINE EtchStr etch_str_n(const char* s, uint32_t len, bool is_static) {
    EtchStr r;
    r.chars = s;
    r.length = len;
    r.is_static = is_static;
    return r;
}

static ETCH_C_INLINE uint32_t etch_id_seed(const char* label, uint32_t seed) {
    uint32_t hash = seed;
    if (!label) {
        return hash ? hash : 1u;
    }
    for (size_t i = 0u; label[i] != '\0'; ++i) {
        hash ^= (uint8_t)label[i];
        hash *= 0x01000193u;
    }
    return hash ? hash : 1u;
}

static ETCH_C_INLINE uint32_t etch_id(const char* label) {
    return etch_id_seed(label, 0x811C9DC5u);
}

static ETCH_C_INLINE uint32_t etch_id_index(uint32_t base_id, uint32_t index) {
    uint32_t hash = base_id;
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        hash ^= (index >> shift) & 0xFFu;
        hash *= 0x01000193u;
    }
    return hash ? hash : 1u;
}

static ETCH_C_INLINE EtchVec2 etch_vec2(float x, float y) {
    EtchVec2 v = {x, y};
    return v;
}

static ETCH_C_INLINE EtchRect etch_rect(float x, float y, float w, float h) {
    EtchRect r = {x, y, w, h};
    return r;
}

static ETCH_C_INLINE EtchColor etch_rgba8(uint8_t r8, uint8_t g8, uint8_t b8, uint8_t a8) {
    EtchColor c = {r8, g8, b8, a8};
    return c;
}

static ETCH_C_INLINE EtchColor etch_rgba(float fr, float fg, float fb, float fa) {
    float cr = (fr < 0.0f) ? 0.0f : ((fr > 1.0f) ? 1.0f : fr);
    float cg = (fg < 0.0f) ? 0.0f : ((fg > 1.0f) ? 1.0f : fg);
    float cb = (fb < 0.0f) ? 0.0f : ((fb > 1.0f) ? 1.0f : fb);
    float ca = (fa < 0.0f) ? 0.0f : ((fa > 1.0f) ? 1.0f : fa);
    EtchColor c = {
        (uint8_t)(cr * 255.0f + 0.5f),
        (uint8_t)(cg * 255.0f + 0.5f),
        (uint8_t)(cb * 255.0f + 0.5f),
        (uint8_t)(ca * 255.0f + 0.5f)
    };
    return c;
}

static ETCH_C_INLINE EtchColor etch_rgb_hex(uint32_t rgb, uint8_t alpha) {
    EtchColor c = {
        (uint8_t)((rgb >> 16) & 0xFFu),
        (uint8_t)((rgb >> 8) & 0xFFu),
        (uint8_t)(rgb & 0xFFu),
        alpha
    };
    return c;
}

static ETCH_C_INLINE EtchColor etch_hex(uint32_t rgba_hex) {
    EtchColor c = {
        (uint8_t)((rgba_hex >> 24) & 0xFFu),
        (uint8_t)((rgba_hex >> 16) & 0xFFu),
        (uint8_t)((rgba_hex >> 8) & 0xFFu),
        (uint8_t)(rgba_hex & 0xFFu)
    };
    return c;
}

static ETCH_C_INLINE EtchColor etch_color_transparent(void) {
    return etch_rgba8(0u, 0u, 0u, 0u);
}

static ETCH_C_INLINE EtchColor etch_color_theme_auto(void) {
    return etch_rgba8(0xFEu, 0x00u, 0xFEu, 0x00u);
}

static ETCH_C_INLINE EtchColor etch_color_white(uint8_t alpha) {
    return etch_rgba8(255u, 255u, 255u, alpha);
}

static ETCH_C_INLINE EtchColor etch_color_black(uint8_t alpha) {
    return etch_rgba8(0u, 0u, 0u, alpha);
}

static ETCH_C_INLINE bool etch_color_is_theme_auto(EtchColor c) {
    return c.r == 0xFEu && c.g == 0x00u && c.b == 0xFEu && c.a == 0x00u;
}

static ETCH_C_INLINE uint32_t etch_color_pack_rgba8(EtchColor c) {
    return (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16) | ((uint32_t)c.a << 24);
}

static ETCH_C_INLINE EtchColor etch_resolve_theme_color(EtchColor explicit_col, EtchColor token_col) {
    return etch_color_is_theme_auto(explicit_col) ? token_col : explicit_col;
}

static ETCH_C_INLINE EtchCorners etch_corners_all(float r) {
    EtchCorners c = {r, r, r, r};
    return c;
}

static ETCH_C_INLINE EtchCorners etch_corners_theme_auto(void) {
    EtchCorners c = {-1.0f, -1.0f, -1.0f, -1.0f};
    return c;
}

static ETCH_C_INLINE bool etch_corners_is_theme_auto(EtchCorners c) {
    return c.tl < 0.0f && c.tr < 0.0f && c.br < 0.0f && c.bl < 0.0f;
}

static ETCH_C_INLINE EtchCorners etch_resolve_theme_corners(EtchCorners explicit_rad, EtchCorners token_rad) {
    return etch_corners_is_theme_auto(explicit_rad) ? token_rad : explicit_rad;
}

static ETCH_C_INLINE EtchPadding etch_padding_all(float p) {
    EtchPadding pad = {p, p, p, p};
    return pad;
}

static ETCH_C_INLINE EtchPadding etch_padding_xy(float horizontal, float vertical) {
    EtchPadding pad = {horizontal, horizontal, vertical, vertical};
    return pad;
}

static ETCH_C_INLINE uint32_t etch_a11y_action_bit(EtchA11yAction a) {
    return 1u << (uint32_t)a;
}

/* ============================================================================
 * Descriptors & Result Structs
 * ============================================================================ */

typedef struct EtchSizing {
    EtchSizingKind kind;
    float value;
    float min;
    float max;
} EtchSizing;

static ETCH_C_INLINE EtchSizing etch_sizing_fit(float min_px, float max_px) {
    EtchSizing s = {ETCH_SIZING_FIT, 0.0f, min_px, max_px};
    return s;
}

static ETCH_C_INLINE EtchSizing etch_sizing_grow(float min_px, float max_px) {
    EtchSizing s = {ETCH_SIZING_GROW, 0.0f, min_px, max_px};
    return s;
}

static ETCH_C_INLINE EtchSizing etch_sizing_fixed(float px) {
    EtchSizing s = {ETCH_SIZING_FIXED, px, px, px};
    return s;
}

static ETCH_C_INLINE EtchSizing etch_sizing_percent(float ratio_0_to_1) {
    EtchSizing s = {ETCH_SIZING_PERCENT, ratio_0_to_1, 0.0f, 0.0f};
    return s;
}

static ETCH_C_INLINE EtchColor etch_color_blend_over(EtchColor under,EtchColor over) {
    const float a=over.a/255.0f;
    const float out=a+under.a/255.0f*(1.0f-a);
    if(out==0)return etch_rgba8(0,0,0,0);
    const float t=a/out;
    return etch_rgba8((uint8_t)(under.r+(over.r-under.r)*t+0.5f),
        (uint8_t)(under.g+(over.g-under.g)*t+0.5f),(uint8_t)(under.b+(over.b-under.b)*t+0.5f),(uint8_t)(out*255+0.5f));
}

typedef struct EtchGridTrack {
    EtchSizingKind kind;
    float value;
    float min_px;
    float max_px;
} EtchGridTrack;

static ETCH_C_INLINE EtchGridTrack etch_grid_px(float v) {
    EtchGridTrack t = {ETCH_SIZING_FIXED, v, v, v};
    return t;
}

static ETCH_C_INLINE EtchGridTrack etch_grid_fr(float v, float lo, float hi) {
    EtchGridTrack t = {ETCH_SIZING_GROW, v, lo, hi};
    return t;
}

static ETCH_C_INLINE EtchGridTrack etch_grid_auto_fit(float lo, float hi) {
    EtchGridTrack t = {ETCH_SIZING_FIT, 0.0f, lo, hi};
    return t;
}

static ETCH_C_INLINE EtchGridTrack etch_grid_percent(float v, float lo, float hi) {
    EtchGridTrack t = {ETCH_SIZING_PERCENT, v, lo, hi};
    return t;
}

typedef struct EtchSpringSpec {
    float response;
    float damping_ratio;
} EtchSpringSpec;

typedef struct EtchA11yNode {
    uint32_t id;
    uint32_t parent_id;
    uint32_t first_child_idx;
    uint32_t next_sibling_idx;
    EtchA11yRole role;
    uint32_t state_flags;
    uint32_t actions_mask;
    EtchRect bounds;
    EtchStr label;
    EtchStr value_text;
    float value_num;
    float min_value;
    float max_value;
} EtchA11yNode;

typedef struct EtchA11yOverrideDesc {
    uint32_t element_id;
    EtchA11yRole role;
    EtchStr label;
    EtchStr value_text;
    uint32_t add_states;
    uint32_t clear_states;
    uint32_t actions_mask;
    bool has_role;
    bool has_label;
    bool has_value_text;
    bool has_actions_mask;
} EtchA11yOverrideDesc;

typedef struct EtchContextDesc {
    uint32_t struct_size;
    uint32_t max_layout_elements;
    struct SDL_Window* window;
    size_t frame_arena_bytes;
    uint32_t max_prims_per_frame;
    const char* font_path;
    float font_size_px;
    bool enable_gpu_debug;
    bool headless;
    const char* gpu_driver;
    EtchWindowRefreshFn on_window_refresh;
    void* refresh_user_data;
    bool enable_os_a11y;
    bool respect_reduced_motion;
    size_t reserved_v025_pad;
    bool prefer_high_performance_gpu;
    bool lcd_subpixel_text;
} EtchContextDesc;

typedef struct EtchTheme {
    EtchThemeId id;
    EtchColor bg_canvas;
    EtchColor bg_surface;
    EtchColor bg_elevated;
    EtchColor bg_chrome;
    EtchColor bg_popup;
    EtchColor bg_tooltip;
    EtchColor bg_input;
    EtchColor bg_table;
    EtchColor bg_node_canvas;
    EtchColor surface_control;
    EtchColor surface_hover;
    EtchColor surface_active;
    EtchColor surface_disabled;
    EtchColor surface_subtle;
    EtchColor surface_subtle_hover;
    EtchColor surface_subtle_body;
    EtchColor surface_selected;
    EtchColor text_primary;
    EtchColor text_secondary;
    EtchColor text_body;
    EtchColor text_code;
    EtchColor text_muted;
    EtchColor text_disabled;
    EtchColor accent;
    EtchColor accent_secondary;
    EtchColor accent_primary;
    EtchColor focus_ring;
    EtchColor danger;
    EtchColor warning;
    EtchColor border_subtle;
    EtchColor border_line;
    EtchColor border_input;
    EtchColor border_node;
    EtchColor border_control;
    EtchColor border_tooltip;
    EtchColor selection;
    EtchColor selection_hover;
    EtchColor zebra_row;
    EtchColor active_line;
    EtchColor area_fill_top;
    EtchColor area_fill_bottom;
    EtchColor divider_hover;
    EtchColor divider_thumb_hover;
    EtchColor grid_minor;
    EtchColor grid_major;
    EtchColor canvas_border;
    EtchColor marquee_fill;
    EtchColor marquee_border;
    EtchColor backdrop;
    EtchColor shadow;
    EtchColor scrollbar_track;
    EtchColor scrollbar_thumb;
    EtchColor scrollbar_thumb_hover;
    EtchColor scrollbar_thumb_drag;
    EtchColor syntax_comment;
    EtchColor syntax_string;
    EtchColor syntax_number;
    EtchColor syntax_keyword;
    float radius_small_px;
    float radius_control_px;
    float radius_table_px;
    float radius_input_px;
    float radius_panel_px;
    float radius_pill_px;
    EtchColor on_primary;
    EtchColor on_error;
} EtchTheme;

typedef struct EtchFrameDesc {
    uint32_t struct_size;
    uint32_t viewport_w;
    uint32_t viewport_h;
    float dpi_scale;
    float dt_seconds;
    EtchVec2 pointer_pos;
    bool pointer_down;
    EtchVec2 scroll_delta;
    bool right_pointer_down;
    uint8_t pointer_clicks;
    uint16_t key_mods;
    uint32_t key_pressed_code;
    bool pixel_snap;
    float bloom_intensity, bloom_threshold;
} EtchFrameDesc;

typedef struct EtchWindowFrameDesc {
    uint32_t struct_size;
    float dt;
    float min_scale;
    bool pixel_snap;
    float bloom_intensity, bloom_threshold;
} EtchWindowFrameDesc;

typedef struct EtchFrameStats {
    uint32_t a11y_node_count;
    uint32_t prim_count;
    uint32_t prim_capacity;
    uint32_t draw_call_count;
    uint32_t blur_pass_count;
    uint32_t slug_glyph_count;
    uint32_t slug_curve_count;
    uint32_t dynamic_slug_curve_count;
    size_t arena_used_bytes;
    size_t arena_peak_bytes;
    double layout_ms;
    double gpu_submit_ms;
    uint32_t active_animations;
    bool idle_sleeping;
    int32_t ime_cursor_offset_win;
    uint32_t sprite_count;
    uint32_t sprite_atlas_size;
    uint32_t cached_subtree_hits;
} EtchFrameStats;

typedef enum EtchBorderAlign { ETCH_BORDER_INSIDE=0, ETCH_BORDER_CENTER=1, ETCH_BORDER_OUTSIDE=2 } EtchBorderAlign;

typedef enum EtchElevation {
    ETCH_ELEVATION_NONE = 0,
    ETCH_ELEVATION_SM = 1,
    ETCH_ELEVATION_MD = 2,
    ETCH_ELEVATION_LG = 3,
    ETCH_ELEVATION_XL = 4
} EtchElevation;

typedef struct EtchShadowLayer {
    EtchVec2 offset;
    float blur_px;
    float spread_px;
    EtchColor color;
    bool inset;
} EtchShadowLayer;
ETCH_API EtchShadowLayer etch_shadow_layer_default(void);

typedef enum EtchGradientKind {
    ETCH_GRADIENT_NONE = 0,
    ETCH_GRADIENT_LINEAR = 1,
    ETCH_GRADIENT_RADIAL = 2,
    ETCH_GRADIENT_CONIC = 3
} EtchGradientKind;

typedef struct EtchGradientStop {
    EtchColor color;
    float position;
} EtchGradientStop;

typedef struct EtchGradientDesc {
    EtchGradientKind kind;
    bool oklab;
    EtchVec2 dir;
    EtchVec2 center;
    float angle_rad;
    uint8_t stop_count;
    EtchGradientStop stops[8];
} EtchGradientDesc;
ETCH_API EtchGradientDesc etch_gradient_desc_default(void);

typedef enum EtchImageFit {
    ETCH_IMAGE_FIT_FILL = 0,
    ETCH_IMAGE_FIT_CONTAIN = 1,
    ETCH_IMAGE_FIT_COVER = 2
} EtchImageFit;

typedef struct SDL_GPUTexture* EtchTextureHandle;

typedef enum EtchBlendMode {
    ETCH_BLEND_NORMAL   = 0,
    ETCH_BLEND_ADDITIVE = 1,
    ETCH_BLEND_MULTIPLY = 2,
    ETCH_BLEND_SCREEN   = 3
} EtchBlendMode;

typedef enum EtchAlphaMaskKind {
    ETCH_ALPHA_MASK_NONE             = 0,
    ETCH_ALPHA_MASK_ROUNDED          = 1,
    ETCH_ALPHA_MASK_LINEAR_GRADIENT  = 2,
    ETCH_ALPHA_MASK_RADIAL_SPOTLIGHT = 3,
    ETCH_ALPHA_MASK_TEXTURE          = 4
} EtchAlphaMaskKind;

typedef struct EtchAlphaMaskDesc {
    EtchAlphaMaskKind kind;
    bool invert;
    float angle_deg;
    float start_t;
    float end_t;
    float alpha_start;
    float alpha_end;
    EtchVec2 center_norm;
    EtchVec2 radius_norm;
    EtchTextureHandle texture;
    EtchRect uv;
    EtchCorners radius;
    bool continuous_corners;
} EtchAlphaMaskDesc;
ETCH_API EtchAlphaMaskDesc etch_alpha_mask_desc_default(void);

#define ETCH_OT_TAG(a, b, c, d) \
    (((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) | \
     ((uint32_t)(uint8_t)(c) << 8) | (uint32_t)(uint8_t)(d))

#define ETCH_AXIS_WGHT ETCH_OT_TAG('w', 'g', 'h', 't')
#define ETCH_AXIS_WDTH ETCH_OT_TAG('w', 'd', 't', 'h')
#define ETCH_AXIS_OPSZ ETCH_OT_TAG('o', 'p', 's', 'z')
#define ETCH_AXIS_SLNT ETCH_OT_TAG('s', 'l', 'n', 't')
#define ETCH_AXIS_ITAL ETCH_OT_TAG('i', 't', 'a', 'l')

typedef enum EtchOpenTypeFlags {
    ETCH_OT_NONE            = 0u,
    ETCH_OT_NO_LIGA         = 1u << 0,
    ETCH_OT_TABULAR_NUMS    = 1u << 1,
    ETCH_OT_SLASHED_ZERO    = 1u << 2,
    ETCH_OT_SMALL_CAPS      = 1u << 3,
    ETCH_OT_OLDSTYLE_NUMS   = 1u << 4,
    ETCH_OT_FRACTIONS       = 1u << 5,
    ETCH_OT_STYLISTIC_SET01 = 1u << 6,
    ETCH_OT_STYLISTIC_SET02 = 1u << 7
} EtchOpenTypeFlags;

typedef struct EtchFontFeature {
    uint32_t tag;
    uint32_t value;
} EtchFontFeature;

typedef struct EtchFontAxisValue {
    uint32_t tag;
    float value;
} EtchFontAxisValue;

typedef enum EtchEasingKind {
    ETCH_EASING_LINEAR = 0,
    ETCH_EASING_IN = 1,
    ETCH_EASING_OUT = 2,
    ETCH_EASING_IN_OUT = 3,
    ETCH_EASING_CUBIC_BEZIER = 4
} EtchEasingKind;

typedef struct EtchEasingSpec {
    EtchEasingKind kind;
    float x1, y1, x2, y2;
} EtchEasingSpec;
ETCH_API EtchEasingSpec etch_easing_linear(void);
ETCH_API EtchEasingSpec etch_easing_in(void);
ETCH_API EtchEasingSpec etch_easing_out(void);
ETCH_API EtchEasingSpec etch_easing_in_out(void);
ETCH_API EtchEasingSpec etch_easing_cubic_bezier(float x1, float y1, float x2, float y2);
ETCH_API float etch_eval_easing(EtchEasingSpec spec, float t);
ETCH_API float etch_tween(uint32_t id, float target, float duration_sec, EtchEasingSpec spec, float delay_sec);
ETCH_API float etch_stagger_delay(uint32_t index, float step_sec, float max_delay_sec);

typedef struct EtchBorderBeamStyle {
    EtchColor color, base_color;
    float width, angle_rad, sweep_fraction, glow_px, speed, softness;
    bool beam_active, symmetric_dual;
} EtchBorderBeamStyle;
ETCH_API EtchBorderBeamStyle etch_border_beam_style_default(void);

typedef struct EtchBoxDesc {
    uint32_t struct_size;
    uint32_t id;
    EtchAxis axis;
    EtchSizing width;
    EtchSizing height;
    EtchPadding padding;
    float gap;
    EtchAlignX align_x;
    EtchAlignY align_y;
    EtchColor background;
    EtchColor background_end;
    EtchVec2 gradient_dir;
    bool radial_gradient;
    EtchColor border_color;
    float border_width;
    EtchCorners radius;
    EtchColor shadow_color;
    float shadow_blur;
    EtchVec2 shadow_offset;
    bool clip_children;
    bool scroll_x;
    bool scroll_y;
    bool show_scrollbar;
    bool focusable;
    EtchAttachTo attach_to;
    uint32_t attach_element_id;
    EtchAnchor anchor_element;
    EtchAnchor anchor_parent;
    EtchVec2 floating_offset;
    int16_t z_index;
    bool pointer_passthrough;
    bool clip_to_parent;
    float backdrop_blur;
    bool clamp_to_viewport;
    bool wrap;
    float row_gap;
    float aspect_ratio;
    uint16_t grid_columns;
    const EtchGridTrack* grid_tracks;
    uint32_t grid_track_count;
    uint16_t grid_col_span;
    bool shrink_width;
    bool shrink_height;
    bool continuous_corners;
    float shadow_spread;
    bool window_drag_passthrough;
    EtchBorderAlign border_align;
    float opacity;
    EtchColor hover_background, hover_border_color;
    float hover_scale, press_scale;
    bool animate_layout;
    uint32_t hero_id;
    EtchSpringSpec transition;
    EtchBorderBeamStyle border_beam;
    float top_rim_alpha;
    float scroll_fade_px;
    EtchPadding border_sides;
    float border_dash_px;
    float border_gap_px;
    EtchPadding edge_fade;
    EtchJustify justify;
    EtchAlignSelf align_self;
    float flex_grow;
    float flex_shrink;
    EtchPadding margin;
    const EtchGridTrack* grid_row_tracks;
    uint32_t grid_row_track_count;
    uint16_t grid_row_span;
    bool flip_on_overflow;
    EtchHitboxBehavior hitbox_behavior;
    bool stick_to_bottom;
    size_t reserved_v026_pad;
    EtchShadowLayer shadows[4];
    uint8_t shadow_count;
    EtchElevation elevation;
    bool shadow_inset;
    EtchGradientDesc gradient;
    float backdrop_saturation;
    float backdrop_brightness;
    float backdrop_noise;
    EtchBlendMode blend_mode;
} EtchBoxDesc;

typedef struct EtchTextDesc {
    uint32_t struct_size;
    EtchColor color;
    float font_size_px;
    uint16_t font_id;
    float letter_spacing;
    bool wrap;
    uint32_t id;
    bool selectable;
    EtchColor selection_color;
    float line_height_px;
    EtchAlignX align_x;
    bool ellipsis;
    uint16_t max_lines;
    uint32_t opentype_flags;
    const EtchFontFeature* font_features;
    uint32_t font_feature_count;
} EtchTextDesc;

typedef struct EtchTextSpan {
    EtchStr text;
    EtchColor color;
    EtchColor background;
    EtchColor border_color;
    float font_size_px;
    uint16_t font_id;
    bool underline;
    bool strikethrough;
    float letter_spacing;
    float padding_x;
    float bg_radius;
    uint32_t id;
    uint16_t icon_id;
    EtchColor icon_color;
    float icon_size_px;
    float icon_gap_px;
    float padding_y;
    EtchColor hover_background;
    bool has_wipe;
    EtchColor color_end;
    float wipe_x;
    float wipe_feather_px;
} EtchTextSpan;

typedef struct EtchRichTextDesc {
    uint32_t struct_size;
    EtchColor color;
    float font_size_px;
    uint16_t font_id;
    float line_height_px;
    float letter_spacing;
    EtchAlignX align_x;
    bool wrap;
    uint32_t id;
    bool selectable;
    EtchColor selection_color;
    bool ellipsis;
    uint16_t max_lines;
} EtchRichTextDesc;

typedef struct EtchCollapsibleDesc {
    uint32_t id;
    EtchStr title;
    uint16_t icon_id;
    bool default_open;
    float header_height_px;
    float font_size_px;
    uint16_t font_id;
    EtchPadding header_padding;
    EtchPadding body_padding;
    float body_gap;
    EtchCorners radius;
    EtchColor header_bg;
    EtchColor header_hover_bg;
    EtchColor body_bg;
    EtchColor border_color;
    float border_width;
    EtchColor text_color;
    EtchColor icon_color;
    EtchSpringSpec spring_spec;
} EtchCollapsibleDesc;

typedef struct EtchCollapsibleResult {
    bool open;
    bool visible;
    bool toggled;
    bool header_hovered;
    float anim_t;
} EtchCollapsibleResult;

typedef struct EtchCollapseDesc {
    uint32_t struct_size;
    uint32_t id;
    bool open;
    EtchSpringSpec spring;
    bool fade_content;
    EtchSizing width;
} EtchCollapseDesc;

typedef struct EtchImageDesc {
    uint32_t id;
    struct SDL_GPUTexture* texture;
    uint32_t sprite_id;
    EtchSizing width;
    EtchSizing height;
    EtchVec2 uv_min;
    EtchVec2 uv_max;
    EtchCorners radius;
    EtchColor tint;
    EtchColor border_color;
    float border_width;
    float aspect_ratio;
    uint16_t grid_col_span;
    bool continuous_corners;
    EtchImageFit fit;
    EtchVec2 source_size;
    EtchBlendMode blend_mode;
} EtchImageDesc;

typedef struct EtchIconDesc {
    uint32_t id;
    uint16_t icon_id;
    float size_px;
    EtchColor color;
    float rotation_rad;
    float scale;
    float opacity;
    EtchColor secondary_color;
} EtchIconDesc;

typedef struct EtchRectDrawDesc {
    EtchRect rect;
    EtchColor fill;
    EtchColor fill_end;
    EtchVec2 gradient_dir;
    bool radial_gradient;
    EtchCorners radius;
    EtchColor border_color;
    float border_width;
    bool foreground;
    float angle_rad;
    float softness;
    bool continuous_corners;
    EtchBorderAlign border_align;
    EtchGradientDesc gradient;
} EtchRectDrawDesc;

typedef struct EtchMeshRectDrawDesc {
    EtchRect rect;
    EtchColor color_tl, color_tr, color_br, color_bl;
    EtchCorners radius;
    bool oklab;
    float grain_intensity, warp_phase;
    bool continuous_corners, foreground;
} EtchMeshRectDrawDesc;
ETCH_API EtchMeshRectDrawDesc etch_mesh_rect_draw_desc_default(void);
ETCH_API void etch_draw_mesh_rect(const EtchMeshRectDrawDesc* desc);

typedef struct EtchPunchHoleDesc {
    EtchRect rect;
    EtchCorners radius;
    float softness;
    bool continuous_corners, foreground;
} EtchPunchHoleDesc;
ETCH_API EtchPunchHoleDesc etch_punch_hole_desc_default(void);
ETCH_API void etch_punch_hole(const EtchPunchHoleDesc* desc);

typedef struct EtchBorderBeamDrawDesc {
    EtchRect rect;
    EtchCorners radius;
    EtchBorderBeamStyle beam;
    float top_rim_alpha;
    bool continuous_corners, foreground;
} EtchBorderBeamDrawDesc;
ETCH_API EtchBorderBeamDrawDesc etch_border_beam_draw_desc_default(void);
ETCH_API void etch_draw_border_beam(const EtchBorderBeamDrawDesc* desc);

typedef struct EtchLineDesc {
    EtchVec2 from;
    EtchVec2 to;
    EtchColor color;
    EtchColor color_end;
    float thickness;
    EtchColor glow_color;
    float glow_px;
    bool foreground;
} EtchLineDesc;

typedef struct EtchPolylineDesc {
    const EtchVec2* points;
    uint32_t point_count;
    EtchColor color;
    float thickness;
    EtchColor glow_color;
    float glow_px;
    bool closed;
    bool foreground;
} EtchPolylineDesc;

typedef struct EtchFilledPolygonDesc {
    const EtchVec2* points;
    uint32_t point_count;
    EtchColor fill;
    EtchColor fill_end;
    EtchVec2 gradient_dir;
    bool radial_gradient;
    EtchColor stroke_color;
    float stroke_width;
    bool foreground;
} EtchFilledPolygonDesc;

typedef struct EtchPathCmd {
    EtchPathVerb verb;
    EtchVec2 p1;
    EtchVec2 p2;
    EtchVec2 p3;
} EtchPathCmd;

typedef struct EtchFilledPathDesc {
    const EtchPathCmd* commands;
    uint32_t command_count;
    EtchStr svg_path_d;
    EtchVec2 offset;
    float scale;
    EtchColor fill;
    EtchColor fill_end;
    EtchVec2 gradient_dir;
    bool radial_gradient;
    EtchColor stroke_color;
    float stroke_width;
    bool foreground;
} EtchFilledPathDesc;

typedef struct EtchAreaSeriesDesc {
    const EtchVec2* points;
    uint32_t point_count;
    float baseline_y;
    EtchColor fill_top;
    EtchColor fill_bottom;
    EtchColor line_color;
    float line_thickness;
    bool smooth;
    bool foreground;
} EtchAreaSeriesDesc;

typedef struct EtchBezierDesc {
    EtchVec2 p0;
    EtchVec2 p1;
    EtchVec2 p2;
    EtchVec2 p3;
    bool cubic;
    EtchColor color_start;
    EtchColor color_end;
    float thickness;
    EtchColor glow_color;
    float glow_px;
    bool foreground;
} EtchBezierDesc;

typedef struct EtchRingDesc {
    EtchVec2 center;
    float radius;
    float thickness;
    float start_angle_rad;
    float sweep_angle_rad;
    EtchColor color;
    EtchColor color_end;
    EtchColor track_color;
    EtchColor glow_color;
    float glow_px;
    bool round_caps;
    bool foreground;
} EtchRingDesc;

typedef struct EtchIconDrawDesc {
    uint16_t icon_id;
    EtchRect rect;
    EtchColor color;
    bool foreground;
    float angle_rad;
} EtchIconDrawDesc;

typedef struct EtchTextDrawDesc {
    EtchStr text;
    EtchVec2 pos;
    uint16_t font_id;
    float size;
    EtchColor color;
    float letter_spacing;
    EtchAlignX align_x;
    EtchAlignY align_y;
    bool foreground;
    float max_width_px;
    bool ellipsis;
    bool has_wipe;
    EtchColor color_end;
    float wipe_x;
    float wipe_feather_px;
    EtchColor shadow_color;
    EtchVec2 shadow_offset;
    float shadow_blur;
} EtchTextDrawDesc;

typedef struct EtchImageDrawDesc {
    struct SDL_GPUTexture* texture;
    uint32_t sprite_id;
    EtchRect rect;
    EtchRect uv_rect;
    EtchColor tint;
    float radius;
    bool foreground;
    bool continuous_corners;
    EtchImageFit fit;
    EtchVec2 source_size;
} EtchImageDrawDesc;

typedef struct EtchTextureDesc {
    const void* rgba_pixels;
    uint32_t width;
    uint32_t height;
    uint32_t row_pitch_bytes;
    bool srgb;
    const char* debug_name;
    bool premultiplied_alpha;
    bool generate_mipmaps;
} EtchTextureDesc;

typedef struct EtchLoadedTexture {
    struct SDL_GPUTexture* texture;
    uint32_t width;
    uint32_t height;
} EtchLoadedTexture;

typedef struct EtchSvgRasterDesc {
    uint32_t struct_size;
    uint32_t width_px;
    uint32_t height_px;
    float    scale;
    bool     srgb;
    bool     generate_mipmaps;
} EtchSvgRasterDesc;

typedef enum EtchWindowBackdrop {
    ETCH_BACKDROP_OPAQUE, ETCH_BACKDROP_TRANSPARENT, ETCH_BACKDROP_SYSTEM_MICA, ETCH_BACKDROP_SYSTEM_ACRYLIC
} EtchWindowBackdrop;
typedef struct EtchWindowChromeDesc {
    uint32_t struct_size;
    bool enabled;
    bool resizable;
    float border_thickness;
    float corner_size;
    bool macos_traffic_lights;
    float traffic_light_inset_px;
    EtchWindowBackdrop backdrop;
} EtchWindowChromeDesc;

typedef struct EtchDragPayloadView {
    uint32_t kind_hash;
    EtchStr label;
    uint64_t u64;
    uint32_t u32;
    const void* data;
    uint32_t size;
} EtchDragPayloadView;

typedef struct EtchTextLineCol {
    uint32_t line;
    uint32_t col_byte;
} EtchTextLineCol;
typedef struct EtchTextUtf16Position {
    uint32_t line, character;
} EtchTextUtf16Position;

typedef struct EtchSyntaxToken {
    uint32_t byte_start;
    uint32_t byte_end;
    EtchColor color;
    uint16_t font_id;
} EtchSyntaxToken;

typedef uint32_t (*EtchSyntaxTokenizeFn)(
    uint32_t line_index,
    EtchStr line,
    EtchSyntaxToken* out_tokens,
    uint32_t max_tokens,
    void* user_data);

typedef struct EtchCodeLineDecoration {
    EtchColor row_bg;
    EtchColor gutter_bar_color;
    EtchStr gutter_tag;
    EtchColor gutter_tag_color;
} EtchCodeLineDecoration;
typedef bool (*EtchCodeLineDecorateFn)(uint32_t line_index, EtchCodeLineDecoration* out_dec, void* user_data);

typedef struct EtchCodeEditorDesc {
    uint32_t struct_size;
    uint32_t id;
    EtchTextBuffer* buffer;
    EtchSizing width;
    EtchSizing height;
    EtchPadding padding;
    float font_size_px;
    uint16_t font_id;
    float line_height_px;
    bool show_line_numbers;
    float gutter_width_px;
    uint32_t tab_width;
    bool read_only;
    bool show_scrollbar;
    bool word_wrap;
    EtchSyntaxTokenizeFn tokenizer;
    void* tokenizer_user_data;
    EtchColor background;
    EtchColor gutter_background;
    EtchColor active_line_color;
    EtchColor selection_color;
    EtchColor text_color;
    EtchColor gutter_text_color;
    EtchColor gutter_active_text_color;
    EtchColor caret_color;
    EtchColor composition_color;
    EtchColor border_color;
    EtchColor focus_border_color;
    float border_width;
    float focus_border_width;
    EtchCorners radius;
    float scroll_fade_px;
    EtchCodeLineDecorateFn line_decorator;
    void* line_decorator_user_data;
    float gutter_tag_width_px;
} EtchCodeEditorDesc;

typedef struct EtchCodeEditorResult {
    bool changed;
    bool focused;
    bool composing;
    uint32_t cursor_byte;
    uint32_t selection_anchor_byte;
    uint32_t cursor_line;
    uint32_t cursor_col_byte;
    uint32_t first_visible_line;
    uint32_t end_visible_line;
    uint32_t total_lines;
    EtchRect ime_cursor_rect;
    int32_t hovered_line;
    int32_t clicked_gutter_line;
} EtchCodeEditorResult;

typedef struct EtchTextInputHighlight {
    uint32_t byte_offset;
    uint32_t byte_length;
    EtchColor text_color;
    EtchColor background;
    EtchColor border_color;
    float radius_px;
} EtchTextInputHighlight;

typedef struct EtchTextInputDesc {
    uint32_t struct_size;
    uint32_t id;
    char* buffer;
    uint32_t capacity;
    EtchStr placeholder;
    EtchSizing width;
    EtchSizing height;
    EtchPadding padding;
    float font_size_px;
    uint16_t font_id;
    float letter_spacing;
    EtchColor text_color;
    EtchColor placeholder_color;
    EtchColor selection_color;
    EtchColor caret_color;
    EtchColor composition_color;
    EtchColor background;
    EtchColor border_color;
    EtchColor focus_border_color;
    float border_width;
    float focus_border_width;
    EtchCorners radius;
    bool multiline;
    bool word_wrap;
    bool read_only;
    bool password;
    bool context_menu, clear_button, escape_blurs, suppress_popup_keys;
    const EtchTextInputHighlight* highlights;
    uint32_t highlight_count;
    EtchStr ghost_text;
    EtchColor ghost_color;
    bool accept_ghost_on_tab;
    bool submit_on_enter;
    EtchColor hover_border_color;
    float focus_glow_blur;
    uint16_t leading_icon_id;
    EtchColor leading_icon_color;
    bool continuous_corners;
    bool disabled;
} EtchTextInputDesc;

typedef struct EtchTextInputResult {
    bool changed;
    bool submitted;
    bool focused;
    bool composing;
    uint32_t byte_length;
    uint32_t cursor_byte;
    uint32_t selection_anchor_byte;
    EtchRect ime_cursor_rect;
    bool ghost_accepted;
} EtchTextInputResult;

typedef struct EtchButtonDesc {
    uint32_t id;
    EtchStr label;
    uint16_t icon_id;
    EtchButtonVariant variant;
    EtchSizing width;
    EtchSizing height;
    EtchPadding padding;
    float font_size_px;
    uint16_t font_id;
    EtchCorners radius;
    bool disabled;
    EtchStr shortcut_hint;
    EtchAlignX align_x;
} EtchButtonDesc;

typedef struct EtchCheckboxDesc {
    uint32_t id;
    EtchStr label;
    bool* checked;
    bool indeterminate;
    bool disabled;
    float box_size_px;
    float font_size_px;
    uint16_t font_id;
    EtchColor accent;
} EtchCheckboxDesc;

typedef struct EtchToggleSwitchDesc {
    uint32_t id;
    EtchStr label;
    bool* value;
    bool disabled;
    float track_width_px;
    float track_height_px;
    float font_size_px;
    uint16_t font_id;
    EtchColor on_color;
    EtchColor off_color;
    EtchColor thumb_color;
    EtchColor border_color;
} EtchToggleSwitchDesc;

typedef struct EtchProgressBarDesc {
    uint32_t id;
    float fraction;
    EtchStr overlay_text;
    EtchSizing width;
    EtchSizing height;
    EtchCorners radius;
    EtchColor track_color;
    EtchColor fill_color;
    EtchColor text_color;
    float font_size_px;
} EtchProgressBarDesc;

typedef struct EtchTabBarDesc {
    uint32_t id;
    uint32_t* active_tab;
    EtchSizing width;
    float height_px;
    float font_size_px;
    EtchColor background;
    EtchColor active_bg;
    EtchColor hover_bg;
    EtchColor accent;
    EtchColor text_color;
    EtchColor active_text_color;
} EtchTabBarDesc;

typedef struct EtchTabItemDesc {
    uint32_t id;
    EtchStr label;
    uint16_t icon_id;
    bool closable;
    bool disabled;
} EtchTabItemDesc;

typedef struct EtchTabItemResult {
    bool selected;
    bool clicked;
    bool closed;
} EtchTabItemResult;

typedef enum EtchSliderStyle {
    ETCH_SLIDER_TRACK = 0,
    ETCH_SLIDER_COMPACT_BAR = 1
} EtchSliderStyle;

typedef struct EtchSliderDesc {
    uint32_t id;
    EtchStr label;
    float* value;
    float min_value;
    float max_value;
    float step;
    const char* format;
    EtchSizing width;
    float height_px;
    bool disabled;
    EtchColor background;
    EtchColor fill_color;
    EtchColor text_color;
    EtchSliderStyle style;
    EtchColor track_color;
    EtchColor thumb_color;
    float track_height_px;
    float thumb_size_px;
    bool show_value;
} EtchSliderDesc;

typedef struct EtchDragValueDesc {
    uint32_t id;
    EtchStr label;
    float* value;
    float min_value;
    float max_value;
    float speed;
    float step;
    const char* format;
    EtchSizing width;
    float height_px;
    bool disabled;
    EtchColor background;
    EtchColor fill_color;
    EtchColor text_color;
} EtchDragValueDesc;

typedef struct EtchTooltipDesc {
    EtchStr text;
    float delay_seconds;
    float max_width_px;
    float font_size_px;
    int16_t z_index;
} EtchTooltipDesc;

typedef struct EtchModalDesc {
    uint32_t id;
    bool* open;
    EtchStr title;
    float width_px;
    EtchSizing height;
    bool dismiss_on_backdrop;
    bool dismiss_on_escape;
    EtchColor backdrop_color;
    EtchColor background;
    EtchColor border_color;
    EtchCorners radius;
    int16_t z_index;
} EtchModalDesc;

typedef struct EtchComboBoxDesc {
    uint32_t id;
    EtchStr label;
    int* selected_index;
    const EtchStr* items;
    uint32_t item_count;
    EtchSizing width;
    float height_px;
    float max_popup_height_px;
    bool disabled;
    EtchColor background;
    EtchColor hover_bg;
    EtchColor text_color;
    EtchColor border_color;
    EtchColor focus_border_color;
    float border_width;
} EtchComboBoxDesc;

typedef struct EtchPopupDesc {
    uint32_t id;
    uint32_t trigger_id;
    EtchSizing width;
    EtchSizing height;
    EtchPadding padding;
    float gap;
    EtchColor background;
    EtchColor border_color;
    EtchCorners radius;
    int16_t z_index;
    bool attach_to_trigger, match_trigger_width, scroll_y;
    float backdrop_blur;
    float shadow_blur;
    EtchColor shadow_color;
    bool flip_on_overflow;
} EtchPopupDesc;

typedef struct EtchMenuBarDesc {
    uint32_t id;
    EtchSizing width;
    float height_px;
    EtchColor background;
} EtchMenuBarDesc;

typedef struct EtchMenuDesc {
    uint32_t id;
    EtchStr label;
    bool disabled;
} EtchMenuDesc;

typedef struct EtchMenuItemDesc {
    uint32_t id;
    EtchStr label;
    EtchStr shortcut_hint;
    uint16_t icon_id;
    bool checked;
    bool disabled;
    bool separator_after;
} EtchMenuItemDesc;

typedef enum EtchSplitSizing {
    ETCH_SPLIT_RATIO = 0,
    ETCH_SPLIT_FIRST_FIXED_PX = 1,
    ETCH_SPLIT_SECOND_FIXED_PX = 2
} EtchSplitSizing;

typedef struct EtchSplitDesc {
    uint32_t struct_size;
    uint32_t id;
    EtchAxis axis;
    EtchSizing width;
    EtchSizing height;
    float default_ratio;
    float min_first_px;
    float min_second_px;
    float divider_size_px;
    EtchColor divider_color;
    EtchColor divider_hover_color;
    EtchColor divider_active_color;
    float divider_hit_px;
    float keyboard_step_px;
    EtchSplitSizing sizing;
    float* extent_px;
    float default_extent_px;
    bool show_grip;
    EtchColor grip_color;
    float hover_highlight_px;
} EtchSplitDesc;

typedef struct EtchSplitResult {
    float ratio;
    bool dragging;
} EtchSplitResult;

typedef struct EtchVirtualListDesc {
    uint32_t struct_size;
    uint32_t id;
    uint32_t item_count;
    float item_height_px;
    float row_gap_px;
    uint32_t overscan_rows;
    EtchSizing width;
    EtchSizing height;
    EtchPadding padding;
    EtchColor background;
    EtchColor border_color;
    float border_width;
    EtchCorners radius;
    bool show_scrollbar;
    EtchVirtualItemHeightFn measure_height_fn;
    void* measure_user_data;
    bool variable_height;
    float scroll_fade_px;
    bool stick_to_bottom;
} EtchVirtualListDesc;

typedef struct EtchVirtualListSlice {
    uint32_t first_index;
    uint32_t end_index;
    uint32_t total_count;
    double scroll_y;
    double max_scroll_y;
    float viewport_height;
    bool at_bottom;
} EtchVirtualListSlice;

typedef struct EtchDockPaneDesc {
    uint32_t id;
    EtchStr title;
    uint16_t icon_id;
    bool closable;
    bool default_open;
} EtchDockPaneDesc;

typedef struct EtchDockSpaceDesc {
    uint32_t id;
    EtchSizing width;
    EtchSizing height;
    float tab_bar_height_px;
    float divider_size_px;
    EtchColor background;
    EtchColor pane_background;
    EtchColor pane_border_color;
    float pane_border_width;
    EtchCorners pane_radius;
    EtchColor tab_bar_bg;
    EtchColor tab_active_bg;
    EtchColor tab_hover_bg;
    EtchColor tab_accent;
} EtchDockSpaceDesc;

typedef struct EtchTableColumnDesc {
    uint32_t id;
    EtchStr title;
    EtchTableColumnSizing sizing;
    float width;
    float min_width;
    float max_width;
    EtchAlignX align_x;
    bool resizable;
    bool sortable;
} EtchTableColumnDesc;

typedef struct EtchTableSelection {
    uint32_t* focused_row;
    uint32_t* anchor_row;
    uint64_t* selected_words;
    uint32_t word_count;
} EtchTableSelection;
ETCH_API bool etch_table_row_selected(const EtchTableSelection* selection, uint32_t row);
typedef struct EtchTableDesc {
    uint32_t struct_size;
    uint32_t id;
    const EtchTableColumnDesc* columns;
    uint32_t column_count;
    uint32_t item_count;
    float header_height_px;
    float row_height_px;
    float row_gap_px;
    uint32_t overscan_rows;
    uint32_t default_sort_column;
    bool default_sort_descending;
    float cell_padding_x;
    EtchSizing width;
    EtchSizing height;
    EtchPadding padding;
    EtchColor background;
    EtchColor header_background;
    EtchColor zebra_color;
    EtchColor hover_row_color;
    EtchColor border_color;
    float border_width;
    EtchCorners radius;
    bool show_scrollbar;
    bool show_column_lines; float header_font_size_px;
    uint32_t* selected_row;
    EtchColor selected_row_color;
    bool horizontal_scroll, keyboard_navigation;
    EtchTableSelection selection;
    float scroll_fade_px;
} EtchTableDesc;

typedef struct EtchTableResult {
    EtchVirtualListSlice slice;
    uint32_t sort_column;
    bool sort_descending;
    bool sort_changed;
} EtchTableResult;

typedef struct EtchTreeNodeDesc {
    uint32_t id;
    EtchStr label;
    uint16_t icon_id;
    EtchColor icon_color;
    EtchColor text_color;
    EtchColor selected_bg;
    EtchColor selected_accent;
    EtchColor hover_bg;
    float row_height_px;
    float font_size_px;
    float indent_px;
    uint16_t font_id;
    bool default_open;
    bool selected;
    bool leaf;
    bool show_indent_guide;
} EtchTreeNodeDesc;

typedef struct EtchTreeNodeResult {
    bool open;
    bool clicked;
    bool double_clicked;
    bool toggled;
    bool hovered;
} EtchTreeNodeResult;

typedef struct EtchCanvasDesc {
    uint32_t struct_size;
    uint32_t id;
    EtchSizing width;
    EtchSizing height;
    EtchVec2 default_pan;
    float default_zoom;
    float min_zoom;
    float max_zoom;
    float grid_step_world;
    uint32_t major_grid_subdiv;
    EtchColor background;
    EtchColor minor_grid_color;
    EtchColor major_grid_color;
    EtchColor border_color;
    float border_width;
    EtchCorners radius;
    bool show_grid;
    bool interactive;
    bool marquee_select;
    EtchColor marquee_fill;
    EtchColor marquee_border;
} EtchCanvasDesc;

typedef struct EtchCanvasState {
    EtchVec2 pan;
    float zoom;
    EtchRect viewport;
    bool panning;
    bool marquee_active;
    bool marquee_finished;
    EtchRect marquee_world;
} EtchCanvasState;

typedef struct EtchCanvasWireDesc {
    EtchVec2 from_world;
    EtchVec2 to_world;
    EtchColor color_start;
    EtchColor color_end;
    EtchColor glow_color;
    float thickness_world;
    float glow_world;
    float tangent_bias;
    int16_t z_index;
    EtchAxis axis;
} EtchCanvasWireDesc;

typedef struct EtchCanvasNodeDesc {
    uint32_t id;
    EtchVec2* world_pos;
    EtchVec2 pos_value;
    EtchSizing width_world;
    EtchSizing height_world;
    EtchPadding padding_world;
    float gap_world;
    EtchAxis axis;
    EtchColor background;
    EtchColor background_end;
    EtchVec2 gradient_dir;
    EtchColor border_color;
    float border_width_world;
    EtchCorners radius_world;
    EtchColor shadow_color;
    float shadow_blur_world;
    EtchVec2 shadow_offset_world;
    int16_t z_index;
    bool draggable;
} EtchCanvasNodeDesc;

typedef struct EtchCanvasNodeResult {
    bool hovered;
    bool pressed;
    bool dragging;
    bool clicked;
    EtchVec2 drag_delta_world;
} EtchCanvasNodeResult;

/* ============================================================================
 * Descriptor Default Factories
 * ============================================================================ */

ETCH_API EtchContextDesc etch_context_desc_default(void);
ETCH_API EtchFrameDesc etch_frame_desc_default(void);
ETCH_API EtchWindowFrameDesc etch_window_frame_desc_default(void);
ETCH_API EtchBoxDesc etch_box_desc_default(void);
ETCH_API EtchTextDesc etch_text_desc_default(void);
ETCH_API EtchTextSpan etch_text_span_default(void);
ETCH_API EtchRichTextDesc etch_rich_text_desc_default(void);
ETCH_API EtchCollapsibleDesc etch_collapsible_desc_default(void);
ETCH_API EtchCollapseDesc etch_collapse_desc_default(void);
ETCH_API EtchImageDesc etch_image_desc_default(void);
ETCH_API EtchIconDesc etch_icon_desc_default(void);
ETCH_API EtchRectDrawDesc etch_rect_draw_desc_default(void);
ETCH_API EtchLineDesc etch_line_draw_desc_default(void);
ETCH_API EtchPolylineDesc etch_polyline_desc_default(void);
ETCH_API EtchFilledPolygonDesc etch_filled_polygon_desc_default(void);
ETCH_API EtchFilledPathDesc etch_filled_path_desc_default(void);
ETCH_API EtchAreaSeriesDesc etch_area_series_desc_default(void);
ETCH_API EtchBezierDesc etch_bezier_desc_default(void);
ETCH_API EtchRingDesc etch_ring_desc_default(void);
ETCH_API EtchIconDrawDesc etch_icon_draw_desc_default(void);
ETCH_API EtchTextDrawDesc etch_text_draw_desc_default(void);
ETCH_API EtchImageDrawDesc etch_image_draw_desc_default(void);
ETCH_API EtchTextureDesc etch_texture_desc_default(void);
ETCH_API EtchSvgRasterDesc etch_svg_raster_desc_default(void);
ETCH_API EtchWindowChromeDesc etch_window_chrome_desc_default(void);
ETCH_API EtchCodeEditorDesc etch_code_editor_desc_default(void);
ETCH_API EtchTextInputHighlight etch_text_input_highlight_default(void);
ETCH_API EtchTextInputDesc etch_text_input_desc_default(void);
ETCH_API EtchButtonDesc etch_button_desc_default(void);
ETCH_API EtchCheckboxDesc etch_checkbox_desc_default(void);
ETCH_API EtchToggleSwitchDesc etch_toggle_switch_desc_default(void);
ETCH_API EtchProgressBarDesc etch_progress_bar_desc_default(void);
ETCH_API EtchTabBarDesc etch_tab_bar_desc_default(void);
ETCH_API EtchTabItemDesc etch_tab_item_desc_default(void);
ETCH_API EtchSliderDesc etch_slider_desc_default(void);
ETCH_API EtchDragValueDesc etch_drag_value_desc_default(void);
ETCH_API EtchTooltipDesc etch_tooltip_desc_default(void);
ETCH_API EtchModalDesc etch_modal_desc_default(void);
ETCH_API EtchComboBoxDesc etch_combo_box_desc_default(void);
ETCH_API EtchPopupDesc etch_popup_desc_default(void);
ETCH_API EtchMenuBarDesc etch_menu_bar_desc_default(void);
ETCH_API EtchMenuDesc etch_menu_desc_default(void);
ETCH_API EtchMenuItemDesc etch_menu_item_desc_default(void);
ETCH_API EtchSplitDesc etch_split_desc_default(void);
ETCH_API EtchVirtualListDesc etch_virtual_list_desc_default(void);
ETCH_API EtchDockPaneDesc etch_dock_pane_desc_default(void);
ETCH_API EtchDockSpaceDesc etch_dock_space_desc_default(void);
ETCH_API EtchTableColumnDesc etch_table_column_desc_default(void);
ETCH_API EtchTableDesc etch_table_desc_default(void);
ETCH_API EtchTreeNodeDesc etch_tree_node_desc_default(void);
ETCH_API EtchCanvasDesc etch_canvas_desc_default(void);
ETCH_API EtchCanvasWireDesc etch_canvas_wire_desc_default(void);
ETCH_API EtchCanvasNodeDesc etch_canvas_node_desc_default(void);

/* ============================================================================
 * Arena, Theme & System API
 * ============================================================================ */

ETCH_API EtchArena etch_arena_create(size_t capacity_bytes);
ETCH_API EtchArena etch_arena_from_buffer(void* buffer, size_t capacity_bytes);
ETCH_API void etch_arena_destroy(EtchArena* arena);
ETCH_API void etch_arena_reset(EtchArena* arena);
ETCH_API size_t etch_arena_mark(const EtchArena* arena);
ETCH_API void etch_arena_rewind(EtchArena* arena, size_t saved_mark);
ETCH_API void* etch_arena_alloc_bytes(EtchArena* arena, size_t size, size_t align);
ETCH_API EtchStr etch_arena_push_str(EtchArena* arena, const char* s, size_t len);
ETCH_API void* etch_frame_arena_alloc(EtchContext* ctx, size_t size, size_t align);
ETCH_API EtchStr etch_frame_arena_push_str(EtchContext* ctx, const char* s, size_t len);

ETCH_API EtchSystemTheme etch_system_theme(void);
ETCH_API bool etch_system_dark_mode(bool default_when_unknown);

ETCH_API EtchTheme etch_theme_dark(void);
ETCH_API EtchTheme etch_theme_light(void);
ETCH_API EtchColor etch_color_oklch(float L, float C, float hue_deg, float alpha);
ETCH_API EtchColor etch_color_lerp_oklab(EtchColor a, EtchColor b, float t);
ETCH_API EtchTheme etch_theme_from_seed(EtchColor seed, bool dark_mode, float chroma_scale);
ETCH_API EtchTheme etch_theme_lerp_oklab(const EtchTheme* a, const EtchTheme* b, float t);
ETCH_API EtchSpringSpec etch_spring_spec_snappy(void);
ETCH_API EtchSpringSpec etch_spring_spec_fluid(void);
ETCH_API EtchSpringSpec etch_spring_spec_bouncy(void);
ETCH_API EtchSpringSpec etch_spring_spec_gentle(void);
typedef struct EtchLayerDesc {
    uint32_t struct_size;
    float opacity;
    EtchVec2 translate, scale, pivot_norm, pivot_px;
    bool animating;
    float blur_px;
    bool blend_plus_lighter;
    float rotation_rad;
    EtchBlendMode blend_mode;
    EtchAlphaMaskDesc alpha_mask;
} EtchLayerDesc;
ETCH_API EtchLayerDesc etch_layer_desc_default(void);
ETCH_API void etch_push_layer(const EtchLayerDesc* desc);
ETCH_API void etch_pop_layer(void);
typedef struct EtchPresenceResult { bool visible; float opacity; bool exiting; } EtchPresenceResult;
typedef enum EtchTerminalColorMode { ETCH_TERMINAL_DEFAULT, ETCH_TERMINAL_ANSI16, ETCH_TERMINAL_ANSI256, ETCH_TERMINAL_TRUE_COLOR } EtchTerminalColorMode;
typedef struct EtchTerminalColor { EtchTerminalColorMode mode; uint32_t value; } EtchTerminalColor;
typedef struct EtchTerminalCell {
    uint32_t codepoint;
    EtchTerminalColor foreground, background;
    uint8_t width;
    bool bold, underline, inverse;
} EtchTerminalCell;
typedef struct EtchTerminalGridDesc {
    uint32_t id;
    const EtchTerminalCell* cells;
    size_t cell_count;
    uint32_t columns, rows, first_row;
    float cell_width_px, line_height_px, font_size_px;
    uint16_t font_id;
    EtchColor foreground, background;
    int32_t cursor_column, cursor_row;
    EtchColor cursor_color;
    EtchSpringSpec cursor_motion;
} EtchTerminalGridDesc;
typedef enum EtchSheetEdge { ETCH_SHEET_BOTTOM, ETCH_SHEET_LEFT, ETCH_SHEET_RIGHT } EtchSheetEdge;
typedef struct EtchSheetDesc {
    uint32_t struct_size;
    uint32_t id;
    bool open;
    EtchSheetEdge edge;
    float extent_px;
    float snap_points[8];
    uint32_t snap_count;
    uint32_t* snap_index;
    EtchColor background;
    EtchSpringSpec motion;
    int16_t z_index;
    bool* open_state;
    bool dismiss_on_backdrop;
    bool dismiss_on_escape;
    bool show_handle;
    EtchColor backdrop_color;
    EtchColor border_color;
    float border_width;
    float corner_radius_px;
    float inset_px;
    float backdrop_blur;
    float top_inset_px;
    EtchColor shadow_color;
    float shadow_blur;
    EtchVec2 shadow_offset;
} EtchSheetDesc;
ETCH_API EtchTerminalGridDesc etch_terminal_grid_desc_default(void);
ETCH_API EtchTerminalCell etch_terminal_cell_default(void);
ETCH_API void etch_terminal_grid(const EtchTerminalGridDesc*);
ETCH_API EtchSheetDesc etch_sheet_desc_default(void);
ETCH_API bool etch_begin_sheet(const EtchSheetDesc*);
ETCH_API void etch_end_sheet(void);

ETCH_API EtchPresenceResult etch_presence(uint32_t id, bool mounted, EtchSpringSpec spec);
ETCH_API void etch_set_time_scale(EtchContext* ctx, float scale);
ETCH_API EtchTheme etch_theme_high_contrast(void);
ETCH_API EtchTheme etch_theme_industrial(void);
ETCH_API void etch_set_theme(const EtchTheme* theme, EtchContext* ctx);
ETCH_API EtchTheme etch_get_theme(const EtchContext* ctx);
ETCH_API void etch_push_theme(const EtchTheme* theme);
ETCH_API void etch_pop_theme(void);

/* ============================================================================
 * Context Lifecycle, Fonts, Icons, Events, Frames & Window Management
 * ============================================================================ */

ETCH_API EtchContext* etch_create_context(const EtchContextDesc* desc);
ETCH_API void etch_destroy_context(EtchContext* ctx);
ETCH_API EtchContext* etch_current_context(void);
ETCH_API void etch_set_current_context(EtchContext* ctx);
ETCH_API struct SDL_GPUDevice* etch_gpu_device(EtchContext* ctx);
typedef enum EtchGpuStatus {
    ETCH_GPU_UNINITIALIZED = 0,
    ETCH_GPU_READY = 1,
    ETCH_GPU_FAILED = 2,
    ETCH_GPU_LOST = 3
} EtchGpuStatus;
ETCH_API EtchGpuStatus etch_gpu_status(const EtchContext* ctx);
ETCH_API const char* etch_gpu_driver_name(const EtchContext* ctx);
/* Retry a failed lazy initialization between frames. NULL keeps the previous
   driver selection and "" selects the default. */
ETCH_API bool etch_retry_gpu_initialization(EtchContext* ctx, const char* driver);
typedef void (*EtchGpuDeviceResetFn)(EtchContext* ctx, uint64_t device_generation, void* user_data);
ETCH_API bool etch_recreate_gpu_device(EtchContext* ctx, const char* driver);
ETCH_API void etch_mark_gpu_device_lost(EtchContext* ctx);
ETCH_API uint64_t etch_gpu_device_generation(const EtchContext* ctx);
ETCH_API void etch_set_gpu_device_reset_callback(EtchContext* ctx, EtchGpuDeviceResetFn fn, void* user_data);
ETCH_API bool etch_begin_async_readback(EtchContext* ctx, uint32_t width, uint32_t height, EtchColor clear_color);
ETCH_API int32_t etch_poll_async_readback(EtchContext* ctx, bool (*receive)(const uint8_t*, uint32_t, uint32_t, uint32_t, void*), void* user_data);

typedef struct EtchFrameTraceSample {
    uint64_t frame_index;
    double layout_us;
    double text_shape_us;
    double prim_build_us;
    double gpu_upload_us;
    double gpu_submit_us;
    uint32_t ui_draw_calls;
    uint32_t total_draw_calls;
    uint32_t blur_passes;
    uint32_t filter_passes;
    uint32_t prim_count;
    uint32_t atlas_evictions;
    uint32_t shape_cache_hits;
    uint32_t shape_cache_misses;
    uint64_t working_set_bytes;
    uint64_t private_commit_bytes;
    uint64_t estimated_vram_bytes;
} EtchFrameTraceSample;
ETCH_API void etch_set_trace_enabled(EtchContext* ctx, bool enabled);
ETCH_API bool etch_trace_enabled(const EtchContext* ctx);
ETCH_API uint32_t etch_trace_sample_count(const EtchContext* ctx);
ETCH_API bool etch_trace_get_sample(const EtchContext* ctx, uint32_t index, EtchFrameTraceSample* out_sample);
ETCH_API void etch_trace_clear(EtchContext* ctx);
ETCH_API uint32_t etch_trace_export_json(const EtchContext* ctx, char* out_buf, uint32_t cap);
typedef struct EtchTracePercentiles {
    uint32_t sample_count;
    double layout_p50_us, layout_p95_us, layout_p99_us;
    double gpu_p50_us, gpu_p95_us, gpu_p99_us;
    uint64_t peak_working_set_bytes, peak_vram_bytes;
} EtchTracePercentiles;
ETCH_API bool etch_trace_compute_percentiles(const EtchContext* ctx, EtchTracePercentiles* out_summary);

ETCH_API EtchSystemPreferences etch_system_preferences(const EtchContext* ctx);
ETCH_API void etch_set_system_preferences_override(EtchContext* ctx, const EtchSystemPreferences* override_prefs);
ETCH_API void etch_apply_system_preferences(EtchContext* ctx, bool auto_theme, bool auto_text_scale, bool auto_rtl);
ETCH_API float etch_active_text_scale(const EtchContext* ctx);
ETCH_API EtchLayoutDirection etch_active_layout_direction(const EtchContext* ctx);
ETCH_API EtchPointerState etch_pointer_state(const EtchContext* ctx);
ETCH_API EtchPointerGesturePhase etch_pointer_gesture_phase(const EtchContext* ctx);
typedef struct EtchTouchPoint {
    uint64_t finger_id;
    EtchVec2 pos;
    float pressure;
} EtchTouchPoint;
ETCH_API uint32_t etch_active_touch_points(EtchTouchPoint* out_points, uint32_t max_points, const EtchContext* ctx);

typedef enum EtchFontWeight {
    ETCH_FONT_WEIGHT_REGULAR = 0,
    ETCH_FONT_WEIGHT_MEDIUM = 1,
    ETCH_FONT_WEIGHT_SEMIBOLD = 2,
    ETCH_FONT_WEIGHT_BOLD = 3,
    ETCH_FONT_WEIGHT_ITALIC = 4,
    ETCH_FONT_WEIGHT_MONO = 5
} EtchFontWeight;

ETCH_API uint16_t etch_load_system_font(EtchContext* ctx, EtchStr family_name, EtchFontWeight weight, uint16_t slot_hint);
ETCH_API uint16_t etch_load_font_file(EtchContext* ctx, const char* path, uint16_t slot_hint, int32_t face_index);
ETCH_API uint16_t etch_load_font_memory(EtchContext* ctx, const void* data, size_t size_bytes, uint16_t slot_hint, int32_t face_index);
ETCH_API uint16_t etch_create_font_variation(EtchContext* ctx, uint16_t base_font_id, const EtchFontAxisValue* axes, uint32_t axis_count, const EtchFontFeature* features, uint32_t feature_count);
ETCH_API bool etch_is_font_loaded(const EtchContext* ctx, uint16_t font_id);
ETCH_API bool etch_set_font_tabular_numbers(EtchContext* ctx, uint16_t font_id, bool enabled);
ETCH_API bool etch_set_font_fallback(EtchContext* ctx, uint16_t primary_font_id, uint16_t fallback_font_id);
ETCH_API bool etch_set_font_scale_adjust(EtchContext* ctx, uint16_t font_id, float scale_factor);
ETCH_API float etch_font_scale_adjust(const EtchContext* ctx, uint16_t font_id);
ETCH_API void etch_set_hinted_small_text(EtchContext* ctx, bool enabled);
ETCH_API bool etch_hinted_small_text(const EtchContext* ctx);
ETCH_API void etch_set_lcd_subpixel_text(EtchContext* ctx, bool enabled);
ETCH_API bool etch_lcd_subpixel_text(const EtchContext* ctx);

typedef enum EtchStrokeCap {
    ETCH_STROKE_CAP_BUTT = 0, ETCH_STROKE_CAP_ROUND = 1, ETCH_STROKE_CAP_SQUARE = 2
} EtchStrokeCap;

typedef enum EtchStrokeJoin {
    ETCH_STROKE_JOIN_MITER = 0, ETCH_STROKE_JOIN_ROUND = 1, ETCH_STROKE_JOIN_BEVEL = 2
} EtchStrokeJoin;

typedef struct EtchSvgStrokeDesc {
    EtchVec4 view_box;
    float stroke_width;
    EtchStrokeCap cap;
    EtchStrokeJoin join;
    float miter_limit;
    uint16_t slot_hint;
} EtchSvgStrokeDesc;

typedef struct EtchSvgIconLayer {
    EtchStr path_d;
    bool stroked;
    EtchSvgStrokeDesc stroke;
    uint8_t color_slot;
    float opacity;
} EtchSvgIconLayer;

ETCH_API EtchSvgStrokeDesc etch_svg_stroke_desc_default(void);
ETCH_API EtchSvgIconLayer etch_svg_icon_layer_default(void);
ETCH_API uint16_t etch_register_svg_stroke_icon(EtchContext* ctx, EtchStr svg_path_d, const EtchSvgStrokeDesc* desc);
ETCH_API uint16_t etch_register_svg_icon(EtchContext* ctx, EtchStr svg_path_d, EtchVec4 view_box, uint16_t slot_hint);
ETCH_API uint16_t etch_register_svg_layered_icon(EtchContext* ctx, const EtchSvgIconLayer* layers, uint32_t layer_count, EtchVec4 view_box, uint16_t slot_hint);
ETCH_API uint16_t etch_register_svg_duotone_icon(EtchContext* ctx, EtchStr fill_path_d, EtchStr stroke_path_d, const EtchSvgStrokeDesc* stroke_desc, float fill_opacity);
ETCH_API bool etch_is_icon_loaded(const EtchContext* ctx, uint16_t icon_id);

ETCH_API void etch_process_event(EtchContext* ctx, const union SDL_Event* event);
ETCH_API bool etch_needs_redraw(const EtchContext* ctx);
ETCH_API void etch_request_redraw(EtchContext* ctx);
ETCH_API void etch_wake_up(EtchContext* ctx);
ETCH_API bool etch_wait_events(EtchContext* ctx, union SDL_Event* first_event_out, int32_t timeout_ms);

ETCH_API void etch_begin_frame(EtchContext* ctx, const EtchFrameDesc* desc);
ETCH_API void etch_end_frame(EtchContext* ctx);
ETCH_API bool etch_frame_in_progress(const EtchContext* ctx);
ETCH_API void etch_begin_window_frame(EtchContext* ctx, struct SDL_Window* window, const EtchWindowFrameDesc* desc);
ETCH_API void etch_end_window_frame(EtchContext* ctx);
ETCH_API bool etch_needs_window_redraw(const EtchContext* ctx, struct SDL_Window* window);
ETCH_API void etch_request_window_redraw(EtchContext* ctx, struct SDL_Window* window);

ETCH_API uint32_t etch_frame_prim_count(const EtchContext* ctx);
ETCH_API bool etch_get_frame_prim(const EtchContext* ctx, uint32_t index, EtchPrim* out_prim);
ETCH_API EtchFrameStats etch_frame_stats(const EtchContext* ctx);

ETCH_API bool etch_render_window(EtchContext* ctx, EtchColor clear_color, struct SDL_Window* window_override);
typedef struct EtchRenderTargetDesc {
    uint32_t struct_size;
    struct SDL_GPUTexture* texture;
    uint32_t width;
    uint32_t height;
    uint32_t sdl_texture_format;
    EtchColor clear_color;
} EtchRenderTargetDesc;
ETCH_API EtchRenderTargetDesc etch_render_target_desc_default(void);
/* Submits the finished UI frame to mip zero of a same-device 2D color target. */
ETCH_API bool etch_render_to_texture(EtchContext*, const EtchRenderTargetDesc*);
ETCH_API void etch_release_window(EtchContext* ctx, struct SDL_Window* window);
ETCH_API uint32_t etch_claimed_window_count(const EtchContext* ctx);
typedef bool (*EtchReadPixelsFn)(const uint8_t* rgba, uint32_t width, uint32_t height,
                                 uint32_t pitch_bytes, void* user_data);
typedef struct EtchReadPixelsDesc {
    uint32_t struct_size;
    uint32_t frame_width, frame_height;
    uint32_t x, y, width, height;
    EtchColor clear_color;
    EtchReadPixelsFn receive;
    void* user_data;
} EtchReadPixelsDesc;
ETCH_API EtchReadPixelsDesc etch_read_pixels_desc_default(void);
/* Synchronous sRGB RGBA8 readback. Zero extent reaches the frame edge.
   Rows are borrowed until receive returns. Do not reenter the context. */
ETCH_API bool etch_render_headless_pixels(EtchContext* ctx, const EtchReadPixelsDesc* desc);
ETCH_API bool etch_render_headless_png(EtchContext* ctx, uint32_t width, uint32_t height, const char* out_png_path, EtchColor clear_color);

ETCH_API void etch_set_window_refresh_callback(EtchContext* ctx, EtchWindowRefreshFn fn, void* user_data);
ETCH_API void etch_set_window_chrome(const EtchWindowChromeDesc* desc, struct SDL_Window* window);
ETCH_API void etch_window_drag_region(EtchRect rect_logical);
ETCH_API void etch_window_client_region(EtchRect rect_logical);
ETCH_API void etch_window_maximize_button_region(EtchRect rect_logical);
ETCH_API void etch_window_maximize_button_element(uint32_t element_id);
ETCH_API EtchWindowHit etch_evaluate_window_hit(const EtchContext* ctx, EtchVec2 logical_pos, struct SDL_Window* window);
ETCH_API void etch_minimize_window(struct SDL_Window* window);
ETCH_API void etch_maximize_window(struct SDL_Window* window);
ETCH_API void etch_restore_window(struct SDL_Window* window);
ETCH_API bool etch_is_window_maximized(const EtchContext* ctx, struct SDL_Window* window);
ETCH_API bool etch_is_window_minimized(const EtchContext* ctx, struct SDL_Window* window);
ETCH_API bool etch_set_window_fullscreen(bool fullscreen, struct SDL_Window* window);
ETCH_API bool etch_is_window_fullscreen(const EtchContext* ctx, struct SDL_Window* window);
ETCH_API EtchPadding etch_window_safe_insets(const EtchContext* ctx, struct SDL_Window* window);

/* ============================================================================
 * Accessibility (A11y) Snapshot, Actions & Native OS Bridge
 * ============================================================================ */

ETCH_API void etch_set_a11y_override(const EtchA11yOverrideDesc* desc);
ETCH_API void etch_emit_a11y_node(uint32_t element_id, EtchA11yRole role, EtchStr label, EtchStr value_text,
    uint32_t state_flags, uint32_t actions_mask, float value_num, EtchRect bounds);
ETCH_API void etch_a11y_announce(EtchStr message, EtchA11yLiveMode priority, EtchContext* ctx, struct SDL_Window* window);
ETCH_API EtchStr etch_last_a11y_announcement(EtchA11yLiveMode* out_priority, uint64_t* out_seq, const EtchContext* ctx, struct SDL_Window* window);
ETCH_API uint32_t etch_a11y_node_count(const EtchContext* ctx, struct SDL_Window* window);
ETCH_API bool etch_get_a11y_node(uint32_t index, EtchA11yNode* out_node, const EtchContext* ctx, struct SDL_Window* window);
ETCH_API bool etch_find_a11y_node(uint32_t element_id, EtchA11yNode* out_node, const EtchContext* ctx, struct SDL_Window* window);
ETCH_API bool etch_perform_a11y_action(uint32_t element_id, EtchA11yAction action, float numeric_value, EtchStr text_value, EtchContext* ctx, struct SDL_Window* window);
ETCH_API const char* etch_a11y_role_name(EtchA11yRole role);
ETCH_API void etch_set_os_a11y_enabled(EtchContext* ctx, bool enabled);
ETCH_API bool etch_os_a11y_enabled(const EtchContext* ctx);
ETCH_API bool etch_os_a11y_active(const EtchContext* ctx, struct SDL_Window* window);

/* ============================================================================
 * Layout Boxes, Text, Collapsibles, Images, Icons & 2D Draw Commands
 * ============================================================================ */

ETCH_API void etch_begin_box(const EtchBoxDesc* desc);
ETCH_API void etch_end_box(void);
ETCH_API void etch_box(const EtchBoxDesc* desc);
ETCH_API void etch_text(EtchStr content, const EtchTextDesc* desc);
ETCH_API void etch_text_spans(const EtchTextSpan* spans, uint32_t span_count, const EtchRichTextDesc* desc);
ETCH_API EtchCollapsibleResult etch_begin_collapsible(const EtchCollapsibleDesc* desc);
ETCH_API void etch_end_collapsible(void);
ETCH_API bool etch_is_collapsible_open(uint32_t element_id);
ETCH_API void etch_set_collapsible_open(uint32_t element_id, bool open);
ETCH_API bool etch_begin_collapse(const EtchCollapseDesc* desc);
ETCH_API void etch_end_collapse(void);
ETCH_API void etch_image(const EtchImageDesc* desc);
ETCH_API void etch_icon(const EtchIconDesc* desc);

ETCH_API void etch_draw_rect(const EtchRectDrawDesc* desc);
ETCH_API void etch_draw_line(const EtchLineDesc* desc);
ETCH_API void etch_draw_filled_polygon(const EtchFilledPolygonDesc* desc);
ETCH_API void etch_draw_filled_path(const EtchFilledPathDesc* desc);
ETCH_API void etch_draw_area_series(const EtchAreaSeriesDesc* desc);
ETCH_API void etch_draw_polyline(const EtchPolylineDesc* desc);
ETCH_API void etch_draw_bezier(const EtchBezierDesc* desc);
ETCH_API void etch_draw_ring(const EtchRingDesc* desc);
ETCH_API void etch_draw_icon(const EtchIconDrawDesc* desc);
typedef struct EtchFontMetrics {float ascent,descent,line_height;} EtchFontMetrics;
ETCH_API EtchFontMetrics etch_font_metrics(uint16_t font_id,float size);
ETCH_API EtchVec2 etch_measure_text(EtchStr text, uint16_t font_id, float size, float letter_spacing);
ETCH_API void etch_draw_text(const EtchTextDrawDesc* desc);
typedef void (*EtchLayoutDrawFn)(EtchRect local_bounds, void* user_data);
ETCH_API void etch_draw_after_layout(EtchLayoutDrawFn callback, void* user_data, bool foreground);
ETCH_API void etch_draw_image(const EtchImageDrawDesc* desc);

/* ============================================================================
 * Dynamic Sprites & External GPU Textures
 * ============================================================================ */

ETCH_API uint32_t etch_create_sprite_rgba(EtchContext* ctx, const EtchTextureDesc* desc);
ETCH_API bool etch_update_sprite_rgba(EtchContext* ctx, uint32_t sprite_id, uint32_t width, uint32_t height, const void* rgba_pixels, uint32_t row_pitch_bytes);
ETCH_API uint32_t etch_load_sprite_file(EtchContext* ctx, const char* path, bool srgb);
ETCH_API uint32_t etch_load_sprite_memory(EtchContext* ctx, const void* data, size_t size_bytes, bool srgb);
ETCH_API void etch_destroy_sprite(EtchContext* ctx, uint32_t sprite_id);
ETCH_API EtchVec2 etch_sprite_size(const EtchContext* ctx, uint32_t sprite_id);
ETCH_API uint32_t etch_sprite_count(const EtchContext* ctx);

ETCH_API EtchLoadedTexture etch_create_texture_rgba(EtchContext* ctx, const EtchTextureDesc* desc);
ETCH_API bool etch_flush_texture_uploads(EtchContext* ctx);
ETCH_API bool etch_update_texture_rgba(EtchContext* ctx, struct SDL_GPUTexture* texture, uint32_t width, uint32_t height, const void* rgba_pixels, uint32_t row_pitch_bytes);
/* Caller owns an RGBA8 texture; the rectangle must fit mip zero. Pixels are copied immediately. */
ETCH_API bool etch_update_texture_region_rgba(EtchContext* ctx, struct SDL_GPUTexture* texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* rgba_pixels, uint32_t row_pitch_bytes);
ETCH_API EtchLoadedTexture etch_load_texture_file(EtchContext* ctx, const char* path, bool srgb);
ETCH_API EtchLoadedTexture etch_load_texture_memory(EtchContext* ctx, const void* data, size_t size_bytes, bool srgb);
ETCH_API EtchLoadedTexture etch_load_svg_file(EtchContext* ctx, const char* path, const EtchSvgRasterDesc* desc);
ETCH_API EtchLoadedTexture etch_load_svg_memory(EtchContext* ctx, const void* data, size_t size_bytes, const EtchSvgRasterDesc* desc);
ETCH_API bool etch_register_svg_font_file(const char* family, bool bold, bool italic, const char* path);
ETCH_API bool etch_register_svg_font_memory(const char* family, bool bold, bool italic, const void* data, size_t size_bytes);
ETCH_API void etch_destroy_loaded_texture(EtchContext* ctx, EtchLoadedTexture* tex);
ETCH_API void etch_destroy_gpu_texture(EtchContext* ctx, struct SDL_GPUTexture* tex);
typedef struct EtchDecodedImage {
    uint8_t* rgba;
    uint32_t width;
    uint32_t height;
} EtchDecodedImage;
ETCH_API bool etch_decode_image_memory(const void* data, size_t size_bytes, EtchDecodedImage* out_image);
ETCH_API bool etch_rasterize_svg_memory(const void* data, size_t size_bytes, const EtchSvgRasterDesc* desc, EtchDecodedImage* out_image);
ETCH_API void etch_free_decoded_image(EtchDecodedImage* image);

/* ============================================================================
 * Text Editing, Selection, Clipboard, IME & Code Editor
 * ============================================================================ */

ETCH_API EtchTextInputResult etch_text_input(const EtchTextInputDesc* desc);
ETCH_API void etch_set_text_selection(uint32_t element_id, uint32_t anchor_byte, uint32_t cursor_byte);
ETCH_API bool etch_has_text_selection(uint32_t element_id);
ETCH_API EtchStr etch_selected_text(uint32_t element_id, EtchArena* arena);
ETCH_API void etch_clear_text_selection(uint32_t element_id);
ETCH_API EtchRect etch_ime_cursor_rect(const EtchContext* ctx);
ETCH_API void etch_request_text_input(uint32_t element_id, EtchRect ime_cursor_rect);
ETCH_API EtchStr etch_typed_text(uint32_t element_id);
typedef struct EtchClipboardImageData {
    const uint8_t* bytes;
    uint32_t size_bytes;
    EtchStr mime_type;
} EtchClipboardImageData;
ETCH_API bool etch_set_clipboard_text(const char* utf8_text);
ETCH_API EtchStr etch_get_clipboard_text(EtchArena* arena);
ETCH_API EtchClipboardImageData etch_get_clipboard_image(EtchArena* arena);

typedef struct EtchFileDialogFilter {
    const char* name;
    const char* pattern;
} EtchFileDialogFilter;

typedef struct EtchFileDialogResult {
    bool ready;
    bool cancelled;
    bool error;
    uint32_t request_tag;
    int32_t selected_filter;
    const EtchStr* paths;
    uint32_t path_count;
} EtchFileDialogResult;

ETCH_API bool etch_open_folder_dialog(EtchContext* ctx, struct SDL_Window* parent_window, const char* default_location, uint32_t request_tag, bool allow_many);
ETCH_API bool etch_open_file_dialog(EtchContext* ctx, struct SDL_Window* parent_window, const EtchFileDialogFilter* filters, uint32_t filter_count, const char* default_location, uint32_t request_tag, bool allow_many);
ETCH_API bool etch_open_save_file_dialog(EtchContext* ctx, struct SDL_Window* parent_window, const EtchFileDialogFilter* filters, uint32_t filter_count, const char* default_location, uint32_t request_tag);
ETCH_API EtchFileDialogResult etch_poll_file_dialog(EtchContext* ctx, EtchArena* arena);

ETCH_API EtchTextBuffer* etch_create_text_buffer(EtchStr initial_utf8, uint32_t initial_cap);
ETCH_API void etch_destroy_text_buffer(EtchTextBuffer* buf);
ETCH_API bool etch_text_buffer_set(EtchTextBuffer* buf, EtchStr utf8);
ETCH_API bool etch_text_buffer_replace(EtchTextBuffer* buf, uint32_t byte_start, uint32_t byte_end, EtchStr utf8,
    bool record_undo, uint32_t before_cursor, uint32_t before_anchor,
    uint32_t after_cursor, uint32_t after_anchor, uint8_t op_group, double timestamp_s);
ETCH_API uint32_t etch_text_buffer_byte_length(const EtchTextBuffer* buf);
ETCH_API uint32_t etch_text_buffer_line_count(const EtchTextBuffer* buf);
ETCH_API uint32_t etch_text_buffer_line_start(const EtchTextBuffer* buf, uint32_t line);
ETCH_API uint32_t etch_text_buffer_line_end(const EtchTextBuffer* buf, uint32_t line);
ETCH_API EtchStr etch_text_buffer_line(const EtchTextBuffer* buf, uint32_t line, EtchArena* scratch);
ETCH_API uint32_t etch_text_buffer_copy_range(const EtchTextBuffer* buf, uint32_t byte_start, uint32_t byte_end, char* out_buf, uint32_t cap);
ETCH_API EtchTextLineCol etch_text_buffer_line_col(const EtchTextBuffer* buf, uint32_t byte_offset);
ETCH_API uint32_t etch_text_buffer_byte_offset(const EtchTextBuffer* buf, uint32_t line, uint32_t col_byte);
/* Zero-based UTF-16 code units. A position within a surrogate pair rounds down. */
ETCH_API EtchTextUtf16Position etch_text_buffer_utf16_position(const EtchTextBuffer* buf, uint32_t byte_offset);
ETCH_API uint32_t etch_text_buffer_byte_from_utf16(const EtchTextBuffer* buf, EtchTextUtf16Position position);
ETCH_API uint64_t etch_text_buffer_revision(const EtchTextBuffer* buf);
typedef struct EtchTextEdit { uint32_t byte_start, byte_end; EtchStr text; } EtchTextEdit;
/* Sorted, disjoint ranges use the expected revision's UTF-8 offsets. One undo step. */
ETCH_API bool etch_text_buffer_apply_edits(EtchTextBuffer* buf, uint64_t expected_revision,
    const EtchTextEdit* edits, uint32_t count);
typedef enum EtchTextDiagnosticSeverity {
    ETCH_TEXT_DIAGNOSTIC_ERROR = 0,
    ETCH_TEXT_DIAGNOSTIC_WARNING = 1,
    ETCH_TEXT_DIAGNOSTIC_INFORMATION = 2,
    ETCH_TEXT_DIAGNOSTIC_HINT = 3
} EtchTextDiagnosticSeverity;
typedef struct EtchTextDiagnostic {
    uint32_t byte_start, byte_end;
    EtchTextDiagnosticSeverity severity;
    EtchStr message;
} EtchTextDiagnostic;
ETCH_API bool etch_text_buffer_set_diagnostics(EtchTextBuffer* buf, uint64_t expected_revision,
    const EtchTextDiagnostic* diagnostics, uint32_t count);
ETCH_API void etch_text_buffer_clear_diagnostics(EtchTextBuffer* buf);
typedef struct EtchTextSearchMatch { uint32_t byte_start, byte_end; } EtchTextSearchMatch;
ETCH_API bool etch_text_buffer_find(const EtchTextBuffer* buf, EtchStr needle, uint32_t start_byte,
    bool case_sensitive, bool whole_word, EtchTextSearchMatch* out_match);
ETCH_API uint32_t etch_text_buffer_find_all(const EtchTextBuffer* buf, EtchStr needle,
    bool case_sensitive, bool whole_word, EtchTextSearchMatch* out_matches, uint32_t max_matches);
ETCH_API bool etch_text_buffer_undo(EtchTextBuffer* buf, uint32_t* out_cursor, uint32_t* out_anchor);
ETCH_API bool etch_text_buffer_redo(EtchTextBuffer* buf, uint32_t* out_cursor, uint32_t* out_anchor);
ETCH_API void etch_text_buffer_clear_undo(EtchTextBuffer* buf);

ETCH_API uint32_t etch_tokenize_cpp_hlsl(uint32_t line_index, EtchStr line, EtchSyntaxToken* out_tokens, uint32_t max_tokens, void* user_data);
ETCH_API EtchCodeEditorResult etch_code_editor(const EtchCodeEditorDesc* desc);
ETCH_API void etch_code_editor_set_cursor(uint32_t element_id, uint32_t anchor_byte, uint32_t cursor_byte, bool scroll_into_view);
ETCH_API void etch_code_editor_scroll_to_line(uint32_t element_id, uint32_t line_index, EtchAlignY align);
ETCH_API void etch_code_editor_set_inline_completion(uint32_t editor_id, EtchStr ghost_text);
ETCH_API bool etch_code_editor_completion_accepted(uint32_t editor_id);
ETCH_API bool etch_code_editor_completion_dismissed(uint32_t editor_id);
ETCH_API void etch_code_editor_set_search_highlights(uint32_t editor_id, EtchStr query, bool case_sensitive);
ETCH_API bool etch_code_editor_set_fold(uint32_t editor_id, uint32_t start_line, uint32_t end_line, bool collapsed);
ETCH_API bool etch_code_editor_is_line_folded(uint32_t editor_id, uint32_t line);
ETCH_API bool etch_code_editor_add_cursor(uint32_t editor_id, uint32_t byte_offset);
ETCH_API void etch_code_editor_clear_extra_cursors(uint32_t editor_id);
ETCH_API uint32_t etch_code_editor_cursor_count(uint32_t editor_id);

/* ============================================================================
 * Desktop Widgets
 * ============================================================================ */

ETCH_API bool etch_button(const EtchButtonDesc* desc);
ETCH_API bool etch_checkbox(const EtchCheckboxDesc* desc);
ETCH_API bool etch_toggle_switch(const EtchToggleSwitchDesc* desc);
ETCH_API void etch_progress_bar(const EtchProgressBarDesc* desc);
ETCH_API bool etch_begin_tab_bar(const EtchTabBarDesc* desc);
ETCH_API EtchTabItemResult etch_tab_item(const EtchTabItemDesc* desc);
ETCH_API void etch_end_tab_bar(void);
ETCH_API bool etch_slider(const EtchSliderDesc* desc);
ETCH_API bool etch_drag_value(const EtchDragValueDesc* desc);
ETCH_API void etch_set_tooltip(uint32_t target_id, const EtchTooltipDesc* desc);
ETCH_API void etch_set_tooltip_text(uint32_t target_id, EtchStr text);
ETCH_API bool etch_begin_modal(const EtchModalDesc* desc);
ETCH_API void etch_end_modal(void);
ETCH_API bool etch_combo_box(const EtchComboBoxDesc* desc);
ETCH_API void etch_open_popup(uint32_t popup_id, EtchVec2 anchor_pos);
ETCH_API void etch_close_popup(uint32_t popup_id);
ETCH_API bool etch_is_popup_open(uint32_t popup_id);
ETCH_API bool etch_begin_popup(const EtchPopupDesc* desc);
ETCH_API void etch_end_popup(void);
ETCH_API bool etch_begin_context_menu(uint32_t popup_id, uint32_t target_id);
ETCH_API void etch_end_context_menu(void);
ETCH_API bool etch_begin_menu_bar(const EtchMenuBarDesc* desc);
ETCH_API void etch_end_menu_bar(void);
ETCH_API bool etch_begin_menu(const EtchMenuDesc* desc);
ETCH_API void etch_end_menu(void);
ETCH_API bool etch_menu_item(const EtchMenuItemDesc* desc);
ETCH_API void etch_menu_separator(void);

/* ============================================================================
 * Interaction, Input, OS Drops, Drag Payload, Springs & Containers
 * ============================================================================ */

ETCH_API bool etch_hovered(uint32_t element_id);
ETCH_API bool etch_pressed(uint32_t element_id);
ETCH_API bool etch_clicked(uint32_t element_id);
ETCH_API bool etch_active(uint32_t element_id);
ETCH_API uint32_t etch_active_id(void);
ETCH_API void etch_set_active(uint32_t element_id);
ETCH_API bool etch_focused(uint32_t element_id);
ETCH_API uint32_t etch_focused_id(void);
ETCH_API void etch_request_focus(uint32_t element_id);
ETCH_API void etch_set_cursor(EtchCursor cursor);
ETCH_API EtchRect etch_element_bounds(uint32_t element_id);
/* Last completed frame; named boxes only. Visible bounds are ancestor clip AABBs,
   not a guarantee against sibling occlusion or rounded clip masks. */
typedef struct EtchElementGeometry {
    EtchRect bounds, visible_bounds;
    EtchVec2 local_scale;
    float opacity;
} EtchElementGeometry;
ETCH_API bool etch_element_geometry(uint32_t element_id, EtchElementGeometry* out_geometry);
ETCH_API bool etch_screen_to_element_local(uint32_t element_id, EtchVec2 screen_pos, EtchVec2* out_local);


ETCH_API void etch_push_id(uint32_t scope_id);
ETCH_API void etch_push_id_str(EtchStr label);
ETCH_API void etch_pop_id(void);
ETCH_API uint32_t etch_scoped_id(uint32_t local_id);
ETCH_API uint32_t etch_scoped_id_str(EtchStr label);

ETCH_API uint16_t etch_key_mods(void);
ETCH_API bool etch_key_pressed(uint32_t keycode, bool include_repeat);
ETCH_API bool etch_consume_key_pressed(uint32_t keycode, bool include_repeat);
ETCH_API bool etch_shortcut(uint16_t required_mods, uint32_t keycode, bool allow_when_typing);
ETCH_API bool etch_consume_shortcut(uint16_t required_mods, uint32_t keycode, bool allow_when_typing);

ETCH_API EtchVec2 etch_pointer_pos(void);
ETCH_API EtchVec2 etch_pointer_delta(void);
ETCH_API bool etch_right_clicked(uint32_t element_id);
ETCH_API bool etch_right_clicked_anywhere(void);
ETCH_API bool etch_double_clicked(uint32_t element_id);
ETCH_API bool etch_dragging(uint32_t element_id, float threshold_px);
ETCH_API bool etch_clicked_without_drag(uint32_t element_id, float max_drag_px);
ETCH_API uint32_t etch_topmost_hovered_id(void);
ETCH_API bool etch_begin_cached_subtree(uint32_t id, uint64_t content_hash);
ETCH_API void etch_end_cached_subtree(void);
ETCH_API void etch_invalidate_cached_subtree(uint32_t id, EtchContext* ctx);
ETCH_API bool etch_should_close_popover(uint32_t popover_id, uint32_t trigger_id);

ETCH_API uint32_t etch_dropped_file_count(void);
ETCH_API EtchStr etch_dropped_file_at(uint32_t index);
ETCH_API bool etch_is_os_dragging(void);
ETCH_API EtchVec2 etch_os_drop_pos(void);
ETCH_API bool etch_os_drag_hovering(uint32_t element_id);
ETCH_API uint32_t etch_dropped_file_on_count(uint32_t element_id);
ETCH_API EtchStr etch_dropped_file_on_at(uint32_t element_id, uint32_t index);
ETCH_API uint32_t etch_dropped_text_count(void);
ETCH_API EtchStr etch_dropped_text_at(uint32_t index);
ETCH_API uint32_t etch_dropped_text_on_count(uint32_t element_id);
ETCH_API EtchStr etch_dropped_text_on_at(uint32_t element_id, uint32_t index);
ETCH_API void etch_begin_focus_scope(uint32_t scope_id, bool trap_tab);
ETCH_API void etch_end_focus_scope(void);
ETCH_API bool etch_keyboard_focus_visible(uint32_t element_id);

ETCH_API bool etch_begin_drag_payload(uint32_t source_id, uint32_t kind_hash, EtchStr label, const void* data, uint32_t size, uint64_t u64, uint32_t u32);
ETCH_API bool etch_is_dragging_payload(uint32_t kind_hash);
ETCH_API bool etch_peek_drag_payload(EtchDragPayloadView* out_view, uint32_t kind_hash);
ETCH_API bool etch_accept_drag_payload(uint32_t target_id, uint32_t kind_hash, EtchDragPayloadView* out_view);

ETCH_API struct SDL_Window* etch_create_desktop_window(const char* title, float logical_w, float logical_h, uint64_t flags);
ETCH_API struct SDL_Window* etch_create_desktop_window_on_display(const char* title, float logical_w, float logical_h, uint32_t display_id, uint64_t flags);
ETCH_API struct SDL_Window* etch_create_desktop_window_at_pointer(const char* title, float logical_w, float logical_h, uint64_t flags);
ETCH_API bool etch_set_desktop_window_min_size(struct SDL_Window* window, float logical_w, float logical_h);
ETCH_API float etch_dpi_scale(const EtchContext* ctx);
ETCH_API float etch_hairline_px(const EtchContext* ctx);
ETCH_API float etch_spring_from(uint32_t anim_id, float target, float initial_value, EtchSpringSpec spec);
ETCH_API float etch_spring(uint32_t anim_id, float target, EtchSpringSpec spec);
ETCH_API EtchVec2 etch_spring_vec2(uint32_t anim_id, EtchVec2 target, EtchSpringSpec spec);
ETCH_API EtchColor etch_spring_color(uint32_t anim_id, EtchColor target, EtchSpringSpec spec);
ETCH_API float etch_spring_angle(uint32_t anim_id, float target_rad, EtchSpringSpec spec);
ETCH_API EtchVec2 etch_scroll_delta(void);
ETCH_API EtchVec2 etch_scroll_max(uint32_t element_id);
ETCH_API EtchVec2 etch_scroll_offset(uint32_t element_id);
ETCH_API void etch_set_scroll_offset(uint32_t element_id, EtchVec2 offset, bool stop_momentum);
ETCH_API bool etch_scroll_stuck_to_bottom(uint32_t element_id);
ETCH_API void etch_set_scroll_stuck_to_bottom(uint32_t element_id, bool stuck);

ETCH_API EtchSplitResult etch_begin_split(const EtchSplitDesc* desc);
ETCH_API void etch_split_second(void);
ETCH_API void etch_end_split(void);
ETCH_API float etch_split_ratio(uint32_t split_id);
ETCH_API void etch_set_split_ratio(uint32_t split_id, float ratio);

ETCH_API EtchVirtualListSlice etch_begin_virtual_list(const EtchVirtualListDesc* desc);
ETCH_API void etch_end_virtual_list(void);
ETCH_API void etch_virtual_list_set_item_height(uint32_t list_id, uint32_t row_index, float height_px);
ETCH_API void etch_virtual_list_invalidate_heights(uint32_t list_id);
ETCH_API double etch_virtual_list_scroll_offset(uint32_t list_id);
ETCH_API void etch_set_virtual_list_scroll_offset(uint32_t list_id, double offset_y, bool stop_momentum);
ETCH_API void etch_virtual_list_scroll_to_row(uint32_t list_id, uint32_t row_index, float item_height_px, EtchAlignY align);
ETCH_API bool etch_virtual_list_is_at_bottom(uint32_t list_id);
ETCH_API void etch_virtual_list_scroll_to_bottom(uint32_t list_id, bool stop_momentum);

ETCH_API bool etch_dock_space_initialized(uint32_t dock_id);
ETCH_API uint32_t etch_dock_reset_root(uint32_t dock_id);
ETCH_API bool etch_dock_split_node(uint32_t dock_id, uint32_t node_idx, EtchAxis axis, float default_ratio,
    uint32_t* out_first_node, uint32_t* out_second_node, uint32_t split_widget_id,
    float min_first_px, float min_second_px);
ETCH_API bool etch_dock_add_pane(uint32_t dock_id, uint32_t leaf_node_idx, const EtchDockPaneDesc* pane);
ETCH_API bool etch_dock_dock_pane(uint32_t dock_id, uint32_t pane_id, uint32_t target_pane_id, EtchDockZone zone, float split_ratio);
ETCH_API bool etch_dock_reorder_pane(uint32_t dock_id, uint32_t pane_id, uint32_t target_pane_id, bool insert_after);
ETCH_API void etch_dock_activate_pane(uint32_t dock_id, uint32_t pane_id);
ETCH_API uint32_t etch_dock_active_pane(uint32_t dock_id, uint32_t leaf_node_idx);
ETCH_API uint32_t etch_dock_find_pane_leaf(uint32_t dock_id, uint32_t pane_id);
ETCH_API bool etch_dock_is_pane_open(uint32_t dock_id, uint32_t pane_id);
ETCH_API void etch_dock_set_pane_open(uint32_t dock_id, uint32_t pane_id, bool open);
ETCH_API void etch_begin_dock_space(const EtchDockSpaceDesc* desc);
ETCH_API uint32_t etch_next_dock_pane(void);
ETCH_API void etch_end_dock_space(void);
typedef struct EtchDockTearOffEvent {
    uint32_t dock_id;
    uint32_t pane_id;
    EtchVec2 screen_pos;
    EtchStr title;
} EtchDockTearOffEvent;
ETCH_API bool etch_dock_detach_pane(uint32_t dock_id, uint32_t pane_id);
ETCH_API bool etch_dock_poll_tear_off(EtchDockTearOffEvent* out_event);
ETCH_API bool etch_dock_transfer_pane(uint32_t src_dock_id, uint32_t dst_dock_id, uint32_t pane_id,
    uint32_t target_pane_id, EtchDockZone zone, float split_ratio,
    struct SDL_Window* src_window, struct SDL_Window* dst_window);
ETCH_API uint32_t etch_dock_save_layout_json(uint32_t dock_id, char* out_buf, uint32_t cap);
ETCH_API bool etch_dock_load_layout_json(uint32_t dock_id, EtchStr json);
ETCH_API uint32_t etch_dock_redock_window_panes(struct SDL_Window* closing_window, uint32_t src_dock_id,
    uint32_t dst_dock_id, uint32_t target_pane_id, EtchDockZone zone, struct SDL_Window* dst_window);

typedef struct EtchMimeDragEntry {
    EtchStr mime_type;
    const uint8_t* bytes;
    uint32_t size;
} EtchMimeDragEntry;
ETCH_API bool etch_begin_mime_drag_payload(uint32_t source_id, EtchStr label, const EtchMimeDragEntry* entries, uint32_t count);
ETCH_API uint32_t etch_mime_drag_format_count(void);
ETCH_API bool etch_peek_mime_drag_entry(EtchStr mime_type, EtchMimeDragEntry* out_entry);
ETCH_API bool etch_accept_mime_drag_payload(uint32_t target_id, EtchStr mime_type, EtchMimeDragEntry* out_entry);
typedef struct EtchClipboardMimeData {
    const uint8_t* bytes;
    uint32_t size_bytes;
} EtchClipboardMimeData;
ETCH_API bool etch_set_clipboard_mime(const EtchMimeDragEntry* entries, uint32_t count);
ETCH_API EtchClipboardMimeData etch_get_clipboard_mime(EtchStr mime_type, EtchArena* arena);
ETCH_API bool etch_has_clipboard_mime(EtchStr mime_type);

typedef struct EtchCommandDesc {
    uint32_t struct_size;
    uint32_t id;
    EtchStr name;
    EtchStr title;
    EtchStr category;
    uint16_t shortcut_mods;
    uint32_t shortcut_keycode;
    bool enabled;
    bool allow_when_typing;
    uint32_t scope_id;
    uint16_t chord_mods;
    uint32_t chord_keycode;
} EtchCommandDesc;
ETCH_API EtchCommandDesc etch_command_desc_default(void);
ETCH_API bool etch_register_command(EtchContext* ctx, const EtchCommandDesc* desc);
ETCH_API bool etch_unregister_command(EtchContext* ctx, uint32_t command_id);
ETCH_API bool etch_set_command_enabled(EtchContext* ctx, uint32_t command_id, bool enabled);
ETCH_API bool etch_is_command_enabled(const EtchContext* ctx, uint32_t command_id);
ETCH_API bool etch_rebind_command_shortcut(EtchContext* ctx, uint32_t command_id, uint16_t mods, uint32_t keycode,
    uint16_t chord_mods, uint32_t chord_keycode);
ETCH_API uint32_t etch_find_command_shortcut_conflict(const EtchContext* ctx, uint16_t mods, uint32_t keycode, uint32_t ignore_id);
ETCH_API uint32_t etch_poll_triggered_command(EtchContext* ctx);
ETCH_API bool etch_dispatch_command(EtchContext* ctx, uint32_t command_id);
ETCH_API uint32_t etch_recent_command_count(const EtchContext* ctx);
ETCH_API uint32_t etch_recent_command_at(const EtchContext* ctx, uint32_t index);
ETCH_API uint32_t etch_command_count(const EtchContext* ctx);
ETCH_API bool etch_get_command(const EtchContext* ctx, uint32_t index, EtchCommandDesc* out_desc);

typedef struct EtchAutomationQuery {
    EtchA11yRole role;
    EtchStr label_contains;
    uint32_t required_states;
    uint32_t forbidden_states;
} EtchAutomationQuery;
typedef struct EtchRecordedFrameInput {
    float dt_seconds;
    EtchVec2 pointer_pos;
    bool pointer_down;
    bool right_pointer_down;
    EtchVec2 scroll_delta;
    uint16_t key_mods;
    uint32_t key_pressed_code;
    char typed_utf8[64];
    uint32_t typed_utf8_len;
} EtchRecordedFrameInput;
ETCH_API uint32_t etch_automation_find_nodes(const EtchContext* ctx, const EtchAutomationQuery* query,
    uint32_t* out_ids, uint32_t max_ids, struct SDL_Window* window);
ETCH_API bool etch_automation_assert_node(const EtchContext* ctx, const EtchAutomationQuery* query,
    EtchA11yNode* out_node, struct SDL_Window* window);
ETCH_API float etch_time_scale(const EtchContext* ctx);
ETCH_API void etch_automation_set_recording(EtchContext* ctx, bool enabled);
ETCH_API uint32_t etch_automation_recorded_count(const EtchContext* ctx);
ETCH_API bool etch_automation_get_recorded(const EtchContext* ctx, uint32_t index, EtchRecordedFrameInput* out_input);
ETCH_API void etch_automation_clear_recording(EtchContext* ctx);
ETCH_API bool etch_automation_replay_frame(EtchContext* ctx, uint32_t index, uint32_t viewport_w, uint32_t viewport_h, float dpi_scale);
ETCH_API uint32_t etch_automation_export_failure_report_json(const EtchContext* ctx, char* out_buf, uint32_t cap, struct SDL_Window* window);

ETCH_API EtchTableResult etch_begin_table(const EtchTableDesc* desc);
ETCH_API bool etch_table_next_row(uint32_t* out_row_index, EtchColor row_bg_override, EtchColor row_border_override);
ETCH_API bool etch_table_next_column(void);
typedef struct EtchTableRowResult {
    uint32_t id;
    bool hovered, pressed, clicked, double_clicked, right_clicked;
} EtchTableRowResult;
ETCH_API EtchTableRowResult etch_table_row_result(uint32_t table_id, uint32_t row_index);
ETCH_API void etch_end_table(void);
ETCH_API float etch_table_column_width(uint32_t table_id, uint32_t col_index);
ETCH_API void etch_set_table_column_width(uint32_t table_id, uint32_t col_index, float width_px);
ETCH_API void etch_set_table_sort(uint32_t table_id, uint32_t col_index, bool descending);
ETCH_API void etch_table_scroll_to_row(uint32_t table_id, uint32_t row_index, float row_height_px, EtchAlignY align);
ETCH_API void etch_table_set_frozen_columns(uint32_t table_id, uint32_t frozen_count);
ETCH_API uint32_t etch_table_frozen_columns(uint32_t table_id);
ETCH_API bool etch_table_set_column_order(uint32_t table_id, const uint32_t* order, uint32_t count);
ETCH_API uint32_t etch_table_get_column_order(uint32_t table_id, uint32_t* out_order, uint32_t cap);

ETCH_API EtchTreeNodeResult etch_begin_tree_node(const EtchTreeNodeDesc* desc);
ETCH_API void etch_end_tree_node(void);
ETCH_API bool etch_is_tree_open(uint32_t node_id);
ETCH_API void etch_set_tree_open(uint32_t node_id, bool open);

ETCH_API EtchCanvasState etch_begin_canvas(const EtchCanvasDesc* desc);
ETCH_API void etch_end_canvas(void);
ETCH_API void etch_canvas_wire(const EtchCanvasWireDesc* desc);
ETCH_API EtchCanvasNodeResult etch_begin_canvas_node(const EtchCanvasNodeDesc* desc);
ETCH_API void etch_end_canvas_node(void);
ETCH_API EtchCanvasState etch_canvas_state(uint32_t canvas_id);
ETCH_API void etch_set_canvas_view(uint32_t canvas_id, EtchVec2 pan, float zoom);

ETCH_API void etch_set_debug_hud_open(EtchContext* ctx, bool open);
ETCH_API bool etch_debug_hud_open(const EtchContext* ctx);
ETCH_API void etch_set_debug_wireframe(EtchContext* ctx, bool enabled);
ETCH_API bool etch_debug_wireframe(const EtchContext* ctx);

#ifdef __cplusplus
}
#endif

#endif /* ETCH_C_H */
