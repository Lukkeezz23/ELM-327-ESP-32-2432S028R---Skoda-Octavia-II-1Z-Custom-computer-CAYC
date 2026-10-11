# Luke – Octavia II Display

Multifunkční dotykový displej pro **Škoda Octavia II / 1Z**, postavený na ESP32 s barevným TFT displejem.

Projekt je zaměřený na zobrazování provozních dat vozidla, DPF, motorových hodnot, servisních informací, knihy jízd, historie tankování a dalších funkcí přímo v autě.

> Projekt je stále ve vývoji.

---

## Hlavní funkce

### Přehled vozidla
- základní provozní informace
- stav připojení
- teploty
- rychlost
- otáčky
- další důležité hodnoty

### Motor
- otáčky motoru
- teplota chladicí kapaliny
- zatížení motoru
- provozní hodnoty získané přes OBD

### Turbo
- tlak turba
- požadované / skutečné hodnoty
- další související údaje

### DPF Monitor
- stav DPF
- množství sazí
- informace o regeneraci
- historie regenerací
- provozní hodnoty související s filtrem pevných částic

### Kniha jízd
Automatické ukládání jízd.

U jednotlivých jízd lze evidovat například:
- datum
- čas
- vzdálenost
- spotřebované palivo
- dobu jízdy
- průměrné hodnoty

Data jsou ukládána do interní paměti ESP32.

### Historie tankování
Automatická detekce tankování podle změny množství paliva v nádrži.

Ukládá se například:
- datum
- čas
- stav kilometrů
- množství paliva před tankováním
- množství paliva po tankování
- odhad natankovaných litrů

Systém používá filtraci změn hladiny paliva, aby krátkodobé výkyvy v nádrži nebyly zaznamenány jako tankování.

### Diagnostika
- čtení diagnostických informací
- zobrazení některých chybových stavů
- monitoring dostupných řídicích jednotek
- základní diagnostické údaje

### Servis
- servisní informace
- počítadla
- intervaly
- upozornění

### Alarmy
Možnost zobrazovat upozornění při překročení nastavených hodnot.

Například:
- vysoká teplota
- nevhodné provozní hodnoty
- stav DPF
- servisní upozornění

### Nastavení
Možnost měnit například:
- text úvodní obrazovky
- text v horní liště
- vybrané parametry displeje
- Wi-Fi nastavení
- některé limity a uživatelské hodnoty

### Webové rozhraní
ESP32 vytváří vlastní webové rozhraní.

Přes telefon nebo počítač lze:
- zobrazit některá data
- změnit nastavení
- upravit texty displeje
- synchronizovat datum a čas
- stáhnout CSV soubory
- pracovat s historií jízd
- pracovat s historií tankování

---

## Hardware

Projekt je určen především pro:

- ESP32
- TFT displej 320 × 240
- dotykovou vrstvu
- modul ESP32-2432S028R / podobnou kompatibilní konfiguraci
- ELM327 Bluetooth adaptér
- vozidlo Škoda Octavia II / 1Z

Displej je používán v orientaci:

**320 × 240 px – landscape**

---

## Připojení k vozidlu

Komunikace s vozidlem probíhá přes:

```text
Octavia II
   │
   ▼
OBD-II
   │
   ▼
ELM327 Bluetooth
   │
   ▼
ESP32
   │
   ▼
TFT displej
ESP32 se pokouší automaticky připojit k nastavenému Bluetooth OBD adaptéru.
Uživatelské rozhraní
Menu je navrženo jako dotykové rozhraní optimalizované pro použití v automobilu.
Obsahuje několik stránek s dlaždicemi, například:
PŘEHLED
MOTOR
TURBO
DPF PRO
KNIHA JÍZD
HISTORIE TANKOVÁNÍ
DIAGNOSTIKA
SERVIS
ALARMY
NASTAVENÍ
UI používá tmavý automobilový styl s barevně odlišenými sekcemi.
Ukládání dat
Projekt používá:
LittleFS
Pro ukládání lokálních souborů.
Typické soubory:
/trips.csv
/fueling.csv
/dpf.csv
Obsahují historii jízd, tankování a regenerací DPF.
Struktura projektu
Příklad:
Luke_Octavia_II/
│
├── Luke_Octavia_II.ino
├── *.h
├── icons/
├── assets/
└── README.md
Některé grafické prvky mohou být uloženy přímo v .h souborech ve formě bitmap nebo datových polí.
Požadavky
Pro kompilaci je potřeba:
Arduino IDE
ESP32 Board Support
TFT_eSPI
LittleFS
Bluetooth / BLE knihovny používané ESP32
další knihovny podle aktuální verze sketche
Doporučená platforma:
Arduino ESP32 Core 3.x
Instalace
Nainstaluj Arduino IDE.
Přidej podporu ESP32.
Nainstaluj potřebné knihovny.
Nastav TFT_eSPI podle použitého displeje.
Otevři hlavní .ino soubor.
Připoj ESP32 přes USB.
Vyber správnou desku a COM port.
Zkompiluj projekt.
Nahraj firmware do ESP32.
Restartuj zařízení.
Podrobný instalační návod je dostupný samostatně v dokumentaci projektu.
Webové rozhraní
Po aktivaci Wi-Fi lze rozhraní otevřít z telefonu nebo počítače.
Webová část slouží například pro:
konfiguraci
změnu textů
synchronizaci času
export dat
správu uživatelských hodnot
Bezpečnost
Projekt je určen především pro zobrazování a vyhodnocování dat.
Diagnostické funkce by měly být používány opatrně.
Nepoužívej zařízení způsobem, který by mohl:
odvádět pozornost při řízení
ovlivnit bezpečnost vozidla
měnit kritické parametry řídicích jednotek bez znalosti jejich významu
Autor projektu nenese odpovědnost za škody vzniklé nesprávným použitím.
Kompatibilita
Primární cílové vozidlo:
Škoda Octavia II / 1Z
Projekt vzniká a je testován především pro tuto platformu.
Funkčnost na jiných vozidlech není zaručena.
Stav projektu
Projekt je aktivně vyvíjen.
Mohou se měnit:
funkce
menu
diagnostické hodnoty
grafika
webové rozhraní
způsob ukládání dat
Plánované funkce
Do budoucna lze projekt rozšířit například o:
více VAG diagnostických hodnot
pokročilejší DPF statistiky
grafy jízd
statistiky tankování
dlouhodobou spotřebu
automatické servisní záznamy
další obrazovky
export statistik
pokročilejší webové rozhraní
Upozornění
Tento projekt není oficiálním produktem společnosti Škoda Auto a není společností Škoda Auto podporován ani schválen.
Názvy značek a ochranné známky patří jejich příslušným vlastníkům.
Licence
Projekt je distribuován pod licencí uvedenou v souboru:
LICENSE
Autor
Luke – Octavia II Display Project
