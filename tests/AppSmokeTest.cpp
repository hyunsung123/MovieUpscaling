#include <windows.h>
#include <chrono>
#include <iostream>
#include <string>
#include <stdexcept>
#include "renderer/GPUUpscaler.h"
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Pump(int milliseconds) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    } while (std::chrono::steady_clock::now() < end);
}
struct AppWindows { DWORD pid; HWND main{}, preview{}; };
void Find(AppWindows& app) {
    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        auto& app = *reinterpret_cast<AppWindows*>(param);
        DWORD pid{}; GetWindowThreadProcessId(hwnd, &pid); if (pid != app.pid) return TRUE;
        wchar_t title[256]{}; GetWindowTextW(hwnd, title, 256);
        if (std::wstring(title).starts_with(L"Window GPU Preview -")) app.main = hwnd;
        if (std::wstring(title).starts_with(L"GPU Preview -")) app.preview = hwnd;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&app));
}
std::wstring Status(HWND main) {
    for (HWND child=GetWindow(main,GW_CHILD); child; child=GetWindow(child,GW_HWNDNEXT)) {
        wchar_t buffer[2048]{}; SendMessageW(child,WM_GETTEXT,2048,reinterpret_cast<LPARAM>(buffer));
        if(std::wstring(buffer).starts_with(L"Input:")) return buffer;
    }
    return {};
}
void Select(HWND main,int id,int index) {
    SendMessageW(GetDlgItem(main,id),CB_SETCURSEL,index,0);
    PostMessageW(main,WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),0);
}
void Frames(HWND source,int count) {
    for(int i=0;i<count;++i) { InvalidateRect(source,nullptr,TRUE); Pump(20); }
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 1;
    bool phase4=argc==3 && std::wstring(argv[2])==L"--phase4";
    PROCESS_INFORMATION process{}; HWND source{};
    try {
        WNDCLASSW cls{}; cls.lpfnWndProc = DefWindowProcW; cls.hInstance = GetModuleHandleW(nullptr);
        cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)); cls.lpszClassName = L"AppSmokeSource";
        RegisterClassW(&cls);
        source = CreateWindowW(cls.lpszClassName, L"Unique app smoke source", (phase4 ? WS_POPUP : WS_OVERLAPPEDWINDOW) | WS_VISIBLE,
            20, 20, phase4 ? 1280 : 400, phase4 ? 720 : 300, nullptr, nullptr, cls.hInstance, nullptr);
        Require(source != nullptr, "Source creation failed");
        std::wstring command = L"\"" + std::wstring(argv[1]) + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        Require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process), "App launch failed");
        AppWindows app{process.dwProcessId};
        for (int i = 0; i < 100 && (!app.main || !app.preview || !IsWindowVisible(app.main)); ++i) { Pump(50); Find(app); }
        Require(app.main && app.preview, "App windows missing");
        Require(IsWindowVisible(app.main), "App initialization did not finish");
        std::cout << "App ready; refreshing\n" << std::flush;
        SendMessageW(app.main, WM_COMMAND, 101, 0);
        HWND combo = GetDlgItem(app.main, 100);
        int selected = static_cast<int>(SendMessageW(combo, CB_FINDSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(L"Unique app smoke source")));
        Require(selected >= 0, "Source not enumerated");
        std::cout << "Source selected; starting\n" << std::flush;
        SendMessageW(combo, CB_SETCURSEL, selected, 0);
        // Keep the source thread pumping while the other process starts WGC.
        // A synchronous cross-process Start command can stall capture startup.
        PostMessageW(app.main, WM_COMMAND, 102, 0);
        for (int i = 0; i < 35; ++i) { InvalidateRect(source, nullptr, TRUE); Pump(40); }
        Require(IsWindowVisible(app.preview), "Preview did not open");
        std::wstring status;
        for (HWND child = GetWindow(app.main, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            wchar_t buffer[2048]{}; SendMessageW(child, WM_GETTEXT, 2048, reinterpret_cast<LPARAM>(buffer));
            if (std::wstring(buffer).starts_with(L"Input:")) status = buffer;
        }
        Require(!IsWindowEnabled(GetDlgItem(app.main, 102)) && IsWindowEnabled(GetDlgItem(app.main, 103)), "Capture did not start");
        Require(!status.empty() && !status.starts_with(L"Input: 0 x 0"), "No captured frame reached app status");
        Require(status.find(L"@ 0.0 render fps") == std::wstring::npos, "No rendered frames");
        if(phase4) {
            const wchar_t* dimensions[]={L"1920 x 1080",L"2560 x 1440",L"3840 x 2160"};
            Require(status.starts_with(L"Input: 1280 x 720"),"Phase 4 source is not exactly 720p");
            for(int output=1;output<=3;++output) for(int mode=0;mode<5;++mode) {
                Select(app.main,106,mode); Select(app.main,107,output); Frames(source,10);
                SendMessageW(app.main,WM_TIMER,1,0); status=Status(app.main);
                Require(!IsWindowEnabled(GetDlgItem(app.main,102)),"Capture stopped during setting change");
                Require(status.find(std::wstring(L"Output: ")+dimensions[output-1])!=std::wstring::npos,"Output setting not applied");
                Require(status.find(std::wstring(L"Upscaler: ")+UpscaleName(static_cast<UpscaleMode>(mode)))!=std::wstring::npos,"Upscale setting not applied");
                Require(status.find(L"@ 0.0 render fps")==std::wstring::npos,"Settings changed but rendering stalled");
                const wchar_t* ratios[]={L"1.50x",L"2.00x",L"3.00x"};
                Require(status.find(std::wstring(L"Scale: ")+(mode==0?L"1.00x":ratios[output-1]))!=std::wstring::npos,"Wrong upscale ratio");
            }
            SetWindowPos(source,nullptr,0,0,640,480,SWP_NOMOVE|SWP_NOZORDER);
            SetWindowPos(app.preview,nullptr,0,0,760,600,SWP_NOMOVE|SWP_NOZORDER);
            Frames(source,15); SendMessageW(app.main,WM_TIMER,1,0); status=Status(app.main);
            Require(status.starts_with(L"Input: 640 x 480"),"Source resize failed during upscale");
            Require(status.find(L"Scale: 4.50x")!=std::wstring::npos,"4:3 source aspect fit failed");
            // Auto must match the monitor containing Preview, independently of its window size.
            Select(app.main,107,0); Frames(source,8); SendMessageW(app.main,WM_TIMER,1,0);
            MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(app.preview,MONITOR_DEFAULTTONEAREST),&monitor);
            auto expected=L"Output: "+std::to_wstring(monitor.rcMonitor.right-monitor.rcMonitor.left)+L" x "+std::to_wstring(monitor.rcMonitor.bottom-monitor.rcMonitor.top);
            Require(Status(app.main).find(expected)!=std::wstring::npos,"Auto does not match Preview monitor");
            std::cout << "PASS: live 5-mode x 3-resolution changes, exact 720p, source/Preview resize, aspect ratio, Auto monitor\n";
        }
        SendMessageW(app.main, WM_COMMAND, 104, 0);
        Require((GetWindowLongPtrW(app.preview, GWL_STYLE) & WS_CAPTION) == 0, "Fullscreen failed");
        SendMessageW(app.preview, WM_KEYDOWN, VK_ESCAPE, 0);
        Require((GetWindowLongPtrW(app.preview, GWL_STYLE) & WS_CAPTION) != 0, "Escape failed");
        SendMessageW(GetDlgItem(app.main, 105), BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(app.main, WM_COMMAND, 105, 0);
        Require((GetWindowLongPtrW(app.preview, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0, "Topmost failed");
        SendMessageW(app.main, WM_COMMAND, 103, 0);
        Require(IsWindowEnabled(GetDlgItem(app.main, 102)), "Stop failed");
        if(phase4) { Select(app.main,106,2); Select(app.main,107,2); }
        PostMessageW(app.main, WM_COMMAND, 102, 0); Frames(source,12);
        if(phase4) {
            SendMessageW(app.main,WM_TIMER,1,0); status=Status(app.main);
            Require(status.find(L"Upscaler: Bicubic")!=std::wstring::npos && status.find(L"Output: 2560 x 1440")!=std::wstring::npos,"Stop -> change settings -> Start lost settings");
            Require(status.find(L"@ 0.0 render fps")==std::wstring::npos,"Restart did not render");
        }
        SendMessageW(app.preview, WM_CLOSE, 0, 0);
        Require(!IsWindowVisible(app.preview) && IsWindowEnabled(GetDlgItem(app.main, 102)), "Preview close failed");
        SendMessageW(app.main, WM_CLOSE, 0, 0);
        Require(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0, "App failed to exit");
        DWORD exitCode{}; GetExitCodeProcess(process.hProcess, &exitCode); Require(exitCode == 0, "App exited with error");
        CloseHandle(process.hThread); CloseHandle(process.hProcess); DestroyWindow(source);
        std::cout << "PASS: source selection, capture status, preview, fullscreen, Escape, topmost, stop/restart, close.\n";
        return 0;
    } catch (std::exception const& e) {
        std::cerr << e.what() << '\n';
        if (process.hProcess) {
            AppWindows app{process.dwProcessId}; Find(app);
            if (app.main) PostMessageW(app.main, WM_CLOSE, 0, 0);
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
        }
        if (source) DestroyWindow(source);
        return 1;
    }
}
