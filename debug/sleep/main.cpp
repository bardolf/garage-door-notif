// Garage door PoC — deep sleep debug firmware
// Cil: overit ESP32 timer wake, RTC slow memory persistence, wake-cause detekci.
// Pokud je MPU pripojen, je take uspan pres I2C — bez toho zere ~3.5 mA a maskuje
// ESP sleep current. Pro pure-ESP test nastav MPU_PRESENT = false.
//
// Testovaci protokol:
//   1. flash + monitor: pio run -e sleep -t upload -t monitor
//   2. sleduj cold boot vystup (boot #1)
//   3. cekej SLEEP_DURATION_S, mel by se objevit "wake #2" se stejnou Vbat
//   4. nech bezet 5-10 cyklu, pak Ctrl+C
//   5. EN/RST tlacitko -> boot_count se vynuluje (RTC mem prezije sleep, ne power cycle)
//   6. (volitelne) zmer realnou spotrebu USB testerem nebo INA219
//      ocekavaná: ~18 µA (13 ESP + 5 MPU); bez MPU sleep ~3.5 mA

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "esp_sleep.h"

static const uint64_t SLEEP_DURATION_S = 30;   // pro debug; produkce 300 (5 min)
static const uint32_t AWAKE_HOLD_MS    = 5000; // jak dlouho drzet ESP active po mereni (pro multimeter readout)
static const bool     MPU_PRESENT      = true; // false = preskoci I2C, jen ESP sleep
static const bool     LED_INDICATE     = true; // GPIO2 LED ON behem wake fáze (visual indicator pro battery test)

#define VBAT_PIN 34
#define LED_PIN  2     // FireBeetle on-board zelena LED, active HIGH

// MPU6050 I2C
static const uint8_t MPU_ADDR         = 0x68;
static const uint8_t REG_PWR_MGMT_1   = 0x6B;
static const uint8_t REG_ACCEL_XOUT_H = 0x3B;
static const uint8_t REG_WHO_AM_I     = 0x75;
static const uint8_t MPU_SLEEP_BIT    = 0x40;   // bit 6 of PWR_MGMT_1
static const float   ACCEL_LSB_PER_G  = 16384.0f;  // ±2g range
static const uint8_t MEASURE_SAMPLES  = 6;      // prvni se zahodi -> 5 platnych

// RTC slow memory (8 KB) — prezije deep sleep, ne power-off / EN reset.
RTC_DATA_ATTR uint32_t boot_count     = 0;
RTC_DATA_ATTR uint64_t total_sleep_us = 0;

static const char* wake_cause_str(esp_sleep_wakeup_cause_t c) {
  switch (c) {
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "cold boot / EN reset";
    case ESP_SLEEP_WAKEUP_TIMER:     return "RTC timer";
    case ESP_SLEEP_WAKEUP_EXT0:      return "EXT0 (RTC IO)";
    case ESP_SLEEP_WAKEUP_EXT1:      return "EXT1 (RTC mask)";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:  return "touchpad";
    case ESP_SLEEP_WAKEUP_ULP:       return "ULP coprocessor";
    default:                         return "other";
  }
}

// Posle PWR_MGMT_1 = 0x40 (SLEEP=1). Idempotentni.
static bool mpu_send_sleep() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_PWR_MGMT_1);
  Wire.write(MPU_SLEEP_BIT);
  return Wire.endTransmission() == 0;
}

// Posle PWR_MGMT_1 = 0x00 (clear SLEEP) a pocka na stabilizaci.
static bool mpu_wake() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_PWR_MGMT_1);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  delay(30);  // stabilizace oscilatoru
  return true;
}

// Cte 6 bajtu od ACCEL_XOUT_H burst-em a parsuje 3x int16.
static bool mpu_read_accel_raw(int16_t &ax, int16_t &ay, int16_t &az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6) != 6) return false;
  ax = (int16_t)((Wire.read() << 8) | Wire.read());
  ay = (int16_t)((Wire.read() << 8) | Wire.read());
  az = (int16_t)((Wire.read() << 8) | Wire.read());
  return true;
}

// Prumer N vzorku v g; prvni se zahodi (po wake byva outlier).
static bool mpu_measure_avg(float &ax, float &ay, float &az) {
  float sx = 0, sy = 0, sz = 0;
  uint8_t valid = 0;
  for (uint8_t i = 0; i < MEASURE_SAMPLES; i++) {
    int16_t rx, ry, rz;
    if (!mpu_read_accel_raw(rx, ry, rz)) { delay(10); continue; }
    if (i == 0) { delay(10); continue; }
    sx += rx / ACCEL_LSB_PER_G;
    sy += ry / ACCEL_LSB_PER_G;
    sz += rz / ACCEL_LSB_PER_G;
    valid++;
    delay(10);
  }
  if (valid == 0) return false;
  ax = sx / valid; ay = sy / valid; az = sz / valid;
  return true;
}

static float read_vbat() {
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  uint64_t sum_mv = 0;
  const int N = 16;
  for (int i = 0; i < N; i++) {
    sum_mv += analogReadMilliVolts(VBAT_PIN);
    delay(2);
  }
  return (sum_mv / (float)N) / 1000.0f * 2.0f;  // on-board divider x2
}

void setup() {
  // LED nejdriv — chceme co nejrychleji indikovat wake (jeste pred Serial init).
  if (LED_INDICATE) {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
  }

  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("=== Sleep debug ==="));

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  bool cold_boot = (cause == ESP_SLEEP_WAKEUP_UNDEFINED);

  if (cold_boot) {
    boot_count = 1;
    total_sleep_us = 0;
    Serial.println(F("[boot] COLD BOOT — RTC mem reset"));
  } else {
    boot_count++;
    Serial.printf("[boot] WAKE #%u  (cause: %s)\n", boot_count, wake_cause_str(cause));
  }

  float vbat = read_vbat();
  uint32_t up_ms = millis();
  float total_sleep_s = total_sleep_us / 1000000.0f;

  Serial.printf("[info] Vbat=%.3f V | uptime since wake=%lu ms | total sleep so far=%.1f s\n",
                vbat, (unsigned long)up_ms, total_sleep_s);

  if (MPU_PRESENT) {
    Wire.begin(21, 22);
    Wire.setClock(400000);

    uint32_t t0 = millis();
    if (!mpu_wake()) {
      Serial.println(F("[mpu] WARN: wake failed (MPU not responding?)"));
    } else {
      float ax, ay, az;
      if (!mpu_measure_avg(ax, ay, az)) {
        Serial.println(F("[mpu] WARN: measurement failed"));
      } else {
        float mag = sqrtf(ax*ax + ay*ay + az*az);
        Serial.printf("[mpu] accel ax=%+.3f ay=%+.3f az=%+.3f g | |a|=%.3f g | took %lu ms\n",
                      ax, ay, az, mag, (unsigned long)(millis() - t0));
      }
      if (mpu_send_sleep()) {
        Serial.println(F("[mpu] I2C SLEEP sent (PWR_MGMT_1 = 0x40)"));
      } else {
        Serial.println(F("[mpu] WARN: SLEEP send failed"));
      }
    }
  }

  // Hold awake — necha ESP v active modu, aby multimeter mel cas ukazat stabilni
  // hodnotu. ESP+MPU sleep konfiguraci uvidis az PO tomhle holdu.
  if (AWAKE_HOLD_MS > 0) {
    Serial.printf("[hold] holding awake for %lu ms (ESP active, MPU sleep)\n",
                  (unsigned long)AWAKE_HOLD_MS);
    Serial.flush();
    delay(AWAKE_HOLD_MS);
  }

  Serial.printf("[sleep] entering deep sleep for %llu s ...\n", SLEEP_DURATION_S);
  Serial.flush();   // drz tu zpravu venku, nez ESP usne

  // Kratky off-blink pred sleep — vizualne potvrdi prechod do sleep
  // (LED zhasne = ESP usnul). Bez toho by to byl jen tichy prechod.
  if (LED_INDICATE) {
    digitalWrite(LED_PIN, LOW);
    delay(50);
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
  }

  total_sleep_us += SLEEP_DURATION_S * 1000000ULL;
  esp_sleep_enable_timer_wakeup(SLEEP_DURATION_S * 1000000ULL);
  esp_deep_sleep_start();
  // za timto se nikdy nedostaneme — wake = restart, pokracuje znovu setup()
}

void loop() {
  // never reached
}
