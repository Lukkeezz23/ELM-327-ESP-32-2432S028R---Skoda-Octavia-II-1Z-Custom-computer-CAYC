Luke – Octavia II Display

Palubní dotykový displej pro Škoda Octavia II (1Z) postavený na ESP32-2432S028R / CYD 2.8" 320×240. Projekt komunikuje přes Bluetooth Classic s adaptérem ELM327, čte motorová a DPF data, vede knihu jízd, sleduje tankování, zobrazuje alarmy a nabízí lehké servisní webové rozhraní.

Aktuální firmware v tomto repozitáři: V16.30.1 FIX1.

Projekt je primárně vyvíjen a laděn pro Škoda Octavia II facelift, 1.6 TDI CAYC / PCR2.1. Základní OBD funkce mohou fungovat i na jiných vozech koncernu VAG, ale zejména DPF data jsou závislá na konkrétní ECU a nejsou univerzální.



Obsah

Co projekt umí

Podporovaný hardware

Důležité informace před instalací

Instalace pro úplného začátečníka

První spuštění a připojení ELM327

Servisní Wi-Fi a WEB V2

Přehled obrazovek

DPF PRO

Kniha jízd

Tankování

Nastavení

Správa napájení

Alarmy

Diagnostika

SPECIALS

Datové soubory a LittleFS

Řešení problémů

Bezpečnost a omezení

Struktura projektu

Co projekt umí

Hlavní funkce

dotykové grafické menu ve třech stránkách,

automatické připojení k uloženému ELM327,

Bluetooth skener pro výběr jiného adaptéru,

možnost okamžitě zastavit neúspěšné pokusy o připojení k ELM,

hlavní automobilový dashboard PŘEHLED,

obrazovka MOTOR s grafickými ukazateli,

obrazovka TURBO,

pokročilý DPF PRO monitoring,

automatická detekce aktivní regenerace DPF,

upozornění na začátek a konec regenerace,

predikce kilometrů do další regenerace,

historie regenerací,

KNIHA JÍZD s ukládáním do LittleFS,

historie tankování a automatická detekce nárůstu hladiny paliva,

obrazovka SENZORY s historií a grafy,

napětí elektrické soustavy,

čtení diagnostických chyb DTC,

servisní počítadla,

nastavitelné alarmy,

Power Manager,

servisní Wi-Fi Access Point,

lehké WEB V2 rozhraní,

CSV export jízd a tankování,

tisk / PDF přehledů přes webový prohlížeč,

export a import konfigurace,

vlastní startup logo a PNG ikony dekódované přes PNGdec.

Co projekt záměrně nedělá

nenahrazuje VCDS nebo ODIS,

neprovádí běžné kódování řídicích jednotek,

automaticky nemaže DTC,

nepoužívá Security Access pro běžnou diagnostiku,

není univerzálním DPF monitorem pro všechny motory,

servisní web není plnohodnotný server ani infotainment.

Podporovaný hardware

Povinné

1. ESP32 displej

Projekt je připraven pro:

ESP32-2432S028R / CYD 2.8" / 320×240

Konfigurace dodaná v projektu používá:

displejový driver ST7789,

dotykový řadič XPT2046,

landscape orientaci 320×240,

podsvícení na GPIO 21.

Existuje více hardwarových revizí desek označovaných jako ESP32-2432S028R/CYD. Pokud má vaše deska jiný LCD driver nebo jiné zapojení, dodaný User_Setup.h nemusí být správný.

2. ELM327

Je nutný adaptér s:

Bluetooth Classic / SPP,

podporou ELM327 příkazů,

CAN ISO 15765-4,

ideálně spolehlivou podporou delších ISO-TP / UDS zpráv.

BLE-only ELM327 není podporovaný. Firmware používá BluetoothSerial, tedy Bluetooth Classic.

Výchozí zařízení v aktuálním projektu:

Název: OBDII
MAC:   19:05:20:02:6e:68
PIN:   1234
záložní PIN: 0000

Jiný adaptér lze vyhledat přímo na displeji a uložit jako nový výchozí.

3. USB kabel

Použijte datový USB kabel. Některé levné USB kabely umí pouze napájení a Arduino IDE přes ně desku neuvidí.

Důležité informace před instalací

Cílová ECU

DPF část byla vyvíjena pro CAYC/PCR2.1 a potvrzené hodnoty zahrnují ECU:

03L906023LP
SW 9972

Potvrzené DPF DIDy:

DID

Význam

22 208B

vypočítané saze

22 208C

naměřené saze

22 208D

km od poslední regenerace

22 20D8

palivo od poslední regenerace

22 207D

sledovaný stav / counter regenerace

Motorová ECU se standardně adresuje:

TX: 0x7E0
RX: 0x7E8

Firmware primárně nastavuje ELM protokol:

ATSP6

tedy ISO 15765-4 CAN, 11 bit, 500 kbit/s.

DPF 100 % není univerzální hodnota ECU

Výchozí referenční hodnota v projektu je:

24.0 g = 100 %

Je nastavitelná. Není prezentována jako univerzální tvrdý limit všech řídicích jednotek.

EGT

EGT = Exhaust Gas Temperature, tedy teplota výfukových plynů. Pokud konkrétní ECU neposkytne správně namapovanou EGT hodnotu, firmware zobrazí --. Hodnota se nevymýšlí.

Diferenciální tlak DPF

Firmware umí použít podporovaný zdroj diferenciálního tlaku, ale ne každá ECU / ELM kombinace jej poskytne v očekávaném formátu. Pokud není dostupný, zobrazí se --.

Instalace pro úplného začátečníka

Následující část předpokládá Windows a Arduino IDE 2.x.

Krok 1 – stáhněte Arduino IDE

Nainstalujte Arduino IDE 2.x.

Po instalaci Arduino IDE spusťte.

Při prvním spuštění nechte IDE dokončit inicializaci.

Krok 2 – nainstalujte podporu ESP32

V Arduino IDE otevřete:

File / Soubor → Preferences / Předvolby

Do pole Additional Boards Manager URLs přidejte:

https://espressif.github.io/arduino-esp32/package_esp32_index.json

Potvrďte tlačítkem OK.

Potom otevřete:

Tools → Board → Boards Manager

Vyhledejte:

esp32

Nainstalujte balíček:

esp32 by Espressif Systems

Pro tento projekt doporučujeme řadu Arduino-ESP32 3.3.x. Projekt je aktuálně vyvíjen s prostředím odpovídajícím 3.3.12.

Po instalaci Arduino IDE restartujte.

Krok 3 – nainstalujte potřebné knihovny

Otevřete:

Sketch → Include Library → Manage Libraries

Postupně vyhledejte a nainstalujte:

Povinné externí knihovny

TFT_eSPI

Autor: Bodmer

TFT_eWidget

Autor: Bodmer

PNGdec

Autor: Larry Bank / bitbank2

XPT2046_Touchscreen

Autor: Paul Stoffregen

Knihovny, které už dodává ESP32 core

Tyto knihovny není potřeba zvlášť instalovat:

SPI

FS

LittleFS

BluetoothSerial

Preferences

WiFi

WebServer

time

FreeRTOS

Neinstalujte náhodné duplicitní knihovny se stejnými názvy. Mohlo by dojít ke konfliktu s verzemi dodanými v ESP32 core.

Krok 4 – připravte projektovou složku

Rozbalte ZIP projektu.

Složka musí obsahovat minimálně:

Luke_Octavia_II_v16_30_FIX1_WEB_STOP_RPM.ino
MenuIcons.h
StartupLogo.h
User_Setup.h

Dále v ní mohou být PNG náhledy, changelogy a další pomocné soubory.

Velmi důležité pravidlo Arduino IDE

Název hlavního .ino souboru a název jeho složky mají být stejné.

Například:

Luke_Octavia_II_v16_30_FIX1_WEB_STOP_RPM/
└── Luke_Octavia_II_v16_30_FIX1_WEB_STOP_RPM.ino

Potom otevřete .ino soubor dvojklikem nebo přes Arduino IDE.

Krok 5 – nastavte TFT_eSPI

Toto je jeden z nejdůležitějších kroků.

Projekt obsahuje vlastní:

User_Setup.h

Ten musí být použit knihovnou TFT_eSPI.

Najděte složku TFT_eSPI

Typická cesta ve Windows je:

C:\Users\VAŠE_JMÉNO\Documents\Arduino\libraries\TFT_eSPI\

V této složce najdete původní:

User_Setup.h

Postup

Původní User_Setup.h si pro jistotu zazálohujte.

Zkopírujte User_Setup.h z tohoto projektu.

Vložte jej do složky TFT_eSPI.

Povolte nahrazení původního souboru.

Dodaný setup obsahuje například:

#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1

#define TFT_BL   21

Po aktualizaci knihovny TFT_eSPI může Library Manager tento soubor přepsat. Pokud po aktualizaci displej přestane fungovat, zkontrolujte User_Setup.h jako první.

Krok 6 – připojte ESP32 k počítači

Připojte ESP32 pomocí datového USB kabelu.

Počkejte několik sekund.

V Arduino IDE otevřete Tools → Port.

Vyberte nový COM port.

Například:

COM5

Pokud žádný COM port nevidíte

Zkontrolujte:

že USB kabel přenáší data,

jiný USB port,

Správce zařízení ve Windows,

případně ovladač USB-UART převodníku vaší konkrétní desky (často CH340 nebo CP210x).

Krok 7 – nastavte desku v Arduino IDE

Pokud máte v instalaci přímo profil pro svou CYD desku, použijte jej.

Pokud ne, běžně se tato deska kompiluje jako:

ESP32 Dev Module

Důležité nastavení:

Flash Size:       4MB
Partition Scheme: Huge APP (3MB No OTA / 1MB SPIFFS)
Upload Speed:     921600
Serial Monitor:   115200 baud

Pokud upload na 921600 selhává, zkuste:

460800

nebo:

115200

Proč je důležitý Huge APP

Firmware je rozsáhlý. Pokud zvolíte příliš malou APP partition, Arduino IDE může hlásit, že sketch je příliš velký.

Krok 8 – nejdříve použijte Verify

Klikněte na tlačítko:

Verify / Ověřit

Arduino IDE projekt pouze zkompiluje a zatím nic nenahraje.

Pokud se objeví chyba, před dalším postupem ji opravte.

Nejčastější příčiny:

chybí TFT_eWidget,

chybí PNGdec,

chybí XPT2046_Touchscreen,

špatná verze ESP32 core,

špatně nastavený TFT_eSPI User_Setup.h,

špatná partition.

Krok 9 – nahrajte firmware

Klikněte:

Upload / Nahrát

Počkejte na dokončení.

Typicky se zobrazí hláška podobná:

Writing at ...
Hash of data verified.
Leaving...
Hard resetting via RTS pin...

ESP32 se následně restartuje.

Pokud upload nezačne

Podle revize desky může být nutné:

stisknout a držet tlačítko BOOT,

spustit Upload,

po zobrazení Connecting... BOOT uvolnit.

Ne všechny CYD desky to vyžadují.

První inicializace LittleFS

Projekt ukládá historii do interního souborového systému LittleFS.

Kvůli ochraně dat je ve zdrojáku standardně:

#define ALLOW_FS_FORMAT_IF_MOUNT_FAIL 0

To znamená:

pokud LittleFS selže, firmware nikdy automaticky nesmaže historii.

To je důležité po běžných updatech firmware.

Úplně nový ESP32

Na úplně novém čipu nemusí LittleFS ještě existovat.

Po prvním uploadu otevřete:

Tools → Serial Monitor

Nastavte:

115200 baud

Pokud uvidíte:

LittleFS OK.

nemusíte nic dělat.

Pokud uvidíte:

LittleFS init selhal - data NEBYLA smazana.

udělejte jednorázově následující:

Najděte ve sketchi:

#define ALLOW_FS_FORMAT_IF_MOUNT_FAIL 0

Dočasně změňte na:

#define ALLOW_FS_FORMAT_IF_MOUNT_FAIL 1

Firmware znovu nahrajte.

Počkejte, až Serial Monitor zobrazí LittleFS OK.

Vraťte hodnotu zpět na:

#define ALLOW_FS_FORMAT_IF_MOUNT_FAIL 0

Firmware nahrajte ještě jednou.

Od této chvíle nechávejte hodnotu 0.

Pokud byste ji dlouhodobě ponechali na 1, náhodná chyba mountu by teoreticky mohla způsobit formátování a ztrátu historie.

První spuštění a připojení ELM327

1. Zapojte ELM327 do auta

OBD zásuvka musí být napájena.

Pro první test doporučujeme:

zapnout zapalování,

případně nastartovat motor, pokud chcete vidět živá motorová data.

2. ESP32 se pokusí o výchozí ELM

Firmware má z výroby nastavené:

OBDII
19:05:20:02:6e:68

Před automatickým připojením nabídne krátkou možnost:

VYBRAT JINÉ

OFFLINE

Pokud používáte jiný ELM, stiskněte VYBRAT JINÉ.

3. Bluetooth sken

ESP32 vyhledá okolní Bluetooth Classic zařízení.

Vyberte svůj ELM podle:

názvu,

MAC adresy,

síly signálu RSSI.

Po úspěšném připojení můžete zařízení uložit jako výchozí.

4. PIN

Firmware automaticky zkouší:

1234

Pokud neuspěje:

0000

5. Inicializace ELM

Po Bluetooth připojení se provede mimo jiné:

ATZ
ATD
ATPC
ATE0
ATL0
ATS0
ATH1
ATAT1
ATAL
ATCAF1
ATCFC1
ATSP6

Poté se zkouší ECU na adrese 7E0 → 7E8 a read-only UDS identifikace.

Co znamenají stavy ELM

PŘIPOJENO

Bluetooth spojení s ELM existuje.

ECU ONLINE

Motorová ECU odpovídá na skutečné OBD požadavky.

Samotné napětí z příkazu ATRV se nepovažuje za důkaz, že ECU komunikuje.

OBD OK, UDS NE

Základní legislativní OBD funguje, ale ELM nebo ECU neodpovídá na potřebné UDS dotazy. Základní motorová data mohou fungovat, DPF PRO ale nemusí.

OFFLINE

ESP32 funguje bez ELM. Menu a uložená data lze používat, živé hodnoty nebudou dostupné.

Servisní Wi-Fi a WEB V2

Web je navržen záměrně lehce, aby ESP32 nebylo přetížené.

Důležité: Web a ELM jsou oddělené

Servisní Wi-Fi režim je z důvodu stability exkluzivní vůči Bluetooth ELM.

To znamená:

při servisním WEBu je Bluetooth ELM vypnutý,

web nepředstírá živý OBD stream,

před odpojením se uloží snapshot posledních platných hodnot,

grafy vytváří prohlížeč, ne ESP32.

Jak WEB zapnout

Na displeji otevřete:

SYSTEM → WI-FI / WEB

Pokud ELM ukazuje PŘIPOJUJI...

Stiskněte velké červené tlačítko:

STOP

STOP:

zruší další reconnect pokusy,

aktivuje ruční offline režim,

zabrání dalšímu PIN/recovery pokusu,

pokud už právě běží systémový SerialBT.connect(), počká pouze na jeho návrat a spojení následně zahodí.

Počkejte na stav:

ZASTAVENO

Potom se odemkne:

ZAPNOUT WEB

Wi-Fi údaje

Po spuštění webu vznikne Access Point:

SSID:  Luke-Octavia-II
Heslo: Octavia2

Připojte telefon nebo notebook.

Telefon může upozornit:

Tato síť nemá připojení k internetu

To je normální. Zůstaňte připojeni.

Potom do prohlížeče napište:

http://192.168.4.1

Diagnostický health endpoint:

http://192.168.4.1/health

Nouzová minimalistická stránka:

http://192.168.4.1/mini

WEB V2 obsahuje

PŘEHLED

stav systému,

poslední snapshot motoru,

DPF stav,

základní souhrn jízdy.

JÍZDY

poslední jízdy,

trend spotřeby,

CSV export,

tisk / PDF přes prohlížeč.

DPF

saze,

predikci,

historii regenerací,

graf intervalů regenerací.

PALIVO

historii automaticky zaznamenaných tankování,

celkově přidané litry,

CSV data.

NASTAVENÍ

displej,

OBD/data,

DPF,

power management,

tank / odometr,

vlastní texty,

export/import JSON konfigurace.

SYSTÉM

FW verzi,

free heap,

minimum heap,

LittleFS využití,

uptime,

reset reason,

Power Manager stav,

počet Wi-Fi klientů.

Přehled obrazovek

Stránka 1 / 3

PŘEHLED
MOTOR
TURBO
DPF PRO
KNIHA JÍZD
TANKOVÁNÍ

Stránka 2 / 3 – FUNKCE

DIAGNOSTIKA
SENZORY
ELEKTRIKA
SERVIS
ALARMY
SPECIALS

Stránka 3 / 3 – SYSTEM

WI-FI / WEB
POWER
NASTAVENÍ
INFO

PŘEHLED

Hlavní dashboard je určený pro rychlé čtení za jízdy.

Obsahuje:

RPM jako TFT_eWidget MeterWidget,

teplotu motoru,

tlak turba,

venkovní teplotu,

odhad km do regenerace DPF.

Rychlost, nádrž a dojezd zde záměrně nejsou, aby obrazovka nebyla přeplněná.

RPM widget používá optimalizované vykreslování proti blikání a omezenou frekvenci redraw.

MOTOR

Obrazovka MOTOR je více technická.

Obsahuje:

grafický ukazatel zatížení motoru,

grafický ukazatel teploty chladicí kapaliny,

RAIL tlak,

MAF,

RPM,

teplotu nasávaného vzduchu,

MAP.

Barvy hlavních ukazatelů vyjadřují stav hodnoty, například studený / normální / zvýšený / alarmový stav chladiva.

TURBO

Samostatná obrazovka pro plnicí tlak a související data.

TURBO je oddělené od MOTORU, aby každá obrazovka zůstala čitelná a specializovaná.

DPF PRO

DPF PRO má dva režimy.

Normální režim

Zobrazuje:

grafické zanesení DPF,

procento zanesení,

vypočítané saze,

naměřené saze,

predikci do další regenerace,

diferenciální tlak, pokud je dostupný,

60s trend tlaku,

km od poslední regenerace,

EGT PRE / POST, pokud jsou dostupné,

historii regenerací.

REGEN MODE

Při potvrzené aktivní regeneraci se DPF PRO automaticky přepne do speciálního režimu.

Zobrazuje například:

aktivní regeneraci,

aktuální saze,

úbytek sazí od začátku,

dobu regenerace,

ujetou vzdálenost během regenerace,

diferenciální tlak,

dostupné EGT hodnoty.

Automatická detekce regenerace

Začátek ani konec regenerace se nepotvrdí jediným vzorkem. Firmware používá potvrzení více měření, aby omezil falešná upozornění.

Při začátku regenerace vyskočí výrazné upozornění.

Predikce regenerace

Predikce kombinuje:

aktuální vývoj sazí,

historii dokončených regenerací.

Proto se může postupně zpřesňovat.

Kniha jízd

Kniha jízd má tři úrovně:

AKTUÁLNÍ → POSLEDNÍ → DETAIL

Aktuální

Během jízdy sleduje například:

vzdálenost,

dobu,

palivo,

průměrnou spotřebu,

průměrnou rychlost,

maximální rychlost.

Denní souhrn

Zobrazuje souhrn jízd za aktuální den.

Poslední

Obsahuje seznam posledních jízd a trend spotřeby.

Detail

U konkrétní jízdy lze zobrazit podrobnosti.

Jízdu lze smazat přímo na displeji s dvojitým potvrzením.

Krátké jízdy

Jízda je označena jako krátká, pokud splní přibližně:

< 1 km
NEBO
< 3 minuty

Krátká jízda se automaticky nemaže.

Ochrana rozjeté jízdy

Během aktivní jízdy se každých přibližně 30 sekund ukládá checkpoint do LittleFS.

Pokud se ESP32 restartuje například kvůli napájení při startování motoru, firmware se pokusí rozjetou jízdu obnovit.

Detekce skutečného konce jízdy

Pouhý krátký výpadek Bluetooth nemá okamžitě vytvořit novou jízdu.

Firmware používá potvrzení skutečného konce motoru / ECU a přibližně 60s debounce.

Tato minuta se nepřičítá k uložené délce jízdy.

Export

Web nabízí:

CSV,

tiskovou stránku,

PDF přes funkci prohlížeče.

Tankování

Aktuální verze umí automatickou detekci tankování podle změny hladiny paliva.

Výchozí parametry:

kapacita nádrže:          55 l
minimální nárůst:         4 l
počet potvrzovacích vzorků: 3
potvrzovací doba:         cca 25 s

Hodnoty jsou nastavitelné.

Tankování se ukládá do:

/fueling.csv

Na displeji lze zobrazit poslední záznamy a přes web lze data exportovat.

Hladina paliva z ECU/plováku není laboratorní měření. Náklon vozidla a přelévání paliva mohou hodnotu ovlivnit, proto firmware používá potvrzení více vzorků a minimální změnu.

SENZORY

SENZORY používají TFT_eWidget grafy a dvě stránky.

Podle dostupnosti ECU mohou obsahovat například:

teplotu motoru,

teplotu sání,

venkovní teplotu,

RPM,

zatížení,

MAF,

MAP,

rail tlak,

škrticí klapku,

napětí,

EGR,

lambda / equivalence ratio,

BARO,

DPF tlak,

saze.

Nepodporované hodnoty se zobrazí jako:

--

Firmware nevyplňuje chybějící senzory vymyšlenou hodnotou.

Historie senzorů drží přibližně posledních 60 sekund v RAM.

ELEKTRIKA

Zobrazuje stav elektrické soustavy a napětí dostupné přes ELM.

Hodnota ATRV je užitečná pro orientační sledování palubního napětí, ale sama o sobě nepotvrzuje komunikaci s motorovou ECU.

Diagnostika

Diagnostika je primárně read-only.

Umí:

číst DTC,

skenovat nakonfigurované řídicí jednotky přes dostupný ELM/UDS přístup,

zobrazit stav komunikace.

Levný ELM327 ale není VCDS.

Nemusí umět:

všechny starší KWP/TP2.0 jednotky PQ35,

všechny UDS služby,

dlouhé zprávy,

stabilní přepínání mezi moduly.

SERVIS

Servisní obrazovka sleduje servisní počítadla a umožňuje resetovat příslušné údaje s potvrzením.

Data se ukládají do Preferences/NVS a zápisy jsou omezené, aby se flash zbytečně neopotřebovávala.

Alarmy

Dostupné alarmy zahrnují například:

motor nad 105 °C,

nízké napětí,

přepětí,

začátek/konec DPF regenerace,

blížící se regeneraci,

odpojení ELM.

Alarmy používají:

hysterezi,

časové potvrzení.

To znamená, že jeden krátký vadný vzorek nemá okamžitě vyvolat poplach a alarm se nemá zapínat/vypínat při kmitání hodnoty přes jednu hranici.

SPECIALS

SPECIALS obsahuje experimentální funkci PODĚKOVAT přes diagnostickou cestu:

ESP32 → Bluetooth → ELM327 → J519 / 09 Central Electrics

Aktuální test používá J519:

TX: 70E
RX: 778

a experimentální Output Test identifikátory směrovek.

Důležité omezení

Tato funkce je experimentální a v aktuální podobě je záměrně určena pouze pro:

stojící vozidlo,

zapnuté zapalování,

vypnutý motor.

Není určena jako běžné ovládání směrovek za jízdy.

Nastavení

Nastavení je rozděleno do kategorií:

DISPLEJ
OBD / DATA
DPF
POWER
SYSTEM
WEB

DISPLEJ

denní jas,

noční jas,

automatický den/noc režim,

touch feedback,

intenzita touch feedbacku.

OBD / DATA

připojit / odpojit ELM,

automatické připojení,

režim polling:

ECO,

NORMAL,

FAST,

výběr výchozího ELM,

pokročilé systémové informace.

DPF

upozornění regenerace,

upozornění na blížící se regeneraci,

hranice km,

referenční DPF 100 % v gramech.

POWER

automatické stmívání,

DIM timeout,

DISPLAY OFF timeout,

LOW POWER prodlevu.

SYSTEM

datum/čas,

firmware,

volnou RAM,

uptime,

pokročilé informace,

reset nastavení s potvrzením.

Ukládání nastavení

Nastavení se nezapisuje do flash při každém jednotlivém dotyku.

Změny se nejprve drží v RAM a Preferences/NVS se aktualizuje se zpožděním. Tím se omezuje počet zápisů do flash.

Správa napájení

Power Manager používá stavy:

ACTIVE
DIM
DISPLAY OFF
LOW POWER

Výchozí chování zahrnuje přibližně:

DIM:          2 min
DISPLAY OFF:  5 min
LOW POWER:    1 min po zhasnutí

Hodnoty lze měnit v nastavení v povolených mezích.

V LOW POWER režimu se omezuje background polling.

Ignition sense

Ve zdrojáku existuje příprava na fyzický vstup zapalování:

#define USE_IGNITION_SENSE 0
#define IGNITION_PIN 27

Ve výchozí konfiguraci je vypnutý. Bez fyzického ACC/ignition signálu firmware odhaduje stav auta podle komunikace s ECU.

Datové soubory a LittleFS

Projekt používá LittleFS například pro:

/trips_v2.csv
/trips.csv
/trip_checkpoint.bin
/fueling.csv
/dpf.csv

Kniha jízd

trips_v2.csv ukládá strukturovaná data, například:

startEpoch;endEpoch;km;fuelL;avgL100;maxKmh;avgKmh;durationSec

Checkpoint

/trip_checkpoint.bin

chrání aktivní jízdu před ztrátou při restartu.

Doporučené první nastavení po instalaci

Po prvním úspěšném spuštění doporučujeme projít:

NASTAVENÍ → DISPLEJ

nastavte jas pro den a noc.

NASTAVENÍ → OBD / DATA

vyberte správný ELM,

nastavte auto reconnect,

pro začátek ponechte NORMAL nebo FAST.

NASTAVENÍ → DPF

ponechte výchozí limit, pokud nevíte přesně, proč jej měnit,

nastavte varování před regenerací.

WEB → Nastavení

zkontrolujte kapacitu nádrže,

nastavte odometr,

případně změňte startup a header text.

SYSTEM

zkontrolujte čas,

free heap,

LittleFS.

Řešení problémů

1. Arduino IDE hlásí, že TFT_eSPI.h neexistuje

Není nainstalovaná knihovna TFT_eSPI.

Otevřete Library Manager a nainstalujte TFT_eSPI by Bodmer.

2. TFT_eWidget.h: No such file or directory

Nainstalujte:

TFT_eWidget by Bodmer

3. PNGdec.h: No such file or directory

Nainstalujte:

PNGdec by Larry Bank / bitbank2

4. XPT2046_Touchscreen.h chybí

Nainstalujte:

XPT2046_Touchscreen by Paul Stoffregen

5. Displej je bílý, černý nebo rozsypaný

Nejprve zkontrolujte:

zda používáte správnou hardwarovou variantu CYD,

zda je správně nahrazen TFT_eSPI/User_Setup.h,

zda vaše deska opravdu používá ST7789,

zda není aktivní jiný TFT_eSPI setup.

6. Dotyk reaguje špatně nebo obráceně

Aktuální konfigurace používá:

TOUCH_IRQ   36
TOUCH_MOSI  32
TOUCH_MISO  39
TOUCH_CLK   25
TOUCH_CS    33

a kalibrační rozsah:

200 – 3900

Pokud máte jinou revizi CYD, může být nutná úprava kalibrace nebo pinů.

7. Sketch too big

Zkontrolujte:

Partition Scheme: Huge APP (3MB No OTA / 1MB SPIFFS)

8. Chyba .dram0.bss will not fit

Nepoužívejte starou V16.24 před RAM opravou.

Aktuální verze používá optimalizované dynamické RPM buffery a již neobsahuje původní velký statický anti-flicker framebuffer.

Pokud se chyba objeví i v aktuální verzi, pošlete celý linker výpis a informace o zvolené desce/core.

9. Upload se zasekne na Connecting...

Vyzkoušejte:

jiný USB kabel,

jiný USB port,

správný COM port,

Upload Speed 460800 nebo 115200,

případně BOOT tlačítko při začátku uploadu.

10. ELM se nepřipojí

Zkontrolujte:

zda je to Bluetooth Classic ELM, ne BLE-only,

zda je OBD zásuvka napájená,

zda je zapnuté zapalování,

správnou MAC adresu,

zda ELM používá PIN 1234 nebo 0000,

zda ELM není současně připojený k telefonu nebo jiné aplikaci.

U levných klonů někdy pomůže ELM fyzicky vytáhnout z OBD a znovu zasunout.

11. ELM zůstává na PŘIPOJUJI... a nejde zapnout WEB

V aktuální V16.30.1 je pro tento případ tlačítko:

STOP

Otevřete:

SYSTEM → WI-FI / WEB

Stiskněte červené STOP a počkejte na:

ZASTAVENO

Až potom použijte:

ZAPNOUT WEB

12. ELM se připojí, ale ECU je offline

Bluetooth spojení a komunikace s ECU jsou dvě různé věci.

Možné příčiny:

nekvalitní ELM clone,

adaptér nezvládá protokol správně,

vypnuté zapalování,

nepodporované auto,

problém na CAN,

ELM zvládne SPP, ale nezvládne požadované UDS/ISO-TP.

13. DPF ukazuje --

Znamená to, že hodnota není potvrzeně dostupná.

Možné příčiny:

jiná ECU nebo SW verze,

ELM nezvládá UDS / dlouhé zprávy,

nepodporovaný DID,

komunikace právě není čerstvá.

Firmware záměrně nezobrazuje staré nebo vymyšlené DPF údaje jako aktuální.

14. Web se neotevře

Zkontrolujte, že displej ukazuje aktivní WEB.

Připojte se na Luke-Octavia-II.

Heslo je Octavia2.

Ignorujte hlášku, že síť nemá internet.

Otevřete přesně:

http://192.168.4.1

Pokud stále nic:

http://192.168.4.1/health

nebo:

http://192.168.4.1/mini

15. LittleFS nefunguje

Viz část První inicializace LittleFS.

Neformátujte úložiště bez důvodu, pokud už v něm máte historii jízd nebo DPF.

16. Displej se při startování motoru restartuje

To je obvykle problém napájení, ne firmware.

Při startéru může napětí výrazně poklesnout.

Doporučujeme kvalitní automobilový DC/DC zdroj s dostatečnou rezervou, správné jištění a stabilní napájení ESP32.

Kniha jízd má checkpoint, takže se snaží rozjetou jízdu po restartu obnovit.

Sériový monitor

Pro ladění používejte:

115200 baud

Firmware pravidelně vypisuje health informace, například:

heap
minHeap
APclients
BT
ECU
OBD task stack
loop stack

To je velmi užitečné při hledání problémů s RAM nebo komunikací.

Bezpečnost a omezení

Za jízdy věnujte pozornost silnici, ne displeji.

Projekt není homologovaný automobilový přístroj.

Hodnoty z ELM327 nemusí mít stejnou přesnost ani aktualizační frekvenci jako data interně používaná řídicí jednotkou.

Levné ELM327 klony se výrazně liší kvalitou.

Diagnostika je doplňková a nenahrazuje profesionální servisní vybavení.

DPF predikce je odhad, nikoli pevný údaj z ECU.

SPECIALS je experimentální diagnostický Output Test a má být testovaný pouze na stojícím vozidle v bezpečných podmínkách.

Před úpravami elektroinstalace automobilu vždy odpojte napájení a používejte správné jištění.

Nepřipojujte ESP32 přímo na 12V palubní síť bez vhodného DC/DC měniče.

Struktura projektu

Typický obsah aktuální složky:

Luke_Octavia_II_v16_30_FIX1_WEB_STOP_RPM/
│
├── Luke_Octavia_II_v16_30_FIX1_WEB_STOP_RPM.ino
├── MenuIcons.h
├── StartupLogo.h
├── User_Setup.h
│
├── 00_overview.png
├── 01_motor.png
├── 02_turbo.png
├── ...
├── status_bluetooth_20.png
├── status_wifi_20.png
├── status_dpf_21.png
│
├── V16_30_CHANGES.txt
├── V16_30_1_CHANGES.txt
└── README.md

MenuIcons.h

Obsahuje embedded PNG data ikon používaných UI. Ikony se dekódují přes PNGdec.

StartupLogo.h

Obsahuje embedded startup grafiku.

User_Setup.h

Konfigurace TFT_eSPI pro cílový displej.

Vývoj a kompatibilita

Firmware je poměrně rozsáhlý a používá:

dvě ESP32 jádra,

background OBD FreeRTOS task,

mutex pro Bluetooth komunikaci,

LittleFS,

Preferences/NVS,

TFT_eSPI,

TFT_eWidget,

PNGdec,

WebServer,

Bluetooth Classic SPP.

OBD timeouty proto neběží přímo v hlavním UI loopu a neměly by běžně zmrazit dotykové rozhraní.

Ochranná známka

ŠKODA a logo ŠKODA jsou ochranné známky příslušného vlastníka. Tento projekt není oficiálně spojen se společností Škoda Auto a není jí podporován ani schválen.

Poznámka k veřejnému repozitáři

Před zveřejněním projektu na GitHubu doporučujeme přidat samostatný soubor LICENSE s licencí, kterou chcete pro vlastní zdrojový kód použít. Licence třetích stran (Arduino-ESP32, TFT_eSPI, TFT_eWidget, PNGdec, XPT2046_Touchscreen atd.) zůstávají samostatné a řídí se podmínkami jejich autorů.

Rychlý checklist před první jízdou

ESP32 úspěšně nabootuje.

Displej i touch fungují.

LittleFS hlásí OK.

ELM327 je Bluetooth Classic.

ELM je vybraný jako výchozí zařízení.

ECU ukazuje ONLINE.

RPM a teplota motoru dávají smysl.

DPF hodnoty nejsou zjevně nesmyslné.

Nastavili jste správnou kapacitu nádrže.

Nastavili jste jas displeje pro den/noc.

Ověřili jste, že displej za jízdy neodvádí pozornost.

Servisní WEB jste vyzkoušeli v klidu, ne při řízení.

Luke – Octavia II Display
ESP32-2432S028R • CAYC/PCR2.1 • ELM327 Bluetooth Classic • TFT_eSPI • TFT_eWidget • PNGdec
