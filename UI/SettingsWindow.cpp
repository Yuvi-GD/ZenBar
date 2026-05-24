#include "include\pch.h"
#include "SettingsWindow.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <stdio.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

// -----------------------------------------------------------------------
// Control IDs
// -----------------------------------------------------------------------
#define ID_CHK_AUTOHIDE         101
#define ID_CHK_SHOWCONTROLS     102
#define ID_CHK_LINKCONTROLS     103
#define ID_CHK_STARTUP          104
#define ID_TXT_DELAY            105
#define ID_BTN_SAVE             106
#define ID_BTN_CANCEL           107
#define ID_SLD_HEIGHT           108
#define ID_LBL_HEIGHT_VAL       109
#define ID_SLD_VOL_STEP         110
#define ID_LBL_VOL_STEP_VAL     111
#define ID_SLD_BRI_STEP         112
#define ID_LBL_BRI_STEP_VAL     113

struct SettingsContext {
    AppBar* pAppBar;
    HWND hChkAutoHide;
    HWND hChkShowControls;
    HWND hChkLinkControls;
    HWND hChkStartup;
    HWND hTxtDelay;
    HWND hSldHeight;
    HWND hLblHeightVal;
    HWND hSldVolStep;
    HWND hLblVolStepVal;
    HWND hSldBriStep;
    HWND hLblBriStepVal;
};

static SettingsContext* g_ctx       = nullptr;
static HBRUSH           g_hbrDark   = nullptr;
static HBRUSH           g_hbrDarker = nullptr;
static HFONT            g_hFont     = nullptr;
static HFONT            g_hFontBold = nullptr;
// Flag set by WM_DESTROY to exit our custom modal loop WITHOUT calling PostQuitMessage
static bool             g_settingsDone = false;

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------
static HWND MakeLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h, UINT id = 0) {
    return CreateWindowW(L"STATIC", text,
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        x, y, w, h, parent, (HMENU)(UINT_PTR)id, nullptr, nullptr);
}

static HWND MakeCheck(HWND parent, const wchar_t* text, int x, int y, int w, int h, UINT id) {
    return CreateWindowW(L"BUTTON", text,
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        x, y, w, h, parent, (HMENU)(UINT_PTR)id, nullptr, nullptr);
}

static HWND MakeSlider(HWND parent, int x, int y, int w, int h, UINT id, int lo, int hi, int cur) {
    HWND hSlider = CreateWindowW(TRACKBAR_CLASSW, nullptr,
        WS_VISIBLE | WS_CHILD | TBS_HORZ | TBS_NOTICKS,
        x, y, w, h, parent, (HMENU)(UINT_PTR)id, nullptr, nullptr);
    SetWindowTheme(hSlider, L"DarkMode_Explorer", nullptr);
    SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
    SendMessageW(hSlider, TBM_SETPOS,   TRUE, cur);
    return hSlider;
}

static void UpdateHeightLabel(HWND hLbl, int val) {
    wchar_t buf[32]; swprintf_s(buf, L"%d px", val); SetWindowTextW(hLbl, buf);
}
static void UpdateStepLabel(HWND hLbl, int val) {
    wchar_t buf[32]; swprintf_s(buf, L"%d%%", val); SetWindowTextW(hLbl, buf);
}

// -----------------------------------------------------------------------
// WndProc
// -----------------------------------------------------------------------
static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
            InitCommonControlsEx(&icc);

            g_hFont     = CreateFontW(-13, 0, 0, 0, FW_NORMAL,   FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontBold = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            int cx = 16, cy = 16, cw = 330;

            // — Behaviour —
            MakeLabel(hWnd, L"BEHAVIOUR", cx, cy, cw, 18);  cy += 22;
            g_ctx->hChkAutoHide = MakeCheck(hWnd, L"Auto-Hide Bar (Overlay Mode)", cx, cy, cw, 20, ID_CHK_AUTOHIDE); cy += 26;
            MakeLabel(hWnd, L"Auto-Hide Delay (ms)", cx + 16, cy + 3, 160, 18);
            g_ctx->hTxtDelay = CreateWindowW(L"EDIT", L"",
                WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER,
                cx + 185, cy, 70, 22, hWnd, (HMENU)ID_TXT_DELAY, nullptr, nullptr);
            SetWindowTheme(g_ctx->hTxtDelay, L"DarkMode_Explorer", nullptr);
            cy += 32;
            g_ctx->hChkShowControls = MakeCheck(hWnd, L"Show Window Controls (Min/Max/Close)", cx, cy, cw, 20, ID_CHK_SHOWCONTROLS); cy += 26;
            g_ctx->hChkLinkControls = MakeCheck(hWnd, L"Only in Auto-Hide mode", cx + 16, cy, cw - 16, 20, ID_CHK_LINKCONTROLS); cy += 32;

            // — Bar Height —
            MakeLabel(hWnd, L"BAR HEIGHT", cx, cy, 200, 18);
            g_ctx->hLblHeightVal = MakeLabel(hWnd, L"32 px", cx + 255, cy, 60, 18, ID_LBL_HEIGHT_VAL);
            cy += 22;
            g_ctx->hSldHeight = MakeSlider(hWnd, cx, cy, cw - 10, 24, ID_SLD_HEIGHT, 16, 64,
                                            g_ctx->pAppBar->GetBarHeight()); cy += 32;

            // — Scroll Step —
            MakeLabel(hWnd, L"SCROLL STEP", cx, cy, cw, 18); cy += 22;
            MakeLabel(hWnd, L"Volume scroll step:", cx, cy + 4, 160, 18);
            g_ctx->hLblVolStepVal = MakeLabel(hWnd, L"2%", cx + 255, cy + 4, 50, 18, ID_LBL_VOL_STEP_VAL);
            cy += 22;
            g_ctx->hSldVolStep = MakeSlider(hWnd, cx, cy, cw - 10, 24, ID_SLD_VOL_STEP, 1, 10,
                                             g_ctx->pAppBar->GetVolScrollStep()); cy += 30;
            MakeLabel(hWnd, L"Brightness scroll step:", cx, cy + 4, 160, 18);
            g_ctx->hLblBriStepVal = MakeLabel(hWnd, L"5%", cx + 255, cy + 4, 50, 18, ID_LBL_BRI_STEP_VAL);
            cy += 22;
            g_ctx->hSldBriStep = MakeSlider(hWnd, cx, cy, cw - 10, 24, ID_SLD_BRI_STEP, 1, 10,
                                             g_ctx->pAppBar->GetBriScrollStep()); cy += 32;

            // — System —
            MakeLabel(hWnd, L"SYSTEM", cx, cy, cw, 18); cy += 22;
            g_ctx->hChkStartup = MakeCheck(hWnd, L"Run on Startup", cx, cy, cw, 20, ID_CHK_STARTUP); cy += 24;

            // — Buttons —
            HWND hSave = CreateWindowW(L"BUTTON", L"Save", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                150, cy, 90, 26, hWnd, (HMENU)ID_BTN_SAVE, nullptr, nullptr);

            HWND hCancel = CreateWindowW(L"BUTTON", L"Cancel", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                250, cy, 90, 26, hWnd, (HMENU)ID_BTN_CANCEL, nullptr, nullptr);

            // Apply font to all children
            EnumChildWindows(hWnd, [](HWND hChild, LPARAM lParam) -> BOOL {
                SendMessage(hChild, WM_SETFONT, (WPARAM)lParam, MAKELPARAM(TRUE, 0));
                return TRUE;
            }, (LPARAM)g_hFont);

            // Populate current values
            SetWindowLongPtr(g_ctx->hChkAutoHide,     GWLP_USERDATA, g_ctx->pAppBar->GetAutoHide());
            SetWindowLongPtr(g_ctx->hChkShowControls, GWLP_USERDATA, g_ctx->pAppBar->GetShowControls());
            SetWindowLongPtr(g_ctx->hChkLinkControls, GWLP_USERDATA, g_ctx->pAppBar->GetControlsOnlyAutoHide());
            SetWindowLongPtr(g_ctx->hChkStartup,      GWLP_USERDATA, g_ctx->pAppBar->IsRunOnStartup());

            wchar_t delayStr[32];
            swprintf_s(delayStr, _countof(delayStr), L"%d", g_ctx->pAppBar->GetAutoHideDelayMs());
            SetWindowTextW(g_ctx->hTxtDelay, delayStr);

            UpdateHeightLabel(g_ctx->hLblHeightVal, g_ctx->pAppBar->GetBarHeight());
            UpdateStepLabel(g_ctx->hLblVolStepVal,  g_ctx->pAppBar->GetVolScrollStep());
            UpdateStepLabel(g_ctx->hLblBriStepVal,  g_ctx->pAppBar->GetBriScrollStep());
            return 0;
        }

        case WM_NOTIFY: {
            LPNMHDR nmhdr = (LPNMHDR)lParam;
            if (nmhdr->code == NM_CUSTOMDRAW) {
                LPNMCUSTOMDRAW nmcd = (LPNMCUSTOMDRAW)lParam;
                if (nmhdr->idFrom == ID_SLD_HEIGHT || nmhdr->idFrom == ID_SLD_VOL_STEP || nmhdr->idFrom == ID_SLD_BRI_STEP) {
                    if (nmcd->dwDrawStage == CDDS_PREPAINT) {
                        return CDRF_NOTIFYITEMDRAW;
                    } else if (nmcd->dwDrawStage == CDDS_ITEMPREPAINT) {
                        if (nmcd->dwItemSpec == TBCD_CHANNEL) {
                            HDC hdc = nmcd->hdc;
                            RECT rc = nmcd->rc;
                            rc.top += (rc.bottom - rc.top) / 2 - 2;
                            rc.bottom = rc.top + 4;
                            HBRUSH hbr = CreateSolidBrush(RGB(60, 60, 80));
                            FillRect(hdc, &rc, hbr);
                            DeleteObject(hbr);
                            return CDRF_SKIPDEFAULT;
                        } else if (nmcd->dwItemSpec == TBCD_THUMB) {
                            HDC hdc = nmcd->hdc;
                            RECT rc = nmcd->rc;
                            HBRUSH hbr = CreateSolidBrush(RGB(200, 200, 220));
                            FillRect(hdc, &rc, hbr);
                            DeleteObject(hbr);
                            return CDRF_SKIPDEFAULT;
                        }
                    }
                }
            }
            break;
        }

        case WM_HSCROLL: {
            if ((HWND)lParam == g_ctx->hSldHeight) {
                UpdateHeightLabel(g_ctx->hLblHeightVal, (int)SendMessageW(g_ctx->hSldHeight,  TBM_GETPOS, 0, 0));
            } else if ((HWND)lParam == g_ctx->hSldVolStep) {
                UpdateStepLabel(g_ctx->hLblVolStepVal,  (int)SendMessageW(g_ctx->hSldVolStep,  TBM_GETPOS, 0, 0));
            } else if ((HWND)lParam == g_ctx->hSldBriStep) {
                UpdateStepLabel(g_ctx->hLblBriStepVal,  (int)SendMessageW(g_ctx->hSldBriStep,  TBM_GETPOS, 0, 0));
            }
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id >= 101 && id <= 104) {
                HWND hChk = (HWND)lParam;
                bool state = GetWindowLongPtr(hChk, GWLP_USERDATA) != 0;
                SetWindowLongPtr(hChk, GWLP_USERDATA, state ? 0 : 1);
                InvalidateRect(hChk, nullptr, FALSE);
            } else if (id == ID_BTN_SAVE) {
                g_ctx->pAppBar->SetAutoHide(GetWindowLongPtr(g_ctx->hChkAutoHide, GWLP_USERDATA) != 0);
                g_ctx->pAppBar->SetShowControls(GetWindowLongPtr(g_ctx->hChkShowControls, GWLP_USERDATA) != 0);
                g_ctx->pAppBar->SetControlsOnlyAutoHide(GetWindowLongPtr(g_ctx->hChkLinkControls, GWLP_USERDATA) != 0);

                wchar_t delayStr[32] = {};
                GetWindowTextW(g_ctx->hTxtDelay, delayStr, _countof(delayStr));
                int delay = _wtoi(delayStr);
                if (delay < 100) delay = 100;
                g_ctx->pAppBar->SetAutoHideDelayMs(delay);

                g_ctx->pAppBar->SetBarHeight(   (int)SendMessageW(g_ctx->hSldHeight,  TBM_GETPOS, 0, 0));
                g_ctx->pAppBar->SetVolScrollStep((int)SendMessageW(g_ctx->hSldVolStep, TBM_GETPOS, 0, 0));
                g_ctx->pAppBar->SetBriScrollStep((int)SendMessageW(g_ctx->hSldBriStep, TBM_GETPOS, 0, 0));

                bool runOnStartup = (GetWindowLongPtr(g_ctx->hChkStartup, GWLP_USERDATA) != 0);
                if (runOnStartup != g_ctx->pAppBar->IsRunOnStartup())
                    g_ctx->pAppBar->SetRunOnStartup(runOnStartup);

                g_ctx->pAppBar->ApplySettingsAndSave();
                DestroyWindow(hWnd);
            } else if (id == ID_BTN_CANCEL) {
                DestroyWindow(hWnd);
            }
            return 0;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
            HDC hdc = pdis->hDC;
            RECT rc = pdis->rcItem;

            if (pdis->CtlID == ID_BTN_SAVE || pdis->CtlID == ID_BTN_CANCEL) {
                bool isPressed = (pdis->itemState & ODS_SELECTED);
                HBRUSH hbr = CreateSolidBrush(isPressed ? RGB(60, 60, 80) : RGB(40, 40, 50));
                FillRect(hdc, &rc, hbr);
                DeleteObject(hbr);
                
                HBRUSH hbrBorder = CreateSolidBrush(RGB(100, 100, 120));
                FrameRect(hdc, &rc, hbrBorder);
                DeleteObject(hbrBorder);
                
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(230, 230, 245));
                SelectObject(hdc, g_hFont);
                wchar_t text[32];
                GetWindowTextW(pdis->hwndItem, text, 32);
                DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                return TRUE;
            }
            else if (pdis->CtlID >= 101 && pdis->CtlID <= 104) {
                bool isChecked = GetWindowLongPtr(pdis->hwndItem, GWLP_USERDATA) != 0;
                FillRect(hdc, &rc, g_hbrDark);

                RECT rcBox = { rc.left, rc.top + (rc.bottom - rc.top - 14)/2, rc.left + 14, rc.top + (rc.bottom - rc.top - 14)/2 + 14 };
                HBRUSH hbrBox = CreateSolidBrush(isChecked ? RGB(0, 120, 215) : RGB(30, 30, 40));
                FillRect(hdc, &rcBox, hbrBox);
                DeleteObject(hbrBox);

                HBRUSH hbrBorder = CreateSolidBrush(RGB(80, 80, 100));
                FrameRect(hdc, &rcBox, hbrBorder);
                DeleteObject(hbrBorder);

                if (isChecked) {
                    HPEN hPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
                    HPEN hOld = (HPEN)SelectObject(hdc, hPen);
                    MoveToEx(hdc, rcBox.left + 3, rcBox.top + 7, nullptr);
                    LineTo(hdc, rcBox.left + 6, rcBox.top + 10);
                    LineTo(hdc, rcBox.left + 11, rcBox.top + 3);
                    SelectObject(hdc, hOld);
                    DeleteObject(hPen);
                }

                RECT rcText = rc;
                rcText.left += 20;
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(230, 230, 245));
                SelectObject(hdc, g_hFont);
                wchar_t text[64];
                GetWindowTextW(pdis->hwndItem, text, 64);
                DrawTextW(hdc, text, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                return TRUE;
            }
            break;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            wchar_t buf[64] = {};
            GetWindowTextW((HWND)lParam, buf, 63);
            bool isHdr = (!wcscmp(buf, L"BEHAVIOUR") || !wcscmp(buf, L"BAR HEIGHT") ||
                          !wcscmp(buf, L"SCROLL STEP") || !wcscmp(buf, L"SYSTEM"));
            if (isHdr) {
                SetTextColor(hdcStatic, RGB(140, 180, 255));
                SetBkColor  (hdcStatic, RGB(22, 22, 30));
                SelectObject(hdcStatic, g_hFontBold);
                return (INT_PTR)g_hbrDark;
            }
            SetTextColor(hdcStatic, RGB(210, 210, 225));
            SetBkColor  (hdcStatic, RGB(22, 22, 30));
            return (INT_PTR)g_hbrDark;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(230, 230, 245));
            SetBkColor  (hdc, RGB(38, 38, 50));
            return (INT_PTR)g_hbrDarker;
        }

        case WM_CTLCOLORBTN: {
            SetTextColor((HDC)wParam, RGB(230, 230, 245));
            SetBkColor  ((HDC)wParam, RGB(22, 22, 30));
            return (INT_PTR)g_hbrDark;
        }

        case WM_ERASEBKGND: {
            RECT rc; GetClientRect(hWnd, &rc);
            FillRect((HDC)wParam, &rc, g_hbrDark);
            return 1;
        }

        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            // Clean up fonts
            if (g_hFont)     { DeleteObject(g_hFont);     g_hFont     = nullptr; }
            if (g_hFontBold) { DeleteObject(g_hFontBold); g_hFontBold = nullptr; }
            // Signal the modal loop to exit.
            // WM_DESTROY is dispatched synchronously from DestroyWindow via DispatchMessage,
            // so after DispatchMessage returns the while(!g_settingsDone) check exits the loop.
            // NO PostQuitMessage / PostThreadMessage — those would corrupt the main message loop.
            g_settingsDone = true;
            // Wake up GetMessage so the while(!g_settingsDone) loop exits instantly.
            PostMessage(nullptr, WM_NULL, 0, 0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// -----------------------------------------------------------------------
// ShowSettingsDialog — public entry point
// -----------------------------------------------------------------------
void ShowSettingsDialog(HWND hParent, AppBar* pAppBar) {
    if (g_ctx != nullptr) return; // Already open

    g_settingsDone = false; // Reset flag so the modal loop runs!

    if (!g_hbrDark) {
        g_hbrDark   = CreateSolidBrush(RGB(22, 22, 30));
        g_hbrDarker = CreateSolidBrush(RGB(38, 38, 50));
    }

    static const wchar_t* CLASS = L"ZenBar_SettingsWnd";

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc   = SettingsWndProc;
    wc.hInstance     = GetModuleHandle(nullptr);
    wc.hIcon         = LoadIcon(wc.hInstance, MAKEINTRESOURCE(107));
    wc.hIconSm       = LoadIcon(wc.hInstance, MAKEINTRESOURCE(107));
    wc.lpszClassName = CLASS;
    wc.hbrBackground = g_hbrDark;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    // RegisterClassExW returns 0 if already registered — that's fine, we just reuse it
    RegisterClassExW(&wc);

    SettingsContext ctx = {};
    ctx.pAppBar = pAppBar;
    g_ctx = &ctx;
    g_settingsDone = false;

    int winW = 380, winH = 480;
    // Enable global dark mode for Win32 controls via undocumented uxtheme ordinals
    HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hUxtheme) {
        // 135: SetPreferredAppMode (0=Default, 1=AllowDark, 2=ForceDark, 3=ForceLight)
        using fnSetPreferredAppMode = int (WINAPI *)(int appMode);
        auto setAppMode = (fnSetPreferredAppMode)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
        if (setAppMode) setAppMode(2); // ForceDark
        FreeLibrary(hUxtheme);
    }

    HWND hWnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        CLASS,
        L"ZenBar - Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, winW, winH,
        hParent, nullptr, wc.hInstance, nullptr
    );
    if (!hWnd) { 
        DWORD err = GetLastError();
        wchar_t buf[256];
        swprintf_s(buf, L"CreateWindowExW failed with error code: %lu", err);
        MessageBoxW(nullptr, buf, L"Error", MB_ICONERROR);
        g_ctx = nullptr; 
        return; 
    }

    // Allow dark mode for this window specifically
    if (hUxtheme) {
        using fnAllowDarkModeForWindow = bool (WINAPI *)(HWND hwnd, bool allow);
        auto allowDark = (fnAllowDarkModeForWindow)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133));
        if (allowDark) allowDark(hWnd, true);
        
        using fnFlushMenuThemes = void (WINAPI *)();
        auto flush = (fnFlushMenuThemes)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(136));
        if (flush) flush();
    }

    // Dark title bar
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hWnd, 20, &dark, sizeof(dark));
    DwmSetWindowAttribute(hWnd, 19, &dark, sizeof(dark));

    // Position just below the bar, horizontally centred on cursor
    RECT rcBar; GetWindowRect(hParent, &rcBar);
    POINT pt;   GetCursorPos(&pt);
    int x = pt.x - winW / 2;
    int y = rcBar.bottom + 4;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    if (x < 4) x = 4;
    if (x + winW > screenW - 4) x = screenW - winW - 4;
    if (y + winH > screenH - 4) y = rcBar.top - winH - 4;
    SetWindowPos(hWnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    // ---- Custom modal loop ----
    // WM_DESTROY sets g_settingsDone = true synchronously (called from DispatchMessage),
    // so the while-check exits the loop immediately after DestroyWindow dispatches WM_DESTROY.
    // We do NOT use PostQuitMessage or PostThreadMessage — they leave stale messages in the
    // queue that break subsequent opens.
    MSG msg = {};
    while (!g_settingsDone) {
        BOOL bRet = GetMessage(&msg, nullptr, 0, 0);
        if (bRet <= 0) {
            // Real WM_QUIT (app is closing) — re-post so the main loop also sees it
            if (bRet == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (!IsDialogMessage(hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    g_ctx = nullptr;
}
