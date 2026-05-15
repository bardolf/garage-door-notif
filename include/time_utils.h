// Local time helpers nad system clock (ESP32 RTC + NTP sync).
//
// Vsechny funkce predpokladaji ze TZ env je nastavena (`setenv("TZ", TZ_CZ)`)
// — to volame na zacatku kazdeho setup, protoze TZ env nepreziva deep sleep
// (na rozdil od system unix time, ktery v RTC chipu zustava).

#pragma once

#include <Arduino.h>
#include <time.h>

// True kdyz NTP uz alespon jednou prosynchronizoval (unix time po roce 2023).
bool time_is_synced();

// Snapshot aktualniho local time do `out`. Vsechny ostatni time helpery to
// pouzivaji interne — single source of truth pro "co je hodinu/yday".
void now_local(struct tm& out);

// Format aktualniho local time do `buf` podle strftime patternu.
void format_time(char* buf, size_t buf_size, const char* fmt);

// Vraci aktualni hodinu (0–23) v lokalnim case.
int current_hour();

// Vraci aktualni day-of-year (0–365) v lokalnim case.
int current_yday();

// True kdyz aktualni hodina spada do [NIGHT_START_HOUR, NIGHT_END_HOUR)
// (interval prekracuje pulnoc, viz config.h).
bool is_night_window();
