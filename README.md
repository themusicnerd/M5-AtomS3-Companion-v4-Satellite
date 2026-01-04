M5-AtomS3-Companion-v4-Satellite

A compact, single-button satellite surface built for Companion v4 using the M5 AtomS3.
Includes external RGB rear LED, full-screen upscaled bitmap mode, a new ultra-fast text mode, WiFi config portal, mDNS autodiscovery, OTA updates, and auto MAC-based deviceID.
Perfect for enhancing productions with on-stage triggers, tally, timers, cues, and status text.

Features
- Two display modes:
  • Bitmap Mode — Renders Companion's 64x64 bitmaps upscaled to 128×128
  • Text Mode (v1.3) — Fast, lower-latency, no bitmap fetches
    - Auto-wraps text similar to companion
    - Supports COLOR= and TEXTCOLOR=
- External RGB LED output on G8/G5/G6 (G7 = Ground), mirrors key colour
- Interactive boot menu — Hold button during boot to configure settings
- QR code display for easy WiFi setup and web portal access
- mDNS service discovery (companion-satellite._tcp) for automatic device discovery
- OTA updates via ArduinoOTA
- Auto deviceID: m5atom-s3_XXXXX (last 5 MAC chars)
- Supports Companion v4 Satellite API: TEXT, BITMAP, COLOR, TEXTCOLOR, BRIGHTNESS, KEY-STATE, PING
- Clean, stable, production-ready codebase
- M5Burner-ready binaries available

Hardware Connections
G8 – LED Red
G5 – LED Green
G6 – LED Blue
G7 – LED Ground
Use a common-cathode RGB LED (G7 = Ground)

Recommended LED
AU: https://www.jaycar.com.au/tricolour-rgb-5mm-led-600-1000mcd-round-diffused/
USA: https://www.adafruit.com/product/302

Installation & Usage
1. Clone this repository.
2. Open M5-AtomS3-Companion-v4-Satellite.ino in Arduino IDE.
3. Arduino will automatically load all .ino tabs (Hardware, Display, Network, Config).
4. Select M5AtomS3 via ESP32 board manager.
5. Install libraries: M5Unified, WiFiManager, Preferences.
6. Flash to the AtomS3.
7. On first boot, device will create a WiFi access point (SSID = m5atom-s3_XXXXX).
8. Device displays QR code for easy WiFi connection (press button to toggle details).
9. Scan QR code or connect manually to the AP, then configure WiFi credentials, Companion IP/Port, and display mode at 192.168.4.1.
10. Device will connect to WiFi and show "Ready" screen.
11. In Companion v4: Device is automatically discovered via mDNS and can be added via the companion interface, or boot into Web Config mode to manually change companion settings.
12. Press button to send KEY-PRESS to Companion. LED mirrors key color.

Boot Menu
- Enter menu: Hold button during boot
- Navigate: Short click to move to next option
- Select: Hold for 1 second to select current option
- Options:
  - Boot: Normal — Continue normal boot
  - Boot: Web Config — Open config portal on current WiFi (displays QR code for portal URL, press button to toggle details)
  - Boot: Reset — Create WiFi AP for reconfiguration (displays WiFi QR code, press button to toggle details)
  - Display: BITMAP/TEXT — Toggle display mode (saves immediately)
  - Rotation: 0°/90°/180°/270° — Adjust text rotation (TEXT mode only, saves immediately)

OTA Firmware Update
- OTA enabled by default.
- Hostname = m5atom-s3_XXXXX (matches deviceID)
- Password = companion-satellite
- Update from Arduino IDE using Network Ports.

Troubleshooting
Only one letter shows in text mode:
- Update to v1.3+ which fixes base64 decoding issues.

LED colours wrong:
- Confirm LED is common-cathode (G7 = Ground).
- Re-check wiring if colors appear swapped.

Not connecting to Companion:
- Check IP and Port in WiFi config portal.
- Ensure device and Companion are on same network.
- Check firewall rules.

Version History
v1.4
- mDNS service discovery for automatic device detection
- DeviceID format changed to m5atom-s3_XXXXX (last 5 MAC chars)
- Interactive boot menu for better web portal control
- QR codes for quick AP and config portal connections (press button to toggle details)
- Improved boot messages for better clarity
- Faster boot time (reduced delays by ~2 seconds)
- Removed runtime 5s long-press config portal (enables Companion delayed actions)
- Overhauled bitmap rendering
  - uses 64x64 images for better scaling performance, visuals, and bandwidth
  - draws to screen in chunks instead of pixel by pixel, significantly improving render time
- Improved text-mode rendering to mimick companion text
- Added "Reconnecting..." overlay when connection is lost

v1.3
- Text-only mode with zero bitmap requests
- Faster UI, reduced latency
- Fixed multi-letter text decoding
- Improved color handling
- Performance improvements

v1.2
- General stability and UI tweaks
- Improved LED behaviour

v1.1
- Fixed PWM LED output
