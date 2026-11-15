// Etch v2.2 — Pure-GPU UI Engine
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

#define ETCH_VERSION_MAJOR 2
#define ETCH_VERSION_MINOR 2
#define ETCH_VERSION_PATCH 0

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
    static constexpr Color white(uint8_t alpha = 255) noexcept { return {255, 255, 255, alpha}; }
    static constexpr Color black(uint8_t alpha = 255) noexcept { return {0, 0, 0, alpha}; }

    // Little-endian R8G8B8A8 word: R in bits [0..7], G [8..15], B [16..23], A [24..31].
    constexpr uint32_t pack_rgba8() const noexcept {
        return static_cast<uint32_t>(r) |
               (static_cast<uint32_t>(g) << 8) |
               (static_cast<uint32_t>(b) << 16) |
               (static_cast<uint32_t>(a) << 24);
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
};

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
};

enum PrimFlags : uint8_t {
    PrimFlag_None = 0,
    PrimFlag_LinearGrad = 1 << 0,
    PrimFlag_RadialGrad = 1 << 1,
    PrimFlag_RoundedClip = 1 << 2,
    PrimFlag_Premultiplied = 1 << 3,
    PrimFlag_StemDarken = 1 << 4,
    PrimFlag_RoundCap = 1 << 5,
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
//       uint4  style;    // x = header [kind:4 | flags:6 | tex_slot:10 | clip_r_q2:12];
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
        const float clamped_r = (clip_radius_px < 0.0f)
            ? 0.0f
            : ((clip_radius_px > 1023.75f) ? 1023.75f : clip_radius_px);
        const uint32_t clip_r_q2 = static_cast<uint32_t>(clamped_r * 4.0f + 0.5f) & 0xFFFu;
        return (static_cast<uint32_t>(kind) & 0xFu) |
               ((static_cast<uint32_t>(flags) & 0x3Fu) << 4) |
               ((static_cast<uint32_t>(tex_slot) & 0x3FFu) << 10) |
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
        const uint8_t flags = (clip_radius > 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None;
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
        bool radial = false) noexcept
    {
        const uint8_t grad_flag = radial ? PrimFlag_RadialGrad : PrimFlag_LinearGrad;
        const uint8_t clip_flag = (clip_radius > 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None;
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
                pack_f16x2(border_width, 1.0f),
            },
        };
    }

    static constexpr Prim make_shadow(
        Rect rect,
        Rect clip_rect,
        Color shadow_color,
        Corners radius = {},
        float blur_radius = 12.0f,
        float clip_radius = 0.0f) noexcept
    {
        const uint8_t flags = (clip_radius > 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None;
        return Prim{
            .bounds = {rect.x, rect.y, rect.w, rect.h},
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
        const uint8_t all_flags = static_cast<uint8_t>(flags | ((clip_radius > 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None));
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
        const uint8_t all_flags = static_cast<uint8_t>(flags | ((clip_radius > 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None));
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
        const uint8_t flags = static_cast<uint8_t>((clip_radius > 0 ? PrimFlag_RoundedClip : 0) |
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
        const uint8_t all_flags = static_cast<uint8_t>(flags | ((clip_radius > 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None));
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
        const uint8_t all_flags = static_cast<uint8_t>((clip_radius > 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None);
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
        const uint8_t flags = (clip_radius > 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None;
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
        const uint8_t flags = (clip_radius > 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None;
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
            ((clip_radius > 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None));
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

struct Context;

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
};

enum class SystemTheme : uint8_t { Unknown = 0, Light = 1, Dark = 2 };
SystemTheme system_theme() noexcept;
bool system_dark_mode(bool default_when_unknown = true) noexcept;
bool frame_in_progress(const Context* ctx) noexcept;

struct FrameDesc {
    uint32_t viewport_w = 1280;
    uint32_t viewport_h = 720;
    float dpi_scale = 1.0f;
    float dt_seconds = 0.0f;
    Vec2 pointer_pos = {-1.0f, -1.0f};
    bool pointer_down = false;
    Vec2 scroll_delta = {};
    bool right_pointer_down = false;
    uint8_t pointer_clicks = 0;      // 0 = use event clicks (1); 2 = double-click
    uint16_t key_mods = Mod_None;
    uint32_t key_pressed_code = 0;   // Optional single-frame SDL_Keycode injection
};

struct FrameStats {
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
};

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
    bool pointer_passthrough = false;
    bool clip_to_parent = false;
    float backdrop_blur = 0.0f;
    bool clamp_to_viewport = false;
    bool wrap = false;
    float row_gap = -1.0f;
    float aspect_ratio = 0.0f;
    uint16_t grid_columns = 0; // Up to 128 columns; explicit tracks take precedence.
    std::span<const GridTrack> grid_tracks = {}; // Copied into frame storage by begin_box.
    uint16_t grid_col_span = 1;
};

inline constexpr uint16_t kFontSans       = 0u;
inline constexpr uint16_t kFontBold       = 1u;
inline constexpr uint16_t kFontMono       = 2u;
inline constexpr uint16_t kMaxFontSlots   = 16u;
inline constexpr uint16_t kInvalidFont    = 0xFFFFu;
inline constexpr uint16_t kMaxVectorIcons = 256u;
inline constexpr uint16_t kInvalidIcon    = 0xFFFFu;

struct TextDesc {
    Color color = Color::white();
    float font_size_px = 16.0f;
    uint16_t font_id = kFontSans;
    float letter_spacing = 0.0f;
    bool wrap = true;
    uint32_t id = 0;
    bool selectable = false;
    Color selection_color = Color::rgba8(56, 189, 248, 88);
    float line_height_px = 0.0f;
    AlignX align_x = AlignX::Left;
    bool ellipsis = false;
    uint16_t max_lines = 0;
};

struct TextSpan {
    Str      text = {};
    Color    color = Color::white();
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
};

struct RichTextDesc {
    Color    color = Color::white();
    float    font_size_px = 14.0f;
    uint16_t font_id = kFontSans;
    float    line_height_px = 0.0f;  // 0.0f = auto from max span metrics
    float    letter_spacing = 0.0f;
    AlignX   align_x = AlignX::Left;
    bool     wrap = true;
    uint32_t id = 0;
    bool selectable = false;
    Color selection_color = Color::rgba8(56, 189, 248, 88);
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
    Corners radius = Corners::all(6.0f);
    Color header_bg = Color::rgba8(30, 41, 59, 190);
    Color header_hover_bg = Color::rgba8(51, 65, 85, 225);
    Color body_bg = Color::rgba8(15, 23, 42, 150);
    Color border_color = Color::rgba8(148, 163, 184, 38);
    float border_width = 1.0f;
    Color text_color = Color::rgb_hex(0xE2E8F0);
    Color icon_color = Color::rgb_hex(0x38BDF8);
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
};

struct IconDesc {
    uint32_t id = 0;
    uint16_t icon_id = kInvalidIcon;
    float size_px = 16.0f;
    Color color = Color::white();
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
};

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
    Color fill_top = Color::rgba8(56, 189, 248, 96);
    Color fill_bottom = Color::rgba8(56, 189, 248, 0);
    Color line_color = Color::rgb_hex(0x38BDF8);
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
    Color    color = Color::white();
    float    letter_spacing = 0.0f;
    AlignX   align_x = AlignX::Left;
    AlignY   align_y = AlignY::Top;
    bool     foreground = false;
};

struct ImageDrawDesc {
    SDL_GPUTexture* texture = nullptr;
    Rect            rect = {}; // Local to current open box (logical px)
    Rect            uv_rect = {0.0f, 0.0f, 1.0f, 1.0f};
    Color           tint = Color::white();
    float           radius = 0.0f;
    bool            foreground = false;
};

struct TextureDesc {
    const void* rgba_pixels = nullptr;
    uint32_t    width = 0;
    uint32_t    height = 0;
    uint32_t    row_pitch_bytes = 0; // 0 = width * 4
    bool        srgb = true;
    const char* debug_name = nullptr;
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
};

struct WindowChromeDesc {
    bool  enabled = false;
    bool  resizable = true;
    float border_thickness = 6.0f;
    float corner_size = 12.0f;
};

struct DragPayloadView {
    uint32_t    kind_hash = 0;
    Str         label = {};
    uint64_t    u64 = 0;
    uint32_t    u32 = 0;
    const void* data = nullptr;
    uint32_t    size = 0;
};

struct TextInputDesc {
    uint32_t id = 0;
    char* buffer = nullptr;            // Caller-owned mutable NUL-terminated UTF-8 buffer
    uint32_t capacity = 0;             // Total byte capacity of `buffer` (including trailing '\0')
    Str placeholder = {};              // Displayed when buffer & IME preedit are both empty
    Sizing width = Sizing::grow();
    Sizing height = Sizing::fixed(36.0f);
    Padding padding = Padding::xy(12.0f, 8.0f);
    float font_size_px = 14.0f;
    uint16_t font_id = 0;
    float letter_spacing = 0.0f;
    Color text_color = Color::rgb_hex(0xF1F5F9);
    Color placeholder_color = Color::rgb_hex(0x64748B);
    Color selection_color = Color::rgba8(56, 189, 248, 88);
    Color caret_color = Color::rgb_hex(0x38BDF8);
    Color composition_color = Color::rgb_hex(0xFBBF24);
    Color background = Color::rgb_hex(0x0F1420);
    Color border_color = Color::rgba8(148, 163, 184, 52);
    Color focus_border_color = Color::rgb_hex(0x38BDF8);
    float border_width = 1.0f;
    float focus_border_width = 1.5f;
    Corners radius = Corners::all(9.0f);
    bool multiline = false;
    bool read_only = false;
    bool password = false;
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
};

struct SplitDesc {
    uint32_t id = 0;
    Axis axis = Axis::Row; // Row = Left|Right (ResizeEW), Col = Top/Bottom (ResizeNS)
    Sizing width = Sizing::grow();
    Sizing height = Sizing::grow();
    float default_ratio = 0.5f;
    float min_first_px = 120.0f;
    float min_second_px = 120.0f;
    float divider_size_px = 6.0f;
    Color divider_color = Color::rgba8(148, 163, 184, 34);
    Color divider_hover_color = Color::rgba8(56, 189, 248, 165);
    Color divider_active_color = Color::rgb_hex(0x38BDF8);
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
};

struct VirtualListSlice {
    uint32_t first_index = 0;
    uint32_t end_index = 0;
    uint32_t total_count = 0;
    double scroll_y = 0.0;
    double max_scroll_y = 0.0;
    float viewport_height = 0.0f;
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
    Color background = {}; Color pane_background = Color::rgb_hex(0x0F1522);
    Color pane_border_color = Color::rgba8(148, 163, 184, 38);
    float pane_border_width = 1.0f; Corners pane_radius = Corners::all(10.0f);
    Color tab_bar_bg = Color::rgb_hex(0x0B101A); Color tab_active_bg = Color::rgb_hex(0x162033);
    Color tab_hover_bg = Color::rgb_hex(0x111827); Color tab_accent = Color::rgb_hex(0x38BDF8);
};
bool dock_space_initialized(uint32_t dock_id) noexcept;
uint32_t dock_reset_root(uint32_t dock_id) noexcept;
bool dock_split_node(uint32_t dock_id, uint32_t node_idx, Axis axis, float default_ratio,
    uint32_t* out_first_node, uint32_t* out_second_node, uint32_t split_widget_id = 0,
    float min_first_px = 120.0f, float min_second_px = 120.0f) noexcept;
bool dock_add_pane(uint32_t dock_id, uint32_t leaf_node_idx, const DockPaneDesc& pane) noexcept;
bool dock_dock_pane(uint32_t dock_id, uint32_t pane_id, uint32_t target_pane_id, DockZone zone, float split_ratio = 0.5f) noexcept;
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
struct TableDesc {
    uint32_t id = 0; std::span<const TableColumnDesc> columns = {};
    uint32_t item_count = 0; float header_height_px = 30.0f; float row_height_px = 26.0f;
    float row_gap_px = 0.0f; uint32_t overscan_rows = 2u;
    uint32_t default_sort_column = 0; bool default_sort_descending = false;
    float cell_padding_x = 10.0f; Sizing width = Sizing::grow(); Sizing height = Sizing::grow();
    Padding padding = {}; Color background = Color::rgb_hex(0x090D16);
    Color header_background = Color::rgb_hex(0x111726);
    Color zebra_color = Color::rgba8(30, 41, 59, 44); Color hover_row_color = Color::rgba8(56, 189, 248, 22);
    Color border_color = Color::rgba8(148, 163, 184, 34); float border_width = 1.0f;
    Corners radius = Corners::all(8.0f); bool show_scrollbar = true;
};
struct TableResult {
    VirtualListSlice slice = {}; uint32_t sort_column = 0;
    bool sort_descending = false; bool sort_changed = false;
};
TableResult begin_table(const TableDesc& desc) noexcept;
bool table_next_row(uint32_t* out_row_index = nullptr, Color row_bg_override = {}, Color row_border_override = {}) noexcept;
bool table_next_column() noexcept;
void end_table() noexcept;
float table_column_width(uint32_t table_id, uint32_t col_index) noexcept;
void set_table_column_width(uint32_t table_id, uint32_t col_index, float width_px) noexcept;
void set_table_sort(uint32_t table_id, uint32_t col_index, bool descending) noexcept;
void table_scroll_to_row(uint32_t table_id, uint32_t row_index, float row_height_px, AlignY align = AlignY::Top) noexcept;

struct TreeNodeDesc {
    uint32_t id = 0; Str label = {}; uint16_t icon_id = kInvalidIcon;
    Color icon_color = Color::rgb_hex(0x38BDF8); Color text_color = Color::rgb_hex(0xE2E8F0);
    Color selected_bg = Color::rgb_hex(0x1A2942); Color selected_accent = Color::rgb_hex(0x38BDF8);
    Color hover_bg = Color::rgb_hex(0x162033);
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
    Color background = Color::rgb_hex(0x090C12);
    Color minor_grid_color = Color::rgba8(148, 163, 184, 40);
    Color major_grid_color = Color::rgba8(148, 163, 184, 28);
    Color border_color = Color::rgba8(148, 163, 184, 44);
    float border_width = 1.0f;
    Corners radius = Corners::all(10.0f);
    bool show_grid = true;
    bool interactive = true;
    bool marquee_select = false;
    Color marquee_fill = Color::rgba(0.28f, 0.56f, 0.96f, 0.16f);
    Color marquee_border = Color::rgba(0.38f, 0.68f, 1.00f, 0.85f);
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
    Color color_start = Color::rgb_hex(0x38BDF8);
    Color color_end = Color::rgb_hex(0x818CF8);
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
    Color background = Color::rgb_hex(0x111726);
    Color background_end = {};
    Vec2 gradient_dir = {0.0f, 1.0f};
    Color border_color = Color::rgba8(148, 163, 184, 56);
    float border_width_world = 1.0f;
    Corners radius_world = Corners::all(10.0f);
    Color shadow_color = Color::rgba8(0, 0, 0, 130);
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

// Zero-atlas SVG path -> Slug vector icon compiler
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
void end_frame(Context* ctx = nullptr) noexcept;
void begin_window_frame(Context* ctx, SDL_Window* window, float dt) noexcept;
void begin_window_frame(Context* ctx, SDL_Window* window, const WindowFrameDesc& desc) noexcept;
void end_window_frame(Context* ctx = nullptr) noexcept;
bool needs_window_redraw(const Context* ctx, SDL_Window* window) noexcept;
void request_window_redraw(Context* ctx, SDL_Window* window) noexcept;

std::span<const Prim> frame_prims(const Context* ctx) noexcept;
FrameStats frame_stats(const Context* ctx) noexcept;

bool render_window(
    Context* ctx,
    Color clear_color = Color::rgb_hex(0x0E1014),
    SDL_Window* window_override = nullptr) noexcept;
void release_window(Context* ctx, SDL_Window* window) noexcept;
uint32_t claimed_window_count(const Context* ctx) noexcept;

void set_window_chrome(const WindowChromeDesc& desc, SDL_Window* window = nullptr) noexcept;
void window_drag_region(Rect rect_logical) noexcept;
void window_client_region(Rect rect_logical) noexcept;
WindowHit evaluate_window_hit(const Context* ctx, Vec2 logical_pos, SDL_Window* window = nullptr) noexcept;
void minimize_window(SDL_Window* window = nullptr) noexcept;
void maximize_window(SDL_Window* window = nullptr) noexcept;
void restore_window(SDL_Window* window = nullptr) noexcept;
bool is_window_maximized(const Context* ctx = nullptr, SDL_Window* window = nullptr) noexcept;

bool render_headless_png(
    Context* ctx,
    uint32_t width,
    uint32_t height,
    const char* out_png_path,
    Color clear_color = Color::rgb_hex(0x0E1014)) noexcept;

// Implicit-context declarative UI flow (operates on current_context()).
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
Vec2 measure_text(Str text, uint16_t font_id = kFontSans, float size = 14.0f, float letter_spacing = 0.0f) noexcept;
void draw_text(const TextDrawDesc& desc) noexcept;
void draw_image(const ImageDrawDesc& desc) noexcept;

LoadedTexture create_texture_rgba(Context* ctx, const TextureDesc& desc) noexcept;
bool update_texture_rgba(
    Context* ctx,
    SDL_GPUTexture* texture,
    uint32_t width,
    uint32_t height,
    const void* rgba_pixels,
    uint32_t row_pitch_bytes = 0) noexcept;
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
bool set_clipboard_text(const char* utf8_text) noexcept;
Str get_clipboard_text(Arena* arena = nullptr) noexcept;

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
void inject_drop_file(Context* ctx, Str utf8_path, SDL_Window* window = nullptr) noexcept;
void inject_drop_text(Context* ctx, Str utf8_text, SDL_Window* window = nullptr) noexcept;
void inject_os_drop_begin(Context* ctx, Vec2 logical_pos, SDL_Window* window = nullptr) noexcept;
void inject_os_drop_move(Context* ctx, Vec2 logical_pos, SDL_Window* window = nullptr) noexcept;
void inject_os_drop_complete(Context* ctx, Vec2 logical_pos, SDL_Window* window = nullptr) noexcept;
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
Vec2 spring_vec2(uint32_t anim_id, Vec2 target, SpringSpec spec = {}) noexcept;
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

} // namespace etch

#endif // ETCH_H
