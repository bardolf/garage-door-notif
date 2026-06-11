#include "net.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "lwip/dns.h"
#include "esp_sntp.h"

#include "config.h"
#include "state.h"
#include "storage.h"

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
  WiFi.begin(cfg.wifi_ssid, cfg.wifi_pass);
  Serial.printf("[wifi] %s ", cfg.wifi_ssid);
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

  // Bug-fix: puvodni kod cekal na "time() > NTP_VALID_EPOCH_THRESHOLD",
  // ale system time po prvnim ever syncu drzi platnou hodnotu pres deep sleep
  // (RTC chip ji uchovava), takze druhe a dalsi volani ntp_sync() okamzite
  // vraceli true BEZ skutecneho sit'oveho dotazu → chip-only drift (typicky
  // 150-200 ppm = ~7 min/mesic) se nikdy nekorigoval.
  //
  // Resi se to explicitnim reset SNTP status flagu pred re-initem. Pak cekame
  // na SNTP_SYNC_STATUS_COMPLETED, coz se set-uje az v SNTP callbacku po prijati
  // realne odpovedi ze serveru.
  time_t t_before;
  time(&t_before);

  // sntp_init() je no-op pokud daemon uz bezi → fresh dotaz by se neposlal.
  // sntp_stop() vynuti restart pri dalsim configTzTime → posila novou query.
  sntp_stop();
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  configTzTime(TZ_CZ, NTP_SERVERS[0], NTP_SERVERS[1], NTP_SERVERS[2]);

  uint32_t start = millis();
  while ((millis() - start) < NTP_TIMEOUT_MS) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      time_t now; time(&now);
      char buf[32];
      struct tm tm_local;
      localtime_r(&now, &tm_local);
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &tm_local);
      long delta = static_cast<long>(static_cast<int64_t>(now) - static_cast<int64_t>(t_before));
      Serial.printf("OK -> %s (delta vs chip clock: %+ld s)\n", buf, delta);
      state.last_ntp_unix = now;
      if (state.first_boot_unix == 0) state.first_boot_unix = static_cast<uint64_t>(now);
      return true;
    }
    delay(200);
  }
  Serial.println(F("TIMEOUT (sntp_get_sync_status != COMPLETED)"));
  return false;
}

bool ntfy_send(const char* title, const char* body,
               const char* priority, const char* tags) {
  HTTPClient http;
  String url = String("http://ntfy.sh/") + cfg.ntfy_topic;
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
