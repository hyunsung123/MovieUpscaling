#include "GPUUpscaler.h"
#include "ShaderCompiler.h"
#include "ShaderSource.h"
#include <algorithm>
#include <cmath>
using namespace winrt;
const wchar_t* UpscaleName(UpscaleMode mode) {
    switch (mode) {
    case UpscaleMode::Native: return L"Off / Native";
    case UpscaleMode::Bilinear: return L"Bilinear";
    case UpscaleMode::Bicubic: return L"Bicubic";
    case UpscaleMode::Lanczos: return L"Lanczos-2";
    case UpscaleMode::Easu: return L"FSR EASU";
    }
    return L"Unknown";
}
UpscaleSize FitOutput(UINT iw, UINT ih, UINT ow, UINT oh, UpscaleMode mode) {
    if (!iw || !ih || !ow || !oh) throw hresult_invalid_argument(L"Invalid upscale dimensions.");
    if (mode == UpscaleMode::Native) return {iw, ih};
    const double scale = std::min(double(ow) / iw, double(oh) / ih);
    return {std::clamp(static_cast<UINT>(std::lround(iw * scale)), 1u, ow),
        std::clamp(static_cast<UINT>(std::lround(ih * scale)), 1u, oh)};
}
void GPUUpscaler::Initialize(ID3D11Device* device) {
    device_.copy_from(device); device_->GetImmediateContext(context_.put());
    auto vs = CompileShader(PresentShader, "VSMain", "vs_5_0");
    check_hresult(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, vertex_.put()));
    const char* sources[] = {BilinearShader, BicubicShader, LanczosShader, EasuShader};
    for (size_t i = 0; i < shaders_.size(); ++i) {
        auto ps = CompileShader(sources[i], "PSMain", "ps_5_0");
        check_hresult(device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, shaders_[i].put()));
    }
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    check_hresult(device_->CreateSamplerState(&sampler, sampler_.put()));
    D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth = 32; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    check_hresult(device_->CreateBuffer(&buffer, nullptr, constants_.put()));
    for (auto& q : queries_) {
        D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        check_hresult(device_->CreateQuery(&desc, q.disjoint.put()));
        desc.Query = D3D11_QUERY_TIMESTAMP;
        check_hresult(device_->CreateQuery(&desc, q.begin.put()));
        check_hresult(device_->CreateQuery(&desc, q.end.put()));
    }
}
void GPUUpscaler::PollTiming() {
    // No polling loop, Flush, or GPU wait. Only read completed scalar query data.
    for (auto& q : queries_) if (q.pending) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{}; UINT64 begin{}, end{};
        auto ready = [&](ID3D11Query* query, void* data, UINT size) {
            HRESULT hr = context_->GetData(query, data, size, D3D11_ASYNC_GETDATA_DONOTFLUSH);
            check_hresult(hr); return hr == S_OK;
        };
        if (!ready(q.disjoint.get(), &disjoint, sizeof(disjoint)) || !ready(q.begin.get(), &begin, sizeof(begin)) ||
            !ready(q.end.get(), &end, sizeof(end))) continue;
        q.pending = false;
        if (q.generation == generation_ && !disjoint.Disjoint && disjoint.Frequency && end >= begin) {
            gpuMs_ = double(end - begin) * 1000.0 / double(disjoint.Frequency);
            ++timingSamples_;
        }
    }
}
ID3D11ShaderResourceView* GPUUpscaler::Process(ID3D11ShaderResourceView* source, UINT iw, UINT ih, UINT ow, UINT oh, UpscaleMode mode, SourceRegion region) {
    if (!iw || !ih || !ow || !oh) throw hresult_invalid_argument(L"Invalid upscale dimensions.");
    const UINT tw=iw, th=ih;
    if(!region.width || !region.height) region={0,0,iw,ih};
    if(region.x>=iw || region.y>=ih || region.width>iw-region.x || region.height>ih-region.y)
        throw hresult_invalid_argument(L"Crop exceeds source texture.");
    iw=region.width; ih=region.height;
    // EASU is an upsampler. Explicitly expose bilinear fallback for downscaling.
    auto effective = mode == UpscaleMode::Easu && (ow < iw || oh < ih) ? UpscaleMode::Bilinear : mode;
    if (inputWidth_ != iw || inputHeight_ != ih || constantWidth_ != ow || constantHeight_ != oh || effective_ != effective || sourceRegion_!=region || textureWidth_!=tw || textureHeight_!=th) {
        ++generation_; gpuMs_.reset(); timingSamples_ = 0;
        effective_ = effective;
        const float constants[] = {float(iw), float(ih), float(ow), float(oh),float(region.x),float(region.y),float(tw),float(th)};
        context_->UpdateSubresource(constants_.get(), 0, nullptr, constants, 0, 0);
        inputWidth_ = iw; inputHeight_ = ih; constantWidth_ = ow; constantHeight_ = oh;
        sourceRegion_=region; textureWidth_=tw; textureHeight_=th;
    }
    PollTiming();
    bypassed_ = mode == UpscaleMode::Native || (iw == ow && ih == oh);
    resultRegion_=bypassed_ ? region : SourceRegion{0,0,ow,oh};
    if (bypassed_) { gpuMs_.reset(); return source; }
    if (width_ != ow || height_ != oh) {
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        target_ = nullptr; view_ = nullptr; texture_ = nullptr;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = ow; desc.Height = oh; desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        check_hresult(device_->CreateTexture2D(&desc, nullptr, texture_.put()));
        check_hresult(device_->CreateShaderResourceView(texture_.get(), nullptr, view_.put()));
        check_hresult(device_->CreateRenderTargetView(texture_.get(), nullptr, target_.put()));
        width_ = ow; height_ = oh; ++allocations_;
    }
    context_->RSSetState(nullptr);
    D3D11_VIEWPORT viewport{0, 0, float(ow), float(oh), 0, 1};
    context_->RSSetViewports(1, &viewport);
    auto target = target_.get(); context_->OMSetRenderTargets(1, &target, nullptr);
    context_->IASetInputLayout(nullptr); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertex_.get(), nullptr, 0);
    context_->PSSetShader(shaders_[static_cast<size_t>(effective) - 1].get(), nullptr, 0);
    context_->PSSetShaderResources(0, 1, &source);
    auto sampler = sampler_.get(); context_->PSSetSamplers(0, 1, &sampler);
    auto constants = constants_.get(); context_->PSSetConstantBuffers(0, 1, &constants);
    Query* timing = nullptr;
    for (auto& q : queries_) if (!q.pending) { timing = &q; break; }
    if (timing) { context_->Begin(timing->disjoint.get()); context_->End(timing->begin.get()); }
    context_->Draw(3, 0);
    if (timing) {
        context_->End(timing->end.get()); context_->End(timing->disjoint.get());
        timing->pending = true; timing->generation = generation_;
    }
    ID3D11ShaderResourceView* none = nullptr; context_->PSSetShaderResources(0, 1, &none);
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    return view_.get();
}
