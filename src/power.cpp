#include "power.h"

#include "config.h"
#include "driver/gpio.h"

void periph_power_on() {
  pinMode(PERIPH_EN_PIN, OUTPUT);
  gpio_hold_dis(static_cast<gpio_num_t>(PERIPH_EN_PIN));
  gpio_deep_sleep_hold_dis();
  digitalWrite(PERIPH_EN_PIN, HIGH);
  delay(20);
}

void periph_power_off_for_sleep() {
  digitalWrite(PERIPH_EN_PIN, LOW);
  gpio_hold_en(static_cast<gpio_num_t>(PERIPH_EN_PIN));
  gpio_deep_sleep_hold_en();
}

// One-shot ADC config — vola se z main setup, ne v kazdem read_vbat (drobny perf win
// + nezavisly na default-framework atttenuation, ktera by se mohla v budoucich
// arduino-esp32 verzich zmenit).
void adc_initialize() {
  analogReadResolution(12);
  analogSetPinAttenuation(VBAT_ADC_PIN, ADC_11db);  // ~3.3 V FS, sedí na divider 1.769×
}

float read_vbat() {
  uint64_t sum_mv = 0;
  constexpr int N = 16;
  for (int i = 0; i < N; i++) {
    sum_mv += analogReadMilliVolts(VBAT_ADC_PIN);
    delay(2);
  }
  return (sum_mv / static_cast<float>(N)) * VBAT_DIVIDER / 1000.0f;
}

float vbat_to_percent(float vbat) {
  static const struct { float v; float p; } curve[] = {
    {4.20f, 100.0f}, {4.10f, 95.0f}, {4.00f, 85.0f}, {3.90f, 70.0f},
    {3.80f, 55.0f}, {3.70f, 35.0f}, {3.60f, 20.0f}, {3.50f, 12.0f},
    {3.40f,   5.0f}, {3.30f,  2.0f}, {3.20f,  0.0f}
  };
  constexpr int N = sizeof(curve) / sizeof(curve[0]);
  if (vbat >= curve[0].v)     return 100.0f;
  if (vbat <= curve[N - 1].v) return 0.0f;
  for (int i = 0; i < N - 1; i++) {
    if (vbat <= curve[i].v && vbat >= curve[i + 1].v) {
      float t = (vbat - curve[i + 1].v) / (curve[i].v - curve[i + 1].v);
      return curve[i + 1].p + t * (curve[i].p - curve[i + 1].p);
    }
  }
  return 0.0f;
}
