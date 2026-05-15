// AP konfiguracni mod — pri prazdnem NVS nebo po double-RST.
//
// Co dela:
//   1. Vyrobi otevreny AP "garaz-XXXXXX" (MAC suffix).
//   2. Bezi web server na 192.168.4.1 s konfig formularem.
//   3. Form ma WiFi scan dropdown + thresholds + hours fieldy.
//   4. Submit → test connect na zadane WiFi (15 s timeout). Pri uspechu uloz
//      do NVS a `ESP.restart()`. Pri failu zobraz error a zustan v AP modu.
//   5. Po 10 min bez submitu → `esp_deep_sleep_start` (sance ze priste mame uz
//      jine podminky — napr. NTP).
//
// LED indikace: trojity blink kazde 3 s (modra). Bezi non-blocking pres
// millis state machine v hlavnim loop, aby HTTP byl responsivni.

#pragma once

// Vstoupi do AP konfiguracniho modu. Vraci se jen pokud user submitne
// validni config — pak `ESP.restart` v save handleru tuto fci stejne preskoci.
// Pokud nedoslo k submitu, fce vola `esp_deep_sleep_start` sama (nevraci se).
void run_ap_mode();
