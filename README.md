<div align="center">

# ZenBar

**A sleek, minimal status bar for Windows 10 & 11**

ZenBar sits at the very top edge of your screen — always visible, never in the way. It gives you instant access to system stats, media controls, and window management in a single, beautiful bar.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows%2010%2F11-blue)](https://github.com/Yuvi-GD/ZenBar/releases)
[![Release](https://img.shields.io/github/v/release/Yuvi-GD/ZenBar)](https://github.com/Yuvi-GD/ZenBar/releases/latest)

</div>

---

## ✨ Features

| Feature | Description |
|---|---|
| 🕐 **Clock** | Clean date & time display in the center |
| 🌐 **Network Speed** | Real-time upload & download from active adapters |
| 💻 **CPU Usage** | Live CPU load percentage |
| 🔊 **Volume** | Scroll the mouse wheel to adjust — no menus needed |
| ☀️ **Brightness** | DDC/CI monitor brightness via scroll wheel |
| 🔋 **Battery** | Percentage + charging state + bottom-edge progress bar |
| 🎵 **Media Controls** | Native WinRT integration — works with Spotify, Chrome, etc. |
| 🖥️ **Window Controls** | Min / Max / Close the active window from the bar |
| 🪟 **Active App** | Shows the foreground app's icon and title in the left zone |
| ⚙️ **Settings** | Dark-themed settings panel for all customization |

---

## 🚀 Download & Install

> [!WARNING]
> **Windows SmartScreen** will show a blue *"Windows protected your PC"* popup on first launch because ZenBar is a newly-released, open-source app without a paid code-signing certificate. The full source code is available right here to audit.

### Installing ZenBar

1. Go to the **[Releases Page](../../releases)** and download `ZenBar-1.0.0-Setup.exe`
2. Run the installer
3. If SmartScreen appears: click **"More info"** → **"Run anyway"**
4. Choose your install type:
   - **All Users** → installs to `C:\Program Files\ZenBar\` (requires admin)
   - **Current User** → installs to `AppData\Local\Programs\ZenBar\` (no admin needed)
5. ZenBar launches automatically and is now searchable in the Start Menu

### Uninstalling

Use **Add or Remove Programs** in Windows Settings — ZenBar registers a proper uninstaller.

---

## 🛠️ Build from Source

**Requirements:**
- Windows 10 / 11
- Visual Studio 2022 with **Desktop development with C++** workload
- Windows 10/11 SDK

**Steps:**
1. Clone the repository:
   ```bash
   git clone https://github.com/Yuvi-GD/ZenBar.git
   cd ZenBar
   ```
2. Open `ZenBar.sln` in Visual Studio 2022
3. Set configuration to **Release | x64**
4. Press `Ctrl+Shift+B` to build
5. Run `x64\Release\ZenBar.exe`

**To build the installer** (requires [Inno Setup 6](https://jrsoftware.org/isinfo.php)):
```powershell
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\ZenBar.iss
```
Output: `dist\ZenBar-1.0.0-Setup.exe`

---

## ⚙️ Default Settings

| Setting | Default |
|---|---|
| Auto-Hide | Off |
| Auto-Hide Delay | 500 ms |
| Show Window Controls | On (Auto-Hide mode only) |
| Volume Scroll Step | 2% |
| Brightness Scroll Step | 5% |
| Bar Height | 32 px |
| Run on Startup | On |

All settings are saved to `config.ini` next to the executable.

## 🔮 Roadmap

ZenBar is actively developed. Here's what's coming next:

### v2.0 — WinUI 3 Redesign *(Next Major Release)*
- Complete UI overhaul using **WinUI 3** for a modern, native Windows 11 aesthetic
- **Notification Center widget** — live notification count and quick dismiss
- **Quick Controls widget** — toggle Wi-Fi, Bluetooth, Focus Assist, and more
- Fluent Design system, Mica/Acrylic materials
- Full settings redesign with a proper window

---

## 🤝 Contributing

Contributions are very welcome!

1. Fork the project
2. Create your feature branch: `git checkout -b feature/AmazingFeature`
3. Commit your changes: `git commit -m 'Add AmazingFeature'`
4. Push: `git push origin feature/AmazingFeature`
5. Open a Pull Request

---

## 📝 License

Distributed under the **MIT License** — see **[LICENSE](LICENSE)** for details.
