// Garage door PoC — power-mereni debug firmware pro LaskaKit ESP32-C3-LPKit v4.
// Cil: cyklit aktivni a deep-sleep fazi tak, aby slo multimetrem prepnout
// rozsah a zmerit obe — peak i klid.
//
// Cyklus (opakuje se):
//   1. wake (cold boot nebo timer)
//   2. PERIPH_EN=HIGH, NeoPixel cerveny  ← vizualni indikace active
//   3. WiFi connect + DNS override + ntfy POST  ← peak ~40+ mA (cca 2-4 s)
//   4. NeoPixel zeleny krátce, pak off
//   5. AWAKE_HOLD_MS (CPU bezi, WiFi off, NeoPixel off) ← cas pro multimeter readout active
//   6. PERIPH_EN=LOW + hold, deep sleep SLEEP_DURATION_S
//      → behem 30 s mas cas prepnout multimeter na nizsi rozsah a zmerit sleep
//   7. wake → goto 1
//
// Dvojity log: USB-CDC (Serial, pres USB-C) + UART0 (Serial0, GPIO20=RX, GPIO21=TX).
// Pri vyvoji: pripoj USB-C → log jde po USB-CDC.
// Pri mereni proudu: USB odpojen, baterie napaji desku, externi USB-UART adapter
// pripojen na GPIO20/21/GND (jen TX/RX/GND — NEPRIPOJUJ adapter VCC, jinak by
// back-poweroval desku a mereni proudu z baterie by bylo nesmysl).
//
// pio run -e c3power -t upload -t monitor

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_NeoPixel.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "lwip/dns.h"

// ---- WiFi/ntfy (sdileno s debug/ntfy) ----
static const char* WIFI_SSID  = "skybit.cz.guest";
static const char* WIFI_PASS  = "vitejteunemcu";
static const char* NTFY_HOST  = "ntfy.sh";
static const char* NTFY_TOPIC = "milan-garaz-2026-x8kf3pq7vn";

// ---- Casovani ----
static const uint64_t SLEEP_DURATION_S = 30;    // okno na prepnuti multimeter rozsahu
static const uint32_t AWAKE_HOLD_MS    = 3000;  // ustaleny active proud po WiFi disconnectu
static const uint32_t WIFI_TIMEOUT_MS  = 10000;
static const uint32_t DNS_TIMEOUT_MS   = 3000;
static const uint32_t HTTP_TIMEOUT_MS  = 5000;

// ---- LaskaKit C3-LPKit v4 piny ----
#define PERIPH_EN_PIN 4
#define NEOPIXEL_PIN  9
#define VBAT_ADC_PIN  0
#define VBAT_DIVIDER  1.769f
#define UART_TX_PIN   21
#define UART_RX_PIN   20

// ---- RTC slow memory — prezije deep sleep ----
RTC_DATA_ATTR uint32_t cycle_count    = 0;
RTC_DATA_ATTR uint32_t wifi_fail_cnt  = 0;
RTC_DATA_ATTR uint32_t dns_fail_cnt   = 0;
RTC_DATA_ATTR uint32_t http_fail_cnt  = 0;
RTC_DATA_ATTR uint32_t http_ok_cnt    = 0;

static Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
static uint32_t t_boot_ms = 0;

// Dual-log: USB-CDC + UART0. USB-CDC pri odpojenem USB nepise (timeout=0).
// Vsechny radky jsou prefixovany [+NNNNms] = milisec od startu setup().
static char log_buf[320];
#define LOG(...) do { \
    int _h = snprintf(log_buf, sizeof(log_buf), "[+%5lums] ", (unsigned long)(millis() - t_boot_ms)); \
    int _n = snprintf(log_buf + _h, sizeof(log_buf) - _h, __VA_ARGS__); \
    if (_n > 0) { \
      size_t _total = _h + _n; \
      Serial.write((const uint8_t*)log_buf, _total); \
      Serial0.write((const uint8_t*)log_buf, _total); \
    } \
} while (0)

static const char* wake_cause_str(esp_sleep_wakeup_cause_t c) {
  switch (c) {
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "cold boot / EN reset";
    case ESP_SLEEP_WAKEUP_TIMER:     return "RTC timer";
    case ESP_SLEEP_WAKEUP_EXT0:      return "EXT0 (RTC IO)";
    case ESP_SLEEP_WAKEUP_EXT1:      return "EXT1 (RTC mask)";
    default:                         return "other";
  }
}

static const char* reset_reason_str(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "POWERON (battery / Vbus connected)";
    case ESP_RST_EXT:      return "EXT (RST pin)";
    case ESP_RST_SW:       return "SW (esp_restart)";
    case ESP_RST_PANIC:    return "PANIC (exception/watchdog)";
    case ESP_RST_INT_WDT:  return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT:      return "WDT (other)";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP wake";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO:     return "SDIO";
    default:               return "unknown";
  }
}

static const char* wifi_status_str(wl_status_t s) {
  switch (s) {
    case WL_NO_SHIELD:       return "NO_SHIELD";
    case WL_IDLE_STATUS:     return "IDLE";
    case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED";
    case WL_CONNECTED:       return "CONNECTED";
    case WL_CONNECT_FAILED:  return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED:    return "DISCONNECTED";
    default:                 return "?";
  }
}

static float read_vbat() {
  analogReadResolution(12);
  uint64_t sum = 0;
  const int N = 16;
  for (int i = 0; i < N; i++) { sum += analogReadMilliVolts(VBAT_ADC_PIN); delay(2); }
  return (sum / (float)N) * VBAT_DIVIDER / 1000.0f;
}

// Override DHCP DNS po pripojeni — guest WiFi resolver casto nepreklada public domeny.
// Stejny pattern jako debug/ntfy/main.cpp.
static void override_dns_public() {
  ip_addr_t cf, ggl;
  IP_ADDR4(&cf,  1, 1, 1, 1);
  IP_ADDR4(&ggl, 8, 8, 8, 8);
  dns_setserver(0, &cf);
  dns_setserver(1, &ggl);
  LOG("[dns]  overridden -> 1.1.1.1, 8.8.8.8 (DHCP DNS bypassed)\n");
}

static bool wifi_connect() {
  LOG("[wifi] starting connect to SSID=\"%s\"\n", WIFI_SSID);
  LOG("[wifi] mac=%s\n", WiFi.macAddress().c_str());

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);   // chceme deterministicke chovani v cyklech
  WiFi.persistent(false);         // neukladat creds do NVS (zbytecny flash wear)
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long t0 = millis();
  wl_status_t last = WL_IDLE_STATUS;
  while (true) {
    wl_status_t st = WiFi.status();
    if (st != last) {
      LOG("[wifi] status=%s (t=%lu ms)\n", wifi_status_str(st), (unsigned long)(millis() - t0));
      last = st;
    }
    if (st == WL_CONNECTED) break;
    if ((millis() - t0) > WIFI_TIMEOUT_MS) {
      LOG("[wifi] TIMEOUT after %u ms, last status=%s\n", (unsigned)WIFI_TIMEOUT_MS, wifi_status_str(st));
      return false;
    }
    delay(100);
  }

  LOG("[wifi] CONNECTED in %lu ms\n", (unsigned long)(millis() - t0));
  LOG("[wifi]  rssi=%d dBm  channel=%d  bssid=%s\n",
      WiFi.RSSI(), WiFi.channel(), WiFi.BSSIDstr().c_str());
  LOG("[wifi]  ip=%s  gw=%s  mask=%s\n",
      WiFi.localIP().toString().c_str(),
      WiFi.gatewayIP().toString().c_str(),
      WiFi.subnetMask().toString().c_str());
  LOG("[wifi]  dhcp dns1=%s  dns2=%s\n",
      WiFi.dnsIP(0).toString().c_str(),
      WiFi.dnsIP(1).toString().c_str());
  return true;
}

static void wifi_off() {
  LOG("[wifi] disconnecting + radio off\n");
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
}

// Explicitni DNS lookup pred HTTP — vidime presny IP a oddelime DNS chybu od HTTP chyby.
static bool dns_resolve(const char* host, IPAddress &out_ip) {
  LOG("[dns]  resolve %s ...\n", host);
  uint32_t t0 = millis();
  // WiFi.hostByName respektuje DNS timeout konfigurovany pro lwip.
  if (!WiFi.hostByName(host, out_ip)) {
    LOG("[dns]  FAIL for %s (%lu ms)\n", host, (unsigned long)(millis() - t0));
    return false;
  }
  LOG("[dns]  OK %s -> %s (%lu ms)\n",
      host, out_ip.toString().c_str(), (unsigned long)(millis() - t0));
  return true;
}

static int ntfy_send(const char* title, const char* body) {
  LOG("[ntfy] POST title=\"%s\" body_len=%u\n", title, (unsigned)strlen(body));
  LOG("[ntfy]  body=\"%s\"\n", body);

  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);

  String url = String("http://") + NTFY_HOST + "/" + NTFY_TOPIC;
  LOG("[ntfy]  url=%s\n", url.c_str());

  uint32_t t0 = millis();
  if (!http.begin(url)) {
    LOG("[ntfy] http.begin() FAILED\n");
    return -100;
  }
  http.addHeader("Content-Type", "text/plain; charset=utf-8");
  http.addHeader("Title", title);
  http.addHeader("Priority", "low");
  http.addHeader("Tags", "battery");

  int code = http.POST((uint8_t*)body, strlen(body));
  uint32_t dt = millis() - t0;

  if (code > 0) {
    String resp = http.getString();
    LOG("[ntfy] HTTP %d in %lu ms, resp_len=%u\n", code, (unsigned long)dt, resp.length());
    if (resp.length() > 0 && resp.length() < 200) {
      LOG("[ntfy]  resp=\"%s\"\n", resp.c_str());
    }
  } else {
    LOG("[ntfy] HTTP error code=%d (%s) after %lu ms\n",
        code, HTTPClient::errorToString(code).c_str(), (unsigned long)dt);
  }
  http.end();
  return code;
}

void setup() {
  t_boot_ms = millis();

  // Power-on peripheral rail nejdrive — NeoPixel signalizuje wake i pred Serial initem.
  pinMode(PERIPH_EN_PIN, OUTPUT);
  digitalWrite(PERIPH_EN_PIN, HIGH);
  // Drzeni z predchoziho sleep musime explicitne uvolnit, jinak GPIO ignoruje zapis.
  gpio_hold_dis((gpio_num_t)PERIPH_EN_PIN);
  gpio_deep_sleep_hold_dis();
  delay(20);

  pixel.begin();
  pixel.setBrightness(40);
  pixel.setPixelColor(0, pixel.Color(255, 0, 0));  // active phase = red
  pixel.show();

  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);             // nikdy neblokuj na USB-CDC pri odpojenem USB
  Serial0.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  delay(200);                           // USB-CDC enumeration window

  cycle_count++;
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  esp_reset_reason_t      rrsn  = esp_reset_reason();

  LOG("\n");
  LOG(">>> woke up\n");
  LOG("================================================\n");
  LOG("=== c3power cycle #%lu ===\n", (unsigned long)cycle_count);
  LOG("[boot] wake_cause=%s  reset_reason=%s\n", wake_cause_str(cause), reset_reason_str(rrsn));
  LOG("[boot] stats: wifi_fail=%lu dns_fail=%lu http_fail=%lu http_ok=%lu\n",
      (unsigned long)wifi_fail_cnt, (unsigned long)dns_fail_cnt,
      (unsigned long)http_fail_cnt, (unsigned long)http_ok_cnt);
  LOG("[boot] heap free=%u min=%u  sketch_size=%u\n",
      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
      (unsigned)ESP.getSketchSize());

  float vbat = read_vbat();
  LOG("[vbat] %.3f V (raw_mv_avg via ADC GPIO%d, divider=%.3fx)\n",
      vbat, VBAT_ADC_PIN, VBAT_DIVIDER);

  uint32_t t_active_start = millis();
  bool ok = false;

  if (wifi_connect()) {
    override_dns_public();

    IPAddress ntfy_ip;
    if (dns_resolve(NTFY_HOST, ntfy_ip)) {
      char body[200];
      snprintf(body, sizeof(body),
               "cycle=%lu vbat=%.3fV rssi=%d ip=%s uptime=%lums",
               (unsigned long)cycle_count, vbat, WiFi.RSSI(),
               WiFi.localIP().toString().c_str(),
               (unsigned long)(millis() - t_boot_ms));

      int code = ntfy_send("C3 power test", body);
      if (code == 200) {
        http_ok_cnt++;
        ok = true;
      } else {
        http_fail_cnt++;
      }
    } else {
      dns_fail_cnt++;
    }
  } else {
    wifi_fail_cnt++;
  }

  uint32_t t_wifi_done = millis();
  LOG("[phase] WiFi+POST done (%lu ms), ok=%d\n",
      (unsigned long)(t_wifi_done - t_active_start), (int)ok);

  wifi_off();

  // Indikator "WiFi hotovo, drzim active pro multimeter readout"
  pixel.setPixelColor(0, ok ? pixel.Color(0, 255, 0) : pixel.Color(255, 128, 0));
  pixel.show();

  LOG("[hold] active hold %lu ms (LED %s, no radio)\n",
      (unsigned long)AWAKE_HOLD_MS, ok ? "green" : "orange");
  Serial.flush();
  Serial0.flush();
  delay(AWAKE_HOLD_MS);

  // Vypnout NeoPixel + odriznout perif rail. Bez gpio_hold_en by GPIO floatlo
  // pres deep sleep a NeoPixel by mohl byt napajeny -> zkresleny sleep current.
  pixel.setPixelColor(0, 0);
  pixel.show();
  digitalWrite(PERIPH_EN_PIN, LOW);
  gpio_hold_en((gpio_num_t)PERIPH_EN_PIN);
  gpio_deep_sleep_hold_en();

  uint32_t t_total_active = millis();
  LOG("[time] active total=%lu ms (wifi+post=%lu ms, hold=%lu ms)\n",
      (unsigned long)(t_total_active - t_boot_ms),
      (unsigned long)(t_wifi_done - t_active_start),
      (unsigned long)(t_total_active - t_wifi_done));
  LOG("[sleep] PERIPH_EN held LOW; expecting ~10-15 uA. Switch multimeter to uA range now.\n");
  LOG(">>> going to sleep for %llu s\n", SLEEP_DURATION_S);
  Serial.flush();
  Serial0.flush();

  esp_sleep_enable_timer_wakeup(SLEEP_DURATION_S * 1000000ULL);
  esp_deep_sleep_start();
  // unreachable — wake = reset
}

void loop() {
  // never reached
}
