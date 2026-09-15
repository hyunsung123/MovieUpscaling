// True separable Lanczos-2: sinc(x) * sinc(x/2), 4 x 4 support.
float Lanczos(float x) {
    x = abs(x);
    if (x < 1e-5) return 1;
    if (x >= 2) return 0;
    float t = 3.141592653589793 * x;
    return sin(t) * sin(t * 0.5) / (t * t * 0.5);
}
float4 PSMain(Vertex v) : SV_Target {
    float2 p = v.position.xy * inputSize / outputSize - 0.5;
    int2 base = int2(floor(p)); float2 f = frac(p);
    float4 wx = float4(Lanczos(f.x + 1), Lanczos(f.x), Lanczos(1 - f.x), Lanczos(2 - f.x));
    float4 wy = float4(Lanczos(f.y + 1), Lanczos(f.y), Lanczos(1 - f.y), Lanczos(2 - f.y));
    wx /= dot(wx, float4(1, 1, 1, 1)); wy /= dot(wy, float4(1, 1, 1, 1));
    float3 sum = 0;
    [unroll] for (int y = 0; y < 4; ++y)
        [unroll] for (int x = 0; x < 4; ++x)
            sum += LoadClamped(base + int2(x - 1, y - 1)) * wx[x] * wy[y];
    return float4(saturate(sum), 1);
}
