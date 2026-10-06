// imu.cpp
#include "imu.h"

namespace {
constexpr uint8_t REG_SMPLRT_DIV = 0x19;
constexpr uint8_t REG_CONFIG = 0x1A;
constexpr uint8_t REG_GYRO_CONFIG = 0x1B;
constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
constexpr uint8_t REG_WHO_AM_I = 0x75;

constexpr uint8_t GYRO_FS_500 = 0x08;   // +-500 deg/s  -> 65.5 LSB/(deg/s)
constexpr uint8_t ACCEL_FS_4G = 0x08;   // +-4 g        -> 8192 LSB/g
constexpr float GYRO_LSB = 65.5f;
constexpr float ACCEL_LSB = 8192.0f;
constexpr uint16_t MAX_CONSECUTIVE_ERRORS = 10;

inline int16_t be16(const uint8_t* p) { return (int16_t)((p[0] << 8) | p[1]); }
}  // namespace

bool Mpu6050::present() {
  wire_.beginTransmission(addr_);
  return wire_.endTransmission() == 0;
}

bool Mpu6050::writeReg(uint8_t reg, uint8_t val) {
  wire_.beginTransmission(addr_);
  wire_.write(reg);
  wire_.write(val);
  return wire_.endTransmission() == 0;
}

bool Mpu6050::readRegs(uint8_t reg, uint8_t* buf, size_t len) {
  wire_.beginTransmission(addr_);
  wire_.write(reg);
  if (wire_.endTransmission() != 0) return false;
  size_t got = wire_.requestFrom(addr_, (uint8_t)len);
  if (got != len) {
    while (wire_.available()) wire_.read();  // flush partial data
    return false;
  }
  for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)wire_.read();
  return true;
}

bool Mpu6050::begin() {
  ok_ = false;
  if (!present()) return false;

  bool good = writeReg(REG_PWR_MGMT_1, 0x80);  // device reset
  delay(100);
  good &= writeReg(REG_PWR_MGMT_1, 0x01);      // wake up, clock = gyro X PLL
  delay(10);
  if (!readRegs(REG_WHO_AM_I, &who_, 1)) return false;
  // Genuine MPU6050 answers 0x68 (even at address 0x69). Common clones answer
  // 0x70/0x72/0x98 with the same register map, so only reject obvious junk.
  if (who_ == 0x00 || who_ == 0xFF) return false;

  good &= writeReg(REG_SMPLRT_DIV, 4);          // 1 kHz / (1+4) = 200 Hz internal
  good &= writeReg(REG_CONFIG, 0x03);           // DLPF ~44 Hz: removes vibration noise
  good &= writeReg(REG_GYRO_CONFIG, GYRO_FS_500);
  good &= writeReg(REG_ACCEL_CONFIG, ACCEL_FS_4G);
  if (!good) return false;

  // Verify the configuration really stuck (catches flaky wiring early).
  uint8_t g = 0, a = 0, pw = 0;
  if (!readRegs(REG_GYRO_CONFIG, &g, 1) || !readRegs(REG_ACCEL_CONFIG, &a, 1) ||
      !readRegs(REG_PWR_MGMT_1, &pw, 1))
    return false;
  if ((g & 0x18) != GYRO_FS_500 || (a & 0x18) != ACCEL_FS_4G || (pw & 0x40)) return false;

  consecutive_ = 0;
  ok_ = true;
  return true;
}

bool Mpu6050::read(rs::Vec3& acc, rs::Vec3& gyr) {
  uint8_t b[14];
  bool good = readRegs(REG_ACCEL_XOUT_H, b, sizeof(b));
  if (good) {
    int16_t ax = be16(b), ay = be16(b + 2), az = be16(b + 4);
    int16_t gx = be16(b + 8), gy = be16(b + 10), gz = be16(b + 12);
    acc = rs::Vec3{ax / ACCEL_LSB, ay / ACCEL_LSB, az / ACCEL_LSB};
    gyr = rs::Vec3{gx / GYRO_LSB, gy / GYRO_LSB, gz / GYRO_LSB};
    // A sleeping / reset chip returns zeros; a disconnected bus often 0xFFFF.
    float an = rs::vnorm(acc);
    if (an < 0.2f || an > 3.9f) good = false;
  }
  if (good) {
    consecutive_ = 0;
    return true;
  }
  errors_++;
  if (consecutive_ < 0xFFFF) consecutive_++;
  if (consecutive_ >= MAX_CONSECUTIVE_ERRORS) ok_ = false;
  return false;
}
