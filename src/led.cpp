#include "led.h"

#include <Adafruit_NeoPixel.h>
#include "config.h"

namespace {
Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
}  // namespace

void led_initialize() {
  pixel.begin();
  pixel.setBrightness(LED_BRIGHTNESS);
  led_off();
}

void led_show(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

void led_off() {
  led_show(0, 0, 0);
}
