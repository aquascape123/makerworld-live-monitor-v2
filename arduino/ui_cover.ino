// ============================================
// COPERTINA DEL MODELLO NELLE NOTIFICHE
// ============================================
// MakerWorld rimpicciolisce l'immagine per noi (circa 15 KB in PNG): il CYD la
// scarica, la decodifica con PNGdec e la disegna riga per riga a destra del
// nome. Se qualcosa va storto la notifica resta, solo senza foto.

// posizione e misure: COVER_X/Y/W/H in ui.ino
const int COVER_MAX_BYTES = 48000;

PNG *coverPng = nullptr;
int coverDrawX = 0, coverDrawY = 0, coverLastY = -1, coverW = 0;

// Ritocco della foto: il pannello del CYD rende le immagini piu' chiare e
// morbide del file originale. gamma > 1 scurisce i mezzitoni, contrast > 1
// aumenta il contrasto, sat > 1 ravviva i colori, sharp = nitidezza (0 = spenta).
struct CoverFx { float gamma; float contrast; float sat; float sharp; };
CoverFx coverFx = {1.4f, 1.2f, 1.3f, 0.8f};   // scelta guardando il CYD 2.4 (versione "D")
uint8_t coverLut[256];
int16_t coverRows[3][COVER_W * 3];   // ultime 3 righe (RGB), per la nitidezza

void coverPrepareLut() {
  for (int i = 0; i < 256; i++) {
    float v = i / 255.0f;
    v = powf(v, coverFx.gamma);
    v = (v - 0.5f) * coverFx.contrast + 0.5f;
    coverLut[i] = (uint8_t)constrain((int)(v * 255.0f + 0.5f), 0, 255);
  }
}

void coverPushRow(int y) {
  int16_t *c = coverRows[y % 3];
  int16_t *u = coverRows[(y > 0 ? y - 1 : y) % 3];
  int16_t *d = coverRows[(y < coverLastY ? y + 1 : y) % 3];
  uint16_t out[COVER_W];
  for (int x = 0; x < coverW; x++) {
    int px[3];
    for (int k = 0; k < 3; k++) {
      int i = x * 3 + k;
      int v = c[i];
      if (coverFx.sharp > 0) {
        int l = x > 0 ? c[i - 3] : v;
        int r = x < coverW - 1 ? c[i + 3] : v;
        v = v + (int)(coverFx.sharp * (4 * v - l - r - u[i] - d[i]) / 4);
      }
      px[k] = constrain(v, 0, 255);
    }
    uint16_t p = ((px[0] & 0xF8) << 8) | ((px[1] & 0xFC) << 3) | (px[2] >> 3);
    out[x] = (p >> 8) | (p << 8);   // byte alto per primo, come vuole il display
  }
  tft.pushImage(coverDrawX, coverDrawY + y, coverW, 1, out);
}

int coverDrawLine(PNGDRAW *pDraw) {
  uint16_t line[COVER_W];
  coverPng->getLineAsRGB565(pDraw, line, PNG_RGB565_LITTLE_ENDIAN, 0x000000);  // trasparenze su nero
  int16_t *row = coverRows[pDraw->y % 3];
  for (int x = 0; x < coverW; x++) {
    uint16_t p = line[x];
    int r = coverLut[((p >> 11) & 0x1F) * 255 / 31];
    int g = coverLut[((p >> 5) & 0x3F) * 255 / 63];
    int b = coverLut[(p & 0x1F) * 255 / 31];
    if (coverFx.sat != 1.0f) {
      int gray = (r * 77 + g * 150 + b * 29) >> 8;
      r = constrain(gray + (int)((r - gray) * coverFx.sat), 0, 255);
      g = constrain(gray + (int)((g - gray) * coverFx.sat), 0, 255);
      b = constrain(gray + (int)((b - gray) * coverFx.sat), 0, 255);
    }
    row[x * 3] = r;
    row[x * 3 + 1] = g;
    row[x * 3 + 2] = b;
  }
  // con la nitidezza serve anche la riga sotto: si disegna quella precedente
  if (coverFx.sharp <= 0) coverPushRow(pDraw->y);
  else if (pDraw->y > 0) coverPushRow(pDraw->y - 1);
  return 1;
}

// Scarica la copertina rimpicciolita da MakerWorld. Il buffer va liberato con free().
uint8_t *coverDownload(const String &url, int &got) {
  got = 0;
  if (url.length() == 0 || WiFi.status() != WL_CONNECTED) return nullptr;
  String u = url + (url.indexOf('?') < 0 ? "?" : "&") +
             "x-oss-process=image/resize,w_" + String(COVER_W) + ",h_" + String(COVER_H) + "/format,png";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(8000);
  http.setUserAgent(SA_USER_AGENT);
  if (!http.begin(client, u)) return nullptr;
  int code = http.GET();
  int len = http.getSize();
  if (code != 200 || len > COVER_MAX_BYTES) {
    Serial.printf("[COVER] HTTP %d, size %d: skipped\n", code, len);
    http.end();
    return nullptr;
  }
  int cap = len > 0 ? len : COVER_MAX_BYTES;
  uint8_t *buf = (uint8_t *)malloc(cap);
  if (!buf) { http.end(); Serial.println("[COVER] no memory"); return nullptr; }
  WiFiClient *s = http.getStreamPtr();
  unsigned long tr = millis();
  while (got < cap && millis() - tr < 8000) {
    int a = s->available();
    if (a > 0) got += s->readBytes(buf + got, min(a, cap - got));
    else if (!http.connected()) break;
    else delay(5);
  }
  http.end();
  if ((len > 0 && got != len) || (len <= 0 && got >= cap) || got == 0) {
    Serial.printf("[COVER] download incomplete (%d of %d)\n", got, len);
    free(buf);
    return nullptr;
  }
  return buf;
}

// Disegna il PNG gia' scaricato, centrato nel riquadro bx, by (COVER_W x COVER_H)
bool coverRender(uint8_t *buf, int len, int bx, int by) {
  bool ok = false;
  coverPrepareLut();
  coverPng = new (std::nothrow) PNG();
  if (coverPng && coverPng->openRAM(buf, len, coverDrawLine) == PNG_SUCCESS) {
    int w = coverPng->getWidth(), h = coverPng->getHeight();
    if (w <= COVER_W && h <= COVER_H) {
      coverW = w;
      coverLastY = h - 1;
      coverDrawX = bx + (COVER_W - w) / 2;
      coverDrawY = by + (COVER_H - h) / 2;
      ok = coverPng->decode(nullptr, 0) == PNG_SUCCESS;
      if (ok && coverFx.sharp > 0) coverPushRow(h - 1);   // ultima riga
      if (ok) tft.drawRect(coverDrawX - 1, coverDrawY - 1, w + 2, h + 2, UI_MUTED);
    }
    coverPng->close();
  }
  delete coverPng;
  coverPng = nullptr;
  return ok;
}

bool coverDraw(const String &url) {
  unsigned long t0 = millis();
  int got;
  uint8_t *buf = coverDownload(url, got);
  if (!buf) return false;
  bool ok = coverRender(buf, got, COVER_X, COVER_Y);
  Serial.printf("[COVER] %s, %d bytes, %lu ms, heap %u\n", ok ? "ok" : "decode failed", got, millis() - t0, ESP.getFreeHeap());
  free(buf);
  return ok;
}
