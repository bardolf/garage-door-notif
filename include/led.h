// Status LED (WS2812 NeoPixel) na GPIO9 — diskretni barevna indikace.
// Sjednocena semantika (zamerne minimalisticka):
//   modra   = cold boot bezi (WiFi, NTP, baseline, boot notif)
//   zelena  = cold boot OK
//   fialova = alert "GARAZ OTEVRENA" poslan (5 s solid)
//   cervena = jakakoli chyba (5 s solid)
//
// Vzdy pred volanim led_*() musi byt PERIPH_EN=HIGH (NeoPixel je na perif
// railu, viz `power.h`).

#pragma once

#include <Arduino.h>

// Inicializace NeoPixel knihovny + nastaveni brightness. Volat jednou v setup().
void led_initialize();

// Nastavi konstantni barvu. Vola pixel.show() — barva je videt okamzite.
void led_show(uint8_t r, uint8_t g, uint8_t b);

// Vypne LED (alias na led_show(0,0,0)).
void led_off();
