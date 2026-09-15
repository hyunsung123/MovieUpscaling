// Adapted from AMD FidelityFX FSR 1, ffx_fsr1.h (v1.20210629), FsrEasuF/SetF/TapF.
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// MIT license: see THIRD_PARTY_NOTICES.md, distributed with this source.
// FP32 HLSL adaptation: clamped RGB loads instead of channel gathers, exact
// reciprocal/rsqrt with zero guards instead of approximate bit arithmetic.
// Preserves 12-tap support, four gradient estimates, anisotropic radial kernel,
// adaptive negative lobe, and nearest-2x2 deringing. NO RCAS pass.
float Luma(float3 c) { return c.g + 0.5 * (c.r + c.b); }
void Edge(inout float2 direction, inout float strength, float weight,
          float a, float b, float c, float d, float e) {
    float2 gradient = float2(d - b, e - a);
    float2 magnitude = max(float2(abs(d - c), abs(e - c)), float2(abs(c - b), abs(c - a)));
    float2 edge = saturate(abs(gradient) / max(magnitude, 1e-6));
    direction += gradient * weight;
    strength += dot(edge, edge) * weight;
}
void Tap(inout float3 color, inout float weight, float2 offset, float2 direction,
         float2 stretch, float lobe, float clipDistance, float3 sampleColor) {
    float2 rotated = float2(dot(offset, direction), dot(offset, float2(-direction.y, direction.x))) * stretch;
    float d2 = min(dot(rotated, rotated), clipDistance);
    float base = 0.4 * d2 - 1;
    float window = lobe * d2 - 1;
    float w = (1.5625 * base * base - 0.5625) * window * window;
    color += sampleColor * w; weight += w;
}
float4 PSMain(Vertex v) : SV_Target {
    float2 p = v.position.xy * inputSize / outputSize - 0.5;
    // Stabilize exact texel boundaries (e.g. 3x upscale) against the FP32
    // reciprocal rounding down: a different floor would change deringing bounds.
    float2 nearest = round(p);
    p = abs(p - nearest) < (1.0 / 4096.0) ? nearest : p;
    int2 q = int2(floor(p)); float2 f = frac(p);
    //     b c
    //   e F g h    F = floor(source position)
    //   i j k l
    //     n o
    float3 b = LoadClamped(q + int2(0,-1)), c = LoadClamped(q + int2(1,-1));
    float3 e = LoadClamped(q + int2(-1,0)), F = LoadClamped(q);
    float3 g = LoadClamped(q + int2(1,0)), h = LoadClamped(q + int2(2,0));
    float3 i = LoadClamped(q + int2(-1,1)), j = LoadClamped(q + int2(0,1));
    float3 k = LoadClamped(q + int2(1,1)), l = LoadClamped(q + int2(2,1));
    float3 n = LoadClamped(q + int2(0,2)), o = LoadClamped(q + int2(1,2));
    float2 direction = 0; float strength = 0;
    Edge(direction, strength, (1-f.x)*(1-f.y), Luma(b), Luma(e), Luma(F), Luma(g), Luma(j));
    Edge(direction, strength, f.x*(1-f.y), Luma(c), Luma(F), Luma(g), Luma(h), Luma(k));
    Edge(direction, strength, (1-f.x)*f.y, Luma(F), Luma(i), Luma(j), Luma(k), Luma(n));
    Edge(direction, strength, f.x*f.y, Luma(g), Luma(j), Luma(k), Luma(l), Luma(o));
    float norm = dot(direction, direction);
    direction = norm < (1.0/32768.0) ? float2(1, 0) : direction * rsqrt(max(norm, 1e-10));
    strength = strength * strength * 0.25;
    float diagonal = dot(direction, direction) / max(abs(direction.x), abs(direction.y));
    float2 stretch = float2(1 + (diagonal - 1) * strength, 1 - 0.5 * strength);
    float lobe = 0.5 - 0.29 * strength;
    float clipDistance = rcp(lobe);
    float3 sum = 0; float weight = 0;
    Tap(sum, weight, float2(0,-1)-f, direction, stretch, lobe, clipDistance, b);
    Tap(sum, weight, float2(1,-1)-f, direction, stretch, lobe, clipDistance, c);
    Tap(sum, weight, float2(-1,0)-f, direction, stretch, lobe, clipDistance, e);
    Tap(sum, weight, float2(0,0)-f, direction, stretch, lobe, clipDistance, F);
    Tap(sum, weight, float2(1,0)-f, direction, stretch, lobe, clipDistance, g);
    Tap(sum, weight, float2(2,0)-f, direction, stretch, lobe, clipDistance, h);
    Tap(sum, weight, float2(-1,1)-f, direction, stretch, lobe, clipDistance, i);
    Tap(sum, weight, float2(0,1)-f, direction, stretch, lobe, clipDistance, j);
    Tap(sum, weight, float2(1,1)-f, direction, stretch, lobe, clipDistance, k);
    Tap(sum, weight, float2(2,1)-f, direction, stretch, lobe, clipDistance, l);
    Tap(sum, weight, float2(0,2)-f, direction, stretch, lobe, clipDistance, n);
    Tap(sum, weight, float2(1,2)-f, direction, stretch, lobe, clipDistance, o);
    float3 lo = min(min(F, g), min(j, k)), hi = max(max(F, g), max(j, k));
    return float4(clamp(sum / max(weight, 1e-6), lo, hi), 1);
}
