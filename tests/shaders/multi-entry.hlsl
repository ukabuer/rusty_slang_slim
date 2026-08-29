[[vk::binding(0, 0)]]
cbuffer SceneConstants : register(b0)
{
    float4 tint;
};

[[vk::binding(1, 0)]] Texture2D<float4> sampledTexture : register(t0);
[[vk::binding(2, 0)]] SamplerState sampledSampler : register(s0);
[[vk::binding(3, 0)]] RWStructuredBuffer<float4> outputValues : register(u1);

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertex_main(uint vertexId : SV_VertexID)
{
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);

    VertexOutput output;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.uv = uv;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0
{
    return sampledTexture.Sample(sampledSampler, input.uv) * tint;
}

[shader("compute")]
[numthreads(8, 1, 1)]
void compute_main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    outputValues[dispatchThreadId.x] = tint;
}
