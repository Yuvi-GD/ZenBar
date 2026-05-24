#pragma once
// Core/AppBar.h — The status bar window.
// ----------------------------------------
// Owns the Win32 window, AppBar shell registration, renderer, and message loop.
// Widget logic is entirely in UI/Widgets.h — AppBar just calls the 5-function API.

#include "include\pch.h"
#include "Renderer.h"

class AppBar {
public:
    AppBar();
    ~AppBar();

    // Creates the window, registers as AppBar, initializes widgets.
    // Returns false if something critical failed.
    bool Create(HINSTANCE hInstance);

private:
    HWND         m_hWnd        = nullptr;
    HINSTANCE    m_hInst       = nullptr;
    int          m_barH        = 32;
    APPBARDATA   m_abd         = {};
    HPOWERNOTIFY m_hPowerNotify = nullptr; // Battery event notification handle
    HPOWERNOTIFY m_hBatterySaverNotify = nullptr;
    HPOWERNOTIFY m_hEnergySaverNotifyW11 = nullptr;
    Renderer     m_renderer    = {};

    // Active window title — shown in left zone (like macOS menu bar)
    WCHAR m_activeTitle[128]   = {};

    // Battery power notification GUID (GUID_BATTERY_PERCENTAGE_REMAINING)
    // Defined manually to avoid requiring batclass.h (DDK header)
    static constexpr GUID BATTERY_GUID = {
        0xa7ad8041, 0xb45a, 0x4cae,
        { 0x87, 0xa3, 0xee, 0xcb, 0xb4, 0x68, 0xa9, 0xe1 }
    };

    static constexpr WCHAR CLASS_NAME[] = L"ZenBarWnd";
    static constexpr UINT  TIMER_UPDATE = 1;
    static constexpr UINT  TIMER_FAST   = 2;
    static constexpr UINT  MSG_APPBAR   = WM_USER + 100;

public:
    bool GetAutoHide() const { return m_autoHide; }
    void SetAutoHide(bool val) { m_autoHide = val; m_reserveSpace = !val; }

    bool GetShowControls() const { return m_showControls; }
    void SetShowControls(bool val) { m_showControls = val; }

    bool GetControlsOnlyAutoHide() const { return m_controlsOnlyAutoHide; }
    void SetControlsOnlyAutoHide(bool val) { m_controlsOnlyAutoHide = val; }

    int GetAutoHideDelayMs() const { return m_autoHideDelayMs; }
    void SetAutoHideDelayMs(int ms) { m_autoHideDelayMs = ms; }
    
    int GetBarHeight() const { return m_barH; }
    void SetBarHeight(int h) { m_barH = max(16, min(64, h)); }
    
    int GetVolScrollStep() const { return m_volScrollStep; }
    void SetVolScrollStep(int s) { m_volScrollStep = max(1, min(10, s)); }
    int GetBriScrollStep() const { return m_briScrollStep; }
    void SetBriScrollStep(int s) { m_briScrollStep = max(1, min(10, s)); }
    
    void ApplySettingsAndSave();

    void SetRunOnStartup(bool enable);
    bool IsRunOnStartup();

    bool m_autoHide = false;
    bool m_reserveSpace = true;
    int m_hoverTicks = 0;
    
    bool m_showControls = true;
    bool m_controlsOnlyAutoHide = true;
    bool m_isContextMenuOpen = false;
    bool m_isTrackingMouse = false;
    int m_autoHideDelayMs = 500;
    int m_volScrollStep = 2;
    int m_briScrollStep = 5;
    
    WCHAR m_configPath[MAX_PATH] = {};

    // Internal setup
    bool RegisterWindowClass(HINSTANCE hInstance);
    bool CreateBarWindow(HINSTANCE hInstance);
    void PositionBar();
    void Paint(HDC hdc);
    void UpdateActiveWindow();
    bool IsFullscreenWindowActive();
    void LoadSettings();
    void SaveSettings();

    // Win32 WndProc — static dispatch → instance handler
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
