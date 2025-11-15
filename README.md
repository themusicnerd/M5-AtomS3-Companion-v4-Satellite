# M5-AtomS3-Companion-v4-Satellite
A compact, single-button satellite surface built for Companion v4 using the M5 AtomS3. Features external RGB back LED, full-screen upscaled bitmap support, WiFi config portal, OTA updates, and automatic MAC-based device ID; perfect for enhancing events with on-stage triggers, info like countdown timers and tally.

## Features
- Uses the M5 AtomS3 (ESP32-S3 / 0.85″ 128×128 screen) :contentReference[oaicite:2]{index=2}  
- Full-screen bitmap rendering (Companion sends 72×72 which is up-scaled)  
- External RGB LED on G8/G5/G6 with G7 ground – mirrors key colour  
- WiFiManager config portal (hold the button for 5 s)  
- OTA firmware updates via ArduinoOTA  
- Auto-generated deviceID “AtomS3_XXXXXX” from full MAC, matching other surfaces  
- Companion API: KEY-STATE, BITMAP, COLOR, BRIGHTNESS, PING/keep-alive  
- Stable code-base ready for release on GitHub & M5Burner  

## Hardware Connections
| Pin        | Function           |
|------------|--------------------|
| G8         | External LED – Red |
| G5         | External LED – Green |
| G6         | External LED – Blue |
| G7         | External LED – Ground |

Ensure the LED is common-cathode with G7 as Ground.

## Installation & Usage
1. Clone this repository.  
2. Open `CompanionSatelliteSingleButton.ino` in Arduino IDE (ESP32 board selected).  
3. Install dependencies: M5Unified, WiFiManager, Preferences.  
4. Update `companion_host` and `companion_port` constants or use the config portal.  
5. Upload firmware to AtomS3.  
6. On first boot the device will show a “BOOTING…” screen, then wait for Companion.  
7. In Companion v4: go to **Surfaces → Add Device**, use the sent deviceID.  
8. Hold the button for 5 seconds to open the WiFi config portal (SSID will be the deviceID).  
9. Press the button to trigger your Companion key press event. LED will mirror colour.

## Firmware Update (OTA)
- On board: The code listens for ArduinoOTA service.  
- Use same hostname (deviceID) during firmware upload.

## Troubleshooting
- **Blank screen or crash**: Ensure BITMAPS=72 in `sendAddDevice()` — higher sizes may exceed RAM.  
- **DeviceID shows “000…”**: Ensure you haven’t stored a different deviceID in Preferences; the firmware uses full MAC.  
- **LED colour incorrect**: Check wiring and whether your LED is common-cathode.  
- **Not connecting to Companion**: Ensure `companion_host` & `companion_port` are set correctly and device and PC are on same network.

## Version
v1.0.0

## License
MIT License — see [LICENSE](LICENSE) file for details.
