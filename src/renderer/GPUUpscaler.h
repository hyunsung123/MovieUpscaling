#pragma once
#include <d3d11.h>
#include <winrt/base.h>
#include <array>
#include <optional>
#include "VideoSettings.h"
enum class UpscaleMode { Native, Bilinear, Bicubic, Lanczos, Easu };
enum class OutputMode { Auto, FullHD, QHD, UHD };
const wchar_t* UpscaleName(UpscaleMode mode);
struct UpscaleSize {
    UINT width{}, height{};
    bool operator==(const UpscaleSize&) const = default;
};
UpscaleSize FitOutput(UINT inputWidth, UINT inputHeight, UINT outputWidth, UINT outputHeight, UpscaleMode mode);

// Owns only the upscale pass. No WGC dependency or CPU access to video pixels.
class GPUUpscaler {
public:
    void Initialize(ID3D11Device* device);
    ID3D11ShaderResourceView* Process(ID3D11ShaderResourceView* source, UINT inputWidth, UINT inputHeight,
        UINT outputWidth, UINT outputHeight, UpscaleMode mode, SourceRegion region = {});
    SourceRegion ResultRegion() const { return resultRegion_; }
    std::optional<double> GPUTimeMs() const { return gpuMs_; }
    uint64_t TimingSamples() const { return timingSamples_; }
    uint64_t Allocations() const { return allocations_; }
    bool Bypassed() const { return bypassed_; }
    UpscaleMode EffectiveMode() const { return effective_; }
private:
    void PollTiming();
    struct Query {
        winrt::com_ptr<ID3D11Query> disjoint, begin, end;
        bool pending{};
        uint64_t generation{};
    };
    winrt::com_ptr<ID3D11Device> device_;
    winrt::com_ptr<ID3D11DeviceContext> context_;
    winrt::com_ptr<ID3D11Texture2D> texture_;
    winrt::com_ptr<ID3D11ShaderResourceView> view_;
    winrt::com_ptr<ID3D11RenderTargetView> target_;
    winrt::com_ptr<ID3D11VertexShader> vertex_;
    std::array<winrt::com_ptr<ID3D11PixelShader>, 4> shaders_;
    winrt::com_ptr<ID3D11SamplerState> sampler_;
    winrt::com_ptr<ID3D11Buffer> constants_;
    std::array<Query, 6> queries_;
    UINT width_{}, height_{}, inputWidth_{}, inputHeight_{}, constantWidth_{}, constantHeight_{};
    UpscaleMode effective_{UpscaleMode::Native};
    bool bypassed_{true};
    uint64_t generation_{}, allocations_{}, timingSamples_{};
    std::optional<double> gpuMs_;
    SourceRegion sourceRegion_{}, resultRegion_{};
    UINT textureWidth_{}, textureHeight_{};
};
