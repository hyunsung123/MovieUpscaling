#include "RCASPass.h"
#include "ShaderCompiler.h"
#include "ShaderSource.h"
#include <cmath>
using namespace winrt;
void RCASPass::Initialize(ID3D11Device* device,DXGI_FORMAT format) {
    wchar_t debug[2]{};
    debugBoost_=GetEnvironmentVariableW(L"MOVIEUPSCALING_DEBUG_RCAS",debug,2)==1 && debug[0]==L'1';
    device_.copy_from(device); device->GetImmediateContext(context_.put()); format_=format;
    auto vs=CompileShader(RcasShader,"VSMain","vs_5_0"), ps=CompileShader(RcasShader,"PSMain","ps_5_0");
    check_hresult(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,vertex_.put()));
    check_hresult(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,pixel_.put()));
    D3D11_BUFFER_DESC b{}; b.ByteWidth=32; b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    check_hresult(device->CreateBuffer(&b,nullptr,dimensions_.put())); b.ByteWidth=16;
    check_hresult(device->CreateBuffer(&b,nullptr,strength_.put()));
}
ID3D11ShaderResourceView* RCASPass::Process(ID3D11ShaderResourceView* source,SourceRegion region,int strength) {
    strength=std::clamp(strength,0,100);
    result_=region;
    if(!strength) return source;
    if(!region.width || !region.height) throw hresult_invalid_argument(L"Empty RCAS region.");
    if(width_!=region.width || height_!=region.height) {
        context_->OMSetRenderTargets(0,nullptr,nullptr); target_=nullptr; view_=nullptr; texture_=nullptr;
        D3D11_TEXTURE2D_DESC d{}; d.Width=region.width; d.Height=region.height;
        d.MipLevels=d.ArraySize=d.SampleDesc.Count=1; d.Format=format_;
        d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        check_hresult(device_->CreateTexture2D(&d,nullptr,texture_.put()));
        check_hresult(device_->CreateRenderTargetView(texture_.get(),nullptr,target_.put()));
        check_hresult(device_->CreateShaderResourceView(texture_.get(),nullptr,view_.put()));
        width_=region.width; height_=region.height; ++allocations_;
    }
    const float dimensions[]={float(region.width),float(region.height),float(region.width),float(region.height),float(region.x),float(region.y),0,0};
    // Reach AMD's full-strength RCAS at 100. The nonlinear curve gives the
    // everyday 20-40 range useful gain; retain some noise attenuation even at
    // the deliberately aggressive end to constrain isolated thin-line ringing.
    const float t=strength/100.f;
    const float sharp[]={std::pow(t,.65f)*(debugBoost_?2.f:1.f),.5f-.15f*t,debugBoost_?-.23f:-.1875f,0};
    context_->UpdateSubresource(dimensions_.get(),0,nullptr,dimensions,0,0);
    context_->UpdateSubresource(strength_.get(),0,nullptr,sharp,0,0);
    context_->RSSetState(nullptr);
    D3D11_VIEWPORT vp{0,0,float(width_),float(height_),0,1}; context_->RSSetViewports(1,&vp);
    auto target=target_.get(); context_->OMSetRenderTargets(1,&target,nullptr);
    context_->IASetInputLayout(nullptr); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertex_.get(),nullptr,0); context_->PSSetShader(pixel_.get(),nullptr,0);
    context_->PSSetShaderResources(0,1,&source);
    ID3D11Buffer* constants[]={dimensions_.get(),strength_.get()}; context_->PSSetConstantBuffers(0,2,constants);
    context_->Draw(3,0);
    ID3D11ShaderResourceView* none=nullptr; context_->PSSetShaderResources(0,1,&none); context_->OMSetRenderTargets(0,nullptr,nullptr);
    result_={0,0,width_,height_}; return view_.get();
}
