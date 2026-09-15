#pragma once
#include <windows.h>
#include <string>
#include <vector>
struct WindowEntry { HWND hwnd; DWORD processId; std::wstring title; };
std::vector<WindowEntry> ListWindows();
