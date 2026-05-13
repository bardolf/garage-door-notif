// LaskaKit ESP32-C3-LPKit — WiFi RSSI test
//
// Ucel: po vymene/preletovani U.FL pigtailu overit, ze anteni cesta funguje.
// Pripoji se na stejnou sit jako ntfy debug a kazdych 5 s vypise RSSI + dalsi
// linkove parametry. Slabe nebo silne kolisajici RSSI = podezreni na studeny
// spoj / vadny pigtail / spatne dotazenou antenu.
//
// Orientacni hodnoty (par metru od AP):
//   OK :  -40 az -60 dBm, stabilni (+- 3 dB)
//   horsi: -65 az -75 dBm
//   spatne: < -80 dBm, velke vykyvy, casty disconnect
//
// Build/upload: pio run -e c3wifi -t upload -t monitor

#include <Arduino.h>
#include <WiFi.h>

const char* WIFI_SSID = "skybit.cz.guest";
const char* WIFI_PASS = "vitejteunemcu";

#define PERIPH_EN_PIN     4
#define WIFI_TIMEOUT_MS   15000
#define STATUS_INTERVAL_MS 5000

static uint32_t loop_count = 0;

static const char* status_str(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS:     return "IDLE";
    case WL_NO_SSID_AVAIL:   return "NO_SSID";
    case WL_SCAN_COMPLETED:  return "SCAN_DONE";
    case WL_CONNECTED:       return "CONNECTED";
    case WL_CONNECT_FAILED:  return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED:    return "DISCONNECTED";
    default:                 return "UNKNOWN";
  }
}

static bool wifi_connect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[wifi] connecting to %s ", WIFI_SSID);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] OK  IP=%s  GW=%s  RSSI=%d dBm  ch=%d  BSSID=%s\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.gatewayIP().toString().c_str(),
                  WiFi.RSSI(),
                  WiFi.channel(),
                  WiFi.BSSIDstr().c_str());
    return true;
  }
  Serial.printf("[wifi] FAIL status=%s\n", status_str(WiFi.status()));
  return false;
}

void setup() {
  pinMode(PERIPH_EN_PIN, OUTPUT);
  digitalWrite(PERIPH_EN_PIN, HIGH);

  Serial.begin(115200);
  delay(2000);   // USB CDC enumerace

  Serial.println();
  Serial.println(F("=== LaskaKit ESP32-C3 WiFi RSSI test ==="));
  Serial.printf("SSID: %s\n", WIFI_SSID);
  Serial.println(F("Vypis kazdych 5 s. Sleduj stabilitu RSSI a pripadne disconnecty."));

  wifi_connect();
}

void loop() {
  loop_count++;
  uint32_t up_s = millis() / 1000;
  wl_status_t st = WiFi.status();

  if (st == WL_CONNECTED) {
    Serial.printf("[%5lus] #%lu  RSSI=%d dBm  ch=%d  IP=%s  status=%s\n",
                  (unsigned long)up_s,
                  (unsigned long)loop_count,
                  WiFi.RSSI(),
                  WiFi.channel(),
                  WiFi.localIP().toString().c_str(),
                  status_str(st));
  } else {
    Serial.printf("[%5lus] #%lu  DISCONNECTED  status=%s  -> reconnect\n",
                  (unsigned long)up_s,
                  (unsigned long)loop_count,
                  status_str(st));
    WiFi.disconnect();
    wifi_connect();
  }

  delay(STATUS_INTERVAL_MS);
}
