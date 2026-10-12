# Octavia II PC

**Octavia II PC** je neoficiální komunitní palubní počítač / diagnostický displej pro **Škoda Octavia II (1Z)** postavený na **ESP32-2432S028R / CYD 2.8" 320×240**. Firmware používá Bluetooth Classic ELM327, zobrazuje motorová a DPF data, vede knihu jízd a historii tankování a nabízí servisní Wi‑Fi web.

Aktuální firmware v tomto balíku: **V16.30.2 PUBLIC**.

> Projekt je primárně vyvíjen a testován pro Octavia II facelift 1.6 TDI CAYC / PCR2.1. DPF/UDS data nejsou univerzální pro všechny motory a ECU.

## Důležité: veřejná verze je záměrně bez osobních údajů

Po prvním nahrání firmware **není přednastaven cizí ELM327 ani osobní název vozidla**. Uživatel musí dokončit vlastní konfiguraci:

1. vybrat svůj Bluetooth Classic ELM327,
2. v servisním webu změnit `NASTAVTE NAZEV VOZIDLA`,
3. změnit `VOZIDLO NENASTAVENO` v horní liště,
4. zkontrolovat parametry vozidla (nádrž, tachometr, spotřeba a další),
5. volitelně doplnit vlastní kontakt/správce instalace.

## Servisní Wi‑Fi

- **SSID:** `Octavia-II-PC`
- **Heslo:** `OctaviaPC`
- **Web:** `http://192.168.4.1`

Wi‑Fi servisní režim je oddělen od Bluetooth komunikace s ELM327.

## Hlavní soubory

```text
Octavia_II_PC_v16_30_FIX2_PUBLIC/
├── Octavia_II_PC_v16_30_FIX2_PUBLIC.ino
├── MenuIcons.h
├── StartupLogo.h
├── User_Setup.h
└── ... grafické náhledy a poznámky k verzím
```

Název složky sketche a hlavního `.ino` souboru je shodný, aby jej Arduino IDE otevřelo bez přejmenování.

## Knihovny

- TFT_eSPI
- TFT_eWidget
- PNGdec
- XPT2046_Touchscreen
- Arduino-ESP32 3.3.x

Podrobnější instalační poznámky jsou v `README_SETUP.txt`.

## Bezpečnost a omezení

Firmware je doplňkový monitor. Diagnostické funkce mají být používány s vozidlem bezpečně a displej neovládejte během jízdy. DPF/UDS hodnoty závisí na konkrétní ECU. Levné ELM327 klony se mohou chovat nestandardně.

## Ochranná známka

**Octavia II PC je neoficiální komunitní projekt. Není produktem ani oficiálním softwarem Škoda Auto a není se Škoda Auto spojen.**
