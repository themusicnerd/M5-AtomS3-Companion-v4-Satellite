/*
 * Display Module - Bitmap and Text Rendering
 *
 * Handles both display modes:
 * - BITMAP: 72x72 RGB/RGBA upscaling to 128x128
 * - TEXT: Dynamic font sizing, word-wrapping, color parsing
 */

// ============================================================================
// Function Prototypes
// ============================================================================

std::vector<String> splitIntoWords(const String& text);
void wrapTextToLines(const String& text, int targetWidth, std::vector<String>& outLines);
bool tryFitSegments(const std::vector<String>& segments, int targetWidth, int screenHeight);

// ============================================================================
// Base64 Decoding
// ============================================================================

const char* B64_TABLE = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64Index(char c) {
  const char* p = strchr(B64_TABLE, c);
  if (!p) return -1;
  return (int)(p - B64_TABLE);
}

String decodeBase64(const String& input) {
  int len = input.length();
  int val = 0;
  int valb = -8;
  String out;

  for (int i = 0; i < len; i++) {
    char c = input[i];
    if (c == '=') break;
    int idx = b64Index(c);
    if (idx < 0) break;

    val = (val << 6) + idx;
    valb += 6;
    if (valb >= 0) {
      char outChar = (char)((val >> valb) & 0xFF);
      out += outChar;
      valb -= 8;
    }
  }
  return out;
}

String decodeCompanionText(const String& encoded) {
  if (encoded.length() == 0) return encoded;

  // Check if it looks like base64
  bool looksBase64 = true;
  for (size_t i = 0; i < encoded.length(); i++) {
    char c = encoded[i];
    if (!((c >= 'A' && c <= 'Z') ||
          (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') ||
          c == '+' || c == '/' || c == '=')) {
      looksBase64 = false;
      break;
    }
  }

  if (!looksBase64) return encoded;

  String decoded = decodeBase64(encoded);
  if (decoded.length() == 0) return encoded;
  return decoded;
}

// ============================================================================
// Color Parsing
// ============================================================================

bool parseColorToken(const String& line, const String& key, int &r, int &g, int &b) {
  int pos = line.indexOf(key);
  if (pos < 0) return false;

  pos += key.length();
  if (pos < (int)line.length() && line[pos] == '=') pos++;

  int end = line.indexOf(' ', pos);
  if (end < 0) end = line.length();

  String val = line.substring(pos, end);
  val.trim();
  if (val.length() == 0) return false;

  // Strip quotes
  if (val.length() >= 2 && val[0] == '"' && val[val.length() - 1] == '"') {
    val = val.substring(1, val.length() - 1);
  }

  // rgb()/rgba()
  if (val.startsWith("rgb(") || val.startsWith("rgba(")) {
    String c = val;
    c.replace("rgba(", "");
    c.replace("rgb(", "");
    c.replace(")", "");
    c.replace(" ", "");
    int p1 = c.indexOf(',');
    int p2 = c.indexOf(',', p1+1);
    if (p1 < 0 || p2 < 0) return false;
    int p3 = c.indexOf(',', p2+1);

    r = c.substring(0, p1).toInt();
    g = c.substring(p1+1, p2).toInt();
    if (p3 >= 0) {
      b = c.substring(p2+1, p3).toInt();
    } else {
      b = c.substring(p2+1).toInt();
    }
    return true;
  }

  // Hex: #RRGGBB
  if (val[0] == '#') {
    if (val.length() < 7) return false;
    String rs = val.substring(1, 3);
    String gs = val.substring(3, 5);
    String bs = val.substring(5, 7);
    r = (int) strtol(rs.c_str(), nullptr, 16);
    g = (int) strtol(gs.c_str(), nullptr, 16);
    b = (int) strtol(bs.c_str(), nullptr, 16);
    return true;
  }

  // CSV: R,G,B
  int c1 = val.indexOf(',');
  int c2 = val.indexOf(',', c1 + 1);
  if (c1 < 0 || c2 < 0) return false;

  r = val.substring(0, c1).toInt();
  g = val.substring(c1 + 1, c2).toInt();
  b = val.substring(c2 + 1).toInt();
  return true;
}

// ============================================================================
// Display Helper Functions
// ============================================================================

void clearScreen(uint16_t color) {
  M5.Display.fillScreen(color);
}

void drawCenterText(const String& txt, uint16_t color, uint16_t bg) {
  M5.Display.fillScreen(bg);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(color, bg);
  M5.Display.setTextDatum(middle_center);

  std::vector<String> lines;
  int start = 0;
  while (true) {
    int idx = txt.indexOf('\n', start);
    if (idx < 0) {
      lines.push_back(txt.substring(start));
      break;
    }
    lines.push_back(txt.substring(start, idx));
    start = idx + 1;
  }

  int lineHeight  = M5.Display.fontHeight();
  int totalHeight = lineHeight * lines.size();
  int topY        = (M5.Display.height() - totalHeight) / 2 + (lineHeight / 2);

  for (int i = 0; i < (int)lines.size(); i++) {
    int y = topY + i * lineHeight;
    M5.Display.drawString(lines[i], M5.Display.width() / 2, y);
  }
}

void applyDisplayBrightness() {
  int p = brightness;
  if (p < 1)   p = 1;
  if (p > 100) p = 100;
  uint8_t level = map(p, 1, 100, 0, 255);
  M5.Display.setBrightness(level);
}

void drawBitmapRGB888FullScreen(uint8_t* rgb, int size) {
  int sw = M5.Display.width();
  int sh = M5.Display.height();

  uint16_t* upscaled = (uint16_t*)ps_malloc(sw * sh * sizeof(uint16_t));
  if (!upscaled) {
    upscaled = (uint16_t*)malloc(sw * sh * sizeof(uint16_t));
  }
  if (!upscaled) {
    Serial.println("[RENDER] Out of memory!");
    return;
  }

  // 2x nearest-neighbor upscale
  for (int y = 0; y < sh; y++) {
    int srcY = y >> 1;
    for (int x = 0; x < sw; x++) {
      int srcX = x >> 1;
      int srcIdx = (srcY * size + srcX) * 3;
      upscaled[y * sw + x] = M5.Display.color565(rgb[srcIdx], rgb[srcIdx+1], rgb[srcIdx+2]);
    }
  }

  M5.Display.startWrite();
  M5.Display.setAddrWindow(0, 0, sw, sh);
  M5.Display.setSwapBytes(true);
  M5.Display.pushPixels(upscaled, sw * sh);
  M5.Display.setSwapBytes(false);
  M5.Display.endWrite();

  free(upscaled);
}

// ============================================================================
// Text Mode Font Helpers
// ============================================================================

void setExtraBigFont() {
  M5.Display.setFont(&fonts::Font4);
  M5.Display.setTextSize(3);
}

void setLargeFont() {
  M5.Display.setFont(&fonts::Font4);
  M5.Display.setTextSize(2);
}

void setNormalFont() {
  M5.Display.setFont(&fonts::Font4);
  M5.Display.setTextSize(1);
}


// ============================================================================
// Text Mode Display Helpers
// ============================================================================

std::vector<String> splitIntoWords(const String& text) {
  std::vector<String> words;
  int start = 0;
  while (start < (int)text.length()) {
    int space = text.indexOf(' ', start);
    if (space < 0) space = text.length();
    String word = text.substring(start, space);
    if (word.length() > 0) words.push_back(word);
    start = space + 1;
  }
  return words;
}

void wrapTextToLines(const String& text, int targetWidth, std::vector<String>& outLines) {
  outLines.clear();

  std::vector<String> words = splitIntoWords(text);
  String currentLine = "";

  for (size_t i = 0; i < words.size(); i++) {
    String candidate = (currentLine.length() == 0) ? words[i] : currentLine + " " + words[i];
    int candidateWidth = M5.Display.textWidth(candidate);

    if (candidateWidth <= targetWidth) {
      currentLine = candidate;
    } else {
      int wordWidth = M5.Display.textWidth(words[i]);

      if (wordWidth > targetWidth) {
        // Word too long - break character by character
        if (currentLine.length() > 0) {
          outLines.push_back(currentLine);
          currentLine = "";
        }

        String word = words[i];
        for (size_t charIdx = 0; charIdx < word.length(); charIdx++) {
          String charCandidate = (currentLine.length() == 0) ? String(word[charIdx]) : currentLine + word[charIdx];
          int charWidth = M5.Display.textWidth(charCandidate);

          if (charWidth <= targetWidth) {
            currentLine = charCandidate;
          } else {
            if (currentLine.length() > 0) {
              outLines.push_back(currentLine);
            }
            currentLine = String(word[charIdx]);
          }
        }
      } else {
        // Word fits on its own line
        if (currentLine.length() > 0) {
          outLines.push_back(currentLine);
        }
        currentLine = words[i];
      }
    }
  }

  if (currentLine.length() > 0) {
    outLines.push_back(currentLine);
  }
}

void applyFixedFontSize(int ptSize) {
  // Map Companion point sizes to M5GFX fonts (Font4 scales well, Font8 has limited chars)
  if (ptSize <= 7) {
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
  // Skip ptSize <= 14, we don't have enough granularity
  } else if (ptSize <= 18) {
    M5.Display.setFont(&fonts::Font4);
    M5.Display.setTextSize(1);
  } else if (ptSize <= 24) {
    M5.Display.setFont(&fonts::Font4);
    M5.Display.setTextSize(2);
  } else if (ptSize <= 36) {
    M5.Display.setFont(&fonts::Font4);
    M5.Display.setTextSize(3);
  } else {
    M5.Display.setFont(&fonts::Font4);
    M5.Display.setTextSize(4);
  }
}


void analyseLayout(int fontSizeOverride) {
  manualLines.clear();

  if (currentText.length() == 0) {
    return;
  }

  M5.Display.setTextWrap(false);

  int screenW = M5.Display.width();
  int screenH = M5.Display.height();
  int targetWidth = (int)(screenW * 0.9);

  // Step 1: Split by \n if present
  std::vector<String> segments;
  bool hasManualNewlines = (currentText.indexOf('\n') >= 0);

  if (hasManualNewlines) {
    int start = 0;
    while (true) {
      int idx = currentText.indexOf('\n', start);
      if (idx < 0) {
        segments.push_back(currentText.substring(start));
        break;
      }
      segments.push_back(currentText.substring(start, idx));
      start = idx + 1;
    }
  } else {
    segments.push_back(currentText);
  }

  // Step 2: Determine font size
  if (fontSizeOverride > 0) {
    // Use Companion-specified font size
    applyFixedFontSize(fontSizeOverride);
  } else {
    // Auto-size font to fit content
    // Try fonts from largest to smallest
    bool foundFit = false;

    // Try ExtraBig (Font4 size 3)
    setExtraBigFont();
    if (tryFitSegments(segments, targetWidth, screenH)) {
      foundFit = true;
    }

    if (!foundFit) {
      // Try Large (Font4 size 2)
      setLargeFont();
      if (tryFitSegments(segments, targetWidth, screenH)) {
        foundFit = true;
      }
    }

    if (!foundFit) {
      // Fallback to Normal font
      setNormalFont();
    }
  }

  // Step 3: Wrap each segment if needed
  manualLines.clear();
  for (size_t i = 0; i < segments.size(); i++) {
    String seg = segments[i];
    int segWidth = M5.Display.textWidth(seg);

    if (segWidth <= targetWidth) {
      manualLines.push_back(seg);
    } else {
      std::vector<String> wrappedLines;
      wrapTextToLines(seg, targetWidth, wrappedLines);
      for (size_t j = 0; j < wrappedLines.size(); j++) {
        manualLines.push_back(wrappedLines[j]);
      }
    }
  }

  // Limit to 5 visible lines
  if ((int)manualLines.size() > 5) {
    manualLines.resize(5);
  }
}

bool tryFitSegments(const std::vector<String>& segments, int targetWidth, int screenHeight) {
  int totalLines = 0;

  for (size_t i = 0; i < segments.size(); i++) {
    String seg = segments[i];
    int segWidth = M5.Display.textWidth(seg);

    if (segWidth <= targetWidth) {
      totalLines++;
    } else {
      std::vector<String> wrappedLines;
      wrapTextToLines(seg, targetWidth, wrappedLines);
      totalLines += wrappedLines.size();
    }
  }

  int lineHeight = M5.Display.fontHeight();
  int totalHeight = lineHeight * totalLines;

  return (totalHeight <= screenHeight && totalLines <= 5);
}

void drawTextPressedBorderIfNeeded() {
  if (!textPressedBorder) return;

  int w = M5.Display.width();
  int h = M5.Display.height();
  uint16_t borderColor = M5.Display.color565(255, 255, 0);

  for (int i = 0; i < 4; i++) {
    M5.Display.drawRect(i, i, w - i * 2, h - i * 2, borderColor);
  }
}

void refreshTextDisplay() {
  M5.Display.fillScreen(bgColor);
  M5.Display.setTextColor(txtColor, bgColor);
  M5.Display.setTextWrap(false);
  M5.Display.clearClipRect();

  if (currentText.length() == 0 || manualLines.size() == 0) {
    drawTextPressedBorderIfNeeded();
    return;
  }

  int screenW = M5.Display.width();
  int screenH = M5.Display.height();
  int lineHeight = M5.Display.fontHeight();
  int lines = manualLines.size();

  if (lines == 1) {
    // Single line - use middle centering
    int centerY = (screenH / 2) + (lineHeight / 8);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(manualLines[0], screenW / 2, centerY);
  } else {
    // Multi-line - use top centering
    M5.Display.setTextDatum(top_center);
    int totalHeight = lineHeight * lines;
    int topY = (screenH - totalHeight) / 2 + (lineHeight / 5);

    for (int i = 0; i < lines; i++) {
      int y = topY + i * lineHeight;
      M5.Display.drawString(manualLines[i], screenW / 2, y);
    }
  }

  drawTextPressedBorderIfNeeded();
}

// ============================================================================
// Text Mode Update Functions
// ============================================================================

void setText(const String& txt, int fontSizeOverride) {
  currentText = txt;
  analyseLayout(fontSizeOverride);
  refreshTextDisplay();
}

// ============================================================================
// Connection Status Overlay
// ============================================================================

void drawReconnectingOverlay() {
  int screenW = M5.Display.width();
  int screenH = M5.Display.height();

  int boxWidth = 110;
  int boxHeight = 30;
  int boxX = (screenW - boxWidth) / 2;
  int boxY = (screenH - boxHeight) / 2;

  uint16_t bgColor = M5.Display.color565(40, 40, 40);
  uint16_t borderColor = M5.Display.color565(255, 0, 0);
  uint16_t textColor = M5.Display.color565(255, 255, 255);

  for (int i = 0; i < 4; i++) {
    M5.Display.drawRect(i, i, screenW - i * 2, screenH - i * 2, borderColor);
  }
  M5.Display.drawLine(0, 0, screenW, screenH, borderColor);
  M5.Display.drawLine(0, screenH, screenW, 0, borderColor);

  M5.Display.fillRect(boxX, boxY, boxWidth, boxHeight, bgColor);
  M5.Display.drawRect(boxX, boxY, boxWidth, boxHeight, borderColor);
  M5.Display.drawRect(boxX+1, boxY+1, boxWidth-2, boxHeight-2, borderColor);

  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(textColor, bgColor);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString("Reconnecting...", screenW / 2, screenH / 2);
}
