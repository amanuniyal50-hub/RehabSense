// imu.h -- minimal, dependency-free MPU6050 driver (register level).
// Why not a library: we need burst reads, error counting and in-place
// re-initialisation after a loose wire, which most libraries don't expose.
#pragma once
#include <Arduino.h>
#include <Wire.h>

#include "rehab_logic.h"

class Mpu6050 {
 public:
  Mpu6050(TwoWire& wire, uint8_t addr) : wire_(wire), addr_(addr) {}

  bool present();                 // ACKs on the bus?
  bool begin();                   // reset + configure; true if OK
  // Burst-read accel (g) and gyro (deg/s). Returns false on a bus error or an
  // implausible sample (all zeros / sleeping chip).
  bool read(rs::Vec3& accG, rs::Vec3& gyroDps);

  bool ok() const { return ok_; }
  uint8_t address() const { return addr_; }
  uint8_t whoAmI() const { return who_; }
  uint32_t errorCount() const { return errors_; }
  uint16_t consecutiveErrors() const { return consecutive_; }

 private:
  bool writeReg(uint8_t reg, uint8_t val);
  bool readRegs(uint8_t reg, uint8_t* buf, size_t len);

  TwoWire& wire_;
  uint8_t addr_;
  uint8_t who_ = 0;
  bool ok_ = false;
  uint32_t errors_ = 0;
  uint16_t consecutive_ = 0;
};
