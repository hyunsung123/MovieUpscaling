// Readback, staging, screenshots and CPU references are confined to tests.
#include "renderer/GPUUpscaler.h"
#include "renderer/RCASPass.h"
#include "renderer/GPUCompositor.h"
#include "renderer/GPUTimer.h"
#include "renderer/ShaderCompiler.h"
#include "utils/GPUInfo.h"
#include "GPUImageTestHelpers.h"
#include <d3d11sdklayers.h>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <stdexcept>
using namespace winrt;
namespace {
std::vector<Pixel> Pattern(UINT w,UINT h) {
    std::vector<Pixel> pixels(w*h);
    for(UINT y=0;y<h;++y) for(UINT x=0;x<w;++x) {
        int gray=static_cast<int>(128+70*std::sin(x*.17+y*.013));
        if(y>h/2) gray=(x%33 < 16)?70:195;
        if(x>w/2 && x<w*3/4) gray=(x>y*1.3+w*.28)?210:45;
        Pixel p{static_cast<unsigned char>(gray),static_cast<unsigned char>(gray),static_cast<unsigned char>(gray),255};
        if(x<24 || y<24 || x>=w-24 || y>=h-24) p={80,140,190,255};
        if(x<12) p={0,0,0,255}; if(x>=w-12) p={255,255,255,255};
        pixels[y*w+x]=p;
    }
    return pixels;
}
void Benchmark(ID3D11Device* device,ID3D11DeviceContext* context,Image& source,GPUUpscaler& upscale,RCASPass& rcas) {
    const char* generator=R"hlsl(
cbuffer Frame:register(b0){float phase;float3 pad;};
float4 VSMain(uint id:SV_VertexID):SV_Position {float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);}
float4 PSMain(float4 p:SV_Position):SV_Target {float2 t=p.xy+float2(phase,phase*.37);return float4(frac(t.x*.019),step(frac((t.x+t.y*.73)*.063),.5),.5+.5*sin(t.y*.18),1);}
)hlsl";
    auto vsCode=CompileShader(generator,"VSMain","vs_5_0"),psCode=CompileShader(generator,"PSMain","ps_5_0");
    com_ptr<ID3D11VertexShader> vs; com_ptr<ID3D11PixelShader> ps; com_ptr<ID3D11Buffer> constants;
    check_hresult(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,vs.put()));
    check_hresult(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,ps.put()));
    D3D11_BUFFER_DESC d{}; d.ByteWidth=16; d.BindFlags=D3D11_BIND_CONSTANT_BUFFER; check_hresult(device->CreateBuffer(&d,nullptr,constants.put()));
    GPUTimer timer; timer.Initialize(device); std::vector<double> up,sharp,total; uint64_t previous=0;
    for(int frame=0;frame<150;++frame) {
        float data[]={float(frame),0,0,0}; context->UpdateSubresource(constants.get(),0,nullptr,data,0,0);
        auto target=source.target.get(); context->OMSetRenderTargets(1,&target,nullptr);
        D3D11_VIEWPORT vp{0,0,1280,720,0,1}; context->RSSetViewports(1,&vp); context->RSSetState(nullptr);
        context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.get(),nullptr,0); context->PSSetShader(ps.get(),nullptr,0);
        auto cb=constants.get(); context->PSSetConstantBuffers(0,1,&cb); context->Draw(3,0); context->OMSetRenderTargets(0,nullptr,nullptr);
        timer.Begin();
        auto image=upscale.Process(source.view.get(),1280,720,3840,2160,UpscaleMode::Easu);
        timer.Split(); rcas.Process(image,upscale.ResultRegion(),25); timer.End();
        context->Flush(); Sleep(5); // Test-only offscreen submission; every source frame differs.
        if(frame>20 && timer.Samples()!=previous && timer.Value()) {
            auto sample=*timer.Value(); up.push_back(sample.first); sharp.push_back(sample.second); total.push_back(sample.total);
            Require(std::abs(sample.first+sample.second-sample.total)<.000001,"Unpaired GPU timing");
        }
        previous=timer.Samples();
    }
    Require(total.size()>40,"Missing completed GPU timings");
    auto median=[](std::vector<double> values){std::sort(values.begin(),values.end());return values[values.size()/2];};
    std::cout<<std::fixed<<std::setprecision(3)<<"Paired 720p -> 4K GPU medians: EASU="<<median(up)<<" ms RCAS25="<<median(sharp)<<" ms total="<<median(total)<<" ms (synthetic, excludes capture/Present)\n";
}
}
int main() {
    try {
        com_ptr<ID3D11Device> device; com_ptr<ID3D11DeviceContext> context;
        auto create=[&](UINT flags){return D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,device.put(),nullptr,context.put());};
        auto hr=create(D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_DEBUG);
        if(hr==DXGI_ERROR_SDK_COMPONENT_MISSING) hr=create(D3D11_CREATE_DEVICE_BGRA_SUPPORT); check_hresult(hr);
        std::wcout<<L"GPU: "<<GPUName(device.get())<<L'\n';
        auto pattern=Pattern(1280,720); auto source=Make(device.get(),1280,720,pattern.data());
        GPUUpscaler upscale; upscale.Initialize(device.get()); RCASPass rcas; rcas.Initialize(device.get());
        for(auto size:{UpscaleSize{1920,1080},UpscaleSize{2560,1440},UpscaleSize{3840,2160}}) {
            auto up=upscale.Process(source.view.get(),1280,720,size.width,size.height,UpscaleMode::Easu);
            auto easu=Read(device.get(),context.get(),up);
            Require(rcas.Process(up,upscale.ResultRegion(),0)==up,"RCAS0 failed to bypass");
            auto low=Read(device.get(),context.get(),rcas.Process(up,upscale.ResultRegion(),25)); auto allocations=rcas.Allocations();
            auto high=Read(device.get(),context.get(),rcas.Process(up,upscale.ResultRegion(),100));
            Require(rcas.Allocations()==allocations,"Strength change reallocated RCAS texture");
            Require(Differences(easu,low)>1000 && Differences(low,high)>1000,"RCAS strength does not affect detail");
            for(auto index:{size_t(0),size_t(size.width-1),size_t(size.width*8+size.width/2)})
                Require(high[index]==easu[index],"Constant black/white/color region changed");
            for(size_t i=0;i<high.size();i+=17) Require(high[i][3]==255,"Invalid RCAS alpha");
            Require(rcas.Process(up,upscale.ResultRegion(),0)==up && rcas.Allocations()==allocations,"Bypass reallocated resources");
            if(size.width==1920) { Save("easu-only.bmp",size.width,size.height,easu); Save("easu-rcas100.bmp",size.width,size.height,high); }
            std::cout<<"PASS RCAS: "<<size.width<<'x'<<size.height<<" bypass, strength, constants, resource reuse\n";
        }
        // Raw float output of the same RCAS shader detects NaN/Inf before UNORM conversion.
        RCASPass floating; floating.Initialize(device.get(),DXGI_FORMAT_R32G32B32A32_FLOAT);
        auto fp=Read<std::array<float,4>>(device.get(),context.get(),floating.Process(source.view.get(),{0,0,1280,720},100));
        for(auto p:fp) for(float c:p) Require(std::isfinite(c) && c>=0 && c<=1,"Nonfinite or out-of-range RCAS output");
        Require(CropRegion(1920,1080,{true,0,10,0,5})==SourceRegion{0,108,1920,918},"Percentage crop dimensions wrong");
        Require(CropRegion(1,1,{true,45,45,45,45})==SourceRegion{0,0,1,1},"Tiny crop became empty");
        for(auto setting:{CropSettings{},CropSettings{true,0,10,0,5},CropSettings{true,10,15,20,5},CropSettings{true,10,10,10,10}}) {
            auto region=CropRegion(1280,720,setting); std::vector<Pixel> reference(region.width*region.height);
            for(UINT y=0;y<region.height;++y) std::copy_n(pattern.data()+(region.y+y)*1280+region.x,region.width,reference.data()+y*region.width);
            auto cropped=Make(device.get(),region.width,region.height,reference.data());
            for(auto mode:{UpscaleMode::Native,UpscaleMode::Bilinear,UpscaleMode::Bicubic,UpscaleMode::Lanczos,UpscaleMode::Easu}) {
                auto size=FitOutput(region.width,region.height,1920,1080,mode);
                auto view=upscale.Process(source.view.get(),1280,720,size.width,size.height,mode,region);
                if(mode==UpscaleMode::Native) { Require(view==source.view.get() && upscale.ResultRegion()==region,"Native crop bypass lost coordinates"); continue; }
                auto actual=Read(device.get(),context.get(),view); auto allocations=upscale.Allocations();
                auto expected=Read(device.get(),context.get(),upscale.Process(cropped.view.get(),region.width,region.height,size.width,size.height,mode));
                Require(upscale.Allocations()==allocations,"Only source origin/size changed but output texture reallocated");
                for(size_t i=0;i<actual.size();i+=19) for(int c=0;c<4;++c)
                    Require(std::abs(int(actual[i][c])-int(expected[i][c]))<=1,"GPU sampling crop differs from reference crop");
            }
        }
        std::cout<<"PASS crop: off/top/asymmetric/16:9, all upscale modes, tiny frame, native bypass\n";
        auto up=upscale.Process(source.view.get(),1280,720,1920,1080,UpscaleMode::Easu);
        auto processed=rcas.Process(up,upscale.ResultRegion(),100); auto processedRegion=rcas.ResultRegion();
        auto processedPixels=Read(device.get(),context.get(),processed);
        auto baseline=Read(device.get(),context.get(),upscale.Process(source.view.get(),1280,720,1920,1080,UpscaleMode::Bilinear));
        GPUCompositor compositor; compositor.Initialize(device.get()); auto canvas=Make(device.get(),1920,1080);
        for(auto mode:{CompareMode::Vertical,CompareMode::Horizontal}) for(float split:{0.f,.25f,.5f,.75f,1.f}) {
            compositor.Draw(canvas.target.get(),1920,1080,processed,processedRegion,source.view.get(),{0,0,1280,720},{1920,1080},{1920,1080},mode,split,false);
            auto image=Read(device.get(),context.get(),canvas.view.get());
            for(UINT y=7;y<1080;y+=23) for(UINT x=7;x<1920;x+=29) {
                float axis=mode==CompareMode::Vertical ? (x+.5f)/1920 : (y+.5f)/1080;
                if(std::abs(axis-split)<.003f) continue;
                auto expected=(axis<split?baseline:processedPixels)[y*1920+x];
                for(int c=0;c<4;++c) Require(std::abs(int(image[y*1920+x][c])-int(expected[c]))<=1,"Split side / position mapping wrong");
            }
            if(split==.5f) Save(mode==CompareMode::Vertical?"split-vertical.bmp":"split-horizontal.bmp",1920,1080,image);
        }
        Require(Differences(baseline,processedPixels)>1000,"A/B halves do not differ");
        auto crop=CropRegion(1280,720,{true,10,20,15,10});
        compositor.Draw(canvas.target.get(),1920,1080,processed,processedRegion,source.view.get(),crop,{1920,1080},{1920,1080},CompareMode::Vertical,.5f,true);
        auto edit=Read(device.get(),context.get(),canvas.view.get()); Save("crop-edit.bmp",1920,1080,edit);
        // Outside is dark, inside is unchanged, cyan border. Overlay never modifies source.
        Require(edit[100*1920+960][0]<baseline[100*1920+960][0]/2,"Crop outside not darkened");
        Require(Read(device.get(),context.get(),source.view.get())==pattern,"Overlay modified source video");
        compositor.Draw(canvas.target.get(),1920,1080,processed,processedRegion,source.view.get(),crop,{1920,1080},{1920,1080},CompareMode::Off,.5f,false);
        auto restored=Read(device.get(),context.get(),canvas.view.get());
        Require(Differences(restored,processedPixels)<100,"Edit overlay persisted in processed output");
        std::cout<<"PASS compositor: vertical/horizontal, 0/25/50/75/100%, A/B pixel reference, edit overlay isolation\n";
        Benchmark(device.get(),context.get(),source,upscale,rcas);
        if(auto info=device.try_as<ID3D11InfoQueue>()) {
            for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
                SIZE_T size=0; check_hresult(info->GetMessage(i,nullptr,&size)); std::vector<char> bytes(size);
                auto message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data()); check_hresult(info->GetMessage(i,message,&size));
                if(message->Severity<=D3D11_MESSAGE_SEVERITY_WARNING) throw std::runtime_error(message->pDescription);
            }
            std::cout<<"PASS: D3D11 debug layer has no warnings/errors\n";
        }
        return 0;
    } catch(hresult_error const& e) { std::wcerr<<e.message().c_str()<<L'\n'; }
    catch(std::exception const& e) { std::cerr<<e.what()<<'\n'; }
    return 1;
}
