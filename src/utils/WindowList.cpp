#include "WindowList.h"
#include <dwmapi.h>
#include <algorithm>
std::vector<WindowEntry> ListWindows() {
    std::vector<WindowEntry> result;
    EnumWindows([](HWND hwnd, LPARAM context) -> BOOL {
        DWORD pid{};
        GetWindowThreadProcessId(hwnd, &pid);
        if (!IsWindowVisible(hwnd) || pid == GetCurrentProcessId()) return TRUE;
        if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
        DWORD cloaked{};
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
        const int length = GetWindowTextLengthW(hwnd);
        if (!length) return TRUE;
        std::wstring title(static_cast<size_t>(length) + 1, L'\0');
        title.resize(GetWindowTextW(hwnd, title.data(), length + 1));
        if (!title.empty()) reinterpret_cast<std::vector<WindowEntry>*>(context)->push_back({hwnd, pid, std::move(title)});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    std::sort(result.begin(), result.end(), [](auto const& a, auto const& b) { return a.title < b.title; });
    return result;
}
