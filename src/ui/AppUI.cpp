#include "AppUI.h"
#include <windows.graphics.directx.direct3d11.interop.h>
#include <sstream>
#include <iomanip>
using namespace winrt;
namespace {
constexpr int Source = 100, RefreshList = 101, StartCapture = 102, StopCapture = 103, ToggleFullscreen = 104, Topmost = 105;
HWND Control(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    HWND hwnd = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) throw_last_error();
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return hwnd;
}
}
int AppUI::Run(HINSTANCE instance, int show) {
    WNDCLASSEXW cls{sizeof(cls)};
    cls.style = CS_DBLCLKS;
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    cls.lpszClassName = L"WindowGPUPreview";
    if (!RegisterClassExW(&cls)) throw_last_error();
    main_ = CreateWindowExW(0, cls.lpszClassName, L"Window GPU Preview - Phase 1-3",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
        650, 390, nullptr, nullptr, instance, this);
    if (!main_) throw_last_error();
    preview_ = CreateWindowExW(0, cls.lpszClassName, L"GPU Preview - double click: fullscreen / Esc: windowed",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 960, 600, nullptr, nullptr, instance, this);
    if (!preview_) throw_last_error();
    // Avoid accidental recursive capture by other instances where Windows supports it.
    SetWindowDisplayAffinity(preview_, WDA_EXCLUDEFROMCAPTURE);
    Control(main_, L"STATIC", L"Source window", 0, 20, 20, 150, 22, 0);
    sources_ = Control(main_, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 20, 48, 490, 260, Source);
    Control(main_, L"BUTTON", L"Refresh", WS_TABSTOP, 520, 48, 90, 26, RefreshList);
    start_ = Control(main_, L"BUTTON", L"Start", WS_TABSTOP, 20, 95, 100, 32, StartCapture);
    stop_ = Control(main_, L"BUTTON", L"Stop", WS_TABSTOP, 130, 95, 100, 32, StopCapture);
    Control(main_, L"BUTTON", L"Fullscreen", WS_TABSTOP, 240, 95, 110, 32, ToggleFullscreen);
    Control(main_, L"BUTTON", L"Always on top", BS_AUTOCHECKBOX | WS_TABSTOP, 370, 95, 160, 32, Topmost);
    status_ = Control(main_, L"STATIC", L"", 0, 20, 150, 595, 170, 0);
    renderer_.Initialize(preview_);
    Refresh(); Stop(L"Ready. Select a window and press Start.");
    ShowWindow(main_, show);
    SetTimer(main_, 1, 1000, nullptr); // Status only; never drives frame rendering.
    MSG msg{};
    int result;
    while ((result = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        if (!IsDialogMessageW(main_, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    capture_.Stop();
    if (result == -1) throw_last_error();
    return static_cast<int>(msg.wParam);
}
LRESULT CALLBACK AppUI::WindowProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<AppUI*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<AppUI*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    try { return self ? self->Handle(hwnd, message, w, l) : DefWindowProcW(hwnd, message, w, l); }
    catch (...) {
        auto error = to_hresult();
        std::wstring detail = hresult_error(error).message().c_str();
        if (self) {
            self->capture_.Stop();
            EnableWindow(self->start_, TRUE); EnableWindow(self->stop_, FALSE);
            SetWindowTextW(self->status_, detail.c_str());
        }
        MessageBoxW(hwnd, detail.c_str(), L"Capture / Direct3D error", MB_OK | MB_ICONERROR);
        return 0;
    }
}
LRESULT AppUI::Handle(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_CTLCOLORSTATIC) {
        SetTextColor(reinterpret_cast<HDC>(w), RGB(235, 235, 235));
        SetBkColor(reinterpret_cast<HDC>(w), RGB(0, 0, 0));
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    }
    if (message == WM_DPICHANGED) {
        auto rect = reinterpret_cast<RECT*>(l);
        SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    if (hwnd == main_) {
        switch (message) {
        case WM_COMMAND:
            switch (LOWORD(w)) {
            case RefreshList: Refresh(); break;
            case StartCapture: Start(); break;
            case StopCapture: Stop(L"Stopped."); break;
            case ToggleFullscreen: Fullscreen(); break;
            case Topmost:
                SetWindowPos(preview_, SendDlgItemMessageW(main_, Topmost, BM_GETCHECK, 0, 0) == BST_CHECKED ? HWND_TOPMOST : HWND_NOTOPMOST,
                    0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); break;
            }
            return 0;
        case WindowCapture::FrameMessage:
            if (w == capture_.Generation() && capture_.Running()) OnFrame();
            return 0;
        case WindowCapture::ClosedMessage:
            if (w == capture_.Generation()) Stop(L"Source window closed. Refresh to select another window.");
            return 0;
        case WM_TIMER: if (capture_.Running()) Status(); return 0;
        case WM_CLOSE:
            capture_.Stop(); DestroyWindow(preview_); DestroyWindow(main_); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
    } else if (hwnd == preview_) {
        switch (message) {
        case WM_LBUTTONDBLCLK: Fullscreen(); return 0;
        case WM_KEYDOWN: if (w == VK_ESCAPE && fullscreen_) Fullscreen(); return 0;
        case WM_CLOSE:
            Stop(L"Preview closed. Press Start to reopen.");
            if (fullscreen_) Fullscreen();
            ShowWindow(preview_, SW_HIDE); return 0;
        case WM_ERASEBKGND: return 1;
        }
    }
    return DefWindowProcW(hwnd, message, w, l);
}
void AppUI::Refresh() {
    HWND previous{};
    auto selected = SendMessageW(sources_, CB_GETCURSEL, 0, 0);
    if (selected >= 0 && static_cast<size_t>(selected) < windows_.size()) previous = windows_[selected].hwnd;
    windows_ = ListWindows();
    SendMessageW(sources_, CB_RESETCONTENT, 0, 0);
    int selection = 0;
    for (size_t i = 0; i < windows_.size(); ++i) {
        auto label = windows_[i].title + L" [PID " + std::to_wstring(windows_[i].processId) + L"]";
        SendMessageW(sources_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (windows_[i].hwnd == previous) selection = static_cast<int>(i);
    }
    SendMessageW(sources_, CB_SETCURSEL, selection, 0);
}
void AppUI::Start() {
    auto selected = SendMessageW(sources_, CB_GETCURSEL, 0, 0);
    if (selected < 0 || static_cast<size_t>(selected) >= windows_.size()) throw hresult_invalid_argument(L"Select a source window first.");
    auto const& entry = windows_[selected];
    DWORD pid{}; GetWindowThreadProcessId(entry.hwnd, &pid);
    if (pid != entry.processId || !IsWindow(entry.hwnd)) throw hresult_invalid_argument(L"Source changed. Refresh the list.");
    if (IsIconic(entry.hwnd)) throw hresult_invalid_argument(L"Restore the minimized source window first.");
    capture_.Stop();
    ShowWindow(preview_, SW_SHOWNOACTIVATE);
    renderer_.Clear();
    captured_ = rendered_ = skipped_ = 0;
    inputWidth_ = inputHeight_ = 0;
    captureFps_ = renderFps_ = submitMs_ = 0;
    statsTime_ = std::chrono::steady_clock::now();
    capture_.Start(entry.hwnd, renderer_.Device(), main_);
    EnableWindow(start_, FALSE); EnableWindow(stop_, TRUE);
    SetWindowTextW(status_, L"Capturing. Waiting for frames...\r\nProtected content may be black; minimized sources may stop producing frames.");
}
void AppUI::Stop(const wchar_t* reason) {
    capture_.Stop();
    EnableWindow(start_, TRUE); EnableWindow(stop_, FALSE);
    renderer_.Clear();
    auto text = std::wstring(reason) + L"\r\nGPU: " + renderer_.AdapterName() +
        L"\r\nMode: GPU passthrough / bilinear aspect fit (SDR)\r\nFSR, sharpening and GPU timing: planned for later phases.";
    SetWindowTextW(status_, text.c_str());
}
void AppUI::OnFrame() {
    auto frame = capture_.TakeLatest(skipped_);
    if (!frame) return;
    ++captured_;
    auto size = frame.ContentSize();
    inputWidth_ = size.Width; inputHeight_ = size.Height;
    if (size.Width > 0 && size.Height > 0) {
        auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        com_ptr<ID3D11Texture2D> texture;
        check_hresult(access->GetInterface(IID_PPV_ARGS(texture.put())));
        auto begin = std::chrono::steady_clock::now();
        if (renderer_.Render(texture.get(), size.Width, size.Height)) ++rendered_;
        submitMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    }
    frame.Close();
    capture_.Resize(size); // Release outstanding frame before pool recreation.
}
void AppUI::Status() {
    auto now = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(now - statsTime_).count();
    if (seconds <= 0) return;
    captureFps_ = captured_ / seconds; renderFps_ = rendered_ / seconds;
    captured_ = rendered_ = 0; statsTime_ = now;
    std::wostringstream text;
    text << std::fixed << std::setprecision(1)
        << L"Input: " << inputWidth_ << L" x " << inputHeight_ << L" @ " << captureFps_ << L" captured fps\r\n"
        << L"Output: " << renderer_.Width() << L" x " << renderer_.Height() << L" @ " << renderFps_ << L" render fps\r\n"
        << L"GPU: " << renderer_.AdapterName() << L"\r\nCPU submit + Present: " << submitMs_ << L" ms (not GPU time)\r\n"
        << L"Discarded queued frames: " << skipped_ << L" | GPU time: N/A\r\n"
        << L"SDR passthrough. Black/frozen image: check protection or minimized source.";
    SetWindowTextW(status_, text.str().c_str());
}
void AppUI::Fullscreen() {
    ShowWindow(preview_, SW_SHOW);
    if (!fullscreen_) {
        GetWindowPlacement(preview_, &placement_);
        MONITORINFO monitor{sizeof(monitor)};
        if (!GetMonitorInfoW(MonitorFromWindow(preview_, MONITOR_DEFAULTTONEAREST), &monitor)) throw_last_error();
        SetWindowLongPtrW(preview_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(preview_, nullptr, monitor.rcMonitor.left, monitor.rcMonitor.top,
            monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
            SWP_NOZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongPtrW(preview_, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(preview_, &placement_);
        SetWindowPos(preview_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    fullscreen_ = !fullscreen_;
    SetForegroundWindow(preview_);
}
