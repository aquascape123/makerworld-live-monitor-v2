// ============================================
// MODALITA' AUTONOMA (senza estensione del browser)
// ============================================
// Il CYD legge da solo i modelli pubblicati su MakerWorld, li confronta con
// l'ultimo controllo e mostra le novita' sullo schermo. Se configurato manda
// anche i messaggi Telegram.
// I punti NON sono disponibili: MakerWorld li mostra solo a chi ha fatto
// l'accesso, e qui non si salva nessuna credenziale MakerWorld.
// La configurazione si fa dal browser: http://<IP del CYD>/config

#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#define SA_USER_AGENT "MakerWorldLiveMonitor/3.0 (ESP32; +https://github.com/aquascape123/makerworld-live-monitor-v2)"
const int SA_PAGE_SIZE = 20;
const int SA_MAX_MODELS = 300;
const int SA_MAX_PENDING = 30;
const uint16_t SA_MIN_INTERVAL_MIN = 10;

struct StandaloneConfig {
  bool enabled = false;
  String handle;          // es. "aquascape"
  String uid;             // id numerico MakerWorld, es. "3182606546"
  String tgToken;
  String tgChat;
  uint16_t intervalMin = 15;
} sa;

ModelCount saBaseline[SA_MAX_MODELS];
int saBaselineCount = 0;
ModelCount saCurrent[SA_MAX_MODELS];
int saCurrentCount = 0;
PendingEvent saPending[SA_MAX_PENDING];
int saPendingCount = 0;

unsigned long saLastCheck = 0;
bool saCheckRequested = false;
bool saEverChecked = false;
String saStatus = "";

// --------------------------------------------
// Configurazione salvata in flash
// --------------------------------------------
void saLoadConfig() {
  Preferences p;
  p.begin("standalone", true);
  sa.enabled = p.getBool("enabled", true)  /* attiva di serie: basta scrivere il nome utente in /config */;
  sa.handle = p.getString("handle", "");
  sa.uid = p.getString("uid", "");
  sa.tgToken = p.getString("tgToken", "");
  sa.tgChat = p.getString("tgChat", "");
  sa.intervalMin = p.getUShort("interval", 15);
  soundOn = p.getBool("sound", true);
  saBaselineCount = p.getInt("baseCount", 0);
  if (saBaselineCount > 0 && saBaselineCount <= SA_MAX_MODELS) {
    p.getBytes("baseline", saBaseline, sizeof(ModelCount) * saBaselineCount);
  } else {
    saBaselineCount = 0;
  }
  p.end();
  if (sa.intervalMin < SA_MIN_INTERVAL_MIN) sa.intervalMin = SA_MIN_INTERVAL_MIN;

  Serial.print("Standalone mode: ");
  Serial.print(sa.enabled ? "ON" : "OFF");
  Serial.print(" handle=@"); Serial.print(sa.handle);
  Serial.print(" uid="); Serial.print(sa.uid);
  Serial.print(" telegram="); Serial.print(sa.tgToken.length() ? "yes" : "no");
  Serial.print(" baseline models="); Serial.println(saBaselineCount);
}

void saSaveConfig() {
  Preferences p;
  p.begin("standalone", false);
  p.putBool("enabled", sa.enabled);
  p.putString("handle", sa.handle);
  p.putString("uid", sa.uid);
  p.putString("tgToken", sa.tgToken);
  p.putString("tgChat", sa.tgChat);
  p.putUShort("interval", sa.intervalMin);
  p.putBool("sound", soundOn);
  p.end();
}

void saSaveBaseline() {
  Preferences p;
  p.begin("standalone", false);
  p.putInt("baseCount", saBaselineCount);
  if (saBaselineCount > 0) {
    p.putBytes("baseline", saBaseline, sizeof(ModelCount) * saBaselineCount);
  }
  p.end();
}

void saClearBaseline() {
  saBaselineCount = 0;
  saSaveBaseline();
}

// Accetta "aquascape", "@aquascape" o un link al profilo
String saNormalizeHandle(String input) {
  input.trim();
  int at = input.indexOf('@');
  if (at >= 0) input = input.substring(at + 1);
  int slash = input.indexOf('/');
  if (slash >= 0) input = input.substring(0, slash);
  String out = "";
  for (unsigned int i = 0; i < input.length(); i++) {
    char c = input[i];
    if (isalnum(c) || c == '_' || c == '-' || c == '.') out += c;
  }
  return out;
}

bool saIsDigits(const String &s) {
  if (s.length() == 0) return false;
  for (unsigned int i = 0; i < s.length(); i++) {
    if (!isdigit(s[i])) return false;
  }
  return true;
}

// --------------------------------------------
// Chiamata HTTPS che restituisce JSON (letto in streaming con un filtro)
// --------------------------------------------
int saGetJson(const String &url, JsonDocument &filter, JsonDocument &doc, String &error) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);   // niente "chunked": il JSON si legge in streaming
  http.setTimeout(15000);
  http.setUserAgent(SA_USER_AGENT);
  if (!http.begin(client, url)) {
    error = "connection failed";
    return -1;
  }
  unsigned long t0 = millis();
  int code = http.GET();
  Serial.printf("[SA] GET %s -> %d in %lu ms\n", url.substring(26, 80).c_str(), code, millis() - t0);
  if (code != 200) {
    error = "MakerWorld HTTP " + String(code);
    http.end();
    return code;
  }
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter),
                                             DeserializationOption::NestingLimit(30));
  http.end();
  if (err) {
    error = String("JSON ") + err.c_str();
    return -1;
  }
  return 200;
}

// --------------------------------------------
// Nome utente -> id numerico
// --------------------------------------------
// Si usa la ricerca utenti di MakerWorld e si prende il risultato con
// esattamente quel nome (@handle).
String saResolveUid(const String &handle, String &error) {
  JsonDocument filter;
  filter["hits"][0]["uid"] = true;
  filter["hits"][0]["handle"] = true;
  JsonDocument doc;
  String url = "https://makerworld.com/api/v1/search-service/search/user?keyword=" + handle + "&limit=20&offset=0";
  if (saGetJson(url, filter, doc, error) != 200) return "";

  for (JsonObject h : doc["hits"].as<JsonArray>()) {
    String hHandle = h["handle"] | "";
    if (hHandle.equalsIgnoreCase(handle)) {
      uint64_t uid = h["uid"] | (uint64_t)0;
      char buf[24];
      snprintf(buf, sizeof(buf), "%llu", (unsigned long long)uid);
      Serial.printf("Resolved @%s -> uid %s\n", handle.c_str(), buf);
      return String(buf);
    }
  }
  error = "profile @" + handle + " not found";
  return "";
}

// Totali del profilo, gli stessi mostrati sul sito (comprendono anche i
// modelli non piu' pubblicati, per questo la somma dei modelli non basta).
bool saFetchProfileTotals(String &error) {
  JsonDocument filter;
  filter["name"] = true;
  filter["likeCount"] = true;
  filter["boostGained"] = true;
  filter["MWCount"]["myDesignDownloadCount"] = true;
  filter["MWCount"]["myDesignPrintCount"] = true;
  JsonDocument doc;
  String url = "https://makerworld.com/api/v1/design-user-service/user/profile/" + sa.uid;
  if (saGetJson(url, filter, doc, error) != 200) return false;
  if (!doc["MWCount"].is<JsonObject>()) {
    error = "profile data missing";
    return false;
  }
  stats.totalDownloads = doc["MWCount"]["myDesignDownloadCount"] | 0;
  stats.totalPrints = doc["MWCount"]["myDesignPrintCount"] | 0;
  stats.totalBoosts = doc["boostGained"] | 0;
  stats.totalLikes = doc["likeCount"] | 0;
  String name = doc["name"] | "";
  if (name.length() > 0) {
    strncpy(stats.accountName, name.c_str(), sizeof(stats.accountName) - 1);
    stats.accountName[sizeof(stats.accountName) - 1] = '\0';
  }
  return true;
}

// --------------------------------------------
// Telegram (inviato direttamente dal CYD)
// --------------------------------------------
bool saTelegramSend(const String &text, const String &photoUrl) {
  if (sa.tgToken.length() == 0 || sa.tgChat.length() == 0) return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  bool withPhoto = photoUrl.length() > 0;
  String url = "https://api.telegram.org/bot" + sa.tgToken + (withPhoto ? "/sendPhoto" : "/sendMessage");
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");

  // Testo semplice (senza parse_mode): nessun problema con < > & nei nomi
  JsonDocument doc;
  doc["chat_id"] = sa.tgChat;
  if (withPhoto) {
    doc["photo"] = photoUrl;   // Telegram scarica la foto da solo
    doc["caption"] = text;
  } else {
    doc["text"] = text;
  }
  String body;
  serializeJson(doc, body);
  int code = http.POST(body);
  http.end();

  Serial.print("Telegram ");
  Serial.print(withPhoto ? "photo" : "message");
  Serial.print(" -> HTTP ");
  Serial.println(code);

  if (code != 200 && withPhoto) {
    return saTelegramSend(text, "");  // la foto non e' andata: si manda solo il testo
  }
  return code == 200;
}

String saTelegramText(const PendingEvent &e) {
  String head;
  if (e.type == "download") head = "📈 New downloads for: ";
  else if (e.type == "print") head = "🖨 New prints for: ";
  else head = "⚡ New boosts for: ";
  String text = head + e.title + "\n\n";
  text += "Total: " + String(e.total) + "\n";
  text += "Increase: +" + String(e.value);
  return text;
}

// --------------------------------------------
// Lettura dei modelli
// --------------------------------------------
const ModelCount *saFindBaseline(uint32_t id) {
  for (int i = 0; i < saBaselineCount; i++) {
    if (saBaseline[i].id == id) return &saBaseline[i];
  }
  return nullptr;
}

void saAddPending(const char *type, const char *title, const char *cover, int32_t value, int32_t total) {
  if (saPendingCount >= SA_MAX_PENDING) return;
  PendingEvent &e = saPending[saPendingCount++];
  e.type = type;
  e.title = title ? title : "";
  e.coverUrl = cover ? cover : "";
  e.value = value;
  e.total = total;
}

// Scarica una pagina di modelli. Ritorna il numero di modelli letti, -1 se errore.
int saFetchPage(int offset, int &total, long &likes, String &creatorName, String &error) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);   // niente "chunked": il JSON si legge in streaming
  http.setTimeout(15000);
  http.setUserAgent(SA_USER_AGENT);

  String url = "https://makerworld.com/api/v1/design-service/published/" + sa.uid +
               "/design?limit=" + String(SA_PAGE_SIZE) + "&offset=" + String(offset);
  Serial.printf("[SA] page offset=%d heap=%u maxblock=%u\n", offset, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  if (!http.begin(client, url)) {
    error = "connection failed";
    return -1;
  }
  unsigned long t0 = millis();
  int code = http.GET();
  Serial.printf("[SA] GET -> %d (%s) in %lu ms\n", code, code < 0 ? http.errorToString(code).c_str() : "ok", millis() - t0);
  if (code != 200) {
    error = "MakerWorld HTTP " + String(code);
    http.end();
    return -1;
  }

  // Si tengono solo i campi necessari (la risposta completa e' ~150 KB)
  JsonDocument filter;
  filter["total"] = true;
  filter["hits"][0]["id"] = true;
  filter["hits"][0]["title"] = true;
  filter["hits"][0]["coverUrl"] = true;
  filter["hits"][0]["downloadCount"] = true;
  filter["hits"][0]["printCount"] = true;
  filter["hits"][0]["boostCnt"] = true;
  filter["hits"][0]["likeCount"] = true;
  filter["hits"][0]["designCreator"]["name"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter),
                                             DeserializationOption::NestingLimit(30));
  Serial.printf("[SA] JSON parsed (%s) in %lu ms, heap=%u\n", err.c_str(), millis() - t0, ESP.getFreeHeap());
  http.end();
  if (err) {
    error = String("JSON ") + err.c_str();
    return -1;
  }

  total = doc["total"] | 0;
  JsonArray hits = doc["hits"].as<JsonArray>();
  int count = 0;
  for (JsonObject h : hits) {
    if (saCurrentCount >= SA_MAX_MODELS) break;
    uint32_t id = h["id"] | 0;
    // Stesso modello gia' letto (la lista si e' spostata durante la lettura)
    bool duplicate = false;
    for (int i = 0; i < saCurrentCount; i++) {
      if (saCurrent[i].id == id) { duplicate = true; break; }
    }
    count++;
    if (duplicate) continue;

    ModelCount &m = saCurrent[saCurrentCount++];
    m.id = id;
    m.downloads = h["downloadCount"] | 0;
    m.prints = h["printCount"] | 0;
    m.boosts = h["boostCnt"] | 0;
    likes += h["likeCount"] | 0;
    if (creatorName.length() == 0) creatorName = h["designCreator"]["name"] | "";

    const ModelCount *prev = saFindBaseline(id);
    if (prev) {
      const char *title = h["title"] | "";
      const char *cover = h["coverUrl"] | "";
      if (m.boosts > prev->boosts) saAddPending("boost", title, cover, m.boosts - prev->boosts, m.boosts);
      if (m.downloads > prev->downloads) saAddPending("download", title, cover, m.downloads - prev->downloads, m.downloads);
      if (m.prints > prev->prints) saAddPending("print", title, cover, m.prints - prev->prints, m.prints);
    }
  }
  return count;
}

bool saCheckNow() {
  if (!saIsDigits(sa.uid)) {
    saStatus = "AUTO: open /config to start";
    return false;
  }

  unsigned long started = millis();
  saCurrentCount = 0;
  saPendingCount = 0;
  int total = 0;
  long likes = 0;
  String creatorName = "";
  String error = "";
  int offset = 0;
  int pages = 0;

  Serial.println("=== STANDALONE CHECK ===");
  while (pages < 20) {
    int got = saFetchPage(offset, total, likes, creatorName, error);
    if (got < 0) break;
    pages++;
    if (got == 0 || saCurrentCount >= total || saCurrentCount >= SA_MAX_MODELS) break;
    offset += got;
    delay(300);  // una richiesta alla volta, con calma
  }

  // Lista incompleta (errore di rete, limite...): meglio saltare il giro
  // che salvare dati parziali e perdere notifiche.
  if (error.length() > 0 || saCurrentCount == 0 ||
      (saCurrentCount < total && saCurrentCount < SA_MAX_MODELS)) {
    if (error.length() == 0) error = "read " + String(saCurrentCount) + "/" + String(total);
    saStatus = "AUTO: " + error;
    Serial.print("Standalone check failed: ");
    Serial.println(error);
    return false;
  }

  bool firstRun = saBaselineCount == 0;

  // Totali: quelli del profilo (uguali al sito). Se non si riesce a
  // leggerli si usa la somma dei modelli pubblicati come riserva.
  String profileError;
  if (!saFetchProfileTotals(profileError)) {
    Serial.print("Profile totals not available (");
    Serial.print(profileError);
    Serial.println("), using sum of models");
    long downloads = 0, prints = 0, boosts = 0;
    for (int i = 0; i < saCurrentCount; i++) {
      downloads += saCurrent[i].downloads;
      prints += saCurrent[i].prints;
      boosts += saCurrent[i].boosts;
    }
    stats.totalDownloads = downloads;
    stats.totalPrints = prints;
    stats.totalBoosts = boosts;
    stats.totalLikes = likes;
    if (creatorName.length() > 0) {
      strncpy(stats.accountName, creatorName.c_str(), sizeof(stats.accountName) - 1);
      stats.accountName[sizeof(stats.accountName) - 1] = '\0';
    }
  }
  Serial.printf("Totals: downloads=%d prints=%d boosts=%d likes=%ld\n",
                stats.totalDownloads, stats.totalPrints, stats.totalBoosts, stats.totalLikes);
  stats.initialized = true;
  saveStats();

  // Nuovo punto di partenza
  memcpy(saBaseline, saCurrent, sizeof(ModelCount) * saCurrentCount);
  saBaselineCount = saCurrentCount;
  saSaveBaseline();

  Serial.print("Models: "); Serial.print(saCurrentCount);
  Serial.print(" pages: "); Serial.print(pages);
  Serial.print(" changes: "); Serial.print(firstRun ? 0 : saPendingCount);
  Serial.print(" time: "); Serial.print(millis() - started); Serial.println(" ms");

  if (!firstRun) {
    for (int i = 0; i < saPendingCount; i++) {
      PendingEvent &e = saPending[i];
      Notification n;
      n.type = e.type;
      n.modelName = e.title;
      n.value = e.value;
      n.total = e.total;
      n.points = e.type == "download" ? e.value : (e.type == "print" ? e.value * 2 : 0);
      enqueueNotification(n);
    }
    for (int i = 0; i < saPendingCount; i++) {
      saTelegramSend(saTelegramText(saPending[i]), saPending[i].coverUrl);
    }
  }

  saStatus = "AUTO: " + String(saCurrentCount) + " models";
  return true;
}

// Da chiamare nel loop: controlla MakerWorld all'intervallo scelto
void saLoop() {
  if (!sa.enabled || WiFi.status() != WL_CONNECTED) return;
  // Non si interrompe una notifica sullo schermo
  if (currentNotif.active || notifQueueCount > 0) return;

  unsigned long now = millis();
  unsigned long interval = (unsigned long)sa.intervalMin * 60000UL;
  bool due = !saEverChecked ? now > 10000 : (now - saLastCheck >= interval);
  if (!due && !saCheckRequested) return;

  saCheckRequested = false;
  saEverChecked = true;
  saLastCheck = now;
  bool ok = saCheckNow();

  // A volte il CYD si avvia con la rete "a meta'": il WiFi risulta collegato
  // ma nessuna connessione funziona (ne' verso MakerWorld ne' dal PC).
  // Se succede: si ricollega il WiFi e si riprova dopo 30 s; al terzo
  // fallimento di fila si riavvia la scheda.
  static int netFailures = 0;
  if (!ok && saStatus.indexOf("HTTP -") >= 0) {
    netFailures++;
    Serial.printf("Network failure %d/3\n", netFailures);
    if (netFailures >= 3) {
      Serial.println("Network still not working: restarting");
      delay(500);
      ESP.restart();
    }
    WiFi.disconnect(false);
    delay(500);
    WiFi.reconnect();
    // prossimo tentativo tra 30 secondi invece che all'intervallo normale
    saLastCheck = millis() - interval + 30000UL;
  } else if (ok) {
    netFailures = 0;
  }

  if (!currentNotif.active && notifQueueCount == 0) updateStatsDisplay();
}

// --------------------------------------------
// Pagina di configurazione: http://<IP>/config
// --------------------------------------------
String saHtmlEscape(const String &s) {
  String o;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

void saSendConfigPage(const String &message) {
  String html = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>MakerWorld Monitor - Setup</title><style>"
                  "body{font-family:system-ui,Arial;background:#0f2027;color:#eee;margin:0;padding:20px}"
                  ".box{max-width:480px;margin:auto;background:#1a3a3a;padding:20px;border-radius:12px}"
                  "h1{color:#00ff88;font-size:1.4em;margin-top:0}label{display:block;margin:14px 0 4px;font-weight:600}"
                  "input[type=text],input[type=number]{width:100%;padding:10px;border-radius:8px;border:0;box-sizing:border-box;font-size:1em}"
                  "small{color:#9bb}.msg{background:#264d40;padding:10px;border-radius:8px;margin-bottom:10px}"
                  "button{margin-top:18px;width:100%;padding:12px;border:0;border-radius:8px;background:#00c070;color:#fff;font-size:1.05em;font-weight:700}"
                  "a{color:#00ff88}</style></head><body><div class='box'>"
                  "<h1>MakerWorld Monitor</h1>");
  if (message.length() > 0) html += "<div class='msg'>" + message + "</div>";
  html += F("<form method='POST' action='/config'>"
            "<label><input type='checkbox' name='enabled' value='1'");
  if (sa.enabled) html += " checked";
  html += F("> Standalone mode (no browser extension)</label>"
            "<small>The display reads MakerWorld by itself. Points are not available in this mode.</small>"
            "<label>MakerWorld username</label>"
            "<input type='text' name='handle' placeholder='@yourname' value='");
  html += saHtmlEscape(sa.handle);
  html += F("'><small>");
  if (sa.uid.length()) html += "Profile ID: " + saHtmlEscape(sa.uid);
  html += F("</small><label>Check every (minutes, min 10)</label><input type='number' name='interval' min='10' max='240' value='");
  html += String(sa.intervalMin);
  html += F("'><label><input type='checkbox' name='sound' value='1'");
  if (soundOn) html += " checked";
  html += F("> Sound (buzzer on GPIO 22)</label><small>Beep at startup and on every notification.</small>"
            "<label>Telegram bot token (optional)</label><input type='text' name='tgToken' autocomplete='off' placeholder='");
  html += sa.tgToken.length() ? F("saved - leave empty to keep it") : F("123456789:ABC...");
  html += F("'><small>Write <b>-</b> to remove it.</small><label>Telegram chat ID</label><input type='text' name='tgChat' value='");
  html += saHtmlEscape(sa.tgChat);
  html += F("'><button type='submit'>Save</button></form>"
            "<form method='POST' action='/check-now'><button type='submit' style='background:#2d6cdf'>Check now</button></form>"
            "<form method='POST' action='/test-sound'><button type='submit' style='background:#555'>Play test sound</button></form>"
            "<form method='POST' action='/test-telegram'><button type='submit' style='background:#555'>Send Telegram test</button></form>"
            "<form method='POST' action='/simulate'><button type='submit' style='background:#7a4fd0'>Simulate 3 notifications</button></form>"
            "<p><a href='/mqtt'>Home Assistant (MQTT) &rarr;</a></p>"
            "<p><small>");
  html += saHtmlEscape(saStatus);
  html += F("</small></p></div></body></html>");
  server.send(200, "text/html", html);
}

void handleConfigGet() {
  saSendConfigPage("");
}

void handleConfigPost() {
  String message = "";
  bool wasEnabled = sa.enabled;
  sa.enabled = server.hasArg("enabled");

  String newHandle = saNormalizeHandle(server.arg("handle"));
  if (newHandle != sa.handle) {
    sa.handle = newHandle;
    sa.uid = "";
    saClearBaseline();  // altro profilo: si riparte da zero, senza notifiche false
  }

  int interval = server.arg("interval").toInt();
  if (interval < SA_MIN_INTERVAL_MIN) interval = SA_MIN_INTERVAL_MIN;
  if (interval > 240) interval = 240;
  sa.intervalMin = interval;
  soundOn = server.hasArg("sound");

  String token = server.arg("tgToken");
  token.trim();
  if (token == "-") sa.tgToken = "";
  else if (token.length() > 0) sa.tgToken = token;
  String chat = server.arg("tgChat");
  chat.trim();
  sa.tgChat = chat;

  if (sa.handle.length() > 0 && sa.uid.length() == 0) {
    if (saIsDigits(sa.handle)) {
      sa.uid = sa.handle;
    } else {
      String error;
      sa.uid = saResolveUid(sa.handle, error);
      if (sa.uid.length() == 0) message = "Could not find the profile: " + saHtmlEscape(error);
    }
  }

  saSaveConfig();
  if (sa.enabled != wasEnabled) mqttReconnectNow();  // Home Assistant: like o punti cambiano
  if (message.length() == 0) {
    message = "Saved.";
    if (sa.enabled && sa.uid.length()) message += " First check in a few seconds (it only saves the starting values, notifications start from the next change).";
  }
  if (sa.enabled && (!wasEnabled || saBaselineCount == 0)) {
    saCheckRequested = true;
  }
  saSendConfigPage(message);
}

void handleCheckNow() {
  saCheckRequested = true;
  saSendConfigPage(sa.enabled ? "Check started: look at the display." : "Enable standalone mode first.");
}

void handleTestSound() {
  if (!soundOn) { saSendConfigPage("Sound is off: tick <b>Sound</b> and press Save first."); return; }
  playNotificationSound();
  saSendConfigPage("Test sound played. Nothing heard? Check the buzzer on GPIO 22 and GND (it must be a passive buzzer).");
}

void handleTestTelegram() {
  bool ok = saTelegramSend("✅ MakerWorld Monitor: Telegram works!", "");
  saSendConfigPage(ok ? "Telegram test sent." : "Telegram test failed: check token and chat ID (and send /start to your bot).");
}

// Prova: abbassa i valori salvati di alcuni modelli, cosi' il prossimo
// controllo trova delle "novita'" e mostra le notifiche con dati veri.
void handleSimulateChanges() {
  if (saBaselineCount < 3) {
    saSendConfigPage("Do a first check before the test.");
    return;
  }
  // Si scelgono modelli diversi che hanno gia' dei numeri, cosi' la
  // notifica mostra un totale vero (e non "+2, totale 0").
  int d = -1, pr = -1, b = -1;
  for (int i = 0; i < saBaselineCount; i++) {
    if (d < 0 && saBaseline[i].downloads >= 2) { d = i; continue; }
    if (pr < 0 && saBaseline[i].prints >= 1) { pr = i; continue; }
    if (b < 0 && saBaseline[i].boosts >= 1) { b = i; continue; }
  }
  if (d >= 0) saBaseline[d].downloads -= 2;
  if (pr >= 0) saBaseline[pr].prints -= 1;
  if (b >= 0) saBaseline[b].boosts -= 1;
  saSaveBaseline();
  saCheckRequested = true;
  saSendConfigPage("Test started: in a few seconds you should see 3 notifications (download, print, boost).");
}

void saRegisterRoutes() {
  server.on("/simulate", HTTP_POST, handleSimulateChanges);
  server.on("/config", HTTP_GET, handleConfigGet);
  server.on("/config", HTTP_POST, handleConfigPost);
  server.on("/check-now", HTTP_POST, handleCheckNow);
  server.on("/test-sound", HTTP_POST, handleTestSound);
  server.on("/test-telegram", HTTP_POST, handleTestTelegram);
}

bool saIsEnabled() {
  return sa.enabled;
}

String saGetStatus() {
  return saStatus;
}
