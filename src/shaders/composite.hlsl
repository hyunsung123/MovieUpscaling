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
    float4 guideBounds;
    float4 guideSize; // zero = guide Off; overlay metadata only
};
// Small built-in bitmap glyphs: no video readback or per-frame font texture.
uint Glyph(uint code) {
    switch(code) {
    case 48: return 31599u; // 0
    case 49: return 29850u; // 1
    case 50: return 29671u; // 2
    case 51: return 31207u; // 3
    case 52: return 18925u; // 4
    case 53: return 31183u; // 5
    case 54: return 31695u; // 6
    case 55: return 9383u; // 7
    case 56: return 31727u; // 8
    case 57: return 31215u; // 9
    case 67: return 29263u; // C
    case 82: return 23275u; // R
    case 79: return 31599u; // O
    case 80: return 4843u; // P
    case 84: return 9367u; // T
    case 65: return 23530u; // A
    case 71: return 31567u; // G
    case 69: return 29391u; // E
    case 120: return 2728u; // x
    default: return 0;
    }
}
uint Digits(uint value) { return value>=10000?5:value>=1000?4:value>=100?3:value>=10?2:1; }
uint DigitChar(uint value,uint at,uint count) {
    uint divisor=1;
    for(uint i=at+1;i<count;++i) divisor*=10;
    return 48+(value/divisor)%10;
}
uint LabelChar(uint at,bool target) {
    const uint cropText[5]={67,82,79,80,32};
    const uint targetText[7]={84,65,82,71,69,84,32};
    uint prefix=target?7:5;
    if(at<prefix) return target?targetText[at]:cropText[at];
    at-=prefix;
    uint w=uint(target?guideSize.x:baselineRegion.z),h=uint(target?guideSize.y:baselineRegion.w);
    uint dw=Digits(w),dh=Digits(h);
    if(at<dw) return DigitChar(w,at,dw);
    if(at==dw) return 120;
    at-=dw+1;
    return at<dh?DigitChar(h,at,dh):32;
}
float3 EditLabels(float2 location,float3 color) {
    float font=clamp(floor(contentRect.w/360),2,4);
    float2 p=(location-contentRect.xy-float2(10,10))/font;
    float rows=guideSize.x>0?2:1;
    if(any(p<0) || p.x>=76 || p.y>=rows*7) return color;
    uint row=uint(p.y)/7,at=uint(p.x)/4;
    uint2 cell=uint2(p.x,p.y)%uint2(4,7);
    bool ink=cell.x<3 && cell.y<5 && ((Glyph(LabelChar(at,row==1))>>(cell.y*3+cell.x))&1)!=0;
    return ink?(row==0?float3(0,1,.8):float3(1,.75,.15)):color*.12;
}
float3 SampleRegion(Texture2D<float4> image, float2 uv, float4 region, float2 size) {
    float2 pixel=region.xy+clamp(uv*region.zw,.5,region.zw-.5);
    return image.SampleLevel(linearClamp,pixel/size,0).rgb;
}
// Presentation uses linear sampling: a 4K processed texture in a 1440p Preview
// naturally softens RCAS detail. Compare sharpening with content and Preview
// at equal dimensions; do not mistake final downsampling for an RCAS bypass.
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
        color=border?float3(0,1,.8):color*(outside?.28:1);
        if(guideSize.x>0) {
            float2 gd=min(abs(uv-guideBounds.xy),abs(uv-guideBounds.zw))*contentRect.zw;
            bool guideBorder=(gd.x<2 && uv.y>=guideBounds.y && uv.y<=guideBounds.w) ||
                             (gd.y<2 && uv.x>=guideBounds.x && uv.x<=guideBounds.z);
            if(guideBorder && frac((location.x+location.y)/14)<.6) color=float3(1,.75,.15);
        }
        return float4(EditLabels(location,color),1);
    }
    float axis=options.x==2 ? uv.y : uv.x;
    float extent=options.x==2 ? contentRect.w : contentRect.z;
    if(options.x>0 && options.y>0 && options.y<1 && abs(axis-options.y)*extent<1) return float4(0,1,.8,1);
    if(options.x>0 && axis<options.y) return float4(SampleRegion(baselineTexture,uv,baselineRegion,imageSizes.zw),1);
    return float4(SampleRegion(sourceTexture,uv,processedRegion,imageSizes.xy),1);
}
