#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <winrt/base.h>
#include <string>
#include "GPUUpscaler.h"
#include "RCASPass.h"
#include "GPUCompositor.h"
#include "GPUTimer.h"
class D3DRenderer {
public:
    void Initialize(HWND preview, bool debug = false);
    bool Render(ID3D11Texture2D* source, UINT width, UINT height);
    // One user-requested redraw of the retained GPU frame; never a frame loop.
    bool Redraw();
    void Clear();
    void SetUpscaleMode(UpscaleMode mode) { mode_ = mode; }
    void SetOutputMode(OutputMode mode) { outputMode_ = mode; }
    void SetSharpen(int value) { sharpen_=std::clamp(value,0,100); }
    int Sharpen() const { return sharpen_; }
    bool RCASDebugBoost() const { return rcas_.DebugBoost(); }
    void SetCrop(CropSettings crop) { crop_=crop; }
    CropSettings Crop() const { return crop_; }
    SourceRegion CroppedRegion() const { return cropRegion_; }
    void SetCropEdit(bool enabled) { cropEdit_=enabled; }
    bool CropEdit() const { return cropEdit_; }
    void SetCompare(CompareMode mode) { compare_=mode; }
    CompareMode Compare() const { return compare_; }
    void SetSplit(float position) { split_=std::clamp(position,0.f,1.f); }
    float Split() const { return split_; }
    DisplayRect PreviewContentRect() const { return compositor_.ContentRect(); }
    std::optional<GPUTimer::Result> PostProcessTime() const { return timer_.Value(); }
    uint64_t RCASAllocations() const { return rcas_.Allocations(); }
    UpscaleMode Mode() const { return mode_; }
    UpscaleSize OutputSize() const;
    UpscaleSize ContentSize() const { return contentSize_; }
    bool SettingsPending() const { return mode_ != appliedMode_ || OutputSize() != appliedOutput_ || crop_!=appliedCrop_ || sharpen_!=appliedSharpen_ || cropEdit_!=appliedEdit_; }
    double Scale() const { return cropRegion_.width ? double(contentSize_.width) / cropRegion_.width : 0; }
    const GPUUpscaler& Upscaler() const { return upscaler_; }
    ID3D11Device* Device() const { return device_.get(); }
    UINT Width() const { return width_; }
    UINT Height() const { return height_; }
    const std::wstring& AdapterName() const { return adapterName_; }
private:
    friend struct RendererTestAccess; // Test-only readback BEFORE flip-discard Present.
    bool Resize();
    void DrawFrame();
    bool PresentFrame();
    bool hasFrame_{};
    HWND hwnd_{};
    UINT width_{}, height_{}, inputWidth_{}, inputHeight_{};
    std::wstring adapterName_;
    UpscaleMode mode_{UpscaleMode::Easu};
    OutputMode outputMode_{OutputMode::Auto};
    UpscaleSize contentSize_{};
    UpscaleSize appliedOutput_{};
    UpscaleMode appliedMode_{UpscaleMode::Native};
    GPUUpscaler upscaler_;
    RCASPass rcas_;
    GPUCompositor compositor_;
    GPUTimer timer_;
    CropSettings crop_{},appliedCrop_{};
    SourceRegion cropRegion_{};
    int sharpen_{25},appliedSharpen_{};
    bool cropEdit_{},appliedEdit_{};
    CompareMode compare_{CompareMode::Off};
    float split_{.5f};
    winrt::com_ptr<ID3D11Device> device_;
    winrt::com_ptr<ID3D11DeviceContext> context_;
    winrt::com_ptr<IDXGISwapChain1> swapChain_;
    winrt::com_ptr<ID3D11RenderTargetView> target_;
    winrt::com_ptr<ID3D11Texture2D> input_;
    winrt::com_ptr<ID3D11ShaderResourceView> inputView_;
};
