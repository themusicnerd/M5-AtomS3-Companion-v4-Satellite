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
  pinMode(LED_PIN_GND, OUTPUT);
  digitalWrite(LED_PIN_GND, LOW);

  attachLedPwm(LED_PIN_RED, 0);
  attachLedPwm(LED_PIN_GREEN, 1);
  attachLedPwm(LED_PIN_BLUE, 2);

  setExternalLedColor(0, 0, 0);
  setExternalLedColor(255, 255, 255);  // Power-on test
}

// ============================================================================
// LED Color Control
// ============================================================================

void setExternalLedColor(uint8_t r, uint8_t g, uint8_t b) {
  lastColorR = r;
  lastColorG = g;
  lastColorB = b;

  // Scale by brightness (min 15% to keep LED visible)
  uint8_t scaledR = r * max(brightness, 15) / 100;
  uint8_t scaledG = g * max(brightness, 15) / 100;
  uint8_t scaledB = b * max(brightness, 15) / 100;

  // For common anode LED, invert: scaledX = 255 - scaledX

  writeLedPwm(LED_PIN_RED, 0, scaledR);
  writeLedPwm(LED_PIN_GREEN, 1, scaledG);
  writeLedPwm(LED_PIN_BLUE, 2, scaledB);
}

// ============================================================================
// Connection Status LED Blink
// ============================================================================

void updateReconnectingLED() {
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
}
