// Power management — peripheral rail switch + battery monitoring.
//
// LaskaKit C3-LPKit v4 ma GPIO4 jako PERIPH_EN MOSFET gate, ktery spina
// 3V3 vetev na headeru i na uSup konektoru. V deep sleep ho drzime LOW
// pres `gpio_hold_en`, cimz odrizneme NeoPixel i MPU od napajeni → 0 µA
// quiescent z perif obvodu.
//
// Vbat se cte z GPIO0 pres on-board divider 1.769×. Pouzivame eFuse-kalibrovany
// `analogReadMilliVolts()` — chyba pod 1 %.

#pragma once

#include <Arduino.h>

// Konfiguruje ADC pro Vbat mereni — resolution 12-bit, attenuation 11 dB.
// Volat jednou v setup. Bez tohohle plati framework default, ktery se muze
// v budoucich arduino-esp32 verzich zmenit.
void adc_initialize();

// Zapne perif rail (PERIPH_EN HIGH). Volat na zacatku setup pred kazdym
// pristupem k MPU / NeoPixel. Uvolnuje hold z predchoziho sleep cyklu.
void periph_power_on();

// Vypne perif rail (PERIPH_EN LOW) + zapne hold pro deep sleep. Bez hold by
// GPIO floatlo pres sleep a perif obvody by zustaly napajene.
void periph_power_off_for_sleep();

// Vraci napeti baterie v voltech. Prumeruje 16 ADC vzorku pres ~32 ms.
float read_vbat();

// Mapuje napeti na procenta podle LiPo discharge krivky pri lehkem zatezi.
// Hodnoty jsou typicke pro single-cell LiPo / 18650 cell.
float vbat_to_percent(float vbat);
