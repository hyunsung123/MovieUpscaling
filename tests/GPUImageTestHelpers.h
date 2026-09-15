#pragma once
#include <windows.h>
#include <d3d11.h>
#include <winrt/base.h>
#include <array>
#include <vector>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstring>
namespace {
using namespace winrt;
using Pixel=std::array<unsigned char,4>;
void Require(bool condition,const char* text) { if(!condition) throw std::runtime_error(text); }
struct Image {
    com_ptr<ID3D11Texture2D> texture;
    com_ptr<ID3D11ShaderResourceView> view;
    com_ptr<ID3D11RenderTargetView> target;
};
Image Make(ID3D11Device* device,UINT width,UINT height,const Pixel* pixels=nullptr) {
    Image image; D3D11_TEXTURE2D_DESC d{}; d.Width=width; d.Height=height;
    d.MipLevels=d.ArraySize=d.SampleDesc.Count=1; d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    D3D11_SUBRESOURCE_DATA data{pixels,width*4,0};
    check_hresult(device->CreateTexture2D(&d,pixels?&data:nullptr,image.texture.put()));
    check_hresult(device->CreateShaderResourceView(image.texture.get(),nullptr,image.view.put()));
    check_hresult(device->CreateRenderTargetView(image.texture.get(),nullptr,image.target.put())); return image;
}
template<class T=Pixel> std::vector<T> Read(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11ShaderResourceView* view) {
    com_ptr<ID3D11Resource> resource; view->GetResource(resource.put()); auto texture=resource.as<ID3D11Texture2D>();
    D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d); d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    com_ptr<ID3D11Texture2D> staging; check_hresult(device->CreateTexture2D(&d,nullptr,staging.put()));
    context->CopyResource(staging.get(),texture.get());
    D3D11_MAPPED_SUBRESOURCE map{}; check_hresult(context->Map(staging.get(),0,D3D11_MAP_READ,0,&map));
    std::vector<T> out(d.Width*d.Height);
    for(UINT y=0;y<d.Height;++y) std::memcpy(out.data()+y*d.Width,static_cast<char*>(map.pData)+y*map.RowPitch,d.Width*sizeof(T));
    context->Unmap(staging.get(),0); return out;
}
void Save(const char* name,UINT width,UINT height,const std::vector<Pixel>& pixels) {
    std::filesystem::create_directories("validation"); std::ofstream file(std::string("validation/")+name,std::ios::binary);
    BITMAPFILEHEADER b{}; b.bfType=0x4d42; b.bfOffBits=sizeof(b)+sizeof(BITMAPINFOHEADER); b.bfSize=b.bfOffBits+width*height*4;
    BITMAPINFOHEADER info{}; info.biSize=sizeof(info); info.biWidth=width; info.biHeight=-static_cast<LONG>(height);
    info.biPlanes=1; info.biBitCount=32; info.biCompression=BI_RGB;
    file.write(reinterpret_cast<char*>(&b),sizeof(b)); file.write(reinterpret_cast<char*>(&info),sizeof(info));
    file.write(reinterpret_cast<const char*>(pixels.data()),width*height*4);
    Require(bool(file),"Validation BMP write failed");
}
size_t Differences(const std::vector<Pixel>& a,const std::vector<Pixel>& b) {
    Require(a.size()==b.size(),"Different image sizes"); size_t changed=0;
    for(size_t i=0;i<a.size();++i) if(a[i]!=b[i]) ++changed; return changed;
}
}
