#include "AppUI.h"
#include <windows.graphics.directx.direct3d11.interop.h>
#include <sstream>
#include <iomanip>
#include <commctrl.h>
#include <windowsx.h>
using namespace winrt;
namespace {
constexpr int Source = 100, RefreshList = 101, StartCapture = 102, StopCapture = 103, ToggleFullscreen = 104, Topmost = 105;
constexpr int Upscale = 106, Output = 107;
constexpr int Sharpen=108, CropMode=109, EditCrop=110, Compare=111, Split=112, CropLeft=113;
constexpr int Guide=117,GuideWidth=118,GuideHeight=119,Fit720=120,AspectLock=121,Preset720=122;
HWND Control(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    HWND hwnd = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) throw_last_error();
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return hwnd;
}
}
int AppUI::Run(HINSTANCE instance, int show) {
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES}; InitCommonControlsEx(&controls);
    WNDCLASSEXW cls{sizeof(cls)};
    cls.style = CS_DBLCLKS;
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    cls.lpszClassName = L"WindowGPUPreview";
    if (!RegisterClassExW(&cls)) throw_last_error();
    main_ = CreateWindowExW(0, cls.lpszClassName, L"Window GPU Preview - GPU Upscaling",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
        1030, 870, nullptr, nullptr, instance, this);
    if (!main_) throw_last_error();
    preview_ = CreateWindowExW(0, cls.lpszClassName, L"GPU Preview - double click: fullscreen / Esc: windowed",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 960, 600, nullptr, nullptr, instance, this);
    if (!preview_) throw_last_error();
    // Avoid accidental recursive capture by other instances where Windows supports it.
    SetWindowDisplayAffinity(preview_, WDA_EXCLUDEFROMCAPTURE);
    Control(main_, L"STATIC", L"Source window", 0, 20, 20, 150, 22, 0);
    sources_ = Control(main_, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 20, 48, 490, 260, Source);
    Control(main_, L"BUTTON", L"Refresh", WS_TABSTOP, 520, 48, 90, 26, RefreshList);
    Control(main_, L"STATIC", L"Upscale", 0, 20, 90, 120, 22, 0);
    upscale_ = Control(main_, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 20, 116, 250, 220, Upscale);
    for (int i = 0; i < 5; ++i) SendMessageW(upscale_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(UpscaleName(static_cast<UpscaleMode>(i))));
    SendMessageW(upscale_, CB_SETCURSEL, static_cast<WPARAM>(renderer_.Mode()), 0);
    Control(main_, L"STATIC", L"Output", 0, 300, 90, 150, 22, 0);
    output_ = Control(main_, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 300, 116, 310, 200, Output);
    for (auto label : {L"Auto (Preview monitor)", L"1920 x 1080", L"2560 x 1440", L"3840 x 2160"})
        SendMessageW(output_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    SendMessageW(output_, CB_SETCURSEL, 0, 0);
    start_ = Control(main_, L"BUTTON", L"Start", WS_TABSTOP, 20, 166, 100, 32, StartCapture);
    stop_ = Control(main_, L"BUTTON", L"Stop", WS_TABSTOP, 130, 166, 100, 32, StopCapture);
    Control(main_, L"BUTTON", L"Fullscreen", WS_TABSTOP, 240, 166, 110, 32, ToggleFullscreen);
    Control(main_, L"BUTTON", L"Always on top", BS_AUTOCHECKBOX | WS_TABSTOP, 370, 166, 160, 32, Topmost);
    Control(main_,L"STATIC",L"Sharpen (F3)",0,20,215,130,22,0);
    sharpenSlider_=Control(main_,TRACKBAR_CLASSW,L"",TBS_NOTICKS|WS_TABSTOP,150,211,390,30,Sharpen);
    SendMessageW(sharpenSlider_,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));
    sharpenValue_=Control(main_,L"STATIC",L"25",0,560,215,90,24,0);
    Control(main_,L"STATIC",L"Crop",0,20,258,60,24,0);
    cropMode_=Control(main_,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,80,254,150,120,CropMode);
    for(auto text:{L"Off",L"Manual"}) SendMessageW(cropMode_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    cropEditButton_=Control(main_,L"BUTTON",L"Edit Crop (F2)",WS_TABSTOP,250,252,155,30,EditCrop);
    const wchar_t* labels[]={L"Left",L"Top",L"Right",L"Bottom"};
    for(int i=0;i<4;++i) {
        cropLabels_[i]=Control(main_,L"STATIC",labels[i],0,20+i*165,295,145,22,0);
        cropSliders_[i]=Control(main_,TRACKBAR_CLASSW,L"",TBS_NOTICKS|WS_TABSTOP,15+i*165,320,150,28,CropLeft+i);
        SendMessageW(cropSliders_[i],TBM_SETRANGE,TRUE,MAKELPARAM(0,45));
    }
    Control(main_,L"STATIC",L"Compare (F1)",0,20,365,125,22,0);
    compareMode_=Control(main_,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,150,361,195,150,Compare);
    for(auto text:{L"Off",L"Split Vertical",L"Split Horizontal"}) SendMessageW(compareMode_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    Control(main_,L"STATIC",L"Split",0,365,365,45,22,0);
    splitSlider_=Control(main_,TRACKBAR_CLASSW,L"",TBS_NOTICKS|WS_TABSTOP,415,359,180,30,Split);
    SendMessageW(splitSlider_,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));
    splitValue_=Control(main_,L"STATIC",L"50%",0,610,365,70,22,0);
    status_ = Control(main_, L"STATIC", L"", 0, 20, 410, 695, 400, 0);
    Control(main_,L"STATIC",L"Input Guide (Crop Edit only)",0,725,20,280,22,0);
    inputGuide_=Control(main_,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,725,48,275,180,Guide);
    for(auto label:{L"Off",L"1280 x 720",L"1920 x 1080",L"Custom"}) SendMessageW(inputGuide_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    customLabel_=Control(main_,L"STATIC",L"Custom width / height (1-16384)",0,725,86,280,22,0);
    guideWidth_=Control(main_,L"EDIT",L"1280",WS_BORDER|ES_NUMBER|WS_TABSTOP,725,112,130,26,GuideWidth);
    guideHeight_=Control(main_,L"EDIT",L"720",WS_BORDER|ES_NUMBER|WS_TABSTOP,870,112,130,26,GuideHeight);
    SendMessageW(guideWidth_,EM_SETLIMITTEXT,5,0); SendMessageW(guideHeight_,EM_SETLIMITTEXT,5,0);
    fit720_=Control(main_,L"BUTTON",L"Fit 720p",WS_TABSTOP,725,152,275,32,Fit720);
    Control(main_,L"STATIC",L"Aspect Lock",0,725,202,110,22,0);
    aspectLock_=Control(main_,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,840,198,160,120,AspectLock);
    for(auto label:{L"Off",L"16:9"}) SendMessageW(aspectLock_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    preset720_=Control(main_,L"BUTTON",L"720p -> 1440p",WS_TABSTOP,725,240,275,32,Preset720);
    Control(main_,L"STATIC",L"This is the captured pixel size, not the streaming service's encoded resolution.",0,725,292,275,48,0);
    guideStatus_=Control(main_,L"STATIC",L"Start capture to use Fit / preset.",0,725,355,280,440,0);
    SyncVideoControls();
    wchar_t debug[8]{};
    renderer_.Initialize(preview_,GetEnvironmentVariableW(L"MOVIEUPSCALING_DEBUG_D3D",debug,8)>0);
    Refresh(); Stop(L"Ready. Select a window and press Start.");
    ShowWindow(main_, show);
    SetTimer(main_, 1, 1000, nullptr); // Status only; never drives frame rendering.
    MSG msg{};
    int result;
    while ((result = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        // Child controls also receive local shortcuts. Never register global hotkeys.
        HWND root=GetAncestor(msg.hwnd,GA_ROOT);
        if(msg.message==WM_KEYDOWN && (root==main_ || root==preview_) && !(msg.lParam&(1LL<<30)) && Hotkey(msg.wParam)) continue;
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
    if(message==WM_KEYDOWN && (hwnd==main_ || hwnd==preview_) && !(l&(1LL<<30)) && Hotkey(w)) return 0;
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
        case WM_HSCROLL: UpdateVideoSettings(GetDlgCtrlID(reinterpret_cast<HWND>(l))); return 0;
        case WM_COMMAND:
            switch (LOWORD(w)) {
            case Guide: if(HIWORD(w)==CBN_SELCHANGE) ChangeGuide(); break;
            case GuideWidth: case GuideHeight:
                if(HIWORD(w)==EN_KILLFOCUS) ChangeGuide(); break;
            case Fit720: case Preset720:
                if(capture_.Running() && renderer_.FitInput720p(LOWORD(w)==Preset720)) {
                    fitted720_=true;
                    if(LOWORD(w)==Preset720) {
                        lastSharpen_=25; SendMessageW(output_,CB_SETCURSEL,2,0);
                        SendMessageW(upscale_,CB_SETCURSEL,static_cast<WPARAM>(UpscaleMode::Easu),0);
                    }
                    SyncVideoControls(); RedrawSettings();
                }
                break;
            case AspectLock:
                if(HIWORD(w)==CBN_SELCHANGE) {
                    auto crop=renderer_.Crop(); crop.aspect16x9=SendMessageW(aspectLock_,CB_GETCURSEL,0,0)==1;
                    if(crop.manual && crop.aspect16x9 && inputWidth_>0 && inputHeight_>0)
                        crop=PixelCrop(FitAspect16x9(CropRegion(inputWidth_,inputHeight_,crop)),true);
                    renderer_.SetCrop(crop); SyncVideoControls(); RedrawSettings();
                }
                break;
            case CropMode:
                if(HIWORD(w)==CBN_SELCHANGE) {
                    auto crop=renderer_.Crop(); crop.manual=SendMessageW(cropMode_,CB_GETCURSEL,0,0)==1;
                    renderer_.SetCrop(crop); if(!crop.manual) renderer_.SetCropEdit(false); SyncVideoControls(); RedrawSettings();
                }
                break;
            case EditCrop: Hotkey(VK_F2); break;
            case Compare:
                if(HIWORD(w)==CBN_SELCHANGE) {
                    auto selection=SendMessageW(compareMode_,CB_GETCURSEL,0,0);
                    if(selection>=0 && selection<=2) renderer_.SetCompare(static_cast<CompareMode>(selection));
                    if(renderer_.Compare()!=CompareMode::Off) lastCompare_=renderer_.Compare(); SyncVideoControls();
                }
                break;
            case Upscale:
                if (HIWORD(w) == CBN_SELCHANGE) {
                    auto index = SendMessageW(upscale_, CB_GETCURSEL, 0, 0);
                    if (index >= 0 && index < 5) renderer_.SetUpscaleMode(static_cast<UpscaleMode>(index));
                }
                break;
            case Output:
                if (HIWORD(w) == CBN_SELCHANGE) {
                    auto index = SendMessageW(output_, CB_GETCURSEL, 0, 0);
                    if (index >= 0 && index < 4) renderer_.SetOutputMode(static_cast<OutputMode>(index));
                }
                break;
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
        case WM_LBUTTONDOWN:
            if(renderer_.Compare()!=CompareMode::Off && !renderer_.CropEdit()) {
                auto r=renderer_.PreviewContentRect();
                float at=renderer_.Compare()==CompareMode::Vertical ? GET_X_LPARAM(l)-r.x : GET_Y_LPARAM(l)-r.y;
                float extent=renderer_.Compare()==CompareMode::Vertical ? r.width : r.height;
                if(extent>0 && std::abs(at-extent*renderer_.Split())<14) { dividerDrag_=true; SetCapture(preview_); }
            }
            return 0;
        case WM_MOUSEMOVE:
            if(dividerDrag_) {
                auto r=renderer_.PreviewContentRect();
                float extent=renderer_.Compare()==CompareMode::Vertical ? r.width : r.height;
                float at=renderer_.Compare()==CompareMode::Vertical ? GET_X_LPARAM(l)-r.x : GET_Y_LPARAM(l)-r.y;
                if(extent>0) renderer_.SetSplit(at/extent); SyncVideoControls();
            }
            return 0;
        case WM_LBUTTONUP: if(dividerDrag_) { dividerDrag_=false; ReleaseCapture(); } return 0;
        case WM_CAPTURECHANGED: dividerDrag_=false; return 0;
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
    fitted720_=false;
    SyncVideoControls();
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
    SyncVideoControls();
    GuideStatus();
    auto text = std::wstring(reason) + L"\r\nGPU: " + renderer_.AdapterName() +
        L"\r\nUpscaler: " + UpscaleName(renderer_.Mode()) +
        L"\r\nSettings apply on the next capture frame. F1: compare / F2: crop edit / F3: RCAS.";
    SetWindowTextW(status_, text.c_str());
}
void AppUI::OnFrame() {
    auto frame = capture_.TakeLatest(skipped_);
    if (!frame) return;
    ++captured_;
    auto size = frame.ContentSize();
    const bool sizeChanged=inputWidth_!=size.Width || inputHeight_!=size.Height;
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
    if(sizeChanged) { SyncVideoControls(); GuideStatus(); }
}
void AppUI::Status(bool sampleCounters) {
    auto now = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(now - statsTime_).count();
    if (seconds <= 0) return;
    if(sampleCounters) {
        captureFps_ = captured_ / seconds; renderFps_ = rendered_ / seconds;
        captured_ = rendered_ = 0; statsTime_ = now;
    }
    std::wostringstream text;
    auto output = renderer_.OutputSize();
    const bool pending = renderer_.SettingsPending();
    text << std::fixed << std::setprecision(1)
        << L"Input: " << inputWidth_ << L" x " << inputHeight_ << L" @ " << captureFps_ << L" captured fps\r\n"
        << L"Captured window: " << inputWidth_ << L" x " << inputHeight_ << L"\r\n"
        << L"Crop region: " << renderer_.CroppedRegion().width << L" x " << renderer_.CroppedRegion().height << L"\r\n"
        << L"Output: " << output.width << L" x " << output.height << L" @ " << renderFps_ << L" render fps\r\n"
        << L"Preview: " << renderer_.Width() << L" x " << renderer_.Height() << L"\r\n"
        << L"Upscaler: " << UpscaleName(renderer_.Mode());
    if (pending) text << L" (pending next frame)";
    else if (renderer_.Mode() == UpscaleMode::Easu && renderer_.Upscaler().EffectiveMode() == UpscaleMode::Bilinear)
        text << L" (bilinear downscale)";
    text << std::setprecision(2) << L" | Scale: ";
    if (pending) text << L"pending"; else text << renderer_.Scale() << L"x";
    text << L"\r\nSharpen: RCAS " << renderer_.Sharpen();
    text << L"\r\nRCAS: ";
    if(renderer_.CropEdit() || renderer_.Sharpen()==0) text << L"Bypassed";
    else if(pending) text << L"Pending next frame";
    else text << (renderer_.RCASDebugBoost()?L"Active (DEBUG BOOST)":L"Active");
    if(renderer_.CropEdit()) text << L"\r\nCrop edit: full frame / processing suspended";
    auto timing=renderer_.PostProcessTime();
    if(pending || renderer_.CropEdit() || !timing) text << L"\r\nUpscale GPU: N/A | RCAS GPU: N/A\r\nPost-process total: N/A";
    else {
        const double upscale=renderer_.Upscaler().Bypassed()?0:timing->first;
        const double rcas=renderer_.Sharpen()==0?0:timing->second;
        text << L"\r\nUpscale GPU: " << upscale << L" ms | RCAS GPU: " << rcas << L" ms"
             << L"\r\nPost-process total: " << upscale+rcas << L" ms";
    }
    text << L"\r\nCompare: ";
    if(renderer_.CropEdit()) text << L"paused for crop edit";
    else if(renderer_.Compare()==CompareMode::Off) text << L"Off";
    else text << L"Bilinear | " << UpscaleName(renderer_.Mode()) << L" + RCAS " << renderer_.Sharpen()
              << (renderer_.Compare()==CompareMode::Vertical?L" (vertical ":L" (horizontal ") << int(renderer_.Split()*100) << L"%)";
    text << L"\r\n"
        << L"GPU: " << renderer_.AdapterName() << L"\r\nCPU submit + Present: " << submitMs_ << L" ms (not GPU time)\r\n"
        << L"Discarded queued frames: " << skipped_ << L"\r\n"
        << L"Capture generation: " << capture_.Generation() << L"\r\n"
        << L"SDR. Black/frozen image: check protection or minimized source.";
    SetWindowTextW(status_, text.str().c_str());
    GuideStatus();
}
void AppUI::SyncVideoControls() {
    SendMessageW(sharpenSlider_,TBM_SETPOS,TRUE,renderer_.Sharpen());
    SetWindowTextW(sharpenValue_,std::to_wstring(renderer_.Sharpen()).c_str());
    auto crop=renderer_.Crop(); SendMessageW(cropMode_,CB_SETCURSEL,crop.manual?1:0,0);
    int values[]={crop.left,crop.top,crop.right,crop.bottom}; const wchar_t* names[]={L"Left",L"Top",L"Right",L"Bottom"};
    if((crop.pixelCrop || crop.aspect16x9) && inputWidth_>0 && inputHeight_>0) {
        auto r=CropRegion(inputWidth_,inputHeight_,crop);
        values[0]=int(std::lround(r.x*100.0/inputWidth_)); values[1]=int(std::lround(r.y*100.0/inputHeight_));
        values[2]=int(std::lround((inputWidth_-r.x-r.width)*100.0/inputWidth_));
        values[3]=int(std::lround((inputHeight_-r.y-r.height)*100.0/inputHeight_));
    }
    for(int i=0;i<4;++i) {
        SendMessageW(cropSliders_[i],TBM_SETRANGE,TRUE,MAKELPARAM(0,(crop.pixelCrop || crop.aspect16x9)?99:45));
        SendMessageW(cropSliders_[i],TBM_SETPOS,TRUE,values[i]);
        SetWindowTextW(cropLabels_[i],(std::wstring(names[i])+L": "+std::to_wstring(values[i])+L"%").c_str());
        ShowWindow(cropSliders_[i],crop.manual?SW_SHOW:SW_HIDE); ShowWindow(cropLabels_[i],crop.manual?SW_SHOW:SW_HIDE);
    }
    SetWindowTextW(cropEditButton_,renderer_.CropEdit()?L"Finish Crop (F2)":L"Edit Crop (F2)");
    SendMessageW(compareMode_,CB_SETCURSEL,static_cast<WPARAM>(renderer_.Compare()),0);
    SendMessageW(splitSlider_,TBM_SETPOS,TRUE,int(std::lround(renderer_.Split()*100)));
    SetWindowTextW(splitValue_,(std::to_wstring(int(std::lround(renderer_.Split()*100)))+L"%").c_str());
    auto guide=renderer_.Guide(); SendMessageW(inputGuide_,CB_SETCURSEL,static_cast<WPARAM>(guide.mode),0);
    SendMessageW(aspectLock_,CB_SETCURSEL,crop.aspect16x9?1:0,0);
    for(auto control:{guideWidth_,guideHeight_,customLabel_}) ShowWindow(control,guide.mode==GuideMode::Custom?SW_SHOW:SW_HIDE);
    const bool ready=capture_.Running() && inputWidth_>0 && inputHeight_>0;
    EnableWindow(fit720_,ready); EnableWindow(preset720_,ready);
}
void AppUI::ChangeGuide() {
    auto guide=renderer_.Guide();
    auto index=SendMessageW(inputGuide_,CB_GETCURSEL,0,0);
    if(index<0 || index>3) return;
    guide.mode=static_cast<GuideMode>(index);
    if(guide.mode==GuideMode::Custom) {
        BOOL validW{},validH{};
        const UINT w=GetDlgItemInt(main_,GuideWidth,&validW,FALSE),h=GetDlgItemInt(main_,GuideHeight,&validH,FALSE);
        if(validW && validH && w>=1 && h>=1 && w<=16384 && h<=16384) {
            guide.customWidth=w; guide.customHeight=h;
        } else {
            SetWindowTextW(guideWidth_,std::to_wstring(guide.customWidth).c_str());
            SetWindowTextW(guideHeight_,std::to_wstring(guide.customHeight).c_str());
        }
    }
    renderer_.SetInputGuide(guide); SyncVideoControls();
    if(renderer_.CropEdit()) RedrawSettings(); else GuideStatus();
}
void AppUI::GuideStatus() {
    if(!capture_.Running() || inputWidth_<=0 || inputHeight_<=0) {
        SetWindowTextW(guideStatus_,L"Start capture to use Fit / preset."); return;
    }
    auto crop=renderer_.CroppedRegion(); auto guide=renderer_.Guide(); auto target=guide.Target();
    std::wostringstream text;
    text<<L"Captured Window: "<<inputWidth_<<L" x "<<inputHeight_
        <<L"\r\nCurrent Crop: "<<crop.width<<L" x "<<crop.height
        <<L"\r\nProcessing input: "<<ProcessingInput(crop);
    if(target.width) {
        text<<L"\r\n\r\nTarget Guide: "<<target.width<<L" x "<<target.height
            <<L"\r\nDifference: "<<std::showpos<<int(crop.width)-int(target.width)<<L" x "<<int(crop.height)-int(target.height)<<std::noshowpos
            <<L"\r\n"<<(guide.mode==GuideMode::HD720?L"720p":guide.mode==GuideMode::HD1080?L"1080p":L"Custom")
            <<L" target: "<<MatchName(MatchTarget(crop,target));
        if(target.width>UINT(inputWidth_) || target.height>UINT(inputHeight_)) text<<L"\r\nTarget exceeds captured frame.";
    } else text<<L"\r\n\r\nTarget Guide: Off";
    if((fitted720_ || guide.mode==GuideMode::HD720) && (inputWidth_<1280 || inputHeight_<720))
        text<<L"\r\nSource smaller than 720p target";
    auto monitor=renderer_.MonitorSize(); auto recommendation=RecommendedOutput(crop,monitor.width,monitor.height);
    if(recommendation.width) text<<L"\r\n\r\nRecommended Output: "<<recommendation.width<<L" x "<<recommendation.height;
    if(renderer_.CropEdit()) text<<L"\r\n\r\nSolid cyan: current crop\r\nDashed amber: target guide";
    else text<<L"\r\n\r\nPress Edit Crop to show guides.";
    SetWindowTextW(guideStatus_,text.str().c_str());
}
void AppUI::UpdateVideoSettings(int controlId) {
    const auto previousSharpen=renderer_.Sharpen();
    renderer_.SetSharpen(static_cast<int>(SendMessageW(sharpenSlider_,TBM_GETPOS,0,0)));
    if(renderer_.Sharpen()>0) lastSharpen_=renderer_.Sharpen();
    auto crop=renderer_.Crop();
    auto previousCrop=crop;
    if(!crop.pixelCrop && !crop.aspect16x9) {
        crop.left=static_cast<int>(SendMessageW(cropSliders_[0],TBM_GETPOS,0,0)); crop.top=static_cast<int>(SendMessageW(cropSliders_[1],TBM_GETPOS,0,0));
        crop.right=static_cast<int>(SendMessageW(cropSliders_[2],TBM_GETPOS,0,0)); crop.bottom=static_cast<int>(SendMessageW(cropSliders_[3],TBM_GETPOS,0,0));
    } else if(controlId>=CropLeft && controlId<CropLeft+4)
        crop=AdjustCropEdge(inputWidth_,inputHeight_,crop,controlId-CropLeft,int(SendMessageW(cropSliders_[controlId-CropLeft],TBM_GETPOS,0,0)));
    renderer_.SetCrop(crop); renderer_.SetSplit(float(SendMessageW(splitSlider_,TBM_GETPOS,0,0))/100);
    SyncVideoControls();
    if(previousSharpen!=renderer_.Sharpen() || previousCrop!=crop) RedrawSettings();
}
void AppUI::RedrawSettings() {
    if(!capture_.Running()) return;
    auto begin=std::chrono::steady_clock::now();
    if(renderer_.Redraw()) ++rendered_;
    submitMs_=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    Status(false); // Update RCAS state immediately, keep the 1-second FPS sample.
}
bool AppUI::Hotkey(WPARAM key) {
    if(key==VK_F1) renderer_.SetCompare(renderer_.Compare()==CompareMode::Off?lastCompare_:CompareMode::Off);
    else if(key==VK_F2) {
        auto crop=renderer_.Crop(); crop.manual=true; renderer_.SetCrop(crop); renderer_.SetCropEdit(!renderer_.CropEdit());
    } else if(key==VK_F3) {
        if(renderer_.Sharpen()) { lastSharpen_=renderer_.Sharpen(); renderer_.SetSharpen(0); }
        else renderer_.SetSharpen(lastSharpen_);
    } else return false;
    SyncVideoControls();
    if(key==VK_F3 || key==VK_F2) RedrawSettings();
    return true;
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
