# Octavia II PC

**Octavia II PC** je neoficiální komunitní palubní počítač a diagnostický displej pro **Škoda Octavia II (1Z)** postavený na **ESP32-2432S028R / CYD 2.8\" 320×240**.

Komunikuje s vozidlem přes **Bluetooth Classic ELM327**, zobrazuje vybraná motorová a DPF data, ukládá jízdní statistiky a nabízí vlastní servisní Wi‑Fi web.

**Aktuální veřejná verze:** `V16.30.2 PUBLIC`

> Projekt je primárně vyvíjen a testován pro **Škoda Octavia II facelift 1.6 TDI CAYC / PCR2.1**. Základní OBD funkce mohou fungovat i jinde, ale DPF/UDS část není univerzální pro všechny motory a řídicí jednotky.

![Octavia II PC – Přehled](00_overview.png)

---

## Co projekt umí

Stručně:

- hlavní palubní přehled s RPM, teplotami, turbem, palivem a dojezdem,
- DPF monitor včetně sledování regenerace,
- motorová, turbo, elektrická a senzorová data,
- kniha jízd a historie tankování,
- základní diagnostické informace a čtení DTC,
- servisní intervaly a nastavitelné alarmy,
- Bluetooth skener a automatické připojení k uloženému ELM327,
- servisní Wi‑Fi web pro nastavení a historii,
- nastavení jasu, textů, vozidla a dalších parametrů,
- správa napájení a příprava pro ignition sense.

Firmware je navržen především jako **doplňkový informační displej**. Diagnostická část je zaměřena na čtení dat, nikoli na kódování řídicích jednotek.

---

## Ukázky obrazovek

| Přehled | Motor | Turbo |
|---|---|---|
| ![](00_overview.png) | ![](01_motor.png) | ![](02_turbo.png) |

| Kniha jízd | Tankování | Diagnostika |
|---|---|---|
| ![](03_kniha_jizd.png) | ![](04_tankovani.png) | ![](05_diagnostika.png) |

Další náhledy jsou přímo v repozitáři.

---

# Hardware

## Potřebujete

- **ESP32-2432S028R / CYD 2.8\" 320×240**,
- datový USB kabel,
- počítač s Arduino IDE,
- **Bluetooth Classic ELM327**,
- vozidlo s podporovanou OBD komunikací.

### ELM327

Doporučen je adaptér používající **Bluetooth Classic / SPP**.

Wi‑Fi ELM327 ani BLE-only adaptéry nejsou určeny pro aktuální způsob komunikace tohoto firmware.

Levné ELM327 klony se mohou výrazně lišit kvalitou a podporovanými příkazy.

---

# Veřejná verze a personalizace

Tato PUBLIC verze **neobsahuje osobní MAC adresu ELM327, jméno původního uživatele ani jeho kontaktní údaje**.

Po prvním spuštění je proto potřeba nastavit vlastní instalaci.

### Uživatel musí nastavit

1. vlastní Bluetooth Classic ELM327,
2. název vozidla na úvodní obrazovce,
3. text v horní liště,
4. kapacitu nádrže a další parametry vozidla,
5. aktuální stav tachometru, pokud chce používat navazující evidenci kilometrů,
6. volitelně vlastní kontaktní údaje v INFO obrazovce.

Výchozí texty jsou záměrně:

```text
NASTAVTE NAZEV VOZIDLA
VOZIDLO NENASTAVENO
```

---

# Instalace od začátku

## 1. Nainstalujte Arduino IDE

Použijte **Arduino IDE 2.x**.

Oficiální stránka:

https://www.arduino.cc/en/software

---

## 2. Přidejte podporu ESP32

V Arduino IDE otevřete:

```text
File → Preferences
```

Do **Additional Boards Manager URLs** přidejte:

```text
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

Potom otevřete:

```text
Tools → Board → Boards Manager
```

Vyhledejte:

```text
esp32 by Espressif Systems
```

Projekt je připraven pro řadu **Arduino-ESP32 3.3.x**.

---

## 3. Nainstalujte knihovny

V **Library Manageru** nainstalujte:

- **TFT_eSPI** – Bodmer
- **TFT_eWidget** – Bodmer
- **PNGdec** – Larry Bank / bitbank2
- **XPT2046_Touchscreen** – Paul Stoffregen

Ostatní použité ESP32 knihovny jsou součástí ESP32 core.

---

## 4. Rozbalte projekt

Hlavní soubory jsou:

```text
Octavia_II_PC_v16_30_FIX2_PUBLIC/
├── Octavia_II_PC_v16_30_FIX2_PUBLIC.ino
├── MenuIcons.h
├── StartupLogo.h
├── User_Setup.h
└── README.md
```

### Důležité

Arduino IDE očekává, že **název složky projektu bude stejný jako název hlavního `.ino` souboru**.

Tedy:

```text
Octavia_II_PC_v16_30_FIX2_PUBLIC/
└── Octavia_II_PC_v16_30_FIX2_PUBLIC.ino
```

`MenuIcons.h` a `StartupLogo.h` musí zůstat ve stejné složce jako `.ino`.

---

## 5. Nastavte TFT_eSPI

Součástí projektu je připravený:

```text
User_Setup.h
```

Ten je určen pro použitý CYD displej.

Najděte složku knihovny TFT_eSPI, například na Windows:

```text
Documents\Arduino\libraries\TFT_eSPI\
```

Původní `User_Setup.h` si doporučuji zazálohovat a následně jej nahraďte souborem dodaným s projektem.

> Po aktualizaci knihovny TFT_eSPI se může její `User_Setup.h` přepsat. Pokud displej po aktualizaci přestane fungovat správně, zkontrolujte nejdříve tento soubor.

---

## 6. Nastavení desky

V Arduino IDE použijte odpovídající CYD profil nebo běžný:

```text
ESP32 Dev Module
```

Doporučené základní nastavení:

```text
Flash Size:       4 MB
Partition Scheme: Huge APP (3MB No OTA / 1MB SPIFFS)
Serial Monitor:   115200 baud
```

Pokud používáte vlastní board definici pro ESP32-2432S028R, zachovejte odpovídající velikost flash a partition scheme.

---

## 7. Ověření a nahrání

Připojte ESP32 datovým USB kabelem.

V Arduino IDE vyberte správný **COM port** a nejprve použijte:

```text
Verify / Ověřit
```

Pokud kompilace proběhne bez chyby, použijte:

```text
Upload / Nahrát
```

Po dokončení se ESP32 restartuje a spustí **Octavia II PC**.

---

# První spuštění

Veřejný firmware nemá uloženou cizí MAC adresu ELM327.

Při první konfiguraci proto vyberte **vlastní ELM327** pomocí Bluetooth skeneru na displeji.

Po nalezení adaptéru jej zvolte a uložte jako výchozí zařízení.

Běžné PINy ELM327 bývají:

```text
1234
0000
```

Po úspěšném připojení může firmware uložený adaptér používat při dalších startech automaticky.

---

# Servisní Wi‑Fi / WEB

Servisní web slouží pro konfiguraci, historii a další servisní funkce.

Po zapnutí webového režimu vytvoří ESP32 vlastní Wi‑Fi síť:

```text
SSID:   Octavia-II-PC
Heslo:  OctaviaPC
```

Telefon nebo notebook připojte k této síti a otevřete:

```text
http://192.168.4.1
```

Telefon může hlásit, že Wi‑Fi **nemá přístup k internetu**. To je správně — jde o lokální síť vytvořenou ESP32.

Bluetooth komunikace s ELM327 a servisní Wi‑Fi jsou záměrně oddělené, aby se co nejméně navzájem rušily.

Po skončení práce webový režim ukončete přímo z displeje nebo webového rozhraní.

---

# Nastavení vozidla

V servisním webu doporučuji po prvním spuštění projít zejména:

```text
NASTAVENÍ → TEXTY
```

Zde si nastavíte vlastní název vozidla a údaje zobrazované na displeji.

Dále zkontrolujte:

- kapacitu palivové nádrže,
- výchozí / záložní hodnoty spotřeby,
- stav tachometru,
- limit detekce tankování,
- servisní hodnoty a alarmy.

---

# DPF

DPF část používá kromě standardního OBD také konkrétní VAG/UDS data.

Aktuální implementace je primárně laděna pro:

```text
Škoda Octavia II facelift
1.6 TDI CAYC
ECU PCR2.1
```

Na jiné ECU mohou některé DPF hodnoty zobrazovat `--` nebo nemusí být dostupné vůbec.

Firmware raději zobrazí nedostupnou hodnotu než údaj, který nebyl potvrzen z řídicí jednotky.

---

# Aktualizace firmware

Pokud už zařízení používáte a máte uloženou historii jízd, tankování nebo vlastní nastavení:

**nepoužívejte bez důvodu Erase All Flash.**

Při běžném nahrání nové verze se uložená data obvykle zachovají, pokud nezměníte partition layout nebo ručně nesmažete flash.

Před větší aktualizací je vhodné důležitá data exportovat přes servisní web.

---

# Nejčastější problémy

### Displej je bílý, černý nebo zobrazuje nesmysly

Zkontrolujte správný `User_Setup.h` v knihovně TFT_eSPI.

### Dotyk nereaguje správně

Ověřte použitou variantu ESP32-2432S028R a nastavení pinů v dodaném `User_Setup.h`.

### Upload se zasekne na `Connecting...`

Vyzkoušejte:

- jiný datový USB kabel,
- jiný USB port,
- správný COM port,
- nižší Upload Speed,
- případně tlačítko BOOT při začátku uploadu.

### ELM327 se nepřipojí

Zkontrolujte:

- že jde o **Bluetooth Classic ELM327**,
- zapnuté zapalování,
- napájení OBD zásuvky,
- zda ELM není současně připojen k jinému telefonu nebo aplikaci,
- případně zkuste jiný ELM327 adaptér.

### WEB nejde otevřít

Zkontrolujte, že:

1. je servisní WEB skutečně zapnutý na displeji,
2. telefon je připojen k `Octavia-II-PC`,
3. otevíráte `http://192.168.4.1`,
4. telefon automaticky nepřepnul zpět na mobilní data nebo jinou Wi‑Fi.

---

# Struktura repozitáře

Kromě samotného firmware repozitář obsahuje také grafické náhledy jednotlivých obrazovek a poznámky ke změnám jednotlivých verzí.

```text
Octavia_II_PC_v16_30_FIX2_PUBLIC.ino   hlavní firmware
MenuIcons.h                            ikony menu
StartupLogo.h                          grafika úvodní obrazovky
User_Setup.h                           konfigurace TFT_eSPI
README_SETUP.txt                       stručné instalační poznámky
V*_CHANGES.txt                         historie změn
*.png                                  náhledy obrazovek a grafiky
```

---

# Bezpečnost

ESP32 **nikdy nepřipojujte přímo na 12–14,8 V palubní sítě vozidla**. Použijte vhodný stabilizovaný napájecí zdroj / DC-DC měnič odpovídající požadavkům desky.

Displej je doplňkové zařízení. Za jízdy jej neovládejte způsobem, který odvádí pozornost od řízení.

Hodnoty zobrazované firmwarem nejsou náhradou profesionální automobilové diagnostiky.

---

# Ochranná známka a odpovědnost

**Octavia II PC je neoficiální komunitní projekt. Není produktem ani oficiálním softwarem Škoda Auto a není se Škoda Auto spojen.**

Názvy vozidel a ochranné známky patří jejich příslušným vlastníkům.

Použití firmware a případné zásahy do vozidla jsou na vlastní odpovědnost uživatele.

---

## Octavia II PC

ESP32-2432S028R • ELM327 Bluetooth Classic • Škoda Octavia II 1Z • CAYC / PCR2.1
