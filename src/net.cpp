#include "net.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "lwip/dns.h"

#include "config.h"
#include "secrets.h"
#include "state.h"

const char* const TZ_CZ = "CET-1CEST,M3.5.0,M10.5.0/3";
const char* const NTP_SERVERS[3] = {
  "cz.pool.ntp.org", "pool.ntp.org", "time.google.com"
};

namespace {

void override_dns_to_public() {
  ip_addr_t cf, ggl;
  IP_ADDR4(&cf,  1, 1, 1, 1);
  IP_ADDR4(&ggl, 8, 8, 8, 8);
  dns_setserver(0, &cf);
  dns_setserver(1, &ggl);
}

}  // namespace

bool wifi_connect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[wifi] %s ", WIFI_SSID);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[wifi] FAIL"));
    return false;
  }
  Serial.printf("[wifi] OK ip=%s rssi=%d\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  override_dns_to_public();
  return true;
}

void wifi_disconnect() {
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
}

bool ntp_sync() {
  Serial.print(F("[ntp] sync ... "));
  configTzTime(TZ_CZ, NTP_SERVERS[0], NTP_SERVERS[1], NTP_SERVERS[2]);
  uint32_t start = millis();
  time_t now = 0;
  while ((millis() - start) < NTP_TIMEOUT_MS) {
    time(&now);
    if (now > NTP_VALID_EPOCH_THRESHOLD) {
      char buf[32];
      struct tm tm_local;
      localtime_r(&now, &tm_local);
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &tm_local);
      Serial.printf("OK -> %s\n", buf);
      state.last_ntp_unix = now;
      if (state.first_boot_unix == 0) state.first_boot_unix = static_cast<uint64_t>(now);
      return true;
    }
    delay(200);
  }
  Serial.println(F("TIMEOUT"));
  return false;
}

bool ntfy_send(const char* title, const char* body,
               const char* priority, const char* tags) {
  HTTPClient http;
  String url = String("http://ntfy.sh/") + NTFY_TOPIC;
  http.begin(url);
  http.addHeader("Content-Type", "text/plain; charset=utf-8");
  if (title)    http.addHeader("Title",    title);
  if (priority) http.addHeader("Priority", priority);
  if (tags)     http.addHeader("Tags",     tags);
  // HTTPClient::POST API zada non-const uint8_t* ale telo ze rozhodne necte zpet.
  // Cast pres const_cast dokumentuje, ze ten pointer nezapise (POST je read-only
  // wrt body buffer).
  int code = http.POST(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(body)),
                       strlen(body));
  http.end();
  Serial.printf("[ntfy] '%s' -> HTTP %d\n", title, code);
  return code >= 200 && code < 300;
}

int wifi_rssi() {
  return WiFi.RSSI();
}

const char* rssi_bars(int rssi) {
  if (rssi >= -60) return "▮▮▮▮▮";
  if (rssi >= -70) return "▮▮▮▮▯";
  if (rssi >= -80) return "▮▮▮▯▯";
  if (rssi >= -90) return "▮▮▯▯▯";
  return "▮▯▯▯▯";
}
