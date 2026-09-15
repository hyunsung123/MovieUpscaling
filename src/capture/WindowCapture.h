#pragma once
#include <windows.h>
#include <d3d11.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <atomic>
#include <memory>

// The callbacks only post coalesced notifications. All frame/resource operations
// happen on the owner thread. A generation rejects notifications from old sessions.
class WindowCapture {
public:
    static constexpr UINT FrameMessage = WM_APP + 1;
    static constexpr UINT ClosedMessage = WM_APP + 2;
    ~WindowCapture();
    void Start(HWND source, ID3D11Device* device, HWND notify);
    void Stop() noexcept;
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame TakeLatest(uint64_t& skipped);
    void Resize(winrt::Windows::Graphics::SizeInt32 size);
    uintptr_t Generation() const { return generation_; }
    bool Running() const { return session_ != nullptr; }
private:
    struct Signal {
        HWND hwnd{};
        uintptr_t generation{};
        std::atomic_bool active{true}, pending{false};
    };
    std::shared_ptr<Signal> signal_;
    uintptr_t generation_{};
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item_{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool pool_{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{nullptr};
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device_{nullptr};
    winrt::Windows::Graphics::SizeInt32 size_{};
    winrt::event_token arrived_{}, closed_{};
};
