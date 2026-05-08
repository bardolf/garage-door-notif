// Garage door notif — finalni integrovana aplikace
//
// Architektura:
//   COLD BOOT (power-on / RST):
//     1. WiFi connect + NTP sync
//     2. MPU6050 baseline capture (50 vzorku, prumer)
//     3. Boot notifikace na ntfy.sh (cas, baterka %, WiFi signal, baseline)
//     4. ESP+MPU sleep, deep sleep WAKE_INTERVAL_S sekund
//
//   WAKE (z deep sleep, kazdych 5 min):
//     1. Wake MPU pres I2C, mereni tilt vs baseline (3 retry pokusy proti glitchum)
//     2. Hystereze: open >60°, close <40° (drzime stav v RTC mem)
//     3. Logika podle casu:
//        - 22:00-06:00 + tilt open + nebyl alert v >4 min → URGENT alert
//        - 21:00 hodina + heartbeat dnes jeste neodeslan → daily heartbeat
//        - posledni NTP sync >24h → forced refresh
//        - MPU error queue ma chyby + rate limit OK → error report
//     4. Pokud potrebujeme WiFi: connect → opportunistic NTP (1h threshold) → poslat
//     5. ESP+MPU sleep, deep sleep WAKE_INTERVAL_S sekund
//
// State persistence pres deep sleep — RTC slow memory (RTC_DATA_ATTR):
//   - boot_count, last_alert_unix, last_heartbeat_yday
//   - baseline accel vector (cold boot capture)
//   - door_was_open (hysteresis)
//
// Time tracking: ESP32 RTC zachovava system time pres deep sleep.
//   time() / localtime() funguji bez extra logiky.
//   Drift FireBeetle s krystalem ~20 ppm = ~2 s/den, NTP refresh 1×/den staci.

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <math.h>
#include "lwip/dns.h"
#include "esp_sleep.h"
#include "secrets.h"

// ===== Hardware konfigurace =====
#define VBAT_PIN 34
#define LED_PIN  2
#define SDA_PIN  21
#define SCL_PIN  22

// ===== Logicka konfigurace =====
static const uint32_t WAKE_INTERVAL_S        = 300;     // 5 min
static const float    TILT_THRESHOLD_OPEN    = 60.0f;   // dle README.md (sekce Architektura)
static const float    TILT_THRESHOLD_CLOSE   = 40.0f;   // hystereze
static const int      NIGHT_START_HOUR       = 22;
static const int      NIGHT_END_HOUR         = 6;
static const int      HEARTBEAT_HOUR         = 21;
// Cilove chovani: alert pri kazdem wake cyklu, pokud jsou vrata otevrena v nocnim okne.
// Limit 4 min (< WAKE_INTERVAL_S = 5 min) — zarucuje ze elapsed cas mezi alerty
// vzdy >= limit, i s drobnym jitterem v okamziku zapisu last_alert_unix.
static const uint32_t ALERT_RATE_LIMIT_S        = 4 * 60;
// Stejny princip pro error reporty — kdyz je MPU broken, user dostane upozorneni
// pri kazdem wake (= cca kazdych 5 min) dokud problem nezmizi.
static const uint32_t ERROR_REPORT_RATE_LIMIT_S = 4 * 60;
// NTP forced refresh: kdyz uz nesyncovalo dyl nez 24 h, je to povinne (drift safety net).
static const uint32_t NTP_RESYNC_INTERVAL_S         = 24 * 3600;
// NTP opportunistic refresh: pokud uz mame WiFi z jineho duvodu (alert/heartbeat/error),
// syncneme zadarmo, pokud poslední sync byl pred vic nez timto cisem. FireBeetle ESP32-E
// pouziva interni RC oscilator pro RTC_SLOW_CLK (ne 32 kHz krystal) → drift 2–5 % =
// 5–15 s na 5min wake cyklus. Threshold 10 min limituje drift na ~30 s mezi syncy.
static const uint32_t NTP_OPPORTUNISTIC_INTERVAL_S  = 600;      // 10 min
static const uint32_t WIFI_TIMEOUT_MS               = 15000;
static const uint32_t NTP_TIMEOUT_MS                = 10000;

// MPU retry: I2C glitch / dotcasna nestabilita -> pokus znova nez ohlasime chybu.
// Total: MPU_RETRY_COUNT pokusu (1 + N-1 retry) s MPU_RETRY_DELAY_MS mezi nimi.
static const uint8_t  MPU_RETRY_COUNT      = 3;
static const uint16_t MPU_RETRY_DELAY_MS   = 100;

// CZ timezone — automaticky resi CET/CEST DST
static const char* TZ_CZ = "CET-1CEST,M3.5.0,M10.5.0/3";
static const char* NTP_SERVERS[] = {"cz.pool.ntp.org", "pool.ntp.org", "time.google.com"};

// ===== MPU6050 =====
static const uint8_t MPU_ADDR         = 0x68;
static const uint8_t REG_PWR_MGMT_1   = 0x6B;
static const uint8_t REG_ACCEL_XOUT_H = 0x3B;
static const uint8_t REG_WHO_AM_I     = 0x75;
static const float   ACCEL_LSB_PER_G  = 16384.0f;

// ===== Persistovany stav (RTC slow memory) =====
struct Vec3 { float x, y, z; };

// Magic sentinel — overuje ze RTC mem byla inicializovana cold-boot rutinou.
// Pri brownout / WDT reset RTC mem se zachovat, ale `esp_sleep_get_wakeup_cause()`
// vraci UNDEFINED (jako pri cold boot). Bez sentinelu bychom prepisovali platnou
// state baseline. Magic = libovolny "unlikely random" 32-bit pattern.
static const uint32_t STATE_MAGIC = 0xC0FFEE42;

RTC_DATA_ATTR struct {
  uint32_t magic;                     // STATE_MAGIC pokud je state platny
  uint32_t boot_count;
  uint64_t last_alert_unix;
  int      last_heartbeat_yday;       // -1 = nikdy
  uint64_t last_ntp_unix;
  Vec3     baseline;
  bool     baseline_valid;
  bool     door_was_open;
  // Error reporting — queue chyb co se nepovedlo poslat hned (typicky MPU fail
  // na pozadi bez WiFi). Posle se na nejblizsim uspesnem WiFi connectu.
  uint32_t pending_error_count;
  uint64_t last_error_report_unix;
  char     pending_error_msg[96];     // posledni typ chyby (lidsky citelny)
} state;

// Bezpecny rate-limit check — pokud cas selne (clock walked back po NTP correction),
// signed elapsed vyjde negativne, nikoli unsigned wrap. Conservative — drzi limit.
static bool elapsed_at_least(uint64_t now, uint64_t since, uint32_t threshold_s) {
  int64_t elapsed = (int64_t)now - (int64_t)since;
  return elapsed >= (int64_t)threshold_s;
}

// ===== Helpers — baterka =====

// LiPo discharge krivka pri lehkem zatezi (ESP v sleep).
// Hodnoty jsou typicke pro single-cell LiPo / 18650.
static float vbat_to_percent(float vbat) {
  static const struct { float v; float p; } curve[] = {
    {4.20f, 100.0f}, {4.10f, 95.0f}, {4.00f, 85.0f}, {3.90f, 70.0f},
    {3.80f, 55.0f}, {3.70f, 35.0f}, {3.60f, 20.0f}, {3.50f, 12.0f},
    {3.40f,   5.0f}, {3.30f,  2.0f}, {3.20f,  0.0f}
  };
  const int N = sizeof(curve) / sizeof(curve[0]);
  if (vbat >= curve[0].v)     return 100.0f;
  if (vbat <= curve[N-1].v)   return 0.0f;
  for (int i = 0; i < N - 1; i++) {
    if (vbat <= curve[i].v && vbat >= curve[i+1].v) {
      float t = (vbat - curve[i+1].v) / (curve[i].v - curve[i+1].v);
      return curve[i+1].p + t * (curve[i].p - curve[i+1].p);
    }
  }
  return 0.0f;
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
  return (sum_mv / (float)N) / 1000.0f * 2.0f;
}

// ===== Helpers — WiFi signal (signal je rod muzsky) =====
static const char* rssi_label(int rssi) {
  if (rssi >= -60) return "vyborny";
  if (rssi >= -70) return "dobry";
  if (rssi >= -80) return "ok";
  if (rssi >= -90) return "slaby";
  return "velmi slaby";
}

// ===== Helpers — MPU6050 =====
static bool mpu_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool mpu_wake_chip() {
  if (!mpu_write(REG_PWR_MGMT_1, 0x00)) return false;
  delay(30);  // stabilizace oscilatoru
  return true;
}

static bool mpu_sleep_chip() {
  return mpu_write(REG_PWR_MGMT_1, 0x40);
}

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

// Prumer N vzorku v g; prvni vzorek se zahodi (po wake byva outlier).
static bool mpu_measure_avg(uint8_t n_samples, Vec3 &out) {
  if (n_samples < 2) return false;
  float sx = 0, sy = 0, sz = 0;
  uint8_t valid = 0;
  for (uint8_t i = 0; i < n_samples; i++) {
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
  out.x = sx / valid; out.y = sy / valid; out.z = sz / valid;
  return true;
}

static float vec_mag(const Vec3 &v) {
  return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
}

// 3D uhel mezi dvema accel vektory (v stupnich).
static float angle_deg(const Vec3 &a, const Vec3 &b) {
  float ma = vec_mag(a), mb = vec_mag(b);
  // NaN guard — pokud je RTC mem corrupted (NaN baseline), netekej dale do acosf.
  if (isnan(ma) || isnan(mb) || ma < 1e-6f || mb < 1e-6f) return 0;
  float c = (a.x*b.x + a.y*b.y + a.z*b.z) / (ma * mb);
  if (isnan(c)) return 0;
  if (c > 1.0f)  c = 1.0f;
  if (c < -1.0f) c = -1.0f;
  return acosf(c) * 180.0f / (float)PI;
}

// ===== WiFi / NTP / ntfy =====
static bool wifi_connect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[wifi] %s ", WIFI_SSID);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[wifi] FAIL"));
    return false;
  }
  Serial.printf("[wifi] OK ip=%s rssi=%d\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  // DNS override — local resolver casto neprekladá public domeny
  ip_addr_t cf, ggl;
  IP_ADDR4(&cf,  1, 1, 1, 1);
  IP_ADDR4(&ggl, 8, 8, 8, 8);
  dns_setserver(0, &cf);
  dns_setserver(1, &ggl);
  return true;
}

static void wifi_disconnect() {
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
}

static bool ntp_sync() {
  Serial.print(F("[ntp] sync ... "));
  configTzTime(TZ_CZ, NTP_SERVERS[0], NTP_SERVERS[1], NTP_SERVERS[2]);
  uint32_t start = millis();
  time_t now = 0;
  while ((millis() - start) < NTP_TIMEOUT_MS) {
    time(&now);
    if (now > 1700000000) {  // sanity: po 2023-11-15
      char buf[32];
      struct tm tm_local;
      localtime_r(&now, &tm_local);
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &tm_local);
      Serial.printf("OK -> %s\n", buf);
      state.last_ntp_unix = now;
      return true;
    }
    delay(200);
  }
  Serial.println(F("TIMEOUT"));
  return false;
}

static bool ntfy_send(const char* title, const char* body,
                      const char* priority, const char* tags) {
  HTTPClient http;
  String url = String("http://ntfy.sh/") + NTFY_TOPIC;
  http.begin(url);
  http.addHeader("Content-Type", "text/plain; charset=utf-8");
  if (title)    http.addHeader("Title",    title);
  if (priority) http.addHeader("Priority", priority);
  if (tags)     http.addHeader("Tags",     tags);
  int code = http.POST((uint8_t*)body, strlen(body));
  http.end();
  Serial.printf("[ntfy] '%s' -> HTTP %d\n", title, code);
  return code >= 200 && code < 300;  // accept all 2xx (ntfy obvykle vraci 200, ale 202 je taky valid)
}

// ===== Time helpers =====
static bool time_is_synced() {
  time_t now;
  time(&now);
  return now > 1700000000;
}

static void format_time(char* buf, size_t buf_size, const char* fmt) {
  time_t now;
  time(&now);
  struct tm tm_local;
  localtime_r(&now, &tm_local);
  strftime(buf, buf_size, fmt, &tm_local);
}

static int current_hour() {
  time_t now;
  time(&now);
  struct tm tm_local;
  localtime_r(&now, &tm_local);
  return tm_local.tm_hour;
}

static int current_yday() {
  time_t now;
  time(&now);
  struct tm tm_local;
  localtime_r(&now, &tm_local);
  return tm_local.tm_yday;
}

static bool is_night_window() {
  int h = current_hour();
  return (h >= NIGHT_START_HOUR || h < NIGHT_END_HOUR);
}

// ===== Notifikacni zpravy =====
static void send_boot_notification(float vbat, float vbat_pct, int rssi, float baseline_mag) {
  char timestr[32];
  format_time(timestr, sizeof(timestr), "%H:%M:%S");
  char body[256];
  snprintf(body, sizeof(body),
    "Cas: %s\n"
    "Baterka: %.0f %% (%.2f V)\n"
    "Signal: %s (%d dBm)\n"
    "Sensor: baseline OK (|a|=%.3f g)",
    timestr, vbat_pct, vbat, rssi_label(rssi), rssi, baseline_mag);
  ntfy_send("Hlidac garazovych vrat aktivni", body, "default", "rocket");
}

static void send_urgent_alert(float tilt, float vbat_pct) {
  char timestr[32];
  format_time(timestr, sizeof(timestr), "%H:%M");
  char body[256];
  snprintf(body, sizeof(body),
    "Cas: %s\n"
    "Naklon: %.0f° (limit %.0f°)\n"
    "Baterka: %.0f %%",
    timestr, tilt, TILT_THRESHOLD_OPEN, vbat_pct);
  ntfy_send("GARAZ OTEVRENA!", body, "urgent", "rotating_light");
}

static void send_heartbeat(bool door_open, float tilt, float vbat, float vbat_pct, int rssi) {
  char timestr[32];
  format_time(timestr, sizeof(timestr), "%H:%M");
  const char* tag = (vbat_pct > 50) ? "white_check_mark"
                  : (vbat_pct > 20) ? "yellow_circle"
                                    : "battery";
  char body[256];
  snprintf(body, sizeof(body),
    "Cas: %s\n"
    "Stav: %s\n"
    "Naklon: %.0f°\n"
    "Baterka: %.0f %% (%.2f V)\n"
    "Signal: %s (%d dBm)",
    timestr,
    door_open ? "OTEVRENO" : "ZAVRENO",
    tilt, vbat_pct, vbat, rssi_label(rssi), rssi);
  ntfy_send("Garaz - denni kontrola", body, "low", tag);
}

// Zaznamena chybu do RTC mem queue — odesle se na nejblizsim WiFi connectu.
// Idempotentni vuci stejnemu typu chyby (jen inkrementuje counter).
static void queue_error(const char* msg) {
  state.pending_error_count++;
  strncpy(state.pending_error_msg, msg, sizeof(state.pending_error_msg) - 1);
  state.pending_error_msg[sizeof(state.pending_error_msg) - 1] = '\0';
  Serial.printf("[error] queued: %s (total pending=%u)\n",
                msg, state.pending_error_count);
}

// Posle souhrn pendingovanych chyb. Predpoklada ze WiFi je up.
// Po uspesnem odeslani vyresetuje queue.
static void flush_pending_errors(float vbat_pct, int rssi) {
  if (state.pending_error_count == 0) return;

  char timestr[32] = "(cas neznamy)";
  if (time_is_synced()) format_time(timestr, sizeof(timestr), "%H:%M");

  // Reassurance text — pomáhá uživateli rozhodnout jestli má panikařit.
  // Jednorazova chyba (i po retry) muze byt napajeci spike, mechanicky otres,
  // dotcasna instabilita kabelu. Opakovane chyby = realny problem (kabel,
  // sensor mrtvy, baterka pod limitem napajeni MPU).
  const char* reassurance = (state.pending_error_count == 1)
    ? "Jednorazova chyba je obvykle OK (I2C glitch, vibrace).\n"
      "Pokud chyba pokracuje v dalsich kontrolach, zkontroluj kabely / sensor."
    : "Chyby se opakuji - pravdepodobne realny problem.\n"
      "Zkontroluj zapojeni MPU (4 draty), pripadne vymen sensor.";

  char body[384];
  snprintf(body, sizeof(body),
    "Cas: %s\n"
    "Pocet chyb od posledniho hlaseni: %u\n"
    "Posledni: %s\n"
    "Baterka: %.0f %%\n"
    "Signal: %s (%d dBm)\n"
    "\n"
    "%s",
    timestr, state.pending_error_count, state.pending_error_msg,
    vbat_pct, rssi_label(rssi), rssi,
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

// ===== Hlavni flow =====
static void enter_deep_sleep() {
  digitalWrite(LED_PIN, LOW);
  Serial.printf("[sleep] deep sleep %lu s\n", (unsigned long)WAKE_INTERVAL_S);
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)WAKE_INTERVAL_S * 1000000ULL);
  esp_deep_sleep_start();
}

static void do_cold_boot() {
  Serial.println(F("=== COLD BOOT ==="));
  state.magic                  = STATE_MAGIC;       // mark RTC mem as initialized
  state.boot_count             = 1;
  state.last_alert_unix        = 0;
  state.last_heartbeat_yday    = -1;
  state.last_ntp_unix          = 0;
  state.baseline_valid         = false;
  state.door_was_open          = false;
  state.pending_error_count    = 0;
  state.last_error_report_unix = 0;
  state.pending_error_msg[0]   = '\0';

  // 1. WiFi + NTP
  bool online = wifi_connect();
  if (online) ntp_sync();

  // 2. Baseline capture
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);
  if (!mpu_wake_chip()) {
    Serial.println(F("[fatal] MPU not responding"));
    queue_error("MPU sensor neodpovida na I2C (zkontroluj zapojeni)");
  } else {
    Vec3 base;
    if (mpu_measure_avg(50, base)) {
      state.baseline       = base;
      state.baseline_valid = true;
      Serial.printf("[mpu] baseline ax=%+.3f ay=%+.3f az=%+.3f |a|=%.3f g\n",
                    base.x, base.y, base.z, vec_mag(base));
    } else {
      Serial.println(F("[mpu] baseline measure FAIL"));
      queue_error("MPU baseline mereni selhalo (sensor nereaguje stabilne)");
    }
    mpu_sleep_chip();
  }

  // 3. Notifikace — bud boot notif (happy path) nebo error report (problem path)
  if (online) {
    float vbat = read_vbat();
    float pct  = vbat_to_percent(vbat);
    int   rssi = WiFi.RSSI();

    if (state.baseline_valid) {
      send_boot_notification(vbat, pct, rssi, vec_mag(state.baseline));
    }
    // Pokud chyby cekaji, posli error report (i kdyz boot prosel — alespon
    // user vi ze baseline failed atd.)
    flush_pending_errors(pct, rssi);

    wifi_disconnect();
  }
  // Bez WiFi — chyby zustavaji v queue, posle se na prvnim uspesnem connectu
}

static void do_wake_cycle() {
  state.boot_count++;
  Serial.printf("=== WAKE #%u ===\n", state.boot_count);

  // 1. MPU mereni — chyby se queueuji misto silent return,
  //    aby se daly ohlasit pres WiFi v zaverecnem kroku.
  Vec3 a = {0, 0, 0};
  bool measure_ok = false;

  if (!state.baseline_valid) {
    Serial.println(F("[warn] baseline not set — RST + cold boot needed"));
    queue_error("Baseline neni zachycen (potreba RST tlacitko)");
  } else {
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000);

    // Retry smyčka: sensor občas vrátí chybu z důvodu I²C glitche, pokus znova.
    bool wake_ever_ok = false;
    for (uint8_t attempt = 1; attempt <= MPU_RETRY_COUNT; attempt++) {
      if (!mpu_wake_chip()) {
        if (attempt < MPU_RETRY_COUNT) {
          Serial.printf("[mpu] wake fail, retry %u/%u za %u ms\n",
                        attempt, MPU_RETRY_COUNT - 1, MPU_RETRY_DELAY_MS);
          delay(MPU_RETRY_DELAY_MS);
        }
        continue;
      }
      wake_ever_ok = true;
      if (mpu_measure_avg(6, a)) {
        measure_ok = true;
        if (attempt > 1) {
          Serial.printf("[mpu] OK po %u. pokusu\n", attempt);
        }
        break;
      }
      // Wake prošel, ale measure failed → další pokus (může být dočasná instabilita)
      if (attempt < MPU_RETRY_COUNT) {
        Serial.printf("[mpu] measure fail, retry %u/%u za %u ms\n",
                      attempt, MPU_RETRY_COUNT - 1, MPU_RETRY_DELAY_MS);
        delay(MPU_RETRY_DELAY_MS);
      }
    }

    if (!measure_ok) {
      if (!wake_ever_ok) {
        Serial.printf("[mpu] wake selhal po %u pokusech\n", MPU_RETRY_COUNT);
        queue_error("MPU sensor neodpovida (probuzeni selhalo po 3 pokusech)");
      } else {
        Serial.printf("[mpu] measure selhal po %u pokusech\n", MPU_RETRY_COUNT);
        queue_error("MPU mereni selhalo (po 3 pokusech)");
      }
    }
    // Best effort — pokud wake někdy prošel, MPU je v active modu, vrátit do sleep.
    if (wake_ever_ok) mpu_sleep_chip();
  }

  // 2. Tilt + hystereze (jen pokud mereni proslo)
  float tilt = 0.0f;
  bool  door_open = state.door_was_open;  // zachova posledni znamy stav pri fail
  if (measure_ok) {
    tilt = angle_deg(a, state.baseline);
    Serial.printf("[mpu] tilt=%.2f° (baseline mag=%.3f, sample mag=%.3f)\n",
                  tilt, vec_mag(state.baseline), vec_mag(a));
    door_open = state.door_was_open
      ? (tilt > TILT_THRESHOLD_CLOSE)
      : (tilt > TILT_THRESHOLD_OPEN);
    state.door_was_open = door_open;
  }

  // 3. Casova logika — co potrebujeme online
  bool need_alert        = false;
  bool need_heartbeat    = false;
  bool need_ntp          = false;
  bool need_error_report = false;

  if (time_is_synced()) {
    time_t now; time(&now);
    if (measure_ok && door_open && is_night_window()) {
      if (elapsed_at_least((uint64_t)now, state.last_alert_unix, ALERT_RATE_LIMIT_S)) {
        need_alert = true;
      } else {
        int64_t since = (int64_t)now - (int64_t)state.last_alert_unix;
        Serial.printf("[rate] alert suppressed (last %lld s ago)\n", (long long)since);
      }
    }
    int yday = current_yday();
    if (current_hour() == HEARTBEAT_HOUR && state.last_heartbeat_yday != yday) {
      need_heartbeat = true;
    }
    if (elapsed_at_least((uint64_t)now, state.last_ntp_unix, NTP_RESYNC_INTERVAL_S)) {
      need_ntp = true;
    }
  } else {
    Serial.println(F("[time] not synced, will try NTP"));
    need_ntp = true;
  }

  // Error report: pendingove chyby + rate limit dodrzeny
  if (state.pending_error_count > 0) {
    if (!time_is_synced()) {
      // Bez synchronizovaneho casu nemuzeme rate-limitovat. Posli pri prvni
      // prilezitosti (kdyz bude WiFi z nejakeho jineho duvodu).
      need_error_report = (need_alert || need_heartbeat || need_ntp);
    } else {
      time_t now; time(&now);
      if (elapsed_at_least((uint64_t)now, state.last_error_report_unix, ERROR_REPORT_RATE_LIMIT_S)) {
        need_error_report = true;
      }
    }
  }

  // Diagnostika: aktualni cas + jestli je v nocnim okne (pomáha při ladění alertů)
  if (time_is_synced()) {
    char timestr[32];
    format_time(timestr, sizeof(timestr), "%H:%M:%S");
    Serial.printf("[time] %s, hour=%d, night_window(%d-%d)=%s\n",
                  timestr, current_hour(),
                  NIGHT_START_HOUR, NIGHT_END_HOUR,
                  is_night_window() ? "YES" : "NO");
  }

  Serial.printf("[state] door=%s tilt=%.1f° alert=%d heart=%d ntp=%d err=%d (pending=%u)\n",
                door_open ? "OPEN" : "closed", tilt,
                need_alert, need_heartbeat, need_ntp, need_error_report,
                state.pending_error_count);

  // 4. WiFi connect a posli vse najednou
  if (need_alert || need_heartbeat || need_ntp || need_error_report) {
    if (wifi_connect()) {
      // Opportunistic NTP — sync jen pokud uplynul rozumny cas od posledniho.
      // Zarucuje cas pravidelne aktualizovan i bez forced 24h refreshe, ale
      // nezatezuje NTP servery pri castych alertech (kazdych 5 min).
      time_t now_unix; time(&now_unix);
      bool ntp_stale = !time_is_synced() ||
                       elapsed_at_least((uint64_t)now_unix, state.last_ntp_unix,
                                        NTP_OPPORTUNISTIC_INTERVAL_S);
      if (ntp_stale) {
        ntp_sync();
      } else {
        int64_t since = (int64_t)now_unix - (int64_t)state.last_ntp_unix;
        Serial.printf("[ntp] sync skipped (last %lld s ago, threshold %lu s)\n",
                      (long long)since,
                      (unsigned long)NTP_OPPORTUNISTIC_INTERVAL_S);
      }

      float vbat = read_vbat();
      float pct  = vbat_to_percent(vbat);
      int   rssi = WiFi.RSSI();

      if (need_alert) {
        send_urgent_alert(tilt, pct);
        time_t now; time(&now);
        state.last_alert_unix = now;
      }
      if (need_heartbeat) {
        send_heartbeat(door_open, tilt, vbat, pct, rssi);
        state.last_heartbeat_yday = current_yday();
      }
      // Errors az nakonec — chceme aby user dostal nejdriv urgent alert,
      // pak heartbeat, pak diagnostiku.
      if (need_error_report) {
        flush_pending_errors(pct, rssi);
      }
      wifi_disconnect();
    } else {
      Serial.println(F("[wifi] connect fail — vse skipnuto, errors zustavaji v queue"));
    }
  }
}

void setup() {
  // LED ON ihned — vizualni indikator wake (pred Serial init).
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  Serial.begin(115200);
  delay(300);
  Serial.println();

  // POZOR: TZ env var se nepřežije deep sleep, musíme ji nastavit při každém wake.
  // ESP32 RTC zachovává unix čas, ale localtime_r() bez TZ defaultuje na UTC →
  // is_night_window() pak srovnává UTC hodinu místo CET/CEST → alert silently skip.
  setenv("TZ", TZ_CZ, 1);
  tzset();

  // Cold-boot detekce robustne — kombinace wake cause + RTC mem magic sentinel.
  // - cause == TIMER + magic OK → normalni wake z deep sleep
  // - cause != TIMER + magic OK → unexpected reset (brownout, WDT, RST), ale state
  //   v RTC mem je validni → continue jako wake (zachovat baseline)
  // - magic NOT ok → opravdu prvni cold boot (power on / EN reset / WDT s power-off)
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  bool magic_valid = (state.magic == STATE_MAGIC);
  bool true_cold_boot = !magic_valid;

  if (true_cold_boot) {
    do_cold_boot();
  } else {
    if (cause != ESP_SLEEP_WAKEUP_TIMER) {
      Serial.printf("[boot] unexpected reset cause=%d, RTC mem valid → continuing as wake\n",
                    (int)cause);
    }
    do_wake_cycle();
  }

  enter_deep_sleep();
  // za enter_deep_sleep() se nikdy nedostaneme — wake = restart, znova setup()
}

void loop() {
  // never reached
}
