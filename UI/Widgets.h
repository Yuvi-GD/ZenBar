#pragma once
// Widgets.h — All widget definitions in ONE place.
// -------------------------------------------------
// Design philosophy:
//   Each widget is a plain struct with two function pointers:
//     - Update(Widget*)  : reads system data, updates text/color
//     - Draw(...)        : paints itself on the bar
//
//   All widgets live in a single global array: g_widgets[].
//   To add a new widget: add one entry to that array in Widgets.cpp.
//   No subclasses. No separate files. No copy-paste boilerplate.

#include "include\pch.h"
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>

// =====================================================================
// Zone — which section of the bar the widget lives in
// =====================================================================
enum class Zone { Left, Center, Right };

// =====================================================================
// Color palette — one place to change all colors
// =====================================================================
namespace BarColors {
    constexpr COLORREF BG           = RGB(10,  10,  16);   // Bar background
    constexpr COLORREF MUTED        = RGB(80,  80, 100);   // Labels / secondary text (dimmer)
    constexpr COLORREF VALUE        = RGB(235, 235, 255);  // Primary values
    constexpr COLORREF CLOCK        = RGB(130, 190, 255);  // Time (soft blue)
    constexpr COLORREF CPU          = RGB(140, 220, 180);  // CPU (mint green)
    constexpr COLORREF NET_UP       = RGB(130, 190, 255);  // Upload (blue)
    constexpr COLORREF NET_DOWN     = RGB(180, 140, 255);  // Download (purple)
    // Volume
    constexpr COLORREF VOLUME       = RGB(255, 180, 80);   // Normal volume (amber)
    constexpr COLORREF VOLUME_HIGH  = RGB(255, 100, 60);   // >75% — warm orange-red warning
    // Brightness
    constexpr COLORREF BRIGHTNESS   = RGB(200, 190, 80);   // Normal (dim yellow)
    constexpr COLORREF BRI_HIGH     = RGB(255, 220, 40);   // >50% — bright yellow
    constexpr COLORREF BRI_MAX      = RGB(255, 140, 0);    // >85% — amber/orange warning
    // Battery
    constexpr COLORREF BAT_CHARGING = RGB(80,  210, 110);  // Plugged in / charging (green)
    constexpr COLORREF BAT_SAVER    = RGB(220, 140,  40);  // Battery saver mode (orange)
    constexpr COLORREF BAT_GOOD     = RGB(0,   191, 255);  // Normal (sky blue)
    constexpr COLORREF BAT_MED      = RGB(230, 195,  60);  // 20-40% (yellow)
    constexpr COLORREF BAT_LOW      = RGB(220,  70,  60);  // <20% (red)
    //
    constexpr COLORREF SEPARATOR    = RGB(40,  40,  55);   // Zone dividers
    constexpr COLORREF WINLABEL     = RGB(160, 160, 180);  // Active window title
}

// =====================================================================
// Widget — the ONLY struct. All state inline. No base class.
// =====================================================================
struct Widget {
    // --- What gets displayed ---
    const wchar_t* name;   // User-facing name for toggling
    wchar_t  text[128];    // Current display string (set by Update)
    COLORREF color;        // Text color (set by Update)
    Zone     zone;         // Which bar zone
    bool     visible;      // false = skip drawing entirely
    RECT     rect;         // Hit testing bounds calculated during Draw

    // Gap in pixels between widgets
    static constexpr int PAD = 16;

    // --- Behavior (function pointers replace inheritance) ---
    //   Update: called every second to refresh text/color from system data
    //   Draw:   called every paint to render on the bar, returns width used
    void (*Update)(Widget* self);
    int  (*Draw)  (Widget* self, HDC hdc, HFONT fontMain, HFONT fontBold,
                   int x, int y, int barH);

    // ---------------------------------------------------------------
    // Per-widget state — each widget uses the fields it needs.
    // Named with a prefix so it's clear which widget owns which field.
    // ---------------------------------------------------------------

    // Clock
    wchar_t clock_date[32];  // "Fri, May 23"
    wchar_t clock_time[32];  // "3:45 PM"

    // Network speed
    ULONG64 net_prevIn;      // Previous total bytes received
    ULONG64 net_prevOut;     // Previous total bytes sent
    bool    net_hasPrev;     // false on first sample (no delta yet)

    // CPU usage
    ULONGLONG cpu_prevIdle;
    ULONGLONG cpu_prevKernel; // NOTE: kernel time INCLUDES idle time
    ULONGLONG cpu_prevUser;
    bool      cpu_hasPrev;

    // Battery
    BYTE bat_percent;        // 0-100, or 255 if no battery (desktop PC)
    bool bat_charging;

    // Volume (COM interface cached to avoid re-init every second)
    IAudioEndpointVolume* vol_iface;
    wchar_t* vol_deviceId;

    // App Controls
    RECT app_rectMin;
    RECT app_rectMax;
    RECT app_rectClose;
    int  app_hoverIndex; // 0=none, 1=min, 2=max, 3=close

    // Brightness (DDC/CI physical monitor handle)
    HANDLE bri_monHandle;    // HPHYSICALMONITOR — HANDLE under the hood
    DWORD  bri_min;
    DWORD  bri_max;

    // Media Controls
    RECT media_rectPrev;
    RECT media_rectPlay;
    RECT media_rectNext;
    RECT media_rectText;
    wchar_t media_songText[128];
    wchar_t media_appId[128];
    bool media_isPlaying;
    bool media_canPrev;
    bool media_canNext;
    bool media_canPlayPause;
};

// =====================================================================
// Public API — called by AppBar
// =====================================================================

// One-time setup. Call after the bar window is created.
// hWnd is needed to find the monitor for the brightness widget.
void Widgets_Init(HWND hWnd);

// Free all resources (COM, DDC/CI handles). Call on WM_DESTROY.
void Widgets_Destroy();

// Communicates AppBar state and settings to widgets
void Widgets_UpdateSettings(bool autoHide, bool showControls, bool controlsOnlyAutoHide);

// Update functions called by WM_TIMER.
void Widgets_UpdateAll();

// Update only one named widget — faster than UpdateAll for scroll/event responses.
void Widgets_UpdateByName(const wchar_t* name);

// Toggle widget visibility and save setting
void Widgets_ToggleVisibility(int index);

// Scroll adjust functions
void Widgets_AdjustVolume(int direction);    // direction: +1 or -1
void Widgets_AdjustBrightness(int direction); // direction: +1 or -1
void Widgets_SetScrollSteps(int volStep, int briStep); // 1-10

// Cycle through multiple SMTC media sessions (dir: +1 next, -1 prev)
void Widgets_CycleMediaSession(int dir);

// Current media session state (read-only from outside Widgets.cpp)
extern int g_mediaSessionIndex;
extern int g_mediaTotalSessions;

// Audio device popup switcher menu
void Widgets_ShowAudioDeviceMenu(HWND hWnd, int x, int y);

// Draw all visible widgets in a given zone.
//   Left/Center zones:  x = starting x (left edge). Returns new x (right of last widget).
//   Right zone:         x = right edge. Draws right-to-left. Returns new x (left of last widget).
int  Widgets_DrawZone(Zone zone, HDC hdc, HFONT fontMain, HFONT fontBold,
                      int x, int barH, int barW);

// Draw the battery % progress bar along the bottom edge of the bar.
// Call AFTER all zone draws, before EndFrame.
void Widgets_DrawBatteryBorder(HDC hdc, int barW, int barH);

// Free all resources (COM, DDC/CI handles). Call on WM_DESTROY.
void Widgets_Destroy();

// =====================================================================
// Shared helper — also used by AppBar for the active window title
// =====================================================================
int DrawBarText(HDC hdc, const wchar_t* text, int x, int y, int barH,
                HFONT font, COLORREF color);

// =====================================================================
// Widget registry — defined in Widgets.cpp
// =====================================================================
extern Widget g_widgets[];
extern int    g_widgetCount;
