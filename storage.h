// storage.h -- persistent config (NVS) and the session log (LittleFS CSV).
// Sessions are always written locally first; the uploader syncs them to
// ThingSpeak later ("store and forward"), so no Wi-Fi is ever required.
#pragma once
#include <Arduino.h>

#include "rehab_logic.h"

struct AppConfig {
  uint32_t magic;
  uint16_t version;
  rs::ExerciseParams params[rs::kExerciseCount];
  char wifiSsid[33];
  char wifiPass[65];
  char tsKey[24];          // ThingSpeak channel WRITE API key
  bool buzzerOn;
  bool safetyAlertInAssessment;  // limit alarm even in silent mode
};

struct SessionRecord {
  uint32_t id = 0;
  uint32_t epoch = 0;      // UTC seconds, 0 if the clock was never synced
  uint32_t uptimeSec = 0;
  uint8_t exercise = 0, mode = 0;
  uint16_t repsTarget = 0, valid = 0, attempts = 0, incomplete = 0, holdShort = 0;
  uint16_t overLimit = 0, tooFast = 0, kneeBent = 0, rotated = 0;
  float maxAngle = 0, avgPeak = 0, avgHold = 0, duration = 0, targetDeg = 0, limitDeg = 0;
};

// One clinical test result (see rs::ClinicalTest). Unused columns stay 0.
struct TestRecord {
  uint32_t id = 0;
  uint32_t epoch = 0;
  uint32_t uptimeSec = 0;
  uint8_t test = 0;        // rs::TestKind: 0 chair stand, 1 TUG, 2 position sense
  float score = 0;         // stands | seconds | mean absolute error (deg)
  uint16_t stands = 0;     // chair stand: full stands (score adds a final half stand)
  float riseSec = 0, walkSec = 0, sitSec = 0;
  float targetDeg = 0;     // position sense
  float err[rs::kPosTrials] = {0, 0, 0};
  float constErr = 0;
};

namespace store {

bool begin();               // mounts LittleFS (formats if needed) and loads config
bool fsOk();
AppConfig& config();        // RAM copy; call saveConfig() after editing
bool saveConfig();
void resetConfig();         // factory defaults (not saved until saveConfig)

// Assigns r.id, appends to the CSV, returns true on success.
bool appendSession(SessionRecord& r);
uint32_t sessionsStored();      // total ids ever assigned
uint32_t pendingUploads();
bool nextPending(SessionRecord& r);
void markUploaded(uint32_t id);
bool clearSessions();       // also clears the test results

// Clinical test results: /tests.csv, ids 1, 2, 3... of their own.
bool appendTest(TestRecord& r);  // assigns r.id
uint32_t testsStored();          // total test ids ever assigned
void forEachTestLine(void (*fn)(const char* line, void* ctx), void* ctx);
void forEachTest(void (*fn)(const TestRecord& r, void* ctx), void* ctx);
const char* testsCsvHeader();

// Iterate all stored sessions, oldest first (reads the rotated file too).
void forEachSession(void (*fn)(const SessionRecord& r, void* ctx), void* ctx);
// Iterate raw CSV data lines (no header), oldest first.
void forEachCsvLine(void (*fn)(const char* line, void* ctx), void* ctx);

size_t fsUsedBytes();
size_t fsTotalBytes();

// For streaming the CSV from the web server. Hold the lock while the File is open.
void lock();
void unlock();
const char* csvHeader();

}  // namespace store
