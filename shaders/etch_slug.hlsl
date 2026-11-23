// Etch Dedicated Slug GPU Quadratic Bezier Shader (SM 6.0 HLSL -> DXIL, SPIR-V, MSL)
// Handles PrimKind::SlugGlyph and PrimKind::SlugFill cleanly isolated from image/FX VGPR pressure.

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

ETCH_COMBINED_SAMPLER(0) Texture2D<float4> g_mask_tex : register(t0, space2);
ETCH_COMBINED_SAMPLER(0) SamplerState      g_mask_smp : register(s0, space2);

ETCH_FRAG_STORAGE(1) StructuredBuffer<ClipShapeGpu> g_clip_shapes     : register(t1, space2);
ETCH_FRAG_STORAGE(2) StructuredBuffer<uint4>        g_slug_curves     : register(t2, space2);
ETCH_FRAG_STORAGE(3) StructuredBuffer<uint2>        g_slug_bands      : register(t3, space2);
ETCH_FRAG_STORAGE(4) StructuredBuffer<uint4>        g_dyn_slug_curves : register(t4, space2);
ETCH_FRAG_STORAGE(5) StructuredBuffer<uint2>        g_dyn_slug_bands  : register(t5, space2);

struct VSOut
{
    float4 position        : SV_Position;
    float2 pixel_pos       : TEXCOORD0;
    nointerpolation float4 bounds      : TEXCOORD1;
    nointerpolation float4 clip        : TEXCOORD2;
    nointerpolation float4 color0      : TEXCOORD3;
    nointerpolation float4 color1      : TEXCOORD4;
    nointerpolation float4 grad_params : TEXCOORD5; // SlugFill: grad_dir; SlugGlyph: text_clip (x,y) or wipe (x0_rel,x1_rel)
    nointerpolation uint4  meta        : TEXCOORD6; // x = kind, y = flags, z = clip_index, w = asuint(clip_r)
    nointerpolation float2 rotation    : TEXCOORD7;
    nointerpolation uint2  slug_meta   : TEXCOORD8; // x = band_offset, y = num_hbands | (num_vbands << 16)
};

VSOut vs_main(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    uint prim_idx = (g_flags & 0x00FFFFFFu) + instance_id;
    Prim p = g_prims[prim_idx];

    uint header   = p.style.x;
    uint kind     = header & 0xFu;
    uint flags    = (header >> 4) & 0x3Fu;
    float clip_r  = (header & (1u << 18)) != 0u ? 0.0f : float((header >> 20) & 0xFFFu) * ((header & (1u << 19)) != 0u ? -0.25f : 0.25f);
    uint clip_idx = (header & (1u << 18)) != 0u ? ((header >> 20) | (((header >> 14) & 15u) << 12)) : 0u;

    static const float2 kCorners[6] = {
        float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f),
        float2(0.0f, 0.0f), float2(1.0f, 1.0f), float2(0.0f, 1.0f)
    };
    float2 corner = kCorners[vertex_id % 6u];

    float pad = 1.0f;
    float2 q_min = p.bounds.xy - pad;
    float2 q_max = p.bounds.xy + p.bounds.zw + pad;

    float angle = (kind == ETCH_PRIM_SLUG_GLYPH) ? asfloat(p.payload.z) : 0.0f;
    float c = cos(angle), sn = sin(angle);
    if (angle != 0.0f)
    {
        float2 hs = p.bounds.zw * 0.5f;
        float2 extent = float2(abs(c) * hs.x + abs(sn) * hs.y, abs(sn) * hs.x + abs(c) * hs.y) + pad;
        float2 center = p.bounds.xy + hs;
        q_min = center - extent;
        q_max = center + extent;
    }

    q_min = max(q_min, p.clip.xy - 1.0f);
    q_max = min(q_max, p.clip.zw + 1.0f);
    q_max = max(q_max, q_min);

    float2 px = lerp(q_min, q_max, corner);

    float4 col1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 gp = float4(0.0f, 1.0f, 0.0f, 0.0f);
    if (kind == ETCH_PRIM_SLUG_GLYPH)
    {
        if ((flags & ETCH_FLAG_TEXT_CLIP) != 0u)
        {
            gp = float4(asfloat(p.style.z), asfloat(p.style.w), 0.0f, 0.0f);
        }
        else if ((flags & ETCH_FLAG_LINEAR_GRAD) != 0u)
        {
            col1 = unpack_rgba8_srgb_to_linear(p.style.z);
            gp = float4(unpack_f16x2_custom(p.payload.w), 0.0f, 0.0f);
        }
    }
    else if ((flags & (ETCH_FLAG_LINEAR_GRAD | ETCH_FLAG_RADIAL_GRAD)) != 0u)
    {
        col1 = unpack_rgba8_srgb_to_linear(p.payload.z);
        gp = float4(unpack_f16x2_custom(p.payload.w), 0.0f, 0.0f);
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
    o.pixel_pos   = px;
    o.bounds      = p.bounds;
    o.clip        = p.clip;
    o.color0      = unpack_rgba8_srgb_to_linear(p.style.y);
    o.color1      = col1;
    o.grad_params = gp;
    o.meta        = uint4(kind, flags | (g_flags & ETCH_GLOBAL_MANUAL_SRGB), clip_idx, asuint(clip_r));
    o.rotation    = float2(c, sn);
    o.slug_meta   = uint2(p.payload.x, p.payload.y);
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

float slug_render_shared(
    float2 render_coord,
    float2 pixels_per_unit,
    uint band_offset,
    uint packed_band_counts,
    StructuredBuffer<uint4> curves,
    StructuredBuffer<uint2> bands)
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

float4 ps_main(VSOut input) : SV_Target0
{
    uint kind  = input.meta.x;
    uint flags = input.meta.y;

    float clip_cov = etch_eval_clip_chain(
        input.pixel_pos, input.clip, asfloat(input.meta.w),
        (flags & ETCH_FLAG_ROUNDED_CLIP) != 0u, input.meta.z,
        g_clip_shapes, g_mask_tex, g_mask_smp);

    float2 size      = max(input.bounds.zw, float2(1e-4f, 1e-4f));
    float2 half_size = 0.5f * size;
    float2 center    = input.bounds.xy + half_size;
    float2 p_rel     = input.pixel_pos - center;

    if (kind == ETCH_PRIM_SLUG_GLYPH && (flags & ETCH_FLAG_TEXT_CLIP) != 0u)
    {
        float text_x = center.x + dot(input.rotation, p_rel);
        clip_cov *= saturate(0.5f + min(text_x - input.grad_params.x, input.grad_params.y - text_x));
    }
    if (clip_cov <= 0.0f) return float4(0.0f, 0.0f, 0.0f, 0.0f);

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

    float4 out_pm = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (kind == ETCH_PRIM_SLUG_GLYPH)
    {
        float2 p_local = float2(
            input.rotation.x * p_rel.x + input.rotation.y * p_rel.y,
            -input.rotation.y * p_rel.x + input.rotation.x * p_rel.y);
        float2 render_coord = float2(
            (p_local.x + half_size.x) / size.x,
            1.0f - (p_local.y + half_size.y) / size.y);
        float cov = 0.0f;
        [loop] for (uint s = 0u; s < taps; ++s)
        {
            float2 tc = ssaa ? (render_coord + kRgss[s] * inv_sz) : render_coord;
            cov += slug_render_shared(tc, ppu, input.slug_meta.x, input.slug_meta.y, g_slug_curves, g_slug_bands);
        }
        cov *= ssaa ? 0.25f : 1.0f;
        if (any(input.rotation != float2(1.0f, 0.0f)))
        {
            cov *= saturate(0.5f - max(abs(p_local.x) - half_size.x, abs(p_local.y) - half_size.y));
        }
        float4 glyph_col = input.color0;
        if ((flags & (ETCH_FLAG_LINEAR_GRAD | ETCH_FLAG_TEXT_CLIP)) == ETCH_FLAG_LINEAR_GRAD)
        {
            float rel_x = input.pixel_pos.x - input.bounds.x;
            float x0_rel = input.grad_params.x;
            float x1_rel = max(input.grad_params.y, x0_rel + 0.5f);
            glyph_col = lerp(input.color0, input.color1, smoothstep(x0_rel, x1_rel, rel_x));
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
    else
    {
        float2 uv = (p_rel + half_size) / size;
        float2 rc = float2(uv.x, 1.0f - uv.y);
        float cov = 0.0f;
        [loop] for (uint s = 0u; s < taps; ++s)
        {
            float2 tc = ssaa ? (rc + kRgss[s] * inv_sz) : rc;
            cov += slug_render_shared(tc, ppu, input.slug_meta.x, input.slug_meta.y, g_dyn_slug_curves, g_dyn_slug_bands);
        }
        cov *= ssaa ? 0.25f : 1.0f;

        float4 fill_col = input.color0;
        bool has_grad = false;
        if ((flags & ETCH_FLAG_LINEAR_GRAD) != 0u)
        {
            float2 dir = input.grad_params.xy;
            float dir_len = length(dir);
            dir = (dir_len > 1e-4f) ? (dir / dir_len) : float2(0.0f, 1.0f);
            float span = max(0.5f * (abs(dir.x) + abs(dir.y)), 1e-4f);
            float t = saturate(0.5f + dot(uv - 0.5f, dir) / (2.0f * span));
            fill_col = lerp(input.color0, input.color1, t);
            has_grad = true;
        }
        else if ((flags & ETCH_FLAG_RADIAL_GRAD) != 0u)
        {
            float t = saturate(length((uv - 0.5f) * 2.0f));
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
        out_pm = float4(fill_col.rgb, 1.0f) * (fill_col.a * cov);
    }

    return etch_finalize_manual_srgb(out_pm * clip_cov, flags);
}
