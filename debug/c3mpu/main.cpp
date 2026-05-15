// MPU6050/6500 bring-up na LaskaKit ESP32-C3-LPKit v4.
// HW: I2C na SDA=GPIO6, SCL=GPIO7. GY-521 modul napajen z 3V3/GND headeru.
// GPIO4 (PERIPH_EN) musi byt HIGH — jinak je 3V3 vetev na headeru bez napajeni.
//
// Pozn.: "GY-521" moduly se prodavaji s MPU-6050 (WHO_AM_I=0x68) i MPU-6500
// (0x70). Pro accel/gyro v zakladnim rozsahu jsou registry kompatibilni; lisi
// se jen teplotni vzorec.
//
// pio run -e c3mpu -t upload -t monitor

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#define I2C_SDA       6
#define I2C_SCL       7
#define PERIPH_EN_PIN 4

static const uint8_t MPU_ADDR         = 0x68;
static const uint8_t REG_PWR_MGMT_1   = 0x6B;
static const uint8_t REG_WHO_AM_I     = 0x75;
static const uint8_t REG_ACCEL_XOUT_H = 0x3B;

// ±2g range -> 16384 LSB/g (stejne pro MPU-6050 i MPU-6500)
static const float ACCEL_LSB_PER_G = 16384.0f;

// Teplotni vzorec se lisi podle cipu — viz datasheet:
//   MPU-6050:  T = raw/340     + 36.53
//   MPU-6500:  T = raw/333.87  + 21.0
static float    temp_lsb_per_c = 340.0f;
static float    temp_offset_c  = 36.53f;
static uint8_t  who_id         = 0;

static const uint16_t LOOP_INTERVAL_MS = 1000;

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

// Burst cteni 8 bajtu od ACCEL_XOUT_H: ax, ay, az, temp.
static bool mpu_read_burst(int16_t &ax, int16_t &ay, int16_t &az, int16_t &t) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_ACCEL_XOUT_H);
  Wire.endTransmission(false);
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)8) != 8) return false;
  ax = (int16_t)((Wire.read() << 8) | Wire.read());
  ay = (int16_t)((Wire.read() << 8) | Wire.read());
  az = (int16_t)((Wire.read() << 8) | Wire.read());
  t  = (int16_t)((Wire.read() << 8) | Wire.read());
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("=== C3 MPU6050 bring-up ==="));
  Serial.printf("I2C: SDA=GPIO%d  SCL=GPIO%d\n", I2C_SDA, I2C_SCL);

  pinMode(PERIPH_EN_PIN, OUTPUT);
  digitalWrite(PERIPH_EN_PIN, HIGH);
  delay(50);
  Serial.printf("PERIPH_EN (GPIO%d) = HIGH\n", PERIPH_EN_PIN);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

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

  mpu_write(REG_PWR_MGMT_1, 0x00);
  delay(50);
  who_id = mpu_read8(REG_WHO_AM_I);
  const char *chip = "neznamy";
  switch (who_id) {
    case 0x68: chip = "MPU-6050"; temp_lsb_per_c = 340.0f;    temp_offset_c = 36.53f; break;
    case 0x70: chip = "MPU-6500"; temp_lsb_per_c = 333.87f;   temp_offset_c = 21.0f;  break;
    case 0x71: chip = "MPU-9250"; temp_lsb_per_c = 333.87f;   temp_offset_c = 21.0f;  break;
    case 0x73: chip = "MPU-9255"; temp_lsb_per_c = 333.87f;   temp_offset_c = 21.0f;  break;
  }
  Serial.printf("WHO_AM_I = 0x%02X  → %s\n", who_id, chip);
  if (who_id != 0x68 && who_id != 0x70 && who_id != 0x71 && who_id != 0x73) {
    Serial.println(F("[FATAL] Neznamy/zadny senzor. Zkontroluj kabelaz a PERIPH_EN."));
    while (true) { delay(1000); }
  }

  Serial.println(F("Sensor online. Logging accel + temp..."));
  Serial.println(F("---"));
}

void loop() {
  int16_t rx, ry, rz, rt;
  if (!mpu_read_burst(rx, ry, rz, rt)) {
    Serial.println(F("[WARN] burst read failed"));
    delay(LOOP_INTERVAL_MS);
    return;
  }

  float ax = rx / ACCEL_LSB_PER_G;
  float ay = ry / ACCEL_LSB_PER_G;
  float az = rz / ACCEL_LSB_PER_G;
  float mag = sqrtf(ax*ax + ay*ay + az*az);
  float tc = rt / temp_lsb_per_c + temp_offset_c;

  Serial.printf("ax=%+.3f ay=%+.3f az=%+.3f g | |a|=%.3f g | T=%.1f C\n",
                ax, ay, az, mag, tc);

  delay(LOOP_INTERVAL_MS);
}
