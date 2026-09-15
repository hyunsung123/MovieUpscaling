#include "ui/AppUI.h"
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        int result;
        { AppUI app; result = app.Run(instance, show); }
        winrt::uninit_apartment();
        return result;
    } catch (winrt::hresult_error const& e) {
        MessageBoxW(nullptr, e.message().c_str(), L"Window GPU Preview", MB_OK | MB_ICONERROR);
    } catch (std::exception const& e) {
        MessageBoxA(nullptr, e.what(), "Window GPU Preview", MB_OK | MB_ICONERROR);
    }
    return 1;
}
