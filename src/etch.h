#ifndef ETCH_H
#define ETCH_H

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

#define ETCH_VERSION_MAJOR 0
#define ETCH_VERSION_MINOR 6
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
};

enum PrimFlags : uint8_t {
    PrimFlag_None = 0,
    PrimFlag_LinearGrad = 1 << 0,
    PrimFlag_RadialGrad = 1 << 1,
    PrimFlag_RoundedClip = 1 << 2,
    PrimFlag_Premultiplied = 1 << 3,
    PrimFlag_StemDarken = 1 << 4,
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

    static constexpr Prim make_slug_glyph(
        Rect rect,
        Rect clip_rect,
        Color tint,
        uint32_t band_offset,
        uint16_t num_hbands,
        uint16_t num_vbands,
        float clip_radius = 0.0f,
        uint8_t flags = PrimFlag_StemDarken) noexcept
    {
        const uint8_t all_flags = static_cast<uint8_t>(flags | ((clip_radius > 0.0f) ? PrimFlag_RoundedClip : PrimFlag_None));
        return Prim{
            .bounds = {rect.x, rect.y, rect.w, rect.h},
            .clip = {clip_rect.x, clip_rect.y, clip_rect.x + clip_rect.w, clip_rect.y + clip_rect.h},
            .payload = {
                band_offset,
                static_cast<uint32_t>(num_hbands) | (static_cast<uint32_t>(num_vbands) << 16),
                0u,
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
    SDL_Window* window = nullptr;
    size_t frame_arena_bytes = 4u * 1024u * 1024u;
    uint32_t max_prims_per_frame = 65536u;
    const char* font_path = nullptr;
    float font_size_px = 16.0f;
    bool enable_gpu_debug = false;
    bool headless = false;
};

struct FrameDesc {
    uint32_t viewport_w = 1280;
    uint32_t viewport_h = 720;
    float dpi_scale = 1.0f;
    float dt_seconds = 0.0f;
    Vec2 pointer_pos = {-1.0f, -1.0f};
    bool pointer_down = false;
    Vec2 scroll_delta = {};
};

struct FrameStats {
    uint32_t prim_count = 0;
    uint32_t draw_call_count = 0;
    uint32_t slug_glyph_count = 0;
    uint32_t slug_curve_count = 0;
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
};

struct TextDesc {
    Color color = Color::white();
    float font_size_px = 16.0f;
    uint16_t font_id = 0;
    float letter_spacing = 0.0f;
    bool wrap = true;
};

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

Context* create_context(const ContextDesc& desc) noexcept;
void destroy_context(Context* ctx) noexcept;

Context* current_context() noexcept;
void set_current_context(Context* ctx) noexcept;

Arena* frame_arena(Context* ctx) noexcept;
SDL_GPUDevice* gpu_device(Context* ctx) noexcept;

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

std::span<const Prim> frame_prims(const Context* ctx) noexcept;
FrameStats frame_stats(const Context* ctx) noexcept;

bool render_window(
    Context* ctx,
    Color clear_color = Color::rgb_hex(0x0E1014),
    SDL_Window* window_override = nullptr) noexcept;

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
void image(const ImageDesc& desc) noexcept;
TextInputResult text_input(const TextInputDesc& desc) noexcept;
void set_text_selection(uint32_t element_id, uint32_t anchor_byte, uint32_t cursor_byte) noexcept;
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

// Analytic damped spring animator & inertial scroll container control
float spring(uint32_t anim_id, float target, SpringSpec spec = {}) noexcept;
Vec2 spring_vec2(uint32_t anim_id, Vec2 target, SpringSpec spec = {}) noexcept;
Vec2 scroll_offset(uint32_t element_id) noexcept;
void set_scroll_offset(uint32_t element_id, Vec2 offset, bool stop_momentum = true) noexcept;

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
