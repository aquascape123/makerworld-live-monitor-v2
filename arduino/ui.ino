// ============================================
// GRAFICA: alto contrasto e font in grassetto
// ============================================
// Sfondo nero pieno, testi bianchi in grassetto (FreeSans Bold della
// libreria TFT_eSPI) e un colore forte per ogni tipo di dato.
// Lo schermo e' 320x240 in orizzontale.

#define UI_BG      0x0000   // nero
#define UI_TILE    0x18E3   // grigio molto scuro (#1C1C1C)
#define UI_LINE    0x4208   // grigio scuro per le linee
#define UI_TEXT    0xFFFF   // bianco
#define UI_MUTED   0xC618   // grigio chiaro (#C0C0C0)
#define UI_GREEN   0x07E0
#define UI_CYAN    0x07FF
#define UI_YELLOW  0xFFE0
#define UI_MAGENTA 0xF81F
#define UI_ORANGE  0xFD20
#define UI_RED     0xF800

#define UI_FONT_S  &FreeSansBold9pt7b
#define UI_FONT_M  &FreeSansBold12pt7b
#define UI_FONT_L  &FreeSansBold18pt7b
#define UI_FONT_XL &FreeSansBold24pt7b

// Riquadri della schermata principale (2 x 2)
const int UI_TILE_W = 151;
const int UI_TILE_H = 82;
const int UI_TILE_X[2] = {6, 163};
const int UI_TILE_Y[2] = {46, 134};

bool uiFramesDrawn = false;
String uiLastValue[4];
String uiLastHeader;
String uiLastFooter;

// 19160 -> "19.160"
String formatThousands(long v) {
  bool neg = v < 0;
  unsigned long n = neg ? -v : v;
  String digits = String(n);
  String out;
  int len = digits.length();
  for (int i = 0; i < len; i++) {
    out += digits[i];
    int left = len - i - 1;
    if (left > 0 && left % 3 == 0) out += '.';
  }
  return neg ? "-" + out : out;
}

// --------------------------------------------
// Icone (16 x 16 circa)
// --------------------------------------------
void uiIconDownload(int x, int y, uint16_t c) {
  tft.fillRect(x + 6, y, 5, 8, c);
  tft.fillTriangle(x + 1, y + 7, x + 15, y + 7, x + 8, y + 13, c);
  tft.fillRect(x, y + 14, 17, 3, c);
}

void uiIconPrint(int x, int y, uint16_t c) {
  tft.fillRect(x + 4, y, 9, 4, c);            // foglio in entrata
  tft.fillRoundRect(x, y + 5, 17, 7, 2, c);   // corpo stampante
  tft.drawRect(x + 3, y + 10, 11, 7, c);      // foglio in uscita (vuoto dentro)
  tft.drawFastHLine(x + 5, y + 13, 7, c);
}

void uiIconBoost(int x, int y, uint16_t c) {
  tft.fillTriangle(x + 10, y, x + 3, y + 10, x + 9, y + 10, c);
  tft.fillTriangle(x + 8, y + 7, x + 14, y + 7, x + 6, y + 17, c);
}

void uiIconHeart(int x, int y, uint16_t c) {
  tft.fillCircle(x + 4, y + 5, 4, c);
  tft.fillCircle(x + 12, y + 5, 4, c);
  tft.fillTriangle(x, y + 6, x + 16, y + 6, x + 8, y + 16, c);
}

void uiIconStar(int x, int y, uint16_t c) {
  int cx = x + 8, cy = y + 9;
  for (int i = 0; i < 5; i++) {
    float a = -PI / 2 + i * 2 * PI / 5;
    float b1 = a - PI / 5, b2 = a + PI / 5;
    tft.fillTriangle(cx + cos(a) * 9, cy + sin(a) * 9,
                     cx + cos(b1) * 4, cy + sin(b1) * 4,
                     cx + cos(b2) * 4, cy + sin(b2) * 4, c);
  }
  tft.fillCircle(cx, cy, 4, c);
}

void uiIconFor(const String &type, int x, int y, uint16_t c) {
  if (type == "download") uiIconDownload(x, y, c);
  else if (type == "print") uiIconPrint(x, y, c);
  else if (type == "boost") uiIconBoost(x, y, c);
  else if (type == "like") uiIconHeart(x, y, c);
  else uiIconStar(x, y, c);
}

// --------------------------------------------
// Schermata principale
// --------------------------------------------
void uiTileInfo(int i, const char *&label, const char *&type, uint16_t &color) {
  switch (i) {
    case 0: label = "Downloads"; type = "download"; color = UI_GREEN; break;
    case 1: label = "Prints";    type = "print";    color = UI_CYAN; break;
    case 2: label = "Boosts";    type = "boost";    color = UI_YELLOW; break;
    default:
      if (saIsEnabled()) { label = "Likes"; type = "like"; color = UI_MAGENTA; }
      else { label = "Points"; type = "points"; color = UI_ORANGE; }
  }
}

void uiDrawTileFrame(int i) {
  const char *label, *type;
  uint16_t color;
  uiTileInfo(i, label, type, color);
  int x = UI_TILE_X[i % 2], y = UI_TILE_Y[i / 2];
  tft.fillRoundRect(x, y, UI_TILE_W, UI_TILE_H, 8, UI_TILE);
  tft.fillRoundRect(x, y, 5, UI_TILE_H, 2, color);
  uiIconFor(type, x + 14, y + 10, color);
  tft.setFreeFont(UI_FONT_S);
  tft.setTextColor(color);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(label, x + 38, y + 19);
}

void uiDrawTileValue(int i, const String &value) {
  int x = UI_TILE_X[i % 2], y = UI_TILE_Y[i / 2];
  tft.fillRect(x + 8, y + 34, UI_TILE_W - 12, 44, UI_TILE);
  tft.setFreeFont(UI_FONT_L);
  if (tft.textWidth(value) > UI_TILE_W - 24) tft.setFreeFont(UI_FONT_M);
  tft.setTextColor(UI_TEXT);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(value, x + 14, y + 57);
}

void uiDrawHeader() {
  String name = strlen(stats.accountName) > 0 ? "@" + String(stats.accountName) : String("MakerWorld");
  bool ok;
  String badge;
  if (saIsEnabled()) {
    String st = saGetStatus();
    ok = st.indexOf("models") >= 0;
    badge = "AUTO";
  } else {
    ok = stats.initialized;
    badge = "LIVE";
  }
  String key = name + badge + (ok ? "1" : "0");
  if (key == uiLastHeader) return;
  uiLastHeader = key;

  tft.fillRect(0, 0, 320, 40, UI_BG);
  tft.setFreeFont(UI_FONT_M);
  tft.setTextColor(UI_TEXT);
  tft.setTextDatum(ML_DATUM);
  while (name.length() > 4 && tft.textWidth(name) > 220) name = name.substring(0, name.length() - 1);
  tft.drawString(name, 10, 20);

  uint16_t c = ok ? UI_GREEN : UI_YELLOW;
  tft.setFreeFont(UI_FONT_S);
  int bw = tft.textWidth(badge) + 34;
  int bx = 314 - bw;
  tft.fillRoundRect(bx, 9, bw, 22, 11, c);
  tft.fillCircle(bx + 12, 20, 4, UI_BG);
  tft.setTextColor(UI_BG);
  tft.drawString(badge, bx + 23, 20);

  tft.drawFastHLine(0, 40, 320, UI_LINE);
}

void uiDrawFooter() {
  String left = "IP " + WiFi.localIP().toString();
  String right;
  uint16_t rightColor = UI_MUTED;
  if (saIsEnabled()) {
    right = saGetStatus();
    if (right.startsWith("AUTO: ")) right = right.substring(6);
    if (right.length() == 0) right = "starting...";
    if (right.indexOf("models") < 0) rightColor = UI_YELLOW;
  } else if (!stats.initialized) {
    right = "Waiting for sync";
    rightColor = UI_YELLOW;
  } else {
    right = "Browser extension";
  }
  String key = left + "|" + right;
  if (key == uiLastFooter) return;
  uiLastFooter = key;

  tft.fillRect(0, 220, 320, 20, UI_BG);
  tft.setTextFont(2);
  tft.setTextColor(UI_MUTED);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(left, 8, 230);
  tft.setTextColor(rightColor);
  tft.setTextDatum(MR_DATUM);
  if (right.length() > 22) right = right.substring(0, 22);
  tft.drawString(right, 312, 230);
}

void showMainScreen() {
  tft.fillScreen(UI_BG);
  uiFramesDrawn = false;
  uiLastHeader = "";
  uiLastFooter = "";
  for (int i = 0; i < 4; i++) uiLastValue[i] = "";
  updateStatsDisplay();
}

void updateStatsDisplay() {
  if (currentNotif.active) return;

  // Riquadro 4 cambiato (Likes <-> Points): si ridisegna la cornice
  static bool lastStandalone = false;
  if (saIsEnabled() != lastStandalone) {
    lastStandalone = saIsEnabled();
    uiFramesDrawn = false;
    uiLastValue[3] = "";
  }

  if (!uiFramesDrawn) {
    for (int i = 0; i < 4; i++) uiDrawTileFrame(i);
    uiFramesDrawn = true;
  }

  String values[4] = {
    formatThousands(stats.totalDownloads),
    formatThousands(stats.totalPrints),
    formatThousands(stats.totalBoosts),
    saIsEnabled() ? formatThousands(stats.totalLikes) : String(stats.totalPoints, 2)
  };
  for (int i = 0; i < 4; i++) {
    if (values[i] != uiLastValue[i]) {
      uiDrawTileValue(i, values[i]);
      uiLastValue[i] = values[i];
    }
  }
  uiDrawHeader();
  uiDrawFooter();
}

void showWaitingForSync() {
  uiDrawFooter();
}

// --------------------------------------------
// Avvio
// --------------------------------------------
void showSplash() {
  tft.fillScreen(UI_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(UI_FONT_L);
  tft.setTextColor(UI_GREEN);
  tft.drawString("MakerWorld", 160, 92);
  tft.setFreeFont(UI_FONT_M);
  tft.setTextColor(UI_TEXT);
  tft.drawString("Live Monitor", 160, 132);
  tft.setTextFont(2);
  tft.setTextColor(UI_MUTED);
  tft.drawString("by @aquascape", 160, 170);
  delay(2000);
}

// --------------------------------------------
// Notifiche
// --------------------------------------------
// Scrive un testo andando a capo tra le parole. Ritorna la y dopo l'ultima riga.
int uiDrawWrapped(const String &text, int x, int y, int maxW, int lineH, int maxLines) {
  String words = text;
  words.trim();
  int lines = 0;
  tft.setTextDatum(TL_DATUM);
  while (words.length() > 0 && lines < maxLines) {
    String line = "";
    int consumed = 0;
    while (consumed < (int)words.length()) {
      int sp = words.indexOf(' ', consumed);
      int end = sp < 0 ? words.length() : sp;
      String candidate = line.length() ? line + " " + words.substring(consumed, end) : words.substring(consumed, end);
      if (tft.textWidth(candidate) > maxW) {
        if (line.length() == 0) {
          // parola da sola troppo lunga: si taglia
          String cut = candidate;
          while (cut.length() > 1 && tft.textWidth(cut) > maxW) cut.remove(cut.length() - 1);
          line = cut;
          consumed += cut.length();
        }
        break;
      }
      line = candidate;
      consumed = end + 1;
    }
    words = consumed < (int)words.length() ? words.substring(consumed) : "";
    words.trim();
    lines++;
    if (lines == maxLines && words.length() > 0) {
      while (line.length() > 1 && tft.textWidth(line + "...") > maxW) line.remove(line.length() - 1);
      line += "...";
    }
    tft.drawString(line, x, y);
    y += lineH;
  }
  return y;
}

void uiNotifHeader(const char *title, const String &iconType, uint16_t color) {
  tft.fillScreen(UI_BG);
  tft.fillRect(0, 0, 320, 44, color);
  uiIconFor(iconType, 12, 13, UI_BG);
  tft.setFreeFont(UI_FONT_M);
  tft.setTextColor(UI_BG);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(title, 40, 22);
}

void uiBlinkBorder(uint16_t color) {
  for (int i = 0; i < 2; i++) {
    tft.drawRect(0, 0, 320, 240, color);
    tft.drawRect(1, 1, 318, 238, color);
    delay(200);
    tft.drawRect(0, 44, 320, 196, UI_BG);
    tft.drawRect(1, 45, 318, 194, UI_BG);
    delay(200);
  }
}

void showNotification() {
  playNotificationSound();
  blinkRed(3, 150);

  const String &type = currentNotif.type;

  if (type == "points") {
    uiNotifHeader("POINTS UPDATE", "points", UI_ORANGE);
    tft.setFreeFont(UI_FONT_XL);
    tft.setTextColor(UI_ORANGE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("+" + String(currentNotif.value, 2), 160, 100);
    tft.setFreeFont(UI_FONT_M);
    tft.setTextColor(UI_TEXT);
    tft.drawString(String(currentNotif.total, 2) + "  >  " + String(currentNotif.points, 2), 160, 160);
    tft.setTextFont(2);
    tft.setTextColor(UI_MUTED);
    tft.drawString("before  >  after", 160, 190);
    uiBlinkBorder(UI_ORANGE);
    blinkRed(2, 100);
    return;
  }

  if (type == "daily_summary") {
    uiNotifHeader("LAST 24 HOURS", "points", UI_CYAN);
    const char *labels[3] = {"Downloads", "Prints", "Points"};
    String vals[3] = {"+" + formatThousands((long)currentNotif.value),
                      "+" + formatThousands((long)currentNotif.total),
                      "+" + String(currentNotif.points, 2)};
    uint16_t cols[3] = {UI_GREEN, UI_CYAN, UI_ORANGE};
    for (int i = 0; i < 3; i++) {
      int y = 78 + i * 52;
      tft.setFreeFont(UI_FONT_M);
      tft.setTextColor(UI_TEXT);
      tft.setTextDatum(ML_DATUM);
      tft.drawString(labels[i], 16, y);
      tft.setFreeFont(UI_FONT_L);
      tft.setTextColor(cols[i]);
      tft.setTextDatum(MR_DATUM);
      tft.drawString(vals[i], 306, y);
    }
    uiBlinkBorder(UI_CYAN);
    blinkRed(2, 100);
    return;
  }

  uint16_t color = UI_GREEN;
  const char *title = "NEW DOWNLOAD";
  if (type == "print") { color = UI_CYAN; title = "NEW PRINT"; }
  else if (type == "boost") { color = UI_YELLOW; title = "NEW BOOST"; }
  uiNotifHeader(title, type, color);

  // Nome del modello: bianco, grassetto, fino a 3 righe
  tft.setFreeFont(UI_FONT_M);
  tft.setTextColor(UI_TEXT);
  uiDrawWrapped(currentNotif.modelName, 12, 56, 296, 26, 3);

  // Valore grande in basso a sinistra, totale a destra
  tft.setFreeFont(UI_FONT_XL);
  tft.setTextColor(color);
  tft.setTextDatum(BL_DATUM);
  tft.drawString("+" + formatThousands((long)currentNotif.value), 12, 214);

  tft.setTextFont(2);
  tft.setTextColor(UI_MUTED);
  tft.setTextDatum(BR_DATUM);
  tft.drawString("TOTAL", 308, 178);
  tft.setFreeFont(UI_FONT_M);
  tft.setTextColor(UI_TEXT);
  tft.drawString(formatThousands((long)currentNotif.total), 308, 208);

  if (type != "boost" && currentNotif.points > 0) {
    uiIconStar(12, 218, UI_ORANGE);
    tft.setFreeFont(UI_FONT_S);
    tft.setTextColor(UI_ORANGE);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("+" + String(currentNotif.points, 0) + " points", 34, 234);
  }

  uiBlinkBorder(color);
  blinkRed(2, 100);
}
