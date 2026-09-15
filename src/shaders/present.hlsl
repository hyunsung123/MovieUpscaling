// Phase 3: GPU-only passthrough with aspect-fit bilinear sampling.
Texture2D sourceTexture : register(t0);
SamplerState linearClamp : register(s0);
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex VSMain(uint id : SV_VertexID) {
    Vertex v;
    v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return v;
}
float4 PSMain(Vertex v) : SV_Target {
    return float4(sourceTexture.Sample(linearClamp, v.uv).rgb, 1);
}
