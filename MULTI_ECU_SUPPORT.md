# Octavia II PC – MULTI-ECU podpora

Tento dokument popisuje chování firmware **V16.31 MULTI-ECU** na různých variantách Octavia II (1Z).

## Princip

Cílem není tvrdit, že každá ECU poskytuje stejné diagnostické hodnoty. Firmware odděluje:

1. **standardní OBD-II/EOBD data** – lze je použít napříč více motory, pokud je ECU podporuje,
2. **VAG/UDS identifikaci** – používá se na kompatibilním CANu pro rozpoznání ECU,
3. **proprietární hodnoty** – zapnou se pouze tehdy, když je jejich mapování pro danou ECU ověřené.

## Profily

### AUTO

Doporučené nastavení.

- nejprve zkusí ELM protokol 6: ISO 15765-4 CAN 11-bit / 500 kbit/s,
- při neúspěchu zkusí `ATSP0` a nechá ELM327 najít OBD protokol,
- na CANu se pokusí načíst read-only UDS identifikaci `F190`, `F187`, `F189`, `F19E`,
- podle identifikace zvolí profil,
- pokud si není jistý, použije Generic OBD-II.

### 1.6 TDI / PCR2.1

Standardní OBD funguje podle dostupných PIDů. Rozšířené DPF hodnoty jsou automaticky povoleny pouze pro ECU mapování, které bylo v projektu ověřeno (`03L906023LP`).

U jiné PCR2.1 zůstane DPF raw mapa vypnutá, dokud nebude ověřena.

### 2.0 TDI CR / EDC17

Firmware používá standardní OBD a na kompatibilním CANu umí UDS identifikaci a read-only diagnostický scan běžných VAG modulů.

Rozšířené DPF saze/regenerace nejsou v této verzi mapovány univerzálním raw DIDem. To je záměrné – EDC17 existuje v mnoha SW/ASAM variantách.

### 1.9 / 2.0 TDI PD / EDC16

Firmware je schopný běžet přes automaticky zvolený EOBD protokol. Zobrazuje jen standardní hodnoty, které konkrétní ECU skutečně poskytne.

VAG proprietární DPF a pevný UDS scan ostatních modulů jsou v tomto profilu vypnuté.

### Generic OBD-II

Bezpečný fallback pro neznámou ECU nebo jiný motor. Zachovává základní funkce palubního počítače bez předstírání VAG-specifických hodnot.

## Capability Manager

Firmware interně rozlišuje schopnosti profilu:

- standardní OBD,
- VAG UDS,
- scan VAG modulů,
- rozšířené DPF,
- standardní DPF tlak,
- SPECIALS/J519 output test.

UI se podle toho chová: nepodporovaný DPF profil je označen jako `N/A / neověřeno` a SPECIALS se na neověřeném profilu nespustí.

## Co je potřeba k doplnění dalšího plného DPF profilu

Pro konkrétní motor/ECU je ideální dodat:

- motorový kód,
- číslo ECU,
- SW verzi,
- ASAM/ODX identifikaci,
- referenční hodnoty z VCDS/ODIS při stejné jízdě,
- případně read-only log odpovědí ECU.

Teprve po porovnání lze bezpečně přidat scaling a význam proprietárních hodnot.
