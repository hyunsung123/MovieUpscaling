#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <winrt/base.h>
#include <string>
#include "GPUUpscaler.h"
class D3DRenderer {
public:
    void Initialize(HWND preview);
    bool Render(ID3D11Texture2D* source, UINT width, UINT height);
    void Clear();
    void SetUpscaleMode(UpscaleMode mode) { mode_ = mode; }
    void SetOutputMode(OutputMode mode) { outputMode_ = mode; }
    UpscaleMode Mode() const { return mode_; }
    UpscaleSize OutputSize() const;
    UpscaleSize ContentSize() const { return contentSize_; }
    bool SettingsPending() const { return mode_ != appliedMode_ || OutputSize() != appliedOutput_; }
    double Scale() const { return inputWidth_ ? double(contentSize_.width) / inputWidth_ : 0; }
    const GPUUpscaler& Upscaler() const { return upscaler_; }
    ID3D11Device* Device() const { return device_.get(); }
    UINT Width() const { return width_; }
    UINT Height() const { return height_; }
    const std::wstring& AdapterName() const { return adapterName_; }
private:
    bool Resize();
    HWND hwnd_{};
    UINT width_{}, height_{}, inputWidth_{}, inputHeight_{};
    std::wstring adapterName_;
    UpscaleMode mode_{UpscaleMode::Easu};
    OutputMode outputMode_{OutputMode::Auto};
    UpscaleSize contentSize_{};
    UpscaleSize appliedOutput_{};
    UpscaleMode appliedMode_{UpscaleMode::Native};
    GPUUpscaler upscaler_;
    winrt::com_ptr<ID3D11Device> device_;
    winrt::com_ptr<ID3D11DeviceContext> context_;
    winrt::com_ptr<IDXGISwapChain1> swapChain_;
    winrt::com_ptr<ID3D11RenderTargetView> target_;
    winrt::com_ptr<ID3D11Texture2D> input_;
    winrt::com_ptr<ID3D11ShaderResourceView> inputView_;
    winrt::com_ptr<ID3D11VertexShader> vertex_;
    winrt::com_ptr<ID3D11PixelShader> pixel_;
    winrt::com_ptr<ID3D11SamplerState> sampler_;
    winrt::com_ptr<ID3D11RasterizerState> scissor_;
};
