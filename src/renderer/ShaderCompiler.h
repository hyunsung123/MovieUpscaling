#pragma once
#include <d3dcompiler.h>
#include <winrt/base.h>
#include <cstring>
inline winrt::com_ptr<ID3DBlob> CompileShader(const char* text, const char* entry, const char* profile) {
    winrt::com_ptr<ID3DBlob> blob, errors;
    HRESULT hr = D3DCompile(text, std::strlen(text), "embedded shader", nullptr, nullptr, entry, profile,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.put(), errors.put());
    if (FAILED(hr) && errors) {
        auto message = static_cast<const char*>(errors->GetBufferPointer());
        OutputDebugStringA(message);
        throw winrt::hresult_error(hr, winrt::to_hstring(message));
    }
    winrt::check_hresult(hr);
    return blob;
}
