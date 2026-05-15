// Persistovany stav aplikace v RTC slow memory (8 KB SRAM).
// Prezije deep sleep i RST tlacitko; ztraci se jen pri uplnem odpojeni napajeni.
//
// `STATE_MAGIC` sentinel rozlisuje "true cold boot" (RTC mem cleared, magic !=)
// od "RST/wake s validnim stavem" (magic ==). Bump magic value pri ZMENE
// LAYOUTU AppState — invalidate existujici RTC stav, vynuti se cold boot.

#pragma once

#include <Arduino.h>
#include "vec3.h"

constexpr uint32_t STATE_MAGIC = 0xC0FFEE45;     // bumped: added heartbeat_unix, wifi_fail_count, error_msg_len constant
constexpr size_t   PENDING_ERROR_MSG_LEN = 96;

struct AppState {
  uint32_t magic;
  uint32_t boot_count;
  uint64_t last_alert_unix;
  uint64_t last_heartbeat_unix;       // 0 = nikdy; nahrazuje yday (year-safe)
  uint64_t last_ntp_unix;
  uint64_t first_boot_unix;           // 0 = nenastaveno
  Vec3     baseline;
  bool     baseline_valid;
  bool     door_was_open;
  // Error queue — chyby z offline kontextu, posila se na pristim WiFi connectu.
  uint32_t pending_error_count;
  uint64_t last_error_report_unix;
  char     pending_error_msg[PENDING_ERROR_MSG_LEN];
  uint8_t  mpu_who_id;                // 0x68=MPU-6050, 0x70=MPU-6500
  uint32_t wifi_consecutive_fails;    // pocet po sobe failnutych WiFi connectu
};

extern RTC_DATA_ATTR AppState state;

// Bezpecny rate-limit check. Tri pripady:
//   - normalni: now > since, vraci elapsed >= threshold
//   - first-ever: since == 0, vrati true (nikdy nebyl predchozi event)
//   - stale future (since > now po NTP correction): vrati true (auto-heal,
//     misto navzdy zablokovaneho rate limitu) — jinak by selhani NTP
//     resync forever-blokovalo alerty.
inline bool elapsed_at_least(uint64_t now, uint64_t since, uint32_t threshold_s) {
  if (since == 0)   return true;        // nikdy predtim
  if (since > now)  return true;        // clock walked back — auto-heal
  return (now - since) >= static_cast<uint64_t>(threshold_s);
}
