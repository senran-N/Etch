// Etch Dedicated Image / Vibrancy BlurGlass / LayerBlur / PunchHole Shader (SM 6.0 HLSL -> DXIL, SPIR-V, MSL)
// Handles PrimKind::Texture, PrimKind::BlurGlass, PrimKind::LayerBlur, PrimKind::PunchHole with minimal VGPR footprint.

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

ETCH_COMBINED_SAMPLER(0) Texture2D<float4> g_tex0     : register(t0, space2);
ETCH_COMBINED_SAMPLER(0) SamplerState      g_smp0     : register(s0, space2);
ETCH_COMBINED_SAMPLER(1) Texture2D<float4> g_tex1     : register(t1, space2);
ETCH_COMBINED_SAMPLER(1) SamplerState      g_smp1     : register(s1, space2);
ETCH_COMBINED_SAMPLER(2) Texture2D<float4> g_tex2     : register(t2, space2);
ETCH_COMBINED_SAMPLER(2) SamplerState      g_smp2     : register(s2, space2);
ETCH_COMBINED_SAMPLER(3) Texture2D<float4> g_tex3     : register(t3, space2);
ETCH_COMBINED_SAMPLER(3) SamplerState      g_smp3     : register(s3, space2);
ETCH_COMBINED_SAMPLER(4) Texture2D<float4> g_tex4     : register(t4, space2);
ETCH_COMBINED_SAMPLER(4) SamplerState      g_smp4     : register(s4, space2);
ETCH_COMBINED_SAMPLER(5) Texture2D<float4> g_tex5     : register(t5, space2);
ETCH_COMBINED_SAMPLER(5) SamplerState      g_smp5     : register(s5, space2);
ETCH_COMBINED_SAMPLER(6) Texture2D<float4> g_tex6     : register(t6, space2);
ETCH_COMBINED_SAMPLER(6) SamplerState      g_smp6     : register(s6, space2);
ETCH_COMBINED_SAMPLER(7) Texture2D<float4> g_tex7     : register(t7, space2);
ETCH_COMBINED_SAMPLER(7) SamplerState      g_smp7     : register(s7, space2);
ETCH_COMBINED_SAMPLER(8) Texture2D<float4> g_tex8     : register(t8, space2);
ETCH_COMBINED_SAMPLER(8) SamplerState      g_smp8     : register(s8, space2);
ETCH_COMBINED_SAMPLER(9) Texture2D<float4> g_mask_tex : register(t9, space2);
ETCH_COMBINED_SAMPLER(9) SamplerState      g_mask_smp : register(s9, space2);

ETCH_FRAG_STORAGE(10) StructuredBuffer<ClipShapeGpu> g_clip_shapes : register(t10, space2);

struct VSOut
{
    float4 position        : SV_Position;
    float2 pixel_pos       : TEXCOORD0;
    nointerpolation float4 bounds       : TEXCOORD1;
    nointerpolation float4 clip         : TEXCOORD2;
    nointerpolation float4 radii        : TEXCOORD3;
    nointerpolation float4 color0       : TEXCOORD4;
    nointerpolation float4 border_color : TEXCOORD5;
    nointerpolation float4 uv_rect      : TEXCOORD6;
    nointerpolation float4 grad_params  : TEXCOORD7; // xy = text_clip (when Texture+TextClip), z = border_width, w = softness
    nointerpolation uint4  meta         : TEXCOORD8; // x = kind, y = flags, z = tex_slot, w = asuint(clip_r)
    nointerpolation uint2  extra        : TEXCOORD9; // x = clip_index, y = vib_packed (BlurGlass)
};

VSOut vs_main(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    uint prim_idx = (g_flags & 0x00FFFFFFu) + instance_id;
    Prim p = g_prims[prim_idx];

    uint header   = p.style.x;
    uint kind     = header & 0xFu;
    uint flags    = (header >> 4) & 0x3Fu;
    uint tex_slot = (header >> 10) & 0xFu;
    float clip_r  = (header & (1u << 18)) != 0u ? 0.0f : float((header >> 20) & 0xFFFu) * ((header & (1u << 19)) != 0u ? -0.25f : 0.25f);
    uint clip_idx = (header & (1u << 18)) != 0u ? ((header >> 20) | (((header >> 14) & 15u) << 12)) : 0u;

    float2 bw_soft = unpack_f16x2_custom(p.style.w);
    float2 r_tl_tr = unpack_f16x2_custom(p.payload.z);
    float2 r_br_bl = unpack_f16x2_custom(p.payload.w);

    float pad = 1.0f;
    if (kind == ETCH_PRIM_LAYER_BLUR)
    {
        pad = 0.0f;
    }
    else if (kind == ETCH_PRIM_PUNCH_HOLE)
    {
        pad = max(1.0f, bw_soft.y * 0.5f + 1.0f);
    }

    static const float2 kCorners[6] = {
        float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f),
        float2(0.0f, 0.0f), float2(1.0f, 1.0f), float2(0.0f, 1.0f)
    };
    float2 corner = kCorners[vertex_id % 6u];

    float2 q_min = max(p.bounds.xy - pad, p.clip.xy - 1.0f);
    float2 q_max = min(p.bounds.xy + p.bounds.zw + pad, p.clip.zw + 1.0f);
    q_max = max(q_max, q_min);
    float2 px = lerp(q_min, q_max, corner);

    float4 uv_rect = float4(0.0f, 0.0f, 1.0f, 1.0f);
    if (kind == ETCH_PRIM_TEXTURE)
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

    float4 gp = float4(0.0f, 0.0f, bw_soft.x, bw_soft.y);
    if (kind == ETCH_PRIM_TEXTURE && (flags & ETCH_FLAG_TEXT_CLIP) != 0u)
    {
        gp = float4(asfloat(p.style.z), asfloat(p.style.w), 0.0f, 1.0f);
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
    o.pixel_pos    = px;
    o.bounds       = p.bounds;
    o.clip         = p.clip;
    o.radii        = float4(r_tl_tr.x, r_tl_tr.y, r_br_bl.x, r_br_bl.y);
    o.color0       = unpack_rgba8_srgb_to_linear(p.style.y);
    o.border_color = unpack_rgba8_srgb_to_linear(p.style.z);
    o.uv_rect      = uv_rect;
    o.grad_params  = gp;
    o.meta         = uint4(kind, flags | (g_flags & ETCH_GLOBAL_MANUAL_SRGB), tex_slot, asuint(clip_r));
    o.extra        = uint2(clip_idx, p.payload.y);
    return o;
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

float4 sample_slot_grad(uint slot, float2 uv, float2 ddx_uv, float2 ddy_uv)
{
    if (slot == 1u) return g_tex1.SampleGrad(g_smp1, uv, ddx_uv, ddy_uv);
    if (slot == 2u) return g_tex2.SampleGrad(g_smp2, uv, ddx_uv, ddy_uv);
    if (slot == 3u) return g_tex3.SampleGrad(g_smp3, uv, ddx_uv, ddy_uv);
    if (slot == 4u) return g_tex4.SampleGrad(g_smp4, uv, ddx_uv, ddy_uv);
    if (slot == 5u) return g_tex5.SampleGrad(g_smp5, uv, ddx_uv, ddy_uv);
    if (slot == 6u) return g_tex6.SampleGrad(g_smp6, uv, ddx_uv, ddy_uv);
    if (slot == 7u) return g_tex7.SampleGrad(g_smp7, uv, ddx_uv, ddy_uv);
    if (slot == 8u) return g_tex8.SampleGrad(g_smp8, uv, ddx_uv, ddy_uv);
    return g_tex0.SampleLevel(g_smp0, uv, 0.0f);
}

float4 ps_main(VSOut input) : SV_Target0
{
    uint kind     = input.meta.x;
    uint flags    = input.meta.y;
    uint tex_slot = input.meta.z;

    float clip_cov = etch_eval_clip_chain(
        input.pixel_pos, input.clip, asfloat(input.meta.w),
        (flags & ETCH_FLAG_ROUNDED_CLIP) != 0u, input.extra.x,
        g_clip_shapes, g_mask_tex, g_mask_smp);
    if (kind == ETCH_PRIM_TEXTURE && (flags & ETCH_FLAG_TEXT_CLIP) != 0u)
    {
        clip_cov *= saturate(0.5f + min(input.pixel_pos.x - input.grad_params.x, input.grad_params.y - input.pixel_pos.x));
    }
    if (clip_cov <= 0.0f) return float4(0.0f, 0.0f, 0.0f, 0.0f);

    float2 size      = max(input.bounds.zw, float2(1e-4f, 1e-4f));
    float2 half_size = 0.5f * size;
    float2 center    = input.bounds.xy + half_size;
    float2 p_rel     = input.pixel_pos - center;

    float4 out_pm = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (kind == ETCH_PRIM_TEXTURE)
    {
        float2 norm_uv = clamp((input.pixel_pos - input.bounds.xy) / size, 0.0f, 1.0f);
        float2 uv      = lerp(input.uv_rect.xy, input.uv_rect.zw, norm_uv);
        float2 uv_span = input.uv_rect.zw - input.uv_rect.xy;
        float4 texel   = sample_slot_grad(tex_slot, uv, float2(uv_span.x / size.x, 0.0f), float2(0.0f, uv_span.y / size.y));

        float d         = etch_sd_rounded_box(p_rel, half_size, input.radii, (flags & ETCH_FLAG_LINEAR_GRAD) != 0u);
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
        uint   vib         = input.extra.y;
        float  noise_amp   = 0.0f;
        if ((vib >> 24) != 0u)
        {
            float sat    = float(vib & 0xFFu) * (2.5f / 255.0f);
            float bright = float((vib >> 8) & 0xFFu) * (2.0f / 255.0f);
            noise_amp    = float((vib >> 16) & 0xFFu) * (0.10f / 255.0f);
            if (blur_lin.a > 1e-4f)
            {
                float3 rgb   = blur_lin.rgb / blur_lin.a;
                float  luma  = dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
                rgb          = max(lerp(luma.xxx, rgb, sat) * bright, float3(0.0f, 0.0f, 0.0f));
                blur_lin.rgb = rgb * blur_lin.a;
            }
        }
        float4 tint_pm  = float4(input.color0.rgb * input.color0.a, input.color0.a);
        float4 glass_pm = tint_pm + blur_lin * (1.0f - input.color0.a);

        if (glass_pm.a > 1e-4f)
        {
            float4 straight_srgb = linear_to_srgb(float4(saturate(glass_pm.rgb / glass_pm.a), 1.0f));
            if (noise_amp > 0.0f)
            {
                float ign2        = frac(52.9829189f * frac(dot(input.pixel_pos + float2(17.0f, 31.0f), float2(0.06711056f, 0.00583715f))));
                straight_srgb.rgb = saturate(straight_srgb.rgb + (ign2 - 0.5f) * noise_amp);
            }
            float ign         = frac(52.9829189f * frac(dot(input.pixel_pos, float2(0.06711056f, 0.00583715f))));
            straight_srgb.rgb = saturate(straight_srgb.rgb + (ign - 0.5f) * (1.0f / 255.0f));
            glass_pm.rgb      = srgb_to_linear(straight_srgb).rgb * glass_pm.a;
        }

        float d         = etch_sd_rounded_box(p_rel, half_size, input.radii, (flags & ETCH_FLAG_ROUND_CAP) != 0u);
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
    else
    {
        // ETCH_PRIM_PUNCH_HOLE
        float d = etch_sd_rounded_box(p_rel, half_size, input.radii, (flags & ETCH_FLAG_ROUND_CAP) != 0u);
        float coverage = saturate(0.5f - d / max(input.grad_params.w, 0.5f)) * input.color0.a;
        out_pm = float4(0.0f, 0.0f, 0.0f, coverage);
    }

    return etch_finalize_manual_srgb(out_pm * clip_cov, flags);
}
