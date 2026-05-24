// Core/AppBar.cpp — Window creation, shell registration, message loop, painting.
// -------------------------------------------------------------------------------
// AppBar's job: own the Win32 window. Everything widget-related is delegated to
// the 5-function Widgets API (Init / UpdateAll / DrawZone / DrawBatteryBorder / Destroy).

#include "include\pch.h"
#include "AppBar.h"
#include "Renderer.h"
#include "UI/SettingsWindow.h"
#include "../UI/Widgets.h"
#include <propsys.h>
#include <propkey.h>

// =====================================================================
// Constructor / Destructor
// =====================================================================

AppBar::AppBar() {}

AppBar::~AppBar()
{
    if (m_hPowerNotify) {
        UnregisterPowerSettingNotification(m_hPowerNotify);
        m_hPowerNotify = nullptr;
    }
    Widgets_Destroy();          // Free COM/DDC-CI handles
    Renderer_Destroy(m_renderer); // Free fonts + back-buffer
}

// =====================================================================
// Create
// =====================================================================

bool AppBar::Create(HINSTANCE hInstance)
{
    m_hInst = hInstance;

    if (!RegisterWindowClass(hInstance)) return false;
    if (!CreateBarWindow(hInstance))     return false;

    // Load persisted settings FIRST — so m_barH, m_autoHide etc. are correct
    // before we initialize fonts and position the bar.
    LoadSettings();
    Widgets_UpdateSettings(m_autoHide, m_showControls, m_controlsOnlyAutoHide);
    Widgets_SetScrollSteps(m_volScrollStep, m_briScrollStep);

    // Scale default bar height to current DPI if not overridden by saved settings
    int dpi = (int)GetDpiForWindow(m_hWnd);
    if (dpi <= 0) dpi = 96;
    // Only apply DPI scaling to DEFAULT value (32). Saved values are stored in raw pixels.
    // If barH is still at the hardcoded default of 32, scale it.
    if (m_barH == 32) {
        m_barH = MulDiv(32, dpi, 96);
    }

    // Init fonts AFTER LoadSettings so font size matches saved barH
    Renderer_Init(m_renderer, dpi, m_barH);

    // Mica / Acrylic / blur effect
    Renderer_ApplyDWMEffect(m_hWnd);

    // Initialize all widgets
    Widgets_Init(m_hWnd);

    // Reserve screen space at top edge
    m_abd.cbSize           = sizeof(APPBARDATA);
    m_abd.hWnd             = m_hWnd;
    m_abd.uCallbackMessage = MSG_APPBAR;
    SHAppBarMessage(ABM_NEW, &m_abd);
    PositionBar();

    // 86% opacity — dark bar, slightly see-through
    SetLayeredWindowAttributes(m_hWnd, 0, 220, LWA_ALPHA);

    // Register for standard power events
    m_hPowerNotify = RegisterPowerSettingNotification(m_hWnd, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE);

    // Register specifically for Battery Saver status (Windows 10 / 11 23H2)
    m_hBatterySaverNotify = RegisterPowerSettingNotification(m_hWnd, &GUID_POWER_SAVING_STATUS, DEVICE_NOTIFY_WINDOW_HANDLE);

    // Register specifically for Energy Saver status (Windows 11 24H2)
    static constexpr GUID GUID_ENERGY_SAVER_STATUS_W11 = { 0x550e8400, 0xe29b, 0x41d4, {0xa7, 0x16, 0x44, 0x66, 0x55, 0x44, 0x00, 0x00} };
    m_hEnergySaverNotifyW11 = RegisterPowerSettingNotification(m_hWnd, &GUID_ENERGY_SAVER_STATUS_W11, DEVICE_NOTIFY_WINDOW_HANDLE);

    // Timers:
    // UPDATE = 1000ms for clock, CPU, network, volume
    // FAST = 100ms for instant fullscreen detection and auto-hide mouse tracking
    SetTimer(m_hWnd, TIMER_UPDATE, 1000, nullptr);
    SetTimer(m_hWnd, TIMER_FAST, 100, nullptr);

    ShowWindow(m_hWnd, SW_SHOWNA); // Show without stealing focus
    UpdateWindow(m_hWnd);
    return true;
}

// =====================================================================
// RegisterWindowClass
// =====================================================================

bool AppBar::RegisterWindowClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex  = {};
    wcex.cbSize        = sizeof(WNDCLASSEXW);
    wcex.style         = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc   = AppBar::WndProc;
    wcex.hInstance     = hInstance;
    wcex.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(107));
    wcex.hIconSm       = LoadIcon(hInstance, MAKEINTRESOURCE(107));
    wcex.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr; // We paint everything ourselves
    wcex.lpszClassName = CLASS_NAME;
    return RegisterClassExW(&wcex) != 0;
}

// =====================================================================
// CreateBarWindow
// =====================================================================

bool AppBar::CreateBarWindow(HINSTANCE hInstance)
{
    // Force modern dark theme for standard Win32 menus (context menu)
    HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hUxtheme) {
        using fnSetPreferredAppMode = int (WINAPI*)(int);
        auto SetPreferredAppMode = (fnSetPreferredAppMode)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135)); // 135 is the undocumented ordinal
        if (SetPreferredAppMode) {
            SetPreferredAppMode(2); // 2 = ForceDark
        }
        FreeLibrary(hUxtheme);
    }

    DWORD exStyle = WS_EX_TOPMOST      // Always on top
                  | WS_EX_TOOLWINDOW   // Hidden from Alt+Tab / taskbar
                  | WS_EX_LAYERED      // For SetLayeredWindowAttributes
                  | WS_EX_NOACTIVATE;  // Clicking bar doesn't steal focus

    m_hWnd = CreateWindowExW(
        exStyle, CLASS_NAME, L"ZenBar", WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInstance,
        this  // Passed to WM_NCCREATE so WndProc can find this instance
    );

    // Apply immersive dark mode attribute to the window itself
    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hWnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));

    return m_hWnd != nullptr;
}

// =====================================================================
// PositionBar — full-width stripe at top of screen
// =====================================================================

void AppBar::PositionBar()
{
    m_abd.uEdge      = ABE_TOP;
    m_abd.rc.left    = 0;
    m_abd.rc.top     = 0;
    m_abd.rc.right   = GetSystemMetrics(SM_CXSCREEN);
    m_abd.rc.bottom  = m_barH;

    if (m_reserveSpace && !m_autoHide) {
        SHAppBarMessage(ABM_QUERYPOS, &m_abd); // Windows adjusts rc to avoid conflicts
        SHAppBarMessage(ABM_SETPOS,   &m_abd); // Reserves the space
    } else {
        APPBARDATA emptyAbd = m_abd;
        emptyAbd.rc = { 0, 0, 0, 0 };
        SHAppBarMessage(ABM_SETPOS, &emptyAbd); // Frees the reserved space
    }

    SetWindowPos(m_hWnd, HWND_TOPMOST,
                 m_abd.rc.left, m_abd.rc.top,
                 m_abd.rc.right - m_abd.rc.left,
                 m_barH,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

// =====================================================================
// UpdateActiveWindow — reads the foreground window title
// =====================================================================

void AppBar::UpdateActiveWindow()
{
    HWND hFg = GetForegroundWindow();
    if (!hFg || hFg == m_hWnd) return; // Don't overwrite with our own title
    GetWindowTextW(hFg, m_activeTitle, _countof(m_activeTitle));

    // Fetch the app's 16x16 icon. Use ICON_SMALL2 (the best small icon).
    // Fallback to GCLP_HICONSM (class icon). If neither available, null = no icon drawn.
    DWORD_PTR result = 0;
    SendMessageTimeoutW(hFg, WM_GETICON, ICON_SMALL2, 0,
                        SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 50, &result);
    HICON hIco = (HICON)result;
    if (!hIco) {
        hIco = (HICON)GetClassLongPtrW(hFg, GCLP_HICONSM);
    }
    m_activeIcon = hIco; // Owned by the window — do NOT destroy
}

// =====================================================================
// Paint — full rendering pipeline (called on WM_PAINT)
// =====================================================================

void AppBar::Paint(HDC hdc)
{
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    int barW = rc.right;
    int barH = rc.bottom;

    // 1. Create back-buffer, fill dark background
    HDC memDC = Renderer_BeginFrame(m_renderer, hdc, barW, barH);

    HFONT fontMain = m_renderer.fontMain;
    HFONT fontBold = m_renderer.fontBold;

    // 2. LEFT ZONE: ZenBar icon + active app icon + active window title
    {
        int x = 14;
        HICON hZenBarIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(107)); // IDI_ZenBar
        DrawIconEx(memDC, x, (barH - 16) / 2, hZenBarIcon, 16, 16, 0, nullptr, DI_NORMAL);
        x += 24; // 16 width + 8 padding

        // Draw active app icon (if available) — no fallback, ZenBar logo is enough
        if (m_activeIcon) {
            DrawIconEx(memDC, x, (barH - 16) / 2, m_activeIcon, 16, 16, 0, nullptr, DI_NORMAL);
            x += 22; // 16 width + 6 padding
        }

        if (m_activeTitle[0]) {
            // Trim to 40 chars so it doesn't bleed into the center
            WCHAR title[44] = {};
            if (wcslen(m_activeTitle) > 40) {
                wcsncpy_s(title, m_activeTitle, 37);
                wcscat_s(title, L"...");
            } else {
                wcscpy_s(title, m_activeTitle);
            }
            DrawBarText(memDC, title, x, 0, barH, fontMain, BarColors::WINLABEL);
        }
    }

    // 3. CENTER ZONE: clock
    Widgets_DrawZone(Zone::Center, memDC, fontMain, fontBold, 0, barH, barW);

    // 4. RIGHT ZONE: CPU, Network, Volume, Brightness, Battery (right-to-left)
    Widgets_DrawZone(Zone::Right, memDC, fontMain, fontBold, barW, barH, barW);

    // 5. Battery border at bottom edge — drawn last, always on top of background
    Widgets_DrawBatteryBorder(memDC, barW, barH);

    // 6. Blit back-buffer to screen (one shot — no flicker)
    Renderer_EndFrame(m_renderer, hdc, barW, barH);
}

// =====================================================================
// WndProc (static) — standard GWLP_USERDATA instance dispatch
// =====================================================================

LRESULT CALLBACK AppBar::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        auto* pCS = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCS->lpCreateParams));
    }
    auto* pThis = reinterpret_cast<AppBar*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    return pThis ? pThis->HandleMessage(hWnd, msg, wParam, lParam)
                 : DefWindowProcW(hWnd, msg, wParam, lParam);
}

// =====================================================================
// HandleMessage — all message handling
// =====================================================================

LRESULT AppBar::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        Paint(hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CONTEXTMENU: {
        int xPos = (short)LOWORD(lParam);
        int yPos = (short)HIWORD(lParam);

        RECT rcBar;
        GetWindowRect(hWnd, &rcBar);

        if (xPos == -1 && yPos == -1) {
            xPos = rcBar.left;
            yPos = rcBar.bottom;
        } else {
            // Hit test widgets using client coordinates using the original cursor pos
            POINT pt = { xPos, yPos };
            ScreenToClient(hWnd, &pt);

            // Always force the menu to spawn exactly below the bar instead of over it
            yPos = rcBar.bottom;

            for (int i = 0; i < g_widgetCount; i++) {
                Widget& w = g_widgets[i];
                if (w.visible && PtInRect(&w.rect, pt)) {
                    if (wcscmp(w.name, L"Volume") == 0) {
                        ShellExecuteW(nullptr, L"open", L"ms-settings:sound", nullptr, nullptr, SW_SHOW);
                        return 0;
                    } else if (wcscmp(w.name, L"Brightness") == 0) {
                        ShellExecuteW(nullptr, L"open", L"ms-settings:display", nullptr, nullptr, SW_SHOW);
                        return 0;
                    } else if (wcscmp(w.name, L"Battery") == 0) {
                        ShellExecuteW(nullptr, L"open", L"ms-settings:batterysaver", nullptr, nullptr, SW_SHOW);
                        return 0;
                    }
                }
            }
        }

        HMENU hMenu = CreatePopupMenu();
        if (hMenu) {
            HMENU hWidgetsMenu = CreatePopupMenu();
            for (int i = 0; i < g_widgetCount; i++) {
                if (wcscmp(g_widgets[i].name, L"App Controls") == 0) continue; // Settings handle App Controls
                UINT flags = MF_STRING;
                if (g_widgets[i].visible) {
                    flags |= MF_CHECKED;
                }
                AppendMenuW(hWidgetsMenu, flags, 1000 + i, g_widgets[i].name);
            }
            AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hWidgetsMenu, L"Toggle Widgets");
            AppendMenuW(hMenu, MF_STRING, 4000, L"Task Manager");
            AppendMenuW(hMenu, MF_STRING, 4001, L"Taskbar Settings");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, 3000, L"Settings...");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, 9999, L"Close StatusBar");

            SetForegroundWindow(hWnd);
            m_isContextMenuOpen = true;
            int selection = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                           xPos, yPos, 0, hWnd, nullptr);
            m_isContextMenuOpen = false;
            DestroyMenu(hMenu);

            if (selection == 9999) {
                PostMessageW(hWnd, WM_CLOSE, 0, 0);
            } else if (selection == 4000) {
                ShellExecuteW(nullptr, L"open", L"taskmgr", nullptr, nullptr, SW_SHOW);
            } else if (selection == 4001) {
                ShellExecuteW(nullptr, L"open", L"ms-settings:taskbar", nullptr, nullptr, SW_SHOW);
            } else if (selection >= 1000 && selection < 1000 + g_widgetCount) {
                int widgetIndex = selection - 1000;
                Widgets_ToggleVisibility(widgetIndex);
                Widgets_UpdateAll();
                InvalidateRect(hWnd, nullptr, FALSE);
            } else if (selection == 3000) {
                ShowSettingsDialog(hWnd, this);
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!m_isTrackingMouse) {
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            m_isTrackingMouse = true;
        }

        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        bool needsRedraw = false;
        
        for (int i = 0; i < g_widgetCount; i++) {
            Widget& w = g_widgets[i];
            if (!w.visible) continue;
            
            if (wcscmp(w.name, L"App Controls") == 0) {
                int oldHover = w.app_hoverIndex;
                if (PtInRect(&w.app_rectMin, pt)) w.app_hoverIndex = 1;
                else if (PtInRect(&w.app_rectMax, pt)) w.app_hoverIndex = 2;
                else if (PtInRect(&w.app_rectClose, pt)) w.app_hoverIndex = 3;
                else w.app_hoverIndex = 0;
                
                if (oldHover != w.app_hoverIndex) {
                    needsRedraw = true;
                }
            }
        }
        
        if (needsRedraw) {
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        m_isTrackingMouse = false;
        bool needsRedraw = false;
        for (int i = 0; i < g_widgetCount; i++) {
            Widget& w = g_widgets[i];
            if (wcscmp(w.name, L"App Controls") == 0) {
                if (w.app_hoverIndex != 0) {
                    w.app_hoverIndex = 0;
                    needsRedraw = true;
                }
            }
        }
        if (needsRedraw) {
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        ScreenToClient(hWnd, &pt);

        short delta = (short)HIWORD(wParam);
        int dir = (delta > 0) ? 1 : -1;

        for (int i = 0; i < g_widgetCount; i++) {
            Widget& w = g_widgets[i];
            if (w.visible && PtInRect(&w.rect, pt)) {
                if (wcscmp(w.name, L"Volume") == 0) {
                    Widgets_AdjustVolume(dir);
                    // Only update the volume widget (fast COM read), not the entire bar
                    Widgets_UpdateByName(L"Volume");
                    InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                } else if (wcscmp(w.name, L"Brightness") == 0) {
                    Widgets_AdjustBrightness(dir);
                    // Only update brightness (fast DDC read), not the entire bar
                    Widgets_UpdateByName(L"Brightness");
                    InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                } else if (wcscmp(w.name, L"Media Controls") == 0) {
                    // CycleMediaSession already kicks FetchMediaAsync internally — just repaint
                    Widgets_CycleMediaSession(dir);
                    InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                }
            }
        }
        break;
    }

    case WM_LBUTTONUP: {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        for (int i = 0; i < g_widgetCount; i++) {
            Widget& w = g_widgets[i];
            if (w.visible && PtInRect(&w.rect, pt)) {
                if (wcscmp(w.name, L"Volume") == 0) {
                    POINT ptScreen = {};
                    GetCursorPos(&ptScreen);
                    RECT rcBar;
                    GetWindowRect(hWnd, &rcBar);
                    Widgets_ShowAudioDeviceMenu(hWnd, ptScreen.x, rcBar.bottom);
                    return 0;
                } else if (wcscmp(w.name, L"Brightness") == 0) {
                    // Left-click on Brightness does nothing
                    return 0;
                } else if (wcscmp(w.name, L"App Controls") == 0) {
                    HWND hFg = GetForegroundWindow();
                    if (hFg && hFg != hWnd) {
                        if (PtInRect(&w.app_rectMin, pt)) {
                            PostMessageW(hFg, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                        } else if (PtInRect(&w.app_rectMax, pt)) {
                            WINDOWPLACEMENT wp = { sizeof(wp) };
                            GetWindowPlacement(hFg, &wp);
                            if (wp.showCmd == SW_SHOWMAXIMIZED)
                                PostMessageW(hFg, WM_SYSCOMMAND, SC_RESTORE, 0);
                            else
                                PostMessageW(hFg, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
                        } else if (PtInRect(&w.app_rectClose, pt)) {
                            PostMessageW(hFg, WM_SYSCOMMAND, SC_CLOSE, 0);
                        }
                    }
                } else if (wcscmp(w.name, L"Media Controls") == 0) {
                    // Run media commands on a detached background thread.
                    // We can't use co_await on the UI STA thread without a WinRT dispatcher,
                    // but std::thread creates a fresh MTA-capable thread where .get() works fine.
                    bool doPrev = PtInRect(&w.media_rectPrev, pt) && w.media_canPrev;
                    bool doPlay = PtInRect(&w.media_rectPlay, pt) && w.media_canPlayPause;
                    bool doNext = PtInRect(&w.media_rectNext, pt) && w.media_canNext;
                    bool doText = PtInRect(&w.media_rectText, pt);

                    if (doText && w.media_appId[0] != 0) {
                        // Find the existing window and bring it to the front
                        struct FindAppCtx {
                            const wchar_t* appId;
                            HWND foundHwnd;
                        } ctx = { w.media_appId, nullptr };

                        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
                            FindAppCtx* pCtx = (FindAppCtx*)lParam;
                            if (IsWindowVisible(hwnd)) {
                                IPropertyStore* pps = nullptr;
                                if (SUCCEEDED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&pps)))) {
                                    PROPVARIANT prop;
                                    PropVariantInit(&prop);
                                    if (SUCCEEDED(pps->GetValue(PKEY_AppUserModel_ID, &prop)) && prop.vt == VT_LPWSTR) {
                                        if (wcscmp(prop.pwszVal, pCtx->appId) == 0) {
                                            pCtx->foundHwnd = hwnd;
                                            PropVariantClear(&prop);
                                            pps->Release();
                                            return FALSE; // Stop enumeration
                                        }
                                    }
                                    PropVariantClear(&prop);
                                    pps->Release();
                                }
                            }
                            return TRUE; // Continue
                        }, (LPARAM)&ctx);

                        if (ctx.foundHwnd) {
                            // SetForegroundWindow silently fails when ZenBar (WS_EX_NOACTIVATE)
                            // is not the foreground owner. SwitchToThisWindow bypasses that.
                            if (IsIconic(ctx.foundHwnd)) ShowWindow(ctx.foundHwnd, SW_RESTORE);
                            AllowSetForegroundWindow(ASFW_ANY);
                            SwitchToThisWindow(ctx.foundHwnd, TRUE);
                        } else {
                            // Fallback to launching
                            wchar_t uri[256];
                            swprintf_s(uri, L"shell:AppsFolder\\%s", w.media_appId);
                            ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOW);
                        }
                        return 0;
                    }

                    if (doPrev || doPlay || doNext) {
                        if (doPlay) {
                            w.media_isPlaying = !w.media_isPlaying;
                            InvalidateRect(hWnd, nullptr, FALSE);
                        }
                        
                        int sessionIdx = g_mediaSessionIndex; 
                        std::thread([hWnd, doPrev, doPlay, doNext, sessionIdx]() {
                            winrt::init_apartment();
                            try {
                                using namespace winrt::Windows::Media::Control;
                                auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
                                if (manager) {
                                    auto sessions = manager.GetSessions();
                                    int count = (int)sessions.Size();
                                    int idx = min(sessionIdx, count - 1);
                                    if (idx >= 0) {
                                        auto session = sessions.GetAt(idx);
                                        if (doPrev)       session.TrySkipPreviousAsync().get();
                                        else if (doPlay)  session.TryTogglePlayPauseAsync().get();
                                        else if (doNext)  session.TrySkipNextAsync().get();
                                        
                                        // Do NOT immediately call Widgets_UpdateByName(L"Media Controls")
                                        // The OS might not have updated GetPlaybackInfo() yet!
                                        // Letting the 1-second auto-refresh timer fetch the true state
                                        // avoids the flicker where it reverts and then flips again.
                                    }
                                }
                            } catch (...) {}
                            winrt::uninit_apartment();
                        }).detach();
                        return 0;
                    }
                }
            }
        }
        break;
    }

    case WM_ERASEBKGND:
        return 1; // We handle background in WM_PAINT — prevent white flash

    case WM_TIMER:
        if (wParam == TIMER_UPDATE) {
            UpdateActiveWindow();
            Widgets_UpdateAll();
            InvalidateRect(hWnd, nullptr, FALSE); // Redraw without erase (no flicker)
        } else if (wParam == TIMER_FAST) {
            bool shouldShow = true;
            bool isFullscreen = IsFullscreenWindowActive();

            if (m_isContextMenuOpen) {
                // NEVER hide the bar while the context menu is actively shown on screen
                shouldShow = true;
            } else if (m_autoHide) {
                POINT pt;
                GetCursorPos(&pt);
                if (pt.y == 0) {
                    m_hoverTicks++;
                    int threshold = m_autoHideDelayMs / 100;
                    if (threshold <= 0) threshold = 1;
                    
                    if (m_hoverTicks >= threshold) {
                        shouldShow = true;
                    } else {
                        shouldShow = IsWindowVisible(hWnd);
                    }
                } else {
                    if (pt.y > m_barH) {
                        m_hoverTicks = 0;
                        shouldShow = false;
                    } else {
                        shouldShow = IsWindowVisible(hWnd);
                    }
                }
            } else {
                if (isFullscreen) {
                    shouldShow = false;
                }
            }

            if (shouldShow && !IsWindowVisible(hWnd)) {
                ShowWindow(hWnd, SW_SHOWNA);
                SetWindowPos(hWnd, HWND_TOPMOST, m_abd.rc.left, m_abd.rc.top, m_abd.rc.right - m_abd.rc.left, m_barH, SWP_SHOWWINDOW | SWP_NOACTIVATE);
            } else if (!shouldShow && IsWindowVisible(hWnd)) {
                ShowWindow(hWnd, SW_HIDE);
            }
        }
        return 0;

    case WM_POWERBROADCAST:
        if (wParam == PBT_POWERSETTINGCHANGE) {
            auto pbs = (POWERBROADCAST_SETTING*)lParam;
            if (pbs->PowerSetting == GUID_CONSOLE_DISPLAY_STATE && pbs->DataLength == sizeof(DWORD)) {
                DWORD isOn = *(DWORD*)pbs->Data;
                if (isOn) {
                    SetTimer(hWnd, TIMER_UPDATE, 1000, nullptr);
                    SetTimer(hWnd, TIMER_FAST, 100, nullptr);
                } else {
                    KillTimer(hWnd, TIMER_UPDATE);
                    KillTimer(hWnd, TIMER_FAST);
                }
            } else if (pbs->PowerSetting == GUID_POWER_SAVING_STATUS && pbs->DataLength == sizeof(DWORD)) {
                extern bool g_batterySaverActive;
                g_batterySaverActive = (*(DWORD*)pbs->Data != 0);
                Widgets_UpdateByName(L"Battery");
                InvalidateRect(hWnd, nullptr, FALSE);
            } else {
                // Check if it's the Windows 11 24H2 Energy Saver GUID
                static constexpr GUID GUID_ENERGY_SAVER_STATUS_W11 = { 0x550e8400, 0xe29b, 0x41d4, {0xa7, 0x16, 0x44, 0x66, 0x55, 0x44, 0x00, 0x00} };
                if (pbs->PowerSetting == GUID_ENERGY_SAVER_STATUS_W11 && pbs->DataLength == sizeof(DWORD)) {
                    extern bool g_batterySaverActive;
                    g_batterySaverActive = (*(DWORD*)pbs->Data != 0);
                    Widgets_UpdateByName(L"Battery");
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }
        } else if (wParam == PBT_APMPOWERSTATUSCHANGE) {
            Widgets_UpdateByName(L"Battery");
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return TRUE;

    case MSG_APPBAR:
        if (lParam == ABN_POSCHANGED) {
            SHAppBarMessage(ABM_QUERYPOS, &m_abd);
            SetWindowPos(hWnd, HWND_TOPMOST,
                         m_abd.rc.left, m_abd.rc.top,
                         m_abd.rc.right  - m_abd.rc.left,
                         m_abd.rc.bottom - m_abd.rc.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        } else if (lParam == ABN_FULLSCREENAPP) {
            BOOL isFullscreen = (BOOL)wParam;
            if (isFullscreen) {
                ShowWindow(hWnd, SW_HIDE);
            } else {
                ShowWindow(hWnd, SW_SHOWNA);
                PositionBar();
            }
        }
        return 0;

    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        PositionBar();
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_DESTROY:
        if (m_hPowerNotify) UnregisterPowerSettingNotification(m_hPowerNotify);
        if (m_hBatterySaverNotify) UnregisterPowerSettingNotification(m_hBatterySaverNotify);
        if (m_hEnergySaverNotifyW11) UnregisterPowerSettingNotification(m_hEnergySaverNotifyW11);
        KillTimer(hWnd, TIMER_UPDATE);
        KillTimer(hWnd, TIMER_FAST);
        SHAppBarMessage(ABM_REMOVE, &m_abd); // Return screen space to Windows
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void AppBar::LoadSettings()
{
    if (m_configPath[0] == 0) {
        GetModuleFileNameW(nullptr, m_configPath, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(m_configPath, L'\\');
        if (lastSlash) {
            wcscpy_s(lastSlash + 1, MAX_PATH - (lastSlash + 1 - m_configPath), L"config.ini");
        }
    }

    m_autoHide = GetPrivateProfileIntW(L"Settings", L"AutoHide", 0, m_configPath) != 0;
    m_reserveSpace = GetPrivateProfileIntW(L"Settings", L"ReserveSpace", 1, m_configPath) != 0;
    m_showControls = GetPrivateProfileIntW(L"Settings", L"ShowControls", 1, m_configPath) != 0;
    m_controlsOnlyAutoHide = GetPrivateProfileIntW(L"Settings", L"ControlsOnlyAutoHide", 1, m_configPath) != 0;
    m_autoHideDelayMs = GetPrivateProfileIntW(L"Settings", L"AutoHideDelayMs", 500, m_configPath);
    m_barH = GetPrivateProfileIntW(L"Settings", L"BarHeight", 32, m_configPath);
    m_volScrollStep = GetPrivateProfileIntW(L"Settings", L"VolScrollStep", 2, m_configPath);
    m_briScrollStep = GetPrivateProfileIntW(L"Settings", L"BriScrollStep", 5, m_configPath);

    if (m_autoHideDelayMs < 100) m_autoHideDelayMs = 100;
    if (m_barH < 16) m_barH = 16;
    if (m_barH > 64) m_barH = 64;
    if (m_volScrollStep < 1) m_volScrollStep = 1;
    if (m_volScrollStep > 10) m_volScrollStep = 10;
    if (m_briScrollStep < 1) m_briScrollStep = 1;
    if (m_briScrollStep > 10) m_briScrollStep = 10;
}

void AppBar::SaveSettings()
{
    if (m_configPath[0] == 0) return;

    WritePrivateProfileStringW(L"Settings", L"AutoHide", m_autoHide ? L"1" : L"0", m_configPath);
    WritePrivateProfileStringW(L"Settings", L"ReserveSpace", m_reserveSpace ? L"1" : L"0", m_configPath);
    WritePrivateProfileStringW(L"Settings", L"ShowControls", m_showControls ? L"1" : L"0", m_configPath);
    WritePrivateProfileStringW(L"Settings", L"ControlsOnlyAutoHide", m_controlsOnlyAutoHide ? L"1" : L"0", m_configPath);
    
    wchar_t delayStr[32];
    swprintf_s(delayStr, _countof(delayStr), L"%d", m_autoHideDelayMs);
    WritePrivateProfileStringW(L"Settings", L"AutoHideDelayMs", delayStr, m_configPath);

    wchar_t heightStr[32];
    swprintf_s(heightStr, _countof(heightStr), L"%d", m_barH);
    WritePrivateProfileStringW(L"Settings", L"BarHeight", heightStr, m_configPath);

    wchar_t stepStr[32];
    swprintf_s(stepStr, _countof(stepStr), L"%d", m_volScrollStep);
    WritePrivateProfileStringW(L"Settings", L"VolScrollStep", stepStr, m_configPath);
    swprintf_s(stepStr, _countof(stepStr), L"%d", m_briScrollStep);
    WritePrivateProfileStringW(L"Settings", L"BriScrollStep", stepStr, m_configPath);
}

void AppBar::ApplySettingsAndSave()
{
    Widgets_UpdateSettings(m_autoHide, m_showControls, m_controlsOnlyAutoHide);
    Widgets_SetScrollSteps(m_volScrollStep, m_briScrollStep);
    SaveSettings();

    // Recreate fonts to apply any BarHeight changes
    Renderer_Destroy(m_renderer);
    int dpi = GetDpiForWindow(m_hWnd);
    Renderer_Init(m_renderer, dpi, m_barH);

    if (!m_autoHide) {
        ShowWindow(m_hWnd, SW_SHOWNA);
    }
    PositionBar();
    Widgets_UpdateAll();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void AppBar::SetRunOnStartup(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            RegSetValueExW(hKey, L"ZenBar", 0, REG_SZ, (const BYTE*)path, (DWORD)((wcslen(path) + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(hKey, L"ZenBar");
        }
        RegCloseKey(hKey);
    }
}

bool AppBar::IsRunOnStartup() {
    bool enabled = false;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t path[MAX_PATH];
        DWORD size = sizeof(path);
        if (RegQueryValueExW(hKey, L"ZenBar", nullptr, nullptr, (LPBYTE)path, &size) == ERROR_SUCCESS) {
            enabled = true;
        }
        RegCloseKey(hKey);
    }
    return enabled;
}

bool AppBar::IsFullscreenWindowActive()
{
    HWND hFg = GetForegroundWindow();
    if (!hFg) return false;
    if (hFg == m_hWnd) return false;

    // Filter out common desktop/shell windows
    wchar_t className[256];
    if (GetClassNameW(hFg, className, 256)) {
        if (wcscmp(className, L"Progman") == 0 ||
            wcscmp(className, L"WorkerW") == 0 ||
            wcscmp(className, L"Shell_TrayWnd") == 0 ||
            wcscmp(className, L"Shell_SecondaryTrayWnd") == 0) {
            return false;
        }
    }

    // Get monitor of the AppBar window
    HMONITOR hMon = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(hMon, &mi)) return false;

    RECT rcFg;
    if (GetWindowRect(hFg, &rcFg)) {
        // Must cover the monitor screen area completely
        if (rcFg.left <= mi.rcMonitor.left &&
            rcFg.top <= mi.rcMonitor.top &&
            rcFg.right >= mi.rcMonitor.right &&
            rcFg.bottom >= mi.rcMonitor.bottom) {
            
            // Check styles to ensure it is actually a popup or borderless window
            DWORD style = GetWindowLongW(hFg, GWL_STYLE);
            if ((style & WS_POPUP) || !(style & WS_CAPTION)) {
                return true;
            }
        }
    }
    return false;
}

