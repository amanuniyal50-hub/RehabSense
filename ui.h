// ui.h -- all OLED drawing (128x64 SSD1306). No app logic in here.
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "rehab_logic.h"
#include "sensing.h"

namespace ui {

// Finds the OLED on its own bus (pins 18/19). If it is not there, looks on the
// IMU bus and, if found there, shares that bus using `imuBusMutex`.
bool begin(SemaphoreHandle_t imuBusMutex);
bool present();
bool sharedBus();

struct BootInfo {
  bool thighOk, shinOk, thighPresent, shinPresent, fsOk;
  String ap;
};

void boot(const BootInfo& b);
void menu(int sel);
void mode(rs::Exercise ex, int sel);
void calPrompt(rs::Exercise ex, bool haveCal);
void calibrating(const LiveData& d);
void calFailed(rs::CalState s, bool arm = false);
void ready(rs::Exercise ex, rs::Mode m, const rs::ExerciseParams& p);
void active(const LiveData& d, rs::Mode m, const char* msg, bool urgent);
void summary(int page, rs::Exercise ex, rs::Mode m, const rs::SessionStats& s,
             const rs::ExerciseParams& p, uint32_t sessionId, const String& sync);
void liveAngles(const LiveData& d);
void info(const String& ap, const String& apIp, bool sta, const String& staIp,
          uint32_t stored, uint32_t pending, const String& sync, const LiveData& d);
void message(const char* title, const char* line1, const char* line2 = "");

// Clinical tests
void testMenu(int sel);
void testCalPrompt();
void testReady(rs::TestKind k, float targetDeg);
void testActive(const LiveData& d, const char* msg, bool urgent);
void testResult(rs::TestKind k, const rs::TestResult& r, rs::TestAbort abort, uint32_t testId);

constexpr int kMenuItems = 6;       // knee, SLR, elbow (= rs::Exercise order), tests, live, info
constexpr int kTestItems = 3;       // chair stand, TUG, position sense (= rs::TestKind order)
constexpr int kSummaryPages = 4;

}  // namespace ui
