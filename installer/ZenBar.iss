; ============================================================
; ZenBar — Inno Setup Script
; ============================================================
; Requirements:
;   - Inno Setup 6.3+ (https://jrsoftware.org/isinfo.php)
;   - Build Release x64 first: x64\Release\ZenBar.exe
;
; To build the installer from CMD:
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\ZenBar.iss
; ============================================================

#define AppName        "ZenBar"
#define AppVersion     "1.0.0"
#define AppPublisher   "ZenBar"
#define AppURL         "https://github.com/Yuvi-GD/ZenBar"
#define AppExeName     "ZenBar.exe"
#define AppDescription "Minimal Status Bar for Windows"

[Setup]
; --- App Identity ---
AppId={{8F3A7B2C-4D9E-4A1F-B6C8-3E7F2D9A1B4C}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases

; --- Install Location ---
; {autopf} = "C:\Program Files"        when user chooses "All Users" (admin)
;          = "C:\Users\...\AppData\Local\Programs"  when "Current User" (no admin)
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes

; --- Privilege Dialog ---
; Shows "Install for all users / current user only" choice
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

; --- Output ---
OutputDir=..\dist
OutputBaseFilename=ZenBar-{#AppVersion}-Setup
SetupIconFile=..\Resources\ZenBar.ico

; --- Compression ---
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes

; --- UI ---
WizardStyle=modern
WizardResizable=no

; --- Windows 10 1809+ required ---
MinVersion=10.0.17763

; --- Uninstaller ---
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExeName}

; --- Restart behavior ---
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "startup"; Description: "Start {#AppName} when Windows starts"; \
    GroupDescription: "Additional Options:"; Flags: unchecked

[Files]
Source: "..\x64\Release\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
; Start Menu shortcut
Name: "{group}\{#AppName}";              Filename: "{app}\{#AppExeName}"; \
    Comment: "{#AppDescription}"
Name: "{group}\Uninstall {#AppName}";    Filename: "{uninstallexe}"

[Registry]
; App Paths — makes Windows Search find "ZenBar"
; HKA = HKLM when admin install, HKCU when per-user install (auto)
Root: HKA; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\{#AppExeName}"; \
    ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExeName}"; Flags: uninsdeletekey

Root: HKA; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\{#AppExeName}"; \
    ValueType: string; ValueName: "Path"; ValueData: "{app}"; Flags: uninsdeletekey

; Run on startup — only when user ticks the checkbox
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "{#AppName}"; ValueData: """{app}\{#AppExeName}"""; \
    Flags: uninsdeletevalue; Tasks: startup

[Run]
; Launch ZenBar after install (optional — user can uncheck)
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName}"; \
    Flags: nowait postinstall skipifsilent

[UninstallRun]
; Kill ZenBar before uninstalling
Filename: "taskkill"; Parameters: "/F /IM {#AppExeName}"; \
    Flags: runhidden; RunOnceId: "StopZenBar"
