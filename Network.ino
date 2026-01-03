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
  String companionDeviceID = "m5atom-s3:" + deviceID.substring(deviceID.length() - 5);

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
  PendingUpdate update;
  update.hasColor = false;
  update.hasBgColor = false;
  update.hasFgColor = false;
  update.hasFontSize = false;
  update.fontSize = 0;
  update.bitmapBase64 = "";
  update.textContent = "";

  // Parse COLOR for LED (both modes)
  int colorPos = line.indexOf("COLOR=");
  if (colorPos >= 0) {
    int start = colorPos + 6;
    int end = line.indexOf(' ', start);
    if (end < 0) end = line.length();
    String c = line.substring(start, end);
    c.trim();

    if (c.startsWith("\"") && c.endsWith("\""))
      c = c.substring(1, c.length() - 1);

    if (c.startsWith("rgba(") || c.startsWith("rgb(")) {
      String s = c;
      s.replace("rgba(", "");
      s.replace("rgb(", "");
      s.replace(")", "");
      s.replace(" ", "");
      int p1 = s.indexOf(',');
      int p2 = s.indexOf(',', p1+1);
      int p3 = s.indexOf(',', p2+1);
      int r = s.substring(0, p1).toInt();
      int g = s.substring(p1+1, p2).toInt();
      int b;
      if (p3 >= 0) {
        b = s.substring(p2+1, p3).toInt();
      } else {
        b = s.substring(p2+1).toInt();
      }

      update.colorR = r;
      update.colorG = g;
      update.colorB = b;
      update.hasColor = true;
    }
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
  if (apiData.length() == 0) return;
  if (apiData.startsWith("PONG"))   return;

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
  Serial.println("[REST] GET /api/host request received");
  Serial.println("[REST] Current companion_host: '" + String(companion_host) + "'");
  restServer.send(200, "text/plain", companion_host);
  Serial.println("[REST] GET /api/host: " + String(companion_host));
}

void handleGetPort() {
  Serial.println("[REST] GET /api/port request received");
  Serial.println("[REST] Current companion_port: '" + String(companion_port) + "'");
  restServer.send(200, "text/plain", companion_port);
  Serial.println("[REST] GET /api/port: " + String(companion_port));
}

void handleGetConfig() {
  String json = "{\"host\":\"" + String(companion_host) + "\",\"port\":" + String(companion_port) + "}";
  Serial.println("[REST] GET /api/config request received");
  Serial.println("[REST] Response JSON: " + json);
  restServer.send(200, "application/json", json);
  Serial.println("[REST] GET /api/config: " + json);
}

void handlePostHost() {
  String newHost = "";

  Serial.println("[REST] POST /api/host request received");

  if (restServer.hasArg("plain")) {
    String body = restServer.arg("plain");
    body.trim();
    Serial.println("[REST] Request body: '" + body + "'");

    // Check for JSON format
    if (body.startsWith("{") && body.endsWith("}")) {
      Serial.println("[REST] Parsing JSON format");

      int hostPos = body.indexOf("\"host\":");
      if (hostPos >= 0) {
        int startQuote = body.indexOf("\"", hostPos + 7);
        int endQuote = body.indexOf("\"", startQuote + 1);
        if (startQuote >= 0 && endQuote > startQuote) {
          newHost = body.substring(startQuote + 1, endQuote);
          Serial.println("[REST] Extracted host from JSON: '" + newHost + "'");
        }
      }
    } else {
      newHost = body;
      Serial.println("[REST] Using plain text format: '" + newHost + "'");
    }
  }

  newHost.trim();

  if (newHost.length() > 0 && newHost.length() < sizeof(companion_host)) {
    strncpy(companion_host, newHost.c_str(), sizeof(companion_host));
    companion_host[sizeof(companion_host) - 1] = '\0';

    preferences.begin("companion", false);
    preferences.putString("companionip", String(companion_host));
    preferences.end();

    restServer.send(200, "text/plain", "OK");
    Serial.println("[REST] POST /api/host: Updated to " + String(companion_host));

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

  Serial.println("[REST] POST /api/port request received");

  if (restServer.hasArg("plain")) {
    String body = restServer.arg("plain");
    body.trim();
    Serial.println("[REST] Request body: '" + body + "'");

    // Try quoted port first
    int startQuote = body.indexOf("\"");
    int endQuote = body.indexOf("\"", startQuote + 1);
    if (startQuote >= 0 && endQuote > startQuote) {
      newPort = body.substring(startQuote + 1, endQuote);
      Serial.println("[REST] Extracted quoted port from JSON: '" + newPort + "'");
    } else {
      newPort = body;
      Serial.println("[REST] Using plain text format: '" + newPort + "'");
    }
  }

  newPort.trim();

  int portNum = newPort.toInt();
  if (portNum > 0 && portNum <= 65535) {
    strncpy(companion_port, newPort.c_str(), sizeof(companion_port));
    companion_port[sizeof(companion_port) - 1] = '\0';

    preferences.begin("companion", false);
    preferences.putString("companionport", String(companion_port));
    preferences.end();

    restServer.send(200, "text/plain", "OK");
    Serial.println("[REST] POST /api/port: Updated to " + String(companion_port));

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

  Serial.println("[REST] POST /api/config request received");

  if (restServer.hasArg("plain")) {
    String body = restServer.arg("plain");
    body.trim();
    Serial.println("[REST] Request body: '" + body + "'");

    if (body.startsWith("{") && body.endsWith("}")) {
      Serial.println("[REST] Parsing JSON format");

      // Parse host
      int hostPos = body.indexOf("\"host\":");
      if (hostPos >= 0) {
        int startQuote = body.indexOf("\"", hostPos + 7);
        int endQuote = body.indexOf("\"", startQuote + 1);
        if (startQuote >= 0 && endQuote > startQuote) {
          newHost = body.substring(startQuote + 1, endQuote);
          Serial.println("[REST] Extracted host from JSON: '" + newHost + "'");
        }
      }

      // Parse port
      int portPos = body.indexOf("\"port\":");
      if (portPos >= 0) {
        // Try quoted port
        int startQuote = body.indexOf("\"", portPos + 7);
        int endQuote = body.indexOf("\"", startQuote + 1);
        if (startQuote >= 0 && endQuote > startQuote) {
          newPort = body.substring(startQuote + 1, endQuote);
          Serial.println("[REST] Extracted quoted port from JSON: '" + newPort + "'");
        } else {
          // Try unquoted port number
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
            Serial.println("[REST] Extracted unquoted port from JSON: '" + newPort + "'");
          }
        }
      }
    } else {
      // Plain text: split by comma
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

  bool hostValid = (newHost.length() > 0 && newHost.length() < sizeof(companion_host));
  int portNum = newPort.toInt();
  bool portValid = (portNum > 0 && portNum <= 65535);

  if (hostValid && portValid) {
    strncpy(companion_host, newHost.c_str(), sizeof(companion_host));
    companion_host[sizeof(companion_host) - 1] = '\0';
    strncpy(companion_port, newPort.c_str(), sizeof(companion_port));
    companion_port[sizeof(companion_port) - 1] = '\0';

    preferences.begin("companion", false);
    preferences.putString("companionip", String(companion_host));
    preferences.putString("companionport", String(companion_port));
    preferences.end();

    restServer.send(200, "text/plain", "OK");
    Serial.println("[REST] POST /api/config: Updated host=" + String(companion_host) + " port=" + String(companion_port));

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

void connectToNetwork() {
  if (stationIP != IPAddress(0,0,0,0))
    wifiManager.setSTAStaticIPConfig(stationIP, stationGW, stationMask);

  WiFi.mode(WIFI_STA);

  // AP + config portal mode
  if (forceRouterModeOnBoot) {
    Serial.println("[WiFi] Boot-hold: starting CONFIG portal (AP)");
    String msg =
      "WiFi CONFIG\n\n"
      "SSID:\n" + deviceID +
      "\n\n192.168.4.1";
    drawCenterText(msg, WHITE, BLACK);

    while (wifiManager.startConfigPortal(deviceID.c_str(), "")) {}
    ESP.restart();
  }

  // Build default displayMode string
  char modeBuf[8];
  if (displayMode == DISPLAY_TEXT) {
    snprintf(modeBuf, sizeof(modeBuf), "text");
  } else {
    snprintf(modeBuf, sizeof(modeBuf), "bitmap");
  }

  // Build rotation string
  char rotBuf[5];
  int rotDeg = 0;
  if      (screenRotation == 1) rotDeg = 90;
  else if (screenRotation == 2) rotDeg = 180;
  else if (screenRotation == 3) rotDeg = 270;
  else                          rotDeg = 0;
  snprintf(rotBuf, sizeof(rotBuf), "%d", rotDeg);

  custom_companionIP   = new WiFiManagerParameter("companionIP", "Companion IP", companion_host, 40);
  custom_companionPort = new WiFiManagerParameter("companionPort", "Satellite Port", companion_port, 6);
  custom_displayMode   = new WiFiManagerParameter("displayMode", "Display Mode (bitmap/text)", modeBuf, 8);
  custom_rotation      = new WiFiManagerParameter("rotation", "Text Rotation (0/90/180/270)", rotBuf, 4);

  wifiManager.addParameter(custom_companionIP);
  wifiManager.addParameter(custom_companionPort);
  wifiManager.addParameter(custom_displayMode);
  wifiManager.addParameter(custom_rotation);
  wifiManager.setSaveParamsCallback(saveParamCallback);

  std::vector<const char*> menu = { "wifi", "param", "info", "sep", "restart", "exit" };
  wifiManager.setMenu(menu);
  wifiManager.setClass("invert");
  wifiManager.setConfigPortalTimeout(180);

  String wifiHostname = "m5atom-s3_" + deviceID.substring(deviceID.length() - 5);
  wifiManager.setHostname(wifiHostname.c_str());
  Serial.printf("[WiFi] WiFiManager hostname set to: %s\n", wifiHostname.c_str());

  wifiManager.setAPCallback([](WiFiManager* wm) {
    Serial.println("[WiFi] Config portal started");
  });

  drawCenterText("Connecting...", WHITE, BLACK);

  String shortDeviceID = "m5atom-s3_" + deviceID.substring(deviceID.length() - 5);
  bool res = wifiManager.autoConnect(shortDeviceID.c_str(), "");
  Serial.printf("[WiFi] AutoConnect - SSID: %s\n", shortDeviceID.c_str());

  if (!res) {
    Serial.println("[WiFi] Failed to connect, showing WiFi ERR");
    drawCenterText("WiFi\nConnection\nFailed", RED, BLACK);
  } else {
    Serial.println("[WiFi] Connected to AP, IP=" + WiFi.localIP().toString());

    // Verify hostname
    String currentHostname = WiFi.getHostname();
    String expectedHostname = "m5atom-s3_" + deviceID.substring(deviceID.length() - 5);
    if (currentHostname != expectedHostname) {
      Serial.printf("[WiFi] Hostname mismatch, resetting from '%s' to '%s'\n", currentHostname.c_str(), expectedHostname.c_str());
      WiFi.setHostname(expectedHostname.c_str());
    }

    // Web config portal (stay on current WiFi)
    if (forceConfigPortalOnBoot) {
      String msg =
        "CONFIG PORTAL\n\n" +
        WiFi.localIP().toString();
      drawCenterText(msg, WHITE, BLACK);

      wifiManager.startWebPortal();

      while (true) {
        ArduinoOTA.handle();
        wifiManager.process();
        delay(10);
      }
    }
  }

  // Update from preferences
  preferences.begin("companion", true);
  if (preferences.getString("companionip").length() > 0)
    preferences.getString("companionip").toCharArray(companion_host, sizeof(companion_host));

  if (preferences.getString("companionport").length() > 0)
    preferences.getString("companionport").toCharArray(companion_port, sizeof(companion_port));

  String modeStr = preferences.getString("displayMode", custom_displayMode->getValue());
  String rotStr  = preferences.getString("rotation",   custom_rotation->getValue());
  preferences.end();

  if (modeStr.equalsIgnoreCase("text")) {
    displayMode = DISPLAY_TEXT;
  } else {
    displayMode = DISPLAY_BITMAP;
  }

  // Map rotation degrees -> index
  if (rotStr.toInt() == 90) screenRotation = 1;
  else if (rotStr.toInt() == 180) screenRotation = 2;
  else if (rotStr.toInt() == 270) screenRotation = 3;
  else screenRotation = 0;

  // Apply rotation (TEXT mode only)
  if (displayMode == DISPLAY_TEXT) {
    M5.Display.setRotation(screenRotation);
  } else {
    M5.Display.setRotation(0);
  }

  Serial.print("[Config] Display mode set to: ");
  Serial.println(displayMode == DISPLAY_TEXT ? "TEXT" : "BITMAP");
  Serial.print("[Config] Text rotation degrees: ");
  Serial.println(rotDeg);
  Serial.print("[Config] Text rotation index: ");
  Serial.println(screenRotation);
}

// ============================================================================
// mDNS Service Discovery
// ============================================================================

void initializeMDNS() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[mDNS] Starting mDNS service...");

    String macShort = deviceID.substring(deviceID.length() - 5);
    String mDNSHostname = "m5atom-s3_" + macShort;
    String mDNSInstanceName = "m5atom-s3:" + macShort;

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
