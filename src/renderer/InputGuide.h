#pragma once
#include "VideoSettings.h"
#include <string>
enum class GuideMode { Off, HD720, HD1080, Custom };
struct InputGuide {
    GuideMode mode{GuideMode::Off};
    UINT customWidth{1280},customHeight{720};
    SourceRegion Target() const {
        switch(mode) {
        case GuideMode::HD720: return {0,0,1280,720};
        case GuideMode::HD1080: return {0,0,1920,1080};
        case GuideMode::Custom: return {0,0,std::clamp(customWidth,1u,16384u),std::clamp(customHeight,1u,16384u)};
        default: return {};
        }
    }
};
enum class TargetMatch { Good, Close, OffTarget };
inline TargetMatch MatchTarget(SourceRegion crop,SourceRegion target) {
    if(!crop.width || !crop.height || !target.width || !target.height) return TargetMatch::OffTarget;
    const auto dw=std::abs(int(crop.width)-int(target.width)),dh=std::abs(int(crop.height)-int(target.height));
    if(dw<=16 && dh<=16) return TargetMatch::Good;
    // Within 8% on BOTH axes, with a minimum tolerance of 32 pixels.
    if(dw<=std::max(32.,target.width*.08) && dh<=std::max(32.,target.height*.08)) return TargetMatch::Close;
    return TargetMatch::OffTarget;
}
inline const wchar_t* MatchName(TargetMatch match) {
    return match==TargetMatch::Good?L"GOOD":match==TargetMatch::Close?L"CLOSE":L"OFF TARGET";
}
inline const wchar_t* ProcessingInput(SourceRegion crop) {
    for(auto size:{SourceRegion{0,0,1280,720},SourceRegion{0,0,1920,1080},SourceRegion{0,0,2560,1440}})
        if(MatchTarget(crop,size)==TargetMatch::Good) return size.height==720?L"~720p":size.height==1080?L"~1080p":L"~1440p";
    return L"custom pixel size";
}
inline SourceRegion Fit720p(UINT width,UINT height) {
    if(!width || !height) return {};
    auto r=FitAspect16x9({0,0,std::min(width,1280u),std::min(height,720u)});
    return {(width-r.width)/2,(height-r.height)/2,r.width,r.height};
}
inline CropSettings PixelCrop(SourceRegion region,bool aspect16x9=false) {
    CropSettings c{}; c.manual=true; c.pixelCrop=true; c.aspect16x9=aspect16x9; c.pixels=region; return c;
}
inline CropSettings AdjustCropEdge(UINT width,UINT height,CropSettings crop,int edge,int percent) {
    if(!width || !height || edge<0 || edge>3) return crop;
    if(!crop.pixelCrop && !crop.aspect16x9) {
        int* values[]={&crop.left,&crop.top,&crop.right,&crop.bottom}; *values[edge]=std::clamp(percent,0,45); return crop;
    }
    auto r=CropRegion(width,height,crop);
    const UINT cut=UINT(std::lround((edge%2?height:width)*std::clamp(percent,0,99)/100.0));
    UINT left=r.x,top=r.y,right=r.x+r.width,bottom=r.y+r.height;
    if(edge==0) left=std::min(cut,right-1);
    if(edge==1) top=std::min(cut,bottom-1);
    if(edge==2) right=std::max(left+1,width-cut);
    if(edge==3) bottom=std::max(top+1,height-cut);
    r={left,top,right-left,bottom-top};
    if(crop.aspect16x9) {
        // The edited axis controls size; center the other axis and clamp inside
        // the frame. If it cannot fit, shrink to the largest fitting 16:9 box.
        if(edge%2==0) {
            r.height=std::min(height,std::max(1u,UINT(std::lround(r.width*9.0/16))));
            r.y=UINT(std::clamp(int(top+bottom)/2-int(r.height)/2,0,int(height-r.height)));
        } else {
            r.width=std::min(width,std::max(1u,UINT(std::lround(r.height*16.0/9))));
            r.x=UINT(std::clamp(int(left+right)/2-int(r.width)/2,0,int(width-r.width)));
        }
        r=FitAspect16x9(r);
    }
    return PixelCrop(r,crop.aspect16x9);
}
inline SourceRegion RecommendedOutput(SourceRegion crop,UINT monitorWidth,UINT monitorHeight) {
    if(MatchTarget(crop,{0,0,1280,720})==TargetMatch::Good && monitorWidth==2560 && monitorHeight==1440) return {0,0,2560,1440};
    if(MatchTarget(crop,{0,0,1920,1080})==TargetMatch::Good && monitorWidth==3840 && monitorHeight==2160) return {0,0,3840,2160};
    return {};
}
