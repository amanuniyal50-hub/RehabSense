// ui.cpp
#include "ui.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "config.h"

using namespace rs;

namespace ui {
namespace {

Adafruit_SSD1306* oled = nullptr;
bool found = false;
bool shared = false;
SemaphoreHandle_t busMtx = nullptr;

// ------------------------------ primitives ----------------------------------
void clear() {
  oled->clearDisplay();
  oled->setTextColor(SSD1306_WHITE);
  oled->setTextSize(1);
  oled->setTextWrap(false);
}

void flush() {
  if (shared && busMtx) xSemaphoreTake(busMtx, portMAX_DELAY);
  oled->display();
  if (shared && busMtx) xSemaphoreGive(busMtx);
}

void text(int x, int y, const char* s, uint8_t size = 1) {
  oled->setTextSize(size);
  oled->setCursor(x, y);
  oled->print(s);
  oled->setTextSize(1);
}

void center(int y, const char* s, uint8_t size = 1) {
  int w = (int)strlen(s) * 6 * size;
  text((128 - w) / 2 < 0 ? 0 : (128 - w) / 2, y, s, size);
}

void titleBar(const char* left, const char* right = nullptr) {
  oled->fillRect(0, 0, 128, 11, SSD1306_WHITE);
  oled->setTextColor(SSD1306_BLACK);
  text(2, 2, left);
  if (right) text(126 - (int)strlen(right) * 6, 2, right);
  oled->setTextColor(SSD1306_WHITE);
}

void footer(const char* s) { text(0, 56, s); }

void degree(int x, int y, int r) { oled->drawCircle(x, y, r, SSD1306_WHITE); }

void bar(int x, int y, int w, int h, float frac) {
  frac = clampf(frac, 0.f, 1.f);
  oled->drawRect(x, y, w, h, SSD1306_WHITE);
  int fw = (int)((w - 2) * frac);
  if (fw > 0) oled->fillRect(x + 1, y + 1, fw, h - 2, SSD1306_WHITE);
}

// Copy with truncation to `maxChars` characters.
void fit(char* out, size_t outSize, const char* s, size_t maxChars) {
  size_t n = strlen(s);
  if (n > maxChars) n = maxChars;
  if (n >= outSize) n = outSize - 1;
  memcpy(out, s, n);
  out[n] = 0;
}

bool probe(TwoWire& w, uint8_t addr) {
  w.beginTransmission(addr);
  return w.endTransmission() == 0;
}

}  // namespace

bool present() { return found; }
bool sharedBus() { return shared; }

bool begin(SemaphoreHandle_t imuBusMutex) {
  busMtx = imuBusMutex;
  uint8_t addr = 0;

  // 1) Preferred: dedicated bus.
  Wire1.begin(PIN_OLED_SDA, PIN_OLED_SCL, OLED_I2C_HZ);
  if (probe(Wire1, OLED_ADDR)) addr = OLED_ADDR;
  else if (probe(Wire1, 0x3D)) addr = 0x3D;
  if (addr) {
    oled = new Adafruit_SSD1306(128, 64, &Wire1, -1, OLED_I2C_HZ, OLED_I2C_HZ);
    shared = false;
  } else {
    // 2) Fallback: OLED wired onto the IMU bus.
    Wire1.end();
    if (busMtx) xSemaphoreTake(busMtx, portMAX_DELAY);
    if (probe(Wire, OLED_ADDR)) addr = OLED_ADDR;
    else if (probe(Wire, 0x3D)) addr = 0x3D;
    if (busMtx) xSemaphoreGive(busMtx);
    if (!addr) return false;
    oled = new Adafruit_SSD1306(128, 64, &Wire, -1, 400000, IMU_I2C_HZ);
    shared = true;
  }

  if (shared && busMtx) xSemaphoreTake(busMtx, portMAX_DELAY);
  // periphBegin = false: we already configured the bus and its pins.
  found = oled->begin(SSD1306_SWITCHCAPVCC, addr, true, false);
  if (shared && busMtx) xSemaphoreGive(busMtx);
  if (found) {
    clear();
    center(24, "RehabSense", 2);
    flush();
  }
  return found;
}

// ------------------------------- screens ------------------------------------
void boot(const BootInfo& b) {
  if (!found) return;
  clear();
  titleBar("RehabSense", "v" RS_FW_VERSION);
  text(0, 14, "Thigh sensor");
  text(84, 14, b.thighOk ? "OK" : (b.thighPresent ? "error" : "missing"));
  text(0, 24, "Shin sensor");
  text(84, 24, b.shinOk ? "OK" : (b.shinPresent ? "error" : "missing"));
  text(0, 34, "Storage");
  text(84, 34, b.fsOk ? "OK" : "error");
  if (b.thighOk && b.shinOk) {
    text(0, 46, "Starting...");
  } else if (b.thighPresent && !b.shinPresent) {
    text(0, 44, "Shin: AD0 pin to 3V3");
    footer("OK = continue anyway");
  } else if (!b.thighPresent && !b.shinPresent) {
#define RS_STR2(x) #x
#define RS_STR(x) RS_STR2(x)
    text(0, 44, "Check SDA" RS_STR(PIN_IMU_SDA) " SCL" RS_STR(PIN_IMU_SCL) " 3V3");  // from config.h
    footer("OK = continue anyway");
  } else {
    text(0, 44, "Check sensor wiring");
    footer("OK = continue anyway");
  }
  flush();
}

void menu(int sel) {
  if (!found) return;
  static const char* items[kMenuItems] = {"Knee flexion", "Straight leg raise", "Elbow flexion",
                                          "Clinical tests", "Live angles", "Device info"};
  clear();
  titleBar("Choose exercise");
  for (int i = 0; i < kMenuItems; i++) {
    int y = 12 + i * 9;  // 9 px rows: six items fit under the title bar
    if (i == sel) {
      oled->fillRect(0, y - 1, 128, 9, SSD1306_WHITE);
      oled->setTextColor(SSD1306_BLACK);
    }
    text(4, y, items[i]);
    oled->setTextColor(SSD1306_WHITE);
  }
  flush();
}

void mode(Exercise ex, int sel) {
  if (!found) return;
  clear();
  titleBar(exerciseName(ex));
  const char* names[2] = {"Guided", "Assessment"};
  const char* sub[2] = {"sound + vibration cues", "silent tracking only"};
  for (int i = 0; i < 2; i++) {
    int y = 14 + i * 20;
    if (i == sel) {
      oled->fillRect(0, y - 1, 128, 19, SSD1306_WHITE);
      oled->setTextColor(SSD1306_BLACK);
    }
    text(4, y, names[i]);
    text(4, y + 9, sub[i]);
    oled->setTextColor(SSD1306_WHITE);
  }
  footer("NEXT switch  OK go");
  flush();
}

void calPrompt(Exercise ex, bool haveCal) {
  if (!found) return;
  clear();
  titleBar("Calibrate");
  const bool elbow = ex == Exercise::ElbowFlexion;
  text(0, 14, elbow ? "Straighten the arm" : "Straighten the leg");
  text(0, 23, "fully, keep it still.");
  text(0, 35, ex == Exercise::StraightLegRaise ? "Lie on your back." : "Sensors flat on the");
  text(0, 44, ex == Exercise::StraightLegRaise ? "Sensors on outer leg." : elbow ? "outer side of arm." : "outer side of leg.");
  footer(haveCal ? "OK start  NEXT skip" : "OK start   BACK menu");
  flush();
}

void calibrating(const LiveData& d) {
  if (!found) return;
  clear();
  titleBar("Calibrating");
  if (d.cal == CalState::WaitingStill) {
    center(18, "Hold still...", 1);
    center(30, "movement detected", 1);
  } else {
    center(18, "Keep still", 1);
  }
  bar(8, 42, 112, 9, d.calProgress);
  footer("BACK cancel");
  flush();
}

void calFailed(CalState s, bool arm) {
  if (!found) return;
  clear();
  titleBar("Calibration failed");
  switch (s) {
    case CalState::FailMountThigh:
      text(0, 16, arm ? "Upper arm sensor is" : "Thigh sensor is not");
      text(0, 25, arm ? "not flat on the SIDE" : "flat on the SIDE of");
      text(0, 34, arm ? "of the arm. Re-strap." : "the leg. Re-strap it.");
      break;
    case CalState::FailMountShin:
      text(0, 16, arm ? "Forearm sensor is not" : "Shin sensor is not");
      text(0, 25, arm ? "flat on the SIDE of" : "flat on the SIDE of");
      text(0, 34, arm ? "the arm. Re-strap it." : "the leg. Re-strap it.");
      break;
    default:
      text(0, 16, arm ? "The arm kept moving." : "The leg kept moving.");
      text(0, 25, arm ? "Rest it on a table" : "Rest it on the bed");
      text(0, 34, "and try again.");
      break;
  }
  footer("OK retry   BACK menu");
  flush();
}

void ready(Exercise ex, Mode m, const ExerciseParams& p) {
  if (!found) return;
  char b[32];
  clear();
  titleBar(exerciseName(ex));
  snprintf(b, sizeof(b), "Mode    %s", modeName(m));
  text(0, 14, b);
  snprintf(b, sizeof(b), "Target  %.0f  hold %.0fs", p.targetDeg, p.holdSec);
  text(0, 24, b);
  snprintf(b, sizeof(b), "Limit   %.0f  reps %u", p.limitDeg, (unsigned)p.reps);
  text(0, 34, b);
  footer("OK start  NEXT recal");
  flush();
}

void active(const LiveData& d, Mode m, const char* msg, bool urgent) {
  if (!found) return;
  char b[32];
  clear();
  const ExerciseParams& p = d.params;
  if (m == Mode::Assessment) {
    titleBar("Assessment", "REC");
    center(16, "Recording", 2);
    uint32_t sec = (uint32_t)d.stats.durationSec();
    snprintf(b, sizeof(b), "Attempts %u   %lu:%02lu", (unsigned)d.stats.attempts,
             (unsigned long)(sec / 60), (unsigned long)(sec % 60));
    center(38, b);
  } else {
    snprintf(b, sizeof(b), "%u/%u", (unsigned)d.stats.validReps, (unsigned)p.reps);
    titleBar(d.exercise == Exercise::KneeFlexion ? "Knee flexion" : d.exercise == Exercise::ElbowFlexion ? "Elbow flexion" : "Leg raise", b);

    // Big angle
    int ang = (int)lroundf(d.metric);
    snprintf(b, sizeof(b), "%d", ang);
    text(0, 15, b, 3);
    degree((int)strlen(b) * 18 + 3, 17, 3);

    // Right column
    snprintf(b, sizeof(b), "Tgt %.0f", p.targetDeg);
    text(76, 14, b);
    if (d.phase == Phase::Holding || d.phase == Phase::Held) {
      snprintf(b, sizeof(b), "%.1f/%.0fs", d.holdElapsed, p.holdSec);
    } else {
      snprintf(b, sizeof(b), "Lim %.0f", p.limitDeg);
    }
    text(76, 24, b);
    if (d.exercise == Exercise::StraightLegRaise) {
      snprintf(b, sizeof(b), "Knee %.0f", d.joint.kneeDeg);
      text(76, 34, b);
    }

    // Range bar: 0 .. limit+10, with target band and limit marks
    const int bx = 0, by = 43, bw = 128, bh = 8;
    const float full = p.limitDeg + 10.f;
    bar(bx, by, bw, bh, d.metric / full);
    auto xAt = [&](float deg) { return bx + (int)((bw - 1) * clampf(deg / full, 0.f, 1.f)); };
    int xt = xAt(p.targetDeg), xl = xAt(p.limitDeg), xlo = xAt(p.targetDeg - p.toleranceDeg);
    oled->drawFastVLine(xt, by - 3, bh + 3, SSD1306_WHITE);
    oled->drawFastVLine(xlo, by - 2, 2, SSD1306_WHITE);
    for (int y = by - 3; y < by + bh; y += 2) oled->drawPixel(xl, y, SSD1306_INVERSE);
  }

  if (d.sensorsLost) {
    msg = "Sensor lost! Cables?";
    urgent = true;
  }
  if (msg && msg[0]) {
    char line[22];
    fit(line, sizeof(line), msg, 21);
    if (urgent) {
      oled->fillRect(0, 54, 128, 10, SSD1306_WHITE);
      oled->setTextColor(SSD1306_BLACK);
    }
    text(1, 55, line);
    oled->setTextColor(SSD1306_WHITE);
  }
  flush();
}

void summary(int page, Exercise ex, Mode m, const SessionStats& s, const ExerciseParams& p,
             uint32_t sessionId, const String& sync) {
  if (!found) return;
  char b[40];
  clear();
  snprintf(b, sizeof(b), "%d/%d", page + 1, kSummaryPages);
  titleBar("Session summary", b);
  switch (page) {
    case 0: {
      snprintf(b, sizeof(b), "%u/%u", (unsigned)s.validReps, (unsigned)p.reps);
      center(15, b, 3);
      center(41, m == Mode::Guided ? "valid reps" : "valid reps (assessed)");
      break;
    }
    case 1:
      snprintf(b, sizeof(b), "Attempts   %u", (unsigned)s.attempts);
      text(0, 14, b);
      snprintf(b, sizeof(b), "Max angle  %.0f", s.maxAngle);
      text(0, 24, b);
      snprintf(b, sizeof(b), "Avg peak   %.0f", s.avgPeak());
      text(0, 34, b);
      snprintf(b, sizeof(b), "Avg hold   %.1f s", s.avgHold());
      text(0, 44, b);
      break;
    case 2:
      snprintf(b, sizeof(b), "Incomplete %u", (unsigned)s.incomplete);
      text(0, 13, b);
      snprintf(b, sizeof(b), "Short hold %u", (unsigned)s.holdShort);
      text(64, 13, b);
      snprintf(b, sizeof(b), "Over limit %u", (unsigned)s.overLimit);
      text(0, 25, b);
      snprintf(b, sizeof(b), "Too fast   %u", (unsigned)s.tooFast);
      text(0, 35, b);
      if (ex == Exercise::StraightLegRaise) {
        snprintf(b, sizeof(b), "Knee bent  %u", (unsigned)s.kneeBent);
        text(0, 45, b);
      } else {
        snprintf(b, sizeof(b), "Rotated    %u", (unsigned)s.rotated);
        text(0, 45, b);
      }
      break;
    default: {
      if (sessionId) snprintf(b, sizeof(b), "Saved as session #%lu", (unsigned long)sessionId);
      else snprintf(b, sizeof(b), "Could not save!");
      text(0, 14, b);
      char line[22];
      fit(line, sizeof(line), sync.c_str(), 21);
      text(0, 28, line);
      if (sync.length() > 21) {
        fit(line, sizeof(line), sync.c_str() + 21, 21);
        text(0, 37, line);
      }
      break;
    }
  }
  footer("NEXT page  OK done");
  flush();
}

void liveAngles(const LiveData& d) {
  if (!found) return;
  char b[32];
  clear();
  titleBar("Live angles", d.calibrated ? nullptr : "no cal");
  text(0, 16, "Knee");
  snprintf(b, sizeof(b), "%.1f", d.joint.kneeDeg);
  text(34, 13, b, 2);
  degree(34 + (int)strlen(b) * 12 + 3, 15, 2);
  snprintf(b, sizeof(b), "Thigh %.1f", d.joint.thighElevDeg);
  text(0, 33, b);
  snprintf(b, sizeof(b), "Rot %.0f", d.joint.rotationDeg);
  text(78, 33, b);
  snprintf(b, sizeof(b), "Shin  %.1f", d.joint.shinDeg);
  text(0, 43, b);
  snprintf(b, sizeof(b), "%.0fHz", d.sampleHz);
  text(90, 43, b);
  footer("OK recal   BACK menu");
  flush();
}

void info(const String& ap, const String& apIp, bool sta, const String& staIp, uint32_t stored,
          uint32_t pending, const String& sync, const LiveData& d) {
  if (!found) return;
  char b[40], line[22];
  clear();
  titleBar("Device info");
  snprintf(b, sizeof(b), "WiFi %s", ap.c_str());
  fit(line, sizeof(line), b, 21);
  text(0, 13, line);
  snprintf(b, sizeof(b), "Pass %s", AP_PASSWORD);
  text(0, 21, b);
  snprintf(b, sizeof(b), "Open %s", apIp.c_str());
  text(0, 29, b);
  snprintf(b, sizeof(b), "Home %s", sta ? staIp.c_str() : "offline");
  fit(line, sizeof(line), b, 21);
  text(0, 37, line);
  snprintf(b, sizeof(b), "Saved %lu  sync %lu", (unsigned long)stored, (unsigned long)pending);
  text(0, 45, b);
  (void)d;
  fit(line, sizeof(line), sync.c_str(), 21);
  text(0, 55, line);
  flush();
}

void message(const char* title, const char* l1, const char* l2) {
  if (!found) return;
  clear();
  titleBar(title);
  text(0, 20, l1);
  text(0, 32, l2);
  flush();
}

// ----------------------------- clinical tests --------------------------------
namespace {
void bottomLine(const char* msg, bool urgent) {
  if (!msg || !msg[0]) return;
  char line[22];
  fit(line, sizeof(line), msg, 21);
  if (urgent) {
    oled->fillRect(0, 54, 128, 10, SSD1306_WHITE);
    oled->setTextColor(SSD1306_BLACK);
  }
  text(1, 55, line);
  oled->setTextColor(SSD1306_WHITE);
}

void lines4(const char* a, const char* b, const char* c, const char* d) {
  text(0, 14, a);
  text(0, 23, b);
  text(0, 32, c);
  text(0, 41, d);
}
}  // namespace

void testMenu(int sel) {
  if (!found) return;
  static const char* items[kTestItems] = {"30 s chair stand", "Timed Up and Go", "Position sense"};
  clear();
  titleBar("Clinical tests");
  for (int i = 0; i < kTestItems; i++) {
    int y = 14 + i * 11;
    if (i == sel) {
      oled->fillRect(0, y - 1, 128, 10, SSD1306_WHITE);
      oled->setTextColor(SSD1306_BLACK);
    }
    text(4, y, items[i]);
    oled->setTextColor(SSD1306_WHITE);
  }
  footer("OK choose  BACK menu");
  flush();
}

void testCalPrompt() {
  if (!found) return;
  clear();
  titleBar("Calibrate standing");
  lines4("Stand up straight,", "both legs still.", "Sensors flat on the", "outer side of leg.");
  footer("OK start  BACK tests");
  flush();
}

void testReady(TestKind k, float targetDeg) {
  if (!found) return;
  char b[12];
  clear();
  switch (k) {
    case TestKind::ChairStand:
      titleBar("30 s chair stand");
      lines4("Sit in the middle of", "the chair, arms", "crossed. On GO: stand", "fully, sit, repeat.");
      footer("OK start  BACK tests");
      break;
    case TestKind::TimedUpGo:
      titleBar("Timed Up and Go");
      lines4("Sit back in a chair.", "Line 3 m away. On GO:", "stand, walk to it,", "turn, walk back, sit.");
      footer("OK start  BACK tests");
      break;
    default:
      snprintf(b, sizeof(b), "%.0f deg", targetDeg);
      titleBar("Position sense", b);
      lines4("Close your eyes.", "Move until it buzzes,", "hold still, go back,", "then find it again.");
      footer("OK start  NEXT angle");
      break;
  }
  flush();
}

void testActive(const LiveData& d, const char* msg, bool urgent) {
  if (!found) return;
  char b[32];
  clear();
  const TestResult& r = d.test;
  if (d.testKind == TestKind::PositionSense) {
    snprintf(b, sizeof(b), "%u/%d", (unsigned)(r.trials + 1 > kPosTrials ? kPosTrials : r.trials + 1),
             kPosTrials);
    titleBar("Position sense", b);
    const int tgt = (int)lroundf(r.targetDeg);
    switch (d.testPhase) {
      case TestPhase::Present:
        if (!d.testArmed) {
          snprintf(b, sizeof(b), "Move away from %d", tgt);
          text(0, 14, b);
          text(0, 23, "(start position)");
        } else if (d.testHold > 0.05f) {
          text(0, 14, "Hold still here...");
          bar(0, 24, 128, 7, d.testHold / 3.0f);
        } else {
          text(0, 14, "Move slowly until");
          text(0, 23, "the brace buzzes.");
        }
        break;
      case TestPhase::Return:
        text(0, 14, "Remembered. Go back");
        text(0, 23, "to start, keep still");
        break;
      default:
        text(0, 14, "Eyes closed: find it");
        text(0, 23, "again, hold still.");
        break;
    }
    // For the physio watching: live angle and the trials so far.
    snprintf(b, sizeof(b), "Knee %d", (int)lroundf(d.joint.kneeDeg));
    text(0, 35, b);
    int x = 0;
    for (int i = 0; i < r.trials && i < kPosTrials; i++) {
      snprintf(b, sizeof(b), "T%d%+.0f", i + 1, r.trialErr[i]);
      text(x, 45, b);
      x += 42;
    }
  } else {
    titleBar(d.testKind == TestKind::ChairStand ? "30 s chair stand" : "Timed Up and Go");
    if (d.testPhase == TestPhase::NeedSeated) {
      center(18, "Sit down", 2);
      center(38, "to start the test");
      if (d.testCalHint) {
        msg = "Recalibrate STANDING";
        urgent = true;
      }
    } else if (d.testPhase == TestPhase::Countdown) {
      snprintf(b, sizeof(b), "%u", (unsigned)d.testCountdown);
      center(16, b, 4);
    } else if (d.testKind == TestKind::ChairStand) {
      snprintf(b, sizeof(b), "%u", (unsigned)r.stands);
      text(0, 15, b, 3);
      text(0, 40, "stands");
      const float left = 30.f - d.testElapsed;
      snprintf(b, sizeof(b), "%.0f s", left < 0 ? 0.f : left);
      text(80, 16, b, 2);
      text(80, 34, "left");
      bar(56, 44, 72, 7, left / 30.f);
    } else {
      snprintf(b, sizeof(b), "%.1f", d.testElapsed);
      text(0, 15, b, 3);
      text((int)strlen(b) * 18 + 4, 29, "s");
      static const char* stage[3] = {"Stand up", "Walk, turn, back", "Sit down"};
      text(0, 42, stage[d.testStage > 2 ? 2 : d.testStage]);
    }
  }
  if (d.sensorsLost) {
    msg = "Sensor lost! Cables?";
    urgent = true;
  }
  bottomLine(msg, urgent);
  flush();
}

void testResult(TestKind k, const TestResult& r, TestAbort abort, uint32_t testId) {
  if (!found) return;
  char b[32];
  clear();
  if (abort != TestAbort::None) {
    titleBar("Test stopped");
    text(0, 16, testAbortText(abort));
    if (abort == TestAbort::NoWalk) {
      text(0, 28, "Walk to the 3 m line,");
      text(0, 37, "turn, come back, sit.");
    } else {
      text(0, 28, "Nothing was saved.");
    }
    footer("OK again  BACK tests");
    flush();
    return;
  }
  char saved[20];
  if (testId) snprintf(saved, sizeof(saved), "saved #%lu", (unsigned long)testId);
  else snprintf(saved, sizeof(saved), "NOT saved!");
  text(128 - (int)strlen(saved) * 6, 47, saved);  // bottom right, above the footer
  switch (k) {
    case TestKind::ChairStand:
      titleBar("Chair stand result");
      snprintf(b, sizeof(b), "%.0f", r.score);
      center(14, b, 3);
      center(39, r.halfStand ? "stands (1 was half)" : "stands in 30 s");
      snprintf(b, sizeof(b), "Rise %.1f s", r.riseSec);
      text(0, 47, b);
      break;
    case TestKind::TimedUpGo:
      titleBar("Timed Up and Go");
      snprintf(b, sizeof(b), "%.1f s", r.score);
      center(14, b, 3);
      snprintf(b, sizeof(b), "Rise %.1f  Walk %.1f", r.riseSec, r.walkSec);
      text(0, 38, b);
      snprintf(b, sizeof(b), "Sit %.1f", r.sitSec);
      text(0, 47, b);
      break;
    default:
      snprintf(b, sizeof(b), "%.0f deg", r.targetDeg);
      titleBar("Position sense", b);
      snprintf(b, sizeof(b), "%.1f", r.score);
      text(0, 14, b, 3);
      text((int)strlen(b) * 18 + 4, 16, "deg mean");
      text((int)strlen(b) * 18 + 4, 26, "error");
      snprintf(b, sizeof(b), "T1 %+.0f T2 %+.0f T3 %+.0f", r.trialErr[0], r.trialErr[1], r.trialErr[2]);
      text(0, 38, b);
      snprintf(b, sizeof(b), "Bias %+.1f", r.constErr);
      text(0, 47, b);
      break;
  }
  footer("OK again  BACK tests");
  flush();
}

}  // namespace ui
