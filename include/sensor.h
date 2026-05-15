// MPU6050 / MPU6500 abstrakce — low-level I2C operace + sample averaging.
// Stateless wrappery nad Wire.h; sluzbe orientuje na single sensor na adrese
// 0x68. Pri zmene desky volat `mpu_initialize_i2c()` pred prvnim cteni.
//
// Chip family detekce: WHO_AM_I register vraci 0x68 (MPU-6050) nebo 0x70
// (MPU-6500); register layout pro accel je v nasi podmnozine kompatibilni,
// takze API se chova stejne pres obe varianty. Volajicimu staci si pamatovat
// `who_am_i()` pro chip identifikaci v logu / boot notifikaci.

#pragma once

#include <Arduino.h>
#include "vec3.h"

constexpr uint8_t MPU_I2C_ADDR = 0x68;

// I2C bus init — vola se na zacatku cold bootu i na zacatku kazdeho wake.
// Idempotentni; Wire.begin lze volat opakovane.
void mpu_initialize_i2c(uint8_t sda_pin, uint8_t scl_pin);

// Probudi chip (PWR_MGMT_1 = 0x00), pocka na stabilizaci oscilatoru.
// Vraci false pri I2C chybe (sensor nereaguje).
bool mpu_wake();

// Uspi chip (PWR_MGMT_1 = SLEEP bit). Po deep sleep s PERIPH_EN=LOW neni
// nutne — MPU dostane 0 V. Volame pro pripad ze PERIPH_EN zustane HIGH.
bool mpu_sleep();

// Cte WHO_AM_I registr. Vraci 0 pri I2C chybe.
uint8_t mpu_who_am_i();

// Lidsky-citelne jmeno chipu podle WHO_AM_I (jen pro Serial log).
const char* mpu_chip_name(uint8_t who_id);

// Akceptujeme MPU-6050/6500/9250/9255 — register layout je v nasi podmnozine
// kompatibilni. Ostatni hodnoty WHO_AM_I jsou bud klony nebo jiny senzor.
bool mpu_is_known_chip(uint8_t who_id);

// Burst cte 6 bajtu od ACCEL_XOUT_H, vraci raw int16 v ±2g range
// (16384 LSB/g). Vraci false pri I2C chybe.
bool mpu_read_accel_raw(int16_t& ax, int16_t& ay, int16_t& az);

// Prumer `n_samples` accel mereni v jednotkach g. Prvni vzorek se zahodi
// (po wake byva outlier). Vraci false pokud zadny vzorek neprosel.
bool mpu_measure_avg(uint8_t n_samples, Vec3& out);

// Slovni popis orientace senzoru z gravitacniho vektoru. Vraci aktualni
// uhel od horizontaly v `theta_out_deg` (0° = lezi naplocho, 90° = stoji
// vertikalne). Pouziva se v boot notifikaci pro install-time UX.
const char* orientation_label(const Vec3& a, float& theta_out_deg);
