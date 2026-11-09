#include <WiFi.h>
#include <WebServer.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include "standalone.h"

// ============ SCHEDA ============
// Default: ESP32-2432S028R (CYD 2.8"), retroilluminazione sul GPIO 21.
// Per il CYD 2.4" (ESP32-2432S024) compilare con -DCYD_24: retroilluminazione sul GPIO 27.
#ifdef CYD_24
  #define BACKLIGHT_PIN 27
  #define SCREEN_ROTATION 1   // sul 2.4" lo schermo e' montato capovolto rispetto al 2.8"
#else
  #define BACKLIGHT_PIN 21
  #define SCREEN_ROTATION 3
#endif

// ============ PIN LED RGB CYD-R ============
#define LED_R 4
#define LED_G 16
#define LED_B 17

// ============ PIN BUZZER ============
#define BUZZER_PIN 22

// ============ OGGETTI ============
TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
Preferences prefs;

// ============ VARIABILI GLOBALI ============
struct Stats {
  int totalDownloads = 0;
  int totalPrints = 0;
  int totalBoosts = 0;
  float totalPoints = 0.0;  // CAMBIATO DA int A float
  long totalLikes = 0;       // usato dalla modalita' autonoma (i punti li' non ci sono)
  char accountName[32] = "";  // AGGIUNTO: Nome account (max 31 caratteri)
  unsigned long lastNotification = 0;
  bool initialized = false;
} stats;

struct Notification {
  String type;
  String modelName;
  float value;    // CAMBIATO DA int A float
  float total;    // CAMBIATO DA int A float
  float points;   // CAMBIATO DA int A float
  unsigned long timestamp;
  bool active = false;
} currentNotif;

// ============ CODA NOTIFICHE ============
// Prima c'era un solo "slot": se arrivavano piu' notifiche insieme ognuna
// copriva la precedente e si vedeva solo l'ultima. Ora vengono messe in coda
// e mostrate una dopo l'altra.
const int NOTIF_QUEUE_SIZE = 10;
Notification notifQueue[NOTIF_QUEUE_SIZE];
int notifQueueHead = 0;   // prossima da mostrare
int notifQueueCount = 0;

unsigned long lastStatsUpdate = 0;
unsigned long lastSyncAttempt = 0;
const unsigned long STATS_UPDATE_INTERVAL = 5000;
const unsigned long NOTIF_DISPLAY_TIME = 15000;      // se non ce ne sono altre in attesa
const unsigned long NOTIF_DISPLAY_TIME_BUSY = 5000;  // se altre notifiche aspettano
const unsigned long SYNC_TIMEOUT = 30000;

// ============ COLORI MODERNI ============
#define COLOR_BG_TOP      0x0000
#define COLOR_BG_BOTTOM   0x0000
#define COLOR_CARD_BG     0x2945
#define COLOR_PRIMARY     0x07FF
#define COLOR_SUCCESS     0x07E0
#define COLOR_WARNING     0xFFE0
#define COLOR_DANGER      0xF81F
#define COLOR_TEXT        0xFFFF
#define COLOR_TEXT_DIM    0xC618
#define COLOR_ACCENT      0x07E0

// ============================================
// FUNZIONI BUZZER
// ============================================
void setupBuzzer() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
}

void playBeep(int frequency, int duration) {
  int period = 1000000L / frequency;
  int pulseWidth = period / 2;
  long cycles = frequency * duration / 1000;
  
  for (long i = 0; i < cycles; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delayMicroseconds(pulseWidth);
    digitalWrite(BUZZER_PIN, LOW);
    delayMicroseconds(pulseWidth);
  }
}

void playNotificationSound() {
  playBeep(523, 150);
  delay(50);
  playBeep(659, 150);
  delay(50);
  playBeep(784, 200);
}

void playSuccessSound() {
  playBeep(523, 200);
  delay(100);
  playBeep(784, 300);
}

void playErrorSound() {
  playBeep(392, 500);
}

void playStartupSound() {
  playBeep(523, 200);
  delay(150);
  playBeep(659, 200);
  delay(150);
  playBeep(784, 200);
  delay(150);
  playBeep(1047, 300);
}

// ============================================
// SALVATAGGIO E CARICAMENTO STATS
// ============================================
void saveStats() {
  prefs.begin("makerworld", false);
  prefs.putInt("downloads", stats.totalDownloads);
  prefs.putInt("prints", stats.totalPrints);
  prefs.putInt("boosts", stats.totalBoosts);
  prefs.putFloat("points", stats.totalPoints);  // CAMBIATO putInt in putFloat
  prefs.putLong("likes", stats.totalLikes);
  prefs.putString("accountName", String(stats.accountName));  // AGGIUNTO: Salva nome account
  prefs.putBool("initialized", stats.initialized);
  prefs.end();
  Serial.println("Stats saved to flash");
  mqttMarkDirty();  // Home Assistant: ripubblica i totali
}

void loadStats() {
  prefs.begin("makerworld", true);
  stats.totalDownloads = prefs.getInt("downloads", 0);
  stats.totalPrints = prefs.getInt("prints", 0);
  stats.totalBoosts = prefs.getInt("boosts", 0);
  stats.totalPoints = prefs.getFloat("points", 0.0);  // CAMBIATO getInt in getFloat
  stats.totalLikes = prefs.getLong("likes", 0);
  
  // AGGIUNTO: Carica nome account
  String accountNameStr = prefs.getString("accountName", "");
  if (accountNameStr.length() > 0) {
    strncpy(stats.accountName, accountNameStr.c_str(), sizeof(stats.accountName) - 1);
    stats.accountName[sizeof(stats.accountName) - 1] = '\0';
  }
  
  stats.initialized = prefs.getBool("initialized", false);
  prefs.end();
  
  Serial.println("Stats loaded from flash:");
  Serial.print("Account: "); Serial.println(stats.accountName);
  Serial.print("Downloads: "); Serial.println(stats.totalDownloads);
  Serial.print("Prints: "); Serial.println(stats.totalPrints);
  Serial.print("Boosts: "); Serial.println(stats.totalBoosts);
  Serial.print("Points: "); Serial.println(stats.totalPoints, 2);  // AGGIUNTO , 2 per mostrare 2 decimali
  Serial.print("Initialized: "); Serial.println(stats.initialized ? "YES" : "NO");
}

// ============================================
// GESTIONE LED RGB
// ============================================
void setupLED() {
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  
  digitalWrite(LED_R, HIGH);
  digitalWrite(LED_G, HIGH);
  digitalWrite(LED_B, HIGH);
}

void setLEDColor(bool red, bool green, bool blue) {
  digitalWrite(LED_R, !red);
  digitalWrite(LED_G, !green);
  digitalWrite(LED_B, !blue);
}

void ledGreen() {
  setLEDColor(false, true, false);
}

void ledRed() {
  setLEDColor(true, false, false);
}

void ledOff() {
  setLEDColor(false, false, false);
}

void blinkRed(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    ledRed();
    delay(delayMs);
    ledOff();
    delay(delayMs);
  }
  ledGreen();
}

// ============================================
// FUNZIONI GRAFICHE AVANZATE
// ============================================
void drawGradient(int x, int y, int w, int h, uint16_t colorTop, uint16_t colorBottom) {
  for (int i = 0; i < h; i++) {
    float ratio = (float)i / h;
    
    uint8_t r1 = (colorTop >> 11) & 0x1F;
    uint8_t g1 = (colorTop >> 5) & 0x3F;
    uint8_t b1 = colorTop & 0x1F;
    
    uint8_t r2 = (colorBottom >> 11) & 0x1F;
    uint8_t g2 = (colorBottom >> 5) & 0x3F;
    uint8_t b2 = colorBottom & 0x1F;
    
    uint8_t r = r1 + (r2 - r1) * ratio;
    uint8_t g = g1 + (g2 - g1) * ratio;
    uint8_t b = b1 + (b2 - b1) * ratio;
    
    uint16_t color = (r << 11) | (g << 5) | b;
    tft.drawFastHLine(x, y + i, w, color);
  }
}

void drawCard(int x, int y, int w, int h, uint16_t color) {
  tft.fillRoundRect(x + 2, y + 2, w, h, 8, 0x18E3);
  tft.fillRoundRect(x, y, w, h, 8, color);
}

void drawDownloadIcon(int x, int y, uint16_t color) {
  tft.fillTriangle(x+5, y+8, x+10, y+3, x+15, y+8, color);
  tft.fillRect(x+9, y, 3, 8, color);
  tft.drawFastHLine(x+3, y+10, 15, color);
}

void drawPrintIcon(int x, int y, uint16_t color) {
  tft.fillRect(x+3, y+3, 15, 6, color);
  tft.fillRect(x+5, y, 11, 4, color);
  tft.drawRect(x+3, y+3, 15, 6, COLOR_BG_TOP);
}

void drawBoostIcon(int x, int y, uint16_t color) {
  tft.fillTriangle(x+10, y, x+6, y+6, x+10, y+6, color);
  tft.fillTriangle(x+10, y+6, x+14, y+6, x+10, y+12, color);
}

void drawStarIcon(int x, int y, uint16_t color) {
  for(int i = 0; i < 5; i++) {
    float angle = -PI/2 + (2 * PI * i / 5);
    int x1 = x + 6 + cos(angle) * 6;
    int y1 = y + 6 + sin(angle) * 6;
    tft.fillCircle(x1, y1, 2, color);
  }
  tft.fillCircle(x+6, y+6, 3, color);
}

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== MakerWorld Monitor CYD-R ===");

  loadStats();
  saLoadConfig();
  mqttLoadConfig();

  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);
  
  setupLED();
  setupBuzzer();
  ledOff();
  
  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.fillScreen(COLOR_BG_TOP);
  tft.setTextColor(COLOR_TEXT, COLOR_BG_TOP);
  
  playStartupSound();
  
  showSplash();
  connectWiFi();
  
  server.on("/notify", HTTP_POST, handleNotification);
  server.on("/ping", HTTP_GET, handlePing);
  server.on("/stats", HTTP_GET, handleStats);
  server.on("/sync", HTTP_POST, handleSync);
  server.on("/resync", HTTP_GET, handleResync);
  server.on("/debug", HTTP_GET, handleDebug);
  server.on("/test", HTTP_GET, handleTestNotification);
  server.on("/reset", HTTP_GET, handleReset);
  server.on("/reset-wifi", HTTP_GET, []() {
    server.send(200, "text/html", 
      "<html><body style='font-family:Arial;text-align:center;padding:50px;'>"
      "<h1>WiFi Reset</h1>"
      "<p>Device will restart in setup mode...</p>"
      "</body></html>");
    delay(1000);
    resetWiFiSettings();
  });
  saRegisterRoutes();
  mqttRegisterRoutes();  // pagina /mqtt per Home Assistant  // pagina /config della modalita' autonoma
  server.on("/screenshot", HTTP_GET, handleScreenshot);
  server.begin();
  
  Serial.println("Server started!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  showMainScreen();
  lastSyncAttempt = millis();
}

// ============================================
// LOOP
// ============================================
void loop() {
  server.handleClient();
  
  unsigned long now = millis();
  
  if (currentNotif.active) {
    unsigned long displayTime = notifQueueCount > 0 ? NOTIF_DISPLAY_TIME_BUSY : NOTIF_DISPLAY_TIME;
    if (now - currentNotif.timestamp > displayTime) {
      currentNotif.active = false;
      if (notifQueueCount == 0) {
        showMainScreen();
      }
    }
  } else if (notifQueueCount > 0) {
    showNextNotification();
  } else {
    if (now - lastStatsUpdate > STATS_UPDATE_INTERVAL) {
      updateStatsDisplay();
      lastStatsUpdate = now;
    }
    
    if (!stats.initialized && !saIsEnabled() && (now - lastSyncAttempt > SYNC_TIMEOUT)) {
      showWaitingForSync();
      lastSyncAttempt = now;
    }
  }

  mqttLoop();  // Home Assistant: collegamento e pubblicazione
  saLoop();  // modalita' autonoma: controlla MakerWorld quando e' il momento

}

// WIFI CONNECTION CON WIFIMANAGER
// ============================================
void connectWiFi() {
  drawGradient(0, 0, 320, 240, COLOR_BG_TOP, COLOR_BG_BOTTOM);
  
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(30, 60);
  tft.println("WiFi Setup");
  
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT_DIM);
  tft.setCursor(20, 90);
  tft.println("1. Connect to WiFi:");
  tft.setTextColor(COLOR_ACCENT);
  tft.setCursor(20, 105);
  tft.println("   'MakerWorld-Setup'");
  
  tft.setTextColor(COLOR_TEXT_DIM);
  tft.setCursor(20, 125);
  tft.println("2. Open browser");
  
  tft.setCursor(20, 140);
  tft.println("3. Enter your WiFi");
  
  WiFiManager wm;
  
  wm.setAPCallback([](WiFiManager *myWiFiManager) {
    tft.fillRect(0, 160, 320, 40, COLOR_BG_BOTTOM);
    tft.setTextColor(COLOR_WARNING);
    tft.setCursor(40, 170);
    tft.println("Setup mode active!");
  });
  
  wm.setConfigPortalTimeout(180);
  
  bool connected = wm.autoConnect("MakerWorld-Setup");
  
  if (connected) {
    ledGreen();
    
    drawGradient(0, 0, 320, 240, COLOR_BG_TOP, COLOR_BG_BOTTOM);
    
    tft.setTextSize(2);
    tft.setTextColor(COLOR_SUCCESS);
    tft.setCursor(60, 80);
    tft.println("WiFi Connected!");
    
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(90, 110);
    tft.print("SSID: ");
    tft.println(WiFi.SSID());
    
    tft.setCursor(90, 125);
    tft.print("IP: ");
    tft.println(WiFi.localIP());
    
    if (!stats.initialized) {
      tft.setTextColor(COLOR_WARNING);
      tft.setCursor(70, 155);
      tft.println("Waiting for sync...");
    }
    
    delay(3000);
  } else {
    tft.fillScreen(COLOR_BG_TOP);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_DANGER);
    tft.setCursor(50, 100);
    tft.println("Setup Failed!");
    
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(60, 130);
    tft.println("Restarting...");
    
    delay(3000);
    ESP.restart();
  }
}

void resetWiFiSettings() {
  WiFiManager wm;
  wm.resetSettings();
  ESP.restart();
}

// Schermate (avvio, principale, notifiche): vedi ui.ino

// ============================================
// CODA NOTIFICHE
// ============================================
// Il font del display conosce solo i caratteri ASCII: lettere accentate e
// simboli tipografici (– ’ “) vengono convertiti, gli altri (emoji...) tolti.
String toDisplayText(const String &in) {
  String out;
  const char *s = in.c_str();
  size_t len = in.length();
  for (size_t i = 0; i < len; ) {
    uint8_t c = (uint8_t)s[i];
    if (c < 0x80) { out += (char)c; i++; continue; }
    // Latin-1 (C3 xx): lettere accentate
    if (c == 0xC3 && i + 1 < len) {
      uint8_t c2 = (uint8_t)s[i + 1];
      static const char *map = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
      if (c2 >= 0x80 && c2 <= 0xBF) out += map[c2 - 0x80];
      i += 2;
      continue;
    }
    // Punteggiatura tipografica (E2 80 xx)
    if (c == 0xE2 && i + 2 < len && (uint8_t)s[i + 1] == 0x80) {
      uint8_t c3 = (uint8_t)s[i + 2];
      if (c3 == 0x93 || c3 == 0x94) out += '-';
      else if (c3 == 0x98 || c3 == 0x99) out += (char)39;  // apostrofo
      else if (c3 == 0x9C || c3 == 0x9D) out += '"';
      else if (c3 == 0xA6) out += "...";
      i += 3;
      continue;
    }
    // Altri caratteri multibyte: si saltano
    int skip = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
    i += skip;
  }
  out.trim();
  return out;
}

void enqueueNotification(const Notification &original) {
  mqttPublishEvent(original);  // Home Assistant: evento per le automazioni
  Notification n = original;
  n.modelName = toDisplayText(original.modelName);
  if (notifQueueCount == NOTIF_QUEUE_SIZE) {
    // Coda piena: si scarta la piu' vecchia (i totali sono comunque aggiornati)
    notifQueueHead = (notifQueueHead + 1) % NOTIF_QUEUE_SIZE;
    notifQueueCount--;
    Serial.println("Notification queue full, oldest dropped");
  }
  int tail = (notifQueueHead + notifQueueCount) % NOTIF_QUEUE_SIZE;
  notifQueue[tail] = n;
  notifQueueCount++;
  Serial.print("Notification queued, waiting: ");
  Serial.println(notifQueueCount);
}

void showNextNotification() {
  if (notifQueueCount == 0) return;
  currentNotif = notifQueue[notifQueueHead];
  notifQueueHead = (notifQueueHead + 1) % NOTIF_QUEUE_SIZE;
  notifQueueCount--;
  Serial.print("Showing notification: ");
  Serial.print(currentNotif.type);
  Serial.print(" - ");
  Serial.print(currentNotif.modelName);
  Serial.print(" (still waiting: ");
  Serial.print(notifQueueCount);
  Serial.println(")");
  showNotification();
  // Il tempo parte da quando la notifica e' davvero visibile
  currentNotif.timestamp = millis();
  currentNotif.active = true;
}

void clearNotificationQueue() {
  notifQueueHead = 0;
  notifQueueCount = 0;
  currentNotif.active = false;
}

// ============================================
// WEB SERVER HANDLERS
// ============================================
void handlePing() {
  server.send(200, "text/plain", "OK");
  Serial.println("Ping received");
}

void handleStats() {
  StaticJsonDocument<256> doc;
  doc["downloads"] = stats.totalDownloads;
  doc["prints"] = stats.totalPrints;
  doc["boosts"] = stats.totalBoosts;
  doc["points"] = stats.totalPoints;  // JSON gestisce automaticamente i float
  doc["initialized"] = stats.initialized;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleResync() {
  server.send(200, "text/plain", "Resync triggered");
  stats.initialized = false;
  saveStats();
  clearNotificationQueue();
  showMainScreen();
  Serial.println("Resync requested");
}

void handleReset() {
  prefs.begin("makerworld", false);
  prefs.clear();
  prefs.end();
  
  stats.totalDownloads = 0;
  stats.totalPrints = 0;
  stats.totalBoosts = 0;
  stats.totalPoints = 0.0;  // RESET A 0.0
  stats.initialized = false;
  
  clearNotificationQueue();
  showMainScreen();
  
  server.send(200, "text/plain", "Stats reset");
  Serial.println("Stats reset!");
}

void handleDebug() {
  String html = "<html><body style='font-family: Arial; background: #0f2027; color: #fff; padding: 20px;'>";
  html += "<h1 style='color: #00ff88;'>MakerWorld Monitor Debug</h1>";
  html += "<div style='background: #1a3a3a; padding: 20px; border-radius: 10px;'>";
  html += "<h2>Current Stats:</h2>";
  html += "<p>Downloads: <strong>" + String(stats.totalDownloads) + "</strong></p>";
  html += "<p>Prints: <strong>" + String(stats.totalPrints) + "</strong></p>";
  html += "<p>Boosts: <strong>" + String(stats.totalBoosts) + "</strong></p>";
  html += "<p>Points: <strong>" + String(stats.totalPoints, 2) + "</strong></p>";  // MOSTRA 2 DECIMALI
  html += "<p>Initialized: <strong>" + String(stats.initialized ? "YES" : "NO") + "</strong></p>";
  html += "<p>Uptime: <strong>" + String(millis() / 1000) + "s</strong></p>";
  html += "</div>";
  html += "<br><a href='/resync' style='color: #00ff88; padding: 10px; background: #1a3a3a; text-decoration: none; border-radius: 5px;'>Force Resync</a>";
  html += " <a href='/reset' style='color: #ff6666; padding: 10px; background: #1a3a3a; text-decoration: none; border-radius: 5px; margin-left: 10px;'>Reset Stats</a>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleTestNotification() {
  Notification notif;
  notif.type = "download";
  notif.modelName = "Bambu Lab X1 Carbon Complete Upgrade Kit with AMS Support";
  notif.value = 5;
  notif.total = 142;
  notif.points = 5.25;  // TEST CON DECIMALI
  enqueueNotification(notif);
  
  server.send(200, "application/json", "{\"status\":\"Test notification queued\"}");
}

void handleSync() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }
  
  String body = server.arg("plain");
  Serial.println("=== SYNC REQUEST ===");
  Serial.println(body);
  
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }
  
  bool updated = false;
  
  // AGGIUNTO: Estrai nome account
  if (doc.containsKey("accountName")) {
    String accountNameStr = doc["accountName"].as<String>();
    strncpy(stats.accountName, accountNameStr.c_str(), sizeof(stats.accountName) - 1);
    stats.accountName[sizeof(stats.accountName) - 1] = '\0';
    Serial.print("Account Name: "); Serial.println(stats.accountName);
    updated = true;
  }
  
  if (doc.containsKey("totalDownloads")) {
    stats.totalDownloads = doc["totalDownloads"];
    updated = true;
  }
  if (doc.containsKey("totalPrints")) {
    stats.totalPrints = doc["totalPrints"];
    updated = true;
  }
  if (doc.containsKey("totalBoosts")) {
    stats.totalBoosts = doc["totalBoosts"];
    updated = true;
  }
  if (doc.containsKey("totalPoints")) {
    stats.totalPoints = doc["totalPoints"].as<float>();  // LEGGE COME FLOAT
    updated = true;
  }
  
  if (updated) {
    stats.initialized = true;
    saveStats();
    Serial.println("=== SYNC COMPLETE ===");
    Serial.print("Account: "); Serial.println(stats.accountName);
    Serial.print("Downloads: "); Serial.println(stats.totalDownloads);
    Serial.print("Prints: "); Serial.println(stats.totalPrints);
    Serial.print("Boosts: "); Serial.println(stats.totalBoosts);
    Serial.print("Points: "); Serial.println(stats.totalPoints, 2);  // STAMPA CON 2 DECIMALI
    
    // AGGIUNTO: Forza ridisegno completo dello schermo
    delay(100);  // Piccolo delay per stabilità
  }
  
  showMainScreen();
  server.send(200, "text/plain", "Synced");
}

void handleNotification() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }
  
  String body = server.arg("plain");
  Serial.println("Notification received:");
  Serial.println(body);
  
  StaticJsonDocument<768> doc;
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }
  
  Notification notif;
  notif.type = doc["type"].as<String>();
  
  if (notif.type == "points") {
    notif.modelName = "";
    notif.value = doc["increase"].as<float>();    // LEGGE COME FLOAT
    notif.total = doc["before"].as<float>();      // LEGGE COME FLOAT
    notif.points = doc["after"].as<float>();      // LEGGE COME FLOAT
    
    // DEBUG CON DECIMALI
    Serial.println("=== POINTS DEBUG ===");
    Serial.print("JSON before: "); Serial.println(doc["before"].as<float>(), 2);
    Serial.print("JSON after: "); Serial.println(doc["after"].as<float>(), 2);
    Serial.print("JSON increase: "); Serial.println(doc["increase"].as<float>(), 2);
    Serial.print("Mapped total (before): "); Serial.println(notif.total, 2);
    Serial.print("Mapped points (after): "); Serial.println(notif.points, 2);
    Serial.print("Mapped value (increase): "); Serial.println(notif.value, 2);
  } else if (notif.type == "daily_summary") {
    notif.modelName = "";
    notif.value = doc["dailyDownloads"].as<float>();    // Downloads nelle 24h
    notif.total = doc["dailyPrints"].as<float>();       // Prints nelle 24h
    notif.points = doc["pointsGained"].as<float>();     // Points guadagnati
    
    // DEBUG DAILY SUMMARY
    Serial.println("=== DAILY SUMMARY DEBUG ===");
    Serial.print("Daily Downloads: "); Serial.println(notif.value, 0);
    Serial.print("Daily Prints: "); Serial.println(notif.total, 0);
    Serial.print("Points Gained: "); Serial.println(notif.points, 2);
    Serial.print("Total Points: "); Serial.println(doc["points"].as<float>(), 2);
  } else {
    notif.modelName = doc["modelName"].as<String>();
    notif.value = doc["value"] | 0;
    notif.total = doc["total"] | 0;
    notif.points = doc["points"].as<float>();     // LEGGE COME FLOAT
  }
  
  
  bool updated = false;
  
  if (doc.containsKey("currentTotalDownloads")) {
    stats.totalDownloads = doc["currentTotalDownloads"];
    updated = true;
  }
  if (doc.containsKey("currentTotalPrints")) {
    stats.totalPrints = doc["currentTotalPrints"];
    updated = true;
  }
  if (doc.containsKey("currentTotalBoosts")) {
    stats.totalBoosts = doc["currentTotalBoosts"];
    updated = true;
  }
  if (doc.containsKey("currentTotalPoints")) {
    stats.totalPoints = doc["currentTotalPoints"].as<float>();  // LEGGE COME FLOAT
    updated = true;
  }
  
  if (updated) {
    stats.initialized = true;
    saveStats();
  }
  
  // Si risponde subito: la notifica verra' mostrata dal loop quando e' il suo turno
  enqueueNotification(notif);
  server.send(200, "text/plain", "OK");
}