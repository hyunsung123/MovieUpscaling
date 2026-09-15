#include <windows.h>
#include <chrono>
#include <iostream>
#include <string>
#include <stdexcept>
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
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 1;
    PROCESS_INFORMATION process{}; HWND source{};
    try {
        WNDCLASSW cls{}; cls.lpfnWndProc = DefWindowProcW; cls.hInstance = GetModuleHandleW(nullptr);
        cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)); cls.lpszClassName = L"AppSmokeSource";
        RegisterClassW(&cls);
        source = CreateWindowW(cls.lpszClassName, L"Unique app smoke source", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            20, 20, 400, 300, nullptr, nullptr, cls.hInstance, nullptr);
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
        SendMessageW(app.main, WM_COMMAND, 104, 0);
        Require((GetWindowLongPtrW(app.preview, GWL_STYLE) & WS_CAPTION) == 0, "Fullscreen failed");
        SendMessageW(app.preview, WM_KEYDOWN, VK_ESCAPE, 0);
        Require((GetWindowLongPtrW(app.preview, GWL_STYLE) & WS_CAPTION) != 0, "Escape failed");
        SendMessageW(GetDlgItem(app.main, 105), BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(app.main, WM_COMMAND, 105, 0);
        Require((GetWindowLongPtrW(app.preview, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0, "Topmost failed");
        SendMessageW(app.main, WM_COMMAND, 103, 0);
        Require(IsWindowEnabled(GetDlgItem(app.main, 102)), "Stop failed");
        PostMessageW(app.main, WM_COMMAND, 102, 0); Pump(100);
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
