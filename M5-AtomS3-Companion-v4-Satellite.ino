/*
 * ============================================================================
 * M5 AtomS3 Companion v4 Satellite
 * ============================================================================
 *
 * Author: Adrian Davis
 * URL: https://github.com/themusicnerd/M5-AtomS3-Companion-v4-Satellite
 * License: MIT
 *
 * Single-button Companion satellite with external RGB LED
 * - Two display modes: BITMAP (72x72 upscaled) or TEXT (dynamic sizing)
 * - WiFiManager config portal with interactive boot menu
 * - REST API on port 9999, mDNS discovery
 * - OTA updates, auto-reconnect
 *
 * External LED pins: G8 (Red), G5 (Green), G6 (Blue), G7 (Ground)
 * ============================================================================
 */

#include <M5Unified.h>
#include <M5GFX.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <memory>
#include <mbedtls/base64.h>
#include <vector>
#include <math.h>
#include <esp32-hal-ledc.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// ============================================================================
// Display Mode Constants
// ============================================================================

#define DISPLAY_BITMAP 0
#define DISPLAY_TEXT   1

// ============================================================================
// Data Structures
// ============================================================================

// Update data structure
struct PendingUpdate {
  String bitmapBase64;
  String textContent;
  int colorR, colorG, colorB;
  int bgR, bgG, bgB;
  int fgR, fgG, fgB;
  int fontSize;  // 0 = auto, >0 = specific point size from Companion
  bool hasColor;
  bool hasBgColor;
  bool hasFgColor;
  bool hasFontSize;
  bool hasData;  // true if update is pending
};

// Simple 2-item queue for rapid button events
struct UpdateQueue {
  PendingUpdate items[2];
  int count;  // 0, 1, or 2

  UpdateQueue() : count(0) {}

  void push(const PendingUpdate& update) {
    if (count < 2) {
      items[count] = update;
      count++;
    } else {
      // Queue full: drop oldest, keep newest
      items[0] = items[1];
      items[1] = update;
    }
  }

  PendingUpdate pop() {
    PendingUpdate result = items[0];
    if (count > 1) {
      items[0] = items[1];
    }
    count--;
    return result;
  }

  bool isEmpty() const {
    return count == 0;
  }
};

// ============================================================================
// Function Prototypes
// ============================================================================

// Display.ino
bool parseColorToken(const String& line, const String& key, int &r, int &g, int &b);
void clearScreen(uint16_t color = BLACK);
void drawCenterText(const String& txt, uint16_t color = WHITE, uint16_t bg = BLACK);
void applyDisplayBrightness();
void drawBitmapRGB888FullScreen(uint8_t* rgb, int size);
void refreshTextDisplay();
void setText(const String& txt, int fontSizeOverride = 0);
void analyseLayout(int fontSizeOverride = 0);
void handleKeyStateTextField(const String& line);
void handleTextModeColors(const String& line);

// Hardware.ino
void setupLED();
void setExternalLedColor(uint8_t r, uint8_t g, uint8_t b);

// Network.ino
void sendAddDevice();
void parseAPI(const String& apiData);
void handleKeyState(const String& line);
void setupRestServer();
void connectToNetwork();
void initializeMDNS();

// Config.ino
void loadPreferences();
void saveParamCallback();
void runBootMenu();

// Update handling
void enqueueUpdate(const PendingUpdate& update);
void processPendingBitmap(const String& bitmapBase64);

// ============================================================================
// Global Objects & State
// ============================================================================

Preferences preferences;
WiFiManager wifiManager;
WiFiClient  client;
WebServer   restServer(9999);

// Companion server
char companion_host[40] = "";
char companion_port[6]  = "16622";

// Static IP (0.0.0.0 = DHCP)
IPAddress stationIP   = IPAddress(0, 0, 0, 0);
IPAddress stationGW   = IPAddress(0, 0, 0, 0);
IPAddress stationMask = IPAddress(255, 255, 255, 0);

String deviceID = "";
const char* AP_password = "";

bool forceConfigPortalOnBoot = false;
bool forceRouterModeOnBoot  = false;

// Timing
unsigned long lastPingTime    = 0;
unsigned long lastConnectTry  = 0;
const unsigned long connectRetryMs = 5000;
const unsigned long pingIntervalMs = 2000;

// Display & LED
int brightness = 100;

const int LED_PIN_RED   = G8;
const int LED_PIN_GREEN = G5;
const int LED_PIN_BLUE  = G6;
const int LED_PIN_GND   = G7;

const uint32_t pwmFreq       = 5000;
const uint8_t  pwmResolution = 8;

uint8_t lastColorR = 0;
uint8_t lastColorG = 0;
uint8_t lastColorB = 0;

int displayMode = DISPLAY_BITMAP;

WiFiManagerParameter* custom_companionIP = nullptr;
WiFiManagerParameter* custom_companionPort = nullptr;
WiFiManagerParameter* custom_displayMode = nullptr;
WiFiManagerParameter* custom_rotation = nullptr;

int screenRotation = 0;  // 0=0°, 1=90°, 2=180°, 3=270° (TEXT mode only)

// Text mode state
bool textPressedBorder = false;

String currentText  = "";
String line1        = "";
String line2        = "";
String line3        = "";
int    numLines     = 0;
const int MAX_AUTO_LINES = 7;

std::vector<String> manualLines;
bool useManualLines = false;

std::vector<String> autoWrappedLines;
bool useAutoWrappedLines = false;

uint16_t bgColor   = BLACK;
uint16_t txtColor  = WHITE;

// Update queue state
UpdateQueue updateQueue;
bool isRenderingNow = false;
unsigned long lastRenderDuration = 0;

void logger(const String& s, const String& type = "info") {
  Serial.println(s);
}

void enqueueUpdate(const PendingUpdate& update) {
  updateQueue.push(update);
}

void processPendingBitmap(const String& bitmapBase64) {
  // Move existing decode logic from Network.ino here
  int inLen = bitmapBase64.length();
  if (inLen <= 0) return;

  size_t out_max = (inLen * 3) / 4 + 4;
  std::unique_ptr<uint8_t[]> buf(new uint8_t[out_max]);
  size_t out_len = 0;

  int res = mbedtls_base64_decode(buf.get(), out_max, &out_len,
                                  (const unsigned char*)bitmapBase64.c_str(), inLen);
  if (res != 0) {
    Serial.println("[RENDER] Base64 decode failed");
    return;
  }

  int sizeRGB  = sqrt(out_len / 3);
  int sizeRGBA = sqrt(out_len / 4);
  bool isRGB   = (sizeRGB  * sizeRGB  * 3 == (int)out_len);
  bool isRGBA  = (sizeRGBA * sizeRGBA * 4 == (int)out_len);

  if (isRGB) {
    drawBitmapRGB888FullScreen(buf.get(), sizeRGB);
  } else if (isRGBA) {
    // Strip alpha channel
    int pixels = sizeRGBA * sizeRGBA;
    std::unique_ptr<uint8_t[]> rgb(new uint8_t[pixels * 3]);
    uint8_t* s = buf.get();
    uint8_t* d = rgb.get();
    for (int i = 0; i < pixels; i++) {
      d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
      s += 4; d += 3;
    }
    drawBitmapRGB888FullScreen(rgb.get(), sizeRGBA);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[M5AtomS3] Booting...");

  // Build deviceID from MAC
  WiFi.mode(WIFI_STA);
  delay(100);

  uint8_t mac[6];
  WiFi.macAddress(mac);

  char macBuf[13];
  sprintf(macBuf, "%02X%02X%02X%02X%02X%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  deviceID = "m5atom-s3_";
  deviceID += macBuf;
  deviceID.toUpperCase();

  Serial.println("[ID] deviceID = " + deviceID);

  loadPreferences();

  // Initialize M5
  auto cfg = M5.config();
  M5.begin(cfg);

  if (displayMode == DISPLAY_TEXT) {
    M5.Display.setRotation(screenRotation);
  } else {
    M5.Display.setRotation(0);
  }

  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
  applyDisplayBrightness();
  clearScreen(BLACK);

  // Boot menu if button held
  if (M5.BtnA.isPressed())
    runBootMenu();

  drawCenterText("Booting...\n\n\nHold button\non boot\nfor MENU", WHITE, BLACK);

  setupLED();

  WiFi.setHostname(deviceID.c_str());
  connectToNetwork();

  ArduinoOTA.setHostname(deviceID.c_str());
  ArduinoOTA.setPassword("companion-satellite");
  ArduinoOTA.begin();

  setupRestServer();
  initializeMDNS();

  clearScreen(BLACK);
  setExternalLedColor(0, 0, 0);

  String waitMsg =
    "Ready\n\n" +
    String(companion_host) + ":" + String(companion_port) +
    "\n\n" + (displayMode == DISPLAY_TEXT ? "TEXT" : "BITMAP") + " mode";

  drawCenterText(waitMsg, WHITE, BLACK);

  Serial.println("[System] Setup complete, entering main loop.");
}

void loop() {
  M5.update();
  ArduinoOTA.handle();
  restServer.handleClient();

  unsigned long now = millis();

  // Companion connection/reconnect
  if (!client.connected() && (now - lastConnectTry >= connectRetryMs)) {
    lastConnectTry = now;
    Serial.print("[NET] Connecting to Companion ");
    Serial.print(companion_host);
    Serial.print(":");
    Serial.println(companion_port);

    if (client.connect(companion_host, atoi(companion_port))) {
      Serial.println("[NET] Connected to Companion API");
      drawCenterText("Connected", GREEN, BLACK);
      sendAddDevice();
      lastPingTime = millis();
    } else {
      Serial.println("[NET] Companion connect failed");
    }
  }

  // Handle Companion traffic
  if (client.connected()) {
    while (client.available()) {
      String line = client.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) parseAPI(line);
    }

    // Process next update from queue
    if (!isRenderingNow && !updateQueue.isEmpty()) {
      isRenderingNow = true;
      unsigned long renderStart = millis();

      PendingUpdate update = updateQueue.pop();

      // Apply LED color
      if (update.hasColor) {
        setExternalLedColor(update.colorR, update.colorG, update.colorB);
      }

      // Render based on mode
      if (displayMode == DISPLAY_BITMAP) {
        if (update.bitmapBase64.length() > 0) {
          processPendingBitmap(update.bitmapBase64);
        }
      } else {
        // TEXT mode
        if (update.hasBgColor) {
          bgColor = M5.Display.color565(update.bgR, update.bgG, update.bgB);
        }
        if (update.hasFgColor) {
          txtColor = M5.Display.color565(update.fgR, update.fgG, update.fgB);
        }
        // Update text with font size override if provided
        int fontOverride = (update.hasFontSize && update.fontSize > 0) ? update.fontSize : 0;
        setText(update.textContent, fontOverride);
      }

      isRenderingNow = false;

      lastRenderDuration = millis() - renderStart;
      Serial.printf("[RENDER] Complete in %lu ms (queue size: %d)\n", lastRenderDuration, updateQueue.count);
    }

    // Button press: send KEY-PRESS message
    if (M5.BtnA.wasPressed()) {
      Serial.println("[BTN] Short press -> KEY=0 PRESSED=true");
      String companionDeviceID = "m5atom-s3:" + deviceID.substring(deviceID.length() - 5);
      client.println("KEY-PRESS DEVICEID=" + companionDeviceID + " KEY=0 PRESSED=true");

      if (displayMode == DISPLAY_TEXT) {
        textPressedBorder = true;
        refreshTextDisplay();
      }
    }

    if (M5.BtnA.wasReleased()) {
      Serial.println("[BTN] Release -> KEY=0 PRESSED=false");
      String companionDeviceID = "m5atom-s3:" + deviceID.substring(deviceID.length() - 5);
      client.println("KEY-PRESS DEVICEID=" + companionDeviceID + " KEY=0 PRESSED=false");

      if (displayMode == DISPLAY_TEXT) {
        textPressedBorder = false;
        refreshTextDisplay();
      }
    }

    // Periodic keepalive
    if (now - lastPingTime >= pingIntervalMs) {
      client.println("PING atoms3");
      lastPingTime = now;
    }
  }

  delay(10);
}
