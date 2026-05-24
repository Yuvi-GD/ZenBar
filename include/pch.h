// pch.h — Precompiled Header for ZenBar
// -----------------------------------------------
// All Windows headers go here — compiled once, reused everywhere.

#ifndef PCH_H
#define PCH_H

// ---- IMPORTANT: Include order matters here ----
// winsock2.h must come before windows.h when using network APIs.
// WIN32_LEAN_AND_MEAN strips it from windows.h, so we include it explicitly.
#include <winsock2.h>           // Must be FIRST — before windows.h
#include <ws2tcpip.h>

#include "include\framework.h"          // windows.h + basic C runtime (defines WIN32_LEAN_AND_MEAN)

// Now safe to include network headers (winsock2.h already included above)
#include <iphlpapi.h>           // GetIfTable (network byte counters)

// Shell
#include <shellapi.h>           // SHAppBarMessage

// Common controls
#include <commctrl.h>

// DWM (Mica/Acrylic/blur)
#include <dwmapi.h>

// Audio (Core Audio API)
#include <mmdeviceapi.h>
#include <endpointvolume.h>

// Monitor brightness (DDC/CI)
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>

// Standard library
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.System.Power.h>

#include <string>
#include <vector>
#include <memory>
#include <thread>

// Auto-link libraries
#pragma comment(lib, "Ws2_32.lib")     // Winsock
#pragma comment(lib, "Ws2_32.lib")     // Winsock
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Dxva2.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "windowsapp.lib")

#endif // PCH_H
