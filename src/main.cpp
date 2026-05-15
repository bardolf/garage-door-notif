// Garage door notif — orchestrace pro LaskaKit ESP32-C3-LPKit v4.
//
// Architektura:
//   COLD BOOT (power-on / RTC mem cleared):
//     1. modra LED, WiFi connect + NTP sync
//     2. MPU bring-up + baseline capture (50 vzorku, prumer)
//     3. Boot notifikace na ntfy.sh
//     4. zelena/cervena LED solid + blink (upload window)
//     5. PERIPH_EN LOW, deep sleep WAKE_INTERVAL_S
//
//   WAKE (z deep sleep):
//     1. MPU wake + measure (3 retry pri I2C glitchi)
//     2. Tilt vs baseline + hystereze: open >60°, close <40°
//     3. Casova logika rozhodne co posilat (alert / heartbeat / NTP / errors)
//        - Alert fail-open: kdyz cas neni synced, presto posli (security feature)
//        - Heartbeat year-safe: kontrola pres unix elapsed, ne yday
//     4. WiFi connect → ntp_sync (opportunistic) → poslat
//     5. fialova LED 5 s pri alertu, cervena 5 s pri chybe
//     6. PERIPH_EN LOW, deep sleep
//
// Modulova struktura — viz hlavicky v include/.

#include <Arduino.h>
#include "esp_sleep.h"
#include "esp_system.h"
#include "soc/rtc.h"
extern "C" uint64_t rtc_time_slowclk_to_us(uint64_t rtc_cycles, uint32_t period);
extern "C" uint64_t rtc_time_get();
extern "C" uint32_t rtc_clk_slow_freq_get_hz();

#include "ap_config.h"
#include "config.h"
#include "led.h"
#include "net.h"
#include "notifications.h"
#include "power.h"
#include "sensor.h"
#include "state.h"
#include "storage.h"
#include "time_utils.h"

namespace {

// Heartbeat dedup pres last_heartbeat_unix — year-safe (yday-pristup mel
// kolizi na Jan 1 / Dec 31). 23 h misto 24 h dava margin proti drobnemu
// driftu mezi wakes.
constexpr uint64_t HEARTBEAT_MIN_GAP_S = 23 * 3600;

bool mpu_bringup() {
  mpu_initialize_i2c(SDA_PIN, SCL_PIN);
  if (!mpu_wake()) return false;
  uint8_t who = mpu_who_am_i();
  state.mpu_who_id = who;
  Serial.printf("[mpu] WHO_AM_I=0x%02X (%s)\n", who, mpu_chip_name(who));
  return mpu_is_known_chip(who);
}

void enter_deep_sleep() {
  Serial.printf("[sleep] %sgoing to sleep for %lu s\n",
                DEEP_SLEEP_ENABLED ? "" : "[DEBUG simulated] ",
                static_cast<unsigned long>(cfg.wake_interval_s));
  Serial.flush();

  if (DEEP_SLEEP_ENABLED) {
    periph_power_off_for_sleep();
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(cfg.wake_interval_s) * 1000000ULL);
    esp_deep_sleep_start();
    // unreachable — wake = reset
  }
  // Debug mode: vrati se do volajiciho. setup() pak preda rizeni loop()u,
  // ktery simuluje wake po delay. Perif rail zustava HIGH (NeoPixel + MPU
  // napajene), USB-CDC nikdy neodpadne, vsechny logy zachyceny v monitoru.
}

void do_cold_boot() {
  Serial.println(F("=== COLD BOOT ==="));
  // Preserve double-RST tracking (nastaveno v setup() pred volanim).
  uint64_t save_rtc_us = state.last_cold_boot_rtc_us;
  uint8_t  save_rapid  = state.rapid_reset_count;
  state = AppState{};                 // zero-init vsech ostatnich fieldu
  state.last_cold_boot_rtc_us  = save_rtc_us;
  state.rapid_reset_count      = save_rapid;
  state.magic                  = STATE_MAGIC;
  state.boot_count             = 1;

  led_show(0, 0, 60);   // modra = cold boot bezi

  bool online = wifi_connect();
  if (online) {
    ntp_sync();
    state.wifi_consecutive_fails = 0;
  } else {
    state.wifi_consecutive_fails++;
  }

  if (!mpu_bringup()) {
    Serial.println(F("[fatal] sensor not responding"));
    queue_error("Senzor nereaguje (možný uvolněný kabel)");
  } else {
    Vec3 base;
    if (mpu_measure_avg(50, base)) {
      state.baseline       = base;
      state.baseline_valid = true;
      Serial.printf("[mpu] baseline ax=%+.3f ay=%+.3f az=%+.3f |a|=%.3f g\n",
                    base.x, base.y, base.z, vec_mag(base));
    } else {
      Serial.println(F("[mpu] baseline measure FAIL"));
      queue_error("Měření reference selhalo (senzor nereaguje stabilně)");
    }
    mpu_sleep();
  }

  if (online) {
    float vbat = read_vbat();
    float pct  = vbat_to_percent(vbat);
    int   rssi = wifi_rssi();
    if (state.baseline_valid) {
      send_boot_notification(vbat, pct, rssi, state.baseline);
    }
    flush_pending_errors(pct, rssi);
    wifi_disconnect();
  }

  bool boot_ok = online && state.baseline_valid && state.pending_error_count == 0;
  uint8_t r_ind = boot_ok ? 0  : 80;
  uint8_t g_ind = boot_ok ? 80 : 0;
  uint8_t b_ind = 0;

  led_show(r_ind, g_ind, b_ind);
  delay(LED_INDICATION_MS);

  Serial.printf("[boot] %lu s upload window — spust ted `pio run -t upload`\n",
                static_cast<unsigned long>(COLD_BOOT_UPLOAD_WINDOW_S));
  for (uint32_t s = 0; s < COLD_BOOT_UPLOAD_WINDOW_S; s++) {
    led_show(r_ind, g_ind, b_ind);
    delay(LED_BLINK_HALF_PERIOD_MS);
    led_off();
    delay(LED_BLINK_HALF_PERIOD_MS);
  }
  led_off();
}

// Pokus o MPU mereni s retry. Vraci true pri uspechu; zaradi chybu do queue
// pri opakovanem selhani. Kazda iterace: wake → measure. Pokud wake nikdy
// neprosel → "nereaguje"; pokud wake prosel ale measure ne → "měření selhalo".
bool measure_with_retry(Vec3& a) {
  bool wake_ever_ok = false;
  for (uint8_t attempt = 1; attempt <= MPU_RETRY_COUNT; attempt++) {
    if (mpu_wake()) {
      wake_ever_ok = true;
      if (mpu_measure_avg(6, a)) {
        if (attempt > 1) Serial.printf("[mpu] OK po %u. pokusu\n", attempt);
        mpu_sleep();
        return true;
      }
    }
    if (attempt < MPU_RETRY_COUNT) {
      Serial.printf("[mpu] retry %u/%u (wake_ok=%d)\n",
                    attempt, MPU_RETRY_COUNT - 1, static_cast<int>(wake_ever_ok));
      delay(MPU_RETRY_DELAY_MS);
    }
  }
  if (wake_ever_ok) {
    mpu_sleep();
    queue_error("Měření selhalo (3 pokusy bez čitelné odpovědi)");
  } else {
    queue_error("Senzor nereaguje (probuzení selhalo po 3 pokusech)");
  }
  return false;
}

// Snapshot rozhodnuti co potrebujeme online. Pocita se za behu wake cyklu na
// zaklade aktualniho stavu door/tilt/cas/error queue.
struct WakeActions {
  bool alert         = false;
  bool heartbeat     = false;
  bool ntp_resync    = false;
  bool error_report  = false;
  bool time_synced   = false;     // doplneno, abychom alert mohli mit "(cas neznamy)" label
};

WakeActions compute_wake_actions(bool measure_ok, bool door_open) {
  WakeActions act;
  act.time_synced = time_is_synced();
  time_t now = 0;
  if (act.time_synced) time(&now);

  // Alert: kdyz dvere otevrene + (nocni okno NEBO cas neni synced fail-open).
  // Fail-open zaruci, ze pri NTP outage user nezustane slepe k vykradeni.
  if (measure_ok && door_open) {
    bool in_alert_window = act.time_synced ? is_night_window() : true;
    if (in_alert_window) {
      if (elapsed_at_least(static_cast<uint64_t>(now), state.last_alert_unix, ALERT_RATE_LIMIT_S)) {
        act.alert = true;
      } else {
        int64_t since = static_cast<int64_t>(now) - static_cast<int64_t>(state.last_alert_unix);
        Serial.printf("[rate] alert suppressed (last %lld s ago)\n", static_cast<long long>(since));
      }
    }
  }

  // Heartbeat: jen kdyz mame cas. >= HEARTBEAT_HOUR misto == zachyti zmeskane
  // wakes. Year-safe dedup pres unix elapsed >= 23h.
  if (act.time_synced && current_hour() >= cfg.heartbeat_hour &&
      elapsed_at_least(static_cast<uint64_t>(now), state.last_heartbeat_unix, HEARTBEAT_MIN_GAP_S)) {
    act.heartbeat = true;
  }

  if (!act.time_synced) {
    act.ntp_resync = true;
  } else if (elapsed_at_least(static_cast<uint64_t>(now), state.last_ntp_unix, NTP_RESYNC_INTERVAL_S)) {
    act.ntp_resync = true;
  }

  if (state.pending_error_count > 0) {
    if (!act.time_synced) {
      act.error_report = (act.alert || act.heartbeat || act.ntp_resync);
    } else if (elapsed_at_least(static_cast<uint64_t>(now), state.last_error_report_unix, ERROR_REPORT_RATE_LIMIT_S)) {
      act.error_report = true;
    }
  }
  return act;
}

void do_wake_cycle() {
  state.boot_count++;
  Serial.printf("=== WAKE #%u ===\n", state.boot_count);

  // 1. MPU mereni
  Vec3 a{};
  bool measure_ok = false;

  if (!state.baseline_valid) {
    Serial.println(F("[warn] baseline not set — vypnout a zapnout pri zavrenych vratech"));
    queue_error("Reference není zachycena (vypnout a zapnout při zavřených vratech)");
  } else {
    mpu_initialize_i2c(SDA_PIN, SCL_PIN);
    measure_ok = measure_with_retry(a);
  }

  // 2. Tilt + hystereze
  float tilt = 0.0f;
  bool  door_open = state.door_was_open;
  if (measure_ok) {
    tilt = angle_deg(a, state.baseline);
    Serial.printf("[mpu] tilt=%.2f° (baseline mag=%.3f, sample mag=%.3f)\n",
                  tilt, vec_mag(state.baseline), vec_mag(a));
    door_open = state.door_was_open
      ? (tilt > TILT_THRESHOLD_CLOSE)
      : (tilt > TILT_THRESHOLD_OPEN);
    state.door_was_open = door_open;
  }

  // 3. Rozhodnuti
  WakeActions act = compute_wake_actions(measure_ok, door_open);

  if (act.time_synced) {
    char timestr[32];
    format_time(timestr, sizeof(timestr), "%H:%M:%S");
    Serial.printf("[time] %s, hour=%d, night_window(%d-%d)=%s\n",
                  timestr, current_hour(),
                  cfg.night_start_hour, cfg.night_end_hour,
                  is_night_window() ? "YES" : "NO");
  } else {
    Serial.println(F("[time] not synced — alert je fail-open"));
  }

  Serial.printf("[state] door=%s tilt=%.1f° alert=%d heart=%d ntp=%d err=%d (pending=%u, wifi_fails=%u)\n",
                door_open ? "OPEN" : "closed", tilt,
                act.alert, act.heartbeat, act.ntp_resync, act.error_report,
                state.pending_error_count, state.wifi_consecutive_fails);

  // 4. WiFi + posli vse
  bool any_send_ok = false;
  bool any_send_attempted = false;

  if (act.alert || act.heartbeat || act.ntp_resync || act.error_report) {
    if (wifi_connect()) {
      state.wifi_consecutive_fails = 0;
      time_t now_unix; time(&now_unix);
      bool ntp_stale = !time_is_synced() ||
                       elapsed_at_least(static_cast<uint64_t>(now_unix), state.last_ntp_unix,
                                        NTP_OPPORTUNISTIC_INTERVAL_S);
      if (ntp_stale) {
        ntp_sync();
      }

      float vbat = read_vbat();
      float pct  = vbat_to_percent(vbat);
      int   rssi = wifi_rssi();

      if (act.alert) {
        any_send_attempted = true;
        if (send_urgent_alert(tilt, pct, act.time_synced)) {
          any_send_ok = true;
          time_t n; time(&n); state.last_alert_unix = n;
        }
      }
      if (act.heartbeat) {
        any_send_attempted = true;
        if (send_heartbeat(door_open, tilt, vbat, pct, rssi)) {
          any_send_ok = true;
          time_t n; time(&n); state.last_heartbeat_unix = n;
        }
      }
      if (act.error_report) {
        any_send_attempted = true;
        uint32_t before = state.pending_error_count;
        flush_pending_errors(pct, rssi);
        if (state.pending_error_count < before) any_send_ok = true;
      }
      wifi_disconnect();
    } else {
      Serial.println(F("[wifi] connect fail — vse skipnuto, errors zustavaji v queue"));
      state.wifi_consecutive_fails++;
      any_send_attempted = true;
      // Visible reporting: po N po sobe failnutych connect attemptu zarad
      // do queue, aby uzivatel po reconnectu uvidel "byl jsi offline".
      // Queue jen jednou na threshold — counter sice roste dal, ale msg
      // se zaktualizuje s aktualnim poctem.
      if (state.wifi_consecutive_fails >= WIFI_FAIL_REPORT_THRESHOLD) {
        char msg[80];
        snprintf(msg, sizeof(msg),
                 "WiFi nedostupné %u× po sobě — zařízení bylo offline",
                 state.wifi_consecutive_fails);
        queue_error(msg);
      }
    }
  }

  // 5. LED
  bool wake_error = (state.pending_error_count > 0)
                 || (any_send_attempted && !any_send_ok);
  if (act.alert && any_send_ok) {
    led_show(80, 0, 80);   // fialova = alert OK
    delay(LED_INDICATION_MS);
    led_off();
  } else if (wake_error) {
    led_show(80, 0, 0);    // cervena = chyba
    delay(LED_INDICATION_MS);
    led_off();
  }
}

// Brownout / panic detection — pokud RTC mem prezila (state.magic OK), ale
// reset reason je necekany, zaloguj a zarad chybu do queue. Cold boot rutina
// na to nepostouchne.
void check_unexpected_reset() {
  esp_reset_reason_t r = esp_reset_reason();
  const char* label = nullptr;
  switch (r) {
    case ESP_RST_BROWNOUT: label = "Brownout reset (slabá baterie?)"; break;
    case ESP_RST_PANIC:    label = "Firmware panic (exception)"; break;
    case ESP_RST_INT_WDT:  label = "Interrupt watchdog reset"; break;
    case ESP_RST_TASK_WDT: label = "Task watchdog reset"; break;
    default: return;       // POWERON / EXT / SW / DEEPSLEEP — vse OK
  }
  Serial.printf("[boot] necekany reset: %s\n", label);
  queue_error(label);
}

}  // namespace

// Double-RST detection. Counter zije v RTC mem (prezije RST) a inkrementuje
// se pri kazdem ne-timer setupu (= RST nebo true cold boot). Pri deep sleep
// wake se nuluje (stabilni provoz). Pokud counter dosahne DOUBLE_RST_THRESHOLD
// v okne DOUBLE_RST_WINDOW_US, je to user-trigger pro AP mode.
//
// Pri prvnim ever bootu (magic invalid) ma RTC mem nahodny obsah → musime
// fields explicitne inicializovat pred ctenim.
bool detect_double_rst(esp_sleep_wakeup_cause_t cause, bool true_cold_boot) {
  if (true_cold_boot) {
    state.last_cold_boot_rtc_us = 0;
    state.rapid_reset_count = 0;
  }
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    state.rapid_reset_count = 0;
    return false;
  }
  // RTC slow clock prezije RST (pouze power-off ho resetuje). Pro mereni gap
  // mezi RST-y vyrobime us z slow clock cycles × period.
  uint64_t now_us = rtc_time_slowclk_to_us(rtc_time_get(), rtc_clk_slow_freq_get_hz());
  uint64_t since  = now_us - state.last_cold_boot_rtc_us;
  if (state.last_cold_boot_rtc_us > 0 && since < DOUBLE_RST_WINDOW_US) {
    state.rapid_reset_count++;
  } else {
    state.rapid_reset_count = 1;
  }
  state.last_cold_boot_rtc_us = now_us;
  Serial.printf("[boot] rapid_reset_count=%u (since=%llu ms)\n",
                state.rapid_reset_count, (unsigned long long)(since / 1000));
  if (state.rapid_reset_count >= DOUBLE_RST_THRESHOLD) {
    state.rapid_reset_count = 0;   // neopakovat AP pri dalsim cold bootu
    return true;
  }
  return false;
}

// External AP trigger: pri propojeni AP_TRIGGER_PIN_IN ↔ AP_TRIGGER_PIN_GND
// pri startu vraci true. Po check uvolni oba piny (INPUT high-Z).
bool detect_external_ap_trigger() {
  pinMode(AP_TRIGGER_PIN_GND, OUTPUT);
  digitalWrite(AP_TRIGGER_PIN_GND, LOW);
  pinMode(AP_TRIGGER_PIN_IN, INPUT_PULLUP);
  delay(5);   // settle pullup vs. driven LOW
  bool triggered = (digitalRead(AP_TRIGGER_PIN_IN) == LOW);
  pinMode(AP_TRIGGER_PIN_GND, INPUT);    // release
  pinMode(AP_TRIGGER_PIN_IN, INPUT);     // release (pullup off)
  return triggered;
}

void setup() {
  periph_power_on();
  led_initialize();
  adc_initialize();

  // External AP trigger check — pred Serial init, rychly. Kontrolujeme jen pri
  // cold bootu / RST, ne pri deep sleep wake — kdyby uzivatel zapomnel propojku
  // odstranit, kazdy wake by skocil do AP a baterii by to vycerpalo.
  esp_sleep_wakeup_cause_t cause_early = esp_sleep_get_wakeup_cause();
  bool ext_ap_trigger = false;
  if (cause_early != ESP_SLEEP_WAKEUP_TIMER) {
    ext_ap_trigger = detect_external_ap_trigger();
  }

  Serial.begin(115200);
  delay(SERIAL_INIT_DELAY_MS);
  Serial.println();

  if (ext_ap_trigger) {
    Serial.println(F("[boot] >>> external AP trigger (GPIO20↔GPIO21 short) <<<"));
  }

  // TZ env var se nepřežije deep sleep — nastavit pri kazdem wake.
  setenv("TZ", TZ_CZ, 1);
  tzset();

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  bool true_cold_boot = (state.magic != STATE_MAGIC);

  bool double_rst = detect_double_rst(cause, true_cold_boot);

  Serial.printf("[boot] cause=%d cold_boot=%d magic=0x%08X\n",
                static_cast<int>(cause), static_cast<int>(true_cold_boot),
                static_cast<unsigned>(state.magic));

  // Nacti NVS config (defaults z config.h / secrets.h pro nenalezene klice).
  cfg_load();

  bool force_ap = double_rst || ext_ap_trigger;
  if (force_ap) {
    if (double_rst)     Serial.println(F("[boot] DOUBLE-RST trigger"));
    if (ext_ap_trigger) Serial.println(F("[boot] external GPIO trigger"));
    Serial.println(F("[boot] mazu NVS config a startuji AP mode"));
    cfg_clear();
    cfg_load();   // znovu nacti = vse defaults
  }

  if (!cfg.configured) {
    Serial.println(F("[boot] NVS prazdne (nebo smazano) → vstupuji do AP modu"));
    run_ap_mode();
    // run_ap_mode bud restartne (po save) nebo deep-sleep (po timeoutu) —
    // sem se nedostaneme. Return je formalita pro toolchain.
    return;
  }

  Serial.printf("[boot] NVS configured (ssid='%s', topic='%s') → normalni boot\n",
                cfg.wifi_ssid, cfg.ntfy_topic);

  if (true_cold_boot) {
    do_cold_boot();
  } else {
    check_unexpected_reset();
    if (cause != ESP_SLEEP_WAKEUP_TIMER) {
      Serial.printf("[boot] cause=%d, RTC mem valid → continuing as wake\n",
                    static_cast<int>(cause));
    }
    do_wake_cycle();
  }

  enter_deep_sleep();
}

void loop() {
  // V produkci nedosazitelne (deep sleep nevrati). V debug modu
  // (DEEP_SLEEP_ENABLED=false) simulujeme wake cyklus: delay misto sleep,
  // pak do_wake_cycle a opet enter_deep_sleep (ktery se v debug rezimu
  // hned vrati). State preziva vse v RAM, RTC mem netreba.
  if (DEEP_SLEEP_ENABLED) return;

  delay(static_cast<uint32_t>(cfg.wake_interval_s) * 1000UL);
  Serial.println(F("[boot] [DEBUG simulated] woke up"));
  do_wake_cycle();
  enter_deep_sleep();
}
