#include "GPUImageTestHelpers.h"
#include "DetailFixture.h"
#include "renderer/GPUCompositor.h"
#include <d3d11sdklayers.h>
#include <iostream>
int main() {
    try {
        InputGuide guide; Require(!guide.Target().width,"Guide default is not Off");
        guide.mode=GuideMode::HD720; Require(guide.Target()==SourceRegion{0,0,1280,720},"720p target");
        guide.mode=GuideMode::HD1080; Require(guide.Target()==SourceRegion{0,0,1920,1080},"1080p target");
        guide={GuideMode::Custom,1500,900}; Require(guide.Target()==SourceRegion{0,0,1500,900},"Custom target");
        Require(MatchTarget({0,0,1274,712},{0,0,1280,720})==TargetMatch::Good,"GOOD example");
        Require(MatchTarget({0,0,1210,680},{0,0,1280,720})==TargetMatch::Close,"CLOSE example");
        Require(MatchTarget({0,0,1530,860},{0,0,1280,720})==TargetMatch::OffTarget,"OFF TARGET example");
        Require(MatchTarget({0,0,1296,704},{0,0,1280,720})==TargetMatch::Good,"Inclusive 16px tolerance");
        Require(MatchTarget({0,0,1297,704},{0,0,1280,720})==TargetMatch::Close,"17px should not be GOOD");
        Require(Fit720p(1366,768)==SourceRegion{43,24,1280,720},"Larger centered fit");
        Require(Fit720p(1280,720)==SourceRegion{0,0,1280,720},"Exact fit");
        Require(Fit720p(640,480)==SourceRegion{0,60,640,360},"Small source must not upscale crop");
        Require(Fit720p(1920,1080)==SourceRegion{320,180,1280,720},"Large fit not centered");
        for(UINT w:{1u,15u,321u,640u,1366u,3840u,16384u}) for(UINT h:{1u,9u,243u,480u,843u,2160u}) {
            auto r=Fit720p(w,h);
            Require(r.width && r.height && r.x+r.width<=w && r.y+r.height<=h,"Fit exceeded frame");
            Require(std::abs(int(w-r.width)-int(r.x*2))<=1 && std::abs(int(h-r.height)-int(r.y*2))<=1,"Fit not centered");
            auto crop=PixelCrop(r,true);
            for(int edge=0;edge<4;++edge) for(int percent:{0,13,45,99}) {
                crop=AdjustCropEdge(w,h,crop,edge,percent); r=CropRegion(w,h,crop);
                Require(r.width && r.height && r.x+r.width<=w && r.y+r.height<=h,"Aspect adjustment exceeded frame");
                Require(std::abs(double(r.width)*9-r.height*16)<=16,"Aspect lock lost approximate 16:9");
            }
            r=CropRegion(std::max(1u,w/2),std::max(1u,h/2),crop);
            Require(r.x+r.width<=std::max(1u,w/2) && r.y+r.height<=std::max(1u,h/2),"Resize crop out of bounds");
        }
        Require(std::wstring(ProcessingInput({0,0,1274,712}))==L"~720p","Processing label");
        Require(std::wstring(ProcessingInput({0,0,1920,1080}))==L"~1080p","1080p processing label");
        Require(std::wstring(ProcessingInput({0,0,2560,1440}))==L"~1440p","1440p processing label");
        Require(RecommendedOutput({0,0,1280,720},2560,1440)==SourceRegion{0,0,2560,1440},"1440p recommendation");
        Require(RecommendedOutput({0,0,1920,1080},3840,2160)==SourceRegion{0,0,3840,2160},"4K recommendation");
        Require(!RecommendedOutput({0,0,1280,720},1920,1080).width,"Unexpected recommendation");
        com_ptr<ID3D11Device> device; com_ptr<ID3D11DeviceContext> context;
        auto create=[&](UINT flags){return D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,device.put(),nullptr,context.put());};
        auto hr=create(D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_DEBUG);
        if(hr==DXGI_ERROR_SDK_COMPONENT_MISSING) hr=create(D3D11_CREATE_DEVICE_BGRA_SUPPORT); check_hresult(hr);
        auto fixture=DetailFixture(); auto source=Make(device.get(),1280,720,fixture.data()); auto canvas=Make(device.get(),1280,720);
        GPUCompositor compositor; compositor.Initialize(device.get());
        const SourceRegion full{0,0,1280,720},crop{80,60,1120,600};
        auto draw=[&](bool edit,InputGuide g) {
            compositor.Draw(canvas.target.get(),1280,720,source.view.get(),full,source.view.get(),crop,{1280,720},{1280,720},CompareMode::Off,.5f,edit,g);
            return Read(device.get(),context.get(),canvas.view.get());
        };
        auto off=draw(true,{});
        for(auto g:{InputGuide{GuideMode::HD720},InputGuide{GuideMode::HD1080},InputGuide{GuideMode::Custom,900,500}}) {
            auto edit=draw(true,g); Require(Differences(edit,off)>100,"Guide overlay missing");
            Require(Read(device.get(),context.get(),source.view.get())==fixture,"Guide changed source pixels");
            auto clean=draw(false,g); Require(clean==fixture,"Guide persisted outside Crop Edit");
            auto name=g.mode==GuideMode::HD720?"guide-720.bmp":g.mode==GuideMode::HD1080?"guide-1080.bmp":"guide-custom.bmp";
            Save(name,1280,720,edit);
        }
        if(auto info=device.try_as<ID3D11InfoQueue>()) {
            for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
                SIZE_T size=0; check_hresult(info->GetMessage(i,nullptr,&size)); std::vector<char> bytes(size);
                auto message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data()); check_hresult(info->GetMessage(i,message,&size));
                Require(message->Severity>D3D11_MESSAGE_SEVERITY_WARNING,message->pDescription);
            }
            std::cout<<"PASS D3D11 guide debug layer: no warnings/errors\n";
        }
        std::cout<<"PASS guide targets, tolerance, fit/centering/small source, aspect lock/bounds, recommendations, GPU overlay isolation\n";
        return 0;
    } catch(hresult_error const& e) { std::wcerr<<e.message().c_str()<<L'\n'; }
    catch(std::exception const& e) { std::cerr<<e.what()<<'\n'; }
    return 1;
}
