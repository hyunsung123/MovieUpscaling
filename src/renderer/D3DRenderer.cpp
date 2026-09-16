#include "D3DRenderer.h"
#include "utils/GPUInfo.h"
#include <algorithm>
#include <cmath>
using namespace winrt;
void D3DRenderer::Initialize(HWND preview,bool debug) {
    hwnd_ = preview;
    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    auto create=[&](UINT flags) { return D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,
        flags,levels,ARRAYSIZE(levels),D3D11_SDK_VERSION,device_.put(),&level,context_.put()); };
    auto hr=create(D3D11_CREATE_DEVICE_BGRA_SUPPORT | (debug?D3D11_CREATE_DEVICE_DEBUG:0));
    if(hr==DXGI_ERROR_SDK_COMPONENT_MISSING && debug) hr=create(D3D11_CREATE_DEVICE_BGRA_SUPPORT);
    check_hresult(hr);
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
    upscaler_.Initialize(device_.get());
    rcas_.Initialize(device_.get()); compositor_.Initialize(device_.get()); timer_.Initialize(device_.get());
    Clear();
}
UpscaleSize D3DRenderer::OutputSize() const {
    switch (outputMode_) {
    case OutputMode::FullHD: return {1920, 1080};
    case OutputMode::QHD: return {2560, 1440};
    case OutputMode::UHD: return {3840, 2160};
    default: return MonitorSize();
    }
}
UpscaleSize D3DRenderer::MonitorSize() const {
    MONITORINFO info{sizeof(info)};
    check_bool(GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info));
    return {static_cast<UINT>(info.rcMonitor.right - info.rcMonitor.left), static_cast<UINT>(info.rcMonitor.bottom - info.rcMonitor.top)};
}
bool D3DRenderer::FitInput720p(bool preset) {
    if(!hasFrame_) return false;
    auto region=CropRegion(inputWidth_,inputHeight_,crop_);
    // Preserve an already-good crop exactly, including an asymmetric position.
    if(!preset || MatchTarget(region,{0,0,1280,720})!=TargetMatch::Good) region=Fit720p(inputWidth_,inputHeight_);
    crop_=PixelCrop(region,preset || crop_.aspect16x9);
    if(preset) {
        guide_.mode=GuideMode::HD720; outputMode_=OutputMode::QHD;
        mode_=UpscaleMode::Easu; sharpen_=25;
    }
    return true;
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
    hasFrame_=false;
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
        timer_.Reset();
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
    hasFrame_=true;
    return PresentFrame();
}
bool D3DRenderer::Redraw() {
    if(!hasFrame_ || !IsWindowVisible(hwnd_) || !Resize()) return false;
    return PresentFrame();
}
bool D3DRenderer::PresentFrame() {
    DrawFrame();
    HRESULT hr = swapChain_->Present(1, 0);
    check_hresult(hr);
    return hr != DXGI_STATUS_OCCLUDED;
}
void D3DRenderer::DrawFrame() {
    const UINT width=inputWidth_,height=inputHeight_;
    const auto output = OutputSize();
    if(SettingsPending()) timer_.Reset();
    cropRegion_=CropRegion(width,height,crop_);
    contentSize_ = FitOutput(cropRegion_.width,cropRegion_.height,output.width,output.height,mode_);
    ID3D11ShaderResourceView* view=inputView_.get(); SourceRegion region{0,0,width,height};
    if(!cropEdit_) {
        timer_.Begin();
        view=upscaler_.Process(inputView_.get(),width,height,contentSize_.width,contentSize_.height,mode_,cropRegion_);
        timer_.Split();
        view=rcas_.Process(view,upscaler_.ResultRegion(),sharpen_);
        timer_.End();
        region=rcas_.ResultRegion();
    }
    appliedMode_ = mode_; appliedOutput_ = output;
    appliedCrop_=crop_; appliedSharpen_=sharpen_; appliedEdit_=cropEdit_;
    compositor_.Draw(target_.get(),width_,height_,view,region,inputView_.get(),cropRegion_,output,contentSize_,compare_,split_,cropEdit_,guide_);
}
