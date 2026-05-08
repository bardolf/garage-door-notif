// Garage door PoC — NTP + periodicky status (cas + Vbat) na ntfy.sh
//
// Krok 1 (boot):  WiFi connect, NTP sync, jednou poslat boot notifikaci
// Krok 2 (loop):  kazdou minutu poslat status (cas + Vbat + RSSI + uptime)

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "lwip/dns.h"

// ---- konfigurace ----
const char* WIFI_SSID  = "skybit.cz.guest";
const char* WIFI_PASS  = "vitejteunemcu";
const char* NTFY_TOPIC = "milan-garaz-2026-x8kf3pq7vn";

// CZ timezone (POSIX format) — automaticky resi CET/CEST DST
const char* TZ_CZ = "CET-1CEST,M3.5.0,M10.5.0/3";

// NTP servery
const char* NTP1 = "cz.pool.ntp.org";
const char* NTP2 = "pool.ntp.org";
const char* NTP3 = "time.google.com";

// FireBeetle ESP32-E V1.0: vestavena baterie divider na GPIO34
#define VBAT_PIN       34
#define VBAT_DIVIDER   2.0f
// analogReadMilliVolts() interne pouziva esp_adc_cal s factory eFuse Vref,
// takze zadna externi korekce nestaci. Doladit jen pokud realna chyba > 2 %.
#define VBAT_CALIBRATION 1.000f

#define LED_PIN 2

#define WIFI_TIMEOUT_MS   15000
#define NTP_TIMEOUT_MS    10000
#define STATUS_INTERVAL_MS (60UL * 1000UL)   // 1 minuta

// ---- WiFi ----
bool wifi_connect() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("[wifi] connecting to %s ", WIFI_SSID);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_TIMEOUT_MS) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[wifi] FAIL");
        return false;
    }

    Serial.printf("[wifi] OK ip=%s rssi=%d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());

    // override DHCP DNS — local resolver neprekladá public domeny
    ip_addr_t cf, ggl;
    IP_ADDR4(&cf,  1, 1, 1, 1);
    IP_ADDR4(&ggl, 8, 8, 8, 8);
    dns_setserver(0, &cf);
    dns_setserver(1, &ggl);
    return true;
}

// ---- NTP ----
bool ntp_sync() {
    Serial.printf("[ntp] syncing via %s ... ", NTP1);
    configTzTime(TZ_CZ, NTP1, NTP2, NTP3);

    unsigned long start = millis();
    time_t now = 0;
    while ((millis() - start) < NTP_TIMEOUT_MS) {
        time(&now);
        if (now > 1700000000) {  // ~ 2023-11-15, sanity check
            struct tm tm_local;
            localtime_r(&now, &tm_local);
            char buf[64];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &tm_local);
            Serial.printf("OK -> %s\n", buf);
            return true;
        }
        delay(200);
    }
    Serial.println("TIMEOUT");
    return false;
}

// ---- Vbat ----
// Pouziva analogReadMilliVolts() ktery vnitrne aplikuje esp_adc_cal
// (factory eFuse Vref calibration) — typicky chyba < 2 %
float read_vbat() {
    uint64_t sum_mv = 0;
    const int N = 32;
    for (int i = 0; i < N; i++) {
        sum_mv += analogReadMilliVolts(VBAT_PIN);
        delay(2);
    }
    float v_pin = (sum_mv / (float)N) / 1000.0f;
    return v_pin * VBAT_DIVIDER * VBAT_CALIBRATION;
}

// ---- ntfy ----
bool ntfy_send(const char* title, const char* body,
               const char* priority, const char* tags) {
    HTTPClient http;
    String url = String("http://ntfy.sh/") + NTFY_TOPIC;
    http.begin(url);
    http.addHeader("Content-Type", "text/plain; charset=utf-8");
    if (title)    http.addHeader("Title",    title);
    if (priority) http.addHeader("Priority", priority);
    if (tags)     http.addHeader("Tags",     tags);

    int code = http.POST((uint8_t*)body, strlen(body));
    http.end();

    Serial.printf("[ntfy] %s -> HTTP %d\n", title, code);
    return code == 200;
}

void send_status() {
    time_t now;
    time(&now);
    struct tm tm_local;
    localtime_r(&now, &tm_local);

    char timestr[32];
    strftime(timestr, sizeof(timestr), "%H:%M:%S", &tm_local);

    float vbat = read_vbat();
    int rssi = WiFi.RSSI();
    unsigned long uptime_min = millis() / 60000UL;

    char body[200];
    snprintf(body, sizeof(body),
             "%s | bat %.2fV | RSSI %d dBm | up %lu min",
             timestr, vbat, rssi, uptime_min);

    const char* tag = (vbat > 3.7) ? "battery" :
                      (vbat > 3.4) ? "yellow_circle" : "red_circle";

    ntfy_send("Status", body, "low", tag);
}

// ---- main ----
void setup() {
    pinMode(LED_PIN, OUTPUT);
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("=== ntfy.sh PoC: NTP + periodicky status ===");
    Serial.printf("Topic: %s\n", NTFY_TOPIC);

    if (!wifi_connect()) {
        Serial.println("[fatal] no wifi -> halt");
        while (true) {
            digitalWrite(LED_PIN, HIGH); delay(80);
            digitalWrite(LED_PIN, LOW);  delay(80);
        }
    }

    if (!ntp_sync()) {
        Serial.println("[warn] NTP timeout, casy budou nepresne");
    }

    // boot notifikace
    char body[160];
    snprintf(body, sizeof(body),
             "Boot OK. RSSI=%d dBm IP=%s Vbat=%.2fV",
             WiFi.RSSI(), WiFi.localIP().toString().c_str(), read_vbat());
    ntfy_send("ESP32 boot", body, "default", "rocket");
}

void loop() {
    static unsigned long last_status_ms = 0;

    unsigned long now_ms = millis();
    if (now_ms - last_status_ms >= STATUS_INTERVAL_MS || last_status_ms == 0) {
        last_status_ms = now_ms;
        send_status();
    }

    // heartbeat blink 1 Hz
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    delay(950);
}
