/*
 * Config Module - Boot Menu & Preferences
 *
 * Manages device configuration:
 * - Interactive boot menu (button-driven)
 * - Preferences/NVS storage
 * - WiFiManager callbacks
 */

// ============================================================================
// Boot Menu Constants
// ============================================================================

#define MENU_BOOT_NORMAL 0
#define MENU_BOOT_CONFIG 1
#define MENU_BOOT_ROUTER 2
#define MENU_DISPLAY_MODE 3
#define MENU_TEXT_ROTATION 4

// ============================================================================
// WiFiManager Helpers
// ============================================================================

String getParam(const String& name) {
  if (wifiManager.server && wifiManager.server->hasArg(name))
    return wifiManager.server->arg(name);
  return "";
}

void buildDisplayModeHTML(char* buffer, size_t bufferSize, int currentMode) {
  snprintf(buffer, bufferSize,
    "<br/><label for='displayMode'>Display Mode</label>"
    "<select name='displayMode' id='displayMode'>"
    "<option value='bitmap'%s>Bitmap</option>"
    "<option value='text'%s>Text</option>"
    "</select>",
    (currentMode == DISPLAY_BITMAP) ? " selected" : "",
    (currentMode == DISPLAY_TEXT) ? " selected" : ""
  );
}

void buildRotationHTML(char* buffer, size_t bufferSize, int currentRotation) {
  snprintf(buffer, bufferSize,
    "<br/><label for='rotation'>Text Rotation</label>"
    "<select name='rotation' id='rotation'>"
    "<option value='0'%s>0°</option>"
    "<option value='90'%s>90°</option>"
    "<option value='180'%s>180°</option>"
    "<option value='270'%s>270°</option>"
    "</select>",
    (currentRotation == 0) ? " selected" : "",
    (currentRotation == 1) ? " selected" : "",
    (currentRotation == 2) ? " selected" : "",
    (currentRotation == 3) ? " selected" : ""
  );
}

void saveParamCallback() {
  String str_companionIP   = getParam("companionIP");
  String str_companionPort = getParam("companionPort");
  String str_displayMode   = getParam("displayMode");
  String str_rotation      = getParam("rotation");

  preferences.begin("companion", false);
  if (str_companionIP.length() > 0)    preferences.putString("companionip",   str_companionIP);
  if (str_companionPort.length() > 0)  preferences.putString("companionport", str_companionPort);
  if (str_displayMode.length() > 0)    preferences.putString("displayMode",   str_displayMode);
  if (str_rotation.length() > 0)       preferences.putString("rotation",      str_rotation);
  preferences.end();
}

// ============================================================================
// Preferences Management
// ============================================================================

int degreesToRotationIndex(int degrees) {
  if      (degrees == 90)  return 1;
  else if (degrees == 180) return 2;
  else if (degrees == 270) return 3;
  else                     return 0;
}

void loadPreferences() {
  preferences.begin("companion", false);
  preferences.putString("deviceid", deviceID);

  if (preferences.getString("companionip").length() > 0)
    preferences.getString("companionip").toCharArray(companion_host.data(), companion_host.size());

  if (preferences.getString("companionport").length() > 0)
    preferences.getString("companionport").toCharArray(companion_port.data(), companion_port.size());

  String modeStr = preferences.getString("displayMode", "bitmap");
  String rotStr  = preferences.getString("rotation",   "0");

  if (modeStr.equalsIgnoreCase("text")) {
    displayMode = DISPLAY_TEXT;
  } else {
    displayMode = DISPLAY_BITMAP;
  }

  int rotDeg = rotStr.toInt();
  screenRotation = degreesToRotationIndex(rotDeg);

  preferences.end();

  Serial.print("[Prefs] Companion IP: ");
  Serial.println(companion_host.data());
  Serial.print("[Prefs] Companion Port: ");
  Serial.println(companion_port.data());
  Serial.print("[Prefs] Display Mode: ");
  Serial.println(displayMode == DISPLAY_TEXT ? "TEXT" : "BITMAP");
  Serial.print("[Prefs] Text rotation degrees: ");
  Serial.println(rotDeg);
  Serial.print("[Prefs] Text rotation index: ");
  Serial.println(screenRotation);
}

void saveDisplaySettings() {
  preferences.begin("companion", false);
  preferences.putString("displayMode", displayMode == DISPLAY_TEXT ? "text" : "bitmap");
  preferences.putString("rotation", String(screenRotation * 90));
  preferences.end();
}

// ============================================================================
// Boot Menu Functions
// ============================================================================

void drawMenuItem(String text, int y, bool selected) {
  if (selected) {
    M5.Display.setTextColor(BLACK, WHITE);
    M5.Display.setCursor(8, y);
    M5.Display.print("> " + text);
    M5.Display.setTextColor(WHITE, BLACK);
  } else {
    M5.Display.setCursor(8, y);
    M5.Display.print("  " + text);
  }
}

void drawBootMenu(int selectedIndex) {
  M5.Display.fillScreen(BLACK);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(WHITE, BLACK);

  M5.Display.setCursor(10, 5);
  M5.Display.print("BOOT MENU");
  M5.Display.drawLine(0, 15, 128, 15, WHITE);

  int y = 25;
  int lineHeight = 18;

  // Boot modes
  drawMenuItem("Boot: Normal", y, selectedIndex == 0);
  drawMenuItem("Boot: Web Config", y + lineHeight, selectedIndex == 1);
  drawMenuItem("Boot: WiFi AP", y + lineHeight * 2, selectedIndex == 2);

  // Display mode
  String dispVal = (displayMode == DISPLAY_TEXT) ? "TEXT" : "BITMAP";
  drawMenuItem("Display: " + dispVal, y + lineHeight * 3, selectedIndex == 3);

  // Rotation (text mode only)
  if (displayMode == DISPLAY_TEXT) {
    int degrees = screenRotation * 90;
    drawMenuItem("Rotation: " + String(degrees) + "\xF8", y + lineHeight * 4, selectedIndex == 4);
  }

  M5.Display.drawLine(0, 112, 128, 112, WHITE);
  M5.Display.setCursor(5, 118);
  M5.Display.setTextSize(1);
  M5.Display.print("Click=Next Hold=OK");
}

void handleMenuSelection(int item) {
  switch (item) {
    case MENU_BOOT_NORMAL:
      break;

    case MENU_BOOT_CONFIG:
      forceConfigPortalOnBoot = true;
      break;

    case MENU_BOOT_ROUTER:
      forceRouterModeOnBoot = true;
      break;

    case MENU_DISPLAY_MODE:
      displayMode = (displayMode == DISPLAY_BITMAP) ? DISPLAY_TEXT : DISPLAY_BITMAP;
      saveDisplaySettings();
      return;

    case MENU_TEXT_ROTATION:
      screenRotation = (screenRotation + 1) % 4;
      saveDisplaySettings();
      return;
  }
}

void runBootMenu() {
  int currentMenuItem = 0;
  int menuItemCount = 5;
  unsigned long menuStartTime = millis();
  bool waitButtonRelease = M5.BtnA.isPressed();
  bool exitMenu = false;
  bool needsRedraw = true;
  bool holdHandled = false;

  while (!exitMenu) {
    M5.update();

    menuItemCount = (displayMode == DISPLAY_TEXT) ? 5 : 4;

    if (currentMenuItem >= menuItemCount) {
      currentMenuItem = menuItemCount - 1;
      needsRedraw = true;
    }

    // Hold for 1s: select item
    if (M5.BtnA.pressedFor(500) && !holdHandled && !waitButtonRelease) {
      handleMenuSelection(currentMenuItem);
      holdHandled = true;
      needsRedraw = true;

      // Show feedback and exit for boot modes
      if (currentMenuItem <= MENU_BOOT_ROUTER) {
        M5.Display.fillScreen(BLACK);
        if (currentMenuItem == MENU_BOOT_NORMAL) {
          drawCenterText("Booting...", WHITE, BLACK);
        } else if (currentMenuItem == MENU_BOOT_CONFIG) {
          drawCenterText("Starting\nConfig Portal...", WHITE, BLACK);
        } else if (currentMenuItem == MENU_BOOT_ROUTER) {
          drawCenterText("Starting\nRouter Mode...", WHITE, BLACK);
        }
        exitMenu = true;
      }
    }

    // Release: navigate if short click
    if (M5.BtnA.wasReleased()) {
      if (waitButtonRelease) {
        waitButtonRelease = false;
        continue;
      }
      if (!holdHandled) {
        currentMenuItem = (currentMenuItem + 1) % menuItemCount;
        needsRedraw = true;
      }
      holdHandled = false;
    }

    if (needsRedraw) {
      drawBootMenu(currentMenuItem);
      needsRedraw = false;
    }

    // Timeout for stuck button detection
    if (waitButtonRelease && millis() - menuStartTime > 10000) {
      currentMenuItem = MENU_BOOT_NORMAL;
      drawCenterText("BUTTON ERROR\nDETECTED\n\nBooting...", WHITE, BLACK);
      break;
    }

    delay(10);
  }
}
