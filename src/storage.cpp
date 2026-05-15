#include "storage.h"

#include <Preferences.h>
#include <string.h>

#include "config.h"
#include "secrets.h"

Config cfg;

namespace {

constexpr const char* NS = "garaz";

void copy_to_cfield(char* dst, const String& src, size_t cap) {
  strncpy(dst, src.c_str(), cap - 1);
  dst[cap - 1] = '\0';
}

}  // namespace

void cfg_load() {
  Preferences p;
  p.begin(NS, /*readOnly=*/true);

  cfg.configured = p.getBool("configured", false);

  copy_to_cfield(cfg.wifi_ssid,   p.getString("wifi_ssid",   WIFI_SSID),    CFG_STR_MAX);
  copy_to_cfield(cfg.wifi_pass,   p.getString("wifi_pass",   WIFI_PASS),    CFG_STR_MAX);
  copy_to_cfield(cfg.ntfy_topic,  p.getString("ntfy_topic",  NTFY_TOPIC),   CFG_STR_MAX);
  copy_to_cfield(cfg.device_name, p.getString("device_name", "garaz"),      CFG_STR_MAX);

  cfg.night_start_hour  = p.getUChar ("night_start", NIGHT_START_HOUR);
  cfg.night_end_hour    = p.getUChar ("night_end",   NIGHT_END_HOUR);
  cfg.heartbeat_hour    = p.getUChar ("hb_hour",     HEARTBEAT_HOUR);
  cfg.wake_interval_s   = p.getUInt  ("wake_int",    WAKE_INTERVAL_S);

  p.end();

  Serial.println(F("[cfg] ===== active configuration ====="));
  Serial.printf( "[cfg] configured:    %s\n", cfg.configured ? "yes" : "NO (AP mode bude)");
  Serial.printf( "[cfg] WiFi SSID:     %s\n", cfg.wifi_ssid);
  Serial.printf( "[cfg] ntfy topic:    %s\n", cfg.ntfy_topic);
  Serial.printf( "[cfg] device name:   %s\n", cfg.device_name);
  Serial.printf( "[cfg] night window:  %02u:00 - %02u:00\n",
                 cfg.night_start_hour, cfg.night_end_hour);
  Serial.printf( "[cfg] heartbeat:     %02u:00\n", cfg.heartbeat_hour);
  Serial.printf( "[cfg] wake interval: %u s\n", cfg.wake_interval_s);
  Serial.println(F("[cfg] ================================"));
}

void cfg_save() {
  Preferences p;
  p.begin(NS, /*readOnly=*/false);

  p.putString("wifi_ssid",   cfg.wifi_ssid);
  p.putString("wifi_pass",   cfg.wifi_pass);
  p.putString("ntfy_topic",  cfg.ntfy_topic);
  p.putString("device_name", cfg.device_name);

  p.putUChar ("night_start", cfg.night_start_hour);
  p.putUChar ("night_end",   cfg.night_end_hour);
  p.putUChar ("hb_hour",     cfg.heartbeat_hour);
  p.putUInt  ("wake_int",    cfg.wake_interval_s);

  p.putBool  ("configured",  true);
  cfg.configured = true;

  p.end();
  Serial.println(F("[cfg] saved to NVS"));
}

void cfg_clear() {
  Preferences p;
  p.begin(NS, /*readOnly=*/false);
  p.clear();
  p.end();
  cfg.configured = false;
  Serial.println(F("[cfg] NVS cleared"));
}
