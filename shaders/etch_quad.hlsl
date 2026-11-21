// Etch Dedicated Lightweight Quad / Border / Gradient / Shadow Shader (SM 6.0 HLSL -> DXIL & SPIR-V)
// Zero texture samplers, zero Slug storage buffers -> minimal VGPR pressure + Early-Z depth support.

#define ETCH_PRIM_RECT        0u
#define ETCH_PRIM_SHADOW      1u
#define ETCH_PRIM_ROT_RECT    10u

#define ETCH_FLAG_LINEAR_GRAD   (1u << 0)
#define ETCH_FLAG_RADIAL_GRAD   (1u << 1)
#define ETCH_FLAG_ROUNDED_CLIP  (1u << 2)
#define ETCH_FLAG_ROUND_CAP     (1u << 5)

#define ETCH_GLOBAL_OPAQUE_PASS (1u << 29)
#define ETCH_GLOBAL_USE_DEPTH   (1u << 30)
#define ETCH_GLOBAL_MANUAL_SRGB (1u << 31)

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

struct ClipShape
{
    float4 bounds;
    float4 radii;
    uint4  meta;
};

#ifdef __spirv__
[[vk::binding(0, 2)]]
#endif
StructuredBuffer<ClipShape> g_clip_shapes : register(t0, space2);

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
    nointerpolation float4 grad_params  : TEXCOORD7; // xy = grad_dir, z = border_width, w = softness
    nointerpolation uint4  meta         : TEXCOORD8; // x = kind, y = flags, z = clip_index, w = asuint(clip_r)
    nointerpolation float2 rotation     : TEXCOORD9;
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

float erf_approx(float x)
{
    float s = sign(x);
    float a = abs(x);
    float t = 1.0f / (1.0f + 0.3275911f * a);
    float poly = t * (0.254829592f + t * (-0.284496736f + t * (1.421413741f + t * (-1.453152027f + t * 1.061405429f))));
    return s * (1.0f - poly * exp(-a * a));
}

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

    bool opaque_pass = (g_flags & ETCH_GLOBAL_OPAQUE_PASS) != 0u;
    float pad = opaque_pass ? 0.0f : 1.0f;
    if (kind == ETCH_PRIM_SHADOW)
    {
        pad = max(bw_soft.y, 1.0f) * 2.0f + 2.0f;
    }

    static const float2 kCorners[6] = {
        float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f),
        float2(0.0f, 0.0f), float2(1.0f, 1.0f), float2(0.0f, 1.0f)
    };
    float2 corner = kCorners[vertex_id % 6u];

    float2 q_min = p.bounds.xy - pad;
    float2 q_max = p.bounds.xy + p.bounds.zw + pad;

    float angle = (kind == ETCH_PRIM_ROT_RECT) ? bw_soft.y : 0.0f;
    float c = cos(angle), sn = sin(angle);
    if (angle != 0.0f)
    {
        float2 hs = p.bounds.zw * 0.5f;
        float2 extent = float2(abs(c) * hs.x + abs(sn) * hs.y, abs(sn) * hs.x + abs(c) * hs.y) + pad;
        float2 center = p.bounds.xy + hs;
        q_min = center - extent;
        q_max = center + extent;
    }
    if (kind == ETCH_PRIM_ROT_RECT) bw_soft.y = 1.0f;

    float clip_pad = opaque_pass ? 0.0f : 1.0f;
    q_min = max(q_min, p.clip.xy - clip_pad);
    q_max = min(q_max, p.clip.zw + clip_pad);
    q_max = max(q_max, q_min);

    float2 px = lerp(q_min, q_max, corner);

    float4 col1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float2 grad_dir = float2(0.0f, 1.0f);
    if ((flags & (ETCH_FLAG_LINEAR_GRAD | ETCH_FLAG_RADIAL_GRAD)) != 0u)
    {
        col1 = unpack_rgba8_srgb_to_linear(p.payload.x);
        grad_dir = unpack_f16x2_custom(p.payload.y);
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
    o.grad_params  = float4(grad_dir.x, grad_dir.y, bw_soft.x, bw_soft.y);
    o.meta         = uint4(kind, flags | (g_flags & (ETCH_GLOBAL_MANUAL_SRGB | ETCH_GLOBAL_OPAQUE_PASS)), clip_idx, asuint(clip_r));
    o.rotation     = float2(c, sn);
    return o;
}

float4 gradient_color(VSOut input, float2 norm_uv)
{
    uint flags = input.meta.y;
    float4 fill_col = input.color0;
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
    uint kind  = input.meta.x;
    uint flags = input.meta.y;
    float2 size      = max(input.bounds.zw, float2(1e-4f, 1e-4f));
    float2 half_size = 0.5f * size;
    float2 center    = input.bounds.xy + half_size;
    float2 p_rel     = input.pixel_pos - center;

    // Fast path: opaque Early-Z square unclipped rect (exact integer pixel quad, no SDF clip needed).
    if ((flags & ETCH_GLOBAL_OPAQUE_PASS) != 0u)
    {
        float4 fill_col = gradient_color(input, (p_rel + half_size) / size);
        float border_w = input.grad_params.z;
        float4 out_pm = float4(fill_col.rgb, 1.0f);
        if (border_w > 0.0f)
        {
            float2 q = abs(p_rel) - half_size;
            float d = max(q.x, q.y);
            float inner_cov = saturate(0.5f - (d + border_w));
            float border_t = 1.0f - inner_cov;
            float4 b_pm = float4(input.border_color.rgb, 1.0f) * (input.border_color.a * border_t);
            out_pm = b_pm + out_pm * (1.0f - b_pm.a);
        }
        if ((flags & ETCH_GLOBAL_MANUAL_SRGB) != 0u)
        {
            out_pm.rgb = linear_to_srgb(saturate(out_pm)).rgb;
        }
        return out_pm;
    }

    float clip_r = asfloat(input.meta.w);
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
    uint clip_index = input.meta.z;
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
    if (clip_cov <= 0.0f) return float4(0.0f, 0.0f, 0.0f, 0.0f);

    float4 out_pm = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (kind == ETCH_PRIM_SHADOW)
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
    else
    {
        float2 p_local = float2(
            input.rotation.x * p_rel.x + input.rotation.y * p_rel.y,
            -input.rotation.y * p_rel.x + input.rotation.x * p_rel.y);
        float d = sd_rounded_box(p_local, half_size, input.radii, (flags & ETCH_FLAG_ROUND_CAP) != 0u);
        float4 fill_col = gradient_color(input, (p_local + half_size) / size);
        float border_w  = input.grad_params.z;
        float softness  = input.grad_params.w;
        float inv_aa    = 1.0f / max(softness, 0.5f);
        float outer_cov = saturate(0.5f - d * inv_aa);
        float inner_cov = (border_w > 0.0f) ? saturate(0.5f - (d + border_w) * inv_aa) : outer_cov;
        float border_t  = max(outer_cov - inner_cov, 0.0f);
        // Exact disjoint-area Porter-Duff over(border, fill): translucent borders blend over fill, opaque borders have zero outer halo.
        float4 b_pm = float4(input.border_color.rgb, 1.0f) * (input.border_color.a * border_t);
        float4 f_pm = float4(fill_col.rgb, 1.0f) * fill_col.a;
        out_pm      = b_pm + f_pm * (inner_cov + border_t * (1.0f - input.border_color.a));
    }

    out_pm *= clip_cov;
    if ((flags & ETCH_GLOBAL_MANUAL_SRGB) != 0u && out_pm.a > 1e-5f)
    {
        float4 straight_lin = saturate(out_pm / out_pm.a);
        out_pm.rgb = linear_to_srgb(straight_lin).rgb * out_pm.a;
    }
    return out_pm;
}
