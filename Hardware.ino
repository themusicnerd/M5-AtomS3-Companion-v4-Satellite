/*
 * Hardware Module - External RGB LED Control
 *
 * Manages external common-cathode RGB LED via PWM
 * Pins: G8 (Red), G5 (Green), G6 (Blue), G7 (Ground)
 */

// ============================================================================
// LED Initialization
// ============================================================================

void setupLED() {
  pinMode(LED_PIN_GND, OUTPUT);
  digitalWrite(LED_PIN_GND, LOW);

  ledcAttach(LED_PIN_RED, pwmFreq, pwmResolution);
  ledcAttach(LED_PIN_GREEN, pwmFreq, pwmResolution);
  ledcAttach(LED_PIN_BLUE, pwmFreq, pwmResolution);

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

  ledcWrite(LED_PIN_RED,   scaledR);
  ledcWrite(LED_PIN_GREEN, scaledG);
  ledcWrite(LED_PIN_BLUE,  scaledB);
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
