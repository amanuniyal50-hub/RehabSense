// feedback.cpp
#include "feedback.h"

#include "config.h"

namespace fb {
namespace {

constexpr uint8_t M = 1;  // motor
constexpr uint8_t B = 2;  // buzzer

struct Step {
  uint16_t ms;
  uint8_t out;
};

// Patterns end with {0,0}.
const Step kClick[] = {{25, B}, {0, 0}};
const Step kTarget[] = {{90, B}, {0, 0}};
const Step kHoldDone[] = {{80, B}, {80, 0}, {80, B}, {0, 0}};
const Step kGoodRep[] = {{160, M}, {0, 0}};
const Step kIncomplete[] = {{450, B}, {0, 0}};
const Step kHoldBroken[] = {{300, M}, {0, 0}};
const Step kOverLimit[] = {{110, M | B}, {70, 0}, {110, M | B}, {70, 0}, {110, M | B}, {0, 0}};
const Step kTooFast[] = {{90, M}, {90, 0}, {90, M}, {0, 0}};
const Step kPosture[] = {{550, M}, {0, 0}};
const Step kSetComplete[] = {{80, B}, {60, 0}, {80, B}, {60, 0}, {250, M | B}, {0, 0}};
const Step kCalDone[] = {{70, B}, {70, 0}, {70, B}, {0, 0}};
const Step kError[] = {{250, B}, {120, 0}, {250, B}, {120, 0}, {250, B}, {0, 0}};
const Step kGo[] = {{400, M | B}, {0, 0}};

struct CueDef {
  const Step* steps;
  uint8_t priority;
};

CueDef def(Cue c) {
  switch (c) {
    case Cue::Click: return {kClick, 0};
    case Cue::Target: return {kTarget, 1};
    case Cue::HoldDone: return {kHoldDone, 1};
    case Cue::GoodRep: return {kGoodRep, 1};
    case Cue::Incomplete: return {kIncomplete, 1};
    case Cue::HoldBroken: return {kHoldBroken, 1};
    case Cue::OverLimit: return {kOverLimit, 3};
    case Cue::TooFast: return {kTooFast, 2};
    case Cue::Posture: return {kPosture, 2};
    case Cue::SetComplete: return {kSetComplete, 2};
    case Cue::CalDone: return {kCalDone, 1};
    case Cue::Error: return {kError, 2};
    case Cue::Go: return {kGo, 2};
    default: return {nullptr, 0};
  }
}

const Step* cur = nullptr;
uint8_t curPrio = 0;
uint8_t idx = 0;
uint32_t stepStart = 0;
bool buzzerOn = true;

void apply(uint8_t out) {
  digitalWrite(PIN_MOTOR, (out & M) ? HIGH : LOW);
  digitalWrite(PIN_BUZZER, ((out & B) && buzzerOn) ? HIGH : LOW);
}

}  // namespace

void begin() {
  pinMode(PIN_MOTOR, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  apply(0);
}

void setBuzzerEnabled(bool on) { buzzerOn = on; }
bool buzzerEnabled() { return buzzerOn; }

void stop() {
  cur = nullptr;
  curPrio = 0;
  apply(0);
}

void play(Cue c) {
  CueDef d = def(c);
  if (!d.steps) return;
  if (cur && d.priority < curPrio) return;  // don't interrupt something more important
  cur = d.steps;
  curPrio = d.priority;
  idx = 0;
  stepStart = millis();
  apply(cur[0].out);
}

void update() {
  if (!cur) return;
  const uint32_t now = millis();
  while (cur && now - stepStart >= cur[idx].ms) {
    stepStart += cur[idx].ms;
    idx++;
    if (cur[idx].ms == 0) {
      stop();
      return;
    }
    apply(cur[idx].out);
  }
}

}  // namespace fb
