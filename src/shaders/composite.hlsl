// Final presentation only: comparison and crop-edit overlays never enter RCAS.
Texture2D<float4> baselineTexture : register(t1);
cbuffer Composition : register(b2) {
    float4 contentRect;
    float4 canvasRect;
    float4 processedRegion;
    float4 baselineRegion;
    float4 imageSizes; // processed texture xy, full WGC texture zw
    float4 options; // compare: 0/1/2, divider 0..1, edit flag, unused
    float4 cropBounds; // normalized left/top/right/bottom coordinates
};
float3 SampleRegion(Texture2D<float4> image, float2 uv, float4 region, float2 size) {
    float2 pixel=region.xy+clamp(uv*region.zw,.5,region.zw-.5);
    return image.SampleLevel(linearClamp,pixel/size,0).rgb;
}
float4 PSMain(Vertex v) : SV_Target {
    float2 location=v.position.xy;
    float2 uv=(location-contentRect.xy)/contentRect.zw;
    if(any(uv<0) || any(uv>=1) || any(location<canvasRect.xy) || any(location>=canvasRect.xy+canvasRect.zw)) return float4(0,0,0,1);
    if(options.z>0) {
        float3 color=baselineTexture.SampleLevel(linearClamp,uv,0).rgb;
        bool outside=any(uv<cropBounds.xy) || any(uv>cropBounds.zw);
        float2 delta=min(abs(uv-cropBounds.xy),abs(uv-cropBounds.zw))*contentRect.zw;
        bool border=(delta.x<2 && uv.y>=cropBounds.y && uv.y<=cropBounds.w) ||
                    (delta.y<2 && uv.x>=cropBounds.x && uv.x<=cropBounds.z);
        return float4(border ? float3(0,1,.8) : color*(outside?.28:1),1);
    }
    float axis=options.x==2 ? uv.y : uv.x;
    float extent=options.x==2 ? contentRect.w : contentRect.z;
    if(options.x>0 && options.y>0 && options.y<1 && abs(axis-options.y)*extent<1) return float4(0,1,.8,1);
    if(options.x>0 && axis<options.y) return float4(SampleRegion(baselineTexture,uv,baselineRegion,imageSizes.zw),1);
    return float4(SampleRegion(sourceTexture,uv,processedRegion,imageSizes.xy),1);
}
