#include "D3DRenderer.h"
#include "utils/GPUInfo.h"
#include "ShaderSource.h"
#include "ShaderCompiler.h"
#include <algorithm>
#include <cmath>
using namespace winrt;
void D3DRenderer::Initialize(HWND preview) {
    hwnd_ = preview;
    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
        device_.put(), &level, context_.put()));
    adapterName_ = GPUName(device_.get());
    auto dxgi = device_.as<IDXGIDevice1>();
    check_hresult(dxgi->SetMaximumFrameLatency(1));
    com_ptr<IDXGIAdapter> adapter;
    check_hresult(dxgi->GetAdapter(adapter.put()));
    com_ptr<IDXGIFactory2> factory;
    check_hresult(adapter->GetParent(IID_PPV_ARGS(factory.put())));
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    check_hresult(factory->CreateSwapChainForHwnd(device_.get(), hwnd_, &desc, nullptr, nullptr, swapChain_.put()));
    check_hresult(factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER));
    auto vs = CompileShader(PresentShader, "VSMain", "vs_5_0"), ps = CompileShader(PresentShader, "PSMain", "ps_5_0");
    check_hresult(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, vertex_.put()));
    check_hresult(device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, pixel_.put()));
    D3D11_SAMPLER_DESC samp{};
    samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp.MaxLOD = D3D11_FLOAT32_MAX;
    check_hresult(device_->CreateSamplerState(&samp, sampler_.put()));
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE; raster.ScissorEnable = TRUE;
    check_hresult(device_->CreateRasterizerState(&raster, scissor_.put()));
    upscaler_.Initialize(device_.get());
    Clear();
}
UpscaleSize D3DRenderer::OutputSize() const {
    switch (outputMode_) {
    case OutputMode::FullHD: return {1920, 1080};
    case OutputMode::QHD: return {2560, 1440};
    case OutputMode::UHD: return {3840, 2160};
    default:
        MONITORINFO info{sizeof(info)};
        check_bool(GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info));
        return {static_cast<UINT>(info.rcMonitor.right - info.rcMonitor.left), static_cast<UINT>(info.rcMonitor.bottom - info.rcMonitor.top)};
    }
}
bool D3DRenderer::Resize() {
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    const UINT w = static_cast<UINT>(rect.right), h = static_cast<UINT>(rect.bottom);
    if (!w || !h || IsIconic(hwnd_)) return false;
    if (w == width_ && h == height_ && target_) return true;
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    target_ = nullptr;
    check_hresult(swapChain_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0));
    com_ptr<ID3D11Texture2D> back;
    check_hresult(swapChain_->GetBuffer(0, IID_PPV_ARGS(back.put())));
    check_hresult(device_->CreateRenderTargetView(back.get(), nullptr, target_.put()));
    width_ = w; height_ = h;
    return true;
}
void D3DRenderer::Clear() {
    if (!swapChain_ || !Resize()) return;
    const float black[] = {0, 0, 0, 1};
    context_->ClearRenderTargetView(target_.get(), black);
    check_hresult(swapChain_->Present(1, 0));
}
bool D3DRenderer::Render(ID3D11Texture2D* source, UINT width, UINT height) {
    if (!Resize() || !IsWindowVisible(hwnd_)) return false;
    D3D11_TEXTURE2D_DESC sourceDesc{};
    source->GetDesc(&sourceDesc);
    // During live resize ContentSize may be larger than the current pool texture.
    // Skip this transitional frame; the caller recreates the frame pool afterward.
    if (!width || !height || width > sourceDesc.Width || height > sourceDesc.Height) return false;
    if (width != inputWidth_ || height != inputHeight_) {
        inputView_ = nullptr; input_ = nullptr;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        check_hresult(device_->CreateTexture2D(&desc, nullptr, input_.put()));
        check_hresult(device_->CreateShaderResourceView(input_.get(), nullptr, inputView_.put()));
        inputWidth_ = width; inputHeight_ = height;
    }
    // One GPU-to-GPU copy gives us a shader-readable texture independent of WGC
    // buffer flags. No staging resource, Map, readback, or CPU pixel processing.
    D3D11_BOX box{0, 0, 0, width, height, 1};
    context_->CopySubresourceRegion(input_.get(), 0, 0, 0, 0, source, 0, &box);
    const auto output = OutputSize();
    contentSize_ = FitOutput(width, height, output.width, output.height, mode_);
    auto view = upscaler_.Process(inputView_.get(), width, height, contentSize_.width, contentSize_.height, mode_);
    appliedMode_ = mode_; appliedOutput_ = output;
    const float black[] = {0, 0, 0, 1};
    context_->ClearRenderTargetView(target_.get(), black);
    // Present the selected output canvas inside the window. Content-only
    // intermediate textures avoid shading black bars. Native stays 1:1 in the
    // output canvas; scissoring center-crops only if the source exceeds it.
    const float scale = std::min(float(width_) / output.width, float(height_) / output.height);
    const float canvasX = (width_ - output.width * scale) / 2, canvasY = (height_ - output.height * scale) / 2;
    D3D11_RECT clip{static_cast<LONG>(std::lround(canvasX)), static_cast<LONG>(std::lround(canvasY)),
        static_cast<LONG>(std::lround(canvasX + output.width * scale)), static_cast<LONG>(std::lround(canvasY + output.height * scale))};
    context_->RSSetState(scissor_.get()); context_->RSSetScissorRects(1, &clip);
    D3D11_VIEWPORT viewport{(width_ - contentSize_.width * scale) / 2, (height_ - contentSize_.height * scale) / 2,
        contentSize_.width * scale, contentSize_.height * scale, 0, 1};
    context_->RSSetViewports(1, &viewport);
    auto target = target_.get();
    context_->OMSetRenderTargets(1, &target, nullptr);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertex_.get(), nullptr, 0);
    context_->PSSetShader(pixel_.get(), nullptr, 0);
    auto sampler = sampler_.get();
    context_->PSSetShaderResources(0, 1, &view);
    context_->PSSetSamplers(0, 1, &sampler);
    context_->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    context_->PSSetShaderResources(0, 1, &none);
    HRESULT hr = swapChain_->Present(1, 0);
    check_hresult(hr);
    return hr != DXGI_STATUS_OCCLUDED;
}
