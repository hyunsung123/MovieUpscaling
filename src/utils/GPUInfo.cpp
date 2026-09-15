#include "GPUInfo.h"
#include <dxgi.h>
#include <winrt/base.h>
std::wstring GPUName(ID3D11Device* device) {
    winrt::com_ptr<IDXGIDevice> dxgi;
    winrt::check_hresult(device->QueryInterface(IID_PPV_ARGS(dxgi.put())));
    winrt::com_ptr<IDXGIAdapter> adapter;
    winrt::check_hresult(dxgi->GetAdapter(adapter.put()));
    DXGI_ADAPTER_DESC desc{};
    winrt::check_hresult(adapter->GetDesc(&desc));
    return desc.Description;
}
