/*
 * Hardware Module - External RGB LED Control
 *
 * Manages external common-cathode RGB LED via PWM
 * Pins: G8 (Red), G5 (Green), G6 (Blue), G7 (Ground)
 */

// ============================================================================
// LED Initialization
// ============================================================================

bool attachLedPwm(uint8_t pin, uint8_t channel) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  return ledcAttach(pin, pwmFreq, pwmResolution);
#else
  ledcSetup(channel, pwmFreq, pwmResolution);
  ledcAttachPin(pin, channel);
  return true;
#endif
}

void writeLedPwm(uint8_t pin, uint8_t channel, uint8_t value) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin, value);
#else
  ledcWrite(channel, value);
#endif
}

void setupLED() {
#ifdef ATOMIC_POE_BUILD
  // G5/G6/G7/G8 are the Atomic PoE W5500 SPI bus.
  Serial.println("[LED] Disabled in Atomic PoE build");
#else
  pinMode(LED_PIN_GND, OUTPUT);
  digitalWrite(LED_PIN_GND, LOW);

  attachLedPwm(LED_PIN_RED, 0);
  attachLedPwm(LED_PIN_GREEN, 1);
  attachLedPwm(LED_PIN_BLUE, 2);

  setExternalLedColor(0, 0, 0);
#endif
}

void runBootColorTest() {
  const uint8_t colors[][3] = {
    {255, 0, 0},
    {0, 255, 0},
    {0, 0, 255},
    {255, 255, 255}
  };

  for (const auto& color : colors) {
    M5.Display.fillScreen(M5.Display.color565(color[0], color[1], color[2]));
    setExternalLedColor(color[0], color[1], color[2]);
    delay(300);
  }

  M5.Display.fillScreen(BLACK);
  setExternalLedColor(0, 0, 0);
}

// ============================================================================
// LED Color Control
// ============================================================================

void setExternalLedColor(uint8_t r, uint8_t g, uint8_t b) {
  lastColorR = r;
  lastColorG = g;
  lastColorB = b;

  // For common anode LED, invert: scaledX = 255 - scaledX

#ifndef ATOMIC_POE_BUILD
  const uint8_t outputR = ledEnabled ? min(255, int(r) * ledBrightnessPercent / 100) : 0;
  const uint8_t outputG = ledEnabled ? min(255, int(g) * ledBrightnessPercent / 100) : 0;
  const uint8_t outputB = ledEnabled ? min(255, int(b) * ledBrightnessPercent / 100) : 0;
  writeLedPwm(LED_PIN_RED, 0, outputR);
  writeLedPwm(LED_PIN_GREEN, 1, outputG);
  writeLedPwm(LED_PIN_BLUE, 2, outputB);
#endif
}

// ============================================================================
// Connection Status LED Blink
// ============================================================================

void updateReconnectingLED() {
#ifdef ATOMIC_POE_BUILD
  return;
#else
  unsigned long now = millis();

  if (now - lastBlinkTime >= blinkIntervalMs) {
    blinkState = !blinkState;
    lastBlinkTime = now;

    if (blinkState) {
      setExternalLedColor(255, 0, 0);  // Red ON
    } else {
      setExternalLedColor(0, 0, 0);    // OFF
    }
  }
#endif
}
