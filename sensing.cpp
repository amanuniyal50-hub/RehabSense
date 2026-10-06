// sensing.cpp
#include "sensing.h"

#include <Wire.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "config.h"
#include "imu.h"

using namespace rs;

namespace {

enum class CmdType : uint8_t { Calibrate, Start, Stop, TestStart, TestCancel, TestClear };
struct Command {
  CmdType type;
  Exercise ex;
  Mode mode;
  ExerciseParams params;
  TestKind test;
  float testTarget;
};

Mpu6050 imuThigh(Wire, IMU_ADDR_THIGH);
Mpu6050 imuShin(Wire, IMU_ADDR_SHIN);
JointModel joint;
RepCounter counter;
ClinicalTest ctest;

SemaphoreHandle_t dataMutex = nullptr;
SemaphoreHandle_t busMutex = nullptr;  // only used if the OLED shares bus 0
QueueHandle_t cmdQueue = nullptr;
QueueHandle_t eventQueue = nullptr;
TaskHandle_t taskHandle = nullptr;

LiveData live;          // written by the task under dataMutex
bool sessionActive = false;
Exercise curEx = Exercise::KneeFlexion;
Mode curMode = Mode::Guided;
uint32_t recoveries = 0, dropped = 0;

inline void busLock() {
  if (busMutex) xSemaphoreTake(busMutex, portMAX_DELAY);
}
inline void busUnlock() {
  if (busMutex) xSemaphoreGive(busMutex);
}

void recoverBus() {
  // Re-start the I2C peripheral and re-configure whichever IMU dropped out.
  busLock();
  Wire.end();
  Wire.begin(PIN_IMU_SDA, PIN_IMU_SCL, IMU_I2C_HZ);
  Wire.setTimeOut(20);
  if (!imuThigh.ok()) imuThigh.begin();
  if (!imuShin.ok()) imuShin.begin();
  busUnlock();
  recoveries++;
}

void handleCommand(const Command& c) {
  const uint32_t now = millis();
  switch (c.type) {
    case CmdType::Calibrate:
      if (sessionActive) {
        counter.finish(now);
        sessionActive = false;
      }
      ctest.reset();
      joint.beginCalibration(now);
      break;
    case CmdType::Start:
      if (!joint.calibrated()) break;  // UI guarantees this; be safe anyway
      ctest.reset();
      curEx = c.ex;
      curMode = c.mode;
      counter.configure(c.ex, c.params);
      counter.start(now);
      sessionActive = true;
      break;
    case CmdType::Stop:
      if (sessionActive) counter.finish(now);
      sessionActive = false;
      break;
    case CmdType::TestStart:
      if (!joint.calibrated()) break;
      if (sessionActive) {
        counter.finish(now);
        sessionActive = false;
      }
      ctest.start(c.test, c.testTarget, now);
      break;
    case CmdType::TestCancel:
      ctest.cancel();
      break;
    case CmdType::TestClear:
      ctest.reset();
      break;
  }
}

void sensorTask(void*) {
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(1000 / SENSOR_HZ);
  uint32_t lastUs = micros();
  uint32_t lastGoodMs = millis();
  uint32_t lastRecoverMs = 0;
  uint32_t rateWindowStart = millis();
  uint32_t rateCount = 0;
  float sampleHz = 0;
  Vec3 aT, gT, aS, gS;
  Event evs[8];

  for (;;) {
    vTaskDelayUntil(&lastWake, period);

    Command c;
    while (xQueueReceive(cmdQueue, &c, 0) == pdTRUE) handleCommand(c);

    busLock();
    const bool okT = imuThigh.read(aT, gT);
    const bool okS = imuShin.read(aS, gS);
    busUnlock();

    const uint32_t nowUs = micros();
    const float dt = (nowUs - lastUs) * 1e-6f;
    lastUs = nowUs;
    const uint32_t nowMs = millis();

    const bool good = okT && okS;
    if (good) {
      joint.update(aT, gT, aS, gS, dt);
      joint.tickCalibration(nowMs);
      lastGoodMs = nowMs;
      rateCount++;

      // TEMP DIAGNOSTIC: print live stillness numbers during calibration so
      // we can see exactly how far off they are from the thresholds
      // (calMaxGyroDps / calMaxAccelDevG in rehab_logic.h), instead of
      // guessing. Remove once calibration is passing reliably.
      static uint32_t lastDiagMs = 0;
      if ((joint.calState() == CalState::WaitingStill || joint.calState() == CalState::Collecting) &&
          nowMs - lastDiagMs > 300) {
        lastDiagMs = nowMs;
        const float outT = SegmentEstimator::outOfPlaneFromAccelDeg(aT);
        const float outS = SegmentEstimator::outOfPlaneFromAccelDeg(aS);
        Serial.printf(
            "[cal-diag] gyroT=%.2f gyroS=%.2f (limit %.1f)  accDevT=%.3f accDevS=%.3f (limit %.2f)  "
            "mountOutT=%.1f mountOutS=%.1f (must be within +-%.0f deg of 0)\n",
            vnorm(gT), vnorm(gS), joint.calMaxGyroDps, fabsf(vnorm(aT) - 1.f),
            fabsf(vnorm(aS) - 1.f), joint.calMaxAccelDevG, outT, outS, joint.calMountMaxOutDeg);
      }
    } else if ((!imuThigh.ok() || !imuShin.ok()) && nowMs - lastRecoverMs > 1000) {
      lastRecoverMs = nowMs;
      recoverBus();
    }
    const bool lost = nowMs - lastGoodMs > 300;

    // Rep counting only on good, calibrated data. While sensors are lost the
    // session simply pauses (hold timers are not advanced by bad data).
    int nev = 0;
    if (sessionActive && good && joint.calibrated()) {
      const JointSample& s = joint.sample();
      const bool knee = usesJointAngle(curEx);  // knee or elbow flexion
      const float metric = knee ? s.kneeDeg : s.thighElevDeg;
      const float speed = fabsf(knee ? s.kneeRateDps : s.thighRateDps);
      nev = counter.update(metric, s.kneeDeg, speed, s.rotationDeg, nowMs, evs, 8);
    } else if (ctest.active() && good && joint.calibrated()) {
      nev = ctest.update(joint.sample(), nowMs, evs, 8);
    }
    for (int i = 0; i < nev; i++) {
      if (xQueueSend(eventQueue, &evs[i], 0) != pdTRUE) dropped++;
    }

    if (nowMs - rateWindowStart >= 1000) {
      sampleHz = rateCount * 1000.f / (nowMs - rateWindowStart);
      rateCount = 0;
      rateWindowStart = nowMs;
    }

    // ---------------- publish snapshot ----------------
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      live.thighOk = imuThigh.ok();
      live.shinOk = imuShin.ok();
      live.i2cErrors = imuThigh.errorCount() + imuShin.errorCount();
      live.recoveries = recoveries;
      live.sampleHz = sampleHz;
      live.sensorsLost = lost;
      live.cal = joint.calState();
      live.calProgress = joint.calProgress();
      live.calibrated = joint.calibrated();
      live.joint = joint.sample();
      if (good) {
        live.accThigh = aT;
        live.accShin = aS;
      }
      live.sessionActive = sessionActive;
      live.exercise = curEx;
      live.mode = curMode;
      live.params = counter.params();
      live.phase = counter.phase();
      live.metric = usesJointAngle(curEx) ? live.joint.kneeDeg : live.joint.thighElevDeg;
      live.holdElapsed = counter.holdElapsedSec(nowMs);
      live.currentPeak = counter.currentPeak();
      live.currentFaults = counter.currentFaults();
      live.stats = counter.stats();
      live.droppedEvents = dropped;
      live.testKind = ctest.kind();
      live.testPhase = ctest.phase();
      live.testAbort = ctest.abortReason();
      live.test = ctest.result();
      live.testElapsed = ctest.elapsedSec(nowMs);
      live.testHold = ctest.holdSec(nowMs);
      live.testCountdown = ctest.countdown();
      live.testStage = ctest.tugStage();
      live.testArmed = ctest.armed();
      live.testCalHint = ctest.calLooksWrong();
      xSemaphoreGive(dataMutex);
    }
  }
}

}  // namespace

namespace sensing {

void setSharedBusMutex(SemaphoreHandle_t m) { busMutex = m; }

bool begin() {
  if (!dataMutex) dataMutex = xSemaphoreCreateMutex();
  if (!cmdQueue) cmdQueue = xQueueCreate(8, sizeof(Command));
  if (!eventQueue) eventQueue = xQueueCreate(32, sizeof(Event));

  busLock();
  Wire.begin(PIN_IMU_SDA, PIN_IMU_SCL, IMU_I2C_HZ);
  Wire.setTimeOut(20);
  const bool pT = imuThigh.present();
  const bool pS = imuShin.present();
  const bool okT = imuThigh.begin();
  const bool okS = imuShin.begin();
  busUnlock();

  if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
    live.thighPresent = pT;
    live.shinPresent = pS;
    live.thighOk = okT;
    live.shinOk = okS;
    xSemaphoreGive(dataMutex);
  }

  if (!taskHandle) {
    // Core 1 (same as loop()) but higher priority, so UI/web work can never
    // delay a sample. Wi-Fi lives on core 0.
    xTaskCreatePinnedToCore(sensorTask, "sensing", 6144, nullptr, 5, &taskHandle, 1);
  }
  return okT && okS;
}

void requestCalibration() {
  Command c{};
  c.type = CmdType::Calibrate;
  xQueueSend(cmdQueue, &c, pdMS_TO_TICKS(50));
}

void startSession(Exercise ex, Mode mode, const ExerciseParams& p) {
  Command c{};
  c.type = CmdType::Start;
  c.ex = ex;
  c.mode = mode;
  c.params = p;
  // Drain stale events from a previous session first.
  Event e;
  while (xQueueReceive(eventQueue, &e, 0) == pdTRUE) {
  }
  xQueueSend(cmdQueue, &c, pdMS_TO_TICKS(50));
}

bool stopSession(SessionStats* out, uint32_t timeoutMs) {
  Command c{};
  c.type = CmdType::Stop;
  xQueueSend(cmdQueue, &c, pdMS_TO_TICKS(50));
  const uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    LiveData d = snapshot();
    if (d.valid && !d.sessionActive) {
      if (out) *out = d.stats;
      return true;
    }
    delay(5);
  }
  if (out) *out = snapshot().stats;
  return false;
}

void startTest(TestKind k, float targetDeg) {
  Command c{};
  c.type = CmdType::TestStart;
  c.test = k;
  c.testTarget = targetDeg;
  Event e;
  while (xQueueReceive(eventQueue, &e, 0) == pdTRUE) {
  }
  xQueueSend(cmdQueue, &c, pdMS_TO_TICKS(50));
}

void cancelTest() {
  Command c{};
  c.type = CmdType::TestCancel;
  xQueueSend(cmdQueue, &c, pdMS_TO_TICKS(50));
}

void clearTest() {
  Command c{};
  c.type = CmdType::TestClear;
  xQueueSend(cmdQueue, &c, pdMS_TO_TICKS(50));
}

LiveData snapshot() {
  LiveData copy;
  if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    copy = live;
    xSemaphoreGive(dataMutex);
    copy.valid = true;
  }
  return copy;
}

bool popEvent(Event& e) { return eventQueue && xQueueReceive(eventQueue, &e, 0) == pdTRUE; }

}  // namespace sensing
