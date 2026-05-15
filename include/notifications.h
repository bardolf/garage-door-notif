// Notifikacni zpravy zasilane uzivateli pres ntfy.sh.
//
// Kazda send_*() funkce sestavi telo zpravy, zavola `ntfy_send()` z net.h
// a vrati uspech HTTP POST. Volajici pak rozhoduje, jak indikovat vysledek
// uzivateli (LED, RTC state update).
//
// Error queue: pri offline kontextu (no WiFi / MPU fail behem cold bootu)
// se chyby ukladaji do RTC mem pres `queue_error()`. Na pristim uspesnem
// WiFi connectu zavola `flush_pending_errors()` a posle souhrn na ntfy.

#pragma once

#include <Arduino.h>
#include "vec3.h"

// Boot notifikace — posila se jen pri cold bootu. Telo obsahuje cas, baterii,
// signal a baseline orientation (pomoc uzivateli pri instalaci).
bool send_boot_notification(float vbat, float vbat_pct, int rssi, const Vec3& baseline);

// Urgent alert "GARAZ OTEVRENA" — posila se kdyz tilt > prah a je nocni okno.
// `time_known` rozlisuje normalni alert (čas v zprave) od fail-open alertu,
// kdyz NTP neselhal a jeste neznam aktualni cas. Bezpecnejsi alertovat
// s "(cas neznamy)" nez tise zahodit.
bool send_urgent_alert(float tilt, float vbat_pct, bool time_known);

// Denni heartbeat — posila se jednou denne v HEARTBEAT_HOUR (kontrola "zarizeni zije").
bool send_heartbeat(bool door_open, float tilt, float vbat, float vbat_pct, int rssi);

// Zaznamena chybu do RTC mem queue (pending_error_msg + pending_error_count).
// Idempotentni vuci typu chyby — counter inkrementuje, posledni zprava prepise.
void queue_error(const char* msg);

// Posle souhrn pendingovanych chyb. Predpoklada WiFi up. Pri uspesnem POST
// vynuluje queue a aktualizuje last_error_report_unix.
void flush_pending_errors(float vbat_pct, int rssi);
