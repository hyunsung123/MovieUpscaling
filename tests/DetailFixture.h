#pragma once
#include <windows.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>

// A deterministic paused 720p chart: antialiased text, diagonal hairlines,
// one/two-pixel features, checker detail, gradients and flat SDR color patches.
// GDI and CPU filtering are TEST ONLY; the app captures this like any window.
inline std::vector<std::array<unsigned char,4>> DetailFixture() {
    constexpr int w=1280,h=720;
    using Pixel=std::array<unsigned char,4>;
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=w; info.bmiHeader.biHeight=-h;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    void* bits{}; auto dc=CreateCompatibleDC(nullptr);
    auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!dc || !bitmap) throw std::runtime_error("Detail fixture GDI allocation failed");
    auto oldBitmap=SelectObject(dc,bitmap); auto pixels=static_cast<Pixel*>(bits);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        int gray=32;
        if(x>=640 && y<460) {
            if(y<220) gray=(std::abs(std::remainder(x-y*.31,11.0))<((x/160)%2?1.:.5))?218:55;
            else { const int cell=2+(x-640)/160*2; gray=((x/cell+y/cell)%2)?205:50; }
        }
        if(y>=480 && y<600) gray=24+x*208/(w-1);
        pixels[y*w+x]={static_cast<unsigned char>(gray),static_cast<unsigned char>(gray),static_cast<unsigned char>(gray),255};
        if(y>=620) {
            const Pixel colors[]={{0,0,0,255},{255,255,255,255},{128,128,128,255},{64,128,192,255}};
            pixels[y*w+x]=colors[x/320];
        }
    }
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(240,240,240));
    for(int row=0;row<8;++row) {
        auto font=CreateFontW(18+row*3,0,0,0,row%2?FW_NORMAL:FW_BOLD,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");
        auto oldFont=SelectObject(dc,font);
        const wchar_t* line=row%2?L"Subtitle: fine detail 0123456789":L"RCAS  Text / edges / Aa Bb Ee";
        TextOutW(dc,28,20+row*54,line,lstrlenW(line));
        SelectObject(dc,oldFont); DeleteObject(font);
    }
    GdiFlush();
    std::vector<Pixel> raw(pixels,pixels+w*h),out=raw;
    // Mild optical softness, to exercise reconstructable edges instead of
    // only saturated 0/1 steps which RCAS correctly protects from overshoot.
    for(int y=1;y<h-1;++y) for(int x=1;x<w-1;++x) for(int c=0;c<3;++c) {
        int sum=0; const int weights[]={1,2,1};
        for(int j=-1;j<=1;++j) for(int i=-1;i<=1;++i) sum+=weights[i+1]*weights[j+1]*raw[(y+j)*w+x+i][c];
        out[y*w+x][c]=static_cast<unsigned char>((sum+8)/16);
    }
    for(auto& p:out) p[3]=255;
    SelectObject(dc,oldBitmap); DeleteObject(bitmap); DeleteDC(dc); return out;
}
