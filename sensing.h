// sensing.h -- the 100 Hz sensor task.
// Runs on core 1 at high priority, owns both IMUs, the JointModel, the
// RepCounter and the ClinicalTest. Everything else talks to it through three
// thread-safe doors:
//   commands in  (calibrate / start / stop / test start, cancel, clear)
//   events out   (rep counted, target reached, warnings ...)
//   snapshot()   (latest angles + state, copied under a mutex)
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "rehab_logic.h"

struct LiveData {
  bool valid = false;  // false if the snapshot could not be taken
  // hardware health
  bool thighOk = false, shinOk = false;
  bool thighPresent = false, shinPresent = false;
  uint32_t i2cErrors = 0;
  uint32_t recoveries = 0;
  float sampleHz = 0;
  bool sensorsLost = false;  // both not delivering for > 300 ms

  // calibration
  rs::CalState cal = rs::CalState::Idle;
  float calProgress = 0;
  bool calibrated = false;

  // angles
  rs::JointSample joint;
  rs::Vec3 accThigh, accShin;  // raw, for diagnostics

  // session
  bool sessionActive = false;
  rs::Exercise exercise = rs::Exercise::KneeFlexion;
  rs::Mode mode = rs::Mode::Guided;
  rs::ExerciseParams params = {};
  rs::Phase phase = rs::Phase::Rest;
  float metric = 0;          // the angle that counts for this exercise
  float holdElapsed = 0;
  float currentPeak = 0;
  uint16_t currentFaults = 0;
  rs::SessionStats stats;
  uint32_t droppedEvents = 0;

  // clinical test (never at the same time as a session)
  rs::TestKind testKind = rs::TestKind::ChairStand;
  rs::TestPhase testPhase = rs::TestPhase::Idle;
  rs::TestAbort testAbort = rs::TestAbort::None;
  rs::TestResult test;
  float testElapsed = 0;     // s since "Go" (or since the start: position sense)
  float testHold = 0;        // position sense: s held still in the band
  uint8_t testCountdown = 0;
  uint8_t testStage = 0;     // TUG: 0 getting up, 1 walking, 2 sitting down
  bool testArmed = false;    // position sense: away from the target, ready to find it
  bool testCalHint = false;  // looks calibrated in the wrong pose
};

namespace sensing {

// Initialise bus 0, probe/configure both IMUs, start the task.
// Returns true if both IMUs answered (the task keeps retrying either way).
bool begin();

// If the OLED ends up on the IMU bus, pass a mutex so both users take turns.
void setSharedBusMutex(SemaphoreHandle_t m);

void requestCalibration();
void startSession(rs::Exercise ex, rs::Mode mode, const rs::ExerciseParams& p);
// Stops the session and copies the final stats. Returns false on timeout.
bool stopSession(rs::SessionStats* out, uint32_t timeoutMs = 300);

// Clinical tests. Progress and the result are in snapshot().test*.
void startTest(rs::TestKind k, float targetDeg);
void cancelTest();  // abort a running test (not saved)
void clearTest();   // back to idle once the result has been shown

LiveData snapshot();
bool popEvent(rs::Event& e);

}  // namespace sensing
