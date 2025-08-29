#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <time.h>
#include <EEPROM.h>
#include <Servo.h>

// ===================== Wi-Fi =====================
const char* WIFI_SSID     = "Bedroom 2 - Yamin";
const char* WIFI_PASSWORD = "78789898";

// ================= Web Server ===================
ESP8266WebServer server(80);

// ================== Servo ======================
Servo feederServo;
const int SERVO_PIN = D4; // GPIO2

// Defaults (can be changed from the Web UI)
uint8_t  DEFAULT_CLOSED_ANGLE = 0;
uint8_t  DEFAULT_OPEN_ANGLE   = 180;
uint16_t DEFAULT_OPEN_MS      = 400;   // ms the chute stays open

// Default feed times (Asia/Dhaka)
uint8_t DEFAULT_FEED1_H = 8,  DEFAULT_FEED1_M = 0;   // 08:00
uint8_t DEFAULT_FEED2_H = 20, DEFAULT_FEED2_M = 0;   // 20:00

// =============== EEPROM Config ==================
struct Config {
  uint32_t magic   = 0xA5FEEDEF;
  uint8_t  version = 2;

  uint8_t  feed1_h, feed1_m;
  uint8_t  feed2_h, feed2_m;

  uint8_t  closedAngle;
  uint8_t  openAngle;
  uint16_t openMs;

  // last-feed date per slot (YYYYMMDD)
  uint32_t lastFeed1YMD = 0;
  uint32_t lastFeed2YMD = 0;

  // NEW: last-feed timestamps (epoch seconds)
  uint32_t lastFeed1TS   = 0;
  uint32_t lastFeed2TS   = 0;
  uint32_t lastAnyFeedTS = 0;
} cfg;

static const uint16_t EEPROM_SIZE = sizeof(Config);

// ============== Time / NTP (Asia/Dhaka) =========
const long TZ_OFFSET  = 6 * 3600; // UTC+6
const long DST_OFFSET = 0;
const char* NTP1 = "bd.pool.ntp.org";
const char* NTP2 = "pool.ntp.org";

// ----------------- Helpers ----------------------
uint32_t ymdToday() {
  time_t now = time(nullptr);
  struct tm tinfo;
  localtime_r(&now, &tinfo);
  return (uint32_t)((tinfo.tm_year + 1900) * 10000 + (tinfo.tm_mon + 1) * 100 + tinfo.tm_mday);
}

bool nowAtOrAfter(uint8_t H, uint8_t M) {
  time_t now = time(nullptr);
  struct tm tinfo;
  localtime_r(&now, &tinfo);
  int nowMin = tinfo.tm_hour * 60 + tinfo.tm_min;
  int trgMin = (int)H * 60 + (int)M;
  return nowMin >= trgMin;
}

String timeString() {
  time_t now = time(nullptr);
  struct tm tinfo;
  localtime_r(&now, &tinfo);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tinfo);
  return String(buf);
}

uint32_t nowEpoch() {
  return (uint32_t) time(nullptr);
}

String tsToLocalString(uint32_t ts) {
  if (ts == 0) return String("-");
  time_t t = (time_t) ts;
  struct tm tinfo;
  localtime_r(&t, &tinfo);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tinfo);
  return String(buf);
}

// --------------- EEPROM I/O ---------------------
void saveConfig() {
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, cfg);

  if (cfg.magic != 0xA5FEEDEF) {
    // Fresh defaults
    cfg.magic      = 0xA5FEEDEF;
    cfg.version    = 2;
    cfg.feed1_h    = DEFAULT_FEED1_H; cfg.feed1_m = DEFAULT_FEED1_M;
    cfg.feed2_h    = DEFAULT_FEED2_H; cfg.feed2_m = DEFAULT_FEED2_M;
    cfg.closedAngle= DEFAULT_CLOSED_ANGLE;
    cfg.openAngle  = DEFAULT_OPEN_ANGLE;
    cfg.openMs     = DEFAULT_OPEN_MS;
    cfg.lastFeed1YMD = cfg.lastFeed2YMD = 0;
    cfg.lastFeed1TS  = cfg.lastFeed2TS  = 0;
    cfg.lastAnyFeedTS= 0;
    saveConfig();
    return;
  }

  // Migrate v1 → v2 safely
  if (cfg.version < 2) {
    cfg.version = 2;
    if (cfg.lastFeed1TS == 0)   cfg.lastFeed1TS = 0;
    if (cfg.lastFeed2TS == 0)   cfg.lastFeed2TS = 0;
    if (cfg.lastAnyFeedTS == 0) cfg.lastAnyFeedTS = 0;
    saveConfig();
  }
}

// --------------- Dispense Control ---------------
void dispense() {
  feederServo.attach(SERVO_PIN);

  // 3 pulses (adjust as you like)
  for (int i = 0; i < 3; i++) {
    feederServo.write(cfg.openAngle);
    delay(cfg.openMs);
    feederServo.write(cfg.closedAngle);
    delay(400);
  }

  feederServo.detach();
}

void markFed(uint8_t slot /*1/2 or 0 for manual/unknown*/) {
  uint32_t today = ymdToday();
  uint32_t ts    = nowEpoch();

  if (slot == 1) {
    cfg.lastFeed1YMD = today;
    cfg.lastFeed1TS  = ts;
  } else if (slot == 2) {
    cfg.lastFeed2YMD = today;
    cfg.lastFeed2TS  = ts;
  }
  cfg.lastAnyFeedTS = ts;
  saveConfig();
}

// Catch-up on boot if a scheduled feed was missed while offline
void catchUpIfMissed() {
  uint32_t today = ymdToday();
  if (nowAtOrAfter(cfg.feed1_h, cfg.feed1_m) && cfg.lastFeed1YMD != today) {
    dispense();
    markFed(1);
  }
  if (nowAtOrAfter(cfg.feed2_h, cfg.feed2_m) && cfg.lastFeed2YMD != today) {
    dispense();
    markFed(2);
  }
}

// ---------------- Web UI Page -------------------
String pageHtml() {
  String html = R"====(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Fish Feeder</title>
<style>
body{font-family:system-ui,Arial,sans-serif;max-width:720px;margin:24px auto;padding:0 12px}
.card{border:1px solid #ddd;border-radius:12px;padding:16px;margin:12px 0}
h1{font-size:22px;margin:8px 0}
label{display:block;margin:8px 0 4px}
input{padding:8px;border:1px solid #ccc;border-radius:8px;width:100%;box-sizing:border-box}
button{padding:10px 16px;border:0;border-radius:10px;background:#0b7;color:#fff;cursor:pointer}
button.secondary{background:#06c}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.small{opacity:.7;font-size:13px}
code{background:#f6f6f6;padding:2px 4px;border-radius:6px}
</style>
</head>
<body>
<h1>Automatic Fish Feeder</h1>

<div class="card">
  <div><b>Now:</b> %%NOW%%</div>
  <div><b>Wi-Fi:</b> %%WIFI%%</div>
  <div><b>Feed Times:</b> %%F1%% &amp; %%F2%% (Asia/Dhaka)</div>
  <div class="small">
    Last feed #1 (date): %%LF1%% | #2: %%LF2%%<br/>
    Last time #1: %%LF1TS%% | #2: %%LF2TS%%<br/>
    <b>Last feed (any):</b> %%LATS%%
  </div>
</div>

<div class="card">
  <form method="POST" action="/save">
    <h3>Configure</h3>
    <div class="grid">
      <div>
        <label>Feed #1 (HH)</label>
        <input name="f1h" type="number" min="0" max="23" value="%%F1H%%">
      </div>
      <div>
        <label>(MM)</label>
        <input name="f1m" type="number" min="0" max="59" value="%%F1M%%">
      </div>
      <div>
        <label>Feed #2 (HH)</label>
        <input name="f2h" type="number" min="0" max="23" value="%%F2H%%">
      </div>
      <div>
        <label>(MM)</label>
        <input name="f2m" type="number" min="0" max="59" value="%%F2M%%">
      </div>
    </div>

    <div class="grid">
      <div>
        <label>Closed Angle (°)</label>
        <input name="cang" type="number" min="0" max="180" value="%%CANG%%">
      </div>
      <div>
        <label>Open Angle (°)</label>
        <input name="oang" type="number" min="0" max="180" value="%%OANG%%">
      </div>
    </div>

    <div>
      <label>Open Duration (ms)</label>
      <input name="oms" type="number" min="100" max="5000" value="%%OMS%%">
    </div>

    <p><button type="submit" class="secondary">Save Settings</button></p>
  </form>
</div>

<div class="card">
  <h3>Manual Feed</h3>
  <p><a href="/feed"><button>Dispense Now</button></a></p>
  <p class="small">Tip: mark a slot with <code>/feed?slot=1</code> or <code>/feed?slot=2</code>.</p>
</div>

<div class="small">© Feeder on ESP8266</div>
</body>
</html>
)====";

  auto ymdToStr = [](uint32_t ymd)->String{
    if (!ymd) return String("-");
    uint16_t y = ymd/10000;
    uint8_t  m = (ymd/100)%100;
    uint8_t  d = ymd%100;
    char b[16]; sprintf(b,"%04u-%02u-%02u", y,m,d);
    return String(b);
  };

  String wifi = WiFi.isConnected() ? (String("Connected (") + WiFi.localIP().toString() + ")") : "Disconnected";

  html.replace("%%NOW%%", timeString());
  html.replace("%%WIFI%%", wifi);

  {
    char b[16];
    sprintf(b, "%02u:%02u", cfg.feed1_h, cfg.feed1_m);
    html.replace("%%F1%%", String(b));
    sprintf(b, "%02u:%02u", cfg.feed2_h, cfg.feed2_m);
    html.replace("%%F2%%", String(b));
  }

  html.replace("%%LF1%%",   ymdToStr(cfg.lastFeed1YMD));
  html.replace("%%LF2%%",   ymdToStr(cfg.lastFeed2YMD));
  html.replace("%%LF1TS%%", tsToLocalString(cfg.lastFeed1TS));
  html.replace("%%LF2TS%%", tsToLocalString(cfg.lastFeed2TS));
  html.replace("%%LATS%%",  tsToLocalString(cfg.lastAnyFeedTS));

  html.replace("%%F1H%%", String(cfg.feed1_h));
  html.replace("%%F1M%%", String(cfg.feed1_m));
  html.replace("%%F2H%%", String(cfg.feed2_h));
  html.replace("%%F2M%%", String(cfg.feed2_m));
  html.replace("%%CANG%%", String(cfg.closedAngle));
  html.replace("%%OANG%%", String(cfg.openAngle));
  html.replace("%%OMS%%",  String(cfg.openMs));
  return html;
}

// ----------------- HTTP Handlers ----------------
void handleRoot() {
  server.send(200, "text/html; charset=utf-8", pageHtml());
}

void handleSave() {
  auto clamp8 = [](long v, long lo, long hi){ return (uint8_t)(v<lo?lo:(v>hi?hi:v)); };

  if (server.hasArg("f1h")) cfg.feed1_h    = clamp8(server.arg("f1h").toInt(), 0, 23);
  if (server.hasArg("f1m")) cfg.feed1_m    = clamp8(server.arg("f1m").toInt(), 0, 59);
  if (server.hasArg("f2h")) cfg.feed2_h    = clamp8(server.arg("f2h").toInt(), 0, 23);
  if (server.hasArg("f2m")) cfg.feed2_m    = clamp8(server.arg("f2m").toInt(), 0, 59);
  if (server.hasArg("cang")) cfg.closedAngle = clamp8(server.arg("cang").toInt(), 0, 180);
  if (server.hasArg("oang")) cfg.openAngle   = clamp8(server.arg("oang").toInt(), 0, 180);
  if (server.hasArg("oms"))  cfg.openMs      = (uint16_t) max(100L, min(5000L, server.arg("oms").toInt()));

  saveConfig();
  server.sendHeader("Location", "/");
  server.send(302);
}

void handleFeed() {
  dispense();
  if (server.hasArg("slot")) {
    int s = server.arg("slot").toInt();
    if (s == 1)      markFed(1);
    else if (s == 2) markFed(2);
    else             markFed(0);
  } else {
    markFed(0);
  }
  server.sendHeader("Location", "/");
  server.send(302);
}

void handleStatus() {
  String json = "{";
  json += "\"now\":\"" + timeString() + "\",";
  json += "\"wifi\":\"" + String(WiFi.isConnected() ? "connected" : "disconnected") + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"feed1\":\"" + String(cfg.feed1_h) + ":" + String(cfg.feed1_m) + "\",";
  json += "\"feed2\":\"" + String(cfg.feed2_h) + ":" + String(cfg.feed2_m) + "\",";
  json += "\"last1_date\":" + String(cfg.lastFeed1YMD) + ",";
  json += "\"last2_date\":" + String(cfg.lastFeed2YMD) + ",";
  json += "\"last1_ts\":" + String(cfg.lastFeed1TS) + ",";
  json += "\"last2_ts\":" + String(cfg.lastFeed2TS) + ",";
  json += "\"last_any_ts\":" + String(cfg.lastAnyFeedTS) + "}";
  server.send(200, "application/json", json);
}

// --------------- Wi-Fi + NTP Setup --------------
void setupWiFiAndTime() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
    delay(250);
  }

  // NTP time with TZ offset
  configTime(TZ_OFFSET, DST_OFFSET, NTP1, NTP2);

  // Wait until time is set (beyond 1970-01-01)
  time_t now = time(nullptr);
  for (int i=0; i<20 && now < 8*3600*24; i++) {
    delay(500);
    now = time(nullptr);
  }
}

// ================== Arduino Setup =================
void setup() {
  // Serial.begin(115200); delay(300); // optional for debugging

  loadConfig();
  pinMode(SERVO_PIN, OUTPUT);

  setupWiFiAndTime();

  server.on("/",            HTTP_GET,  handleRoot);
  server.on("/save",        HTTP_POST, handleSave);
  server.on("/feed",        HTTP_ANY,  handleFeed);
  server.on("/api/status",  HTTP_GET,  handleStatus);
  server.begin();

  // Catch up missed feeds on boot (if time is valid)
  if (time(nullptr) > 8*3600*24) {
    catchUpIfMissed();
  }
}

// ================== Arduino Loop ==================
unsigned long lastCheckMs = 0;

void loop() {
  server.handleClient();

  // Check schedule every ~2s
  if (millis() - lastCheckMs >= 2000) {
    lastCheckMs = millis();

    if (time(nullptr) > 8*3600*24) {
      uint32_t today = ymdToday();

      // Feed #1
      if (nowAtOrAfter(cfg.feed1_h, cfg.feed1_m) && cfg.lastFeed1YMD != today) {
        dispense();
        markFed(1);
      }
      // Feed #2
      if (nowAtOrAfter(cfg.feed2_h, cfg.feed2_m) && cfg.lastFeed2YMD != today) {
        dispense();
        markFed(2);
      }
    }

    // Reconnect Wi-Fi if needed
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
    }
  }
}
