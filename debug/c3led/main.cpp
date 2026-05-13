// LaskaKit ESP32-C3-LPKit — NeoPixel LED test
//
// Cil: overit, ze jediny "viditelny" LED na desce je adresovatelny WS2812
// (NeoPixel) na GPIO 9, a tedy ho jde softwarove vypnout (nemusime ho
// odpajovat kvuli spotrebe).
//
// Sekvence:
//   1. setup() — zapne sekundarni regulator (GPIO 4 HIGH), zhasne LED
//   2. 5 s blackout — uzivatel uvidi, jestli puvodne svitici LED zhasla
//   3. loop — cyklus RED -> GREEN -> BLUE -> OFF po 1 s
//
// Vsechno se loguje na Serial (USB CDC).
//
// Build/upload: pio run -e c3led -t upload -t monitor

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN       9   // WS2812 data pin
#define PERIPH_EN_PIN 4   // LaskaKit secondary regulator (peripherals power)
#define VBAT_ADC_PIN  0   // v4.0: ADC_BATT bridge closed by default → reads battery
#define VBAT_DIVIDER  1.769f  // z potisku desky v4.0 ("battery voltage IO0: dividerRatio 1.769")

Adafruit_NeoPixel pixels(1, LED_PIN, NEO_GRB + NEO_KHZ800);
static uint32_t loop_count = 0;

static float read_vbat() {
  uint32_t mv = analogReadMilliVolts(VBAT_ADC_PIN);
  return (float)mv * VBAT_DIVIDER / 1000.0f;
}

// Prumer N vzorku - vyfiltruje sum ADC, stabilni hodnota i bez USB.
static float read_vbat_avg(int samples) {
  uint32_t sum_mv = 0;
  for (int i = 0; i < samples; i++) {
    sum_mv += analogReadMilliVolts(VBAT_ADC_PIN);
    delay(5);
  }
  float mv_avg = (float)sum_mv / (float)samples;
  return mv_avg * VBAT_DIVIDER / 1000.0f;
}

static void led_set(uint8_t r, uint8_t g, uint8_t b) {
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
}

static void blink_n(uint8_t r, uint8_t g, uint8_t b, int n,
                    int on_ms = 400, int off_ms = 400) {
  for (int i = 0; i < n; i++) {
    led_set(r, g, b);
    delay(on_ms);
    led_set(0, 0, 0);
    delay(off_ms);
  }
}

// Zakoduje napeti do blikani: jednotky (R), desetiny (G), setiny (B).
// Pred zacatkem kratky bily zablesk = "start mereni".
static void show_voltage_blinks(float v) {
  int integer    = (int)v;
  int tenths     = ((int)(v * 10.0f)) % 10;
  int hundredths = ((int)(v * 100.0f)) % 10;

  Serial.printf("[blink] V=%.3f → R=%d  G=%d  B=%d\n",
                v, integer, tenths, hundredths);

  // Start marker
  led_set(40, 40, 40);  delay(100);
  led_set(0, 0, 0);     delay(800);

  blink_n(255, 0,   0,   integer);     delay(800);
  blink_n(0,   255, 0,   tenths);      delay(800);
  blink_n(0,   0,   255, hundredths);
}

void setup() {
  // Zapnout napajeni perifernich obvodu (vcetne NeoPixelu).
  pinMode(PERIPH_EN_PIN, OUTPUT);
  digitalWrite(PERIPH_EN_PIN, HIGH);

  // USB CDC potrebuje par sekund na enumeraci, jinak prvni vypis chybi.
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println(F("=== LaskaKit ESP32-C3-LPKit LED test ==="));
  Serial.printf("LED (NeoPixel) pin: GPIO %d\n", LED_PIN);
  Serial.printf("Peripheral enable pin: GPIO %d (HIGH)\n", PERIPH_EN_PIN);
  Serial.printf("Vbat ADC pin: GPIO %d (divider=%.1fx)\n", VBAT_ADC_PIN, VBAT_DIVIDER);
  Serial.printf("Initial Vbat = %.3f V (raw=%lu mV)\n",
                read_vbat(), (unsigned long)analogReadMilliVolts(VBAT_ADC_PIN));

  pixels.begin();
  pixels.setBrightness(10);     // 10/255 — slaba intenzita, sance ze i tak slo videt
  led_set(0, 0, 0);

  Serial.println(F("[test] LED zhasla. Spoustim mereni baterie + blink-kod:"));
  Serial.println(F("[test]   R = jednotky V, G = desetiny V, B = setiny V"));
  Serial.println(F("[test]   Pred kazdym mereni kratky bily zablesk (start marker)."));
  delay(2000);
}

void loop() {
  loop_count++;
  float v = read_vbat_avg(32);
  uint32_t up_s = millis() / 1000;
  Serial.printf("---- loop %lu | %lu s | Vbat=%.3f V ----\n",
                (unsigned long)loop_count, (unsigned long)up_s, v);
  show_voltage_blinks(v);
  delay(3000);  // dlouha pauza pred dalsim merenim — uzivatel ma cas dekodovat
}
