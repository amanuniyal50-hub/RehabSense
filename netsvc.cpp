// netsvc.cpp
#include "netsvc.h"

#include <DNSServer.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "feedback.h"
#include "sensing.h"
#include "storage.h"
#include "web_page.h"

using namespace rs;

namespace net {
namespace {

WebServer server(80);
DNSServer dns;
String apName;
uint32_t lastStaBegin = 0;
bool staWanted = false;

// Upload status is written by the uploader task and read by the UI/web.
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
char statusBuf[72] = "Not configured";
volatile uint32_t uploaded = 0;
TaskHandle_t uploaderHandle = nullptr;

// Never allocate (String/malloc) inside a critical section: copy to a stack buffer.
void getStatus(char* out, size_t n) {
  portENTER_CRITICAL(&statusMux);
  strncpy(out, statusBuf, n - 1);
  out[n - 1] = 0;
  portEXIT_CRITICAL(&statusMux);
}

void setStatus(const char* s) {
  portENTER_CRITICAL(&statusMux);
  strncpy(statusBuf, s, sizeof(statusBuf) - 1);
  statusBuf[sizeof(statusBuf) - 1] = 0;
  portEXIT_CRITICAL(&statusMux);
}

// ------------------------------ helpers -------------------------------------
// Lets the physio app (physio-app/, opened from disk or localhost) call the API.
void allowCors() { server.sendHeader("Access-Control-Allow-Origin", "*"); }

void sendJson(int code, const String& body) {
  allowCors();
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

String jsonEscape(const char* s) {
  String o;
  for (; *s; s++) {
    char c = *s;
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

// Validated float field. Records an error message if invalid.
struct Form {
  String errors;
  bool bad = false;
  void err(const char* field, const char* msg) {
    errors += errors.length() ? "," : "";
    errors += "\"";
    errors += field;
    errors += "\":\"";
    errors += msg;
    errors += "\"";
    bad = true;
  }
  bool num(const char* field, float lo, float hi, float& out, const char* msg) {
    if (!server.hasArg(field)) return false;  // not sent: keep old value
    String v = server.arg(field);
    v.trim();
    if (!v.length()) { err(field, "Enter a number"); return false; }
    char* end = nullptr;
    float f = strtof(v.c_str(), &end);
    if (end == v.c_str() || *end != 0 || f != f) { err(field, "Enter a number"); return false; }
    if (f < lo || f > hi) { err(field, msg); return false; }
    out = f;
    return true;
  }
};

// ------------------------------ handlers ------------------------------------
void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleLive() {
  LiveData d = sensing::snapshot();
  char b[960];
  const ExerciseParams& p = d.sessionActive ? d.params : store::config().params[0];
  // Clinical test progress (testPhase: rs::TestPhase, testCount: stands or trials).
  const TestPhase tp = d.testPhase;
  const bool testActive = tp != TestPhase::Idle && tp != TestPhase::Done && tp != TestPhase::Aborted;
  const TestResult& t = d.test;
  const bool pos = d.testKind == TestKind::PositionSense;
  snprintf(b, sizeof(b),
           "{\"active\":%s,\"exercise\":%u,\"mode\":%u,\"calibrated\":%s,\"cal\":%u,"
           "\"metric\":%.1f,\"knee\":%.1f,\"thigh\":%.1f,\"shin\":%.1f,\"rotation\":%.1f,"
           "\"phase\":%u,\"valid\":%u,\"attempts\":%u,\"hold\":%.1f,\"peak\":%.1f,\"faults\":%u,"
           "\"lost\":%s,\"thighOk\":%s,\"shinOk\":%s,\"hz\":%.0f,\"i2cErrors\":%u,\"pending\":%u,"
           "\"params\":{\"target\":%.0f,\"tol\":%.0f,\"limit\":%.0f,\"reps\":%u},"
           "\"testActive\":%s,\"testKind\":%u,\"testPhase\":%u,\"testTime\":%.1f,\"testCount\":%u,"
           "\"testLast\":%.1f,\"testTarget\":%.0f,\"testHold\":%.1f,\"testStage\":%u,\"testCd\":%u}",
           d.sessionActive ? "true" : "false", (unsigned)d.exercise, (unsigned)d.mode,
           d.calibrated ? "true" : "false", (unsigned)d.cal, d.metric, d.joint.kneeDeg,
           d.joint.thighElevDeg, d.joint.shinDeg, d.joint.rotationDeg, (unsigned)d.phase,
           (unsigned)d.stats.validReps, (unsigned)d.stats.attempts, d.holdElapsed, d.currentPeak,
           (unsigned)d.currentFaults, d.sensorsLost ? "true" : "false", d.thighOk ? "true" : "false",
           d.shinOk ? "true" : "false", d.sampleHz, (unsigned)d.i2cErrors,
           (unsigned)store::pendingUploads(), p.targetDeg, p.toleranceDeg, p.limitDeg,
           (unsigned)p.reps, testActive ? "true" : "false", (unsigned)d.testKind, (unsigned)tp,
           d.testElapsed, (unsigned)(pos ? t.trials : t.stands),
           pos && t.trials ? t.trialErr[t.trials - 1] : 0.f, t.targetDeg, d.testHold,
           (unsigned)d.testStage, (unsigned)d.testCountdown);
  sendJson(200, b);
}

String paramsJson(const ExerciseParams& p) {
  char b[200];
  snprintf(b, sizeof(b),
           "{\"target\":%.0f,\"tol\":%.0f,\"limit\":%.0f,\"hold\":%.1f,\"reps\":%u,\"speed\":%.0f,"
           "\"knee\":%.0f,\"rotate\":%.0f,\"rest\":%.0f}",
           p.targetDeg, p.toleranceDeg, p.limitDeg, p.holdSec, (unsigned)p.reps, p.maxSpeedDps,
           p.kneeBentDeg, p.rotateDeg, p.restDeg);
  return b;
}

void handleGetConfig() {
  AppConfig& c = store::config();
  String o = "{\"params\":[";
  o += paramsJson(c.params[0]);
  o += ",";
  o += paramsJson(c.params[1]);
  o += ",";
  o += paramsJson(c.params[2]);  // elbow flexion (firmware 1.4.0)
  o += "],\"buzzer\":";
  o += c.buzzerOn ? "true" : "false";
  o += ",\"safety\":";
  o += c.safetyAlertInAssessment ? "true" : "false";
  o += ",\"wifiSsid\":\"";
  o += jsonEscape(c.wifiSsid);
  o += "\",\"tsKeySet\":";
  o += c.tsKey[0] ? "true" : "false";
  o += ",\"staConnected\":";
  o += staConnected() ? "true" : "false";
  o += ",\"staIp\":\"";
  o += staIp();
  o += "\",\"ap\":\"";
  o += jsonEscape(apName.c_str());
  o += "\",\"fw\":\"" RS_FW_VERSION "\"}";
  sendJson(200, o);
}

void readExercise(Form& f, const char* pre, ExerciseParams& p, bool slr) {
  char k[16];
  auto key = [&](const char* s) {
    snprintf(k, sizeof(k), "%s_%s", pre, s);
    return k;
  };
  float target = p.targetDeg;
  f.num(key("target"), 10, 150, target, "Between 10 and 150");
  float limit = p.limitDeg;
  if (f.num(key("limit"), 13, 170, limit, "Between 13 and 170") && limit < target + 3)
    f.err(k, "Must be at least 3 degrees above the target");
  f.num(key("hold"), 0, 30, p.holdSec, "Between 0 and 30 s");
  float reps = p.reps;
  if (f.num(key("reps"), 1, 50, reps, "Between 1 and 50")) p.reps = (uint16_t)(reps + 0.5f);
  f.num(key("tol"), 1, 20, p.toleranceDeg, "Between 1 and 20");
  float speed = p.maxSpeedDps;
  if (f.num(key("speed"), 0, 600, speed, "0 (off) or 20 to 600") && speed != 0 && speed < 20)
    f.err(k, "0 (off) or 20 to 600");
  p.maxSpeedDps = speed;
  if (slr) f.num(key("knee"), 3, 45, p.kneeBentDeg, "Between 3 and 45");
  // Rotation check: 0 turns it off (a crooked strap can trip it on deep bends).
  float rot = p.rotateDeg;
  if (f.num(key("rotate"), 0, 60, rot, "0 (off) or 5 to 60") && rot != 0 && rot < 5)
    f.err(k, "0 (off) or 5 to 60");
  p.rotateDeg = rot;
  // Rest angle: a rep ends below it and starts 5 deg above it. Raise it for a
  // patient who can't fully straighten, or no rep ever finishes.
  float rest = p.restDeg;
  if (f.num(key("rest"), 2, 40, rest, "Between 2 and 40")) {
    if (rest > target - 10) {
      f.err(k, "At least 10 degrees below the target");
    } else {
      p.restDeg = rest;
      p.startDeg = rest + 5;
    }
  }
  p.targetDeg = target;
  p.limitDeg = limit;
}

void handlePostConfig() {
  AppConfig& c = store::config();
  ExerciseParams k = c.params[0], s = c.params[1], e = c.params[2];
  Form f;
  readExercise(f, "k", k, false);
  readExercise(f, "s", s, true);
  readExercise(f, "e", e, false);  // elbow flexion: e_target, e_limit ...

  String tsKey = server.hasArg("ts_key") ? server.arg("ts_key") : String();
  tsKey.trim();
  if (tsKey.length() && tsKey != "-") {
    bool okKey = tsKey.length() <= 23;
    for (size_t i = 0; i < tsKey.length() && okKey; i++) okKey = isalnum((unsigned char)tsKey[i]);
    if (!okKey) f.err("ts_key", "Letters and digits only (the 16-character write key)");
  }

  if (f.bad) {
    sendJson(400, "{\"ok\":false,\"errors\":{" + f.errors + "}}");
    return;
  }

  ExerciseParams k2 = k, s2 = s, e2 = e;
  bool adjusted = sanitizeParams(k2, Exercise::KneeFlexion);
  adjusted |= sanitizeParams(s2, Exercise::StraightLegRaise);
  adjusted |= sanitizeParams(e2, Exercise::ElbowFlexion);
  c.params[0] = k2;
  c.params[1] = s2;
  c.params[2] = e2;

  if (server.hasArg("buzzer")) c.buzzerOn = server.arg("buzzer") == "1";
  if (server.hasArg("safety")) c.safetyAlertInAssessment = server.arg("safety") == "1";
  fb::setBuzzerEnabled(c.buzzerOn);

  bool wifiChanged = false;
  if (server.hasArg("wifi_ssid")) {
    String ssid = server.arg("wifi_ssid");
    ssid.trim();
    if (ssid != c.wifiSsid) {
      strncpy(c.wifiSsid, ssid.c_str(), sizeof(c.wifiSsid) - 1);
      c.wifiSsid[sizeof(c.wifiSsid) - 1] = 0;
      wifiChanged = true;
    }
  }
  if (server.hasArg("wifi_pass") && server.arg("wifi_pass").length()) {
    strncpy(c.wifiPass, server.arg("wifi_pass").c_str(), sizeof(c.wifiPass) - 1);
    c.wifiPass[sizeof(c.wifiPass) - 1] = 0;
    wifiChanged = true;
  }
  if (tsKey == "-") c.tsKey[0] = 0;
  else if (tsKey.length()) {
    strncpy(c.tsKey, tsKey.c_str(), sizeof(c.tsKey) - 1);
    c.tsKey[sizeof(c.tsKey) - 1] = 0;
  }

  bool saved = store::saveConfig();
  if (wifiChanged) applyWifiConfig();
  sendJson(saved ? 200 : 500, String("{\"ok\":") + (saved ? "true" : "false") +
                                  ",\"adjusted\":" + (adjusted ? "true" : "false") + "}");
}

// Last 30 sessions as JSON.
struct Ring {
  static const int N = 30;
  SessionRecord r[N];
  int count = 0, head = 0;
};
void collect(const SessionRecord& rec, void* ctx) {
  Ring* g = (Ring*)ctx;
  g->r[g->head] = rec;
  g->head = (g->head + 1) % Ring::N;
  if (g->count < Ring::N) g->count++;
}

void handleSessions() {
  Ring* g = new Ring();  // ~2.4 KB, keep it off the small loop stack
  store::forEachSession(collect, g);
  String o = "[";
  int start = (g->head - g->count + Ring::N) % Ring::N;
  for (int i = 0; i < g->count; i++) {
    const SessionRecord& s = g->r[(start + i) % Ring::N];
    char b[200];
    snprintf(b, sizeof(b),
             "%s{\"id\":%u,\"ex\":%u,\"mode\":%u,\"valid\":%u,\"target\":%u,\"attempts\":%u,"
             "\"peak\":%.1f,\"max\":%.1f,\"hold\":%.2f,\"warn\":%u,\"epoch\":%u}",
             i ? "," : "", (unsigned)s.id, (unsigned)s.exercise, (unsigned)s.mode,
             (unsigned)s.valid, (unsigned)s.repsTarget, (unsigned)s.attempts, s.avgPeak,
             s.maxAngle, s.avgHold,
             (unsigned)(s.overLimit + s.tooFast + s.kneeBent + s.rotated), (unsigned)s.epoch);
    o += b;
  }
  o += "]";
  delete g;
  sendJson(200, o);
}

void sendCsvLine(const char* line, void*) {
  server.sendContent(line);
  server.sendContent("\n");
}

void handleCsv() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  allowCors();
  server.sendHeader("Content-Disposition", "attachment; filename=rehabsense_sessions.csv");
  server.send(200, "text/csv", "");
  server.sendContent(store::csvHeader());
  server.sendContent("\n");
  store::forEachCsvLine(sendCsvLine, nullptr);
  server.sendContent("");  // end of chunked response
}

// Clinical test results: the full CSV, and the last 20 as JSON for the phone page.
void handleTestsCsv() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  allowCors();
  server.sendHeader("Content-Disposition", "attachment; filename=rehabsense_tests.csv");
  server.send(200, "text/csv", "");
  server.sendContent(store::testsCsvHeader());
  server.sendContent("\n");
  store::forEachTestLine(sendCsvLine, nullptr);
  server.sendContent("");
}

struct TestRing {
  static const int N = 20;
  TestRecord r[N];
  int count = 0, head = 0;
};
void collectTest(const TestRecord& rec, void* ctx) {
  TestRing* g = (TestRing*)ctx;
  g->r[g->head] = rec;
  g->head = (g->head + 1) % TestRing::N;
  if (g->count < TestRing::N) g->count++;
}

void handleTests() {
  TestRing* g = new TestRing();
  store::forEachTest(collectTest, g);
  String o = "[";
  int start = (g->head - g->count + TestRing::N) % TestRing::N;
  for (int i = 0; i < g->count; i++) {
    const TestRecord& t = g->r[(start + i) % TestRing::N];
    char b[160];
    snprintf(b, sizeof(b),
             "%s{\"id\":%u,\"test\":%u,\"score\":%.2f,\"stands\":%u,\"target\":%.0f,\"bias\":%.1f,"
             "\"epoch\":%u}",
             i ? "," : "", (unsigned)t.id, (unsigned)t.test, t.score, (unsigned)t.stands,
             t.targetDeg, t.constErr, (unsigned)t.epoch);
    o += b;
  }
  o += "]";
  delete g;
  sendJson(200, o);
}

void handleClear() {
  if (server.arg("confirm") != "yes") {
    sendJson(400, "{\"ok\":false,\"error\":\"send confirm=yes\"}");
    return;
  }
  bool ok = store::clearSessions();
  sendJson(ok ? 200 : 500, ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void handleStatus() {
  LiveData d = sensing::snapshot();
  char b[480];
  char st[72];
  getStatus(st, sizeof(st));
  snprintf(b, sizeof(b),
           "{\"fw\":\"%s\",\"uptime_s\":%lu,\"heap\":%u,\"thighOk\":%s,\"shinOk\":%s,"
           "\"hz\":%.0f,\"i2cErrors\":%u,\"recoveries\":%u,\"sessions\":%u,\"tests\":%u,"
           "\"pending\":%u,\"uploaded\":%u,\"fsUsed\":%u,\"fsTotal\":%u,\"upload\":\"%s\"}",
           RS_FW_VERSION, (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap(),
           d.thighOk ? "true" : "false", d.shinOk ? "true" : "false", d.sampleHz,
           (unsigned)d.i2cErrors, (unsigned)d.recoveries, (unsigned)store::sessionsStored(),
           (unsigned)store::testsStored(), (unsigned)store::pendingUploads(), (unsigned)uploaded,
           (unsigned)store::fsUsedBytes(), (unsigned)store::fsTotalBytes(), jsonEscape(st).c_str());
  sendJson(200, b);
}

void handleNotFound() {
  // Captive portal: any unknown host (phone connectivity checks) -> our page.
  String host = server.hostHeader();
  if (host.length() && host != WiFi.softAPIP().toString() && host != staIp()) {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "Not found");
}

// ------------------------------ uploader ------------------------------------
void isoTime(uint32_t epoch, char* out, size_t n) {
  time_t t = (time_t)epoch;
  struct tm tmv;
  gmtime_r(&t, &tmv);
  strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

void uploaderTask(void*) {
  uint32_t lastAttempt = 0;
  bool first = true;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    char key[24];
    strncpy(key, store::config().tsKey, sizeof(key) - 1);
    key[sizeof(key) - 1] = 0;

    if (!key[0]) { setStatus("No ThingSpeak key set"); continue; }
    if (WiFi.status() != WL_CONNECTED) {
      setStatus(store::pendingUploads() ? "Offline - sessions saved, will sync later"
                                        : "Offline");
      continue;
    }
    if (store::pendingUploads() == 0) { setStatus("All sessions synced"); continue; }
    if (!first && millis() - lastAttempt < THINGSPEAK_MIN_GAP_MS) continue;

    SessionRecord r;
    if (!store::nextPending(r)) continue;
    first = false;
    lastAttempt = millis();

    char url[420];
    int n = snprintf(url, sizeof(url),
                     "http://api.thingspeak.com/update?api_key=%s&field1=%u&field2=%u&field3=%u"
                     "&field4=%.1f&field5=%.1f&field6=%.2f&field7=%u&field8=%.0f&status=%s-session-%u",
                     key, (unsigned)(r.exercise + 1), (unsigned)r.valid, (unsigned)r.attempts,
                     r.maxAngle, r.avgPeak, r.avgHold,
                     (unsigned)(r.overLimit + r.tooFast + r.kneeBent + r.rotated), r.duration,
                     r.mode == 0 ? "Guided" : "Assessment", (unsigned)r.id);
    if (r.epoch > 1700000000UL && n > 0 && n < (int)sizeof(url) - 40) {
      char ts[32];
      isoTime(r.epoch, ts, sizeof(ts));
      snprintf(url + n, sizeof(url) - n, "&created_at=%s", ts);
    }

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(6000);
    int code = -1;
    String body;
    if (http.begin(client, url)) {
      code = http.GET();
      if (code > 0) body = http.getString();
      http.end();
    }
    char msg[72];
    if (code == 200 && body.toInt() > 0) {
      store::markUploaded(r.id);
      uploaded = uploaded + 1;
      snprintf(msg, sizeof(msg), "Synced session #%u", (unsigned)r.id);
    } else if (code == 200) {
      snprintf(msg, sizeof(msg), "ThingSpeak refused #%u (check key / rate)", (unsigned)r.id);
    } else {
      snprintf(msg, sizeof(msg), "Sync failed (HTTP %d), retrying", code);
    }
    setStatus(msg);
  }
}

}  // namespace

// ------------------------------- public -------------------------------------
void begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  String mac = WiFi.macAddress();  // "AA:BB:CC:DD:EE:FF"
  mac.replace(":", "");
  apName = String(AP_SSID_PREFIX) + mac.substring(8);
  WiFi.softAP(apName.c_str(), AP_PASSWORD);
  WiFi.setSleep(false);  // snappier web page
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/live", HTTP_GET, handleLive);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/sessions", HTTP_GET, handleSessions);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/clear", HTTP_POST, handleClear);
  server.on("/sessions.csv", HTTP_GET, handleCsv);
  server.on("/api/tests", HTTP_GET, handleTests);
  server.on("/tests.csv", HTTP_GET, handleTestsCsv);
  server.onNotFound(handleNotFound);
  server.begin();

  configTime(0, 0, NTP_SERVER);  // UTC; only used to timestamp sessions
  applyWifiConfig();

  if (!uploaderHandle)
    xTaskCreatePinnedToCore(uploaderTask, "uploader", 8192, nullptr, 1, &uploaderHandle, 0);
}

void applyWifiConfig() {
  const AppConfig& c = store::config();
  staWanted = c.wifiSsid[0] != 0;
  WiFi.disconnect();
  if (staWanted) {
    WiFi.begin(c.wifiSsid, c.wifiPass);
    lastStaBegin = millis();
  }
}

void loop() {
  dns.processNextRequest();
  server.handleClient();
  // Retry the home Wi-Fi once a minute (continuous retries disturb the hotspot).
  if (staWanted && WiFi.status() != WL_CONNECTED && millis() - lastStaBegin > 60000) {
    const AppConfig& c = store::config();
    WiFi.begin(c.wifiSsid, c.wifiPass);
    lastStaBegin = millis();
  }
}

String apSsid() { return apName; }
String apIp() { return WiFi.softAPIP().toString(); }
bool staConnected() { return WiFi.status() == WL_CONNECTED; }
String staIp() { return staConnected() ? WiFi.localIP().toString() : String(""); }
String uploadStatus() {
  char st[72];
  getStatus(st, sizeof(st));
  return String(st);
}
uint32_t uploadedCount() { return uploaded; }

}  // namespace net
