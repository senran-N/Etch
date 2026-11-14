#ifdef __spirv__
#define ETCH_KAWASE_COMBINED_SAMPLER(slot) [[vk::combinedImageSampler]] [[vk::binding(slot, 2)]]
#define ETCH_KAWASE_UNIFORM_FS(slot)       [[vk::binding(slot, 3)]]
#else
#define ETCH_KAWASE_COMBINED_SAMPLER(slot)
#define ETCH_KAWASE_UNIFORM_FS(slot)
#endif

ETCH_KAWASE_COMBINED_SAMPLER(0) Texture2D<float4> g_src_tex : register(t0, space2);
ETCH_KAWASE_COMBINED_SAMPLER(0) SamplerState      g_src_smp : register(s0, space2);

ETCH_KAWASE_UNIFORM_FS(0) cbuffer KawaseFS : register(b0, space3)
{
    float2 g_texel_size;
    float  g_offset;
    uint   g_mode; // 0 = 5-tap downsample, 1 = 8-tap tent upsample, 2 = linear-to-sRGB present
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut vs_main(uint vertex_id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    o.uv  = uv;
    o.pos = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
    return o;
}

float linear_to_srgb_chan(float c)
{
    return (c <= 0.0031308f) ? (c * 12.92f) : (1.055f * pow(max(c, 0.0f), 1.0f / 2.4f) - 0.055f);
}

float3 linear_to_srgb(float3 c)
{
    return float3(linear_to_srgb_chan(c.r), linear_to_srgb_chan(c.g), linear_to_srgb_chan(c.b));
}

float4 ps_main(VSOut input) : SV_Target0
{
    float2 uv = input.uv;
    if (g_mode == 0u)
    {
        float2 d = g_texel_size * g_offset;
        float4 sum = g_src_tex.SampleLevel(g_src_smp, uv, 0.0f) * 4.0f;
        sum += g_src_tex.SampleLevel(g_src_smp, uv + float2(-d.x, -d.y), 0.0f);
        sum += g_src_tex.SampleLevel(g_src_smp, uv + float2( d.x, -d.y), 0.0f);
        sum += g_src_tex.SampleLevel(g_src_smp, uv + float2(-d.x,  d.y), 0.0f);
        sum += g_src_tex.SampleLevel(g_src_smp, uv + float2( d.x,  d.y), 0.0f);
        return sum * (1.0f / 8.0f);
    }
    if (g_mode == 1u)
    {
        float2 d = g_texel_size * g_offset;
        float4 sum = 0.0f;
        sum += g_src_tex.SampleLevel(g_src_smp, uv + float2(-d.x * 2.0f,  0.0f),       0.0f);
        sum += g_src_tex.SampleLevel(g_src_smp, uv + float2(-d.x,         d.y),        0.0f) * 2.0f;
        sum += g_src_tex.SampleLevel(g_src_smp, uv + float2( 0.0f,        d.y * 2.0f), 0.0f);
        sum += g_src_tex.SampleLevel(g_src_smp, uv + float2( d.x,         d.y),        0.0f) * 2.0f;
        sum += g_src_tex.SampleLevel(g_src_smp, uv + float2( d.x * 2.0f,  0.0f),       0.0f);
        sum += g_src_tex.SampleLevel(g_src_smp, uv + float2( d.x,        -d.y),        0.0f) * 2.0f;
        sum += g_src_tex.SampleLevel(g_src_smp, uv + float2( 0.0f,       -d.y * 2.0f), 0.0f);
        sum += g_src_tex.SampleLevel(g_src_smp, uv + float2(-d.x,        -d.y),        0.0f) * 2.0f;
        return sum * (1.0f / 12.0f);
    }

    float4 c = g_src_tex.SampleLevel(g_src_smp, uv, 0.0f);
    float3 s = 0.0f;
    if (c.a > 1e-5f)
    {
        s = linear_to_srgb(saturate(c.rgb / c.a)) * c.a;
    }
    return float4(s, c.a);
}
