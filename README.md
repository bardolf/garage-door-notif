# Garage door notif

Battery-powered detekce otevřených garážových vrat v noci (22:00–06:00) přes ESP32 + MPU6050 + ntfy.sh push notifikace. Cílová životnost na 18650 baterii: **~1 rok mezi nabíjeními** (omezeno self-discharge baterky ~2–4 %/měsíc + kapacitní degradace + chlad v garáži, ne čistou spotřebou ESP).

**Dokumentace:**
- [`USAGE.md`](USAGE.md) — pro koncového uživatele (jak to funguje, co znamenají notifikace, FAQ)

---

## Use case

**Co řešíme:** Detekovat, jestli jsou garážová vrata **otevřená v noci mezi 22:00 a 06:00**, a poslat alert na mobil.

**Klíčový rozdíl proti motion detekci:** vrata mohou být otevřená už od 14:00 a zůstat tak — v 22:00 už žádný motion event nenastane. **Musíme polovat stav, ne čekat na změnu.**

**Design cíle:**
- Alert do ~5 min po 22:00, pokud jsou vrata otevřená
- Alert i během noci, pokud se otevřou
- Battery-powered, žádné kabelování ke garáži
- Životnost **~1 rok** mezi nabíjeními (realistický cíl, ne teoretické maximum)
- Žádný „false positive" z větru / vibrací

---

## Architektura

Timer-based polling (ne motion-driven). ESP probudí RTC každých 5 min, přečte tilt z MPU6050, rozhodne na základě času a stavu, případně pošle notifikaci, vrátí se do deep sleep.

```
COLD BOOT (power-on / RST)
   │
   ├─► WiFi + NTP sync
   ├─► MPU baseline capture (50 vzorků, průměr — předpoklad: vrata zavřená)
   ├─► Boot notifikace na ntfy.sh
   └─► ESP+MPU deep sleep, wake za 5 min
                    │
                    ▼
WAKE CYCLE (každých 5 min)
   │
   ├─► Wake MPU přes I²C (clear SLEEP bit, wait 30 ms)
   ├─► Read accel × 6 (zahod první, průměr ze zbylých 5)
   ├─► Spočti tilt vůči baseline (3D úhel)
   ├─► MPU zpět do SLEEP mode (5 µA)
   │
   ├─► Hystereze: open >60°, close <40° (stav v RTC mem)
   │
   ├─► Časová logika:
   │     ├─ 22:00-06:00 + open + rate limit OK (4 min) → URGENT alert
   │     ├─ hodina 21 + heartbeat dnes neodeslán      → daily heartbeat
   │     ├─ poslední NTP sync >24h                    → opportunistic refresh
   │     └─ MPU error queue má pendingy               → error report
   │
   ├─► Pokud potřeba WiFi: connect → ntp_sync (zadarmo) → poslat → disconnect
   └─► ESP+MPU deep sleep, wake za 5 min
```

### Klíčové principy

1. **Timer wake (RTC), ne motion wake (MPU INT)** — vrata stojí staticky, motion event se nestane.
2. **MPU drží v SLEEP mode (5 µA) mezi měřeními** — ESP probudí přes I²C jen na ~50 ms při wake.
3. **Hystereze + průměr** — proti vibracím / větru. První sample po wake je outlier (zahodit).
4. **Rate limiting alertů 4 min** — pri stuck-open vratech alert každý wake cyklus (~5 min), neoslepí appku ale informuje opakovaně.
5. **NTP sync 1× za 24 h + opportunistic** — pokaždé když už máme WiFi (alert/heartbeat), syncneme i čas zadarmo.
6. **State v RTC slow memory** — baseline, alert/heartbeat trackery, error queue, hysteresis stav přežívají deep sleep, ne power-off.
7. **Errors přes ntfy** — jediný komunikační kanál, takže senzor failures se queueují a posílají při příští WiFi příležitosti.

---

## Hardware

### BOM

| Položka | Stav | Pozn. |
|---|---|---|
| **DFRobot FireBeetle ESP32-E V1.0** (board ID `dfrobot_firebeetle2_esp32e`) | ✅ aktuální PoC | ESP32 + WiFi + integrovaný TP4056 charger + JST-PH 2.0. CH340 USB-Serial má ~430 µA leak — viz [Spotřeba](#spotřeba). |
| **[LaskKit ESP-12 board](https://www.laskakit.cz/laskkit-esp-12-board/)** | 🔄 plánovaná migrace | ESP32-C3, nativní USB-CDC (žádný CH340), cíl ~20 µA sleep — viz [Migrace na ESP32-C3](#migrace-na-esp32-c3). **Pozor: existují 2 varianty desky** — jedna s integrovanou PCB anténou (plug-and-play), druhá s u.FL/IPEX konektorem pro **externí anténu + pigtail kabel** (nutno dokoupit zvlášť). Při objednávce ověřit kterou variantu máš; pokud externí, dokoupit anténu 2,4 GHz + u.FL pigtail. |
| **GY-521 MPU6050** (akcelerometr) | ✅ funguje | 6-axis IMU, I²C, on-board 3,3 V LDO + 4,7k pull-upy |
| **[LaskKit GeB Li-Ion 18650 1S1P 3,7 V 3200 mAh](https://www.laskakit.cz/geb-li-ion-baterie-1x18650-1s1p-3-7v-3200mah/)** | ✅ zvolená baterie | 18650 cell s integrovanou ochranou (over-discharge / over-current). Nabíjí se přes USB FireBeetle (TP4056). Ověřit JST-PH 2.0 konektor / případně zalisovat. |
| **DuPont kabely female-female ×4** | ✅ mám | propojení MPU ↔ ESP (VCC, GND, SDA, SCL) |
| **Lepidlo / 3D tištěný držák** | 🟡 vyřešit | MPU musí být pevně přichycený k vratům, ne k rámu |

### Pin allocation

| GPIO | Funkce | Pozn. |
|---|---|---|
| 2 | onboard LED | output, active HIGH |
| 21 | I²C SDA → GY-521 SDA | default ESP32 I²C |
| 22 | I²C SCL → GY-521 SCL | default ESP32 I²C |
| 34 | ADC1 — Vbat sense | on-board divider ×2, použít `analogReadMilliVolts()` |

### Zapojení MPU6050 ↔ ESP

| GY-521 pin | FireBeetle pin |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |
| INT, AD0, XCL, XDA | nezapojeno (timer wake, ne motion; AD0=LOW → adresa 0x68) |

---

## Jak to spustit

| Cíl | Příkaz |
|---|---|
| **Finální app** (default) | `pio run -t upload -t monitor` |
| Jen build (bez upload) | `pio run` |
| Jen monitor (bez flash) | `pio device monitor` |
| Vyčistit build cache | `pio run -t clean` |

Port: `/dev/ttyUSB0`. Baud: 115200.

Debug moduly a vývojářské nástroje viz sekce [Debug — vývojářské nástroje](#debug--vývojářské-nástroje) na konci.

---

## Struktura projektu

```
src/main.cpp              ← finální integrovaná aplikace (env: app)
debug/
  ntfy/main.cpp           ← WiFi + NTP + ntfy + Vbat status loop
  tilt/main.cpp           ← MPU6050 baseline + tilt monitoring
  sleep/main.cpp          ← ESP+MPU deep sleep + RTC mem
include/
  secrets.h               ← WiFi heslo, ntfy topic (NEVERZOVÁNO)
  secrets.h.example       ← šablona
platformio.ini            ← envs: app (default), tilt, ntfy, sleep
USAGE.md                  ← uživatelský návod
```

Filozofie: každý debug modul je samostatně flashovatelný firmware, izolovaně testuje jednu věc. Finální `src/main.cpp` skládá funkce z odladěných modulů.

---

## Co dělá hlavní app

State persistovaný přes deep sleep v RTC slow memory (`RTC_DATA_ATTR`):
boot_count, baseline vector, door_was_open, last_alert_unix, last_heartbeat_yday, last_ntp_unix, error queue (count + last message).

**Cold boot** (po zapnutí / RST):
1. WiFi connect + NTP sync (CET/CEST automaticky přes POSIX TZ)
2. MPU6050 baseline capture (50 vzorků, průměr, předpoklad zavřených vrat)
3. Boot notifikace nebo error report na ntfy.sh
4. ESP+MPU deep sleep, wake za 5 min

**Wake cyklus** (každých 5 min) — viz [Architektura](#architektura) výše.

Detaily v hlavičce `src/main.cpp`.

### Typy notifikací

| Událost | Title | Priority | Tag |
|---|---|---|---|
| Cold boot | Hlidac garazovych vrat aktivni | default | rocket |
| Vrata otevřená 22-06 | GARAZ OTEVRENA! | **urgent** | rotating_light |
| Denní kontrola 21:00 | Garaz - denni kontrola | low | white_check_mark / yellow_circle / battery |
| Senzor / baseline error | Hlidac vrat - CHYBA | high | warning |

Detaily a uživatelský pohled v [`USAGE.md`](USAGE.md).

---

## Secrets

Citlivé údaje (WiFi heslo, ntfy topic) jsou v `include/secrets.h`, **neverzuje se** (.gitignored). Pro nový clone:

```bash
cp include/secrets.h.example include/secrets.h
# uprav WIFI_SSID, WIFI_PASS, NTFY_TOPIC v include/secrets.h
```

Soubor se kompiluje přímo do binárky přes `#include "secrets.h"` v `src/main.cpp`.

---

## ntfy.sh

- Topic: `milan-garaz-2026-x8kf3pq7vn` (random, regenerovat před deploymentem)
- Free tier: 60 burst / replenish 1 každých 5 s = ~17k/den max
- Web subscriber pro test bez appky: `https://ntfy.sh/<topic>`
- Server-side history (12 h): `curl 'https://ntfy.sh/<topic>/json?poll=1&since=1h'`

```bash
# poslat testovací zprávu
curl -d "test" -H "Title: Test" ntfy.sh/milan-garaz-2026-x8kf3pq7vn

# přečíst historii za poslední hodinu
curl -s "https://ntfy.sh/milan-garaz-2026-x8kf3pq7vn/json?poll=1&since=1h"
```

---

## Spotřeba

### Teoretická predikce (z původního design doc)

| Komponenta | Spotřeba | Pozn. |
|---|---|---|
| ESP32 deep sleep | ~13 µA | DFRobot claim pro FireBeetle E |
| MPU6050 sleep mode | ~5 µA | 99,9 % času |
| Wake event (no WiFi) | ~50 mA × 200 ms | I²C read MPU |
| Alert / heartbeat (s WiFi) | ~80 mA × 5 s | NTP + HTTP POST |

**Denní budget** (předpoklad: 288 wake/den, ~5 alert/heartbeat events/den):

| Položka | Výpočet | Energie/den |
|---|---|---|
| ESP32 deep sleep | 24 h × 13 µA × 3,3 V | 1,0 mWh |
| MPU sleep | 24 h × 5 µA × 3,3 V | 0,4 mWh |
| 288 wake × 0,2 s | 50 mA × 3,3 V × 57,6 s | 2,6 mWh |
| 5 WiFi events × 5 s | 80 mA × 3,3 V × 25 s | 1,8 mWh |
| **Σ teoretický cíl** | | **~6 mWh/den** |

**Teoretické maximum** (čistě výpočet ze spotřeby, ESP32-C3 + odpájené LED):

| Baterie | Kapacita | Teoretická životnost |
|---|---|---|
| 1000 mAh LiPo | ~3,7 Wh | ~1,7 roku |
| **18650 3200 mAh** ([LaskKit GeB](https://www.laskakit.cz/geb-li-ion-baterie-1x18650-1s1p-3-7v-3200mah/)) | ~11,8 Wh | ~5,5 let |

**Realistický cíl: ~1 rok mezi nabíjeními** na 18650. Teoretické maximum je výrazně optimistické, reálnou životnost limitují:

- **Self-discharge baterie** ~2–4 %/měsíc = ~25–40 % kapacity ztracené čistě sezením za rok
- **Kapacitní degradace Li-Ion** ~3–5 %/rok stárnutím (i bez cyklování)
- **Chlad v zimní garáži** (0 °C) snižuje efektivní kapacitu o ~20–30 %
- **Protection cutoff baterky** zastaví ESP při ~3,0 V, nevyužije se posledních ~5–10 % kapacity
- **Reálné wake events** typicky 2–3× teoretická hodnota (retries, error events, NTP timeouty)

➜ Plánovat **nabíjení 1× ročně** je realistický cíl. „5,5 let" je horní strop matematiky, ne reálné očekávání.

### Naměřené hodnoty (FireBeetle ESP32-E)

Setup: baterie přes JST-PH 2.0 → multimetr v sérii → FireBeetle. Bez USB. `debug/sleep/main.cpp` s `AWAKE_HOLD_MS = 5000`, `SLEEP_DURATION_S = 30`.

| Konfigurace | Active hold | Deep sleep | Pozn. |
|---|---|---|---|
| FireBeetle + GY-521 (LED on) | 37,9 mA | **1,91 mA** | LED dominuje |
| FireBeetle + GY-521 (LED **odpájena**) | 36,5 mA | **0,47 mA** | −1,44 mA z LED |
| FireBeetle samotný (MPU odpojen) | — | **0,43 mA** | GY-521 v sleep tahala 40 µA |

**Závěr:** ze 430 µA na FireBeetle samotném je **~417 µA leak mimo ESP32 chip** (claim 13 µA). Hlavní podezřelý: **CH340 USB-Serial čip** v suspend modu (typicky 150–300 µA bez USB) + AMS1117 LDO quiescent + drobné peripherály. **Hardware-side limit FireBeetle ESP32-E V1.0**, neopravitelné firmwarem.

**Reálná životnost na FireBeetle při 0,47 mA sleep:**
- 0,47 mA × 24 h ≈ 11 mAh/den
- 1000 mAh LiPo → ~88 dní (~3 měsíce)
- **18650 3200 mAh → ~283 dní (~9 měsíců)**

Pro PoC akceptovatelné. Pro **~1 rok mezi nabíjeními** je nutná migrace na desku bez CH340 (viz [Migrace na ESP32-C3](#migrace-na-esp32-c3)) — bez ní jsme limitovaní na ~9 měsíců i s ideálními podmínkami, v zimě / s degradovanou baterií spadne na ~6 měsíců.

**Co je proven:**
- ✅ ESP32 deep sleep funguje (FireBeetle E claim 13 µA, fakticky neviditelné při 430 µA limitu desky)
- ✅ MPU I²C SLEEP funguje (žádná detekovatelná spotřeba navíc, < 10 µA)
- ✅ GPIO2 LED — programovatelná, off-stav = 0 mA, **netřeba odpájet** (firmware ji prostě nedriverуje HIGH)
- ✅ GY-521 zelená LED — naměřeno 1,44 mA always-on, **odpájet před deploymentem**
- ✅ Vbat měření: `analogReadMilliVolts()` s eFuse calibration → chyba <0,5 % vs multimetr (žádný externí faktor netřeba)

### Naměřené hodnoty (LaskaKit ESP32-C3-LPKit v4)

Setup: baterie přes JST → multimetr v sérii → C3-LPKit v4 + GY-521 (zelená LED odpájena, MPU napájena přes `PERIPH_EN` rail = GPIO4). Firmware: `debug/c3sleep/main.cpp` (`AWAKE_HOLD_MS = 5000`, `SLEEP_DURATION_S = 30`, NeoPixel brightness 40/255 jasně červená). `gpio_hold_en(GPIO4)` drží `PERIPH_EN=LOW` přes deep sleep → MPU a NeoPixel rail odpojený.

| Konfigurace | Active hold | Deep sleep | Pozn. |
|---|---|---|---|
| C3-LPKit v4 + GY-521 (NeoPixel on) | **24 mA** | **19 µA** | MPU napájena z PERIPH_EN, neuspána přes I²C |

Active proud (24 mA) = ESP32-C3 active ~10 mA + NeoPixel ~10 mA + MPU6500 active ~4 mA. Při deep sleep `PERIPH_EN=LOW` odřízne NeoPixel i MPU rail → vidíš čistě C3 + LDO + charger IC quiescent.

### Porovnání FireBeetle vs. LaskaKit C3-LPKit v4

| Metrika | FireBeetle (LED odpájena) | C3-LPKit v4 | Zlepšení |
|---|---:|---:|---:|
| Active (5 s hold, LED on) | 36,5 mA | 24 mA | **−34 %** |
| Deep sleep | 470 µA | 19 µA | **−96 % (25× méně)** |
| Sleep mAh/den | 11,3 | **0,46** | −24× |
| Sleep life (1000 mAh) | ~88 dní | ~2 200 dní (~6 let)* | |
| Sleep life (18650 3200 mAh) | ~283 dní | ~7 000 dní (~19 let)* | |

*Pure-sleep teoretické maximum (bez wake events). V praxi limitované self-discharge a kapacitní degradací — viz Wake interval tabulka níže.

**Co C3 vyhrává:** absence CH340 USB-Serial čipu (~150–300 µA leak) a moderní low-quiescent LDO. Hardware-side limit FireBeetle (~430 µA) na C3 zmizel; sleep proud je teď v řádu, kde dominuje **self-discharge baterie**, ne deska.

**Co C3 nevyhrává:** active spotřeba CPU je nižší ale ne dramaticky (RISC-V vs Xtensa dual-core), WiFi peak je stejný řád (~150–200 mA). Migraci ospravedlňuje téměř výhradně sleep proud.

### Wake interval vs. životnost (projekce pro ESP32-C3)

Otázka: jak se mění životnost, pokud probouzím častěji? Předpoklad: počet odeslaných zpráv (alerty + heartbeat) zůstává konstantní ~5/den, mění se jen frekvence MPU read cyklů bez WiFi.

**Vstupy** (z naměřených / teoretických hodnot výše):
- Wake bez WiFi: 50 mA × 0,2 s = **0,00278 mAh/wake**
- WiFi event: 80 mA × 5 s = 0,111 mAh × 5/den = **0,56 mAh/den**
- Sleep ESP32-C3 + MPU rail off (PERIPH_EN=LOW) **naměřeno 19 µA** × 24 h = **0,46 mAh/den**
- Self-discharge 18650 (3 %/měs ze 3200 mAh) = **3,2 mAh/den**
- Kapacita: 3200 mAh

| Wake int. | Wakes/den | Wake E [mAh] | Sleep [mAh] | WiFi [mAh] | Self-disch. [mAh] | **Σ/den** | **Životnost** |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 min  | 1440 | 4,00 | 0,48 | 0,56 | 3,2 | **8,24** | ~388 dní (~1,06 r) |
| 2 min  | 720  | 2,00 | 0,48 | 0,56 | 3,2 | **6,24** | ~513 dní (~1,40 r) |
| 5 min  | 288  | 0,80 | 0,48 | 0,56 | 3,2 | **5,04** | ~635 dní (~1,74 r) |
| 10 min | 144  | 0,40 | 0,48 | 0,56 | 3,2 | **4,64** | ~690 dní (~1,89 r) |
| 20 min | 72   | 0,20 | 0,48 | 0,56 | 3,2 | **4,44** | ~721 dní (~1,98 r) |
| 30 min | 48   | 0,13 | 0,48 | 0,56 | 3,2 | **4,37** | ~732 dní (~2,00 r) |

**Insight: self-discharge ~3,2 mAh/den je floor, který nelze podlézt.** Sleep + zprávy přidají dalších ~1,0 mAh/den (taky fixní). Variabilní část = wake events:

- **5 min** (aktuální): 0,80 mAh/den, +25 % nad floor — rozumný kompromis
- **2 min**: 2,0 mAh/den, +62 % nad floor — znatelný hit (cena: −24 % životnosti vs. 5 min)
- **1 min**: 4,0 mAh/den, +125 % nad floor — wake events už dominují nad samovybíjením

➜ Mezi „self-discharge převažuje" a „wake events převažují" je hranice **někde mezi 2 a 5 min wake intervalem**. Pod 5 min jdeme rapidně dolů, nad 5 min už další zpomalování přínos nedává (rozdíl 5 → 30 min je jen ~15 % životnosti).

**Caveats:** kalkulace neuvažují kapacitní degradaci (~3–5 %/r), chlad v zimě (−20–30 % efektivní kapacity) ani stuck-open scénář (vrata nechaná otevřená přes noc → ~120 alertů přes rate-limit → ~13 mAh extra v jedné noci). Reálná životnost bude o 20–40 % nižší.

Na **aktuálním FireBeetle** (sleep 0,47 mA = 11,3 mAh/den) je tento výpočet bezpředmětný — sleep proud desky 3× převyšuje self-discharge a wake interval mizí v šumu (1 min vs. 30 min = jen ~25 % rozdíl, oba ~6–7 měsíců).

**Na C3-LPKit v4** (sleep 19 µA = 0,46 mAh/den naměřeno) je tahle projekce konečně realistická — sleep proud klesl ~25× a tabulka výše přestává být teoretická. Wake interval má teď reálný dopad: 5 min = 1,74 r, 30 min = 2,00 r. Self-discharge zůstává nepřekročitelný floor.

---

## Hardware gotchas

1. **Local DNS nepřekládá public domény** — vždy `dns_setserver()` na 1.1.1.1 + 8.8.8.8 hned po WiFi connect (přes `lwip/dns.h`). Mnoho domácích routerů (např. 192.168.x.1) má jen interní zóny, public domény nepřeloží.
2. **`analogRead() × 3,3 / 4095` má ~4 % chybu** — vždy použít `analogReadMilliVolts()` (vnitřně volá `esp_adc_cal_raw_to_voltage()` s factory eFuse Vref). `VBAT_CALIBRATION = 1.000`.
3. **Při USB connected nelze spolehlivě měřit SoC** — TP4056 charger drží 4,2 V v CV phase, ESP vidí výstup chargeru, ne pouze baterii. Pro field deployment OK (USB jen příležitostně).
4. **POSIX TZ pro Prahu**: `CET-1CEST,M3.5.0,M10.5.0/3` (auto DST).
5. **CH340 driver na Fedoře**: built-in (`ch341` kernel modul), `/dev/ttyUSB0` rovnou použitelný — žádný `usermod -aG dialout` netřeba.
6. **CH340 žere ~150–300 µA i bez USB** — známý leak FireBeetle desky, viz [Spotřeba](#spotřeba) výše.
7. **Použít ADC1 piny (GPIO32–39)** pro Vbat — ADC2 koliduje s WiFi.
8. **Wake spike s WiFi až 200 mA** — multimetr na 20 mA range vyhoří pojistku. Pro měřicí kapitolu použít 200 mA range nebo 10A bezfusový port.
9. **První sample po MPU wake je outlier** — vždy zahodit, brát od druhého.
10. **Půlnoc v okně 22:00–06:00** — použít `(hour >= 22 || hour < 6)`, ne `&&` (vždy false).
11. **Magnetické / vibrační rušení garážových motorů** — pokud bude problém, vložit po wake `delay(5 s)` a přeměřit, nebo filtrovat měření s |a| daleko od 1g.
12. **MPU baseline „closed"** — zachycen při cold bootu, předpoklad vrata zavřená. Po výměně baterie / RST musí být vrata zavřená, jinak posun reference.

---

## Migrace na ESP32-C3

**Důvod:** FireBeetle ESP32-E má hardware-side leak ~430 µA z CH340 + LDO. ESP32-C3 má **nativní USB-CDC** (žádný externí USB-Serial čip). Typický deep sleep ESP32-C3 + clean board layout: **~5–10 µA chip + ~5 µA peripherály = ~15 µA**. + MPU sleep 5 µA → cíl **~20 µA total**, což odpovídá teoretické predikci.

Objednaná deska: <https://www.laskakit.cz/laskkit-esp-12-board/>

**Migrační kroky (až dorazí):**
- [ ] Přemapovat GPIO ve firmwaru (SDA/SCL/Vbat — ESP32-C3 má jinou pinovou mapu)
- [ ] Změnit `board` v `platformio.ini` (z `dfrobot_firebeetle2_esp32e` na `esp32-c3-devkitm-1` nebo specifický LaskKit ID)
- [ ] Nahrát `sleep` debug, znova provést stejné měření
- [ ] Cíl: sleep proud ESP32-C3 + MPU sleep ≤ 25 µA (limit rozlišení multimetru na 20 mA range = 0,02–0,03 mA)

Code portability: `Wire.h`, `esp_sleep_*`, `esp_adc_cal`, `WiFi.h` — vše funguje 1:1, jen jiná GPIO čísla.

---

## Budoucí rozšíření (v2)

### Top priorita — pokud bude pokračování projektu
- **AP config mode + NVS settings** — když WiFi nejede / se mění, zařízení vytvoří hotspot „garage-config", uživatel přes browser na `192.168.4.1` zadá nové credentials, ESP uloží do **NVS** (Non-Volatile Storage ve flash, přežije power-off). Eliminuje rekompilaci `secrets.h`. Stejný mechanismus pro threshold, window times, ntfy topic. Rozdíl mezi „technický projekt" a „použitelný produkt".
- **Magnetic reed switch jako primary detector** — pasivní spínač = nulová spotřeba pro detekci, MPU jen jako záloha / „magnetic field nesedí, něco není OK". Pro binary open/closed scénář spolehlivější než tilt + řádově lepší power budget.
- **OTA firmware updates** — update bez fyzického přístupu k zařízení. ESP32 podporuje, využíváme WiFi co stejně máme. Užitečné jakmile zařízení visí na vratech.

### Nice-to-have
- **Recovery notifikace** — po vyřešení chyby poslat „sensor opět OK". Uživatel má čistý mental model „byl problém / je vyřešeno".
- **Hard low-battery alert (jednou)** — Vbat < 3,3 V → poslední šance zpráva před tím, než ESP zhasne. Aktuální heartbeat už battery % obsahuje, takže tohle je jen safety net.
- **Battery health tracking** — log Vbat samples do NVS, počítej kapacitní degradaci 18650 v čase. Po roce uvidíš jestli baterka zestárla nebo drží.
- **Vacation mode** — temporary 24/7 alert (ne jen 22-06). Aktivované přes ntfy příkaz nebo dočasně do NVS.

### Spíše ne (pro úplnost — proč to nedává smysl)
- **Ultrasonic distance / PIR motion** — jiný use case (auto v garáži / zloděj uvnitř). Není to evoluce „detekce otevřených vrat", spíš samostatný projekt.
- **Adaptive sleep duration** — spočítáno: ~0,8 mAh/den úspora vs. self-discharge ~30 mAh/měsíc. Low value, ladění toho času nestojí.
- **Solar power** — overkill pro nabíjení 1× ročně. Solar dává smysl jen pokud životnost klesne pod ~3 měsíce nebo zařízení nemá fyzický přístup.
- **Multi-door support** — málokdo má víc garáží. Pokud by, lze řešit přes víc samostatných zařízení.

---

## Debug — vývojářské nástroje

Každý debug modul je samostatně flashovatelný firmware testující jednu věc izolovaně. Slouží pro ladění a diagnostiku během vývoje (ne pro deployment).

### Příkazy

| Modul | Příkaz |
|---|---|
| **Tilt debug** (MPU6050 monitoring) | `pio run -e tilt -t upload -t monitor` |
| **Ntfy debug** (WiFi + Vbat status loop) | `pio run -e ntfy -t upload -t monitor` |
| **Sleep debug** (ESP+MPU deep sleep) | `pio run -e sleep -t upload -t monitor` |
| Jen build (bez upload) | `pio run -e <env>` |
| Vyčistit build cache | `pio run -e <env> -t clean` |

### Co každý modul dělá

**`tilt`** — po bootu zachytí baseline (50 vzorků, průměr), pak každou sekundu loguje tilt vůči baseline. Threshold 60° / 40° (hystereze). Per-axis values + delta + magnitude + úhel. Bez WiFi, bez sleep.

**`ntfy`** — WiFi connect, DNS override 1.1.1.1, NTP sync, boot notifikace, pak každou minutu status (čas + Vbat + RSSI + uptime). LED blink 1 Hz. Bez MPU, bez sleep.

**`sleep`** — ESP+MPU deep sleep cyklus. RTC timer wake po 30 s, boot counter v RTC slow memory, wake-cause detekce, Vbat reading. Konfigurovatelné `AWAKE_HOLD_MS` (delší active fáze pro multimetr odečet) a `MPU_PRESENT` (skip I²C při testu bez sensoru). EN/RST tlačítko vynuluje boot counter.

### Přidání nového debug modulu

1. `mkdir debug/<jmeno> && touch debug/<jmeno>/main.cpp`
2. V `platformio.ini` přidat:
   ```ini
   [env:<jmeno>]
   build_src_filter = -<*> +<../debug/<jmeno>/main.cpp>
   ```
3. `pio run -e <jmeno> -t upload -t monitor`

Sdílená konfigurace (board, port, build_flags) je v `[env]` sekci, neopisovat.

---

## TODO

### Před git init / prvním commit
- [x] Vytáhnout WiFi credentials a NTFY_TOPIC do `secrets.h` (gitignored)
- [x] `.gitignore` (`.pio/`, `secrets.h`, OS junk)
- [x] `README.md` s build/upload instrukcemi
- [x] `USAGE.md` pro netechnické uživatele
- [ ] Připnout PIO platform version v `platformio.ini` (`platform = espressif32@^6.5.0`)
- [ ] Otestovat finální `src/main.cpp` end-to-end (cold boot → wake cyklus → alert → heartbeat)
- [ ] Git init + first commit

### Před field deploymentem (battery-powered)
- [ ] **Odpájet GY-521 zelenou power LED** — naměřeno 1,44 mA always-on
- [ ] Ověřit jestli FireBeetle má další power LED (zatím nezpozorovaná, možná chybí na V1.0 revizi)
- [ ] 3D tištěný / lepený držák MPU na vrata
- [ ] Test 1 týden v reálných podmínkách, sledovat false positives z větru
- [ ] Doladit threshold 60° / 40° pokud místní prostředí vyžaduje

### Až dorazí ESP32-C3
- [ ] Migrace dle sekce [Migrace na ESP32-C3](#migrace-na-esp32-c3) výše
