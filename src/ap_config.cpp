#include "ap_config.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_sleep.h"

#include "config.h"
#include "led.h"
#include "power.h"
#include "storage.h"

namespace {

constexpr uint32_t AP_TIMEOUT_MS         = 10 * 60 * 1000UL;
constexpr uint32_t AP_LED_BURST_PERIOD_MS = 3000;
constexpr uint32_t AP_LED_PULSE_MS        = 100;
constexpr uint8_t  AP_LED_BURST_COUNT     = 3;
constexpr uint32_t WIFI_TEST_TIMEOUT_MS   = 15000;
constexpr uint32_t AP_LOG_HEARTBEAT_MS    = 30000;   // log "still waiting" kazde 30 s

WebServer server(80);

// State pro non-blocking LED triple-blink (3× za 3 s):
//   Trojity blink trva 3 × (on 100 + off 100) = 600 ms, pak pauza 2400 ms.
//   Behem pauzy server svobodne handleClient(). Behem blinku kratky delay
//   blokuje ~600 ms / 3000 ms = 20 % casu — pro form submit akceptovatelne.
uint32_t last_led_burst = 0;

String ap_ssid() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[32];
  snprintf(buf, sizeof(buf), "garaz-%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(buf);
}

// Vygeneruj default ntfy topic z MAC adresy (per-device unikatni).
// Pouziva se jako pre-fill v form polozce pro fresh install. User muze prepsat.
// Format: "garaz-XXXXXX-YYYYYY" (12 hex chars z MAC + chip suffix = ~10^14 kombinaci).
String default_ntfy_topic() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[40];
  snprintf(buf, sizeof(buf), "garaz-%02X%02X%02X-%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

void led_ap_tick() {
  uint32_t now = millis();
  if (now - last_led_burst < AP_LED_BURST_PERIOD_MS) return;
  last_led_burst = now;
  for (uint8_t i = 0; i < AP_LED_BURST_COUNT; i++) {
    led_show(0, 0, 80);    // modra
    delay(AP_LED_PULSE_MS);
    led_off();
    delay(AP_LED_PULSE_MS);
  }
}

String html_escape(const String& s) {
  String out;
  out.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '&':  out += "&amp;";  break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;   // attributes pouzivame s ' delimitery
      default:   out += c;
    }
  }
  return out;
}

// HTML form. Pri prvnim load NVS prazdny → fieldy default z config.h.
// Pri double-RST: NVS uz vymazano, ale uzivatel mozna chce zachovat stare
// hodnoty. (V soucasnem designu cfg_clear je vola PRED run_ap_mode, takze
// cfg je defaultni — to je OK, user prepis z hlavy nebo z poznamek.)
String build_form_html(const String& wifi_scan_options, const String& status_msg) {
  String h;
  h.reserve(4096);
  h += F("<!DOCTYPE html><html lang=cs><head><meta charset=utf-8>");
  h += F("<meta name=viewport content='width=device-width,initial-scale=1'>");
  h += F("<title>Hlidac vrat — konfigurace</title>");
  h += F("<style>");
  h += F("body{font-family:system-ui,sans-serif;max-width:480px;margin:1em auto;padding:0 1em;color:#222;}");
  h += F("h1{font-size:1.2em;}h2{font-size:1.05em;margin-top:1.5em;border-top:1px solid #ddd;padding-top:1em;}");
  h += F("label{display:block;margin:0.6em 0 0.2em;font-size:0.9em;color:#555;}");
  h += F("input,select{width:100%;padding:0.5em;font-size:1em;box-sizing:border-box;border:1px solid #bbb;border-radius:4px;}");
  h += F("button{margin-top:1.5em;padding:0.8em;width:100%;font-size:1em;background:#0066cc;color:#fff;border:0;border-radius:4px;cursor:pointer;}");
  h += F("button:hover{background:#0055aa;}");
  h += F(".msg{padding:0.6em;border-radius:4px;margin:0.8em 0;}");
  h += F(".err{background:#ffe0e0;border:1px solid #c00;}");
  h += F(".ok{background:#e0ffe0;border:1px solid #0a0;}");
  h += F(".hint{font-size:0.8em;color:#777;margin-top:0.2em;}");
  h += F("</style></head><body>");
  h += F("<h1>Hlídač vrat — konfigurace</h1>");
  if (status_msg.length()) {
    h += status_msg;
  }
  h += F("<form method=POST action='/save'>");

  h += F("<h2>Síť</h2>");
  h += F("<label>WiFi síť (SSID)</label>");
  h += F("<select name=wifi_ssid>");
  h += wifi_scan_options;
  h += F("</select>");
  h += F("<div class=hint>Pokud chybí, zkus znova načíst stránku.</div>");

  h += F("<label>Heslo WiFi</label>");
  h += F("<input id=pwd name=wifi_pass type=password autocomplete=off>");
  h += F("<label style='font-weight:normal;color:#555;cursor:pointer'>");
  h += F("<input type=checkbox style='width:auto;margin-right:.4em;vertical-align:middle' ");
  h += F("onclick=\"var p=document.getElementById('pwd');p.type=this.checked?'text':'password'\"> ");
  h += F("Zobrazit heslo</label>");

  h += F("<label>ntfy.sh topic</label>");
  h += F("<input name=ntfy_topic value='");
  h += html_escape(cfg.configured ? cfg.ntfy_topic : default_ntfy_topic());
  h += F("'>");
  h += F("<div class=hint>Topic je <b>veřejný kanál</b> přes který chodí notifikace. Kdokoli s jeho znalostí může číst tvoje zprávy. Předvyplněný je náhodný (z MAC adresy), můžeš nechat nebo nahradit vlastním unikátním (10+ znaků, ne slovník).<br><b>Zapamatuj si ho</b> — budeš ho potřebovat zadat v mobilní aplikaci ntfy.sh, abys notifikace dostával.</div>");

  h += F("<label>Název zařízení</label>");
  h += F("<input name=device_name value='");
  h += html_escape(cfg.device_name);
  h += F("'>");

  h += F("<h2>Chování</h2>");

  h += F("<label>Začátek nočního okna (hodina 0–23)</label>");
  h += F("<input name=night_start type=number min=0 max=23 value='");
  h += String(cfg.night_start_hour);
  h += F("'>");
  h += F("<label>Konec nočního okna (hodina 0–23)</label>");
  h += F("<input name=night_end type=number min=0 max=23 value='");
  h += String(cfg.night_end_hour);
  h += F("'>");
  h += F("<div class=hint>Alerty se posílají pouze v tomto okně.</div>");

  h += F("<label>Hodina denní kontroly (heartbeat)</label>");
  h += F("<input name=hb_hour type=number min=0 max=23 value='");
  h += String(cfg.heartbeat_hour);
  h += F("'>");
  h += F("<div class=hint>Jednou denně v tuto hodinu zařízení pošle zprávu &bdquo;žiju&ldquo; s aktuálním stavem (baterie, signál, doba běhu). Pokud zpráva někdy nedorazí, zařízení má problém — typicky vybitou baterii nebo WiFi výpadek. Doporučená hodina je krátce před začátkem nočního okna, abys měl jistotu že systém v noci funguje.</div>");

  h += F("<label>Interval kontroly (sekundy)</label>");
  h += F("<input name=wake_int type=number min=10 max=3600 value='");
  h += String(cfg.wake_interval_s);
  h += F("'>");
  h += F("<div class=hint>Doporučeno 300 (5 min). Menší = rychlejší detekce, kratší výdrž baterie.</div>");

  h += F("<button type=submit>Uložit a otestovat WiFi</button>");
  h += F("</form></body></html>");
  return h;
}

String wifi_scan_html() {
  Serial.println(F("[ap] scanning WiFi..."));
  int n = WiFi.scanNetworks();
  Serial.printf("[ap] found %d networks\n", n);
  String opts;
  opts.reserve(2048);
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;   // hidden — skip
    int rssi = WiFi.RSSI(i);
    opts += F("<option value='");
    opts += html_escape(ssid);
    opts += F("'>");
    opts += html_escape(ssid);
    opts += F(" (");
    opts += rssi;
    opts += F(" dBm)</option>");
  }
  WiFi.scanDelete();
  return opts;
}

void handle_root() {
  Serial.printf("[ap] HTTP GET / from %s\n", server.client().remoteIP().toString().c_str());
  String opts = wifi_scan_html();
  server.send(200, "text/html; charset=utf-8", build_form_html(opts, ""));
  Serial.println(F("[ap] form sent"));
}

bool test_wifi_connect(const char* ssid, const char* pass) {
  Serial.printf("[ap] test connect to '%s'...\n", ssid);
  WiFi.mode(WIFI_AP_STA);   // AP zustane behem testu — uzivatel se nedostane z formu jinak
  WiFi.begin(ssid, pass);
  uint32_t t0 = millis();
  while ((millis() - t0) < WIFI_TEST_TIMEOUT_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[ap] OK ip=%s rssi=%d\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      WiFi.disconnect(true, false);    // disconnect STA jen, AP necht
      WiFi.mode(WIFI_AP);
      return true;
    }
    delay(250);
  }
  Serial.println(F("[ap] test FAIL"));
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_AP);
  return false;
}

void handle_save() {
  Serial.printf("[ap] HTTP POST /save from %s\n", server.client().remoteIP().toString().c_str());
  String ssid       = server.arg("wifi_ssid");
  String pass       = server.arg("wifi_pass");
  String topic      = server.arg("ntfy_topic");
  String dev_name   = server.arg("device_name");
  int   night_st    = server.arg("night_start").toInt();
  int   night_en    = server.arg("night_end").toInt();
  int   hb_hr       = server.arg("hb_hour").toInt();
  uint32_t wake_int = static_cast<uint32_t>(server.arg("wake_int").toInt());

  Serial.printf("[ap] form submit: ssid='%s' topic='%s' wake=%us\n",
                ssid.c_str(), topic.c_str(), wake_int);

  // Validace — povinna pole + delky podle WiFi standard a NVS buffer.
  // Bez delkove kontroly by se strncpy tise zkratil, ale test_wifi_connect by
  // pracoval s plnou stringou → save by ulozil jine heslo nez to testovane,
  // priste boot by failoval s "spatne heslo".
  String err;
  if (ssid.length() == 0 || pass.length() == 0 || topic.length() == 0) {
    err = F("Vyplň SSID, heslo i ntfy topic.");
  } else if (ssid.length() > 32) {
    err = F("SSID je delší než 32 znaků (WiFi limit).");
  } else if (pass.length() > 63) {
    err = F("Heslo je delší než 63 znaků (WPA2 limit).");
  } else if (topic.length() > CFG_STR_MAX - 1) {
    err = F("Topic je příliš dlouhý.");
  } else if (dev_name.length() > CFG_STR_MAX - 1) {
    err = F("Název zařízení je příliš dlouhý.");
  }
  // Clamp numericke fieldy (HTML min/max je jen client-side, server-side prepis)
  if (night_st < 0 || night_st > 23) night_st = 22;
  if (night_en < 0 || night_en > 23) night_en = 6;
  if (hb_hr   < 0 || hb_hr   > 23) hb_hr   = 21;
  if (wake_int < 10)    wake_int = 10;
  if (wake_int > 3600)  wake_int = 3600;

  if (err.length() > 0) {
    String msg = F("<div class=msg err>");
    msg += err;
    msg += F("</div>");
    server.send(400, "text/html; charset=utf-8", build_form_html(wifi_scan_html(), msg));
    return;
  }

  // Test connect (nezavisly na AP)
  if (!test_wifi_connect(ssid.c_str(), pass.c_str())) {
    String err = F("<div class=msg err>WiFi se nepodařilo připojit. Zkontroluj SSID a heslo.</div>");
    server.send(200, "text/html; charset=utf-8", build_form_html(wifi_scan_html(), err));
    return;
  }

  // Uloz do NVS
  strncpy(cfg.wifi_ssid,   ssid.c_str(),     CFG_STR_MAX - 1);
  strncpy(cfg.wifi_pass,   pass.c_str(),     CFG_STR_MAX - 1);
  strncpy(cfg.ntfy_topic,  topic.c_str(),    CFG_STR_MAX - 1);
  strncpy(cfg.device_name, dev_name.c_str(), CFG_STR_MAX - 1);
  cfg.wifi_ssid[CFG_STR_MAX-1] = '\0';
  cfg.wifi_pass[CFG_STR_MAX-1] = '\0';
  cfg.ntfy_topic[CFG_STR_MAX-1] = '\0';
  cfg.device_name[CFG_STR_MAX-1] = '\0';

  cfg.night_start_hour  = static_cast<uint8_t>(night_st);
  cfg.night_end_hour    = static_cast<uint8_t>(night_en);
  cfg.heartbeat_hour    = static_cast<uint8_t>(hb_hr);
  cfg.wake_interval_s   = wake_int;

  cfg_save();

  String ok = F("<!DOCTYPE html><html><head><meta charset=utf-8>"
                "<title>Uloženo</title>"
                "<style>body{font-family:system-ui;max-width:480px;margin:2em auto;padding:0 1em;text-align:center}</style>"
                "</head><body><h1>✅ Uloženo</h1>"
                "<p>WiFi otestována, konfigurace uložena.</p>"
                "<p>Zařízení se restartuje. Připoj telefon zpět na domácí WiFi.</p>"
                "</body></html>");
  server.send(200, "text/html; charset=utf-8", ok);
  server.client().flush();
  delay(500);
  Serial.println(F("[ap] saved + restart"));
  ESP.restart();
}

void handle_not_found() {
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

}  // namespace

void run_ap_mode() {
  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F("=== AP CONFIG MODE ==="));
  Serial.println(F("================================================"));

  String ssid = ap_ssid();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid.c_str());

  Serial.printf("[ap] SSID:    %s  (otevrene, bez hesla)\n", ssid.c_str());
  Serial.printf("[ap] IP:      %s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("[ap] URL:     http://%s/\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("[ap] MAC:     %s\n", WiFi.softAPmacAddress().c_str());
  Serial.printf("[ap] timeout: %lu min\n",
                static_cast<unsigned long>(AP_TIMEOUT_MS / 60000UL));
  Serial.println(F("[ap] LED: trojity blink modra kazde 3 s"));
  Serial.println(F("[ap] cekam na klienta..."));

  // AP_STACONNECTED / AP_STADISCONNECTED eventy — vidime kdyz se telefon
  // pripoji / odpoji od naseho AP (bez vlivu na HTTP POST handler).
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
      Serial.printf("[ap] klient pripojen MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
                    info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
                    info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
                    info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5]);
    } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
      Serial.println(F("[ap] klient odpojen"));
    }
  });

  server.on("/",        HTTP_GET,  handle_root);
  server.on("/save",    HTTP_POST, handle_save);
  server.onNotFound(handle_not_found);
  server.begin();

  uint32_t start = millis();
  uint32_t last_heartbeat = start;
  last_led_burst = start - AP_LED_BURST_PERIOD_MS;   // hned prvni blink

  while ((millis() - start) < AP_TIMEOUT_MS) {
    server.handleClient();
    led_ap_tick();

    uint32_t elapsed = millis() - start;
    if ((millis() - last_heartbeat) >= AP_LOG_HEARTBEAT_MS) {
      last_heartbeat = millis();
      uint32_t left_s = (AP_TIMEOUT_MS - elapsed) / 1000UL;
      Serial.printf("[ap] cekam na klienta, %lu s do timeoutu (clients=%u)\n",
                    static_cast<unsigned long>(left_s), WiFi.softAPgetStationNum());
    }
    delay(5);
  }

  Serial.printf("[ap] TIMEOUT po %lu s bez submitu — deep sleep %u s, AP znovu pri wake\n",
                static_cast<unsigned long>(AP_TIMEOUT_MS / 1000UL),
                cfg.wake_interval_s);
  led_off();
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  periph_power_off_for_sleep();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(cfg.wake_interval_s) * 1000000ULL);
  esp_deep_sleep_start();
}
