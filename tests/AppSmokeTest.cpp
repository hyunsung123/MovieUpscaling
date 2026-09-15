#include <windows.h>
#include <chrono>
#include <iostream>
#include <string>
#include <stdexcept>
#include "renderer/GPUUpscaler.h"
#include <commctrl.h>
#include <fstream>
#include <filesystem>
namespace {
bool qualitySource=false;
LRESULT CALLBACK SourceProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_PAINT && qualitySource) {
        PAINTSTRUCT ps{}; auto dc=BeginPaint(hwnd,&ps); RECT r{}; GetClientRect(hwnd,&r);
        auto brush=CreateSolidBrush(RGB(38,42,48)); FillRect(dc,&r,brush); DeleteObject(brush);
        SetTextColor(dc,RGB(235,235,235)); SetBkMode(dc,TRANSPARENT);
        auto font=CreateFontW(38,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");
        auto old=SelectObject(dc,font); TextOutW(dc,40,40,L"Manual crop / RCAS / A-B validation",34);
        TextOutW(dc,140,r.bottom-110,L"Subtitle sample: fine text and diagonal edges",44);
        for(int i=0;i<70;++i) {
            auto pen=CreatePen(PS_SOLID,1,RGB(70+i*2,70+i*2,70+i*2)); auto previous=SelectObject(dc,pen);
            MoveToEx(dc,80+i*8,160,nullptr); LineTo(dc,120+i*9,r.bottom-160); SelectObject(dc,previous); DeleteObject(pen);
        }
        SelectObject(dc,old); DeleteObject(font); EndPaint(hwnd,&ps); return 0;
    }
    return DefWindowProcW(hwnd,msg,w,l);
}
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
    // Let the real combo selection event update dependent sliders before the
    // test edits them, just as a user opening Manual crop would do.
    Pump(40);
}
void Frames(HWND source,int count) {
    for(int i=0;i<count;++i) { InvalidateRect(source,nullptr,TRUE); Pump(20); }
}
void Slider(HWND main,int id,int value) {
    auto slider=GetDlgItem(main,id); SendMessageW(slider,TBM_SETPOS,TRUE,value);
    PostMessageW(main,WM_HSCROLL,TB_THUMBPOSITION,reinterpret_cast<LPARAM>(slider));
}
std::wstring Generation(std::wstring const& text) {
    auto begin=text.find(L"Capture generation:"); Require(begin!=std::wstring::npos,"Missing capture generation");
    return text.substr(begin,text.find(L"\r\n",begin)-begin);
}
void Screenshot(HWND window) {
    std::filesystem::create_directories("validation"); RECT r{}; GetWindowRect(window,&r); int w=r.right-r.left,h=r.bottom-r.top;
    auto dc=GetWindowDC(window),memory=CreateCompatibleDC(dc);
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=w; info.bmiHeader.biHeight=-h;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void* bits{}; auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0); auto old=SelectObject(memory,bitmap);
    Require(PrintWindow(window,memory,2)!=0,"UI screenshot failed");
    BITMAPFILEHEADER header{}; header.bfType=0x4d42; header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER); header.bfSize=header.bfOffBits+w*h*4;
    std::ofstream file("validation/app-phase5.bmp",std::ios::binary); file.write(reinterpret_cast<char*>(&header),sizeof(header));
    file.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(info.bmiHeader)); file.write(static_cast<char*>(bits),w*h*4);
    SelectObject(memory,old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(window,dc);
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 1;
    bool phase4=argc==3 && std::wstring(argv[2])==L"--phase4";
    bool phase5=argc==3 && std::wstring(argv[2])==L"--phase5";
    qualitySource=phase5;
    PROCESS_INFORMATION process{}; HWND source{};
    try {
        WNDCLASSW cls{}; cls.lpfnWndProc = SourceProc; cls.hInstance = GetModuleHandleW(nullptr);
        cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)); cls.lpszClassName = L"AppSmokeSource";
        RegisterClassW(&cls);
        source = CreateWindowW(cls.lpszClassName, L"Unique app smoke source", ((phase4||phase5) ? WS_POPUP : WS_OVERLAPPEDWINDOW) | WS_VISIBLE,
            20, 20, (phase4||phase5) ? 1280 : 400, (phase4||phase5) ? 720 : 300, nullptr, nullptr, cls.hInstance, nullptr);
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
        if(phase5) {
            auto generation=Generation(status);
            auto state=[&]() {
                Frames(source,10); SendMessageW(app.main,WM_TIMER,1,0); auto s=Status(app.main);
                Require(Generation(s)==generation,"Settings restarted/duplicated WGC capture");
                Require(!IsWindowEnabled(GetDlgItem(app.main,102)),"Capture stopped during Phase 5 edits");
                return s;
            };
            Select(app.main,107,3); Select(app.main,109,1); Slider(app.main,114,10); Slider(app.main,116,5);
            auto firstCrop=state();
            if(firstCrop.find(L"Crop region: 1280 x 612")==std::wstring::npos) std::wcerr<<firstCrop<<L'\n';
            Require(firstCrop.find(L"Crop region: 1280 x 612")!=std::wstring::npos,"Top/bottom crop not applied");
            Slider(app.main,113,10); Slider(app.main,114,20); Slider(app.main,115,15); Slider(app.main,116,10);
            Require(state().find(L"Crop region: 960 x 504")!=std::wstring::npos,"Asymmetric live crop wrong");
            for(int id=113;id<=116;++id) Slider(app.main,id,10);
            Require(state().find(L"Crop region: 1024 x 576")!=std::wstring::npos,"16:9 crop wrong");
            SendMessageW(app.main,WM_COMMAND,110,0);
            Require(state().find(L"Crop edit: full frame")!=std::wstring::npos,"Edit Crop button failed");
            SendMessageW(app.preview,WM_KEYDOWN,VK_F2,0);
            Require(state().find(L"Crop edit: full frame")==std::wstring::npos,"F2 did not finish crop edit");
            SetWindowPos(source,nullptr,0,0,1600,900,SWP_NOMOVE|SWP_NOZORDER);
            Require(state().find(L"Crop region: 1280 x 720")!=std::wstring::npos,"Source resize with crop failed");
            Slider(app.main,108,100); Require(state().find(L"Sharpen: RCAS 100")!=std::wstring::npos,"Sharpen slider failed");
            SendMessageW(app.preview,WM_KEYDOWN,VK_F3,0); Require(state().find(L"Sharpen: RCAS 0")!=std::wstring::npos,"F3 bypass failed");
            SendMessageW(app.main,WM_KEYDOWN,VK_F3,0); Require(state().find(L"Sharpen: RCAS 100")!=std::wstring::npos,"F3 restore failed");
            for(int direction=1;direction<=2;++direction) for(int pos:{25,50,75}) {
                Select(app.main,111,direction); Slider(app.main,112,pos); auto s=state();
                Require(s.find(direction==1?L"(vertical ":L"(horizontal ")!=std::wstring::npos,"Compare direction failed");
                Require(s.find(std::to_wstring(pos)+L"%)")!=std::wstring::npos,"Split position failed");
            }
            SendMessageW(app.preview,WM_KEYDOWN,VK_F1,0); Require(state().find(L"Compare: Off")!=std::wstring::npos,"F1 comparison toggle failed");
            SendMessageW(app.preview,WM_KEYDOWN,VK_F1,0); Require(state().find(L"(horizontal ")!=std::wstring::npos,"F1 restore orientation failed");
            Select(app.main,111,1); Slider(app.main,112,50); state();
            RECT r{}; GetClientRect(app.preview,&r);
            SendMessageW(app.preview,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(r.right/2,r.bottom/2));
            SendMessageW(app.preview,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(r.right*3/4,r.bottom/2));
            SendMessageW(app.preview,WM_LBUTTONUP,0,MAKELPARAM(r.right*3/4,r.bottom/2));
            state(); Require(SendMessageW(GetDlgItem(app.main,112),TBM_GETPOS,0,0)==75,"Preview divider drag failed");
            Screenshot(app.main);
            Select(app.main,109,0); Require(state().find(L"Crop region: 1600 x 900")!=std::wstring::npos,"Crop Off did not restore full source");
            std::cout<<"PASS: actual app crop edit, live resize, RCAS slider/F3, A/B F1/F2, divider drag; same WGC generation\n";
        }
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
        if(phase4||phase5) { Select(app.main,106,2); Select(app.main,107,2); }
        PostMessageW(app.main, WM_COMMAND, 102, 0); Frames(source,12);
        if(phase4||phase5) {
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
            // Only the process spawned by THIS test: close an initialization
            // error dialog too, so a failed test never leaves a locked binary.
            if(WaitForSingleObject(process.hProcess,1000)!=WAIT_OBJECT_0) TerminateProcess(process.hProcess,1);
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
        }
        if (source) DestroyWindow(source);
        return 1;
    }
}
