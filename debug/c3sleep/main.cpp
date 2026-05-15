// Garage door PoC — isolovane mereni spotreby ESP32-C3 bez WiFi.
// Varianta c3power minus radio: jen wake → LED indikace → hold → deep sleep.
// Cil: zmerit active proud bez WiFi peaku (jen CPU + NeoPixel) a klidovy proud
// samotneho C3 v deep sleep (PERIPH_EN LOW → NeoPixel odpojeny).
//
// Cyklus:
//   1. wake (cold boot nebo timer)
//   2. PERIPH_EN=HIGH, NeoPixel cerveny  ← active phase
//   3. AWAKE_HOLD_MS (CPU bezi, NeoPixel sviti) ← cas pro multimeter readout active
//   4. NeoPixel zhasne, PERIPH_EN=LOW + hold, deep sleep
//   5. wake → goto 1
//
// Dual log: USB-CDC (Serial) + UART0 (Serial0, GPIO20=RX, GPIO21=TX).
//
// pio run -e c3sleep -t upload -t monitor

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "esp_sleep.h"
#include "driver/gpio.h"

// ---- Casovani ----
static const uint64_t SLEEP_DURATION_S = 30;
static const uint32_t AWAKE_HOLD_MS    = 5000;  // delsi nez c3power — bez WiFi muzes v klidu cist multimeter

// ---- LaskaKit C3-LPKit v4 piny ----
#define PERIPH_EN_PIN 4
#define NEOPIXEL_PIN  9
#define VBAT_ADC_PIN  0
#define VBAT_DIVIDER  1.769f
#define UART_TX_PIN   21
#define UART_RX_PIN   20

RTC_DATA_ATTR uint32_t cycle_count = 0;

static Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
static uint32_t t_boot_ms = 0;

static char log_buf[256];
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
    default:                         return "other";
  }
}

static const char* reset_reason_str(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT (RST pin)";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP wake";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    default:                return "unknown";
  }
}

static float read_vbat() {
  analogReadResolution(12);
  uint64_t sum = 0;
  const int N = 16;
  for (int i = 0; i < N; i++) { sum += analogReadMilliVolts(VBAT_ADC_PIN); delay(2); }
  return (sum / (float)N) * VBAT_DIVIDER / 1000.0f;
}

void setup() {
  t_boot_ms = millis();

  pinMode(PERIPH_EN_PIN, OUTPUT);
  digitalWrite(PERIPH_EN_PIN, HIGH);
  gpio_hold_dis((gpio_num_t)PERIPH_EN_PIN);
  gpio_deep_sleep_hold_dis();
  delay(20);

  pixel.begin();
  pixel.setBrightness(40);
  pixel.setPixelColor(0, pixel.Color(255, 0, 0));   // active phase = red
  pixel.show();

  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  Serial0.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  delay(200);

  cycle_count++;
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  esp_reset_reason_t      rrsn  = esp_reset_reason();

  LOG("\n");
  LOG(">>> woke up\n");
  LOG("================================================\n");
  LOG("=== c3sleep cycle #%lu (no WiFi) ===\n", (unsigned long)cycle_count);
  LOG("[boot] wake_cause=%s  reset_reason=%s\n", wake_cause_str(cause), reset_reason_str(rrsn));
  LOG("[boot] heap free=%u\n", (unsigned)ESP.getFreeHeap());

  float vbat = read_vbat();
  LOG("[vbat] %.3f V\n", vbat);

  LOG("[hold] active hold %lu ms (CPU on, NeoPixel red)\n", (unsigned long)AWAKE_HOLD_MS);
  Serial.flush();
  Serial0.flush();
  delay(AWAKE_HOLD_MS);

  pixel.setPixelColor(0, 0);
  pixel.show();
  digitalWrite(PERIPH_EN_PIN, LOW);
  gpio_hold_en((gpio_num_t)PERIPH_EN_PIN);
  gpio_deep_sleep_hold_en();

  LOG("[time] active total=%lu ms\n", (unsigned long)(millis() - t_boot_ms));
  LOG("[sleep] PERIPH_EN held LOW; NeoPixel rail off. Switch multimeter to uA now.\n");
  LOG(">>> going to sleep for %llu s\n", SLEEP_DURATION_S);
  Serial.flush();
  Serial0.flush();

  esp_sleep_enable_timer_wakeup(SLEEP_DURATION_S * 1000000ULL);
  esp_deep_sleep_start();
}

void loop() {
  // never reached
}
