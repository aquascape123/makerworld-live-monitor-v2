// ============================================
// HOME ASSISTANT VIA MQTT (facoltativo)
// ============================================
// Idea nata dalla pull request di annaglyph sull'estensione: qui pero' e' il
// CYD a pubblicare, quindi funziona anche senza browser aperto.
//
// Con la "discovery" di Home Assistant i sensori compaiono da soli:
//   <prefix>/sensor/<id>/<chiave>/config   (retained)
//   makerworld/<id>/state                   JSON con i totali (retained)
//   makerworld/<id>/status                  online / offline (LWT)
//   makerworld/<id>/event                   ogni notifica (per le automazioni)
// Configurazione: http://<IP del CYD>/mqtt

#include <PubSubClient.h>

struct MqttConfig {
  bool enabled = false;
  String host;
  uint16_t port = 1883;
  String user;
  String pass;
  String prefix = "homeassistant";
} mq;

WiFiClient mqttNet;
PubSubClient mqttClient(mqttNet);
String mqttId;            // es. "mwlm_a1b2c3", uguale per sempre su questa scheda
String mqttBase;          // "makerworld/<id>"
String mqttStatus = "MQTT off";
bool mqttDirty = true;    // totali da ripubblicare
bool mqttDiscoveryDone = false;
unsigned long mqttLastTry = 0;
unsigned long mqttRetryMs = 10000;

void mqttLoadConfig() {
  Preferences p;
  p.begin("mqtt", true);
  mq.enabled = p.getBool("enabled", false);
  mq.host = p.getString("host", "");
  mq.port = p.getUShort("port", 1883);
  mq.user = p.getString("user", "");
  mq.pass = p.getString("pass", "");
  mq.prefix = p.getString("prefix", "homeassistant");
  p.end();
  if (mq.prefix.length() == 0) mq.prefix = "homeassistant";

  uint64_t mac = ESP.getEfuseMac();  // valido anche prima di accendere il WiFi
  char id[16];
  snprintf(id, sizeof(id), "mwlm_%02x%02x%02x", (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  mqttId = id;
  mqttBase = "makerworld/" + mqttId;

  Serial.printf("MQTT: %s host=%s:%u id=%s\n", mq.enabled ? "ON" : "OFF", mq.host.c_str(), mq.port, id);
}

void mqttSaveConfig() {
  Preferences p;
  p.begin("mqtt", false);
  p.putBool("enabled", mq.enabled);
  p.putString("host", mq.host);
  p.putUShort("port", mq.port);
  p.putString("user", mq.user);
  p.putString("pass", mq.pass);
  p.putString("prefix", mq.prefix);
  p.end();
}

// Chiamata da saveStats(): i totali sono cambiati
void mqttMarkDirty() {
  mqttDirty = true;
}

bool mqttPublishJson(const String &topic, JsonDocument &doc, bool retain) {
  String payload;
  serializeJson(doc, payload);
  bool ok = mqttClient.publish(topic.c_str(), payload.c_str(), retain);
  if (!ok) Serial.printf("MQTT publish failed: %s (%u bytes)\n", topic.c_str(), payload.length());
  return ok;
}

void mqttAddDevice(JsonDocument &doc) {
  JsonObject dev = doc["device"].to<JsonObject>();
  dev["identifiers"].to<JsonArray>().add(mqttId);
  String name = "MakerWorld Monitor";
  if (stats.accountName[0]) name += String(" ") + stats.accountName;
  dev["name"] = name;
  dev["manufacturer"] = "MakerWorld Live Monitor";
  dev["model"] = "ESP32 CYD";
  dev["sw_version"] = "3.0";
  dev["configuration_url"] = "http://" + WiFi.localIP().toString() + "/config";
}

void mqttDiscoverSensor(const char *key, const char *name, const char *icon) {
  String topic = mq.prefix + "/sensor/" + mqttId + "/" + key + "/config";
  JsonDocument doc;
  doc["name"] = name;
  doc["unique_id"] = mqttId + "_" + key;
  doc["state_topic"] = mqttBase + "/state";
  doc["value_template"] = String("{{ value_json.") + key + " }}";
  doc["availability_topic"] = mqttBase + "/status";
  doc["state_class"] = "measurement";
  doc["icon"] = icon;
  mqttAddDevice(doc);
  mqttPublishJson(topic, doc, true);
}

void mqttPublishDiscovery() {
  mqttDiscoverSensor("downloads", "Downloads", "mdi:download");
  mqttDiscoverSensor("prints", "Prints", "mdi:printer-3d");
  mqttDiscoverSensor("boosts", "Boosts", "mdi:rocket-launch");

  // Like solo in modalita' autonoma, punti solo con l'estensione:
  // il sensore che non ha dati viene tolto da Home Assistant (config vuota).
  String likesTopic = mq.prefix + "/sensor/" + mqttId + "/likes/config";
  String pointsTopic = mq.prefix + "/sensor/" + mqttId + "/points/config";
  if (saIsEnabled()) {
    mqttDiscoverSensor("likes", "Likes", "mdi:heart");
    mqttClient.publish(pointsTopic.c_str(), "", true);
  } else {
    mqttDiscoverSensor("points", "Points", "mdi:star");
    mqttClient.publish(likesTopic.c_str(), "", true);
  }

  // Entita' "evento": scatta a ogni notifica, utile per le automazioni
  String topic = mq.prefix + "/event/" + mqttId + "/activity/config";
  JsonDocument doc;
  doc["name"] = "Activity";
  doc["unique_id"] = mqttId + "_activity";
  doc["state_topic"] = mqttBase + "/event";
  doc["availability_topic"] = mqttBase + "/status";
  doc["icon"] = "mdi:bell-ring";
  JsonArray types = doc["event_types"].to<JsonArray>();
  types.add("download");
  types.add("print");
  types.add("boost");
  types.add("points");
  types.add("daily_summary");
  mqttAddDevice(doc);
  mqttPublishJson(topic, doc, true);
}

void mqttPublishState() {
  JsonDocument doc;
  doc["downloads"] = stats.totalDownloads;
  doc["prints"] = stats.totalPrints;
  doc["boosts"] = stats.totalBoosts;
  if (saIsEnabled()) doc["likes"] = stats.totalLikes;
  else doc["points"] = serialized(String(stats.totalPoints, 2));
  doc["account"] = stats.accountName;
  if (mqttPublishJson(mqttBase + "/state", doc, true)) mqttDirty = false;
}

// Chiamata da enqueueNotification(): una novita' da mostrare
void mqttPublishEvent(const Notification &n) {
  if (!mq.enabled || !mqttClient.connected()) return;
  if (n.type != "download" && n.type != "print" && n.type != "boost" &&
      n.type != "points" && n.type != "daily_summary") return;
  JsonDocument doc;
  doc["event_type"] = n.type;
  doc["model"] = n.modelName;
  doc["value"] = n.value;
  doc["total"] = n.total;
  mqttPublishJson(mqttBase + "/event", doc, false);
}

bool mqttConnect() {
  mqttClient.setServer(mq.host.c_str(), mq.port);
  mqttClient.setBufferSize(1024);     // i messaggi di discovery superano i 256 byte standard
  mqttClient.setKeepAlive(90);        // il controllo di MakerWorld puo' tenere occupato il CYD
  mqttClient.setSocketTimeout(5);
  mqttNet.setTimeout(5);
  String lwt = mqttBase + "/status";
  bool ok = mq.user.length()
    ? mqttClient.connect(mqttId.c_str(), mq.user.c_str(), mq.pass.c_str(), lwt.c_str(), 0, true, "offline")
    : mqttClient.connect(mqttId.c_str(), lwt.c_str(), 0, true, "offline");
  if (!ok) {
    int st = mqttClient.state();
    const char *why = st == 5 ? "wrong user/password" : st == 4 ? "bad credentials"
                    : st == -2 ? "server not reachable" : st == -4 ? "timeout" : "error";
    mqttStatus = "MQTT: " + String(why) + " (" + String(st) + ")";
    Serial.println(mqttStatus);
    return false;
  }
  mqttClient.publish(lwt.c_str(), "online", true);
  mqttPublishDiscovery();
  mqttDiscoveryDone = true;
  mqttDirty = true;
  mqttStatus = "MQTT: connected to " + mq.host;
  Serial.println(mqttStatus);
  return true;
}

void mqttLoop() {
  if (!mq.enabled || mq.host.length() == 0 || WiFi.status() != WL_CONNECTED) return;
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (mqttLastTry != 0 && now - mqttLastTry < mqttRetryMs) return;
    mqttLastTry = now;
    if (mqttConnect()) {
      mqttRetryMs = 10000;
    } else {
      mqttRetryMs = min(mqttRetryMs * 2, 300000UL);  // 10 s, 20 s, ... fino a 5 minuti
      return;
    }
  }
  mqttClient.loop();
  if (mqttDirty && stats.initialized) mqttPublishState();
}

void mqttReconnectNow() {
  if (mqttClient.connected()) {
    mqttClient.publish((mqttBase + "/status").c_str(), "offline", true);
    mqttClient.disconnect();
  }
  mqttLastTry = 0;
  mqttRetryMs = 10000;
  mqttStatus = mq.enabled ? "MQTT: connecting..." : "MQTT off";
}

// --------------------------------------------
// Pagina di configurazione: http://<IP>/mqtt
// --------------------------------------------
void mqttSendPage(const String &message) {
  String html = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>MakerWorld Monitor - Home Assistant</title><style>"
                  "body{font-family:system-ui,Arial;background:#0f2027;color:#eee;margin:0;padding:20px}"
                  ".box{max-width:480px;margin:auto;background:#1a3a3a;padding:20px;border-radius:12px}"
                  "h1{color:#00ff88;font-size:1.4em;margin-top:0}label{display:block;margin:14px 0 4px;font-weight:600}"
                  "input[type=text],input[type=number],input[type=password]{width:100%;padding:10px;border-radius:8px;border:0;box-sizing:border-box;font-size:1em}"
                  "small{color:#9bb}.msg{background:#264d40;padding:10px;border-radius:8px;margin-bottom:10px}"
                  "button{margin-top:18px;width:100%;padding:12px;border:0;border-radius:8px;background:#00c070;color:#fff;font-size:1.05em;font-weight:700}"
                  "a{color:#00ff88}code{color:#ffd166}</style></head><body><div class='box'>"
                  "<h1>Home Assistant (MQTT)</h1>");
  if (message.length() > 0) html += "<div class='msg'>" + message + "</div>";
  html += F("<form method='POST' action='/mqtt'><label><input type='checkbox' name='enabled' value='1'");
  if (mq.enabled) html += " checked";
  html += F("> Send data to Home Assistant</label>"
            "<small>Sensors appear by themselves with MQTT discovery.</small>"
            "<label>MQTT broker (IP or name)</label><input type='text' name='host' placeholder='192.168.1.10' value='");
  html += saHtmlEscape(mq.host);
  html += F("'><label>Port</label><input type='number' name='port' min='1' max='65535' value='");
  html += String(mq.port);
  html += F("'><label>User (optional)</label><input type='text' name='user' autocomplete='off' value='");
  html += saHtmlEscape(mq.user);
  html += F("'><label>Password</label><input type='password' name='pass' autocomplete='new-password' placeholder='");
  html += mq.pass.length() ? F("saved - leave empty to keep it") : F("");
  html += F("'><small>Write <b>-</b> to remove it.</small>"
            "<label>Discovery prefix</label><input type='text' name='prefix' value='");
  html += saHtmlEscape(mq.prefix);
  html += F("'><button type='submit'>Save</button></form>"
            "<form method='POST' action='/mqtt-test'><button type='submit' style='background:#2d6cdf'>Send test event</button></form>"
            "<p><small>");
  html += saHtmlEscape(mqttStatus);
  html += F("<br>Topics: <code>");
  html += mqttBase;
  html += F("/state</code>, <code>/event</code>, <code>/status</code></small></p>"
            "<p><a href='/config'>&larr; Back to setup</a></p></div></body></html>");
  server.send(200, "text/html", html);
}

void handleMqttGet() {
  mqttSendPage("");
}

void handleMqttPost() {
  mq.enabled = server.hasArg("enabled");
  String host = server.arg("host");
  host.trim();
  mq.host = host;
  long port = server.arg("port").toInt();
  mq.port = (port >= 1 && port <= 65535) ? port : 1883;
  String user = server.arg("user");
  user.trim();
  mq.user = user;
  String pass = server.arg("pass");
  if (pass == "-") mq.pass = "";
  else if (pass.length() > 0) mq.pass = pass;
  String prefix = server.arg("prefix");
  prefix.trim();
  mq.prefix = prefix.length() ? prefix : "homeassistant";
  mqttSaveConfig();
  mqttReconnectNow();
  String msg = "Saved.";
  if (mq.enabled && mq.host.length() == 0) msg = "Saved, but the broker address is empty.";
  else if (mq.enabled) msg += " Connecting in a few seconds: reload this page to see the status.";
  mqttSendPage(msg);
}

void handleMqttTest() {
  if (!mq.enabled || !mqttClient.connected()) {
    mqttSendPage("Not connected to the broker: check the settings first.");
    return;
  }
  Notification n;
  n.type = "download";
  n.modelName = "Test from MakerWorld Monitor";
  n.value = 1;
  n.total = stats.totalDownloads;
  mqttPublishEvent(n);
  mqttDirty = true;
  mqttSendPage("Test event sent: in Home Assistant look at the <b>Activity</b> entity.");
}

void mqttRegisterRoutes() {
  server.on("/mqtt", HTTP_GET, handleMqttGet);
  server.on("/mqtt", HTTP_POST, handleMqttPost);
  server.on("/mqtt-test", HTTP_POST, handleMqttTest);
}
