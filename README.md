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
ESP32
   │
   ▼
TFT displej
ESP32 se pokouší automaticky připojit k nastavenému Bluetooth OBD adaptéru.
Uživatelské rozhraní
Menu je navrženo jako dotykové rozhraní optimalizované pro použití v automobilu.
Obsahuje několik stránek s dlaždicemi.

