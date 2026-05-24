# ZenBar

A sleek, minimalistic, and highly customizable status bar for Windows. It sits at the top edge of your screen and provides beautiful, glanceable system statistics such as Network Speed, CPU Temperature, Battery Status, Media Controls, and Brightness/Volume indicators. 

When you use the "Auto-Hide" feature, it remains completely out of your way until you hover at the top of your screen. It even features built-in Min, Max, and Close buttons so that you can close the underlying windows effortlessly.

## ✨ Features

- **Network Speed**: Real-time upload and download speeds, directly from your active network adapters.
- **Hardware Monitoring**: Integrates with LibreHardwareMonitor to display accurate CPU temperatures.
- **Advanced Battery Status**: Not just percentage, but actual system states including "Charging" (CHG), "Battery Mode" (BAT), and Windows 11 "Battery Saver Mode" (BSM). The bottom edge of the bar acts as a thin visual progress bar for your battery life!
- **Media Controls**: Elegant now-playing text and transport controls that integrate natively with Windows Media APIs (WinRT). Control Spotify, Chrome, or any supported media player.
- **Quick Controls**: Glanceable volume and brightness indicators. You can even scroll your mouse wheel over them to adjust brightness and volume rapidly without opening any menus!
- **Sleek Customizability**: Includes a native Dark Mode Settings window to configure auto-hide delays, sizing, and run-on-startup behavior.

## 🛠️ Built With
- **C++20**
- **Win32 API** (GDI for fast, native drawing)
- **Windows Runtime (WinRT)** for media controls integration
- **WMI (Windows Management Instrumentation)** for system hardware metrics
- **Visual Studio 2022**

## 🚀 Getting Started

> ⚠️ **Note on Windows SmartScreen:** 
> Because ZenBar is a newly released, unsigned open-source utility, Windows SmartScreen will show a blue *"Windows protected your PC"* popup on your first download. 
> 
> The entire project is transparently open-source right here for you to audit! You can easily bypass the warning using either method below.

### 📥 Download & Install (For Regular Users)

If you don't want to compile the code from source, you can download the ready-to-use application archive:

1. Go to the **[Releases Page](../../releases)** on GitHub and download the latest `ZenBar.zip`.
2. **Bypass SmartScreen** using one of these quick options:
   - **The GUI Way:** Double-click the ZIP. If the warning pops up, click **"More info"** and then select **"Run anyway"**.
   - **The PowerShell Way:** Open PowerShell in your Downloads directory and instantly strip the web tracking flag by running:
```powershell
     Unblock-File -Path .\ZenBar.zip
```
3. Extract the ZIP file safely.
4. Double-click `ZenBar.exe` to run it! (You can also configure it to run on startup directly from its Settings menu).

### 🛠️ Build & Setup (For Developers)
1. Clone the repository to your local machine.
2. Double click on the `ZenBar.sln` to open the project in Visual Studio 2022.
3. At the top toolbar, ensure your build configuration is set to **Debug** or **Release** and your architecture is set to **x64**. *(Note: x86 is not supported)*
4. Go to `Build -> Build Solution` (or press `Ctrl+Shift+B`).
5. Run the executable `ZenBar.exe` from the `x64/Release` (or `x64/Debug`) folder!

## 🤝 Contributing

Contributions are what make the open source community such an amazing place to learn, inspire, and create. Any contributions you make are **greatly appreciated**.

1. Fork the Project
2. Create your Feature Branch (`git checkout -b feature/AmazingFeature`)
3. Commit your Changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the Branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## 📝 License

Distributed under the MIT License. See the **[LICENSE](LICENSE)** file for more information.
