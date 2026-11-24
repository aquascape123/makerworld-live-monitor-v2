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
// Copertina del modello nelle notifiche (disegnata da ui_cover.ino)
const int COVER_X = 168, COVER_Y = 54, COVER_W = 140, COVER_H = 105;
#define UI_GREEN   0x07E0
#define UI_CYAN    0x07FF
#define UI_YELLOW  0xFFE0
#define UI_MAGENTA 0xF81F
#define UI_ORANGE  0xFD20
#define UI_RED     0xF800
#define UI_BLUE    0x547F   // azzurro (#5088FF)
#define UI_PURPLE  0xAB7F   // viola (#AA6EFF)

#define UI_FONT_S  &FreeSansBold9pt7b
#define UI_FONT_M  &FreeSansBold12pt7b
#define UI_FONT_L  &FreeSansBold18pt7b
#define UI_FONT_XL &FreeSansBold24pt7b

// Riquadri della schermata principale: da 1 a 8, scelti in /config.
// La disposizione si adatta al numero: 1-3 in colonna, 4 = 2x2,
// 5-6 = 2x3, 7-8 = 2x4 (piu' piccoli).
enum { ST_DOWNLOADS, ST_PRINTS, ST_BOOSTS, ST_POINTS, ST_FOLLOWERS, ST_LIKES, ST_COLLECTIONS, ST_MODELS, ST_COUNT };
const int UI_AREA_TOP = 46, UI_AREA_BOTTOM = 216, UI_GAP = 6;

bool uiFramesDrawn = false;
String uiLastValue[ST_COUNT];
String uiLastLayout;
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

void uiIconPerson(int x, int y, uint16_t c) {
  tft.fillCircle(x + 8, y + 4, 4, c);
  tft.fillRoundRect(x + 1, y + 10, 15, 7, 3, c);
}

void uiIconBookmark(int x, int y, uint16_t c) {
  tft.fillRect(x + 3, y, 11, 17, c);
  tft.fillTriangle(x + 4, y + 17, x + 12, y + 17, x + 8, y + 11, UI_TILE);
}

void uiIconBox(int x, int y, uint16_t c) {
  tft.drawRect(x + 1, y + 3, 15, 14, c);
  tft.drawRect(x + 2, y + 4, 13, 12, c);
  tft.fillTriangle(x + 1, y + 3, x + 16, y + 3, x + 8, y - 1, c);
}

void uiIconFor(const String &type, int x, int y, uint16_t c) {
  if (type == "download") uiIconDownload(x, y, c);
  else if (type == "print") uiIconPrint(x, y, c);
  else if (type == "boost") uiIconBoost(x, y, c);
  else if (type == "like") uiIconHeart(x, y, c);
  else if (type == "follower") uiIconPerson(x, y, c);
  else if (type == "collection") uiIconBookmark(x, y, c);
  else if (type == "model") uiIconBox(x, y, c);
  else uiIconStar(x, y, c);
}

// --------------------------------------------
// Schermata principale
// --------------------------------------------
const char *UI_STAT_NAMES[ST_COUNT] = {"Downloads", "Prints", "Boosts", "Points",
                                       "Followers", "Likes", "Collections", "Models"};

// Elenco dei riquadri da mostrare, nell'ordine scelto
int uiActiveTiles(uint8_t *out) {
  int n = 0;
  if (uiTilesCfg.length() > 0) {
    int start = 0;
    while (start < (int)uiTilesCfg.length() && n < ST_COUNT) {
      int comma = uiTilesCfg.indexOf(',', start);
      if (comma < 0) comma = uiTilesCfg.length();
      int id = uiTilesCfg.substring(start, comma).toInt();
      if (id >= 0 && id < ST_COUNT) out[n++] = id;
      start = comma + 1;
    }
  }
  if (n == 0) {   // di serie: come prima (Likes, oppure Points con l'accesso)
    out[0] = ST_DOWNLOADS; out[1] = ST_PRINTS; out[2] = ST_BOOSTS;
    out[3] = uiFourthIsPoints() ? ST_POINTS : ST_LIKES;
    n = 4;
  }
  return n;
}

void uiTileInfo(int id, const char *&label, const char *&type, uint16_t &color) {
  label = UI_STAT_NAMES[id];
  switch (id) {
    case ST_DOWNLOADS:   type = "download";   color = UI_GREEN; break;
    case ST_PRINTS:      type = "print";      color = UI_CYAN; break;
    case ST_BOOSTS:      type = "boost";      color = UI_YELLOW; break;
    case ST_POINTS:      type = "points";     color = UI_ORANGE; break;
    case ST_FOLLOWERS:   type = "follower";   color = UI_BLUE; break;
    case ST_LIKES:       type = "like";       color = UI_MAGENTA; break;
    case ST_COLLECTIONS: type = "collection"; color = UI_PURPLE; break;
    default:             type = "model";      color = UI_TEXT; break;
  }
}

String uiTileValue(int id) {
  switch (id) {
    case ST_DOWNLOADS:   return formatThousands(stats.totalDownloads);
    case ST_PRINTS:      return formatThousands(stats.totalPrints);
    case ST_BOOSTS:      return formatThousands(stats.totalBoosts);
    case ST_POINTS:
      if (saIsEnabled() && !mwLoggedIn()) return "login";
      if (saIsEnabled() && !mwPointsKnown()) return "...";
      return formatPoints(stats.totalPoints);
    case ST_FOLLOWERS:   return stats.totalFollowers < 0 ? String("...") : formatThousands(stats.totalFollowers);
    case ST_LIKES:       return formatThousands(stats.totalLikes);
    case ST_COLLECTIONS: return formatThousands(stats.totalCollections);
    default:             return formatThousands(stats.totalModels);
  }
}

// Posizione e misure del riquadro numero `pos` su `n`
void uiTileBox(int pos, int n, int &x, int &y, int &w, int &h) {
  int cols = n <= 3 ? 1 : 2;
  int rows = (n + cols - 1) / cols;
  w = (320 - 2 * UI_GAP - (cols - 1) * UI_GAP) / cols;
  h = (UI_AREA_BOTTOM - UI_AREA_TOP - (rows - 1) * UI_GAP) / rows;
  x = UI_GAP + (pos % cols) * (w + UI_GAP);
  y = UI_AREA_TOP + (pos / cols) * (h + UI_GAP);
}

// 3 stili: grande (nome sopra, numero grande), medio, piccolo (nome e numero su due righe strette)
void uiDrawTileFrame(int pos, int n, int id) {
  const char *label, *type;
  uint16_t color;
  uiTileInfo(id, label, type, color);
  int x, y, w, h;
  uiTileBox(pos, n, x, y, w, h);
  tft.fillRoundRect(x, y, w, h, 8, UI_TILE);
  tft.fillRoundRect(x, y, 5, h, 2, color);
  if (w > 200 && h < 70) {   // riga larga: nome a sinistra, numero grande a destra
    uiIconFor(type, x + 14, y + h / 2 - 9, color);
    tft.setFreeFont(UI_FONT_M);
    tft.setTextColor(color);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(label, x + 40, y + h / 2);
  } else if (h >= 48) {
    int iy = h >= 70 ? 10 : 6;
    uiIconFor(type, x + 14, y + iy, color);
    tft.setFreeFont(UI_FONT_S);
    tft.setTextColor(color);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(label, x + 38, y + iy + 9);
  } else {
    tft.setTextFont(2);
    tft.setTextColor(color);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(label, x + 12, y + 3);
  }
}

void uiDrawTileValue(int pos, int n, const String &value) {
  int x, y, w, h;
  uiTileBox(pos, n, x, y, w, h);
  if (w > 200 && h < 70) {
    tft.fillRect(x + 172, y + 4, w - 178, h - 8, UI_TILE);   // il nome finisce prima (Collections ~ x+165)
    tft.setFreeFont(UI_FONT_L);
    if (tft.textWidth(value) > w - 190) tft.setFreeFont(UI_FONT_M);
    tft.setTextColor(UI_TEXT);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(value, x + w - 14, y + h / 2);
  } else if (h >= 48) {
    int top = h >= 70 ? 34 : 27;
    int ah = h - top - 3;
    tft.fillRect(x + 8, y + top, w - 12, ah, UI_TILE);
    // il carattere piu' grande che sta nel riquadro
    if (ah >= 44 && w > 200) tft.setFreeFont(UI_FONT_XL);
    else if (ah >= 30) tft.setFreeFont(UI_FONT_L);
    else tft.setFreeFont(UI_FONT_M);
    if (tft.textWidth(value) > w - 24) tft.setFreeFont(UI_FONT_M);
    if (tft.textWidth(value) > w - 24) tft.setFreeFont(UI_FONT_S);
    tft.setTextColor(UI_TEXT);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(value, x + 14, y + top + ah / 2);
  } else {
    tft.fillRect(x + 8, y + 19, w - 12, h - 20, UI_TILE);
    tft.setFreeFont(UI_FONT_M);
    if (tft.textWidth(value) > w - 20) tft.setFreeFont(UI_FONT_S);
    tft.setTextColor(UI_TEXT);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(value, x + w - 8, y + h - 11);
  }
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
  updateStatsDisplay();
}

void updateStatsDisplay() {
  if (currentNotif.active) return;

  uint8_t tiles[ST_COUNT];
  int n = uiActiveTiles(tiles);

  // Riquadri cambiati (scelta in /config, Likes <-> Points): si ridisegna tutto
  String layout = "";
  for (int i = 0; i < n; i++) layout += String(tiles[i]) + ",";
  if (layout != uiLastLayout) {
    uiLastLayout = layout;
    if (uiFramesDrawn) tft.fillRect(0, UI_AREA_TOP - 2, 320, UI_AREA_BOTTOM - UI_AREA_TOP + 4, UI_BG);
    uiFramesDrawn = false;
  }

  if (!uiFramesDrawn) {
    for (int i = 0; i < n; i++) uiDrawTileFrame(i, n, tiles[i]);
    for (int i = 0; i < ST_COUNT; i++) uiLastValue[i] = "";   // la cornice cancella anche i numeri
    uiFramesDrawn = true;
  }

  for (int i = 0; i < n; i++) {
    String v = uiTileValue(tiles[i]);
    if (v != uiLastValue[i]) {
      uiDrawTileValue(i, n, v);
      uiLastValue[i] = v;
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
    tft.drawString("+" + formatPoints(currentNotif.value), 160, 100);
    tft.setFreeFont(UI_FONT_M);
    tft.setTextColor(UI_TEXT);
    tft.drawString(formatPoints(currentNotif.total) + "  >  " + formatPoints(currentNotif.points), 160, 160);
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
  else if (type == "follower") { color = UI_BLUE; title = currentNotif.value > 1 ? "NEW FOLLOWERS" : "NEW FOLLOWER"; }
  uiNotifHeader(title, type, color);

  // Nome del modello: bianco, grassetto. Con la foto sta nella colonna di
  // sinistra (fino a 4 righe), senza foto usa tutta la larghezza.
  bool withCover = currentNotif.coverUrl.length() > 0;
  tft.setFreeFont(UI_FONT_M);
  tft.setTextColor(UI_TEXT);
  if (withCover) uiDrawWrapped(currentNotif.modelName, 12, 56, COVER_X - 18, 26, 4);
  else uiDrawWrapped(currentNotif.modelName, 12, 56, 296, 26, 3);

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

  // La foto arriva per ultima (circa 1 secondo): il testo e' gia' visibile
  if (withCover) coverDraw(currentNotif.coverUrl);

  uiBlinkBorder(color);
  blinkRed(2, 100);
}

// Riquadro 4: Points con l'estensione o con l'accesso MakerWorld, altrimenti Likes
bool uiFourthIsPoints() {
  return !saIsEnabled() || mwLoggedIn();
}

// Punti: interi senza decimali (156), altrimenti con due (38.75)
String formatPoints(float p) {
  long whole = (long)(p + (p >= 0 ? 0.005f : -0.005f));
  if (fabsf(p - whole) < 0.005f) return formatThousands(whole);
  return String(p, 2);
}

// --------------------------------------------
// Riquadri della schermata principale (scelta in /config)
// --------------------------------------------
String saTilesSection() {
  uint8_t tiles[ST_COUNT];
  int n = uiActiveTiles(tiles);
  String h = F("<label>Display tiles</label><small>Choose what to show and in which position "
               "(1-3 tiles: big, 4: like now, 5-6 and 7-8: smaller). Points need the MakerWorld login below.</small>"
               "<table style='width:100%;margin-top:6px'>");
  for (int id = 0; id < ST_COUNT; id++) {
    int pos = 0;
    for (int i = 0; i < n; i++) if (tiles[i] == id) pos = i + 1;
    h += "<tr><td>" + String(UI_STAT_NAMES[id]) + "</td><td style='text-align:right'><select name='t" + String(id) +
         "' style='padding:6px;border-radius:6px;font-size:1em'>";
    for (int p = 0; p <= ST_COUNT; p++) {
      h += "<option value='" + String(p) + "'" + (p == pos ? " selected" : "") + ">" + (p == 0 ? String("hidden") : String(p)) + "</option>";
    }
    h += "</select></td></tr>";
  }
  h += F("</table>");
  return h;
}

void saReadTilesForm() {
  if (!server.hasArg("t0")) return;   // vecchio modulo senza la scelta: non si tocca
  String out = "";
  for (int p = 1; p <= ST_COUNT; p++) {          // per posizione...
    for (int id = 0; id < ST_COUNT; id++) {      // ...e a parita' di posizione, nell'ordine dell'elenco
      if (server.arg("t" + String(id)).toInt() == p) out += String(id) + ",";
    }
  }
  if (out.endsWith(",")) out.remove(out.length() - 1);
  // Uguale alla scelta di serie: si lascia "di serie" (cosi' Likes/Points cambia da solo con l'accesso)
  String saved = uiTilesCfg;
  uiTilesCfg = "";
  uint8_t def[ST_COUNT];
  int dn = uiActiveTiles(def);
  String defStr = "";
  for (int i = 0; i < dn; i++) defStr += (i ? "," : "") + String(def[i]);
  uiTilesCfg = (out.length() == 0 || out == defStr) ? "" : out;
  if (out.length() == 0) Serial.println("Tiles: none chosen, back to default");
  (void)saved;
}
