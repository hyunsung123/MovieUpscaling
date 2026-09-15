// Integration test requires an unlocked, interactive Windows desktop and a GPU.
// Pixel readback here is test-only; production sources never map video textures.
#include "capture/WindowCapture.h"
#include "renderer/D3DRenderer.h"
#include <windows.graphics.directx.direct3d11.interop.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace winrt;
LRESULT CALLBACK TestProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{}; auto dc = BeginPaint(hwnd, &ps);
        RECT r{}; GetClientRect(hwnd, &r);
        auto brush = CreateSolidBrush(RGB(35, 170, 65));
        FillRect(dc, &r, brush); DeleteObject(brush); EndPaint(hwnd, &ps); return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
int main() {
    try {
        std::cout << "Initialize WinRT\n" << std::flush;
        init_apartment(apartment_type::single_threaded);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSW cls{}; cls.lpfnWndProc = TestProc; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"CaptureSmokeSource";
        RegisterClassW(&cls);
        HWND source = CreateWindowW(cls.lpszClassName, L"Capture smoke source", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 420, 320, nullptr, nullptr, cls.hInstance, nullptr);
        HWND preview = CreateWindowW(cls.lpszClassName, L"Capture smoke preview", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 500, 40, 500, 400, nullptr, nullptr, cls.hInstance, nullptr);
        if (!source || !preview) throw_last_error();
        std::cout << "Initialize D3D renderer\n" << std::flush;
        D3DRenderer renderer; renderer.Initialize(preview);
        WindowCapture capture;
        uint64_t skipped{}; int frames{}, sessions{}; bool resized{}, resizeSeen{}, closed{}, pixelsChecked{};
        std::cout << "Start WGC\n" << std::flush;
        capture.Start(source, renderer.Device(), preview);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(18);
        while (!closed && std::chrono::steady_clock::now() < deadline) {
            // Trigger source damage without driving the preview renderer on a timer.
            if (source) InvalidateRect(source, nullptr, FALSE);
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WindowCapture::ClosedMessage && msg.wParam == capture.Generation()) { closed = true; continue; }
                if (msg.message == WindowCapture::FrameMessage && msg.wParam == capture.Generation()) {
                    auto frame = capture.TakeLatest(skipped); if (!frame) continue;
                    auto size = frame.ContentSize();
                    auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
                    com_ptr<ID3D11Texture2D> texture;
                    check_hresult(access->GetInterface(IID_PPV_ARGS(texture.put())));
                    if (renderer.Render(texture.get(), size.Width, size.Height)) ++frames;
                    if (!pixelsChecked) {
                        D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
                        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
                        com_ptr<ID3D11Texture2D> staging;
                        check_hresult(renderer.Device()->CreateTexture2D(&desc, nullptr, staging.put()));
                        com_ptr<ID3D11DeviceContext> context; renderer.Device()->GetImmediateContext(context.put());
                        context->CopyResource(staging.get(), texture.get());
                        D3D11_MAPPED_SUBRESOURCE mapped{};
                        check_hresult(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
                        auto p = static_cast<const unsigned char*>(mapped.pData) + (desc.Height / 2) * mapped.RowPitch + (desc.Width / 2) * 4;
                        bool green = p[1] > 120 && p[1] > p[0] + 40 && p[1] > p[2] + 40;
                        context->Unmap(staging.get(), 0);
                        if (!green) throw std::runtime_error("Captured center pixel did not match the green test source");
                        pixelsChecked = true;
                    }
                    if (resized && size.Width > 500) resizeSeen = true;
                    frame.Close(); capture.Resize(size);
                    if (frames >= 3 && !resized) {
                        SetWindowPos(source, nullptr, 0, 0, 620, 440, SWP_NOMOVE | SWP_NOZORDER);
                        SetWindowPos(preview, nullptr, 0, 0, 640, 480, SWP_NOMOVE | SWP_NOZORDER); resized = true;
                    }
                    if (frames >= 6 && resizeSeen && sessions == 0) {
                        capture.Stop(); capture.Start(source, renderer.Device(), preview); ++sessions;
                    }
                    if (frames >= 10 && sessions == 1 && source) { DestroyWindow(source); source = nullptr; }
                } else { TranslateMessage(&msg); DispatchMessageW(&msg); }
            }
            MsgWaitForMultipleObjectsEx(0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        capture.Stop(); capture.Stop();
        if (source) DestroyWindow(source);
        DestroyWindow(preview);
        if (!closed || !resizeSeen || !pixelsChecked || frames < 10 || sessions != 1) throw std::runtime_error("Capture lifecycle timed out or incomplete");
        std::wcout << L"PASS: GPU=" << renderer.AdapterName() << L"; frames=" << frames << L"; pixel, resize, restart, source-close, repeated stop verified.\n";
        return 0;
    } catch (hresult_error const& e) { std::wcerr << L"HRESULT 0x" << std::hex << static_cast<unsigned>(e.code().value) << L": " << e.message().c_str() << L'\n'; }
    catch (std::exception const& e) { std::cerr << e.what() << '\n'; }
    return 1;
}
