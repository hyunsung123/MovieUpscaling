// AMD FidelityFX FSR 1 RCAS FP32 adaptation. Copyright (c) 2021 AMD.
// MIT permission in THIRD_PARTY_NOTICES.md. Five-tap adaptive RCAS, NOT unsharp mask.
// Dimensions/common declarations are prepended. Default enable AMD noise attenuation.
cbuffer Sharpen : register(b1) { float sharpness; float3 padding; };
float4 PSMain(Vertex v) : SV_Target {
    int2 p = int2(v.position.xy);
    float3 b=LoadClamped(p+int2(0,-1)), d=LoadClamped(p+int2(-1,0));
    float3 e=LoadClamped(p), f=LoadClamped(p+int2(1,0)), h=LoadClamped(p+int2(0,1));
    float3 mn=min(min(b,d),min(f,h)), mx=max(max(b,d),max(f,h));
    // RCAS limiting solutions for avoiding undershoot below 0 / overshoot above 1.
    float3 hitMin=min(mn,e) / max(4*mx,1e-6);
    float3 hitMax=(1-max(mx,e)) / min(4*mn-4,-1e-6);
    float3 lobes=max(-hitMin,hitMax);
    float lobe=max(-0.1875,min(max(lobes.r,max(lobes.g,lobes.b)),0)) * sharpness;
    float bL=b.g+.5*(b.r+b.b), dL=d.g+.5*(d.r+d.b), eL=e.g+.5*(e.r+e.b);
    float fL=f.g+.5*(f.r+f.b), hL=h.g+.5*(h.r+h.b);
    float range=max(max(max(bL,dL),max(fL,hL)),eL)-min(min(min(bL,dL),min(fL,hL)),eL);
    float noise=saturate(abs(.25*(bL+dL+fL+hL)-eL)/max(range,1e-6));
    lobe *= 1-.5*noise;
    float3 result=(e+lobe*(b+d+f+h))/(1+4*lobe);
    // Practical video guard: permit at most 2% overshoot of the local five-tap
    // envelope, in addition to RCAS's own limiters. Helps subtitles/compression.
    return float4(saturate(clamp(result,min(mn,e)-.02,max(mx,e)+.02)),1);
}
