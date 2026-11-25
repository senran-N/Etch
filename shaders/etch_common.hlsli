// Etch Shared HLSL Definitions & Helpers (included by etch_quad, etch_subpixel, etch_slug, etch_image, etch_fx).
#ifndef ETCH_COMMON_HLSLI
#define ETCH_COMMON_HLSLI

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
#define ETCH_FLAG_TEXT_CLIP     (1u << 5)

#define ETCH_GLOBAL_TEXT_ATLAS_R8  (1u << 27)
#define ETCH_GLOBAL_GRAYSCALE_TEXT (1u << 28)
#define ETCH_GLOBAL_OPAQUE_PASS    (1u << 29)
#define ETCH_GLOBAL_USE_DEPTH      (1u << 30)
#define ETCH_GLOBAL_MANUAL_SRGB    (1u << 31)

#ifdef __spirv__
#define ETCH_COMBINED_SAMPLER(slot) [[vk::combinedImageSampler]] [[vk::binding(slot, 2)]]
#define ETCH_FRAG_STORAGE(slot)     [[vk::binding(slot, 2)]]
#else
#define ETCH_COMBINED_SAMPLER(slot)
#define ETCH_FRAG_STORAGE(slot)
#endif

// Strictly 64 bytes matching etch::Prim in include/etch/etch.h.
struct Prim
{
    float4 bounds;   // xy = top-left px, zw = width, height px
    float4 clip;     // xy = clip min px, zw = clip max px
    uint4  payload;  // kind-specific payload
    uint4  style;    // x = header; y = color0 sRGB RGBA8; z = border/color_end; w = f16x2(border_width, softness/angle)
};

// Strictly 64 bytes matching etch::ClipShapeGpu in src/etch_arena.cpp.
struct ClipShapeGpu
{
    float4 bounds;      // (x, y, w, h)
    float4 radii;       // (tl, tr, br, bl)
    uint4  meta;        // x=parent_index; y: bit0=continuous, bits[1..3]=mask_kind (0=RoundedEdgeFade,1=LinearAlphaGrad,2=RadialSpotlight,3=TextureAlphaMask), bit4=invert; z=fade_tb_f16x2 or pack_f16x2(alpha_start,alpha_end); w=fade_lr_f16x2 or pack_f16x2(start_t,end_t)
    float4 mask_params; // Linear:(dir_x,dir_y,origin_dot,inv_len); Radial:(cx,cy,inv_rx,inv_ry); Texture:(u0,v0,u1,v1)
};

// Bitwise IEEE-754 binary16 -> float32 decode for portable SM 6.0 DXIL & Vulkan 1.0 SPIR-V.
float unpack_f16_bits(uint h)
{
    uint sign_bit = (h & 0x8000u) << 16;
    uint exp_bits = (h >> 10) & 0x1Fu;
    uint mant_bits = h & 0x03FFu;
    if (exp_bits == 0u)
    {
        if (mant_bits == 0u) return asfloat(sign_bit);
        float val = float(mant_bits) * (1.0f / 16777216.0f);
        return (sign_bit != 0u) ? -val : val;
    }
    if (exp_bits == 31u)
    {
        return asfloat(sign_bit | 0x7F800000u | (mant_bits << 13));
    }
    return asfloat(sign_bit | ((exp_bits + 112u) << 23) | (mant_bits << 13));
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

float linear_to_srgb_chan(float c)
{
    return (c <= 0.0031308f) ? (c * 12.92f) : (1.055f * pow(max(c, 0.0f), 1.0f / 2.4f) - 0.055f);
}

float4 srgb_to_linear(float4 c)
{
    return float4(srgb_to_linear_chan(c.r), srgb_to_linear_chan(c.g), srgb_to_linear_chan(c.b), c.a);
}

float3 linear_to_srgb3(float3 c)
{
    return float3(linear_to_srgb_chan(c.r), linear_to_srgb_chan(c.g), linear_to_srgb_chan(c.b));
}

float4 linear_to_srgb(float4 c)
{
    return float4(linear_to_srgb3(c.rgb), c.a);
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

float4 etch_finalize_manual_srgb(float4 out_pm, uint flags)
{
    if ((flags & ETCH_GLOBAL_MANUAL_SRGB) != 0u && out_pm.a > 1e-5f)
    {
        float3 straight_lin = saturate(out_pm.rgb / out_pm.a);
        out_pm.rgb = linear_to_srgb3(straight_lin) * out_pm.a;
    }
    return out_pm;
}

// Oklab matrices: Bjorn Ottosson's public-domain reference (see THIRD_PARTY_NOTICES.md).
float3 etch_rgb_to_oklab(float3 rgb)
{
    float3 lms = float3(
        dot(rgb, float3(0.4122214708f, 0.5363325363f, 0.0514459929f)),
        dot(rgb, float3(0.2119034982f, 0.6806995451f, 0.1073969566f)),
        dot(rgb, float3(0.0883024619f, 0.2817188376f, 0.6299787005f)));
    lms = pow(max(lms, float3(0.0f, 0.0f, 0.0f)), 1.0f / 3.0f);
    return float3(
        dot(lms, float3(0.2104542553f, 0.7936177850f, -0.0040720468f)),
        dot(lms, float3(1.9779984951f, -2.4285922050f, 0.4505937099f)),
        dot(lms, float3(0.0259040371f, 0.7827717662f, -0.8086757660f)));
}

float3 etch_oklab_to_rgb(float3 lab)
{
    float3 lms = float3(
        lab.x + 0.3963377774f * lab.y + 0.2158037573f * lab.z,
        lab.x - 0.1055613458f * lab.y - 0.0638541728f * lab.z,
        lab.x - 0.0894841775f * lab.y - 1.2914855480f * lab.z);
    lms = lms * lms * lms;
    return saturate(float3(
        dot(lms, float3(4.0767416621f, -3.3077115913f, 0.2309699292f)),
        dot(lms, float3(-1.2684380046f, 2.6097574011f, -0.3413193965f)),
        dot(lms, float3(-0.0041960863f, -0.7034186147f, 1.7076147010f))));
}

float erf_approx(float x)
{
    float ax = abs(x);
    float t = 1.0f / (1.0f + 0.3275911f * ax);
    float poly = (((((1.061405429f * t - 1.453152027f) * t + 1.421413741f) * t - 0.284496736f) * t + 0.254829592f) * t);
    float e = 1.0f - poly * exp(-ax * ax);
    return (x < 0.0f) ? -e : e;
}

float etch_sd_rounded_box(float2 p, float2 half_size, float4 radii, bool continuous)
{
    // radii: x = tl (-x,-y), y = tr (+x,-y), z = br (+x,+y), w = bl (-x,+y)
    float2 pair = (p.x > 0.0f) ? radii.zy : radii.wx;
    float r = (p.y > 0.0f) ? pair.x : pair.y;
    r = max(min(r, min(half_size.x, half_size.y)), 0.0f);
    float2 q = abs(p) - half_size + r;
    if (continuous && r > 0.0f)
    {
        float2 v = max(q, 0.0f);
        float n = sqrt(length(v * v));
        float gradient = (n > 1e-4f) ? (length(v * v * v) / (n * n * n)) : 1.0f;
        return (min(max(q.x, q.y), 0.0f) + n - r) / max(gradient, 1e-4f);
    }
    return min(max(q.x, q.y), 0.0f) + length(max(q, 0.0f)) - r;
}

float etch_eval_inset_shadow_cov(float2 p_local, float2 half_size, float4 radii, float2 offset, float spread, float inv_s2, bool continuous)
{
    float d_outer = etch_sd_rounded_box(p_local, half_size, radii, continuous);
    float outer_cov = saturate(0.5f - d_outer);
    float2 raw_inner_hs = half_size - spread;
    if (raw_inner_hs.x <= 0.0f || raw_inner_hs.y <= 0.0f)
    {
        return outer_cov;
    }
    float4 outer_r = min(radii, min(half_size.x, half_size.y).xxxx);
    float4 inner_r = float4(
        outer_r.x > 0.0f ? max(outer_r.x - spread, 0.0f) : 0.0f,
        outer_r.y > 0.0f ? max(outer_r.y - spread, 0.0f) : 0.0f,
        outer_r.z > 0.0f ? max(outer_r.z - spread, 0.0f) : 0.0f,
        outer_r.w > 0.0f ? max(outer_r.w - spread, 0.0f) : 0.0f);
    float d_inner = etch_sd_rounded_box(p_local - offset, raw_inner_hs, inner_r, continuous);
    if (inv_s2 < 0.0f)
    {
        return max(outer_cov - saturate(0.5f - d_inner), 0.0f);
    }
    return outer_cov * saturate(0.5f * (1.0f + erf_approx(d_inner * inv_s2)));
}

float etch_smooth_range(float x, float start_t, float end_t)
{
    float span = end_t - start_t;
    if (abs(span) > 1e-5f)
    {
        float s = saturate((x - start_t) / span);
        return s * s * (3.0f - 2.0f * s);
    }
    return (x >= start_t) ? 1.0f : 0.0f;
}

float etch_eval_clip_chain(
    float2 px,
    float4 clip_rect,
    float clip_r,
    bool rounded_clip,
    uint clip_index,
    StructuredBuffer<ClipShapeGpu> clip_shapes,
    Texture2D<float4> mask_tex,
    SamplerState mask_smp)
{
    float2 clip_half   = 0.5f * max(clip_rect.zw - clip_rect.xy, 0.0f);
    float2 clip_center = 0.5f * (clip_rect.xy + clip_rect.zw);
    float2 clip_rel    = px - clip_center;
    float clip_d;
    if (rounded_clip && clip_r != 0.0f)
    {
        clip_d = etch_sd_rounded_box(clip_rel, clip_half, abs(clip_r).xxxx, clip_r < 0.0f);
    }
    else
    {
        float2 cq = abs(clip_rel) - clip_half;
        clip_d = max(cq.x, cq.y);
    }
    float clip_cov = saturate(0.5f - clip_d);
    float fade_cov = 1.0f;
    [loop] while (clip_index != 0u)
    {
        ClipShapeGpu shape = clip_shapes[clip_index];
        uint meta_y = shape.meta.y;
        uint mask_kind = (meta_y >> 1u) & 0x7u;
        bool invert = (meta_y & (1u << 4u)) != 0u;
        if (mask_kind == 0u)
        {
            float2 half_shape = shape.bounds.zw * 0.5f;
            float2 relative = px - shape.bounds.xy - half_shape;
            float distance = etch_sd_rounded_box(relative, half_shape, shape.radii, (meta_y & 1u) != 0u);
            float shape_cov = saturate(0.5f - distance);
            if (invert) shape_cov = 1.0f - shape_cov;
            clip_cov = min(clip_cov, shape_cov);
            float2 tb = unpack_f16x2_custom(shape.meta.z);
            float2 lr = unpack_f16x2_custom(shape.meta.w);
            if (tb.x > 0.0f) fade_cov *= smoothstep(0.0f, tb.x, px.y - shape.bounds.y);
            if (tb.y > 0.0f) fade_cov *= smoothstep(0.0f, tb.y, shape.bounds.y + shape.bounds.w - px.y);
            if (lr.x > 0.0f) fade_cov *= smoothstep(0.0f, lr.x, px.x - shape.bounds.x);
            if (lr.y > 0.0f) fade_cov *= smoothstep(0.0f, lr.y, shape.bounds.x + shape.bounds.z - px.x);
        }
        else
        {
            float2 alphas = unpack_f16x2_custom(shape.meta.z);
            float2 range  = unpack_f16x2_custom(shape.meta.w);
            float4 mp     = shape.mask_params;
            float u = 0.0f;
            if (mask_kind == 1u)
            {
                float t = saturate((dot(px, mp.xy) - mp.z) * mp.w);
                u = etch_smooth_range(t, range.x, range.y);
            }
            else if (mask_kind == 2u)
            {
                float r = length((px - mp.xy) * mp.zw);
                u = etch_smooth_range(r, range.x, range.y);
            }
            else
            {
                float2 uv = lerp(mp.xy, mp.zw, saturate((px - shape.bounds.xy) / max(shape.bounds.zw, float2(1e-4f, 1e-4f))));
                u = 1.0f - mask_tex.SampleLevel(mask_smp, uv, 0.0f).a;
            }
            if (invert) u = 1.0f - u;
            fade_cov *= saturate(lerp(alphas.x, alphas.y, u));
        }
        clip_index = shape.meta.x;
    }
    return clip_cov * fade_cov;
}

#endif // ETCH_COMMON_HLSLI
