#pragma once
#include "capture/WindowCapture.h"
#include "renderer/D3DRenderer.h"
#include "utils/WindowList.h"
#include <chrono>
class AppUI {
public:
    int Run(HINSTANCE instance, int show);
private:
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(HWND, UINT, WPARAM, LPARAM);
    void Refresh();
    void Start();
    void Stop(const wchar_t* reason);
    void OnFrame();
    void Fullscreen();
    void Status();
    void SyncVideoControls();
    void UpdateVideoSettings();
    bool Hotkey(WPARAM key);
    HWND main_{}, preview_{}, sources_{}, status_{}, start_{}, stop_{}, upscale_{}, output_{};
    bool fullscreen_{};
    HWND sharpenSlider_{},sharpenValue_{},cropMode_{},cropEditButton_{},compareMode_{},splitSlider_{},splitValue_{};
    std::array<HWND,4> cropSliders_{},cropLabels_{};
    int lastSharpen_{25};
    CompareMode lastCompare_{CompareMode::Vertical};
    bool dividerDrag_{};
    WINDOWPLACEMENT placement_{sizeof(WINDOWPLACEMENT)};
    std::vector<WindowEntry> windows_;
    D3DRenderer renderer_;
    WindowCapture capture_;
    uint64_t captured_{}, rendered_{}, skipped_{};
    int inputWidth_{}, inputHeight_{};
    double captureFps_{}, renderFps_{}, submitMs_{};
    std::chrono::steady_clock::time_point statsTime_{};
};
