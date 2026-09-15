float4 PSMain(Vertex v) : SV_Target {
    float2 pixel = clamp(v.uv * inputSize, 0.5, inputSize - 0.5) + sourceOrigin;
    return float4(sourceTexture.SampleLevel(linearClamp, pixel / textureSize, 0).rgb, 1);
}
