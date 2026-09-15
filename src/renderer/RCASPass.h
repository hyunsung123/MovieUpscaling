#pragma once
#include <winrt/base.h>
#include "VideoSettings.h"
class RCASPass {
public:
    void Initialize(ID3D11Device* device, DXGI_FORMAT format=DXGI_FORMAT_B8G8R8A8_UNORM);
    ID3D11ShaderResourceView* Process(ID3D11ShaderResourceView* source, SourceRegion region, int strength);
    SourceRegion ResultRegion() const { return result_; }
    uint64_t Allocations() const { return allocations_; }
    bool DebugBoost() const { return debugBoost_; }
private:
    winrt::com_ptr<ID3D11Device> device_;
    winrt::com_ptr<ID3D11DeviceContext> context_;
    winrt::com_ptr<ID3D11Texture2D> texture_;
    winrt::com_ptr<ID3D11ShaderResourceView> view_;
    winrt::com_ptr<ID3D11RenderTargetView> target_;
    winrt::com_ptr<ID3D11VertexShader> vertex_;
    winrt::com_ptr<ID3D11PixelShader> pixel_;
    winrt::com_ptr<ID3D11Buffer> dimensions_, strength_;
    SourceRegion result_{};
    DXGI_FORMAT format_{};
    UINT width_{},height_{};
    uint64_t allocations_{};
    bool debugBoost_{};
};
