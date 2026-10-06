// main.cpp -- RehabSense application flow.
//
//   Boot -> Menu -> Mode -> Calibrate -> Ready -> Active -> Summary -> Menu
//                    \-> Live angles (goniometer check)   \-> Device info
//                    \-> Clinical tests -> Calibrate standing -> TestReady
//                                           -> TestActive -> TestResult
//
// Buttons: NEXT (32) moves / switches, OK (33) confirms, BACK (27) goes back.
// During a session: hold BACK to finish early.
#include <Arduino.h>
#include <time.h>

#include "buttons.h"
#include "config.h"
#include "feedback.h"
#include "netsvc.h"
#include "rehab_logic.h"
#include "sensing.h"
#include "storage.h"
#include "ui.h"

using namespace rs;

namespace {

enum class Screen : uint8_t {
  Boot, Menu, Mode, CalPrompt, Calibrating, CalFailed, Ready, Active, Summary, Live, Info,
  TestMenu, TestReady, TestActive, TestResult
};

// What the calibration is for (decides the prompt and where it goes next).
enum class CalFor : uint8_t { Exercise, Live, Test };

Screen screen = Screen::Boot;
uint32_t screenSince = 0;
int menuSel = 0;
int modeSel = 0;
Exercise exercise = Exercise::KneeFlexion;
Mode mode = Mode::Guided;
CalFor calFor = CalFor::Exercise;
bool calSeenRunning = false;
uint32_t calRequestedAt = 0;
CalState calFailState = CalState::Idle;

// session / summary
SessionStats lastStats;
ExerciseParams sessionParams;
uint32_t lastSessionId = 0;
int summaryPage = 0;
uint32_t finishAt = 0;         // auto-finish after "set complete"
bool lostCueGiven = false;

// clinical tests
int testSel = 0;
TestKind testKind = TestKind::ChairStand;
float posTarget = 45;          // position sense target (NEXT on the ready screen)
bool testSeenRunning = false;
uint32_t testRequestedAt = 0;
TestResult lastTest;
TestAbort lastTestAbort = TestAbort::None;
uint32_t lastTestId = 0;

// active-screen message
char msg[40] = "";
bool msgUrgent = false;
uint32_t msgUntil = 0;

LiveData live;
uint32_t lastSnap = 0, lastDraw = 0, lastSerial = 0;
SemaphoreHandle_t imuBusMutex = nullptr;

void go(Screen s) {
  screen = s;
  screenSince = millis();
  lastDraw = 0;  // redraw immediately
}

void say(const char* text, bool urgent = false, uint32_t ms = 2500) {
  strncpy(msg, text, sizeof(msg) - 1);
  msg[sizeof(msg) - 1] = 0;
  msgUrgent = urgent;
  msgUntil = millis() + ms;
}

void cue(fb::Cue c) {
  // Assessment mode is silent, except the safety-limit alarm if enabled.
  if (screen == Screen::Active && mode == Mode::Assessment) {
    bool allowed = (c == fb::Cue::OverLimit && store::config().safetyAlertInAssessment);
    if (!allowed) return;
  }
  fb::play(c);
}

void startCalibration(CalFor f) {
  calFor = f;
  calSeenRunning = false;
  calRequestedAt = millis();
  sensing::requestCalibration();
  go(Screen::Calibrating);
}

void startSession() {
  sessionParams = store::config().params[(int)exercise];
  sensing::startSession(exercise, mode, sessionParams);
  msg[0] = 0;
  msgUntil = 0;
  finishAt = 0;
  lostCueGiven = false;
  Serial.printf("[session] start %s, %s, target %.0f limit %.0f hold %.1fs reps %u\n",
                exerciseName(exercise), modeName(mode), sessionParams.targetDeg,
                sessionParams.limitDeg, sessionParams.holdSec, (unsigned)sessionParams.reps);
  go(Screen::Active);
}

void finishSession() {
  SessionStats s;
  sensing::stopSession(&s);
  finishAt = 0;
  fb::stop();
  if (s.attempts == 0) {
    ui::message("No reps recorded", "Nothing was saved.", "Returning...");
    delay(1200);
    go(Screen::Ready);
    return;
  }
  lastStats = s;
  SessionRecord r;
  time_t now = time(nullptr);
  r.epoch = now > 1700000000 ? (uint32_t)now : 0;
  r.uptimeSec = millis() / 1000;
  r.exercise = (uint8_t)exercise;
  r.mode = (uint8_t)mode;
  r.repsTarget = sessionParams.reps;
  r.valid = s.validReps;
  r.attempts = s.attempts;
  r.incomplete = s.incomplete;
  r.holdShort = s.holdShort;
  r.overLimit = s.overLimit;
  r.tooFast = s.tooFast;
  r.kneeBent = s.kneeBent;
  r.rotated = s.rotated;
  r.maxAngle = s.maxAngle;
  r.avgPeak = s.avgPeak();
  r.avgHold = s.avgHold();
  r.duration = s.durationSec();
  r.targetDeg = sessionParams.targetDeg;
  r.limitDeg = sessionParams.limitDeg;
  lastSessionId = store::appendSession(r) ? r.id : 0;
  Serial.printf("[session] saved #%lu: %u/%u valid, %u attempts, max %.1f, avg hold %.2fs\n",
                (unsigned long)lastSessionId, (unsigned)s.validReps, (unsigned)sessionParams.reps,
                (unsigned)s.attempts, s.maxAngle, s.avgHold());
  summaryPage = 0;
  go(Screen::Summary);
}

// ------------------------------ clinical tests ------------------------------
void startTest() {
  sensing::startTest(testKind, posTarget);
  msg[0] = 0;
  msgUntil = 0;
  testSeenRunning = false;
  testRequestedAt = millis();
  lostCueGiven = false;
  Serial.printf("[test] start %s\n", testName(testKind));
  go(Screen::TestActive);
}

void finishTest(const LiveData& d) {
  lastTest = d.test;
  lastTestAbort = d.testPhase == TestPhase::Done ? TestAbort::None : d.testAbort;
  lastTestId = 0;
  if (lastTestAbort == TestAbort::None) {
    const TestResult& r = d.test;
    TestRecord t;
    time_t now = time(nullptr);
    t.epoch = now > 1700000000 ? (uint32_t)now : 0;
    t.uptimeSec = millis() / 1000;
    t.test = (uint8_t)testKind;
    t.score = r.score;
    t.stands = r.stands;
    t.riseSec = r.riseSec;
    t.walkSec = r.walkSec;
    t.sitSec = r.sitSec;
    t.targetDeg = r.targetDeg;
    for (int i = 0; i < kPosTrials; i++) t.err[i] = r.trialErr[i];
    t.constErr = r.constErr;
    lastTestId = store::appendTest(t) ? t.id : 0;
    Serial.printf("[test] %s: score %.2f (stands %u, rise %.2f, walk %.2f, sit %.2f, errors %+.1f %+.1f %+.1f) saved #%lu\n",
                  testName(testKind), r.score, (unsigned)r.stands, r.riseSec, r.walkSec, r.sitSec,
                  r.trialErr[0], r.trialErr[1], r.trialErr[2], (unsigned long)lastTestId);
  } else {
    Serial.printf("[test] %s stopped: %s\n", testName(testKind), testAbortText(lastTestAbort));
  }
  go(Screen::TestResult);
}

void leaveTest(Screen next) {
  sensing::clearTest();
  go(next);
}

void onTestEvent(const Event& e) {
  char b[40];
  switch (e.type) {
    case EventType::TestCountdown:
      fb::play(fb::Cue::Target);
      break;
    case EventType::TestGo:
      fb::play(fb::Cue::Go);
      say("GO!", false, 1500);
      break;
    case EventType::TestStand:
      if (testKind == TestKind::ChairStand) fb::play(fb::Cue::Target);  // audible count
      else say("Walk to the line", false, 3000);
      break;
    // Position sense: the buzz only ever means "you are on the target".
    case EventType::PosInBand:
      fb::play(fb::Cue::GoodRep);
      break;
    case EventType::PosBandLost:
      fb::play(fb::Cue::Incomplete);
      say("Lost it: find the buzz", true, 2000);
      break;
    case EventType::PosMemorised:
      fb::play(fb::Cue::HoldDone);
      break;
    case EventType::PosReproduce:
      fb::play(fb::Cue::Target);
      break;
    case EventType::PosRecorded:
      fb::play(fb::Cue::CalDone);
      snprintf(b, sizeof(b), "Trial %u: %+.1f deg", (unsigned)e.validReps, e.value);
      say(b, false, 4000);
      Serial.printf("[test] position sense trial %u: reproduced %.1f, error %+.1f\n",
                    (unsigned)e.validReps, e.value2, e.value);
      break;
    case EventType::TestDone:
      fb::play(fb::Cue::SetComplete);
      break;
    case EventType::TestAborted:
      fb::play(fb::Cue::Error);
      break;
    default:
      break;
  }
}

// ------------------------- events from the sensor task ----------------------
void onEvent(const Event& e) {
  if (screen == Screen::TestActive) {
    onTestEvent(e);
    return;
  }
  if (screen != Screen::Active) return;
  char b[40];
  const ExerciseParams& p = sessionParams;
  switch (e.type) {
    case EventType::RepStarted:
      break;
    case EventType::TargetReached:
      cue(fb::Cue::Target);
      if (p.holdSec > 0) {
        snprintf(b, sizeof(b), "Hold it for %.0f s", p.holdSec);
        say(b, false, 60000);
      }
      break;
    case EventType::HoldComplete:
      cue(fb::Cue::HoldDone);
      say("Now return slowly", false, 60000);
      break;
    case EventType::HoldBroken:
      cue(fb::Cue::HoldBroken);
      say("Hold broken - go back up", true);
      break;
    case EventType::OverLimit:
      cue(fb::Cue::OverLimit);
      snprintf(b, sizeof(b), "Too far! Limit %.0f", p.limitDeg);
      say(b, true, 3000);
      break;
    case EventType::TooFast:
      cue(fb::Cue::TooFast);
      say("Slow down", true);
      break;
    case EventType::KneeBent:
      cue(fb::Cue::Posture);
      say("Keep the knee straight", true);
      break;
    case EventType::Rotated:
      cue(fb::Cue::Posture);
      say(exercise == Exercise::ElbowFlexion ? "Keep the thumb up" : "Leg rotated - realign", true);
      break;
    case EventType::RepDone:
      if (e.valid) {
        cue(fb::Cue::GoodRep);
        snprintf(b, sizeof(b), "Good rep! %u/%u", (unsigned)e.validReps, (unsigned)p.reps);
        say(b);
      } else {
        if (e.faults & kFaultIncomplete) {
          cue(fb::Cue::Incomplete);
          snprintf(b, sizeof(b), "Not counted: reach %.0f", p.targetDeg);
        } else if (e.faults & kFaultHoldShort) {
          cue(fb::Cue::Incomplete);
          snprintf(b, sizeof(b), "Not counted: hold %.0f s", p.holdSec);
        } else {
          // The specific fault was already cued the moment it happened.
          uint16_t f = e.faults;
          uint16_t bit = f & (uint16_t)(-(int16_t)f);  // lowest set bit
          snprintf(b, sizeof(b), "Not counted: %s", faultText(bit));
        }
        say(b, true, 3000);
      }
      Serial.printf("[rep] %s peak %.1f hold %.2fs faults 0x%02x -> %u valid / %u attempts\n",
                    e.valid ? "VALID" : "not counted", e.value, e.value2, e.faults,
                    (unsigned)e.validReps, (unsigned)e.attempts);
      break;
    case EventType::SetComplete:
      if (mode == Mode::Guided) fb::play(fb::Cue::SetComplete);
      else fb::play(fb::Cue::Target);  // one quiet beep: "you can stop now"
      say("Set complete!", false, 5000);
      finishAt = millis() + 2500;
      break;
    default:
      break;
  }
}

// What the active screen says when no event message is showing.
const char* phaseHint(const LiveData& d, char* b, size_t n) {
  const ExerciseParams& p = sessionParams;
  const char* verb = exercise == Exercise::KneeFlexion ? "Bend knee"
                     : exercise == Exercise::ElbowFlexion ? "Bend elbow" : "Lift leg";
  switch (d.phase) {
    case Phase::Rest:
      snprintf(b, n, "%s to %.0f", verb, p.targetDeg);
      break;
    case Phase::Moving:
      snprintf(b, n, "Keep going to %.0f", p.targetDeg);
      break;
    case Phase::Holding:
      snprintf(b, n, "Hold... %.1f s", d.holdElapsed);
      break;
    case Phase::Held:
      snprintf(b, n, "Now return slowly");
      break;
    case Phase::Returning:
      snprintf(b, n, "Return to start");
      break;
  }
  return b;
}

// ------------------------------- buttons ------------------------------------
void onButton(const btn::Event& e) {
  const bool shortP = e.press == btn::Press::Short;
  const bool longP = e.press == btn::Press::Long;
  const btn::Id id = e.id;
  // No clicks during a test: position sense is done with eyes closed.
  if (screen != Screen::Active && screen != Screen::TestActive && shortP) fb::play(fb::Cue::Click);

  switch (screen) {
    case Screen::Boot:
      if (id == btn::Id::Ok && shortP) go(Screen::Menu);
      break;

    case Screen::Menu:
      if (!shortP) break;
      if (id == btn::Id::Next) menuSel = (menuSel + 1) % ui::kMenuItems;
      else if (id == btn::Id::Back) menuSel = (menuSel + ui::kMenuItems - 1) % ui::kMenuItems;
      else if (id == btn::Id::Ok) {
        if (menuSel < kExerciseCount) {  // menu order = rs::Exercise order
          exercise = (Exercise)menuSel;
          go(Screen::Mode);
        } else if (menuSel == 3) {
          go(Screen::TestMenu);
        } else if (menuSel == 4) {
          if (live.calibrated) go(Screen::Live);
          else { calFor = CalFor::Live; go(Screen::CalPrompt); }
        } else {
          go(Screen::Info);
        }
      }
      lastDraw = 0;
      break;

    case Screen::Mode:
      if (!shortP) break;
      if (id == btn::Id::Next) modeSel ^= 1;
      else if (id == btn::Id::Back) go(Screen::Menu);
      else if (id == btn::Id::Ok) {
        mode = (Mode)modeSel;
        calFor = CalFor::Exercise;
        go(Screen::CalPrompt);
      }
      lastDraw = 0;
      break;

    case Screen::CalPrompt:
      if (!shortP) break;
      if (id == btn::Id::Ok) startCalibration(calFor);
      // Tests always recalibrate: chair stand and TUG need the standing pose.
      else if (id == btn::Id::Next && live.calibrated && calFor != CalFor::Test)
        go(calFor == CalFor::Live ? Screen::Live : Screen::Ready);
      else if (id == btn::Id::Back)
        go(calFor == CalFor::Live ? Screen::Menu : calFor == CalFor::Test ? Screen::TestMenu : Screen::Mode);
      break;

    case Screen::Calibrating:
      if (id == btn::Id::Back && shortP) go(Screen::CalPrompt);
      break;

    case Screen::CalFailed:
      if (!shortP) break;
      if (id == btn::Id::Ok) startCalibration(calFor);
      else if (id == btn::Id::Back) go(Screen::Menu);
      break;

    case Screen::Ready:
      if (!shortP) break;
      if (id == btn::Id::Ok) startSession();
      else if (id == btn::Id::Next) { calFor = CalFor::Exercise; go(Screen::CalPrompt); }
      else if (id == btn::Id::Back) go(Screen::Menu);
      break;

    case Screen::TestMenu:
      if (!shortP) break;
      if (id == btn::Id::Next) testSel = (testSel + 1) % ui::kTestItems;
      else if (id == btn::Id::Back) go(Screen::Menu);
      else if (id == btn::Id::Ok) {
        testKind = (TestKind)testSel;  // menu order = rs::TestKind order
        calFor = CalFor::Test;
        go(Screen::CalPrompt);
      }
      lastDraw = 0;
      break;

    case Screen::TestReady:
      if (!shortP) break;
      if (id == btn::Id::Ok) startTest();
      else if (id == btn::Id::Next && testKind == TestKind::PositionSense)
        posTarget = posTarget >= 60 ? 30 : posTarget + 15;  // 30 / 45 / 60
      else if (id == btn::Id::Back) go(Screen::TestMenu);
      lastDraw = 0;
      break;

    case Screen::TestActive:
      if (id == btn::Id::Back && longP) sensing::cancelTest();  // tick() shows "stopped"
      else if (shortP) say("Hold BACK to stop");
      break;

    case Screen::TestResult:
      if (!shortP) break;
      if (id == btn::Id::Ok) leaveTest(Screen::TestReady);
      else if (id == btn::Id::Back) leaveTest(Screen::TestMenu);
      break;

    case Screen::Active:
      if (id == btn::Id::Back && longP) finishSession();
      else if (shortP) say("Hold BACK to finish");
      break;

    case Screen::Summary:
      if (!shortP) break;
      if (id == btn::Id::Next) summaryPage = (summaryPage + 1) % ui::kSummaryPages;
      else if (id == btn::Id::Back) summaryPage = (summaryPage + ui::kSummaryPages - 1) % ui::kSummaryPages;
      else if (id == btn::Id::Ok) go(Screen::Menu);
      lastDraw = 0;
      break;

    case Screen::Live:
      if (!shortP) break;
      if (id == btn::Id::Ok) startCalibration(CalFor::Live);
      else if (id == btn::Id::Back) go(Screen::Menu);
      break;

    case Screen::Info:
      if (shortP && (id == btn::Id::Ok || id == btn::Id::Back)) go(Screen::Menu);
      break;
  }
}

// ------------------------------- per-screen logic ---------------------------
void tick() {
  const uint32_t now = millis();
  switch (screen) {
    case Screen::Boot:
      if (live.valid && live.thighOk && live.shinOk && now - screenSince > 1500) go(Screen::Menu);
      break;

    case Screen::Calibrating: {
      if (live.cal == CalState::WaitingStill || live.cal == CalState::Collecting) calSeenRunning = true;
      if (!calSeenRunning) {
        if (now - calRequestedAt > 1000) {  // request got lost? ask again
          calRequestedAt = now;
          sensing::requestCalibration();
        }
        break;
      }
      if (live.cal == CalState::Done) {
        fb::play(fb::Cue::CalDone);
        Serial.println("[cal] done");
        go(calFor == CalFor::Live ? Screen::Live
           : calFor == CalFor::Test ? Screen::TestReady : Screen::Ready);
      } else if (live.cal == CalState::FailMountThigh || live.cal == CalState::FailMountShin ||
                 live.cal == CalState::FailTimeout) {
        calFailState = live.cal;
        fb::play(fb::Cue::Error);
        Serial.printf("[cal] failed: %s\n", calStateText(live.cal));
        go(Screen::CalFailed);
      }
      break;
    }

    case Screen::Active:
      if (live.sensorsLost && !lostCueGiven) {
        fb::play(fb::Cue::Error);  // always audible: the data is not being recorded
        lostCueGiven = true;
      }
      if (!live.sensorsLost) lostCueGiven = false;
      if (finishAt && (int32_t)(now - finishAt) >= 0) finishSession();
      break;

    case Screen::TestActive: {
      const TestPhase ph = live.testPhase;
      const bool running = ph != TestPhase::Idle && ph != TestPhase::Done && ph != TestPhase::Aborted;
      // Ignore the previous test's state until the sensor task has started this one.
      if (!testSeenRunning) {
        if (running) testSeenRunning = true;
        else if (now - testRequestedAt > 1000) {
          testRequestedAt = now;
          sensing::startTest(testKind, posTarget);
        }
        break;
      }
      if (live.sensorsLost && !lostCueGiven) {
        fb::play(fb::Cue::Error);
        lostCueGiven = true;
      }
      if (!live.sensorsLost) lostCueGiven = false;
      if (ph == TestPhase::Done || ph == TestPhase::Aborted) finishTest(live);
      break;
    }

    default:
      break;
  }
}

void draw() {
  char b[40];
  switch (screen) {
    case Screen::Boot: {
      ui::BootInfo bi{live.thighOk, live.shinOk, live.thighPresent, live.shinPresent, store::fsOk(),
                      net::apSsid()};
      ui::boot(bi);
      break;
    }
    case Screen::Menu: ui::menu(menuSel); break;
    case Screen::Mode: ui::mode(exercise, modeSel); break;
    case Screen::CalPrompt:
      if (calFor == CalFor::Test) ui::testCalPrompt();
      else ui::calPrompt(exercise, live.calibrated);
      break;
    case Screen::Calibrating: ui::calibrating(live); break;
    case Screen::CalFailed:
      ui::calFailed(calFailState, calFor == CalFor::Exercise && exercise == Exercise::ElbowFlexion);
      break;
    case Screen::Ready: ui::ready(exercise, mode, store::config().params[(int)exercise]); break;
    case Screen::Active: {
      const bool showMsg = msg[0] && (int32_t)(msgUntil - millis()) > 0;
      if (showMsg) ui::active(live, mode, msg, msgUrgent);
      else ui::active(live, mode, phaseHint(live, b, sizeof(b)), false);
      break;
    }
    case Screen::Summary:
      ui::summary(summaryPage, exercise, mode, lastStats, sessionParams, lastSessionId,
                  net::uploadStatus());
      break;
    case Screen::Live: ui::liveAngles(live); break;
    case Screen::TestMenu: ui::testMenu(testSel); break;
    case Screen::TestReady: ui::testReady(testKind, posTarget); break;
    case Screen::TestActive: {
      const bool showMsg = msg[0] && (int32_t)(msgUntil - millis()) > 0;
      // Until the sensor task picks up the new test, show it as "sit down"/"find it".
      if (!testSeenRunning) {
        LiveData d = live;
        d.testKind = testKind;
        d.testPhase = testKind == TestKind::PositionSense ? TestPhase::Present : TestPhase::NeedSeated;
        d.test = TestResult();
        d.test.targetDeg = posTarget;
        d.testHold = 0;
        d.testArmed = false;
        d.testCalHint = false;
        ui::testActive(d, nullptr, false);
      } else {
        ui::testActive(live, showMsg ? msg : nullptr, showMsg && msgUrgent);
      }
      break;
    }
    case Screen::TestResult: ui::testResult(testKind, lastTest, lastTestAbort, lastTestId); break;
    case Screen::Info:
      ui::info(net::apSsid(), net::apIp(), net::staConnected(), net::staIp(),
               store::sessionsStored(), store::pendingUploads(), net::uploadStatus(), live);
      break;
  }
}

}  // namespace

// ------------------------------- Arduino ------------------------------------
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\n=== RehabSense " RS_FW_VERSION " ===");

  fb::begin();
  btn::begin();

  bool fs = store::begin();
  fb::setBuzzerEnabled(store::config().buzzerOn);
  Serial.printf("[boot] storage %s, %lu sessions stored\n", fs ? "OK" : "FAILED",
                (unsigned long)store::sessionsStored());

  // One mutex guards the IMU bus; it only matters if the OLED shares it.
  imuBusMutex = xSemaphoreCreateMutex();
  sensing::setSharedBusMutex(imuBusMutex);
  bool imus = sensing::begin();
  live = sensing::snapshot();
  Serial.printf("[boot] thigh IMU (0x%02X) %s, shin IMU (0x%02X) %s\n", IMU_ADDR_THIGH,
                live.thighOk ? "OK" : (live.thighPresent ? "ERROR" : "MISSING"), IMU_ADDR_SHIN,
                live.shinOk ? "OK" : (live.shinPresent ? "ERROR" : "MISSING"));

  bool oled = ui::begin(imuBusMutex);
  Serial.printf("[boot] OLED %s%s\n", oled ? "OK" : "NOT FOUND",
                oled && ui::sharedBus() ? " (sharing the IMU bus - slower refresh)" : "");

  net::begin();
  Serial.printf("[boot] hotspot %s / %s -> http://%s\n", net::apSsid().c_str(), AP_PASSWORD,
                net::apIp().c_str());

  if (!imus) fb::play(fb::Cue::Error);
  go(Screen::Boot);
}

void loop() {
  const uint32_t now = millis();
  net::loop();
  fb::update();

  if (now - lastSnap >= 20) {
    LiveData d = sensing::snapshot();
    if (d.valid) live = d;
    lastSnap = now;
  }

  Event e;
  while (sensing::popEvent(e)) onEvent(e);

  btn::Event be;
  if (btn::poll(be)) onButton(be);

  tick();

  const uint32_t refresh = ui::sharedBus() ? UI_REFRESH_SHARED_MS : UI_REFRESH_MS;
  if (lastDraw == 0 || now - lastDraw >= refresh) {
    draw();
    lastDraw = now ? now : 1;
  }

  // Live-angle screen streams CSV for the Arduino Serial Plotter / goniometer table.
  if (screen == Screen::Live && now - lastSerial >= 100) {
    Serial.printf("knee:%.1f,thigh:%.1f,shin:%.1f,rot:%.1f\n", live.joint.kneeDeg,
                  live.joint.thighElevDeg, live.joint.shinDeg, live.joint.rotationDeg);
    lastSerial = now;
  }

  delay(2);  // yield; the sensor task has its own schedule
}
