# Garážový hlídač — uživatelský návod

> Tenhle návod je psaný pro běžného uživatele, ne pro techniky.
> Vysvětluje co zařízení dělá, jaké zprávy budete dostávat a co s nimi.

---

## Co to je a co to dělá

V garáži je malé zařízení (ESP32 + senzor náklonu) připevněné k vratům. Běží na baterii, která vydrží **dlouho** (řádově měsíce až roky podle typu baterie).

**Jeho jedinou prací je hlídat: jsou vrata otevřená v noci?**

Pokud ano (mezi **22:00 a 06:00**), pošle vám okamžitou notifikaci na mobil. Mimo tuto dobu vás zařízení neotravuje — předpokládá se, že přes den s vraty pracujete normálně.

Notifikace chodí přes službu **ntfy.sh** (free, žádný účet, jen appka v telefonu).

---

## Jaké notifikace budete dostávat

Existují **4 druhy** zpráv, podle situace:

### 🚀 „Hlidac garazovych vrat aktivni" — po zapnutí / nabití

Pošle se jednou, hned po tom co zařízení nastartuje (např. po výměně baterie nebo prvním zapnutí).

**Co v ní stojí:**
```
Cas: 17:23:45
Baterka: 95 % (4,18 V)
Signal: vyborny (-54 dBm)
Sensor: baseline OK (|a|=0.998 g)
```

**Co to znamená:**
- Zařízení se nastartovalo, čas se synchronizoval, baterie nabita, WiFi v pohodě
- „Sensor baseline OK" = senzor zachytil aktuální polohu vrat (předpokládá se zavřeno) jako referenční
- Pokud tahle zpráva nepřišla, něco selhalo — projděte sekci „Co dělat když..."

### 🚨 „GARAZ OTEVRENA!" — urgentní v noci

Jediná zpráva která má **urgent** prioritu (na iPhone i Androidu obejde tichý režim, rozsvítí displej).

**Kdy přijde:**
- Mezi 22:00 a 06:00
- Vrata jsou otevřená (náklon > 60° od původní polohy)
- **Při každé kontrole** (cca každých 5 min, dokud vrata nezavřete) — chceme abyste byli upozorněni opakovaně, ne jednou a dost

**Co v ní stojí:**
```
Cas: 22:30
Naklon: 67° (limit 60°)
Baterka: 85 %
```

**Co dělat:** zkontrolujte garáž. Buď fyzicky, nebo přes kameru / sousedy.

### ✅ „Garaz - denni kontrola" — denní heartbeat

Posílá se **jednou denně ve 21:00** (hodina před začátkem nočního okna).

**Smysl:** ověření že zařízení žije a sensor funguje. **Pokud ji jeden den nedostanete, něco se pokazilo** — vybitá baterka, mrtvý ESP, vypnutá WiFi.

**Co v ní stojí:**
```
Cas: 21:00
Stav: ZAVRENO
Naklon: 2°
Baterka: 87 % (4,10 V)
Signal: dobry (-67 dBm)
```

### ⚠️ „Hlidac vrat - CHYBA" — diagnostické chyby

Pošle se když zařízení detekuje problém — typicky **senzor přestal odpovídat**. Priority `high` (důraznější než heartbeat, ale ne urgent).

**Kdy přijde:**
- MPU sensor neodpovídá na I²C komunikaci (kabel se uvolnil?)
- MPU měření selhává opakovaně (sensor se rozbil?)
- Baseline nebyl zachycen po startu
- Tyto chyby trvají déle (každých ~5 min nová zpráva s počtem chyb), dokud se problém neopraví

**Co v ní stojí:**
```
Cas: 14:23
Pocet chyb od posledniho hlaseni: 3
Posledni: MPU sensor neodpovida (wake selhal)
Baterka: 87 %
Signal: dobry (-67 dBm)
```

**Co dělat:** otevřete garáž, zkontrolujte:
- Jsou všechny 4 dráty MPU senzoru zapojené (VCC, GND, SDA, SCL)?
- Není kabel přerušený?
- Stiskněte RST tlačítko na zařízení — pokud problém zmizí, byl to dočasný glitch
- Pokud chyby pokračují, sensor je pravděpodobně mrtvý → výměna MPU6050 modulu

**Důležité:** pokud chodí chyby, **alerty na otevřená vrata nemusí fungovat**. Zařízení nedokáže měřit náklon, takže by neviděl ani skutečně otevřená vrata. **Berte chybové zprávy vážně.**

---

## Jak rozumět údajům ve zprávách

### Baterka v procentech

| % | Voltáž | Stav | Co dělat |
|---|---|---|---|
| **>90** | >4,1 V | Plně nabito | nic |
| **70–90** | 3,9–4,1 V | Vysoký stav | nic |
| **40–70** | 3,8–3,9 V | Normální | nic |
| **20–40** | 3,7–3,8 V | Začíná docházet | naplánovat nabití do týdne |
| **5–20** | 3,5–3,7 V | Nízká | **brzy nabít** |
| **<5** | <3,5 V | Kritická | **nabít HNED**, jinak hrozí výpadek |
| **0** | ≤3,2 V | Vybitá | zařízení vypnuto, nezachycuje |

Procenta jsou orientační (LiPo baterka má nelineární vybíjení), ale dávají dobrý odhad. **Důvěřujte heartbeatu — pokud chodí, zařízení žije.**

### Síla signálu (RSSI)

| Slovo ve zprávě | dBm | Co to znamená |
|---|---|---|
| **vyborny** | > −60 | velmi blízko routeru, žádný problém |
| **dobry** | −60 až −70 | normální, plně dostačuje |
| **ok** | −70 až −80 | slabší, ale ještě funguje |
| **slaby** | −80 až −90 | hraniční, občas může vypadnout |
| **velmi slaby** | < −90 | nestabilní, alerty se nemusí dostat |

Pokud se zhorší, můžete zvážit lepší pozici routeru nebo WiFi extender. Ale obvykle není třeba řešit.

### Náklon ve stupních

`Naklon: X°` — o kolik se vrata pohnula od původní polohy (zachycené při startu).

- **0–10°** = zavřeno (drobné chvění z větru, vibrací)
- **10–60°** = pootevřeno (normálně se nemělo by stávat, ale není to alert)
- **>60°** = otevřeno (alert v noci!)

---

## Co dělat když...

### V noci přijde urgentní notifikace

1. **Zkontrolujte vrata** — fyzicky, kamerou, požádejte souseda
2. Pokud je to **planý poplach** (např. silný vítr roztřásl vrata):
   - Žádný akce není nutná, zařízení samo zase zaregistruje „zavřeno" jakmile se náklon vrátí
   - Pokud vítr nadále působí, dostanete další alert do 5 minut (cyklicky až do uklidnění)
3. Pokud někdo vrata reálně otevřel (zloděj, zapomenuté otevření): jednejte podle situace

### Zpráva „GARAZ OTEVRENA" se opakuje každých 5 min

Vrata jsou pravděpodobně **skutečně otevřená a zaseklá** v té poloze. Buď je někdo zapomněl zavřít, nebo má motor vrat poruchu. Alerty budou chodit dokud vrata nezavřete (nebo dokud nezhasne baterka).

### Heartbeat (denní zpráva ve 21:00) nepřišel

Možnosti, od nejpravděpodobnější:
1. **Vybitá baterie** — ESP nemá energii na WiFi
2. **WiFi výpadek** — router restartoval, ESP se zatím nepřipojilo
3. **Spadlý ntfy.sh** (vzácné, ale stane se)
4. **Hardware selhal** — ESP nebo MPU vypovědělo službu

**Co udělat:** počkejte do dalšího dne. Pokud druhý den znova nepřišel, otevřete garáž a zkontrolujte:
- Svítí na zařízení nějaká LED? (zelená na senzoru by měla… kdyžtak ne, máme ji odpájenou pro úsporu)
- Je baterie nabitá? (zkuste nabít přes USB-C kabel)
- Jsou všechny kabely zapojené?

### Nepřišla úvodní zpráva po zapnutí

Pravděpodobně se nepodařilo připojit k WiFi nebo nesynchronizovat čas. Zařízení po 5 minutách znovu zkusí. Pokud po 30 min nic, zkontrolujte:
- WiFi router běží?
- Heslo / SSID v `secrets.h` jsou správné? (technická úprava firmwaru)

### Baterie pod 20 %

Naplánujte nabití do týdne. Při kritickém stavu (<5 %) se zařízení samo vypne kolem 3,2 V — nehrozí poškození baterky.

---

## Nabíjení

Zařízení má **vestavěný USB-C konektor + nabíjecí čip TP4056**. Nabíjení:

1. Připojte USB-C kabel do zařízení
2. Druhý konec do jakéhokoliv USB nabíječe (telefon nabíječka, powerbanka, PC, …)
3. Červená LED na zařízení svítí během nabíjení
4. LED zhasne když je baterka plně nabitá (~2–3 hodiny pro 1000 mAh, ~6 h pro 18650)
5. Zařízení **může běžet i během nabíjení**, není třeba vypínat

**Důležité:** zařízení se po výměně/odpojení baterky chová jako po prvním zapnutí — pošle „Garaz spustena" notifikaci a znovu zachytí baseline (předpokládá vrata zavřená).

➜ **Nabíjejte tedy se zavřenými vraty**, jinak bude baseline špatně.

---

## Časté otázky

**Q: Co když dorazí FALSE positive (alert, ale vrata zavřená)?**
A: Vyhodnoťte jednou. Pokud se opakuje, zařízení může mít posunutý baseline (např. vrata si „sednou" za pár měsíců). Stačí zařízení **resetovat tlačítkem RST**, znovu zachytí baseline z aktuální polohy.

**Q: Funguje to bez WiFi?**
A: Ne. Zařízení potřebuje WiFi pro odeslání notifikace přes ntfy.sh. Bez WiFi neudělá nic užitečného (žádný lokální alert / siréna).

**Q: Jak nastavit které vrata má hlídat?**
A: Senzor musí být **pevně přilepený k vratům** (ne na rámu, na pohyblivé části). Při zapnutí se zachytí aktuální poloha jako „zavřeno". Pak když se vrata pohnou >60°, alert.

**Q: Funguje to na sekční / rolovací / křídlová vrata?**
A: Ano, na všechna která se naklánějí o víc než 60° při otevření. Sekční (skládací do stropu) typicky 90°, křídlová 90°, rolovací (svisle navíjená) jen ~5–10° — **na rolovací nefunguje**, je třeba jiný senzor.

**Q: Lze zařízení rozšířit o detekci přes den?**
A: Aktuálně hlídá jen 22:00–06:00. Rozšíření by šlo, ale baterie vydrží míň (víc alertů = víc WiFi = víc energie). Pokud potřeba, dá se firmware upravit.

**Q: Co když Já chci notifikaci vypnout (např. legitimně otevírám vrata v noci)?**
A: Buď vypněte ntfy.sh appku v telefonu před tím, nebo akceptujte alert (přijde každých ~5 min dokud vrata nezavřete). Plánovaný „suppress" mód není.

---

## Technický overview pro zvědavé

Zařízení obsahuje:
- **ESP32** mikrokontroler — mozek, WiFi
- **MPU6050** akcelerometr — měří náklon
- **LiPo baterii** s integrovaným nabíjecím čipem

Cyklus:
1. ESP32 spí 99,9 % času (deep sleep, ~20 µA)
2. Každých 5 minut se na ~150 ms probudí
3. Probudí senzor přes I²C, změří náklon
4. Pokud je důvod (alert, denní heartbeat, sync času), zapne WiFi a pošle zprávu
5. Zpět do spánku

Detaily v `README.md`.
