// Garage door PoC — tilt debug firmware
// Cil: zmerit baseline po bootu, monitorovat odchylku, logovat prekroceni 60 deg.
// HW: FireBeetle ESP32-E V1.0 + MPU6050 (GY-521) na I2C (SDA=GPIO21, SCL=GPIO22).

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

// ---- MPU6050 registry ----
static const uint8_t MPU_ADDR         = 0x68;
static const uint8_t REG_PWR_MGMT_1   = 0x6B;
static const uint8_t REG_WHO_AM_I     = 0x75;
static const uint8_t REG_ACCEL_XOUT_H = 0x3B;

// ---- Konfigurace mereni ----
static const uint8_t  SAMPLE_COUNT        = 10;     // vzorku na jedno mereni (prvni se zahodi)
static const uint16_t SAMPLE_DELAY_MS     = 10;     // mezi vzorky
static const uint8_t  BASELINE_SAMPLES    = 50;     // pro stabilni baseline po bootu
static const uint16_t LOOP_INTERVAL_MS    = 1000;   // jak casto logovat
static const float    THRESHOLD_OPEN_DEG  = 60.0f;  // dle README.md
static const float    THRESHOLD_CLOSE_DEG = 40.0f;  // hystereze

// ±2g range -> 16384 LSB/g
static const float ACCEL_LSB_PER_G = 16384.0f;

// ---- Stav ----
struct Vec3 { float x, y, z; };
static Vec3  baseline = {0, 0, 0};
static float baseline_mag = 0;
static bool  threshold_active = false;

// ---- I2C helpers ----
static void mpu_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static uint8_t mpu_read8(uint8_t reg) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1);
  return Wire.read();
}

// Cte 6 bajtu od ACCEL_XOUT_H jednim burst-em.
static bool mpu_read_accel_raw(int16_t &ax, int16_t &ay, int16_t &az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_ACCEL_XOUT_H);
  Wire.endTransmission(false);
  uint8_t got = Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6);
  if (got != 6) return false;
  ax = (int16_t)((Wire.read() << 8) | Wire.read());
  ay = (int16_t)((Wire.read() << 8) | Wire.read());
  az = (int16_t)((Wire.read() << 8) | Wire.read());
  return true;
}

// Prumer N vzorku v g; prvni vzorek se zahodi (po wake byva outlier).
static bool measure_accel_g(uint8_t n_samples, Vec3 &out) {
  if (n_samples < 2) return false;
  float sx = 0, sy = 0, sz = 0;
  uint8_t valid = 0;
  for (uint8_t i = 0; i < n_samples; i++) {
    int16_t rx, ry, rz;
    if (!mpu_read_accel_raw(rx, ry, rz)) {
      Serial.printf("[WARN] sample %u read fail\n", i);
      delay(SAMPLE_DELAY_MS);
      continue;
    }
    if (i == 0) { delay(SAMPLE_DELAY_MS); continue; }
    sx += rx / ACCEL_LSB_PER_G;
    sy += ry / ACCEL_LSB_PER_G;
    sz += rz / ACCEL_LSB_PER_G;
    valid++;
    delay(SAMPLE_DELAY_MS);
  }
  if (valid == 0) return false;
  out.x = sx / valid;
  out.y = sy / valid;
  out.z = sz / valid;
  return true;
}

static float vec_mag(const Vec3 &v) {
  return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
}

// 3D uhel mezi aktualnim vektorem a baseline.
static float angle_from_baseline_deg(const Vec3 &v) {
  float mag = vec_mag(v);
  if (mag < 1e-6f || baseline_mag < 1e-6f) return 0;
  float dot = v.x*baseline.x + v.y*baseline.y + v.z*baseline.z;
  float c = dot / (mag * baseline_mag);
  if (c > 1.0f)  c = 1.0f;
  if (c < -1.0f) c = -1.0f;
  return acosf(c) * 180.0f / (float)PI;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("=== Garage tilt debug ==="));
  Serial.printf("Threshold: open >%.1f deg, close <%.1f deg (hysteresis)\n",
                THRESHOLD_OPEN_DEG, THRESHOLD_CLOSE_DEG);

  Wire.begin(21, 22);
  Wire.setClock(400000);

  // I2C scan (sanity)
  Serial.print(F("I2C scan: "));
  uint8_t found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("0x%02X ", a);
      found++;
    }
  }
  Serial.printf("(%u device%s)\n", found, found == 1 ? "" : "s");

  // Wake MPU + WHO_AM_I check
  mpu_write(REG_PWR_MGMT_1, 0x00);
  delay(50);
  uint8_t who = mpu_read8(REG_WHO_AM_I);
  Serial.printf("WHO_AM_I = 0x%02X (expected 0x68)\n", who);
  if (who != 0x68) {
    Serial.println(F("[FATAL] MPU6050 not responding. Check wiring."));
    while (true) { delay(1000); }
  }

  // Baseline capture
  Serial.printf("Capturing baseline (%u samples)...\n", BASELINE_SAMPLES);
  delay(200);
  if (!measure_accel_g(BASELINE_SAMPLES, baseline)) {
    Serial.println(F("[FATAL] baseline capture failed"));
    while (true) { delay(1000); }
  }
  baseline_mag = vec_mag(baseline);
  Serial.printf("Baseline:  ax=%+.4f g  ay=%+.4f g  az=%+.4f g  |a|=%.4f g\n",
                baseline.x, baseline.y, baseline.z, baseline_mag);
  Serial.println(F("Baseline locked. Monitoring tilt..."));
  Serial.println(F("---"));
}

void loop() {
  Vec3 a;
  if (!measure_accel_g(SAMPLE_COUNT, a)) {
    Serial.println(F("[WARN] measurement failed"));
    delay(LOOP_INTERVAL_MS);
    return;
  }

  float mag   = vec_mag(a);
  float angle = angle_from_baseline_deg(a);

  float dx = a.x - baseline.x;
  float dy = a.y - baseline.y;
  float dz = a.z - baseline.z;

  if (!threshold_active && angle > THRESHOLD_OPEN_DEG) {
    threshold_active = true;
    Serial.printf("[ALERT] THRESHOLD CROSSED  angle=%.2f deg > %.1f\n",
                  angle, THRESHOLD_OPEN_DEG);
  } else if (threshold_active && angle < THRESHOLD_CLOSE_DEG) {
    threshold_active = false;
    Serial.printf("[INFO]  threshold cleared  angle=%.2f deg < %.1f\n",
                  angle, THRESHOLD_CLOSE_DEG);
  }
  const char *state = threshold_active ? "OPEN " : "OK   ";

  Serial.printf(
    "[%s] ax=%+.3f ay=%+.3f az=%+.3f g | dx=%+.3f dy=%+.3f dz=%+.3f | |a|=%.3f g | tilt=%6.2f deg\n",
    state, a.x, a.y, a.z, dx, dy, dz, mag, angle);

  delay(LOOP_INTERVAL_MS);
}
