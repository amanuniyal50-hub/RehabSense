// buttons.h -- three debounced buttons with short and long presses.
#pragma once
#include <Arduino.h>

namespace btn {

enum class Id : uint8_t { Next = 0, Ok = 1, Back = 2 };
enum class Press : uint8_t { None = 0, Short, Long };

struct Event {
  Id id;
  Press press;
};

void begin();
// Call every loop(). Returns true and fills `e` when a press happened.
bool poll(Event& e);

}  // namespace btn
