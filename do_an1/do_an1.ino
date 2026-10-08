/*
 * ĐỒ ÁN 1 – GIÁM SÁT MÔI TRƯỜNG & ĐIỀU KHIỂN THIẾT BỊ (ESP32 DevKit 30 chân)
 * ---------------------------------------------------------------------------
 * Cảm biến : DHT22 (nhiệt độ, độ ẩm) · BH1750 (ánh sáng, I2C) · MQ-135 (chất lượng
 *            không khí, analog) · PIR (chuyển động)
 * Chấp hành: module relay 2 kênh – Relay 1 = đèn, Relay 2 = quạt/thông gió
 * Web      : ESP32 tự phát trang web (file index_html.h) và REST API JSON.
 *            Mở http://<IP-ESP32> hoặc http://doan1.local trên máy cùng mạng WiFi.
 *
 * Gán chân (theo sơ đồ Proteus do_an1):
 *   PIR OUT -> GPIO12      DHT22 DATA -> GPIO14      MQ-135 AO -> GPIO35
 *   BH1750 SDA -> GPIO21   BH1750 SCL -> GPIO22
 *   Relay IN1 -> GPIO25    Relay IN2 -> GPIO26
 *
 * Thư viện cần cài (Arduino IDE > Library Manager):
 *   - "DHT sensor library" (Adafruit) + "Adafruit Unified Sensor"
 *   - "BH1750" (Christopher Laws)
 * Board: "ESP32 Dev Module" (gói esp32 by Espressif, bản 2.x hoặc 3.x)
 */
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Wire.h>
#include <time.h>
#include <DHT.h>
#include <BH1750.h>
#include "index_html.h"

// ============================== CẤU HÌNH ==============================
const char *WIFI_SSID = "HO TRO SINH VIEN";        // <-- sửa tên WiFi
const char *WIFI_PASS = "12345678@";    // <-- sửa mật khẩu WiFi
const char *AP_SSID   = "";    // WiFi do ESP32 tự phát nếu không vào được WiFi trên
const char *AP_PASS   = "";       // tối thiểu 8 ký tự
const char *HOSTNAME  = "doan1";          // http://doan1.local

#define PIN_PIR     12      // GPIO12 là chân strapping: PIR phải ở mức THẤP lúc cấp điện
#define PIN_DHT     14
#define PIN_MQ135   35
#define PIN_SDA     21
#define PIN_SCL     22
#define PIN_RELAY1  25
#define PIN_RELAY2  26
#define DHT_TYPE    DHT22

const bool  RELAY_ACTIVE_LOW = true;   // module relay thông dụng kích mức THẤP; đổi false nếu kích mức cao
const float MQ_DIVIDER       = 1.0f;   // hệ số cầu phân áp ở AO (vd 10k/20k -> 0.667); 1.0 nếu nối thẳng
const uint32_t MQ_WARMUP_MS  = 60000;  // bỏ qua luật khí trong 60 s đầu (cảm biến đang nóng lên)
const float T_HYST           = 1.0f;   // quạt tắt khi nhiệt độ xuống dưới ngưỡng - 1 °C
const int   GAS_HYST         = 200;    // quạt tắt khi MQ-135 xuống dưới ngưỡng - 200
const uint32_t FAN_MIN_ON_MS = 10000;  // quạt bật tối thiểu 10 s để không đóng cắt liên tục

// Ngưỡng mặc định (đổi được trên web, lưu vào Flash)
struct Config {
  float luxOn = 50;      // trời tối khi lux < luxOn
  int   hold  = 30;      // giữ đèn bao nhiêu giây sau lần chuyển động cuối
  float tOn   = 32;      // bật quạt khi nhiệt độ >= tOn
  int   gasOn = 2000;    // bật quạt khi MQ-135 (ADC 0-4095) >= gasOn
} cfg;

// ============================== BIẾN TOÀN CỤC ==============================
WebServer server(80);
Preferences prefs;
DHT dht(PIN_DHT, DHT_TYPE);
BH1750 lightMeter;
bool bhOk = false;

float tempC = NAN, humi = NAN, lux = -1;
float gasAdc = 0, gasVolt = 0;
bool  motion = false;
uint32_t lastMotionMs = 0;
bool  everMotion = false;

bool relayOn[3]  = {false, false, false};   // chỉ dùng chỉ số 1, 2
bool autoMode[3] = {false, true, true};
String why[3]    = {"", "Khởi động", "Khởi động"};
uint32_t relayChangedMs[3] = {0, 0, 0};

bool apMode = false;
uint32_t bootMs = 0;

// Nhật ký sự kiện (vòng 12 dòng)
const int EV_MAX = 12;
String evBuf[EV_MAX];
int evCount = 0;          // tổng số sự kiện đã ghi (tăng dần)

// ============================== TIỆN ÍCH ==============================
String clockStr() {
  struct tm ti;
  if (!apMode && getLocalTime(&ti, 10)) {
    char b[12];
    strftime(b, sizeof(b), "%H:%M:%S", &ti);
    return String(b);
  }
  uint32_t s = (millis() - bootMs) / 1000;
  char b[16];
  snprintf(b, sizeof(b), "+%02lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
  return String(b);
}

void logEvent(const String &msg) {
  String line = clockStr() + "  " + msg;
  evBuf[evCount % EV_MAX] = line;
  evCount++;
  Serial.println("[EV] " + line);
}

void writeRelay(int ch, bool on) {
  int pin = (ch == 1) ? PIN_RELAY1 : PIN_RELAY2;
  digitalWrite(pin, (on ^ RELAY_ACTIVE_LOW) ? HIGH : LOW);
}

void setRelay(int ch, bool on, const String &reason) {
  why[ch] = reason;
  if (relayOn[ch] == on) return;
  relayOn[ch] = on;
  relayChangedMs[ch] = millis();
  writeRelay(ch, on);
  logEvent(String(ch == 1 ? "Đèn" : "Quạt") + (on ? " BẬT – " : " TẮT – ") + reason);
}

String jnum(float v, int dec) {          // số -> JSON, NaN -> null
  if (isnan(v)) return "null";
  return String(v, dec);
}

String jstr(const String &s) {           // chuỗi -> JSON (thoát ký tự đặc biệt)
  String o = "\"";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else o += c;
  }
  return o + "\"";
}

// ============================== LƯU / ĐỌC CẤU HÌNH ==============================
void loadConfig() {
  prefs.begin("doan1", true);
  cfg.luxOn = prefs.getFloat("luxOn", cfg.luxOn);
  cfg.hold  = prefs.getInt("hold", cfg.hold);
  cfg.tOn   = prefs.getFloat("tOn", cfg.tOn);
  cfg.gasOn = prefs.getInt("gasOn", cfg.gasOn);
  autoMode[1] = prefs.getBool("a1", true);
  autoMode[2] = prefs.getBool("a2", true);
  prefs.end();
}

void saveConfig() {
  prefs.begin("doan1", false);
  prefs.putFloat("luxOn", cfg.luxOn);
  prefs.putInt("hold", cfg.hold);
  prefs.putFloat("tOn", cfg.tOn);
  prefs.putInt("gasOn", cfg.gasOn);
  prefs.putBool("a1", autoMode[1]);
  prefs.putBool("a2", autoMode[2]);
  prefs.end();
}

// ============================== ĐỌC CẢM BIẾN ==============================
void readSensors() {
  static uint32_t tDht = 0, tFast = 0;
  uint32_t now = millis();

  // PIR: đọc liên tục
  bool p = digitalRead(PIN_PIR) == HIGH;
  if (p) { lastMotionMs = now; everMotion = true; }
  if (p != motion) {
    motion = p;
    if (p) logEvent("PIR phát hiện chuyển động");
  }

  // BH1750 + MQ-135: mỗi 500 ms
  if (now - tFast >= 500) {
    tFast = now;
    if (bhOk) {
      float l = lightMeter.readLightLevel();
      lux = (l < 0) ? -1 : l;
    }
    uint32_t sum = 0, mv = 0;
    for (int i = 0; i < 16; i++) { sum += analogRead(PIN_MQ135); mv += analogReadMilliVolts(PIN_MQ135); }
    float raw = sum / 16.0f;
    gasAdc  = (gasAdc == 0) ? raw : gasAdc * 0.8f + raw * 0.2f;          // lọc trung bình trượt
    gasVolt = (mv / 16.0f) / 1000.0f / MQ_DIVIDER;                        // điện áp thật ở chân AO
  }

  // DHT22: tối thiểu 2 s một lần
  if (now - tDht >= 2000) {
    tDht = now;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    tempC = t;          // NAN nếu lỗi đọc
    humi  = h;
  }
}

// ============================== LUẬT TỰ ĐỘNG ==============================
void autoControl() {
  uint32_t now = millis();

  // Relay 1 – Đèn: bật khi có người và trời tối; khi đèn đã bật thì bỏ qua lux
  // (ánh sáng của chính đèn làm lux tăng), chỉ tắt khi hết chuyển động sau "hold" giây.
  if (autoMode[1]) {
    bool recent = everMotion && (now - lastMotionMs) < (uint32_t)cfg.hold * 1000UL;
    bool dark   = (lux < 0) || (lux < cfg.luxOn);       // lỗi BH1750 thì coi như tối
    if (!relayOn[1]) {
      if (motion && dark) setRelay(1, true, "Có người, trời tối (" + String(lux, 0) + " lux)");
      else why[1] = motion ? "Có người nhưng trời sáng" : "Không có người";
    } else if (!recent) {
      setRelay(1, false, "Không có chuyển động trong " + String(cfg.hold) + " s");
    } else {
      why[1] = "Đang giữ đèn, còn " + String((cfg.hold * 1000UL - (now - lastMotionMs)) / 1000) + " s";
    }
  }

  // Relay 2 – Quạt: bật khi nóng hoặc không khí kém; tắt khi cả hai đã về dưới ngưỡng trừ trễ
  if (autoMode[2]) {
    bool warm = (now - bootMs) >= MQ_WARMUP_MS;
    bool hot  = !isnan(tempC) && tempC >= cfg.tOn;
    bool cool = isnan(tempC) || tempC <= cfg.tOn - T_HYST;
    bool bad  = warm && gasAdc >= cfg.gasOn;
    bool good = !warm || gasAdc <= cfg.gasOn - GAS_HYST;
    if (!relayOn[2]) {
      if (hot || bad) setRelay(2, true, hot ? "Nóng " + String(tempC, 1) + " °C" : "Không khí kém (" + String(gasAdc, 0) + ")");
      else why[2] = "Nhiệt độ và không khí bình thường";
    } else if (cool && good && (now - relayChangedMs[2]) >= FAN_MIN_ON_MS) {
      setRelay(2, false, "Đã mát và không khí tốt");
    } else {
      why[2] = hot ? "Đang nóng" : (bad ? "Không khí còn kém" : "Chờ về dưới ngưỡng trừ dải trễ");
    }
  }
}

// ============================== WEB ==============================
String stateJson() {
  uint32_t now = millis();
  int warmLeft = (now - bootMs) >= MQ_WARMUP_MS ? 0 : (MQ_WARMUP_MS - (now - bootMs)) / 1000;
  long ago = everMotion ? (long)((now - lastMotionMs) / 1000) : -1;

  String j = "{";
  j += "\"t\":" + jnum(tempC, 1);
  j += ",\"h\":" + jnum(humi, 1);
  j += ",\"lux\":" + (lux < 0 ? String("null") : String(lux, 1));
  j += ",\"gas\":" + String(gasAdc, 0);
  j += ",\"gasV\":" + String(gasVolt, 3);
  j += ",\"warm\":" + String(warmLeft);
  j += ",\"motion\":" + String(motion ? 1 : 0);
  j += ",\"motionAgo\":" + String(ago);
  for (int ch = 1; ch <= 2; ch++) {
    j += ",\"r" + String(ch) + "\":" + String(relayOn[ch] ? 1 : 0);
    j += ",\"a" + String(ch) + "\":" + String(autoMode[ch] ? 1 : 0);
    j += ",\"why" + String(ch) + "\":" + jstr(why[ch]);
  }
  j += ",\"cfg\":{\"luxOn\":" + String(cfg.luxOn, 0) + ",\"hold\":" + String(cfg.hold) +
       ",\"tOn\":" + String(cfg.tOn, 1) + ",\"gasOn\":" + String(cfg.gasOn) + "}";
  j += ",\"evn\":" + String(evCount) + ",\"ev\":[";
  int n = evCount < EV_MAX ? evCount : EV_MAX;
  for (int i = 0; i < n; i++) {                       // mới nhất lên đầu
    if (i) j += ",";
    j += jstr(evBuf[(evCount - 1 - i) % EV_MAX]);
  }
  j += "]";
  j += ",\"ip\":" + jstr(apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString());
  j += ",\"mode\":" + jstr(apMode ? String("AP ") + AP_SSID : String(WIFI_SSID));
  j += ",\"rssi\":" + String(apMode ? 0 : WiFi.RSSI());
  j += ",\"clock\":" + jstr(clockStr());
  j += ",\"uptime\":" + String((now - bootMs) / 1000);
  j += "}";
  return j;
}

void sendJson(const String &body, int code = 200) {
  server.sendHeader("Access-Control-Allow-Origin", "*");   // cho phép mở index.html trực tiếp từ máy tính
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json; charset=utf-8", body);
}

void sendError(const String &msg) { sendJson("{\"error\":" + jstr(msg) + "}", 400); }

void handleRoot()  { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); }
void handleState() { sendJson(stateJson()); }

void handleRelay() {                    // POST /api/relay?ch=1&on=1  -> chuyển relay đó sang MANUAL
  int ch = server.arg("ch").toInt();
  if ((ch != 1 && ch != 2) || !server.hasArg("on")) return sendError("Cần ch=1|2 và on=0|1");
  bool on = server.arg("on").toInt() == 1;
  if (autoMode[ch]) { autoMode[ch] = false; saveConfig(); logEvent(String(ch == 1 ? "Đèn" : "Quạt") + " chuyển sang MANUAL"); }
  setRelay(ch, on, on ? "Bật tay từ web" : "Tắt tay từ web");
  sendJson(stateJson());
}

void handleAuto() {                     // POST /api/auto?ch=1&on=1
  int ch = server.arg("ch").toInt();
  if ((ch != 1 && ch != 2) || !server.hasArg("on")) return sendError("Cần ch=1|2 và on=0|1");
  autoMode[ch] = server.arg("on").toInt() == 1;
  saveConfig();
  logEvent(String(ch == 1 ? "Đèn" : "Quạt") + (autoMode[ch] ? " chuyển sang AUTO" : " chuyển sang MANUAL"));
  if (autoMode[ch]) autoControl();
  sendJson(stateJson());
}

void handleConfig() {                   // POST /api/config?luxOn=50&hold=30&tOn=32&gasOn=2000
  if (server.hasArg("luxOn")) cfg.luxOn = constrain(server.arg("luxOn").toFloat(), 0, 65000);
  if (server.hasArg("hold"))  cfg.hold  = constrain(server.arg("hold").toInt(), 5, 3600);
  if (server.hasArg("tOn"))   cfg.tOn   = constrain(server.arg("tOn").toFloat(), 10, 60);
  if (server.hasArg("gasOn")) cfg.gasOn = constrain(server.arg("gasOn").toInt(), 100, 4095);
  saveConfig();
  logEvent("Đổi ngưỡng: tối<" + String(cfg.luxOn, 0) + " lux, giữ " + String(cfg.hold) + " s, quạt " +
           String(cfg.tOn, 1) + " °C / MQ " + String(cfg.gasOn));
  sendJson(stateJson());
}

void handleNotFound() {
  if (server.method() == HTTP_OPTIONS) {               // preflight CORS
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.send(204);
    return;
  }
  server.send(404, "text/plain", "Not found");
}

// ============================== WIFI ==============================
void startWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Đang kết nối WiFi \"%s\"", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) { delay(300); Serial.print("."); }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    apMode = false;
    WiFi.setAutoReconnect(true);
    configTzTime("ICT-7", "pool.ntp.org", "time.google.com");     // giờ Việt Nam
    Serial.print("Đã vào WiFi. Mở trình duyệt: http://"); Serial.println(WiFi.localIP());
  } else {
    apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("Không vào được WiFi -> phát WiFi \"%s\" (mk %s). Mở http://", AP_SSID, AP_PASS);
    Serial.println(WiFi.softAPIP());
  }
  if (MDNS.begin(HOSTNAME)) { MDNS.addService("http", "tcp", 80); Serial.printf("Hoặc mở: http://%s.local\n", HOSTNAME); }
}

// ============================== SETUP / LOOP ==============================
void setup() {
  // Đặt relay ở trạng thái TẮT trước khi bật OUTPUT để relay không nháy lúc khởi động
  digitalWrite(PIN_RELAY1, RELAY_ACTIVE_LOW ? HIGH : LOW);
  digitalWrite(PIN_RELAY2, RELAY_ACTIVE_LOW ? HIGH : LOW);
  pinMode(PIN_RELAY1, OUTPUT);
  pinMode(PIN_RELAY2, OUTPUT);

  Serial.begin(115200);
  delay(200);
  bootMs = millis();
  Serial.println("\n=== DO AN 1 - GIAM SAT MOI TRUONG ===");

  pinMode(PIN_PIR, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_MQ135, ADC_11db);   // đo được tới ~3,1 V

  dht.begin();
  Wire.begin(PIN_SDA, PIN_SCL);
  bhOk = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  Serial.println(bhOk ? "BH1750 OK" : "Không tìm thấy BH1750 (kiểm tra SDA=21, SCL=22)");

  loadConfig();
  startWiFi();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/relay", HTTP_POST, handleRelay);
  server.on("/api/auto", HTTP_POST, handleAuto);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.onNotFound(handleNotFound);
  server.begin();

  logEvent(String("Khởi động – ") + (apMode ? "chế độ AP" : "đã vào WiFi"));
}

void loop() {
  server.handleClient();
  readSensors();

  static uint32_t tCtl = 0, tPrint = 0;
  uint32_t now = millis();
  if (now - tCtl >= 200) { tCtl = now; autoControl(); }

  if (now - tPrint >= 2000) {       // in trạng thái ra Serial Monitor (115200)
    tPrint = now;
    Serial.printf("T=%.1fC H=%.1f%% Lux=%.0f MQ=%.0f (%.2fV) PIR=%d | Den=%s(%s) Quat=%s(%s)\n",
                  tempC, humi, lux, gasAdc, gasVolt, motion,
                  relayOn[1] ? "ON" : "OFF", autoMode[1] ? "AUTO" : "MAN",
                  relayOn[2] ? "ON" : "OFF", autoMode[2] ? "AUTO" : "MAN");
  }
  delay(2);
}
