// Etch Dedicated Hardware 3-Channel LCD Subpixel Text Shader (SM 6.0 HLSL -> DXIL & SPIR-V)
// Hardware blend state:
//   src_color = CONSTANT_COLOR (linear text_color.rgb), dst_color = ONE_MINUS_SRC_COLOR (1 - alpha * cov.rgb)
//   src_alpha = ONE, dst_alpha = ONE_MINUS_SRC_ALPHA
// Achieves mathematically exact ClearType / LCD RGB subpixel compositing on D3D12, Vulkan & Metal in 1 pass.

#define ETCH_FLAG_ROUNDED_CLIP  (1u << 2)
#define ETCH_FLAG_STEM_DARKEN   (1u << 4)
#define ETCH_FLAG_TEXT_CLIP     (1u << 5)

#define ETCH_GLOBAL_GRAYSCALE_TEXT (1u << 28)
#define ETCH_GLOBAL_USE_DEPTH      (1u << 30)
#define ETCH_GLOBAL_MANUAL_SRGB    (1u << 31)

struct Prim
{
    float4 bounds;
    float4 clip;
    uint4  payload;
    uint4  style;
};

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

#ifdef __spirv__
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]]
#endif
Texture2D<float4> g_subpixel_atlas : register(t0, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]]
#endif
SamplerState g_subpixel_smp : register(s0, space2);

struct ClipShape
{
    float4 bounds;
    float4 radii;
    uint4  meta;
};

#ifdef __spirv__
[[vk::binding(1, 2)]]
#endif
StructuredBuffer<ClipShape> g_clip_shapes : register(t1, space2);

struct VSOut
{
    float4 position  : SV_Position;
    float2 pixel_pos : TEXCOORD0;
    float2 uv        : TEXCOORD1;
    nointerpolation float4 clip      : TEXCOORD2;
    nointerpolation float4 text_clip : TEXCOORD3; // x = min_x, y = max_x, z = text_alpha, w = srgb_luma
    nointerpolation uint4  meta      : TEXCOORD4; // x = flags, y = clip_index, z = asuint(clip_r), w = 0
    nointerpolation float4 text_color: TEXCOORD5; // linear rgb, straight a (for single-DC R8_UNORM grayscale text)
};

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
    if (exp_bits == 31u) return asfloat(sign_bit | 0x7F800000u | (mant_bits << 13));
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

float3 linear_to_srgb3(float3 c)
{
    return float3(linear_to_srgb_chan(c.r), linear_to_srgb_chan(c.g), linear_to_srgb_chan(c.b));
}

float sd_rounded_box(float2 p, float2 b, float4 r, bool continuous)
{
    float2 rr = (p.x > 0.0f) ? float2(r.y, r.z) : float2(r.x, r.w);
    float rad = (p.y > 0.0f) ? rr.y : rr.x;
    rad = min(rad, min(b.x, b.y));
    float2 q = abs(p) - b + rad;
    float2 m = max(q, 0.0f);
    if (continuous && rad > 0.5f && m.x > 0.0f && m.y > 0.0f)
    {
        float2 u = m / rad;
        float k = 2.8f;
        float s = pow(pow(u.x, k) + pow(u.y, k), 1.0f / k);
        return (s - 1.0f) * rad;
    }
    return min(max(q.x, q.y), 0.0f) + length(m) - rad;
}

VSOut vs_main(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    uint prim_idx = (g_flags & 0x00FFFFFFu) + instance_id;
    Prim p = g_prims[prim_idx];

    uint header   = p.style.x;
    uint flags    = (header >> 4) & 0x3Fu;
    float clip_r  = (header & (1u << 18)) != 0u ? 0.0f : float((header >> 20) & 0xFFFu) * ((header & (1u << 19)) != 0u ? -0.25f : 0.25f);
    uint clip_idx = (header & (1u << 18)) != 0u ? ((header >> 20) | (((header >> 14) & 15u) << 12)) : 0u;

    static const float2 kCorners[6] = {
        float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f),
        float2(0.0f, 0.0f), float2(1.0f, 1.0f), float2(0.0f, 1.0f)
    };
    float2 corner = kCorners[vertex_id % 6u];

    float2 bmin = p.bounds.xy;
    float2 bsize = max(p.bounds.zw, float2(1e-4f, 1e-4f));
    float2 q_min = max(bmin, p.clip.xy - 1.0f);
    float2 q_max = min(bmin + bsize, p.clip.zw + 1.0f);
    q_max = max(q_max, q_min);

    float2 px = lerp(q_min, q_max, corner);
    float2 norm_uv = saturate((px - bmin) / bsize);
    float2 uv0 = unpack_unorm16x2(p.payload.x);
    float2 uv1 = unpack_unorm16x2(p.payload.y);

    float4 col_srgb = float4(
        float(p.style.y & 0xFFu),
        float((p.style.y >> 8) & 0xFFu),
        float((p.style.y >> 16) & 0xFFu),
        float((p.style.y >> 24) & 0xFFu)
    ) * (1.0f / 255.0f);
    float3 col_lin = float3(
        srgb_to_linear_chan(col_srgb.r),
        srgb_to_linear_chan(col_srgb.g),
        srgb_to_linear_chan(col_srgb.b));
    float lin_luma = saturate(dot(col_lin, float3(0.2126f, 0.7152f, 0.0722f)));
    float srgb_luma = pow(lin_luma, 1.0f / 2.2f);

    float z = ((g_flags & ETCH_GLOBAL_USE_DEPTH) != 0u)
        ? (1.0f - float(g_prim_depths[prim_idx] + 1u) * (1.0f / 8388608.0f))
        : 0.0f;

    VSOut o;
    o.position = float4(
        px.x * g_inv_viewport_size.x * 2.0f - 1.0f,
        1.0f - px.y * g_inv_viewport_size.y * 2.0f,
        z,
        1.0f
    );
    o.pixel_pos  = px;
    o.uv         = lerp(uv0, uv1, norm_uv);
    o.clip       = p.clip;
    o.text_clip  = float4(asfloat(p.style.z), asfloat(p.style.w), col_srgb.a, srgb_luma);
    o.meta       = uint4(flags | (g_flags & (ETCH_GLOBAL_MANUAL_SRGB | ETCH_GLOBAL_GRAYSCALE_TEXT)), clip_idx, asuint(clip_r), 0u);
    o.text_color = float4(col_lin, col_srgb.a);
    return o;
}

float4 ps_main(VSOut input) : SV_Target0
{
    uint flags = input.meta.x;

    float clip_r = asfloat(input.meta.z);
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
    uint clip_index = input.meta.y;
    [loop] while (clip_index != 0u)
    {
        ClipShape shape = g_clip_shapes[clip_index];
        float2 half_shape = shape.bounds.zw * 0.5f;
        float2 relative = input.pixel_pos - shape.bounds.xy - half_shape;
        float distance = sd_rounded_box(relative, half_shape, shape.radii, shape.meta.y != 0u);
        clip_cov = min(clip_cov, saturate(0.5f - distance));
        float2 tb = unpack_f16x2_custom(shape.meta.z);
        float2 lr = unpack_f16x2_custom(shape.meta.w);
        if (tb.x > 0.0f) fade_cov *= smoothstep(0.0f, tb.x, input.pixel_pos.y - shape.bounds.y);
        if (tb.y > 0.0f) fade_cov *= smoothstep(0.0f, tb.y, shape.bounds.y + shape.bounds.w - input.pixel_pos.y);
        if (lr.x > 0.0f) fade_cov *= smoothstep(0.0f, lr.x, input.pixel_pos.x - shape.bounds.x);
        if (lr.y > 0.0f) fade_cov *= smoothstep(0.0f, lr.y, shape.bounds.x + shape.bounds.z - input.pixel_pos.x);
        clip_index = shape.meta.x;
    }
    clip_cov *= fade_cov;
    if ((flags & ETCH_FLAG_TEXT_CLIP) != 0u)
    {
        clip_cov *= saturate(0.5f + min(input.pixel_pos.x - input.text_clip.x, input.text_clip.y - input.pixel_pos.x));
    }
    if (clip_cov <= 0.0f) return float4(0.0f, 0.0f, 0.0f, 0.0f);

    float4 tex_sample = g_subpixel_atlas.Sample(g_subpixel_smp, input.uv);
    if ((flags & ETCH_GLOBAL_GRAYSCALE_TEXT) != 0u)
    {
        float cov = tex_sample.r;
        if ((flags & ETCH_FLAG_STEM_DARKEN) != 0u && cov > 0.0f)
        {
            float srgb_luma = input.text_clip.w;
            // Polarity-calibrated stem darkening: prevents thin/spindly dark-on-light text in linear blending
            // and gently trims light-on-dark halo bloat.
            float dark_factor = saturate(1.0f - 2.0f * srgb_luma);
            float light_trim = saturate(2.0f * srgb_luma - 1.0f) * 0.16f;
            cov = lerp(cov, pow(cov, 0.74f), dark_factor * 0.85f);
            cov = lerp(cov, cov * cov, light_trim);
        }
        float a = saturate(cov * input.text_color.a * clip_cov);
        float3 rgb = ((flags & ETCH_GLOBAL_MANUAL_SRGB) != 0u)
            ? linear_to_srgb3(saturate(input.text_color.rgb))
            : input.text_color.rgb;
        return float4(rgb * a, a);
    }

    float4 cov = tex_sample;
    if ((flags & ETCH_FLAG_STEM_DARKEN) != 0u)
    {
        float srgb_luma = input.text_clip.w;
        float dark_boost = saturate(1.0f - 2.0f * srgb_luma) * 0.36f;
        float light_trim = saturate(2.0f * srgb_luma - 1.0f) * 0.16f;
        cov = lerp(cov, cov * (2.0f - cov), dark_boost);
        cov = lerp(cov, cov * cov, light_trim);
    }

    float scale = input.text_clip.z * clip_cov;
    return saturate(cov * scale);
}
