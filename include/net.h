// Sit'ova vrstva — WiFi connect/disconnect, NTP sync, ntfy.sh POST.
//
// Pri WiFi connectu se override-uje DHCP DNS na 1.1.1.1 + 8.8.8.8 (guest WiFi
// casto rozdava lokalni resolver co nepreklada public domeny → bez tohohle
// HTTP POST padne s "DNS failed").

#pragma once

#include <Arduino.h>

// Connect k AP definovanemu v secrets.h. Po uspechu nastavi public DNS.
// Vraci true pri WL_CONNECTED, false pri timeout.
bool wifi_connect();

// Disconnect + WIFI_OFF. Volat pred prechodem do deep sleep.
void wifi_disconnect();

// NTP sync na cz.pool.ntp.org. Pri uspechu nastavi `state.last_ntp_unix`
// a (poprve) `state.first_boot_unix`. Vraci false pri timeout.
bool ntp_sync();

// HTTP POST na ntfy.sh/{NTFY_TOPIC}. Vraci true pri 2xx response.
// Title / priority / tags se posilaji jako HTTP headery; title mit ASCII-only
// (ntfy header parser ma problem s UTF-8 v Title), body podporuje UTF-8.
bool ntfy_send(const char* title, const char* body,
               const char* priority, const char* tags);

// Aktualni WiFi RSSI v dBm. Validni jen kdyz wifi_connect() vratil true.
int wifi_rssi();

// 5-segmentova bar reprezentace signalu z RSSI (jako mobil indikator):
// "▮▮▮▮▮" (vyborny, >=-60) → "▮▯▯▯▯" (velmi slaby, <-90).
const char* rssi_bars(int rssi);
