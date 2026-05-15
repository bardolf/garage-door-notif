#include "sensor.h"

#include <Wire.h>
#include <math.h>
#include "config.h"

namespace {

constexpr uint8_t REG_PWR_MGMT_1   = 0x6B;
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
constexpr uint8_t REG_WHO_AM_I     = 0x75;
constexpr float   ACCEL_LSB_PER_G  = 16384.0f;   // ±2g range
constexpr uint8_t PWR_SLEEP_BIT    = 0x40;

bool i2c_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_I2C_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

}  // namespace

void mpu_initialize_i2c(uint8_t sda_pin, uint8_t scl_pin) {
  Wire.begin(sda_pin, scl_pin);
  Wire.setClock(400000);
  Wire.setTimeOut(I2C_TIMEOUT_MS);   // bran proti wedged-bus stackingu pres retries
}

bool mpu_wake() {
  if (!i2c_write(REG_PWR_MGMT_1, 0x00)) return false;
  delay(30);  // stabilizace oscilatoru
  return true;
}

bool mpu_sleep() {
  return i2c_write(REG_PWR_MGMT_1, PWR_SLEEP_BIT);
}

uint8_t mpu_who_am_i() {
  Wire.beginTransmission(MPU_I2C_ADDR);
  Wire.write(REG_WHO_AM_I);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom(static_cast<uint8_t>(MPU_I2C_ADDR), static_cast<uint8_t>(1)) != 1) return 0;
  return Wire.read();
}

const char* mpu_chip_name(uint8_t who_id) {
  switch (who_id) {
    case 0x68: return "MPU-6050";
    case 0x70: return "MPU-6500";
    case 0x71: return "MPU-9250";
    case 0x73: return "MPU-9255";
    default:   return "neznamy";
  }
}

bool mpu_is_known_chip(uint8_t who_id) {
  return who_id == 0x68 || who_id == 0x70 || who_id == 0x71 || who_id == 0x73;
}

bool mpu_read_accel_raw(int16_t& ax, int16_t& ay, int16_t& az) {
  Wire.beginTransmission(MPU_I2C_ADDR);
  Wire.write(REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(static_cast<uint8_t>(MPU_I2C_ADDR), static_cast<uint8_t>(6)) != 6) return false;
  // Explicitni byte-by-byte read (vyhne se neuplnemu evaluation order na operandech
  // |) + cast pres uint16_t pred int16_t (safe sign extension).
  auto read_be16 = []() {
    uint16_t hi = static_cast<uint16_t>(Wire.read());
    uint16_t lo = static_cast<uint16_t>(Wire.read());
    return static_cast<int16_t>((hi << 8) | lo);
  };
  ax = read_be16();
  ay = read_be16();
  az = read_be16();
  return true;
}

bool mpu_measure_avg(uint8_t n_samples, Vec3& out) {
  if (n_samples < 2) return false;
  float sx = 0, sy = 0, sz = 0;
  uint8_t valid = 0;
  for (uint8_t i = 0; i < n_samples; i++) {
    int16_t rx, ry, rz;
    if (!mpu_read_accel_raw(rx, ry, rz)) { delay(10); continue; }
    if (i == 0) { delay(10); continue; }   // prvni vzorek je outlier po wake
    sx += rx / ACCEL_LSB_PER_G;
    sy += ry / ACCEL_LSB_PER_G;
    sz += rz / ACCEL_LSB_PER_G;
    valid++;
    delay(10);
  }
  if (valid == 0) return false;
  out.x = sx / valid;
  out.y = sy / valid;
  out.z = sz / valid;
  return true;
}

const char* orientation_label(const Vec3& a, float& theta_out_deg) {
  float m = vec_mag(a);
  if (!isfinite(m) || m < 1e-6f) { theta_out_deg = 0; return "neznámá"; }
  float c = fabsf(a.z) / m;
  if (c > 1.0f) c = 1.0f;
  theta_out_deg = acosf(c) * 180.0f / static_cast<float>(PI);
  if (theta_out_deg < 25.0f) return "leží naplocho (Z dominantní)";
  if (theta_out_deg > 65.0f) return "stojí vertikálně (X/Y dominantní)";
  return "šikmo";
}
