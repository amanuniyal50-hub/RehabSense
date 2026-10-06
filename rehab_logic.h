// rehab_logic.h
// ---------------------------------------------------------------------------
// RehabSense core logic. Pure C++ (no Arduino headers), so the exact same code
// runs on the ESP32 and in the host unit tests (host_tests/).
//
//  SegmentEstimator  one IMU strapped flat on the SIDE of a leg segment.
//                    The board's Z axis (normal) is then ~ the knee/hip
//                    flexion axis, so the segment's sagittal angle is a
//                    rotation about Z: gyro Z + gravity direction in X-Y.
//                    Complementary filter => no yaw needed => no yaw drift.
//  JointModel        two segments -> knee angle, thigh elevation, rates,
//                    out-of-plane (leg rotation) check, stillness calibration.
//  RepCounter        exercise state machine with hysteresis bands,
//                    hold timing and fault detection.
//  ClinicalTest      standardised visit tests on the same sensors: 30 s
//                    chair stand, Timed Up and Go, knee position sense.
// ---------------------------------------------------------------------------
#pragma once
#include <stdint.h>

namespace rs {

// ------------------------------- math --------------------------------------
constexpr float kRad2Deg = 57.2957795f;
constexpr float kDeg2Rad = 0.01745329252f;

struct Vec3 {
  float x = 0, y = 0, z = 0;
};

float wrapDeg(float a);                 // -> (-180, 180]
float clampf(float v, float lo, float hi);
float vnorm(const Vec3& v);

// ------------------------------ enums ---------------------------------------
// ElbowFlexion uses the same two sensors on the upper arm ("thigh" slot, 0x68)
// and the forearm ("shin" slot, 0x69): elbow angle = forearm - upper arm, the
// same maths as the knee, so it shares the knee's rep engine and checks.
enum class Exercise : uint8_t { KneeFlexion = 0, StraightLegRaise = 1, ElbowFlexion = 2 };
constexpr int kExerciseCount = 3;
const char* exerciseName(Exercise e);   // "Knee flexion" / "Straight leg raise" / "Elbow flexion"
// The joint angle drives the rep for knee and elbow flexion; SLR uses thigh elevation.
inline bool usesJointAngle(Exercise e) { return e != Exercise::StraightLegRaise; }

enum class Mode : uint8_t { Guided = 0, Assessment = 1 };
const char* modeName(Mode m);

// Physio-prescribed parameters for one exercise. All angles in degrees.
struct ExerciseParams {
  float targetDeg;     // rep must reach this
  float toleranceDeg;  // hold continues while angle >= target - tolerance
  float limitDeg;      // safety limit set by physio (> target)
  float holdSec;       // required hold at target
  uint16_t reps;       // valid reps per set
  float maxSpeedDps;   // 0 = disabled; faster movement = "too fast"
  float kneeBentDeg;   // SLR only: knee must stay below this
  float rotateDeg;     // leg rotation (out of plane) allowed
  float startDeg;      // angle above which a rep has started
  float restDeg;       // angle below which the leg is back at rest
};
ExerciseParams defaultParams(Exercise e);
// Clamp to sane ranges and fix inconsistent combos (limit <= target etc).
// Returns true if anything had to be changed.
bool sanitizeParams(ExerciseParams& p, Exercise e);

// --------------------------- segment estimator ------------------------------
class SegmentEstimator {
 public:
  void reset();
  // acc in g, gyro in deg/s (raw; bias is removed internally), dt in seconds.
  void update(const Vec3& accG, const Vec3& gyroDps, float dt);
  // Seed angle directly from gravity (used right after calibration).
  void seedFromAccel(const Vec3& accG);
  void setGyroBiasZ(float b) { biasZ_ = b; }
  float gyroBiasZ() const { return biasZ_; }

  float angleDeg() const { return angle_; }          // continuous (unwrapped)
  float outOfPlaneDeg() const { return outPlane_; }  // low-passed tilt of Z axis vs horizontal
  float accelTrust() const { return trust_; }        // 0..1, how much accel was trusted last step
  bool initialised() const { return init_; }

  static float accelAngleDeg(const Vec3& a);         // sagittal angle from gravity only
  static float outOfPlaneFromAccelDeg(const Vec3& a);

  float tauSec = 0.5f;  // complementary filter time constant

 private:
  bool init_ = false;
  float angle_ = 0;
  float biasZ_ = 0;
  float outPlane_ = 0;
  float trust_ = 0;
};

// ------------------------------ calibration --------------------------------
enum class CalState : uint8_t {
  Idle = 0,
  WaitingStill,   // collecting, but movement keeps resetting it
  Collecting,     // still, filling the window
  Done,
  FailMountThigh, // thigh board not flat on the side of the leg
  FailMountShin,
  FailTimeout     // never still long enough
};
const char* calStateText(CalState s);

struct CalResult {
  bool valid = false;
  float thighRef = 0, shinRef = 0;          // estimator angles at 0 deg pose
  float thighOut0 = 0, shinOut0 = 0;        // out-of-plane at 0 deg pose
  float biasThigh = 0, biasShin = 0;        // gyro Z bias (deg/s)
};

// ------------------------------- joint model --------------------------------
struct JointSample {
  float kneeDeg = 0;        // flexion, +ve = bent (sign auto-detected)
  float thighElevDeg = 0;   // thigh lift from calibration pose, +ve = up
  float shinDeg = 0;        // shin change from calibration pose
  float kneeRateDps = 0;    // filtered d(knee)/dt
  float thighRateDps = 0;   // filtered d(thighElev)/dt
  float rotationDeg = 0;    // worst out-of-plane deviation of either sensor
};

class JointModel {
 public:
  void reset();
  // Feed one synchronized sample from both IMUs.
  void update(const Vec3& accT, const Vec3& gyrT, const Vec3& accS, const Vec3& gyrS, float dt);

  // --- calibration: leg straight & still. Call begin, keep feeding update().
  void beginCalibration(uint32_t nowMs);
  CalState calState() const { return cal_; }
  float calProgress() const;  // 0..1
  const CalResult& calibration() const { return calRes_; }
  bool calibrated() const { return calRes_.valid; }
  void tickCalibration(uint32_t nowMs);  // call once per sample after update()

  const JointSample& sample() const { return s_; }
  const SegmentEstimator& thigh() const { return thigh_; }
  const SegmentEstimator& shin() const { return shin_; }

  // tunables
  // calMaxGyroDps/calMaxAccelDevG loosened from 4.0/0.08 - the original values
  // assumed near-zero gyro bias at rest, but cheap/clone MPU6050 boards can
  // have several deg/s of raw (uncorrected) gyro offset even sitting still,
  // which kept this stillness gate from ever passing. This gate only decides
  // when to START measuring the bias (setGyroBiasZ below), so loosening it
  // doesn't hurt the actual calibration accuracy once it's collected.
  int calSamples = 150;             // 1.5 s at 100 Hz
  float calMaxGyroDps = 10.0f;       // stillness threshold
  float calMaxAccelDevG = 0.15f;    // |a| must be within 1 +- this
  float calMountMaxOutDeg = 40.0f;  // board normal must be roughly horizontal
  uint32_t calTimeoutMs = 20000;
  float signDetectDeg = 15.0f;      // magnitude that fixes the sign convention

 private:
  SegmentEstimator thigh_, shin_;
  JointSample s_;
  float prevKnee_ = 0, prevThigh_ = 0;
  bool havePrev_ = false;
  int8_t kneeSign_ = 0, thighSign_ = 0;

  // calibration accumulators
  CalState cal_ = CalState::Idle;
  CalResult calRes_;
  uint32_t calStartMs_ = 0;
  int calCount_ = 0;
  double sumGzT_ = 0, sumGzS_ = 0;
  Vec3 sumAT_, sumAS_;
  Vec3 lastAT_, lastAS_, lastGT_, lastGS_;
  bool lastStill_ = false;
};

// ------------------------------ rep counter ---------------------------------
enum class Phase : uint8_t { Rest = 0, Moving, Holding, Held, Returning };
const char* phaseName(Phase p);

enum Fault : uint16_t {
  kFaultNone = 0,
  kFaultIncomplete = 1 << 0,  // target not reached
  kFaultHoldShort = 1 << 1,   // reached target but did not hold long enough
  kFaultOverLimit = 1 << 2,   // exceeded physio limit
  kFaultTooFast = 1 << 3,
  kFaultKneeBent = 1 << 4,    // SLR: knee bent
  kFaultRotated = 1 << 5,     // leg rotated out of plane
};
const char* faultText(uint16_t faultBit);   // short text for one bit

enum class EventType : uint8_t {
  None = 0,
  RepStarted,
  TargetReached,
  HoldComplete,
  HoldBroken,
  RepDone,       // value = peak, value2 = hold seconds, faults = fault bits, valid flag
  OverLimit,     // value = angle
  TooFast,       // value = speed
  KneeBent,      // value = knee angle
  Rotated,       // value = rotation
  SetComplete,   // valid reps reached target count
  // clinical tests (ClinicalTest)
  TestCountdown, // value = seconds left (3, 2, 1)
  TestGo,        // timing starts
  TestStand,     // chair stand: full stand counted (value = stands). TUG: up (value = s)
  PosInBand,     // position sense: inside the target band, hold still
  PosBandLost,   // left the band before the hold was complete
  PosMemorised,  // value = memorised angle
  PosReproduce,  // back at the start: now find the angle again
  PosRecorded,   // value = error (deg, + = more bent), value2 = reproduced angle
  TestDone,      // value = score
  TestAborted,   // value = TestAbort
};
const char* eventName(EventType t);

struct Event {
  EventType type = EventType::None;
  bool valid = false;
  uint16_t faults = 0;
  float value = 0;
  float value2 = 0;
  uint16_t validReps = 0;
  uint16_t attempts = 0;
};

struct SessionStats {
  uint16_t attempts = 0, validReps = 0;
  uint16_t incomplete = 0, holdShort = 0, overLimit = 0, tooFast = 0, kneeBent = 0, rotated = 0;
  uint16_t reachedTarget = 0;   // reps that reached target (for avg hold)
  float maxAngle = 0;
  float sumPeak = 0, sumHold = 0;
  uint32_t startMs = 0, endMs = 0;
  float avgPeak() const { return attempts ? sumPeak / attempts : 0.f; }
  float avgHold() const { return reachedTarget ? sumHold / reachedTarget : 0.f; }
  float durationSec() const { return endMs > startMs ? (endMs - startMs) / 1000.f : 0.f; }
  uint16_t warnings() const { return overLimit + tooFast + kneeBent + rotated; }
};

class RepCounter {
 public:
  void configure(Exercise ex, const ExerciseParams& p);
  void start(uint32_t nowMs);   // clears stats
  // metric: main angle (knee flexion or thigh elevation)
  // kneeDeg: used as the quality check for SLR
  // speedDps: |d metric / dt|
  // Writes up to maxOut events, returns count.
  int update(float metric, float kneeDeg, float speedDps, float rotationDeg, uint32_t nowMs,
             Event* out, int maxOut);
  void finish(uint32_t nowMs);  // closes stats (endMs)

  Phase phase() const { return phase_; }
  const SessionStats& stats() const { return st_; }
  const ExerciseParams& params() const { return p_; }
  Exercise exercise() const { return ex_; }
  float currentPeak() const { return peak_; }
  float holdElapsedSec(uint32_t nowMs) const;
  uint16_t currentFaults() const { return faults_; }
  bool setComplete() const { return setDone_; }

 private:
  void finishRep(uint32_t nowMs, Event* out, int maxOut, int& n);
  void emit(Event* out, int maxOut, int& n, EventType t, float v = 0, float v2 = 0);

  Exercise ex_ = Exercise::KneeFlexion;
  ExerciseParams p_ = {};
  Phase phase_ = Phase::Rest;
  SessionStats st_;
  float peak_ = 0;
  uint16_t faults_ = 0;
  bool reached_ = false;
  bool holdOk_ = false;
  uint32_t holdStartMs_ = 0, holdEndMs_ = 0;
  uint32_t kneeBentSinceMs_ = 0, rotatedSinceMs_ = 0;
  int fastCount_ = 0;
  bool setDone_ = false;
};

// ----------------------------- clinical tests --------------------------------
// Standardised tests a physio runs at a visit:
//  ChairStand     CDC STEADI 30-second chair stand: full stands in 30 s. If the
//                 patient is over halfway up when time runs out, it counts.
//  TimedUpGo      CDC STEADI Timed Up and Go: on "Go" stand up, walk 3 m, turn,
//                 walk back, sit down. The time is also split into rise,
//                 walk + turn and sit (an "instrumented" TUG).
//  PositionSense  knee joint position sense: find a target angle with haptic
//                 guidance, hold it to memorise it, go back, then reproduce it
//                 with eyes closed. Error = reproduced - memorised.
// Sitting vs standing uses thresholds tens of degrees apart, and position
// sense compares two readings of the same sensors, so neither needs the
// absolute accuracy the goniometer check measures.
// Chair stand and TUG need a STANDING calibration (thigh upright = 0 deg).
enum class TestKind : uint8_t { ChairStand = 0, TimedUpGo = 1, PositionSense = 2 };
constexpr int kTestCount = 3;
constexpr int kPosTrials = 3;
const char* testName(TestKind k);

enum class TestPhase : uint8_t {
  Idle = 0,
  NeedSeated,  // chair stand / TUG: waiting for the patient to sit
  Countdown,   // 3-2-1, then "Go"
  Running,     // chair stand: the 30 s window. TUG: timing until seated again
  Present,     // position sense: move until the buzz, hold still there
  Return,      // position sense: back to the start position, keep still
  Reproduce,   // position sense: eyes closed, find the angle again and hold
  Done,
  Aborted,
};

enum class TestAbort : uint8_t { None = 0, User, Timeout, NoWalk };
const char* testAbortText(TestAbort a);

struct TestResult {
  TestKind kind = TestKind::ChairStand;
  float score = 0;         // chair stand: stands (incl. a final half stand)
                           // TUG: seconds. Position sense: mean absolute error (deg)
  uint16_t stands = 0;     // chair stand: full stands
  bool halfStand = false;  // chair stand: over halfway up when the 30 s ended
  float riseSec = 0;       // chair stand: mean seat -> full stand. TUG: "Go" -> standing
  float walkSec = 0;       // TUG: standing -> starting to sit (walk + turn)
  float sitSec = 0;        // TUG: starting to sit -> resting on the seat
  float targetDeg = 0;     // position sense target
  uint8_t trials = 0;      // position sense trials done
  float trialErr[kPosTrials] = {0, 0, 0};  // reproduced - memorised (+ = more bent)
  float constErr = 0;      // position sense: mean signed error
};

class ClinicalTest {
 public:
  void start(TestKind k, float targetDeg, uint32_t nowMs);
  void cancel();  // -> Aborted (User)
  void reset();   // -> Idle
  // Feed every good, calibrated sample. Writes up to maxOut events, returns count.
  int update(const JointSample& s, uint32_t nowMs, Event* out, int maxOut);

  TestKind kind() const { return kind_; }
  TestPhase phase() const { return phase_; }
  bool active() const;
  const TestResult& result() const { return res_; }
  TestAbort abortReason() const { return abort_; }
  float elapsedSec(uint32_t nowMs) const;  // since "Go" (chair/TUG) or since start
  uint8_t countdown() const { return cd_; }
  uint8_t tugStage() const;                // 0 getting up, 1 walking, 2 sitting down
  float holdSec(uint32_t nowMs) const;     // position sense: time held still in the band
  bool armed() const { return armed_; }    // position sense: far enough from the target
  bool calLooksWrong() const { return calHint_; }  // knee bent but thigh upright

  // tunables
  float sitDeg = 60;        // thigh at least this far from upright ...
  float kneeSitDeg = 45;    // ... and knee at least this bent = sitting
  float standDeg = 20;      // thigh within this of upright ...
  float kneeStandDeg = 25;  // ... and knee this straight = full stand
  uint32_t seatedConfirmMs = 500;
  uint32_t chairWindowMs = 30000;
  uint32_t tugTimeoutMs = 60000;
  float tugMinWalkSec = 1.5f;
  float settleDps = 15;     // TUG stops when the thigh comes to rest on the seat
  float posBandDeg = 3;     // presentation band (+-)
  uint32_t posHoldMs = 3000;
  float posAwayDeg = 15;    // leave the target by this much between steps
  float posMoveDeg = 8;     // reproduction = at least this far from the start pose
  float posStillDeg = 3;    // "still" = stays within +- this ...
  uint32_t posStillMs = 1500;  // ... for this long
  uint32_t posStepTimeoutMs = 60000;

 private:
  enum Move : uint8_t { kSeated, kRising, kUp, kLowering, kSitting };
  void emit(Event* out, int maxOut, int& n, EventType t, float v = 0, float v2 = 0);
  void stop(TestAbort a, Event* out, int maxOut, int& n);
  void done(Event* out, int maxOut, int& n);
  void runChair(float h, bool seated, bool standing, uint32_t now, Event* out, int maxOut, int& n);
  void runTug(float h, float thighRate, bool seated, bool standing, uint32_t now, Event* out,
              int maxOut, int& n);
  void runPosition(float k, uint32_t now, Event* out, int maxOut, int& n);
  void trackStill(float k, uint32_t now);
  float stillMean() const { return stillN_ ? (float)(stillSum_ / stillN_) : stillAnchor_; }

  TestKind kind_ = TestKind::ChairStand;
  TestPhase phase_ = TestPhase::Idle;
  TestAbort abort_ = TestAbort::None;
  TestResult res_;
  uint32_t startMs_ = 0, phaseMs_ = 0, goMs_ = 0, seatedSince_ = 0;
  uint8_t cd_ = 0;
  uint8_t move_ = kSeated;
  float lastH_ = 0;
  bool calHint_ = false;
  double baseSum_ = 0;
  uint32_t baseN_ = 0;
  float baseline_ = 90;     // thigh angle while seated (measured in the countdown)
  // chair stand
  uint32_t mark_ = 0;
  float riseSum_ = 0;
  uint16_t riseN_ = 0;
  // TUG
  uint32_t upMs_ = 0, uprightMs_ = 0, seatMs_ = 0, endMs_ = 0;
  // position sense
  bool armed_ = false, inBand_ = false, moved_ = false, stillInit_ = false;
  int8_t side_ = 0;
  float ref_ = 0, retPose_ = 0, stillAnchor_ = 0;
  uint32_t stillSince_ = 0, stillN_ = 0;
  double stillSum_ = 0;
};

}  // namespace rs
