/*
 * Network Module - WiFi, Companion API, REST Server, mDNS
 *
 * Handles all network communications:
 * - WiFi connection via WiFiManager
 * - Companion v4 Satellite API protocol
 * - REST API server on port 9999
 * - mDNS service discovery
 */

// ============================================================================
// Companion v4 Satellite API
// ============================================================================

void sendAddDevice() {
  String cmd;
  String companionDeviceID = "m5atom-s3:" + deviceID.substring(deviceID.length() - 5);  // Keep colon format

  if (displayMode == DISPLAY_TEXT) {
    cmd = "ADD-DEVICE DEVICEID=" + companionDeviceID +
          " PRODUCT_NAME=\"M5 AtomS3 (TEXT)\" "
          "KEYS_TOTAL=1 KEYS_PER_ROW=1 "
          "COLORS=rgb TEXT=true TEXT_STYLE=true BITMAPS=0";
  } else {
    cmd = "ADD-DEVICE DEVICEID=" + companionDeviceID +
          " PRODUCT_NAME=\"M5 AtomS3 (BITMAP)\" "
          "KEYS_TOTAL=1 KEYS_PER_ROW=1 "
          "COLORS=rgb TEXT=false BITMAPS=64";
  }

  client.println(cmd);
  Serial.println("[API] Sent: " + cmd);
}

void handleKeyState(const String& line) {
  // Track content updates for ping interval backoff
  lastContentUpdateTime = millis();

  PendingUpdate update;
  update.hasColor = false;
  update.hasBgColor = false;
  update.hasFgColor = false;
  update.hasFontSize = false;
  update.fontSize = 0;
  update.bitmapBase64 = "";
  update.textContent = "";

  // Parse COLOR for LED (both modes)
  int r, g, b;
  if (parseColorToken(line, "COLOR", r, g, b)) {
    update.colorR = r;
    update.colorG = g;
    update.colorB = b;
    update.hasColor = true;
  }

  // Display handling by mode
  if (displayMode == DISPLAY_BITMAP) {
    // Extract BITMAP= base64 string
    int bmpPos = line.indexOf("BITMAP=");
    if (bmpPos >= 0) {
      int start = bmpPos + 7;
      int end = line.indexOf(' ', start);
      if (end < 0) end = line.length();
      String bmp = line.substring(start, end);
      bmp.trim();

      if (bmp.startsWith("\"") && bmp.endsWith("\""))
        bmp = bmp.substring(1, bmp.length() - 1);

      update.bitmapBase64 = bmp;
    }
  } else {
    // TEXT mode - parse colors, font size, and text
    int r, g, b;
    if (parseColorToken(line, "COLOR", r, g, b)) {
      update.bgR = r;
      update.bgG = g;
      update.bgB = b;
      update.hasBgColor = true;
    }

    if (parseColorToken(line, "TEXTCOLOR", r, g, b)) {
      update.fgR = r;
      update.fgG = g;
      update.fgB = b;
      update.hasFgColor = true;
    }

    // Parse FONT_SIZE if present
    int fontPos = line.indexOf("FONT_SIZE=");
    if (fontPos >= 0) {
      int start = fontPos + 10;
      int end = line.indexOf(' ', start);
      if (end < 0) end = line.length();
      String sizeStr = line.substring(start, end);
      sizeStr.trim();

      // Strip quotes if present
      if (sizeStr.startsWith("\"") && sizeStr.endsWith("\""))
        sizeStr = sizeStr.substring(1, sizeStr.length() - 1);

      // Convert to int (0 for "auto" or invalid, otherwise numeric value)
      update.fontSize = sizeStr.toInt();
      update.hasFontSize = true;
    }

    // Extract TEXT= field
    int tPos = line.indexOf("TEXT=");
    if (tPos >= 0) {
      int firstQuote = line.indexOf('"', tPos);
      if (firstQuote >= 0) {
        int secondQuote = line.indexOf('"', firstQuote + 1);
        if (secondQuote >= 0) {
          String textField = line.substring(firstQuote + 1, secondQuote);
          String decoded = decodeCompanionText(textField);
          decoded.replace("\\n", "\n");
          update.textContent = decoded;
        }
      }
    }
  }

  enqueueUpdate(update);
}

void parseAPI(const String& apiData) {
  // ANY message from Companion means connection is alive
  lastMessageTime = millis();
  hasConnectedOnce = true;

  if (unansweredPingCount > 0) {
    unansweredPingCount = 0;
  }

  if (apiData.length() == 0) return;
  if (apiData.startsWith("PONG")) return;

  Serial.println("[API] RX");

  if (apiData.startsWith("PING")) {
    String payload = apiData.substring(apiData.indexOf(' ') + 1);
    client.println("PONG " + payload);
    return;
  }

  if (apiData.startsWith("BRIGHTNESS")) {
    int valPos = apiData.indexOf("VALUE=");
    if (valPos >= 0) {
      String v = apiData.substring(valPos + 6);
      v.trim();
      brightness = v.toInt();
      Serial.println("[API] BRIGHTNESS set to " + String(brightness));
      setExternalLedColor(lastColorR, lastColorG, lastColorB);
      applyDisplayBrightness();
    }
    return;
  }

  if (apiData.startsWith("KEYS-CLEAR")) {
    Serial.println("[API] KEYS-CLEAR");
    updateQueue.count = 0;  // Clear queue

    setExternalLedColor(0,0,0);
    if (displayMode == DISPLAY_TEXT) {
      setText("");
    } else {
      clearScreen(BLACK);
    }
    return;
  }

  if (apiData.startsWith("KEY-STATE")) {
    handleKeyState(apiData);
    return;
  }
}

// ============================================================================
// REST API Server
// ============================================================================

void handleGetHost() {
  restServer.send(200, "text/plain", companion_host.data());
  Serial.println("[REST] GET /api/host: " + String(companion_host.data()));
}

void handleGetPort() {
  restServer.send(200, "text/plain", companion_port.data());
  Serial.println("[REST] GET /api/port: " + String(companion_port.data()));
}

void handleGetConfig() {
  String json = "{\"host\":\"" + String(companion_host.data()) + "\",\"port\":" + String(companion_port.data()) + "}";
  restServer.send(200, "application/json", json);
  Serial.println("[REST] GET /api/config: " + json);
}

void handlePostHost() {
  String newHost = "";

  if (restServer.hasArg("plain")) {
    String body = restServer.arg("plain");
    body.trim();

    if (body.startsWith("{") && body.endsWith("}")) {
      int hostPos = body.indexOf("\"host\":");
      if (hostPos >= 0) {
        int startQuote = body.indexOf("\"", hostPos + 7);
        int endQuote = body.indexOf("\"", startQuote + 1);
        if (startQuote >= 0 && endQuote > startQuote) {
          newHost = body.substring(startQuote + 1, endQuote);
        }
      }
    } else {
      newHost = body;
    }
  }

  newHost.trim();

  if (newHost.length() > 0 && newHost.length() < companion_host.size()) {
    strncpy(companion_host.data(), newHost.c_str(), companion_host.size());
    companion_host[companion_host.size() - 1] = '\0';

    preferences.begin("companion", false);
    preferences.putString("companionip", String(companion_host.data()));
    preferences.end();

    restServer.send(200, "text/plain", "OK");
    Serial.println("[REST] POST /api/host: Updated to " + String(companion_host.data()));

    if (client.connected()) {
      client.stop();
    }
  } else {
    restServer.send(400, "text/plain", "Invalid host");
    Serial.println("[REST] POST /api/host: Invalid host - " + newHost);
  }
}

void handlePostPort() {
  String newPort = "";

  if (restServer.hasArg("plain")) {
    String body = restServer.arg("plain");
    body.trim();

    int startQuote = body.indexOf("\"");
    int endQuote = body.indexOf("\"", startQuote + 1);
    if (startQuote >= 0 && endQuote > startQuote) {
      newPort = body.substring(startQuote + 1, endQuote);
    } else {
      newPort = body;
    }
  }

  newPort.trim();

  int portNum = newPort.toInt();
  if (portNum > 0 && portNum <= 65535) {
    strncpy(companion_port.data(), newPort.c_str(), companion_port.size());
    companion_port[companion_port.size() - 1] = '\0';

    preferences.begin("companion", false);
    preferences.putString("companionport", String(companion_port.data()));
    preferences.end();

    restServer.send(200, "text/plain", "OK");
    Serial.println("[REST] POST /api/port: Updated to " + String(companion_port.data()));

    if (client.connected()) {
      client.stop();
    }
  } else {
    restServer.send(400, "text/plain", "Invalid port number");
    Serial.println("[REST] POST /api/port: Invalid port - " + newPort);
  }
}

void handlePostConfig() {
  String newHost = "";
  String newPort = "";

  if (restServer.hasArg("plain")) {
    String body = restServer.arg("plain");
    body.trim();

    if (body.startsWith("{") && body.endsWith("}")) {
      // Parse host
      int hostPos = body.indexOf("\"host\":");
      if (hostPos >= 0) {
        int startQuote = body.indexOf("\"", hostPos + 7);
        int endQuote = body.indexOf("\"", startQuote + 1);
        if (startQuote >= 0 && endQuote > startQuote) {
          newHost = body.substring(startQuote + 1, endQuote);
        }
      }

      // Parse port (try quoted, then unquoted)
      int portPos = body.indexOf("\"port\":");
      if (portPos >= 0) {
        int startQuote = body.indexOf("\"", portPos + 7);
        int endQuote = body.indexOf("\"", startQuote + 1);
        if (startQuote >= 0 && endQuote > startQuote) {
          newPort = body.substring(startQuote + 1, endQuote);
        } else {
          int startNum = portPos + 7;
          while (startNum < body.length() && (body.charAt(startNum) == ' ' || body.charAt(startNum) == ':')) {
            startNum++;
          }
          int endNumComma = body.indexOf(",", startNum);
          int endNumBrace = body.indexOf("}", startNum);
          int endNum = -1;

          if (endNumComma >= 0 && endNumBrace >= 0) {
            endNum = (endNumComma < endNumBrace) ? endNumComma : endNumBrace;
          } else if (endNumComma >= 0) {
            endNum = endNumComma;
          } else if (endNumBrace >= 0) {
            endNum = endNumBrace;
          }

          if (endNum >= 0) {
            newPort = body.substring(startNum, endNum);
            newPort.trim();
          }
        }
      }
    } else {
      int commaPos = body.indexOf(',');
      if (commaPos >= 0) {
        newHost = body.substring(0, commaPos);
        newPort = body.substring(commaPos + 1);
        newHost.trim();
        newPort.trim();
      }
    }
  }

  newHost.trim();
  newPort.trim();

  bool hostValid = (newHost.length() > 0 && newHost.length() < companion_host.size());
  int portNum = newPort.toInt();
  bool portValid = (portNum > 0 && portNum <= 65535);

  if (hostValid && portValid) {
    strncpy(companion_host.data(), newHost.c_str(), companion_host.size());
    companion_host[companion_host.size() - 1] = '\0';
    strncpy(companion_port.data(), newPort.c_str(), companion_port.size());
    companion_port[companion_port.size() - 1] = '\0';

    preferences.begin("companion", false);
    preferences.putString("companionip", String(companion_host.data()));
    preferences.putString("companionport", String(companion_port.data()));
    preferences.end();

    restServer.send(200, "text/plain", "OK");
    Serial.println("[REST] POST /api/config: Updated host=" + String(companion_host.data()) + " port=" + String(companion_port.data()));

    if (client.connected()) {
      client.stop();
    }
  } else {
    restServer.send(400, "text/plain", "Invalid config");
    Serial.println("[REST] POST /api/config: Invalid config");
  }
}

void setupRestServer() {
  restServer.on("/api/host", HTTP_GET, handleGetHost);
  restServer.on("/api/port", HTTP_GET, handleGetPort);
  restServer.on("/api/config", HTTP_GET, handleGetConfig);

  restServer.on("/api/host", HTTP_POST, handlePostHost);
  restServer.on("/api/port", HTTP_POST, handlePostPort);
  restServer.on("/api/config", HTTP_POST, handlePostConfig);

  restServer.begin();
  Serial.println("[REST] REST API server started on port 9999");
  Serial.println("[REST] Available endpoints:");
  Serial.println("[REST]   GET  /api/host");
  Serial.println("[REST]   GET  /api/port");
  Serial.println("[REST]   GET  /api/config");
  Serial.println("[REST]   POST /api/host");
  Serial.println("[REST]   POST /api/port");
  Serial.println("[REST]   POST /api/config");
}

// ============================================================================
// WiFi Connection Management
// ============================================================================

// Run non-blocking AP config portal with QR code display
void runAPConfigPortal(const String& wifiHostname) {
  Serial.println("[WiFi] Starting config portal (AP mode)");

  String qrWifiString = "WIFI:T:nopass;S:" + wifiHostname + ";P:;;";

  wifiManager.setConfigPortalBlocking(false);
  wifiManager.startConfigPortal(wifiHostname.c_str(), "");

  // Run portal until connected
  bool showQR = true;
  bool lastShowQR = false;
  while (WiFi.status() != WL_CONNECTED) {
    M5.update();
    wifiManager.process();

    if (M5.BtnA.wasPressed()) {
      showQR = !showQR;
    }

    if (showQR != lastShowQR) {
      if (showQR) {
        M5.Display.fillScreen(BLACK);
        M5.Display.qrcode(qrWifiString.c_str(), 0, 0, M5.Display.width(), 6);
      } else {
        String msg = "WiFi CONFIG\n\nSSID:\n" + wifiHostname + "\n\n192.168.4.1";
        drawCenterText(msg, WHITE, BLACK);
      }
      lastShowQR = showQR;
    }

    delay(10);
  }
}

void connectToNetwork() {
  if (stationIP != IPAddress(0,0,0,0))
    wifiManager.setSTAStaticIPConfig(stationIP, stationGW, stationMask);

  WiFi.mode(WIFI_STA);
  String wifiHostname = getShortDeviceID();

  char displayModeHTML[512];
  char rotationHTML[768];
  buildDisplayModeHTML(displayModeHTML, sizeof(displayModeHTML), displayMode);
  buildRotationHTML(rotationHTML, sizeof(rotationHTML), screenRotation);

  custom_companionIP   = new WiFiManagerParameter("companionIP", "Companion IP", companion_host.data(), companion_host.size());
  custom_companionPort = new WiFiManagerParameter("companionPort", "Satellite Port", companion_port.data(), companion_port.size());
  custom_displayMode   = new WiFiManagerParameter(displayModeHTML);
  custom_rotation      = new WiFiManagerParameter(rotationHTML);

  wifiManager.addParameter(custom_companionIP);
  wifiManager.addParameter(custom_companionPort);
  wifiManager.addParameter(custom_displayMode);
  wifiManager.addParameter(custom_rotation);
  wifiManager.setSaveParamsCallback(saveParamCallback);

  std::vector<const char*> menu = { "wifi", "param", "info", "sep", "restart", "exit" };
  wifiManager.setMenu(menu);
  wifiManager.setClass("invert");
  wifiManager.setConfigPortalTimeout(180);

  wifiManager.setHostname(wifiHostname.c_str());
  Serial.printf("[WiFi] WiFiManager hostname set to: %s\n", wifiHostname.c_str());

  bool needsAPMode = false;
  wifiManager.setAPCallback([&needsAPMode](WiFiManager* wm) {
    Serial.println("[WiFi] Config portal started");
    needsAPMode = true;
  });

  // Boot menu forced AP mode
  if (forceRouterModeOnBoot) {
    runAPConfigPortal(wifiHostname);
    ESP.restart();
  }

  drawCenterText("Connecting...", WHITE, BLACK);

  wifiManager.setConfigPortalBlocking(false);
  bool res = wifiManager.autoConnect(wifiHostname.c_str(), "");
  Serial.printf("[WiFi] AutoConnect - SSID: %s\n", wifiHostname.c_str());

  // If autoConnect triggered AP mode (no saved WiFi), show QR code
  if (needsAPMode) {
    runAPConfigPortal(wifiHostname);
  }

  if (!res || WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Failed to connect");
    drawCenterText("WiFi\nConnection\nFailed", RED, BLACK);
  } else {
    Serial.println("[WiFi] Connected to AP, IP=" + WiFi.localIP().toString());

    // Verify hostname
    String currentHostname = WiFi.getHostname();
    if (currentHostname != wifiHostname) {
      Serial.printf("[WiFi] Hostname mismatch, resetting from '%s' to '%s'\n", currentHostname.c_str(), wifiHostname.c_str());
      WiFi.setHostname(wifiHostname.c_str());
    }

    // Web config portal (stay on current WiFi)
    bool showQR = true;
    bool lastShowQR = false;
    if (forceConfigPortalOnBoot) {
      String ipAddress = WiFi.localIP().toString();
      String portalURL = "http://" + ipAddress;

      wifiManager.setConfigPortalBlocking(false);
      wifiManager.startWebPortal();

      while (true) {
        M5.update();
        wifiManager.process();

        if (M5.BtnA.wasPressed()) {
          showQR = !showQR;
        }

        if (showQR != lastShowQR) {
          if (showQR) {
            M5.Display.fillScreen(BLACK);
            M5.Display.qrcode(portalURL.c_str(), 0, 0, M5.Display.width(), 6);
          } else {
            drawCenterText(portalURL, WHITE, BLACK);
          }
          lastShowQR = showQR;
        }

        ArduinoOTA.handle();
        wifiManager.process();
        delay(10);
      }
    }
  }

  // Apply rotation
  if (displayMode == DISPLAY_TEXT) {
    M5.Display.setRotation(screenRotation);
  } else {
    M5.Display.setRotation(0);
  }
}

// ============================================================================
// mDNS Service Discovery
// ============================================================================

void initializeMDNS() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[mDNS] Starting mDNS service...");

    String mDNSHostname = getShortDeviceID();
    String macShort = deviceID.substring(deviceID.length() - 5);
    String mDNSInstanceName = "m5atom-s3:" + macShort;  // Keep colon format

    if (!MDNS.begin(mDNSHostname.c_str())) {
      Serial.println("[mDNS] ERROR: mDNS failed to start!");
    } else {
      Serial.printf("[mDNS] mDNS started with hostname: %s\n", mDNSHostname.c_str());
      MDNS.setInstanceName(mDNSInstanceName);

      if (MDNS.addService("companion-satellite", "tcp", 9999)) {
        Serial.println("[mDNS] companion-satellite service registered on port 9999");

        MDNS.addServiceTxt("companion-satellite", "tcp", "restEnabled", "true");
        MDNS.addServiceTxt("companion-satellite", "tcp", "deviceId", macShort);
        MDNS.addServiceTxt("companion-satellite", "tcp", "prefix", "m5atom-s3");
        MDNS.addServiceTxt("companion-satellite", "tcp", "productName", "M5 AtomS3");
        MDNS.addServiceTxt("companion-satellite", "tcp", "apiVersion", "4");

        Serial.println("[mDNS] Service text records added");
        Serial.printf("[mDNS] Instance name: %s\n", mDNSInstanceName.c_str());
        Serial.println("[mDNS] Test with: dns-sd -B _companion-satellite._tcp");
        Serial.println("[mDNS] SUCCESS: Full companion-satellite service name working!");
      } else {
        Serial.println("[mDNS] ERROR: companion-satellite service registration failed!");
      }
    }
  }
}
