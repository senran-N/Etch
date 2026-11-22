// Etch Shared HLSL Helpers (included by etch_quad.hlsl and etch_uber.hlsl).
#ifndef ETCH_COMMON_HLSLI
#define ETCH_COMMON_HLSLI

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
    return outer_cov * saturate(0.5f * (1.0f + erf_approx(d_inner * inv_s2)));
}

#endif // ETCH_COMMON_HLSLI
