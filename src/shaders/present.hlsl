// common.hlsl is prepended at build time. Also used for hardware bilinear upscale.
float4 PSMain(Vertex v) : SV_Target {
    return float4(sourceTexture.Sample(linearClamp, v.uv).rgb, 1);
}
