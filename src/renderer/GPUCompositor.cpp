#include "GPUCompositor.h"
#include "ShaderCompiler.h"
#include "ShaderSource.h"
using namespace winrt;
void GPUCompositor::Initialize(ID3D11Device* device) {
    device->GetImmediateContext(context_.put());
    auto vs=CompileShader(CompositeShader,"VSMain","vs_5_0"), ps=CompileShader(CompositeShader,"PSMain","ps_5_0");
    check_hresult(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,vertex_.put()));
    check_hresult(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,pixel_.put()));
    D3D11_SAMPLER_DESC s{}; s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; s.MaxLOD=D3D11_FLOAT32_MAX;
    check_hresult(device->CreateSamplerState(&s,sampler_.put()));
    D3D11_BUFFER_DESC b{}; b.ByteWidth=144; b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    check_hresult(device->CreateBuffer(&b,nullptr,constants_.put()));
}
void GPUCompositor::Draw(ID3D11RenderTargetView* target,UINT width,UINT height,
    ID3D11ShaderResourceView* processed,SourceRegion processedRegion,ID3D11ShaderResourceView* source,SourceRegion crop,
    UpscaleSize canvas,UpscaleSize content,CompareMode compare,float split,bool edit,InputGuide guide) {
    auto sourceSize=FullRegion(source), processedSize=FullRegion(processed);
    if(edit) { canvas={width,height}; content={sourceSize.width,sourceSize.height}; }
    float scale=std::min(float(width)/canvas.width,float(height)/canvas.height);
    DisplayRect canvasRect{(width-canvas.width*scale)/2,(height-canvas.height*scale)/2,canvas.width*scale,canvas.height*scale};
    if(edit) scale=std::min(float(width)/content.width,float(height)/content.height);
    contentRect_={(width-content.width*scale)/2,(height-content.height*scale)/2,content.width*scale,content.height*scale};
    struct Constants {
        DisplayRect content,canvas;
        float processed[4],baseline[4],sizes[4],options[4],crop[4],guideBounds[4],guideSize[4];
    } c{contentRect_,canvasRect,
        {float(processedRegion.x),float(processedRegion.y),float(processedRegion.width),float(processedRegion.height)},
        {float(crop.x),float(crop.y),float(crop.width),float(crop.height)},
        {float(processedSize.width),float(processedSize.height),float(sourceSize.width),float(sourceSize.height)},
        {float(compare),std::clamp(split,0.f,1.f),edit?1.f:0.f,0},
        {float(crop.x)/sourceSize.width,float(crop.y)/sourceSize.height,float(crop.x+crop.width)/sourceSize.width,float(crop.y+crop.height)/sourceSize.height}};
    const auto targetSize=guide.Target();
    // Center the target on the crop, shifting inside the captured frame when
    // it fits. Oversized targets retain their true size and are clipped visually.
    const float gx=std::clamp(float(crop.x)+crop.width*.5f-targetSize.width*.5f,
        std::min(0.f,float(sourceSize.width)-targetSize.width),std::max(0.f,float(sourceSize.width)-targetSize.width));
    const float gy=std::clamp(float(crop.y)+crop.height*.5f-targetSize.height*.5f,
        std::min(0.f,float(sourceSize.height)-targetSize.height),std::max(0.f,float(sourceSize.height)-targetSize.height));
    c.guideBounds[0]=gx/sourceSize.width; c.guideBounds[1]=gy/sourceSize.height;
    c.guideBounds[2]=(gx+targetSize.width)/sourceSize.width; c.guideBounds[3]=(gy+targetSize.height)/sourceSize.height;
    c.guideSize[0]=float(targetSize.width); c.guideSize[1]=float(targetSize.height);
    static_assert(sizeof(Constants)==144);
    context_->UpdateSubresource(constants_.get(),0,nullptr,&c,0,0);
    D3D11_VIEWPORT vp{0,0,float(width),float(height),0,1}; context_->RSSetViewports(1,&vp); context_->RSSetState(nullptr);
    context_->OMSetRenderTargets(1,&target,nullptr);
    context_->IASetInputLayout(nullptr); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertex_.get(),nullptr,0); context_->PSSetShader(pixel_.get(),nullptr,0);
    ID3D11ShaderResourceView* views[]={processed,source}; context_->PSSetShaderResources(0,2,views);
    auto sampler=sampler_.get(); context_->PSSetSamplers(0,1,&sampler);
    auto constants=constants_.get(); context_->PSSetConstantBuffers(2,1,&constants);
    context_->Draw(3,0);
    ID3D11ShaderResourceView* none[2]{}; context_->PSSetShaderResources(0,2,none);
    context_->OMSetRenderTargets(0,nullptr,nullptr);
}
