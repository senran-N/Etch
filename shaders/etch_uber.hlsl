// Etch Pure-GPU Uber Shader (SM 6.0 HLSL -> DXIL & Vulkan 1.0 SPIR-V)
// Compiled offline via dxc.exe to src/shaders_gen/etch_uber_{vs,ps}_{dxil,spv}.h.

#define ETCH_PRIM_RECT       0u
#define ETCH_PRIM_SHADOW     1u
#define ETCH_PRIM_GLYPH      2u
#define ETCH_PRIM_TEXTURE    3u
#define ETCH_PRIM_SLUG_GLYPH 4u
#define ETCH_PRIM_BLUR_GLASS 5u

#define ETCH_FLAG_LINEAR_GRAD   (1u << 0)
#define ETCH_FLAG_RADIAL_GRAD   (1u << 1)
#define ETCH_FLAG_ROUNDED_CLIP  (1u << 2)
#define ETCH_FLAG_PREMULTIPLIED (1u << 3)
#define ETCH_FLAG_STEM_DARKEN   (1u << 4)

#define ETCH_GLOBAL_MANUAL_SRGB (1u << 31)

// Strictly 64 bytes matching etch::Prim in src/etch.h (zero float3 members).
struct Prim
{
    float4 bounds;   // xy = top-left px, zw = width, height px
    float4 clip;     // xy = clip min px, zw = clip max px
    uint4  payload;  // xy = uv0/uv1 (UNORM16x2) OR (color1 RGBA8, f16x2 grad_dir); zw = f16x2 radii
    uint4  style;    // x = header [kind:4 | flags:6 | tex_slot:10 | clip_r_q2:12];
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
//   DXIL:  t0..t3, space2 + s0..s3, space2
//   SPIRV: set 2 binding 0..3 combined image samplers
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

struct VSOut
{
    float4 position        : SV_Position;
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

float3 srgb_to_linear(float3 c)
{
    return float3(srgb_to_linear_chan(c.r), srgb_to_linear_chan(c.g), srgb_to_linear_chan(c.b));
}

float linear_to_srgb_chan(float c)
{
    return (c <= 0.0031308f) ? (c * 12.92f) : (1.055f * pow(max(c, 0.0f), 1.0f / 2.4f) - 0.055f);
}

float3 linear_to_srgb(float3 c)
{
    return float3(linear_to_srgb_chan(c.r), linear_to_srgb_chan(c.g), linear_to_srgb_chan(c.b));
}

float4 unpack_rgba8_srgb_to_linear(uint packed)
{
    float4 c = float4(
        float(packed & 0xFFu),
        float((packed >> 8) & 0xFFu),
        float((packed >> 16) & 0xFFu),
        float((packed >> 24) & 0xFFu)
    ) * (1.0f / 255.0f);
    return float4(srgb_to_linear(c.rgb), c.a);
}

VSOut vs_main(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    uint prim_base = g_flags & 0x00FFFFFFu;
    Prim p = g_prims[prim_base + instance_id];

    uint header      = p.style.x;
    uint kind        = header & 0xFu;
    uint flags       = (header >> 4) & 0x3Fu;
    uint tex_slot    = (header >> 10) & 0x3FFu;
    float clip_r     = float((header >> 20) & 0xFFFu) * 0.25f;

    float2 bw_soft   = unpack_f16x2_custom(p.style.w);
    float2 r_tl_tr   = unpack_f16x2_custom(p.payload.z);
    float2 r_br_bl   = unpack_f16x2_custom(p.payload.w);

    float pad = 1.0f;
    if (kind == ETCH_PRIM_GLYPH)
    {
        // Keep glyph quad strictly inside glyph texel bounds so linear sampling never leaks adjacent atlas glyphs.
        pad = 0.0f;
    }
    else if (kind == ETCH_PRIM_SHADOW)
    {
        pad = max(bw_soft.y, 1.0f) * 2.0f + 2.0f;
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
    else if ((flags & (ETCH_FLAG_LINEAR_GRAD | ETCH_FLAG_RADIAL_GRAD)) != 0u)
    {
        col1 = unpack_rgba8_srgb_to_linear(p.payload.x);
        grad_dir = unpack_f16x2_custom(p.payload.y);
    }

    VSOut o;
    o.position = float4(
        px.x * g_inv_viewport_size.x * 2.0f - 1.0f,
        1.0f - px.y * g_inv_viewport_size.y * 2.0f,
        0.0f,
        1.0f
    );
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
    return o;
}

float sd_rounded_box(float2 p, float2 half_size, float4 radii)
{
    // radii: x = tl (-x,-y), y = tr (+x,-y), z = br (+x,+y), w = bl (-x,+y)
    float2 pair = (p.x > 0.0f) ? radii.zy : radii.wx;
    float r = (p.y > 0.0f) ? pair.x : pair.y;
    r = max(min(r, min(half_size.x, half_size.y)), 0.0f);
    float2 q = abs(p) - half_size + r;
    return min(max(q.x, q.y), 0.0f) + length(max(q, 0.0f)) - r;
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
    return g_tex0.SampleLevel(g_smp0, uv, 0.0f);
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
    if ((flags & ETCH_FLAG_ROUNDED_CLIP) != 0u && clip_r > 0.0f)
    {
        clip_d = sd_rounded_box(clip_rel, clip_half, clip_r.xxxx);
    }
    else
    {
        float2 cq = abs(clip_rel) - clip_half;
        clip_d = max(cq.x, cq.y);
    }
    float clip_cov = saturate(0.5f - clip_d);
    if (clip_cov <= 0.0f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float2 size      = max(input.bounds.zw, float2(1e-4f, 1e-4f));
    float2 half_size = 0.5f * size;
    float2 center    = input.bounds.xy + half_size;
    float2 p_rel     = input.pixel_pos - center;

    float4 out_pm = float4(0.0f, 0.0f, 0.0f, 0.0f);

    if (kind == ETCH_PRIM_RECT)
    {
        float d = sd_rounded_box(p_rel, half_size, input.radii);

        float4 fill_col = input.color0;
        if ((flags & ETCH_FLAG_LINEAR_GRAD) != 0u)
        {
            float2 norm_uv = (input.pixel_pos - input.bounds.xy) / size;
            float2 dir = input.grad_params.xy;
            float dir_len = length(dir);
            dir = (dir_len > 1e-4f) ? (dir / dir_len) : float2(0.0f, 1.0f);
            float span = max(0.5f * (abs(dir.x) + abs(dir.y)), 1e-4f);
            float t = saturate(0.5f + dot(norm_uv - 0.5f, dir) / (2.0f * span));
            fill_col = lerp(input.color0, input.color1, t);
        }
        else if ((flags & ETCH_FLAG_RADIAL_GRAD) != 0u)
        {
            float2 norm_uv = (input.pixel_pos - input.bounds.xy) / size;
            float t = saturate(length((norm_uv - 0.5f) * 2.0f));
            fill_col = lerp(input.color0, input.color1, t);
        }

        float border_w  = input.grad_params.z;
        float softness  = input.grad_params.w;
        float inv_aa    = 1.0f / max(softness, 0.5f);
        float outer_cov = saturate(0.5f - d * inv_aa);
        float inner_cov = (border_w > 0.0f) ? saturate(0.5f - (d + border_w)) : 1.0f;
        float border_t  = 1.0f - inner_cov;

        float4 b_pm = float4(input.border_color.rgb, 1.0f) * (input.border_color.a * border_t);
        float4 f_pm = float4(fill_col.rgb, 1.0f) * fill_col.a;
        out_pm      = (b_pm + f_pm * (1.0f - b_pm.a)) * outer_cov;
    }
    else if (kind == ETCH_PRIM_SHADOW)
    {
        float blur       = max(input.grad_params.w, 1.0f);
        float sigma      = max(blur * 0.5f, 0.5f);
        float inv_s2     = 1.0f / max(sigma * 1.41421356f, 0.5f);
        float d          = sd_rounded_box(p_rel, half_size, input.radii);
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
        if ((flags & ETCH_FLAG_STEM_DARKEN) != 0u)
        {
            float luma = saturate(dot(input.color0.rgb, float3(0.2126f, 0.7152f, 0.0722f)));
            cov = lerp(cov, cov * (2.0f - cov), 0.35f * luma);
        }
        out_pm = float4(input.color0.rgb, 1.0f) * (input.color0.a * cov);
    }
    else if (kind == ETCH_PRIM_TEXTURE)
    {
        float2 norm_uv = clamp((input.pixel_pos - input.bounds.xy) / size, 0.0f, 1.0f);
        float2 uv      = lerp(input.uv_rect.xy, input.uv_rect.zw, norm_uv);
        float4 texel   = sample_slot(tex_slot, uv);

        float d         = sd_rounded_box(p_rel, half_size, input.radii);
        float border_w  = input.grad_params.z;
        float softness  = input.grad_params.w;
        float inv_aa    = 1.0f / max(softness, 0.5f);
        float outer_cov = saturate(0.5f - d * inv_aa);
        float inner_cov = (border_w > 0.0f) ? saturate(0.5f - (d + border_w)) : 1.0f;
        float border_t  = 1.0f - inner_cov;

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
        out_pm = (b_pm + tex_pm * (1.0f - b_pm.a)) * outer_cov;
    }

    out_pm *= clip_cov;

    if ((flags & ETCH_GLOBAL_MANUAL_SRGB) != 0u && out_pm.a > 1e-5f)
    {
        float3 straight_lin = saturate(out_pm.rgb / out_pm.a);
        out_pm.rgb = linear_to_srgb(straight_lin) * out_pm.a;
    }

    return out_pm;
}
