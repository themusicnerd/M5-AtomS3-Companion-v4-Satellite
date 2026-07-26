M5-AtomS3-Companion-v4-Satellite

A compact, single-button satellite surface built for Companion v4 using the M5 AtomS3.
Separate Wi-Fi + external LED and Atomic PoE/W5500 firmware variants include full-screen bitmap mode, ultra-fast text mode, wired or Wi-Fi configuration, browser updates, and an automatic MAC-based device ID.
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
- Optional mDNS service discovery (companion-satellite._tcp) for automatic device discovery
- OTA updates via ArduinoOTA
- Auto deviceID: m5atom-s3_XXXXX (last 5 MAC chars)
- Supports the Companion Satellite API used by Companion v4: TEXT, BITMAP, COLOR, TEXTCOLOR, BRIGHTNESS, KEY-STATE, PING
- Clean, stable, production-ready codebase
- M5Burner-ready binaries available
- Atomic PoE Base build with W5500 DHCP, wired setup/REST page and browser firmware updates

Hardware Connections
G8 – LED Red
G5 – LED Green
G6 – LED Blue
G7 – LED Ground
Use a common-cathode RGB LED (G7 = Ground)

The external LED and Atomic PoE Base use the same GPIOs. Install the normal firmware for Wi-Fi + LED, or the `-poe` firmware for Ethernet + PoE; the PoE variant disables external LED access.

Recommended LED
AU: https://www.jaycar.com.au/tricolour-rgb-5mm-led-600-1000mcd-round-diffused/
USA: https://www.adafruit.com/product/302

Installation & Usage
Initial install with ESPHome Web (recommended)
1. Download the matching `M5-AtomS3-Companion-v4-Satellite-wifi-factory.bin` or `M5-AtomS3-Companion-v4-Satellite-poe-factory.bin` from the latest GitHub release. This is the complete first-install image.
2. Connect the AtomS3 with a USB **data** cable and open [ESPHome Web](https://web.esphome.io/).
3. Select **Connect**, choose the serial device, then choose **Install** and select the downloaded `.bin`.
4. Configure Wi-Fi and Companion after boot. Future updates use the built-in `http://<device-ip>:9999/update` page—no USB cable required.

Each network variant has two release files: `*-factory.bin` is for the first USB flash only; `*.ino.bin` is the smaller application image for browser updates. The application image will not boot when flashed as a first install.

The v1.3.10 release provides:

- `*-wifi-factory.bin` / `*-wifi.ino.bin` — Wi-FiManager, mDNS, ArduinoOTA and external RGB LED.
- `*-poe-factory.bin` / `*-poe.ino.bin` — Atomic PoE Base, W5500 DHCP and wired setup at `http://<dhcp-ip>:9999/`.

The PoE build uses G5=SCK, G7=MISO, G8=MOSI and G6=CS. Its DHCP address and setup URL appear on the AtomS3 display.

Arduino development environment
1. Clone this repository.
2. Open M5-AtomS3-Companion-v4-Satellite.ino in Arduino IDE.
3. Arduino will automatically load all .ino tabs (Hardware, Display, Network, Config).
4. Select M5AtomS3 via ESP32 board manager.
5. Install libraries: M5Unified, WiFiManager and M5-Ethernet. Preferences is supplied by the ESP32 board package.
6. Build normally for Wi-Fi + LED. Define `ATOMIC_POE_BUILD` for the Atomic PoE/W5500 variant.
7. Flash to the AtomS3.
8. On first boot, device will create a WiFi access point (SSID = m5atom-s3_XXXXX).
9. Device displays QR code for easy WiFi connection (press button to toggle details).
10. Scan QR code or connect manually to the AP, then configure WiFi credentials, Companion IP/Port, display mode, and mDNS discovery at 192.168.4.1.
11. Device will connect to WiFi and show "Ready" screen.
12. In Companion v4: Device is automatically discovered via mDNS when enabled, or can be configured manually with the Companion IP and port. Boot into Web Config mode to change either setting.
13. Press button to send KEY-PRESS to Companion. LED mirrors key color.

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
- **Web update:** browse to `http://<device-ip>:9999/update`, choose the matching `*-wifi.ino.bin` or `*-poe.ino.bin`, then wait for the automatic reboot. It is open by default; use the **Optional protection** form on that page to set or remove a password. Once set, sign in as `admin` with your chosen password. Do not remove power during the upload.
- Use only the matching application `.ino.bin` from a GitHub release; do not upload bootloader or partition files.
- OTA enabled by default.
- Hostname = m5atom-s3_XXXXX (matches deviceID)
- Password protection is optional for the browser updater; ArduinoOTA retains its existing password.
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
v1.3.10
- Separate Wi-Fi + LED and Atomic PoE/W5500 firmware variants
- Wired DHCP setup, REST settings and streamed browser firmware updates
- Persistent brightness, display mode and rotation settings
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

Unreleased
- Added a persisted mDNS enable/disable setting in the web configuration portal
- Fixed text mode using the tiny reconnect-overlay font after a connection recovers

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
