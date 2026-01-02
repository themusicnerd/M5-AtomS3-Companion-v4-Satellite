M5-AtomS3-Companion-v4-Satellite

A compact, single-button satellite surface built for Companion v4 using the M5 AtomS3.
Includes external RGB rear LED, full-screen upscaled bitmap mode, a new ultra-fast text mode, WiFi config portal, mDNS autodiscovery, OTA updates, and auto MAC-based deviceID.
Perfect for enhancing productions with on-stage triggers, tally, timers, cues, and status text.

Features
- Two display modes:
  • Bitmap Mode — Renders Companion’s 72×72 bitmaps upscaled to 128×128
  • Text Mode (v1.3) — Fast, low-latency, no bitmap fetches
    - 1–2 chars → Extra-big font
    - 3 chars → Ultra-large font
    - 4–6 chars → Large centered font
    - >6 chars → Auto-wrap multi-line
    - Supports COLOR= and TEXTCOLOR=
- External RGB LED output on G8/G5/G6 (G7 = Ground), mirrors key colour
- Interactive boot menu — Hold button during boot to configure settings
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
3. Select M5AtomS3 via ESP32 board manager.
4. Install libraries: M5Unified, WiFiManager, Preferences.
5. Flash to the AtomS3.
6. On first boot, device will create a WiFi access point (SSID = m5atom-s3_XXXXX).
7. Connect to the AP and configure WiFi credentials, Companion IP/Port, and display mode.
8. Device will connect to WiFi and show "Ready" screen.
9. In Companion v4: Device is automatically discovered via mDNS, or manually add using deviceID.
10. Press button to send KEY-PRESS to Companion. LED mirrors key color.

Boot Menu
- Enter menu: Hold button during boot
- Navigate: Short click to move to next option
- Select: Hold for 1 second to select current option
- Options:
  - Boot: Normal — Continue normal boot
  - Boot: Web Config — Open config portal on current WiFi
  - Boot: WiFi AP — Create WiFi AP for reconfiguration (SSID = m5atom-s3_XXXXX)
  - Display: BITMAP/TEXT — Toggle display mode (saves immediately)
  - Rotation: 0°/90°/180°/270° — Adjust text rotation (TEXT mode only, saves immediately)

OTA Firmware Update
- OTA enabled by default.
- Hostname = m5atom-s3_XXXXX (matches deviceID)
- Password = companion-satellite
- Update from Arduino IDE using Network Ports.

Troubleshooting
Only one letter shows in text mode:
- Update to v1.3 which fixes base64 decoding issues.

LED colours wrong:
- Confirm LED is common-cathode (G7 = Ground).
- Re-check wiring if colors appear swapped.

Not connecting to Companion:
- Check IP and Port in WiFi config portal.
- Ensure device and Companion are on same network.
- Check firewall rules.

Blank screen or crashes:
- Ensure sendAddDevice() uses BITMAPS=72 (required by bitmap mode).

Version History
v1.4 (Community)
- mDNS service discovery for automatic device detection
- DeviceID format changed to m5atom-s3_XXXXX (last 5 MAC chars)
- Interactive boot menu for configuration without web portal
- Improved boot messages for better clarity
- Faster boot time (reduced delays by ~2 seconds)
- Removed runtime 5s long-press config portal (enables Companion delayed actions)

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
