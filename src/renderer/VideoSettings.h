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
    bool operator==(const CropSettings&) const = default;
};
enum class CompareMode { Off, Vertical, Horizontal };
inline SourceRegion CropRegion(UINT width, UINT height, CropSettings c) {
    if (!c.manual) return {0,0,width,height};
    auto cut = [](UINT dimension, int percent) { return static_cast<UINT>(std::lround(double(dimension) * std::clamp(percent,0,45) / 100.0)); };
    UINT x=std::min(cut(width,c.left),width ? width-1 : 0), y=std::min(cut(height,c.top),height ? height-1 : 0);
    UINT right=std::min(cut(width,c.right),width ? width-x-1 : 0), bottom=std::min(cut(height,c.bottom),height ? height-y-1 : 0);
    return {x,y,width-x-right,height-y-bottom};
}
inline SourceRegion FullRegion(ID3D11ShaderResourceView* view) {
    winrt::com_ptr<ID3D11Resource> resource; view->GetResource(resource.put());
    D3D11_TEXTURE2D_DESC desc{}; resource.as<ID3D11Texture2D>()->GetDesc(&desc);
    return {0,0,desc.Width,desc.Height};
}
