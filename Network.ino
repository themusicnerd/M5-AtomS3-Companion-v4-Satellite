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

// Device-specific settings are intentionally separate from /api/config: that
// endpoint is owned by Companion's Satellite discovery and only carries host
// and port.  A Companion module (or another REST client) can use this endpoint
// without interfering with the one-click surface setup flow.
String jsonSetting(const String& body, const char* name) {
  const String key = String("\"") + name + "\"";
  int pos = body.indexOf(key);
  if (pos < 0) return "";
  pos = body.indexOf(':', pos + key.length());
  if (pos < 0) return "";
  pos++;
  while (pos < body.length() && isspace(body[pos])) pos++;
  if (pos < body.length() && body[pos] == '\"') {
    const int end = body.indexOf('\"', ++pos);
    return end < 0 ? "" : body.substring(pos, end);
  }
  int end = pos;
  while (end < body.length() && body[end] != ',' && body[end] != '}') end++;
  String value = body.substring(pos, end); value.trim(); return value;
}

void applySerialProvisioning(const String& body) {
  const String ssid = jsonSetting(body, "ssid");
  const String password = jsonSetting(body, "password");
  const String host = jsonSetting(body, "companionHost");
  const String port = jsonSetting(body, "companionPort");
  const String name = jsonSetting(body, "deviceName");
  if (port.length() && (port.toInt() < 1 || port.toInt() > 65535)) {
    Serial.println("PROVISION-ERROR invalid companionPort");
    return;
  }
  preferences.begin("companion", false);
  if (host.length()) {
    host.toCharArray(companion_host.data(), companion_host.size());
    preferences.putString("companionip", host);
  }
  if (port.length()) {
    port.toCharArray(companion_port.data(), companion_port.size());
    preferences.putString("companionport", port);
  }
  if (name.length()) {
    configuredDeviceName = name.substring(0, 48);
    preferences.putString("deviceName", configuredDeviceName);
  }
  preferences.end();
  Serial.println("PROVISION-OK");
#ifndef ATOMIC_POE_BUILD
  if (ssid.length()) {
    delay(100);
    WiFi.persistent(true);
    WiFi.begin(ssid.c_str(), password.c_str());
  }
#endif
}

void handleSerialProvisioning() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\n') {
      serialProvisionBuffer.trim();
      if (serialProvisionBuffer.startsWith("PROVISION "))
        applySerialProvisioning(serialProvisionBuffer.substring(10));
      serialProvisionBuffer = "";
    } else if (c != '\r' && serialProvisionBuffer.length() < 512) {
      serialProvisionBuffer += c;
    }
  }
}

void handleGetSettings() {
  const String mode = displayMode == DISPLAY_TEXT ? "text" : "bitmap";
  const String json = "{\"displayMode\":\"" + mode + "\",\"rotation\":" + String(screenRotation * 90) + ",\"brightness\":" + String(brightness) + ",\"ledEnabled\":" + String(ledEnabled ? "true" : "false") + ",\"ledBrightness\":" + String(ledBrightnessPercent) + "}";
  restServer.send(200, "application/json", json);
}

void handlePostSettings() {
  const String body = restServer.arg("plain");
  const String mode = jsonSetting(body, "displayMode");
  const String rotation = jsonSetting(body, "rotation");
  const String brightnessValue = jsonSetting(body, "brightness");
  const String ledEnabledValue = jsonSetting(body, "ledEnabled");
  const String ledBrightnessValue = jsonSetting(body, "ledBrightness");
  if (mode.length() && !mode.equalsIgnoreCase("text") && !mode.equalsIgnoreCase("bitmap")) { restServer.send(400, "text/plain", "Invalid displayMode"); return; }
  if (rotation.length() && !(rotation == "0" || rotation == "90" || rotation == "180" || rotation == "270")) { restServer.send(400, "text/plain", "Invalid rotation"); return; }
  if (brightnessValue.length() && (brightnessValue.toInt() < 0 || brightnessValue.toInt() > 100)) { restServer.send(400, "text/plain", "Invalid brightness"); return; }
  if (ledEnabledValue.length() && !(ledEnabledValue == "true" || ledEnabledValue == "false")) { restServer.send(400, "text/plain", "Invalid ledEnabled"); return; }
  if (ledBrightnessValue.length() && (ledBrightnessValue.toInt() < 0 || ledBrightnessValue.toInt() > 200)) { restServer.send(400, "text/plain", "Invalid ledBrightness"); return; }
  if (mode.length()) displayMode = mode.equalsIgnoreCase("text") ? DISPLAY_TEXT : DISPLAY_BITMAP;
  if (rotation.length()) screenRotation = degreesToRotationIndex(rotation.toInt());
  if (brightnessValue.length()) { brightness = brightnessValue.toInt(); applyDisplayBrightness(); }
  if (ledEnabledValue.length()) ledEnabled = ledEnabledValue == "true";
  if (ledBrightnessValue.length()) ledBrightnessPercent = ledBrightnessValue.toInt();
  saveDisplaySettings();
  setExternalLedColor(lastColorR, lastColorG, lastColorB);
  M5.Display.setRotation(displayMode == DISPLAY_TEXT ? screenRotation : 0);
  restServer.send(200, "application/json", "{\"ok\":true}");
}

void handlePostHardwareTest() {
  const String body = restServer.arg("plain");
  const String target = jsonSetting(body, "target");
  const String value = jsonSetting(body, "value");
  if (target == "led") {
    if (value == "red") setExternalLedColor(255, 0, 0);
    else if (value == "green") setExternalLedColor(0, 255, 0);
    else if (value == "blue") setExternalLedColor(0, 0, 255);
    else if (value == "white") setExternalLedColor(255, 255, 255);
    else if (value == "off") setExternalLedColor(0, 0, 0);
    else { restServer.send(400, "text/plain", "LED value must be red, green, blue, white, or off"); return; }
  } else if (target == "display") {
    if (value == "red") M5.Display.fillScreen(RED);
    else if (value == "green") M5.Display.fillScreen(GREEN);
    else if (value == "blue") M5.Display.fillScreen(BLUE);
    else if (value == "white") M5.Display.fillScreen(WHITE);
    else if (value == "off") M5.Display.fillScreen(BLACK);
    else { restServer.send(400, "text/plain", "Display value must be red, green, blue, white, or off"); return; }
  } else if (target == "text") {
    if (!value.length()) { restServer.send(400, "text/plain", "Provide test text"); return; }
    setText(value.substring(0, 96));
  } else {
    restServer.send(400, "text/plain", "target must be led, display, or text");
    return;
  }
  restServer.send(200, "application/json", "{\"ok\":true}");
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

// Browser-based OTA update.  Use the application .bin from a GitHub release,
// never a bootloader or partition image.
const char* firmwareUpdateUser = "admin";
// Empty by default: updates are open until the owner elects to protect them.
String firmwareUpdatePassword = "";

bool requireFirmwareUpdateAuth() {
  if (firmwareUpdatePassword.length() == 0 || restServer.authenticate(firmwareUpdateUser, firmwareUpdatePassword.c_str())) return true;
  restServer.requestAuthentication();
  return false;
}

void handleFirmwareUpdatePage() {
  if (!requireFirmwareUpdateAuth()) return;
  restServer.send(200, "text/html",
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<h2>Firmware update</h2><p>Select the release application <code>.bin</code> file. "
    "Do not power off the device while it updates.</p>"
    "<form method=POST action=/update enctype=multipart/form-data>"
    "<input type=file name=firmware accept='.bin' required><button type=submit>Install and reboot</button></form>"
    "<hr><h3>Optional protection</h3><form method=POST action=/update/password><input type=password name=password placeholder='Leave blank to remove'><button type=submit>Save update password</button></form>");
}

void handleFirmwareUpload() {
  if (firmwareUpdatePassword.length() && !restServer.authenticate(firmwareUpdateUser, firmwareUpdatePassword.c_str())) return;
  auto& upload = restServer.upload();
  if (upload.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
  else if (upload.status == UPLOAD_FILE_WRITE) Update.write(upload.buf, upload.currentSize);
  else if (upload.status == UPLOAD_FILE_END) Update.end(true);
  else if (upload.status == UPLOAD_FILE_ABORTED) Update.abort();
}

void handleFirmwareUpdateResult() {
  if (!requireFirmwareUpdateAuth()) return;
  const bool success = !Update.hasError();
  restServer.send(success ? 200 : 500, "text/plain", success ? "Update complete. Rebooting..." : "Firmware update failed.");
  if (success) { delay(500); ESP.restart(); }
}

void handleFirmwareUpdatePassword() {
  if (!requireFirmwareUpdateAuth()) return;
  firmwareUpdatePassword = restServer.arg("password");
  preferences.begin("companion", false); preferences.putString("updatepassword", firmwareUpdatePassword); preferences.end();
  restServer.send(200, "text/plain", firmwareUpdatePassword.length() ? "Update password saved." : "Update password removed.");
}

String statusJsonEscape(String value) {
  value.replace("\\", "\\\\"); value.replace("\"", "\\\"");
  value.replace("\n", "\\n"); value.replace("\r", "\\r");
  return value;
}

void handleStatus() {
  String json = "{\"deviceName\":\"" + statusJsonEscape(configuredDeviceName.length() ? configuredDeviceName : "M5 AtomS3") + "\",\"deviceId\":\"" + statusJsonEscape(deviceID) + "\",\"firmware\":\"" FIRMWARE_VERSION "\",";
#ifdef ATOMIC_POE_BUILD
  json += "\"network\":\"ethernet\",\"networkConnected\":" + String(Ethernet.linkStatus() == LinkON ? "true" : "false") + ",";
#else
  json += "\"network\":\"wifi\",\"networkConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"ssid\":\"" + statusJsonEscape(WiFi.SSID()) + "\",\"ip\":\"" + WiFi.localIP().toString() + "\",";
#endif
  json += "\"companionConnected\":" + String(client.connected() ? "true" : "false") + ",";
  json += "\"companion\":\"" + statusJsonEscape(String(companion_host.data()) + ":" + companion_port.data()) + "\",";
  json += "\"text\":\"" + statusJsonEscape(currentText) + "\",\"displayMode\":\"" +
    String(displayMode == DISPLAY_TEXT ? "text" : "bitmap") + "\",";
  json += "\"ledEnabled\":" + String(ledEnabled ? "true" : "false") + ",\"ledBrightness\":" + String(ledBrightnessPercent) + ",";
  json += "\"buttonPressed\":" + String(M5.BtnA.isPressed() ? "true" : "false") + ",";
  json += "\"color\":{\"r\":" + String(lastColorR) + ",\"g\":" + String(lastColorG) + ",\"b\":" + String(lastColorB) + "},";
  json += "\"lastMessageAgeMs\":" + String(lastMessageTime ? millis() - lastMessageTime : 0) +
    ",\"uptimeSeconds\":" + String(millis() / 1000) + "}";
  restServer.send(200, "application/json", json);
}

void handleConfigPage() {
  const String mode = displayMode == DISPLAY_TEXT ? "text" : "bitmap";
  const String html =
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>M5 AtomS3</title><h2>M5 AtomS3</h2><h3>Live troubleshooting status</h3>"
    "<div id=state>Loading...</div><p>Incoming text: <code id=t>-</code></p>"
    "<p>Incoming colour: <span id=sw style='display:inline-block;width:2em;height:1em;border:1px solid'></span> <code id=c>-</code></p><p>Network: "
#ifdef ATOMIC_POE_BUILD
    "Atomic PoE / W5500"
#else
    "Wi-Fi"
#endif
    "</p><label>Companion host <input id=h value='" + String(companion_host.data()) +
    "'></label><br><label>Port <input id=p value='" + String(companion_port.data()) +
    "'></label><br><label>Display <select id=m><option>bitmap</option><option" +
    String(mode == "text" ? " selected" : "") + ">text</option></select></label><br>"
    "<label>Rotation <select id=r><option>0</option><option>90</option><option>180</option>"
    "<option>270</option></select></label><br><button onclick=s()>Save</button> "
    "<label><input id=le type=checkbox" + String(ledEnabled ? " checked" : "") + "> External RGB LED enabled</label> <label>LED scale <input id=lb type=number min=0 max=200 value='" + String(ledBrightnessPercent) + "'>%</label><br>"
    "<hr><b>Hardware tests</b><p>Button: <strong id=bt>released</strong></p>"
    "<p>External LED: <button onclick=tt('led','red')>Red</button> <button onclick=tt('led','green')>Green</button> <button onclick=tt('led','blue')>Blue</button> <button onclick=tt('led','white')>White</button> <button onclick=tt('led','off')>Off</button></p>"
    "<p>Screen: <button onclick=tt('display','red')>Red</button> <button onclick=tt('display','green')>Green</button> <button onclick=tt('display','blue')>Blue</button> <button onclick=tt('display','white')>White</button> <button onclick=tt('display','off')>Off</button></p>"
    "<p><input id=tx placeholder='Screen test text'><button onclick=tt('text',tx.value)>Show text</button></p>"
    "<a href=/update>Firmware update</a><pre id=o></pre><script>r.value='" +
    String(screenRotation * 90) + "';async function s(){let a=await fetch('/api/config',{method:'POST',"
    "headers:{'Content-Type':'application/json'},body:JSON.stringify({host:h.value,port:+p.value})});"
    "let b=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({displayMode:m.value,rotation:+r.value,ledEnabled:le.checked,ledBrightness:+lb.value})});o.textContent=(await a.text())+' '+"
    "(await b.text())}async function tt(target,value){let z=await fetch('/api/test',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({target,value})});o.textContent=await z.text()}async function u(){try{let x=await(await fetch('/api/status')).json();"
    "state.textContent=(x.networkConnected?'Network connected':'Network disconnected')+' | '+"
    "(x.companionConnected?'Companion connected':'Companion disconnected')+' | '+(x.ip||x.network);"
    "t.textContent=x.text||'(none)';bt.textContent=x.buttonPressed?'PRESSED':'released';let q=x.color;c.textContent=`rgb(${q.r}, ${q.g}, ${q.b})`;"
    "sw.style.background=`rgb(${q.r},${q.g},${q.b})`}catch(e){state.textContent='Status unavailable'}}"
    "u();setInterval(u,2000)</script>";
  restServer.send(200, "text/html", html);
}

void setupRestServer() {
  restServer.on("/", HTTP_GET, handleConfigPage);
  restServer.on("/api/host", HTTP_GET, handleGetHost);
  restServer.on("/api/port", HTTP_GET, handleGetPort);
  restServer.on("/api/config", HTTP_GET, handleGetConfig);
  restServer.on("/api/settings", HTTP_GET, handleGetSettings);
  restServer.on("/api/status", HTTP_GET, handleStatus);

  restServer.on("/api/host", HTTP_POST, handlePostHost);
  restServer.on("/api/port", HTTP_POST, handlePostPort);
  restServer.on("/api/config", HTTP_POST, handlePostConfig);
  restServer.on("/api/settings", HTTP_POST, handlePostSettings);
  restServer.on("/api/test", HTTP_POST, handlePostHardwareTest);
  restServer.on("/update", HTTP_GET, handleFirmwareUpdatePage);
  restServer.on("/update", HTTP_POST, handleFirmwareUpdateResult, handleFirmwareUpload);
  restServer.on("/update/password", HTTP_POST, handleFirmwareUpdatePassword);

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
#ifndef ATOMIC_POE_BUILD
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
  char mdnsHTML[512];
  buildDisplayModeHTML(displayModeHTML, sizeof(displayModeHTML), displayMode);
  buildRotationHTML(rotationHTML, sizeof(rotationHTML), screenRotation);
  buildMDNSHTML(mdnsHTML, sizeof(mdnsHTML), mdnsEnabled);

  custom_companionIP   = new WiFiManagerParameter("companionIP", "Companion IP", companion_host.data(), companion_host.size());
  custom_companionPort = new WiFiManagerParameter("companionPort", "Satellite Port", companion_port.data(), companion_port.size());
  custom_displayMode   = new WiFiManagerParameter(displayModeHTML);
  custom_rotation      = new WiFiManagerParameter(rotationHTML);
  custom_mdnsEnabled   = new WiFiManagerParameter(mdnsHTML);

  wifiManager.addParameter(custom_companionIP);
  wifiManager.addParameter(custom_companionPort);
  wifiManager.addParameter(custom_displayMode);
  wifiManager.addParameter(custom_rotation);
  wifiManager.addParameter(custom_mdnsEnabled);
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
#else
void runAPConfigPortal(const String&) {}

void connectToNetwork() {
  uint8_t ethernetMac[6];
  esp_read_mac(ethernetMac, ESP_MAC_WIFI_STA);
  Serial.println("[Ethernet] Initialising Atomic PoE W5500");
  drawCenterText("Ethernet\nDHCP...", WHITE, BLACK);
  SPI.begin(5, 7, 8, -1);
  Ethernet.init(6);
  while (Ethernet.begin(ethernetMac, 15000, 4000) == 0) {
    Serial.println("[Ethernet] DHCP failed; retrying");
    drawCenterText("Ethernet\nDHCP failed\nRetrying...", RED, BLACK);
    delay(5000);
  }
  Serial.println("[Ethernet] DHCP address: " + Ethernet.localIP().toString());
  drawCenterText("Ethernet ready\n\n" + Ethernet.localIP().toString() +
                 "\n\nSetup:\nhttp://" + Ethernet.localIP().toString() + ":9999", GREEN, BLACK);
  delay(1500);
  M5.Display.setRotation(displayMode == DISPLAY_TEXT ? screenRotation : 0);
}
#endif

// ============================================================================
// mDNS Service Discovery
// ============================================================================

void initializeMDNS() {
#ifdef ATOMIC_POE_BUILD
  Serial.println("[mDNS] W5500 build: use the displayed DHCP address and wired setup page");
  return;
#else
  if (!mdnsEnabled) {
    Serial.println("[mDNS] Discovery disabled in configuration");
    return;
  }

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
#endif
}
