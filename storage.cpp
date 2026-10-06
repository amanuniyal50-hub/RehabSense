// storage.cpp
#include "storage.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"

namespace store {
namespace {

constexpr uint32_t kMagic = 0x52534E31;  // "RSN1"
constexpr uint16_t kVersion = 4;
const char* kNs = "rehab";

const char* kHeader =
    "id,epoch_utc,uptime_s,exercise,mode,reps_target,valid,attempts,incomplete,hold_short,"
    "over_limit,too_fast,knee_bent,rotated,max_angle,avg_peak,avg_hold_s,duration_s,"
    "target_deg,limit_deg";

const char* kTestsHeader =
    "id,epoch_utc,uptime_s,test,score,stands,rise_s,walk_s,sit_s,target_deg,trial1_err,"
    "trial2_err,trial3_err,const_err";

AppConfig cfg;
Preferences prefs;
SemaphoreHandle_t mtx = nullptr;
bool fsMounted = false;
uint32_t nextId = 1;
uint32_t nextTestId = 1;
uint32_t lastUploaded = 0;

void newFile(const char* path, const char* header) {
  File f = LittleFS.open(path, "w");
  if (f) {
    f.println(header);
    f.close();
  }
}

void setDefaults(AppConfig& c) {
  memset(&c, 0, sizeof(c));
  c.magic = kMagic;
  c.version = kVersion;
  c.params[0] = rs::defaultParams(rs::Exercise::KneeFlexion);
  c.params[1] = rs::defaultParams(rs::Exercise::StraightLegRaise);
  c.params[2] = rs::defaultParams(rs::Exercise::ElbowFlexion);
  c.buzzerOn = true;
  c.safetyAlertInAssessment = true;
}

struct Lock {
  Lock() { store::lock(); }
  ~Lock() { store::unlock(); }
};

bool parseLine(const char* line, SessionRecord& r) {
  unsigned id, ep, up, ex, md, rt, va, at, inc, hs, ol, tf, kb, ro;
  float mx, ap, ah, du, td, ld;
  int n = sscanf(line, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%f,%f,%f,%f,%f,%f", &id, &ep,
                 &up, &ex, &md, &rt, &va, &at, &inc, &hs, &ol, &tf, &kb, &ro, &mx, &ap, &ah, &du,
                 &td, &ld);
  if (n != 20) return false;
  r.id = id; r.epoch = ep; r.uptimeSec = up;
  r.exercise = (uint8_t)ex; r.mode = (uint8_t)md;
  r.repsTarget = rt; r.valid = va; r.attempts = at; r.incomplete = inc; r.holdShort = hs;
  r.overLimit = ol; r.tooFast = tf; r.kneeBent = kb; r.rotated = ro;
  r.maxAngle = mx; r.avgPeak = ap; r.avgHold = ah; r.duration = du; r.targetDeg = td; r.limitDeg = ld;
  return true;
}

// Scan one CSV file for the first record with id > after.
bool findFirstAfter(const char* path, uint32_t after, SessionRecord& out) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  char line[200];
  bool found = false;
  while (f.available()) {
    size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = 0;
    if (n == 0 || line[0] < '0' || line[0] > '9') continue;  // header / blank
    SessionRecord r;
    if (parseLine(line, r) && r.id > after) {
      out = r;
      found = true;
      break;
    }
  }
  f.close();
  return found;
}

}  // namespace

void lock() {
  if (mtx) xSemaphoreTake(mtx, portMAX_DELAY);
}
void unlock() {
  if (mtx) xSemaphoreGive(mtx);
}
const char* csvHeader() { return kHeader; }

bool begin() {
  if (!mtx) mtx = xSemaphoreCreateMutex();
  prefs.begin(kNs, false);

  setDefaults(cfg);
  AppConfig loaded;
  size_t got = prefs.getBytes("cfg", &loaded, sizeof(loaded));
  if (got == sizeof(loaded) && loaded.magic == kMagic && loaded.version == kVersion) {
    cfg = loaded;
    // Re-validate: a corrupted or hand-edited value must never reach the logic.
    for (int i = 0; i < rs::kExerciseCount; i++) rs::sanitizeParams(cfg.params[i], (rs::Exercise)i);
    cfg.wifiSsid[sizeof(cfg.wifiSsid) - 1] = 0;
    cfg.wifiPass[sizeof(cfg.wifiPass) - 1] = 0;
    cfg.tsKey[sizeof(cfg.tsKey) - 1] = 0;
  }
  nextId = prefs.getUInt("nid", 1);
  nextTestId = prefs.getUInt("tid", 1);
  lastUploaded = prefs.getUInt("lup", 0);
  if (lastUploaded >= nextId) lastUploaded = nextId - 1;

  fsMounted = LittleFS.begin(true);  // format on first use
  if (fsMounted && !LittleFS.exists(SESSIONS_FILE)) {
    File f = LittleFS.open(SESSIONS_FILE, "w");
    if (f) {
      f.println(kHeader);
      f.close();
    }
  }
  if (fsMounted && !LittleFS.exists(TESTS_FILE)) newFile(TESTS_FILE, kTestsHeader);
  return fsMounted;
}

bool fsOk() { return fsMounted; }
AppConfig& config() { return cfg; }

bool saveConfig() {
  for (int i = 0; i < rs::kExerciseCount; i++) rs::sanitizeParams(cfg.params[i], (rs::Exercise)i);
  cfg.magic = kMagic;
  cfg.version = kVersion;
  return prefs.putBytes("cfg", &cfg, sizeof(cfg)) == sizeof(cfg);
}

void resetConfig() { setDefaults(cfg); }

bool appendSession(SessionRecord& r) {
  if (!fsMounted) return false;
  Lock l;
  // Rotate the log if it gets large (keeps one previous file).
  File chk = LittleFS.open(SESSIONS_FILE, "r");
  size_t size = chk ? chk.size() : 0;
  if (chk) chk.close();
  if (size > SESSIONS_MAX_BYTES) {
    LittleFS.remove(SESSIONS_OLD_FILE);
    LittleFS.rename(SESSIONS_FILE, SESSIONS_OLD_FILE);
    File nf = LittleFS.open(SESSIONS_FILE, "w");
    if (nf) {
      nf.println(kHeader);
      nf.close();
    }
  }

  r.id = nextId;
  File f = LittleFS.open(SESSIONS_FILE, "a");
  if (!f) return false;
  f.printf("%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.1f,%.1f,%.2f,%.1f,%.1f,%.1f\n",
           (unsigned)r.id, (unsigned)r.epoch, (unsigned)r.uptimeSec, (unsigned)r.exercise,
           (unsigned)r.mode, (unsigned)r.repsTarget, (unsigned)r.valid, (unsigned)r.attempts,
           (unsigned)r.incomplete, (unsigned)r.holdShort, (unsigned)r.overLimit,
           (unsigned)r.tooFast, (unsigned)r.kneeBent, (unsigned)r.rotated, r.maxAngle, r.avgPeak,
           r.avgHold, r.duration, r.targetDeg, r.limitDeg);
  f.close();
  nextId++;
  prefs.putUInt("nid", nextId);
  return true;
}

uint32_t sessionsStored() { return nextId - 1; }

uint32_t pendingUploads() {
  uint32_t last = nextId - 1;
  return last > lastUploaded ? last - lastUploaded : 0;
}

bool nextPending(SessionRecord& r) {
  if (!fsMounted || pendingUploads() == 0) return false;
  Lock l;
  if (findFirstAfter(SESSIONS_OLD_FILE, lastUploaded, r)) return true;
  if (findFirstAfter(SESSIONS_FILE, lastUploaded, r)) return true;
  // Pending count says yes but no record exists (e.g. file lost): skip ahead.
  lastUploaded = nextId - 1;
  prefs.putUInt("lup", lastUploaded);
  return false;
}

void markUploaded(uint32_t id) {
  Lock l;
  if (id > lastUploaded) {
    lastUploaded = id;
    prefs.putUInt("lup", lastUploaded);
  }
}

bool clearSessions() {
  if (!fsMounted) return false;
  Lock l;
  LittleFS.remove(SESSIONS_OLD_FILE);
  LittleFS.remove(SESSIONS_FILE);
  LittleFS.remove(TESTS_OLD_FILE);
  LittleFS.remove(TESTS_FILE);
  newFile(TESTS_FILE, kTestsHeader);
  File f = LittleFS.open(SESSIONS_FILE, "w");
  if (!f) return false;
  f.println(kHeader);
  f.close();
  lastUploaded = nextId - 1;  // nothing left to upload
  prefs.putUInt("lup", lastUploaded);
  return true;
}

// ------------------------------ test results --------------------------------
const char* testsCsvHeader() { return kTestsHeader; }
uint32_t testsStored() { return nextTestId - 1; }

bool appendTest(TestRecord& r) {
  if (!fsMounted) return false;
  Lock l;
  File chk = LittleFS.open(TESTS_FILE, "r");
  size_t size = chk ? chk.size() : 0;
  if (chk) chk.close();
  if (size > TESTS_MAX_BYTES) {
    LittleFS.remove(TESTS_OLD_FILE);
    LittleFS.rename(TESTS_FILE, TESTS_OLD_FILE);
    newFile(TESTS_FILE, kTestsHeader);
  }
  r.id = nextTestId;
  File f = LittleFS.open(TESTS_FILE, "a");
  if (!f) return false;
  f.printf("%u,%u,%u,%u,%.2f,%u,%.2f,%.2f,%.2f,%.0f,%.1f,%.1f,%.1f,%.2f\n", (unsigned)r.id,
           (unsigned)r.epoch, (unsigned)r.uptimeSec, (unsigned)r.test, r.score,
           (unsigned)r.stands, r.riseSec, r.walkSec, r.sitSec, r.targetDeg, r.err[0], r.err[1],
           r.err[2], r.constErr);
  f.close();
  nextTestId++;
  prefs.putUInt("tid", nextTestId);
  return true;
}

static bool parseTestLine(const char* line, TestRecord& r) {
  unsigned id, ep, up, t, st;
  float sc, ri, wa, si, tg, e0, e1, e2, ce;
  int n = sscanf(line, "%u,%u,%u,%u,%f,%u,%f,%f,%f,%f,%f,%f,%f,%f", &id, &ep, &up, &t, &sc, &st,
                 &ri, &wa, &si, &tg, &e0, &e1, &e2, &ce);
  if (n != 14) return false;
  r.id = id; r.epoch = ep; r.uptimeSec = up; r.test = (uint8_t)t; r.score = sc;
  r.stands = (uint16_t)st; r.riseSec = ri; r.walkSec = wa; r.sitSec = si; r.targetDeg = tg;
  r.err[0] = e0; r.err[1] = e1; r.err[2] = e2; r.constErr = ce;
  return true;
}

struct TestScan {
  void (*fn)(const TestRecord&, void*);
  void* ctx;
};
static void testLine(const char* line, void* ctx) {
  TestScan* s = (TestScan*)ctx;
  TestRecord r;
  if (parseTestLine(line, r)) s->fn(r, s->ctx);
}

static void scanFile(const char* path, void (*rec)(const SessionRecord&, void*),
                     void (*raw)(const char*, void*), void* ctx);

void forEachTestLine(void (*fn)(const char* line, void* ctx), void* ctx) {
  if (!fsMounted || !fn) return;
  Lock l;
  scanFile(TESTS_OLD_FILE, nullptr, fn, ctx);
  scanFile(TESTS_FILE, nullptr, fn, ctx);
}

void forEachTest(void (*fn)(const TestRecord& r, void* ctx), void* ctx) {
  if (!fn) return;
  TestScan s{fn, ctx};
  forEachTestLine(testLine, &s);
}

static void scanFile(const char* path, void (*rec)(const SessionRecord&, void*),
                     void (*raw)(const char*, void*), void* ctx) {
  File f = LittleFS.open(path, "r");
  if (!f) return;
  char line[200];
  while (f.available()) {
    size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = 0;
    if (n && line[n - 1] == '\r') line[--n] = 0;
    if (n == 0 || line[0] < '0' || line[0] > '9') continue;
    if (raw) raw(line, ctx);
    if (rec) {
      SessionRecord r;
      if (parseLine(line, r)) rec(r, ctx);
    }
  }
  f.close();
}

void forEachSession(void (*fn)(const SessionRecord& r, void* ctx), void* ctx) {
  if (!fsMounted || !fn) return;
  Lock l;
  scanFile(SESSIONS_OLD_FILE, fn, nullptr, ctx);
  scanFile(SESSIONS_FILE, fn, nullptr, ctx);
}

void forEachCsvLine(void (*fn)(const char* line, void* ctx), void* ctx) {
  if (!fsMounted || !fn) return;
  Lock l;
  scanFile(SESSIONS_OLD_FILE, nullptr, fn, ctx);
  scanFile(SESSIONS_FILE, nullptr, fn, ctx);
}

size_t fsUsedBytes() { return fsMounted ? LittleFS.usedBytes() : 0; }
size_t fsTotalBytes() { return fsMounted ? LittleFS.totalBytes() : 0; }

}  // namespace store
