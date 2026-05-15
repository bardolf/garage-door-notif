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

### 🚀 „Hlidac garazovych vrat aktivni" — po zapnutí

Pošle se jednou, hned po tom co zařízení nastartuje (zapnutí spínače, výměna baterie, první instalace).

**Co v ní stojí:**
```
Čas: 17:23:45
Baterie: 95 % (4,18 V)
Signál: ▮▮▮▮▮ (-54 dBm)

Reference zavřených vrat zachycena.
Orientace: leží naplocho (Z dominantní) (3° od horizontály).

Pokud orientace neodpovídá realitě (vrata byla otevřená při instalaci),
při zavřených vratech zařízení vypnout a zapnout — uloží se nová reference.
```

**Co to znamená:**
- Zařízení se nastartovalo, čas se synchronizoval, baterie nabitá, WiFi v pohodě
- „Reference zachycena" = senzor si zapamatoval aktuální polohu vrat jako „zavřeno"
- Pokud orientace v zprávě neodpovídá skutečné poloze zavřených vrat (např. píše „leží naplocho" ale vrata jsou vlastně otevřená), zařízení v zavřené poloze **vypnout a znovu zapnout** spínačem — uloží se nová reference
- Pokud tahle zpráva nepřišla, něco selhalo — projděte sekci „Co dělat když..."

### 🚨 „GARAZ OTEVRENA!" — urgentní v noci

Jediná zpráva která má **urgent** prioritu (na iPhone i Androidu obejde tichý režim, rozsvítí displej).

**Kdy přijde:**
- Mezi 22:00 a 06:00
- Vrata jsou otevřená (náklon > 60° od původní polohy)
- **Při každé kontrole** (cca každých 5 min, dokud vrata nezavřete) — chceme abyste byli upozorněni opakovaně, ne jednou a dost

**Co v ní stojí:**
```
Čas: 22:30
Náklon: 67° (limit 60°)
Baterie: 85 %

Zkontroluj vrata.
```

**Co dělat:** zkontrolujte garáž. Buď fyzicky, nebo přes kameru / sousedy.

> Pokud zařízení ztratilo synchronizaci času (např. WiFi výpadek delší dobu), místo času v notifikaci uvidíte `Čas: (čas neznámý)`. Alert přesto pošle — bezpečnostní funkce funguje i bez přesného času.

### ✅ „Garaz - denni kontrola" — denní heartbeat

Posílá se **jednou denně, od 21:00**. Pokud bylo zařízení ve 21:00 zaneprázdněné (poslední alert, WiFi sync), dorazí o pár minut později. Nikdy ne dříve.

**Smysl:** ověření že zařízení žije a senzor funguje. **Pokud ji jeden den nedostanete, něco se pokazilo** — vybitá baterie, mrtvý ESP, vypnutá WiFi.

**Co v ní stojí:**
```
Čas: 21:00
Stav: ZAVŘENO
Náklon: 2°
Baterie: 87 % (4,10 V)
Signál: ▮▮▮▮▯ (-67 dBm)
Běží: 47d 3h 12m

Pokud tato denní kontrola někdy nedorazí, něco se pokazilo — zkontroluj zařízení.
```

### ⚠️ „Hlidac vrat - CHYBA" — diagnostické chyby

Pošle se když zařízení detekuje problém — typicky **senzor přestal odpovídat**. Priority `high` (důraznější než heartbeat, ale ne urgent).

**Kdy přijde:**
- Senzor nereaguje (uvolněný kabel? rozbitý modul?)
- Reference nebyla zachycena po startu (vypnutí/zapnutí proběhlo špatně)
- WiFi výpadek 3× po sobě (zařízení bylo offline) — pošle se na prvním reconnectu
- Aby vás chyby nezahltily, posílají se nejvýš jednou za **30 minut**

**Co v ní stojí:**
```
Čas: 14:23
Počet chyb od posledního hlášení: 3
Poslední: Senzor nereaguje (probuzení selhalo po 3 pokusech)
Baterie: 87 %
Signál: ▮▮▮▮▯ (-67 dBm)

Chyby se opakují — pravděpodobně skutečný problém.
Zkontroluj připojení senzoru (4 vodiče), případně vyměň modul.
```

**Co dělat:** otevřete krabičku, zkontrolujte:
- Jsou všechny 4 vodiče k senzoru zapojené (VCC, GND, SDA, SCL)?
- Není kabel přerušený?
- **Vypněte a zapněte** zařízení spínačem — pokud problém zmizí, byl to dočasný glitch
- Pokud chyby pokračují, senzor je pravděpodobně mrtvý → výměna modulu

**Důležité:** pokud chodí chyby, **alerty na otevřená vrata nemusí fungovat**. Zařízení nedokáže měřit náklon, takže by nevidělo ani skutečně otevřená vrata. **Berte chybové zprávy vážně.**

---

## LED na zařízení

V krabičce je viditelná RGB LED dioda (skrz průzor ve víku). Slouží jako sekundární indikátor stavu — víc se na ni ale spoléhat nemá smysl, primární kanál jsou notifikace v telefonu.

| Barva | Význam |
|-------|--------|
| 🔵 **Modrá svítí** | Cold boot právě probíhá (po zapnutí spínače / výměně baterie). Trvá 5–10 s. |
| 🟢 **Zelená 5 s svítí + 10 s bliká** | Cold boot proběhl úspěšně. Reference zachycena, WiFi OK, notifikace odeslána. Bliká během 10s okna pro nahrávání nového firmware (technické). |
| 🔴 **Červená 5 s svítí + 10 s bliká** | Cold boot selhal — typicky chybí WiFi, nepodařilo se zachytit referenci nebo senzor nereaguje. Měla by dorazit i CHYBA notifikace (pokud WiFi zafungovala). |
| 🟣 **Fialová svítí 5 s** | Právě byl odeslán alert „GARÁŽ OTEVŘENA". Vidíte jenom během běžné kontroly (každých 5 min), kdy byl alert úspěšně doručen. |
| 🔴 **Červená svítí 5 s** během běžného provozu | Některá kontrola selhala (senzor nereaguje nebo se nepodařilo poslat zprávu). Chybová notifikace dorazí po reconnectu WiFi. |
| **Zhasnutá** | Normální stav během deep sleep i během kontroly co neměla nic na hlášení. Většinu času uvidíte zhasnutou — to je správně. |

**Proč to tak svítí krátce:** dlouhé svícení by žralo baterii. 5 s solid umožňuje zaregistrovat barvu zrakem během krátké návštěvy garáže, deep sleep pak LED úplně odpojí (0 µA).

---

## Jak rozumět údajům ve zprávách

### Baterie v procentech

| % | Voltáž | Stav | Co dělat |
|---|---|---|---|
| **>90** | >4,1 V | Plně nabito | nic |
| **70–90** | 3,9–4,1 V | Vysoký stav | nic |
| **40–70** | 3,8–3,9 V | Normální | nic |
| **20–40** | 3,7–3,8 V | Začíná docházet | naplánovat nabití do týdne |
| **5–20** | 3,5–3,7 V | Nízká | **brzy nabít** |
| **<5** | <3,5 V | Kritická | **nabít HNED**, jinak hrozí výpadek |
| **0** | ≤3,2 V | Vybitá | zařízení vypnuto, nezachycuje |

Procenta jsou orientační (LiPo baterie má nelineární vybíjení), ale dávají dobrý odhad. **Důvěřujte heartbeatu — pokud chodí, zařízení žije.**

### Síla signálu (RSSI)

V zprávě vidíte signál jako 5-segmentový bar (`▮▮▮▮▮` plný / `▮▯▯▯▯` jeden) + dBm hodnotu:

| Bar | dBm | Co to znamená |
|---|---|---|
| **▮▮▮▮▮** | > −60 | velmi blízko routeru, žádný problém |
| **▮▮▮▮▯** | −60 až −70 | normální, plně dostačuje |
| **▮▮▮▯▯** | −70 až −80 | slabší, ale ještě funguje |
| **▮▮▯▯▯** | −80 až −90 | hraniční, občas může vypadnout |
| **▮▯▯▯▯** | < −90 | nestabilní, alerty se nemusí dostat |

Pokud se zhorší, můžete zvážit lepší pozici routeru nebo WiFi extender. Ale obvykle není třeba řešit.

### Náklon ve stupních

`Naklon: X°` — o kolik se vrata pohnula od původní polohy (zachycené při startu).

- **0–10°** = zavřeno (drobné chvění z větru, vibrací)
- **10–60°** = pootevřeno (normálně by se to nemělo stávat, ale není to alert)
- **>60°** = otevřeno (alert v noci!)

### Čas ve zprávách

Čas v notifikacích (`Čas: HH:MM`) **nemusí vždy přesně odpovídat skutečnému času** — může být odchýlený o pár minut. Zařízení nemá hodinový krystal pro přesné měření času; používá interní oscilátor a pravidelně si čas synchronizuje přes internet (každých ~10 minut). Pro účely hlídání nočního okna 22:00–06:00 je to dostatečně přesné, drobné odchýlky neovlivňují funkci.

Pokud zařízení ztratilo synchronizaci úplně (např. dlouhý WiFi výpadek, čerstvý cold boot bez internetu), v zprávě uvidíte `Čas: (čas neznámý)`. Alerty fungují i tak — jenom bez timestampu.

---

## Co dělat když...

### V noci přijde urgentní notifikace

1. **Zkontrolujte vrata** — fyzicky, kamerou, požádejte souseda
2. Pokud je to **planý poplach** (např. silný vítr roztřásl vrata):
   - Žádná akce není nutná, zařízení samo zase zaregistruje „zavřeno" jakmile se náklon vrátí
   - Pokud vítr nadále působí, dostanete další alert do 5 minut (cyklicky až do uklidnění)
3. Pokud někdo vrata reálně otevřel (zloděj, zapomenuté otevření): jednejte podle situace

### Zpráva „GARAZ OTEVRENA" se opakuje každých 5 min

Vrata jsou pravděpodobně **skutečně otevřená a zaseklá** v té poloze. Buď je někdo zapomněl zavřít, nebo má motor vrat poruchu. Alerty budou chodit dokud vrata nezavřete (nebo dokud nedojde baterie).

### Heartbeat (denní zpráva od 21:00) nepřišel

Možnosti, od nejpravděpodobnější:
1. **Vybitá baterie** — ESP nemá energii na WiFi
2. **WiFi výpadek** — router restartoval, ESP se zatím nepřipojilo (po reconnectu by měla dorazit chybová notifikace „WiFi nedostupné Xx po sobě")
3. **Spadlý ntfy.sh** (vzácné, ale stane se)
4. **Hardware selhal** — ESP nebo senzor vypovědělo službu

**Co udělat:** počkejte do dalšího dne. Pokud druhý den znova nepřišel, otevřete krabičku a zkontrolujte:
- Svítí na zařízení LED? Pokud bliká **červeně**, je v chybovém stavu (viz LED sekce výše)
- Je baterie nabitá? (zkuste nabít přes USB-C kabel — viz Nabíjení)
- Jsou všechny vodiče k senzoru zapojené?

### Nepřišla úvodní zpráva po zapnutí

Pravděpodobně se nepodařilo připojit k WiFi nebo nesynchronizovat čas. Zařízení po 5 minutách znovu zkusí. Sledujte LED:
- **Zelená blikající** ~10 s po zapnutí = vše OK, jen se nepovedlo poslat (síťový problém)
- **Červená blikající** ~10 s po zapnutí = chyba (WiFi nedostupná, senzor nereaguje, čas nesynchronizován)

Pokud po 30 min nic nedoraziilo:
- WiFi router běží? Heslo / SSID správné?
- Pokud LED neukazuje žádnou aktivitu = vybitá baterie nebo HW selhání

### Baterie pod 20 %

Naplánujte nabití do týdne. Při kritickém stavu (<5 %) se zařízení samo vypne kolem 3,2 V — nehrozí poškození baterie.

---

## Nabíjení

Zařízení má **vestavěný USB-C konektor + nabíjecí čip**. Nabíjení:

1. Připojte USB-C kabel do zařízení
2. Druhý konec do jakéhokoliv USB nabíječe (telefonní nabíječka, powerbanka, PC, …)
3. Indikační LED na desce nabíjení svítí během nabíjení (na samotném ESP modulu, mimo průzor)
4. Zhasne když je baterie plně nabitá (~2–3 hodiny pro 1000 mAh, ~6 h pro 18650)
5. Zařízení **může běžet i během nabíjení**, není třeba vypínat

**Důležité:** zařízení se po vypnutí/zapnutí spínačem (nebo výměně baterie) chová jako po prvním zapnutí — pošle „Hlidac garazovych vrat aktivni" notifikaci a znovu zachytí referenci polohy vrat.

➜ **Nabíjení samotné** s běžícím zařízením tohle nevyvolá (napájení nepřerušuje). Pokud ale potřebujete novou referenci (vrata se posunula, vyměnili jste senzor), **vypněte a zapněte spínač při zavřených vratech**.

---

## Časté otázky

**Q: Co když dorazí FALSE positive (alert, ale vrata zavřená)?**
A: Vyhodnoťte jednou. Pokud se opakuje, zařízení může mít posunutou referenci (např. vrata si „sednou" za pár měsíců). Při zavřených vratech **vypněte a zapněte spínač** — zařízení zachytí novou referenci z aktuální polohy.

**Q: Funguje to bez WiFi?**
A: Ne. Zařízení potřebuje WiFi pro odeslání notifikace přes ntfy.sh. Bez WiFi neudělá nic užitečného (žádný lokální alert / siréna).

**Q: Jak nastavit, která vrata má zařízení hlídat?**
A: Senzor musí být **pevně přilepený k vratům** (ne na rámu, ne na pohyblivé části okolo). Při zapnutí spínače se zachytí aktuální poloha jako „zavřeno". Pak když se vrata pohnou >60°, alert.

**Q: Funguje to na sekční / rolovací / křídlová vrata?**
A: Ano, na všechna, která se naklánějí o víc než 60° při otevření. Sekční (skládací do stropu) typicky 90°, křídlová 90°, rolovací (svisle navíjená) jen ~5–10° — **na rolovací nefunguje**, je třeba jiný senzor.

**Q: Lze zařízení rozšířit o detekci přes den?**
A: Aktuálně hlídá jen 22:00–06:00. Rozšíření by šlo, ale baterie vydrží míň (víc alertů = víc WiFi = víc energie). Pokud potřeba, dá se firmware upravit.

**Q: Co když chci notifikaci vypnout (např. legitimně otevírám vrata v noci)?**
A: Buď vypněte ntfy.sh appku v telefonu před tím, nebo akceptujte alert (přijde každých ~5 min dokud vrata nezavřete). Plánovaný „suppress" mód není.

---

## Technický overview pro zvědavé

Zařízení obsahuje:
- **ESP32-C3** mikrokontroler (LaskaKit C3-LPKit v4) — mozek, WiFi, USB-C
- **MPU6050 / MPU6500** akcelerometr (GY-521 modul) — měří náklon
- **WS2812 NeoPixel** RGB LED — indikace stavu
- **LiPo baterii** s integrovaným nabíjecím čipem na desce
- **Spínač** v sérii s baterií (pro vynucenou novou referenci)

Cyklus:
1. ESP32 spí 99,9 % času (deep sleep, naměřeno **19 µA**)
2. Každých 5 minut se na ~5 s probudí
3. Probudí senzor přes I²C, změří náklon (3 retry pokusy proti glitchům)
4. Pokud je důvod (alert, denní heartbeat, sync času, error report), zapne WiFi a pošle zprávu
5. Zpět do spánku

**Životnost na baterii** (18650 3200 mAh, klidový provoz bez alertů): teoreticky ~1,5–2 roky. Reálně limitované samovybíjením baterie a chladem v zimní garáži → **plánovat nabíjení 1× ročně**.

Detaily v `README.md`.
