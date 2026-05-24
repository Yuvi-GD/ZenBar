#pragma once
// Core/Renderer.h — Double-buffered GDI painter for the status bar.
// -----------------------------------------------------------------
// Prevents flicker by drawing everything to an off-screen bitmap
// (the "back buffer"), then blitting the completed frame to the screen
// in one fast BitBlt call.
//
// Also owns the font handles used by all widgets.
// Call Renderer_Init() once after window creation.
// Call Renderer_Destroy() once on WM_DESTROY.

#include "include\pch.h"

// ------------------------------------------------------------------
// DWM/Mica defines (not all are in older SDKs, so we define manually)
// ------------------------------------------------------------------
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

typedef enum _SB_DWM_SYSTEMBACKDROP_TYPE {
    SB_DWMSBT_AUTO            = 0,
    SB_DWMSBT_NONE            = 1,
    SB_DWMSBT_MAINWINDOW      = 2, // Mica
    SB_DWMSBT_TRANSIENTWINDOW = 3, // Acrylic
    SB_DWMSBT_TABBEDWINDOW    = 4
} SB_DWM_SYSTEMBACKDROP_TYPE;

// ------------------------------------------------------------------
// Renderer state — all drawing resources in one place
// ------------------------------------------------------------------
struct Renderer {
    // Back-buffer resources (recreated if bar is resized)
    HDC     memDC      = nullptr;
    HBITMAP memBitmap  = nullptr;
    HBITMAP oldBitmap  = nullptr;
    int     bufWidth   = 0;
    int     bufHeight  = 0;

    // Fonts (created once, reused every frame)
    HFONT   fontMain   = nullptr;  // Segoe UI, regular, 11pt
    HFONT   fontBold   = nullptr;  // Segoe UI, bold, 11pt
    HFONT   fontSmall  = nullptr;  // Segoe UI, regular, 9pt  (not used yet)
};

// ------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------

// Creates the GDI resources needed (fonts, brushes, etc.)
void Renderer_Init(Renderer& r, int dpi, int barH);

// Free all GDI resources. Call on WM_DESTROY.
void Renderer_Destroy(Renderer& r);

// Apply DWM blur/Mica effect to the window.
// Returns false if the effect isn't supported (older Windows).
bool Renderer_ApplyDWMEffect(HWND hWnd);

// ------------------------------------------------------------------
// Per-frame
// ------------------------------------------------------------------

// Begin a new frame.
// Creates/resizes the back-buffer if needed, fills background,
// returns the memory DC for widgets to draw into.
HDC Renderer_BeginFrame(Renderer& r, HDC screenDC, int width, int height);

// Finish the frame — blit memory DC to screen.
void Renderer_EndFrame(Renderer& r, HDC screenDC, int width, int height);

// ------------------------------------------------------------------
// Drawing helpers (used by AppBar for zone separators / left zone)
// ------------------------------------------------------------------

// Draw a 1px vertical separator line at x
void Renderer_DrawSeparator(HDC memDC, int x, int y, int height);
