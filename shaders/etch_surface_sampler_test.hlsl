#ifdef __spirv__
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]]
#endif
Texture2D<float4> g_tex0 : register(t0, space2);
#ifdef __spirv__
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]]
#endif
SamplerState g_smp0 : register(s0, space2);

#ifdef __spirv__
[[vk::binding(0, 3)]]
#endif
cbuffer SurfaceFS : register(b0, space3)
{
    float4 viewport_time;
    float4 params[4];
};

float4 ps_main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
    float2 sample_uv = uv * params[0].xy + params[0].zw;
    float4 c = g_tex0.SampleLevel(g_smp0, sample_uv, 0.0f);
    return float4(c.rgb * c.a, c.a);
}
