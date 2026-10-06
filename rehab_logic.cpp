// rehab_logic.cpp  -- see rehab_logic.h for the overview.
#include "rehab_logic.h"

#include <math.h>

namespace rs {

// ------------------------------- math --------------------------------------
float wrapDeg(float a) {
  a = fmodf(a + 180.0f, 360.0f);
  if (a < 0) a += 360.0f;
  return a - 180.0f;
}

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float vnorm(const Vec3& v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }

// ------------------------------ names ---------------------------------------
const char* exerciseName(Exercise e) {
  switch (e) {
    case Exercise::KneeFlexion: return "Knee flexion";
    case Exercise::StraightLegRaise: return "Straight leg raise";
    case Exercise::ElbowFlexion: return "Elbow flexion";
  }
  return "?";
}
const char* modeName(Mode m) { return m == Mode::Guided ? "Guided" : "Assessment"; }

const char* calStateText(CalState s) {
  switch (s) {
    case CalState::Idle: return "Idle";
    case CalState::WaitingStill: return "Hold still";
    case CalState::Collecting: return "Measuring";
    case CalState::Done: return "Done";
    case CalState::FailMountThigh: return "Thigh sensor not flat on side of leg";
    case CalState::FailMountShin: return "Shin sensor not flat on side of leg";
    case CalState::FailTimeout: return "Leg kept moving";
  }
  return "?";
}

const char* phaseName(Phase p) {
  switch (p) {
    case Phase::Rest: return "Rest";
    case Phase::Moving: return "Moving";
    case Phase::Holding: return "Holding";
    case Phase::Held: return "Held";
    case Phase::Returning: return "Returning";
  }
  return "?";
}

const char* faultText(uint16_t f) {
  switch (f) {
    case kFaultIncomplete: return "Incomplete";
    case kFaultHoldShort: return "Hold too short";
    case kFaultOverLimit: return "Over limit";
    case kFaultTooFast: return "Too fast";
    case kFaultKneeBent: return "Knee bent";
    case kFaultRotated: return "Leg rotated";
    default: return "";
  }
}

const char* eventName(EventType t) {
  switch (t) {
    case EventType::None: return "None";
    case EventType::RepStarted: return "RepStarted";
    case EventType::TargetReached: return "TargetReached";
    case EventType::HoldComplete: return "HoldComplete";
    case EventType::HoldBroken: return "HoldBroken";
    case EventType::RepDone: return "RepDone";
    case EventType::OverLimit: return "OverLimit";
    case EventType::TooFast: return "TooFast";
    case EventType::KneeBent: return "KneeBent";
    case EventType::Rotated: return "Rotated";
    case EventType::SetComplete: return "SetComplete";
    case EventType::TestCountdown: return "TestCountdown";
    case EventType::TestGo: return "TestGo";
    case EventType::TestStand: return "TestStand";
    case EventType::PosInBand: return "PosInBand";
    case EventType::PosBandLost: return "PosBandLost";
    case EventType::PosMemorised: return "PosMemorised";
    case EventType::PosReproduce: return "PosReproduce";
    case EventType::PosRecorded: return "PosRecorded";
    case EventType::TestDone: return "TestDone";
    case EventType::TestAborted: return "TestAborted";
  }
  return "?";
}

const char* testName(TestKind k) {
  switch (k) {
    case TestKind::ChairStand: return "30 s chair stand";
    case TestKind::TimedUpGo: return "Timed Up and Go";
    case TestKind::PositionSense: return "Position sense";
  }
  return "?";
}

const char* testAbortText(TestAbort a) {
  switch (a) {
    case TestAbort::None: return "";
    case TestAbort::User: return "Stopped";
    case TestAbort::Timeout: return "Took too long";
    case TestAbort::NoWalk: return "Sat down too soon";
  }
  return "?";
}

// ---------------------------- parameters ------------------------------------
ExerciseParams defaultParams(Exercise e) {
  ExerciseParams p{};
  if (e == Exercise::KneeFlexion) {
    p.targetDeg = 40;
    p.toleranceDeg = 5;
    p.limitDeg = 55;
    p.holdSec = 3;
    p.reps = 10;
    p.maxSpeedDps = 150;
    p.kneeBentDeg = 10;   // unused for knee flexion
    p.rotateDeg = 25;
    p.startDeg = 15;
    p.restDeg = 10;
  } else if (e == Exercise::ElbowFlexion) {
    // Bend the elbow from straight; the rotation check catches the forearm
    // twisting (keep the thumb pointing up).
    p.targetDeg = 70;
    p.toleranceDeg = 5;
    p.limitDeg = 80;
    p.holdSec = 3;
    p.reps = 10;
    p.maxSpeedDps = 150;
    p.kneeBentDeg = 10;   // unused for elbow flexion
    p.rotateDeg = 25;
    p.startDeg = 15;
    p.restDeg = 10;
  } else {
    p.targetDeg = 20;
    p.toleranceDeg = 5;
    p.limitDeg = 30;
    p.holdSec = 3;
    p.reps = 10;
    p.maxSpeedDps = 120;
    p.kneeBentDeg = 10;
    p.rotateDeg = 25;
    p.startDeg = 10;
    p.restDeg = 5;
  }
  return p;
}

static bool fix(float& v, float lo, float hi) {
  float c = clampf(v, lo, hi);
  if (c != v || v != v) {  // v != v catches NaN
    v = (v != v) ? lo : c;
    return true;
  }
  return false;
}

bool sanitizeParams(ExerciseParams& p, Exercise e) {
  (void)e;
  bool ch = false;
  ch |= fix(p.targetDeg, 10, 150);
  ch |= fix(p.startDeg, 5, p.targetDeg - 5);
  ch |= fix(p.restDeg, 2, p.startDeg - 2);
  ch |= fix(p.toleranceDeg, 1, 20);
  // Hold band must stay clearly above the start threshold.
  float maxTol = p.targetDeg - p.startDeg - 2;
  if (maxTol < 1) maxTol = 1;
  ch |= fix(p.toleranceDeg, 1, maxTol);
  ch |= fix(p.limitDeg, p.targetDeg + 3, 170);
  ch |= fix(p.holdSec, 0, 30);
  if (p.reps < 1) { p.reps = 1; ch = true; }
  if (p.reps > 50) { p.reps = 50; ch = true; }
  if (p.maxSpeedDps != 0) ch |= fix(p.maxSpeedDps, 20, 600);
  ch |= fix(p.kneeBentDeg, 3, 45);
  if (p.rotateDeg != 0) ch |= fix(p.rotateDeg, 5, 60);
  return ch;
}

// --------------------------- segment estimator ------------------------------
float SegmentEstimator::accelAngleDeg(const Vec3& a) {
  // The accelerometer measures the (fixed) "up" vector. When the board rotates
  // by +phi about its Z axis, that vector rotates by -phi in board coordinates,
  // so the negative of its angle tracks the segment angle with the same sign
  // as gyro Z.
  return -atan2f(a.y, a.x) * kRad2Deg;
}

float SegmentEstimator::outOfPlaneFromAccelDeg(const Vec3& a) {
  float n = vnorm(a);
  if (n < 1e-3f) return 0;
  return asinf(clampf(a.z / n, -1.f, 1.f)) * kRad2Deg;
}

void SegmentEstimator::reset() {
  init_ = false;
  angle_ = 0;
  outPlane_ = 0;
  trust_ = 0;
}

void SegmentEstimator::seedFromAccel(const Vec3& a) {
  angle_ = accelAngleDeg(a);
  outPlane_ = outOfPlaneFromAccelDeg(a);
  init_ = true;
}

void SegmentEstimator::update(const Vec3& a, const Vec3& g, float dt) {
  dt = clampf(dt, 0.001f, 0.1f);
  const float an = vnorm(a);
  if (!init_) {
    if (an > 0.3f) seedFromAccel(a);
    return;
  }
  // 1) predict with the gyro (bias removed)
  const float pred = angle_ + (g.z - biasZ_) * dt;

  // 2) how much can we trust gravity right now?
  //    - |a| far from 1 g  => the leg is accelerating (fast rep), don't trust
  //    - little of gravity in the X-Y plane => atan2 is ill-conditioned
  const float inPlane = sqrtf(a.x * a.x + a.y * a.y);
  const float tMag = clampf(1.0f - fabsf(an - 1.0f) / 0.25f, 0.f, 1.f);
  const float tPlane = clampf((inPlane - 0.35f) / 0.30f, 0.f, 1.f);
  trust_ = tMag * tPlane;

  // 3) complementary correction toward the gravity angle (wrap-safe)
  const float k = (dt / (tauSec + dt)) * trust_;
  const float innov = wrapDeg(accelAngleDeg(a) - pred);
  angle_ = pred + k * innov;

  // 4) out-of-plane tilt (leg rotation check), low-passed
  if (an > 0.3f) outPlane_ += 0.05f * (outOfPlaneFromAccelDeg(a) - outPlane_);
}

// ------------------------------- joint model --------------------------------
void JointModel::reset() {
  thigh_.reset();
  shin_.reset();
  s_ = JointSample();
  havePrev_ = false;
  kneeSign_ = thighSign_ = 0;
  cal_ = CalState::Idle;
  calRes_ = CalResult();
}

void JointModel::update(const Vec3& aT, const Vec3& gT, const Vec3& aS, const Vec3& gS, float dt) {
  lastAT_ = aT;
  lastAS_ = aS;
  lastGT_ = gT;
  lastGS_ = gS;
  thigh_.update(aT, gT, dt);
  shin_.update(aS, gS, dt);

  if (!calRes_.valid) {
    s_ = JointSample();
    return;
  }

  const float dThigh = wrapDeg(thigh_.angleDeg() - calRes_.thighRef);
  const float dShin = wrapDeg(shin_.angleDeg() - calRes_.shinRef);
  const float kneeRaw = wrapDeg(dShin - dThigh);

  // Sign conventions are fixed automatically by the first clear movement, so
  // it works on the left or right leg and with either board orientation.
  if (kneeSign_ == 0 && fabsf(kneeRaw) > signDetectDeg) kneeSign_ = kneeRaw > 0 ? 1 : -1;
  if (thighSign_ == 0 && fabsf(dThigh) > signDetectDeg) thighSign_ = dThigh > 0 ? 1 : -1;

  s_.kneeDeg = kneeSign_ ? kneeSign_ * kneeRaw : fabsf(kneeRaw);
  s_.thighElevDeg = thighSign_ ? thighSign_ * dThigh : fabsf(dThigh);
  s_.shinDeg = dShin;

  const float rotT = fabsf(thigh_.outOfPlaneDeg() - calRes_.thighOut0);
  const float rotS = fabsf(shin_.outOfPlaneDeg() - calRes_.shinOut0);
  s_.rotationDeg = rotT > rotS ? rotT : rotS;

  const float sdt = clampf(dt, 0.001f, 0.1f);
  if (havePrev_) {
    const float dk = (s_.kneeDeg - prevKnee_) / sdt;
    const float dth = (s_.thighElevDeg - prevThigh_) / sdt;
    s_.kneeRateDps += 0.3f * (dk - s_.kneeRateDps);
    s_.thighRateDps += 0.3f * (dth - s_.thighRateDps);
  }
  prevKnee_ = s_.kneeDeg;
  prevThigh_ = s_.thighElevDeg;
  havePrev_ = true;
}

void JointModel::beginCalibration(uint32_t nowMs) {
  cal_ = CalState::WaitingStill;
  calStartMs_ = nowMs;
  calCount_ = 0;
  sumGzT_ = sumGzS_ = 0;
  sumAT_ = Vec3();
  sumAS_ = Vec3();
  calRes_ = CalResult();  // invalidates previous calibration
  kneeSign_ = thighSign_ = 0;
  havePrev_ = false;
  s_ = JointSample();
}

float JointModel::calProgress() const {
  if (cal_ == CalState::Done) return 1.f;
  if (cal_ != CalState::Collecting) return 0.f;
  return clampf((float)calCount_ / (float)calSamples, 0.f, 1.f);
}

static float gyroMag(const Vec3& g) { return vnorm(g); }

void JointModel::tickCalibration(uint32_t nowMs) {
  if (cal_ != CalState::WaitingStill && cal_ != CalState::Collecting) return;

  const bool still = gyroMag(lastGT_) < calMaxGyroDps && gyroMag(lastGS_) < calMaxGyroDps &&
                     fabsf(vnorm(lastAT_) - 1.f) < calMaxAccelDevG &&
                     fabsf(vnorm(lastAS_) - 1.f) < calMaxAccelDevG;

  if (!still) {
    calCount_ = 0;
    sumGzT_ = sumGzS_ = 0;
    sumAT_ = Vec3();
    sumAS_ = Vec3();
    cal_ = CalState::WaitingStill;
    if (nowMs - calStartMs_ > calTimeoutMs) cal_ = CalState::FailTimeout;
    return;
  }

  cal_ = CalState::Collecting;
  sumGzT_ += lastGT_.z;
  sumGzS_ += lastGS_.z;
  sumAT_.x += lastAT_.x; sumAT_.y += lastAT_.y; sumAT_.z += lastAT_.z;
  sumAS_.x += lastAS_.x; sumAS_.y += lastAS_.y; sumAS_.z += lastAS_.z;
  calCount_++;
  if (calCount_ < calSamples) return;

  const float n = (float)calCount_;
  Vec3 mT{sumAT_.x / n, sumAT_.y / n, sumAT_.z / n};
  Vec3 mS{sumAS_.x / n, sumAS_.y / n, sumAS_.z / n};

  const float outT = SegmentEstimator::outOfPlaneFromAccelDeg(mT);
  const float outS = SegmentEstimator::outOfPlaneFromAccelDeg(mS);
  if (fabsf(outT) > calMountMaxOutDeg) { cal_ = CalState::FailMountThigh; return; }
  if (fabsf(outS) > calMountMaxOutDeg) { cal_ = CalState::FailMountShin; return; }

  thigh_.setGyroBiasZ((float)(sumGzT_ / n));
  shin_.setGyroBiasZ((float)(sumGzS_ / n));
  thigh_.seedFromAccel(mT);
  shin_.seedFromAccel(mS);

  calRes_.valid = true;
  calRes_.thighRef = thigh_.angleDeg();
  calRes_.shinRef = shin_.angleDeg();
  calRes_.thighOut0 = outT;
  calRes_.shinOut0 = outS;
  calRes_.biasThigh = thigh_.gyroBiasZ();
  calRes_.biasShin = shin_.gyroBiasZ();
  kneeSign_ = thighSign_ = 0;
  havePrev_ = false;
  cal_ = CalState::Done;
}

// ------------------------------ rep counter ---------------------------------
void RepCounter::configure(Exercise ex, const ExerciseParams& p) {
  ex_ = ex;
  p_ = p;
  sanitizeParams(p_, ex_);
}

void RepCounter::start(uint32_t nowMs) {
  st_ = SessionStats();
  st_.startMs = nowMs;
  st_.endMs = nowMs;
  phase_ = Phase::Rest;
  peak_ = 0;
  faults_ = 0;
  reached_ = holdOk_ = false;
  holdStartMs_ = holdEndMs_ = 0;
  kneeBentSinceMs_ = rotatedSinceMs_ = 0;
  fastCount_ = 0;
  setDone_ = false;
}

void RepCounter::finish(uint32_t nowMs) { st_.endMs = nowMs; }

float RepCounter::holdElapsedSec(uint32_t nowMs) const {
  if (phase_ == Phase::Holding) return (nowMs - holdStartMs_) / 1000.f;
  if (phase_ == Phase::Held || (phase_ == Phase::Returning && reached_))
    return (holdEndMs_ - holdStartMs_) / 1000.f;
  return 0.f;
}

void RepCounter::emit(Event* out, int maxOut, int& n, EventType t, float v, float v2) {
  if (!out || n >= maxOut) return;
  Event& e = out[n++];
  e = Event();
  e.type = t;
  e.value = v;
  e.value2 = v2;
  e.faults = faults_;
  e.validReps = st_.validReps;
  e.attempts = st_.attempts;
}

int RepCounter::update(float m, float knee, float speed, float rot, uint32_t now, Event* out,
                       int maxOut) {
  int n = 0;
  st_.endMs = now;

  // ---------------- fault monitors (only while a rep is in progress) --------
  if (phase_ != Phase::Rest) {
    if (m > peak_) peak_ = m;

    if (m > p_.limitDeg && !(faults_ & kFaultOverLimit)) {
      faults_ |= kFaultOverLimit;
      emit(out, maxOut, n, EventType::OverLimit, m);
    }

    if (p_.maxSpeedDps > 0) {
      if (speed > p_.maxSpeedDps) {
        if (++fastCount_ >= 3 && !(faults_ & kFaultTooFast)) {
          faults_ |= kFaultTooFast;
          emit(out, maxOut, n, EventType::TooFast, speed);
        }
      } else {
        fastCount_ = 0;
      }
    }

    if (ex_ == Exercise::StraightLegRaise) {
      if (knee > p_.kneeBentDeg) {
        if (kneeBentSinceMs_ == 0) kneeBentSinceMs_ = now ? now : 1;
        if (now - kneeBentSinceMs_ >= 300 && !(faults_ & kFaultKneeBent)) {
          faults_ |= kFaultKneeBent;
          emit(out, maxOut, n, EventType::KneeBent, knee);
        }
      } else {
        kneeBentSinceMs_ = 0;
      }
    }

    if (p_.rotateDeg > 0) {
      if (rot > p_.rotateDeg) {
        if (rotatedSinceMs_ == 0) rotatedSinceMs_ = now ? now : 1;
        if (now - rotatedSinceMs_ >= 500 && !(faults_ & kFaultRotated)) {
          faults_ |= kFaultRotated;
          emit(out, maxOut, n, EventType::Rotated, rot);
        }
      } else {
        rotatedSinceMs_ = 0;
      }
    }
  }

  // ---------------- phase machine -------------------------------------------
  const float bandLo = p_.targetDeg - p_.toleranceDeg;
  switch (phase_) {
    case Phase::Rest:
      if (m > p_.startDeg) {
        phase_ = Phase::Moving;
        peak_ = m;
        faults_ = 0;
        reached_ = holdOk_ = false;
        fastCount_ = 0;
        kneeBentSinceMs_ = rotatedSinceMs_ = 0;
        holdStartMs_ = holdEndMs_ = now;
        emit(out, maxOut, n, EventType::RepStarted, m);
      }
      break;

    case Phase::Moving:
      if (m >= p_.targetDeg) {
        reached_ = true;
        holdStartMs_ = holdEndMs_ = now;
        phase_ = Phase::Holding;
        emit(out, maxOut, n, EventType::TargetReached, m);
        if (p_.holdSec <= 0) {
          holdOk_ = true;
          phase_ = Phase::Held;
          emit(out, maxOut, n, EventType::HoldComplete, 0);
        }
      } else if (m < p_.restDeg) {
        finishRep(now, out, maxOut, n);
      }
      break;

    case Phase::Holding:
      if (m < bandLo) {
        holdEndMs_ = now;
        phase_ = Phase::Returning;
        emit(out, maxOut, n, EventType::HoldBroken, (holdEndMs_ - holdStartMs_) / 1000.f);
      } else {
        holdEndMs_ = now;
        if (now - holdStartMs_ >= (uint32_t)(p_.holdSec * 1000.f)) {
          holdOk_ = true;
          phase_ = Phase::Held;
          emit(out, maxOut, n, EventType::HoldComplete, (holdEndMs_ - holdStartMs_) / 1000.f);
        }
      }
      break;

    case Phase::Held:
      if (m < bandLo) phase_ = Phase::Returning;
      else holdEndMs_ = now;
      break;

    case Phase::Returning:
      // Forgiving: if the hold was broken but the patient climbs back to the
      // target before returning to rest, restart the hold timer.
      if (!holdOk_ && m >= p_.targetDeg) {
        reached_ = true;
        holdStartMs_ = holdEndMs_ = now;
        phase_ = Phase::Holding;
        emit(out, maxOut, n, EventType::TargetReached, m);
      }
      break;
  }

  // Leaving the hold straight to rest in one step is handled here.
  if (phase_ == Phase::Returning && m < p_.restDeg) finishRep(now, out, maxOut, n);

  return n;
}

void RepCounter::finishRep(uint32_t now, Event* out, int maxOut, int& n) {
  (void)now;
  // A tiny wobble that never got anywhere is not an attempt.
  if (!reached_ && peak_ < p_.startDeg + 5.f) {
    phase_ = Phase::Rest;
    peak_ = 0;
    faults_ = 0;
    return;
  }

  uint16_t f = faults_;
  if (!reached_) f |= kFaultIncomplete;
  else if (!holdOk_) f |= kFaultHoldShort;
  const float holdSec = reached_ ? (holdEndMs_ - holdStartMs_) / 1000.f : 0.f;
  const bool valid = (f == 0);

  st_.attempts++;
  if (valid) st_.validReps++;
  if (f & kFaultIncomplete) st_.incomplete++;
  if (f & kFaultHoldShort) st_.holdShort++;
  if (f & kFaultOverLimit) st_.overLimit++;
  if (f & kFaultTooFast) st_.tooFast++;
  if (f & kFaultKneeBent) st_.kneeBent++;
  if (f & kFaultRotated) st_.rotated++;
  if (reached_) {
    st_.reachedTarget++;
    st_.sumHold += holdSec;
  }
  st_.sumPeak += peak_;
  if (peak_ > st_.maxAngle) st_.maxAngle = peak_;

  faults_ = f;  // so the RepDone event carries the full fault set
  if (out && n < maxOut) {
    Event& e = out[n++];
    e = Event();
    e.type = EventType::RepDone;
    e.valid = valid;
    e.faults = f;
    e.value = peak_;
    e.value2 = holdSec;
    e.validReps = st_.validReps;
    e.attempts = st_.attempts;
  }
  if (!setDone_ && st_.validReps >= p_.reps) {
    setDone_ = true;
    emit(out, maxOut, n, EventType::SetComplete, (float)st_.validReps);
  }

  phase_ = Phase::Rest;
  peak_ = 0;
  faults_ = 0;
  reached_ = holdOk_ = false;
}

// ----------------------------- clinical tests --------------------------------
void ClinicalTest::reset() {
  phase_ = TestPhase::Idle;
  abort_ = TestAbort::None;
  res_ = TestResult();
  cd_ = 0;
  calHint_ = false;
}

void ClinicalTest::start(TestKind k, float targetDeg, uint32_t now) {
  reset();
  kind_ = k;
  res_.kind = k;
  res_.targetDeg = k == TestKind::PositionSense ? targetDeg : 0;
  startMs_ = phaseMs_ = now;
  goMs_ = seatedSince_ = 0;
  move_ = kSeated;
  baseSum_ = 0;
  baseN_ = 0;
  baseline_ = 90;
  riseSum_ = 0;
  riseN_ = 0;
  upMs_ = uprightMs_ = seatMs_ = endMs_ = 0;
  armed_ = inBand_ = moved_ = stillInit_ = false;
  side_ = 0;
  ref_ = retPose_ = 0;
  phase_ = k == TestKind::PositionSense ? TestPhase::Present : TestPhase::NeedSeated;
}

void ClinicalTest::cancel() {
  if (!active()) return;
  phase_ = TestPhase::Aborted;
  abort_ = TestAbort::User;
}

bool ClinicalTest::active() const {
  return phase_ != TestPhase::Idle && phase_ != TestPhase::Done && phase_ != TestPhase::Aborted;
}

float ClinicalTest::elapsedSec(uint32_t now) const {
  if (kind_ == TestKind::PositionSense) return active() ? (now - startMs_) / 1000.f : 0.f;
  if (phase_ == TestPhase::Running) return (now - goMs_) / 1000.f;
  if (phase_ == TestPhase::Done)
    return kind_ == TestKind::ChairStand ? chairWindowMs / 1000.f : res_.score;
  return 0.f;
}

uint8_t ClinicalTest::tugStage() const {
  if (move_ == kSeated || move_ == kRising) return 0;
  if (move_ == kSitting || lastH_ > 40) return 2;
  return 1;
}

float ClinicalTest::holdSec(uint32_t now) const {
  if (phase_ != TestPhase::Present || !inBand_ || !stillInit_) return 0.f;
  return (now - stillSince_) / 1000.f;
}

void ClinicalTest::emit(Event* out, int maxOut, int& n, EventType t, float v, float v2) {
  if (!out || n >= maxOut) return;
  Event& e = out[n++];
  e = Event();
  e.type = t;
  e.value = v;
  e.value2 = v2;
  e.valid = t == EventType::TestDone;
  e.validReps = kind_ == TestKind::PositionSense ? res_.trials : res_.stands;
}

void ClinicalTest::stop(TestAbort a, Event* out, int maxOut, int& n) {
  phase_ = TestPhase::Aborted;
  abort_ = a;
  emit(out, maxOut, n, EventType::TestAborted, (float)a);
}

void ClinicalTest::done(Event* out, int maxOut, int& n) {
  phase_ = TestPhase::Done;
  emit(out, maxOut, n, EventType::TestDone, res_.score);
}

// Stillness = the angle stays within +-posStillDeg of where it stopped.
void ClinicalTest::trackStill(float k, uint32_t now) {
  if (!stillInit_ || fabsf(k - stillAnchor_) > posStillDeg) {
    stillAnchor_ = k;
    stillSince_ = now;
    stillSum_ = 0;
    stillN_ = 0;
    stillInit_ = true;
  }
  stillSum_ += k;
  stillN_++;
}

int ClinicalTest::update(const JointSample& s, uint32_t now, Event* out, int maxOut) {
  int n = 0;
  if (!active()) return 0;
  // Thigh angle from the standing calibration pose; the sign does not matter.
  const float h = fabsf(s.thighElevDeg);
  const float k = s.kneeDeg;
  lastH_ = h;
  const bool seated = h > sitDeg && k > kneeSitDeg;
  const bool standing = h < standDeg && k < kneeStandDeg;

  switch (phase_) {
    case TestPhase::NeedSeated:
      // A bent knee with an upright thigh while "sitting" means the calibration
      // was not done standing.
      calHint_ = k > 60 && h < 30;
      if (!seated) {
        seatedSince_ = 0;
        break;
      }
      if (!seatedSince_) seatedSince_ = now ? now : 1;
      if (now - seatedSince_ >= seatedConfirmMs) {
        phase_ = TestPhase::Countdown;
        phaseMs_ = now;
        baseSum_ = 0;
        baseN_ = 0;
        cd_ = 3;
        emit(out, maxOut, n, EventType::TestCountdown, 3);
      }
      break;

    case TestPhase::Countdown: {
      if (!seated) {  // stood up before "Go": wait for the seat again
        phase_ = TestPhase::NeedSeated;
        seatedSince_ = 0;
        cd_ = 0;
        break;
      }
      baseSum_ += h;
      baseN_++;
      const uint32_t el = now - phaseMs_;
      if (el >= 3000) {
        baseline_ = baseN_ ? (float)(baseSum_ / baseN_) : 90.f;
        phase_ = TestPhase::Running;
        goMs_ = now;
        cd_ = 0;
        move_ = kSeated;
        emit(out, maxOut, n, EventType::TestGo);
      } else {
        const uint8_t left = (uint8_t)(3 - el / 1000);
        if (left < cd_) {
          cd_ = left;
          emit(out, maxOut, n, EventType::TestCountdown, left);
        }
      }
      break;
    }

    case TestPhase::Running:
      if (kind_ == TestKind::ChairStand) runChair(h, seated, standing, now, out, maxOut, n);
      else runTug(h, fabsf(s.thighRateDps), seated, standing, now, out, maxOut, n);
      break;

    case TestPhase::Present:
    case TestPhase::Return:
    case TestPhase::Reproduce:
      runPosition(k, now, out, maxOut, n);
      break;

    default:
      break;
  }
  return n;
}

void ClinicalTest::runChair(float h, bool seated, bool standing, uint32_t now, Event* out,
                            int maxOut, int& n) {
  if (now - goMs_ >= chairWindowMs) {
    // STEADI: over halfway to standing when the 30 s end counts as a stand.
    res_.halfStand = move_ == kRising && h < baseline_ * 0.5f;
    res_.score = res_.stands + (res_.halfStand ? 1 : 0);
    res_.riseSec = riseN_ ? riseSum_ / riseN_ : 0.f;
    done(out, maxOut, n);
    return;
  }
  switch (move_) {
    case kSeated:
      if (h < baseline_ - 10) {  // leaving the seat
        move_ = kRising;
        mark_ = now;
      }
      break;
    case kRising:
      if (standing) {
        res_.stands++;
        res_.score = res_.stands;
        riseSum_ += (now - mark_) / 1000.f;
        riseN_++;
        move_ = kUp;
        emit(out, maxOut, n, EventType::TestStand, res_.stands);
      } else if (h > baseline_ - 5) {
        move_ = kSeated;  // back on the seat without reaching a full stand
      }
      break;
    case kUp:
      if (h > standDeg + 10) move_ = kLowering;
      break;
    default:  // kLowering
      if (seated) {
        move_ = kSeated;
      } else if (standing) {
        move_ = kUp;
      }
      break;
  }
}

void ClinicalTest::runTug(float h, float thighRate, bool seated, bool standing, uint32_t now,
                          Event* out, int maxOut, int& n) {
  if (now - goMs_ > tugTimeoutMs) {
    stop(TestAbort::Timeout, out, maxOut, n);
    return;
  }
  switch (move_) {
    case kSeated:
      if (h < sitDeg - 5) move_ = kRising;
      break;
    case kRising:
      if (standing) {
        upMs_ = uprightMs_ = now;
        res_.riseSec = (now - goMs_) / 1000.f;
        move_ = kUp;
        emit(out, maxOut, n, EventType::TestStand, res_.riseSec);
      } else if (seated) {
        move_ = kSeated;
      }
      break;
    case kUp:
      if (h < standDeg + 5) uprightMs_ = now;  // last moment upright = start of the sit
      if (seated) {
        move_ = kSitting;
        seatMs_ = now;
        endMs_ = 0;
      }
      break;
    default: {  // kSitting: stop the clock when the thigh comes to rest on the seat
      if (h < sitDeg - 5) {  // only a deep bend, not a sit
        move_ = kUp;
        break;
      }
      if (!endMs_ && thighRate < settleDps && h >= baseline_ - 15) endMs_ = now;
      const bool settled = endMs_ && now - endMs_ >= 500;
      if (!settled && now - seatMs_ < 3000) break;
      if (!endMs_) endMs_ = seatMs_;  // never settled (fidgeting): use the seat time
      res_.score = (endMs_ - goMs_) / 1000.f;
      res_.walkSec = (uprightMs_ - upMs_) / 1000.f;
      res_.sitSec = (endMs_ - uprightMs_) / 1000.f;
      if (res_.walkSec < tugMinWalkSec) stop(TestAbort::NoWalk, out, maxOut, n);
      else done(out, maxOut, n);
      break;
    }
  }
}

void ClinicalTest::runPosition(float k, uint32_t now, Event* out, int maxOut, int& n) {
  if (now - phaseMs_ > posStepTimeoutMs) {
    stop(TestAbort::Timeout, out, maxOut, n);
    return;
  }
  const float target = res_.targetDeg;
  switch (phase_) {
    case TestPhase::Present: {
      // Each presentation starts clearly away from the target, so the angle
      // is found by moving to it, not by already being there.
      if (!armed_) {
        if (fabsf(k - target) >= posAwayDeg) {
          armed_ = true;
          side_ = k > target ? 1 : -1;
        }
        break;
      }
      const float d = fabsf(k - target);
      if (!inBand_) {
        if (d <= posBandDeg) {
          inBand_ = true;
          stillInit_ = false;
          emit(out, maxOut, n, EventType::PosInBand, k);
        }
      } else if (d > posBandDeg + 2) {
        inBand_ = false;
        emit(out, maxOut, n, EventType::PosBandLost, k);
      }
      if (!inBand_) break;
      trackStill(k, now);
      if (now - stillSince_ >= posHoldMs) {
        ref_ = stillMean();
        phase_ = TestPhase::Return;
        phaseMs_ = now;
        stillInit_ = false;
        emit(out, maxOut, n, EventType::PosMemorised, ref_);
      }
      break;
    }
    case TestPhase::Return: {
      trackStill(k, now);
      const float off = k - ref_;
      if (fabsf(off) >= posAwayDeg && off * side_ > 0 && now - stillSince_ >= posStillMs) {
        retPose_ = stillMean();
        phase_ = TestPhase::Reproduce;
        phaseMs_ = now;
        moved_ = false;
        stillInit_ = false;
        emit(out, maxOut, n, EventType::PosReproduce, retPose_);
      }
      break;
    }
    default: {  // Reproduce
      trackStill(k, now);
      if (!moved_ && fabsf(k - retPose_) >= posMoveDeg) moved_ = true;
      if (!moved_ || now - stillSince_ < posStillMs || fabsf(stillMean() - retPose_) < posMoveDeg)
        break;
      const float rep = stillMean();
      const float err = rep - ref_;
      res_.trialErr[res_.trials++] = err;
      emit(out, maxOut, n, EventType::PosRecorded, err, rep);
      if (res_.trials >= kPosTrials) {
        float sa = 0, ss = 0;
        for (int i = 0; i < kPosTrials; i++) {
          sa += fabsf(res_.trialErr[i]);
          ss += res_.trialErr[i];
        }
        res_.score = sa / kPosTrials;
        res_.constErr = ss / kPosTrials;
        done(out, maxOut, n);
      } else {
        phase_ = TestPhase::Present;
        phaseMs_ = now;
        armed_ = inBand_ = false;
        stillInit_ = false;
      }
      break;
    }
  }
}

}  // namespace rs
