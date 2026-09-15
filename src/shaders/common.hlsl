Texture2D<float4> sourceTexture : register(t0);
SamplerState linearClamp : register(s0);
cbuffer Dimensions : register(b0) { float2 inputSize; float2 outputSize; };
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex VSMain(uint id : SV_VertexID) {
    Vertex v;
    v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return v;
}
float3 LoadClamped(int2 p) {
    return sourceTexture.Load(int3(clamp(p, int2(0, 0), int2(inputSize) - 1), 0)).rgb;
}
