// Etch Pure-GPU Uber Shader (SM 6.0 HLSL -> DXIL & Vulkan 1.0 SPIR-V)
// Compiled offline via dxc.exe to src/shaders_gen/etch_uber_{vs,ps}_{dxil,spv}.h.

#define ETCH_PRIM_RECT        0u
#define ETCH_PRIM_SHADOW      1u
#define ETCH_PRIM_GLYPH       2u
#define ETCH_PRIM_TEXTURE     3u
#define ETCH_PRIM_SLUG_GLYPH  4u
#define ETCH_PRIM_BLUR_GLASS  5u
#define ETCH_PRIM_CANVAS_GRID 6u
#define ETCH_PRIM_BEZIER_WIRE 7u
#define ETCH_PRIM_RING_ARC    8u
#define ETCH_PRIM_SLUG_FILL   9u
#define ETCH_PRIM_ROT_RECT    10u
#define ETCH_PRIM_MESH_GRAD   11u
#define ETCH_PRIM_BORDER_BEAM 12u
#define ETCH_PRIM_PUNCH_HOLE  13u
#define ETCH_PRIM_LAYER_BLUR  14u

#define ETCH_FLAG_LINEAR_GRAD   (1u << 0)
#define ETCH_FLAG_RADIAL_GRAD   (1u << 1)
#define ETCH_FLAG_ROUNDED_CLIP  (1u << 2)
#define ETCH_FLAG_PREMULTIPLIED (1u << 3)
#define ETCH_FLAG_STEM_DARKEN   (1u << 4)
#define ETCH_FLAG_ROUND_CAP     (1u << 5)

#define ETCH_GLOBAL_USE_DEPTH   (1u << 30)
#define ETCH_GLOBAL_MANUAL_SRGB (1u << 31)

// Strictly 64 bytes matching etch::Prim in src/etch.h (zero float3 members).
struct Prim
{
    float4 bounds;   // xy = top-left px, zw = width, height px
    float4 clip;     // xy = clip min px, zw = clip max px
    uint4  payload;  // xy = uv0/uv1 (UNORM16x2) OR (color1 RGBA8, f16x2 grad_dir); zw = f16x2 radii
    uint4  style;    // x = header [kind:4 | flags:6 | tex_slot:4 | clip_index_hi:4 | indexed:1 | continuous:1 | clip_r_or_index_lo:12];
                     // y = color0 sRGB RGBA8; z = border sRGB RGBA8; w = f16x2(border_width, softness)
};

// Vertex stage bindings per SDL_CreateGPUShader:
//   DXIL:  t0, space0 (storage buffer), b0, space1 (uniform buffer)
//   SPIRV: set 0 binding 0 (storage buffer), set 1 binding 0 (uniform buffer)
#ifdef __spirv__
[[vk::binding(0, 0)]]
#endif
StructuredBuffer<Prim> g_prims : register(t0, space0);

#ifdef __spirv__
[[vk::binding(1, 0)]]
#endif
StructuredBuffer<uint> g_prim_depths : register(t1, space0);

#ifdef __spirv__
[[vk::binding(0, 1)]]
#endif
cbuffer GlobalsVS : register(b0, space1)
{
    float2 g_viewport_size;
    float2 g_inv_viewport_size;
    float  g_dpi_scale;
    float  g_time_seconds;
    uint   g_frame_index;
    uint   g_flags;
};

// Fragment stage bindings per SDL_CreateGPUShader:
//   DXIL:  t0..t8, space2 + s0..s8, space2
//   SPIRV: set 2 binding 0..8 combined image samplers
#ifdef __spirv__
#define ETCH_COMBINED_SAMPLER(slot) [[vk::combinedImageSampler]] [[vk::binding(slot, 2)]]
#else
#define ETCH_COMBINED_SAMPLER(slot)
#endif

ETCH_COMBINED_SAMPLER(0) Texture2D<float4> g_tex0 : register(t0, space2);
ETCH_COMBINED_SAMPLER(0) SamplerState      g_smp0 : register(s0, space2);

ETCH_COMBINED_SAMPLER(1) Texture2D<float4> g_tex1 : register(t1, space2);
ETCH_COMBINED_SAMPLER(1) SamplerState      g_smp1 : register(s1, space2);

ETCH_COMBINED_SAMPLER(2) Texture2D<float4> g_tex2 : register(t2, space2);
ETCH_COMBINED_SAMPLER(2) SamplerState      g_smp2 : register(s2, space2);

ETCH_COMBINED_SAMPLER(3) Texture2D<float4> g_tex3 : register(t3, space2);
ETCH_COMBINED_SAMPLER(3) SamplerState      g_smp3 : register(s3, space2);

ETCH_COMBINED_SAMPLER(4) Texture2D<float4> g_tex4 : register(t4, space2);
ETCH_COMBINED_SAMPLER(4) SamplerState g_smp4 : register(s4, space2);
ETCH_COMBINED_SAMPLER(5) Texture2D<float4> g_tex5 : register(t5, space2);
ETCH_COMBINED_SAMPLER(5) SamplerState g_smp5 : register(s5, space2);
ETCH_COMBINED_SAMPLER(6) Texture2D<float4> g_tex6 : register(t6, space2);
ETCH_COMBINED_SAMPLER(6) SamplerState g_smp6 : register(s6, space2);
ETCH_COMBINED_SAMPLER(7) Texture2D<float4> g_tex7 : register(t7, space2);
ETCH_COMBINED_SAMPLER(7) SamplerState g_smp7 : register(s7, space2);
ETCH_COMBINED_SAMPLER(8) Texture2D<float4> g_tex8 : register(t8, space2);
ETCH_COMBINED_SAMPLER(8) SamplerState g_smp8 : register(s8, space2);

#ifdef __spirv__
#define ETCH_FRAG_STORAGE(slot) [[vk::binding(slot, 2)]]
#else
#define ETCH_FRAG_STORAGE(slot)
#endif

ETCH_FRAG_STORAGE(9) StructuredBuffer<uint4> g_slug_curves : register(t9, space2);
ETCH_FRAG_STORAGE(10) StructuredBuffer<uint2> g_slug_bands  : register(t10, space2);

ETCH_FRAG_STORAGE(11) StructuredBuffer<uint4> g_dyn_slug_curves : register(t11, space2);
ETCH_FRAG_STORAGE(12) StructuredBuffer<uint2> g_dyn_slug_bands : register(t12, space2);

struct ClipShape {
    float4 bounds;
    float4 radii;
    uint4 meta;
};
ETCH_FRAG_STORAGE(13) StructuredBuffer<ClipShape> g_clip_shapes : register(t13, space2);

struct VSOut
{
    float4 position        : SV_Position;
    nointerpolation float2 rotation : TEXCOORD11;
    nointerpolation uint clip_index : TEXCOORD12;
    float2 pixel_pos       : TEXCOORD0;
    nointerpolation float4 bounds         : TEXCOORD1;
    nointerpolation float4 clip           : TEXCOORD2;
    nointerpolation float4 radii          : TEXCOORD3;
    nointerpolation float4 color0         : TEXCOORD4;
    nointerpolation float4 color1         : TEXCOORD5;
    nointerpolation float4 border_color   : TEXCOORD6;
    nointerpolation float4 uv_rect        : TEXCOORD7;
    nointerpolation float4 grad_params    : TEXCOORD8; // xy = grad_dir, z = border_width, w = softness
    nointerpolation uint4  meta           : TEXCOORD9; // x = kind, y = flags | manual_srgb_bit, z = tex_slot, w = asuint(clip_radius)
    nointerpolation uint2  slug_meta      : TEXCOORD10; // x = band_offset, y = num_hbands | (num_vbands << 16)
};

// Bitwise IEEE-754 binary16 -> float32 decode for portable SM 6.0 DXIL & Vulkan 1.0 SPIR-V.
float unpack_f16_bits(uint h)
{
    uint sign_bit = (h & 0x8000u) << 16;
    uint exp_bits = (h >> 10) & 0x1Fu;
    uint mant_bits = h & 0x03FFu;

    if (exp_bits == 0u)
    {
        if (mant_bits == 0u)
        {
            return asfloat(sign_bit);
        }
        // Normalize subnormal
        float val = float(mant_bits) * (1.0f / 16777216.0f); // mant * 2^-24
        return (sign_bit != 0u) ? -val : val;
    }
    if (exp_bits == 31u)
    {
        uint bits = sign_bit | 0x7F800000u | (mant_bits << 13);
        return asfloat(bits);
    }
    uint f_bits = sign_bit | ((exp_bits + 112u) << 23) | (mant_bits << 13);
    return asfloat(f_bits);
}

float2 unpack_f16x2_custom(uint packed)
{
    return float2(unpack_f16_bits(packed & 0xFFFFu), unpack_f16_bits((packed >> 16) & 0xFFFFu));
}

float2 unpack_unorm16x2(uint packed)
{
    return float2(float(packed & 0xFFFFu), float((packed >> 16) & 0xFFFFu)) * (1.0f / 65535.0f);
}

float srgb_to_linear_chan(float c)
{
    return (c <= 0.04045f) ? (c * (1.0f / 12.92f)) : pow(max((c + 0.055f) * (1.0f / 1.055f), 0.0f), 2.4f);
}

float4 srgb_to_linear(float4 c)
{
    return float4(srgb_to_linear_chan(c.r), srgb_to_linear_chan(c.g), srgb_to_linear_chan(c.b), c.a);
}

float linear_to_srgb_chan(float c)
{
    return (c <= 0.0031308f) ? (c * 12.92f) : (1.055f * pow(max(c, 0.0f), 1.0f / 2.4f) - 0.055f);
}

float4 linear_to_srgb(float4 c)
{
    return float4(linear_to_srgb_chan(c.r), linear_to_srgb_chan(c.g), linear_to_srgb_chan(c.b), c.a);
}

float4 unpack_rgba8_srgb_to_linear(uint packed)
{
    float4 c = float4(
        float(packed & 0xFFu),
        float((packed >> 8) & 0xFFu),
        float((packed >> 16) & 0xFFu),
        float((packed >> 24) & 0xFFu)
    ) * (1.0f / 255.0f);
    return srgb_to_linear(c);
}

VSOut vs_main(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    uint prim_base = g_flags & 0x00FFFFFFu;
    Prim p = g_prims[prim_base + instance_id];

    uint header      = p.style.x;
    uint kind        = header & 0xFu;
    uint flags       = (header >> 4) & 0x3Fu;
    uint tex_slot    = (header >> 10) & 0xFu;
    float clip_r     = (header & (1u<<18)) != 0u ? 0.0f : float((header >> 20) & 0xFFFu) * ((header & (1u << 19)) != 0u ? -0.25f : 0.25f);

    float2 bw_soft   = unpack_f16x2_custom(p.style.w);
    float2 r_tl_tr   = unpack_f16x2_custom(p.payload.z);
    float2 r_br_bl   = unpack_f16x2_custom(p.payload.w);

    float pad = 1.0f;
    if (kind == ETCH_PRIM_GLYPH || kind == ETCH_PRIM_CANVAS_GRID || kind == ETCH_PRIM_BEZIER_WIRE || kind == ETCH_PRIM_RING_ARC || kind == ETCH_PRIM_LAYER_BLUR)
    {
        // Keep glyph quad inside texel bounds; CanvasGrid, BezierWire, RingArc & LayerBlur already include AA/blur bounds.
        pad = 0.0f;
    }
    else if (kind == ETCH_PRIM_SHADOW)
    {
        pad = max(bw_soft.y, 1.0f) * 2.0f + 2.0f;
    }
    else if (kind == ETCH_PRIM_PUNCH_HOLE)
    {
        pad = max(1.0f, bw_soft.y * .5f + 1.0f);
    }
    else if (kind == ETCH_PRIM_BORDER_BEAM)
    {
        pad = max(max(unpack_f16x2_custom(p.payload.y).y, 1.0f) + 2.0f, bw_soft.y * .5f + 1.0f);
    }

    // Quad triangle-list corners: (0,0), (1,0), (1,1), (0,0), (1,1), (0,1)
    static const float2 kCorners[6] = {
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(0.0f, 1.0f)
    };
    float2 corner = kCorners[vertex_id % 6u];

    float2 q_min = p.bounds.xy - pad;
    float2 q_max = p.bounds.xy + p.bounds.zw + pad;

    float angle = kind == ETCH_PRIM_ROT_RECT ? bw_soft.y :
        (kind == ETCH_PRIM_SLUG_GLYPH ? asfloat(p.payload.z) : 0.0f);
    float c = cos(angle), sn = sin(angle);
    if (angle != 0.0f) {
        float2 hs = p.bounds.zw * 0.5f;
        float2 extent = float2(abs(c)*hs.x + abs(sn)*hs.y, abs(sn)*hs.x + abs(c)*hs.y) + pad;
        float2 center = p.bounds.xy + hs;
        q_min = center - extent;
        q_max = center + extent;
    }
    if (kind == ETCH_PRIM_ROT_RECT) bw_soft.y = 1.0f;
    // Cull / tighten quad to clip rectangle (+1px AA margin) for fillrate savings.
    q_min = max(q_min, p.clip.xy - 1.0f);
    q_max = min(q_max, p.clip.zw + 1.0f);
    q_max = max(q_max, q_min);

    float2 px = lerp(q_min, q_max, corner);

    float4 col1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 uv_rect = float4(0.0f, 0.0f, 1.0f, 1.0f);
    float2 grad_dir = float2(0.0f, 1.0f);

    if (kind == ETCH_PRIM_GLYPH || kind == ETCH_PRIM_TEXTURE)
    {
        uv_rect = float4(unpack_unorm16x2(p.payload.x), unpack_unorm16x2(p.payload.y));
    }
    else if (kind == ETCH_PRIM_BLUR_GLASS)
    {
        uv_rect = float4(g_inv_viewport_size, 1.0f - asfloat(p.payload.x), 0.0f);
    }
    else if (kind == ETCH_PRIM_LAYER_BLUR)
    {
        uv_rect = float4(g_inv_viewport_size, asfloat(p.payload.x), float(p.payload.y));
    }
    else if (kind == ETCH_PRIM_CANVAS_GRID)
    {
        uv_rect = float4(asfloat(p.payload.x), asfloat(p.payload.y), r_tl_tr.x, r_tl_tr.y);
        col1    = float4(r_br_bl.x, r_br_bl.y, 0.0f, 0.0f);
    }
    else if (kind == ETCH_PRIM_BEZIER_WIRE)
    {
        uv_rect = float4(unpack_unorm16x2(p.payload.x), unpack_unorm16x2(p.payload.y));
        r_tl_tr = unpack_unorm16x2(p.payload.z);
        r_br_bl = float2(0.0f, 0.0f);
        col1    = unpack_rgba8_srgb_to_linear(p.payload.w);
    }
    else if (kind == ETCH_PRIM_RING_ARC)
    {
        uv_rect = float4(asfloat(p.payload.x), asfloat(p.payload.y), r_tl_tr.x, r_tl_tr.y);
        col1    = unpack_rgba8_srgb_to_linear(p.payload.w);
    }
    else if (kind == ETCH_PRIM_MESH_GRAD)
    {
        col1 = unpack_rgba8_srgb_to_linear(p.payload.x);
        uv_rect = unpack_rgba8_srgb_to_linear(p.payload.y);
    }
    else if (kind == ETCH_PRIM_BORDER_BEAM)
    {
        uv_rect = float4(unpack_f16x2_custom(p.payload.x), unpack_f16x2_custom(p.payload.y));
    }
    else if (kind != ETCH_PRIM_SLUG_GLYPH && (flags & (ETCH_FLAG_LINEAR_GRAD | ETCH_FLAG_RADIAL_GRAD)) != 0u)
    {
        col1 = unpack_rgba8_srgb_to_linear(kind == ETCH_PRIM_SLUG_FILL ? p.payload.z : p.payload.x);
        grad_dir = unpack_f16x2_custom(kind == ETCH_PRIM_SLUG_FILL ? p.payload.w : p.payload.y);
    }

    VSOut o;
    float z = ((g_flags & ETCH_GLOBAL_USE_DEPTH) != 0u)
        ? (1.0f - float(g_prim_depths[prim_base + instance_id] + 1u) * (1.0f / 8388608.0f))
        : 0.0f;
    o.position = float4(
        px.x * g_inv_viewport_size.x * 2.0f - 1.0f,
        1.0f - px.y * g_inv_viewport_size.y * 2.0f,
        z,
        1.0f
    );
    o.rotation = float2(c, sn);
    o.clip_index=(header & (1u<<18)) != 0u ? ((header>>20) | (((header>>14)&15u)<<12)) : 0u;
    o.pixel_pos    = px;
    o.bounds       = p.bounds;
    o.clip         = p.clip;
    o.radii        = float4(r_tl_tr.x, r_tl_tr.y, r_br_bl.x, r_br_bl.y);
    o.color0       = unpack_rgba8_srgb_to_linear(p.style.y);
    o.color1       = col1;
    o.border_color = unpack_rgba8_srgb_to_linear(p.style.z);
    o.uv_rect      = uv_rect;
    o.grad_params  = float4(grad_dir.x, grad_dir.y, bw_soft.x, bw_soft.y);
    o.meta         = uint4(kind, flags | (g_flags & ETCH_GLOBAL_MANUAL_SRGB), tex_slot, asuint(clip_r));
    if ((kind == ETCH_PRIM_SLUG_GLYPH || kind == ETCH_PRIM_GLYPH || kind == ETCH_PRIM_TEXTURE) && (flags & ETCH_FLAG_ROUND_CAP) != 0u)
        o.grad_params = float4(asfloat(p.style.z), asfloat(p.style.w), 0.0f, 1.0f);
    if (kind == ETCH_PRIM_MESH_GRAD) o.grad_params = float4(bw_soft, 0.0f, 0.0f);
    if (kind == ETCH_PRIM_BORDER_BEAM) o.grad_params = float4(bw_soft, 0.0f, 0.0f);
    o.slug_meta    = uint2(p.payload.x, p.payload.y);
    return o;
}

uint slug_calc_root_code(float y1, float y2, float y3)
{
    uint i1 = asuint(y1) >> 31u;
    uint i2 = asuint(y2) >> 30u;
    uint i3 = asuint(y3) >> 29u;
    uint shift = (i2 & 2u) | (i1 & ~2u);
    shift = (i3 & 4u) | (shift & ~4u);
    return (0x2E74u >> shift) & 0x0101u;
}

// Oklab matrices: Bjorn Ottosson's public-domain reference (see THIRD_PARTY_NOTICES.md).
float3 mesh_to_lab(float3 rgb) {
    float3 lms = float3(dot(rgb,float3(.4122214708,.5363325363,.0514459929)),
        dot(rgb,float3(.2119034982,.6806995451,.1073969566)),dot(rgb,float3(.0883024619,.2817188376,.6299787005)));
    lms = pow(max(lms,0.0f),1.0f/3.0f);
    return float3(dot(lms,float3(.2104542553,.7936177850,-.0040720468)),
        dot(lms,float3(1.9779984951,-2.4285922050,.4505937099)),dot(lms,float3(.0259040371,.7827717662,-.8086757660)));
}
float3 mesh_from_lab(float3 lab) {
    float3 lms = float3(lab.x+.3963377774*lab.y+.2158037573*lab.z,
        lab.x-.1055613458*lab.y-.0638541728*lab.z,lab.x-.0894841775*lab.y-1.2914855480*lab.z);
    lms=lms*lms*lms;
    return saturate(float3(dot(lms,float3(4.0767416621,-3.3077115913,.2309699292)),
        dot(lms,float3(-1.2684380046,2.6097574011,-.3413193965)),dot(lms,float3(-.0041960863,-.7034186147,1.7076147010))));
}

float2 slug_solve_horiz_poly(float4 p12, float2 p3)
{
    float2 a = p12.xy - p12.zw * 2.0f + p3;
    float2 b = p12.xy - p12.zw;
    float t1, t2;
    if (abs(a.y) < (1.0f / 65536.0f))
    {
        float t = (abs(b.y) > 1e-6f) ? clamp(p12.y * (0.5f / b.y), -1e4f, 1e4f) : 0.5f;
        t1 = t;
        t2 = t;
    }
    else
    {
        float ra = 1.0f / a.y;
        float d  = sqrt(max(b.y * b.y - a.y * p12.y, 0.0f));
        t1 = (b.y - d) * ra;
        t2 = (b.y + d) * ra;
    }
    return float2((a.x * t1 - b.x * 2.0f) * t1 + p12.x,
                  (a.x * t2 - b.x * 2.0f) * t2 + p12.x);
}

float2 slug_solve_vert_poly(float4 p12, float2 p3)
{
    float2 a = p12.xy - p12.zw * 2.0f + p3;
    float2 b = p12.xy - p12.zw;
    float t1, t2;
    if (abs(a.x) < (1.0f / 65536.0f))
    {
        float t = (abs(b.x) > 1e-6f) ? clamp(p12.x * (0.5f / b.x), -1e4f, 1e4f) : 0.5f;
        t1 = t;
        t2 = t;
    }
    else
    {
        float ra = 1.0f / a.x;
        float d  = sqrt(max(b.x * b.x - a.x * p12.x, 0.0f));
        t1 = (b.x - d) * ra;
        t2 = (b.x + d) * ra;
    }
    return float2((a.y * t1 - b.y * 2.0f) * t1 + p12.y,
                  (a.y * t2 - b.y * 2.0f) * t2 + p12.y);
}

float slug_render_shared(float2 render_coord, float2 pixels_per_unit, uint band_offset, uint packed_band_counts,
    StructuredBuffer<uint4> curves, StructuredBuffer<uint2> bands)
{
    uint num_hbands = packed_band_counts & 0xFFFFu;
    uint num_vbands = (packed_band_counts >> 16) & 0xFFFFu;
    if (num_hbands == 0u || num_vbands == 0u)
    {
        return 0.0f;
    }

    int hband_idx = clamp(int(render_coord.y * float(num_hbands)), 0, int(num_hbands) - 1);
    int vband_idx = clamp(int(render_coord.x * float(num_vbands)), 0, int(num_vbands) - 1);

    float xcov = 0.0f;
    float xwgt = 0.0f;
    // CPU band entries occupy 16 bytes; each logical entry spans two uint2 lanes.
    uint2 hband = bands[(band_offset + uint(hband_idx)) * 2u];
    uint  hlist_base = band_offset + hband.y;
    for (uint i = 0u; i < hband.x; ++i)
    {
        uint  curve_idx = bands[(hlist_base + i) * 2u].x;
        uint4 c = curves[curve_idx];
        float2 cp1 = unpack_f16x2_custom(c.x) - render_coord;
        float2 cp2 = unpack_f16x2_custom(c.y) - render_coord;
        float2 cp3 = unpack_f16x2_custom(c.z) - render_coord;

        if (max(max(cp1.x, cp2.x), cp3.x) * pixels_per_unit.x < -0.5f)
        {
            break;
        }

        uint code = slug_calc_root_code(cp1.y, cp2.y, cp3.y);
        if (code != 0u)
        {
            float2 r = slug_solve_horiz_poly(float4(cp1, cp2), cp3) * pixels_per_unit.x;
            if ((code & 1u) != 0u)
            {
                xcov += saturate(r.x + 0.5f);
                xwgt = max(xwgt, saturate(1.0f - abs(r.x) * 2.0f));
            }
            if (code > 1u)
            {
                xcov -= saturate(r.y + 0.5f);
                xwgt = max(xwgt, saturate(1.0f - abs(r.y) * 2.0f));
            }
        }
    }

    float ycov = 0.0f;
    float ywgt = 0.0f;
    uint2 vband = bands[(band_offset + num_hbands + uint(vband_idx)) * 2u];
    uint  vlist_base = band_offset + vband.y;
    for (uint j = 0u; j < vband.x; ++j)
    {
        uint  curve_idx = bands[(vlist_base + j) * 2u].x;
        uint4 c = curves[curve_idx];
        float2 cp1 = unpack_f16x2_custom(c.x) - render_coord;
        float2 cp2 = unpack_f16x2_custom(c.y) - render_coord;
        float2 cp3 = unpack_f16x2_custom(c.z) - render_coord;

        if (max(max(cp1.y, cp2.y), cp3.y) * pixels_per_unit.y < -0.5f)
        {
            break;
        }

        uint code = slug_calc_root_code(cp1.x, cp2.x, cp3.x);
        if (code != 0u)
        {
            float2 r = slug_solve_vert_poly(float4(cp1, cp2), cp3) * pixels_per_unit.y;
            if ((code & 1u) != 0u)
            {
                ycov -= saturate(r.x + 0.5f);
                ywgt = max(ywgt, saturate(1.0f - abs(r.x) * 2.0f));
            }
            if (code > 1u)
            {
                ycov += saturate(r.y + 0.5f);
                ywgt = max(ywgt, saturate(1.0f - abs(r.y) * 2.0f));
            }
        }
    }

    return saturate(max(abs(xcov * xwgt + ycov * ywgt) / max(xwgt + ywgt, 1.0f / 65536.0f),
                        min(abs(xcov), abs(ycov))));
}

float slug_render(float2 coord, float2 size, uint offset, uint counts) {
    return slug_render_shared(coord, size, offset, counts, g_slug_curves, g_slug_bands);
}
float slug_render_dyn(float2 coord, float2 size, uint offset, uint counts) {
    return slug_render_shared(coord, size, offset, counts, g_dyn_slug_curves, g_dyn_slug_bands);
}

float sd_rounded_box(float2 p, float2 half_size, float4 radii, bool continuous = false)
{
    // radii: x = tl (-x,-y), y = tr (+x,-y), z = br (+x,+y), w = bl (-x,+y)
    float2 pair = (p.x > 0.0f) ? radii.zy : radii.wx;
    float r = (p.y > 0.0f) ? pair.x : pair.y;
    r = max(min(r, min(half_size.x, half_size.y)), 0.0f);
    float2 q = abs(p) - half_size + r;
    if (continuous && r > 0.0f) {
        float2 v = max(q, 0.0f);
        float n = sqrt(length(v * v));
        float gradient = n > 1e-4f ? length(v * v * v) / (n * n * n) : 1.0f;
        return (min(max(q.x, q.y), 0.0f) + n - r) / max(gradient, 1e-4f);
    }
    return min(max(q.x, q.y), 0.0f) + length(max(q, 0.0f)) - r;
}

float2 sd_quadratic_bezier(float2 pos, float2 A, float2 B, float2 C)
{
    float2 a = B - A;
    float2 b = A - 2.0f * B + C;
    if (dot(b, b) < 1e-2f)
    {
        float2 ac = C - A;
        float t_seg = saturate(dot(pos - A, ac) / max(dot(ac, ac), 1e-4f));
        return float2(length(pos - lerp(A, C, t_seg)), t_seg);
    }
    float2 c  = a * 2.0f;
    float2 d  = A - pos;
    float  kk = 1.0f / dot(b, b);
    float  kx = kk * dot(a, b);
    float  ky = kk * (2.0f * dot(a, a) + dot(d, b)) / 3.0f;
    float  kz = kk * dot(d, a);

    float p  = ky - kx * kx;
    float p3 = p * p * p;
    float q  = kx * (2.0f * kx * kx - 3.0f * ky) + kz;
    float h  = q * q + 4.0f * p3;

    float best_d2 = 1e20f;
    float best_t  = 0.0f;
    if (h >= 0.0f)
    {
        h = sqrt(h);
        float2 x  = (float2(h, -h) - q) * 0.5f;
        float2 uv = sign(x) * pow(max(abs(x), 1e-12f), float2(1.0f / 3.0f, 1.0f / 3.0f));
        float  t  = saturate(uv.x + uv.y - kx);
        float2 dv = d + (c + b * t) * t;
        best_d2   = dot(dv, dv);
        best_t    = t;
    }
    else
    {
        float  z   = sqrt(max(-p, 1e-8f));
        float  v   = acos(clamp(q / (-2.0f * z * z * z), -1.0f, 1.0f)) / 3.0f;
        float  m   = cos(v);
        float  n   = sin(v) * 1.732050808f;
        float4 t   = saturate(float4(m + m, -n - m, n - m, 0.0f) * z - kx);
        float2 dv1 = d + (c + b * t.x) * t.x;
        float2 dv2 = d + (c + b * t.y) * t.y;
        float2 dv3 = d + (c + b * t.z) * t.z;
        float  d1  = dot(dv1, dv1);
        float  d2  = dot(dv2, dv2);
        float  d3  = dot(dv3, dv3);
        best_d2    = d1;
        best_t     = t.x;
        if (d2 < best_d2) { best_d2 = d2; best_t = t.y; }
        if (d3 < best_d2) { best_d2 = d3; best_t = t.z; }
    }
    return float2(sqrt(max(best_d2, 0.0f)), best_t);
}

float erf_approx(float x)
{
    float ax = abs(x);
    float t = 1.0f / (1.0f + 0.3275911f * ax);
    float poly = (((((1.061405429f * t - 1.453152027f) * t + 1.421413741f) * t - 0.284496736f) * t + 0.254829592f) * t);
    float e = 1.0f - poly * exp(-ax * ax);
    return (x < 0.0f) ? -e : e;
}

float4 sample_slot(uint slot, float2 uv)
{
    if (slot == 1u) return g_tex1.SampleLevel(g_smp1, uv, 0.0f);
    if (slot == 2u) return g_tex2.SampleLevel(g_smp2, uv, 0.0f);
    if (slot == 3u) return g_tex3.SampleLevel(g_smp3, uv, 0.0f);
    if (slot == 4u) return g_tex4.SampleLevel(g_smp4, uv, 0.0f);
    if (slot == 5u) return g_tex5.SampleLevel(g_smp5, uv, 0.0f);
    if (slot == 6u) return g_tex6.SampleLevel(g_smp6, uv, 0.0f);
    if (slot == 7u) return g_tex7.SampleLevel(g_smp7, uv, 0.0f);
    if (slot == 8u) return g_tex8.SampleLevel(g_smp8, uv, 0.0f);
    return g_tex0.SampleLevel(g_smp0, uv, 0.0f);
}

float4 gradient_color(VSOut input, float2 norm_uv) {
    float4 fill_col = input.color0;
    uint flags = input.meta.y;
    bool has_grad = false;
    if ((flags & ETCH_FLAG_LINEAR_GRAD) != 0u)
    {
        float2 dir = input.grad_params.xy;
        float dir_len = length(dir);
        dir = (dir_len > 1e-4f) ? (dir / dir_len) : float2(0.0f, 1.0f);
        float span = max(0.5f * (abs(dir.x) + abs(dir.y)), 1e-4f);
        float t = saturate(0.5f + dot(norm_uv - 0.5f, dir) / (2.0f * span));
        fill_col = lerp(input.color0, input.color1, t);
        has_grad = true;
    }
    else if ((flags & ETCH_FLAG_RADIAL_GRAD) != 0u)
    {
        float t = saturate(length((norm_uv - 0.5f) * 2.0f));
        fill_col = lerp(input.color0, input.color1, t);
        has_grad = true;
    }
    if (has_grad)
    {
        float ign = frac(52.9829189f * frac(dot(input.pixel_pos, float2(0.06711056f, 0.00583715f))));
        float4 enc = linear_to_srgb(saturate(fill_col));
        enc.rgb = saturate(enc.rgb + (ign - 0.5f) * (1.0f / 255.0f));
        fill_col = float4(srgb_to_linear(enc).rgb, fill_col.a);
    }
    return fill_col;
}

float4 ps_main(VSOut input) : SV_Target0
{
    uint kind     = input.meta.x;
    uint flags    = input.meta.y;
    uint tex_slot = input.meta.z;
    float clip_r  = asfloat(input.meta.w);

    // 1. Scissor + parent rounded corner clipping coverage
    float2 clip_half   = 0.5f * max(input.clip.zw - input.clip.xy, 0.0f);
    float2 clip_center = 0.5f * (input.clip.xy + input.clip.zw);
    float2 clip_rel    = input.pixel_pos - clip_center;
    float clip_d;
    if ((flags & ETCH_FLAG_ROUNDED_CLIP) != 0u && clip_r != 0.0f)
    {
        clip_d = sd_rounded_box(clip_rel, clip_half, abs(clip_r).xxxx, clip_r < 0.0f);
    }
    else
    {
        float2 cq = abs(clip_rel) - clip_half;
        clip_d = max(cq.x, cq.y);
    }
    float clip_cov = saturate(0.5f - clip_d);
    float fade_cov = 1.0f;
    uint clip_index=input.clip_index;
    [loop] while(clip_index!=0u) {
        ClipShape shape=g_clip_shapes[clip_index];
        float2 half_shape=shape.bounds.zw*0.5f;
        float2 relative=input.pixel_pos-shape.bounds.xy-half_shape;
        float distance=sd_rounded_box(relative,half_shape,shape.radii,shape.meta.y!=0u);
        clip_cov=min(clip_cov,saturate(0.5f-distance));
        float2 tb=unpack_f16x2_custom(shape.meta.z),lr=unpack_f16x2_custom(shape.meta.w);
        if(tb.x>0.0f) fade_cov*=smoothstep(0.0f,tb.x,input.pixel_pos.y-shape.bounds.y);
        if(tb.y>0.0f) fade_cov*=smoothstep(0.0f,tb.y,shape.bounds.y+shape.bounds.w-input.pixel_pos.y);
        if(lr.x>0.0f) fade_cov*=smoothstep(0.0f,lr.x,input.pixel_pos.x-shape.bounds.x);
        if(lr.y>0.0f) fade_cov*=smoothstep(0.0f,lr.y,shape.bounds.x+shape.bounds.z-input.pixel_pos.x);
        clip_index=shape.meta.x;
    }
    clip_cov*=fade_cov;
    // Glyph/emoji reuse the wire cap flag for a second, horizontal text clip.
    if ((kind == ETCH_PRIM_SLUG_GLYPH || kind == ETCH_PRIM_GLYPH || kind == ETCH_PRIM_TEXTURE) && (flags & ETCH_FLAG_ROUND_CAP) != 0u) {
        float2 text_center=input.bounds.xy+input.bounds.zw*0.5f;
        float text_x=text_center.x+dot(input.rotation,input.pixel_pos-text_center);
        clip_cov *= saturate(0.5f + min(text_x-input.grad_params.x,input.grad_params.y-text_x));
    }
    if (clip_cov <= 0.0f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float2 size      = max(input.bounds.zw, float2(1e-4f, 1e-4f));
    float2 half_size = 0.5f * size;
    float2 center    = input.bounds.xy + half_size;
    float2 p_rel     = input.pixel_pos - center;
    float2 p_local = float2(input.rotation.x*p_rel.x + input.rotation.y*p_rel.y,
        -input.rotation.y*p_rel.x + input.rotation.x*p_rel.y);

    float4 out_pm = float4(0.0f, 0.0f, 0.0f, 0.0f);

    if (kind == ETCH_PRIM_PUNCH_HOLE) {
        float d=sd_rounded_box(p_rel,half_size,input.radii,(flags & ETCH_FLAG_ROUND_CAP)!=0u);
        float coverage=saturate(.5f-d/max(input.grad_params.w,.5f))*input.color0.a;
        out_pm=float4(0.0f,0.0f,0.0f,coverage);
    }
    else if (kind == ETCH_PRIM_BORDER_BEAM) {
        float d=sd_rounded_box(p_rel,half_size,input.radii,(flags & ETCH_FLAG_ROUND_CAP)!=0u);
        float width=input.uv_rect.z;
        float softness=max(input.grad_params.y,.5f);
        float outer=saturate(.5f-d/softness);
        float inner=saturate(.5f-(d+width)/softness);
        float ring=max(outer-inner,0.0f);
        float turn=(atan2(p_rel.y/half_size.y,p_rel.x/half_size.x)-input.uv_rect.x)/6.283185307f;
        float tail=frac(-turn);
        if((flags & 1u)!=0u) tail=min(tail,frac(.5f-turn));
        float beam=saturate(1.0f-tail/max(input.uv_rect.y,.001f));
        beam=beam*beam*(3.0f-2.0f*beam);
        float glow_t=input.uv_rect.w>0.0f ? saturate(1.0f-abs(d+.5f*width)/input.uv_rect.w) : 0.0f;
        float glow=glow_t*glow_t*glow_t*.5f*(1.0f-ring);
        out_pm=float4(input.border_color.rgb,1.0f)*(input.border_color.a*ring*(1.0f-beam))
            +float4(input.color0.rgb,1.0f)*(input.color0.a*beam*(ring+glow));
        if((flags & 2u)!=0u) {
            float rim=(saturate(.5f-d)-saturate(.5f-d-1.0f))*input.grad_params.x*saturate(-p_rel.y/half_size.y);
            out_pm=max(out_pm,float4(rim.xxx,rim));
        }
    }
    else if (kind == ETCH_PRIM_MESH_GRAD) {
        float2 uv=saturate((p_rel+half_size)/size);
        if(input.grad_params.y!=0.0f) uv=saturate(uv+.12f*sin(uv.yx*3.14159265f+input.grad_params.y));
        float4 tl=input.color0, tr=input.border_color, br=input.color1, bl=input.uv_rect;
        bool lab=(flags & 1u)!=0u;
        if(lab) {tl.rgb=mesh_to_lab(tl.rgb);tr.rgb=mesh_to_lab(tr.rgb);br.rgb=mesh_to_lab(br.rgb);bl.rgb=mesh_to_lab(bl.rgb);}
        float4 col=lerp(lerp(tl,tr,uv.x),lerp(bl,br,uv.x),uv.y);
        if(lab) col.rgb=mesh_from_lab(col.rgb);
        if(input.grad_params.x>0.0f) {
            float ign=frac(52.9829189f*frac(dot(input.pixel_pos,float2(.06711056f,.00583715f))));
            float4 encoded=linear_to_srgb(col);
            encoded.rgb=saturate(encoded.rgb+(ign-.5f)*input.grad_params.x);
            col=srgb_to_linear(encoded);
        }
        float d=sd_rounded_box(p_rel,half_size,input.radii,(flags & ETCH_FLAG_ROUND_CAP)!=0u);
        out_pm=float4(col.rgb,1.0f)*(col.a*saturate(.5f-d));
    }
    else if (kind == ETCH_PRIM_RECT || kind == ETCH_PRIM_ROT_RECT)
    {
        float d = sd_rounded_box(p_local, half_size, input.radii, (flags & ETCH_FLAG_ROUND_CAP) != 0u);

        float4 fill_col = gradient_color(input, (p_local + half_size) / size);

        float border_w  = input.grad_params.z;
        float softness  = input.grad_params.w;
        float inv_aa    = 1.0f / max(softness, 0.5f);
        float outer_cov = saturate(0.5f - d * inv_aa);
        float inner_cov = (border_w > 0.0f) ? saturate(0.5f - (d + border_w) * inv_aa) : outer_cov;
        float border_t  = max(outer_cov - inner_cov, 0.0f);

        float4 b_pm = float4(input.border_color.rgb, 1.0f) * (input.border_color.a * border_t);
        float4 f_pm = float4(fill_col.rgb, 1.0f) * fill_col.a;
        out_pm      = b_pm + f_pm * (inner_cov + border_t * (1.0f - input.border_color.a));
    }
    else if (kind == ETCH_PRIM_SHADOW)
    {
        float blur       = max(input.grad_params.w, 1.0f);
        float sigma      = max(blur * 0.5f, 0.5f);
        float inv_s2     = 1.0f / max(sigma * 1.41421356f, 0.5f);
        float d          = sd_rounded_box(p_rel, half_size, input.radii, (flags & ETCH_FLAG_ROUND_CAP) != 0u);
        float edge_cov   = 0.5f * (1.0f - erf_approx(d * inv_s2));
        float size_atten = erf_approx(half_size.x * inv_s2) * erf_approx(half_size.y * inv_s2);
        float shadow_cov = saturate(edge_cov * size_atten);
        out_pm           = float4(input.color0.rgb, 1.0f) * (input.color0.a * shadow_cov);
    }
    else if (kind == ETCH_PRIM_GLYPH)
    {
        float2 norm_uv = saturate((input.pixel_pos - input.bounds.xy) / size);
        float2 uv      = lerp(input.uv_rect.xy, input.uv_rect.zw, norm_uv);
        float  cov     = sample_slot(tex_slot, uv).a;
        float4 glyph_col = input.color0;
        if ((flags & ETCH_FLAG_LINEAR_GRAD) != 0u)
        {
            float rel_x = input.pixel_pos.x - input.bounds.x;
            float x0_rel = input.radii.z;
            float x1_rel = max(input.radii.w, x0_rel + 0.5f);
            glyph_col = lerp(input.color0, input.border_color, smoothstep(x0_rel, x1_rel, rel_x));
        }
        if ((flags & ETCH_FLAG_STEM_DARKEN) != 0u)
        {
            float lin_luma   = saturate(dot(glyph_col.rgb, float3(0.2126f, 0.7152f, 0.0722f)));
            float srgb_luma  = pow(lin_luma, 1.0f / 2.2f);
            float dark_boost = saturate(1.0f - 2.0f * srgb_luma) * 0.24f;
            float light_trim = saturate(2.0f * srgb_luma - 1.0f) * 0.12f;
            cov = lerp(cov, cov * (2.0f - cov), dark_boost);
            cov = lerp(cov, cov * cov, light_trim);
        }
        out_pm = float4(glyph_col.rgb, 1.0f) * (glyph_col.a * cov);
    }
    else if (kind == ETCH_PRIM_SLUG_GLYPH)
    {
        float2 render_coord = float2(
            (p_local.x + half_size.x) / size.x,
            1.0f - (p_local.y + half_size.y) / size.y);
        static const float2 kRgss[4] = {
            float2(-0.125f, -0.375f),
            float2( 0.375f, -0.125f),
            float2( 0.125f,  0.375f),
            float2(-0.375f,  0.125f),
        };
        const bool ssaa = min(size.x, size.y) < 42.0f;
        const uint taps = ssaa ? 4u : 1u;
        const float2 inv_sz = 1.0f / size;
        const float2 ppu = ssaa ? (size * 2.0f) : size;
        float cov = 0.0f;
        [loop] for (uint s = 0u; s < taps; ++s)
        {
            float2 tc = ssaa ? (render_coord + kRgss[s] * inv_sz) : render_coord;
            cov += slug_render(tc, ppu, input.slug_meta.x, input.slug_meta.y);
        }
        cov *= ssaa ? 0.25f : 1.0f;
        if (any(input.rotation != float2(1.0f, 0.0f)))
            cov *= saturate(0.5f - max(abs(p_local.x)-half_size.x, abs(p_local.y)-half_size.y));
        float4 glyph_col = input.color0;
        if ((flags & (ETCH_FLAG_LINEAR_GRAD | ETCH_FLAG_ROUND_CAP)) == ETCH_FLAG_LINEAR_GRAD)
        {
            float rel_x = input.pixel_pos.x - input.bounds.x;
            float x0_rel = input.radii.z;
            float x1_rel = max(input.radii.w, x0_rel + 0.5f);
            glyph_col = lerp(input.color0, input.border_color, smoothstep(x0_rel, x1_rel, rel_x));
        }
        if ((flags & ETCH_FLAG_STEM_DARKEN) != 0u)
        {
            float darken_t  = saturate((28.0f - size.y) / 16.0f);
            float lin_luma  = saturate(dot(glyph_col.rgb, float3(0.2126f, 0.7152f, 0.0722f)));
            float srgb_luma = pow(lin_luma, 1.0f / 2.2f);
            float dark_boost = saturate(1.0f - 2.0f * srgb_luma) * 0.24f * darken_t;
            float light_trim = saturate(2.0f * srgb_luma - 1.0f) * 0.14f * darken_t;
            cov = lerp(cov, cov * (2.0f - cov), dark_boost);
            cov = lerp(cov, cov * cov, light_trim);
        }
        out_pm = float4(glyph_col.rgb, 1.0f) * (glyph_col.a * cov);
    }
    else if (kind == ETCH_PRIM_SLUG_FILL) {
        float2 uv = (p_rel + half_size) / size;
        float2 rc = float2(uv.x, 1.0f - uv.y);
        static const float2 kRgssFill[4] = {
            float2(-0.125f, -0.375f),
            float2( 0.375f, -0.125f),
            float2( 0.125f,  0.375f),
            float2(-0.375f,  0.125f),
        };
        const bool ssaa = min(size.x, size.y) < 42.0f;
        const uint taps = ssaa ? 4u : 1u;
        const float2 inv_sz = 1.0f / size;
        const float2 ppu = ssaa ? (size * 2.0f) : size;
        float cov = 0.0f;
        [loop] for (uint s = 0u; s < taps; ++s)
        {
            float2 tc = ssaa ? (rc + kRgssFill[s] * inv_sz) : rc;
            cov += slug_render_dyn(tc, ppu, input.slug_meta.x, input.slug_meta.y);
        }
        cov *= ssaa ? 0.25f : 1.0f;
        float4 col = gradient_color(input, uv);
        out_pm = float4(col.rgb, 1.0f) * (col.a * cov);
    }
    else if (kind == ETCH_PRIM_TEXTURE)
    {
        float2 norm_uv = clamp((input.pixel_pos - input.bounds.xy) / size, 0.0f, 1.0f);
        float2 uv      = lerp(input.uv_rect.xy, input.uv_rect.zw, norm_uv);
        float4 texel   = sample_slot(tex_slot, uv);

        float d         = sd_rounded_box(p_rel, half_size, input.radii, (flags & ETCH_FLAG_LINEAR_GRAD) != 0u);
        float border_w  = input.grad_params.z;
        float softness  = input.grad_params.w;
        float inv_aa    = 1.0f / max(softness, 0.5f);
        float outer_cov = saturate(0.5f - d * inv_aa);
        float inner_cov = (border_w > 0.0f) ? saturate(0.5f - (d + border_w) * inv_aa) : outer_cov;
        float border_t  = max(outer_cov - inner_cov, 0.0f);

        float4 b_pm = float4(input.border_color.rgb, 1.0f) * (input.border_color.a * border_t);
        float4 tex_pm;
        if ((flags & ETCH_FLAG_PREMULTIPLIED) != 0u)
        {
            tex_pm = float4(texel.rgb * input.color0.rgb * input.color0.a, texel.a * input.color0.a);
        }
        else
        {
            float ta = texel.a * input.color0.a;
            tex_pm = float4(texel.rgb * input.color0.rgb * ta, ta);
        }
        out_pm = b_pm + tex_pm * (inner_cov + border_t * (1.0f - input.border_color.a));
    }
    else if (kind == ETCH_PRIM_BLUR_GLASS)
    {
        float2 backdrop_uv = saturate(input.pixel_pos * input.uv_rect.xy);
        float4 blur_lin    = sample_slot(tex_slot, backdrop_uv);
        // Porter-Duff straight tint OVER premultiplied backdrop preserves alpha on transparent windows.
        float4 tint_pm     = float4(input.color0.rgb * input.color0.a, input.color0.a);
        float4 glass_pm    = tint_pm + blur_lin * (1.0f - input.color0.a);

        // Apply +-0.5/255 IGN dithering in straight sRGB space, then re-premultiply.
        if (glass_pm.a > 1e-4f)
        {
            float4 straight_srgb = linear_to_srgb(float4(saturate(glass_pm.rgb / glass_pm.a), 1.0f));
            float  ign           = frac(52.9829189f * frac(dot(input.pixel_pos, float2(0.06711056f, 0.00583715f))));
            straight_srgb.rgb    = saturate(straight_srgb.rgb + (ign - 0.5f) * (1.0f / 255.0f));
            glass_pm.rgb         = srgb_to_linear(straight_srgb).rgb * glass_pm.a;
        }

        float d         = sd_rounded_box(p_rel, half_size, input.radii, (flags & ETCH_FLAG_ROUND_CAP) != 0u);
        float border_w  = input.grad_params.z;
        float softness  = input.grad_params.w;
        float inv_aa    = 1.0f / max(softness, 0.5f);
        float outer_cov = saturate(0.5f - d * inv_aa);
        float inner_cov = (border_w > 0.0f) ? saturate(0.5f - (d + border_w) * inv_aa) : outer_cov;
        float border_t  = max(outer_cov - inner_cov, 0.0f);

        float4 b_pm = float4(input.border_color.rgb, 1.0f) * (input.border_color.a * border_t);
        float4 g_pm = glass_pm * input.uv_rect.z;
        out_pm      = b_pm + g_pm * (inner_cov + border_t * (1.0f - input.border_color.a));
    }
    else if (kind == ETCH_PRIM_LAYER_BLUR)
    {
        float2 layer_uv = saturate(input.pixel_pos * input.uv_rect.xy);
        float4 c_lo = sample_slot(tex_slot, layer_uv);
        float4 c_hi = sample_slot(uint(input.uv_rect.w + 0.5f), layer_uv);
        out_pm = lerp(c_lo, c_hi, saturate(input.uv_rect.z));
    }
    else if (kind == ETCH_PRIM_CANVAS_GRID)
    {
        float2 local_p    = input.pixel_pos - input.bounds.xy;
        float2 gm         = local_p - input.uv_rect.xy;
        float  minor_step = max(input.uv_rect.z, 2.0f);
        float  major_step = max(input.uv_rect.w, 4.0f);
        float  dot_r      = max(input.grad_params.z, 0.5f);
        float  minor_fade = saturate(input.grad_params.w);

        float2 cell_m  = gm - floor(gm / minor_step + 0.5f) * minor_step;
        float  dot_cov = saturate(0.5f - (length(cell_m) - dot_r)) * minor_fade;

        float2 cell_M  = gm - floor(gm / major_step + 0.5f) * major_step;
        float  maj_d   = min(abs(cell_M.x), abs(cell_M.y));
        float  maj_cov = saturate(0.85f - maj_d);

        float  axis_d   = min(abs(local_p.x - input.color1.x), abs(local_p.y - input.color1.y));
        float  axis_cov = saturate(1.1f - axis_d) * 0.65f;

        float4 min_pm = float4(input.color0.rgb, 1.0f) * (input.color0.a * dot_cov);
        float4 maj_pm = float4(input.border_color.rgb, 1.0f) * saturate(input.border_color.a * max(maj_cov, axis_cov));
        out_pm        = maj_pm + min_pm * (1.0f - maj_pm.a);
    }
    else if (kind == ETCH_PRIM_BEZIER_WIRE)
    {
        float2 A  = input.bounds.xy + input.uv_rect.xy * size;
        float2 B  = input.bounds.xy + input.uv_rect.zw * size;
        float2 C  = input.bounds.xy + input.radii.xy * size;
        float2 dt = sd_quadratic_bezier(input.pixel_pos, A, B, C);
        float  d  = dt.x;
        float  t  = dt.y;

        float  half_w   = max(input.grad_params.z, 0.5f);
        float  glow_r   = input.grad_params.w;
        float  core_cov = saturate(0.5f - (d - half_w));
        float4 wire_col = lerp(input.color0, input.color1, t);
        float4 core_pm  = float4(wire_col.rgb, 1.0f) * (wire_col.a * core_cov);

        float4 glow_pm = float4(0.0f, 0.0f, 0.0f, 0.0f);
        if (glow_r > 0.25f && input.border_color.a > 0.0f)
        {
            float gd       = max(d - half_w, 0.0f);
            float glow_cov = exp(-(gd * gd) / max(0.35f * glow_r * glow_r, 1.0f));
            glow_pm        = float4(input.border_color.rgb, 1.0f) * (input.border_color.a * glow_cov);
        }
        out_pm = core_pm + glow_pm * (1.0f - core_pm.a);
    }
    else if (kind == ETCH_PRIM_RING_ARC)
    {
        float start_rad = input.uv_rect.x;
        float sweep_rad = input.uv_rect.y;
        float abs_sweep = abs(sweep_rad);
        float radius    = max(input.uv_rect.z, 0.5f);
        float half_w    = max(input.uv_rect.w, 0.5f);
        float glow_r    = input.grad_params.w;

        if (abs_sweep >= 1e-4f || (flags & ETCH_FLAG_ROUND_CAP) != 0u)
        {
            float  mid = start_rad + 0.5f * sweep_rad;
            float2 u   = float2(cos(mid), sin(mid));
            float2 v   = float2(-u.y, u.x);
            float  p_u = dot(p_rel, u);
            float  p_v = dot(p_rel, v);

            float d = 0.0f;
            if (abs_sweep >= 6.2830f)
            {
                d = abs(length(p_rel) - radius) - half_w;
            }
            else if (abs_sweep < 1e-4f)
            {
                d = length(p_rel - u * radius) - half_w;
            }
            else
            {
                float  alpha = 0.5f * abs_sweep;
                float2 q     = float2(abs(p_v), p_u);
                float2 sc    = float2(sin(alpha), cos(alpha));
                if (sc.y * q.x <= sc.x * q.y)
                {
                    float d_rad = abs(length(q) - radius) - half_w;
                    d = d_rad;
                    if ((flags & ETCH_FLAG_ROUND_CAP) == 0u) {
                        float proj = clamp(dot(q, sc), max(radius - half_w, 0.0f), radius + half_w);
                        d = max(d_rad, -length(q - sc * proj));
                    }
                }
                else if ((flags & ETCH_FLAG_ROUND_CAP) != 0u)
                {
                    d = length(q - sc * radius) - half_w;
                }
                else
                {
                    float proj = clamp(dot(q, sc), max(radius - half_w, 0.0f), radius + half_w);
                    d = length(q - sc * proj);
                }
            }

            float grad_t = 0.0f;
            if (abs_sweep >= 1e-4f)
            {
                if (abs_sweep >= 6.2830f)
                {
                    float2 u0       = float2(cos(start_rad), sin(start_rad));
                    float2 v0       = float2(-u0.y, u0.x);
                    float  a0       = (length(p_rel) > 1e-4f) ? atan2(dot(p_rel, v0), dot(p_rel, u0)) : 0.0f;
                    float  signed_a = (sweep_rad >= 0.0f) ? a0 : -a0;
                    if (signed_a < 0.0f)
                    {
                        signed_a += 6.28318531f;
                    }
                    grad_t = saturate(signed_a / abs_sweep);
                }
                else
                {
                    float ang = (abs(p_u) + abs(p_v) > 1e-4f) ? atan2(p_v, p_u) : 0.0f;
                    grad_t = saturate(0.5f + ang / sweep_rad);
                }
            }

            float  core_cov = saturate(0.5f - d);
            float4 arc_col  = lerp(input.color0, input.color1, grad_t);
            float4 core_pm  = float4(arc_col.rgb, 1.0f) * (arc_col.a * core_cov);

            float4 glow_pm = float4(0.0f, 0.0f, 0.0f, 0.0f);
            if (glow_r > 0.25f && input.border_color.a > 0.0f)
            {
                float gd       = max(d, 0.0f);
                float glow_cov = exp(-(gd * gd) / max(0.35f * glow_r * glow_r, 1.0f));
                glow_pm        = float4(input.border_color.rgb, 1.0f) * (input.border_color.a * glow_cov);
            }
            out_pm = core_pm + glow_pm * (1.0f - core_pm.a);
        }
    }

    out_pm *= clip_cov;

    if ((flags & ETCH_GLOBAL_MANUAL_SRGB) != 0u && out_pm.a > 1e-5f)
    {
        float4 straight_lin = saturate(out_pm / out_pm.a);
        out_pm.rgb = linear_to_srgb(straight_lin).rgb * out_pm.a;
    }

    return out_pm;
}
