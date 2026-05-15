#include "notifications.h"

#include <string.h>
#include <time.h>
#include "config.h"
#include "net.h"
#include "sensor.h"
#include "state.h"
#include "time_utils.h"

namespace {

// UTF-8 safe truncate: po orezani na byte limit ustoupi zpet pred multibyte
// sekvenci, aby vystup nikdy nekoncil v polovine codepoint-u. Vstup je dst[N]
// uz nul-terminovany (po strncpy), funkce upravuje konec.
void utf8_safe_truncate(char* dst, size_t cap) {
  if (cap == 0) return;
  // strncpy do dst (cap-1) uz nas vraci nul-terminated, ale posledni byte
  // pred nul muze byt lead/cont byte cut mid-sequence. Zalezujeme z konce.
  size_t len = strnlen(dst, cap);
  if (len == 0) return;
  // Pripad: posledni byte je continuation (0b10xxxxxx). Zalezuj zpet az k
  // lead byte a smaz cely neuplny codepoint.
  while (len > 0 && (static_cast<unsigned char>(dst[len - 1]) & 0xC0) == 0x80) {
    --len;
  }
  // Pripad: posledni byte je lead byte (0b11xxxxxx) ale nasledujici cont bytes
  // byly orizly. Smaz i ho.
  if (len > 0 && (static_cast<unsigned char>(dst[len - 1]) & 0x80) != 0) {
    --len;
  }
  dst[len] = '\0';
}

}  // namespace

bool send_boot_notification(float vbat, float vbat_pct, int rssi, const Vec3& baseline) {
  char timestr[32];
  format_time(timestr, sizeof(timestr), "%H:%M:%S");
  float theta;
  const char* orient = orientation_label(baseline, theta);
  char body[384];
  snprintf(body, sizeof(body),
    "Čas: %s\n"
    "Baterie: %.0f %% (%.2f V)\n"
    "Signál: %s (%d dBm)\n"
    "\n"
    "Reference zavřených vrat zachycena.\n"
    "Orientace: %s (%.0f° od horizontály).\n"
    "\n"
    "Pokud orientace neodpovídá realitě (vrata mají být zevřená při instalaci),\n"
    "při zavřených vratech zařízení vypnout a zapnout — uloží se nová reference.",
    timestr, vbat_pct, vbat, rssi_bars(rssi), rssi,
    orient, theta);
  return ntfy_send("Hlidac garazovych vrat aktivni", body, "default", "rocket");
}

bool send_urgent_alert(float tilt, float vbat_pct, bool time_known) {
  char timestr[32];
  if (time_known) {
    format_time(timestr, sizeof(timestr), "%H:%M");
  } else {
    snprintf(timestr, sizeof(timestr), "(čas neznámý)");
  }
  char body[256];
  snprintf(body, sizeof(body),
    "Čas: %s\n"
    "Náklon: %.0f° (limit %.0f°)\n"
    "Baterie: %.0f %%\n"
    "\n"
    "Zkontroluj vrata.",
    timestr, tilt, TILT_THRESHOLD_OPEN, vbat_pct);
  return ntfy_send("GARAZ OTEVRENA!", body, "urgent", "rotating_light");
}

bool send_heartbeat(bool door_open, float tilt, float vbat, float vbat_pct, int rssi) {
  char timestr[32];
  format_time(timestr, sizeof(timestr), "%H:%M");
  const char* tag = (vbat_pct > 50) ? "white_check_mark"
                  : (vbat_pct > 20) ? "yellow_circle"
                                    : "battery";
  char uptime_str[32] = "?";
  if (state.first_boot_unix > 0) {
    time_t now; time(&now);
    int64_t elapsed = static_cast<int64_t>(now) - static_cast<int64_t>(state.first_boot_unix);
    if (elapsed < 0) elapsed = 0;
    long days  = static_cast<long>(elapsed / 86400);
    long hours = static_cast<long>((elapsed % 86400) / 3600);
    long mins  = static_cast<long>((elapsed % 3600) / 60);
    snprintf(uptime_str, sizeof(uptime_str), "%ldd %ldh %ldm", days, hours, mins);
  }

  char body[320];
  snprintf(body, sizeof(body),
    "Čas: %s\n"
    "Stav: %s\n"
    "Náklon: %.0f°\n"
    "Baterie: %.0f %% (%.2f V)\n"
    "Signál: %s (%d dBm)\n"
    "Běží: %s\n"
    "\n"
    "Pokud tato denní kontrola někdy nedorazí, něco se pokazilo — zkontroluj zařízení.",
    timestr,
    door_open ? "OTEVŘENO" : "ZAVŘENO",
    tilt, vbat_pct, vbat, rssi_bars(rssi), rssi,
    uptime_str);
  return ntfy_send("Garaz - denni kontrola", body, "low", tag);
}

void queue_error(const char* msg) {
  // Cap counteru — chrani pred theoretickym overflow v RTC mem pri dlouhem outage.
  if (state.pending_error_count < PENDING_ERROR_COUNT_CAP) {
    state.pending_error_count++;
  }
  strncpy(state.pending_error_msg, msg, PENDING_ERROR_MSG_LEN - 1);
  state.pending_error_msg[PENDING_ERROR_MSG_LEN - 1] = '\0';
  utf8_safe_truncate(state.pending_error_msg, PENDING_ERROR_MSG_LEN);
  Serial.printf("[error] queued: %s (total pending=%u)\n",
                msg, state.pending_error_count);
}

void flush_pending_errors(float vbat_pct, int rssi) {
  if (state.pending_error_count == 0) return;

  char timestr[32] = "(čas neznámý)";
  if (time_is_synced()) format_time(timestr, sizeof(timestr), "%H:%M");

  const char* reassurance = (state.pending_error_count == 1)
    ? "Jednorázová chyba je obvykle OK (vibrace, krátkodobá porucha).\n"
      "Pokud se chyba zopakuje, zkontroluj připojení senzoru."
    : "Chyby se opakují — pravděpodobně skutečný problém.\n"
      "Zkontroluj připojení senzoru (4 vodiče), případně vyměň modul.";

  char body[448];
  snprintf(body, sizeof(body),
    "Čas: %s\n"
    "Počet chyb od posledního hlášení: %u\n"
    "Poslední: %s\n"
    "Baterie: %.0f %%\n"
    "Signál: %s (%d dBm)\n"
    "\n"
    "%s",
    timestr, state.pending_error_count, state.pending_error_msg,
    vbat_pct, rssi_bars(rssi), rssi,
    reassurance);

  if (ntfy_send("Hlidac vrat - CHYBA", body, "high", "warning")) {
    state.pending_error_count = 0;
    state.pending_error_msg[0] = '\0';
    time_t now; time(&now);
    state.last_error_report_unix = now;
    Serial.println(F("[error] queue flushed"));
  } else {
    Serial.println(F("[error] flush FAIL — queue zustava"));
  }
}
