# Funkce – Octavia II PC

Tento dokument shrnuje funkce projektu **Octavia II PC** podle firmware **V16.31.2 HOTFIX / MULTI-ECU**.

Octavia II PC je neoficiální komunitní palubní počítač a diagnostický displej pro **Škoda Octavia II (1Z)** postavený na **ESP32-2432S028R / CYD 2.8" 320×240**. S vozidlem komunikuje přes **Bluetooth Classic / SPP ELM327** a kombinuje živá OBD data, DPF monitoring, knihu jízd, servisní funkce, alarmy a vlastní servisní Wi‑Fi WEB.

> **Důležitá zásada projektu:** pokud konkrétní hodnota není pro danou ECU potvrzená nebo dostupná, firmware zobrazí `--`, `N/A` nebo funkci omezí. Neověřená data se nevydávají za skutečná.

---

## Obsah

1. [Hlavní menu](#hlavní-menu)
2. [Přehled](#1-přehled)
3. [Motor](#2-motor)
4. [Turbo](#3-turbo)
5. [DPF PRO](#4-dpf-pro)
6. [Kniha jízd](#5-kniha-jízd)
7. [Tankování](#6-tankování)
8. [Diagnostika](#7-diagnostika)
9. [Senzory](#8-senzory)
10. [Elektrika](#9-elektrika)
11. [Servis](#10-servis)
12. [Alarmy](#11-alarmy)
13. [SPECIALS / Poděkování řidiči](#12-specials--poděkování-řidiči)
14. [Wi‑Fi / servisní WEB](#13-wi-fi--servisní-web)
15. [Power Manager](#14-power-manager)
16. [Nastavení](#15-nastavení)
17. [INFO a personalizace](#16-info-a-personalizace)
18. [Bluetooth a ELM327](#17-bluetooth-a-elm327)
19. [MULTI-ECU a kompatibilita](#18-multi-ecu-a-kompatibilita)
20. [Ukládání dat](#19-ukládání-dat)
21. [Bezpečnost a omezení](#20-bezpečnost-a-omezení)

---

# Hlavní menu

Uživatelské rozhraní je rozděleno do **tří stránek**.

### Stránka 1 – Jízda

| Funkce | Popis |
|---|---|
| **PŘEHLED** | Hlavní palubní obrazovka pro běžnou jízdu. |
| **MOTOR** | Zatížení motoru, teplota chladiva, rail, MAF, RPM, sání a MAP. |
| **TURBO** | Grafický ukazatel plnicího tlaku + MAP, BARO a RPM. |
| **DPF PRO** | Stav DPF, saze, regenerace, diferenciální tlak, EGT a historie. |
| **KNIHA JÍZD** | Aktuální jízda, historie, statistiky a detaily uložených jízd. |
| **TANKOVÁNÍ** | Automatická historie tankování a odhad množství paliva. |

### Stránka 2 – Funkce

| Funkce | Popis |
|---|---|
| **DIAGNOSTIKA** | Read-only scan dostupných řídicích jednotek a DTC. |
| **SENZORY** | Rozšířený přehled OBD hodnot a krátkodobé grafy. |
| **ELEKTRIKA** | Napětí palubní sítě, stav dobíjení a elektrické alarmy. |
| **SERVIS** | Počítadla oleje/filtru a inspekce. |
| **ALARMY** | Zapnutí a vypnutí jednotlivých upozornění. |
| **SPECIALS** | Aktivní výstupní test J519 na ověřeném profilu. |

### Stránka 3 – System

| Funkce | Popis |
|---|---|
| **WI‑FI / WEB** | Spuštění lokálního servisního webu. |
| **POWER** | Správa stmívání, vypnutí displeje a úsporného režimu. |
| **NASTAVENÍ** | Nastavení displeje, OBD, DPF, vozidla a systému. |
| **INFO** | Firmware, hardware a volitelné kontaktní údaje. |

Horní stavová lišta průběžně indikuje stav **DPF, ECU, Bluetooth a Wi‑Fi**.

---

# 1. Přehled

Obrazovka **PŘEHLED** je hlavní jízdní dashboard.

Zobrazuje zejména:

- **RPM** pomocí grafického otáčkoměru,
- **teplotu chladicí kapaliny**,
- **plnicí tlak / turbo**,
- **stav paliva**, pokud je dostupný,
- **odhad dojezdu**,
- **venkovní teplotu**, pokud ji ECU podporuje,
- **stav / odhad DPF regenerace**, pokud je aktivní ověřený DPF profil.

Pro výpočet dojezdu firmware používá dostupný stav paliva a spotřebu z historie jízd. Pokud ještě není dostatek dat, lze použít nastavitelnou záložní spotřebu.

U ECU bez ověřeného rozšířeného DPF profilu se DPF informace nezaměňují za skutečná data – zobrazí se odpovídající stav `N/A` / neověřeno.

---

# 2. Motor

Obrazovka **MOTOR** zobrazuje hlavní provozní veličiny motoru.

### Hlavní ukazatele

- **zatížení motoru [%]**,
- **teplota chladicí kapaliny [°C]**,
- **tlak palivové rampy / RAIL [MPa]**,
- **MAF – průtok vzduchu [g/s]**,
- **RPM**,
- **teplota nasávaného vzduchu [°C]**,
- **MAP – absolutní tlak v sání [kPa]**.

Barvy ukazatelů se mění podle provozního stavu, aby byly důležité hodnoty viditelné i periferně během jízdy.

---

# 3. Turbo

Obrazovka **TURBO GAUGE** obsahuje grafický analogový ukazatel plnicího tlaku.

Zobrazuje:

- vypočítaný **relativní tlak turba**,
- **MAP ABS** – absolutní tlak v sacím potrubí,
- **BARO** – atmosférický tlak, pokud je dostupný,
- **RPM**.

Pokud je BARO k dispozici, plnicí tlak se vztahuje vůči aktuálnímu atmosférickému tlaku. Jinak firmware používá bezpečný referenční základ.

---

# 4. DPF PRO

DPF část je jedna z nejrozsáhlejších funkcí projektu.

## Ověřený rozšířený profil

Pro ověřené mapování ECU může firmware zobrazovat:

- **vypočítané saze [g]**,
- **naměřené saze [g]**,
- **procentuální zanesení DPF**,
- **stav aktivní regenerace**,
- **kilometry od poslední regenerace**,
- **palivo od poslední regenerace**,
- **odhad kilometrů do další regenerace**,
- **diferenciální tlak DPF [hPa]**, pokud jej ECU poskytuje,
- **EGT PRE** – teplotu před DPF,
- **EGT POST** – teplotu za DPF.

### Predikce regenerace

Firmware umí kombinovat:

- aktuální nárůst sazí,
- kilometry od poslední regenerace,
- historii předchozích regenerací.

Výsledkem je orientační údaj **„odhad do regenerace“**. Jde o predikci, nikoli o údaj přímo přikázaný ECU.

## Aktivní regenerace

Při detekované regeneraci se obrazovka automaticky přepne do režimu, který zvýrazňuje:

- aktivní regeneraci,
- aktuální množství sazí,
- změnu množství sazí během regenerace,
- délku regenerace,
- vzdálenost ujetou během regenerace,
- EGT před a za DPF.

Firmware při zahájení a dokončení regenerace může zobrazit samostatné upozornění.

## Historie DPF

Dokončené regenerace se ukládají do LittleFS.

Obrazovka **HISTORIE DPF** zobrazuje poslední záznamy a například:

- interval mezi regeneracemi v kilometrech,
- průměrný interval,
- poslední interval,
- délku regenerace,
- hodnoty sazí před/po regeneraci.

## Neověřená ECU

Pokud firmware nemá pro danou ECU potvrzené proprietární DPF mapování:

- neukazuje neověřené saze,
- neukazuje falešný stav regenerace,
- označí rozšířené DPF jako **NEOVĚŘENO**,
- standardní OBD data motoru zůstávají funkční,
- standardní DPF PIDy se mohou použít pouze tehdy, pokud je ECU skutečně podporuje.

---

# 5. Kniha jízd

Kniha jízd automaticky zaznamenává provoz vozidla.

## Aktuální jízda

Během jízdy se sleduje:

- ujetá vzdálenost,
- doba jízdy,
- průměrná spotřeba,
- spotřebované palivo, pokud je dostupný vhodný zdroj,
- maximální rychlost,
- průměrná rychlost.

## Denní souhrn

Pokud je k dispozici platný datum a čas, zobrazuje se také souhrn za aktuální den:

- počet jízd,
- celková vzdálenost,
- celková doba,
- spotřeba.

## Historie jízd

Záložka **POSLEDNÍ** nabízí:

- celkový počet jízd,
- celkovou zaznamenanou vzdálenost,
- poslední uložené jízdy,
- stránkování starších záznamů,
- označení krátkých jízd.

## Detail jízdy

U každé uložené jízdy lze otevřít detail s údaji jako:

- datum / identifikace jízdy,
- vzdálenost,
- doba,
- spotřeba,
- spotřebované palivo,
- maximální a průměrná rychlost.

Jednotlivou jízdu lze smazat; mazání vyžaduje druhé potvrzení.

## Trend spotřeby

Firmware z posledních platných jízd vytváří krátký přehled / trend spotřeby.

## Ochrana rozjeté jízdy

Aktivní jízda má checkpoint v LittleFS. Po neočekávaném restartu může být rozpracovaný záznam obnoven místo úplného ztracení.

---

# 6. Tankování

Firmware obsahuje automatickou detekci tankování.

Sleduje změnu hladiny paliva a při potvrzeném nárůstu vytvoří záznam.

### Záznam obsahuje

- čas tankování,
- stav tachometru, pokud byl nakonfigurován,
- množství paliva před tankováním,
- množství paliva po tankování,
- odhad doplněných litrů.

### Ochrana proti falešným záznamům

Tankování není vyhodnoceno podle jednoho vzorku. Firmware používá potvrzovací logiku a minimální nárůst paliva, aby například náklon vozidla nebo pohyb paliva v nádrži nevytvořil falešný záznam.

Výchozí minimální nárůst je **4 l** a lze jej změnit přes servisní WEB.

Pokud ECU stav paliva neposkytuje, lze pro některé výpočty použít ruční hodnotu.

---

# 7. Diagnostika

Diagnostická část je navržena primárně jako **read-only**.

## Celkový scan

Firmware umí prohledat seznam běžných VAG jednotek, například:

- 01 – Motor,
- 02 – Převodovka,
- 03 – ABS / ESP,
- 08 – Klimatizace,
- 09 – Centrální elektrika,
- 10 – Parkovací asistent,
- 15 – Airbag,
- 16 – Volant,
- 17 – Přístroje,
- 19 – CAN Gateway,
- 25 – Imobilizér,
- 42 / 52 / 62 / 72 – dveřní jednotky,
- 44 – Posilovač řízení,
- 46 – Komfort,
- 55 – Naklápění světel,
- 56 – Rádio,
- 69 – Tažné zařízení,
- 77 – Telefon.

Výsledek ukazuje:

- zda jednotka odpověděla,
- počet nalezených DTC,
- jednotlivé chybové kódy,
- stav „bez odpovědi“, pokud konkrétní modul přes daný protokol komunikovat neumí.

> „Bez odpovědi“ automaticky neznamená poruchu řídicí jednotky. Některé starší PQ35 jednotky mohou vyžadovat jiný diagnostický protokol nebo rozhraní.

Firmware v této části **nemaže závady a nezapisuje do jednotek**.

---

# 8. Senzory

Obrazovka **SENZORY** má dvě stránky a krátkodobý graf historie.

## Senzory 1/2

Podle podpory ECU se zobrazují:

- RPM,
- zatížení motoru,
- MAF,
- MAP,
- tlak RAIL,
- poloha škrticí klapky,
- napětí palubní sítě,
- okamžitý průtok paliva.

Graf sleduje přibližně posledních **60 sekund** a zobrazuje teplotní trend:

- chladicí kapalina,
- nasávaný vzduch,
- venkovní teplota.

## Senzory 2/2

Podle dostupnosti ECU:

- EGR požadavek,
- EGR odchylka,
- O2 B1S1,
- BARO,
- diferenciální tlak DPF,
- EGT před DPF,
- vypočítané saze,
- naměřené saze.

Graf na druhé stránce může zobrazit trend lambda hodnot.

---

# 9. Elektrika

Obrazovka **ELEKTRIKA** sleduje stav 12V palubní sítě.

Preferovaným zdrojem je standardní OBD napětí měřené ECU; pokud není dostupné, firmware může použít napájecí napětí hlášené ELM327.

Zobrazuje:

- **napětí baterie / palubní sítě**,
- **nabíjecí napětí při běžícím motoru**,
- **RPM**,
- stav motoru,
- stav elektrického alarmu.

### Orientační stavy

Firmware rozlišuje například:

- baterie OK,
- baterie slabší,
- nízké napětí,
- dobíjení OK,
- slabé dobíjení,
- přepětí.

---

# 10. Servis

Servisní obrazovka obsahuje dvě nezávislá počítadla:

### Olej / filtr

Výchozí interval:

- **15 000 km**,
- **365 dní**.

### Inspekce

Výchozí interval:

- **30 000 km**,
- **730 dní**.

U obou položek se zobrazuje:

- počet ujetých kilometrů od resetu,
- limit,
- počet dní / časový interval,
- grafický progress bar.

Reset servisního počítadla vyžaduje druhé potvrzení.

> Kilometry servisních intervalů se počítají pouze v době, kdy Octavia II PC běží a má dostupná jízdní data. Nejde o přepis servisního intervalu uloženého v přístrojové desce automobilu.

---

# 11. Alarmy

Jednotlivé alarmy lze samostatně zapnout nebo vypnout.

| Alarm | Funkce |
|---|---|
| **MOTOR > 105 °C** | Upozornění na vysokou teplotu chladicí kapaliny. |
| **NÍZKÉ NAPĚTÍ** | Při běžícím motoru hlídá příliš nízké napětí palubní sítě. |
| **PŘEPĚTÍ** | Při běžícím motoru hlídá příliš vysoké napětí. |
| **DPF REGENERACE** | Upozorní na začátek a dokončení potvrzené regenerace. |
| **DPF DO X KM** | Upozornění, pokud odhad do další regenerace klesne pod nastavený práh. |
| **ELM ODPOJENÍ** | Upozornění při výpadku uloženého ELM327. |

### Ochrana proti falešným alarmům

Nebezpečná hodnota musí určitou dobu trvat; jednorázový chybný OBD vzorek tedy nemá okamžitě vyvolat popup.

Používá se také hystereze, aby se stejné upozornění neopakovalo při hodnotě pohybující se přesně kolem hranice.

### Aktuální prahy firmware

- vysoká teplota motoru: **105 °C**,
- nízké napětí při běžícím motoru: přibližně pod **12,2 V**,
- přepětí při běžícím motoru: přibližně nad **15,2 V**,
- upozornění DPF „brzy“: nastavitelné **10–50 km**.

---

# 12. SPECIALS / Poděkování řidiči

**SPECIALS** je aktivní funkce – na rozdíl od běžného read-only monitoringu může posílat Output Test příkazy do centrální elektriky **J519**.

Aktuální funkce **PODĚKOVAT!** se pokouší vytvořit světelnou sekvenci:

```text
LEVÁ → PRAVÁ → OBA ×2 → PRAVÁ → LEVÁ → OBA ×1
```

Funkce je z bezpečnostních důvodů povolena pouze pro ověřený profil, u kterého je známa adresace J519.

### Bezpečnostní podmínky

Test vyžaduje:

- vozidlo stojí,
- motor je vypnutý,
- zapalování je zapnuté,
- ELM327 je připojený.

Pokud BCM/J519 příkaz odmítne, firmware může zobrazit diagnostický návratový kód místo předstírání úspěchu.

> SPECIALS není univerzálně garantovaná funkce. Různé verze BCM/J519 mohou mít jiné podmínky nebo nemusí daný Output Test podporovat.

---

# 13. Wi‑Fi / servisní WEB

Octavia II PC obsahuje vlastní lokální servisní WEB běžící přímo na ESP32.

### Přístup

```text
SSID:  Octavia-II-PC
Heslo: OctaviaPC
Web:   http://192.168.4.1
```

Internet není potřeba.

## Oddělení Wi‑Fi a Bluetooth

Wi‑Fi servisní režim je záměrně oddělen od Bluetooth OBD komunikace. Pro spuštění WEBu musí být ELM odpojený. Během WEB režimu je Bluetooth vypnutý.

Po ukončení WEBu firmware Bluetooth znovu inicializuje a může obnovit připojení k uloženému ELM327.

## WEB – Přehled

Servisní WEB obsahuje přehled uloženého / posledního známého stavu systému a vozidla. Nejde o druhý permanentní live OBD dashboard – WEB během aktivního Wi‑Fi režimu nepředstírá živá Bluetooth data.

## WEB – Kniha jízd

Umožňuje:

- zobrazit uložené jízdy,
- zobrazit detail jízd,
- stáhnout **CSV**,
- vygenerovat **PDF přehled**,
- vygenerovat **PDF jednotlivé jízdy**,
- tisknout přehled přes prohlížeč.

## WEB – Tankování

Umožňuje:

- zobrazit historii automaticky zaznamenaných tankování,
- stáhnout `fueling.csv`.

## WEB – DPF

Na podporovaném profilu zobrazuje:

- zanesení,
- saze,
- km od regenerace,
- odhad do regenerace,
- diferenciální tlak,
- historii regenerací,
- graf intervalů regenerací.

Na neověřené ECU jasně uvede, že rozšířené DPF není potvrzené.

## WEB – Nastavení

Přes WEB lze nastavovat mimo jiné:

### Displej

- denní jas,
- noční jas,
- automatický den / noc,
- touch feedback,
- sílu touch feedbacku.

### OBD / data

- rychlost dat / režim OBD,
- automatické připojení ELM,
- profil motoru / ECU.

### DPF

- hranici „DPF 100 %“ v gramech,
- upozornění na regeneraci,
- upozornění „regenerace brzy“,
- hranici kilometrů pro toto upozornění.

### Power

- automatické stmívání,
- čas do DIM,
- čas do vypnutí displeje,
- čas do LOW POWER.

### Vozidlo

- kapacitu nádrže,
- záložní spotřebu pro dojezd,
- minimální nárůst paliva pro detekci tankování,
- aktuální stav tachometru.

### Personalizace

- úvodní text,
- text v horní liště,
- jméno vlastníka / správce,
- e-mail,
- telefon,
- web / poznámku.

## Záloha konfigurace

WEB umí:

- export nastavení do **JSON**,
- import konfigurace zpět z JSON.

## Systémová stránka

Zobrazuje například:

- verzi firmware,
- aktivní profil motoru,
- identifikaci ECU,
- uptime,
- volnou a minimální RAM,
- využití LittleFS,
- počet klientů AP,
- Power režim,
- důvod posledního resetu,
- čas.

Z WEBu lze také synchronizovat čas podle telefonu a bezpečně ukončit servisní Wi‑Fi.

---

# 14. Power Manager

Power Manager snižuje spotřebu a omezuje svícení displeje při neaktivitě / vypnutém motoru.

Podporované stavy:

- **ACTIVE** – běžný provoz,
- **DIM** – snížený jas,
- **DISPLAY OFF** – zhasnutý displej,
- **LOW POWER** – zpomalení background polling smyček.

Nastavit lze:

- automatické stmívání,
- dobu do DIM,
- dobu do vypnutí displeje,
- dobu do LOW POWER.

Výchozí hodnoty jsou přibližně:

- DIM po **2 minutách**,
- displej OFF po **5 minutách**,
- LOW POWER po další **1 minutě**.

Firmware je připraven také na volitelný fyzický **ignition sense** vstup. S takovým vstupem lze použít hlubší sleep režim bezpečněji než pouze podle OBD aktivity.

---

# 15. Nastavení

Menu **NASTAVENÍ** je rozděleno do několika skupin.

## Displej

- denní jas,
- noční jas,
- AUTO den/noc,
- touch feedback,
- síla odezvy dotyku.

## OBD / DATA

- ruční připojení / odpojení ELM,
- automatické znovupřipojení,
- rychlost dat: **ECO / NORMAL / FAST**,
- výběr výchozího ELM327,
- vstup do pokročilého profilu vozidla.

## DPF nastavení

- upozornění na regeneraci,
- upozornění „regen brzy“,
- práh kilometrů do regenerace,
- nastavitelný limit sazí pro výpočet 100 %.

## System

- aktuální čas,
- verze firmware,
- volná RAM,
- uptime,
- pokročilé nastavení,
- obnovení výchozích nastavení.

Obnovení nastavení nemaže automaticky uložené jízdy a historii.

## Profil vozidla

Zobrazuje:

- nastavený profil,
- skutečně aktivní profil,
- detekovaný OBD protokol,
- identifikaci ECU.

Doporučená volba je **AUTO**.

---

# 16. INFO a personalizace

INFO obrazovka obsahuje:

- volitelné jméno vlastníka / správce,
- e-mail,
- telefon,
- web / poznámku,
- verzi firmware,
- identifikaci použitého hardware.

PUBLIC build nemá natvrdo vložené osobní údaje autora instalace.

Výchozí texty jsou záměrně nastaveny jako:

```text
NASTAVTE NAZEV VOZIDLA
VOZIDLO NENASTAVENO
```

Uživatel si je může změnit přes servisní WEB.

---

# 17. Bluetooth a ELM327

Projekt používá **Bluetooth Classic / SPP**, nikoli BLE.

### Funkce Bluetooth části

- scan dostupných zařízení,
- výběr vlastního ELM327,
- uložení názvu a MAC adresy jako výchozího zařízení,
- automatické připojení při startu,
- automatický reconnect při výpadku,
- ruční STOP neúspěšného připojování,
- obnovení Bluetooth stacku při opakovaném selhání,
- běžné párovací PINy `1234` a `0000`.

PUBLIC verze neobsahuje MAC adresu konkrétního adaptéru – každý uživatel si vybere svůj ELM.

OBD komunikace běží v samostatném **FreeRTOS tasku**, aby blokující odpověď ELM327 nezastavila dotykové UI.

---

# 18. MULTI-ECU a kompatibilita

Firmware používá profily motoru / ECU.

| Profil | Úroveň podpory |
|---|---|
| **AUTO** | Doporučený režim. Firmware zkouší detekci a bezpečný fallback. |
| **1.6 TDI CAYC / PCR2.1** | Nejlépe ověřený profil včetně rozšířeného DPF pro konkrétní potvrzené mapování ECU. |
| **2.0 TDI CR / EDC17** | Standardní OBD + základní UDS identifikace podle dostupnosti ECU. |
| **1.9 / 2.0 TDI PD / EDC16** | Základní Generic OBD; dostupnost hodnot závisí na ECU/protokolu. |
| **Generic OBD-II** | Bezpečný fallback pro standardní SAE PIDy. |

## Automatická detekce

Firmware se může pokusit:

1. použít běžný VAG CAN profil,
2. ověřit standardní OBD komunikaci,
3. přečíst identifikaci ECU přes UDS, pokud je dostupná,
4. vybrat nejbezpečnější profil,
5. aktivovat jen funkce, pro které existuje vhodný datový zdroj.

Schopnosti vozidla jsou oddělené – například Generic OBD může fungovat, i když konkrétní rozšířené DPF nebo SPECIALS dostupné nejsou.

---

# 19. Ukládání dat

Projekt používá dvě hlavní vrstvy ukládání.

## Preferences / NVS

Slouží pro dlouhodobá nastavení, například:

- jas displeje,
- režimy OBD,
- výchozí ELM,
- profil motoru,
- alarmy,
- DPF limity,
- kapacitu nádrže,
- tachometr,
- Power Manager,
- personalizaci.

## LittleFS

Používá se pro historii a větší záznamy, například:

- `trips_v2.csv` – strukturovaná historie jízd,
- `trips.csv` – zpětně kompatibilní čitelný formát,
- `trips_export.csv` – export pro uživatele,
- `fueling.csv` – historie tankování,
- `dpf.csv` – historie dokončených regenerací,
- checkpoint aktivní jízdy.

LittleFS se při běžném selhání mountu automaticky nemaže, aby chyba souborového systému sama o sobě nezničila historii.

---

# 20. Bezpečnost a omezení

- Projekt je **neoficiální komunitní zařízení** a není produktem společnosti Škoda Auto a.s.
- ESP32 nesmí být připojeno přímo na 12–14,8 V palubní sítě bez vhodného měniče / napájení.
- Displej nesmí odvádět pozornost řidiče.
- Dostupnost OBD PIDů se liší podle motoru, ECU, roku výroby a použitého ELM327.
- Levné ELM327 klony se mohou výrazně lišit podporou protokolů a příkazů.
- Hodnota `--` nebo `N/A` je záměrná, pokud firmware nemá potvrzená data.
- Diagnostický scan je primárně read-only a nenahrazuje VCDS/ODIS.
- Servisní počítadla jsou interní počítadla Octavia II PC; nepřepisují servisní interval v přístrojovém štítu.
- Predikce dojezdu a DPF regenerace jsou orientační výpočty.
- Funkce **SPECIALS** je aktivní výstupní test a musí se používat pouze na bezpečně odstaveném vozidle.

---

## Stručný souhrn

**Octavia II PC** spojuje v jednom zařízení:

- palubní počítač,
- živý OBD monitor,
- DPF monitor,
- knihu jízd,
- historii tankování,
- read-only diagnostiku,
- servisní počítadla,
- elektrické a motorové alarmy,
- MULTI-ECU profily,
- vlastní Wi‑Fi servisní WEB,
- konfiguraci a exporty,
- správu napájení,
- personalizované uživatelské rozhraní.

Projekt je navržen tak, aby na podporované ECU zobrazil co nejvíce užitečných dat a na neznámé ECU raději bezpečně omezil funkce, než aby zobrazoval neověřené hodnoty.
