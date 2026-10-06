// buttons.cpp
#include "buttons.h"

#include "config.h"

namespace btn {
namespace {

constexpr uint32_t kDebounceMs = 25;
constexpr uint32_t kLongMs = 700;

struct State {
  uint8_t pin;
  bool stable = false;      // true = pressed (debounced)
  bool lastRaw = false;
  uint32_t lastChange = 0;
  uint32_t pressedAt = 0;
  bool longFired = false;
};

State s[3] = {{PIN_BTN_NEXT}, {PIN_BTN_OK}, {PIN_BTN_BACK}};

}  // namespace

void begin() {
  for (auto& b : s) {
    pinMode(b.pin, INPUT_PULLUP);
    b.lastRaw = b.stable = (digitalRead(b.pin) == LOW);
    b.lastChange = millis();
  }
}

bool poll(Event& e) {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < 3; i++) {
    State& b = s[i];
    const bool raw = digitalRead(b.pin) == LOW;
    if (raw != b.lastRaw) {
      b.lastRaw = raw;
      b.lastChange = now;
    }
    if (now - b.lastChange >= kDebounceMs && raw != b.stable) {
      b.stable = raw;
      if (raw) {
        b.pressedAt = now;
        b.longFired = false;
      } else if (!b.longFired) {
        e = Event{(Id)i, Press::Short};
        return true;
      }
    }
    if (b.stable && !b.longFired && now - b.pressedAt >= kLongMs) {
      b.longFired = true;
      e = Event{(Id)i, Press::Long};
      return true;
    }
  }
  return false;
}

}  // namespace btn
