// Widgets.cpp — All widget update + draw logic in one file.
// ----------------------------------------------------------
// To add a new widget:
//   1. Write a static UpdateXxx(Widget*) function
//   2. Write a static DrawXxx(Widget*, ...) function (or reuse Draw_Default)
//   3. Add one entry to g_widgets[] at the bottom of this file
//   4. In Widgets_Init(), initialize any state that widget needs
// That's it. No new files, no new classes, no project changes.

#include "include\pch.h"
#include "Widgets.h"
#include <pdh.h>
#include <pdhmsg.h>
#include <powrprof.h>
#include <vector>
#include <winrt/Windows.Media.Control.h>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "Powrprof.lib")

static winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager g_mediaSessionManager = nullptr;

// =====================================================================
// Shared draw helper
// =====================================================================
int DrawBarText(HDC hdc, const wchar_t* text, int x, int y, int barH,
                HFONT font, COLORREF color)
{
    if (!text || !text[0]) return 0;
    SelectObject(hdc, font);
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);

    SIZE sz = {};
    int len = (int)wcslen(text);
    GetTextExtentPoint32W(hdc, text, len, &sz);
    TextOutW(hdc, x, y + (barH - sz.cy) / 2, text, len);
    return sz.cx;
}

// Measure text width without drawing (used for RTL layout)
static int MeasureText(HDC hdc, const wchar_t* text, HFONT font)
{
    if (!text || !text[0]) return 0;
    SelectObject(hdc, font);
    SIZE sz = {};
    GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
    return sz.cx;
}

// =====================================================================
// Helper: format bytes/sec into a human-readable speed string
// =====================================================================
static void FormatSpeed(ULONG64 bps, const wchar_t* arrow, wchar_t* buf, int bufLen)
{
    if      (bps >= 1024ULL * 1024) swprintf_s(buf, bufLen, L"%s%.1fMB/s", arrow, bps / (1024.0 * 1024.0));
    else if (bps >= 1024)           swprintf_s(buf, bufLen, L"%s%.0fKB/s", arrow, bps / 1024.0);
    else                            swprintf_s(buf, bufLen, L"%s%lluB/s",   arrow, bps);
}

// =====================================================================
// Helper: convert FILETIME to ULONGLONG (for CPU calculation)
// =====================================================================
static ULONGLONG FtToU64(const FILETIME& ft)
{
    return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

// =====================================================================
// DEFAULT DRAW — used by most widgets (single colored text string)
// =====================================================================
static int Draw_Default(Widget* w, HDC hdc, HFONT fontMain, HFONT fontBold,
                        int x, int y, int barH)
{
    return DrawBarText(hdc, w->text, x, y, barH, fontBold, w->color);
}

// =====================================================================
// CLOCK WIDGET
// Center zone — "Fri, May 23  ·  3:45 PM"
// Date in muted gray, dot separator, time in bold blue.
// =====================================================================
static void Update_Clock(Widget* w)
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    // Date: "Fri, May 23"
    WCHAR day[16], monthDay[16];
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"ddd",   day,      _countof(day),      nullptr);
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"MMM d", monthDay, _countof(monthDay), nullptr);
    swprintf_s(w->clock_date, _countof(w->clock_date), L"%s, %s", day, monthDay);

    // Time: "3:45 PM" (12h, no seconds — keeps center clean)
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st,
                    nullptr, w->clock_time, _countof(w->clock_time));
}

static int Draw_Clock(Widget* w, HDC hdc, HFONT fontMain, HFONT fontBold,
                      int x, int y, int barH)
{
    // Date (muted) · Time (blue bold)
    int curX = x;
    curX += DrawBarText(hdc, w->clock_date, curX, y, barH, fontMain, BarColors::MUTED);
    curX += 6;
    curX += DrawBarText(hdc, L"\u00B7", curX, y, barH, fontMain, BarColors::SEPARATOR); // ·
    curX += 6;
    curX += DrawBarText(hdc, w->clock_time, curX, y, barH, fontBold, BarColors::CLOCK);
    return curX - x;
}

// =====================================================================
// GPU WIDGET
// Right zone — "GPU 12%"
// Uses Performance Data Helper (PDH) to track all active GPUs.
// =====================================================================
static PDH_HQUERY g_gpuQuery = nullptr;
static PDH_HCOUNTER g_gpuCounter = nullptr;
static bool g_gpuPdhInit = false;

static void Init_GPU_PDH()
{
    if (g_gpuPdhInit) return;
    if (PdhOpenQueryW(nullptr, 0, &g_gpuQuery) == ERROR_SUCCESS) {
        if (PdhAddEnglishCounterW(g_gpuQuery, L"\\GPU Engine(*)\\Utilization Percentage", 0, &g_gpuCounter) == ERROR_SUCCESS) {
            PdhCollectQueryData(g_gpuQuery);
            g_gpuPdhInit = true;
        } else {
            PdhCloseQuery(g_gpuQuery);
            g_gpuQuery = nullptr;
        }
    }
}

static void Update_GPU(Widget* w)
{
    if (!g_gpuPdhInit) {
        Init_GPU_PDH();
        if (!g_gpuPdhInit) {
            w->visible = false;
            return;
        }
    }

    PDH_STATUS status = PdhCollectQueryData(g_gpuQuery);
    if (status != ERROR_SUCCESS) return;

    DWORD bufferSize = 0;
    DWORD itemCount = 0;
    status = PdhGetFormattedCounterArrayW(g_gpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
    if (status == PDH_MORE_DATA && bufferSize > 0) {
        std::vector<BYTE> buffer(bufferSize);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        status = PdhGetFormattedCounterArrayW(g_gpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
        if (status == ERROR_SUCCESS) {
            struct GpuInfo {
                wchar_t luidStr[64];
                double sum3D;
            };
            std::vector<GpuInfo> gpus;

            for (DWORD i = 0; i < itemCount; i++) {
                const wchar_t* name = items[i].szName;
                if (!name) continue;

                if (wcsstr(name, L"engtype_3D") != nullptr || wcsstr(name, L"_3D") != nullptr) {
                    wchar_t luidStr[64] = L"default";
                    const wchar_t* pLuid = wcsstr(name, L"luid_");
                    if (pLuid) {
                        const wchar_t* pNext = wcschr(pLuid + 5, L'_');
                        if (pNext) pNext = wcschr(pNext + 1, L'_');
                        if (pNext) {
                            size_t copyLen = pNext - pLuid;
                            if (copyLen > 63) copyLen = 63;
                            wcsncpy_s(luidStr, pLuid, copyLen);
                            luidStr[copyLen] = L'\0';
                        } else {
                            wcscpy_s(luidStr, pLuid);
                        }
                    }

                    double val = items[i].FmtValue.doubleValue;
                    if (val < 0.0) val = 0.0;
                    if (val > 100.0) val = 100.0;

                    bool found = false;
                    for (auto& gpu : gpus) {
                        if (wcscmp(gpu.luidStr, luidStr) == 0) {
                            gpu.sum3D += val;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        GpuInfo info = {};
                        wcscpy_s(info.luidStr, luidStr);
                        info.sum3D = val;
                        gpus.push_back(info);
                    }
                }
            }

            if (gpus.empty()) {
                wcscpy_s(w->text, L"GPU 0%");
                w->color = BarColors::CPU;
            } else if (gpus.size() == 1) {
                int pct = (int)(gpus[0].sum3D + 0.5);
                if (pct > 100) pct = 100;
                swprintf_s(w->text, L"GPU %d%%", pct);
                w->color = (pct >= 90) ? BarColors::BAT_LOW
                         : (pct >= 70) ? BarColors::BAT_MED
                         :               BarColors::CPU;
            } else {
                wchar_t temp[128] = L"";
                int maxPct = 0;
                for (size_t g = 0; g < gpus.size(); g++) {
                    int pct = (int)(gpus[g].sum3D + 0.5);
                    if (pct > 100) pct = 100;
                    if (pct > maxPct) maxPct = pct;

                    wchar_t gpuStr[32];
                    swprintf_s(gpuStr, L"GPU%d %d%% ", (int)g, pct);
                    wcscat_s(temp, gpuStr);
                }
                size_t len = wcslen(temp);
                if (len > 0 && temp[len - 1] == L' ') temp[len - 1] = L'\0';
                wcscpy_s(w->text, temp);
                w->color = (maxPct >= 90) ? BarColors::BAT_LOW
                         : (maxPct >= 70) ? BarColors::BAT_MED
                         :                  BarColors::CPU;
            }
        }
    } else {
        wcscpy_s(w->text, L"GPU ---%");
        w->color = BarColors::MUTED;
    }
}

// =====================================================================
// AUDIO DEVICE SWITCHER WIDGET LOGIC
// =====================================================================

// GUID for IPolicyConfig and CPolicyConfigClient
#ifndef __IPolicyConfig_INTERFACE_DEFINED__
#define __IPolicyConfig_INTERFACE_DEFINED__

interface DECLSPEC_UUID("f8679f50-850a-41cf-9c72-430f290290c8") IPolicyConfig : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX **) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX **) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX *, WAVEFORMATEX *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY &, PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY &, const PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR wszDeviceId, ERole eRole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};
class DECLSPEC_UUID("870af99c-171d-4f9e-af0d-e63df40c2bc9") CPolicyConfigClient;
#endif

// Property key for device friendly name
static const PROPERTYKEY Local_PKEY_Device_FriendlyName = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

void Widgets_ShowAudioDeviceMenu(HWND hWnd, int x, int y)
{
    IMMDeviceEnumerator* pEnum = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&pEnum));
    if (FAILED(hr)) return;

    IMMDeviceCollection* pCol = nullptr;
    hr = pEnum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCol);
    if (FAILED(hr)) {
        pEnum->Release();
        return;
    }

    // Get current default device ID
    LPWSTR defaultDevId = nullptr;
    IMMDevice* pDefaultDev = nullptr;
    if (SUCCEEDED(pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDefaultDev))) {
        pDefaultDev->GetId(&defaultDevId);
        pDefaultDev->Release();
    }

    UINT count = 0;
    pCol->GetCount(&count);

    HMENU hMenu = CreatePopupMenu();
    if (hMenu) {
        AppendMenuW(hMenu, MF_STRING | MF_DISABLED, 0, L"Select Playback Device");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

        std::vector<std::wstring> devIds;

        for (UINT i = 0; i < count && i < 16; i++) {
            IMMDevice* pDev = nullptr;
            if (SUCCEEDED(pCol->Item(i, &pDev))) {
                LPWSTR devId = nullptr;
                pDev->GetId(&devId);

                IPropertyStore* pProp = nullptr;
                if (SUCCEEDED(pDev->OpenPropertyStore(STGM_READ, &pProp))) {
                    PROPVARIANT varName;
                    PropVariantInit(&varName);
                    if (SUCCEEDED(pProp->GetValue(Local_PKEY_Device_FriendlyName, &varName))) {
                        UINT flags = MF_STRING;
                        if (defaultDevId && devId && wcscmp(defaultDevId, devId) == 0) {
                            flags |= MF_CHECKED;
                        }

                        AppendMenuW(hMenu, flags, 2000 + i, varName.pwszVal);
                        devIds.push_back(devId ? devId : L"");
                    }
                    PropVariantClear(&varName);
                    pProp->Release();
                }
                if (devId) CoTaskMemFree(devId);
                pDev->Release();
            }
        }

        SetForegroundWindow(hWnd);
        int selection = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                       x, y, 0, hWnd, nullptr);
        DestroyMenu(hMenu);

        if (selection >= 2000 && selection < 2000 + (int)devIds.size()) {
            int idx = selection - 2000;
            const std::wstring& targetId = devIds[idx];

            IPolicyConfig* pPolicyConfig = nullptr;
            hr = CoCreateInstance(__uuidof(CPolicyConfigClient), nullptr,
                                  CLSCTX_ALL, __uuidof(IPolicyConfig),
                                  reinterpret_cast<void**>(&pPolicyConfig));
            if (SUCCEEDED(hr)) {
                pPolicyConfig->SetDefaultEndpoint(targetId.c_str(), eConsole);
                pPolicyConfig->SetDefaultEndpoint(targetId.c_str(), eMultimedia);
                pPolicyConfig->SetDefaultEndpoint(targetId.c_str(), eCommunications);
                pPolicyConfig->Release();
            }
        }
    }

    if (defaultDevId) CoTaskMemFree(defaultDevId);
    pCol->Release();
    pEnum->Release();
}

// =====================================================================
// REGISTRY SETTINGS PERSISTENCE HELPERS
// =====================================================================
static void LoadWidgetSettings();
static void SaveWidgetSetting(const wchar_t* name, bool visible);


// =====================================================================
// CPU WIDGET
// Right zone — "CPU 6.5%"
// Uses GetSystemTimes delta. Color shifts red when high.
// WHY TIMER: no "CPU% changed" event exists in Windows.
// =====================================================================
static void Update_CPU(Widget* w)
{
    FILETIME ftIdle, ftKernel, ftUser;
    if (!GetSystemTimes(&ftIdle, &ftKernel, &ftUser)) return;

    ULONGLONG idle   = FtToU64(ftIdle);
    ULONGLONG kernel = FtToU64(ftKernel); // Includes idle!
    ULONGLONG user   = FtToU64(ftUser);

    if (w->cpu_hasPrev) {
        ULONGLONG dIdle   = idle   - w->cpu_prevIdle;
        ULONGLONG dKernel = kernel - w->cpu_prevKernel;
        ULONGLONG dUser   = user   - w->cpu_prevUser;
        ULONGLONG total   = dKernel + dUser;

        int pct = (total > 0) ? (int)(((total - dIdle) * 100) / total) : 0;
        pct = max(0, min(100, pct));

        swprintf_s(w->text, _countof(w->text), L"CPU %d%%", pct);
        w->color = (pct >= 90) ? BarColors::BAT_LOW
                 : (pct >= 70) ? BarColors::BAT_MED
                 :               BarColors::CPU;
    } else {
        wcscpy_s(w->text, L"CPU ---%");
        w->color   = BarColors::MUTED;
        w->cpu_hasPrev = true;
    }

    w->cpu_prevIdle   = idle;
    w->cpu_prevKernel = kernel;
    w->cpu_prevUser   = user;
}

// Weather — Right zone
// Uses IP-API.com for geolocation, then open-meteo.com for weather.
// =====================================================================
#include <wininet.h>
#pragma comment(lib, "wininet.lib")

static bool g_weatherFetching = false;
static ULONGLONG g_weatherLastFetch = 0; // GetTickCount64()
static double g_weatherLat = 0.0;
static double g_weatherLon = 0.0;
static bool g_weatherHasLocation = false;

static void FetchWeatherAsync(Widget* w) {
    if (g_weatherFetching) return;
    g_weatherFetching = true;
    
    std::thread([w]() {
        bool success = false;
        HINTERNET hInt = InternetOpenW(L"ZenBar", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
        if (hInt) {
            // 1. Get Location
            if (!g_weatherHasLocation) {
                HINTERNET hUrl = InternetOpenUrlW(hInt, L"http://ip-api.com/json/", nullptr, 0, INTERNET_FLAG_RELOAD, 0);
                if (hUrl) {
                    char buf[1024]; DWORD read = 0;
                    std::string resp;
                    while (InternetReadFile(hUrl, buf, sizeof(buf) - 1, &read) && read > 0) {
                        buf[read] = 0; resp += buf;
                    }
                    InternetCloseHandle(hUrl);
                    
                    size_t latPos = resp.find("\"lat\":");
                    size_t lonPos = resp.find("\"lon\":");
                    if (latPos != std::string::npos && lonPos != std::string::npos) {
                        g_weatherLat = atof(resp.c_str() + latPos + 6);
                        g_weatherLon = atof(resp.c_str() + lonPos + 6);
                        g_weatherHasLocation = true;
                    }
                }
            }
            
            // 2. Get Weather
            if (g_weatherHasLocation) {
                wchar_t url[256];
                swprintf_s(url, L"https://api.open-meteo.com/v1/forecast?latitude=%f&longitude=%f&current_weather=true", g_weatherLat, g_weatherLon);
                HINTERNET hUrl = InternetOpenUrlW(hInt, url, nullptr, 0, INTERNET_FLAG_RELOAD, 0);
                if (hUrl) {
                    char buf[1024]; DWORD read = 0;
                    std::string resp;
                    while (InternetReadFile(hUrl, buf, sizeof(buf) - 1, &read) && read > 0) {
                        buf[read] = 0; resp += buf;
                    }
                    InternetCloseHandle(hUrl);
                    
                    size_t curPos = resp.find("\"current_weather\"");
                    if (curPos != std::string::npos) {
                        size_t tempPos = resp.find("\"temperature\":", curPos);
                        size_t codePos = resp.find("\"weathercode\":", curPos);
                        if (tempPos != std::string::npos && codePos != std::string::npos) {
                            double temp = atof(resp.c_str() + tempPos + 14);
                            int code = atoi(resp.c_str() + codePos + 14);
                            
                            const wchar_t* icon = L"\u2601"; // Cloud
                            if (code == 0) icon = L"\u2600"; // Clear
                            else if (code >= 1 && code <= 3) icon = L"\u26C5"; // Partly cloudy
                            else if (code >= 50 && code <= 69) icon = L"\u2614"; // Rain
                            else if (code >= 70 && code <= 79) icon = L"\u2744"; // Snow
                            else if (code >= 95) icon = L"\u26A1"; // Thunder

                            swprintf_s(w->text, L"%d\u00B0C %s", (int)(temp + 0.5), icon);
                            w->color = BarColors::VALUE;
                            success = true;
                        }
                    }
                }
            }
            InternetCloseHandle(hInt);
        }
        
        if (!success && w->text[0] == 0) {
            wcscpy_s(w->text, L"Weather ---");
        }
        g_weatherFetching = false;
        
        // Post a message to redraw or just let the timer catch it
    }).detach();
}

static void Update_Weather(Widget* w) {
    if (!w->visible) return;
    
    ULONGLONG now = GetTickCount64();
    // Fetch every 30 minutes (1800000 ms)
    if (w->text[0] == 0 || (now - g_weatherLastFetch) > 1800000) {
        g_weatherLastFetch = now;
        FetchWeatherAsync(w);
        if (w->text[0] == 0) {
            wcscpy_s(w->text, L"Updating...");
            w->color = BarColors::MUTED;
        }
    }
}

// =====================================================================
// NETWORK WIDGET
// Right zone — "▲1.4MB/s ▼14.7KB/s"
// Uses GetIfTable (32-bit counters, widely available, no netioapi needed).
// WHY TIMER: network speed IS a delta measurement — no event exists.
// =====================================================================
static void Update_Network(Widget* w)
{
    // Get required buffer size first
    ULONG tableSize = 0;
    GetIfTable(nullptr, &tableSize, FALSE);
    if (tableSize == 0) return;

    MIB_IFTABLE* pTable = reinterpret_cast<MIB_IFTABLE*>(malloc(tableSize));
    if (!pTable) return;

    ULONG64 totalIn = 0, totalOut = 0;

    if (GetIfTable(pTable, &tableSize, FALSE) == NO_ERROR) {
        for (DWORD i = 0; i < pTable->dwNumEntries; i++) {
            MIB_IFROW& row = pTable->table[i];
            // Skip loopback and disconnected adapters
            if (row.dwType == MIB_IF_TYPE_LOOPBACK)                 continue;
            if (row.dwOperStatus != MIB_IF_OPER_STATUS_OPERATIONAL) continue;
            totalIn  += row.dwInOctets;   // 32-bit, wraps at ~4GB
            totalOut += row.dwOutOctets;
        }
    }
    free(pTable);

    if (w->net_hasPrev) {
        // Guard against 32-bit counter wrap (happens on fast NICs)
        ULONG64 dIn  = (totalIn  >= w->net_prevIn)  ? (totalIn  - w->net_prevIn)  : totalIn;
        ULONG64 dOut = (totalOut >= w->net_prevOut)  ? (totalOut - w->net_prevOut) : totalOut;

        WCHAR up[24], down[24];
        FormatSpeed(dOut, L"\u25B2", up,   _countof(up));    // ▲ upload
        FormatSpeed(dIn,  L"\u25BC", down, _countof(down));  // ▼ download
        swprintf_s(w->text, _countof(w->text), L"%s %s", up, down);
        w->color = BarColors::NET_UP; // Color handled per-segment in Draw_Network
    } else {
        wcscpy_s(w->text, L"\u25B2--- \u25BC---");
        w->net_hasPrev = true;
    }

    w->net_prevIn  = totalIn;
    w->net_prevOut = totalOut;
}

// Network has two colored parts — upload (blue) and download (purple)
static int Draw_Network(Widget* w, HDC hdc, HFONT fontMain, HFONT fontBold,
                        int x, int y, int barH)
{
    // Find the space that splits the two speed strings
    const wchar_t* space = wcschr(w->text, L' ');
    if (!space) return Draw_Default(w, hdc, fontMain, fontBold, x, y, barH);

    int curX = x;

    // Upload part (up to the space)
    WCHAR upPart[32] = {};
    int upLen = (int)(space - w->text);
    wcsncpy_s(upPart, w->text, upLen);
    curX += DrawBarText(hdc, upPart, curX, y, barH, fontMain, BarColors::NET_UP);
    curX += 6;

    // Download part (after the space)
    curX += DrawBarText(hdc, space + 1, curX, y, barH, fontMain, BarColors::NET_DOWN);
    return curX - x;
}

// =====================================================================
// Globals
// =====================================================================
bool g_isAutoHideEnabled = false;
bool g_showControls = true;
bool g_controlsOnlyAutoHide = true;
bool g_mediaUserEnabled = true; // User's toggle — FetchMediaAsync respects this
int  g_volScrollStep = 2;       // Scroll step for volume (1-10)
int  g_briScrollStep = 5;       // Scroll step for brightness (1-10)

void Widgets_UpdateSettings(bool autoHide, bool showControls, bool controlsOnlyAutoHide) {
    g_isAutoHideEnabled = autoHide;
    g_showControls = showControls;
    g_controlsOnlyAutoHide = controlsOnlyAutoHide;
}

void Widgets_SetScrollSteps(int volStep, int briStep) {
    g_volScrollStep = max(1, min(10, volStep));
    g_briScrollStep = max(1, min(10, briStep));
}

// =====================================================================
// VOLUME WIDGET
// Right zone — "VOL 75%" or "VOL MUTE"
// Uses cached COM interface that auto-rebinds when default device changes.
// =====================================================================
static void Update_Volume(Widget* w)
{
    IMMDeviceEnumerator* pEnum = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnum))) {
        IMMDevice* pDev = nullptr;
        if (SUCCEEDED(pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDev))) {
            LPWSTR newId = nullptr;
            if (SUCCEEDED(pDev->GetId(&newId))) {
                if (!w->vol_deviceId || wcscmp(w->vol_deviceId, newId) != 0) {
                    if (w->vol_iface) { w->vol_iface->Release(); w->vol_iface = nullptr; }
                    pDev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**)&w->vol_iface);
                    if (w->vol_deviceId) CoTaskMemFree(w->vol_deviceId);
                    w->vol_deviceId = newId;
                } else {
                    CoTaskMemFree(newId);
                }
            }
            pDev->Release();
        }
        pEnum->Release();
    }

    if (!w->vol_iface) {
        wcscpy_s(w->text, L"VOL ---");
        w->color = BarColors::MUTED;
        w->visible = false;
        return;
    }
    w->visible = true;

    float fVol = 0.0f;
    w->vol_iface->GetMasterVolumeLevelScalar(&fVol);
    BOOL muted = FALSE;
    w->vol_iface->GetMute(&muted);

    int pct = (int)(fVol * 100.0f + 0.5f);
    if (muted || pct == 0) {
        wcscpy_s(w->text, L"VOL MUTE");
        w->color = BarColors::MUTED;
    } else {
        swprintf_s(w->text, _countof(w->text), L"VOL %d%%", pct);
        // Color feedback: normal amber < 75%, orange-red warning >= 75%
        w->color = (pct >= 75) ? BarColors::VOLUME_HIGH : BarColors::VOLUME;
    }
}

// =====================================================================
// BRIGHTNESS WIDGET
// Right zone — "BRI 80%"
// Uses DDC/CI with a Power API scheme fallback for laptops/internal panels.
// =====================================================================
static int PowerGetBrightness()
{
    int val = 50; // default fallback
    GUID* pActiveScheme = nullptr;
    if (PowerGetActiveScheme(nullptr, &pActiveScheme) == ERROR_SUCCESS) {
        SYSTEM_POWER_STATUS sps = {};
        BOOL onAC = TRUE;
        if (GetSystemPowerStatus(&sps)) {
            onAC = (sps.ACLineStatus == 1);
        }
        DWORD level = 0;
        DWORD result = 0;
        if (onAC) {
            result = PowerReadACValueIndex(nullptr, pActiveScheme, &GUID_VIDEO_SUBGROUP, &GUID_DEVICE_POWER_POLICY_VIDEO_BRIGHTNESS, &level);
        } else {
            result = PowerReadDCValueIndex(nullptr, pActiveScheme, &GUID_VIDEO_SUBGROUP, &GUID_DEVICE_POWER_POLICY_VIDEO_BRIGHTNESS, &level);
        }
        if (result == ERROR_SUCCESS) {
            val = (int)level;
        }
        LocalFree(pActiveScheme);
    }
    return val;
}

static void PowerSetBrightness(int level)
{
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    GUID* pActiveScheme = nullptr;
    if (PowerGetActiveScheme(nullptr, &pActiveScheme) == ERROR_SUCCESS) {
        SYSTEM_POWER_STATUS sps = {};
        BOOL onAC = TRUE;
        if (GetSystemPowerStatus(&sps)) {
            onAC = (sps.ACLineStatus == 1);
        }
        if (onAC) {
            PowerWriteACValueIndex(nullptr, pActiveScheme, &GUID_VIDEO_SUBGROUP, &GUID_DEVICE_POWER_POLICY_VIDEO_BRIGHTNESS, level);
        } else {
            PowerWriteDCValueIndex(nullptr, pActiveScheme, &GUID_VIDEO_SUBGROUP, &GUID_DEVICE_POWER_POLICY_VIDEO_BRIGHTNESS, level);
        }
        PowerSetActiveScheme(nullptr, pActiveScheme);
        LocalFree(pActiveScheme);
    }
}

static void Update_Brightness(Widget* w)
{
    int pct = 0;
    if (w->bri_monHandle) {
        DWORD cur = 0, dummy = 0;
        if (GetMonitorBrightness((HANDLE)w->bri_monHandle, &dummy, &cur, &dummy)) {
            DWORD range = w->bri_max - w->bri_min;
            pct = (range > 0) ? (int)(((cur - w->bri_min) * 100) / range) : 0;
        } else {
            pct = PowerGetBrightness();
        }
    } else {
        pct = PowerGetBrightness();
    }
    swprintf_s(w->text, _countof(w->text), L"BRI %d%%", pct);
    // Heat-map: off/dim=muted gray, low=dim yellow, mid=bright yellow, high=amber warning
    if (pct == 0)       w->color = BarColors::MUTED;
    else if (pct < 50)  w->color = BarColors::BRIGHTNESS;  // dim yellow
    else if (pct < 85)  w->color = BarColors::BRI_HIGH;    // bright yellow
    else                w->color = BarColors::BRI_MAX;     // amber/orange — high brightness warning
}

// =====================================================================
// BATTERY WIDGET
// Right zone — "BAT 87%" or "CHG 87%"
// Update() is called by timer AND by AppBar on WM_POWERBROADCAST (event-driven).
// =====================================================================
bool g_batterySaverActive = false;

static void Update_Battery(Widget* w)
{
    SYSTEM_POWER_STATUS sps = {};
    if (!GetSystemPowerStatus(&sps)) return;

    w->bat_percent  = sps.BatteryLifePercent;
    w->bat_charging = (sps.ACLineStatus == 1);
    
    // Support Windows 10 Battery Saver AND Windows 11 Energy Saver
    bool batterySaver = g_batterySaverActive || (sps.SystemStatusFlag == 1);
    
    w->visible = true;

    if (w->bat_percent == 255) {
        w->visible = false;
        return;
    }

    if (w->bat_charging) {
        // Charging — show CHG prefix, always green regardless of percentage
        swprintf_s(w->text, _countof(w->text), L"CHG %d%%", (int)w->bat_percent);
        w->color = BarColors::BAT_CHARGING;
    } else if (batterySaver) {
        swprintf_s(w->text, _countof(w->text), L"SAV %d%%", (int)w->bat_percent);
        w->color = BarColors::BAT_SAVER;
    } else if (w->bat_percent <= 15) {
        // Low battery — red
        swprintf_s(w->text, _countof(w->text), L"BAT %d%%", (int)w->bat_percent);
        w->color = BarColors::BAT_LOW;
    } else if (w->bat_percent <= 35) {
        // Getting low — yellow
        swprintf_s(w->text, _countof(w->text), L"BAT %d%%", (int)w->bat_percent);
        w->color = BarColors::BAT_MED;
    } else {
        // Normal — soft blue
        swprintf_s(w->text, _countof(w->text), L"BAT %d%%", (int)w->bat_percent);
        w->color = BarColors::BAT_GOOD;
    }
}

// =====================================================================
// MEDIA CONTROLS WIDGET
// Right zone — ⏮ ⏸/▶ ⏭  Song Name  [1/N if multiple players]
// =====================================================================
using namespace winrt::Windows::Media::Control;

// Index of the currently-shown session (0-based). Cycled by scrolling over the widget.
int g_mediaSessionIndex = 0;
int g_mediaTotalSessions = 0;
static winrt::hstring g_mediaLastActiveId = L"";
static winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager g_mediaManager = nullptr;

static winrt::fire_and_forget FetchMediaAsync(Widget* w) {
    try {
        if (!g_mediaUserEnabled) { w->visible = false; co_return; }

        if (!g_mediaManager) {
            g_mediaManager = co_await GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
        }
        if (!g_mediaManager) { w->visible = false; g_mediaTotalSessions = 0; co_return; }

        auto sessions = g_mediaManager.GetSessions();
        int count = (int)sessions.Size();
        g_mediaTotalSessions = count;

        if (count == 0) { w->visible = false; co_return; }

        // Auto-switch: only jump if the OS-designated "current" session *changes* to a new app.
        // This allows manual scrolling to stick until the user starts music somewhere else.
        auto currentSession = g_mediaManager.GetCurrentSession();
        if (currentSession) {
            auto currentId = currentSession.SourceAppUserModelId();
            if (currentId != g_mediaLastActiveId) {
                g_mediaLastActiveId = currentId;
                // Jump to this new session
                for (int i = 0; i < count; i++) {
                    auto s = sessions.GetAt(i);
                    if (s && s.SourceAppUserModelId() == currentId) {
                        g_mediaSessionIndex = i;
                        break;
                    }
                }
            }
        }

        // Clamp
        if (g_mediaSessionIndex >= count) g_mediaSessionIndex = count - 1;
        if (g_mediaSessionIndex < 0)      g_mediaSessionIndex = 0;

        auto session = sessions.GetAt(g_mediaSessionIndex);
        if (!session) { w->visible = false; co_return; }
        
        auto appId = session.SourceAppUserModelId();
        wcscpy_s(w->media_appId, appId.c_str());

        auto playback = session.GetPlaybackInfo();
        if (playback) {
            w->media_isPlaying = (playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);
            auto controls = playback.Controls();
            w->media_canPlayPause = controls.IsPlayEnabled() || controls.IsPauseEnabled() || controls.IsPlayPauseToggleEnabled();
            w->media_canPrev = controls.IsPreviousEnabled();
            w->media_canNext = controls.IsNextEnabled();
        } else {
            w->media_isPlaying = false;
            w->media_canPlayPause = w->media_canPrev = w->media_canNext = false;
        }

        auto props = co_await session.TryGetMediaPropertiesAsync();
        if (props) {
            std::wstring title  = props.Title().c_str();
            std::wstring artist = props.Artist().c_str();
            std::wstring display = title;
            if (!artist.empty()) display += L" - " + artist;

            int maxChars = (count > 1) ? 22 : 28;
            if ((int)display.length() > maxChars)
                display = display.substr(0, maxChars - 3) + L"...";

            if (count > 1) {
                wchar_t idx[16];
                swprintf_s(idx, L" [%d/%d]", g_mediaSessionIndex + 1, count);
                display += idx;
            }

            wcscpy_s(w->media_songText, display.c_str());
        } else {
            wcscpy_s(w->media_songText, L"");
        }

        if (g_mediaUserEnabled) w->visible = true;
    } catch (...) {
        w->visible = false;
    }
}

static void Update_Media(Widget* w) {
    if (!g_mediaUserEnabled) { w->visible = false; return; }
    FetchMediaAsync(w);
}

// Called from AppBar WM_MOUSEWHEEL on the Media widget — cycles sessions and immediately fetches.
void Widgets_CycleMediaSession(int dir) {
    if (g_mediaTotalSessions <= 1) return;
    g_mediaSessionIndex += dir;
    if (g_mediaSessionIndex < 0)                    g_mediaSessionIndex = g_mediaTotalSessions - 1;
    if (g_mediaSessionIndex >= g_mediaTotalSessions) g_mediaSessionIndex = 0;
    // Immediately kick a fetch for the newly selected session
    for (int i = 0; i < g_widgetCount; i++) {
        if (g_widgets[i].Update == Update_Media) {
            FetchMediaAsync(&g_widgets[i]);
            break;
        }
    }
}

static int Draw_Media(Widget* w, HDC hdc, HFONT fontMain, HFONT fontBold, int x, int y, int barH) {
    int btnW = 24;
    int curX = x;
    
    w->media_rectPrev = { curX, 0, curX + btnW, barH };
    DrawBarText(hdc, L"\u23EE", curX + 6, y, barH, fontMain, w->media_canPrev ? BarColors::VALUE : BarColors::MUTED); // Prev (⏪)
    curX += btnW;
    
    w->media_rectPlay = { curX, 0, curX + btnW, barH };
    const wchar_t* playIcon = w->media_isPlaying ? L"\u23F8" : L"\u25B6"; // Pause ⏸ or Play ▶
    DrawBarText(hdc, playIcon, curX + 6, y, barH, fontMain, w->media_canPlayPause ? BarColors::VALUE : BarColors::MUTED); 
    curX += btnW;
    
    w->media_rectNext = { curX, 0, curX + btnW, barH };
    DrawBarText(hdc, L"\u23ED", curX + 6, y, barH, fontMain, w->media_canNext ? BarColors::VALUE : BarColors::MUTED); // Next (⏩)
    curX += btnW;
    
    if (w->media_songText[0] != 0) {
        curX += 4; // margin
        int textW = DrawBarText(hdc, w->media_songText, curX, y, barH, fontMain, BarColors::MUTED);
        w->media_rectText = { curX, 0, curX + textW, barH };
        curX += textW;
    } else {
        w->media_rectText = { 0, 0, 0, 0 };
    }
    
    return curX - x;
}

// =====================================================================
// APP CONTROLS WIDGET
// Right zone — Min, Max, Close buttons for active window
// =====================================================================
static void Update_AppControls(Widget* w) {
    if (!g_showControls) {
        w->visible = false;
    } else if (g_controlsOnlyAutoHide) {
        w->visible = g_isAutoHideEnabled;
    } else {
        w->visible = true;
    }
}

static int Draw_AppControls(Widget* w, HDC hdc, HFONT fontMain, HFONT fontBold, int x, int y, int barH) {
    int btnW = 46; // Make buttons wider like standard Windows 11 controls
    w->app_rectMin = { x, 0, x + btnW, barH };
    w->app_rectMax = { x + btnW, 0, x + btnW * 2, barH };
    w->app_rectClose = { x + btnW * 2, 0, x + btnW * 3, barH };

    if (w->app_hoverIndex == 1) {
        HBRUSH hBr = CreateSolidBrush(RGB(50, 50, 50));
        FillRect(hdc, &w->app_rectMin, hBr);
        DeleteObject(hBr);
    } else if (w->app_hoverIndex == 2) {
        HBRUSH hBr = CreateSolidBrush(RGB(50, 50, 50));
        FillRect(hdc, &w->app_rectMax, hBr);
        DeleteObject(hBr);
    } else if (w->app_hoverIndex == 3) {
        HBRUSH hBr = CreateSolidBrush(RGB(232, 17, 35)); // Standard Windows red close
        FillRect(hdc, &w->app_rectClose, hBr);
        DeleteObject(hBr);
    }

    DrawBarText(hdc, L"\u2014", x + (btnW / 2) - 6, y, barH, fontMain, BarColors::VALUE); // Min
    DrawBarText(hdc, L"\u25A1", x + btnW + (btnW / 2) - 4, y, barH, fontMain, BarColors::VALUE); // Max
    
    // Draw close button text: white if hovered, otherwise red
    COLORREF closeColor = (w->app_hoverIndex == 3) ? RGB(255, 255, 255) : BarColors::BAT_LOW;
    DrawBarText(hdc, L"\u2715", x + btnW * 2 + (btnW / 2) - 4, y, barH, fontMain, closeColor); // Close

    return btnW * 3;
}

// =====================================================================
// WIDGET REGISTRY — add/remove/reorder widgets here
// Visual order (Left → Right): Clock (center), CPU, GPU, Network, Volume, Brightness, Battery, App Controls
// =====================================================================
Widget g_widgets[] = {
    // Clock — Center zone
    { .name = L"Clock",          .zone = Zone::Center, .visible = true, .Update = Update_Clock,      .Draw = Draw_Clock   },
    // CPU — Right zone
    { .name = L"CPU Usage",      .zone = Zone::Right,  .visible = false, .Update = Update_CPU,        .Draw = Draw_Default },
    // GPU — Right zone
    { .name = L"GPU Usage",      .zone = Zone::Right,  .visible = false, .Update = Update_GPU,        .Draw = Draw_Default },
    // Network — Right zone (draws up+down in two colors)
    { .name = L"Network Speed",  .zone = Zone::Right,  .visible = true, .Update = Update_Network,    .Draw = Draw_Network },

    // Weather — Left zone (disabled by default)
    { .name = L"Weather",        .zone = Zone::Right,  .visible = false,.Update = Update_Weather,    .Draw = Draw_Default },
    // Media Controls — Right zone
    { .name = L"Media Controls", .zone = Zone::Right,  .visible = true, .Update = Update_Media,      .Draw = Draw_Media },
    // Volume — Right zone
    { .name = L"Volume",         .zone = Zone::Right,  .visible = true, .Update = Update_Volume,     .Draw = Draw_Default },
    // Brightness — Right zone (auto-hides if DDC/CI unavailable)
    { .name = L"Brightness",     .zone = Zone::Right,  .visible = true, .Update = Update_Brightness, .Draw = Draw_Default },
    // Battery — Right zone (auto-hides on desktop PCs with no battery)
    { .name = L"Battery",        .zone = Zone::Right,  .visible = true, .Update = Update_Battery,    .Draw = Draw_Default },
    // App Controls — Right zone (extreme right)
    { .name = L"App Controls",   .zone = Zone::Right,  .visible = true, .Update = Update_AppControls,.Draw = Draw_AppControls },
};
int g_widgetCount = _countof(g_widgets);

// Load settings from Registry
static void LoadWidgetSettings()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\ZenBar\\Widgets", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        for (int i = 0; i < g_widgetCount; i++) {
            DWORD val = 1;
            DWORD size = sizeof(val);
            if (RegQueryValueExW(hKey, g_widgets[i].name, nullptr, nullptr, reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS) {
                g_widgets[i].visible = (val != 0);
            }
        }
        // Sync the media user-enable flag from saved visibility
        for (int i = 0; i < g_widgetCount; i++) {
            if (wcscmp(g_widgets[i].name, L"Media Controls") == 0) {
                g_mediaUserEnabled = g_widgets[i].visible;
                break;
            }
        }
        RegCloseKey(hKey);
    }
}

// Save settings to Registry
static void SaveWidgetSetting(const wchar_t* name, bool visible)
{
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\ZenBar\\Widgets", 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        DWORD val = visible ? 1 : 0;
        RegSetValueExW(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(val));
        RegCloseKey(hKey);
    }
}

// Toggle visibility of a widget
void Widgets_ToggleVisibility(int index)
{
    if (index >= 0 && index < g_widgetCount) {
        bool newVisible = !g_widgets[index].visible;
        g_widgets[index].visible = newVisible;
        // For Media Controls, sync the user-enable flag so async updates respect it
        if (wcscmp(g_widgets[index].name, L"Media Controls") == 0) {
            g_mediaUserEnabled = newVisible;
        }
        SaveWidgetSetting(g_widgets[index].name, newVisible);
    }
}

// Scroll adjust functions — use configurable step
void Widgets_AdjustVolume(int direction)
{
    int step = g_volScrollStep * direction; // direction: +1 or -1 (from scroll)
    for (int i = 0; i < g_widgetCount; i++) {
        Widget& w = g_widgets[i];
        if (w.Update == Update_Volume && w.vol_iface) {
            float current = 0.0f;
            w.vol_iface->GetMasterVolumeLevelScalar(&current);
            int pct = (int)(current * 100.0f + 0.5f);
            pct += step;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            
            BOOL muted = FALSE;
            w.vol_iface->GetMute(&muted);
            if (muted && step > 0) {
                w.vol_iface->SetMute(FALSE, nullptr);
            }

            w.vol_iface->SetMasterVolumeLevelScalar((float)pct / 100.0f, nullptr);
            break;
        }
    }
}

void Widgets_AdjustBrightness(int direction)
{
    int step = g_briScrollStep * direction;
    for (int i = 0; i < g_widgetCount; i++) {
        Widget& w = g_widgets[i];
        if (w.Update != Update_Brightness) continue;

        if (w.bri_monHandle) {
            DWORD mn = w.bri_min;
            DWORD mx = w.bri_max;
            DWORD range = mx - mn;
            DWORD cur = 0, dummy = 0;
            if (GetMonitorBrightness((HANDLE)w.bri_monHandle, &dummy, &cur, &dummy)) {
                int pct = (range > 0) ? (int)(((cur - mn) * 100) / range) : 0;
                pct += step;
                if (pct < 0) pct = 0;
                if (pct > 100) pct = 100;

                DWORD newVal = mn + (range * pct) / 100;
                SetMonitorBrightness((HANDLE)w.bri_monHandle, newVal);
            }
        } else {
            int pct = PowerGetBrightness();
            pct += step;
            PowerSetBrightness(pct);
        }
        break;
    }
}

// =====================================================================
// Widgets_Init — one-time setup
// =====================================================================
void Widgets_Init(HWND hWnd)
{
    // Load persisted visibility settings
    LoadWidgetSettings();

    // --- Brightness: get DDC/CI handle for primary monitor ---
    HMONITOR hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY);
    DWORD numPhys = 0;
    bool physicalMonitorFound = false;
    if (GetNumberOfPhysicalMonitorsFromHMONITOR(hMon, &numPhys) && numPhys > 0) {
        PHYSICAL_MONITOR pm = {};
        if (GetPhysicalMonitorsFromHMONITOR(hMon, 1, &pm)) {
            DWORD mn = 0, cur = 0, mx = 0;
            if (GetMonitorBrightness(pm.hPhysicalMonitor, &mn, &cur, &mx)) {
                for (int i = 0; i < g_widgetCount; i++) {
                    if (g_widgets[i].Update == Update_Brightness) {
                        g_widgets[i].bri_monHandle = (HANDLE)pm.hPhysicalMonitor;
                        g_widgets[i].bri_min       = mn;
                        g_widgets[i].bri_max       = mx;
                        physicalMonitorFound = true;
                    }
                }
            } else {
                DestroyPhysicalMonitors(1, &pm);
            }
        }
    }
    if (!physicalMonitorFound) {
        for (int i = 0; i < g_widgetCount; i++) {
            if (g_widgets[i].Update == Update_Brightness) {
                g_widgets[i].bri_monHandle = nullptr;
                g_widgets[i].bri_min       = 0;
                g_widgets[i].bri_max       = 100;
            }
        }
    }

    // Initialize PDH GPU query if GPU is visible
    for (int i = 0; i < g_widgetCount; i++) {
        if (g_widgets[i].Update == Update_GPU && g_widgets[i].visible) {
            Init_GPU_PDH();
        }
    }

    // Run initial update on all widgets so first paint has real data
    Widgets_UpdateAll();
}

// =====================================================================
// Widgets_UpdateAll — called every second
// =====================================================================
void Widgets_UpdateAll()
{
    for (int i = 0; i < g_widgetCount; i++) {
        g_widgets[i].Update(&g_widgets[i]);
    }
}

// Update ONLY the named widget — used by scroll handlers to avoid running
// the heavy full-update (COM queries, DDC/CI, PDH) just to refresh one value.
void Widgets_UpdateByName(const wchar_t* name)
{
    for (int i = 0; i < g_widgetCount; i++) {
        if (wcscmp(g_widgets[i].name, name) == 0) {
            g_widgets[i].Update(&g_widgets[i]);
            break;
        }
    }
}

// =====================================================================
// Widgets_DrawZone — draw all visible widgets in a zone
// =====================================================================
int Widgets_DrawZone(Zone zone, HDC hdc, HFONT fontMain, HFONT fontBold,
                     int x, int barH, int barW)
{
    if (zone == Zone::Right) {
        bool first = true;
        for (int i = g_widgetCount - 1; i >= 0; i--) {
            Widget& w = g_widgets[i];
            if (w.zone != Zone::Right || !w.visible) continue;

            if (first && wcscmp(w.name, L"App Controls") != 0) {
                x -= 16; // Add right padding if App Controls are disabled/hidden
            }

            // Measure using the standard fontBold
            int ww = 0;
            if (wcscmp(w.name, L"App Controls") == 0) ww = 46 * 3;
            else if (wcscmp(w.name, L"Media Controls") == 0) {
                ww = 24 * 3; // Prev, Play, Next buttons
                if (w.media_songText[0] != 0) {
                    ww += 4 + MeasureText(hdc, w.media_songText, fontMain);
                }
            }
            else ww = MeasureText(hdc, w.text, fontBold);

            if (!first) {
                HPEN hPen = CreatePen(PS_SOLID, 1, BarColors::SEPARATOR);
                HPEN hOld = (HPEN)SelectObject(hdc, hPen);
                MoveToEx(hdc, x + 5, 6,          nullptr);
                LineTo  (hdc, x + 5, barH - 6);
                SelectObject(hdc, hOld);
                DeleteObject(hPen);
                x -= 10;
            }
            first = false;

            x -= ww;
            w.Draw(&w, hdc, fontMain, fontBold, x, 0, barH);
            w.rect = { x, 0, x + ww, barH }; // Cache client coordinates
            x -= Widget::PAD;
        }
        return x;

    } else if (zone == Zone::Center) {
        int totalW = 0;
        for (int i = 0; i < g_widgetCount; i++) {
            Widget& w = g_widgets[i];
            if (w.zone != Zone::Center || !w.visible) continue;
            totalW += MeasureText(hdc, w.clock_date, fontMain) + 6
                    + MeasureText(hdc, L"\u00B7",    fontMain) + 6
                    + MeasureText(hdc, w.clock_time, fontBold);
            totalW += Widget::PAD;
        }
        if (totalW > Widget::PAD) totalW -= Widget::PAD;

        int startX = (barW - totalW) / 2;
        for (int i = 0; i < g_widgetCount; i++) {
            Widget& w = g_widgets[i];
            if (w.zone != Zone::Center || !w.visible) continue;
            int consumed = w.Draw(&w, hdc, fontMain, fontBold, startX, 0, barH);
            w.rect = { startX, 0, startX + consumed, barH }; // Cache client coordinates
            startX += consumed + Widget::PAD;
        }
        return startX;

    } else {
        for (int i = 0; i < g_widgetCount; i++) {
            Widget& w = g_widgets[i];
            if (w.zone != Zone::Left || !w.visible) continue;
            int consumed = w.Draw(&w, hdc, fontMain, fontBold, x, 0, barH);
            w.rect = { x, 0, x + consumed, barH }; // Cache client coordinates
            x += consumed + Widget::PAD;
        }
        return x;
    }
}

// =====================================================================
// Widgets_DrawBatteryBorder — colored bottom edge = battery %
// =====================================================================
void Widgets_DrawBatteryBorder(HDC hdc, int barW, int barH)
{
    for (int i = 0; i < g_widgetCount; i++) {
        Widget& w = g_widgets[i];
        if (w.Update != Update_Battery) continue;
        if (w.bat_percent == 255 || !w.visible) return;

        int fillW = (barW * (int)w.bat_percent) / 100;

        HBRUSH hFill = CreateSolidBrush(w.color);
        RECT rFill = { 0, barH - 2, fillW, barH };
        FillRect(hdc, &rFill, hFill);
        DeleteObject(hFill);

        if (fillW < barW) {
            // Brighter track so the user can easily see the unfilled portion
            HBRUSH hTrack = CreateSolidBrush(RGB(90, 90, 100));
            RECT rTrack = { fillW, barH - 2, barW, barH };
            FillRect(hdc, &rTrack, hTrack);
            DeleteObject(hTrack);
        }
        return;
    }
}

// =====================================================================
// Widgets_Destroy — free COM + DDC/CI resources
// =====================================================================
void Widgets_Destroy()
{
    if (g_gpuQuery) {
        PdhCloseQuery(g_gpuQuery);
        g_gpuQuery = nullptr;
        g_gpuPdhInit = false;
    }

    for (int i = 0; i < g_widgetCount; i++) {
        Widget& w = g_widgets[i];

        if (w.vol_iface) {
            w.vol_iface->Release();
            w.vol_iface = nullptr;
        }
        if (w.vol_deviceId) {
            CoTaskMemFree(w.vol_deviceId);
            w.vol_deviceId = nullptr;
        }

        if (w.bri_monHandle) {
            PHYSICAL_MONITOR pm = { (HANDLE)w.bri_monHandle };
            DestroyPhysicalMonitors(1, &pm);
            w.bri_monHandle = nullptr;
        }
    }
}
