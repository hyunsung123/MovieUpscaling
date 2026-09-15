// Production shaders and compositor, with readback confined to this test.
#include "GPUImageTestHelpers.h"
#include "DetailFixture.h"
#include "renderer/D3DRenderer.h"
#include "utils/GPUInfo.h"
#include <d3d11sdklayers.h>
#include <iostream>
#include <iomanip>

namespace {
constexpr UINT W=2560,H=1440;
double EdgeContrast(const std::vector<Pixel>& image,const std::vector<Pixel>& baseline) {
    // RMS of adjacent luma differences on a FIXED baseline edge mask. Excludes
    // flat/gradient panels; no per-pixel monotonicity assumption or moving mask.
    double sum=0; size_t count=0;
    for(UINT y=12;y<910;++y) for(UINT x=12;x<W-12;++x) {
        const size_t i=y*W+x;
        for(size_t j:{i+1,i+W}) {
            if(std::abs(int(baseline[i][1])-int(baseline[j][1]))<4) continue;
            const double difference=int(image[i][1])-int(image[j][1]);
            sum+=difference*difference; ++count;
        }
    }
    Require(count>10000,"Insufficient fixture edges"); return std::sqrt(sum/count);
}
double EdgeChange(const std::vector<Pixel>& image,const std::vector<Pixel>& baseline) {
    double sum=0; size_t count=0;
    for(UINT y=12;y<910;++y) for(UINT x=12;x<W-12;++x) {
        const size_t i=y*W+x;
        if(std::abs(int(baseline[i][1])-int(baseline[i+1][1]))<4 &&
           std::abs(int(baseline[i][1])-int(baseline[i+W][1]))<4) continue;
        sum+=std::abs(int(image[i][1])-int(baseline[i][1])); ++count;
    }
    return sum/count;
}
void DebugCheck(ID3D11Device* device) {
    com_ptr<ID3D11Device> owner; owner.copy_from(device);
    if(auto info=owner.try_as<ID3D11InfoQueue>()) {
        for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
            SIZE_T size=0; check_hresult(info->GetMessage(i,nullptr,&size)); std::vector<char> bytes(size);
            auto message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data()); check_hresult(info->GetMessage(i,message,&size));
            Require(message->Severity>D3D11_MESSAGE_SEVERITY_WARNING,message->pDescription);
        }
        std::cout<<"PASS D3D11 debug layer: no warnings/errors\n";
    }
}
}
int main() {
    try {
        SetEnvironmentVariableW(L"MOVIEUPSCALING_DEBUG_RCAS",nullptr);
        com_ptr<ID3D11Device> device; com_ptr<ID3D11DeviceContext> context;
        auto create=[&](UINT flags){return D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,device.put(),nullptr,context.put());};
        auto hr=create(D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_DEBUG);
        if(hr==DXGI_ERROR_SDK_COMPONENT_MISSING) hr=create(D3D11_CREATE_DEVICE_BGRA_SUPPORT);
        check_hresult(hr); std::wcout<<L"GPU: "<<GPUName(device.get())<<L'\n';
        const auto fixture=DetailFixture(); auto source=Make(device.get(),1280,720,fixture.data());
        Save("rcas-source.bmp",1280,720,fixture);
        GPUUpscaler upscale; upscale.Initialize(device.get()); RCASPass rcas; rcas.Initialize(device.get());
        Require(!rcas.DebugBoost(),"Debug boost enabled by default");
        auto up=upscale.Process(source.view.get(),1280,720,W,H,UpscaleMode::Easu);
        auto region=upscale.ResultRegion(); auto baseline=Read(device.get(),context.get(),up);
        GPUCompositor compositor; compositor.Initialize(device.get()); auto canvas=Make(device.get(),W,H);
        std::vector<Pixel> first,last; double previous=0;
        for(int strength:{0,25,50,100}) {
            auto view=rcas.Process(up,region,strength);
            if(!strength) Require(view==up && rcas.Allocations()==0,"Fresh bypass allocated or replaced input");
            else Require(rcas.Allocations()==1,"Strength reallocated output");
            // Explicit final path: pass the RETURNED RCAS SRV to presentation.
            compositor.Draw(canvas.target.get(),W,H,view,rcas.ResultRegion(),source.view.get(),{0,0,1280,720},{W,H},{W,H},CompareMode::Off,.5f,false);
            auto image=Read(device.get(),context.get(),canvas.view.get());
            auto direct=Read(device.get(),context.get(),view);
            Require(image==direct,"1:1 presentation changed or bypassed RCAS pixels");
            if(!strength) { Require(image==baseline,"RCAS0 is not exact identity through compositor"); first=image; }
            const double contrast=EdgeContrast(image,baseline),change=EdgeChange(image,baseline);
            std::cout<<"RCAS "<<strength<<": edge RMS="<<contrast<<" mean edge change="<<change<<" /255\n";
            if(strength) Require(contrast>previous*1.02,"Sharpen did not meaningfully increase edge contrast");
            if(strength==25) Require(change>.35 && change<5,"25 is not mild but visible");
            if(strength==100) {
                Require(change>=3 && contrast>EdgeContrast(baseline,baseline)*1.12,"100 is perceptually too weak");
                Require(contrast<EdgeContrast(baseline,baseline)*2.5,"Edge contrast/ringing explosion");
                last=image;
            }
            previous=contrast;
            for(UINT y=1280;y<H-16;++y) for(UINT x=32;x<W-32;++x) {
                if(x%640<16 || x%640>624) continue;
                Require(image[y*W+x]==baseline[y*W+x],"Flat color changed");
            }
            // Smooth ramps should not acquire ringing/banding; tolerate 1 UNORM step.
            for(UINT y=1000;y<1170;y+=7) for(UINT x=32;x<W-32;++x)
                Require(std::abs(int(image[y*W+x][1])-int(baseline[y*W+x][1]))<=1,"Smooth gradient damaged");
            // Bound overshoot against the local five-tap envelope, as a TEST
            // criterion only. No extra local clamp exists in the production shader.
            int maxOvershoot=0;
            for(UINT y=1;y<H-1;++y) for(UINT x=1;x<W-1;++x) {
                const size_t i=y*W+x; int lo=255,hi=0;
                for(size_t j:{i,i-1,i+1,i-W,i+W}) {lo=std::min(lo,int(baseline[j][1]));hi=std::max(hi,int(baseline[j][1]));}
                maxOvershoot=std::max({maxOvershoot,lo-image[i][1],image[i][1]-hi});
            }
            std::cout<<"  maximum local overshoot="<<maxOvershoot<<" /255\n";
            Require(maxOvershoot<=40,"Severe local ringing in normal RCAS");
            const auto name="rcas-"+std::to_string(strength)+".bmp"; Save(name.c_str(),W,H,image);
        }
        Require(EdgeChange(last,first)>=3,"Compositor discarded RCAS output");
        compositor.Draw(canvas.target.get(),W,H,rcas.Process(up,region,100),region,up,region,{W,H},{W,H},CompareMode::Vertical,.5f,false);
        Save("rcas-split.bmp",W,H,Read(device.get(),context.get(),canvas.view.get()));
        Require(Read(device.get(),context.get(),up)==baseline,"RCAS modified EASU source");
        // Quantify final-stage softening separately. A 4K intermediate displayed
        // on a 1440p Preview is bilinearly downsampled, not a 1:1 RCAS comparison.
        up=upscale.Process(source.view.get(),1280,720,3840,2160,UpscaleMode::Easu); region=upscale.ResultRegion();
        std::vector<Pixel> downBaseline;
        for(int strength:{0,100}) {
            auto view=rcas.Process(up,region,strength);
            compositor.Draw(canvas.target.get(),W,H,view,rcas.ResultRegion(),source.view.get(),{0,0,1280,720},{3840,2160},{3840,2160},CompareMode::Off,.5f,false);
            auto image=Read(device.get(),context.get(),canvas.view.get());
            if(!strength) downBaseline=image;
            else {
                const double change=EdgeChange(image,downBaseline);
                Require(change>.1,"4K->1440p compositor lost sharpening entirely");
                std::cout<<"4K -> 1440p mean edge change="<<change<<" (1:1="<<EdgeChange(last,first)<<")\n";
                Save("rcas-4k-to-1440p.bmp",W,H,image);
            }
        }
        // Raw float results expose NaN/Inf hidden by UNORM conversion. Debug
        // boost must differ, remain bounded, preserve flats, and still bypass 0.
        up=upscale.Process(source.view.get(),1280,720,W,H,UpscaleMode::Easu); region=upscale.ResultRegion();
        for(bool debug:{false,true}) {
            SetEnvironmentVariableW(L"MOVIEUPSCALING_DEBUG_RCAS",debug?L"1":nullptr);
            RCASPass floating; floating.Initialize(device.get(),DXGI_FORMAT_R32G32B32A32_FLOAT);
            Require(floating.DebugBoost()==debug,"Debug environment gate failed");
            Require(floating.Process(up,region,0)==up && !floating.Allocations(),"Debug bypass allocated/drew");
            for(int strength:{25,50,100}) {
                auto fp=Read<std::array<float,4>>(device.get(),context.get(),floating.Process(up,region,strength));
                for(auto p:fp) for(float c:p) Require(std::isfinite(c) && c>=0 && c<=1,"Nonfinite/out-of-range float RCAS");
                for(UINT x=32;x<W-32;++x) if(x%640>=16 && x%640<=624) {
                    const size_t i=1300*W+x;
                    for(int c=0;c<3;++c) Require(std::abs(fp[i][c]-baseline[i][2-c]/255.f)<.00001,"Float flat stability failed");
                }
            }
            if(debug) {
                RCASPass boosted; boosted.Initialize(device.get());
                auto image=Read(device.get(),context.get(),boosted.Process(up,region,100));
                Require(EdgeChange(image,first)>EdgeChange(last,first)*1.4,"Diagnostic boost is too subtle");
                Save("rcas-debug-100.bmp",W,H,image);
            }
        }
        SetEnvironmentVariableW(L"MOVIEUPSCALING_DEBUG_RCAS",nullptr);
        DebugCheck(device.get());
        std::cout<<"PASS RCAS perceptual contrast, compositor routing, bypass/reuse, flats/ramps, ringing, float and debug boost\n";
        return 0;
    } catch(hresult_error const& e) { std::wcerr<<e.message().c_str()<<L'\n'; }
    catch(std::exception const& e) { std::cerr<<e.what()<<'\n'; }
    return 1;
}
