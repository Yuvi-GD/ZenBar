// ZenBar.cpp — Entry point. This file does ONLY 3 things:
//   1. Enable DPI awareness (so the bar is sharp on all screens)
//   2. Initialize COM (required by VolumeWidget's audio API)
//   3. Create the AppBar and run the message loop
//
// All actual logic lives in Core/AppBar.cpp.

#include "include\pch.h"
#include <winrt/base.h>
#include "Core\ZenBar.h"
#include "Core/AppBar.h"

int APIENTRY wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_     LPWSTR    lpCmdLine,
    _In_     int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    // ---- 0. Single Instance Check ----
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\ZenBarSingleInstanceMutex");
    if (hMutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // ---- 1. DPI awareness ----
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // ---- 2. COM initialization ----
    // Use WinRT initialization to allow PowerManager to work.
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    // ---- 3. Create and run the status bar ----
    {
        AppBar bar;
        if (!bar.Create(hInstance)) {
            winrt::uninit_apartment();
            CloseHandle(hMutex);
            return 1;
        }

        // Standard Win32 message loop.
        MSG msg = {};
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        // 'bar' destructor runs here
    }

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    CoUninitialize();

    // ExitProcess ensures all background threads (WinRT fire_and_forget coroutines,
    // media std::thread detached workers, etc.) are forcibly terminated so the
    // process doesn't linger in Task Manager after the user clicks "Close Status Bar".
    ExitProcess(0);
}

