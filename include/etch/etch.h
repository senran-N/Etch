// Etch — Pure-GPU UI Engine
#ifndef ETCH_H
#define ETCH_H

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>
#include <type_traits>

#define ETCH_VERSION_MAJOR 0
#define ETCH_VERSION_MINOR 24
#define ETCH_VERSION_PATCH 0
#define ETCH_VERSION_STR_DETAIL(x) #x
#define ETCH_VERSION_STR(x) ETCH_VERSION_STR_DETAIL(x)
#define ETCH_VERSION_STRING ETCH_VERSION_STR(ETCH_VERSION_MAJOR) "." ETCH_VERSION_STR(ETCH_VERSION_MINOR) "." ETCH_VERSION_STR(ETCH_VERSION_PATCH)

#ifndef ETCH_ASSERT
#    if defined(NDEBUG)
#        define ETCH_ASSERT(cond) do { (void)sizeof(cond); } while (0)
#    elif defined(_MSC_VER)
#        define ETCH_ASSERT(cond) do { if (!(cond)) { __debugbreak(); } } while (0)
#    elif defined(__clang__) || defined(__GNUC__)
#        define ETCH_ASSERT(cond) do { if (!(cond)) { __builtin_trap(); } } while (0)
#    else
#        define ETCH_ASSERT(cond) do { if (!(cond)) { *(volatile int*)0 = 0; } } while (0)
#    endif
#endif

#define ETCH_COUNTOF(arr) (sizeof(arr) / sizeof((arr)[0]))

// Opaque SDL3 forward declarations (avoid pulling SDL3 headers into public user code).
struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUTexture;
union SDL_Event;

namespace etch {

// ============================================================================
// Math, Color, Packing & String Slice PODs (Orthodox C++20 Aggregates)
// ============================================================================

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

constexpr Vec2 operator+(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
constexpr Vec2 operator-(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
constexpr Vec2 operator-(Vec2 v) noexcept { return {-v.x, -v.y}; }
constexpr Vec2 operator*(Vec2 a, float s) noexcept { return {a.x * s, a.y * s}; }
constexpr Vec2 operator*(float s, Vec2 a) noexcept { return {a.x * s, a.y * s}; }
constexpr Vec2 operator*(Vec2 a, Vec2 b) noexcept { return {a.x * b.x, a.y * b.y}; }
constexpr Vec2 operator/(Vec2 a, float s) noexcept { return {a.x / s, a.y / s}; }

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4324) // Structure padded due to alignas() for GPU buffer contracts
#endif

struct alignas(16) Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};
static_assert(sizeof(Vec4) == 16 && alignof(Vec4) == 16);

struct alignas(16) Uint4 {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t z = 0;
    uint32_t w = 0;
};
static_assert(sizeof(Uint4) == 16 && alignof(Uint4) == 16);

#ifdef _MSC_VER
#    pragma warning(pop)
#endif

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    constexpr bool is_empty() const noexcept {
        return w <= 0.0f || h <= 0.0f;
    }

    constexpr bool contains(Vec2 p) const noexcept {
        return p.x >= x && p.y >= y && p.x < (x + w) && p.y < (y + h);
    }

    constexpr bool intersects(Rect o) const noexcept {
        return w > 0.0f && h > 0.0f && o.w > 0.0f && o.h > 0.0f &&
               x < (o.x + o.w) && (x + w) > o.x &&
               y < (o.y + o.h) && (y + h) > o.y;
    }
};

// Colors are authored in sRGB [0..255]. Shaders decode sRGB -> linear before alpha blending
// and stem darkening so anti-aliased edges and soft shadows remain physically accurate.
struct Color {
    static Color oklch(float L, float C, float hue_deg, float alpha = 1.0f) noexcept;
    static Color lerp_oklab(Color a, Color b, float t) noexcept;
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;

    static constexpr Color rgba8(uint8_t r8, uint8_t g8, uint8_t b8, uint8_t a8 = 255) noexcept {
        return {r8, g8, b8, a8};
    }

    static constexpr Color rgba(float fr, float fg, float fb, float fa = 1.0f) noexcept {
        const auto to_u8 = [](float v) constexpr noexcept -> uint8_t {
            const float clamped = (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
            return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
        };
        return {to_u8(fr), to_u8(fg), to_u8(fb), to_u8(fa)};
    }

    // Format: 0xRRGGBBAA
    static constexpr Color hex(uint32_t rgba_hex) noexcept {
        return {
            static_cast<uint8_t>((rgba_hex >> 24) & 0xFFu),
            static_cast<uint8_t>((rgba_hex >> 16) & 0xFFu),
            static_cast<uint8_t>((rgba_hex >> 8) & 0xFFu),
            static_cast<uint8_t>(rgba_hex & 0xFFu),
        };
    }

    // Format: 0xRRGGBB with explicit alpha
    static constexpr Color rgb_hex(uint32_t rgb, uint8_t alpha = 255) noexcept {
        return {
            static_cast<uint8_t>((rgb >> 16) & 0xFFu),
            static_cast<uint8_t>((rgb >> 8) & 0xFFu),
            static_cast<uint8_t>(rgb & 0xFFu),
            alpha,
        };
    }

    static constexpr Color transparent() noexcept { return {0, 0, 0, 0}; }
    static constexpr Color theme_auto() noexcept { return {0xFEu, 0x00u, 0xFEu, 0x00u}; }
    static constexpr Color white(uint8_t alpha = 255) noexcept { return {255, 255, 255, alpha}; }
    static constexpr Color black(uint8_t alpha = 255) noexcept { return {0, 0, 0, alpha}; }
    constexpr bool is_theme_auto() const noexcept { return r == 0xFEu && g == 0x00u && b == 0xFEu && a == 0x00u; }

    // Little-endian R8G8B8A8 word: R in bits [0..7], G [8..15], B [16..23], A [24..31].
    constexpr uint32_t pack_rgba8() const noexcept {
        return static_cast<uint32_t>(r) |
               (static_cast<uint32_t>(g) << 8) |
               (static_cast<uint32_t>(b) << 16) |
               (static_cast<uint32_t>(a) << 24);
    }

    // Source-over compositing of straight RGBA colors; preserves RGB over transparency.
    static constexpr Color blend_over(Color under, Color over) noexcept {
        const float a = over.a / 255.0f;
        const float out = a + under.a / 255.0f * (1.0f - a);
        if (out == 0) return transparent();
        Color c = lerp(under, over, a / out);
        c.a = static_cast<uint8_t>(out * 255.0f + 0.5f);
        return c;
    }

    static constexpr Color lerp(Color a, Color b, float t) noexcept {
        const float u = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
        const auto mix_u8 = [u](uint8_t ca, uint8_t cb) constexpr noexcept -> uint8_t {
            const float fa = static_cast<float>(ca);
            const float fb = static_cast<float>(cb);
            return static_cast<uint8_t>(fa + (fb - fa) * u + 0.5f);
        };
        return {
            mix_u8(a.r, b.r),
            mix_u8(a.g, b.g),
            mix_u8(a.b, b.b),
            mix_u8(a.a, b.a),
        };
    }
};

struct Corners {
    float tl = 0.0f;
    float tr = 0.0f;
    float br = 0.0f;
    float bl = 0.0f;

    static constexpr Corners all(float r) noexcept {
        return {r, r, r, r};
    }

    static constexpr Corners theme_auto() noexcept {
        return {-1.0f, -1.0f, -1.0f, -1.0f};
    }

    constexpr bool is_theme_auto() const noexcept {
        return tl < 0.0f && tr < 0.0f && br < 0.0f && bl < 0.0f;
    }
};

constexpr Color resolve_theme_color(Color explicit_col, Color token_col) noexcept {
    return explicit_col.is_theme_auto() ? token_col : explicit_col;
}

constexpr Corners resolve_theme_corners(Corners explicit_rad, Corners token_rad) noexcept {
    return explicit_rad.is_theme_auto() ? token_rad : explicit_rad;
}

struct Padding {
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;

    static constexpr Padding all(float p) noexcept {
        return {p, p, p, p};
    }

    static constexpr Padding xy(float horizontal, float vertical) noexcept {
        return {horizontal, horizontal, vertical, vertical};
    }
};

constexpr uint32_t cstr_len(const char* s) noexcept {
    uint32_t n = 0u;
    if (s) {
        while (s[n] != '\0') {
            ++n;
        }
    }
    return n;
}

struct Str {
    const char* chars = nullptr;
    uint32_t length = 0;
    bool is_static = false;

    constexpr Str() noexcept = default;
    constexpr Str(const char* s, uint32_t len, bool stat = false) noexcept
        : chars(s), length(len), is_static(stat) {}
    constexpr Str(const char* s) noexcept
        : chars(s), length(cstr_len(s)), is_static(false) {}
};

template <size_t N>
constexpr Str str_lit(const char (&literal)[N]) noexcept {
    static_assert(N > 0);
    return Str{literal, static_cast<uint32_t>(N - 1), true};
}

constexpr Str str_cstr(const char* s) noexcept {
    return Str{s};
}

// IEEE-754 float32 -> binary16 (round-to-nearest-even), matching HLSL f16tof32().
constexpr uint16_t pack_f16(float value) noexcept {
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const uint32_t abs_bits = bits & 0x7FFFFFFFu;

    if (abs_bits >= 0x7F800000u) {
        // Inf or quiet NaN
        return static_cast<uint16_t>(sign | ((abs_bits > 0x7F800000u) ? 0x7E00u : 0x7C00u));
    }
    if (abs_bits >= 0x47800000u) {
        // Overflow to +/-Inf
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    if (abs_bits < 0x38800000u) {
        // Subnormal or underflow to signed zero
        if (abs_bits < 0x33000000u) {
            return static_cast<uint16_t>(sign);
        }
        const uint32_t exp = abs_bits >> 23;
        const uint32_t mant = (abs_bits & 0x007FFFFFu) | 0x00800000u;
        const uint32_t total_shift = 126u - exp; // [14..24]
        const uint32_t half = 1u << (total_shift - 1u);
        const uint32_t rem = mant & ((1u << total_shift) - 1u);
        uint32_t q = mant >> total_shift;
        if (rem > half || (rem == half && (q & 1u) != 0u)) {
            ++q;
        }
        return static_cast<uint16_t>(sign | q);
    }

    const uint32_t rounding_bias = ((abs_bits >> 13) & 1u) + 0x0FFFu;
    const uint32_t biased = abs_bits + rounding_bias;
    if (biased >= 0x47800000u) {
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    return static_cast<uint16_t>(sign | ((biased - 0x38000000u) >> 13));
}

constexpr uint32_t pack_f16x2(float lo, float hi) noexcept {
    return static_cast<uint32_t>(pack_f16(lo)) |
           (static_cast<uint32_t>(pack_f16(hi)) << 16);
}

constexpr uint32_t pack_unorm16x2(float u, float v) noexcept {
    const auto to_u16 = [](float x) constexpr noexcept -> uint32_t {
        const float clamped = (x < 0.0f) ? 0.0f : ((x > 1.0f) ? 1.0f : x);
        return static_cast<uint32_t>(clamped * 65535.0f + 0.5f);
    };
    return to_u16(u) | (to_u16(v) << 16);
}

// Compile-time FNV-1a 32-bit semantic ID for cross-frame state lookup.
constexpr uint32_t id(const char* label, uint32_t seed = 0x811C9DC5u) noexcept {
    if (!label) {
        return seed;
    }
    uint32_t hash = seed;
    for (size_t i = 0; label[i] != '\0'; ++i) {
        hash ^= static_cast<uint8_t>(label[i]);
        hash *= 0x01000193u;
    }
    return hash;
}

constexpr uint32_t id_index(uint32_t base_id, uint32_t index) noexcept {
    uint32_t hash = base_id;
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        hash ^= (index >> shift) & 0xFFu;
        hash *= 0x01000193u;
    }
    return hash;
}

// ============================================================================
// Frame Linear Allocator (O(1) Reset, Zero Heap Churn on Hot Path)
// ============================================================================

struct Arena {
    uint8_t* base = nullptr;
    size_t capacity = 0;
    size_t offset = 0;
    size_t peak_offset = 0;

    void reset() noexcept {
        offset = 0;
    }

    size_t used() const noexcept {
        return offset;
    }

    size_t remaining() const noexcept {
        return (offset <= capacity) ? (capacity - offset) : 0;
    }

    size_t mark() const noexcept {
        return offset;
    }

    void rewind(size_t saved_mark) noexcept {
        ETCH_ASSERT(saved_mark <= offset);
        if (saved_mark <= offset) {
            offset = saved_mark;
        }
    }

    // Aligns the actual returned address (base + offset) to a power-of-two boundary.
    void* alloc_bytes(size_t size, size_t align = 16) noexcept {
        ETCH_ASSERT(align != 0 && (align & (align - 1)) == 0);
        if (!base || size == 0 || offset > capacity) {
            return nullptr;
        }
        const uintptr_t cur_addr = reinterpret_cast<uintptr_t>(base) + offset;
        const uintptr_t mask = static_cast<uintptr_t>(align - 1);
        const uintptr_t aligned_addr = (cur_addr + mask) & ~mask;
        const size_t pad = static_cast<size_t>(aligned_addr - cur_addr);
        ETCH_ASSERT(pad <= capacity - offset && "etch::Arena out of memory during alignment");
        if (pad > capacity - offset) {
            return nullptr;
        }
        const size_t aligned_offset = offset + pad;
        ETCH_ASSERT(size <= capacity - aligned_offset && "etch::Arena out of memory");
        if (size > capacity - aligned_offset) {
            return nullptr;
        }
        offset = aligned_offset + size;
        if (offset > peak_offset) {
            peak_offset = offset;
        }
        return reinterpret_cast<void*>(aligned_addr);
    }

    template <typename T>
    T* alloc(size_t count = 1) noexcept {
        static_assert(std::is_trivially_destructible_v<T>, "Arena::alloc requires trivially destructible T");
        if (count == 0 || count > (SIZE_MAX / sizeof(T))) {
            return nullptr;
        }
        const size_t total = count * sizeof(T);
        void* ptr = alloc_bytes(total, alignof(T));
        if (ptr) {
            std::memset(ptr, 0, total);
        }
        return static_cast<T*>(ptr);
    }

    template <typename T>
    T* alloc_uninit(size_t count = 1) noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "Arena::alloc_uninit requires trivially copyable T");
        if (count == 0 || count > (SIZE_MAX / sizeof(T))) {
            return nullptr;
        }
        return static_cast<T*>(alloc_bytes(count * sizeof(T), alignof(T)));
    }

    template <typename T>
    std::span<T> alloc_span(size_t count) noexcept {
        if (count == 0) {
            return {};
        }
        T* ptr = alloc<T>(count);
        return ptr ? std::span<T>(ptr, count) : std::span<T>{};
    }

    Str push_str(const char* s, size_t len) noexcept {
        if (!s || len == 0 || len > 0xFFFFFFFFu) {
            return {};
        }
        char* dst = static_cast<char*>(alloc_bytes(len + 1, 1));
        if (!dst) {
            return {};
        }
        std::memcpy(dst, s, len);
        dst[len] = '\0';
        return Str{dst, static_cast<uint32_t>(len), false};
    }
};

inline Arena arena_from_buffer(void* buffer, size_t capacity_bytes) noexcept {
    return Arena{
        .base = static_cast<uint8_t*>(buffer),
        .capacity = buffer ? capacity_bytes : 0,
        .offset = 0,
        .peak_offset = 0,
    };
}

Arena arena_create(size_t capacity_bytes) noexcept;
void arena_destroy(Arena* arena) noexcept;

// ============================================================================
// GPU Batch Primitive & Uniform Contracts (Strict 16B Alignment, 64B Prim)
// ============================================================================
// Coordinate contract: all bounds, clip rects, radii, border widths, and blur
// radii stored in Prim are expressed in physical framebuffer pixels.
// Color contract: RGBA8 words store sRGB values; shaders decode sRGB -> linear.

enum class PrimKind : uint8_t {
    Rect = 0,
    Shadow = 1,
    Glyph = 2,
    Texture = 3,
    SlugGlyph = 4,
    BlurGlass = 5,
    CanvasGrid = 6,
    BezierWire = 7,
    RingArc = 8,
    SlugFill = 9,
    RotatedRect = 10,
    MeshGrad = 11,
    BorderBeam = 12,
    PunchHole = 13,
    LayerBlur = 14,
};

enum PrimFlags : uint8_t {
    PrimFlag_None = 0,
    PrimFlag_LinearGrad = 1 << 0,
    PrimFlag_RadialGrad = 1 << 1,
    PrimFlag_RoundedClip = 1 << 2,
    PrimFlag_Premultiplied = 1 << 3,
    PrimFlag_StemDarken = 1 << 4,
    PrimFlag_RoundCap = 1 << 5,
    PrimFlag_Continuous = 1 << 5, // Rect/Shadow/BlurGlass superellipse corners.
    PrimFlag_TextClip = 1 << 5, // Glyph/emoji style.zw hold a secondary horizontal clip.
};

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4324) // Structure padded due to alignas() for GPU buffer contracts
#endif

// Matches HLSL StructuredBuffer<Prim> layout in shaders/etch_uber.hlsl (strictly 64 bytes = 4 x 16B).
// Uses alignas(16) so Prim[] can share a single SDL_GPUTransferBuffer right after GlobalsUniform (32B).
//
//   struct Prim {
//       float4 bounds;   // xy = top-left px, zw = width, height px
//       float4 clip;     // xy = clip min px, zw = clip max px
//       uint4  payload;  // xy = uv0/uv1 (UNORM16x2) OR (color1 RGBA8, f16x2 grad_dir); zw = f16x2 radii
//       uint4  style;    // x = header [kind:4 | flags:6 | tex_slot:4 | clip_index_hi:4 | indexed_clip:1 | continuous_clip:1 | clip_r_q2_or_index_lo:12];
//                        // y = color0 sRGB RGBA8; z = border sRGB RGBA8; w = f16x2(border_width, softness)
//   };
struct alignas(16) Prim {
    Vec4 bounds = {};
    Vec4 clip = {};
    Uint4 payload = {};
    Uint4 style = {};

    static constexpr uint32_t pack_header(
        PrimKind kind,
        uint8_t flags = PrimFlag_None,
        uint16_t tex_slot = 0,
        float clip_radius_px = 0.0f) noexcept
    {
        // Negative radius selects continuous clipping; bit 19 is independent of the texture slot.
        const float magnitude = clip_radius_px < 0 ? -clip_radius_px : clip_radius_px;
        const float clamped_r = magnitude > 1023.75f ? 1023.75f : magnitude;
        const uint32_t clip_r_q2 = static_cast<uint32_t>(clamped_r * 4.0f + 0.5f) & 0xFFFu;
        return (static_cast<uint32_t>(kind) & 0xFu) |
               ((static_cast<uint32_t>(flags) & 0x3Fu) << 4) |
               ((static_cast<uint32_t>(tex_slot) & 0xFu) << 10) |
               (clip_radius_px < 0 ? (1u << 19) : 0u) |
               (clip_r_q2 << 20);
    }

    static constexpr Prim make_rect(
        Rect rect,
        Rect clip_rect,
        Color fill,
        Corners radius = {},
        Color border_color = {},
        float border_width = 0.0f,
        float softness = 1.0f,
        float clip_radius = 0.0f) noexcept
    {
        const uint8_t flags = (clip_radius != 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None;
        return Prim{
            .bounds = {rect.x, rect.y, rect.w, rect.h},
            .clip = {clip_rect.x, clip_rect.y, clip_rect.x + clip_rect.w, clip_rect.y + clip_rect.h},
            .payload = {0u, 0u, pack_f16x2(radius.tl, radius.tr), pack_f16x2(radius.br, radius.bl)},
            .style = {
                pack_header(PrimKind::Rect, flags, 0, clip_radius),
                fill.pack_rgba8(),
                border_color.pack_rgba8(),
                pack_f16x2(border_width, softness),
            },
        };
    }

    static constexpr Prim make_gradient_rect(
        Rect rect,
        Rect clip_rect,
        Color color_start,
        Color color_end,
        Vec2 grad_dir,
        Corners radius = {},
        Color border_color = {},
        float border_width = 0.0f,
        float clip_radius = 0.0f,
        bool radial = false,
        float softness = 1.0f) noexcept
    {
        const uint8_t grad_flag = radial ? PrimFlag_RadialGrad : PrimFlag_LinearGrad;
        const uint8_t clip_flag = (clip_radius != 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None;
        return Prim{
            .bounds = {rect.x, rect.y, rect.w, rect.h},
            .clip = {clip_rect.x, clip_rect.y, clip_rect.x + clip_rect.w, clip_rect.y + clip_rect.h},
            .payload = {
                color_end.pack_rgba8(),
                pack_f16x2(grad_dir.x, grad_dir.y),
                pack_f16x2(radius.tl, radius.tr),
                pack_f16x2(radius.br, radius.bl),
            },
            .style = {
                pack_header(PrimKind::Rect, static_cast<uint8_t>(grad_flag | clip_flag), 0, clip_radius),
                color_start.pack_rgba8(),
                border_color.pack_rgba8(),
                pack_f16x2(border_width, softness),
            },
        };
    }

    static constexpr Prim make_mesh_grad(Rect rect, Rect clip_rect,
        Color tl, Color tr, Color br, Color bl, Corners radius = {},
        bool oklab = true, float grain = 0, float phase = 0, bool continuous = false, float clip_radius = 0) noexcept {
        return {.bounds={rect.x,rect.y,rect.w,rect.h},
            .clip={clip_rect.x,clip_rect.y,clip_rect.x+clip_rect.w,clip_rect.y+clip_rect.h},
            .payload={br.pack_rgba8(),bl.pack_rgba8(),pack_f16x2(radius.tl,radius.tr),pack_f16x2(radius.br,radius.bl)},
            .style={pack_header(PrimKind::MeshGrad,uint8_t((oklab ? 1u : 0u)|(continuous ? PrimFlag_Continuous : 0u)|(clip_radius!=0 ? PrimFlag_RoundedClip : 0u)),0,clip_radius),
                tl.pack_rgba8(),tr.pack_rgba8(),pack_f16x2(grain,phase)}};
    }

    static constexpr Prim make_punch_hole(Rect rect, Rect clip_rect, Corners radius = {},
        float softness = 1, bool continuous = false, float clip_radius = 0) noexcept {
        return {.bounds={rect.x,rect.y,rect.w,rect.h},
            .clip={clip_rect.x,clip_rect.y,clip_rect.x+clip_rect.w,clip_rect.y+clip_rect.h},
            .payload={0,0,pack_f16x2(radius.tl,radius.tr),pack_f16x2(radius.br,radius.bl)},
            .style={pack_header(PrimKind::PunchHole,uint8_t((continuous ? PrimFlag_Continuous : 0u)|(clip_radius!=0 ? PrimFlag_RoundedClip : 0u)),0,clip_radius),
                Color::white().pack_rgba8(),0,pack_f16x2(0,softness)}};
    }

    static constexpr Prim make_border_beam(Rect rect, Rect clip_rect, Color color, Color base_color,
        Corners radius = {}, float width = 1.5f, float angle_rad = 0, float sweep_fraction = .25f,
        float glow_px = 0, float top_rim_alpha = 0, float softness = 1, bool symmetric_dual = false,
        bool continuous = false, float clip_radius = 0) noexcept {
        return {.bounds={rect.x,rect.y,rect.w,rect.h},
            .clip={clip_rect.x,clip_rect.y,clip_rect.x+clip_rect.w,clip_rect.y+clip_rect.h},
            .payload={pack_f16x2(angle_rad,sweep_fraction),pack_f16x2(width,glow_px),pack_f16x2(radius.tl,radius.tr),pack_f16x2(radius.br,radius.bl)},
            .style={pack_header(PrimKind::BorderBeam,uint8_t((symmetric_dual ? 1u : 0u)|(top_rim_alpha>0 ? 2u : 0u)|(continuous ? PrimFlag_Continuous : 0u)|(clip_radius!=0 ? PrimFlag_RoundedClip : 0u)),0,clip_radius),
                color.pack_rgba8(),base_color.pack_rgba8(),pack_f16x2(top_rim_alpha,softness)}};
    }

    static constexpr Prim make_shadow(
        Rect rect,
        Rect clip_rect,
        Color shadow_color,
        Corners radius = {},
        float blur_radius = 12.0f,
        float clip_radius = 0.0f, Vec2 offset = {}, float spread = 0.0f) noexcept
    {
        radius = {radius.tl + spread > 0 ? radius.tl + spread : 0, radius.tr + spread > 0 ? radius.tr + spread : 0,
            radius.br + spread > 0 ? radius.br + spread : 0, radius.bl + spread > 0 ? radius.bl + spread : 0};
        const uint8_t flags = (clip_radius != 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None;
        return Prim{
            .bounds = {rect.x + offset.x - spread, rect.y + offset.y - spread, rect.w + 2 * spread, rect.h + 2 * spread},
            .clip = {clip_rect.x, clip_rect.y, clip_rect.x + clip_rect.w, clip_rect.y + clip_rect.h},
            .payload = {0u, 0u, pack_f16x2(radius.tl, radius.tr), pack_f16x2(radius.br, radius.bl)},
            .style = {
                pack_header(PrimKind::Shadow, flags, 0, clip_radius),
                shadow_color.pack_rgba8(),
                0u,
                pack_f16x2(0.0f, blur_radius),
            },
        };
    }

    static constexpr Prim make_glyph(
        Rect rect,
        Rect clip_rect,
        Color tint,
        Vec2 uv_min,
        Vec2 uv_max,
        uint16_t atlas_tex_slot = 0,
        float clip_radius = 0.0f,
        uint8_t flags = PrimFlag_StemDarken) noexcept
    {
        const uint8_t all_flags = static_cast<uint8_t>(flags | ((clip_radius != 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None));
        return Prim{
            .bounds = {rect.x, rect.y, rect.w, rect.h},
            .clip = {clip_rect.x, clip_rect.y, clip_rect.x + clip_rect.w, clip_rect.y + clip_rect.h},
            .payload = {
                pack_unorm16x2(uv_min.x, uv_min.y),
                pack_unorm16x2(uv_max.x, uv_max.y),
                0u,
                0u,
            },
            .style = {
                pack_header(PrimKind::Glyph, all_flags, atlas_tex_slot, clip_radius),
                tint.pack_rgba8(),
                0u,
                0u,
            },
        };
    }

    static Prim make_slug_glyph(
        Rect rect,
        Rect clip_rect,
        Color tint,
        uint32_t band_offset,
        uint16_t num_hbands,
        uint16_t num_vbands,
        float clip_radius = 0.0f,
        uint8_t flags = PrimFlag_StemDarken,
        float angle_rad = 0.0f) noexcept
    {
        const uint8_t all_flags = static_cast<uint8_t>(flags | ((clip_radius != 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None));
        return Prim{
            .bounds = {rect.x, rect.y, rect.w, rect.h},
            .clip = {clip_rect.x, clip_rect.y, clip_rect.x + clip_rect.w, clip_rect.y + clip_rect.h},
            .payload = {
                band_offset,
                static_cast<uint32_t>(num_hbands) | (static_cast<uint32_t>(num_vbands) << 16),
                angle_rad == 0.0f ? 0u : std::bit_cast<uint32_t>(std::atan2(std::sin(angle_rad), std::cos(angle_rad))),
                0u,
            },
            .style = {
                pack_header(PrimKind::SlugGlyph, all_flags, 0u, clip_radius),
                tint.pack_rgba8(),
                0u,
                0u,
            },
        };
    }

    static constexpr Prim make_slug_fill(Rect rect, Rect clip_rect, Color fill,
        uint32_t band_offset, uint16_t hbands, uint16_t vbands, Color fill_end = {},
        Vec2 gradient_dir = {0, 1}, bool radial = false, float clip_radius = 0) noexcept
    {
        const uint8_t flags = static_cast<uint8_t>((clip_radius != 0 ? PrimFlag_RoundedClip : 0) |
            ((fill_end.pack_rgba8() != 0 || radial) ? (radial ? PrimFlag_RadialGrad : PrimFlag_LinearGrad) : 0));
        return Prim{.bounds = {rect.x, rect.y, rect.w, rect.h},
            .clip = {clip_rect.x, clip_rect.y, clip_rect.x + clip_rect.w, clip_rect.y + clip_rect.h},
            .payload = {band_offset, uint32_t(hbands) | (uint32_t(vbands) << 16), fill_end.pack_rgba8(), pack_f16x2(gradient_dir.x, gradient_dir.y)},
            .style = {pack_header(PrimKind::SlugFill, flags, 0, clip_radius), fill.pack_rgba8(), 0, 0}};
    }

    static Prim make_rotated_rect(Rect rect, Rect clip_rect, Color fill, float angle_rad,
        Corners radii = {}, Color border = {}, float border_width = 0, Color fill_end = {},
        Vec2 gradient_dir = {0, 1}, bool radial = false, float clip_radius = 0) noexcept
    {
        Prim p = (fill_end.pack_rgba8() != 0 || radial)
            ? make_gradient_rect(rect, clip_rect, fill, fill_end, gradient_dir, radii, border, border_width, clip_radius, radial)
            : make_rect(rect, clip_rect, fill, radii, border, border_width, 1, clip_radius);
        p.style.x = (p.style.x & ~0xFu) | uint32_t(PrimKind::RotatedRect);
        p.style.w = pack_f16x2(border_width, std::atan2(std::sin(angle_rad), std::cos(angle_rad)));
        return p;
    }

    static constexpr Prim make_texture(
        Rect rect,
        Rect clip_rect,
        Vec2 uv_min,
        Vec2 uv_max,
        uint16_t tex_slot,
        Corners radius = {},
        Color tint = {255, 255, 255, 255},
        Color border_color = {},
        float border_width = 0.0f,
        uint8_t flags = PrimFlag_None,
        float clip_radius = 0.0f) noexcept
    {
        const uint8_t all_flags = static_cast<uint8_t>(flags | ((clip_radius != 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None));
        return Prim{
            .bounds = {rect.x, rect.y, rect.w, rect.h},
            .clip = {clip_rect.x, clip_rect.y, clip_rect.x + clip_rect.w, clip_rect.y + clip_rect.h},
            .payload = {
                pack_unorm16x2(uv_min.x, uv_min.y),
                pack_unorm16x2(uv_max.x, uv_max.y),
                pack_f16x2(radius.tl, radius.tr),
                pack_f16x2(radius.br, radius.bl),
            },
            .style = {
                pack_header(PrimKind::Texture, all_flags, tex_slot, clip_radius),
                tint.pack_rgba8(),
                border_color.pack_rgba8(),
                pack_f16x2(border_width, 1.0f),
            },
        };
    }

    static constexpr Prim make_blur_glass(
        Rect rect,
        Rect clip_rect,
        Color tint,
        Corners radius = {},
        Color border_color = {},
        float border_width = 0.0f,
        uint16_t tex_slot = 0,
        float clip_radius = 0.0f) noexcept
    {
        const uint8_t all_flags = static_cast<uint8_t>((clip_radius != 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None);
        return Prim{
            .bounds = {rect.x, rect.y, rect.w, rect.h},
            .clip = {clip_rect.x, clip_rect.y, clip_rect.x + clip_rect.w, clip_rect.y + clip_rect.h},
            .payload = {
                0u,
                0u,
                pack_f16x2(radius.tl, radius.tr),
                pack_f16x2(radius.br, radius.bl),
            },
            .style = {
                pack_header(PrimKind::BlurGlass, all_flags, tex_slot, clip_radius),
                tint.pack_rgba8(),
                border_color.pack_rgba8(),
                pack_f16x2(border_width, 1.0f),
            },
        };
    }

    static constexpr Prim make_layer_blur(
        Rect rect,
        Rect clip_rect,
        float blend_t,
        uint32_t slot_hi = 0u,
        float clip_radius = 0.0f) noexcept
    {
        const uint8_t all_flags = static_cast<uint8_t>((clip_radius != 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None);
        return Prim{
            .bounds = {rect.x, rect.y, rect.w, rect.h},
            .clip = {clip_rect.x, clip_rect.y, clip_rect.x + clip_rect.w, clip_rect.y + clip_rect.h},
            .payload = {std::bit_cast<uint32_t>(blend_t), slot_hi, 0u, 0u},
            .style = {
                pack_header(PrimKind::LayerBlur, all_flags, 0u, clip_radius),
                Color::white().pack_rgba8(),
                0u,
                0u,
            },
        };
    }

    static constexpr Prim make_canvas_grid(
        Rect rect,
        Rect clip_rect,
        Vec2 phase_px,
        float minor_step_px,
        float major_step_px,
        Vec2 origin_rel_px,
        Color minor_color,
        Color major_color,
        float dot_radius_px = 1.35f,
        float minor_alpha_scale = 1.0f,
        float clip_radius = 0.0f) noexcept
    {
        const auto clamp_origin = [](float v) constexpr noexcept -> float {
            return (v < -32000.0f) ? -32000.0f : ((v > 32000.0f) ? 32000.0f : v);
        };
        const uint8_t flags = (clip_radius != 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None;
        return Prim{
            .bounds = {rect.x, rect.y, rect.w, rect.h},
            .clip = {clip_rect.x, clip_rect.y, clip_rect.x + clip_rect.w, clip_rect.y + clip_rect.h},
            .payload = {
                std::bit_cast<uint32_t>(phase_px.x),
                std::bit_cast<uint32_t>(phase_px.y),
                pack_f16x2(minor_step_px, major_step_px),
                pack_f16x2(clamp_origin(origin_rel_px.x), clamp_origin(origin_rel_px.y)),
            },
            .style = {
                pack_header(PrimKind::CanvasGrid, flags, 0u, clip_radius),
                minor_color.pack_rgba8(),
                major_color.pack_rgba8(),
                pack_f16x2(dot_radius_px, minor_alpha_scale),
            },
        };
    }

    static constexpr Prim make_bezier_wire(
        Vec2 q0_px,
        Vec2 q1_px,
        Vec2 q2_px,
        Rect clip_rect,
        Color color_start,
        Color color_end,
        float half_width_px = 1.5f,
        Color glow_color = {},
        float glow_px = 0.0f,
        float clip_radius = 0.0f) noexcept
    {
        const auto min3f = [](float a, float b, float c) constexpr noexcept -> float {
            const float ab = (a < b) ? a : b;
            return (ab < c) ? ab : c;
        };
        const auto max3f = [](float a, float b, float c) constexpr noexcept -> float {
            const float ab = (a > b) ? a : b;
            return (ab > c) ? ab : c;
        };
        const float hw = (half_width_px > 0.5f) ? half_width_px : 0.5f;
        const float gw = (glow_px > 0.0f) ? glow_px : 0.0f;
        const float margin = hw + gw + 2.0f;
        const float bx = min3f(q0_px.x, q1_px.x, q2_px.x) - margin;
        const float by = min3f(q0_px.y, q1_px.y, q2_px.y) - margin;
        const float raw_w = (max3f(q0_px.x, q1_px.x, q2_px.x) + margin) - bx;
        const float raw_h = (max3f(q0_px.y, q1_px.y, q2_px.y) + margin) - by;
        const float bw = (raw_w > 1.0f) ? raw_w : 1.0f;
        const float bh = (raw_h > 1.0f) ? raw_h : 1.0f;
        const uint8_t flags = (clip_radius != 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None;
        return Prim{
            .bounds = {bx, by, bw, bh},
            .clip = {clip_rect.x, clip_rect.y, clip_rect.x + clip_rect.w, clip_rect.y + clip_rect.h},
            .payload = {
                pack_unorm16x2((q0_px.x - bx) / bw, (q0_px.y - by) / bh),
                pack_unorm16x2((q1_px.x - bx) / bw, (q1_px.y - by) / bh),
                pack_unorm16x2((q2_px.x - bx) / bw, (q2_px.y - by) / bh),
                color_end.pack_rgba8(),
            },
            .style = {
                pack_header(PrimKind::BezierWire, flags, 0u, clip_radius),
                color_start.pack_rgba8(),
                glow_color.pack_rgba8(),
                pack_f16x2(hw, gw),
            },
        };
    }

    static constexpr Prim make_ring_arc(
        Vec2 center_px,
        float radius_px,
        float half_thickness_px,
        float start_angle_rad,
        float sweep_angle_rad,
        Rect clip_rect,
        Color color_start,
        Color color_end,
        bool round_caps = true,
        Color glow_color = {},
        float glow_px = 0.0f,
        float clip_radius = 0.0f) noexcept
    {
        const float r = (radius_px > 0.5f) ? radius_px : 0.5f;
        const float ht = (half_thickness_px > 0.5f) ? half_thickness_px : 0.5f;
        const float gw = (glow_px > 0.0f) ? glow_px : 0.0f;
        const float ext = r + ht + gw + 2.0f;
        const uint8_t flags = static_cast<uint8_t>(
            (round_caps ? PrimFlag_RoundCap : PrimFlag_None) |
            ((clip_radius != 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None));
        return Prim{
            .bounds = {center_px.x - ext, center_px.y - ext, 2.0f * ext, 2.0f * ext},
            .clip = {clip_rect.x, clip_rect.y, clip_rect.x + clip_rect.w, clip_rect.y + clip_rect.h},
            .payload = {
                std::bit_cast<uint32_t>(start_angle_rad),
                std::bit_cast<uint32_t>(sweep_angle_rad),
                pack_f16x2(r, ht),
                color_end.pack_rgba8(),
            },
            .style = {
                pack_header(PrimKind::RingArc, flags, 0u, clip_radius),
                color_start.pack_rgba8(),
                glow_color.pack_rgba8(),
                pack_f16x2(ht, gw),
            },
        };
    }
};

static_assert(sizeof(Prim) == 64);
static_assert(sizeof(Prim) % 16 == 0);
static_assert(alignof(Prim) == 16);
static_assert(std::is_standard_layout_v<Prim>);
static_assert(std::is_trivially_copyable_v<Prim>);
static_assert(std::is_aggregate_v<Prim>);

struct alignas(16) GlobalsUniform {
    Vec2 viewport_size = {};
    Vec2 inv_viewport_size = {};
    float dpi_scale = 1.0f;
    float time_seconds = 0.0f;
    uint32_t frame_index = 0;
    uint32_t flags = 0;
};

static_assert(sizeof(GlobalsUniform) == 32);
static_assert(sizeof(GlobalsUniform) % 16 == 0);
static_assert(alignof(GlobalsUniform) == 16);
static_assert(std::is_standard_layout_v<GlobalsUniform>);
static_assert(std::is_trivially_copyable_v<GlobalsUniform>);

#ifdef _MSC_VER
#    pragma warning(pop)
#endif

// ============================================================================
// Declarative UI & Context Lifecycle API
// ============================================================================

enum class A11yRole : uint8_t { None, Window, Group, Region, Toolbar, MenuBar, Menu, MenuItem, TabList, Tab, Dialog, ScrollArea, List, ListItem, Table, Tree, TreeItem, StaticText, Button, Checkbox, Switch, Slider, SpinButton, ProgressBar, ComboBox, TextInput, CodeEditor, Canvas, Tooltip };
enum A11yStateFlags : uint32_t { A11yState_None = 0u, A11yState_Focused = 1u << 0, A11yState_Hovered = 1u << 1, A11yState_Pressed = 1u << 2, A11yState_Checked = 1u << 3, A11yState_Selected = 1u << 4, A11yState_Expanded = 1u << 5, A11yState_Disabled = 1u << 6, A11yState_ReadOnly = 1u << 7, A11yState_Modal = 1u << 8, A11yState_Multiline = 1u << 9, A11yState_Password = 1u << 10 };
enum class A11yAction : uint8_t { Focus, Press, Increment, Decrement, Expand, Collapse, Dismiss, SetValue };
constexpr uint32_t a11y_action_bit(A11yAction a) noexcept { return 1u << static_cast<uint8_t>(a); }
struct A11yNode {
 uint32_t id = 0, parent_id = 0, first_child_idx = 0xFFFFFFFFu, next_sibling_idx = 0xFFFFFFFFu;
 A11yRole role = A11yRole::None;
 uint32_t state_flags = A11yState_None, actions_mask = 0u;
 Rect bounds = {}; Str label = {}, value_text = {};
 float value_num = 0, min_value = 0, max_value = 0;
};
struct A11yOverrideDesc {
 uint32_t element_id = 0; A11yRole role = A11yRole::None; Str label = {}, value_text = {};
 uint32_t add_states = 0, clear_states = 0, actions_mask = 0;
 bool has_role = false, has_label = false, has_value_text = false, has_actions_mask = false;
};
struct Context;
using WindowRefreshFn = void (*)(Context* ctx, SDL_Window* window, float dt_seconds, void* user_data) noexcept;

struct ContextDesc {
    uint32_t max_layout_elements = 16384u;
    SDL_Window* window = nullptr;
    size_t frame_arena_bytes = 4u * 1024u * 1024u;
    // Initial reserve, clamped [64..4194304]; doubles on demand up to 4194304 prims.
    uint32_t max_prims_per_frame = 65536u;
    const char* font_path = nullptr;
    float font_size_px = 16.0f;
    bool enable_gpu_debug = false;
    bool headless = false;
    // Optional SDL GPU backend name (vulkan, direct3d12, metal).
    const char* gpu_driver = nullptr;
    WindowRefreshFn on_window_refresh = nullptr;
    void* refresh_user_data = nullptr;
    bool enable_os_a11y = true;
    bool respect_reduced_motion = true;
};

enum KeyMod : uint16_t {
    Mod_None    = 0u,
    Mod_Shift   = 1u << 0,
    Mod_Ctrl    = 1u << 1,
    Mod_Alt     = 1u << 2,
    Mod_Super   = 1u << 3,
#if defined(__APPLE__)
    Mod_Primary = Mod_Super,
#else
    Mod_Primary = Mod_Ctrl,
#endif
};

struct WindowFrameDesc {
    float dt = 0.0f;
    float min_scale = 0.0f;
    bool pixel_snap = true;
    float bloom_intensity = 0, bloom_threshold = .7f;
};

enum class SystemTheme : uint8_t { Unknown = 0, Light = 1, Dark = 2 };
SystemTheme system_theme() noexcept;
bool system_dark_mode(bool default_when_unknown = true) noexcept;
bool frame_in_progress(const Context* ctx) noexcept;

enum class ThemeId : uint8_t {
    Dark = 0,
    Light = 1,
    HighContrast = 2,
    Industrial = 3,
    Custom = 4,
};

struct Theme {
    ThemeId id = ThemeId::Dark;
    Color bg_canvas = Color::rgb_hex(0x0E1014);
    Color bg_surface = Color::rgb_hex(0x0F1522);
    Color bg_elevated = Color::rgb_hex(0x111726);
    Color bg_chrome = Color::rgb_hex(0x0B101A);
    Color bg_popup = Color::rgb_hex(0x111827);
    Color bg_tooltip = Color::rgb_hex(0x0F172A);
    Color bg_input = Color::rgb_hex(0x0F1420);
    Color bg_table = Color::rgb_hex(0x090D16);
    Color bg_node_canvas = Color::rgb_hex(0x090C12);
    Color surface_control = Color::rgb_hex(0x1E293B);
    Color surface_hover = Color::rgb_hex(0x334155);
    Color surface_active = Color::rgb_hex(0x162033);
    Color surface_disabled = Color::rgb_hex(0x18202C);
    Color surface_subtle = Color::rgba8(30, 41, 59, 190);
    Color surface_subtle_hover = Color::rgba8(51, 65, 85, 225);
    Color surface_subtle_body = Color::rgba8(15, 23, 42, 150);
    Color surface_selected = Color::rgb_hex(0x1A2942);
    Color text_primary = Color::white();
    Color text_secondary = Color::rgb_hex(0xE2E8F0);
    Color text_body = Color::rgb_hex(0xF1F5F9);
    Color text_code = Color::rgb_hex(0xCBD5E1);
    Color text_muted = Color::rgb_hex(0x94A3B8);
    Color text_disabled = Color::rgb_hex(0x64748B);
    Color accent = Color::rgb_hex(0x38BDF8);
    Color accent_secondary = Color::rgb_hex(0x818CF8);
    Color accent_primary = Color::rgb_hex(0x0369A1);
    Color focus_ring = Color::rgb_hex(0x60A5FA);
    Color danger = Color::rgb_hex(0xB91C1C);
    Color warning = Color::rgb_hex(0xFBBF24);
    Color border_subtle = Color::rgba8(148, 163, 184, 38);
    Color border_line = Color::rgba8(148, 163, 184, 34);
    Color border_input = Color::rgba8(148, 163, 184, 52);
    Color border_node = Color::rgba8(148, 163, 184, 56);
    Color border_control = Color::rgb_hex(0x334155);
    Color border_tooltip = Color::rgb_hex(0x475569);
    Color selection = Color::rgba8(56, 189, 248, 88);
    Color selection_hover = Color::rgba8(56, 189, 248, 22);
    Color zebra_row = Color::rgba8(30, 41, 59, 44);
    Color active_line = Color::rgba8(148, 163, 184, 16);
    Color area_fill_top = Color::rgba8(56, 189, 248, 96);
    Color area_fill_bottom = Color::rgba8(56, 189, 248, 0);
    Color divider_hover = Color::rgba8(56, 189, 248, 165);
    Color divider_thumb_hover = Color::rgba8(226, 232, 240, 220);
    Color grid_minor = Color::rgba8(148, 163, 184, 40);
    Color grid_major = Color::rgba8(148, 163, 184, 28);
    Color canvas_border = Color::rgba8(148, 163, 184, 44);
    Color marquee_fill = Color::rgba(0.28f, 0.56f, 0.96f, 0.16f);
    Color marquee_border = Color::rgba(0.38f, 0.68f, 1.00f, 0.85f);
    Color backdrop = Color::rgba8(0, 0, 0, 150);
    Color shadow = Color::rgba8(0, 0, 0, 130);
    Color scrollbar_track = Color::rgba8(255, 255, 255, 18);
    Color scrollbar_thumb = Color::rgba8(148, 163, 184, 125);
    Color scrollbar_thumb_hover = Color::rgba8(148, 163, 184, 195);
    Color scrollbar_thumb_drag = Color::rgba8(96, 165, 250, 230);
    Color syntax_comment = Color::rgb_hex(0x64748B);
    Color syntax_string = Color::rgb_hex(0xA3BE8C);
    Color syntax_number = Color::rgb_hex(0xB48EAD);
    Color syntax_keyword = Color::rgb_hex(0x81A1C1);
    float radius_small_px = 5.0f;
    float radius_control_px = 6.0f;
    float radius_table_px = 8.0f;
    float radius_input_px = 9.0f;
    float radius_panel_px = 10.0f;
    float radius_pill_px = 999.0f;

    static constexpr Theme dark() noexcept { return Theme{}; }
    static Theme light() noexcept;
    static Theme high_contrast() noexcept;
    static Theme industrial() noexcept;
    static Theme from_seed(Color seed, bool dark_mode = true, float chroma_scale = 1.0f) noexcept;
    Color on_primary = Color::white();
    Color on_error = Color::white();
};

void set_theme(const Theme& t, Context* ctx = nullptr) noexcept;
const Theme& theme(const Context* ctx = nullptr) noexcept;
void push_theme(const Theme& t) noexcept;
void pop_theme() noexcept;

struct FrameDesc {
    uint32_t viewport_w = 1280;
    uint32_t viewport_h = 720;
    float dpi_scale = 1.0f;
    float dt_seconds = 0.0f;
    Vec2 pointer_pos = {NAN, NAN}; // Unspecified; finite negative coordinates are valid.
    bool pointer_down = false;
    Vec2 scroll_delta = {};
    bool right_pointer_down = false;
    uint8_t pointer_clicks = 0;      // 0 = use event clicks (1); 2 = double-click
    uint16_t key_mods = Mod_None;
    uint32_t key_pressed_code = 0;   // Optional single-frame SDL_Keycode injection
    bool pixel_snap = true; // Align axis-aligned rectangle edges to physical pixels.
    float bloom_intensity = 0; // Opt-in whole-frame emissive bloom.
    float bloom_threshold = .7f; // Linear luminance threshold in [0,1].
};

struct FrameStats {
    uint32_t a11y_node_count = 0u;
    uint32_t prim_count = 0;
    uint32_t prim_capacity = 0;
    uint32_t draw_call_count = 0;
    uint32_t blur_pass_count = 0;
    uint32_t slug_glyph_count = 0;
    uint32_t slug_curve_count = 0;
    uint32_t dynamic_slug_curve_count = 0;
    size_t arena_used_bytes = 0;
    size_t arena_peak_bytes = 0;
    double layout_ms = 0.0;
    double gpu_submit_ms = 0.0;
    uint32_t active_animations = 0;
    bool idle_sleeping = false;
    int32_t ime_cursor_offset_win = 0; // Cursor within the IME area, in window pixels.
    uint32_t sprite_count = 0;
    uint32_t sprite_atlas_size = 0;
};

enum class SizingKind : uint8_t {
    Fit = 0,
    Grow = 1,
    Fixed = 2,
    Percent = 3,
};

struct Sizing {
    SizingKind kind = SizingKind::Fit;
    float value = 0.0f;
    float min = 0.0f;
    float max = 0.0f;

    static constexpr Sizing fit(float min_px = 0.0f, float max_px = 0.0f) noexcept {
        return {SizingKind::Fit, 0.0f, min_px, max_px};
    }

    static constexpr Sizing grow(float min_px = 0.0f, float max_px = 0.0f) noexcept {
        return {SizingKind::Grow, 0.0f, min_px, max_px};
    }

    static constexpr Sizing fixed(float px) noexcept {
        return {SizingKind::Fixed, px, px, px};
    }

    static constexpr Sizing percent(float ratio_0_to_1) noexcept {
        return {SizingKind::Percent, ratio_0_to_1, 0.0f, 0.0f};
    }
};

enum class Axis : uint8_t {
    Row = 0,
    Col = 1,
};

enum class AlignX : uint8_t {
    Left = 0,
    Center = 1,
    Right = 2,
};

enum class AlignY : uint8_t {
    Top = 0,
    Center = 1,
    Bottom = 2,
};

enum class Cursor : uint8_t {
    Arrow = 0,
    Hand,
    IBeam,
    ResizeEW,
    ResizeNS,
    ResizeNWSE,
    ResizeNESW,
    Move,
    NotAllowed,
    Crosshair,
};

enum class Anchor : uint8_t {
    TopLeft = 0,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

enum class AttachTo : uint8_t {
    None = 0,
    Parent,
    Element,
    Root,
};

struct SpringSpec {
    float response = 0.20f;
    float damping_ratio = 1.0f;
    static constexpr SpringSpec snappy() noexcept { return {0.18f, 1.0f}; }
    static constexpr SpringSpec fluid() noexcept { return {0.32f, 1.0f}; }
    static constexpr SpringSpec bouncy() noexcept { return {0.36f, 0.65f}; }
    static constexpr SpringSpec gentle() noexcept { return {0.50f, 1.0f}; }
};

// Zero pauses animation time; reduced-motion preference settles springs immediately.
void set_time_scale(Context* ctx, float scale) noexcept;
struct LayerDesc {
    float opacity = 1.0f;
    Vec2 translate = {};
    Vec2 scale = {1,1};
    Vec2 pivot_norm = {.5f,.5f};
    Vec2 pivot_px = {NAN,NAN};
    bool animating = false;
    float blur_px = 0.0f;
    bool blend_plus_lighter = false;
};
void push_layer(const LayerDesc& desc) noexcept;
void pop_layer() noexcept;
struct PresenceResult {
    bool visible = false;
    float opacity = 0.0f;
    bool exiting = false;
};
PresenceResult presence(uint32_t id, bool mounted, SpringSpec spec = SpringSpec::fluid()) noexcept;
struct GridTrack {
    SizingKind kind = SizingKind::Grow;
    float value = 1.0f;
    float min_px = 0.0f;
    float max_px = 0.0f;
    static constexpr GridTrack px(float v) noexcept { return {SizingKind::Fixed, v, v, v}; }
    static constexpr GridTrack fr(float v = 1, float lo = 0, float hi = 0) noexcept { return {SizingKind::Grow, v, lo, hi}; }
    static constexpr GridTrack auto_fit(float lo = 0, float hi = 0) noexcept { return {SizingKind::Fit, 0, lo, hi}; }
    static constexpr GridTrack percent(float v, float lo = 0, float hi = 0) noexcept { return {SizingKind::Percent, v, lo, hi}; }
};

enum class BorderAlign : uint8_t { Inside=0, Center=1, Outside=2 };

struct BorderBeamStyle {
    Color color = {}, base_color = {};
    float width = 1.5f, angle_rad = 0, sweep_fraction = .25f, glow_px = 0, speed = 1, softness = 1;
    bool beam_active = false, symmetric_dual = false;
};

struct BoxDesc {
    uint32_t id = 0;
    Axis axis = Axis::Row;
    Sizing width = Sizing::fit();
    Sizing height = Sizing::fit();
    Padding padding = {};
    float gap = 0.0f;
    AlignX align_x = AlignX::Left;
    AlignY align_y = AlignY::Top;
    Color background = {};
    Color background_end = {};
    Vec2 gradient_dir = {0.0f, 1.0f};
    bool radial_gradient = false;
    Color border_color = {};
    float border_width = 0.0f;
    Corners radius = {};
    Color shadow_color = {};
    float shadow_blur = 0.0f;
    Vec2 shadow_offset = {};
    bool clip_children = false;
    bool scroll_x = false;
    bool scroll_y = false;
    bool show_scrollbar = true;
    bool focusable = false;
    AttachTo attach_to = AttachTo::None;
    uint32_t attach_element_id = 0;
    Anchor anchor_element = Anchor::TopLeft;
    Anchor anchor_parent = Anchor::BottomLeft;
    Vec2 floating_offset = {};
    int16_t z_index = 10;
    bool pointer_passthrough = false; // Layout ID only; descendants remain interactive. Floating boxes also pass through capture.
    bool clip_to_parent = false;
    float backdrop_blur = 0.0f;
    bool clamp_to_viewport = false;
    bool wrap = false;
    float row_gap = -1.0f;
    float aspect_ratio = 0.0f;
    uint16_t grid_columns = 0; // Up to 128 columns; explicit tracks take precedence.
    std::span<const GridTrack> grid_tracks = {}; // Copied into frame storage by begin_box.
    uint16_t grid_col_span = 1;
    bool shrink_width = false; // Fit/Grow may shrink below content width; clips overflow.
    bool shrink_height = false; // Symmetric content-independent vertical minimum and clipping.
    bool continuous_corners = false; // Exponent-4 superellipse corners.
    float shadow_spread = 0.0f; // Logical pixels; negative values inset the shadow.
    bool window_drag_passthrough = false; // Allow caption dragging through this pointer target.
    BorderAlign border_align = BorderAlign::Inside;
    float opacity = 1;
    Color hover_background = {}, hover_border_color = {};
    float hover_scale = 1, press_scale = 1;
    bool animate_layout = false;
    uint32_t hero_id = 0;
    SpringSpec transition = SpringSpec::snappy();
    BorderBeamStyle border_beam = {};
    float top_rim_alpha = 0;
    float scroll_fade_px = 0; // Content edge masks; no leading fade at scroll zero.
    Padding border_sides = {}; // Positive sides replace the uniform width.
    float border_dash_px = 0.0f;
    float border_gap_px = 0.0f; // Zero uses the dash length.
    Padding edge_fade = {}; // Logical px fade on {left, right, top, bottom} for clipped containers.
};

inline constexpr uint16_t kFontSans       = 0u;
inline constexpr uint16_t kFontBold       = 1u;
inline constexpr uint16_t kFontMono       = 2u;
inline constexpr uint16_t kFontMedium = 14u, kFontSemiBold = 15u; // Lazy static faces; missing faces fall back to Sans.
inline constexpr uint16_t kFontItalic = 16u;
inline constexpr uint16_t kMaxFontSlots   = 64u;
inline constexpr uint16_t kInvalidFont    = 0xFFFFu;
inline constexpr uint16_t kMaxVectorIcons = 1024u;
inline constexpr uint32_t kInvalidSprite = 0u;
inline constexpr uint16_t kInvalidIcon    = 0xFFFFu;

struct TextDesc {
    Color color = Color::theme_auto();
    float font_size_px = 16.0f;
    uint16_t font_id = kFontSans;
    float letter_spacing = 0.0f;
    bool wrap = true;
    uint32_t id = 0;
    bool selectable = false;
    Color selection_color = Color::theme_auto();
    float line_height_px = 0.0f;
    AlignX align_x = AlignX::Left;
    bool ellipsis = false;
    uint16_t max_lines = 0;
};

struct TextSpan {
    Str      text = {};
    Color    color = Color::theme_auto();
    Color    background = {};        // Optional inline pill background
    Color    border_color = {};      // Optional 1px border on inline pill
    float    font_size_px = 0.0f;    // 0.0f = inherit RichTextDesc::font_size_px
    uint16_t font_id = kInvalidFont; // kInvalidFont = inherit RichTextDesc::font_id
    bool     underline = false;
    bool     strikethrough = false;
    float    letter_spacing = 0.0f;  // Extra tracking in logical px
    float    padding_x = 0.0f;       // Horizontal pill padding at span start/end in logical px
    float    bg_radius = 4.0f;       // Pill corner radius in logical px
    uint32_t id = 0;                 // Optional semantic ID for hovered(id) / pressed(id) / clicked(id)
    uint16_t icon_id = kInvalidIcon;
    Color icon_color = Color::theme_auto();
    float icon_size_px = 0.0f; // Zero uses 90% of the effective font size.
    float icon_gap_px = 4.0f;
    float padding_y = 0.0f;
    Color hover_background = {};
    bool has_wipe = false;
    Color color_end = {};
    float wipe_x = 0.0f;
    float wipe_feather_px = 0.0f;
};

struct RichTextDesc {
    Color    color = Color::theme_auto();
    float    font_size_px = 14.0f;
    uint16_t font_id = kFontSans;
    float    line_height_px = 0.0f;  // 0.0f = auto from max span metrics
    float    letter_spacing = 0.0f;
    AlignX   align_x = AlignX::Left;
    bool     wrap = true;
    uint32_t id = 0;
    bool selectable = false;
    Color selection_color = Color::theme_auto();
    bool ellipsis = false;
    uint16_t max_lines = 0;
};

struct CollapsibleDesc {
    uint32_t id = 0;
    Str title = {};
    uint16_t icon_id = kInvalidIcon;
    bool default_open = true;
    float header_height_px = 28.0f;
    float font_size_px = 12.5f;
    uint16_t font_id = kFontSans;
    Padding header_padding = Padding::xy(10.0f, 0.0f);
    Padding body_padding = Padding::xy(10.0f, 8.0f);
    float body_gap = 6.0f;
    Corners radius = Corners::theme_auto();
    Color header_bg = Color::theme_auto();
    Color header_hover_bg = Color::theme_auto();
    Color body_bg = Color::theme_auto();
    Color border_color = Color::theme_auto();
    float border_width = 1.0f;
    Color text_color = Color::theme_auto();
    Color icon_color = Color::theme_auto();
    SpringSpec spring_spec = { .response = 0.20f, .damping_ratio = 0.92f };
};
struct CollapsibleResult {
    bool open = false;
    bool visible = false;
    bool toggled = false;
    bool header_hovered = false;
    float anim_t = 0.0f;
};
CollapsibleResult begin_collapsible(const CollapsibleDesc& desc) noexcept;
void end_collapsible() noexcept;
bool is_collapsible_open(uint32_t element_id) noexcept;
void set_collapsible_open(uint32_t element_id, bool open) noexcept;

struct ImageDesc {
    uint32_t id = 0;
    SDL_GPUTexture* texture = nullptr; // Zero-copy external GPU texture slot
    uint32_t sprite_id = kInvalidSprite;
    Sizing width = Sizing::fit();
    Sizing height = Sizing::fit();
    Vec2 uv_min = {0.0f, 0.0f};
    Vec2 uv_max = {1.0f, 1.0f};
    Corners radius = {};
    Color tint = Color::white();
    Color border_color = {};
    float border_width = 0.0f;
    float aspect_ratio = 0.0f;
    uint16_t grid_col_span = 1;
    bool continuous_corners = false;
};

struct IconDesc {
    uint32_t id = 0;
    uint16_t icon_id = kInvalidIcon;
    float size_px = 16.0f;
    Color color = Color::theme_auto();
};

struct RectDrawDesc {
    Rect    rect = {};
    Color   fill = {};
    Color   fill_end = {};
    Vec2    gradient_dir = {0.0f, 1.0f};
    bool    radial_gradient = false;
    Corners radius = {};
    Color   border_color = {};
    float   border_width = 0.0f;
    bool    foreground = false;
    float   angle_rad = 0.0f;
    float   softness = 1.0f; // Physical-pixel AA width, minimum 0.5.
    bool continuous_corners = false; // Exponent-4 superellipse corners.
    BorderAlign border_align = BorderAlign::Inside;
};

struct MeshRectDrawDesc {
    Rect rect = {};
    Color color_tl = {}, color_tr = {}, color_br = {}, color_bl = {};
    Corners radius = {};
    bool oklab = true;
    float grain_intensity = 0, warp_phase = 0;
    bool continuous_corners = false, foreground = false;
};
void draw_mesh_rect(const MeshRectDrawDesc& desc) noexcept;

struct PunchHoleDesc {
    Rect rect = {};
    Corners radius = {};
    float softness = 1;
    bool continuous_corners = false, foreground = false;
};
void punch_hole(const PunchHoleDesc& desc) noexcept;

struct BorderBeamDrawDesc {
    Rect rect = {};
    Corners radius = {};
    BorderBeamStyle beam = {};
    float top_rim_alpha = 0;
    bool continuous_corners = false, foreground = false;
};
void draw_border_beam(const BorderBeamDrawDesc& desc) noexcept;

struct LineDesc {
    Vec2  from = {};
    Vec2  to = {};
    Color color = Color::white();
    Color color_end = {};        // 0 alpha = solid `color`
    float thickness = 1.5f;
    Color glow_color = {};
    float glow_px = 0.0f;
    bool  foreground = false;
};

struct PolylineDesc {
    std::span<const Vec2> points = {};
    Color color = Color::white();
    float thickness = 1.5f;
    Color glow_color = {};
    float glow_px = 0.0f;
    bool  closed = false;
    bool  foreground = false;
};

struct FilledPolygonDesc {
    std::span<const Vec2> points = {};
    Color fill = {};
    Color fill_end = {};
    Vec2 gradient_dir = {0, 1};
    bool radial_gradient = false;
    Color stroke_color = {};
    float stroke_width = 0;
    bool foreground = false;
};
enum class PathVerb : uint8_t { MoveTo = 0, LineTo = 1, QuadTo = 2, CubicTo = 3, Close = 4 };
struct PathCmd {
    PathVerb verb = PathVerb::MoveTo;
    Vec2 p1 = {}, p2 = {}, p3 = {};
};
struct FilledPathDesc {
    std::span<const PathCmd> commands = {};
    Str svg_path_d = {};
    Vec2 offset = {};
    float scale = 1;
    Color fill = {};
    Color fill_end = {};
    Vec2 gradient_dir = {0, 1};
    bool radial_gradient = false;
    Color stroke_color = {};
    float stroke_width = 0;
    bool foreground = false;
};
struct AreaSeriesDesc {
    std::span<const Vec2> points = {};
    float baseline_y = 0;
    Color fill_top = Color::theme_auto();
    Color fill_bottom = Color::theme_auto();
    Color line_color = Color::theme_auto();
    float line_thickness = 1.75f;
    bool smooth = false;
    bool foreground = false;
};

struct BezierDesc {
    Vec2  p0 = {};
    Vec2  p1 = {};               // Quadratic control point (or first cubic control point)
    Vec2  p2 = {};               // Quadratic end point (or second cubic control point)
    Vec2  p3 = {};               // Cubic end point when `cubic == true`
    bool  cubic = false;
    Color color_start = Color::white();
    Color color_end = {};        // 0 alpha = solid `color_start`
    float thickness = 1.75f;
    Color glow_color = {};
    float glow_px = 0.0f;
    bool  foreground = false;
};

struct RingDesc {
    Vec2  center = {};
    float radius = 16.0f;
    float thickness = 3.0f;
    float start_angle_rad = -1.57079633f; // Default -PI/2 (12 o'clock)
    float sweep_angle_rad = 6.28318531f;  // Positive clockwise in Y-down coordinates
    Color color = Color::white();
    Color color_end = {};                 // 0 alpha = solid `color`
    Color track_color = {};               // Optional full-circle background ring if a > 0
    Color glow_color = {};
    float glow_px = 0.0f;
    bool  round_caps = true;
    bool  foreground = false;
};

struct IconDrawDesc {
    uint16_t icon_id = kInvalidIcon;
    Rect     rect = {};
    Color    color = Color::white();
    bool     foreground = false;
    float    angle_rad = 0.0f;
};

struct TextDrawDesc {
    Str      text = {};
    Vec2     pos = {}; // Local to current open box top-left (logical px)
    uint16_t font_id = kFontSans;
    float    size = 14.0f;
    Color    color = Color::theme_auto();
    float    letter_spacing = 0.0f;
    AlignX   align_x = AlignX::Left;
    AlignY   align_y = AlignY::Top;
    bool     foreground = false;
    float    max_width_px = 0.0f; // Logical px, like size; 0 = unlimited.
    bool     ellipsis = false;
    bool     has_wipe = false;
    Color    color_end = {};
    float    wipe_x = 0.0f;
    float    wipe_feather_px = 0.0f;
    Color    shadow_color = {};
    Vec2     shadow_offset = {};
    float    shadow_blur = 0.0f;
};

struct ImageDrawDesc {
    SDL_GPUTexture* texture = nullptr;
    uint32_t sprite_id = kInvalidSprite;
    Rect            rect = {}; // Local to current open box (logical px)
    Rect            uv_rect = {0.0f, 0.0f, 1.0f, 1.0f};
    Color           tint = Color::white();
    float           radius = 0.0f;
    bool            foreground = false;
    bool            continuous_corners = false;
};

struct TextureDesc {
    const void* rgba_pixels = nullptr;
    uint32_t    width = 0;
    uint32_t    height = 0;
    uint32_t    row_pitch_bytes = 0; // 0 = width * 4
    bool        srgb = true;
    const char* debug_name = nullptr;
    bool premultiplied_alpha = false;
};

struct LoadedTexture {
    SDL_GPUTexture* texture = nullptr;
    uint32_t        width = 0;
    uint32_t        height = 0;

    constexpr explicit operator bool() const noexcept { return texture != nullptr; }
};

enum class WindowHit : uint8_t {
    Normal = 0,
    Client = 1,
    Caption = 2,
    ResizeTopLeft = 3,
    ResizeTop = 4,
    ResizeTopRight = 5,
    ResizeRight = 6,
    ResizeBottomRight = 7,
    ResizeBottom = 8,
    ResizeBottomLeft = 9,
    ResizeLeft = 10,
    Maximize = 11,
};

enum class WindowBackdrop : uint8_t { Opaque, Transparent, SystemMica, SystemAcrylic };
struct WindowChromeDesc {
    bool  enabled = false;
    bool  resizable = true;
    float border_thickness = 6.0f;
    float corner_size = 12.0f;
    bool  macos_traffic_lights = true;
    float traffic_light_inset_px = 76.0f;
    WindowBackdrop backdrop = WindowBackdrop::Opaque; // Alpha composition requires SDL_WINDOW_TRANSPARENT at creation.
};

struct DragPayloadView {
    uint32_t    kind_hash = 0;
    Str         label = {};
    uint64_t    u64 = 0;
    uint32_t    u32 = 0;
    const void* data = nullptr;
    uint32_t    size = 0;
};

struct TextLineCol { uint32_t line = 0; uint32_t col_byte = 0; };
struct TextBufferEdit;
// Caller owns this POD. Initialize once, free explicitly; do not shallow-copy ownership.
struct TextBuffer {
    char* data = nullptr; uint32_t cap = 0, gap_start = 0, gap_end = 0;
    // Prefix entries are byte offsets <= gap_start. Suffix entries encode
    // byte_length - logical_start, so local typing never shifts suffix entries.
    uint32_t* line_data = nullptr; uint32_t line_cap = 0, line_gap_start = 0, line_gap_end = 0;
    TextBufferEdit* undo_ops = nullptr; uint32_t undo_cap = 0, undo_count = 0, undo_cursor = 0;
    char* undo_blob = nullptr; uint32_t undo_blob_cap = 0, undo_blob_used = 0;
    uint64_t version = 1;
};
bool init_text_buffer(TextBuffer*, Str initial_utf8 = {}, uint32_t initial_cap = 4096) noexcept;
void free_text_buffer(TextBuffer*) noexcept;
bool text_buffer_set(TextBuffer*, Str) noexcept;
bool text_buffer_replace(TextBuffer*, uint32_t byte_start, uint32_t byte_end, Str,
    bool record_undo = true, uint32_t before_cursor = 0, uint32_t before_anchor = 0,
    uint32_t after_cursor = 0, uint32_t after_anchor = 0, uint8_t op_group = 0, double timestamp_s = 0.0) noexcept;
uint32_t text_buffer_byte_length(const TextBuffer*) noexcept;
uint32_t text_buffer_line_count(const TextBuffer*) noexcept;
uint32_t text_buffer_line_start(const TextBuffer*, uint32_t) noexcept;
uint32_t text_buffer_line_end(const TextBuffer*, uint32_t) noexcept;
Str text_buffer_line(const TextBuffer*, uint32_t, Arena* scratch = nullptr) noexcept;
uint32_t text_buffer_copy_range(const TextBuffer*, uint32_t, uint32_t, char*, uint32_t) noexcept;
TextLineCol text_buffer_line_col(const TextBuffer*, uint32_t) noexcept;
uint32_t text_buffer_byte_offset(const TextBuffer*, uint32_t, uint32_t) noexcept;
bool text_buffer_undo(TextBuffer*, uint32_t* out_cursor = nullptr, uint32_t* out_anchor = nullptr) noexcept;
bool text_buffer_redo(TextBuffer*, uint32_t* out_cursor = nullptr, uint32_t* out_anchor = nullptr) noexcept;
void text_buffer_clear_undo(TextBuffer*) noexcept;
struct SyntaxToken {
    uint32_t byte_start = 0, byte_end = 0; Color color = Color::theme_auto(); uint16_t font_id = kFontMono;
};
using SyntaxTokenizeFn = uint32_t (*)(uint32_t, Str, SyntaxToken*, uint32_t, void*) noexcept;
uint32_t tokenize_cpp_hlsl(uint32_t, Str, SyntaxToken*, uint32_t, void*) noexcept;
struct CodeLineDecoration {
    Color row_bg = {};
    Color gutter_bar_color = {};
    Str gutter_tag = {};
    Color gutter_tag_color = Color::theme_auto();
};
using CodeLineDecorateFn = bool (*)(uint32_t line_index, CodeLineDecoration* out_dec, void* user_data) noexcept;
struct CodeEditorDesc {
    uint32_t id = 0; TextBuffer* buffer = nullptr;
    Sizing width = Sizing::grow(); Sizing height = Sizing::grow();
    Padding padding = Padding::xy(10, 8);
    float font_size_px = 14; uint16_t font_id = kFontMono; float line_height_px = 0;
    bool show_line_numbers = true; float gutter_width_px = 0; uint32_t tab_width = 4;
    bool read_only = false; bool show_scrollbar = true; bool word_wrap = false;
    SyntaxTokenizeFn tokenizer = tokenize_cpp_hlsl; void* tokenizer_user_data = nullptr;
    Color background = Color::theme_auto(), gutter_background = Color::theme_auto();
    Color active_line_color = Color::theme_auto(), selection_color = Color::theme_auto();
    Color text_color = Color::theme_auto(), gutter_text_color = Color::theme_auto();
    Color gutter_active_text_color = Color::theme_auto(), caret_color = Color::theme_auto();
    Color composition_color = Color::theme_auto(), border_color = Color::theme_auto();
    Color focus_border_color = Color::theme_auto();
    float border_width = 1, focus_border_width = 1.5f; Corners radius = Corners::theme_auto();
    float scroll_fade_px = 0;
    CodeLineDecorateFn line_decorator = nullptr;
    void* line_decorator_user_data = nullptr;
    float gutter_tag_width_px = 0;
};
struct CodeEditorResult {
    bool changed = false, focused = false, composing = false;
    uint32_t cursor_byte = 0, selection_anchor_byte = 0, cursor_line = 0, cursor_col_byte = 0;
    uint32_t first_visible_line = 0, end_visible_line = 0, total_lines = 0;
    Rect ime_cursor_rect = {};
    int32_t hovered_line = -1, clicked_gutter_line = -1;
};
CodeEditorResult code_editor(const CodeEditorDesc&) noexcept;
void code_editor_set_cursor(uint32_t, uint32_t anchor_byte, uint32_t cursor_byte, bool scroll_into_view = true) noexcept;
void code_editor_scroll_to_line(uint32_t, uint32_t, AlignY align = AlignY::Center) noexcept;

struct TextInputHighlight {
    uint32_t byte_offset = 0;
    uint32_t byte_length = 0;
    Color text_color = {};
    Color background = {};
    Color border_color = {};
    float radius_px = 4.0f;
};

struct TextInputDesc {
    uint32_t id = 0;
    char* buffer = nullptr;            // Writable NUL-terminated UTF-8; read_only permits bounded, unterminated memory.
    uint32_t capacity = 0;             // Total byte capacity of `buffer` (including trailing '\0')
    Str placeholder = {};              // Displayed when buffer & IME preedit are both empty
    Sizing width = Sizing::grow();
    Sizing height = Sizing::fixed(36.0f);
    Padding padding = Padding::xy(12.0f, 8.0f);
    float font_size_px = 14.0f;
    uint16_t font_id = 0;
    float letter_spacing = 0.0f;
    Color text_color = Color::theme_auto();
    Color placeholder_color = Color::theme_auto();
    Color selection_color = Color::theme_auto();
    Color caret_color = Color::theme_auto();
    Color composition_color = Color::theme_auto();
    Color background = Color::theme_auto();
    Color border_color = Color::theme_auto();
    Color focus_border_color = Color::theme_auto();
    float border_width = 1.0f;
    float focus_border_width = 1.5f;
    Corners radius = Corners::theme_auto();
    bool multiline = false;
    bool word_wrap = false;
    bool read_only = false;
    bool password = false;
    bool context_menu = true;
    bool clear_button = false;
    bool escape_blurs = true;
    bool suppress_popup_keys = false;
    std::span<const TextInputHighlight> highlights = {};
    Str ghost_text = {};
    Color ghost_color = Color::theme_auto();
    bool accept_ghost_on_tab = false;
};

struct TextInputResult {
    bool changed = false;
    bool submitted = false;
    bool focused = false;
    bool composing = false;
    uint32_t byte_length = 0;
    uint32_t cursor_byte = 0;
    uint32_t selection_anchor_byte = 0;
    Rect ime_cursor_rect = {};
    bool ghost_accepted = false;
};

// Standard desktop controls. Mutable values remain caller-owned.
enum class ButtonVariant : uint8_t { Default = 0, Primary = 1, Ghost = 2, Danger = 3 };
struct ButtonDesc {
    uint32_t id = 0; Str label = {}; uint16_t icon_id = kInvalidIcon;
    ButtonVariant variant = ButtonVariant::Default;
    Sizing width = Sizing::fit(); Sizing height = Sizing::fixed(32);
    Padding padding = Padding::xy(14, 0); float font_size_px = 13; uint16_t font_id = kFontSans;
    Corners radius = Corners::theme_auto(); bool disabled = false; Str shortcut_hint = {};
    AlignX align_x = AlignX::Center;
};
struct CheckboxDesc {
    uint32_t id = 0; Str label = {}; bool* checked = nullptr; bool indeterminate = false;
    bool disabled = false; float box_size_px = 18; float font_size_px = 13;
    uint16_t font_id = kFontSans; Color accent = Color::theme_auto();
};
struct ToggleSwitchDesc {
    uint32_t id = 0; Str label = {}; bool* value = nullptr; bool disabled = false;
    float track_width_px = 38; float track_height_px = 20; float font_size_px = 13;
    uint16_t font_id = kFontSans; Color on_color = Color::theme_auto();
    Color off_color = Color::theme_auto();
};
struct ProgressBarDesc {
    uint32_t id = 0; float fraction = 0; Str overlay_text = {};
    Sizing width = Sizing::grow(); Sizing height = Sizing::fixed(10); Corners radius = Corners::theme_auto();
    Color track_color = Color::theme_auto(); Color fill_color = Color::theme_auto();
    Color text_color = Color::theme_auto(); float font_size_px = 11;
};
struct TabBarDesc {
    uint32_t id = 0; uint32_t* active_tab = nullptr; Sizing width = Sizing::grow();
    float height_px = 32; float font_size_px = 12.5f;
    Color background = Color::theme_auto(); Color active_bg = Color::theme_auto();
    Color hover_bg = Color::theme_auto(); Color accent = Color::theme_auto();
    Color text_color = Color::theme_auto(); Color active_text_color = Color::theme_auto();
};
struct TabItemDesc { uint32_t id = 0; Str label = {}; uint16_t icon_id = kInvalidIcon; bool closable = false; bool disabled = false; };
struct TabItemResult { bool selected = false; bool clicked = false; bool closed = false; };
struct SliderDesc {
    uint32_t id = 0; Str label = {}; float* value = nullptr; float min_value = 0; float max_value = 1;
    float step = 0; const char* format = "%.2f"; Sizing width = Sizing::grow(); float height_px = 26; bool disabled = false;
    Color background = Color::theme_auto(); Color fill_color = Color::theme_auto(); Color text_color = Color::theme_auto();
};
struct DragValueDesc {
    uint32_t id = 0; Str label = {}; float* value = nullptr; float min_value = -1e9f; float max_value = 1e9f;
    float speed = .1f; float step = 0; const char* format = "%.2f"; Sizing width = Sizing::fixed(110); float height_px = 26; bool disabled = false;
    Color background = Color::theme_auto(); Color fill_color = Color::theme_auto(); Color text_color = Color::theme_auto();
};
struct TooltipDesc { Str text = {}; float delay_seconds = .35f; float max_width_px = 280; float font_size_px = 12; int16_t z_index = 220; };
struct ModalDesc {
    uint32_t id = 0; bool* open = nullptr; Str title = {}; float width_px = 420; Sizing height = Sizing::fit();
    bool dismiss_on_backdrop = true; bool dismiss_on_escape = true;
    Color backdrop_color = Color::theme_auto(); Color background = Color::theme_auto();
    Color border_color = Color::theme_auto(); Corners radius = Corners::theme_auto(); int16_t z_index = 180;
};
struct ComboBoxDesc {
    uint32_t id = 0; Str label = {}; int* selected_index = nullptr; std::span<const Str> items = {};
    Sizing width = Sizing::grow(); float height_px = 28; float max_popup_height_px = 220; bool disabled = false;
    Color background = Color::theme_auto(); Color hover_bg = Color::theme_auto(); Color text_color = Color::theme_auto();
};
struct PopupDesc {
    uint32_t id = 0; uint32_t trigger_id = 0; Sizing width = Sizing::fixed(220); Sizing height = Sizing::fit();
    Padding padding = Padding::all(6); float gap = 2;
    Color background = Color::theme_auto(); Color border_color = Color::theme_auto();
    Corners radius = Corners::theme_auto(); int16_t z_index = 200;
    bool attach_to_trigger = false; bool match_trigger_width = false; bool scroll_y = true;
};
struct MenuBarDesc { uint32_t id = 0; Sizing width = Sizing::grow(); float height_px = 28; Color background = Color::theme_auto(); };
struct MenuDesc { uint32_t id = 0; Str label = {}; bool disabled = false; };
struct MenuItemDesc {
    uint32_t id = 0; Str label = {}; Str shortcut_hint = {}; uint16_t icon_id = kInvalidIcon;
    bool checked = false; bool disabled = false; bool separator_after = false;
};
bool button(const ButtonDesc& desc) noexcept;
bool checkbox(const CheckboxDesc& desc) noexcept;
bool toggle_switch(const ToggleSwitchDesc& desc) noexcept;
void progress_bar(const ProgressBarDesc& desc) noexcept;
bool begin_tab_bar(const TabBarDesc& desc) noexcept;
TabItemResult tab_item(const TabItemDesc& desc) noexcept;
void end_tab_bar() noexcept;
bool slider(const SliderDesc& desc) noexcept;
bool drag_value(const DragValueDesc& desc) noexcept;
void set_tooltip(uint32_t target_id, const TooltipDesc& desc) noexcept;
void set_tooltip(uint32_t target_id, Str text) noexcept;
bool begin_modal(const ModalDesc& desc) noexcept;
void end_modal() noexcept;
bool combo_box(const ComboBoxDesc& desc) noexcept;
void open_popup(uint32_t popup_id, Vec2 anchor_pos = {NAN,NAN}) noexcept;
void close_popup(uint32_t popup_id = 0) noexcept;
bool is_popup_open(uint32_t popup_id) noexcept;
bool begin_popup(const PopupDesc& desc) noexcept;
void end_popup() noexcept;
bool begin_context_menu(uint32_t popup_id, uint32_t target_id = 0) noexcept;
void end_context_menu() noexcept;
bool begin_menu_bar(const MenuBarDesc& desc) noexcept;
void end_menu_bar() noexcept;
bool begin_menu(const MenuDesc& desc) noexcept;
void end_menu() noexcept;
bool menu_item(const MenuItemDesc& desc) noexcept;
void menu_separator() noexcept;

struct SplitDesc {
    uint32_t id = 0;
    Axis axis = Axis::Row; // Row = Left|Right (ResizeEW), Col = Top/Bottom (ResizeNS)
    Sizing width = Sizing::grow();
    Sizing height = Sizing::grow();
    float default_ratio = 0.5f;
    float min_first_px = 120.0f;
    float min_second_px = 120.0f;
    float divider_size_px = 6.0f;
    Color divider_color = Color::theme_auto();
    Color divider_hover_color = Color::theme_auto();
    Color divider_active_color = Color::theme_auto();
    float divider_hit_px = 8.0f; // Hit target is independent of divider_size_px.
    float keyboard_step_px = 8.0f;
};

struct SplitResult {
    float ratio = 0.5f;
    bool dragging = false;
};

using VirtualItemHeightFn = float (*)(uint32_t index, void* user_data) noexcept;

struct VirtualListDesc {
    uint32_t id = 0;
    uint32_t item_count = 0;
    float item_height_px = 28.0f;
    float row_gap_px = 0.0f;
    uint32_t overscan_rows = 2u;
    Sizing width = Sizing::grow();
    Sizing height = Sizing::grow();
    Padding padding = {};
    Color background = {};
    Color border_color = {};
    float border_width = 0.0f;
    Corners radius = {};
    bool show_scrollbar = true;
    VirtualItemHeightFn measure_height_fn = nullptr;
    void* measure_user_data = nullptr;
    bool variable_height = false;
    float scroll_fade_px = 0;
    bool stick_to_bottom = false;
};

struct VirtualListSlice {
    uint32_t first_index = 0;
    uint32_t end_index = 0;
    uint32_t total_count = 0;
    double scroll_y = 0.0;
    double max_scroll_y = 0.0;
    float viewport_height = 0.0f;
    bool at_bottom = false;
};


enum class DockZone : uint8_t { None = 0, Center = 1, Left = 2, Right = 3, Top = 4, Bottom = 5 };
struct DockPaneDesc {
    uint32_t id = 0; Str title = {}; uint16_t icon_id = kInvalidIcon;
    bool closable = false; bool default_open = true;
};
struct DockSpaceDesc {
    uint32_t id = 0;
    Sizing width = Sizing::grow(); Sizing height = Sizing::grow();
    float tab_bar_height_px = 30.0f; float divider_size_px = 6.0f;
    Color background = {}; Color pane_background = Color::theme_auto();
    Color pane_border_color = Color::theme_auto();
    float pane_border_width = 1.0f; Corners pane_radius = Corners::theme_auto();
    Color tab_bar_bg = Color::theme_auto(); Color tab_active_bg = Color::theme_auto();
    Color tab_hover_bg = Color::theme_auto(); Color tab_accent = Color::theme_auto();
};
bool dock_space_initialized(uint32_t dock_id) noexcept;
uint32_t dock_reset_root(uint32_t dock_id) noexcept;
bool dock_split_node(uint32_t dock_id, uint32_t node_idx, Axis axis, float default_ratio,
    uint32_t* out_first_node, uint32_t* out_second_node, uint32_t split_widget_id = 0,
    float min_first_px = 120.0f, float min_second_px = 120.0f) noexcept;
bool dock_add_pane(uint32_t dock_id, uint32_t leaf_node_idx, const DockPaneDesc& pane) noexcept;
bool dock_dock_pane(uint32_t dock_id, uint32_t pane_id, uint32_t target_pane_id, DockZone zone, float split_ratio = 0.5f) noexcept;
bool dock_reorder_pane(uint32_t dock_id, uint32_t pane_id, uint32_t target_pane_id, bool insert_after = false) noexcept;
void dock_activate_pane(uint32_t dock_id, uint32_t pane_id) noexcept;
uint32_t dock_active_pane(uint32_t dock_id, uint32_t leaf_node_idx) noexcept;
uint32_t dock_find_pane_leaf(uint32_t dock_id, uint32_t pane_id) noexcept;
bool dock_is_pane_open(uint32_t dock_id, uint32_t pane_id) noexcept;
void dock_set_pane_open(uint32_t dock_id, uint32_t pane_id, bool open) noexcept;
void begin_dock_space(const DockSpaceDesc& desc) noexcept;
uint32_t next_dock_pane() noexcept;
void end_dock_space() noexcept;

inline constexpr uint32_t kMaxTableColumns = 16u;
enum class TableColumnSizing : uint8_t { Fixed = 0, Stretch = 1 };
struct TableColumnDesc {
    uint32_t id = 0; Str title = {}; TableColumnSizing sizing = TableColumnSizing::Fixed;
    float width = 120.0f; float min_width = 48.0f; float max_width = 800.0f;
    AlignX align_x = AlignX::Left; bool resizable = true; bool sortable = true;
};
// Optional caller-owned multi-selection bitmap; one bit per row. Storage remains valid through end_table.
// Explicit selection overrides legacy selected_row highlighting.
struct TableSelection {
    uint32_t* focused_row = nullptr;
    uint32_t* anchor_row = nullptr;
    uint64_t* selected_words = nullptr;
    uint32_t word_count = 0;
};
bool table_row_selected(const TableSelection& selection, uint32_t row) noexcept;
struct TableDesc {
    uint32_t id = 0; std::span<const TableColumnDesc> columns = {};
    uint32_t item_count = 0; float header_height_px = 30.0f; float row_height_px = 26.0f;
    float row_gap_px = 0.0f; uint32_t overscan_rows = 2u;
    uint32_t default_sort_column = 0; bool default_sort_descending = false;
    float cell_padding_x = 10.0f; Sizing width = Sizing::grow(); Sizing height = Sizing::grow();
    Padding padding = {}; Color background = Color::theme_auto();
    Color header_background = Color::theme_auto();
    Color zebra_color = Color::theme_auto(); Color hover_row_color = Color::theme_auto();
    Color border_color = Color::theme_auto(); float border_width = 1.0f;
    Corners radius = Corners::theme_auto(); bool show_scrollbar = true;
    bool show_column_lines = true; float header_font_size_px = 12;
    uint32_t* selected_row = nullptr; // UINT32_MAX means no selection; click updates caller state.
    Color selected_row_color = Color::theme_auto();
    bool horizontal_scroll = true; bool keyboard_navigation = true;
    TableSelection selection = {};
    float scroll_fade_px = 0;
};
struct TableResult {
    VirtualListSlice slice = {}; uint32_t sort_column = 0;
    bool sort_descending = false; bool sort_changed = false;
};
TableResult begin_table(const TableDesc& desc) noexcept;
bool table_next_row(uint32_t* out_row_index = nullptr, Color row_bg_override = {}, Color row_border_override = {}) noexcept;
bool table_next_column() noexcept;
struct TableRowResult {
    uint32_t id = 0;
    bool hovered = false, pressed = false, clicked = false, double_clicked = false, right_clicked = false;
};
TableRowResult table_row_result(uint32_t table_id, uint32_t row_index) noexcept;
void end_table() noexcept;
float table_column_width(uint32_t table_id, uint32_t col_index) noexcept;
void set_table_column_width(uint32_t table_id, uint32_t col_index, float width_px) noexcept;
void set_table_sort(uint32_t table_id, uint32_t col_index, bool descending) noexcept;
void table_scroll_to_row(uint32_t table_id, uint32_t row_index, float row_height_px, AlignY align = AlignY::Top) noexcept;

struct TreeNodeDesc {
    uint32_t id = 0; Str label = {}; uint16_t icon_id = kInvalidIcon;
    Color icon_color = Color::theme_auto(); Color text_color = Color::theme_auto();
    Color selected_bg = Color::theme_auto(); Color selected_accent = Color::theme_auto();
    Color hover_bg = Color::theme_auto();
    float row_height_px = 26.0f; float font_size_px = 12.5f; float indent_px = 16.0f;
    uint16_t font_id = kFontSans; bool default_open = false; bool selected = false;
    bool leaf = false; bool show_indent_guide = true;
};
struct TreeNodeResult {
    bool open = false; bool clicked = false; bool double_clicked = false;
    bool toggled = false; bool hovered = false;
};
TreeNodeResult begin_tree_node(const TreeNodeDesc& desc) noexcept;
void end_tree_node() noexcept;
bool is_tree_open(uint32_t node_id) noexcept;
void set_tree_open(uint32_t node_id, bool open) noexcept;

struct CanvasDesc {
    uint32_t id = 0;
    Sizing width = Sizing::grow();
    Sizing height = Sizing::grow();
    Vec2 default_pan = {};
    float default_zoom = 1.0f;
    float min_zoom = 0.15f;
    float max_zoom = 8.0f;
    float grid_step_world = 24.0f;
    uint32_t major_grid_subdiv = 4u;
    Color background = Color::theme_auto();
    Color minor_grid_color = Color::theme_auto();
    Color major_grid_color = Color::theme_auto();
    Color border_color = Color::theme_auto();
    float border_width = 1.0f;
    Corners radius = Corners::theme_auto();
    bool show_grid = true;
    bool interactive = true;
    bool marquee_select = false;
    Color marquee_fill = Color::theme_auto();
    Color marquee_border = Color::theme_auto();
};

struct CanvasState {
    Vec2 pan = {};
    float zoom = 1.0f;
    Rect viewport = {};
    bool panning = false;
    bool marquee_active = false;
    bool marquee_finished = false;
    Rect marquee_world = {};

    constexpr Vec2 world_to_local(Vec2 world) const noexcept { return pan + world * zoom; }
    constexpr Vec2 world_to_screen(Vec2 world) const noexcept { return Vec2{viewport.x, viewport.y} + pan + world * zoom; }
    constexpr Vec2 screen_to_world(Vec2 screen) const noexcept {
        const float inv_z = (zoom > 1e-4f) ? (1.0f / zoom) : 1.0f;
        return (screen - Vec2{viewport.x, viewport.y} - pan) * inv_z;
    }
};

struct CanvasWireDesc {
    Vec2 from_world = {};
    Vec2 to_world = {};
    Color color_start = Color::theme_auto();
    Color color_end = Color::theme_auto();
    Color glow_color = {};
    float thickness_world = 2.2f;
    float glow_world = 0.0f;
    float tangent_bias = 0.45f;
    int16_t z_index = 0;
};

struct CanvasNodeDesc {
    uint32_t id = 0;
    Vec2* world_pos = nullptr;
    Vec2 pos_value = {};
    Sizing width_world = Sizing::fixed(200.0f);
    Sizing height_world = Sizing::fit();
    Padding padding_world = {};
    float gap_world = 0.0f;
    Axis axis = Axis::Col;
    Color background = Color::theme_auto();
    Color background_end = {};
    Vec2 gradient_dir = {0.0f, 1.0f};
    Color border_color = Color::theme_auto();
    float border_width_world = 1.0f;
    Corners radius_world = Corners::theme_auto();
    Color shadow_color = Color::theme_auto();
    float shadow_blur_world = 14.0f;
    Vec2 shadow_offset_world = {0.0f, 6.0f};
    int16_t z_index = 2;
    bool draggable = true;
};

struct CanvasNodeResult {
    bool hovered = false;
    bool pressed = false;
    bool dragging = false;
    bool clicked = false;
    Vec2 drag_delta_world = {};
};

Context* create_context(const ContextDesc& desc) noexcept;
void destroy_context(Context* ctx) noexcept;

Context* current_context() noexcept;
void set_current_context(Context* ctx) noexcept;

Arena* frame_arena(Context* ctx) noexcept;
SDL_GPUDevice* gpu_device(Context* ctx) noexcept;

// Multi-font registry & automatic cluster-safe fallback chain
enum class FontWeight : uint8_t {
    Regular = 0,
    Medium = 1,
    SemiBold = 2,
    Bold = 3,
    Italic = 4,
    Mono = 5,
};
uint16_t load_system_font(
    Context* ctx,
    Str family_name,
    FontWeight weight = FontWeight::Regular,
    uint16_t slot_hint = kInvalidFont) noexcept;
uint16_t load_font_file(
    Context* ctx,
    const char* path,
    uint16_t slot_hint = kInvalidFont,
    int32_t face_index = 0) noexcept;
uint16_t load_font_memory(
    Context* ctx,
    const void* data,
    size_t size_bytes,
    uint16_t slot_hint = kInvalidFont,
    int32_t face_index = 0) noexcept;
bool is_font_loaded(const Context* ctx, uint16_t font_id) noexcept;
// Configure a loaded font outside a frame; applies to measurement and all text APIs.
bool set_font_tabular_numbers(Context* ctx, uint16_t font_id, bool enabled) noexcept;

// Zero-atlas SVG path -> Slug vector icon compiler
enum class StrokeCap : uint8_t { Butt = 0, Round = 1, Square = 2 };
enum class StrokeJoin : uint8_t { Miter = 0, Round = 1, Bevel = 2 };

struct SvgStrokeDesc {
    Vec4 view_box = {0.0f, 0.0f, 24.0f, 24.0f};
    float stroke_width = 2.0f;
    StrokeCap cap = StrokeCap::Round;
    StrokeJoin join = StrokeJoin::Round;
    float miter_limit = 4.0f;
    uint16_t slot_hint = kInvalidIcon;
};

uint16_t register_svg_stroke_icon(Context* ctx, Str svg_path_d, const SvgStrokeDesc& desc = {}) noexcept;
uint16_t register_svg_icon(
    Context* ctx,
    Str svg_path_d,
    Vec4 view_box = {0.0f, 0.0f, 24.0f, 24.0f},
    uint16_t slot_hint = kInvalidIcon) noexcept;
bool is_icon_loaded(const Context* ctx, uint16_t icon_id) noexcept;

void process_event(Context* ctx, const SDL_Event& event) noexcept;

// Event-driven 0% idle sleep / wakeup scheduler:
// - needs_redraw() is true when input events or running animations require frames.
// - wait_events() blocks inside SDL_WaitEvent when idle, or polls non-blocking at full refresh when animating.
// - wake_up() is thread-safe and pushes a custom SDL user event to wake an idle event loop.
bool needs_redraw(const Context* ctx) noexcept;
void request_redraw(Context* ctx = nullptr) noexcept;
void wake_up(Context* ctx = nullptr) noexcept;
bool wait_events(Context* ctx, SDL_Event* first_event_out, int32_t timeout_ms = -1) noexcept;

// begin_frame binds ctx as the active thread context; end_frame finalizes ctx (or current_context()).
void begin_frame(Context* ctx, const FrameDesc& desc) noexcept;
void set_a11y_override(const A11yOverrideDesc& desc) noexcept;
std::span<const A11yNode> frame_a11y_nodes(const Context* ctx = nullptr, SDL_Window* window = nullptr) noexcept;
const A11yNode* find_a11y_node(uint32_t element_id, const Context* ctx = nullptr, SDL_Window* window = nullptr) noexcept;
bool perform_a11y_action(uint32_t element_id, A11yAction action, float numeric_value = 0, Str text_value = {}, Context* ctx = nullptr, SDL_Window* window = nullptr) noexcept;
const char* a11y_role_name(A11yRole role) noexcept;
void set_os_a11y_enabled(Context* ctx, bool enabled) noexcept;
bool os_a11y_enabled(const Context* ctx = nullptr) noexcept;
bool os_a11y_active(const Context* ctx = nullptr, SDL_Window* window = nullptr) noexcept;
void end_frame(Context* ctx = nullptr) noexcept;
// Main thread; caller destroys the returned window with SDL_DestroyWindow.
SDL_Window* create_desktop_window(const char* title, float logical_w, float logical_h, uint64_t flags = 0) noexcept;
// display_id=0 selects the primary display; pointer variant selects the cursor's display.
SDL_Window* create_desktop_window_on_display(const char* title, float logical_w, float logical_h, uint32_t display_id, uint64_t flags = 0) noexcept;
SDL_Window* create_desktop_window_at_pointer(const char* title, float logical_w, float logical_h, uint64_t flags = 0) noexcept;
bool set_desktop_window_min_size(SDL_Window* window, float logical_w, float logical_h) noexcept;
void begin_window_frame(Context* ctx, SDL_Window* window, float dt) noexcept;
void begin_window_frame(Context* ctx, SDL_Window* window, const WindowFrameDesc& desc) noexcept;
void end_window_frame(Context* ctx = nullptr) noexcept;
bool needs_window_redraw(const Context* ctx, SDL_Window* window) noexcept;
void request_window_redraw(Context* ctx, SDL_Window* window) noexcept;

std::span<const Prim> frame_prims(const Context* ctx) noexcept;
FrameStats frame_stats(const Context* ctx) noexcept;

bool render_window(
    Context* ctx,
    Color clear_color = Color::theme_auto(),
    SDL_Window* window_override = nullptr) noexcept;
void release_window(Context* ctx, SDL_Window* window) noexcept;
uint32_t claimed_window_count(const Context* ctx) noexcept;

void set_window_refresh_callback(Context* ctx, WindowRefreshFn fn, void* user_data = nullptr) noexcept;
void set_window_chrome(const WindowChromeDesc& desc, SDL_Window* window = nullptr) noexcept;
void window_drag_region(Rect rect_logical) noexcept;
void window_client_region(Rect rect_logical) noexcept;
// Register each frame. The ID overload resolves this frame's completed layout bounds.
void window_maximize_button_region(Rect rect_logical) noexcept;
void window_maximize_button_region(uint32_t element_id) noexcept;
WindowHit evaluate_window_hit(const Context* ctx, Vec2 logical_pos, SDL_Window* window = nullptr) noexcept;
void minimize_window(SDL_Window* window = nullptr) noexcept;
void maximize_window(SDL_Window* window = nullptr) noexcept;
void restore_window(SDL_Window* window = nullptr) noexcept;
bool is_window_maximized(const Context* ctx = nullptr, SDL_Window* window = nullptr) noexcept;
bool is_window_minimized(const Context* ctx = nullptr, SDL_Window* window = nullptr) noexcept;
bool set_window_fullscreen(bool fullscreen, SDL_Window* window = nullptr) noexcept;
bool is_window_fullscreen(const Context* ctx = nullptr, SDL_Window* window = nullptr) noexcept;
Padding window_safe_insets(const Context* ctx = nullptr, SDL_Window* window = nullptr) noexcept;

bool render_headless_png(
    Context* ctx,
    uint32_t width,
    uint32_t height,
    const char* out_png_path,
    Color clear_color = Color::theme_auto()) noexcept;

// Implicit-context declarative UI flow (operates on current_context()).
using ShaderSurfaceId = uint32_t; // Zero is invalid; handles carry a slot generation.
struct ShaderSurfaceCode {
    const void* data = nullptr;
    size_t size = 0;
    const char* entrypoint = "ps_main";
};
struct ShaderSurfaceCreateDesc {
    ShaderSurfaceCode spirv = {}, dxil = {}, msl = {};
    bool continuous_time = false;
    float max_fps = 60;
    uint32_t num_samplers = 0; // 0..4 fragment samplers at t0..t3 / s0..s3, space2.
    bool sampler_mirror_repeat = false;
    bool enable_feedback = false;
};
struct ShaderSurfaceEvalDesc {
    uint32_t width = 1, height = 1;
    uint64_t revision = 0;
    Vec4 params[4] = {};
    SDL_GPUTexture* textures[4] = {};
    bool enabled = true;
};
// Fragment shaders receive TEXCOORD0 UV and one 80-byte uniform at b0,space3:
// float4 viewport_time (width,height,time,frame), followed by float4 params[4].
// Output is premultiplied linear RGBA16F. Bytecode is copied by SDL at creation.
ShaderSurfaceId create_shader_surface(Context*, const ShaderSurfaceCreateDesc&) noexcept;
void destroy_shader_surface(Context*, ShaderSurfaceId) noexcept;
// Evaluate once per frame, before drawing the returned borrowed texture. A surface
// belongs to its first window until release_window(); keep it alive through render.
SDL_GPUTexture* eval_shader_surface(Context*, ShaderSurfaceId, const ShaderSurfaceEvalDesc&) noexcept;

enum class TerminalColorMode : uint8_t { Default, Ansi16, Ansi256, TrueColor };
struct TerminalColor { TerminalColorMode mode = TerminalColorMode::Default; uint32_t value = 0; }; // TrueColor: 0xRRGGBB
struct TerminalCell {
    uint32_t codepoint = ' ';
    TerminalColor foreground = {}, background = {};
    uint8_t width = 1; // 2 for a wide leading cell, 0 for its continuation.
    bool bold = false, underline = false, inverse = false;
};
struct TerminalGridDesc {
    uint32_t id = 0;
    std::span<const TerminalCell> cells = {}; // Caller-owned physical row ring; no parser or retained copy.
    uint32_t columns = 80, rows = 24, first_row = 0;
    float cell_width_px = 9, line_height_px = 20, font_size_px = 14;
    uint16_t font_id = kFontMono;
    Color foreground = Color::theme_auto(), background = Color::theme_auto();
    int32_t cursor_column = -1, cursor_row = -1;
    Color cursor_color = Color::theme_auto();
    SpringSpec cursor_motion = SpringSpec::snappy();
};
void terminal_grid(const TerminalGridDesc&) noexcept;

enum class SheetEdge : uint8_t { Bottom, Left, Right };
struct SheetDesc {
    uint32_t id = 0;
    bool open = true;
    SheetEdge edge = SheetEdge::Bottom;
    float extent_px = 360;
    float snap_points[8] = {0,.5f,1}; // Strictly increasing normalized visible fractions.
    uint32_t snap_count = 3;
    uint32_t* snap_index = nullptr; // Optional caller-owned selection; updated after a drag.
    Color background = Color::theme_auto();
    SpringSpec motion = SpringSpec::fluid();
    int16_t z_index = 16000;
    bool* open_state = nullptr;       // Optional writable open flag updated on backdrop/Escape/snap-0 dismissal.
    bool dismiss_on_backdrop = false;
    bool dismiss_on_escape = false;
    bool show_handle = true;
    Color backdrop_color = {};
    Color border_color = {};
    float border_width = 0.0f;
    float corner_radius_px = 16.0f;
    float inset_px = 0.0f;
    float backdrop_blur = 0.0f;
};
bool begin_sheet(const SheetDesc&) noexcept; // Call end_sheet only when true.
void end_sheet() noexcept;

void begin_box(const BoxDesc& desc = {}) noexcept;
void end_box() noexcept;
void box(const BoxDesc& desc = {}) noexcept;
void text(Str content, const TextDesc& desc = {}) noexcept;
void text_spans(std::span<const TextSpan> spans, const RichTextDesc& desc = {}) noexcept;
inline void text_spans(std::initializer_list<TextSpan> spans, const RichTextDesc& desc = {}) noexcept {
    text_spans(std::span<const TextSpan>(spans.begin(), spans.size()), desc);
}
void image(const ImageDesc& desc) noexcept;
void icon(const IconDesc& desc) noexcept;

// Box-local immediate 2D vector drawing (coordinates relative to the currently open begin_box)
void draw_rect(const RectDrawDesc& desc) noexcept;
void draw_line(const LineDesc& desc) noexcept;
void draw_filled_polygon(const FilledPolygonDesc& desc) noexcept;
void draw_filled_path(const FilledPathDesc& desc) noexcept;
void draw_area_series(const AreaSeriesDesc& desc) noexcept;
void draw_polyline(const PolylineDesc& desc) noexcept;
void draw_bezier(const BezierDesc& desc) noexcept;
void draw_ring(const RingDesc& desc) noexcept;
void draw_icon(const IconDrawDesc& desc) noexcept;
struct FontMetrics {float ascent = 0, descent = 0, line_height = 0;};
FontMetrics font_metrics(uint16_t font_id = kFontSans, float size = 14.0f) noexcept;
Vec2 measure_text(Str text, uint16_t font_id = kFontSans, float size = 14.0f, float letter_spacing = 0.0f) noexcept;
void draw_text(const TextDrawDesc& desc) noexcept;
// Invoked at end_frame after layout, with local logical bounds from this frame.
// The callback may measure/read state and call draw_*; it must not build layout or change UI state.
// user_data must remain valid until end_frame returns. No second layout pass is required.
using LayoutDrawFn = void (*)(Rect local_bounds, void* user_data) noexcept;
void draw_after_layout(LayoutDrawFn callback, void* user_data = nullptr, bool foreground = false) noexcept;
void draw_image(const ImageDrawDesc& desc) noexcept;

uint32_t create_sprite_rgba(Context* ctx, const TextureDesc& desc) noexcept;
bool update_sprite_rgba(Context* ctx, uint32_t sprite_id, uint32_t width, uint32_t height,
    const void* rgba_pixels, uint32_t row_pitch_bytes = 0) noexcept;
uint32_t load_sprite_file(Context* ctx, const char* path, bool srgb = true) noexcept;
uint32_t load_sprite_memory(Context* ctx, const void* data, size_t size_bytes, bool srgb = true) noexcept;
void destroy_sprite(Context* ctx, uint32_t sprite_id) noexcept;
Vec2 sprite_size(const Context* ctx, uint32_t sprite_id) noexcept;
uint32_t sprite_count(const Context* ctx) noexcept;

LoadedTexture create_texture_rgba(Context* ctx, const TextureDesc& desc) noexcept;
// Upload calls copy caller pixels immediately, share a queued GPU batch, and flush before Etch rendering.
// Call explicitly before consuming updated textures with external SDL GPU commands (same owning thread).
bool flush_texture_uploads(Context* ctx) noexcept;
bool update_texture_rgba(
    Context* ctx,
    SDL_GPUTexture* texture,
    uint32_t width,
    uint32_t height,
    const void* rgba_pixels,
    uint32_t row_pitch_bytes = 0) noexcept;
// The caller owns a 2D RGBA8 texture; the rectangle must fit its mip-zero dimensions.
// Partial uploads preserve all texels outside the rectangle and copy caller pixels immediately.
bool update_texture_region_rgba(Context* ctx, SDL_GPUTexture* texture,
    uint32_t x, uint32_t y, uint32_t width, uint32_t height,
    const void* rgba_pixels, uint32_t row_pitch_bytes = 0) noexcept;
LoadedTexture load_texture_file(Context* ctx, const char* path, bool srgb = true) noexcept;
LoadedTexture load_texture_memory(Context* ctx, const void* data, size_t size_bytes, bool srgb = true) noexcept;
void destroy_texture(Context* ctx, LoadedTexture* tex) noexcept;
void destroy_texture(Context* ctx, SDL_GPUTexture* tex) noexcept;

TextInputResult text_input(const TextInputDesc& desc) noexcept;
void set_text_selection(uint32_t element_id, uint32_t anchor_byte, uint32_t cursor_byte) noexcept;
bool has_text_selection(uint32_t element_id = 0) noexcept;
Str selected_text(uint32_t element_id = 0, Arena* arena = nullptr) noexcept;
void clear_text_selection(uint32_t element_id = 0) noexcept;
Rect ime_cursor_rect(const Context* ctx = nullptr) noexcept;
void request_text_input(uint32_t element_id, Rect ime_cursor_rect = {}) noexcept;
Str typed_text(uint32_t element_id = 0) noexcept;
struct ClipboardImageData {
    const uint8_t* bytes = nullptr;
    uint32_t size_bytes = 0;
    Str mime_type = {};
};
bool set_clipboard_text(const char* utf8_text) noexcept;
Str get_clipboard_text(Arena* arena = nullptr) noexcept;
ClipboardImageData get_clipboard_image(Arena* arena = nullptr) noexcept;

// Asynchronous native OS folder & file dialogs (non-blocking; completion wakes wait_events()).
struct FileDialogFilter {
    const char* name = nullptr;    // Human-readable label, e.g. "Images"
    const char* pattern = nullptr; // Semicolon-separated extensions ("png;jpg") or "*"
};
struct FileDialogResult {
    bool ready = false;
    bool cancelled = false;
    bool error = false;
    uint32_t request_tag = 0;
    int32_t selected_filter = -1;
    std::span<const Str> paths = {};
};
bool open_folder_dialog(Context* ctx, SDL_Window* parent_window = nullptr, const char* default_location = nullptr, uint32_t request_tag = 0, bool allow_many = false) noexcept;
bool open_file_dialog(Context* ctx, SDL_Window* parent_window = nullptr, std::span<const FileDialogFilter> filters = {}, const char* default_location = nullptr, uint32_t request_tag = 0, bool allow_many = false) noexcept;
bool open_save_file_dialog(Context* ctx, SDL_Window* parent_window = nullptr, std::span<const FileDialogFilter> filters = {}, const char* default_location = nullptr, uint32_t request_tag = 0) noexcept;
FileDialogResult poll_file_dialog(Context* ctx = nullptr, Arena* arena = nullptr) noexcept;

// Interaction, pointer capture, focus navigation, cursor & layout queries
bool hovered(uint32_t element_id) noexcept;
bool pressed(uint32_t element_id) noexcept;
bool clicked(uint32_t element_id) noexcept;
bool active(uint32_t element_id) noexcept;
uint32_t active_id() noexcept;
void set_active(uint32_t element_id) noexcept;
bool focused(uint32_t element_id) noexcept;
uint32_t focused_id() noexcept;
void request_focus(uint32_t element_id) noexcept;
void set_cursor(Cursor cursor) noexcept;
Rect element_bounds(uint32_t element_id) noexcept;
// Snapshot from the last completed frame, in logical screen pixels. Only named boxes
// are supported. visible_bounds intersects ancestor clip bounding rectangles; rounded
// masks and overlapping siblings can further occlude it. No frame-arena pointers escape.
struct ElementGeometry {
    Rect bounds = {}, visible_bounds = {};
    Vec2 local_scale = {1,1}; // Screen pixels per box-local unit, including Canvas zoom.
    float opacity = 0;
};
bool element_geometry(uint32_t element_id, ElementGeometry* out_geometry) noexcept;
// Allows positions outside the box for captured drags; false leaves output unchanged.
bool screen_to_element_local(uint32_t element_id, Vec2 screen_pos, Vec2* out_local) noexcept;


// Pro desktop input, shortcuts, popover dismissal & file drops
uint16_t key_mods() noexcept;
bool key_pressed(uint32_t keycode, bool include_repeat = false) noexcept;
bool shortcut(uint16_t required_mods, uint32_t keycode, bool allow_when_typing = false) noexcept;

Vec2 pointer_pos() noexcept;
Vec2 pointer_delta() noexcept;
bool right_clicked(uint32_t element_id) noexcept;
bool right_clicked_anywhere() noexcept;
bool double_clicked(uint32_t element_id) noexcept;
bool dragging(uint32_t element_id, float threshold_px = 3.0f) noexcept;
bool should_close_popover(uint32_t popover_id, uint32_t trigger_id = 0) noexcept;

std::span<const Str> dropped_files() noexcept;
bool is_os_dragging() noexcept;
Vec2 os_drop_pos() noexcept;
bool os_drag_hovering(uint32_t element_id) noexcept;
std::span<const Str> dropped_files_on(uint32_t element_id) noexcept;
std::span<const Str> dropped_texts() noexcept;
std::span<const Str> dropped_texts_on(uint32_t element_id) noexcept;
void begin_focus_scope(uint32_t scope_id, bool trap_tab = true) noexcept;
void end_focus_scope() noexcept;
bool keyboard_focus_visible(uint32_t element_id = 0) noexcept;

bool begin_drag_payload(
    uint32_t source_id,
    uint32_t kind_hash,
    Str label,
    const void* data,
    uint32_t size,
    uint64_t u64 = 0,
    uint32_t u32 = 0) noexcept;
bool is_dragging_payload(uint32_t kind_hash = 0) noexcept;
bool peek_drag_payload(DragPayloadView* out_view, uint32_t kind_hash = 0) noexcept;
bool accept_drag_payload(uint32_t target_id, uint32_t kind_hash, DragPayloadView* out_view) noexcept;

// Analytic damped spring animator & inertial scroll container control
float spring(uint32_t anim_id, float target, SpringSpec spec = {}) noexcept;
float spring(uint32_t anim_id, float target, float initial_value, SpringSpec spec = {}) noexcept;
Vec2 spring_vec2(uint32_t anim_id, Vec2 target, SpringSpec spec = {}) noexcept;
Vec2 scroll_delta() noexcept;
Vec2 scroll_max(uint32_t element_id) noexcept;
Vec2 scroll_offset(uint32_t element_id) noexcept;
void set_scroll_offset(uint32_t element_id, Vec2 offset, bool stop_momentum = true) noexcept;

// Resizable Split Pane
SplitResult begin_split(const SplitDesc& desc) noexcept;
void split_second() noexcept;
void end_split() noexcept;
float split_ratio(uint32_t split_id) noexcept;
void set_split_ratio(uint32_t split_id, float ratio) noexcept;

// Million-Row Virtual List (double-precision scrolling, O(visible) layout)
VirtualListSlice begin_virtual_list(const VirtualListDesc& desc) noexcept;
void end_virtual_list() noexcept;
void virtual_list_set_item_height(uint32_t list_id, uint32_t row_index, float height_px) noexcept;
void virtual_list_invalidate_heights(uint32_t list_id) noexcept;
double virtual_list_scroll_offset(uint32_t list_id) noexcept;
void set_virtual_list_scroll_offset(uint32_t list_id, double offset_y, bool stop_momentum = true) noexcept;
void virtual_list_scroll_to_row(
    uint32_t list_id,
    uint32_t row_index,
    float item_height_px,
    AlignY align = AlignY::Top) noexcept;
bool virtual_list_is_at_bottom(uint32_t list_id) noexcept;
void virtual_list_scroll_to_bottom(uint32_t list_id, bool stop_momentum = true) noexcept;

// Infinite 2D Canvas & Node-Graph Wires
CanvasState begin_canvas(const CanvasDesc& desc) noexcept;
void end_canvas() noexcept;
void canvas_wire(const CanvasWireDesc& desc) noexcept;
CanvasNodeResult begin_canvas_node(const CanvasNodeDesc& desc) noexcept;
void end_canvas_node() noexcept;
CanvasState canvas_state(uint32_t canvas_id) noexcept;
void set_canvas_view(uint32_t canvas_id, Vec2 pan, float zoom) noexcept;

// F12 Real-Time Performance & Batch Inspector HUD
void set_debug_hud_open(Context* ctx, bool open) noexcept;
bool debug_hud_open(const Context* ctx = nullptr) noexcept;
void set_debug_wireframe(Context* ctx, bool enabled) noexcept;
bool debug_wireframe(const Context* ctx = nullptr) noexcept;

struct CollapsibleScope {
    CollapsibleResult result = {};
    ~CollapsibleScope() noexcept { if (result.visible) end_collapsible(); }
};
#define ETCH_COLLAPSIBLE(...) \
    if (::etch::CollapsibleScope ETCH_CONCAT(_etch_col_, __LINE__) = {::etch::begin_collapsible(__VA_ARGS__)}; \
        ETCH_CONCAT(_etch_col_, __LINE__).result.visible)

struct BoxScope {
    bool active = true;
    ~BoxScope() noexcept { end_box(); }
};

#define ETCH_CONCAT_INNER(a, b) a##b
#define ETCH_CONCAT(a, b) ETCH_CONCAT_INNER(a, b)
#define ETCH_BOX(...) \
    if (::etch::BoxScope ETCH_CONCAT(_etch_box_, __LINE__) = (::etch::begin_box(__VA_ARGS__), ::etch::BoxScope{}); \
        ETCH_CONCAT(_etch_box_, __LINE__).active)

struct LayerScope {
    bool active = true;
    ~LayerScope() noexcept { pop_layer(); }
};
#define ETCH_LAYER(...) \
    for (::etch::LayerScope ETCH_CONCAT(_etch_layer_, __LINE__) = (::etch::push_layer(__VA_ARGS__), ::etch::LayerScope{}); \
         ETCH_CONCAT(_etch_layer_, __LINE__).active; ETCH_CONCAT(_etch_layer_, __LINE__).active = false)

struct PresenceScope {
    PresenceResult result = {};
    ~PresenceScope() noexcept { if (result.visible) pop_layer(); }
};
#define ETCH_PRESENCE(id, mounted) \
    for (::etch::PresenceScope ETCH_CONCAT(_etch_presence_, __LINE__) = {::etch::presence(id, mounted)}; \
         ETCH_CONCAT(_etch_presence_, __LINE__).result.visible && \
             (::etch::push_layer({.opacity = ETCH_CONCAT(_etch_presence_, __LINE__).result.opacity}), true); \
         ::etch::pop_layer(), ETCH_CONCAT(_etch_presence_, __LINE__).result.visible = false)

struct ThemeScope {
    bool active = true;
    ~ThemeScope() noexcept { pop_theme(); }
};
#define ETCH_THEME(...) \
    if (::etch::ThemeScope ETCH_CONCAT(_etch_theme_, __LINE__) = (::etch::push_theme(__VA_ARGS__), ::etch::ThemeScope{}); \
        ETCH_CONCAT(_etch_theme_, __LINE__).active)

} // namespace etch

#endif // ETCH_H
