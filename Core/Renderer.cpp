#include "include\pch.h"
#include "Renderer.h"
#include "../UI/Widgets.h"  // For BarColors namespace

// ------------------------------------------------------------------
// Renderer_Init — create fonts once
// ------------------------------------------------------------------
void Renderer_Init(Renderer& r, int dpi, int barH)
{
    // Ensure we don't divide by zero; default barH is 32.
    if (barH < 16) barH = 16;
    
    // Calculate proportional font sizes. 
    // At barH=32, main=11pt, small=9pt.
    int ptMain = max(8, (barH * 11) / 32);
    int ptSmall = max(7, (barH * 9) / 32);

    int mainHeight  = -MulDiv(ptMain, dpi, 72);
    int boldHeight  = -MulDiv(ptMain, dpi, 72); 
    int smallHeight = -MulDiv(ptSmall, dpi, 72);

    r.fontMain = CreateFontW(
        mainHeight, 0, 0, 0,
        FW_NORMAL,              // Regular weight
        FALSE, FALSE, FALSE,    // No italic/underline/strikeout
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,      // ClearType anti-aliasing (important for dark bars)
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI"             // Windows system font — always available
    );

    r.fontBold = CreateFontW(
        boldHeight, 0, 0, 0,
        FW_SEMIBOLD,            // SemiBold (600 weight) — slightly bolder than normal
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI"
    );

    r.fontSmall = CreateFontW(
        smallHeight, 0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI"
    );
}

// ------------------------------------------------------------------
// Renderer_Destroy — free GDI objects
// ------------------------------------------------------------------
void Renderer_Destroy(Renderer& r)
{
    // Clean up back-buffer
    if (r.memDC) {
        if (r.oldBitmap) SelectObject(r.memDC, r.oldBitmap);
        DeleteDC(r.memDC);
        r.memDC = nullptr;
    }
    if (r.memBitmap) {
        DeleteObject(r.memBitmap);
        r.memBitmap = nullptr;
    }

    // Clean up fonts
    if (r.fontMain)  { DeleteObject(r.fontMain);  r.fontMain  = nullptr; }
    if (r.fontBold)  { DeleteObject(r.fontBold);  r.fontBold  = nullptr; }
    if (r.fontSmall) { DeleteObject(r.fontSmall); r.fontSmall = nullptr; }
}

// ------------------------------------------------------------------
// Renderer_ApplyDWMEffect — Mica on Win11, blur on Win10
// ------------------------------------------------------------------
bool Renderer_ApplyDWMEffect(HWND hWnd)
{
    // Try Mica (Windows 11 22H2+)
    // DWMWA_SYSTEMBACKDROP_TYPE = 38 — set to DWMSBT_MAINWINDOW (Mica)
    SB_DWM_SYSTEMBACKDROP_TYPE backdrop = SB_DWMSBT_MAINWINDOW;
    HRESULT hr = DwmSetWindowAttribute(hWnd, DWMWA_SYSTEMBACKDROP_TYPE,
                                       &backdrop, sizeof(backdrop));
    if (SUCCEEDED(hr)) return true;

    // Fall back: Acrylic blur (Windows 10 / older Win11)
    // DWM_BLURBEHIND.hRgnBlur = NULL means "blur the entire window"
    struct DWM_BLURBEHIND_LOCAL {
        DWORD dwFlags;
        BOOL  fEnable;
        HRGN  hRgnBlur;
        BOOL  fTransitionOnMaximized;
    } bb = {};
    bb.dwFlags = 0x00000001; // DWM_BB_ENABLE
    bb.fEnable = TRUE;
    bb.hRgnBlur = nullptr;
    DwmEnableBlurBehindWindow(hWnd, reinterpret_cast<DWM_BLURBEHIND*>(&bb));

    return false; // Mica wasn't available
}

// ------------------------------------------------------------------
// Renderer_BeginFrame — set up back-buffer, fill background
// ------------------------------------------------------------------
HDC Renderer_BeginFrame(Renderer& r, HDC screenDC, int width, int height)
{
    // (Re)create back-buffer if size changed
    if (!r.memDC || r.bufWidth != width || r.bufHeight != height) {
        // Clean up old resources
        if (r.memDC) {
            if (r.oldBitmap) SelectObject(r.memDC, r.oldBitmap);
            DeleteDC(r.memDC);
        }
        if (r.memBitmap) DeleteObject(r.memBitmap);

        // Create new back-buffer at the correct size
        r.memDC     = CreateCompatibleDC(screenDC);
        r.memBitmap = CreateCompatibleBitmap(screenDC, width, height);
        r.oldBitmap = (HBITMAP)SelectObject(r.memDC, r.memBitmap);
        r.bufWidth  = width;
        r.bufHeight = height;
    }

    // Fill background with our dark color
    // This is the "canvas" every widget draws onto.
    RECT rc = { 0, 0, width, height };
    HBRUSH hBgBrush = CreateSolidBrush(BarColors::BG);
    FillRect(r.memDC, &rc, hBgBrush);
    DeleteObject(hBgBrush);

    return r.memDC; // Caller draws widgets into this DC
}

// Renderer_EndFrame — blit back-buffer to screen
void Renderer_EndFrame(Renderer& r, HDC screenDC, int width, int height)
{
    BitBlt(screenDC, 0, 0, width, height, r.memDC, 0, 0, SRCCOPY);
}

// ------------------------------------------------------------------
// Renderer_DrawSeparator — thin vertical divider between zones
// ------------------------------------------------------------------
void Renderer_DrawSeparator(HDC memDC, int x, int y, int height)
{
    // Draw a 1px vertical line using the separator color
    HPEN hPen = CreatePen(PS_SOLID, 1, BarColors::SEPARATOR);
    HPEN hOld = (HPEN)SelectObject(memDC, hPen);
    MoveToEx(memDC, x, y + 6, nullptr);      // 6px padding from top
    LineTo(memDC, x, y + height - 6);         // 6px padding from bottom
    SelectObject(memDC, hOld);
    DeleteObject(hPen);
}
