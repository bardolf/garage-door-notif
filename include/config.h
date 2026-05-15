// Centralizovane konstanty pro celou aplikaci.
// HW piny pro LaskaKit ESP32-C3-LPKit v4 + logicke prahy z README.md sekce
// "Architektura". Pri zmene desky nebo behavior tweaks meni se tady.
//
// Citlive credentials (WIFI_SSID, NTFY_TOPIC, ...) jsou v include/secrets.h
// (gitignored).

#pragma once

#include <Arduino.h>

// ===== HW piny =====
constexpr uint8_t  SDA_PIN        = 6;
constexpr uint8_t  SCL_PIN        = 7;
constexpr uint8_t  NEOPIXEL_PIN   = 9;
constexpr uint8_t  PERIPH_EN_PIN  = 4;
constexpr uint8_t  VBAT_ADC_PIN   = 0;
constexpr float    VBAT_DIVIDER   = 1.769f;
constexpr uint8_t  LED_BRIGHTNESS = 60;

// ===== Logicke prahy a casovani =====
constexpr uint32_t WAKE_INTERVAL_S            = 30;     // 5 min //!!!TODO!!!
constexpr float    TILT_THRESHOLD_OPEN        = 60.0f;
constexpr float    TILT_THRESHOLD_CLOSE       = 40.0f;
constexpr int      NIGHT_START_HOUR           = 10; //!!!TODO!!!
constexpr int      NIGHT_END_HOUR             = 6;
constexpr int      HEARTBEAT_HOUR             = 21;
constexpr uint32_t ALERT_RATE_LIMIT_S         = 4 * 60;
constexpr uint32_t ERROR_REPORT_RATE_LIMIT_S  = 30 * 60;   // 30 min — error po chybe nesmi spamovat
constexpr uint32_t NTP_RESYNC_INTERVAL_S      = 24 * 3600;
constexpr uint32_t NTP_OPPORTUNISTIC_INTERVAL_S = 600;
constexpr uint32_t WIFI_TIMEOUT_MS            = 15000;
constexpr uint32_t NTP_TIMEOUT_MS             = 10000;
constexpr uint32_t I2C_TIMEOUT_MS             = 25;        // bus-timeout proti wedged I2C
constexpr uint8_t  MPU_RETRY_COUNT            = 3;
constexpr uint16_t MPU_RETRY_DELAY_MS         = 100;
constexpr uint32_t COLD_BOOT_UPLOAD_WINDOW_S  = 10;        // dev-only; produkce-friendly hodnota

// NTP epoch sanity threshold — vsechny `time()` < tohle se povazuji za "ne-synced".
constexpr time_t   NTP_VALID_EPOCH_THRESHOLD  = 1700000000; // ~ 2023-11-15

// Po N po sobe failnutych WiFi connectech se posila warning notif (pokud se kdy podari).
constexpr uint8_t  WIFI_FAIL_REPORT_THRESHOLD = 3;
// Strop pending error counteru — vyssi hodnoty zafrejzuji v RTC mem.
constexpr uint32_t PENDING_ERROR_COUNT_CAP    = 999;

// LED indikace casovani
constexpr uint32_t LED_INDICATION_MS          = 5000;
constexpr uint32_t LED_BLINK_HALF_PERIOD_MS   = 500;

// Cekani po Serial.begin pred prvnim printf — USB-CDC potrebuje cas na host enumeraci.
constexpr uint32_t SERIAL_INIT_DELAY_MS       = 300;

// Debug switch — false = misto realneho deep sleep jen delay(WAKE_INTERVAL_S).
// USB-CDC zustane connected, logy z kazdeho wake cyklu jsou videt continuously,
// "going to sleep" / "woke up" hlasky se stale printuji (pro vizualni sanity).
// Chip nikdy realne nezaspi → spotreba je ~25 mA continuous, jen pro debug u stolu.
// Pred deploymentem zpet na `true`.
constexpr bool     DEEP_SLEEP_ENABLED         = false; //!!!TODO!!!

// CZ timezone (POSIX format) — automaticky resi CET/CEST DST.
// Definice v net.cpp (extern aby slo sdilet pres TU).
extern const char* const TZ_CZ;
extern const char* const NTP_SERVERS[3];
