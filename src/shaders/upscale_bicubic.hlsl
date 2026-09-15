// Catmull-Rom interpolating cubic, a = -0.5; separable 4 x 4 support.
float Cubic(float x) {
    x = abs(x);
    if (x < 1) return ((1.5 * x - 2.5) * x) * x + 1;
    if (x < 2) return ((-0.5 * x + 2.5) * x - 4) * x + 2;
    return 0;
}
float4 PSMain(Vertex v) : SV_Target {
    float2 p = v.position.xy * inputSize / outputSize - 0.5;
    int2 base = int2(floor(p)); float2 f = frac(p);
    float4 wx = float4(Cubic(f.x + 1), Cubic(f.x), Cubic(1 - f.x), Cubic(2 - f.x));
    float4 wy = float4(Cubic(f.y + 1), Cubic(f.y), Cubic(1 - f.y), Cubic(2 - f.y));
    float3 sum = 0;
    [unroll] for (int y = 0; y < 4; ++y)
        [unroll] for (int x = 0; x < 4; ++x)
            sum += LoadClamped(base + int2(x - 1, y - 1)) * wx[x] * wy[y];
    return float4(saturate(sum), 1);
}
