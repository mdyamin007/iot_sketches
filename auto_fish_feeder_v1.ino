#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <time.h>
#include <EEPROM.h>
#include <Servo.h>

// ====== Wi-Fi ক্রেডেনশিয়াল ======
const char* WIFI_SSID     = "Bedroom 2 - Yamin";
const char* WIFI_PASSWORD = "78789898";

// ====== ওয়েব সার্ভার ======
ESP8266WebServer server(80);

// ====== সার্ভো সেটআপ ======
Servo feederServo;
const int SERVO_PIN = D4; // GPIO2
// ডিফল্ট অ্যাঙ্গেল/ডিউরেশন (ওয়েব UI থেকে বদলানো যাবে)
uint8_t DEFAULT_CLOSED_ANGLE = 0;
uint8_t DEFAULT_OPEN_ANGLE   = 180;
uint16_t DEFAULT_OPEN_MS     = 400; // কতক্ষণ খোলা থাকবে (মিলিসেকেন্ড)

// ====== ফিড টাইম (ডিফল্ট) ======
uint8_t DEFAULT_FEED1_H = 8,  DEFAULT_FEED1_M = 0;   // সকাল ৮:০০
uint8_t DEFAULT_FEED2_H = 20, DEFAULT_FEED2_M = 0;   // সন্ধ্যা ৮:০০

// ====== EEPROM সেভড কনফিগ ======
struct Config {
  uint32_t magic = 0xA5FEEDEF;
  uint8_t  version = 1;

  uint8_t  feed1_h, feed1_m;
  uint8_t  feed2_h, feed2_m;

  uint8_t  closedAngle;
  uint8_t  openAngle;
  uint16_t openMs;

  // ডাবল-ফিড এড়াতে YYYYMMDD ফরম্যাটে “শেষ ফিড” দিন সেভ
  uint32_t lastFeed1YMD = 0;
  uint32_t lastFeed2YMD = 0;
} cfg;

static const uint16_t EEPROM_SIZE = sizeof(Config);

// ====== সময়/টাইমজোন (Asia/Dhaka, UTC+6, no DST) ======
const long TZ_OFFSET = 6 * 3600; // সেকেন্ড
const long DST_OFFSET = 0;       // বাংলাদেশে সাধারণত DST নেই
const char* NTP1 = "bd.pool.ntp.org";
const char* NTP2 = "pool.ntp.org";

// ইউটিল: YYYYMMDD হিসেবে আজকের তারিখ
uint32_t ymdToday() {
  time_t now = time(nullptr);
  struct tm tinfo;
  localtime_r(&now, &tinfo);
  return (uint32_t)((tinfo.tm_year + 1900) * 10000 + (tinfo.tm_mon + 1) * 100 + tinfo.tm_mday);
}

// ইউটিল: now ≥ (H:M) কি না
bool nowAtOrAfter(uint8_t H, uint8_t M) {
  time_t now = time(nullptr);
  struct tm tinfo;
  localtime_r(&now, &tinfo);
  int nowMin = tinfo.tm_hour * 60 + tinfo.tm_min;
  int trgMin = (int)H * 60 + (int)M;
  return nowMin >= trgMin;
}

// ইউটিল: বর্তমান সময় স্ট্রিং
String timeString() {
  time_t now = time(nullptr);
  struct tm tinfo;
  localtime_r(&now, &tinfo);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tinfo);
  return String(buf);
}

// EEPROM লোড/সেভ
void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, cfg);
  if (cfg.magic != 0xA5FEEDEF || cfg.version != 1) {
    // ডিফল্ট সেটিংস
    cfg.magic = 0xA5FEEDEF;
    cfg.version = 1;
    cfg.feed1_h = DEFAULT_FEED1_H; cfg.feed1_m = DEFAULT_FEED1_M;
    cfg.feed2_h = DEFAULT_FEED2_H; cfg.feed2_m = DEFAULT_FEED2_M;
    cfg.closedAngle = DEFAULT_CLOSED_ANGLE;
    cfg.openAngle   = DEFAULT_OPEN_ANGLE;
    cfg.openMs      = DEFAULT_OPEN_MS;
    cfg.lastFeed1YMD = 0;
    cfg.lastFeed2YMD = 0;
    EEPROM.put(0, cfg);
    EEPROM.commit();
  }
}

void saveConfig() {
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

// সার্ভো চালানো (ডিসপেন্স)
void dispense() {
  feederServo.attach(SERVO_PIN);
  feederServo.write(cfg.openAngle);
  delay(cfg.openMs);
  feederServo.write(cfg.closedAngle);
  delay(400);
  feederServo.write(cfg.openAngle);
  delay(cfg.openMs);
  feederServo.write(cfg.closedAngle);
  delay(400);
  feederServo.write(cfg.openAngle);
  delay(cfg.openMs);
  feederServo.write(cfg.closedAngle);
  delay(400);
  feederServo.detach();
}

// “ক্যাচ-আপ” লজিক: বুটের পর যদি আজকের শিডিউল টাইম পেরিয়ে যায় এবং এখনো ফিড হয়নি, সঙ্গে সঙ্গে ফিড
void catchUpIfMissed() {
  uint32_t today = ymdToday();
  // Feed 1
  if (nowAtOrAfter(cfg.feed1_h, cfg.feed1_m) && cfg.lastFeed1YMD != today) {
    dispense();
    cfg.lastFeed1YMD = today;
    saveConfig();
  }
  // Feed 2
  if (nowAtOrAfter(cfg.feed2_h, cfg.feed2_m) && cfg.lastFeed2YMD != today) {
    dispense();
    cfg.lastFeed2YMD = today;
    saveConfig();
  }
}

// ওয়েব UI HTML
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
button{padding:10px 16px;border:0;border-radius:10px;background:#0b7; color:#fff;cursor:pointer}
button.secondary{background:#06c}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.small{opacity:.7;font-size:13px}
</style>
</head>
<body>
<h1>Automatic Fish Feeder</h1>

<div class="card">
  <div><b>Now:</b> %%NOW%%</div>
  <div><b>Wi-Fi:</b> %%WIFI%%</div>
  <div><b>Feed Times:</b> %%F1%% &amp; %%F2%% (Asia/Dhaka)</div>
  <div class="small">Last feed #1: %%LF1%% | Last feed #2: %%LF2%%</div>
</div>

<div class="card">
  <form method="POST" action="/save">
    <h3>Configure</h3>
    <div class="grid">
      <div>
        <label>Feed #1 (HH:MM)</label>
        <input name="f1h" type="number" min="0" max="23" value="%%F1H%%">
      </div>
      <div style="margin-top:28px">
        <input name="f1m" type="number" min="0" max="59" value="%%F1M%%">
      </div>
      <div>
        <label>Feed #2 (HH:MM)</label>
        <input name="f2h" type="number" min="0" max="23" value="%%F2H%%">
      </div>
      <div style="margin-top:28px">
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
  <p><a href="/feed" ><button>Dispense Now</button></a></p>
  <p class="small">Tip: #1 বা #2 স্লট হিসেবে মার্ক করতে চাইলে <code>/feed?slot=1</code> বা <code>/feed?slot=2</code> ইউআরএল ব্যবহার করুন।</p>
</div>

<div class="small">© Feeder on ESP8266</div>
</body>
</html>
)====";

  // টোকেন রিপ্লেস
  auto ymdToStr = [](uint32_t ymd)->String{
    if(!ymd) return String("-");
    uint16_t y = ymd/10000;
    uint8_t m = (ymd/100)%100;
    uint8_t d = ymd%100;
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
  html.replace("%%LF1%%", ymdToStr(cfg.lastFeed1YMD));
  html.replace("%%LF2%%", ymdToStr(cfg.lastFeed2YMD));
  html.replace("%%F1H%%", String(cfg.feed1_h));
  html.replace("%%F1M%%", String(cfg.feed1_m));
  html.replace("%%F2H%%", String(cfg.feed2_h));
  html.replace("%%F2M%%", String(cfg.feed2_m));
  html.replace("%%CANG%%", String(cfg.closedAngle));
  html.replace("%%OANG%%", String(cfg.openAngle));
  html.replace("%%OMS%%",  String(cfg.openMs));
  return html;
}

// রুট হ্যান্ডলারস
void handleRoot() {
  server.send(200, "text/html; charset=utf-8", pageHtml());
}

void handleSave() {
  // ফর্ম ভ্যালু পড়া + ভ্যালিডেশন
  auto clamp = [](long v, long lo, long hi){ return (uint8_t) (v<lo?lo:(v>hi?hi:v)); };

  if (server.hasArg("f1h")) cfg.feed1_h = clamp(server.arg("f1h").toInt(), 0, 23);
  if (server.hasArg("f1m")) cfg.feed1_m = clamp(server.arg("f1m").toInt(), 0, 59);
  if (server.hasArg("f2h")) cfg.feed2_h = clamp(server.arg("f2h").toInt(), 0, 23);
  if (server.hasArg("f2m")) cfg.feed2_m = clamp(server.arg("f2m").toInt(), 0, 59);

  if (server.hasArg("cang")) cfg.closedAngle = clamp(server.arg("cang").toInt(), 0, 180);
  if (server.hasArg("oang")) cfg.openAngle   = clamp(server.arg("oang").toInt(), 0, 180);
  if (server.hasArg("oms"))  cfg.openMs      = (uint16_t) max(100L, min(5000L, server.arg("oms").toInt()));

  saveConfig();
  server.sendHeader("Location", "/");
  server.send(302);
}

void handleFeed() {
  dispense();
  // চাইলে #1/#2 হিসেবে “ডান” মার্ক করা
  if (server.hasArg("slot")) {
    uint32_t today = ymdToday();
    int s = server.arg("slot").toInt();
    if (s == 1) cfg.lastFeed1YMD = today;
    if (s == 2) cfg.lastFeed2YMD = today;
    saveConfig();
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
  json += "\"last1\":" + String(cfg.lastFeed1YMD) + ",";
  json += "\"last2\":" + String(cfg.lastFeed2YMD) + "}";
  server.send(200, "application/json", json);
}

// ওয়াই-ফাই + টাইম সিঙ্ক
void setupWiFiAndTime() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
    delay(250);
  }

  // NTP টাইম (Asia/Dhaka offset)
  configTime(TZ_OFFSET, DST_OFFSET, NTP1, NTP2);

  // টাইম লোড হওয়া পর্যন্ত অপেক্ষা (টাইম > 1970?)
  time_t now = time(nullptr);
  for (int i=0; i<20 && now < 8*3600*24; i++) {
    delay(500);
    now = time(nullptr);
  }
}

void setup() {
  // সিরিয়াল ডিবাগ অত আবশ্যক না — চাইলে চালু করুন
  // Serial.begin(115200); delay(300);

  loadConfig();
  pinMode(SERVO_PIN, OUTPUT);

  setupWiFiAndTime();

  // ওয়েব সার্ভার রুটস
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/feed", HTTP_ANY, handleFeed);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.begin();

  // বুটে “মিসড ফিড” ক্যাচ-আপ (টাইম পেলে তবেই)
  if (time(nullptr) > 8*3600*24) {
    catchUpIfMissed();
  }
}

unsigned long lastCheckMs = 0;

void loop() {
  server.handleClient();

  // প্রতি 2 সেকেন্ডে শিডিউল চেক
  if (millis() - lastCheckMs >= 2000) {
    lastCheckMs = millis();
    if (time(nullptr) > 8*3600*24) {
      uint32_t today = ymdToday();
      // ফিড #1
      if (nowAtOrAfter(cfg.feed1_h, cfg.feed1_m) && cfg.lastFeed1YMD != today) {
        dispense();
        cfg.lastFeed1YMD = today;
        saveConfig();
      }
      // ফিড #2
      if (nowAtOrAfter(cfg.feed2_h, cfg.feed2_m) && cfg.lastFeed2YMD != today) {
        dispense();
        cfg.lastFeed2YMD = today;
        saveConfig();
      }
    }

    // ওয়াই-ফাই হারালে রিট্রাই
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
    }
  }
}

