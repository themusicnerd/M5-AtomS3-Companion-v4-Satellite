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
bool wrapToLines(const String& src, String& l1, String& l2, String& l3, int& outLines, int maxLines, int targetWidth = -1);
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

  // Split into lines
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
  if (p < 0)   p = 0;
  if (p > 100) p = 100;
  uint8_t level = map(p, 0, 100, 0, 255);
  M5.Display.setBrightness(level);
}

void drawBitmapRGB888FullScreen(uint8_t* rgb, int size) {
  int sw = M5.Display.width();   // 128
  int sh = M5.Display.height();  // 128

  // Allocate upscaled buffer (128x128x2 = 32KB RGB565)
  uint16_t* upscaled = (uint16_t*)ps_malloc(sw * sh * sizeof(uint16_t));
  if (!upscaled) {
    upscaled = (uint16_t*)malloc(sw * sh * sizeof(uint16_t));
  }
  if (!upscaled) {
    Serial.println("[RENDER] Out of memory!");
    return;  // Can't render without memory
  }

  // Pre-upscale with nearest-neighbor
  for (int y = 0; y < sh; y++) {
    int srcY = y >> 1;  // Bit shift for 2x scaling
    for (int x = 0; x < sw; x++) {
      int srcX = x >> 1;
      int srcIdx = (srcY * size + srcX) * 3;
      upscaled[y * sw + x] = M5.Display.color565(rgb[srcIdx], rgb[srcIdx+1], rgb[srcIdx+2]);
    }
  }

  // Single block transfer
  M5.Display.startWrite();
  M5.Display.setAddrWindow(0, 0, sw, sh);
  M5.Display.setSwapBytes(true);  // May be needed for correct color order
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
  // Map Companion point sizes to M5GFX fonts
  // Font4 = versatile, scales well with textSize
  // Font8 has limited character support - avoid it
  if (ptSize <= 7) {
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1);
  } else if (ptSize <= 14) {
    M5.Display.setFont(&fonts::Font4);
    M5.Display.setTextSize(1);
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

bool wrapToLines(const String& src, String& l1, String& l2, String& l3, int& outLines, int maxLines, int targetWidth) {
  l1 = "";
  l2 = "";
  l3 = "";
  outLines = 0;

  if (src.length() == 0) return true;

  M5.Display.setTextWrap(false);

  int screenW = (targetWidth > 0) ? targetWidth : M5.Display.width();

  std::vector<String> lines;
  wrapTextToLines(src, screenW, lines);

  if ((int)lines.size() > maxLines) {
    return false;
  }

  outLines = lines.size();

  if (outLines >= 1) l1 = lines[0];
  if (outLines >= 2) l2 = lines[1];
  if (outLines >= 3) l3 = lines[2];

  autoWrappedLines.clear();
  for (int i = 0; i < outLines && i < maxLines; i++) {
    autoWrappedLines.push_back(lines[i]);
  }

  return true;
}

void analyseLayout(int fontSizeOverride) {
  line1 = "";
  line2 = "";
  line3 = "";
  numLines = 0;
  useManualLines = false;
  useAutoWrappedLines = false;
  manualLines.clear();
  autoWrappedLines.clear();

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

  // Step 4: Set rendering mode
  if (hasManualNewlines || manualLines.size() > 1) {
    useManualLines = true;
  } else if (manualLines.size() == 1) {
    // Single line
    numLines = 1;
    line1 = manualLines[0];
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

  // Clear any clipping to allow large text to render even if it exceeds margins
  M5.Display.clearClipRect();

  if (currentText.length() == 0) {
    drawTextPressedBorderIfNeeded();
    return;
  }

  int screenW = M5.Display.width();
  int screenH = M5.Display.height();

  // Manual multi-line mode
  if (useManualLines) {
    Serial.printf("[RENDER] Manual multi-line mode: %d lines\n", manualLines.size());
    M5.Display.setTextDatum(top_center);

    int lineHeight  = M5.Display.fontHeight();
    int lines       = manualLines.size();
    int totalHeight = lineHeight * lines;

    // Calculate top Y position with baseline offset for better centering
    int baseTopY = (screenH - totalHeight) / 2;
    int topY = baseTopY + (lineHeight / 5);

    Serial.printf("[RENDER] Manual: lineHeight=%d totalHeight=%d baseTopY=%d topY=%d\n",
                  lineHeight, totalHeight, baseTopY, topY);

    for (int i = 0; i < lines; i++) {
      int y = topY + i * lineHeight;
      M5.Display.drawString(manualLines[i], screenW / 2, y);
    }

    drawTextPressedBorderIfNeeded();
    return;
  }

  // Single-line rendering (numLines == 1)
  if (numLines == 1) {
    Serial.printf("[RENDER] Single-line mode: fontHeight=%d\n", M5.Display.fontHeight());

    // For large fonts, shift down by 10-15% of font height to improve vertical centering
    // This compensates for M5GFX baseline positioning behavior
    int lineHeight = M5.Display.fontHeight();
    int yOffset = lineHeight / 8;  // Shift down by 12.5% of font height
    int centerY = (screenH / 2) + yOffset;

    Serial.printf("[RENDER] Single: screenH=%d centerY=%d yOffset=%d\n", screenH, centerY, yOffset);

    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(line1, screenW / 2, centerY);
  } else {
    // Multi-line rendering (numLines > 1)
    Serial.printf("[RENDER] Multi-line mode: %d lines, autoWrapped=%d\n",
                  numLines, useAutoWrappedLines ? 1 : 0);

    M5.Display.setTextDatum(top_center);

    int lineHeight  = M5.Display.fontHeight();
    int totalHeight = lineHeight * numLines;

    // Calculate top Y position with baseline offset for better centering
    // M5GFX fontHeight() includes ascent + descent, but top_center datum
    // positions at the top of ascent. Subtract ~20% offset to shift text down.
    int baseTopY = (screenH - totalHeight) / 2;
    int topY = baseTopY + (lineHeight / 5);

    Serial.printf("[RENDER] Multi: lineHeight=%d totalHeight=%d baseTopY=%d topY=%d\n",
                  lineHeight, totalHeight, baseTopY, topY);

    if (useAutoWrappedLines && autoWrappedLines.size() > 0) {
      for (int i = 0; i < numLines && i < (int)autoWrappedLines.size(); i++) {
        int y = topY + i * lineHeight;
        M5.Display.drawString(autoWrappedLines[i], screenW / 2, y);
      }
    } else {
      // Fallback to legacy 3-line array
      String lines[3] = { line1, line2, line3 };
      for (int i = 0; i < numLines && i < 3; i++) {
        int y = topY + i * lineHeight;
        M5.Display.drawString(lines[i], screenW / 2, y);
      }
    }
  }

  drawTextPressedBorderIfNeeded();
}

// ============================================================================
// Text Mode Update Functions
// ============================================================================

void setTextNow(const String& txt, int fontSizeOverride = 0) {
  currentText = txt;
  analyseLayout(fontSizeOverride);
  refreshTextDisplay();
}

void setText(const String& txt, int fontSizeOverride) {
  setTextNow(txt, fontSizeOverride);
}

// ============================================================================
// Text Mode API Handlers
// ============================================================================

void handleKeyStateTextField(const String& line) {
  int tPos = line.indexOf("TEXT=");
  if (tPos < 0) return;

  int firstQuote = line.indexOf('"', tPos);
  if (firstQuote < 0) return;

  int secondQuote = line.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) return;

  String textField = line.substring(firstQuote + 1, secondQuote);
  String decoded   = decodeCompanionText(textField);
  decoded.replace("\\n", "\n");

  Serial.print("[API] TEXT encoded=\"");
  Serial.print(textField);
  Serial.print("\" decoded=\"");
  Serial.print(decoded);
  Serial.println("\"");

  setText(decoded);
}

void handleTextModeColors(const String& line) {
  int r, g, b;
  bool bgOk  = false;
  bool txtOk = false;

  if (parseColorToken(line, "COLOR", r, g, b)) {
    bgColor = M5.Display.color565(r, g, b);
    bgOk = true;
    Serial.printf("[API] TEXT BG COLOR r=%d g=%d b=%d\n", r, g, b);
  }

  if (parseColorToken(line, "TEXTCOLOR", r, g, b)) {
    txtColor = M5.Display.color565(r, g, b);
    txtOk = true;
    Serial.printf("[API] TEXT FG COLOR r=%d g=%d b=%d\n", r, g, b);
  }

  if (bgOk || txtOk) {
    refreshTextDisplay();
  }
}
