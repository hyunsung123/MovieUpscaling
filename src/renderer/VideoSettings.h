#pragma once
#include <d3d11.h>
#include <winrt/base.h>
#include <algorithm>
#include <cmath>
struct SourceRegion {
    UINT x{}, y{}, width{}, height{};
    bool operator==(const SourceRegion&) const = default;
};
struct CropSettings {
    bool manual{};
    int left{}, top{}, right{}, bottom{};
    bool pixelCrop{}, aspect16x9{};
    SourceRegion pixels{};
    bool operator==(const CropSettings&) const = default;
};
enum class CompareMode { Off, Vertical, Horizontal };
inline SourceRegion FitAspect16x9(SourceRegion r) {
    if(!r.width || !r.height) return r;
    UINT w=r.width,h=r.height;
    if(uint64_t(w)*9>uint64_t(h)*16) w=std::max(1u,UINT(std::lround(h*16.0/9)));
    else h=std::max(1u,UINT(std::lround(w*9.0/16)));
    w=std::min(w,r.width); h=std::min(h,r.height);
    return {r.x+(r.width-w)/2,r.y+(r.height-h)/2,w,h};
}
inline SourceRegion CropRegion(UINT width, UINT height, CropSettings c) {
    if (!c.manual) return {0,0,width,height};
    if(!width || !height) return {};
    if(c.pixelCrop) {
        UINT w=std::clamp(c.pixels.width,1u,width),h=std::clamp(c.pixels.height,1u,height);
        SourceRegion r{std::min(c.pixels.x,width-w),std::min(c.pixels.y,height-h),w,h};
        return c.aspect16x9 && (w!=c.pixels.width || h!=c.pixels.height)?FitAspect16x9(r):r;
    }
    auto cut = [](UINT dimension, int percent) { return static_cast<UINT>(std::lround(double(dimension) * std::clamp(percent,0,45) / 100.0)); };
    UINT x=std::min(cut(width,c.left),width ? width-1 : 0), y=std::min(cut(height,c.top),height ? height-1 : 0);
    UINT right=std::min(cut(width,c.right),width ? width-x-1 : 0), bottom=std::min(cut(height,c.bottom),height ? height-y-1 : 0);
    SourceRegion r{x,y,width-x-right,height-y-bottom};
    return c.aspect16x9?FitAspect16x9(r):r;
}
inline SourceRegion FullRegion(ID3D11ShaderResourceView* view) {
    winrt::com_ptr<ID3D11Resource> resource; view->GetResource(resource.put());
    D3D11_TEXTURE2D_DESC desc{}; resource.as<ID3D11Texture2D>()->GetDesc(&desc);
    return {0,0,desc.Width,desc.Height};
}
