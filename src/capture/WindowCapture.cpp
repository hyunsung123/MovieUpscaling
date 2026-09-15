#include "WindowCapture.h"
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <dxgi.h>
using namespace winrt;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;

WindowCapture::~WindowCapture() { Stop(); }
void WindowCapture::Start(HWND source, ID3D11Device* device, HWND notify) {
    Stop();
    if (!IsWindow(source)) throw hresult_invalid_argument(L"Source window no longer exists. Refresh the list.");
    if (!GraphicsCaptureSession::IsSupported()) throw hresult_error(E_NOTIMPL, L"Windows Graphics Capture is unavailable.");
    try {
        auto interop = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        check_hresult(interop->CreateForWindow(source, guid_of<GraphicsCaptureItem>(), put_abi(item_)));
        com_ptr<IDXGIDevice> dxgi;
        check_hresult(device->QueryInterface(IID_PPV_ARGS(dxgi.put())));
        com_ptr<IInspectable> inspectable;
        check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
        device_ = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
        size_ = item_.Size();
        if (size_.Width <= 0 || size_.Height <= 0) throw hresult_invalid_argument(L"Restore the source window before capture.");
        pool_ = Direct3D11CaptureFramePool::CreateFreeThreaded(device_, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size_);
        signal_ = std::make_shared<Signal>();
        signal_->hwnd = notify;
        signal_->generation = ++generation_;
        auto signal = signal_;
        arrived_ = pool_.FrameArrived([signal](auto const&, auto const&) noexcept {
            if (signal->active && !signal->pending.exchange(true)) {
                if (!PostMessageW(signal->hwnd, FrameMessage, signal->generation, 0)) signal->pending = false;
            }
        });
        closed_ = item_.Closed([signal](auto const&, auto const&) noexcept {
            if (signal->active) PostMessageW(signal->hwnd, ClosedMessage, signal->generation, 0);
        });
        session_ = pool_.CreateCaptureSession(item_);
        session_.StartCapture();
    } catch (...) { Stop(); throw; }
}
void WindowCapture::Stop() noexcept {
    ++generation_;
    if (signal_) signal_->active = false;
    try { if (pool_ && arrived_.value) pool_.FrameArrived(arrived_); } catch (...) {}
    try { if (item_ && closed_.value) item_.Closed(closed_); } catch (...) {}
    try { if (session_) session_.Close(); } catch (...) {}
    try { if (pool_) pool_.Close(); } catch (...) {}
    session_ = nullptr; pool_ = nullptr; item_ = nullptr; device_ = nullptr;
    signal_.reset(); arrived_ = {}; closed_ = {}; size_ = {};
}
Direct3D11CaptureFrame WindowCapture::TakeLatest(uint64_t& skipped) {
    if (!pool_) return nullptr;
    signal_->pending = false;
    auto latest = pool_.TryGetNextFrame();
    // Two buffers: bounded draining keeps the message pump responsive.
    if (latest) {
        if (auto newer = pool_.TryGetNextFrame()) {
            latest.Close(); latest = std::move(newer); ++skipped;
        }
    }
    return latest;
}
void WindowCapture::Resize(SizeInt32 size) {
    if (size.Width > 0 && size.Height > 0 && (size.Width != size_.Width || size.Height != size_.Height)) {
        pool_.Recreate(device_, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        size_ = size;
    }
}
