#pragma once
#include "GPUUpscaler.h"
#include "InputGuide.h"
struct DisplayRect { float x{}, y{}, width{}, height{}; };
class GPUCompositor {
public:
    void Initialize(ID3D11Device* device);
    void Draw(ID3D11RenderTargetView* target, UINT width, UINT height,
        ID3D11ShaderResourceView* processed, SourceRegion processedRegion,
        ID3D11ShaderResourceView* source, SourceRegion crop, UpscaleSize canvas, UpscaleSize content,
        CompareMode compare, float split, bool edit, InputGuide guide={});
    DisplayRect ContentRect() const { return contentRect_; }
private:
    winrt::com_ptr<ID3D11DeviceContext> context_;
    winrt::com_ptr<ID3D11VertexShader> vertex_;
    winrt::com_ptr<ID3D11PixelShader> pixel_;
    winrt::com_ptr<ID3D11SamplerState> sampler_;
    winrt::com_ptr<ID3D11Buffer> constants_;
    DisplayRect contentRect_{};
};
