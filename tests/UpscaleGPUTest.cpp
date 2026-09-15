// GPU filter validation. CPU pixels / staging resources exist ONLY in tests.
#include "renderer/GPUUpscaler.h"
#include "renderer/ShaderCompiler.h"
#include "utils/GPUInfo.h"
#include <d3d11sdklayers.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <vector>
#include <stdexcept>
using namespace winrt;
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
constexpr UINT IW = 1280, IH = 720;
using Pixel = std::array<unsigned char, 4>; // BGRA
struct Fixture {
    com_ptr<ID3D11Device> device;
    com_ptr<ID3D11DeviceContext> context;
    com_ptr<ID3D11Texture2D> source;
    com_ptr<ID3D11ShaderResourceView> srv;
    std::vector<Pixel> pixels;
    Fixture() {
        auto create = [&](UINT flags) { return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            nullptr, 0, D3D11_SDK_VERSION, device.put(), nullptr, context.put()); };
        HRESULT hr = create(D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG);
        if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) hr = create(D3D11_CREATE_DEVICE_BGRA_SUPPORT);
        check_hresult(hr);
        pixels.resize(IW * IH);
        for (UINT y = 0; y < IH; ++y) for (UINT x = 0; x < IW; ++x) {
            // Slanted edges, high frequency detail, color ramps, flat border.
            unsigned char g = static_cast<unsigned char>((x * 19 + y * 7) % 200 + 20);
            Pixel p{g, static_cast<unsigned char>((x > y * 1.37 + 250) ? 220 : 35), static_cast<unsigned char>((x + y) % 256), 255};
            if (x < 20 || y < 20 || x >= IW - 20 || y >= IH - 20) p = {65, 170, 35, 255};
            pixels[y * IW + x] = p;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = IW; desc.Height = IH; desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        D3D11_SUBRESOURCE_DATA data{pixels.data(), IW * 4, 0};
        check_hresult(device->CreateTexture2D(&desc, &data, source.put()));
        check_hresult(device->CreateShaderResourceView(source.get(), nullptr, srv.put()));
    }
    std::vector<Pixel> Read(ID3D11ShaderResourceView* view, UINT width, UINT height) {
        com_ptr<ID3D11Resource> resource; view->GetResource(resource.put());
        auto texture = resource.as<ID3D11Texture2D>();
        D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
        Require(desc.Width == width && desc.Height == height, "GPU output dimensions differ from requested dimensions");
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        com_ptr<ID3D11Texture2D> staging; check_hresult(device->CreateTexture2D(&desc, nullptr, staging.put()));
        context->CopyResource(staging.get(), texture.get());
        D3D11_MAPPED_SUBRESOURCE map{}; check_hresult(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &map));
        std::vector<Pixel> result(width * height);
        for (UINT y = 0; y < height; ++y) std::memcpy(result.data() + y * width, static_cast<char*>(map.pData) + y * map.RowPitch, width * 4);
        context->Unmap(staging.get(), 0); return result;
    }
    Pixel At(int x, int y) const { return pixels[std::clamp(y, 0, int(IH) - 1) * IW + std::clamp(x, 0, int(IW) - 1)]; }
};
double Kernel(double x, UpscaleMode mode) {
    x = std::abs(x);
    if (mode == UpscaleMode::Bilinear) return std::max(0.0, 1 - x);
    if (mode == UpscaleMode::Bicubic) {
        if (x < 1) return 1.5*x*x*x - 2.5*x*x + 1;
        if (x < 2) return -.5*x*x*x + 2.5*x*x - 4*x + 2;
        return 0;
    }
    if (x < 1e-8) return 1;
    if (x >= 2) return 0;
    const double t = 3.14159265358979323846*x;
    return std::sin(t) * std::sin(t/2) / (t*t/2);
}
void ValidatePixels(Fixture const& f, std::vector<Pixel> const& image, UINT ow, UINT oh, UpscaleMode mode) {
    // Independent double-precision separable convolution for cubic/sinc filters;
    // EASU must respect local 2x2 bounds even on saturated diagonal edges.
    for (UINT sample = 0; sample < 1600; ++sample) {
        UINT x = (sample * 7919u) % ow, y = (sample * 3571u) % oh;
        double sx = (x + .5) * IW / ow - .5, sy = (y + .5) * IH / oh - .5;
        int ix = static_cast<int>(std::floor(sx)), iy = static_cast<int>(std::floor(sy));
        auto actual = image[y * ow + x]; Require(actual[3] == 255, "Non-opaque output");
        for (int channel = 0; channel < 3; ++channel) {
            if (mode == UpscaleMode::Easu) {
                int lo = 255, hi = 0;
                for (int j = 0; j <= 1; ++j) for (int i = 0; i <= 1; ++i) {
                    int c = f.At(ix+i, iy+j)[channel]; lo = std::min(lo,c); hi = std::max(hi,c);
                }
                if (actual[channel] < lo - 1 || actual[channel] > hi + 1) {
                    std::cerr << "EASU bounds at " << x << ',' << y << " source " << std::setprecision(12) << sx << ',' << sy
                        << " channel=" << channel << " actual=" << int(actual[channel]) << " bounds=" << lo << ',' << hi << '\n';
                    throw std::runtime_error("EASU ringing / wrong sampling neighborhood");
                }
            } else {
                double sum = 0, weights = 0;
                for (int j = -1; j <= 2; ++j) for (int i = -1; i <= 2; ++i) {
                    double w = Kernel(sx-ix-i, mode) * Kernel(sy-iy-j, mode);
                    sum += f.At(ix+i,iy+j)[channel] * w; weights += w;
                }
                double expected = std::clamp(sum / weights, 0.0, 255.0);
                Require(std::abs(actual[channel] - expected) <= 1.6, "Filter differs from mathematical reference");
            }
        }
    }
    Require(image[0] == Pixel{65,170,35,255} && image.back() == Pixel{65,170,35,255}, "Border clamp / constant color failed");
}
void Benchmark(Fixture& f, GPUUpscaler& scaler) {
    // GPU-generated moving detailed source: every benchmark frame is distinct.
    // This measures ONLY the upscale draw; no WGC/Present/CPU readback in loop.
    const char* generator = R"hlsl(
cbuffer Frame : register(b0) { float phase; float3 padding; };
float4 VSMain(uint id:SV_VertexID):SV_Position { float2 p=float2((id<<1)&2,id&2); return float4(p*float2(2,-2)+float2(-1,1),0,1); }
float4 PSMain(float4 p:SV_Position):SV_Target {
 float2 t=p.xy+float2(phase,phase*.37);
 return float4(frac(t.x*.019), step(frac((t.x+t.y*.73)*.063),.5), .5+.5*sin(t.y*.18),1);
})hlsl";
    auto vsCode = CompileShader(generator,"VSMain","vs_5_0"), psCode = CompileShader(generator,"PSMain","ps_5_0");
    com_ptr<ID3D11VertexShader> vs; com_ptr<ID3D11PixelShader> ps;
    check_hresult(f.device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,vs.put()));
    check_hresult(f.device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,ps.put()));
    com_ptr<ID3D11RenderTargetView> target; check_hresult(f.device->CreateRenderTargetView(f.source.get(),nullptr,target.put()));
    D3D11_BUFFER_DESC desc{}; desc.ByteWidth=16; desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    com_ptr<ID3D11Buffer> constants; check_hresult(f.device->CreateBuffer(&desc,nullptr,constants.put()));
    std::vector<double> times;
    uint64_t lastSamples=0;
    for (int frame=0; frame<180; ++frame) {
        float data[4]={float(frame),0,0,0}; f.context->UpdateSubresource(constants.get(),0,nullptr,data,0,0);
        auto rt=target.get(); f.context->OMSetRenderTargets(1,&rt,nullptr);
        D3D11_VIEWPORT viewport{0,0,float(IW),float(IH),0,1}; f.context->RSSetViewports(1,&viewport);
        f.context->VSSetShader(vs.get(),nullptr,0); f.context->PSSetShader(ps.get(),nullptr,0);
        auto cb=constants.get(); f.context->PSSetConstantBuffers(0,1,&cb);
        f.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); f.context->Draw(3,0);
        f.context->OMSetRenderTargets(0,nullptr,nullptr);
        scaler.Process(f.srv.get(),IW,IH,3840,2160,UpscaleMode::Easu);
        f.context->Flush(); // Test harness submits offscreen work; production uses Present.
        if (frame>20 && scaler.TimingSamples()!=lastSamples && scaler.GPUTimeMs()) times.push_back(*scaler.GPUTimeMs());
        lastSamples=scaler.TimingSamples();
        Sleep(3); // Test pacing only, no query busy wait.
    }
    Require(times.size()>30,"Insufficient completed, non-disjoint GPU timestamps");
    std::sort(times.begin(),times.end());
    std::cout << std::fixed << std::setprecision(3) << "EASU 1280x720 -> 3840x2160 GPU draw: median=" << times[times.size()/2]
        << " ms, p95=" << times[times.size()*95/100] << " ms; samples=" << times.size() << " (synthetic; not end-to-end FPS)\n";
}
}
int main() {
    try {
        Fixture f; GPUUpscaler scaler; scaler.Initialize(f.device.get());
        std::wcout << L"GPU: " << GPUName(f.device.get()) << L'\n';
        for (auto output : {UpscaleSize{1920,1080}, UpscaleSize{2560,1440}, UpscaleSize{3840,2160}}) {
            std::array<std::vector<Pixel>,4> results;
            auto native = scaler.Process(f.srv.get(),IW,IH,IW,IH,UpscaleMode::Native);
            Require(native == f.srv.get() && scaler.Bypassed(), "Native allocated/resampled a frame");
            for (int mode=1; mode<=4; ++mode) {
                auto before=scaler.Allocations();
                auto view=scaler.Process(f.srv.get(),IW,IH,output.width,output.height,static_cast<UpscaleMode>(mode));
                if(mode>1) Require(before==scaler.Allocations(),"Mode switch reallocated output texture");
                results[mode-1]=f.Read(view,output.width,output.height);
                ValidatePixels(f,results[mode-1],output.width,output.height,static_cast<UpscaleMode>(mode));
            }
            for (int a=0;a<4;++a) for (int b=a+1;b<4;++b) {
                size_t differences=0;
                for(size_t i=0;i<results[a].size();i+=13) if(results[a][i]!=results[b][i]) ++differences;
                Require(differences>1000,"Two upscale modes produced indistinguishable images");
            }
            std::cout << "PASS: 720p -> " << output.width << 'x' << output.height << "; Native, 4 filters, math reference, EASU bounds, mode differences, texture reuse\n";
        }
        auto fit=FitOutput(640,480,3840,2160,UpscaleMode::Easu); Require(fit.width==2880 && fit.height==2160,"Aspect ratio failed");
        auto narrow=FitOutput(1,720,1920,1080,UpscaleMode::Easu); Require(narrow.width>=1,"Narrow source failed");
        auto before=scaler.Allocations();
        for(int mode=0;mode<5;++mode) Require(scaler.Process(f.srv.get(),IW,IH,IW,IH,static_cast<UpscaleMode>(mode))==f.srv.get(),"1:1 bypass failed");
        Require(before==scaler.Allocations(),"1:1 bypass allocated output");
        scaler.Process(f.srv.get(),IW,IH,640,360,UpscaleMode::Easu);
        Require(scaler.EffectiveMode()==UpscaleMode::Bilinear,"EASU downscale fallback not exposed");
        Benchmark(f,scaler);
        if(auto info=f.device.try_as<ID3D11InfoQueue>()) {
            for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
                SIZE_T size=0; check_hresult(info->GetMessage(i,nullptr,&size)); std::vector<char> bytes(size);
                auto message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data()); check_hresult(info->GetMessage(i,message,&size));
                if(message->Severity<=D3D11_MESSAGE_SEVERITY_WARNING) throw std::runtime_error(message->pDescription);
            }
            std::cout << "PASS: D3D11 debug layer has no warnings/errors\n";
        } else std::cout << "D3D11 debug layer unavailable; validation used hardware device\n";
        return 0;
    } catch(hresult_error const& e) { std::wcerr << e.message().c_str() << L'\n'; }
    catch(std::exception const& e) { std::cerr << e.what() << '\n'; }
    return 1;
}
