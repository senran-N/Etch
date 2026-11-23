// Etch Dedicated Special FX Shader (SM 6.0 HLSL -> DXIL, SPIR-V, MSL)
// Lazily instantiated only when a frame emits CanvasGrid, BezierWire, RingArc, MeshGrad, or BorderBeam.

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

ETCH_FRAG_STORAGE(1) StructuredBuffer<ClipShapeGpu> g_clip_shapes : register(t1, space2);

struct VSOut
{
    float4 position        : SV_Position;
    float2 pixel_pos       : TEXCOORD0;
    nointerpolation float4 bounds       : TEXCOORD1;
    nointerpolation float4 clip         : TEXCOORD2;
    nointerpolation float4 radii        : TEXCOORD3;
    nointerpolation float4 color0       : TEXCOORD4;
    nointerpolation float4 color1       : TEXCOORD5;
    nointerpolation float4 border_color : TEXCOORD6;
    nointerpolation float4 uv_rect      : TEXCOORD7;
    nointerpolation float4 grad_params  : TEXCOORD8;
    nointerpolation uint4  meta         : TEXCOORD9; // x = kind, y = flags, z = clip_index, w = asuint(clip_r)
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

    float2 bw_soft = unpack_f16x2_custom(p.style.w);
    float2 r_tl_tr = unpack_f16x2_custom(p.payload.z);
    float2 r_br_bl = unpack_f16x2_custom(p.payload.w);

    float pad = 0.0f;
    if (kind == ETCH_PRIM_MESH_GRAD)
    {
        pad = 1.0f;
    }
    else if (kind == ETCH_PRIM_BORDER_BEAM)
    {
        pad = max(max(unpack_f16x2_custom(p.payload.y).y, 1.0f) + 2.0f, bw_soft.y * 0.5f + 1.0f);
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

    float4 col1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 uv_rect = float4(0.0f, 0.0f, 1.0f, 1.0f);
    float4 gp = float4(0.0f, 1.0f, bw_soft.x, bw_soft.y);

    if (kind == ETCH_PRIM_CANVAS_GRID)
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
        col1    = unpack_rgba8_srgb_to_linear(p.payload.x);
        uv_rect = unpack_rgba8_srgb_to_linear(p.payload.y);
        gp      = float4(bw_soft, 0.0f, 0.0f);
    }
    else if (kind == ETCH_PRIM_BORDER_BEAM)
    {
        uv_rect = float4(unpack_f16x2_custom(p.payload.x), unpack_f16x2_custom(p.payload.y));
        gp      = float4(bw_soft, 0.0f, 0.0f);
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
    o.color1       = col1;
    o.border_color = unpack_rgba8_srgb_to_linear(p.style.z);
    o.uv_rect      = uv_rect;
    o.grad_params  = gp;
    o.meta         = uint4(kind, flags | (g_flags & ETCH_GLOBAL_MANUAL_SRGB), clip_idx, asuint(clip_r));
    return o;
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

float4 ps_main(VSOut input) : SV_Target0
{
    uint kind  = input.meta.x;
    uint flags = input.meta.y;

    float clip_cov = etch_eval_clip_chain(
        input.pixel_pos, input.clip, asfloat(input.meta.w),
        (flags & ETCH_FLAG_ROUNDED_CLIP) != 0u, input.meta.z,
        g_clip_shapes, g_mask_tex, g_mask_smp);
    if (clip_cov <= 0.0f) return float4(0.0f, 0.0f, 0.0f, 0.0f);

    float2 size      = max(input.bounds.zw, float2(1e-4f, 1e-4f));
    float2 half_size = 0.5f * size;
    float2 center    = input.bounds.xy + half_size;
    float2 p_rel     = input.pixel_pos - center;

    float4 out_pm = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (kind == ETCH_PRIM_BORDER_BEAM)
    {
        float d        = etch_sd_rounded_box(p_rel, half_size, input.radii, (flags & ETCH_FLAG_ROUND_CAP) != 0u);
        float width    = input.uv_rect.z;
        float softness = max(input.grad_params.y, 0.5f);
        float outer    = saturate(0.5f - d / softness);
        float inner    = saturate(0.5f - (d + width) / softness);
        float ring     = max(outer - inner, 0.0f);
        float turn     = (atan2(p_rel.y / half_size.y, p_rel.x / half_size.x) - input.uv_rect.x) / 6.283185307f;
        float tail     = frac(-turn);
        if ((flags & 1u) != 0u) tail = min(tail, frac(0.5f - turn));
        float beam     = saturate(1.0f - tail / max(input.uv_rect.y, 0.001f));
        beam           = beam * beam * (3.0f - 2.0f * beam);
        float glow_t   = input.uv_rect.w > 0.0f ? saturate(1.0f - abs(d + 0.5f * width) / input.uv_rect.w) : 0.0f;
        float glow     = glow_t * glow_t * glow_t * 0.5f * (1.0f - ring);
        out_pm         = float4(input.border_color.rgb, 1.0f) * (input.border_color.a * ring * (1.0f - beam))
                       + float4(input.color0.rgb, 1.0f) * (input.color0.a * beam * (ring + glow));
        if ((flags & 2u) != 0u)
        {
            float rim = (saturate(0.5f - d) - saturate(0.5f - d - 1.0f)) * input.grad_params.x * saturate(-p_rel.y / half_size.y);
            out_pm = max(out_pm, float4(rim.xxx, rim));
        }
    }
    else if (kind == ETCH_PRIM_MESH_GRAD)
    {
        float2 uv = saturate((p_rel + half_size) / size);
        if (input.grad_params.y != 0.0f) uv = saturate(uv + 0.12f * sin(uv.yx * 3.14159265f + input.grad_params.y));
        float4 tl = input.color0, tr = input.border_color, br = input.color1, bl = input.uv_rect;
        bool lab = (flags & 1u) != 0u;
        if (lab)
        {
            tl.rgb = etch_rgb_to_oklab(tl.rgb);
            tr.rgb = etch_rgb_to_oklab(tr.rgb);
            br.rgb = etch_rgb_to_oklab(br.rgb);
            bl.rgb = etch_rgb_to_oklab(bl.rgb);
        }
        float4 col = lerp(lerp(tl, tr, uv.x), lerp(bl, br, uv.x), uv.y);
        if (lab) col.rgb = etch_oklab_to_rgb(col.rgb);
        if (input.grad_params.x > 0.0f)
        {
            float ign = frac(52.9829189f * frac(dot(input.pixel_pos, float2(0.06711056f, 0.00583715f))));
            float4 encoded = linear_to_srgb(col);
            encoded.rgb = saturate(encoded.rgb + (ign - 0.5f) * input.grad_params.x);
            col = srgb_to_linear(encoded);
        }
        float d = etch_sd_rounded_box(p_rel, half_size, input.radii, (flags & ETCH_FLAG_ROUND_CAP) != 0u);
        out_pm = float4(col.rgb, 1.0f) * (col.a * saturate(0.5f - d));
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
    else
    {
        // ETCH_PRIM_RING_ARC
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
                    if ((flags & ETCH_FLAG_ROUND_CAP) == 0u)
                    {
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

    return etch_finalize_manual_srgb(out_pm * clip_cov, flags);
}
