// Etch Dedicated Hinted Glyph Shader (SM 6.0 HLSL -> DXIL, SPIR-V, MSL)
// Handles 100% of PrimKind::Glyph:
//   - Single-DC R8_UNORM 4x X-phase grayscale hinted text (all colors, text wipe, all BlendModes & AlphaMasks)
//   - Opt-in 3-channel ClearType RGB LCD subpixel text (CONSTANT_COLOR + ONE_MINUS_SRC_COLOR) + RGBA .a fallback.

#include "etch_common.hlsli"

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

ETCH_COMBINED_SAMPLER(0) Texture2D<float4> g_subpixel_atlas : register(t0, space2);
ETCH_COMBINED_SAMPLER(0) SamplerState      g_subpixel_smp   : register(s0, space2);

ETCH_COMBINED_SAMPLER(1) Texture2D<float4> g_mask_tex       : register(t1, space2);
ETCH_COMBINED_SAMPLER(1) SamplerState      g_mask_smp       : register(s1, space2);

ETCH_FRAG_STORAGE(2) StructuredBuffer<ClipShapeGpu> g_clip_shapes : register(t2, space2);

struct VSOut
{
    float4 position  : SV_Position;
    float2 pixel_pos : TEXCOORD0;
    float2 uv        : TEXCOORD1;
    nointerpolation float4 clip       : TEXCOORD2;
    nointerpolation float4 text_clip  : TEXCOORD3; // x = min_x, y = max_x, z = text_alpha, w = srgb_luma
    nointerpolation uint4  meta       : TEXCOORD4; // x = flags, y = clip_index, z = asuint(clip_r), w = 0
    nointerpolation float4 text_color : TEXCOORD5; // linear rgb, straight a
    nointerpolation float4 wipe_color : TEXCOORD6; // linear rgb, straight a (text wipe end color)
    nointerpolation float4 wipe_range : TEXCOORD7; // x = bounds.x, y = x0_rel, z = x1_rel, w = 0
};

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

    float4 col_lin = unpack_rgba8_srgb_to_linear(p.style.y);
    float lin_luma = saturate(dot(col_lin.rgb, float3(0.2126f, 0.7152f, 0.0722f)));
    float srgb_luma = pow(lin_luma, 1.0f / 2.2f);

    float4 wipe_col = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float2 wipe_x = float2(0.0f, 0.0f);
    if ((flags & (ETCH_FLAG_LINEAR_GRAD | ETCH_FLAG_TEXT_CLIP)) == ETCH_FLAG_LINEAR_GRAD)
    {
        wipe_col = unpack_rgba8_srgb_to_linear(p.style.z);
        wipe_x = unpack_f16x2_custom(p.payload.w);
    }

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
    o.text_clip  = float4(asfloat(p.style.z), asfloat(p.style.w), col_lin.a, srgb_luma);
    o.meta       = uint4(flags | (g_flags & (ETCH_GLOBAL_MANUAL_SRGB | ETCH_GLOBAL_GRAYSCALE_TEXT | ETCH_GLOBAL_TEXT_ATLAS_R8)), clip_idx, asuint(clip_r), 0u);
    o.text_color = col_lin;
    o.wipe_color = wipe_col;
    o.wipe_range = float4(bmin.x, wipe_x.x, wipe_x.y, 0.0f);
    return o;
}

float4 ps_main(VSOut input) : SV_Target0
{
    uint flags = input.meta.x;

    float clip_cov = etch_eval_clip_chain(
        input.pixel_pos, input.clip, asfloat(input.meta.z),
        (flags & ETCH_FLAG_ROUNDED_CLIP) != 0u, input.meta.y,
        g_clip_shapes, g_mask_tex, g_mask_smp);
    if ((flags & ETCH_FLAG_TEXT_CLIP) != 0u)
    {
        clip_cov *= saturate(0.5f + min(input.pixel_pos.x - input.text_clip.x, input.text_clip.y - input.pixel_pos.x));
    }
    if (clip_cov <= 0.0f) return float4(0.0f, 0.0f, 0.0f, 0.0f);

    float4 tex_sample = g_subpixel_atlas.SampleLevel(g_subpixel_smp, input.uv, 0.0f);
    if ((flags & ETCH_GLOBAL_GRAYSCALE_TEXT) != 0u)
    {
        float cov = ((flags & ETCH_GLOBAL_TEXT_ATLAS_R8) != 0u) ? tex_sample.r : tex_sample.a;
        float4 glyph_col = input.text_color;
        float srgb_luma = input.text_clip.w;
        if ((flags & (ETCH_FLAG_LINEAR_GRAD | ETCH_FLAG_TEXT_CLIP)) == ETCH_FLAG_LINEAR_GRAD)
        {
            float rel_x = input.pixel_pos.x - input.wipe_range.x;
            float x0_rel = input.wipe_range.y;
            float x1_rel = max(input.wipe_range.z, x0_rel + 0.5f);
            glyph_col = lerp(input.text_color, input.wipe_color, smoothstep(x0_rel, x1_rel, rel_x));
            float lin_luma = saturate(dot(glyph_col.rgb, float3(0.2126f, 0.7152f, 0.0722f)));
            srgb_luma = pow(lin_luma, 1.0f / 2.2f);
        }
        if ((flags & ETCH_FLAG_STEM_DARKEN) != 0u && cov > 0.0f)
        {
            // Polarity-calibrated stem darkening: prevents thin/spindly dark-on-light text in linear blending
            // and gently trims light-on-dark halo bloat.
            float dark_factor = saturate(1.0f - 2.0f * srgb_luma);
            float light_trim = saturate(2.0f * srgb_luma - 1.0f) * 0.16f;
            cov = lerp(cov, pow(cov, 0.74f), dark_factor * 0.85f);
            cov = lerp(cov, cov * cov, light_trim);
        }
        float a = saturate(cov * glyph_col.a * clip_cov);
        float3 rgb = ((flags & ETCH_GLOBAL_MANUAL_SRGB) != 0u)
            ? linear_to_srgb3(saturate(glyph_col.rgb))
            : glyph_col.rgb;
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
