#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <winrt/base.h>
#include <string>
class D3DRenderer {
public:
    void Initialize(HWND preview);
    bool Render(ID3D11Texture2D* source, UINT width, UINT height);
    void Clear();
    ID3D11Device* Device() const { return device_.get(); }
    UINT Width() const { return width_; }
    UINT Height() const { return height_; }
    const std::wstring& AdapterName() const { return adapterName_; }
private:
    bool Resize();
    HWND hwnd_{};
    UINT width_{}, height_{}, inputWidth_{}, inputHeight_{};
    std::wstring adapterName_;
    winrt::com_ptr<ID3D11Device> device_;
    winrt::com_ptr<ID3D11DeviceContext> context_;
    winrt::com_ptr<IDXGISwapChain1> swapChain_;
    winrt::com_ptr<ID3D11RenderTargetView> target_;
    winrt::com_ptr<ID3D11Texture2D> input_;
    winrt::com_ptr<ID3D11ShaderResourceView> inputView_;
    winrt::com_ptr<ID3D11VertexShader> vertex_;
    winrt::com_ptr<ID3D11PixelShader> pixel_;
    winrt::com_ptr<ID3D11SamplerState> sampler_;
};
