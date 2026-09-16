// Exercise the production renderer with ONE real WGC frame, then no capture.
// All readback is test-only and occurs before flip-discard invalidates contents.
#include "GPUImageTestHelpers.h"
#include "DetailFixture.h"
#include "renderer/D3DRenderer.h"
#include "capture/WindowCapture.h"
#include <windows.graphics.directx.direct3d11.interop.h>
#include <d3d11sdklayers.h>
#include <chrono>
#include <iostream>

struct RendererTestAccess {
    static std::vector<Pixel> DrawAndInspect(D3DRenderer& renderer) {
        // This is the exact function called by PresentFrame/Render/Redraw.
        renderer.DrawFrame();
        com_ptr<ID3D11Resource> resource; renderer.target_->GetResource(resource.put());
        auto texture=resource.as<ID3D11Texture2D>();
        D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d);
        d.Usage=D3D11_USAGE_STAGING; d.BindFlags=0; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        d.MiscFlags=0;
        com_ptr<ID3D11Texture2D> staging; check_hresult(renderer.device_->CreateTexture2D(&d,nullptr,staging.put()));
        renderer.context_->CopyResource(staging.get(),texture.get());
        D3D11_MAPPED_SUBRESOURCE map{}; check_hresult(renderer.context_->Map(staging.get(),0,D3D11_MAP_READ,0,&map));
        std::vector<Pixel> pixels(d.Width*d.Height);
        for(UINT y=0;y<d.Height;++y) std::memcpy(pixels.data()+y*d.Width,static_cast<char*>(map.pData)+y*map.RowPitch,d.Width*4);
        renderer.context_->Unmap(staging.get(),0);
        check_hresult(renderer.swapChain_->Present(1,0)); return pixels;
    }
};
namespace {
std::vector<Pixel> fixture;
LRESULT CALLBACK SourceProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps{}; auto dc=BeginPaint(hwnd,&ps);
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=1280; info.bmiHeader.biHeight=-720;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        SetDIBitsToDevice(dc,0,0,1280,720,0,0,0,720,fixture.data(),&info,DIB_RGB_COLORS);
        EndPaint(hwnd,&ps); return 0;
    }
    return DefWindowProcW(hwnd,msg,w,l);
}
double Difference(const std::vector<Pixel>& a,const std::vector<Pixel>& b) {
    double sum=0;
    // Detail panels only; identical mask for every strength.
    for(size_t i=0;i<2560*920;++i) sum+=std::abs(int(a[i][1])-int(b[i][1]));
    return sum/(2560*920);
}
}
int main() {
    HWND source{},preview{};
    try {
        init_apartment(apartment_type::single_threaded);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        SetEnvironmentVariableW(L"MOVIEUPSCALING_DEBUG_RCAS",nullptr);
        fixture=DetailFixture();
        WNDCLASSW cls{}; cls.lpfnWndProc=SourceProc; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"RCASPausedSource";
        RegisterClassW(&cls);
        source=CreateWindowW(cls.lpszClassName,L"RCAS runtime paused detail",WS_POPUP|WS_VISIBLE,20,20,1280,720,nullptr,nullptr,cls.hInstance,nullptr);
        cls.lpfnWndProc=DefWindowProcW; cls.lpszClassName=L"RCASRuntimePreview"; RegisterClassW(&cls);
        preview=CreateWindowW(cls.lpszClassName,L"RCAS runtime 1:1 validation",WS_POPUP|WS_VISIBLE,0,0,2560,1440,nullptr,nullptr,cls.hInstance,nullptr);
        Require(source && preview,"Runtime windows unavailable");
        D3DRenderer renderer; renderer.Initialize(preview,true);
        renderer.SetOutputMode(OutputMode::QHD); renderer.SetSharpen(0);
        Require(!renderer.Redraw(),"Redraw before first frame should be a no-op");
        WindowCapture capture; capture.Start(source,renderer.Device(),preview);
        UINT frames=0; uint64_t skipped=0;
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!frames && std::chrono::steady_clock::now()<deadline) {
            MSG msg{};
            while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            auto frame=capture.TakeLatest(skipped);
            if(frame) {
                auto size=frame.ContentSize();
                auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
                com_ptr<ID3D11Texture2D> texture; check_hresult(access->GetInterface(IID_PPV_ARGS(texture.put())));
                Require(size.Width==1280 && size.Height==720,"WGC fixture is not 720p");
                Require(renderer.Render(texture.get(),size.Width,size.Height),"Initial captured render failed");
                frame.Close(); ++frames;
            } else MsgWaitForMultipleObjectsEx(0,nullptr,16,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
        Require(frames==1,"WGC runtime frame timed out");
        capture.Stop(); // No further frames can mask a broken paused redraw.
        Require(!renderer.RCASAllocations(),"RCAS0 allocated a sharpening texture");
        std::vector<Pixel> baseline,sharpened;
        double mild=0,strong=0;
        for(int strength:{0,25,100,0,100}) {
            renderer.SetSharpen(strength);
            Require(renderer.Redraw(),"Paused GPU redraw failed");
            Require(!renderer.SettingsPending(),"Paused redraw did not apply settings");
            auto image=RendererTestAccess::DrawAndInspect(renderer);
            if(baseline.empty()) baseline=image;
            const double difference=Difference(image,baseline);
            if(!strength) Require(image==baseline,"F3-like zero restore changed the same captured frame");
            if(strength==25) { mild=difference; Require(mild>.2,"Actual swapchain RCAS25 is invisible"); }
            if(strength==100) {
                strong=difference; Require(strong>2 && strong>mild*2,"Actual swapchain received weak/pre-RCAS image");
                if(!sharpened.empty()) Require(image==sharpened,"Repeated paused RCAS output shimmered");
                sharpened=image;
            }
            if(strength) Require(renderer.RCASAllocations()==1,"Paused setting change reallocated RCAS");
            auto name="runtime-rcas-"+std::to_string(strength)+".bmp"; Save(name.c_str(),2560,1440,image);
        }
        renderer.SetCrop(PixelCrop({2,5,1274,712}));
        Require(renderer.FitInput720p(true),"Quick preset failed");
        Require(renderer.Crop().aspect16x9 && renderer.Crop().pixels==SourceRegion{2,5,1274,712},"Preset changed an already-good crop");
        Require(renderer.Guide().mode==GuideMode::HD720 && renderer.OutputSize()==UpscaleSize{2560,1440} && renderer.Mode()==UpscaleMode::Easu && renderer.Sharpen()==25,"Preset settings incorrect");
        Require(renderer.Redraw() && renderer.CroppedRegion()==SourceRegion{2,5,1274,712},"Preset did not preserve the processing region");
        renderer.SetCropEdit(true); renderer.Redraw();
        auto overlay=RendererTestAccess::DrawAndInspect(renderer); Save("guide-runtime-edit.bmp",2560,1440,overlay);
        renderer.SetCropEdit(false); renderer.Redraw();
        auto normal=RendererTestAccess::DrawAndInspect(renderer);
        renderer.SetInputGuide({}); renderer.Redraw();
        Require(normal==RendererTestAccess::DrawAndInspect(renderer),"Guide leaked into EASU/RCAS output");
        renderer.Clear(); Require(!renderer.Redraw(),"Stop/Clear retained a displayable stale frame");
        Require(!renderer.FitInput720p(true),"Preset used a cleared frame");
        com_ptr<ID3D11Device> device; device.copy_from(renderer.Device());
        if(auto info=device.try_as<ID3D11InfoQueue>()) {
            for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
                SIZE_T size=0; check_hresult(info->GetMessage(i,nullptr,&size)); std::vector<char> bytes(size);
                auto message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data()); check_hresult(info->GetMessage(i,message,&size));
                Require(message->Severity>D3D11_MESSAGE_SEVERITY_WARNING,message->pDescription);
            }
            std::cout<<"PASS runtime renderer D3D11 debug layer: no warnings/errors\n";
        }
        std::cout<<"PASS one WGC frame -> EASU -> RCAS -> actual 1440p swapchain: mean detail delta25="<<mild<<", delta100="<<strong<<" /255; paused redraw/reuse/clear\n";
        DestroyWindow(source); DestroyWindow(preview); return 0;
    } catch(hresult_error const& e) { std::wcerr<<e.message().c_str()<<L'\n'; }
    catch(std::exception const& e) { std::cerr<<e.what()<<'\n'; }
    if(source) DestroyWindow(source); if(preview) DestroyWindow(preview); return 1;
}
