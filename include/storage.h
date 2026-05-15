// Persistentni konfigurace v NVS (Non-Volatile Storage ve flash).
// Prezije power-off, na rozdil od RTC mem.
//
// Workflow:
//   1. `cfg_load()` v setup() — naplni globalni `cfg` z NVS. Pro nenalezene
//      klice pouzije compile-time defaults z config.h (resp. secrets.h pro
//      creds).
//   2. `cfg.configured` flag rozlisi "fresh device (AP mode)" od "OK boot".
//   3. AP form submit → `cfg_save()` zapise zmenene fieldy + nastavi
//      `configured = true` → `ESP.restart()`.
//   4. Double-RST trigger → `cfg_clear()` smaze NVS namespace cele →
//      pri pristim bootu cfg.configured == false → AP mode.

#pragma once

#include <Arduino.h>

constexpr size_t CFG_STR_MAX = 64;

struct Config {
  bool     configured;          // true = NVS ma platnou config (po AP submitu)
  // Network
  char     wifi_ssid[CFG_STR_MAX];
  char     wifi_pass[CFG_STR_MAX];
  char     ntfy_topic[CFG_STR_MAX];
  char     device_name[CFG_STR_MAX];
  // Behavior
  uint8_t  night_start_hour;
  uint8_t  night_end_hour;
  uint8_t  heartbeat_hour;
  uint32_t wake_interval_s;
};

extern Config cfg;

// Naplni `cfg` z NVS. Pro nenalezene klice pouzije defaults
// z config.h / secrets.h. Vola se v setup() po `pixel.begin`.
void cfg_load();

// Uloz aktualni `cfg` do NVS. Volat z AP form submit handleru po
// uspesnem test-connectu. Setuje `cfg.configured = true`.
void cfg_save();

// Vymaze cely "garaz" NVS namespace. Pro factory reset / double-RST.
// Nedotyka se RTC mem (baseline, counters zustavaji).
void cfg_clear();
