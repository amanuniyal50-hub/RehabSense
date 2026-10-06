// feedback.h -- non-blocking haptic + audio cue player.
// Each cue is a short pattern of motor/buzzer on-off steps. Higher-priority
// cues (safety limit) interrupt lower ones; nothing ever calls delay().
#pragma once
#include <Arduino.h>

namespace fb {

enum class Cue : uint8_t {
  None = 0,
  Click,        // button press
  Target,       // target angle reached       : short beep
  HoldDone,     // hold complete              : double beep
  GoodRep,      // valid rep counted          : short buzz
  Incomplete,   // rep ended without target   : long beep
  HoldBroken,   // dropped out of the hold    : low buzz
  OverLimit,    // passed the physio's limit  : urgent triple buzz+beep
  TooFast,      // slow down                  : double buzz
  Posture,      // knee bent / leg rotated    : long buzz
  SetComplete,  // all reps done              : celebration
  CalDone,      // calibration finished       : two beeps
  Error,        // something needs attention  : three long beeps
  Go,           // clinical test starts       : long beep + buzz
};

void begin();
void play(Cue c);
void update();                 // call every loop()
void stop();
void setBuzzerEnabled(bool on);
bool buzzerEnabled();

}  // namespace fb
