/*
  Octavia II PC
  ESP32-2432S028R / CYD 2.8" 320x240
  Arduino-ESP32 3.3.x
  V16.31 MULTI-ECU: viceprofilova podpora Octavia II + bezpecny Generic OBD fallback

  Hotove moduly v tomto sketchi:
    - nova bitmapova start animace SKODA / Octavia II PC
    - tri stranky grafickeho menu; po uspesnem ELM connectu se zobrazi PREHLED
    - upozorneni
    - Bluetooth scanner + vyber zarizeni
    - defaultni ELM327
    - motorova obrazovka + Turbo Gauge + elektrika/alternator + DTC
    - viceprofilova vrstva motoru/ECU + automaticka identifikace, pokud ji ECU poskytne
    - overeny VAG DPF monitoring CAYC/PCR2.1 (208B/208C/208D/20D8/207D)
    - 2.0 TDI CR / EDC17 a starsi TDI PD pouzivaji bezpecny standardni OBD profil;
      neoverene proprietarni DPF DIDy se nikdy nevymysleji
    - Kniha jizd + automaticka historie tankovani
    - LittleFS logy
    - Wi-Fi AP + radio-setrne servisni Web UI
    - Power Manager (soft + priprava na ignition input) + servisni pocitadla + alarmy

  KNIHOVNY NAVIC:
    - TFT_eWidget by Bodmer (Arduino Library Manager)
    - PNGdec by Larry Bank / BitBank (Arduino Library Manager)

  DULEZITE:
    Firmware nejprve zkusi rychly VAG CAN profil (ISO 15765-4 CAN 11-bit / 500 kbit/s)
    a pokud motor neodpovi, prepne ELM327 na automaticky vyber OBD protokolu.
    U CAN/UDS jednotek cte pouze identifikacni DIDy F190/F187/F189/F19E a podle
    nich zvoli profil. Proprietarni DPF hodnoty jsou povoleny jen pro profil,
    pro ktery je zname a overene mapovani. V ostatnich profilech zustavaji skryte/NA.
*/

#include <Arduino.h>
#include <SPI.h>

// FS musi byt nacteno PRED TFT_eSPI.
// TFT_eSPI pri SMOOTH_FONT umi definovat FS_NO_GLOBALS a tim by
// WebServer v Arduino-ESP32 3.3.12 nevidel globalni alias FS.
#include <FS.h>
#include <LittleFS.h>

#include <TFT_eSPI.h>
#include <TFT_eWidget.h>
#include <PNGdec.h>
#include "MenuIcons.h"
#include "StartupLogo.h"
#include <XPT2046_Touchscreen.h>
#include <BluetoothSerial.h>


#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sleep.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ========================= NASTAVENI =========================

#define SCREEN_ROTATION 1
#define TFT_BL 21

#define TOUCH_IRQ   36
#define TOUCH_MOSI  32
#define TOUCH_MISO  39
#define TOUCH_CLK   25
#define TOUCH_CS    33

#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3900
#define TOUCH_MIN_Y 200
#define TOUCH_MAX_Y 3900

// Verejna edice: znacka projektu je pevna, osobni udaje uzivatele jsou zamerne prazdne.
static const char *PROJECT_NAME = "Octavia II PC";
static const char *BT_LOCAL_NAME = "Octavia_II_PC";
static const char *DEFAULT_SPLASH_TITLE = "NASTAVTE NAZEV VOZIDLA";
static const char *DEFAULT_HEADER_TITLE = "VOZIDLO NENASTAVENO";

// Zadny cizi ELM327 nesmi byt ve verejnem buildu prednastaven.
// Pri prvnim startu proto firmware automaticky otevre BT scan.
static const char *FACTORY_DEFAULT_NAME = "";
static const char *FACTORY_DEFAULT_MAC  = "";

static const char *BT_PIN_1 = "1234";
static const char *BT_PIN_2 = "0000";

// Verejne, genericke udaje servisniho AP - nejsou vazane na konkretniho uzivatele.
static const char *AP_SSID = "Octavia-II-PC";
static const char *AP_PASS = "OctaviaPC";


#define USE_IGNITION_SENSE 0
#define IGNITION_PIN 27
#define IGNITION_ACTIVE_LEVEL HIGH


// ===================== SPECIALS / ELM -> J519 =====================
// V16.16: zadne GPIO, tranzistory ani optocleny. SPECIALS pouziva vyhradne
// diagnostickou cestu ESP32 -> Bluetooth -> ELM327 -> J519 (09-Central Electrics).
// VCDS labely pro J519 identifikuji test 967 = leve smerovky a 968 = prave.
// Diagnosticke identifikatory odpovidaji 0x03C7 a 0x03C8.
#define SPECIAL_J519_TX            "70E"
#define SPECIAL_J519_RX            "778"
#define SPECIAL_LEFT_DID           "03C7"
#define SPECIAL_RIGHT_DID          "03C8"

// Testovaci sekvence. Prvni build je zamerne uzamceny na stojici vozidlo
// a vypnuty motor, protoze jde o diagnosticky Output Test.
#define SPECIAL_FAST_ON_MS          180UL
#define SPECIAL_FAST_GAP_MS         130UL
#define SPECIAL_HAZARD_ON_MS        320UL
#define SPECIAL_HAZARD_GAP_MS       220UL

#define BT_SCAN_TIME_MS          10000UL
#define OBD_POLL_INTERVAL_MS       140UL
#define UI_REFRESH_MS              100UL
#define ECU_REPROBE_INTERVAL_MS    5000UL
#define BT_LINK_CHECK_INTERVAL_MS  1000UL
#define ALERT_DEFAULT_MS          3500UL
#define TRIP_END_DELAY_MS        60000UL
#define TRIP_CHECKPOINT_MS       30000UL
#define TRIP_SHORT_DISTANCE_KM      1.0f
#define TRIP_SHORT_DURATION_SEC      180UL
#define DISPLAY_DIM_DELAY_MS    120000UL
#define DEEP_SLEEP_DELAY_MS     300000UL
#define DISPLAY_OFF_DELAY_MS     300000UL   // 5 min po vypnuti klicku/ECU
#define POWER_LOW_DELAY_MS        60000UL   // 1 min po zhasnuti prejit do setrneho rezimu
#define RECONNECT_INTERVAL_MS    10000UL

#define ALARM_CONFIRM_COOLANT_MS  2000UL
#define ALARM_CONFIRM_VOLTAGE_MS  3000UL
#define ALARM_CONFIRM_DPF_SOON_MS 2000UL
#define SENSOR_HISTORY_SAMPLES      60

// Jmenovita kapacita nadrze pro prepocet procent na orientacni litry.
// Pokud mas jinou nadrz, zmen pouze tuto hodnotu.
#define DEFAULT_TANK_CAPACITY_L     55.0f
#define DEFAULT_DPF_REGEN_LIMIT_G    24.0f
#define DEFAULT_DPF_BASELINE_G        3.0f

// Automaticka detekce tankovani. Nizsi limit nez ~3-4 l by u plovaku nadrze
// snadno zamenuje naklon auta / prelevani paliva za skutecne tankovani.
#define DEFAULT_FUELING_MIN_RISE_L      4.0f
#define DEFAULT_RANGE_CONSUMPTION_L100   5.5f
#define FUELING_CONFIRM_SAMPLES            3
#define FUELING_CONFIRM_MS           25000UL
#define FUEL_BASELINE_PERSIST_MS    120000UL
#define ODOMETER_PERSIST_STEP_KM         1.0f

// 0 = pri chybe mountu NIKDY automaticky nemazat LittleFS. Pro uplne novy/
// neformatovany kus lze docasne prepnout na 1, jednou nahrat a pak vratit na 0.
#define ALLOW_FS_FORMAT_IF_MOUNT_FAIL 0

// ELM327 protokol 6 = ISO 15765-4 CAN 11-bit / 500 kbit/s. Pouziva jej rada
// Octavia II CR a umoznuje i fyzickou VAG UDS adresaci motoru 7E0/7E8.
#define VAG_CAN_11_500_PROTOCOL    6
#define ENGINE_TX_HEADER       "7E0"
#define ENGINE_RX_HEADER       "7E8"

// =========================== BARVY ============================

#define COL_BG        0x0841
#define COL_PANEL     0x10A2
#define COL_PANEL2    0x18E3
#define COL_BORDER    0x4A49   // svetlejsi ramecky - lepsi orientace za jizdy
#define COL_SELECT    0x1165
#define COL_TEXT      TFT_WHITE
#define COL_MUTED     0xC618   // svetle seda misto tmave sede
#define COL_LABEL     TFT_WHITE // ciste bila pro vsechny staticke popisky
#define COL_CYAN      0x07FF   // maximalne jasna cyan
#define COL_BLUE      0x5DFF   // svetlejsi modra, nezanika na tmavem pozadi
#define COL_GREEN     0x07E0
#define COL_YELLOW    0xFFE0
#define COL_ORANGE    0xFD20
#define COL_RED       0xF800

// =========================== OBJEKTY ==========================

TFT_eSPI tft = TFT_eSPI();

// PŘEHLED: MeterWidget se kreslí do interního sprite 239x126 a následně
// se zmenší na displej. Díky tomu zůstává skutečný TFT_eWidget RPM budík,
// ale nezabere téměř celou 320x240 obrazovku.
TFT_eSprite liveRpmMeterSprite = TFT_eSprite(&tft);
MeterWidget liveRpmMeter = MeterWidget(&liveRpmMeterSprite);
// V16.24 FIX1: zmenseny RPM frame je 8-bit sprite alokovany az za behu.
// Nezabira tedy desitky kB staticke .dram0.bss.
TFT_eSprite *liveRpmOutputSprite = nullptr;

// TFT_eWidget prvky pro datove obrazovky. GraphWidget/TraceWidget
// vykresluji live trendy bez prekreslovani cele obrazovky.
GraphWidget liveBoostGraph = GraphWidget(&tft);
TraceWidget liveBoostTrace = TraceWidget(&liveBoostGraph);
GraphWidget sensorsTrendGraph = GraphWidget(&tft);
TraceWidget sensorsTraceA = TraceWidget(&sensorsTrendGraph);
TraceWidget sensorsTraceB = TraceWidget(&sensorsTrendGraph);
TraceWidget sensorsTraceC = TraceWidget(&sensorsTrendGraph);

// Jediny sdileny PNGdec dekoder pro menu i stavove ikony.
// Je staticky, aby interni PNG buffer nelezel na stacku.
PNG uiPng;

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
BluetoothSerial SerialBT;
Preferences prefs;
WebServer server(80);

// OBD komunikace bezi v samostatnem FreeRTOS tasku, aby nikdy neblokovala dotyk/UI.
SemaphoreHandle_t btMutex = nullptr;
SemaphoreHandle_t diagMutex = nullptr;
TaskHandle_t obdTaskHandle = nullptr;

// ========================= DATOVE TYPY ========================

enum ScreenId {
  SCREEN_MENU,
  SCREEN_MENU_MORE,
  SCREEN_MENU_SYSTEM,
  SCREEN_SPECIALS,
  SCREEN_SENSORS,
  SCREEN_DPF,
  SCREEN_DPF_HISTORY,
  SCREEN_ENGINE,
  SCREEN_TURBO,
  SCREEN_ELECTRICAL,
  SCREEN_DIAGNOSTICS,
  SCREEN_TRIPS,
  SCREEN_FUELING,
  SCREEN_SERVICE,
  SCREEN_POWER,
  SCREEN_ALARMS,
  SCREEN_LIVE,
  SCREEN_SETTINGS,
  SCREEN_SETTINGS_MORE,      // DISPLAY
  SCREEN_SETTINGS_TEXT,      // OBD / DATA
  SCREEN_SETTINGS_DPF,
  SCREEN_SETTINGS_SYSTEM,
  SCREEN_SETTINGS_ADVANCED,
  SCREEN_INFO,
  SCREEN_WIFI,
  SCREEN_BT_SCAN,
  SCREEN_BT_RESULTS,
  SCREEN_CONNECTING,
  SCREEN_CONNECT_FAIL,
  SCREEN_ASK_DEFAULT
};

enum PowerMode : uint8_t {
  POWER_ACTIVE,
  POWER_DIMMED,
  POWER_DISPLAY_OFF,
  POWER_LOW
};

// ======================= PROFILY VOZIDLA / ECU =======================
// Hodnota ulozena uzivatelem. AUTO je doporucena volba; pokud identifikace
// konkretni ECU neni mozna, firmware bezpecne spadne do GENERIC OBD.
enum EngineProfile : uint8_t {
  ENGINE_PROFILE_AUTO = 0,
  ENGINE_PROFILE_CAYC_PCR21 = 1,
  ENGINE_PROFILE_20TDI_CR_EDC17 = 2,
  ENGINE_PROFILE_TDI_PD_EDC16 = 3,
  ENGINE_PROFILE_GENERIC_OBD = 4
};

struct VehicleCapabilities {
  bool standardObd = true;
  bool vagUds = false;
  bool vagModuleScan = false;
  bool advancedDpf = false;       // saze / km / palivo / regen stav z overenych VAG DIDu
  bool standardDpfPressure = true; // SAE PID 7A se muze zkusit, pokud jej ECU podporuje
  bool j519Specials = false;
};

static const char *engineProfileLabel(EngineProfile p) {
  switch (p) {
    case ENGINE_PROFILE_AUTO: return "AUTO";
    case ENGINE_PROFILE_CAYC_PCR21: return "1.6 TDI / PCR2.1";
    case ENGINE_PROFILE_20TDI_CR_EDC17: return "2.0 TDI CR / EDC17";
    case ENGINE_PROFILE_TDI_PD_EDC16: return "TDI PD / EDC16";
    default: return "GENERIC OBD";
  }
}

static const char *engineProfileShortLabel(EngineProfile p) {
  switch (p) {
    case ENGINE_PROFILE_AUTO: return "AUTO";
    case ENGINE_PROFILE_CAYC_PCR21: return "1.6 PCR2.1";
    case ENGINE_PROFILE_20TDI_CR_EDC17: return "2.0 CR";
    case ENGINE_PROFILE_TDI_PD_EDC16: return "TDI PD";
    default: return "GENERIC";
  }
}


// SPECIALS je vykonavano v OBD tasku, aby se ELM nikdy neprekryl s beznym pollingem.
static volatile bool specialsRunning = false;
static volatile bool specialsRequestPending = false;
static volatile bool specialsNeedsRedraw = false;
static String specialsStatus = "PRIPRAVENO";
static String specialsLastElmReply = "";
static uint8_t specialsIoValueMode = 0; // 0=neznamy, 1=FF, 2=64, 3=01
static uint8_t sensorPage = 0;

struct VehicleData {
  bool elmConnected = false;
  bool ecuResponding = false;
  bool udsResponding = false;
  String protocol = "";
  String vin = "";
  String ecuPartNumber = "";
  String ecuSoftware = "";
  String odxId = "";
  float speedKmh = 0;
  float rpm = 0;
  float coolantC = NAN;
  float voltageV = NAN;          // ELM ATRV, pouze interni diagnostika
  float moduleVoltageV = NAN;    // SAE PID 0142 - napeti 12V site merené ECU
  bool moduleVoltageAvailable = false;
  float engineLoadPct = NAN;
  float mafGps = NAN;
  float mapKpa = NAN;
  float intakeTempC = NAN;
  bool intakeAvailable = false;
  float ambientTempC = NAN;      // SAE PID 0146 - venkovni teplota, pokud ECU poskytuje
  bool ambientAvailable = false;
  float baroKpa = NAN;
  bool baroAvailable = false;
  float railPressureKpa = NAN;
  bool railAvailable = false;
  float fuelRateLph = NAN;
  bool fuelRateAvailable = false;
  float throttlePct = NAN;
  bool throttleAvailable = false;
  float egrCommandPct = NAN;
  bool egrCommandAvailable = false;
  float egrErrorPct = NAN;
  bool egrErrorAvailable = false;
  float lambdaCommanded = NAN;
  bool lambdaCommandedAvailable = false;
  float lambdaB1S1 = NAN;
  float o2VoltageB1S1 = NAN;
  bool lambdaB1S1Available = false;
  float fuelLevelPct = NAN;
  bool fuelLevelAvailable = false;
  bool fuelLevelAutomatic = false;
  unsigned long fuelLevelUpdateMs = 0;
  unsigned long lastGoodObdMs = 0;
};

struct DpfData {
  bool available = false;
  bool regenActive = false;
  float sootCalculatedG = NAN;
  float sootMeasuredG = NAN;
  float diffPressureHpa = NAN;   // zatim nemapovano pro tuto ECU
  float egtPreC = NAN;           // zatim nemapovano pro tuto ECU
  float egtPostC = NAN;          // zatim nemapovano pro tuto ECU
  float ashG = NAN;              // 22406C zatim bez potvrzeneho scalingu
  float kmSinceRegen = NAN;
  float minutesSinceRegen = NAN; // 222089 zatim bez potvrzeneho scalingu
  float fuelSinceRegenL = NAN;
  bool diffPressureAvailable = false;
  uint32_t regenRaw = 0;
  bool regenStatusAvailable = false;
  unsigned long regenStatusUpdateMs = 0;
  unsigned long lastUpdateMs = 0;
};

// Posledni platny snapshot pro servisni WEB. Wi-Fi servis je zamerne
// exkluzivni vuci Bluetooth ELM, proto si pred odpojenim uchovame jen
// nekolik cisel. Web pak nezatezuje OBD a nic "live" nepredstira.
struct WebVehicleSnapshot {
  bool valid = false;
  bool regenActive = false;
  time_t epoch = 0;
  float rpm = NAN;
  float coolantC = NAN;
  float boostBar = NAN;
  float moduleVoltageV = NAN;
  float ambientTempC = NAN;
  float dpfPct = NAN;
  float dpfCalcG = NAN;
  float dpfMeasuredG = NAN;
  float dpfDiffHpa = NAN;
  float dpfKmSince = NAN;
  float dpfKmLeft = NAN;
  float dpfHistoryAvgKm = NAN;
};

struct TripData {
  bool active = false;
  bool recovered = false;
  unsigned long startMs = 0;
  unsigned long lastUpdateMs = 0;
  unsigned long engineStoppedSince = 0;
  unsigned long elapsedBeforeBootSec = 0;
  unsigned long stopElapsedSec = 0;
  unsigned long lastCheckpointMs = 0;
  time_t startEpoch = 0;
  float distanceKm = 0;
  float fuelLiters = 0;
  bool fuelAvailable = false;
  float lastFuelCounterL = NAN;
  bool fuelCounterValid = false;
  float maxSpeedKmh = 0;
};


struct TripRecord {
  int id = -1;
  time_t startEpoch = 0;
  time_t endEpoch = 0;
  float distanceKm = 0;
  float fuelLiters = NAN;
  float avgConsumption = NAN;
  float maxSpeedKmh = 0;
  float avgSpeedKmh = 0;
  unsigned long durationSec = 0;
};

enum TripView : uint8_t {
  TRIP_VIEW_CURRENT = 0,
  TRIP_VIEW_RECENT,
  TRIP_VIEW_DETAIL
};

struct TripDailySummary {
  uint16_t count = 0;
  float distanceKm = 0;
  float fuelLiters = 0;
  bool fuelAvailable = false;
  unsigned long durationSec = 0;
};

struct TripStats {
  uint32_t count = 0;
  float totalKm = 0;
  float longestKm = 0;
  float bestConsumption = NAN;
};

struct TripCheckpointDisk {
  uint32_t magic = 0x4C543236UL; // "LT26"
  uint32_t startEpoch = 0;
  uint32_t elapsedSec = 0;
  float distanceKm = 0;
  float fuelLiters = 0;
  float maxSpeedKmh = 0;
  float lastFuelCounterL = 0;
  uint8_t fuelAvailable = 0;
  uint8_t fuelCounterValid = 0;
};

struct SensorHistorySample {
  int16_t coolant10 = INT16_MIN;
  int16_t intake10 = INT16_MIN;
  int16_t ambient10 = INT16_MIN;
  int16_t lambdaCmd1000 = INT16_MIN;
  int16_t lambdaAct1000 = INT16_MIN;
  int16_t dpfPressure10 = INT16_MIN;
};

struct FuelingRecord {
  int id = -1;
  time_t epoch = 0;
  float odometerKm = NAN;
  float beforeLiters = NAN;
  float afterLiters = NAN;
  float addedLiters = NAN;
};

struct DeviceInfo {
  String name;
  String mac;
  int rssi;
};

struct AlertState {
  bool active = false;
  String title;
  String line1;
  String line2;
  uint16_t color = COL_YELLOW;
  unsigned long untilMs = 0;
};

struct ServiceCounters {
  float oilKm = 0;
  float inspectionKm = 0;
  time_t oilEpoch = 0;
  time_t inspectionEpoch = 0;
  float oilIntervalKm = 15000.0f;
  float inspectionIntervalKm = 30000.0f;
  uint16_t oilIntervalDays = 365;
  uint16_t inspectionIntervalDays = 730;
};

struct DtcState {
  String codes[8];
  int count = 0;
  bool scanned = false;
  bool busy = false;
  unsigned long lastScanMs = 0;
};

struct DiagModuleSpec {
  const char *vagAddress;
  const char *name;
  const char *txId;
  const char *rxId;
};

struct DiagModuleState {
  bool scanned = false;
  bool online = false;
  String codes[16];
  int count = 0;
  String note = "";
};

// Bezne VAG UDS fyzicke adresy. Na PQ35 se mohou jednotlive moduly/protokoly lisit;
// scan je proto read-only a vysledek "bez odpovedi" neznamena zavadu jednotky.
static const DiagModuleSpec DIAG_MODULES[] = {
  {"01", "MOTOR",              "7E0", "7E8"},
  {"02", "PREVODOVKA",         "7E1", "7E9"},
  {"03", "ABS / ESP",           "713", "77D"},
  {"08", "KLIMATIZACE",         "746", "7B0"},
  {"09", "CENTR. ELEKTRIKA",    "70E", "778"},
  {"10", "PARKOVACI ASISTENT",  "70A", "774"},
  {"15", "AIRBAG",              "715", "77F"},
  {"16", "VOLANT",              "70C", "776"},
  {"17", "PRISTROJE",           "714", "77E"},
  {"19", "CAN GATEWAY",         "710", "77A"},
  {"25", "IMOBILIZER",          "711", "77B"},
  {"42", "DVERE RIDICE",        "74A", "7B4"},
  {"44", "POSILOVAC RIZENI",    "712", "77C"},
  {"46", "KOMFORT",             "70D", "777"},
  {"52", "DVERE SPOLUJEZDCE",   "74B", "7B5"},
  {"55", "NAKLAPENI SVETEL",    "754", "7BE"},
  {"56", "RADIO",               "718", "782"},
  {"62", "DVERE ZADNI LEVE",    "73E", "7A8"},
  {"69", "TAZNE ZARIZENI",      "747", "7B1"},
  {"72", "DVERE ZADNI PRAVE",   "73F", "7A9"},
  {"77", "TELEFON",             "76B", "7D5"}
};
static const int DIAG_MODULE_COUNT = sizeof(DIAG_MODULES) / sizeof(DIAG_MODULES[0]);

// =========================== GLOBAL ===========================

VehicleData car;
DpfData dpf;
WebVehicleSnapshot webVehicleSnapshot;
TripData trip;
AlertState alert;
ServiceCounters service;
DtcState dtc;
DiagModuleState diagModules[DIAG_MODULE_COUNT];

ScreenId currentScreen = SCREEN_MENU;
ScreenId dpfHistoryParent = SCREEN_MENU_MORE;

#define MAX_DEVICES 12
#define DEVICES_PER_PAGE 3
DeviceInfo devices[MAX_DEVICES];
int deviceCount = 0;
int selectedDevice = -1;
int btPage = 0;

volatile int foundDuringScan = 0;
bool btScanning = false;
unsigned long btScanStart = 0;
unsigned long lastRadarFrame = 0;
int radarAngle = 0;

bool hasDefaultDevice = false;
String defaultName;
String defaultMac;
String pendingDeviceName;
String pendingDeviceMac;
unsigned long lastReconnectAttempt = 0;
bool connectUserChoseOther = false;
bool connectUserChoseOffline = false;
// Skutecny OFFLINE rezim: po zvoleni OFFLINE se firmware nesmi na pozadi
// znovu pokouset o SerialBT.connect(), protoze ten umi na levnem ELM blokovat UI.
bool offlineMode = false;

bool wifiStaConnected = false;
String wifiSSID;
String wifiPassword;

EngineProfile configuredEngineProfile = ENGINE_PROFILE_AUTO;
EngineProfile activeEngineProfile = ENGINE_PROFILE_GENERIC_OBD;
VehicleCapabilities vehicleCaps;
uint8_t detectedElmProtocol = 0;
bool enginePhysicalCanAddressing = false;
bool engineProfileDetected = false;
String engineDetectionNote = "CEKAM NA ECU";

float tankCapacityL = DEFAULT_TANK_CAPACITY_L;
float rangeFallbackL100 = DEFAULT_RANGE_CONSUMPTION_L100;
bool manualFuelConfigured = false;
float manualFuelPct = NAN;
bool storedFuelValid = false;
float storedFuelPct = NAN;
float lastStoredFuelPersistPct = NAN;
unsigned long lastStoredFuelPersistMs = 0;
float dpfRegenLimitG = DEFAULT_DPF_REGEN_LIMIT_G;
bool fastDataMode = true;
String developerName = "";
String developerEmail = "";
String developerPhone = "";
String developerWeb = "";
String splashTitle = DEFAULT_SPLASH_TITLE;
String mainHeaderTitle = DEFAULT_HEADER_TITLE;
static const char *FW_VERSION = "16.31.0 MULTI-ECU";

// Hodiny z pristrojoveho stitu (J285 / adresa 17). Cteni je read-only.
// U PQ35/A5 je fyzicka UDS adresa pristroju typicky TX 0x714 / RX 0x77E.
volatile bool vehicleClockAvailable = false;
volatile uint8_t vehicleClockHour = 0;
volatile uint8_t vehicleClockMinute = 0;
volatile bool vehicleClockSyncRequested = false;
volatile bool vehicleClockSyncBusy = false;
volatile unsigned long vehicleClockLastOkMs = 0;
unsigned long vehicleClockLastAttemptMs = 0;
static const unsigned long VEHICLE_CLOCK_SYNC_PERIOD_MS = 300000UL;

volatile bool diagScanAllRequested = false;
volatile int diagSingleRequested = -1;
volatile bool diagNeedsRedraw = false;
int diagPage = 0;
int diagDetailIndex = -1;

bool previousTouch = false;
uint8_t displayBrightness = 220;
bool displayDimmed = false;
bool displayOffAfterIgnition = false;
unsigned long ignitionOffSinceMs = 0;
unsigned long lastUserActivityMs = 0;
unsigned long lastUiRefresh = 0;

bool warnedCoolant = false;
bool warnedVoltage = false;
bool warnedOverVoltage = false;
bool warnedDisconnect = false;

// Uzivatelsky volitelne alarmy (ulozene v Preferences).
bool alarmCoolantEnabled = true;
bool alarmLowVoltageEnabled = true;
bool alarmOverVoltageEnabled = true;
bool alarmRegenEnabled = true;
bool alarmRegenSoonEnabled = true;
bool alarmElmDisconnectEnabled = true;
bool warnedRegenSoon = false;
unsigned long coolantAlarmSinceMs = 0;
unsigned long lowVoltageAlarmSinceMs = 0;
unsigned long overVoltageAlarmSinceMs = 0;
unsigned long regenSoonAlarmSinceMs = 0;

SensorHistorySample sensorHistory[SENSOR_HISTORY_SAMPLES];
uint8_t sensorHistoryHead = 0;
uint8_t sensorHistoryCount = 0;
unsigned long sensorHistoryLastSampleMs = 0;
uint32_t telemetryRejectedValues = 0;

float dpfHistoricalIntervalKm = NAN;
uint8_t dpfHistoricalIntervalCount = 0;
unsigned long dpfHistoryCacheMs = 0;

// Historie tankovani + orientacni celkovy stav km. Skutecny odometr neni
// standardni OBD-II PID; uzivatel proto jednou zada stav tachometru pres WEB
// a firmware jej dal pricita ze stejne integrovane vzdalenosti jako Kniha jizd.
float fuelingMinRiseL = DEFAULT_FUELING_MIN_RISE_L;
bool trackedOdometerConfigured = false;
float trackedOdometerKm = NAN;
float lastOdometerPersistKm = NAN;

bool fuelBaselineValid = false;
float fuelBaselinePct = NAN;
unsigned long lastFuelBaselinePersistMs = 0;
unsigned long lastFuelLevelHandledMs = 0;
bool fuelCandidateActive = false;
float fuelCandidateBeforePct = NAN;
float fuelCandidateAfterPct = NAN;
unsigned long fuelCandidateSinceMs = 0;
uint8_t fuelCandidateSamples = 0;
bool fuelCandidateSawStopped = false;
bool tripsDynamicCacheValid = false;
TripView tripView = TRIP_VIEW_CURRENT;
int tripSelectedId = -1;
uint8_t tripRecentPage = 0;
bool tripDeleteArmed = false;
unsigned long tripDeleteArmUntil = 0;

static const float ALARM_COOLANT_C = 105.0f;
// Prah DPF upozorneni je od V16.29 uzivatelsky nastavitelny pres SETTINGS V2.
bool previousRegenActive = false;
bool regenSessionRunning = false;
unsigned long regenStartMs = 0;
unsigned long regenLastIntegrateMs = 0;
float regenDistanceKm = 0;
float regenSootBefore = NAN;
float regenIntervalKm = NAN;

bool powerAutoDim = true;
unsigned long powerDimDelayMs = DISPLAY_DIM_DELAY_MS;
unsigned long powerDisplayOffDelayMs = DISPLAY_OFF_DELAY_MS;
unsigned long powerLowDelayMs = POWER_LOW_DELAY_MS;

// V16.29 SETTINGS V2
uint8_t displayNightBrightness = 70;
bool autoDayNightBrightness = true;
bool touchFeedbackEnabled = true;
uint8_t touchFeedbackStrength = 28;
bool autoElmReconnect = true;
uint8_t obdDataMode = 2; // 0=ECO, 1=NORMAL, 2=FAST
float alarmRegenSoonKm = 10.0f;
bool settingsSubpageContext = false;
bool settingsDirty = false;
unsigned long settingsDirtySinceMs = 0;
bool settingsResetArmed = false;
unsigned long settingsResetArmUntil = 0;

PowerMode powerMode = POWER_ACTIVE;
volatile bool powerLowMode = false;
unsigned long displayOffSinceMs = 0;
bool serviceResetArmed = false;
uint8_t serviceResetTarget = 0;
unsigned long serviceResetArmUntil = 0;
float lastServicePersistKm = 0;
unsigned long lastPressureAttemptMs = 0;
unsigned long lastServicePersistMs = 0;
float lastServicePersistInspectionKm = 0.0f;
uint8_t btRecoveryFailures = 0;

bool storageReady = false;
volatile bool reconnectRequested = false;
volatile bool reconnectInProgress = false;
volatile bool reconnectFinished = false;
volatile bool reconnectResult = false;
// V16.30.2: STOP na WI-FI / WEB musi byt okamzity i kdyz prave uvnitr
// BluetoothSerial bezi blokujici connect(). Flag zabrani jakemukoli dalsimu
// PIN pokusu / BT recovery a po navratu z aktualniho connectu session zahodi.
volatile bool reconnectAbortRequested = false;
volatile bool wifiConnectRequested = false;
unsigned long wifiConnectRequestedMs = 0;

// V15.7: Servisni Wi-Fi je ZAMERNE oddelena od Bluetooth/ELM provozu.
// Pri zapnuti webu se Bluetooth Classic uplne ukonci a OBD task se pozastavi.
// Tim uvolnime desitky kB RAM a hlavne odstranime soubeh Wi-Fi AP + BT Classic,
// ktery na puvodnim ESP32 zpusoboval zamrzani a propady heapu pod 4 kB.
volatile bool serviceWifiActive = false;
volatile bool webClientActive = false;
uint8_t apClientCount = 0;
bool webRoutesConfigured = false;
bool ntpConfigured = false;
unsigned long lastHealthLogMs = 0;
unsigned long serviceWifiNoClientSince = 0;
static const unsigned long SERVICE_WIFI_IDLE_OFF_MS = 10UL * 60UL * 1000UL;

// V16.30.2: po ukonceni servisniho WEBu nestartujeme Bluetooth Classic
// okamzite uvnitr touch handleru. Prechod Wi-Fi AP -> BT Classic dostane
// cas na korektni uvolneni radioveho/controller stacku a BT se obnovi pozdeji.
volatile bool btResumeAfterWebPending = false;
unsigned long btResumeAfterWebAt = 0;
uint8_t btResumeAfterWebAttempts = 0;

uint8_t obdPollIndex = 0;
uint8_t fastPollIndex = 0;
volatile bool diagnosticExclusive = false;
unsigned long lastObdPoll = 0;

// Forward declarations pro funkce pouzite napric UI/BT moduly.
bool restartBluetoothMaster();
bool backgroundReconnectDefaultDevice();
String elmCommand(const String &cmd, uint32_t timeoutMs = 1200, bool debug = false);
static float clampFloat(float v, float lo, float hi);
void drawMainMenu();
void drawSpecialsScreen();
void drawSensorsScreen();
void updateSensorsDynamic();
void updateSpecialsSequence();
void cancelSpecialsSequence();
void invalidateLiveTelemetry(bool dropUds = true);
bool startServiceWifi();
void manualDisconnectElm();
void stopElmReconnectAttempts();
void requestElmReconnect();
void stopServiceWifi(bool restartBt = true);
void serviceDeferredBluetoothResume();
bool syncVehicleClockFromCluster();
void drawSettingsTextScreen();
void drawFuelingScreen();
void updateFuelingDetection();
void persistTrackedOdometer(bool force);
void persistFuelBaseline(bool force);
void drawSettingRow(int y, const String &label, const String &value, uint16_t accent);
void drawSettingsDpfScreen();
void drawSettingsSystemScreen();
void drawSettingsAdvancedScreen();
void applyEngineProfile(EngineProfile p, const String &reason = "");
void resolveEngineProfileAfterIdentification();
bool vehicleHasAdvancedDpf();

const char *resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

// =========================== HELPERS ==========================

// Bezpecne snapshoty diagnostiky mezi OBD taskem a UI/Web taskem.
DiagModuleState copyDiagState(int idx) {
  DiagModuleState out;
  if (idx < 0 || idx >= DIAG_MODULE_COUNT) return out;
  if (diagMutex && xSemaphoreTake(diagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    out = diagModules[idx];
    xSemaphoreGive(diagMutex);
  } else if (!diagMutex) {
    out = diagModules[idx];
  }
  return out;
}

DtcState copyDtcState() {
  DtcState out;
  if (diagMutex && xSemaphoreTake(diagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    out = dtc;
    xSemaphoreGive(diagMutex);
  } else if (!diagMutex) {
    out = dtc;
  }
  return out;
}

String formatFloat(float v, uint8_t decimals = 1, const String &fallback = "--") {
  if (isnan(v)) return fallback;
  return String(v, static_cast<unsigned int>(decimals));
}

String twoDigits(int v) {
  return v < 10 ? "0" + String(v) : String(v);
}

String formatDuration(unsigned long sec) {
  return twoDigits(sec / 3600UL) + ":" +
         twoDigits((sec % 3600UL) / 60UL) + ":" +
         twoDigits(sec % 60UL);
}

bool clockIsValid() {
  return time(nullptr) > 1700000000;
}

String vehicleClockText() {
  if (!vehicleClockAvailable) return "--:--";
  uint32_t base = (uint32_t)vehicleClockHour * 60UL + (uint32_t)vehicleClockMinute;
  uint32_t elapsedMin = vehicleClockLastOkMs ? ((millis() - vehicleClockLastOkMs) / 60000UL) : 0;
  uint32_t total = (base + elapsedMin) % (24UL * 60UL);
  return twoDigits((int)(total / 60UL)) + ":" + twoDigits((int)(total % 60UL));
}

String sanitizeUiText(String value, const String &fallback, size_t maxLen) {
  value.replace("\r", " ");
  value.replace("\n", " ");
  value.trim();
  String out;
  out.reserve(min(maxLen, value.length()));
  for (size_t i = 0; i < value.length() && out.length() < maxLen; i++) {
    uint8_t c = (uint8_t)value[i];
    if (c >= 32) out += (char)c;
  }
  out.trim();
  return out.length() ? out : fallback;
}

String formatDateTime(time_t value) {
  if (value <= 0) return "--";
  struct tm ti;
  localtime_r(&value, &ti);
  char buf[32];
  strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", &ti);
  return String(buf);
}


String csvField(const String &line, int index, char sep = ';') {
  int start = 0;
  for (int i = 0; i < index; i++) {
    int p = line.indexOf(sep, start);
    if (p < 0) return "";
    start = p + 1;
  }
  int end = line.indexOf(sep, start);
  if (end < 0) end = line.length();
  return line.substring(start, end);
}

bool parseTripRecord(const String &line, int id, TripRecord &r) {
  if (!line.length() || line.indexOf(';') < 0) return false;
  r.id = id;
  r.startEpoch = (time_t)strtoul(csvField(line, 0).c_str(), nullptr, 10);
  r.endEpoch = (time_t)strtoul(csvField(line, 1).c_str(), nullptr, 10);
  r.distanceKm = csvField(line, 2).toFloat();
  String fuel = csvField(line, 3);
  r.fuelLiters = fuel.length() ? fuel.toFloat() : NAN;
  String cons = csvField(line, 4);
  r.avgConsumption = cons.length() ? cons.toFloat() : NAN;
  r.maxSpeedKmh = csvField(line, 5).toFloat();
  r.avgSpeedKmh = csvField(line, 6).toFloat();
  r.durationSec = (unsigned long)strtoul(csvField(line, 7).c_str(), nullptr, 10);
  return true;
}

int loadRecentTrips(TripRecord *out, int maxCount) {
  if (!storageReady) return 0;
  fs::File f = LittleFS.open("/trips_v2.csv", "r");
  if (!f) return 0;
  int count = 0, id = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    TripRecord r;
    if (!parseTripRecord(line, id++, r)) continue;
    if (count < maxCount) out[count++] = r;
    else {
      for (int i = 0; i < maxCount - 1; i++) out[i] = out[i + 1];
      out[maxCount - 1] = r;
    }
  }
  f.close();
  return count;
}

bool loadTripById(int wantedId, TripRecord &out) {
  if (!storageReady) return false;
  fs::File f = LittleFS.open("/trips_v2.csv", "r");
  if (!f) return false;
  int id = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    TripRecord r;
    if (!parseTripRecord(line, id, r)) { id++; continue; }
    if (id == wantedId) { out = r; f.close(); return true; }
    id++;
  }
  f.close();
  return false;
}



unsigned long currentTripElapsedSec() {
  if (!trip.active) return 0;
  return trip.elapsedBeforeBootSec + (millis() - trip.startMs) / 1000UL;
}

bool tripRecordIsShort(const TripRecord &r) {
  return r.distanceKm < TRIP_SHORT_DISTANCE_KM || r.durationSec < TRIP_SHORT_DURATION_SEC;
}

bool sameLocalDay(time_t a, time_t b) {
  if (a <= 0 || b <= 0) return false;
  struct tm ta, tb;
  localtime_r(&a, &ta);
  localtime_r(&b, &tb);
  return ta.tm_year == tb.tm_year && ta.tm_yday == tb.tm_yday;
}

String tripAutoName(time_t startEpoch, int id = -1) {
  if (startEpoch <= 0) return id >= 0 ? String("Jizda #") + id : String("Aktualni jizda");
  struct tm ti;
  localtime_r(&startEpoch, &ti);
  char buf[24];
  if (clockIsValid() && sameLocalDay(startEpoch, time(nullptr)))
    strftime(buf, sizeof(buf), "Dnes %H:%M", &ti);
  else
    strftime(buf, sizeof(buf), "%d.%m. %H:%M", &ti);
  return String(buf);
}

int countTripRecords() {
  if (!storageReady) return 0;
  fs::File f = LittleFS.open("/trips_v2.csv", "r");
  if (!f) return 0;
  int count = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() && line.indexOf(';') >= 0) count++;
  }
  f.close();
  return count;
}

TripDailySummary getTodayTripSummary() {
  TripDailySummary out;
  if (!clockIsValid()) return out;
  time_t now = time(nullptr);
  if (storageReady) {
    fs::File f = LittleFS.open("/trips_v2.csv", "r");
    if (f) {
      int id = 0;
      while (f.available()) {
        String line = f.readStringUntil('\n'); line.trim();
        TripRecord r;
        if (!parseTripRecord(line, id++, r) || !sameLocalDay(r.startEpoch, now)) continue;
        out.count++;
        out.distanceKm += r.distanceKm;
        out.durationSec += r.durationSec;
        if (!isnan(r.fuelLiters)) { out.fuelLiters += r.fuelLiters; out.fuelAvailable = true; }
      }
      f.close();
    }
  }
  if (trip.active && sameLocalDay(trip.startEpoch, now)) {
    out.count++;
    out.distanceKm += trip.distanceKm;
    out.durationSec += currentTripElapsedSec();
    if (trip.fuelAvailable) { out.fuelLiters += trip.fuelLiters; out.fuelAvailable = true; }
  }
  return out;
}

TripStats getTripStats() {
  TripStats st;
  if (!storageReady) return st;
  fs::File f = LittleFS.open("/trips_v2.csv", "r");
  if (!f) return st;
  int id = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    TripRecord r;
    if (!parseTripRecord(line, id++, r)) continue;
    st.count++;
    st.totalKm += r.distanceKm;
    if (r.distanceKm > st.longestKm) st.longestKm = r.distanceKm;
    if (!tripRecordIsShort(r) && !isnan(r.avgConsumption) && r.avgConsumption >= 2.0f && r.avgConsumption <= 20.0f &&
        (isnan(st.bestConsumption) || r.avgConsumption < st.bestConsumption)) st.bestConsumption = r.avgConsumption;
  }
  f.close();
  return st;
}

void rebuildLegacyTripsCsv() {
  if (!storageReady) return;
  LittleFS.remove("/trips.csv");
  fs::File src = LittleFS.open("/trips_v2.csv", "r");
  fs::File dst = LittleFS.open("/trips.csv", FILE_WRITE);
  if (!src || !dst) { if (src) src.close(); if (dst) dst.close(); return; }
  int id = 0;
  while (src.available()) {
    String line = src.readStringUntil('\n'); line.trim();
    TripRecord r;
    if (!parseTripRecord(line, id++, r)) continue;
    String fuelField = !isnan(r.fuelLiters) ? String(r.fuelLiters, 2) + " L" : String("--");
    String row = formatDateTime(r.startEpoch) + "," + formatDateTime(r.endEpoch) + "," +
                 String(r.distanceKm, 2) + " km," + fuelField + "," +
                 (!isnan(r.avgConsumption) ? String(r.avgConsumption, 2) + " L/100" : String("--")) + "," +
                 formatDuration(r.durationSec) + "," + String(r.maxSpeedKmh, 0) + " max," +
                 String(r.avgSpeedKmh, 0) + " avg";
    dst.println(row);
  }
  src.close(); dst.close();
}

bool deleteTripById(int wantedId) {
  if (!storageReady || wantedId < 0) return false;
  fs::File src = LittleFS.open("/trips_v2.csv", "r");
  if (!src) return false;
  LittleFS.remove("/trips_tmp.csv");
  fs::File dst = LittleFS.open("/trips_tmp.csv", FILE_WRITE);
  if (!dst) { src.close(); return false; }
  int id = 0; bool removed = false;
  while (src.available()) {
    String line = src.readStringUntil('\n'); line.trim();
    if (!line.length()) continue;
    if (id == wantedId) removed = true;
    else dst.println(line);
    id++;
  }
  src.close(); dst.close();
  if (!removed) { LittleFS.remove("/trips_tmp.csv"); return false; }
  LittleFS.remove("/trips_v2.csv");
  if (!LittleFS.rename("/trips_tmp.csv", "/trips_v2.csv")) return false;
  rebuildLegacyTripsCsv();
  return true;
}

void clearTripCheckpoint() {
  if (storageReady) LittleFS.remove("/trip_checkpoint.bin");
}

void persistTripCheckpoint(bool force) {
  if (!trip.active || !storageReady) return;
  unsigned long now = millis();
  if (!force && now - trip.lastCheckpointMs < TRIP_CHECKPOINT_MS) return;
  trip.lastCheckpointMs = now;
  TripCheckpointDisk cp;
  cp.startEpoch = (uint32_t)trip.startEpoch;
  cp.elapsedSec = currentTripElapsedSec();
  cp.distanceKm = trip.distanceKm;
  cp.fuelLiters = trip.fuelLiters;
  cp.maxSpeedKmh = trip.maxSpeedKmh;
  cp.lastFuelCounterL = isnan(trip.lastFuelCounterL) ? 0.0f : trip.lastFuelCounterL;
  cp.fuelAvailable = trip.fuelAvailable ? 1 : 0;
  cp.fuelCounterValid = (trip.fuelCounterValid && !isnan(trip.lastFuelCounterL)) ? 1 : 0;
  fs::File f = LittleFS.open("/trip_checkpoint.bin", FILE_WRITE);
  if (f) { f.write((const uint8_t*)&cp, sizeof(cp)); f.close(); }
}

void restoreTripCheckpoint() {
  if (!storageReady) return;
  fs::File f = LittleFS.open("/trip_checkpoint.bin", "r");
  if (!f || f.size() != sizeof(TripCheckpointDisk)) { if (f) f.close(); return; }
  TripCheckpointDisk cp;
  size_t got = f.read((uint8_t*)&cp, sizeof(cp)); f.close();
  if (got != sizeof(cp) || cp.magic != 0x4C543236UL) { LittleFS.remove("/trip_checkpoint.bin"); return; }
  trip.active = true;
  trip.recovered = true;
  trip.startMs = millis();
  trip.lastUpdateMs = millis();
  trip.engineStoppedSince = 0;
  trip.stopElapsedSec = 0;
  trip.elapsedBeforeBootSec = cp.elapsedSec;
  trip.startEpoch = (time_t)cp.startEpoch;
  trip.distanceKm = max(0.0f, cp.distanceKm);
  trip.fuelLiters = max(0.0f, cp.fuelLiters);
  trip.fuelAvailable = cp.fuelAvailable != 0;
  trip.maxSpeedKmh = max(0.0f, cp.maxSpeedKmh);
  trip.fuelCounterValid = cp.fuelCounterValid != 0;
  trip.lastFuelCounterL = trip.fuelCounterValid ? cp.lastFuelCounterL : NAN;
  trip.lastCheckpointMs = millis();
  Serial.printf("TRIP checkpoint obnoven: %.2f km, %lu s\n", trip.distanceKm, trip.elapsedBeforeBootSec);
}

bool buildTripsExportCsv() {
  if (!storageReady) return false;
  fs::File src = LittleFS.open("/trips_v2.csv", "r");
  if (!src) return false;
  LittleFS.remove("/trips_export.csv");
  fs::File dst = LittleFS.open("/trips_export.csv", FILE_WRITE);
  if (!dst) { src.close(); return false; }
  dst.println("ID;Nazev;Zacatek;Konec;Vzdalenost_km;Palivo_L;Spotreba_L_100;Max_km_h;Prumer_km_h;Doba;Kratka_jizda");
  int id = 0;
  while (src.available()) {
    String line = src.readStringUntil('\n'); line.trim();
    TripRecord r;
    if (!parseTripRecord(line, id, r)) { id++; continue; }
    String row = String(id) + ";" + tripAutoName(r.startEpoch, id) + ";" + formatDateTime(r.startEpoch) + ";" +
                 formatDateTime(r.endEpoch) + ";" + String(r.distanceKm, 3) + ";" +
                 (isnan(r.fuelLiters) ? String("") : String(r.fuelLiters, 3)) + ";" +
                 (isnan(r.avgConsumption) ? String("") : String(r.avgConsumption, 2)) + ";" +
                 String(r.maxSpeedKmh, 1) + ";" + String(r.avgSpeedKmh, 1) + ";" +
                 formatDuration(r.durationSec) + ";" + (tripRecordIsShort(r) ? "ANO" : "NE");
    dst.println(row); id++;
  }
  src.close(); dst.close();
  return true;
}

bool parseFuelingRecord(const String &line, int id, FuelingRecord &r) {
  if (!line.length() || line.indexOf(';') < 0) return false;
  r.id = id;
  r.epoch = (time_t)strtoull(csvField(line, 0).c_str(), nullptr, 10);
  String odo = csvField(line, 1);
  r.odometerKm = odo.length() ? odo.toFloat() : NAN;
  String before = csvField(line, 2);
  String after = csvField(line, 3);
  String added = csvField(line, 4);
  r.beforeLiters = before.length() ? before.toFloat() : NAN;
  r.afterLiters = after.length() ? after.toFloat() : NAN;
  r.addedLiters = added.length() ? added.toFloat() : NAN;
  return true;
}

int loadRecentFuelings(FuelingRecord *out, int maxCount) {
  if (!storageReady || maxCount <= 0) return 0;
  fs::File f = LittleFS.open("/fueling.csv", "r");
  if (!f) return 0;
  int count = 0, id = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    FuelingRecord r;
    if (!parseFuelingRecord(line, id++, r)) continue;
    if (count < maxCount) out[count++] = r;
    else {
      for (int i = 0; i < maxCount - 1; i++) out[i] = out[i + 1];
      out[maxCount - 1] = r;
    }
  }
  f.close();
  return count;
}

bool inside(int px, int py, int x, int y, int w, int h) {
  return px >= x && px <= x + w && py >= y && py <= y + h;
}

void invalidateLiveTelemetry(bool dropUds) {
  car.ecuResponding = false;
  if (dropUds) {
    car.udsResponding = false;
    vehicleClockAvailable = false;
    // Pri ztrate cele Bluetooth/ELM session uz ani posledni ATRV neni aktualni.
    car.voltageV = NAN;
  }

  car.moduleVoltageV = NAN;
  car.moduleVoltageAvailable = false;
  car.speedKmh = 0.0f;
  car.rpm = 0.0f;
  car.coolantC = NAN;
  car.engineLoadPct = NAN;
  car.mafGps = NAN;
  car.mapKpa = NAN;
  car.intakeTempC = NAN;
  car.intakeAvailable = false;
  car.ambientTempC = NAN;
  car.ambientAvailable = false;
  car.baroKpa = NAN;
  car.baroAvailable = false;
  car.railPressureKpa = NAN;
  car.railAvailable = false;
  car.fuelRateLph = NAN;
  car.fuelRateAvailable = false;
  car.throttlePct = NAN;
  car.throttleAvailable = false;
  car.egrCommandPct = NAN;
  car.egrCommandAvailable = false;
  car.egrErrorPct = NAN;
  car.egrErrorAvailable = false;
  car.lambdaCommanded = NAN;
  car.lambdaCommandedAvailable = false;
  car.lambdaB1S1 = NAN;
  car.o2VoltageB1S1 = NAN;
  car.lambdaB1S1Available = false;
  car.fuelLevelPct = NAN;
  car.fuelLevelAvailable = false;
  car.fuelLevelAutomatic = false;

  dpf.available = false;
  dpf.regenActive = false;
  dpf.sootCalculatedG = NAN;
  dpf.sootMeasuredG = NAN;
  dpf.kmSinceRegen = NAN;
  dpf.fuelSinceRegenL = NAN;
  dpf.minutesSinceRegen = NAN;
  dpf.ashG = NAN;
  dpf.egtPreC = NAN;
  dpf.egtPostC = NAN;
  dpf.regenRaw = 0;
  dpf.regenStatusAvailable = false;
  dpf.regenStatusUpdateMs = 0;
  dpf.diffPressureAvailable = false;
  dpf.diffPressureHpa = NAN;
  dpf.lastUpdateMs = 0;
}

void applyBrightness(uint8_t v) {
  analogWrite(TFT_BL, v);
}

static void markSettingsDirty() {
  settingsDirty = true;
  settingsDirtySinceMs = millis();
}

static uint8_t activeUiBrightness() {
  if (autoDayNightBrightness && clockIsValid()) {
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    if (ti.tm_hour >= 20 || ti.tm_hour < 6) return displayNightBrightness;
  }
  return displayBrightness;
}

static void flushDeferredSettings(bool force = false) {
  if (!settingsDirty) return;
  if (!force && millis() - settingsDirtySinceMs < 2500UL) return;

  prefs.putUChar("brightness", displayBrightness);
  prefs.putUChar("nightBright", displayNightBrightness);
  prefs.putBool("autoDayNight", autoDayNightBrightness);
  prefs.putBool("touchFb", touchFeedbackEnabled);
  prefs.putUChar("touchFbStr", touchFeedbackStrength);
  prefs.putBool("autoElm", autoElmReconnect);
  prefs.putUChar("obdMode", obdDataMode);
  prefs.putBool("fastData", obdDataMode == 2);
  prefs.putUChar("engineProf", (uint8_t)configuredEngineProfile);
  prefs.putFloat("dpfLimitG", dpfRegenLimitG);
  prefs.putFloat("regenSoonKm", alarmRegenSoonKm);
  prefs.putBool("alRegen", alarmRegenEnabled);
  prefs.putBool("alRegen10", alarmRegenSoonEnabled);
  prefs.putBool("pwrAutoDim", powerAutoDim);
  prefs.putULong("pwrDimMs", powerDimDelayMs);
  prefs.putULong("pwrOffMs", powerDisplayOffDelayMs);
  prefs.putULong("pwrLowMs", powerLowDelayMs);
  prefs.putFloat("tankCapL", tankCapacityL);
  prefs.putFloat("rangeL100", rangeFallbackL100);
  settingsDirty = false;
}

void setBrightness(uint8_t v, bool persist = true) {
  displayBrightness = v;
  applyBrightness(activeUiBrightness());
  if (persist) markSettingsDirty();
}

void noteUserActivity() {
  lastUserActivityMs = millis();
  if (displayDimmed) {
    displayDimmed = false;
    applyBrightness(activeUiBrightness());
  }
}

void touchFeedbackPulse() {
  if (!touchFeedbackEnabled || displayDimmed || displayOffAfterIgnition) return;
  uint8_t active = activeUiBrightness();
  if (active < 25) return;
  uint8_t maxDrop = active > 8 ? (uint8_t)(active - 8) : 0;
  uint8_t drop = touchFeedbackStrength < maxDrop ? touchFeedbackStrength : maxDrop;
  uint8_t pulse = active - drop;
  applyBrightness(pulse);
  delay(18);
  applyBrightness(active);
}

static void updateScheduledBrightness() {
  static uint8_t lastApplied = 255;
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 30000UL) return;
  lastCheck = millis();
  if (displayDimmed || displayOffAfterIgnition || powerMode != POWER_ACTIVE) return;
  uint8_t wanted = activeUiBrightness();
  if (wanted != lastApplied) {
    applyBrightness(wanted);
    lastApplied = wanted;
  }
}

// ============================ TOUCH ===========================

bool getTap(int &x, int &y) {
  bool nowTouched = touch.touched();

  if (!nowTouched) {
    previousTouch = false;
    return false;
  }
  if (previousTouch) return false;

  TS_Point p = touch.getPoint();
  // Slaby prvni kontakt jeste nepovazujeme za kompletni tap. previousTouch
  // nastavime az po validnim tlaku, jinak se lehky dotyk zbytecne "ztratil".
  if (p.z < 200) return false;
  previousTouch = true;

  int tx = constrain(p.x, TOUCH_MIN_X, TOUCH_MAX_X);
  int ty = constrain(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y);

  x = map(tx, TOUCH_MIN_X, TOUCH_MAX_X, 0, 319);
  y = map(ty, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, 239);

  // Touch debug je zamerne tichy, aby Serial Monitor zustal citelny.
  noteUserActivity();
  touchFeedbackPulse();
  return true;
}

// ======================== GRAFICKE PRVKY ======================

// ==================== PNGdec UI IKONY ====================
// V16.24: vsechny UI ikony i uvodni logo jsou PNG dekodovane pres PNGdec.
// Menu, DPF, ECU, Bluetooth i Wi-Fi se dekoduji jednim PNGdec rendererem.

uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t a4) {
  if (a4 == 0) return bg;
  if (a4 >= 15) return fg;

  uint8_t br = (bg >> 11) & 0x1F;
  uint8_t bg6 = (bg >> 5) & 0x3F;
  uint8_t bb = bg & 0x1F;
  uint8_t fr = (fg >> 11) & 0x1F;
  uint8_t fg6 = (fg >> 5) & 0x3F;
  uint8_t fb = fg & 0x1F;

  uint8_t r = (br * (15 - a4) + fr * a4 + 7) / 15;
  uint8_t g = (bg6 * (15 - a4) + fg6 * a4 + 7) / 15;
  uint8_t b = (bb * (15 - a4) + fb * a4 + 7) / 15;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

struct UiPngContext {
  int16_t x;
  int16_t y;
  uint16_t bg565;
};

static uint32_t rgb565ToPngBackground(uint16_t c) {
  uint8_t r = (uint8_t)((((c >> 11) & 0x1F) * 255U) / 31U);
  uint8_t g = (uint8_t)((((c >> 5)  & 0x3F) * 255U) / 63U);
  uint8_t b = (uint8_t)((( c        & 0x1F) * 255U) / 31U);
  return ((uint32_t)b << 16) | ((uint32_t)g << 8) | r; // 00BBGGRR
}

static int drawUiPngLine(PNGDRAW *pDraw) {
  UiPngContext *ctx = (UiPngContext *)pDraw->pUser;
  if (!ctx || pDraw->iWidth <= 0 || pDraw->iWidth > 320) return 0;
  uint16_t lineBuffer[320]; // stack-local: nespotrebovava globalni .dram0.bss
  uiPng.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN,
                        rgb565ToPngBackground(ctx->bg565));
  tft.pushImage(ctx->x, ctx->y + pDraw->y, pDraw->iWidth, 1, lineBuffer);
  return 1;
}

static bool drawUiPngFlash(const uint8_t *pngData, size_t pngLen,
                           int x, int y, uint16_t bg565) {
  if (!pngData || pngLen == 0) return false;
  int rc = uiPng.openFLASH((uint8_t *)pngData, (int)pngLen, drawUiPngLine);
  if (rc != PNG_SUCCESS) return false;
  UiPngContext ctx = {(int16_t)x, (int16_t)y, bg565};
  rc = uiPng.decode((void *)&ctx, PNG_FAST_PALETTE);
  uiPng.close();
  return rc == PNG_SUCCESS;
}

void drawBluetoothIcon(int x, int y, uint16_t color, int size = 1, uint16_t bg = COL_BG) {
  (void)color; // stav je barevne vyjadren rameckem/badgem; PNG zustava transparentni.
  if (size >= 2) drawUiPngFlash(UI_PNG_STATUS_BLUETOOTH_40, UI_PNG_STATUS_BLUETOOTH_40_LEN, x - 20, y - 20, bg);
  else           drawUiPngFlash(UI_PNG_STATUS_BLUETOOTH_20, UI_PNG_STATUS_BLUETOOTH_20_LEN, x - 10, y - 10, bg);
}


void drawButton(int x, int y, int w, int h, const String &label, bool active = true, bool primary = false) {
  uint16_t bg = COL_PANEL2;
  uint16_t border = COL_BORDER;
  uint16_t text = COL_TEXT;

  if (!active) {
    bg = COL_PANEL;
    text = COL_MUTED;
  } else if (primary) {
    bg = COL_BLUE;
    border = COL_CYAN;
  }

  tft.fillRoundRect(x, y, w, h, 7, bg);
  tft.drawRoundRect(x, y, w, h, 7, border);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(text, bg);
  tft.drawString(label, x + w / 2, y + h / 2);
  tft.setTextFont(1);
}

void drawElmStopButton(int x, int y, int w, int h, bool active = true) {
  uint16_t bg = active ? COL_RED : blend565(COL_PANEL, COL_RED, 5);
  uint16_t border = active ? TFT_WHITE : COL_BORDER;
  uint16_t text = active ? TFT_BLACK : COL_MUTED;
  tft.fillRoundRect(x, y, w, h, 7, bg);
  tft.drawRoundRect(x, y, w, h, 7, border);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(text, bg);
  tft.drawString("STOP", x + w / 2, y + h / 2);
  tft.setTextFont(1);
}

void drawDpfHeaderStatus() {
  // V15.8: velky stavovy badge. Zeleny = DPF data OK, oranzovy = regenerace,
  // sedy = data nejsou dostupna. Ikona je PNG dekodovana pres PNGdec.
  uint16_t c = !dpf.available ? COL_MUTED : (dpf.regenActive ? COL_ORANGE : COL_GREEN);
  const int x = 187, y = 7, w = 28, h = 27;
  tft.fillRoundRect(x, y, w, h, 7, COL_PANEL2);
  tft.drawRoundRect(x, y, w, h, 7, blend565(COL_PANEL2, c, 9));
  drawUiPngFlash(UI_PNG_STATUS_DPF, UI_PNG_STATUS_DPF_LEN, x + 3, y + 3, COL_PANEL2);
  if (dpf.regenActive) tft.fillCircle(x + 24, y + 4, 3, COL_ORANGE);
}

void drawHeader(const String &title, bool backButton = false) {
  tft.fillRect(0, 0, 320, 40, COL_BG);

  if (backButton) {
    tft.fillRoundRect(8, 6, 32, 28, 7, COL_PANEL2);
    tft.drawRoundRect(8, 6, 32, 28, 7, COL_BORDER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_TEXT, COL_PANEL2);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.drawString("<", 24, 20);
  }

  // V15.8: vlevo zustava jen nadpis, vpravo jsou 4 velke a citelne status badge:
  // DPF / ECU / Bluetooth / Wi-Fi. Hodiny byly z hlavicky odstraneny, aby se
  // statusy neprekryvaly a byly citelne jednim pohledem za jizdy.
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  int tx = backButton ? 48 : 10;
  int maxW = backButton ? 132 : 166;
  tft.setTextFont(2);
  tft.setTextSize(1);
  if (tft.textWidth(title) <= maxW) {
    tft.drawString(title, tx, 11);
  } else {
    tft.setTextFont(1);
    tft.setTextSize(2);
    tft.drawString(title, tx, 10);
  }
  tft.setTextFont(1); tft.setTextSize(1);

  drawDpfHeaderStatus();

  uint16_t ecuColor = car.ecuResponding ? COL_GREEN : COL_RED;
  tft.fillRoundRect(218, 7, 28, 27, 7, COL_PANEL2);
  tft.drawRoundRect(218, 7, 28, 27, 7, blend565(COL_PANEL2, ecuColor, 9));
  drawUiPngFlash(UI_PNG_STATUS_ENGINE, UI_PNG_STATUS_ENGINE_LEN, 222, 11, COL_PANEL2);

  uint16_t btColor = car.elmConnected ? COL_GREEN : ((offlineMode || serviceWifiActive) ? COL_MUTED : COL_RED);
  tft.fillRoundRect(249, 7, 28, 27, 7, COL_PANEL2);
  tft.drawRoundRect(249, 7, 28, 27, 7, blend565(COL_PANEL2, btColor, 9));
  drawUiPngFlash(UI_PNG_STATUS_BLUETOOTH_20, UI_PNG_STATUS_BLUETOOTH_20_LEN, 253, 11, COL_PANEL2);

  uint16_t wifiColor = (serviceWifiActive || wifiStaConnected) ? COL_GREEN : COL_MUTED;
  tft.fillRoundRect(280, 7, 31, 27, 7, COL_PANEL2);
  tft.drawRoundRect(280, 7, 31, 27, 7, blend565(COL_PANEL2, wifiColor, 9));
  drawUiPngFlash(UI_PNG_STATUS_WIFI, UI_PNG_STATUS_WIFI_LEN, 285, 10, COL_PANEL2);
}

void drawMetricCard(int x, int y, int w, int h,
                    const String &label, const String &value,
                    const String &unit, uint16_t accent = COL_CYAN) {
  tft.fillRoundRect(x, y, w, h, 8, COL_PANEL);
  tft.drawRoundRect(x, y, w, h, 8, COL_BORDER);
  tft.drawFastHLine(x + 7, y + 3, w - 14, accent);

  // Popisek: FONT2 je podstatne citelnejsi nez puvodni 8px systemovy font.
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(COL_LABEL, COL_PANEL);
  tft.drawString(label, x + 7, y + 5);

  // Hodnota: velky Font4 u kratkych hodnot, Font2 u delsiho stavu ONLINE/CEKAM.
  tft.setTextColor(accent, COL_PANEL);
  tft.setTextFont((h >= 48 && value.length() <= 5) ? 4 : 2);
  tft.setTextSize(1);
  tft.drawString(value, x + 7, y + 24);

  if (unit.length()) {
    tft.setTextDatum(BR_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(COL_TEXT, COL_PANEL);
    tft.drawString(unit, x + w - 6, y + h - 5);
  }
  tft.setTextFont(1);
}

// V16.3: funkcni mini otackomer pro PREHLED. Stupnice je 0-6000 rpm,
// rucicka se pohybuje podle skutecneho RPM. Staticka stupnice se kresli jen pri
// otevreni obrazovky; pri aktualizaci se meni pouze rucicka a cislo, aby nic neblikalo.
static int overviewTachLastNeedleX = -1;
static int overviewTachLastNeedleY = -1;
static int overviewTachLastRpmShown = -99999;

void drawThickLine(int x0, int y0, int x1, int y1, uint16_t color, int width = 2) {
  tft.drawLine(x0, y0, x1, y1, color);
  if (width >= 2) {
    tft.drawLine(x0 + 1, y0, x1 + 1, y1, color);
    tft.drawLine(x0, y0 + 1, x1, y1 + 1, color);
  }
  if (width >= 3) tft.drawLine(x0 - 1, y0, x1 - 1, y1, color);
}

uint16_t rpmColor(float rpm) {
  if (isnan(rpm) || rpm < 0) return COL_MUTED;
  if (rpm < 2500.0f) return COL_GREEN;
  if (rpm < 3500.0f) return COL_CYAN;
  if (rpm < 4500.0f) return COL_ORANGE;
  return COL_RED;
}

float tachAngleForRpm(float rpm) {
  rpm = clampFloat(rpm, 0.0f, 6000.0f);
  return 150.0f + (rpm / 6000.0f) * 240.0f;
}

void drawOverviewTachStatic(int x, int y, int w, int h) {
  tft.fillRoundRect(x, y, w, h, 8, COL_PANEL);
  tft.drawRoundRect(x, y, w, h, 8, COL_BORDER);
  tft.drawFastHLine(x + 7, y + 3, w - 14, COL_ORANGE);

  const int cx = x + 31;
  const int cy = y + 31;
  const int r = 22;

  // barevna stupnice po segmentech
  for (int i = 0; i < 36; i++) {
    float a0 = 150.0f + i * (240.0f / 36.0f);
    float a1 = 150.0f + (i + 1) * (240.0f / 36.0f);
    float rpmMid = ((i + 0.5f) / 36.0f) * 6000.0f;
    uint16_t c = rpmMid < 4000.0f ? COL_CYAN : (rpmMid < 5000.0f ? COL_ORANGE : COL_RED);
    int x0 = cx + (int)round(cos(a0 * DEG_TO_RAD) * r);
    int y0 = cy + (int)round(sin(a0 * DEG_TO_RAD) * r);
    int x1 = cx + (int)round(cos(a1 * DEG_TO_RAD) * r);
    int y1 = cy + (int)round(sin(a1 * DEG_TO_RAD) * r);
    drawThickLine(x0, y0, x1, y1, c, 2);
  }

  // hlavni dilky 0..6 x1000
  for (int k = 0; k <= 6; k++) {
    float a = (150.0f + (k / 6.0f) * 240.0f) * DEG_TO_RAD;
    int x0 = cx + (int)round(cos(a) * (r - 1));
    int y0 = cy + (int)round(sin(a) * (r - 1));
    int x1 = cx + (int)round(cos(a) * (r - 5));
    int y1 = cy + (int)round(sin(a) * (r - 5));
    drawThickLine(x0, y0, x1, y1, COL_TEXT, 2);
  }

  tft.fillCircle(cx, cy, 3, COL_TEXT);
  tft.fillCircle(cx, cy, 1, COL_PANEL);

  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(COL_LABEL, COL_PANEL);
  tft.drawString("RPM", x + 59, y + 6);
  tft.setTextFont(1);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.drawString("x1000", x + 60, y + 42);

  overviewTachLastNeedleX = -1;
  overviewTachLastNeedleY = -1;
  overviewTachLastRpmShown = -99999;
}

void updateOverviewTach(int x, int y, int w, int h, float rpm) {
  int rpmInt = isnan(rpm) ? 0 : (int)round(clampFloat(rpm, 0.0f, 6000.0f));
  if (rpmInt == overviewTachLastRpmShown && overviewTachLastNeedleX >= 0) return;

  const int cx = x + 31;
  const int cy = y + 31;
  const int needleLen = 14; // zamerne kratke: nezasahuje do staticke stupnice

  if (overviewTachLastNeedleX >= 0) {
    drawThickLine(cx, cy, overviewTachLastNeedleX, overviewTachLastNeedleY, COL_PANEL, 3);
  }

  float a = tachAngleForRpm((float)rpmInt) * DEG_TO_RAD;
  int nx = cx + (int)round(cos(a) * needleLen);
  int ny = cy + (int)round(sin(a) * needleLen);
  uint16_t rpmC = rpmColor((float)rpmInt);
  drawThickLine(cx, cy, nx, ny, rpmC, 3);
  tft.fillCircle(cx, cy, 3, COL_TEXT);
  tft.fillCircle(cx, cy, 1, rpmC);

  // Hodnota se prekresli jen vpravo od budiku.
  tft.fillRect(x + 58, y + 20, w - 64, 22, COL_PANEL);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(4);
  tft.setTextSize(1);
  tft.setTextColor(rpmColor((float)rpmInt), COL_PANEL);
  tft.drawString(String(rpmInt), x + 59, y + 20);
  tft.setTextFont(1);

  overviewTachLastNeedleX = nx;
  overviewTachLastNeedleY = ny;
  overviewTachLastRpmShown = rpmInt;
}

// Prekresli pouze hodnotu uvnitr karty, ne celou obrazovku.
void updateMetricValue(int x, int y, int w, int h,
                       const String &value, const String &unit,
                       uint16_t accent = COL_CYAN) {
  // V15.1 anti-flicker: pokud se hodnota nezmenila, na TFT neposilame vubec nic.
  // Tohle je dulezite hlavne pri 100ms UI smycce - puvodne se karta mazala
  // a kreslila znovu i desetkrat za sekundu se stejnou hodnotou.
  struct CacheEntry {
    bool used;
    int x, y, w, h;
    String value;
    String unit;
    uint16_t accent;
  };
  static CacheEntry cache[32] = {};

  int slot = -1;
  for (int i = 0; i < 32; i++) {
    if (cache[i].used && cache[i].x == x && cache[i].y == y &&
        cache[i].w == w && cache[i].h == h) {
      slot = i;
      break;
    }
    if (slot < 0 && !cache[i].used) slot = i;
  }
  if (slot < 0) slot = (x + y + w + h) & 31;

  CacheEntry &c = cache[slot];
  if (c.used && c.x == x && c.y == y && c.w == w && c.h == h &&
      c.value == value && c.unit == unit && c.accent == accent) return;

  c.used = true; c.x = x; c.y = y; c.w = w; c.h = h;
  c.value = value; c.unit = unit; c.accent = accent;

  // Zachovej horni popisek; mazeme pouze oblast dynamicke hodnoty.
  tft.fillRect(x + 5, y + 21, w - 10, h - 25, COL_PANEL);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(accent, COL_PANEL);
  tft.setTextFont((h >= 48 && value.length() <= 5) ? 4 : 2);
  tft.setTextSize(1);
  tft.drawString(value, x + 7, y + 24);

  if (unit.length()) {
    tft.setTextDatum(BR_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(COL_TEXT, COL_PANEL);
    tft.drawString(unit, x + w - 6, y + h - 5);
  }
  tft.setTextFont(1);
}

void updateHeaderStatus() {
  // V15.8 anti-flicker: prekresluji se jen badge, jejichz stav se zmenil.
  static int lastDpfState = -99;
  static int lastEcuState = -99;
  static int lastBtState = -99;
  static int lastWifiState = -99;

  int dpfState = !dpf.available ? 0 : (dpf.regenActive ? 2 : 1);
  if (dpfState != lastDpfState) {
    tft.fillRect(185, 5, 32, 31, COL_BG);
    drawDpfHeaderStatus();
    lastDpfState = dpfState;
  }

  int ecuState = car.ecuResponding ? 1 : 0;
  if (ecuState != lastEcuState) {
    uint16_t c = ecuState ? COL_GREEN : COL_RED;
    tft.fillRect(216, 5, 32, 31, COL_BG);
    tft.fillRoundRect(218, 7, 28, 27, 7, COL_PANEL2);
    tft.drawRoundRect(218, 7, 28, 27, 7, blend565(COL_PANEL2, c, 9));
    drawUiPngFlash(UI_PNG_STATUS_ENGINE, UI_PNG_STATUS_ENGINE_LEN, 222, 11, COL_PANEL2);
    lastEcuState = ecuState;
  }

  int btState = car.elmConnected ? 2 : ((offlineMode || serviceWifiActive) ? 1 : 0);
  if (btState != lastBtState) {
    uint16_t c = car.elmConnected ? COL_GREEN : ((offlineMode || serviceWifiActive) ? COL_MUTED : COL_RED);
    tft.fillRect(247, 5, 32, 31, COL_BG);
    tft.fillRoundRect(249, 7, 28, 27, 7, COL_PANEL2);
    tft.drawRoundRect(249, 7, 28, 27, 7, blend565(COL_PANEL2, c, 9));
    drawUiPngFlash(UI_PNG_STATUS_BLUETOOTH_20, UI_PNG_STATUS_BLUETOOTH_20_LEN, 253, 11, COL_PANEL2);
    lastBtState = btState;
  }

  int wifiState = (serviceWifiActive || wifiStaConnected) ? 1 : 0;
  if (wifiState != lastWifiState) {
    uint16_t c = wifiState ? COL_GREEN : COL_MUTED;
    tft.fillRect(278, 5, 36, 31, COL_BG);
    tft.fillRoundRect(280, 7, 31, 27, 7, COL_PANEL2);
    tft.drawRoundRect(280, 7, 31, 27, 7, blend565(COL_PANEL2, c, 9));
    drawUiPngFlash(UI_PNG_STATUS_WIFI, UI_PNG_STATUS_WIFI_LEN, 285, 10, COL_PANEL2);
    lastWifiState = wifiState;
  }
}

// ========================= START ANIMACE ======================

void showSplash() {
  tft.fillScreen(TFT_BLACK);

  // V16.23: puvodni RGB565 SkodaLogo.h byl odstranen.
  // Nove potunene SKODA logo je 320x180 PNG v PROGMEM a dekoduje se pres PNGdec,
  // stejne jako zbytek noveho grafickeho rozhrani.
  drawUiPngFlash(STARTUP_SKODA_PNG, STARTUP_SKODA_PNG_LEN, 0, 0, TFT_BLACK);

  uint16_t skodaGreen = tft.color565(75, 220, 80);

  // Uvodni text zustava uzivatelsky menitelny pres servisni WEB.
  // Je umisten pod obrazkem, aby logo zustalo ciste a dominantni.
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_CYAN, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextSize(1);
  String splashShown = splashTitle;
  if (tft.textWidth(splashShown) > 300) {
    while (splashShown.length() > 3 && tft.textWidth(splashShown + "...") > 300) {
      splashShown.remove(splashShown.length() - 1);
    }
    splashShown += "...";
  }
  tft.drawString(splashShown, 160, 194);
  tft.drawString(splashShown, 161, 194); // jemne zesileni

  tft.setTextFont(2);
  tft.setTextColor(COL_MUTED, TFT_BLACK);
  tft.drawString(String("FW ") + FW_VERSION, 160, 214);

  // Kratky premium progress bar v zelene barve loga.
  tft.fillRoundRect(80, 228, 160, 5, 2, COL_PANEL2);
  for (int w = 0; w <= 160; w += 16) {
    tft.fillRoundRect(80, 228, w, 5, 2, skodaGreen);
    delay(25);
  }

  delay(2600);
  tft.setTextFont(1);
  tft.setTextSize(1);
}


// ============================= MENU ===========================
// V16.8 MENU POLISH
// Finalni uprava hlavniho menu: bez drobnych popisku, vetsi bitmapove ikony,
// silnejsi vizualni hierarchie, cistsi horni lista a jemnejsi glow karet.


static uint16_t premiumCardBg(uint16_t accent) {
  return blend565(COL_BG, accent, 1);
}

static uint16_t premiumCardInner(uint16_t accent) {
  return blend565(COL_BG, accent, 1);
}

// V16.24: hlavni menu pouziva transparentni PNG ikony z MenuIcons.h pres PNGdec.

static bool menuPngForIcon(int iconType, const uint8_t *&data, size_t &len, int &w, int &h) {
  // V16.24: vsechny bezne menu ikony maji pruhledny canvas 58x44.
  // DPF je sirsi 76x42, aby byla jeho silueta citelna i na 100px dlazdici.
  data = nullptr; len = 0; w = 58; h = 44;
  switch (iconType) {
    case 1:  data = MENU_PNG_MOTOR;    len = MENU_PNG_MOTOR_LEN; break;
    case 2:  data = MENU_PNG_TURBO;    len = MENU_PNG_TURBO_LEN; break;
    case 3:  data = MENU_PNG_DIAG;     len = MENU_PNG_DIAG_LEN; break;
    case 4:  data = MENU_PNG_TRIP;     len = MENU_PNG_TRIP_LEN; break;
    case 6:  data = MENU_PNG_SENSORS;  len = MENU_PNG_SENSORS_LEN; break;
    case 7:  data = MENU_PNG_ELECTRIC; len = MENU_PNG_ELECTRIC_LEN; break;
    case 8:  data = MENU_PNG_SERVICE;  len = MENU_PNG_SERVICE_LEN; break;
    case 9:  data = MENU_PNG_WIFI;     len = MENU_PNG_WIFI_LEN; break;
    case 10: data = MENU_PNG_POWER;    len = MENU_PNG_POWER_LEN; break;
    case 11: data = MENU_PNG_SETTINGS; len = MENU_PNG_SETTINGS_LEN; break;
    case 13: data = MENU_PNG_ALARMS;   len = MENU_PNG_ALARMS_LEN; break;
    case 14: data = MENU_PNG_FUEL;     len = MENU_PNG_FUEL_LEN; break;
    case 16: data = MENU_PNG_SPECIALS; len = MENU_PNG_SPECIALS_LEN; break;
    case 17: data = MENU_PNG_INFO;     len = MENU_PNG_INFO_LEN; break;
    case 12: data = MENU_PNG_OVERVIEW; len = MENU_PNG_OVERVIEW_LEN; break;
    case 15: data = MENU_PNG_DPF;      len = MENU_PNG_DPF_LEN; w = 76; h = 42; break;
    default: return false;
  }
  return true;
}

static void drawPremiumIcon(int iconType, int cx, int cy, uint16_t accent, uint16_t bg) {
  const uint8_t *data = nullptr;
  size_t len = 0;
  int iw = 58, ih = 44;
  if (menuPngForIcon(iconType, data, len, iw, ih)) {
    const int px = cx - iw / 2;
    const int py = cy - ih / 2;
    // Ikona uz nema vlastni cerny ctverec ani ramecek. PNG alpha se sklada
    // primo s barvou dlazdice, takze vypada jako soucast karty.
    if (drawUiPngFlash(data, len, px, py, bg)) return;
  }

  // Fallback pouze pri poskozenem PNG; i ten pouziva pozadi dlazdice.
  tft.drawCircle(cx, cy, 18, accent);
  tft.setTextDatum(MC_DATUM); tft.setTextFont(2); tft.setTextColor(accent, bg);
  tft.drawString("?", cx, cy);
}

static void drawMenuTileLabel(int x, int y, int w, int h, const String &label, uint16_t bg) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_TEXT, bg);
  tft.setTextFont(2);
  tft.setTextSize(1);

  int split = label.lastIndexOf(' ');
  bool twoLine = false;
  String line1 = label;
  String line2 = "";
  if (split > 0 && tft.textWidth(label) > w - 12) {
    line1 = label.substring(0, split);
    line2 = label.substring(split + 1);
    twoLine = true;
  }

  if (!twoLine && tft.textWidth(label) > w - 12) {
    tft.setTextFont(1);
    tft.setTextSize(2);
  }

  if (twoLine) {
    if (tft.textWidth(line1) > w - 12 || tft.textWidth(line2) > w - 12) {
      tft.setTextFont(1);
      tft.setTextSize(2);
    }
    tft.drawString(line1, x + w/2, y + h - 22);
    tft.drawString(line2, x + w/2, y + h - 10);
  } else {
    tft.drawString(label, x + w/2, y + h - 14);
  }

  tft.setTextFont(1);
  tft.setTextSize(1);
}

void drawMenuTilePremium(int x, int y, int w, int h, const String &label,
                         const String &sub, int iconType, uint16_t accent) {
  (void)sub;
  const uint16_t bg = premiumCardInner(accent);
  const uint16_t outerGlow = blend565(COL_BG, accent, 4);
  const uint16_t border = blend565(COL_BORDER, accent, 10);

  tft.fillRoundRect(x + 2, y + 3, w, h, 10, TFT_BLACK);
  tft.fillRoundRect(x, y, w, h, 10, premiumCardBg(accent));
  tft.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 8, bg);

  tft.drawRoundRect(x - 1, y - 1, w + 2, h + 2, 11, outerGlow);
  tft.drawRoundRect(x, y, w, h, 10, border);
  tft.drawRoundRect(x + 2, y + 2, w - 4, h - 4, 8, blend565(bg, accent, 4));

  tft.drawFastHLine(x + 16, y + 5, w - 32, blend565(accent, TFT_WHITE, 1));
  tft.drawFastHLine(x + 20, y + 6, w - 40, blend565(bg, accent, 10));
  tft.drawFastHLine(x + 18, y + h - 4, w - 36, blend565(bg, accent, 8));

  const int iconCy = y + 28;
  drawPremiumIcon(iconType, x + w/2, iconCy, accent, bg);
  drawMenuTileLabel(x, y, w, h, label, bg);
}

void drawMenuFooter(const String &pageText, const String &leftText = "", const String &rightText = "") {
  const int y = 210;
  tft.fillRect(0, y, 320, 30, TFT_BLACK);
  tft.drawFastHLine(10, y, 300, blend565(COL_BG, COL_CYAN, 5));

  int active = 1;
  if (pageText.startsWith("2")) active = 2;
  else if (pageText.startsWith("3")) active = 3;

  const int dotX[3] = {148, 160, 172};
  for (int i = 0; i < 3; ++i) {
    uint16_t c = (i + 1 == active) ? COL_CYAN : blend565(TFT_BLACK, COL_MUTED, 5);
    int r = (i + 1 == active) ? 4 : 3;
    tft.fillCircle(dotX[i], 225, r, c);
    if (i + 1 != active) tft.drawCircle(dotX[i], 225, r + 1, COL_BORDER);
  }

  tft.setTextFont(2);
  tft.setTextSize(1);
  if (leftText.length()) {
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COL_LABEL, TFT_BLACK);
    tft.drawString(leftText, 10, 225);
  }
  if (rightText.length()) {
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(COL_CYAN, TFT_BLACK);
    tft.drawString(rightText, 310, 225);
  }
}

static void drawPremiumMenuHeader(const String &title, bool backButton) {
  // Premium hlavicka menu. V16.24: jen nove PNGdec status ikony, zadne legacy bitmapy.
  tft.fillRect(0, 0, 320, 38, TFT_BLACK);
  uint16_t box  = blend565(COL_BG, COL_CYAN, 1);
  uint16_t edge = blend565(COL_BORDER, COL_CYAN, 7);
  tft.fillRoundRect(4, 4, 312, 30, 8, box);
  tft.drawRoundRect(4, 4, 312, 30, 8, edge);
  tft.drawFastHLine(10, 31, 300, blend565(COL_BG, COL_CYAN, 9));

  int tx = 12;
  if (backButton) {
    tft.drawLine(17, 19, 26, 11, COL_CYAN);
    tft.drawLine(17, 19, 26, 27, COL_CYAN);
    tft.drawFastHLine(18, 19, 12, COL_CYAN);
    tx = 34;
  }

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COL_TEXT, box);
  tft.setTextFont(2);
  tft.setTextSize(1);
  String leftTitle = title;
  // Verejna edice: hlavni lista respektuje text nastaveny uzivatelem ve WEBu.
  tft.drawString(leftTitle, tx, 18);

  // Hodiny uprostred hlavicky.
  String tm = "--:--";
  if (vehicleClockAvailable) tm = vehicleClockText();
  else if (clockIsValid()) {
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char tb[6];
    strftime(tb, sizeof(tb), "%H:%M", &ti);
    tm = String(tb);
  }
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(TFT_WHITE, box);
  tft.drawString(tm, 164, 19);

  // Mala venkovni teplota; zamerne neni dominantni.
  String tempText = "--C";
  if (car.ambientAvailable && !isnan(car.ambientTempC)) {
    tempText = String((int)roundf(car.ambientTempC)) + "C";
  }
  tft.setTextDatum(MR_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, box);
  tft.drawString(tempText, 249, 18);

  // Stav BT a Wi-Fi uz pouze pres nove PNGdec ikony.
  uint16_t btC = car.elmConnected ? COL_GREEN : ((offlineMode || serviceWifiActive) ? COL_MUTED : COL_RED);
  uint16_t wifiC = (serviceWifiActive || wifiStaConnected) ? COL_GREEN : COL_MUTED;

  tft.fillRoundRect(255, 7, 27, 24, 6, box);
  tft.drawRoundRect(255, 7, 27, 24, 6, blend565(box, btC, 9));
  drawUiPngFlash(UI_PNG_STATUS_BLUETOOTH_20, UI_PNG_STATUS_BLUETOOTH_20_LEN,
                 259, 9, box);

  tft.fillRoundRect(286, 7, 27, 24, 6, box);
  tft.drawRoundRect(286, 7, 27, 24, 6, blend565(box, wifiC, 9));
  drawUiPngFlash(UI_PNG_STATUS_WIFI, UI_PNG_STATUS_WIFI_LEN,
                 289, 9, box);

  tft.setTextFont(1);
  tft.setTextSize(1);
}

void drawMainMenu() {
  currentScreen = SCREEN_MENU;
  tft.fillScreen(TFT_BLACK);
  drawPremiumMenuHeader(mainHeaderTitle, false);

  uint16_t dpfAccent = vehicleCaps.advancedDpf ? COL_ORANGE : COL_MUTED;
  uint16_t engineAccent = COL_RED;

  drawMenuTilePremium(6,   42, 100, 78, "PREHLED",   "", 12, COL_CYAN);
  drawMenuTilePremium(110, 42, 100, 78, "MOTOR",     "", 1, engineAccent);
  drawMenuTilePremium(214, 42, 100, 78, "TURBO",     "", 2, COL_BLUE);

  drawMenuTilePremium(6,   126,100, 78, "DPF PRO",   "", 15, dpfAccent);
  drawMenuTilePremium(110, 126,100, 78, "KNIHA JIZD", "", 4, COL_YELLOW);
  drawMenuTilePremium(214, 126,100, 78, "TANKOVANI", "", 14, COL_GREEN);

  drawMenuFooter("1 / 3", "", "DALSI  >");
}

void drawMoreMenu() {
  currentScreen = SCREEN_MENU_MORE;
  tft.fillScreen(TFT_BLACK);
  drawPremiumMenuHeader("FUNKCE", true);

  uint16_t diagAccent = (!car.ecuResponding || dtc.count == 0) ? COL_CYAN : COL_RED;

  drawMenuTilePremium(6,   42, 100, 78, "DIAGNOSTIKA", "", 3, diagAccent);
  drawMenuTilePremium(110, 42, 100, 78, "SENZORY",     "", 6, COL_CYAN);
  drawMenuTilePremium(214, 42, 100, 78, "ELEKTRIKA",   "", 7, COL_YELLOW);

  drawMenuTilePremium(6,   126,100, 78, "SERVIS",      "", 8, COL_GREEN);
  drawMenuTilePremium(110, 126,100, 78, "ALARMY",      "", 13, COL_ORANGE);
  drawMenuTilePremium(214, 126,100, 78, "SPECIALS",    "", 16, vehicleCaps.j519Specials ? COL_RED : COL_MUTED);

  drawMenuFooter("2 / 3", "<  HLAVNI", "SYSTEM  >");
}

void drawSystemMenu() {
  currentScreen = SCREEN_MENU_SYSTEM;
  tft.fillScreen(TFT_BLACK);
  drawPremiumMenuHeader("SYSTEM", true);

  drawMenuTilePremium(8,   48, 148, 72, "WI-FI / WEB", "", 9, COL_CYAN);
  drawMenuTilePremium(164, 48, 148, 72, "POWER",       "", 10, COL_BLUE);
  drawMenuTilePremium(8,   128,148, 72, "NASTAVENI",   "", 11, COL_CYAN);
  drawMenuTilePremium(164, 128,148, 72, "INFO",        "", 17, COL_GREEN);

  drawMenuFooter("3 / 3", "<  FUNKCE", "HLAVNI");
}


// ============================ SENZORY ==========================
// V16.18: hlavni graficke prvky teto obrazovky pouzivaji TFT_eWidget.
// Stranka 1: teplotni trend (motor / sani / venku)
// Stranka 2: lambda trend (pozadovana / skutecna)

static float sensorsGraphX = 0.0f;
static unsigned long sensorsGraphLastMs = 0;

static String sensorFmt(float v, uint8_t dec = 1) {
  return isnan(v) ? String("--") : String(v, (unsigned int)dec);
}

static String sensorRailMpa() {
  return isnan(car.railPressureKpa) ? String("--") : String(car.railPressureKpa / 1000.0f, 1);
}

static String sensorDpfPressure() {
  return (!dpf.diffPressureAvailable || isnan(dpf.diffPressureHpa)) ? String("--") : String(dpf.diffPressureHpa, 1);
}

static int16_t sensorHistoryPack(float v, float scale, float lo, float hi) {
  if (isnan(v) || isinf(v) || v < lo || v > hi) return INT16_MIN;
  long packed = lroundf(v * scale);
  if (packed <= INT16_MIN || packed > INT16_MAX) return INT16_MIN;
  return (int16_t)packed;
}

static float sensorHistoryUnpack(int16_t v, float scale) {
  return v == INT16_MIN ? NAN : ((float)v / scale);
}

void sampleSensorHistory() {
  if (!car.ecuResponding) return;
  const unsigned long now = millis();
  if (sensorHistoryLastSampleMs && now - sensorHistoryLastSampleMs < 1000UL) return;
  sensorHistoryLastSampleMs = now;

  SensorHistorySample &h = sensorHistory[sensorHistoryHead];
  h.coolant10 = sensorHistoryPack(car.coolantC, 10.0f, -40.0f, 150.0f);
  h.intake10 = sensorHistoryPack(car.intakeTempC, 10.0f, -40.0f, 120.0f);
  h.ambient10 = sensorHistoryPack(car.ambientTempC, 10.0f, -50.0f, 80.0f);
  h.lambdaCmd1000 = sensorHistoryPack(car.lambdaCommanded, 1000.0f, 0.3f, 2.0f);
  h.lambdaAct1000 = sensorHistoryPack(car.lambdaB1S1, 1000.0f, 0.3f, 2.0f);
  h.dpfPressure10 = sensorHistoryPack(
      dpf.diffPressureAvailable ? dpf.diffPressureHpa : NAN,
      10.0f, -100.0f, 1000.0f);

  sensorHistoryHead = (sensorHistoryHead + 1) % SENSOR_HISTORY_SAMPLES;
  if (sensorHistoryCount < SENSOR_HISTORY_SAMPLES) sensorHistoryCount++;
}

static void drawSensorCompactRow(int x, int y, int w, const String &label,
                                 const String &value, const String &unit,
                                 uint16_t accent = COL_CYAN) {
  const int h = 20;
  tft.fillRoundRect(x, y, w, h, 5, COL_PANEL);
  tft.drawRoundRect(x, y, w, h, 5, blend565(COL_BORDER, accent, 5));
  tft.fillRoundRect(x + 2, y + 4, 3, h - 8, 1, accent);

  tft.setTextDatum(ML_DATUM);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COL_PANEL);
  tft.drawString(label, x + 8, y + h / 2);

  String out = value;
  if (unit.length() && value != "--") out += unit;
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(accent, COL_PANEL);
  tft.drawString(out, x + w - 6, y + h / 2);
}

static void updateSensorCompactValue(int x, int y, int w,
                                     const String &value, const String &unit,
                                     uint16_t accent = COL_CYAN) {
  const int h = 20;
  const int valueX = x + 82;
  tft.fillRect(valueX, y + 3, w - (valueX - x) - 5, h - 6, COL_PANEL);
  String out = value;
  if (unit.length() && value != "--") out += unit;
  tft.setTextDatum(MR_DATUM);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(accent, COL_PANEL);
  tft.drawString(out, x + w - 6, y + h / 2);
}

static void drawSensorTrendLegend() {
  // Oblast mezi hlavickou a grafem je vyhrazena jen legende.
  tft.fillRect(7, 39, 306, 12, COL_BG);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextDatum(ML_DATUM);

  if (sensorPage == 0) {
    String a = String("MOTOR ") + sensorFmt(car.coolantC,0) + (isnan(car.coolantC) ? "" : "C");
    String b = String("SANI ") + sensorFmt(car.intakeTempC,0) + (isnan(car.intakeTempC) ? "" : "C");
    String c = String("VENKU ") + sensorFmt(car.ambientTempC,0) + (isnan(car.ambientTempC) ? "" : "C");
    tft.setTextColor(COL_GREEN, COL_BG);  tft.drawString(a, 9, 45);
    tft.setTextColor(COL_BLUE, COL_BG);   tft.drawString(b, 112, 45);
    tft.setTextColor(COL_YELLOW, COL_BG); tft.drawString(c, 211, 45);
  } else {
    String a = String("CMD ") + sensorFmt(car.lambdaCommanded,3);
    String b = String("B1S1 ") + sensorFmt(car.lambdaB1S1,3);
    tft.setTextColor(COL_CYAN, COL_BG);  tft.drawString(a, 9, 45);
    tft.setTextColor(COL_GREEN, COL_BG); tft.drawString(b, 160, 45);
  }
}

static void resetSensorsWidgetGraph() {
  const uint16_t graphBg = blend565(TFT_BLACK, COL_PANEL2, 4);
  sensorsTrendGraph.createGraph(306, 62, graphBg);

  if (sensorPage == 0) {
    sensorsTrendGraph.setGraphScale(0.0f, 60.0f, -20.0f, 120.0f);
    sensorsTrendGraph.setGraphGrid(0.0f, 10.0f, -20.0f, 20.0f, COL_BORDER);
    sensorsTrendGraph.drawGraph(7, 52);
    sensorsTraceA.startTrace(COL_GREEN);
    sensorsTraceB.startTrace(COL_BLUE);
    sensorsTraceC.startTrace(COL_YELLOW);
  } else {
    sensorsTrendGraph.setGraphScale(0.0f, 60.0f, 0.50f, 1.50f);
    sensorsTrendGraph.setGraphGrid(0.0f, 10.0f, 0.50f, 0.10f, COL_BORDER);
    sensorsTrendGraph.drawGraph(7, 52);
    sensorsTraceA.startTrace(COL_CYAN);
    sensorsTraceB.startTrace(COL_GREEN);
  }

  // Pri navratu na SENZORY graf nezacina od nuly. Nacteme az poslednich
  // 60 s z maleho RAM ring-bufferu a nechame par sekund mista pro live data.
  uint8_t count = sensorHistoryCount;
  uint8_t oldest = (sensorHistoryHead + SENSOR_HISTORY_SAMPLES - count) % SENSOR_HISTORY_SAMPLES;
  float xStart = count < 50 ? (50.0f - count) : 0.0f;
  if (count > 50) {
    oldest = (sensorHistoryHead + SENSOR_HISTORY_SAMPLES - 50) % SENSOR_HISTORY_SAMPLES;
    count = 50;
  }

  for (uint8_t i = 0; i < count; i++) {
    const SensorHistorySample &h = sensorHistory[(oldest + i) % SENSOR_HISTORY_SAMPLES];
    float x = xStart + i;
    if (sensorPage == 0) {
      float a = sensorHistoryUnpack(h.coolant10, 10.0f);
      float b = sensorHistoryUnpack(h.intake10, 10.0f);
      float c = sensorHistoryUnpack(h.ambient10, 10.0f);
      if (!isnan(a)) sensorsTraceA.addPoint(x, clampFloat(a, -20.0f, 120.0f));
      if (!isnan(b)) sensorsTraceB.addPoint(x, clampFloat(b, -20.0f, 120.0f));
      if (!isnan(c)) sensorsTraceC.addPoint(x, clampFloat(c, -20.0f, 120.0f));
    } else {
      float a = sensorHistoryUnpack(h.lambdaCmd1000, 1000.0f);
      float b = sensorHistoryUnpack(h.lambdaAct1000, 1000.0f);
      if (!isnan(a)) sensorsTraceA.addPoint(x, clampFloat(a, 0.50f, 1.50f));
      if (!isnan(b)) sensorsTraceB.addPoint(x, clampFloat(b, 0.50f, 1.50f));
    }
  }

  sensorsGraphX = count ? (xStart + count - 1) : 0.0f;
  sensorsGraphLastMs = millis();
  drawSensorTrendLegend();
}

static void plotSensorsWidgetGraph() {
  if (millis() - sensorsGraphLastMs < 1000UL) return;
  float dt = (millis() - sensorsGraphLastMs) / 1000.0f;
  sensorsGraphLastMs = millis();
  if (dt < 0.5f || dt > 3.0f) dt = 1.0f;
  sensorsGraphX += dt;

  if (sensorsGraphX > 60.0f) {
    resetSensorsWidgetGraph();
    return;
  }

  if (sensorPage == 0) {
    if (!isnan(car.coolantC))    sensorsTraceA.addPoint(sensorsGraphX, clampFloat(car.coolantC, -20.0f, 120.0f));
    if (!isnan(car.intakeTempC)) sensorsTraceB.addPoint(sensorsGraphX, clampFloat(car.intakeTempC, -20.0f, 120.0f));
    if (!isnan(car.ambientTempC)) sensorsTraceC.addPoint(sensorsGraphX, clampFloat(car.ambientTempC, -20.0f, 120.0f));
  } else {
    if (!isnan(car.lambdaCommanded)) sensorsTraceA.addPoint(sensorsGraphX, clampFloat(car.lambdaCommanded, 0.50f, 1.50f));
    if (!isnan(car.lambdaB1S1))      sensorsTraceB.addPoint(sensorsGraphX, clampFloat(car.lambdaB1S1, 0.50f, 1.50f));
  }
}

void drawSensorsScreen() {
  currentScreen = SCREEN_SENSORS;
  tft.fillScreen(COL_BG);
  drawHeader(sensorPage == 0 ? "SENZORY 1/2" : "SENZORY 2/2", true);
  resetSensorsWidgetGraph();

  const int lx = 7, rx = 162, w = 151;
  const int ys[4] = {118, 139, 160, 181};

  if (sensorPage == 0) {
    drawSensorCompactRow(lx, ys[0], w, "RPM",       String((int)round(car.rpm)), "", rpmColor(car.rpm));
    drawSensorCompactRow(lx, ys[1], w, "ZATIZENI",  sensorFmt(car.engineLoadPct,0), "%", COL_CYAN);
    drawSensorCompactRow(lx, ys[2], w, "MAF",       sensorFmt(car.mafGps,1), "g/s", COL_CYAN);
    drawSensorCompactRow(lx, ys[3], w, "MAP",       sensorFmt(car.mapKpa,0), "kPa", COL_CYAN);

    drawSensorCompactRow(rx, ys[0], w, "RAIL",      sensorRailMpa(), "MPa", COL_ORANGE);
    drawSensorCompactRow(rx, ys[1], w, "SKRTICI",   sensorFmt(car.throttlePct,0), "%", COL_GREEN);
    drawSensorCompactRow(rx, ys[2], w, "NAPETI",    sensorFmt(car.moduleVoltageV,2), "V", COL_YELLOW);
    drawSensorCompactRow(rx, ys[3], w, "PALIVO",    sensorFmt(car.fuelRateLph,2), "l/h", COL_YELLOW);
  } else {
    drawSensorCompactRow(lx, ys[0], w, "EGR CMD",   sensorFmt(car.egrCommandPct,0), "%", COL_GREEN);
    drawSensorCompactRow(lx, ys[1], w, "EGR ERROR", sensorFmt(car.egrErrorPct,0), "%",
                         (!isnan(car.egrErrorPct) && fabsf(car.egrErrorPct) > 20.0f) ? COL_ORANGE : COL_CYAN);
    drawSensorCompactRow(lx, ys[2], w, "O2 B1S1",   sensorFmt(car.o2VoltageB1S1,2), "V", COL_GREEN);
    drawSensorCompactRow(lx, ys[3], w, "BARO",      sensorFmt(car.baroKpa,0), "kPa", COL_CYAN);

    drawSensorCompactRow(rx, ys[0], w, "DPF DELTA", sensorDpfPressure(), "hPa", COL_ORANGE);
    drawSensorCompactRow(rx, ys[1], w, "EGT PRE",   sensorFmt(dpf.egtPreC,0), "C", COL_ORANGE);
    drawSensorCompactRow(rx, ys[2], w, "SAZE CALC", sensorFmt(dpf.sootCalculatedG,1), "g", COL_ORANGE);
    drawSensorCompactRow(rx, ys[3], w, "SAZE MER",  sensorFmt(dpf.sootMeasuredG,1), "g", COL_ORANGE);
  }

  tft.fillRect(0, 207, 320, 33, COL_BG);
  tft.drawFastHLine(8, 207, 304, COL_BORDER);
  tft.setTextFont(2); tft.setTextSize(1);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(TFT_WHITE, COL_BG);
  tft.drawString("< ZPET", 10, 224);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("HISTORIE 60 s", 160, 224);
  tft.setTextDatum(MR_DATUM); tft.setTextColor(COL_CYAN, COL_BG);
  tft.drawString(sensorPage == 0 ? "DALSI >" : "< PREDCHOZI", 310, 224);
}

void updateSensorsDynamic() {
  if (currentScreen != SCREEN_SENSORS) return;
  updateHeaderStatus();
  plotSensorsWidgetGraph();

  static unsigned long lastLegendUpdate = 0;
  if (millis() - lastLegendUpdate >= 500UL) {
    lastLegendUpdate = millis();
    drawSensorTrendLegend();
  }

  const int lx = 7, rx = 162, w = 151;
  const int ys[4] = {118, 139, 160, 181};

  if (sensorPage == 0) {
    updateSensorCompactValue(lx,ys[0],w,String((int)round(car.rpm)),"",rpmColor(car.rpm));
    updateSensorCompactValue(lx,ys[1],w,sensorFmt(car.engineLoadPct,0),"%",COL_CYAN);
    updateSensorCompactValue(lx,ys[2],w,sensorFmt(car.mafGps,1),"g/s",COL_CYAN);
    updateSensorCompactValue(lx,ys[3],w,sensorFmt(car.mapKpa,0),"kPa",COL_CYAN);

    updateSensorCompactValue(rx,ys[0],w,sensorRailMpa(),"MPa",COL_ORANGE);
    updateSensorCompactValue(rx,ys[1],w,sensorFmt(car.throttlePct,0),"%",COL_GREEN);
    updateSensorCompactValue(rx,ys[2],w,sensorFmt(car.moduleVoltageV,2),"V",COL_YELLOW);
    updateSensorCompactValue(rx,ys[3],w,sensorFmt(car.fuelRateLph,2),"l/h",COL_YELLOW);
  } else {
    updateSensorCompactValue(lx,ys[0],w,sensorFmt(car.egrCommandPct,0),"%",COL_GREEN);
    updateSensorCompactValue(lx,ys[1],w,sensorFmt(car.egrErrorPct,0),"%",
                             (!isnan(car.egrErrorPct)&&fabsf(car.egrErrorPct)>20.0f)?COL_ORANGE:COL_CYAN);
    updateSensorCompactValue(lx,ys[2],w,sensorFmt(car.o2VoltageB1S1,2),"V",COL_GREEN);
    updateSensorCompactValue(lx,ys[3],w,sensorFmt(car.baroKpa,0),"kPa",COL_CYAN);

    updateSensorCompactValue(rx,ys[0],w,sensorDpfPressure(),"hPa",COL_ORANGE);
    updateSensorCompactValue(rx,ys[1],w,sensorFmt(dpf.egtPreC,0),"C",COL_ORANGE);
    updateSensorCompactValue(rx,ys[2],w,sensorFmt(dpf.sootCalculatedG,1),"g",COL_ORANGE);
    updateSensorCompactValue(rx,ys[3],w,sensorFmt(dpf.sootMeasuredG,1),"g",COL_ORANGE);
  }
}

// ============================ SPECIALS =========================

static void setSpecialsStatus(const String &s, const String &reply = "") {
  if (diagMutex && xSemaphoreTake(diagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    specialsStatus = s;
    if (reply.length()) specialsLastElmReply = reply;
    xSemaphoreGive(diagMutex);
  } else {
    specialsStatus = s;
    if (reply.length()) specialsLastElmReply = reply;
  }
  specialsNeedsRedraw = true;
}

static String getSpecialsStatus() {
  String s;
  if (diagMutex && xSemaphoreTake(diagMutex, pdMS_TO_TICKS(60)) == pdTRUE) {
    s = specialsStatus;
    xSemaphoreGive(diagMutex);
  } else {
    s = specialsStatus;
  }
  return s;
}

static String cleanElmHex(String s) {
  s.toUpperCase();
  s.replace(" ", "");
  s.replace("\r", "");
  s.replace("\n", "");
  s.replace(">", "");
  return s;
}

static String compactElmReply(const String &r) {
  String s = cleanElmHex(r);
  if (s.length() > 30) s = s.substring(0, 30);
  return s;
}

static bool specialUdsPositive(const String &r, const char *did) {
  String needle = String("6F") + did;
  return cleanElmHex(r).indexOf(needle) >= 0;
}

// Aktivuje jeden J519 vystup pomoci UDS InputOutputControlByIdentifier (0x2F).
// U VAG se stejny identifikator v label souborech objevuje jako T967/T968.
// Ruzne BCM mohou pouzivat jinou reprezentaci 100 %; pri prvnim ON proto
// bezpecne zkousime pouze tri hodnoty PRO STEJNY vystup a uspesny format si
// zapamatujeme. Na zadny jiny DID firmware nesaha.
static bool specialJ519Output(const char *did, bool on) {
  if (!on) {
    String r = elmCommand(String("2F") + did + "00", 1000, true); // returnControlToECU
    setSpecialsStatus(getSpecialsStatus(), compactElmReply(r));
    return specialUdsPositive(r, did);
  }

  const char *values[] = {"FF", "64", "01"};
  int start = 0, end = 3;
  if (specialsIoValueMode >= 1 && specialsIoValueMode <= 3) {
    start = specialsIoValueMode - 1;
    end = start + 1;
  }

  for (int i = start; i < end; ++i) {
    String r = elmCommand(String("2F") + did + "03" + values[i], 1200, true); // shortTermAdjustment
    setSpecialsStatus(getSpecialsStatus(), compactElmReply(r));
    if (specialUdsPositive(r, did)) {
      specialsIoValueMode = (uint8_t)(i + 1);
      return true;
    }
  }
  return false;
}

static void specialReleaseBoth() {
  specialJ519Output(SPECIAL_LEFT_DID, false);
  specialJ519Output(SPECIAL_RIGHT_DID, false);
}

static bool specialPulseOne(const char *did, uint32_t onMs, uint32_t gapMs) {
  if (!specialJ519Output(did, true)) return false;
  vTaskDelay(pdMS_TO_TICKS(onMs));
  if (!specialJ519Output(did, false)) return false;
  if (gapMs) vTaskDelay(pdMS_TO_TICKS(gapMs));
  return true;
}

static bool specialPulseHazard(uint32_t onMs, uint32_t gapMs) {
  // "Vystrazne" zde nevytvarime stiskem fyzickeho tlacitka. J519 dostane
  // soucasne diagnosticky povel pro levou i pravou stranu.
  if (!specialJ519Output(SPECIAL_LEFT_DID, true)) return false;
  if (!specialJ519Output(SPECIAL_RIGHT_DID, true)) {
    specialJ519Output(SPECIAL_LEFT_DID, false);
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(onMs));
  bool l = specialJ519Output(SPECIAL_LEFT_DID, false);
  bool r = specialJ519Output(SPECIAL_RIGHT_DID, false);
  if (gapMs) vTaskDelay(pdMS_TO_TICKS(gapMs));
  return l && r;
}

// Bezi vyhradne v obdBackgroundTask(). UI zustava responzivni, ale po dobu
// nekolika sekund je normalni motorovy polling pozastaven.
static bool runThankYouElmSequence() {
  if (!vehicleCaps.j519Specials) {
    setSpecialsStatus("SPECIALS: PROFIL NEOVEREN");
    return false;
  }
  if (!car.elmConnected) {
    setSpecialsStatus("ELM NENI PRIPOJENO");
    return false;
  }
  if (car.speedKmh > 0.5f) {
    setSpecialsStatus("TEST JEN PRI STANI");
    return false;
  }
  if (car.rpm > 200.0f) {
    setSpecialsStatus("PRO TEST VYPNI MOTOR");
    return false;
  }

  specialsRunning = true;
  specialsIoValueMode = 0;
  setSpecialsStatus("J519: PRIPOJUJI...");

  bool ok = false;
  if (!setDiagnosticAddress(SPECIAL_J519_TX, SPECIAL_J519_RX)) {
    setSpecialsStatus("J519: HEADER ERROR");
    goto cleanup;
  }

  // Extended session. Nektere BCM ji pro Output Test vyzaduji, jine povel
  // odmitnou, ale 0x2F povoli i v default session. Proto 1003 neni hard fail.
  {
    String session = elmCommand("1003", 1200, true);
    String cs = cleanElmHex(session);
    if (cs.indexOf("5003") >= 0) setSpecialsStatus("J519: SESSION OK", compactElmReply(session));
    else setSpecialsStatus("J519: ZKOUSIM OUTPUT", compactElmReply(session));
  }

  // Nejprve vzdy vratime oba vystupy do rizeni ECU.
  specialReleaseBoth();

  setSpecialsStatus("L");
  if (!specialPulseOne(SPECIAL_LEFT_DID, SPECIAL_FAST_ON_MS, SPECIAL_FAST_GAP_MS)) goto output_fail;
  setSpecialsStatus("P");
  if (!specialPulseOne(SPECIAL_RIGHT_DID, SPECIAL_FAST_ON_MS, SPECIAL_FAST_GAP_MS)) goto output_fail;

  setSpecialsStatus("VYSTRAHY 1/2");
  if (!specialPulseHazard(SPECIAL_HAZARD_ON_MS, SPECIAL_HAZARD_GAP_MS)) goto output_fail;
  setSpecialsStatus("VYSTRAHY 2/2");
  if (!specialPulseHazard(SPECIAL_HAZARD_ON_MS, SPECIAL_HAZARD_GAP_MS)) goto output_fail;

  setSpecialsStatus("P");
  if (!specialPulseOne(SPECIAL_RIGHT_DID, SPECIAL_FAST_ON_MS, SPECIAL_FAST_GAP_MS)) goto output_fail;
  setSpecialsStatus("L");
  if (!specialPulseOne(SPECIAL_LEFT_DID, SPECIAL_FAST_ON_MS, SPECIAL_FAST_GAP_MS)) goto output_fail;

  setSpecialsStatus("VYSTRAHA");
  if (!specialPulseHazard(SPECIAL_HAZARD_ON_MS, 0)) goto output_fail;

  ok = true;
  setSpecialsStatus("HOTOVO");
  goto cleanup;

output_fail:
  setSpecialsStatus(String("J519 ODMITL  ") + compactElmReply(specialsLastElmReply));

cleanup:
  specialReleaseBoth();
  elmCommand("1001", 700, true); // default diagnostic session, pokud ji BCM podporuje
  restoreEngineDiagnosticAddress();
  specialsRunning = false;
  specialsNeedsRedraw = true;
  return ok;
}

void startThankYouSequence() {
  if (specialsRunning || specialsRequestPending) return;
  if (!car.elmConnected) {
    setSpecialsStatus("ELM NENI PRIPOJENO");
    return;
  }
  if (car.speedKmh > 0.5f) {
    setSpecialsStatus("ZASTAV VOZIDLO");
    return;
  }
  if (car.rpm > 200.0f) {
    setSpecialsStatus("VYPNI MOTOR, KLICK ON");
    return;
  }
  setSpecialsStatus("CEKAM NA OBD TASK...");
  specialsRequestPending = true;
}

void cancelSpecialsSequence() {
  // V16.16: sekvence je diagnosticka a kratka; tlacitko BACK je behem ni blokovane.
}

void updateSpecialsSequence() {
  // Sekvenci provadi OBD task, zde neni zadne GPIO ani casovac vystupu.
}

void drawSpecialsScreen() {
  currentScreen = SCREEN_SPECIALS;
  tft.fillScreen(TFT_BLACK);
  drawPremiumMenuHeader("SPECIALS", true);

  const uint16_t panel = blend565(COL_BG, COL_RED, 1);
  tft.fillRoundRect(12, 48, 296, 62, 12, panel);
  tft.drawRoundRect(12, 48, 296, 62, 12, blend565(COL_BORDER, COL_RED, 9));
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, panel);
  tft.drawString("PODEKOVANI RIDICI", 160, 64);
  tft.setTextFont(1);
  tft.setTextColor(COL_MUTED, panel);
  tft.drawString("L > P > OBA x2 > P > L > OBA x1", 160, 91);

  bool safe = car.elmConnected && car.speedKmh <= 0.5f && car.rpm <= 200.0f;
  bool busy = specialsRunning || specialsRequestPending;
  uint16_t buttonColor = busy ? COL_ORANGE : (safe ? COL_GREEN : COL_RED);
  tft.fillRoundRect(40, 123, 240, 58, 14, blend565(COL_BG, buttonColor, 4));
  tft.drawRoundRect(40, 123, 240, 58, 14, buttonColor);
  tft.drawRoundRect(43, 126, 234, 52, 11, blend565(COL_BG, buttonColor, 10));
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(TFT_WHITE, blend565(COL_BG, buttonColor, 4));
  tft.drawString(busy ? "PROBIHA..." : "PODEKOVAT!", 160, 151);

  tft.setTextFont(1);
  tft.setTextColor(safe ? COL_GREEN : COL_ORANGE, TFT_BLACK);
  tft.drawString(getSpecialsStatus(), 160, 195);
  tft.setTextColor(COL_MUTED, TFT_BLACK);
  tft.drawString("TEST: stani, zapalovani ON, motor OFF", 160, 207);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COL_CYAN, TFT_BLACK);
  tft.drawString("< FUNKCE", 10, 226);
  tft.setTextDatum(MC_DATUM);
}

void updateSpecialsDynamic() {
  if (!specialsNeedsRedraw || currentScreen != SCREEN_SPECIALS) return;
  specialsNeedsRedraw = false;
  drawSpecialsScreen();
}

// ============================== DPF ===========================

static float clampFloat(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static bool telemetryValueValid(float v, float lo, float hi) {
  if (isnan(v) || isinf(v) || v < lo || v > hi) {
    telemetryRejectedValues++;
    return false;
  }
  return true;
}

static void refreshDpfPredictionHistory(bool force = false) {
  const unsigned long now = millis();
  if (!force && dpfHistoryCacheMs && now - dpfHistoryCacheMs < 60000UL) return;
  dpfHistoryCacheMs = now;
  dpfHistoricalIntervalKm = NAN;
  dpfHistoricalIntervalCount = 0;
  if (!storageReady) return;

  fs::File f = LittleFS.open("/dpf.csv", "r");
  if (!f) return;
  float recent[8] = {0};
  uint8_t count = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;
    int c1 = line.indexOf(',');
    int c2 = c1 >= 0 ? line.indexOf(',', c1 + 1) : -1;
    int c3 = c2 >= 0 ? line.indexOf(',', c2 + 1) : -1;
    if (c3 < 0) continue;
    float km = line.substring(c3 + 1).toFloat();
    if (!telemetryValueValid(km, 30.0f, 2000.0f)) continue;
    if (count < 8) recent[count++] = km;
    else {
      for (uint8_t i = 0; i < 7; i++) recent[i] = recent[i + 1];
      recent[7] = km;
    }
  }
  f.close();
  if (!count) return;

  // Novejsi intervaly maji vetsi vahu, proto se predikce postupne prizpusobuje
  // realnemu stylu jizdy, ale jeden neobvykly interval ji nerozhodi.
  float weighted = 0.0f, weights = 0.0f;
  for (uint8_t i = 0; i < count; i++) {
    float w = 1.0f + 0.35f * i;
    weighted += recent[i] * w;
    weights += w;
  }
  dpfHistoricalIntervalKm = weighted / weights;
  dpfHistoricalIntervalCount = count;
}

void drawArcSegment(int cx, int cy, int rOuter, int thickness,
                    float startDeg, float endDeg, uint16_t color) {
  if (endDeg < startDeg) return;
  int rInner = rOuter - thickness;
  for (float a = startDeg; a <= endDeg; a += 3.0f) {
    float rad = a * DEG_TO_RAD;
    int x1 = cx + (int)(cos(rad) * rInner);
    int y1 = cy + (int)(sin(rad) * rInner);
    int x2 = cx + (int)(cos(rad) * rOuter);
    int y2 = cy + (int)(sin(rad) * rOuter);
    tft.drawLine(x1, y1, x2, y2, color);
  }
}

float dpfSootPercent() {
  if (!dpf.available || isnan(dpf.sootCalculatedG) || dpfRegenLimitG <= 0.1f) return NAN;
  return clampFloat(dpf.sootCalculatedG / dpfRegenLimitG * 100.0f, 0.0f, 150.0f);
}

float dpfLoadingPercent() {
  if (!dpf.available) return NAN;
  float calc = isnan(dpf.sootCalculatedG) ? 0.0f : max(0.0f, dpf.sootCalculatedG);
  float measured = isnan(dpf.sootMeasuredG) ? 0.0f : max(0.0f, dpf.sootMeasuredG);
  float effective = max(calc, measured);
  if (dpfRegenLimitG <= 0.1f) return NAN;
  return clampFloat(effective / dpfRegenLimitG * 100.0f, 0.0f, 150.0f);
}

float dpfEstimatedKmToRegen() {
  if (!dpf.available || isnan(dpf.kmSinceRegen)) return NAN;
  if (dpf.regenActive) return 0.0f;

  refreshDpfPredictionHistory(false);

  float sootEstimate = NAN;
  if (!isnan(dpf.sootCalculatedG)) {
    if (dpf.sootCalculatedG >= dpfRegenLimitG) return 0.0f;
    if (dpf.kmSinceRegen >= 8.0f && dpf.sootCalculatedG > DEFAULT_DPF_BASELINE_G + 0.25f) {
      float slope = (dpf.sootCalculatedG - DEFAULT_DPF_BASELINE_G) / dpf.kmSinceRegen;
      if (slope > 0.0005f)
        sootEstimate = clampFloat((dpfRegenLimitG - dpf.sootCalculatedG) / slope, 0.0f, 2000.0f);
    }
  }

  float historyEstimate = NAN;
  if (!isnan(dpfHistoricalIntervalKm) && dpfHistoricalIntervalCount >= 2)
    historyEstimate = clampFloat(dpfHistoricalIntervalKm - dpf.kmSinceRegen, 0.0f, 2000.0f);

  if (!isnan(sootEstimate) && !isnan(historyEstimate)) {
    // Aktualni rust sazi je primarni signal, historie vyhlazuje kratkodobe vykyvy.
    return clampFloat(sootEstimate * 0.68f + historyEstimate * 0.32f, 0.0f, 2000.0f);
  }
  if (!isnan(sootEstimate)) return sootEstimate;
  return historyEstimate;
}

static uint16_t dpfStateColor(float pct) {
  if (dpf.regenActive) return COL_ORANGE;
  if (isnan(pct)) return COL_MUTED;
  if (pct >= 90.0f) return COL_ORANGE;
  if (pct >= 70.0f) return COL_YELLOW;
  return COL_GREEN;
}

static uint16_t dpfEstimateColor(float kmLeft) {
  if (dpf.regenActive) return COL_ORANGE;
  if (isnan(kmLeft)) return COL_MUTED;
  if (kmLeft <= 10.0f) return COL_ORANGE;
  if (kmLeft <= 50.0f) return COL_YELLOW;
  return COL_GREEN;
}

static void drawDpfFilterVisual(int x, int y, int w, int h, float pct, uint16_t accent) {
  // Stylizovana vlozka filtru. Jednotlive kanalky se postupne vyplnuji podle
  // aktualniho zaneseni. Bez sprite/framebufferu = minimalni narok na RAM.
  const int pipeW = 10;
  const int bodyX = x + pipeW;
  const int bodyW = w - pipeW * 2;
  const int midY = y + h / 2;

  tft.fillRoundRect(x, midY - 5, pipeW + 3, 10, 3, COL_PANEL2);
  tft.fillRoundRect(x + w - pipeW - 3, midY - 5, pipeW + 3, 10, 3, COL_PANEL2);
  tft.fillRoundRect(bodyX, y, bodyW, h, 8, COL_PANEL2);
  tft.drawRoundRect(bodyX, y, bodyW, h, 8, blend565(COL_BORDER, accent, 7));

  const int cells = 12;
  const int innerX = bodyX + 8;
  const int innerY = y + 7;
  const int innerW = bodyW - 16;
  const int innerH = h - 14;
  const int gap = 2;
  const int cw = max(3, (innerW - (cells - 1) * gap) / cells);
  int active = isnan(pct) ? -1 : (int)roundf(clampFloat(pct / 100.0f, 0.0f, 1.0f) * cells);

  for (int i = 0; i < cells; i++) {
    int cx = innerX + i * (cw + gap);
    uint16_t fill = i < active ? accent : COL_BG;
    tft.fillRoundRect(cx, innerY, cw, innerH, 2, fill);
    tft.drawRoundRect(cx, innerY, cw, innerH, 2, COL_BORDER);
  }
}

static void drawDpfPressureTrend(int x, int y, int w, int h) {
  tft.fillRect(x, y, w, h, COL_PANEL);
  if (sensorHistoryCount < 2) return;

  uint8_t count = sensorHistoryCount < 40 ? sensorHistoryCount : 40;
  uint8_t oldest = (sensorHistoryHead + SENSOR_HISTORY_SAMPLES - count) % SENSOR_HISTORY_SAMPLES;
  float maxV = 20.0f;
  bool any = false;
  for (uint8_t i = 0; i < count; i++) {
    float v = sensorHistoryUnpack(sensorHistory[(oldest + i) % SENSOR_HISTORY_SAMPLES].dpfPressure10, 10.0f);
    if (!isnan(v) && v >= 0.0f) { maxV = max(maxV, v * 1.15f); any = true; }
  }
  if (!any) return;

  int lastX = -1, lastY = -1;
  for (uint8_t i = 0; i < count; i++) {
    float v = sensorHistoryUnpack(sensorHistory[(oldest + i) % SENSOR_HISTORY_SAMPLES].dpfPressure10, 10.0f);
    if (isnan(v) || v < 0.0f) { lastX = -1; continue; }
    int px = x + (count <= 1 ? 0 : (int)((long)i * (w - 1) / (count - 1)));
    int py = y + h - 1 - (int)(clampFloat(v / maxV, 0.0f, 1.0f) * (h - 2));
    if (lastX >= 0) tft.drawLine(lastX, lastY, px, py, COL_YELLOW);
    lastX = px; lastY = py;
  }
}

static void drawDpfBottomMetric(int x, int y, int w, const String &label,
                                const String &value, uint16_t accent) {
  const int h = 35;
  tft.fillRoundRect(x, y, w, h, 7, COL_PANEL2);
  tft.drawRoundRect(x, y, w, h, 7, COL_BORDER);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COL_LABEL, COL_PANEL2);
  tft.drawString(label, x + 6, y + 3);
  tft.setTextDatum(BR_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(accent, COL_PANEL2);
  tft.drawString(value, x + w - 6, y + h - 3);
  tft.setTextFont(1);
}

static void drawDpfLoadingCard() {
  const int x = 8, y = 46, w = 150, h = 149;
  float pct = dpfLoadingPercent();
  uint16_t accent = dpfStateColor(pct);

  tft.fillRoundRect(x, y, w, h, 10, COL_PANEL);
  tft.drawRoundRect(x, y, w, h, 10, COL_BORDER);
  tft.fillRoundRect(x + 1, y + 1, w - 2, 4, 2, accent);

  tft.setTextDatum(TC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COL_LABEL, COL_PANEL);
  tft.drawString("ZANESENI DPF", x + w / 2, y + 8);

  drawDpfFilterVisual(x + 14, y + 28, w - 28, 37, pct, accent);

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(accent, COL_PANEL);
  tft.drawString(isnan(pct) ? "--" : String(pct, 0) + "%", x + w / 2, y + 88);

  String eff = "--";
  if (!isnan(dpf.sootCalculatedG)) eff = String(dpf.sootCalculatedG, 1) + " / " + String(dpfRegenLimitG, 0) + " g";
  tft.setTextFont(2);
  tft.setTextColor(COL_TEXT, COL_PANEL);
  tft.drawString(eff, x + w / 2, y + 113);

  tft.setTextDatum(BL_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COL_CYAN, COL_PANEL);
  tft.drawString(String("CALC ") + (isnan(dpf.sootCalculatedG) ? String("--") : String(dpf.sootCalculatedG, 1) + " g"), x + 8, y + h - 4);
  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(COL_YELLOW, COL_PANEL);
  tft.drawString(String("MER ") + (isnan(dpf.sootMeasuredG) ? String("--") : String(dpf.sootMeasuredG, 1) + " g"), x + w - 8, y + h - 4);
  tft.setTextFont(1);
}

static void drawDpfEstimateCard() {
  const int x = 164, y = 46, w = 148, h = 54;
  float kmLeft = dpfEstimatedKmToRegen();
  uint16_t accent = dpfEstimateColor(kmLeft);
  tft.fillRoundRect(x, y, w, h, 9, COL_PANEL);
  tft.drawRoundRect(x, y, w, h, 9, COL_BORDER);
  tft.setTextDatum(TC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COL_LABEL, COL_PANEL);
  tft.drawString(dpf.regenActive ? "REGENERACE" : "ODHAD DO REGEN.", x + w / 2, y + 6);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(accent, COL_PANEL);
  String value = dpf.regenActive ? "AKTIVNI" : (isnan(kmLeft) ? "--" : String("~") + String(kmLeft, 0) + " km");
  tft.drawString(value, x + w / 2, y + 36);
  tft.setTextFont(1);
}

static void drawDpfPressureCard() {
  const int x = 164, y = 104, w = 148, h = 45;
  tft.fillRoundRect(x, y, w, h, 8, COL_PANEL);
  tft.drawRoundRect(x, y, w, h, 8, COL_BORDER);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COL_LABEL, COL_PANEL);
  tft.drawString("DIF. TLAK", x + 7, y + 4);
  tft.setTextFont(2);
  tft.setTextColor(dpf.diffPressureAvailable ? COL_YELLOW : COL_MUTED, COL_PANEL);
  String p = dpf.diffPressureAvailable && !isnan(dpf.diffPressureHpa) ? String(dpf.diffPressureHpa, 1) + " hPa" : "--";
  tft.drawString(p, x + 7, y + 20);
  drawDpfPressureTrend(x + 84, y + 18, 56, 20);
  tft.setTextFont(1);
}

static void drawDpfSinceCard() {
  const int x = 164, y = 153, w = 148, h = 42;
  tft.fillRoundRect(x, y, w, h, 8, COL_PANEL);
  tft.drawRoundRect(x, y, w, h, 8, COL_BORDER);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COL_LABEL, COL_PANEL);
  tft.drawString("OD POSL. REGEN.", x + 7, y + 4);
  tft.setTextFont(2);
  tft.setTextColor(COL_GREEN, COL_PANEL);
  String km = isnan(dpf.kmSinceRegen) ? "--" : String(dpf.kmSinceRegen, 0) + " km";
  tft.drawString(km, x + 7, y + 20);
  tft.setTextFont(1);
}

static void drawDpfNormalBody() {
  drawDpfLoadingCard();
  drawDpfEstimateCard();
  drawDpfPressureCard();
  drawDpfSinceCard();
  drawButton(8, 201, 92, 35, "HISTORIE");
  drawDpfBottomMetric(104, 201, 100, "EGT PRE",
                      isnan(dpf.egtPreC) ? "--" : String(dpf.egtPreC, 0) + " C",
                      isnan(dpf.egtPreC) ? COL_MUTED : COL_ORANGE);
  drawDpfBottomMetric(208, 201, 104, "EGT POST",
                      isnan(dpf.egtPostC) ? "--" : String(dpf.egtPostC, 0) + " C",
                      isnan(dpf.egtPostC) ? COL_MUTED : COL_ORANGE);
}

static void drawDpfRegenBanner() {
  const int x = 8, y = 46, w = 304, h = 34;
  tft.fillRoundRect(x, y, w, h, 9, blend565(COL_PANEL, COL_ORANGE, 5));
  tft.drawRoundRect(x, y, w, h, 9, COL_ORANGE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COL_ORANGE, blend565(COL_PANEL, COL_ORANGE, 5));
  tft.drawString("DPF REGENERACE AKTIVNI", x + w / 2, y + h / 2);
  tft.setTextFont(1);
}

static void drawDpfRegenSootCard() {
  const int x = 8, y = 86, w = 148, h = 109;
  float pct = dpfLoadingPercent();
  tft.fillRoundRect(x, y, w, h, 9, COL_PANEL);
  tft.drawRoundRect(x, y, w, h, 9, COL_BORDER);
  tft.setTextDatum(TC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COL_LABEL, COL_PANEL);
  tft.drawString("SAZE V DPF", x + w / 2, y + 7);
  drawDpfFilterVisual(x + 13, y + 24, w - 26, 34, pct, COL_ORANGE);

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(COL_ORANGE, COL_PANEL);
  tft.drawString(isnan(dpf.sootCalculatedG) ? "--" : String(dpf.sootCalculatedG, 1) + " g", x + w / 2, y + 75);

  String delta = "UBYTEK --";
  if (regenSessionRunning && !isnan(regenSootBefore) && !isnan(dpf.sootCalculatedG)) {
    float removed = regenSootBefore - dpf.sootCalculatedG;
    delta = String("UBYTEK ") + String(max(0.0f, removed), 1) + " g";
  }
  tft.setTextFont(2);
  tft.setTextColor(COL_GREEN, COL_PANEL);
  tft.drawString(delta, x + w / 2, y + h - 11);
}

static void drawDpfRegenStatsCard() {
  const int x = 162, y = 86, w = 150, h = 109;
  tft.fillRoundRect(x, y, w, h, 9, COL_PANEL);
  tft.drawRoundRect(x, y, w, h, 9, COL_BORDER);

  unsigned long sec = regenSessionRunning && regenStartMs ? (millis() - regenStartMs) / 1000UL : 0;
  String duration = regenSessionRunning ? formatDuration(sec) : "--";
  String dist = regenSessionRunning ? String(regenDistanceKm, 1) + " km" : "--";
  String pressure = dpf.diffPressureAvailable && !isnan(dpf.diffPressureHpa) ? String(dpf.diffPressureHpa, 1) + " hPa" : "--";

  const int ys[3] = {101, 132, 163};
  const char *labels[3] = {"TRVANI", "UJETO", "DIF. TLAK"};
  String vals[3] = {duration, dist, pressure};
  uint16_t cols[3] = {COL_ORANGE, COL_GREEN, dpf.diffPressureAvailable ? COL_YELLOW : COL_MUTED};
  for (int i = 0; i < 3; i++) {
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(COL_LABEL, COL_PANEL);
    tft.drawString(labels[i], x + 8, ys[i]);
    tft.setTextDatum(MR_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(cols[i], COL_PANEL);
    tft.drawString(vals[i], x + w - 8, ys[i]);
  }
  tft.setTextFont(1);
}

static void drawDpfRegenBody() {
  drawDpfRegenBanner();
  drawDpfRegenSootCard();
  drawDpfRegenStatsCard();
  drawButton(8, 201, 92, 31, "HISTORIE");
  drawDpfBottomMetric(104, 201, 100, "EGT PRE",
                      isnan(dpf.egtPreC) ? "--" : String(dpf.egtPreC, 0) + " C",
                      isnan(dpf.egtPreC) ? COL_MUTED : COL_ORANGE);
  drawDpfBottomMetric(208, 201, 104, "EGT POST",
                      isnan(dpf.egtPostC) ? "--" : String(dpf.egtPostC, 0) + " C",
                      isnan(dpf.egtPostC) ? COL_MUTED : COL_ORANGE);
}

static void drawDpfUnsupportedBody() {
  tft.fillRoundRect(12, 52, 296, 132, 10, COL_PANEL);
  tft.drawRoundRect(12, 52, 296, 132, 10, COL_YELLOW);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COL_YELLOW, COL_PANEL);
  tft.drawString("ROZSIRENE DPF NENI OVERENO", 160, 76);
  tft.setTextColor(COL_TEXT, COL_PANEL);
  tft.drawString(String(engineProfileLabel(activeEngineProfile)), 160, 103);
  tft.setTextFont(1);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.drawString("OBD data motoru zustavaji aktivni.", 160, 128);
  tft.drawString("DPF saze/regen zobrazime az po overeni mapy ECU.", 160, 145);
  tft.setTextFont(2);
  tft.setTextColor(dpf.diffPressureAvailable ? COL_GREEN : COL_MUTED, COL_PANEL);
  String pressure = dpf.diffPressureAvailable && !isnan(dpf.diffPressureHpa) ? String(dpf.diffPressureHpa, 1) + " hPa" : "PID 7A: --";
  tft.drawString(pressure, 160, 166);
  drawButton(74, 202, 172, 32, "ZPET / MENU");
}

void drawDpfScreen() {
  currentScreen = SCREEN_DPF;
  tft.fillScreen(COL_BG);
  drawHeader("DPF PRO", true);
  if (!vehicleHasAdvancedDpf()) { drawDpfUnsupportedBody(); return; }
  if (dpf.regenActive) drawDpfRegenBody();
  else drawDpfNormalBody();
}

void updateDpfDynamic() {
  if (currentScreen != SCREEN_DPF) return;
  if (!vehicleHasAdvancedDpf()) {
    updateHeaderStatus();
    static unsigned long lastUnsupportedRefresh = 0;
    if (millis() - lastUnsupportedRefresh > 1200UL) { lastUnsupportedRefresh = millis(); drawDpfUnsupportedBody(); }
    return;
  }
  updateHeaderStatus();

  static bool lastMode = false;
  static bool modeInit = false;
  static String lastLoadingKey = "";
  static String lastEstimateKey = "";
  static String lastPressureKey = "";
  static String lastSinceKey = "";
  static String lastEgtKey = "";
  static String lastRegenKey = "";

  if (!modeInit || lastMode != dpf.regenActive) {
    lastMode = dpf.regenActive;
    modeInit = true;
    lastLoadingKey = lastEstimateKey = lastPressureKey = lastSinceKey = lastEgtKey = lastRegenKey = "";
    drawDpfScreen();
    return;
  }

  if (dpf.regenActive) {
    String regenKey = String(isnan(dpf.sootCalculatedG) ? -999.0f : dpf.sootCalculatedG, 1) + ":" +
                      String(regenSessionRunning ? (millis() - regenStartMs) / 1000UL : 0) + ":" +
                      String(regenDistanceKm, 1) + ":" +
                      String(dpf.diffPressureAvailable && !isnan(dpf.diffPressureHpa) ? dpf.diffPressureHpa : -999.0f, 1);
    if (regenKey != lastRegenKey) {
      drawDpfRegenSootCard();
      drawDpfRegenStatsCard();
      lastRegenKey = regenKey;
    }
  } else {
    String loadingKey = String(isnan(dpf.sootCalculatedG) ? -999.0f : dpf.sootCalculatedG, 1) + ":" +
                        String(isnan(dpf.sootMeasuredG) ? -999.0f : dpf.sootMeasuredG, 1);
    if (loadingKey != lastLoadingKey) { drawDpfLoadingCard(); lastLoadingKey = loadingKey; }

    float kmLeft = dpfEstimatedKmToRegen();
    String estimateKey = isnan(kmLeft) ? "--" : String(kmLeft, 0);
    if (estimateKey != lastEstimateKey) { drawDpfEstimateCard(); lastEstimateKey = estimateKey; }

    String pressureKey = String(dpf.diffPressureAvailable && !isnan(dpf.diffPressureHpa) ? dpf.diffPressureHpa : -999.0f, 1) + ":" + String(sensorHistoryHead);
    if (pressureKey != lastPressureKey) { drawDpfPressureCard(); lastPressureKey = pressureKey; }

    String sinceKey = isnan(dpf.kmSinceRegen) ? "--" : String(dpf.kmSinceRegen, 0);
    if (sinceKey != lastSinceKey) { drawDpfSinceCard(); lastSinceKey = sinceKey; }
  }

  String egtKey = String(isnan(dpf.egtPreC) ? -999.0f : dpf.egtPreC, 0) + ":" +
                  String(isnan(dpf.egtPostC) ? -999.0f : dpf.egtPostC, 0);
  if (egtKey != lastEgtKey) {
    drawDpfBottomMetric(104, 201, 100, "EGT PRE",
                        isnan(dpf.egtPreC) ? "--" : String(dpf.egtPreC, 0) + " C",
                        isnan(dpf.egtPreC) ? COL_MUTED : COL_ORANGE);
    drawDpfBottomMetric(208, 201, 104, "EGT POST",
                        isnan(dpf.egtPostC) ? "--" : String(dpf.egtPostC, 0) + " C",
                        isnan(dpf.egtPostC) ? COL_MUTED : COL_ORANGE);
    lastEgtKey = egtKey;
  }
}

void drawDpfHistory() {
  currentScreen = SCREEN_DPF_HISTORY;
  tft.fillScreen(COL_BG);
  drawHeader("HISTORIE DPF", true);

  fs::File f = LittleFS.open("/dpf.csv", "r");
  String lines[6];
  int count = 0;
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (!line.length()) continue;
      if (count < 6) lines[count++] = line;
      else {
        for (int i = 0; i < 5; i++) lines[i] = lines[i + 1];
        lines[5] = line;
      }
    }
    f.close();
  }

  tft.fillRoundRect(12, 48, 296, 146, 10, COL_PANEL);
  tft.drawRoundRect(12, 48, 296, 146, 10, COL_BORDER);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.drawString("INTERVALY REGENERACI", 22, 57);

  if (!count) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_MUTED, COL_PANEL);
    tft.drawString("Zatim neni ulozena dokoncena regenerace", 160, 116);
    tft.drawString("Graf se zacne plnit automaticky", 160, 134);
    return;
  }

  float km[6] = {0};
  float sootBefore[6] = {0};
  float sootAfter[6] = {0};
  float maxKm = 100.0f;

  float sumKm = 0.0f;
  int validKm = 0;
  for (int i = 0; i < count; i++) {
    String line = lines[i];
    int c1 = line.indexOf(',');
    int c2 = c1 >= 0 ? line.indexOf(',', c1 + 1) : -1;
    int c3 = c2 >= 0 ? line.indexOf(',', c2 + 1) : -1;
    if (c3 >= 0) km[i] = line.substring(c3 + 1).toFloat();
    if (c1 >= 0 && c2 > c1) {
      String soot = line.substring(c1 + 1, c2);
      int arrow = soot.indexOf("g->");
      if (arrow > 0) {
        sootBefore[i] = soot.substring(0, arrow).toFloat();
        sootAfter[i] = soot.substring(arrow + 3).toFloat();
      }
    }
    if (km[i] > 0.0f) { sumKm += km[i]; validKm++; }
    if (km[i] > maxKm) maxKm = km[i];
  }
  if (validKm) {
    tft.setTextDatum(TR_DATUM);
    tft.setTextFont(1);
    tft.setTextColor(COL_GREEN, COL_PANEL);
    tft.drawString("PRUMER " + String(sumKm / validKm, 0) + " km", 298, 57);
  }
  maxKm = ceil(maxKm / 100.0f) * 100.0f;

  const int baseY = 174;
  const int topY = 77;
  tft.drawFastHLine(30, baseY, 262, COL_BORDER);
  tft.drawFastHLine(30, topY, 262, COL_PANEL2);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.drawString(String((int)maxKm) + " km", 302, topY - 4);

  int barW = 28;
  int gap = count > 1 ? (250 - count * barW) / (count - 1) : 0;
  if (gap < 7) gap = 7;
  int x = 34;
  for (int i = 0; i < count; i++) {
    int bh = (int)(clampFloat(km[i] / maxKm, 0, 1) * 88.0f);
    uint16_t c = (i == count - 1) ? COL_ORANGE : COL_CYAN;
    tft.fillRoundRect(x, baseY - bh, barW, bh, 4, c);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(COL_TEXT, COL_PANEL);
    tft.drawString(String((int)round(km[i])), x + barW / 2, baseY - bh - 13);
    tft.setTextColor(COL_MUTED, COL_PANEL);
    tft.drawString(String(i + 1), x + barW / 2, baseY + 5);
    x += barW + gap;
  }

  String last = lines[count - 1];
  int c1 = last.indexOf(',');
  int c2 = c1 >= 0 ? last.indexOf(',', c1 + 1) : -1;
  int c3 = c2 >= 0 ? last.indexOf(',', c2 + 1) : -1;
  String dur = (c2 >= 0 && c3 > c2) ? last.substring(c2 + 1, c3) : "--";
  tft.fillRoundRect(12, 201, 296, 31, 8, COL_PANEL2);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COL_MUTED, COL_PANEL2);
  tft.drawString("POSLEDNI", 20, 216);
  tft.setTextColor(COL_TEXT, COL_PANEL2);
  String lastInfo = String((int)round(km[count - 1])) + " km  |  " + dur + "  |  " +
                    String(sootBefore[count - 1], 1) + " -> " + String(sootAfter[count - 1], 1) + " g";
  tft.drawString(lastInfo, 76, 216);
}

// ========================= MOTOR / TURBO =====================

void drawProgressBar(int x, int y, int w, int h, float ratio, uint16_t color) {
  ratio = clampFloat(ratio, 0.0f, 1.0f);
  tft.fillRoundRect(x, y, w, h, h / 2, COL_PANEL2);
  int fw = (int)((w - 4) * ratio);
  if (fw > 0) tft.fillRoundRect(x + 2, y + 2, fw, h - 4, max(1, (h - 4) / 2), color);
}

static uint16_t engineLoadColor(float loadPct) {
  if (isnan(loadPct)) return COL_MUTED;
  if (loadPct >= 95.0f) return COL_RED;
  if (loadPct >= 85.0f) return COL_ORANGE;
  if (loadPct >= 65.0f) return COL_YELLOW;
  return COL_GREEN;
}

static uint16_t engineCoolantColor(float tempC) {
  if (isnan(tempC)) return COL_MUTED;
  if (tempC >= 105.0f) return COL_RED;
  if (tempC >= 100.0f) return COL_ORANGE;
  if (tempC < 70.0f) return COL_BLUE;
  return COL_GREEN;
}

// Kompaktni obloukovy ukazatel. Nepouziva dalsi sprite/framebuffer, aby MOTOR
// nezvysoval tlak na DRAM. Pri aktualizaci se prekresli jen segmenty oblouku
// a mala oblast s ciselnou hodnotou.
static void drawEngineArcSegments(int cx, int cy, int r, float ratio, uint16_t accent) {
  ratio = clampFloat(ratio, 0.0f, 1.0f);
  const int segments = 21;
  const float a0 = 205.0f;
  const float a1 = 335.0f;
  int active = (ratio <= 0.0f) ? -1 : (int)roundf(ratio * (segments - 1));

  for (int i = 0; i < segments; i++) {
    float a = a0 + (a1 - a0) * ((float)i / (segments - 1));
    float rad = a * DEG_TO_RAD;
    int x1 = cx + (int)roundf(cosf(rad) * (r - 9));
    int y1 = cy + (int)roundf(sinf(rad) * (r - 9));
    int x2 = cx + (int)roundf(cosf(rad) * r);
    int y2 = cy + (int)roundf(sinf(rad) * r);
    uint16_t c = (i <= active) ? accent : COL_BORDER;
    tft.drawLine(x1, y1, x2, y2, c);
    // Druhy pixel vedle segmentu zlepsi citelnost bez SmoothArc API zavislosti.
    tft.drawLine(x1 + 1, y1, x2 + 1, y2, c);
  }
}

static void drawEngineGaugeCard(int x, int y, int w, int h,
                                const String &label, float value,
                                float minValue, float maxValue,
                                const String &unit, uint16_t accent) {
  tft.fillRoundRect(x, y, w, h, 9, COL_PANEL);
  tft.drawRoundRect(x, y, w, h, 9, blend565(COL_BORDER, accent, 6));
  tft.drawFastHLine(x + 16, y + 3, w - 32, accent);

  tft.setTextDatum(TC_DATUM);
  tft.setTextFont(2); tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COL_PANEL);
  tft.drawString(label, x + w / 2, y + 7);

  int cx = x + w / 2;
  int cy = y + 61;
  int r = 40;
  float ratio = isnan(value) ? 0.0f : (value - minValue) / (maxValue - minValue);
  drawEngineArcSegments(cx, cy, r, ratio, accent);

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(accent, COL_PANEL);
  String shown = isnan(value) ? String("--") : String((int)round(value));
  tft.drawString(shown, cx - (unit.length() ? 5 : 0), y + 47);
  tft.setTextFont(1);
  if (unit.length()) {
    tft.setTextDatum(BL_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, COL_PANEL);
    tft.drawString(unit, cx + 22, y + 55);
  }
}

static void updateEngineGaugeCard(int x, int y, int w, int h,
                                  float value, float minValue, float maxValue,
                                  const String &unit, uint16_t accent) {
  int cx = x + w / 2;
  int cy = y + 61;
  int r = 40;
  float ratio = isnan(value) ? 0.0f : (value - minValue) / (maxValue - minValue);

  // Ciselna cast je mala, takze ji muzeme bez blikani lokalne vycistit.
  // Potom znovu dokreslime segmenty, ktere prochazeji stejnou oblasti, aby
  // pri poklesu hodnoty nezustaly v oblouku zadne "díry".
  tft.fillRect(cx - 42, y + 32, 84, 28, COL_PANEL);
  drawEngineArcSegments(cx, cy, r, ratio, accent);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4); tft.setTextSize(1);
  tft.setTextColor(accent, COL_PANEL);
  String shown = isnan(value) ? String("--") : String((int)round(value));
  tft.drawString(shown, cx - (unit.length() ? 5 : 0), y + 47);
  if (unit.length()) {
    tft.setTextDatum(BL_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, COL_PANEL);
    tft.drawString(unit, cx + 22, y + 55);
  }
  tft.setTextFont(1);
}

static void drawEngineBarRow(int x, int y, int w, const String &label,
                             const String &value, const String &unit,
                             float ratio, uint16_t accent) {
  const int h = 31;
  tft.fillRoundRect(x, y, w, h, 7, COL_PANEL);
  tft.drawRoundRect(x, y, w, h, 7, COL_BORDER);

  tft.setTextDatum(ML_DATUM);
  tft.setTextFont(2); tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COL_PANEL);
  tft.drawString(label, x + 9, y + 10);

  tft.setTextDatum(MR_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(accent, COL_PANEL);
  String shown = value + (unit.length() ? String(" ") + unit : String(""));
  tft.drawString(shown, x + w - 8, y + 10);

  drawProgressBar(x + 65, y + 20, w - 138, 7, ratio, accent);
  tft.setTextFont(1);
}

static void updateEngineBarRow(int x, int y, int w, const String &value,
                               const String &unit, float ratio, uint16_t accent) {
  // Hodnota + bar maji vlastni male oblasti; ramecek/popisek zustava nedotceny.
  tft.fillRect(x + w - 88, y + 3, 80, 15, COL_PANEL);
  tft.setTextDatum(MR_DATUM);
  tft.setTextFont(2); tft.setTextSize(1);
  tft.setTextColor(accent, COL_PANEL);
  String shown = value + (unit.length() ? String(" ") + unit : String(""));
  tft.drawString(shown, x + w - 8, y + 10);
  drawProgressBar(x + 65, y + 20, w - 138, 7, ratio, accent);
  tft.setTextFont(1);
}

static void drawEngineMiniChip(int x, int y, int w, const String &label,
                               const String &value, const String &unit,
                               uint16_t accent) {
  const int h = 38;
  tft.fillRoundRect(x, y, w, h, 6, COL_PANEL2);
  tft.drawRoundRect(x, y, w, h, 6, blend565(COL_BORDER, accent, 5));
  tft.setTextDatum(TC_DATUM);
  tft.setTextFont(2); tft.setTextColor(TFT_WHITE, COL_PANEL2);
  tft.drawString(label, x + w/2, y + 2);
  tft.setTextDatum(BC_DATUM);
  tft.setTextFont(2); tft.setTextColor(accent, COL_PANEL2);
  tft.drawString(value + (unit.length() ? String(" ") + unit : String("")), x + w/2, y + h - 2);
  tft.setTextFont(1);
}

static void updateEngineMiniChip(int x, int y, int w, const String &value,
                                 const String &unit, uint16_t accent) {
  const int h = 38;
  tft.fillRect(x + 4, y + 17, w - 8, h - 19, COL_PANEL2);
  tft.setTextDatum(BC_DATUM);
  tft.setTextFont(2); tft.setTextSize(1);
  tft.setTextColor(accent, COL_PANEL2);
  tft.drawString(value + (unit.length() ? String(" ") + unit : String("")), x + w/2, y + h - 2);
  tft.setTextFont(1);
}

void drawEngineScreen() {
  currentScreen = SCREEN_ENGINE;
  tft.fillScreen(COL_BG);
  drawHeader("MOTOR", true);

  const uint16_t loadCol = engineLoadColor(car.engineLoadPct);
  const uint16_t coolantCol = engineCoolantColor(car.coolantC);

  // Dva hlavni ukazatele: stav motoru je citelny i perifernim pohledem.
  drawEngineGaugeCard(10, 46, 147, 78, "ZATIZENI", car.engineLoadPct, 0.0f, 100.0f, "%", loadCol);
  drawEngineGaugeCard(163, 46, 147, 78, "CHLADIVO", car.coolantC, 50.0f, 120.0f, "C", coolantCol);

  String rail = isnan(car.railPressureKpa) ? "--" : String(car.railPressureKpa / 1000.0f, 1);
  float railRatio = isnan(car.railPressureKpa) ? 0.0f : (car.railPressureKpa / 1000.0f) / 200.0f;
  drawEngineBarRow(10, 130, 300, "RAIL", rail, "MPa", railRatio, COL_ORANGE);

  String maf = formatFloat(car.mafGps, 1);
  float mafRatio = isnan(car.mafGps) ? 0.0f : car.mafGps / 150.0f;
  drawEngineBarRow(10, 165, 300, "MAF", maf, "g/s", mafRatio, COL_CYAN);

  drawEngineMiniChip(10, 195, 95, "RPM", String((int)round(car.rpm)), "", rpmColor(car.rpm));
  drawEngineMiniChip(112, 195, 95, "SANI", formatFloat(car.intakeTempC, 0), "C", COL_BLUE);
  drawEngineMiniChip(214, 195, 96, "MAP", formatFloat(car.mapKpa, 0), "kPa", COL_CYAN);
}

void updateEngineDynamic() {
  if (currentScreen != SCREEN_ENGINE) return;
  updateHeaderStatus();

  // UI MOTOR nepotrebuje prekreslovat stovky krat za sekundu. 160 ms je dost
  // rychle pro zatizeni/MAF/RAIL a vyrazne snizuje SPI provoz i blikani.
  static unsigned long lastUiMs = 0;
  unsigned long now = millis();
  if (now - lastUiMs < 160UL) return;
  lastUiMs = now;

  static int lastLoad10 = -99999;
  static int lastCoolant10 = -99999;
  static int lastRail10 = -99999;
  static int lastMaf10 = -99999;
  static int lastRpm = -99999;
  static int lastIntake = -99999;
  static int lastMap = -99999;
  static uint16_t lastLoadCol = 0xFFFF;
  static uint16_t lastCoolantCol = 0xFFFF;

  int load10 = isnan(car.engineLoadPct) ? -32768 : (int)round(car.engineLoadPct * 10.0f);
  int coolant10 = isnan(car.coolantC) ? -32768 : (int)round(car.coolantC * 10.0f);
  uint16_t loadCol = engineLoadColor(car.engineLoadPct);
  uint16_t coolantCol = engineCoolantColor(car.coolantC);
  if (load10 != lastLoad10 || loadCol != lastLoadCol) {
    updateEngineGaugeCard(10, 46, 147, 78, car.engineLoadPct, 0.0f, 100.0f, "%", loadCol);
    lastLoad10 = load10; lastLoadCol = loadCol;
  }
  if (coolant10 != lastCoolant10 || coolantCol != lastCoolantCol) {
    updateEngineGaugeCard(163, 46, 147, 78, car.coolantC, 50.0f, 120.0f, "C", coolantCol);
    lastCoolant10 = coolant10; lastCoolantCol = coolantCol;
  }

  int rail10 = isnan(car.railPressureKpa) ? -32768 : (int)round((car.railPressureKpa / 1000.0f) * 10.0f);
  if (rail10 != lastRail10) {
    String rail = isnan(car.railPressureKpa) ? "--" : String(car.railPressureKpa / 1000.0f, 1);
    float ratio = isnan(car.railPressureKpa) ? 0.0f : (car.railPressureKpa / 1000.0f) / 200.0f;
    updateEngineBarRow(10, 130, 300, rail, "MPa", ratio, COL_ORANGE);
    lastRail10 = rail10;
  }

  int maf10 = isnan(car.mafGps) ? -32768 : (int)round(car.mafGps * 10.0f);
  if (maf10 != lastMaf10) {
    updateEngineBarRow(10, 165, 300, formatFloat(car.mafGps, 1), "g/s",
                       isnan(car.mafGps) ? 0.0f : car.mafGps / 150.0f, COL_CYAN);
    lastMaf10 = maf10;
  }

  int rpmNow = (int)round(car.rpm);
  if (rpmNow != lastRpm) {
    updateEngineMiniChip(10, 195, 95, String(rpmNow), "", rpmColor(car.rpm));
    lastRpm = rpmNow;
  }
  int intakeNow = isnan(car.intakeTempC) ? -32768 : (int)round(car.intakeTempC);
  if (intakeNow != lastIntake) {
    updateEngineMiniChip(112, 195, 95, formatFloat(car.intakeTempC, 0), "C", COL_BLUE);
    lastIntake = intakeNow;
  }
  int mapNow = isnan(car.mapKpa) ? -32768 : (int)round(car.mapKpa);
  if (mapNow != lastMap) {
    updateEngineMiniChip(214, 195, 96, formatFloat(car.mapKpa, 0), "kPa", COL_CYAN);
    lastMap = mapNow;
  }
}

float currentBoostBar() {
  if (isnan(car.mapKpa)) return NAN;
  float ambient = isnan(car.baroKpa) ? 100.0f : car.baroKpa;
  float boost = (car.mapKpa - ambient) / 100.0f;
  return boost < 0 ? 0.0f : boost;
}

void updateTurboGaugeValue(bool force = false) {
  const int cx = 105, cy = 135, r = 72;
  const float a0 = 140.0f, a1 = 400.0f;
  static float previousNeedleA = a0;
  static float previousBoost = -99.0f;
  static float previousArcEnd = a0;
  static uint16_t previousArcColor = COL_CYAN;

  float boost = currentBoostBar();
  bool boostValid = !isnan(boost);
  float shownBoost = boostValid ? boost : 0.0f;
  if (!force) {
    if (!boostValid && previousBoost == -99.0f) return;
    if (boostValid && previousBoost != -99.0f && fabs(shownBoost - previousBoost) < 0.01f) return;
  }

  float ratio = isnan(boost) ? 0 : clampFloat(boost / 2.0f, 0, 1);
  float needleA = a0 + (a1 - a0) * ratio;
  float arcEnd = a0 + (a1 - a0) * ratio;
  uint16_t arcColor = shownBoost > 1.5f ? COL_ORANGE : COL_CYAN;

  if (force) {
    previousNeedleA = a0;
    previousArcEnd = a0;
    previousArcColor = arcColor;
  }

  // Smaz pouze starou rucicku. Vnejsi oblouk uz neprekreslujeme cely.
  float oldRad = previousNeedleA * DEG_TO_RAD;
  tft.drawLine(cx, cy, cx + (int)(cos(oldRad) * 49), cy + (int)(sin(oldRad) * 49), COL_PANEL);

  // Zmen pouze cast oblouku, ktera se od minule hodnoty zmenila.
  if (force || arcColor != previousArcColor) {
    drawArcSegment(cx, cy, r, 7, a0, a1, COL_BORDER);
    if (arcEnd > a0) drawArcSegment(cx, cy, r, 7, a0, arcEnd, arcColor);
  } else if (arcEnd > previousArcEnd) {
    drawArcSegment(cx, cy, r, 7, previousArcEnd, arcEnd, arcColor);
  } else if (arcEnd < previousArcEnd) {
    drawArcSegment(cx, cy, r, 7, arcEnd, previousArcEnd, COL_BORDER);
  }

  float rad = needleA * DEG_TO_RAD;
  tft.drawLine(cx, cy, cx + (int)(cos(rad) * 49), cy + (int)(sin(rad) * 49), COL_TEXT);
  tft.fillCircle(cx, cy, 5, COL_CYAN);

  // Hodnota se maze jen v malem obdelniku uvnitr ciferniku.
  tft.fillRect(52, 147, 106, 54, COL_PANEL);
  tft.setTextDatum(MC_DATUM); tft.setTextFont(4); tft.setTextSize(1);
  tft.setTextColor(shownBoost > 1.5f ? COL_ORANGE : COL_CYAN, COL_PANEL);
  tft.drawString(isnan(boost) ? "--" : String(boost, 2), cx, cy + 14);
  tft.setTextFont(2); tft.setTextColor(COL_TEXT, COL_PANEL);
  tft.drawString("bar", cx, cy + 40);
  tft.setTextFont(1);

  previousNeedleA = needleA;
  previousBoost = isnan(boost) ? -99.0f : boost;
  previousArcEnd = arcEnd;
  previousArcColor = arcColor;
}

void drawTurboGaugeBody() {
  const int cx = 105, cy = 135, r = 72;
  tft.fillRoundRect(12, 48, 188, 177, 12, COL_PANEL);
  tft.drawRoundRect(12, 48, 188, 177, 12, COL_BORDER);
  tft.setTextDatum(TC_DATUM); tft.setTextFont(2); tft.setTextSize(1); tft.setTextColor(COL_LABEL, COL_PANEL);
  tft.drawString("TLAK TURBA", cx, 55);
  tft.setTextFont(1);

  float a0 = 140.0f, a1 = 400.0f;
  drawArcSegment(cx, cy, r, 7, a0, a1, COL_BORDER);
  for (int i = 0; i <= 8; i++) {
    float a = (a0 + (a1 - a0) * i / 8.0f) * DEG_TO_RAD;
    int x1 = cx + (int)(cos(a) * 58), y1 = cy + (int)(sin(a) * 58);
    int x2 = cx + (int)(cos(a) * 67), y2 = cy + (int)(sin(a) * 67);
    tft.drawLine(x1, y1, x2, y2, COL_MUTED);
  }
  tft.setTextDatum(MC_DATUM); tft.setTextFont(2); tft.setTextSize(1); tft.setTextColor(COL_LABEL, COL_PANEL);
  tft.drawString(isnan(car.baroKpa) ? "vuci 100 kPa" : "vuci BARO", cx, 211);
  updateTurboGaugeValue(true);
}

void drawTurboScreen() {
  currentScreen = SCREEN_TURBO;
  tft.fillScreen(COL_BG);
  drawHeader("TURBO GAUGE", true);
  drawTurboGaugeBody();
  drawMetricCard(208, 48, 100, 53, "MAP ABS", formatFloat(car.mapKpa, 0), "kPa", COL_CYAN);
  drawMetricCard(208, 109, 100, 53, "BARO", formatFloat(car.baroKpa, 0), "kPa", COL_BLUE);
  drawMetricCard(208, 170, 100, 55, "RPM", String((int)round(car.rpm)), "", rpmColor(car.rpm));
}

void updateTurboDynamic() {
  if (currentScreen != SCREEN_TURBO) return;
  updateHeaderStatus();
  updateTurboGaugeValue(false);
  updateMetricValue(208,48,100,53,formatFloat(car.mapKpa,0),"kPa",COL_CYAN);
  updateMetricValue(208,109,100,53,formatFloat(car.baroKpa,0),"kPa",COL_BLUE);
  updateMetricValue(208,170,100,55,String((int)round(car.rpm)),"",rpmColor(car.rpm));
}

// ======================= ELEKTRIKA / ALTERNATOR ===============

float electricalBusVoltage() {
  // Preferujeme napeti merene ECU pres PID 0142. Pokud ho tato varianta ECU
  // neposkytne, pouzijeme napajeci napeti ELM na stejne 12V palubni siti.
  // Neni to druhy nezavisly voltmetr - baterie a alternator jsou fyzicky
  // pripojene na stejnou palubni sit.
  if (car.moduleVoltageAvailable && !isnan(car.moduleVoltageV)) return car.moduleVoltageV;
  return car.voltageV;
}

String electricalStatus(uint16_t &color) {
  float v = electricalBusVoltage();
  if (isnan(v)) { color = COL_MUTED; return "CEKAM NA DATA"; }
  if (car.rpm > 500) {
    if (v > 15.0f) { color = COL_RED; return "PREPETI"; }
    if (v < 13.2f) { color = COL_ORANGE; return "SLABE DOBIJENI"; }
    color = COL_GREEN; return "DOBIJENI OK";
  }
  if (v >= 12.4f) { color = COL_GREEN; return "BATERIE OK"; }
  if (v >= 12.0f) { color = COL_YELLOW; return "BATERIE SLABSI"; }
  color = COL_RED; return "NIZKE NAPETI";
}

void drawElectricalScreen() {
  currentScreen = SCREEN_ELECTRICAL;
  tft.fillScreen(COL_BG);
  drawHeader("ELEKTRIKA", true);
  uint16_t statusColor; String status = electricalStatus(statusColor);
  float v = electricalBusVoltage();

  // PID 0142 meri napeti palubni 12V site primo ridici jednotkou. Nejde o ATRV
  // z ELM adapteru. Baterie a alternator jsou ve voze na stejne 12V sbernici;
  // pri bezicim motoru tedy "alternator" znamena skutecne nabijeci napeti site.
  drawMetricCard(12, 52, 144, 76, "BATERIE", formatFloat(v, 2), "V", statusColor);
  drawMetricCard(164,52, 144, 76, "ALTERNATOR", car.rpm > 500 ? formatFloat(v, 2) : String("--"), "V",
                 car.rpm > 500 ? statusColor : COL_MUTED);

  tft.fillRoundRect(12, 138, 296, 42, 9, COL_PANEL2);
  tft.drawRoundRect(12, 138, 296, 42, 9, statusColor);
  tft.setTextDatum(MC_DATUM); tft.setTextFont(2); tft.setTextColor(statusColor, COL_PANEL2);
  tft.drawString(status, 160, 159); tft.setTextFont(1);

  drawMetricCard(12, 190, 94, 42, "RPM", String((int)round(car.rpm)), "", rpmColor(car.rpm));
  drawMetricCard(113,190,94,42,"MOTOR",car.rpm>500?"BEZI":"STOJI","",car.rpm>500?COL_GREEN:COL_MUTED);
  drawMetricCard(214,190,94,42,"ALARM",warnedVoltage||warnedOverVoltage?"ANO":"NE","",warnedVoltage||warnedOverVoltage?COL_RED:COL_GREEN);
}

void updateElectricalDynamic() {
  if (currentScreen != SCREEN_ELECTRICAL) return;
  updateHeaderStatus();
  uint16_t statusColor; String status = electricalStatus(statusColor);
  float v = electricalBusVoltage();
  updateMetricValue(12,52,144,76,formatFloat(v,2),"V",statusColor);
  updateMetricValue(164,52,144,76,car.rpm>500?formatFloat(v,2):String("--"),"V",car.rpm>500?statusColor:COL_MUTED);

  static String lastStatus = ""; static uint16_t lastStatusColor = 0;
  if (status != lastStatus || statusColor != lastStatusColor) {
    tft.fillRoundRect(12, 138, 296, 42, 9, COL_PANEL2);
    tft.drawRoundRect(12, 138, 296, 42, 9, statusColor);
    tft.setTextDatum(MC_DATUM); tft.setTextFont(2); tft.setTextColor(statusColor, COL_PANEL2);
    tft.drawString(status, 160, 159); tft.setTextFont(1);
    lastStatus = status; lastStatusColor = statusColor;
  }
  updateMetricValue(12,190,94,42,String((int)round(car.rpm)),"",rpmColor(car.rpm));
  updateMetricValue(113,190,94,42,car.rpm>500?"BEZI":"STOJI","",car.rpm>500?COL_GREEN:COL_MUTED);
  updateMetricValue(214,190,94,42,warnedVoltage||warnedOverVoltage?"ANO":"NE","",warnedVoltage||warnedOverVoltage?COL_RED:COL_GREEN);
}

// ============================ DIAGNOSTIKA =====================

void requestDiagScanAll() {
  if (!car.elmConnected) return;
  bool accepted = false;
  if (diagMutex && xSemaphoreTake(diagMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    if (!dtc.busy) { dtc.busy = true; accepted = true; }
    xSemaphoreGive(diagMutex);
  } else if (!diagMutex && !dtc.busy) {
    dtc.busy = true; accepted = true;
  }
  if (accepted) diagScanAllRequested = true;
}

void requestDiagScanOne(int idx) {
  if (!car.elmConnected || idx < 0 || idx >= DIAG_MODULE_COUNT) return;
  bool accepted = false;
  if (diagMutex && xSemaphoreTake(diagMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    if (!dtc.busy) { dtc.busy = true; accepted = true; }
    xSemaphoreGive(diagMutex);
  } else if (!diagMutex && !dtc.busy) {
    dtc.busy = true; accepted = true;
  }
  if (accepted) diagSingleRequested = idx;
}

void drawDiagnosticsScreen() {
  currentScreen = SCREEN_DIAGNOSTICS;
  tft.fillScreen(COL_BG);
  DtcState dtcView = copyDtcState();

  if (diagDetailIndex >= 0 && diagDetailIndex < DIAG_MODULE_COUNT) {
    const DiagModuleSpec &sp = DIAG_MODULES[diagDetailIndex];
    DiagModuleState st = copyDiagState(diagDetailIndex);
    drawHeader(String("DIAG ") + sp.vagAddress, true);

    uint16_t c = !st.scanned ? COL_YELLOW : (!st.online ? COL_MUTED : (st.count ? COL_ORANGE : COL_GREEN));
    tft.fillRoundRect(12, 48, 296, 50, 9, COL_PANEL);
    tft.drawRoundRect(12, 48, 296, 50, 9, c);
    tft.setTextDatum(TL_DATUM); tft.setTextFont(2); tft.setTextColor(COL_TEXT, COL_PANEL);
    tft.drawString(sp.name, 22, 57);
    tft.setTextFont(1); tft.setTextColor(c, COL_PANEL);
    String state = dtcView.busy ? "CTU..." : (!st.scanned ? "NECTENO" : (!st.online ? "BEZ ODPOVEDI" : (st.count ? String(st.count) + " ZAVAD" : "BEZ ZAVAD")));
    tft.drawString(state, 22, 80);
    drawButton(222, 58, 76, 30, dtcView.busy ? "CEKEJ" : "NACIST", !dtcView.busy, true);

    int y = 107;
    if (st.scanned && st.online && st.count) {
      for (int i = 0; i < st.count && i < 6; i++) {
        tft.fillRoundRect(12, y, 296, 20, 5, COL_PANEL2);
        tft.setTextDatum(TL_DATUM); tft.setTextFont(2); tft.setTextColor(COL_ORANGE, COL_PANEL2);
        tft.drawString(st.codes[i], 20, y + 3);
        y += 23;
      }
    } else {
      tft.setTextDatum(MC_DATUM); tft.setTextFont(1); tft.setTextColor(COL_LABEL, COL_BG);
      tft.drawString(st.note.length() ? st.note : "Read-only diagnostika", 160, 135);
      tft.setTextColor(COL_MUTED, COL_BG);
      tft.drawString("Bez mazani chyb / bez zapisu do jednotky", 160, 153);
    }
    tft.setTextDatum(BC_DATUM); tft.setTextFont(1); tft.setTextColor(COL_MUTED, COL_BG);
    tft.drawString("U nekterych PQ35 jednotek muze byt potreba KWP/VCDS rozhrani.", 160, 235);
    return;
  }

  drawHeader("DIAGNOSTIKA", true);
  int online = 0, faults = 0, scanned = 0;
  for (int i = 0; i < DIAG_MODULE_COUNT; i++) {
    DiagModuleState st = copyDiagState(i);
    if (st.scanned) scanned++;
    if (st.online) online++;
    faults += st.count;
  }

  tft.fillRoundRect(12, 47, 296, 43, 9, COL_PANEL);
  tft.drawRoundRect(12, 47, 296, 43, 9, faults ? COL_ORANGE : COL_BORDER);
  tft.setTextDatum(TL_DATUM); tft.setTextFont(1); tft.setTextColor(COL_LABEL, COL_PANEL);
  tft.drawString("CELKOVY SCAN DOSAZITELNYCH JEDNOTEK", 20, 54);
  tft.setTextFont(2); tft.setTextColor(faults ? COL_ORANGE : COL_GREEN, COL_PANEL);
  tft.drawString(scanned ? (String(online) + "/" + String(DIAG_MODULE_COUNT) + " online | " + String(faults) + " DTC") : "PRIPRAVENO", 20, 69);
  drawButton(226, 54, 72, 29, dtcView.busy ? "CEKEJ" : "SCAN", !dtcView.busy, true);

  int first = diagPage * 4;
  int y = 97;
  for (int row = 0; row < 4; row++) {
    int idx = first + row;
    if (idx >= DIAG_MODULE_COUNT) break;
    const DiagModuleSpec &sp = DIAG_MODULES[idx];
    DiagModuleState st = copyDiagState(idx);
    uint16_t c = !st.scanned ? COL_MUTED : (!st.online ? COL_RED : (st.count ? COL_ORANGE : COL_GREEN));
    tft.fillRoundRect(12, y, 296, 28, 6, COL_PANEL);
    tft.drawRoundRect(12, y, 296, 28, 6, COL_BORDER);
    tft.fillCircle(24, y + 14, 5, c);
    tft.setTextDatum(ML_DATUM); tft.setTextFont(2); tft.setTextColor(COL_TEXT, COL_PANEL);
    tft.drawString(String(sp.vagAddress) + "  " + sp.name, 36, y + 14);
    tft.setTextDatum(MR_DATUM); tft.setTextFont(1); tft.setTextColor(c, COL_PANEL);
    String stat = !st.scanned ? "NACIST >" : (!st.online ? "NEDOSTUPNA >" : (st.count ? String(st.count) + " DTC >" : "OK >"));
    tft.drawString(stat, 299, y + 14);
    y += 31;
  }

  tft.setTextDatum(BL_DATUM); tft.setTextFont(2); tft.setTextColor(diagPage > 0 ? COL_CYAN : COL_MUTED, COL_BG);
  tft.drawString("< PREV", 12, 236);
  tft.setTextDatum(BR_DATUM); tft.setTextColor((diagPage + 1) * 4 < DIAG_MODULE_COUNT ? COL_CYAN : COL_MUTED, COL_BG);
  tft.drawString("NEXT >", 308, 236);
  tft.setTextFont(1);
}

// ============================ SERVIS ==========================

int serviceDaysSince(time_t epoch) {
  if (epoch <= 0 || !clockIsValid()) return -1;
  long sec = (long)(time(nullptr) - epoch);
  if (sec < 0) return 0;
  return (int)(sec / 86400L);
}

void drawServiceCard(int y, const String &title, float km, float intervalKm,
                     time_t epoch, uint16_t daysLimit, uint16_t accent, uint8_t target) {
  int days = serviceDaysSince(epoch);
  float ratioKm = intervalKm > 0 ? km / intervalKm : 0;
  float ratioDays = days >= 0 && daysLimit > 0 ? (float)days / daysLimit : 0;
  float ratio = max(ratioKm, ratioDays);
  uint16_t c = ratio >= 1.0f ? COL_RED : (ratio >= 0.8f ? COL_ORANGE : accent);

  tft.fillRoundRect(12, y, 296, 72, 10, COL_PANEL);
  tft.drawRoundRect(12, y, 296, 72, 10, COL_BORDER);
  tft.setTextDatum(TL_DATUM); tft.setTextSize(1); tft.setTextColor(COL_TEXT, COL_PANEL);
  tft.drawString(title, 22, y + 9);
  tft.setTextColor(c, COL_PANEL); tft.setTextSize(2);
  tft.drawString(String((int)round(km)) + " km", 22, y + 28);
  tft.setTextSize(1); tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.drawString("/ " + String((int)intervalKm) + " km", 107, y + 36);
  String dayText = days < 0 ? "cas --" : String(days) + " / " + String(daysLimit) + " dni";
  tft.drawString(dayText, 22, y + 55);
  drawProgressBar(163, y + 31, 86, 10, ratio, c);

  bool armed = serviceResetArmed && serviceResetTarget == target && millis() < serviceResetArmUntil;
  drawButton(255, y + 22, 45, 29, armed ? "OK?" : "RESET", true, armed);
}

void drawServiceScreen() {
  currentScreen = SCREEN_SERVICE;
  tft.fillScreen(COL_BG);
  drawHeader("SERVIS", true);
  drawServiceCard(49, "OLEJ / FILTR", service.oilKm, service.oilIntervalKm,
                  service.oilEpoch, service.oilIntervalDays, COL_GREEN, 1);
  drawServiceCard(128, "INSPEKCE", service.inspectionKm, service.inspectionIntervalKm,
                  service.inspectionEpoch, service.inspectionIntervalDays, COL_CYAN, 2);
  tft.setTextDatum(MC_DATUM); tft.setTextSize(1); tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Km se pocitaji pouze pri zapnutem monitoru", 160, 218);
  tft.drawString("RESET vyzaduje druhe potvrzeni", 160, 232);
}

// ============================ POWER ===========================

const char *powerModeLabel() {
  switch (powerMode) {
    case POWER_DIMMED: return "DIM";
    case POWER_DISPLAY_OFF: return "DISPLAY OFF";
    case POWER_LOW: return "LOW POWER";
    default: return "ACTIVE";
  }
}

void drawPowerScreen() {
  currentScreen = SCREEN_POWER;
  tft.fillScreen(COL_BG);
  drawHeader("POWER MAN.", true);

  drawSettingRow(46, "REZIM", powerModeLabel(), powerMode == POWER_LOW ? COL_YELLOW : COL_GREEN);

  drawSettingRow(86, "AUTO STMIVANI", powerAutoDim ? "ZAP" : "VYP",
                 powerAutoDim ? COL_GREEN : COL_MUTED);
  drawButton(245, 91, 57, 28, powerAutoDim ? "ON" : "OFF", true, powerAutoDim);

  drawSettingRow(126, "DIM PO", String(powerDimDelayMs / 60000UL) + " min", COL_CYAN);
  drawButton(218, 131, 38, 28, "-"); drawButton(264, 131, 38, 28, "+");

  drawSettingRow(166, "DISPLEJ OFF", String(powerDisplayOffDelayMs / 60000UL) + " min", COL_BLUE);
  drawButton(218, 171, 38, 28, "-"); drawButton(264, 171, 38, 28, "+");

  tft.setTextDatum(ML_DATUM); tft.setTextFont(2); tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString(String("LOW POWER +") + String(powerLowDelayMs / 60000UL) + " min", 16, 220);
  tft.setTextDatum(MR_DATUM); tft.drawString("-   +", 300, 220);
  tft.setTextFont(1);
}

// ============================= ALARMY ==========================

void drawAlarmToggleRow(int y, const String &label, bool enabled, uint16_t accent) {
  tft.fillRoundRect(12, y, 296, 27, 7, COL_PANEL);
  tft.drawRoundRect(12, y, 296, 27, 7, enabled ? accent : COL_BORDER);
  tft.setTextDatum(ML_DATUM); tft.setTextFont(2); tft.setTextColor(enabled ? COL_TEXT : COL_MUTED, COL_PANEL);
  tft.drawString(label, 20, y + 14);
  uint16_t sw = enabled ? accent : COL_PANEL2;
  tft.fillRoundRect(258, y + 5, 40, 17, 8, sw);
  tft.drawRoundRect(258, y + 5, 40, 17, 8, enabled ? accent : COL_BORDER);
  tft.fillCircle(enabled ? 289 : 267, y + 13, 6, enabled ? TFT_WHITE : COL_MUTED);
  tft.setTextFont(1);
}

void drawAlarmsScreen() {
  currentScreen = SCREEN_ALARMS;
  tft.fillScreen(COL_BG);
  drawHeader("ALARMY", true);
  drawAlarmToggleRow(45,  "MOTOR > 105 C", alarmCoolantEnabled, COL_RED);
  drawAlarmToggleRow(76,  "NIZKE NAPETI", alarmLowVoltageEnabled, COL_YELLOW);
  drawAlarmToggleRow(107, "PREPETI", alarmOverVoltageEnabled, COL_RED);
  drawAlarmToggleRow(138, "DPF REGENERACE", alarmRegenEnabled, COL_ORANGE);
  drawAlarmToggleRow(169, String("DPF DO ") + String((int)alarmRegenSoonKm) + " KM", alarmRegenSoonEnabled, COL_ORANGE);
  drawAlarmToggleRow(200, "ELM ODPOJENI", alarmElmDisconnectEnabled, COL_CYAN);
}

void saveAlarmPreferences() {
  prefs.putBool("alCoolant", alarmCoolantEnabled);
  prefs.putBool("alLowV", alarmLowVoltageEnabled);
  prefs.putBool("alHighV", alarmOverVoltageEnabled);
  prefs.putBool("alRegen", alarmRegenEnabled);
  prefs.putBool("alRegen10", alarmRegenSoonEnabled);
  prefs.putBool("alElm", alarmElmDisconnectEnabled);
}

// ============================ PREHLED ==========================

float displayFuelPct() {
  if (car.fuelLevelAvailable && !isnan(car.fuelLevelPct)) return clampFloat(car.fuelLevelPct, 0.0f, 100.0f);
  if (manualFuelConfigured && !isnan(manualFuelPct)) return clampFloat(manualFuelPct, 0.0f, 100.0f);
  if (storedFuelValid && !isnan(storedFuelPct)) return clampFloat(storedFuelPct, 0.0f, 100.0f);
  return NAN;
}

float recentTripsAverageL100() {
  static unsigned long lastCalcMs = 0;
  static float cached = NAN;
  if (lastCalcMs && millis() - lastCalcMs < 60000UL) return cached;
  lastCalcMs = millis();
  TripRecord rec[12];
  int n = loadRecentTrips(rec, 12);
  float kmSum = 0.0f, fuelSum = 0.0f;
  for (int i = 0; i < n; i++) {
    if (rec[i].distanceKm < 1.0f || isnan(rec[i].fuelLiters) || rec[i].fuelLiters <= 0.0f) continue;
    float c = rec[i].fuelLiters * 100.0f / rec[i].distanceKm;
    if (c < 2.0f || c > 20.0f) continue;
    kmSum += rec[i].distanceKm;
    fuelSum += rec[i].fuelLiters;
  }
  cached = (kmSum >= 3.0f && fuelSum > 0.0f) ? (fuelSum * 100.0f / kmSum) : NAN;
  return cached;
}

float estimatedRangeKm() {
  float fuelPct = displayFuelPct();
  if (isnan(fuelPct) || tankCapacityL <= 0.1f) return NAN;
  float avgL100 = NAN;
  // 1) aktualni jizda
  if (trip.active && trip.fuelAvailable && trip.distanceKm >= 2.0f && trip.fuelLiters > 0.01f)
    avgL100 = trip.fuelLiters * 100.0f / trip.distanceKm;
  // 2) spotreba od posledni regenerace
  if ((isnan(avgL100) || avgL100 < 2.0f || avgL100 > 20.0f) &&
      dpf.available && !isnan(dpf.fuelSinceRegenL) && !isnan(dpf.kmSinceRegen) &&
      dpf.kmSinceRegen >= 10.0f && dpf.fuelSinceRegenL > 0.1f)
    avgL100 = dpf.fuelSinceRegenL * 100.0f / dpf.kmSinceRegen;
  // 3) historie ulozenych jizd
  if (isnan(avgL100) || avgL100 < 2.0f || avgL100 > 20.0f) avgL100 = recentTripsAverageL100();
  // 4) uzivatelsky nastavitelny konzervativni fallback - dojezd tak neni trvale --
  if (isnan(avgL100) || avgL100 < 2.0f || avgL100 > 20.0f) avgL100 = rangeFallbackL100;
  if (avgL100 < 2.0f || avgL100 > 20.0f) return NAN;
  float liters = tankCapacityL * fuelPct / 100.0f;
  return clampFloat(liters * 100.0f / avgL100, 0.0f, 2000.0f);
}

// ======================= PREHLED / TFT_eWidget =======================
// V16.24: hlavni PREHLED zustava dashboard. Vlevo je skutecny TFT_eWidget
// MeterWidget pro RPM, vpravo teplota motoru a tlak turba. Dole je DPF km
// do regenerace a mala venkovni teplota. Bez rychlosti, nadrze a dojezdu.
//
// MeterWidget nativne kresli 239x126. Kvuli rozlozeni 320x240 ho kreslime
// do 8-bit TFT_eSprite (~30 kB misto ~60 kB) a pri vykresleni orizneme jeho prazdne okraje a zvetsime aktivni cast do panelu.
// Pouzivame readPixel(), tedy vzdy skutecnou RGB565 barvu bez ohledu na bpp.

static constexpr int LIVE_RPM_SRC_W = 239;
static constexpr int LIVE_RPM_SRC_H = 126;
// MeterWidget ma uvnitr vlastni okraje. V16.30.2 je pri skalovani orezeme,
// aby se zvetsil skutecny oblouk/stupnice, ne pouze jeho framebuffer.
static constexpr int LIVE_RPM_CROP_X = 16;
static constexpr int LIVE_RPM_CROP_Y = 0;
static constexpr int LIVE_RPM_CROP_W = 207;
static constexpr int LIVE_RPM_CROP_H = 112;
static constexpr int LIVE_RPM_DST_X = 8;
static constexpr int LIVE_RPM_DST_Y = 45;
static constexpr int LIVE_RPM_DST_W = 180;
static constexpr int LIVE_RPM_DST_H = 110;
static bool liveRpmSpriteReady = false;
static bool liveRpmSpriteFailed = false;
static bool liveRpmFrameValid = false;
static unsigned long liveRpmWidgetLastMs = 0;
static int liveRpmNumericLast = -99999;
static int liveRpmMeterLast = -99999;

static void drawOverviewValueCard(int x, int y, int w, int h,
                                  const String &label, const String &value,
                                  const String &unit, uint16_t accent) {
  tft.fillRoundRect(x, y, w, h, 8, COL_PANEL);
  tft.drawRoundRect(x, y, w, h, 8, blend565(COL_BORDER, accent, 7));
  tft.drawFastHLine(x + 12, y + 3, w - 24, accent);

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2); tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COL_PANEL);
  tft.drawString(label, x + w/2, y + 10);

  tft.setTextFont(4);
  tft.setTextColor(accent, COL_PANEL);
  int valueY = y + h/2 + ((h >= 70) ? 9 : 8);
  tft.drawString(value, x + w/2 - (unit.length() ? 7 : 0), valueY);

  if (unit.length()) {
    tft.setTextDatum(BR_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, COL_PANEL);
    tft.drawString(unit, x + w - 7, y + h - 4);
  }
  tft.setTextFont(1); tft.setTextSize(1);
}

static void updateOverviewValueCard(int x, int y, int w, int h,
                                    const String &value, const String &unit,
                                    uint16_t accent) {
  // V16.24 anti-flicker: pokud se text ani barva nezmenily, na TFT neposilame nic.
  struct OverviewCache { bool used; int x,y,w,h; String value,unit; uint16_t accent; };
  static OverviewCache cache[8] = {};
  int slot = -1;
  for (int i=0;i<8;i++) {
    if (cache[i].used && cache[i].x==x && cache[i].y==y && cache[i].w==w && cache[i].h==h) { slot=i; break; }
    if (slot<0 && !cache[i].used) slot=i;
  }
  if (slot<0) slot=(x+y+w+h)&7;
  OverviewCache &c=cache[slot];
  if (c.used && c.x==x && c.y==y && c.w==w && c.h==h && c.value==value && c.unit==unit && c.accent==accent) return;
  c.used=true; c.x=x; c.y=y; c.w=w; c.h=h; c.value=value; c.unit=unit; c.accent=accent;

  int top = y + 20;
  tft.fillRect(x + 4, top, w - 8, h - (top - y) - 4, COL_PANEL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4); tft.setTextSize(1);
  tft.setTextColor(accent, COL_PANEL);
  int valueY = y + h/2 + ((h >= 70) ? 9 : 8);
  tft.drawString(value, x + w/2 - (unit.length() ? 7 : 0), valueY);
  if (unit.length()) {
    tft.setTextDatum(BR_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, COL_PANEL);
    tft.drawString(unit, x + w - 7, y + h - 4);
  }
  tft.setTextFont(1);
}

static void drawOverviewAmbientChip(int x, int y, int w, int h) {
  uint16_t accent = car.ambientAvailable ? COL_CYAN : COL_MUTED;
  String val = car.ambientAvailable ? String((int)round(car.ambientTempC)) + " C" : String("-- C");
  tft.fillRoundRect(x, y, w, h, 7, COL_PANEL);
  tft.drawRoundRect(x, y, w, h, 7, blend565(COL_BORDER, accent, 6));
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2); tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COL_PANEL);
  tft.drawString("VENKU", x + w/2, y + 9);
  tft.setTextFont(2);
  tft.setTextColor(accent, COL_PANEL);
  tft.drawString(val, x + w/2, y + h - 12);
  tft.setTextFont(1);
}

static void updateOverviewAmbientChip(int x, int y, int w, int h) {
  uint16_t accent = car.ambientAvailable ? COL_CYAN : COL_MUTED;
  String val = car.ambientAvailable ? String((int)round(car.ambientTempC)) + " C" : String("-- C");
  static String lastVal = "__";
  static uint16_t lastAccent = 0xFFFF;
  static int lastX=-1,lastY=-1,lastW=-1,lastH=-1;
  if (lastX==x && lastY==y && lastW==w && lastH==h && lastVal==val && lastAccent==accent) return;
  lastX=x; lastY=y; lastW=w; lastH=h; lastVal=val; lastAccent=accent;
  tft.fillRect(x + 4, y + 16, w - 8, h - 20, COL_PANEL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2); tft.setTextSize(1);
  tft.setTextColor(accent, COL_PANEL);
  tft.drawString(val, x + w/2, y + h - 12);
  tft.setTextFont(1);
}

static bool ensureLiveRpmMeterSprite() {
  if (liveRpmSpriteReady && liveRpmMeterSprite.created()) return true;
  if (liveRpmSpriteFailed) return false;

  // 8 bpp staci pro eWidget meter a vyrazne snizuje narok na souvisly heap.
  liveRpmMeterSprite.setColorDepth(8);
  if (liveRpmMeterSprite.createSprite(LIVE_RPM_SRC_W, LIVE_RPM_SRC_H) == nullptr) {
    liveRpmSpriteReady = false;
    liveRpmSpriteFailed = true;
    Serial.println("[UI] RPM MeterWidget sprite allocation FAILED");
    return false;
  }
  liveRpmMeterSprite.fillSprite(TFT_BLACK);

  liveRpmMeter.setZones(75, 100, 65, 75, 50, 65, 0, 50);
  liveRpmMeter.analogMeter(0, 0, 6000.0f, "RPM", "0", "1500", "3000", "4500", "6000");
  liveRpmMeter.updateNeedle(clampFloat(car.rpm, 0.0f, 6000.0f), 0);
  liveRpmSpriteReady = true;
  Serial.println("[UI] RPM MeterWidget sprite OK (8bpp 239x126)");
  return true;
}

static void drawRpmMeterFallback() {
  // Pouze nouzovy fallback pri nedostatku RAM. Normalni cesta vzdy pouziva
  // TFT_eWidget MeterWidget vyse. Fallback zajisti, ze PŘEHLED nezustane prazdny.
  const int x = 6, y = 42, w = 176, h = 94;
  tft.fillRoundRect(x, y, w, h, 8, COL_PANEL);
  tft.drawRoundRect(x, y, w, h, 8, COL_RED);
  int cx = x + w/2, cy = y + 69, r = 58;
  for (int a = 205; a <= 335; a += 10) {
    float rad = a * DEG_TO_RAD;
    int x1 = cx + (int)(cos(rad) * (r - 8));
    int y1 = cy + (int)(sin(rad) * (r - 8));
    int x2 = cx + (int)(cos(rad) * r);
    int y2 = cy + (int)(sin(rad) * r);
    uint16_t c = a > 305 ? COL_RED : (a > 280 ? COL_ORANGE : COL_GREEN);
    tft.drawLine(x1, y1, x2, y2, c);
  }
  float rpm = clampFloat(car.rpm, 0.0f, 6000.0f);
  float ang = (205.0f + (rpm / 6000.0f) * 130.0f) * DEG_TO_RAD;
  tft.drawLine(cx, cy, cx + (int)(cos(ang) * 46.0f), cy + (int)(sin(ang) * 46.0f), COL_RED);
  tft.fillCircle(cx, cy, 4, TFT_WHITE);
  tft.setTextDatum(MC_DATUM); tft.setTextFont(4); tft.setTextColor(rpmColor(rpm), COL_PANEL);
  tft.drawString(String((int)round(rpm)), cx, y + 63);
  tft.setTextFont(2); tft.setTextColor(TFT_WHITE, COL_PANEL); tft.drawString("RPM", cx, y + 84);
}

static bool ensureLiveRpmOutputSprite() {
  if (liveRpmOutputSprite && liveRpmOutputSprite->created()) return true;
  if (!liveRpmOutputSprite) {
    liveRpmOutputSprite = new TFT_eSprite(&tft);
    if (!liveRpmOutputSprite) return false;
    liveRpmOutputSprite->setColorDepth(8);
  }
  if (liveRpmOutputSprite->createSprite(LIVE_RPM_DST_W, LIVE_RPM_DST_H) == nullptr) {
    Serial.println("[UI] RPM output sprite allocation FAILED");
    return false;
  }
  liveRpmOutputSprite->fillSprite(TFT_BLACK);
  return true;
}

static void pushScaledLiveRpmMeter(bool forceFull = false) {
  (void)forceFull;
  if (!ensureLiveRpmMeterSprite()) {
    drawRpmMeterFallback();
    return;
  }
  if (!ensureLiveRpmOutputSprite()) {
    // Nouzova cesta bez velkeho framebufferu: po radcich. Pouzije se jen pri
    // nedostatku heapu; normalne se cely meter pushne jednim sprite blitem.
    uint16_t line[LIVE_RPM_DST_W];
    bool oldSwap = tft.getSwapBytes();
    tft.setSwapBytes(true);
    for (int dy = 0; dy < LIVE_RPM_DST_H; ++dy) {
      const int sy = LIVE_RPM_CROP_Y + (dy * LIVE_RPM_CROP_H) / LIVE_RPM_DST_H;
      for (int dx = 0; dx < LIVE_RPM_DST_W; ++dx) {
        const int sx = LIVE_RPM_CROP_X + (dx * LIVE_RPM_CROP_W) / LIVE_RPM_DST_W;
        line[dx] = liveRpmMeterSprite.readPixel(sx, sy);
      }
      tft.pushImage(LIVE_RPM_DST_X, LIVE_RPM_DST_Y + dy, LIVE_RPM_DST_W, 1, line);
    }
    tft.setSwapBytes(oldSwap);
    liveRpmFrameValid = true;
    return;
  }

  // Cely zmenseny frame nejprve vznikne mimo obrazovku. Na fyzicky TFT se
  // nasledne odesle jednim pushSprite(), takze neni videt prekreslovani radku.
  uint16_t line[LIVE_RPM_DST_W];
  for (int dy = 0; dy < LIVE_RPM_DST_H; ++dy) {
    const int sy = LIVE_RPM_CROP_Y + (dy * LIVE_RPM_CROP_H) / LIVE_RPM_DST_H;
    for (int dx = 0; dx < LIVE_RPM_DST_W; ++dx) {
      const int sx = LIVE_RPM_CROP_X + (dx * LIVE_RPM_CROP_W) / LIVE_RPM_DST_W;
      line[dx] = liveRpmMeterSprite.readPixel(sx, sy);
    }
    liveRpmOutputSprite->pushImage(0, dy, LIVE_RPM_DST_W, 1, line);
  }
  liveRpmOutputSprite->pushSprite(LIVE_RPM_DST_X, LIVE_RPM_DST_Y);
  liveRpmFrameValid = true;
}

static void drawLiveRpmNumeric() {
  if (!liveRpmSpriteReady) return;
  int rpmNow = (int)round(car.rpm);
  if (rpmNow == liveRpmNumericLast) return;
  uint16_t accent = rpmColor(car.rpm);
  const int w = 70, h = 17;
  const int x = LIVE_RPM_DST_X + (LIVE_RPM_DST_W - w) / 2;
  const int y = LIVE_RPM_DST_Y + LIVE_RPM_DST_H - h + 1;
  tft.fillRoundRect(x, y, w, h, 5, TFT_BLACK);
  tft.drawRoundRect(x, y, w, h, 5, blend565(COL_BORDER, accent, 8));
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2); tft.setTextSize(1);
  tft.setTextColor(accent, TFT_BLACK);
  tft.drawString(String(rpmNow), x + w/2, y + h/2);
  tft.setTextFont(1);
  liveRpmNumericLast = rpmNow;
}

static void updateLiveRpmOverview() {
  const int rpmNow = (int)round(clampFloat(car.rpm, 0.0f, 6000.0f));
  // 10 rpm hystereze odstrani zbytecne prekreslovani pri drobnem jitteru PIDu.
  if (liveRpmMeterLast != -99999 && abs(rpmNow - liveRpmMeterLast) < 10) {
    drawLiveRpmNumeric();
    return;
  }
  if (millis() - liveRpmWidgetLastMs < 120UL) return;
  liveRpmWidgetLastMs = millis();

  if (!ensureLiveRpmMeterSprite()) {
    drawRpmMeterFallback();
    liveRpmNumericLast = rpmNow;
    liveRpmMeterLast = rpmNow;
    return;
  }
  liveRpmMeter.updateNeedle((float)rpmNow, 0);
  pushScaledLiveRpmMeter(false);
  drawLiveRpmNumeric();
  liveRpmMeterLast = rpmNow;
}

static String overviewRegenValue() {
  if (dpf.regenActive) return "REGEN";
  float km = dpfEstimatedKmToRegen();
  if (isnan(km)) return "--";
  return String((int)round(km));
}

void drawLiveScreen() {
  currentScreen = SCREEN_LIVE;
  tft.fillScreen(TFT_BLACK);
  drawHeader("PREHLED", true);

  // Vyvazeny dashboard: RPM je dominantni, ale nezahlti cely 320x240 displej.
  tft.fillRoundRect(6, 42, 184, 118, 8, COL_PANEL);
  tft.drawRoundRect(6, 42, 184, 118, 8, blend565(COL_BORDER, COL_CYAN, 7));
  liveRpmFrameValid = false;
  liveRpmNumericLast = -99999;
  liveRpmMeterLast = -99999;
  if (ensureLiveRpmMeterSprite()) {
    liveRpmMeter.updateNeedle(clampFloat(car.rpm, 0.0f, 6000.0f), 0);
  }
  pushScaledLiveRpmMeter(true);
  drawLiveRpmNumeric();
  liveRpmMeterLast = (int)round(clampFloat(car.rpm, 0.0f, 6000.0f));
  liveRpmWidgetLastMs = millis();

  float boost = currentBoostBar();
  drawOverviewValueCard(196, 42, 118, 56, "TEPLOTA MOTORU",
                        isnan(car.coolantC) ? String("--") : String((int)round(car.coolantC)),
                        "C", (!isnan(car.coolantC) && car.coolantC >= 105.0f) ? COL_RED : COL_GREEN);

  drawOverviewValueCard(196, 104, 118, 56, "TLAK TURBA",
                        isnan(boost) ? String("--") : String(boost, 1),
                        "bar", COL_BLUE);

  // U neoverene ECU nepredstirame, ze je rozsirene DPF jen "offline".
  bool advDpf = vehicleHasAdvancedDpf();
  String regenValue = advDpf ? overviewRegenValue() : String("N/A");
  drawOverviewValueCard(6, 168, 178, 52, advDpf ? "DPF DO REGEN." : "DPF PROFIL",
                        regenValue,
                        (advDpf && !dpf.regenActive) ? "km" : "",
                        advDpf ? (dpf.regenActive ? COL_ORANGE : (regenValue == "--" ? COL_MUTED : COL_GREEN)) : COL_MUTED);

  // Venkovni teplota je pouze doplnkova mala karta.
  drawOverviewAmbientChip(190, 168, 124, 52);
}

void updateLiveDynamic() {
  if (currentScreen != SCREEN_LIVE) return;
  updateHeaderStatus();
  updateLiveRpmOverview();

  float boost = currentBoostBar();
  updateOverviewValueCard(196, 42, 118, 56,
                          isnan(car.coolantC) ? String("--") : String((int)round(car.coolantC)),
                          "C", (!isnan(car.coolantC) && car.coolantC >= 105.0f) ? COL_RED : COL_GREEN);

  updateOverviewValueCard(196, 104, 118, 56,
                          isnan(boost) ? String("--") : String(boost, 1),
                          "bar", COL_BLUE);

  bool advDpf = vehicleHasAdvancedDpf();
  String regenValue = advDpf ? overviewRegenValue() : String("N/A");
  updateOverviewValueCard(6, 168, 178, 52,
                          regenValue,
                          (advDpf && !dpf.regenActive) ? "km" : "",
                          advDpf ? (dpf.regenActive ? COL_ORANGE : (regenValue == "--" ? COL_MUTED : COL_GREEN)) : COL_MUTED);

  updateOverviewAmbientChip(190, 168, 124, 52);
}

// =========================== KNIHA JIZD =======================

void drawTripTabs() {
  const int y = 42, h = 22;
  tft.fillRoundRect(12, y, 144, h, 6, tripView == TRIP_VIEW_CURRENT ? COL_CYAN : COL_PANEL);
  tft.drawRoundRect(12, y, 144, h, 6, tripView == TRIP_VIEW_CURRENT ? COL_CYAN : COL_BORDER);
  tft.fillRoundRect(164, y, 144, h, 6, tripView == TRIP_VIEW_RECENT ? COL_CYAN : COL_PANEL);
  tft.drawRoundRect(164, y, 144, h, 6, tripView == TRIP_VIEW_RECENT ? COL_CYAN : COL_BORDER);
  tft.setTextDatum(MC_DATUM); tft.setTextFont(2);
  tft.setTextColor(tripView == TRIP_VIEW_CURRENT ? TFT_BLACK : COL_TEXT,
                   tripView == TRIP_VIEW_CURRENT ? COL_CYAN : COL_PANEL);
  tft.drawString("AKTUALNI", 84, y + 11);
  tft.setTextColor(tripView == TRIP_VIEW_RECENT ? TFT_BLACK : COL_TEXT,
                   tripView == TRIP_VIEW_RECENT ? COL_CYAN : COL_PANEL);
  tft.drawString("POSLEDNI", 236, y + 11);
  tft.setTextDatum(TL_DATUM);
}

void drawTripCurrentPanel() {
  unsigned long durationSec = currentTripElapsedSec();
  float avgSpeed = durationSec > 0 ? trip.distanceKm / (durationSec / 3600.0f) : 0;
  float avgCons = (trip.fuelAvailable && trip.distanceKm > 0.05f) ? trip.fuelLiters * 100.0f / trip.distanceKm : NAN;

  tft.fillRoundRect(12, 68, 296, 58, 9, COL_PANEL);
  tft.drawRoundRect(12, 68, 296, 58, 9, trip.active ? COL_GREEN : COL_BORDER);
  tft.setTextDatum(TL_DATUM); tft.setTextFont(2);
  tft.setTextColor(trip.active ? COL_GREEN : COL_LABEL, COL_PANEL);
  String name = trip.active ? tripAutoName(trip.startEpoch) : String("CEKAM NA JIZDU");
  if (trip.recovered && trip.active) name += "  OBNOVENA";
  tft.drawString(name, 20, 73);

  const int cx[3] = {20, 116, 214};
  String vals[3] = {String(trip.distanceKm, 1), formatDuration(durationSec), !isnan(avgCons) ? String(avgCons, 1) : String("--")};
  String labs[3] = {"KM", "DOBA", "L/100"};
  for (int i=0;i<3;i++) {
    tft.setTextFont(2); tft.setTextColor(i==0?COL_CYAN:(i==1?COL_TEXT:COL_YELLOW), COL_PANEL);
    tft.drawString(vals[i], cx[i], 89);
    tft.setTextFont(2); tft.setTextColor(COL_MUTED, COL_PANEL);
    tft.drawString(labs[i], cx[i], 108);
  }
  tft.setTextDatum(TR_DATUM); tft.setTextFont(1); tft.setTextColor(COL_MUTED, COL_PANEL);
  String extra = "PAL " + (trip.fuelAvailable ? String(trip.fuelLiters, 2)+"L" : String("--")) + "  MAX " + String(trip.maxSpeedKmh,0);
  tft.drawString(extra, 300, 75);
  tft.setTextDatum(TL_DATUM);
  (void)avgSpeed;
}

void drawTripDailySummary() {
  TripDailySummary d = getTodayTripSummary();
  tft.fillRoundRect(12, 131, 296, 31, 8, COL_PANEL2);
  tft.drawRoundRect(12, 131, 296, 31, 8, COL_BORDER);
  tft.setTextFont(1); tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_LABEL, COL_PANEL2); tft.drawString("DNES", 20, 137);
  if (!clockIsValid()) {
    tft.setTextColor(COL_MUTED, COL_PANEL2); tft.drawString("nastav datum/cas pro denni souhrn", 58, 137);
    return;
  }
  float cons = (d.fuelAvailable && d.distanceKm > 0.05f) ? d.fuelLiters * 100.0f / d.distanceKm : NAN;
  tft.setTextColor(COL_CYAN, COL_PANEL2); tft.drawString(String(d.distanceKm,1)+" km", 58, 137);
  tft.setTextColor(COL_TEXT, COL_PANEL2); tft.drawString(formatDuration(d.durationSec), 123, 137);
  tft.setTextColor(COL_YELLOW, COL_PANEL2); tft.drawString(isnan(cons)?"-- L/100":String(cons,1)+" L/100", 195, 137);
  tft.setTextDatum(TR_DATUM); tft.setTextColor(COL_MUTED, COL_PANEL2); tft.drawString(String(d.count)+" jizd", 300, 151);
  tft.setTextDatum(TL_DATUM);
}

void drawConsumptionTrend() {
  const int x=12, y=167, w=296, h=38;
  tft.fillRoundRect(x,y,w,h,8,COL_PANEL);
  tft.drawRoundRect(x,y,w,h,8,COL_BORDER);
  tft.setTextFont(1); tft.setTextDatum(TL_DATUM); tft.setTextColor(COL_LABEL,COL_PANEL);
  tft.drawString("TREND SPOTREBY / 8 JIZD", 20, 171);

  TripRecord rec[14];
  int n=loadRecentTrips(rec,14);
  float v[8]; int c=0;
  for (int i=n-1;i>=0 && c<8;i--) {
    if (!tripRecordIsShort(rec[i]) && !isnan(rec[i].avgConsumption) && rec[i].avgConsumption>=2.0f && rec[i].avgConsumption<=20.0f)
      v[c++]=rec[i].avgConsumption;
  }
  if (c<2) {
    tft.setTextColor(COL_MUTED,COL_PANEL); tft.drawString("Zatim malo dat", 20, 187); return;
  }
  // v[] je od nejnovejsi; vykreslime chronologicky zleva doprava.
  float mn=v[0], mx=v[0]; for(int i=1;i<c;i++){mn=min(mn,v[i]);mx=max(mx,v[i]);}
  if (mx-mn<0.5f) { mn-=0.25f; mx+=0.25f; }
  int gx=118, gy=181, gw=178, gh=17;
  int px=-1,py=-1;
  for(int j=0;j<c;j++) {
    int i=c-1-j;
    int xx=gx + (c==1?0:(j*gw/(c-1)));
    int yy=gy+gh-(int)((v[i]-mn)*gh/(mx-mn));
    if(px>=0) tft.drawLine(px,py,xx,yy,COL_YELLOW);
    tft.fillCircle(xx,yy,2,COL_CYAN); px=xx;py=yy;
  }
  tft.setTextColor(COL_MUTED,COL_PANEL);
  tft.drawString(String(mn,1), 20, 187);
  tft.drawString("-", 52, 187);
  tft.drawString(String(mx,1)+" L/100", 62, 187);
}

void drawTripsCurrentView() {
  tripView = TRIP_VIEW_CURRENT;
  tft.fillScreen(COL_BG);
  drawHeader("KNIHA JIZD", true);
  drawTripTabs();
  drawTripCurrentPanel();
  drawTripDailySummary();
  drawConsumptionTrend();
  tft.setTextDatum(MC_DATUM); tft.setTextFont(1); tft.setTextColor(COL_MUTED,COL_BG);
  tft.drawString("kratke jizdy jsou oznaceny, ne mazany",160,222);
  tft.setTextDatum(TL_DATUM);
  tripsDynamicCacheValid=false;
}

void drawTripsRecentView() {
  tripView = TRIP_VIEW_RECENT;
  tripDeleteArmed=false;
  tft.fillScreen(COL_BG);
  drawHeader("KNIHA JIZD", true);
  drawTripTabs();
  TripStats st=getTripStats();
  tft.fillRoundRect(12,68,296,33,8,COL_PANEL2); tft.drawRoundRect(12,68,296,33,8,COL_BORDER);
  tft.setTextFont(1); tft.setTextColor(COL_TEXT,COL_PANEL2); tft.setTextDatum(TL_DATUM);
  tft.drawString("CELKEM " + String(st.count) + "  |  " + String(st.totalKm,0) + " km",20,74);
  tft.setTextColor(COL_MUTED,COL_PANEL2);
  String recLine="REKORD " + String(st.longestKm,1) + " km  |  BEST " + (isnan(st.bestConsumption)?String("--"):String(st.bestConsumption,1)+" L/100");
  tft.drawString(recLine,20,87);

  int total=countTripRecords();
  int pageCount=max(1,(total+4)/5);
  if (tripRecentPage>=pageCount) tripRecentPage=pageCount-1;
  for(int row=0;row<5;row++) {
    int newestIndex=tripRecentPage*5+row;
    int id=total-1-newestIndex;
    int yy=105+row*20;
    tft.fillRoundRect(12,yy,296,18,5,COL_PANEL);
    if(id<0) continue;
    TripRecord r; if(!loadTripById(id,r)) continue;
    bool sh=tripRecordIsShort(r);
    tft.setTextFont(1); tft.setTextDatum(TL_DATUM);
    tft.setTextColor(sh?COL_ORANGE:COL_CYAN,COL_PANEL);
    tft.drawString(tripAutoName(r.startEpoch,id),18,yy+5);
    if(sh) { tft.setTextColor(COL_ORANGE,COL_PANEL); tft.drawString("KRATKA",112,yy+5); }
    tft.setTextDatum(TR_DATUM); tft.setTextColor(COL_GREEN,COL_PANEL);
    tft.drawString(String(r.distanceKm,1)+" km",300,yy+5); tft.setTextDatum(TL_DATUM);
  }
  tft.setTextDatum(MC_DATUM); tft.setTextFont(1);
  if(tripRecentPage>0){tft.fillRoundRect(12,210,72,24,6,COL_PANEL2);tft.setTextColor(COL_TEXT,COL_PANEL2);tft.drawString("< NOVEJSI",48,222);}
  tft.setTextColor(COL_MUTED,COL_BG); tft.drawString(String(tripRecentPage+1)+"/"+String(pageCount),160,222);
  if(tripRecentPage+1<pageCount){tft.fillRoundRect(236,210,72,24,6,COL_PANEL2);tft.setTextColor(COL_TEXT,COL_PANEL2);tft.drawString("STARSI >",272,222);}
  tft.setTextDatum(TL_DATUM);
}

void drawTripDetailView(int id) {
  TripRecord r;
  if(!loadTripById(id,r)){drawTripsRecentView();return;}
  tripView=TRIP_VIEW_DETAIL; tripSelectedId=id;
  tft.fillScreen(COL_BG); drawHeader("DETAIL JIZDY", true);
  bool sh=tripRecordIsShort(r);
  tft.setTextDatum(TL_DATUM); tft.setTextFont(2); tft.setTextColor(COL_CYAN,COL_BG);
  tft.drawString(tripAutoName(r.startEpoch,id),14,46);
  if(sh){tft.setTextFont(1);tft.setTextColor(COL_ORANGE,COL_BG);tft.drawString("KRATKA JIZDA",220,50);}
  tft.fillRoundRect(12,70,296,116,9,COL_PANEL); tft.drawRoundRect(12,70,296,116,9,COL_BORDER);
  struct Cell { const char* l; String v; } cells[8] = {
    {"VZDALENOST",String(r.distanceKm,2)+" km"},
    {"DOBA",formatDuration(r.durationSec)},
    {"SPOTREBA",isnan(r.avgConsumption)?String("--"):String(r.avgConsumption,1)+" L/100"},
    {"PALIVO",isnan(r.fuelLiters)?String("--"):String(r.fuelLiters,2)+" L"},
    {"PRUMER",String(r.avgSpeedKmh,0)+" km/h"},
    {"MAX",String(r.maxSpeedKmh,0)+" km/h"},
    {"START",formatDateTime(r.startEpoch)},
    {"KONEC",formatDateTime(r.endEpoch)}
  };
  for(int i=0;i<8;i++) {
    int col=i%2,row=i/2,xx=20+col*145,yy=78+row*26;
    tft.setTextFont(1);tft.setTextColor(COL_MUTED,COL_PANEL);tft.drawString(cells[i].l,xx,yy);
    tft.setTextColor((i<2)?COL_CYAN:COL_TEXT,COL_PANEL);tft.drawString(cells[i].v,xx,yy+11);
  }
  uint16_t bc=tripDeleteArmed?COL_RED:COL_PANEL2;
  tft.fillRoundRect(74,195,172,32,8,bc);tft.drawRoundRect(74,195,172,32,8,COL_RED);
  tft.setTextDatum(MC_DATUM);tft.setTextFont(2);tft.setTextColor(tripDeleteArmed?TFT_WHITE:COL_RED,bc);
  tft.drawString(tripDeleteArmed?"POTVRDIT SMAZANI":"SMAZAT JIZDU",160,211);
  tft.setTextDatum(TL_DATUM);
}

void drawTripsScreen() {
  currentScreen=SCREEN_TRIPS;
  if(tripView==TRIP_VIEW_RECENT) drawTripsRecentView();
  else if(tripView==TRIP_VIEW_DETAIL && tripSelectedId>=0) drawTripDetailView(tripSelectedId);
  else drawTripsCurrentView();
}

void updateTripsDynamic() {
  if(currentScreen!=SCREEN_TRIPS) return;
  updateHeaderStatus();
  static unsigned long lastPaint=0;
  if(tripView!=TRIP_VIEW_CURRENT || millis()-lastPaint<1500UL) return;
  lastPaint=millis();
  // Aktualizujeme jen tri obsahove bloky, hlavicka ani taby se nepremazavaji.
  drawTripCurrentPanel();
  drawTripDailySummary();
  drawConsumptionTrend();
}

// Forward declaration: Kniha jizd vola alert pred jeho definici nize v souboru.
void showAlert(const String &title, const String &line1, const String &line2,
               uint16_t color, unsigned long durationMs);

void handleTripsTouch(int x,int y) {
  if(tripView==TRIP_VIEW_DETAIL) {
    if(inside(x,y,8,5,34,32)) { tripDeleteArmed=false; drawTripsRecentView(); return; }
    if(inside(x,y,74,195,172,32)) {
      if(!tripDeleteArmed || millis()>tripDeleteArmUntil) {
        tripDeleteArmed=true; tripDeleteArmUntil=millis()+3500UL; drawTripDetailView(tripSelectedId); return;
      }
      int deletedId=tripSelectedId;
      tripDeleteArmed=false;
      if(deleteTripById(deletedId)) {
        tripSelectedId=-1;
        drawTripsRecentView();
        showAlert("JIZDA SMAZANA","Zaznam byl odstranen","",COL_GREEN,1800);
      } else showAlert("CHYBA","Jizdu se nepodarilo smazat","",COL_RED,2500);
      return;
    }
    return;
  }

  if(inside(x,y,8,5,34,32)) { tripView=TRIP_VIEW_CURRENT; drawMainMenu(); return; }
  if(inside(x,y,12,42,144,22)) { drawTripsCurrentView(); return; }
  if(inside(x,y,164,42,144,22)) { drawTripsRecentView(); return; }

  if(tripView==TRIP_VIEW_RECENT) {
    int total=countTripRecords();
    for(int row=0;row<5;row++) {
      int yy=105+row*20;
      if(!inside(x,y,12,yy,296,18)) continue;
      int id=total-1-(tripRecentPage*5+row);
      if(id>=0) drawTripDetailView(id);
      return;
    }
    int pageCount=max(1,(total+4)/5);
    if(inside(x,y,12,210,72,24) && tripRecentPage>0) { tripRecentPage--; drawTripsRecentView(); return; }
    if(inside(x,y,236,210,72,24) && tripRecentPage+1<pageCount) { tripRecentPage++; drawTripsRecentView(); return; }
  }
}

// ======================== HISTORIE TANKOVANI =================

void drawFuelingScreen() {
  currentScreen = SCREEN_FUELING;
  tft.fillScreen(COL_BG);
  drawHeader("HISTORIE TANKOVANI", true);

  float currentL = (car.fuelLevelAvailable && !isnan(car.fuelLevelPct))
                   ? tankCapacityL * clampFloat(car.fuelLevelPct, 0.0f, 100.0f) / 100.0f : NAN;
  tft.fillRoundRect(12, 48, 296, 50, 9, COL_PANEL);
  tft.drawRoundRect(12, 48, 296, 50, 9, COL_CYAN);
  tft.setTextDatum(TL_DATUM); tft.setTextFont(1); tft.setTextColor(COL_LABEL, COL_PANEL);
  tft.drawString("AUTOMATICKA DETEKCE", 22, 56);
  tft.setTextFont(4); tft.setTextColor(car.fuelLevelAvailable ? COL_CYAN : COL_MUTED, COL_PANEL);
  tft.drawString(isnan(currentL) ? String("--") : String(currentL, 1), 22, 68);
  tft.setTextFont(2); tft.setTextColor(COL_TEXT, COL_PANEL); tft.drawString("L v nadrzi", 90, 75);
  tft.setTextDatum(TR_DATUM); tft.setTextFont(1); tft.setTextColor(COL_GREEN, COL_PANEL);
  tft.drawString("zapis od +" + String(fuelingMinRiseL, 1) + " L / 3 vzorky", 298, 57);
  tft.setTextColor(trackedOdometerConfigured ? COL_TEXT : COL_ORANGE, COL_PANEL);
  String odo = trackedOdometerConfigured ? (String(trackedOdometerKm, 0) + " km") : String("KM nastav pres WEB");
  tft.drawString(odo, 298, 83);

  tft.setTextDatum(TL_DATUM); tft.setTextFont(1); tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("POSLEDNI TANKOVANI", 14, 108);

  FuelingRecord rec[4];
  int count = loadRecentFuelings(rec, 4);
  int y = 122;
  if (!count) {
    tft.setTextColor(COL_MUTED, COL_BG);
    tft.drawString("Zatim zadne automaticky zaznamenane tankovani.", 18, 139);
  } else {
    for (int i = count - 1; i >= 0; i--) {
      const FuelingRecord &r = rec[i];
      tft.fillRoundRect(12, y, 296, 25, 5, COL_PANEL);
      tft.setTextDatum(TL_DATUM); tft.setTextFont(1); tft.setTextColor(COL_TEXT, COL_PANEL);
      String when = r.epoch > 0 ? formatDateTime(r.epoch) : String("bez data");
      tft.drawString(when, 17, y + 4);
      tft.setTextDatum(TR_DATUM); tft.setTextColor(COL_GREEN, COL_PANEL);
      tft.drawString(isnan(r.addedLiters) ? String("+") : String("+") + String(r.addedLiters, 1) + " L", 301, y + 4);
      tft.setTextDatum(TL_DATUM); tft.setTextColor(COL_MUTED, COL_PANEL);
      String detail = (isnan(r.beforeLiters) || isnan(r.afterLiters)) ? String("hladina --") : (String(r.beforeLiters, 1) + " -> " + String(r.afterLiters, 1) + " L");
      if (!isnan(r.odometerKm)) detail += "  |  " + String(r.odometerKm, 0) + " km";
      tft.drawString(detail, 17, y + 14);
      y += 27;
    }
  }
  tft.setTextFont(1); tft.setTextDatum(TL_DATUM);
}

// =========================== NASTAVENI ========================

void drawSettingRow(int y, const String &label, const String &value, uint16_t accent = COL_CYAN) {
  tft.fillRoundRect(12, y, 296, 38, 8, COL_PANEL);
  tft.drawRoundRect(12, y, 296, 38, 8, COL_BORDER);
  tft.setTextDatum(ML_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COL_LABEL, COL_PANEL);
  tft.drawString(label, 20, y + 19);

  String shown = value;
  if (shown.length() > 15) shown = shown.substring(0, 13) + "..";
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(accent, COL_PANEL);
  tft.drawString(shown, 205, y + 19);
  tft.setTextFont(1);
}

static void drawSettingsTile(int x, int y, int w, int h, const String &title,
                             const String &sub, uint16_t accent) {
  uint16_t bg = blend565(COL_BG, accent, 1);
  tft.fillRoundRect(x, y, w, h, 9, bg);
  tft.drawRoundRect(x, y, w, h, 9, blend565(COL_BORDER, accent, 9));
  tft.drawFastHLine(x + 12, y + 5, w - 24, accent);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COL_TEXT, bg);
  tft.drawString(title, x + w / 2, y + 24);
  tft.setTextFont(2); // V16.29: i pomocny text je citelny za jizdy
  tft.setTextColor(COL_MUTED, bg);
  tft.drawString(sub, x + w / 2, y + 48);
  tft.setTextFont(1);
}

void drawSettingsScreen() {
  currentScreen = SCREEN_SETTINGS;
  settingsSubpageContext = false;
  tft.fillScreen(COL_BG);
  drawHeader("NASTAVENI", true);

  drawSettingsTile(6,   46, 100, 72, "DISPLEJ", "jas / touch", COL_YELLOW);
  drawSettingsTile(110, 46, 100, 72, "OBD / DATA", "ELM / data", COL_CYAN);
  drawSettingsTile(214, 46, 100, 72, "DPF", "alarm/limit", COL_ORANGE);
  drawSettingsTile(6,   124,100, 72, "POWER", "uspory", COL_BLUE);
  drawSettingsTile(110, 124,100, 72, "SYSTEM", "cas / FW", COL_GREEN);
  drawSettingsTile(214, 124,100, 72, "WEB", "servis", COL_CYAN);

  tft.setTextDatum(MC_DATUM); tft.setTextFont(2); tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString(settingsDirty ? "ZMENY CEKAJI NA ULOZENI" : "ZMENY SE UKLADAJI AUTOMATICKY", 160, 218);
  tft.setTextFont(1);
}

void drawSettingsMoreScreen() {
  // DISPLAY
  currentScreen = SCREEN_SETTINGS_MORE;
  tft.fillScreen(COL_BG);
  drawHeader("DISPLEJ", true);

  drawSettingRow(46,  "JAS DEN", String(displayBrightness), COL_YELLOW);
  drawButton(218, 51, 38, 28, "-"); drawButton(264, 51, 38, 28, "+");

  drawSettingRow(88,  "JAS NOC", String(displayNightBrightness), COL_BLUE);
  drawButton(218, 93, 38, 28, "-"); drawButton(264, 93, 38, 28, "+");

  drawSettingRow(130, "AUTO DEN / NOC", autoDayNightBrightness ? "ZAP" : "VYP",
                 autoDayNightBrightness ? COL_GREEN : COL_MUTED);
  drawButton(245, 135, 57, 28, autoDayNightBrightness ? "ON" : "OFF", true, autoDayNightBrightness);

  drawSettingRow(172, "TOUCH FEEDBACK", touchFeedbackEnabled ? "ZAP" : "VYP",
                 touchFeedbackEnabled ? COL_GREEN : COL_MUTED);
  drawButton(245, 177, 57, 28, touchFeedbackEnabled ? "ON" : "OFF", true, touchFeedbackEnabled);

  tft.setTextDatum(ML_DATUM); tft.setTextFont(2); tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString(String("SILA TOUCH: ") + String(touchFeedbackStrength), 16, 220);
  tft.setTextDatum(MR_DATUM);
  tft.drawString("-   +", 300, 220);
  tft.setTextFont(1);
}

static const char *obdModeLabel() {
  if (obdDataMode == 0) return "ECO";
  if (obdDataMode == 1) return "NORMAL";
  return "FAST";
}

void drawSettingsTextScreen() {
  // OBD / DATA
  currentScreen = SCREEN_SETTINGS_TEXT;
  tft.fillScreen(COL_BG);
  drawHeader("OBD / DATA", true);

  String elmState = car.elmConnected ? "PRIPOJENO" : (reconnectInProgress || reconnectRequested ? "PRIPOJUJI" : "ODPOJENO");
  drawSettingRow(46, "ELM327", elmState, car.elmConnected ? COL_GREEN : COL_YELLOW);
  drawButton(218, 51, 84, 28, car.elmConnected ? "ODPOJIT" : "PRIPOJIT", true, car.elmConnected);

  drawSettingRow(88, "AUTO PRIPOJENI", autoElmReconnect ? "ZAP" : "VYP",
                 autoElmReconnect ? COL_GREEN : COL_MUTED);
  drawButton(245, 93, 57, 28, autoElmReconnect ? "ON" : "OFF", true, autoElmReconnect);

  drawSettingRow(130, "RYCHLOST DAT", obdModeLabel(), obdDataMode == 2 ? COL_GREEN : COL_CYAN);
  drawButton(218, 135, 84, 28, "ZMENA");

  drawSettingRow(172, "VYCHOZI ELM", hasDefaultDevice ? defaultName : "NENI", hasDefaultDevice ? COL_CYAN : COL_YELLOW);
  drawButton(218, 177, 84, 28, "VYBRAT");

  drawButton(88, 210, 144, 26, "POKROCILE", true, false);
}

void drawSettingsDpfScreen() {
  currentScreen = SCREEN_SETTINGS_DPF;
  tft.fillScreen(COL_BG);
  drawHeader("DPF NASTAVENI", true);

  drawSettingRow(46, "REGEN UPOZORNENI", alarmRegenEnabled ? "ZAP" : "VYP",
                 alarmRegenEnabled ? COL_ORANGE : COL_MUTED);
  drawButton(245, 51, 57, 28, alarmRegenEnabled ? "ON" : "OFF", true, alarmRegenEnabled);

  drawSettingRow(88, "REGEN BRZY", alarmRegenSoonEnabled ? "ZAP" : "VYP",
                 alarmRegenSoonEnabled ? COL_ORANGE : COL_MUTED);
  drawButton(245, 93, 57, 28, alarmRegenSoonEnabled ? "ON" : "OFF", true, alarmRegenSoonEnabled);

  drawSettingRow(130, "PRAH VAROVANI", String(alarmRegenSoonKm, 0) + " km", COL_YELLOW);
  drawButton(218, 135, 38, 28, "-"); drawButton(264, 135, 38, 28, "+");

  drawSettingRow(172, "DPF 100 %", String(dpfRegenLimitG, 1) + " g", COL_CYAN);
  drawButton(218, 177, 38, 28, "-"); drawButton(264, 177, 38, 28, "+");

  tft.setTextDatum(MC_DATUM); tft.setTextFont(2); tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Predikce se uci z historie regeneraci", 160, 220);
  tft.setTextFont(1);
}

void drawSettingsSystemScreen() {
  currentScreen = SCREEN_SETTINGS_SYSTEM;
  tft.fillScreen(COL_BG);
  drawHeader("SYSTEM", true);

  String clockValue = clockIsValid() ? formatDateTime(time(nullptr)) : String("NASTAV PRES WEB");
  drawSettingRow(46, "DATUM / CAS", clockValue, clockIsValid() ? COL_GREEN : COL_YELLOW);
  drawSettingRow(88, "FIRMWARE", String("V") + FW_VERSION, COL_CYAN);
  drawSettingRow(130, "VOLNA RAM", String(ESP.getFreeHeap() / 1024UL) + " kB", COL_GREEN);
  drawSettingRow(172, "UPTIME", formatDuration(millis() / 1000UL), COL_TEXT);

  drawButton(12, 212, 140, 25, "POKROCILE", true, false);
  drawButton(168, 212, 140, 25, settingsResetArmed ? "POTVRDIT RESET" : "VYCHOZI", true, settingsResetArmed);
}

void drawSettingsAdvancedScreen() {
  currentScreen = SCREEN_SETTINGS_ADVANCED;
  tft.fillScreen(COL_BG);
  drawHeader("PROFIL VOZIDLA", true);

  drawSettingRow(46, "NASTAVENY PROFIL", engineProfileShortLabel(configuredEngineProfile), COL_CYAN);
  drawButton(218, 51, 84, 28, "ZMENA");
  drawSettingRow(88, "AKTIVNI PROFIL", engineProfileShortLabel(activeEngineProfile), engineProfileDetected ? COL_GREEN : COL_YELLOW);
  drawSettingRow(130, "OBD PROTOKOL", car.protocol.length() ? car.protocol : String("--"), COL_TEXT);
  String ecuInfo = car.ecuPartNumber.length() ? car.ecuPartNumber : engineDetectionNote;
  drawSettingRow(172, "ECU", ecuInfo, COL_TEXT);

  tft.setTextDatum(MC_DATUM); tft.setTextFont(1); tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("AUTO = doporuceno. Zmena profilu se pouzije", 160, 214);
  tft.drawString("pri pristim pripojeni ELM327.", 160, 227);
}

void drawWifiScreen() {
  currentScreen = SCREEN_WIFI;
  tft.fillScreen(COL_BG);
  drawHeader("WI-FI / WEB", true);

  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);

  if (!serviceWifiActive) {
    const bool elmConnecting = !car.elmConnected && (reconnectInProgress || reconnectRequested) && !offlineMode;
    const bool elmStopping = !car.elmConnected && reconnectInProgress && (offlineMode || reconnectAbortRequested);
    uint16_t elmColor = COL_RED;
    String elmState = "ODPOJENO";
    if (car.elmConnected) { elmColor = COL_GREEN; elmState = "PRIPOJENO"; }
    else if (elmStopping) { elmColor = COL_RED; elmState = "ZASTAVUJI..."; }
    else if (elmConnecting) { elmColor = COL_YELLOW; elmState = "PRIPOJUJI..."; }
    else if (offlineMode || reconnectAbortRequested) { elmColor = COL_MUTED; elmState = "ZASTAVENO"; }

    tft.fillRoundRect(12, 48, 296, 58, 9, COL_PANEL);
    tft.drawRoundRect(12, 48, 296, 58, 9, elmColor);
    tft.setTextColor(COL_LABEL, COL_PANEL); tft.drawString("ELM327 / BLUETOOTH", 22, 57);
    tft.setTextFont(2); tft.setTextColor(elmColor, COL_PANEL); tft.drawString(elmState, 22, 76);

    if (elmConnecting || elmStopping) {
      drawElmStopButton(58, 113, 204, 34, elmConnecting);
    } else if (car.elmConnected) {
      drawButton(58, 113, 204, 34, "ODPOJIT ELM", true, false);
    } else {
      drawButton(58, 113, 204, 34, "PRIPOJIT ELM", true, true);
    }

    tft.fillRoundRect(12, 154, 296, 28, 8, COL_PANEL2);
    tft.setTextColor(elmStopping ? COL_RED : COL_YELLOW, COL_PANEL2);
    if (elmStopping) tft.drawString("STOP prijat - cekam jen na navrat BT connect().", 20, 164);
    else tft.drawString("Servisni WEB lze spustit jen s odpojenym ELM.", 20, 164);

    const bool webReady = offlineMode && !reconnectInProgress && !reconnectRequested;
    drawButton(72, 191, 176, 38, webReady ? "ZAPNOUT WEB" : "WEB CEKA", webReady, webReady);
  } else {
    tft.fillRoundRect(12, 48, 296, 82, 9, COL_PANEL);
    tft.drawRoundRect(12, 48, 296, 82, 9, COL_GREEN);
    tft.setTextColor(COL_LABEL, COL_PANEL); tft.drawString("ACCESS POINT", 22, 57);
    tft.setTextFont(2); tft.setTextColor(COL_CYAN, COL_PANEL); tft.drawString(String(AP_SSID), 22, 73);
    tft.setTextFont(1); tft.setTextColor(COL_TEXT, COL_PANEL);
    tft.drawString("Heslo: " + String(AP_PASS), 22, 96);
    tft.drawString("Web: 192.168.4.1   |   klienti: " + String(apClientCount), 22, 112);

    tft.fillRoundRect(12, 138, 296, 42, 9, COL_PANEL2);
    tft.setTextColor(COL_GREEN, COL_PANEL2); tft.drawString("WEB LITE: jizdy / PDF / servis / nastaveni / INFO", 22, 148);
    tft.setTextColor(COL_MUTED, COL_PANEL2); tft.drawString("Bluetooth je po dobu WEBu vypnuty.", 22, 164);

    drawButton(72, 191, 176, 38, "VYPNOUT WEB", true, false);
  }
}

void drawInfoScreen() {
  currentScreen = SCREEN_INFO;
  tft.fillScreen(COL_BG);
  drawHeader("INFO", true);

  tft.fillRoundRect(12, 48, 296, 72, 10, COL_PANEL);
  tft.drawRoundRect(12, 48, 296, 72, 10, COL_CYAN);
  tft.setTextDatum(TL_DATUM); tft.setTextFont(2); tft.setTextColor(COL_CYAN, COL_PANEL);
  tft.drawString("VLASTNIK / KONTAKT", 22, 57);
  tft.setTextColor(COL_TEXT, COL_PANEL); tft.drawString(developerName.length() ? developerName : "NASTAVTE PRES WEB", 22, 78);
  tft.setTextFont(1); tft.setTextColor(COL_LABEL, COL_PANEL);
  tft.drawString(developerEmail.length() ? developerEmail : "E-mail nenastaven", 22, 99);
  if (developerPhone.length()) { tft.setTextDatum(TR_DATUM); tft.drawString(developerPhone, 298, 99); }

  tft.fillRoundRect(12, 128, 296, 50, 10, COL_PANEL);
  tft.setTextDatum(TL_DATUM); tft.setTextFont(1); tft.setTextColor(COL_LABEL, COL_PANEL);
  tft.drawString("FIRMWARE", 22, 137); tft.setTextFont(2); tft.setTextColor(COL_GREEN, COL_PANEL);
  tft.drawString(String(FW_VERSION), 22, 152);
  tft.setTextDatum(TR_DATUM); tft.setTextFont(1); tft.setTextColor(COL_TEXT, COL_PANEL);
  tft.drawString("ESP32-2432S028R", 298, 158);

  tft.fillRoundRect(12, 186, 296, 44, 10, COL_PANEL2);
  tft.setTextDatum(TL_DATUM); tft.setTextFont(1); tft.setTextColor(COL_LABEL, COL_PANEL2);
  tft.drawString("VOZIDLO / ECU", 22, 194);
  tft.setTextColor(COL_TEXT, COL_PANEL2);
  String ecu = car.ecuPartNumber.length() ? car.ecuPartNumber + " / SW " + car.ecuSoftware : String(engineProfileLabel(activeEngineProfile));
  if (ecu.length() > 38) ecu = ecu.substring(0, 36) + "..";
  tft.drawString(ecu, 22, 209);
  tft.setTextColor(COL_CYAN, COL_PANEL2);
  tft.drawString(developerWeb.length() ? developerWeb : "Kontakt lze upravit pres WEB", 22, 222);
}

// ============================ ALERTY ==========================

void redrawCurrentScreen();

void showAlert(const String &title, const String &line1, const String &line2,
               uint16_t color, unsigned long durationMs = ALERT_DEFAULT_MS) {
  alert.active = true;
  alert.title = title;
  alert.line1 = line1;
  alert.line2 = line2;
  alert.color = color;
  alert.untilMs = millis() + durationMs;

  tft.fillRoundRect(18, 55, 284, 130, 14, COL_PANEL);
  tft.drawRoundRect(18, 55, 284, 130, 14, color);

  tft.fillCircle(160, 83, 16, color);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_BLACK, color);
  tft.setTextSize(2);
  tft.drawString("!", 160, 83);

  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(color, COL_PANEL);
  tft.drawString(title, 160, 114);

  tft.setTextColor(COL_TEXT, COL_PANEL);
  tft.drawString(line1, 160, 142);
  tft.setTextColor(COL_MUTED, COL_PANEL);
  tft.drawString(line2, 160, 162);
  tft.setTextFont(1);
}

static bool alarmConditionHeld(bool condition, unsigned long &sinceMs, unsigned long holdMs) {
  if (!condition) {
    sinceMs = 0;
    return false;
  }
  if (!sinceMs) sinceMs = millis();
  return millis() - sinceMs >= holdMs;
}

void updateAlerts() {
  if (alert.active && (long)(millis() - alert.untilMs) >= 0) {
    alert.active = false;
    redrawCurrentScreen();
  }

  // V16.25: hystereze zustava, navic musi nebezpecna hodnota chvili vydrzet.
  // Jediny chybny OBD vzorek tak uz nevyvola popup.
  bool coolantHigh = alarmCoolantEnabled && !isnan(car.coolantC) && car.coolantC >= ALARM_COOLANT_C;
  if (alarmConditionHeld(coolantHigh, coolantAlarmSinceMs, ALARM_CONFIRM_COOLANT_MS) && !warnedCoolant) {
    warnedCoolant = true;
    showAlert("TEPLOTA MOTORU", String(car.coolantC, 0) + " C",
              "Zkontroluj chlazeni motoru", COL_RED, 5000);
  }
  if (!isnan(car.coolantC) && car.coolantC < ALARM_COOLANT_C - 5.0f) {
    warnedCoolant = false;
    coolantAlarmSinceMs = 0;
  }

  float ev = electricalBusVoltage();
  bool lowVoltage = alarmLowVoltageEnabled && car.elmConnected && !isnan(ev) &&
                    car.rpm > 500 && ev < 12.2f;
  if (alarmConditionHeld(lowVoltage, lowVoltageAlarmSinceMs, ALARM_CONFIRM_VOLTAGE_MS) && !warnedVoltage) {
    warnedVoltage = true;
    showAlert("NIZKE NAPETI", String(ev, 1) + " V", "Zkontroluj dobijeni", COL_YELLOW, 5000);
  }
  if (!isnan(ev) && ev > 12.8f) {
    warnedVoltage = false;
    lowVoltageAlarmSinceMs = 0;
  }

  bool highVoltage = alarmOverVoltageEnabled && car.elmConnected && !isnan(ev) &&
                     car.rpm > 500 && ev > 15.2f;
  if (alarmConditionHeld(highVoltage, overVoltageAlarmSinceMs, ALARM_CONFIRM_VOLTAGE_MS) && !warnedOverVoltage) {
    warnedOverVoltage = true;
    showAlert("PREPETI", String(ev, 1) + " V", "Zkontroluj regulator alternatoru", COL_RED, 5000);
  }
  if (!isnan(ev) && ev < 14.9f) {
    warnedOverVoltage = false;
    overVoltageAlarmSinceMs = 0;
  }

  float kmToRegen = dpfEstimatedKmToRegen();
  bool regenSoon = alarmRegenSoonEnabled && dpf.available && !dpf.regenActive &&
                   !isnan(kmToRegen) && kmToRegen <= alarmRegenSoonKm;
  if (alarmConditionHeld(regenSoon, regenSoonAlarmSinceMs, ALARM_CONFIRM_DPF_SOON_MS) && !warnedRegenSoon) {
    warnedRegenSoon = true;
    showAlert("DPF BRZY", "Predikce do regenerace", String(kmToRegen, 0) + " km", COL_ORANGE, 5000);
  }
  if (!regenSoon && (isnan(kmToRegen) || kmToRegen > alarmRegenSoonKm + 5.0f)) {
    warnedRegenSoon = false;
    regenSoonAlarmSinceMs = 0;
  }
}

// ========================== ULOZISTE ==========================

void initStorage() {
  // Bezpecny mount: nahodna chyba LittleFS nesmi automaticky smazat historii.
  storageReady = LittleFS.begin(false);

#if ALLOW_FS_FORMAT_IF_MOUNT_FAIL
  if (!storageReady) {
    Serial.println("LittleFS mount selhal - POVOLEN jednorazovy format.");
    storageReady = LittleFS.begin(true);
  }
#endif

  if (storageReady) {
    Serial.println("LittleFS OK.");
  } else {
    Serial.println("LittleFS init selhal - data NEBYLA smazana. Logovani/PDF bude docasne nedostupne.");
  }
}

void saveTrip() {
  if (!storageReady || trip.distanceKm < 0.05f) return;

  // Konec jizdy uklada okamzik prvniho potvrzeneho zastaveni motoru, ne konec
  // 60s potvrzovaciho okna. Funguje i po obnoveni rozjete jizdy po restartu ESP32.
  unsigned long durationSec = trip.stopElapsedSec ? trip.stopElapsedSec : currentTripElapsedSec();
  float avgSpeed = durationSec > 0 ? trip.distanceKm / (durationSec / 3600.0f) : 0;
  float avgCons = (trip.fuelAvailable && trip.distanceKm > 0.05f)
                  ? trip.fuelLiters * 100.0f / trip.distanceKm : NAN;
  time_t endEpoch = 0;
  if (trip.startEpoch > 0) {
    endEpoch = trip.startEpoch + (time_t)durationSec;
  } else if (clockIsValid()) {
    unsigned long elapsedNow = currentTripElapsedSec();
    unsigned long graceSec = elapsedNow > durationSec ? elapsedNow - durationSec : 0;
    endEpoch = time(nullptr) - (time_t)graceSec;
  }

  // V15.1 strukturovany log pro Web UI a PDF:
  // startEpoch;endEpoch;km;fuelL;avgL100;maxKmh;avgKmh;durationSec
  fs::File fv2 = LittleFS.open("/trips_v2.csv", FILE_APPEND);
  if (fv2) {
    String row2 = String((uint32_t)trip.startEpoch) + ";" +
                  String((uint32_t)endEpoch) + ";" +
                  String(trip.distanceKm, 3) + ";" +
                  (trip.fuelAvailable ? String(trip.fuelLiters, 3) : String("")) + ";" +
                  (!isnan(avgCons) ? String(avgCons, 2) : String("")) + ";" +
                  String(trip.maxSpeedKmh, 1) + ";" +
                  String(avgSpeed, 1) + ";" + String(durationSec);
    fv2.println(row2);
    fv2.close();
  }

  // Puvodni citelny CSV ponechavame kvuli zpetne kompatibilite.
  fs::File f = LittleFS.open("/trips.csv", FILE_APPEND);
  if (f) {
    String fuelField = trip.fuelAvailable ? (String(trip.fuelLiters, 2) + " L") : String("--");
    String row = formatDateTime(trip.startEpoch) + "," +
                 formatDateTime(endEpoch) + "," +
                 String(trip.distanceKm, 2) + " km," + fuelField + "," +
                 (!isnan(avgCons) ? String(avgCons, 2) + " L/100" : String("--")) + "," +
                 formatDuration(durationSec) + "," + String(trip.maxSpeedKmh, 0) + " max," +
                 String(avgSpeed, 0) + " avg";
    f.println(row);
    f.close();
    Serial.println("TRIP ulozen: " + row);
  }
}

void persistTrackedOdometer(bool force) {
  if (!trackedOdometerConfigured || isnan(trackedOdometerKm)) return;
  if (!force && !isnan(lastOdometerPersistKm) &&
      trackedOdometerKm - lastOdometerPersistKm < ODOMETER_PERSIST_STEP_KM) return;
  prefs.putBool("odoSet", true);
  prefs.putFloat("odoKm", trackedOdometerKm);
  lastOdometerPersistKm = trackedOdometerKm;
}

void persistFuelBaseline(bool force) {
  if (!fuelBaselineValid || isnan(fuelBaselinePct)) return;
  if (!force && millis() - lastFuelBaselinePersistMs < FUEL_BASELINE_PERSIST_MS) return;
  prefs.putBool("fuelBaseOk", true);
  prefs.putFloat("fuelBasePct", fuelBaselinePct);
  lastFuelBaselinePersistMs = millis();
}

void saveFuelingRecord(float beforePct, float afterPct) {
  if (!storageReady || tankCapacityL <= 0.1f) return;
  beforePct = clampFloat(beforePct, 0.0f, 100.0f);
  afterPct = clampFloat(afterPct, 0.0f, 100.0f);
  float beforeL = tankCapacityL * beforePct / 100.0f;
  float afterL = tankCapacityL * afterPct / 100.0f;
  float addedL = max(0.0f, afterL - beforeL);
  time_t epoch = clockIsValid() ? time(nullptr) : 0;

  fs::File f = LittleFS.open("/fueling.csv", FILE_APPEND);
  if (f) {
    String row = String((uint32_t)epoch) + ";" +
                 (trackedOdometerConfigured && !isnan(trackedOdometerKm) ? String(trackedOdometerKm, 1) : String("")) + ";" +
                 String(beforeL, 2) + ";" + String(afterL, 2) + ";" + String(addedL, 2);
    f.println(row);
    f.close();
    Serial.println("TANKOVANI ulozeno: " + row);
  }
}

void updateFuelingDetection() {
  if (!car.fuelLevelAvailable || isnan(car.fuelLevelPct) || tankCapacityL <= 0.1f) return;
  if (!car.fuelLevelUpdateMs || car.fuelLevelUpdateMs == lastFuelLevelHandledMs) return;
  lastFuelLevelHandledMs = car.fuelLevelUpdateMs;

  float pct = clampFloat(car.fuelLevelPct, 0.0f, 100.0f);
  float minRisePct = fuelingMinRiseL * 100.0f / tankCapacityL;

  if (!fuelBaselineValid || isnan(fuelBaselinePct)) {
    fuelBaselinePct = pct;
    fuelBaselineValid = true;
    persistFuelBaseline(true);
    return;
  }

  if (fuelCandidateActive) {
    float risePct = pct - fuelCandidateBeforePct;
    if (car.speedKmh < 5.0f) fuelCandidateSawStopped = true;

    if (risePct >= minRisePct * 0.75f) {
      if (pct > fuelCandidateAfterPct) fuelCandidateAfterPct = pct;
      if (fuelCandidateSamples < 250) fuelCandidateSamples++;
      if (fuelCandidateSamples >= FUELING_CONFIRM_SAMPLES &&
          millis() - fuelCandidateSinceMs >= FUELING_CONFIRM_MS && fuelCandidateSawStopped) {
        float beforePct = fuelCandidateBeforePct;
        float afterPct = fuelCandidateAfterPct;
        saveFuelingRecord(beforePct, afterPct);
        fuelBaselinePct = afterPct;
        fuelBaselineValid = true;
        persistFuelBaseline(true);
        fuelCandidateActive = false;
        float addedL = tankCapacityL * (afterPct - beforePct) / 100.0f;
        showAlert("TANKOVANI", "Automaticky zaznamenano", "+" + String(addedL, 1) + " L", COL_GREEN, 4500);
        return;
      }
    } else if (risePct < minRisePct * 0.40f) {
      // Kratky vykyv plovaku / naklon auta.
      fuelCandidateActive = false;
      fuelCandidateSamples = 0;
    }
    return;
  }

  float deltaPct = pct - fuelBaselinePct;
  if (deltaPct >= minRisePct) {
    fuelCandidateActive = true;
    fuelCandidateBeforePct = fuelBaselinePct;
    fuelCandidateAfterPct = pct;
    fuelCandidateSinceMs = millis();
    fuelCandidateSamples = 1;
    fuelCandidateSawStopped = car.speedKmh < 5.0f;
    return;
  }

  // Spotreba posouva baseline pouze dolu. Male pohyby smerem nahoru ignorujeme,
  // aby se baseline po zatackach a svazich postupne "nevysplhala".
  if (pct < fuelBaselinePct - 0.4f) {
    fuelBaselinePct = pct;
    persistFuelBaseline(false);
  }
}

void logDpfRegeneration(float sootBefore, float sootAfter,
                        unsigned long durationSec, float distanceKm) {
  if (!storageReady) return;
  fs::File f = LittleFS.open("/dpf.csv", FILE_APPEND);
  if (!f) return;

  String row =
    formatDateTime(clockIsValid() ? time(nullptr) : 0) + "," +
    String(sootBefore, 1) + "g->" + String(sootAfter, 1) + "g," +
    formatDuration(durationSec) + "," + String(distanceKm, 1) + "km";

  f.println(row);
  f.close();
  dpfHistoryCacheMs = 0;
}

void persistServiceCounters(bool force = false) {
  // V15.4: setrime NVS. Drive se zapisovalo kazdou minutu i pri stani,
  // protoze po uplynuti 60 s podminka vzdy propadla.
  if (!force) {
    if (millis() - lastServicePersistMs < 300000UL) return;  // max 1x za 5 min
    if (fabs(service.oilKm - lastServicePersistKm) < 1.0f &&
        fabs(service.inspectionKm - lastServicePersistInspectionKm) < 1.0f) return;
  }

  prefs.putFloat("svcOilKm", service.oilKm);
  prefs.putFloat("svcInspKm", service.inspectionKm);
  prefs.putULong64("svcOilEpoch", (uint64_t)service.oilEpoch);
  prefs.putULong64("svcInspEpoch", (uint64_t)service.inspectionEpoch);
  lastServicePersistKm = service.oilKm;
  lastServicePersistInspectionKm = service.inspectionKm;
  lastServicePersistMs = millis();
}

void resetServiceCounter(uint8_t target) {
  time_t now = clockIsValid() ? time(nullptr) : 0;
  if (target == 1) {
    service.oilKm = 0;
    service.oilEpoch = now;
  } else if (target == 2) {
    service.inspectionKm = 0;
    service.inspectionEpoch = now;
  }
  persistServiceCounters(true);
  serviceResetArmed = false;
  serviceResetTarget = 0;
}

void updateRegenSession() {
  static bool initialized = false;
  if (!vehicleCaps.advancedDpf) {
    initialized = false;
    regenSessionRunning = false;
    previousRegenActive = false;
    return;
  }
  static unsigned long invalidSince = 0;

  // Nevyhodnocujeme prechod REGEN->OFF pri vypadku ELM/ECU. Jinak by pouhe
  // odpojeni adapteru uprostred regenerace vytvorilo falesny zaznam "hotovo".
  bool regenStatusFresh = car.elmConnected && car.ecuResponding && car.udsResponding &&
                          dpf.regenStatusAvailable && dpf.regenStatusUpdateMs &&
                          millis() - dpf.regenStatusUpdateMs < 12000UL;
  if (!regenStatusFresh) {
    if (!invalidSince) invalidSince = millis();
    // Po delsim vypadku rozbehnutou session zahodime bez logovani; konec jsme
    // fyzicky nepozorovali a nechceme falshovat historii regeneraci.
    if (regenSessionRunning && millis() - invalidSince > 15000UL) {
      regenSessionRunning = false;
      initialized = false;
    }
    return;
  }
  invalidSince = 0;

  if (!initialized) {
    previousRegenActive = dpf.regenActive;
    initialized = true;
  }

  unsigned long now = millis();
  if (dpf.regenActive && !previousRegenActive) {
    regenSessionRunning = true;
    regenStartMs = now;
    regenLastIntegrateMs = now;
    regenDistanceKm = 0;
    regenSootBefore = dpf.sootCalculatedG;
    regenIntervalKm = dpf.kmSinceRegen;
    if (alarmRegenEnabled) {
      showAlert("DPF REGENERACE!", "AKTIVNI REGENERACE ZACALA",
                isnan(regenSootBefore) ? "Otevri DPF PRO pro detail" : String(regenSootBefore, 1) + " g sazi / sleduj DPF PRO",
                COL_ORANGE, 8000);
    }
  }

  if (dpf.regenActive && regenSessionRunning) {
    float dtHours = (now - regenLastIntegrateMs) / 3600000.0f;
    regenLastIntegrateMs = now;
    if (dtHours > 0 && dtHours < 0.01f) regenDistanceKm += car.speedKmh * dtHours;
  }

  if (!dpf.regenActive && previousRegenActive && regenSessionRunning) {
    unsigned long durationSec = (now - regenStartMs) / 1000UL;
    float after = dpf.sootCalculatedG;
    logDpfRegeneration(regenSootBefore, after, durationSec,
                       isnan(regenIntervalKm) ? regenDistanceKm : regenIntervalKm);
    if (alarmRegenEnabled) {
      showAlert("DPF HOTOVO", "Regenerace byla dokoncena",
                formatDuration(durationSec) + " / " + String(regenDistanceKm, 1) + " km",
                COL_GREEN, 5000);
    }
    regenSessionRunning = false;
  }

  previousRegenActive = dpf.regenActive;
}

// =========================== KNIHA JIZD LOGIKA ================

void startTrip() {
  trip.active = true;
  trip.recovered = false;
  trip.startMs = millis();
  trip.lastUpdateMs = millis();
  trip.engineStoppedSince = 0;
  trip.elapsedBeforeBootSec = 0;
  trip.stopElapsedSec = 0;
  trip.lastCheckpointMs = 0;
  trip.startEpoch = clockIsValid() ? time(nullptr) : 0;
  trip.distanceKm = 0;
  trip.fuelLiters = 0;
  trip.fuelAvailable = false;
  trip.maxSpeedKmh = 0;
  if (dpf.available && !isnan(dpf.fuelSinceRegenL)) {
    trip.lastFuelCounterL = dpf.fuelSinceRegenL;
    trip.fuelCounterValid = true;
    trip.fuelAvailable = true;
  } else {
    trip.lastFuelCounterL = NAN;
    trip.fuelCounterValid = false;
  }
  clearTripCheckpoint();
  persistTripCheckpoint(true);
}

void finishTrip() {
  if (!trip.active) return;
  persistTripCheckpoint(true);
  saveTrip();
  persistServiceCounters(true);
  persistTrackedOdometer(true);
  persistFuelBaseline(true);
  clearTripCheckpoint();
  trip.active = false;
  trip.recovered = false;
  trip.engineStoppedSince = 0;
  trip.stopElapsedSec = 0;
  trip.elapsedBeforeBootSec = 0;
  trip.startEpoch = 0;
  trip.distanceKm = 0;
  trip.fuelLiters = 0;
  trip.fuelAvailable = false;
  trip.maxSpeedKmh = 0;
  trip.fuelCounterValid = false;
  trip.lastFuelCounterL = NAN;
}

void updateTrip() {
  unsigned long now = millis();
  bool telemetryFresh = car.elmConnected && car.lastGoodObdMs && (now - car.lastGoodObdMs < 5000UL);
  bool engineRunning = telemetryFresh && car.rpm > 400.0f;
  bool engineStoppedFresh = telemetryFresh && car.rpm < 200.0f;
  bool ecuShutdownEvidence = car.elmConnected && !car.ecuResponding;

  if (engineRunning && !trip.active) startTrip();
  if (!trip.active) return;

  float dtHours = (now - trip.lastUpdateMs) / 3600000.0f;
  trip.lastUpdateMs = now;

  // Pri ztrate BT/ELM neintegrujeme posledni starou rychlost. Rozjeta jizda ale
  // zustane aktivni a checkpoint ji ochrani pred rozdelenim na dva zaznamy.
  if (telemetryFresh && dtHours > 0 && dtHours < 0.01f) {
    float deltaKm = max(0.0f, car.speedKmh) * dtHours;
    trip.distanceKm += deltaKm;
    service.oilKm += deltaKm;
    service.inspectionKm += deltaKm;
    if (trackedOdometerConfigured && !isnan(trackedOdometerKm)) {
      trackedOdometerKm += deltaKm;
      persistTrackedOdometer(false);
    }

    if (dpf.available && !isnan(dpf.fuelSinceRegenL)) {
      if (!trip.fuelCounterValid) {
        trip.lastFuelCounterL = dpf.fuelSinceRegenL;
        trip.fuelCounterValid = true;
        trip.fuelAvailable = true;
      } else {
        float deltaFuel = dpf.fuelSinceRegenL - trip.lastFuelCounterL;
        if (deltaFuel >= 0.0f && deltaFuel < 1.0f) {
          trip.fuelLiters += deltaFuel;
          trip.fuelAvailable = true;
        }
        trip.lastFuelCounterL = dpf.fuelSinceRegenL;
      }
    } else if (car.fuelRateAvailable && !isnan(car.fuelRateLph) && car.fuelRateLph >= 0) {
      trip.fuelLiters += car.fuelRateLph * dtHours;
      trip.fuelAvailable = true;
    }
    persistServiceCounters(false);
  }

  if (clockIsValid()) {
    if (trip.startEpoch <= 0) trip.startEpoch = time(nullptr) - (time_t)currentTripElapsedSec();
    if (service.oilEpoch <= 0) service.oilEpoch = time(nullptr);
    if (service.inspectionEpoch <= 0) service.inspectionEpoch = time(nullptr);
  }

  if (telemetryFresh && car.speedKmh > trip.maxSpeedKmh) trip.maxSpeedKmh = car.speedKmh;

  // Skutecny konec jizdy: musi existovat dukaz z auta (cerstve RPM=0 nebo ELM
  // zustal pripojeny, ale motorova ECU uz neodpovida). Pouhy vypadek Bluetooth
  // jizdu nikdy neukonci.
  bool stopEvidence = engineStoppedFresh || ecuShutdownEvidence;
  if (engineRunning) {
    trip.engineStoppedSince = 0;
    trip.stopElapsedSec = 0;
  } else if (stopEvidence) {
    if (!trip.engineStoppedSince) {
      trip.engineStoppedSince = now;
      trip.stopElapsedSec = currentTripElapsedSec();
      persistTripCheckpoint(true);
    }
    if (now - trip.engineStoppedSince >= TRIP_END_DELAY_MS) finishTrip();
  } else {
    // Neznamy stav (typicky ztraceny ELM): cekame na navrat dat a checkpoint
    // ponechavame aktivni. Stop timer se nema pocitat bez dukazu o vypnuti motoru.
    trip.engineStoppedSince = 0;
    trip.stopElapsedSec = 0;
  }

  if (trip.active) persistTripCheckpoint(false);
}

// ========================= BLUETOOTH SCANNER ==================

void drawSignal(int x, int y, int rssi) {
  int bars = 1;
  if (rssi > -85) bars = 2;
  if (rssi > -70) bars = 3;
  if (rssi > -55) bars = 4;

  for (int i = 0; i < 4; i++) {
    int h = 4 + i * 3;
    tft.fillRect(x + i * 5, y + 16 - h, 3, h, i < bars ? COL_CYAN : COL_BORDER);
  }
}

void drawDeviceCard(int idx, int y) {
  DeviceInfo &d = devices[idx];
  bool selected = idx == selectedDevice;
  uint16_t bg = selected ? COL_SELECT : COL_PANEL;
  uint16_t border = selected ? COL_CYAN : COL_BORDER;

  tft.fillRoundRect(10, y, 300, 38, 8, bg);
  tft.drawRoundRect(10, y, 300, 38, 8, border);

  tft.fillRoundRect(18, y + 7, 25, 25, 6, COL_PANEL2);
  drawBluetoothIcon(30, y + 19, COL_CYAN, 1, COL_PANEL2);

  String name = d.name.length() ? d.name : "Nezname zarizeni";
  if (name.length() > 22) name = name.substring(0, 19) + "...";

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT, bg);
  tft.drawString(name, 53, y + 6);
  tft.setTextColor(COL_MUTED, bg);
  tft.drawString(d.mac, 53, y + 21);

  drawSignal(282, y + 10, d.rssi);

  if (selected) {
    tft.fillCircle(260, y + 19, 8, COL_CYAN);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_BLACK, COL_CYAN);
    tft.drawString("V", 260, y + 19);
  }
}

void drawBtResults() {
  currentScreen = SCREEN_BT_RESULTS;
  tft.fillScreen(COL_BG);
  drawHeader("BLUETOOTH", true);

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("NALEZENA ZARIZENI", 12, 46);
  tft.setTextColor(COL_CYAN, COL_BG);
  tft.drawString(String(deviceCount), 132, 46);

  int first = btPage * DEVICES_PER_PAGE;
  int y = 65;

  for (int i = 0; i < DEVICES_PER_PAGE; i++) {
    int idx = first + i;
    if (idx >= deviceCount) break;
    drawDeviceCard(idx, y);
    y += 44;
  }

  int pageCount = max(1, (deviceCount + DEVICES_PER_PAGE - 1) / DEVICES_PER_PAGE);
  if (pageCount > 1) {
    drawButton(10, 196, 28, 20, "<", btPage > 0);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_MUTED, COL_BG);
    tft.drawString(String(btPage + 1) + "/" + String(pageCount), 55, 206);
    drawButton(72, 196, 28, 20, ">", btPage < pageCount - 1);
  }

  // Zadani uzivatele:
  drawButton(100, 210, 98, 25, "OPAKOVAT");
  drawButton(205, 210, 105, 25, "POKRACOVAT", selectedDevice >= 0, true);
}

void drawBtScanScreen() {
  currentScreen = SCREEN_BT_SCAN;
  tft.fillScreen(COL_BG);
  drawHeader("BLUETOOTH", true);
  tft.setTextDatum(MC_DATUM); tft.setTextFont(2); tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString("Hledam zarizeni", 160, 58);
  tft.setTextFont(1); tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Bluetooth Classic", 160, 78);
  drawButton(220, 218, 90, 20, "OFFLINE");
}

void drawRadar() {
  const int cx = 160, cy = 132, r = 45;

  tft.fillCircle(cx, cy, r + 2, COL_BG);
  tft.drawCircle(cx, cy, 15, COL_BORDER);
  tft.drawCircle(cx, cy, 30, COL_BORDER);
  tft.drawCircle(cx, cy, 44, COL_BORDER);
  tft.drawLine(cx - r, cy, cx + r, cy, COL_PANEL2);
  tft.drawLine(cx, cy - r, cx, cy + r, COL_PANEL2);

  float a = radarAngle * DEG_TO_RAD;
  tft.drawLine(cx, cy, cx + cos(a) * r, cy + sin(a) * r, COL_CYAN);
  tft.fillCircle(cx, cy, 4, COL_CYAN);

  radarAngle = (radarAngle + 8) % 360;

  tft.fillRect(50, 180, 220, 18, COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Nalezeno " + String(foundDuringScan) + " zarizeni", 160, 188);

  float p = min(1.0f, (float)(millis() - btScanStart) / BT_SCAN_TIME_MS);
  tft.fillRoundRect(35, 208, 250, 7, 4, COL_PANEL2);
  int width = (int)(246 * p);
  if (width > 0) tft.fillRoundRect(37, 210, width, 3, 2, COL_CYAN);
}

void btFound(BTAdvertisedDevice *device) {
  foundDuringScan++;
  Serial.println("BT FOUND: " + String(device->toString().c_str()));
}

void loadScanResults() {
  BTScanResults *results = SerialBT.getScanResults();

  deviceCount = 0;
  if (!results) return;

  int count = min(results->getCount(), MAX_DEVICES);

  for (int i = 0; i < count; i++) {
    BTAdvertisedDevice *d = results->getDevice(i);
    devices[deviceCount].name = d->getName().c_str();
    devices[deviceCount].mac = d->getAddress().toString().c_str();
    devices[deviceCount].rssi = d->getRSSI();
    deviceCount++;
  }

  for (int i = 0; i < deviceCount - 1; i++) {
    for (int j = i + 1; j < deviceCount; j++) {
      if (devices[j].rssi > devices[i].rssi) {
        DeviceInfo tmp = devices[i];
        devices[i] = devices[j];
        devices[j] = tmp;
      }
    }
  }
}

void startBtScan() {
  // Uzivatel vedome zahajil hledani ELM -> opoustime offline rezim a rusime
  // pripadny naplanovany automaticky reconnect.
  offlineMode = false;
  reconnectRequested = false;
  diagnosticExclusive = true;

  bool locked = !btMutex || xSemaphoreTake(btMutex, pdMS_TO_TICKS(500)) == pdTRUE;
  if (!locked) {
    diagnosticExclusive = false;
    showAlert("BLUETOOTH", "Prave probiha obnova spojeni", "Zkus HLEDAT znovu za chvili", COL_YELLOW, 2500);
    return;
  }

  SerialBT.disconnect();
  SerialBT.discoverAsyncStop();
  SerialBT.discoverClear();
  if (btMutex) xSemaphoreGive(btMutex);

  car.elmConnected = false;
  invalidateLiveTelemetry(true);
  diagnosticExclusive = false;

  selectedDevice = -1;
  btPage = 0;
  foundDuringScan = 0;

  drawBtScanScreen();

  btScanStart = millis();
  btScanning = true;

  if (!SerialBT.discoverAsync(btFound)) {
    btScanning = false;
    showAlert("BLUETOOTH", "Scan se nepodarilo spustit", "", COL_RED, 4000);
  }
}

void enterOfflineMode() {
  offlineMode = true;
  connectUserChoseOffline = true;
  reconnectAbortRequested = true;
  reconnectRequested = false;
  reconnectFinished = false;
  car.elmConnected = false;
  invalidateLiveTelemetry(true);
  diagnosticExclusive = false;
  warnedDisconnect = false;
  lastReconnectAttempt = millis();
  if (!btMutex || xSemaphoreTake(btMutex, pdMS_TO_TICKS(80)) == pdTRUE) {
    SerialBT.disconnect();
    if (btMutex) xSemaphoreGive(btMutex);
  }
}

int offerOtherDeviceBeforeAutoConnect(const String &name, const String &mac, unsigned long waitMs = 2600UL) {
  tft.fillScreen(COL_BG);
  drawHeader("ELM327 START");
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2); tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString("Automaticke pripojeni", 160, 67);
  tft.setTextFont(1); tft.setTextColor(COL_CYAN, COL_BG);
  tft.drawString(name, 160, 92);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString(mac, 160, 108);
  drawButton(20, 151, 180, 42, "VYBRAT JINE", true, true);
  drawButton(208,151, 92, 42, "OFFLINE");
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("Pokracuji automaticky za 3 s", 160, 218);

  unsigned long start = millis();
  while (millis() - start < waitMs) {
    int x, y;
    if (getTap(x, y)) {
      if (inside(x, y, 12, 142, 195, 62)) return 1;
      if (inside(x, y, 202, 142, 108, 62)) { enterOfflineMode(); return 2; }
    }
    delay(15);
  }
  return 0;
}

// ========================= BLUETOOTH CONNECT ==================

bool macToBytes(const String &mac, uint8_t out[6]) {
  if (mac.length() != 17) return false;
  for (int i = 0; i < 6; i++)
    out[i] = strtoul(mac.substring(i * 3, i * 3 + 2).c_str(), nullptr, 16);
  return true;
}

void saveDefaultDevice(const String &name, const String &mac) {
  hasDefaultDevice = true;
  defaultName = name;
  defaultMac = mac;
  prefs.putBool("hasDefault", true);
  prefs.putString("defName", defaultName);
  prefs.putString("defMac", defaultMac);
}

void clearDefaultDevice() {
  hasDefaultDevice = false;
  defaultName = "";
  defaultMac = "";
  prefs.putBool("hasDefault", false);
  prefs.remove("defName");
  prefs.remove("defMac");
}

String elmCommand(const String &cmd, uint32_t timeoutMs, bool debug) {
  if (btMutex) {
    TickType_t waitTicks = pdMS_TO_TICKS(timeoutMs + 500);
    if (xSemaphoreTake(btMutex, waitTicks) != pdTRUE) {
      if (debug) Serial.println("ELM mutex timeout: " + cmd);
      return "";
    }
  }

  while (SerialBT.available()) SerialBT.read();

  if (debug) Serial.println("ELM >> " + cmd);
  SerialBT.print(cmd);
  SerialBT.print("\r");

  String response;
  response.reserve(256);
  unsigned long start = millis();
  bool done = false;

  while (millis() - start < timeoutMs) {
    while (SerialBT.available()) {
      char c = SerialBT.read();
      // Ochrana heapu proti vadnemu/ukecanemu klonu ELM. Bezne odpovedi maji
      // desitky az stovky znaku; 2048 je dost i pro delsi ISO-TP odpovedi.
      if (response.length() < 2048) response += c;
      if (c == '>') {
        done = true;
        break;
      }
    }
    if (done) break;
    vTaskDelay(1);
  }

  if (btMutex) xSemaphoreGive(btMutex);

  if (debug) {
    if (done) Serial.println("ELM << " + response);
    else      Serial.println("ELM TIMEOUT << " + response);
  }

  return response;
}

bool obdReplyContains(const String &response, const String &needle) {
  String s = response;
  s.toUpperCase();
  s.replace(" ", "");
  s.replace("\r", "");
  s.replace("\n", "");
  return s.indexOf(needle) >= 0;
}

bool elmResponseIsOk(const String &r) {
  String s = r;
  s.toUpperCase();
  return s.indexOf("OK") >= 0 && s.indexOf("?") < 0;
}

// Prevede ATDPN (napr. "A6" nebo "6") na cislo ELM protokolu.
static uint8_t parseElmProtocolNumber(String dpn) {
  dpn.toUpperCase(); dpn.trim();
  dpn.replace(" ", ""); dpn.replace("\r", ""); dpn.replace("\n", ""); dpn.replace(">", "");
  if (dpn.startsWith("A")) dpn.remove(0, 1);
  if (!dpn.length()) return 0;
  char c = dpn[0];
  if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
  if (c >= 'A' && c <= 'F') return (uint8_t)(10 + c - 'A');
  return 0;
}

// Minimalni ISO-TP parser pro identifikacni UDS odpovedi pri startu.
// Potrebujeme jen payload 62xxxx a ASCII cast; zadne zapisove sluzby.
static String extractUdsAscii(const String &response, uint16_t did) {
  String src = response;
  src.replace("\r", "\n");
  String payload;
  int start = 0;
  while (start < src.length()) {
    int nl = src.indexOf('\n', start);
    if (nl < 0) nl = src.length();
    String line = src.substring(start, nl);
    start = nl + 1;
    line.toUpperCase(); line.replace(" ", ""); line.replace(">", ""); line.trim();
    if (!line.length()) continue;
    if (line.startsWith("7E8") && line.length() > 3) line = line.substring(3);
    if (line.length() < 2) continue;
    uint8_t pci = (uint8_t)strtoul(line.substring(0, 2).c_str(), nullptr, 16);
    uint8_t type = pci >> 4;
    if (type == 0x0) {
      int bytes = pci & 0x0F;
      String part = line.substring(2);
      if (part.length() > bytes * 2) part = part.substring(0, bytes * 2);
      payload += part;
    } else if (type == 0x1) {
      if (line.length() > 4) payload += line.substring(4);
    } else if (type == 0x2) {
      if (line.length() > 2) payload += line.substring(2);
    } else {
      payload += line;
    }
  }

  char needle[8];
  snprintf(needle, sizeof(needle), "62%04X", did);
  int p = payload.indexOf(needle);
  if (p < 0) return "";
  p += 6;

  String out;
  for (int i = p; i + 1 < payload.length(); i += 2) {
    uint8_t b = (uint8_t)strtoul(payload.substring(i, i + 2).c_str(), nullptr, 16);
    if (b == 0x00 || b == 0xFF) continue;
    if (b >= 32 && b <= 126) out += (char)b;
  }
  out.trim();
  return out;
}

static bool verifiedCaycDpfMapping() {
  // Rozsirene DPF scalingy v tomto firmware byly fyzicky overeny na teto ECU.
  // U jineho PCR2.1 profilu ponechame bezpecne jen standardni OBD, dokud nebude
  // konkretni mapa zmerena a potvrzena. Tím se vyhneme "spravnym" cislam ze
  // spatneho DIDu.
  String pn = car.ecuPartNumber;
  pn.toUpperCase();
  pn.replace(" ", "");
  return pn.indexOf("03L906023LP") >= 0;
}

void applyEngineProfile(EngineProfile p, const String &reason) {
  activeEngineProfile = p;
  vehicleCaps = VehicleCapabilities();

  switch (p) {
    case ENGINE_PROFILE_CAYC_PCR21: {
      vehicleCaps.vagUds = car.udsResponding && enginePhysicalCanAddressing;
      vehicleCaps.vagModuleScan = enginePhysicalCanAddressing;
      bool verifiedDpf = verifiedCaycDpfMapping();
      // Pri rucni volbe dovolime fallback pouze kdyz ECU identifikaci F187
      // neposkytla vubec; pokud ECU vrati jine cislo dilu, raw DPF se nezapne.
      bool manualWithoutPartNo = configuredEngineProfile == ENGINE_PROFILE_CAYC_PCR21 &&
                                 car.udsResponding && !car.ecuPartNumber.length();
      vehicleCaps.advancedDpf = verifiedDpf || manualWithoutPartNo;
      vehicleCaps.standardDpfPressure = true;
      // SPECIALS jsou zamerne pouze pro puvodne overeny CAYC profil na CANu.
      vehicleCaps.j519Specials = enginePhysicalCanAddressing && (verifiedDpf || manualWithoutPartNo);
      break;
    }
    case ENGINE_PROFILE_20TDI_CR_EDC17:
      vehicleCaps.vagUds = car.udsResponding && enginePhysicalCanAddressing;
      vehicleCaps.vagModuleScan = enginePhysicalCanAddressing;
      vehicleCaps.advancedDpf = false; // bez overenych raw DIDu nic nehadej
      vehicleCaps.standardDpfPressure = true;
      vehicleCaps.j519Specials = false;
      break;
    case ENGINE_PROFILE_TDI_PD_EDC16:
      vehicleCaps.vagUds = false;
      vehicleCaps.vagModuleScan = false;
      vehicleCaps.advancedDpf = false;
      vehicleCaps.standardDpfPressure = true;
      vehicleCaps.j519Specials = false;
      break;
    case ENGINE_PROFILE_GENERIC_OBD:
    default:
      vehicleCaps.vagUds = false;
      vehicleCaps.vagModuleScan = false;
      vehicleCaps.advancedDpf = false;
      vehicleCaps.standardDpfPressure = true;
      vehicleCaps.j519Specials = false;
      break;
  }

  engineProfileDetected = configuredEngineProfile != ENGINE_PROFILE_AUTO || p != ENGINE_PROFILE_GENERIC_OBD;
  engineDetectionNote = reason.length() ? reason : String(engineProfileLabel(p));
  if (p == ENGINE_PROFILE_CAYC_PCR21 && !vehicleCaps.advancedDpf)
    engineDetectionNote += " / RAW DPF NEOVERENO";
  if (!vehicleCaps.advancedDpf) {
    dpf.available = false;
    dpf.regenActive = false;
    dpf.sootCalculatedG = NAN;
    dpf.sootMeasuredG = NAN;
    dpf.kmSinceRegen = NAN;
    dpf.fuelSinceRegenL = NAN;
    dpf.regenStatusAvailable = false;
  }
  Serial.println(String("ENGINE PROFILE: ") + engineProfileLabel(p) + " | " + engineDetectionNote);
}

bool vehicleHasAdvancedDpf() {
  return vehicleCaps.advancedDpf && car.elmConnected && car.ecuResponding && car.udsResponding;
}

static uint8_t readObdFuelTypeCode() {
  String r = elmCommand("0151", 900);
  r.toUpperCase(); r.replace(" ", ""); r.replace("\r", ""); r.replace("\n", ""); r.replace(">", "");
  int p = r.indexOf("4151");
  if (p < 0 || r.length() < p + 6) return 0;
  return (uint8_t)strtoul(r.substring(p + 4, p + 6).c_str(), nullptr, 16);
}

static EngineProfile detectEngineProfileFromIdentification() {
  String id = car.ecuPartNumber + " " + car.ecuSoftware + " " + car.odxId;
  id.toUpperCase();

  // CAYC/PCR2.1: testovany ECU 03L906023LP a bezne ODX oznaceni ECM16TDI/PCR2.
  if (id.indexOf("03L906023LP") >= 0 || id.indexOf("03L 906 023 LP") >= 0 ||
      id.indexOf("ECM16TDI") >= 0 || id.indexOf("PCR2") >= 0)
    return ENGINE_PROFILE_CAYC_PCR21;

  // U 2.0 TDI CR se v ODX/ASAM identifikaci bezne objevuje ECM20TDI.
  // Samotny retezec EDC17 bereme jen jako rodinu ECU; proprietarni DPF zustava vypnuty.
  if (id.indexOf("ECM20TDI") >= 0 || id.indexOf("EDC17") >= 0)
    return ENGINE_PROFILE_20TDI_CR_EDC17;

  // Starsi TDI-PD/EDC16 casto neodpovida na novou UDS identifikaci. Pokud ELM
  // zvolil ne-CAN EOBD protokol a SAE PID 51 potvrdi diesel, je to pro Octavia II
  // rozumny a bezpecny AUTO profil. Zadna proprietarni DPF data se tim nepovoli.
  if (!car.udsResponding && detectedElmProtocol != VAG_CAN_11_500_PROTOCOL) {
    uint8_t fuelType = readObdFuelTypeCode();
    if (fuelType == 4) return ENGINE_PROFILE_TDI_PD_EDC16; // SAE: Diesel
  }

  return ENGINE_PROFILE_GENERIC_OBD;
}

void resolveEngineProfileAfterIdentification() {
  if (configuredEngineProfile != ENGINE_PROFILE_AUTO) {
    applyEngineProfile(configuredEngineProfile, "RUCNI VOLBA UZIVATELE");
    return;
  }
  EngineProfile detected = detectEngineProfileFromIdentification();
  if (detected == ENGINE_PROFILE_GENERIC_OBD) {
    applyEngineProfile(detected, car.udsResponding ? "UDS ECU NEZNAMEHO PROFILU" : "GENERIC OBD FALLBACK");
  } else {
    applyEngineProfile(detected, "AUTO DETEKCE ECU/ODX");
  }
}

// Nastavi fyzickou adresu motorove ECU jen pokud jsme na VAG CAN 11/500.
// U starsich ISO/KWP protokolu nechavame hlavicku plne na ELM327.
bool selectEngineEcu() {
  if (!enginePhysicalCanAddressing) return true;
  String r = elmCommand(String("ATSH") + ENGINE_TX_HEADER, 1000);
  return elmResponseIsOk(r);
}

// Read-only UDS ReadDataByIdentifier. Neprovadi zadne kodovani, mazani ani zapis.
String udsReadDid(uint16_t did, uint32_t timeoutMs = 1800) {
  char cmd[8];
  snprintf(cmd, sizeof(cmd), "22%04X", did);
  return elmCommand(String(cmd), timeoutMs);
}

bool udsPositive(const String &r, uint16_t did) {
  char needle[8];
  snprintf(needle, sizeof(needle), "62%04X", did);
  return obdReplyContains(r, String(needle));
}

void identifyEcuUds() {
  car.udsResponding = false;
  car.vin = "";
  car.ecuPartNumber = "";
  car.ecuSoftware = "";
  car.odxId = "";

  if (!enginePhysicalCanAddressing) {
    Serial.println("UDS identifikace preskocena - aktivni OBD protokol neni CAN 11/500.");
    return;
  }

  Serial.println("===== VAG UDS IDENTIFIKACE (READ-ONLY) =====");
  selectEngineEcu();

  String f190 = udsReadDid(0xF190, 2500);
  if (udsPositive(f190, 0xF190)) { car.udsResponding = true; car.vin = extractUdsAscii(f190, 0xF190); }
  Serial.println("UDS F190 / VIN RAW: " + f190);

  String f187 = udsReadDid(0xF187, 1800);
  if (udsPositive(f187, 0xF187)) { car.udsResponding = true; car.ecuPartNumber = extractUdsAscii(f187, 0xF187); }
  Serial.println("UDS F187 / ECU PART RAW: " + f187);

  String f189 = udsReadDid(0xF189, 1800);
  if (udsPositive(f189, 0xF189)) { car.udsResponding = true; car.ecuSoftware = extractUdsAscii(f189, 0xF189); }
  Serial.println("UDS F189 / ECU SW RAW: " + f189);

  String f19e = udsReadDid(0xF19E, 1800);
  if (udsPositive(f19e, 0xF19E)) { car.udsResponding = true; car.odxId = extractUdsAscii(f19e, 0xF19E); }
  Serial.println("UDS F19E / ODX-ASAM RAW: " + f19e);

  Serial.println("ECU PART: " + (car.ecuPartNumber.length() ? car.ecuPartNumber : String("--")));
  Serial.println("ECU SW:   " + (car.ecuSoftware.length() ? car.ecuSoftware : String("--")));
  Serial.println("ODX/ASAM: " + (car.odxId.length() ? car.odxId : String("--")));
  Serial.println(String("UDS: ") + (car.udsResponding ? "ANO" : "NE"));
  Serial.println("============================================");
}

bool probeEcu(uint32_t timeoutMs) {
  String r;
  bool ok = false;

  if (enginePhysicalCanAddressing) {
    selectEngineEcu();
    r = elmCommand("0100", timeoutMs);
    ok = obdReplyContains(r, "4100");
    // Funkcni adresa je bezpecny fallback pro legislativni OBD.
    if (!ok) {
      elmCommand("ATSH7DF", 800);
      r = elmCommand("0100", timeoutMs);
      ok = obdReplyContains(r, "4100");
      selectEngineEcu();
    }
  } else {
    // U ISO9141/KWP/auto-detekovaneho protokolu ELM sam spravuje hlavicky.
    r = elmCommand("0100", timeoutMs);
    ok = obdReplyContains(r, "4100");
  }

  car.ecuResponding = ok;
  if (ok) car.lastGoodObdMs = millis();
  return ok;
}

static void configureElmCommon() {
  elmCommand("ATE0", 1200);
  elmCommand("ATL0", 1200);
  elmCommand("ATS0", 1200);
  elmCommand("ATH1", 1200);
  elmCommand("ATAT1", 1200);
  elmCommand("ATAL", 1200);
  // Na ISO-TP jsou tyto prikazy dulezite, na jinych protokolech je lze ignorovat.
  elmCommand("ATCAF1", 1200);
  elmCommand("ATCFC1", 1200);
}

bool initElm(bool showUi = true) {
  if (showUi) {
    tft.fillScreen(COL_BG);
    drawHeader("ELM327 INIT");
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
  }
  int y = 50;

  auto statusLine = [&](const String &text, uint16_t color) {
    if (!showUi || y > 220) return;
    tft.fillCircle(18, y + 5, 4, color);
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.drawString(text, 30, y);
    y += 20;
  };

  statusLine("Bluetooth spojeni", COL_GREEN);

  String r = elmCommand("ATZ", 4000);
  if (!r.length()) return false;
  statusLine("Reset ELM327", COL_GREEN);

  elmCommand("ATD", 1200);
  elmCommand("ATPC", 1200);
  configureElmCommon();

  bool ecuOk = false;
  detectedElmProtocol = 0;
  enginePhysicalCanAddressing = false;

  // Rychly prvni pokus: vetsina Octavia II CR pouziva ISO15765-4 CAN 11/500.
  r = elmCommand("ATSP6", 1800);
  if (elmResponseIsOk(r)) {
    detectedElmProtocol = 6;
    enginePhysicalCanAddressing = true;
    selectEngineEcu();
    ecuOk = probeEcu(2800);
  }

  // Starsi Octavia II muze pouzivat jiny EOBD protokol. Pokud SP6 nefunguje,
  // vratime ELM do defaultu a nechame jej protokol jednou automaticky najit.
  if (!ecuOk) {
    statusLine("SP6 bez ECU - zkousim AUTO", COL_YELLOW);
    elmCommand("ATD", 1200);
    configureElmCommon();
    elmCommand("ATSP0", 1500);
    enginePhysicalCanAddressing = false;
    String autoReply = elmCommand("0100", 8500);
    ecuOk = obdReplyContains(autoReply, "4100");
    if (ecuOk) car.lastGoodObdMs = millis();

    String dpnAuto = elmCommand("ATDPN", 1200);
    detectedElmProtocol = parseElmProtocolNumber(dpnAuto);
    enginePhysicalCanAddressing = detectedElmProtocol == VAG_CAN_11_500_PROTOCOL;
    if (enginePhysicalCanAddressing) {
      // Po auto-detekci CANu muzeme znovu bezpecne pouzit fyzickou adresu motoru.
      selectEngineEcu();
      probeEcu(2200);
    }
  }

  String dp = elmCommand("ATDP", 1200);
  String dpn = elmCommand("ATDPN", 1200);
  if (!detectedElmProtocol) detectedElmProtocol = parseElmProtocolNumber(dpn);
  car.protocol = dp;
  Serial.println("ELM protocol: " + dp + " / " + dpn);
  statusLine(ecuOk ? (String("OBD OK / protokol ") + String(detectedElmProtocol)) : "ECU bez odpovedi na 0100",
             ecuOk ? COL_GREEN : COL_RED);

  if (ecuOk) {
    car.ecuResponding = true;
    if (enginePhysicalCanAddressing) {
      identifyEcuUds();
      statusLine(car.udsResponding ? "VAG UDS identifikace OK" : "OBD OK, UDS bez odpovedi",
                 car.udsResponding ? COL_GREEN : COL_YELLOW);
    } else {
      car.udsResponding = false;
      statusLine("Starsí/generic OBD profil", COL_YELLOW);
    }
    resolveEngineProfileAfterIdentification();
    statusLine(String("Profil: ") + engineProfileShortLabel(activeEngineProfile), COL_CYAN);
  } else {
    car.udsResponding = false;
    resolveEngineProfileAfterIdentification();
    statusLine("BT je OK; kontroluj ELM/OBD", COL_YELLOW);
  }

  delay(250);
  return true;
}

bool connectToDevice(const String &name, const String &mac, bool askDefaultAfterSuccess) {
  // Explicitni pokus o spojeni znamena, ze uzivatel nechce zustat offline.
  connectUserChoseOther = false;
  connectUserChoseOffline = false;
  offlineMode = false;
  pendingDeviceName = name;
  pendingDeviceMac = mac;

  tft.fillScreen(COL_BG);
  drawHeader("PRIPOJOVANI");

  tft.fillCircle(160, 105, 40, COL_PANEL);
  drawBluetoothIcon(160, 105, COL_CYAN, 2, COL_PANEL);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(2);
  tft.drawString("Pripojuji...", 160, 165);

  tft.setTextSize(1);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString(name + " / " + mac, 160, 186);
  drawButton(24, 202, 174, 31, "JINE ZARIZENI");
  drawButton(206, 202, 90, 31, "OFFLINE", true, true);

  // Pred blokujicim SPP connectem jsou vzdy dostupne obe volby.
  unsigned long chooseStart = millis();
  while (millis() - chooseStart < 1300UL) {
    int tx, ty;
    if (getTap(tx, ty)) {
      if (inside(tx, ty, 18, 195, 184, 44)) {
        connectUserChoseOther = true;
        startBtScan();
        return false;
      }
      if (inside(tx, ty, 202, 195, 106, 44)) {
        enterOfflineMode();
        drawMainMenu();
        return false;
      }
    }
    delay(12);
  }

  uint8_t addr[6];
  if (!macToBytes(mac, addr)) return false;

  car.elmConnected = false;
  car.ecuResponding = false;
  engineProfileDetected = false;
  engineDetectionNote = "DETEKCE ECU...";

  bool ok = false;
  if (!btMutex || xSemaphoreTake(btMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    SerialBT.disconnect();

    SerialBT.setPin(BT_PIN_1, 4);
    ok = SerialBT.connect(addr);

    if (!ok) {
      SerialBT.setPin(BT_PIN_2, 4);
      ok = SerialBT.connect(addr);
    }

    if (btMutex) xSemaphoreGive(btMutex);
  }

  if (!ok && !askDefaultAfterSuccess && WiFi.softAPgetStationNum() == 0) {
    // Boot recovery pouze pokud na AP neni telefon. Teardown/restart Bluedroid
    // soucasne s aktivnim SoftAP klientem je presne scenario, kteremu se ve
    // V15.7 vyhybame.
    restartBluetoothMaster();
    delay(1200);
    if (!btMutex || xSemaphoreTake(btMutex, pdMS_TO_TICKS(1200)) == pdTRUE) {
      SerialBT.setPin(BT_PIN_1, 4);
      ok = SerialBT.connect(addr);
      if (!ok) {
        SerialBT.setPin(BT_PIN_2, 4);
        ok = SerialBT.connect(addr);
      }
      if (btMutex) xSemaphoreGive(btMutex);
    }

    // Posledni kratky retry po uvolneni stare SPP session na strane donglu.
    if (!ok) {
      delay(2500);
      if (!btMutex || xSemaphoreTake(btMutex, pdMS_TO_TICKS(1200)) == pdTRUE) {
        SerialBT.setPin(BT_PIN_1, 4);
        ok = SerialBT.connect(addr);
        if (btMutex) xSemaphoreGive(btMutex);
      }
    }
  }

  if (!ok) {
    car.elmConnected = false;
    return false;
  }

  offlineMode = false;
  diagnosticExclusive = true;
  car.elmConnected = true;
  car.ecuResponding = false;
  warnedDisconnect = false;

  bool elmInitOk = initElm(true);
  if (!elmInitOk) {
    // diagnosticExclusive nechame aktivni az do fyzickeho odpojeni. Jinak by
    // OBD task mohl mezi initElm(false) a disconnectem stihnout spustit novy dotaz.
    car.elmConnected = false;
    invalidateLiveTelemetry(true);
    if (!btMutex || xSemaphoreTake(btMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      SerialBT.disconnect();
      if (btMutex) xSemaphoreGive(btMutex);
    }
    diagnosticExclusive = false;
    return false;
  }
  diagnosticExclusive = false;

  if (askDefaultAfterSuccess && (!hasDefaultDevice || mac != defaultMac)) {
    currentScreen = SCREEN_ASK_DEFAULT;

    tft.fillScreen(COL_BG);
    drawHeader("PRIPOJENO");

    tft.fillRoundRect(25, 65, 270, 115, 12, COL_PANEL);
    tft.drawRoundRect(25, 65, 270, 115, 12, COL_GREEN);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_TEXT, COL_PANEL);
    tft.setTextSize(2);
    tft.drawString("Zvolit jako", 160, 95);
    tft.drawString("defaultni zarizeni?", 160, 120);

    tft.setTextSize(1);
    tft.setTextColor(COL_MUTED, COL_PANEL);
    tft.drawString(name, 160, 150);

    drawButton(62, 198, 90, 30, "NE");
    drawButton(168, 198, 90, 30, "ANO", true, true);
    return true;
  }

  drawLiveScreen();
  return true;
}

void drawConnectingScreen() {
  currentScreen = SCREEN_CONNECTING;
  tft.fillScreen(COL_BG);
  drawHeader("ELM327");
  tft.fillCircle(160, 98, 40, COL_PANEL2);
  drawBluetoothIcon(160, 98, COL_CYAN, 2, COL_PANEL2);
  tft.setTextDatum(MC_DATUM); tft.setTextFont(2); tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString("Pripojuji ELM...", 160, 151);
  tft.setTextFont(1); tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString(defaultName + " / " + defaultMac, 160, 174);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Pokus probiha na pozadi - displej zustava ovladatelny", 160, 191);
  drawButton(82, 205, 156, 30, "OFFLINE", true, true);
}

void drawConnectFail() {
  currentScreen = SCREEN_CONNECT_FAIL;
  tft.fillScreen(COL_BG);
  drawHeader("ELM327");

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_RED, COL_BG);
  tft.setTextSize(2);
  tft.drawString("Zarizeni nedostupne", 160, 88);

  tft.setTextColor(COL_MUTED, COL_BG);
  tft.setTextSize(1);
  tft.drawString(pendingDeviceName + " / " + pendingDeviceMac, 160, 116);

  drawButton(30, 165, 82, 34, "HLEDAT");
  drawButton(120, 165, 82, 34, "ZNOVU", true, true);
  drawButton(210, 165, 82, 34, "OFFLINE");
}

// ============================ OBD =============================

bool extractBytesAfter(const String &response, const String &prefix, uint8_t *out, int count) {
  String s = response;
  s.toUpperCase();
  s.replace(" ", "");
  s.replace("\r", "");
  s.replace("\n", "");
  s.replace(">", "");

  int pos = s.indexOf(prefix);
  if (pos < 0) return false;

  pos += prefix.length();
  if (s.length() < pos + count * 2) return false;

  for (int i = 0; i < count; i++)
    out[i] = (uint8_t)strtoul(s.substring(pos + i * 2, pos + i * 2 + 2).c_str(), nullptr, 16);

  return true;
}

bool queryPid2(const String &cmd, const String &prefix, uint8_t &a, uint8_t &b) {
  String r = elmCommand(cmd, 650);
  uint8_t d[2];
  if (!extractBytesAfter(r, prefix, d, 2)) return false;
  a = d[0];
  b = d[1];
  car.lastGoodObdMs = millis();
  car.ecuResponding = true;
  return true;
}

bool queryPid4(const String &cmd, const String &prefix, uint8_t &a, uint8_t &b, uint8_t &c, uint8_t &d) {
  String r = elmCommand(cmd, 700);
  uint8_t v[4];
  if (!extractBytesAfter(r, prefix, v, 4)) return false;
  a = v[0]; b = v[1]; c = v[2]; d = v[3];
  car.lastGoodObdMs = millis();
  car.ecuResponding = true;
  return true;
}

bool queryPid1(const String &cmd, const String &prefix, uint8_t &a) {
  String r = elmCommand(cmd, 650);
  uint8_t d[1];
  if (!extractBytesAfter(r, prefix, d, 1)) return false;
  a = d[0];
  car.lastGoodObdMs = millis();
  car.ecuResponding = true;
  return true;
}


bool pollStandardDpfPressure() {
  if (!vehicleCaps.standardDpfPressure) {
    dpf.diffPressureAvailable = false;
    return false;
  }
  // SAE J1979 Mode 01 PID 7A: A=support bitmap, B/C=DPF Bank1 delta pressure.
  // kPa = signed16(B,C)/100, tedy hPa = signed16(B,C)/10.
  String r = elmCommand("017A", 750);
  uint8_t d[7];
  if (!extractBytesAfter(r, "417A", d, 7)) {
    dpf.diffPressureAvailable = false;
    return false;
  }
  if ((d[0] & 0x01) == 0) {
    dpf.diffPressureAvailable = false;
    return false;
  }
  int16_t raw = (int16_t)(((uint16_t)d[1] << 8) | d[2]);
  float v = raw / 10.0f;
  if (!telemetryValueValid(v, -100.0f, 1000.0f)) {
    dpf.diffPressureAvailable = false;
    return false;
  }
  dpf.diffPressureHpa = v;
  dpf.diffPressureAvailable = true;
  car.lastGoodObdMs = millis();
  return true;
}

bool pollFastCorePid(bool rpmPid) {
  uint8_t a = 0, b = 0;
  if (rpmPid) {
    bool ok = queryPid2("010C", "410C", a, b);
    if (ok) { float v = ((a * 256.0f) + b) / 4.0f; if (telemetryValueValid(v, 0.0f, 7000.0f)) car.rpm = v; }
    return ok;
  }
  bool ok = queryPid1("010B", "410B", a);
  if (ok) { float v = a; if (telemetryValueValid(v, 20.0f, 300.0f)) car.mapKpa = v; }
  return ok;
}

void pollObdOneItem();

bool pollDrivingPriorityPid(uint8_t slot) {
  uint8_t a = 0, b = 0;
  bool ok = false;
  switch (slot % 3) {
    case 0:
      ok = queryPid1("010D", "410D", a);
      if (ok) { float v = a; if (telemetryValueValid(v, 0.0f, 300.0f)) car.speedKmh = v; }
      break;
    case 1:
      ok = queryPid1("0105", "4105", a);
      if (ok) { float v = (float)a - 40.0f; if (telemetryValueValid(v, -40.0f, 150.0f)) car.coolantC = v; }
      break;
    default: {
      static unsigned long lastAmbient = 0;
      if (!lastAmbient || millis() - lastAmbient >= 8000UL) {
        lastAmbient = millis();
        ok = queryPid1("0146", "4146", a);
        if (ok) { float v = (float)a - 40.0f; if (telemetryValueValid(v, -50.0f, 80.0f)) { car.ambientTempC = v; car.ambientAvailable = true; } }
      } else {
        pollObdOneItem();
        ok = true;
      }
      break;
    }
  }
  return ok;
}

static uint32_t smartObdLoopDelayMs(bool fastScreen) {
  if (powerLowMode) return 500UL;
  if (dpf.regenActive) return 65UL;
  if (obdDataMode == 0) return 220UL;
  if (fastScreen && obdDataMode == 2) return 55UL;
  if (currentScreen == SCREEN_SENSORS || currentScreen == SCREEN_DPF) return obdDataMode == 2 ? 85UL : 120UL;
  return obdDataMode == 2 ? 100UL : OBD_POLL_INTERVAL_MS;
}

float parseElmVoltage(const String &response) {
  String s = response;
  s.toUpperCase();

  int vpos = s.indexOf('V');
  if (vpos < 0) return NAN;

  int start = vpos - 1;
  while (start >= 0) {
    char c = s[start];
    if (!((c >= '0' && c <= '9') || c == '.')) break;
    start--;
  }
  start++;

  float v = s.substring(start, vpos).toFloat();
  return v > 0 ? v : NAN;
}

String decodeDtc(uint8_t a, uint8_t b) {
  const char systems[] = {'P','C','B','U'};
  char buf[6];
  snprintf(buf, sizeof(buf), "%c%01X%01X%01X%01X",
           systems[(a >> 6) & 0x03], (a >> 4) & 0x03,
           a & 0x0F, (b >> 4) & 0x0F, b & 0x0F);
  return String(buf);
}

String extractIsoTpPayload(const String &response, const String &rxHeader) {
  String payload;
  String src = response;
  src.replace("\r", "\n");
  int start = 0;
  String rx = rxHeader; rx.toUpperCase();
  while (start < src.length()) {
    int nl = src.indexOf('\n', start);
    if (nl < 0) nl = src.length();
    String line = src.substring(start, nl);
    start = nl + 1;
    line.trim(); line.toUpperCase(); line.replace(" ", ""); line.replace("\r", ""); line.replace(">", "");
    if (!line.length()) continue;
    if (rx.length() && line.startsWith(rx)) line = line.substring(rx.length());
    if (line.length() < 2) continue;
    uint8_t pci = (uint8_t)strtoul(line.substring(0, 2).c_str(), nullptr, 16);
    uint8_t type = pci >> 4;
    if (type == 0x0) {
      int bytes = pci & 0x0F;
      String part = line.substring(2);
      if (part.length() > bytes * 2) part = part.substring(0, bytes * 2);
      payload += part;
    } else if (type == 0x1) {
      if (line.length() > 4) payload += line.substring(4);
    } else if (type == 0x2) {
      if (line.length() > 2) payload += line.substring(2);
    } else if (line.startsWith("43") || line.startsWith("59") || line.startsWith("7F")) {
      payload += line;
    }
  }
  return payload;
}

String extractIsoTpPayload7E8(const String &response) {
  return extractIsoTpPayload(response, "7E8");
}

void restoreEngineDiagnosticAddress() {
  elmCommand("ATCRA", 600);
  if (enginePhysicalCanAddressing) selectEngineEcu();
}

bool setDiagnosticAddress(const String &tx, const String &rx) {
  String a = elmCommand("ATSH" + tx, 700);
  String b = elmCommand("ATCRA" + rx, 700);
  return elmResponseIsOk(a) && elmResponseIsOk(b);
}

int parseUdsDtcPayload(const String &payload, String *out, int maxCodes) {
  int pos = payload.indexOf("5902");
  if (pos < 0) return 0;
  pos += 4;
  // UDS 0x19/0x02: prvni byte po 5902 je availability mask, pak 3-byte DTC + status.
  if (pos + 2 <= payload.length()) pos += 2;
  int count = 0;
  while (pos + 7 < payload.length() && count < maxCodes) {
    String dtcHex = payload.substring(pos, pos + 6);
    String status = payload.substring(pos + 6, pos + 8);
    pos += 8;
    if (dtcHex == "000000") continue;
    out[count++] = dtcHex + " [" + status + "]";
  }
  return count;
}

bool scanOneDiagModuleInternal(int idx) {
  if (idx < 0 || idx >= DIAG_MODULE_COUNT || !car.elmConnected) return false;

  DiagModuleState next;
  next.scanned = true;
  next.online = false;
  next.count = 0;
  next.note = "";

  const DiagModuleSpec &sp = DIAG_MODULES[idx];

  if (idx == 0) {
    // Motor: standardni OBD Mode 03 funguje i v Generic profilu.
    // Na CANu pouzijeme fyzickou adresu 7E0; u starsich protokolu nechame
    // hlavicku na ELM327 a pouze zrusime pripadny CAN receive filter.
    if (enginePhysicalCanAddressing) selectEngineEcu();
    else elmCommand("ATCRA", 600);

    String r = elmCommand("03", 2500);
    String payload = extractIsoTpPayload(r, enginePhysicalCanAddressing ? String(sp.rxId) : String(""));
    int pos = payload.indexOf("43");
    if (pos >= 0) {
      next.online = true; pos += 2;
      while (pos + 3 < payload.length() && next.count < 16) {
        uint8_t a = (uint8_t)strtoul(payload.substring(pos, pos + 2).c_str(), nullptr, 16);
        uint8_t b = (uint8_t)strtoul(payload.substring(pos + 2, pos + 4).c_str(), nullptr, 16);
        pos += 4;
        if (!a && !b) continue;
        next.codes[next.count++] = decodeDtc(a, b);
      }
    } else {
      next.note = "bez Mode 03 odpovedi";
    }
  } else if (!vehicleCaps.vagModuleScan || !enginePhysicalCanAddressing) {
    // Pevne VAG CAN adresy ostatnich jednotek nejsou univerzalni pro starsi
    // ISO/KWP profily. Radsi je neoslovujeme, nez abychom predstirali podporu.
    next.note = "profil nepodporuje VAG scan";
  } else {
    if (!setDiagnosticAddress(sp.txId, sp.rxId)) {
      next.note = "header error";
    } else {
      // Ostatni UDS jednotky: pouze ReadDTCInformation. Zadne mazani ani zapis.
      String r = elmCommand("1902FF", 1200);
      String payload = extractIsoTpPayload(r, sp.rxId);
      if (payload.indexOf("5902") >= 0 || payload.indexOf("7F19") >= 0) {
        next.online = true;
        next.count = parseUdsDtcPayload(payload, next.codes, 16);
        if (payload.indexOf("7F19") >= 0) next.note = "UDS odpoved, DTC cteni odmitnuto";
      } else {
        next.note = "bez UDS odpovedi";
      }
    }
  }

  restoreEngineDiagnosticAddress();
  if (diagMutex && xSemaphoreTake(diagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    diagModules[idx] = next;
    xSemaphoreGive(diagMutex);
  } else if (!diagMutex) {
    diagModules[idx] = next;
  }
  return next.online;
}

void scanAllDiagModulesInternal() {
  for (int i = 0; i < DIAG_MODULE_COUNT; i++) {
    scanOneDiagModuleInternal(i);
    // UI i Web tak vidi postup scanu, ne az konec dlouheho autoscan cyklu.
    diagNeedsRedraw = true;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  // Zrcadli motor do puvodniho DTC stavu pro API zpetnou kompatibilitu.
  DiagModuleState motor = copyDiagState(0);
  if (diagMutex && xSemaphoreTake(diagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    dtc.count = min(motor.count, 8);
    for (int i = 0; i < dtc.count; i++) dtc.codes[i] = motor.codes[i];
    dtc.scanned = motor.scanned;
    dtc.lastScanMs = millis();
    dtc.busy = false;
    xSemaphoreGive(diagMutex);
  } else {
    dtc.busy = false;
  }
}


bool pollFuelLevelPid(float &pctOut) {
  uint8_t a = 0;
  // Nejdrive fyzicka adresa motoru, stejne jako ostatni legislativni PIDy.
  if (queryPid1("012F", "412F", a)) {
    pctOut = a * 100.0f / 255.0f;
    return true;
  }

  // Nektere VAG/ELM kombinace vrati volitelny PID 2F jen na funkcni OBD adrese 7DF.
  // Tento fallback pouzijeme jen na CAN 11/500; u starsich protokolu hlavicky
  // spravuje ELM327 a dalsi ATSH by mohl komunikaci rozhodit.
  if (!enginePhysicalCanAddressing) return false;
  elmCommand("ATSH7DF", 900);
  String r = elmCommand("012F", 1100);
  bool ok = extractBytesAfter(r, "412F", &a, 1);
  selectEngineEcu();
  if (!ok) return false;
  car.lastGoodObdMs = millis();
  car.ecuResponding = true;
  pctOut = a * 100.0f / 255.0f;
  return true;
}

void persistFuelLevelIfNeeded(float pct) {
  if (isnan(pct)) return;
  unsigned long now = millis();
  bool step = isnan(lastStoredFuelPersistPct) || fabsf(pct - lastStoredFuelPersistPct) >= 1.0f;
  bool age = lastStoredFuelPersistMs == 0 || now - lastStoredFuelPersistMs >= 300000UL;
  if (!step && !age) return;
  storedFuelPct = clampFloat(pct, 0.0f, 100.0f);
  storedFuelValid = true;
  prefs.putBool("fuelLastOk", true);
  prefs.putFloat("fuelLastPct", storedFuelPct);
  lastStoredFuelPersistPct = storedFuelPct;
  lastStoredFuelPersistMs = now;
}

void pollObdOneItem() {
  if (!car.elmConnected) return;

  uint8_t a = 0, b = 0, c = 0, d = 0;
  bool ok = false;

  // Fast data (RPM/MAP) se v cyklu opakuji casteji. Pomale senzory jsou
  // rozlozene, aby Cheap ELM327 nebyl zahlcen a UI zustalo plynule.
  switch (obdPollIndex) {
    case 0:
    case 3:
      ok = queryPid2("010C", "410C", a, b);
      if (ok) { float v = ((a * 256.0f) + b) / 4.0f; if (telemetryValueValid(v, 0.0f, 7000.0f)) car.rpm = v; }
      break;

    case 1:
    case 6:
      ok = queryPid1("010B", "410B", a);
      if (ok) { float v = a; if (telemetryValueValid(v, 20.0f, 300.0f)) car.mapKpa = v; }
      break;

    case 2:
      ok = queryPid1("010D", "410D", a);
      if (ok) { float v = a; if (telemetryValueValid(v, 0.0f, 300.0f)) car.speedKmh = v; }
      break;

    case 4:
      ok = queryPid1("0105", "4105", a);
      if (ok) { float v = (float)a - 40.0f; if (telemetryValueValid(v, -40.0f, 150.0f)) car.coolantC = v; }
      break;

    case 5: {
      float v = parseElmVoltage(elmCommand("ATRV", 700));
      if (telemetryValueValid(v, 6.0f, 18.0f)) {
        car.voltageV = v;
        // DULEZITE: ATRV meri napajeni na ELM327. Neni to odpoved ECU,
        // proto nesmi prodluzovat lastGoodObdMs ani nastavovat ECU ONLINE.
        ok = true;
      }
      break;
    }

    case 7:
      ok = queryPid1("0104", "4104", a);
      if (ok) { float v = a * 100.0f / 255.0f; if (telemetryValueValid(v, 0.0f, 100.0f)) car.engineLoadPct = v; }
      break;

    case 8:
      ok = queryPid2("0110", "4110", a, b);
      if (ok) { float v = ((a * 256.0f) + b) / 100.0f; if (telemetryValueValid(v, 0.0f, 500.0f)) car.mafGps = v; }
      break;

    case 9: {
      static unsigned long lastFuelLevelAttemptMs = 0;
      const unsigned long retryMs = car.fuelLevelAvailable ? 12000UL : 5000UL;
      if (lastFuelLevelAttemptMs == 0UL || millis() - lastFuelLevelAttemptMs >= retryMs) {
        lastFuelLevelAttemptMs = millis();
        float fuelPct = NAN;
        ok = pollFuelLevelPid(fuelPct);
        if (ok && telemetryValueValid(fuelPct, 0.0f, 100.0f)) {
          car.fuelLevelPct = fuelPct;
          car.fuelLevelAvailable = true;
          car.fuelLevelAutomatic = true;
          car.fuelLevelUpdateMs = millis();
          persistFuelLevelIfNeeded(car.fuelLevelPct);
        }
      }
      break;
    }

    case 10: {
      static unsigned long lastAttempt = 0;
      const unsigned long period = car.intakeAvailable ? 5000UL : 30000UL;
      if (lastAttempt == 0 || millis() - lastAttempt >= period) {
        lastAttempt = millis();
        ok = queryPid1("010F", "410F", a);
        if (ok) { float v = (float)a - 40.0f; if (telemetryValueValid(v, -40.0f, 120.0f)) { car.intakeTempC = v; car.intakeAvailable = true; } }
      }
      break;
    }

    case 11: {
      static unsigned long lastAttempt = 0;
      // BARO se meni pomalu; po uspesnem nacteni neni duvod ho dotazovat v kazdem cyklu.
      const unsigned long period = car.baroAvailable ? 15000UL : 30000UL;
      if (lastAttempt == 0 || millis() - lastAttempt >= period) {
        lastAttempt = millis();
        ok = queryPid1("0133", "4133", a);
        if (ok) { float v = a; if (telemetryValueValid(v, 50.0f, 120.0f)) { car.baroKpa = v; car.baroAvailable = true; } }
      }
      break;
    }

    case 12: {
      static unsigned long lastAttempt = 0;
      const unsigned long period = car.railAvailable ? 2500UL : 30000UL;
      if (lastAttempt == 0 || millis() - lastAttempt >= period) {
        lastAttempt = millis();
        ok = queryPid2("0123", "4123", a, b);
        if (ok) { float v = ((a * 256.0f) + b) * 10.0f; if (telemetryValueValid(v, 0.0f, 200000.0f)) { car.railPressureKpa = v; car.railAvailable = true; } }
      }
      break;
    }

    case 13: {
      // SAE PID 42 = Control module voltage. Na ELEKTRICE pouzivame toto,
      // nikoli ATRV z ELM adapteru.
      static unsigned long lastAttempt = 0;
      if (lastAttempt == 0 || millis() - lastAttempt >= 2500UL) {
        lastAttempt = millis();
        ok = queryPid2("0142", "4142", a, b);
        if (ok) {
          float v = (((uint16_t)a << 8) | b) / 1000.0f;
          if (telemetryValueValid(v, 6.0f, 18.0f)) { car.moduleVoltageV = v; car.moduleVoltageAvailable = true; }
        }
      }
      break;
    }

    case 14: {
      // SAE PID 46 = Ambient air temperature. Nektere ECU ho neposkytuji;
      // v takovem pripade PREHLED korektne ukaze --.
      static unsigned long lastAttempt = 0;
      const unsigned long period = car.ambientAvailable ? 10000UL : 30000UL;
      if (lastAttempt == 0 || millis() - lastAttempt >= period) {
        lastAttempt = millis();
        ok = queryPid1("0146", "4146", a);
        if (ok) { float v = (float)a - 40.0f; if (telemetryValueValid(v, -50.0f, 80.0f)) { car.ambientTempC = v; car.ambientAvailable = true; } }
      }
      break;
    }


    case 15: {
      // SAE PID 11 - absolutni poloha skrtici klapky.
      static unsigned long lastAttempt = 0;
      const unsigned long period = car.throttleAvailable ? (currentScreen == SCREEN_SENSORS ? 2500UL : 10000UL) : 30000UL;
      if (lastAttempt == 0 || millis() - lastAttempt >= period) {
        lastAttempt = millis();
        ok = queryPid1("0111", "4111", a);
        if (ok) { float v = a * 100.0f / 255.0f; if (telemetryValueValid(v, 0.0f, 100.0f)) { car.throttlePct = v; car.throttleAvailable = true; } }
      }
      break;
    }

    case 16: {
      // SAE PID 2C - pozadovana hodnota EGR.
      static unsigned long lastAttempt = 0;
      const unsigned long period = car.egrCommandAvailable ? (currentScreen == SCREEN_SENSORS ? 2500UL : 10000UL) : 30000UL;
      if (lastAttempt == 0 || millis() - lastAttempt >= period) {
        lastAttempt = millis();
        ok = queryPid1("012C", "412C", a);
        if (ok) { float v = a * 100.0f / 255.0f; if (telemetryValueValid(v, 0.0f, 100.0f)) { car.egrCommandPct = v; car.egrCommandAvailable = true; } }
      }
      break;
    }

    case 17: {
      // SAE PID 2D - EGR error, nikoliv vymyslena "skutecna" poloha.
      static unsigned long lastAttempt = 0;
      const unsigned long period = car.egrErrorAvailable ? (currentScreen == SCREEN_SENSORS ? 2500UL : 10000UL) : 30000UL;
      if (lastAttempt == 0 || millis() - lastAttempt >= period) {
        lastAttempt = millis();
        ok = queryPid1("012D", "412D", a);
        if (ok) { float v = ((int)a - 128) * 100.0f / 128.0f; if (telemetryValueValid(v, -100.0f, 100.0f)) { car.egrErrorPct = v; car.egrErrorAvailable = true; } }
      }
      break;
    }

    case 18: {
      // SAE PID 44 - commanded equivalence ratio (lambda request).
      static unsigned long lastAttempt = 0;
      const unsigned long period = car.lambdaCommandedAvailable ? (currentScreen == SCREEN_SENSORS ? 3000UL : 12000UL) : 30000UL;
      if (lastAttempt == 0 || millis() - lastAttempt >= period) {
        lastAttempt = millis();
        ok = queryPid2("0144", "4144", a, b);
        if (ok) {
          float v = (((uint16_t)a << 8) | b) / 32768.0f;
          if (telemetryValueValid(v, 0.3f, 2.0f)) { car.lambdaCommanded = v; car.lambdaCommandedAvailable = true; }
        }
      }
      break;
    }

    case 19: {
      // SAE PID 24 - wideband O2 B1S1: equivalence ratio + sensor voltage.
      static unsigned long lastAttempt = 0;
      const unsigned long period = car.lambdaB1S1Available ? (currentScreen == SCREEN_SENSORS ? 3000UL : 12000UL) : 30000UL;
      if (lastAttempt == 0 || millis() - lastAttempt >= period) {
        lastAttempt = millis();
        ok = queryPid4("0124", "4124", a, b, c, d);
        if (ok) {
          float lv = (((uint16_t)a << 8) | b) / 32768.0f;
          float ov = (((uint16_t)c << 8) | d) / 8192.0f;
          if (telemetryValueValid(lv, 0.3f, 2.0f) && telemetryValueValid(ov, 0.0f, 5.0f)) {
            car.lambdaB1S1 = lv; car.o2VoltageB1S1 = ov; car.lambdaB1S1Available = true;
          }
        }
      }
      break;
    }

    case 20: {
      // SAE PID 5E - okamzity prutok paliva, pokud ho ECU poskytuje.
      static unsigned long lastAttempt = 0;
      const unsigned long period = car.fuelRateAvailable ? (currentScreen == SCREEN_SENSORS ? 3000UL : 12000UL) : 30000UL;
      if (lastAttempt == 0 || millis() - lastAttempt >= period) {
        lastAttempt = millis();
        ok = queryPid2("015E", "415E", a, b);
        if (ok) {
          float v = (((uint16_t)a << 8) | b) / 20.0f;
          if (telemetryValueValid(v, 0.0f, 60.0f)) { car.fuelRateLph = v; car.fuelRateAvailable = true; }
        }
      }
      break;
    }
  }

  obdPollIndex = (obdPollIndex + 1) % 21;

  if (car.lastGoodObdMs && millis() - car.lastGoodObdMs > 6000UL) {
    invalidateLiveTelemetry(false);
  }
}

void pollDpfOneItem();

void obdBackgroundTask(void *parameter) {
  unsigned long lastProbe = 0;
  uint8_t obdSinceDpf = 0;
  uint8_t drivingPriorityIndex = 0;

  for (;;) {
    // V15.7: v servisnim WEB rezimu je Bluetooth stack vypnuty a OBD task spi.
    if (serviceWifiActive) {
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }

    // Reconnect probiha ZDE, ne v hlavnim loopu. SerialBT.connect() muze na
    // levnych ELM klonech blokovat sekundy; timto zustane dotyk i web responzivni.
    if (reconnectRequested && !reconnectInProgress) {
      reconnectRequested = false;
      if (reconnectAbortRequested || offlineMode) {
        reconnectResult = false;
        reconnectFinished = true;
      } else {
        reconnectInProgress = true;
        reconnectResult = backgroundReconnectDefaultDevice();
        reconnectFinished = true;
        reconnectInProgress = false;
      }
    }

    if (!car.elmConnected) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // Pozadavky z UI na rozsireny read-only scan jednotek obslouzi primo OBD task.
    if (diagScanAllRequested) {
      diagScanAllRequested = false;
      diagnosticExclusive = true;
      scanAllDiagModulesInternal();
      diagnosticExclusive = false;
      diagNeedsRedraw = true;
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (diagSingleRequested >= 0) {
      int idx = diagSingleRequested;
      diagSingleRequested = -1;
      diagnosticExclusive = true;
      scanOneDiagModuleInternal(idx);
      if (diagMutex && xSemaphoreTake(diagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        dtc.busy = false;
        xSemaphoreGive(diagMutex);
      } else {
        dtc.busy = false;
      }
      diagnosticExclusive = false;
      diagNeedsRedraw = true;
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // SPECIALS / PODEKOVAT: jedinym vlastnikem ELM je OBD task.
    // Tim se diagnosticky Output Test nemuze prekryt s normalnim PID/DPF pollingem.
    if (specialsRequestPending) {
      specialsRequestPending = false;
      diagnosticExclusive = true;
      runThankYouElmSequence();
      diagnosticExclusive = false;
      vTaskDelay(pdMS_TO_TICKS(120));
      continue;
    }

    // Pri rucnim diagnostickem/DPF discovery scanu nechame ELM pouze konzoli.
    if (diagnosticExclusive) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    // ELM je pripojeny, ale ECU zatim neodpovida.
    // Zkousime pouze jeden probe po 5 s, ne osm PIDu dokola.
    if (!car.ecuResponding) {
      if (millis() - lastProbe >= ECU_REPROBE_INTERVAL_MS) {
        lastProbe = millis();
        probeEcu(5000);
      }
      vTaskDelay(pdMS_TO_TICKS(powerLowMode ? 500UL : 100UL));
      continue;
    }

    bool fastScreen = (obdDataMode == 2) &&
                      (currentScreen == SCREEN_ENGINE || currentScreen == SCREEN_TURBO || currentScreen == SCREEN_LIVE);

    if (fastScreen) {
      // V16.25: foreground scheduler. Na PREHLEDU/MOTOR/TURBO jsou rychle RPM
      // a MAP, ale rychlost, coolant a ambient uz necekaji na cely 21-PID cyklus.
      switch (fastPollIndex++ % 8) {
        case 0: case 2: case 5: pollFastCorePid(true); break;
        case 1: case 4:         pollFastCorePid(false); break;
        case 3:                 pollDrivingPriorityPid(drivingPriorityIndex++); break;
        case 6:                 pollObdOneItem(); break;
        default:                pollObdOneItem(); break;
      }
    } else {
      pollObdOneItem();
    }

    // DPF ma pri aktivni regeneraci nejvyssi prioritu. Na DPF/SENZORY/PREHLED
    // se obnovuje casteji, na ostatnich obrazovkach setrime ELM.
    uint8_t dpfDivider = dpf.regenActive ? 1 :
                         (currentScreen == SCREEN_DPF ? 2 :
                         ((currentScreen == SCREEN_SENSORS || currentScreen == SCREEN_LIVE) ? 3 :
                         (fastScreen ? 7 : 5)));
    if (++obdSinceDpf >= dpfDivider) {
      obdSinceDpf = 0;
      pollDpfOneItem();
    }

    // Standardni PID 7A pro diferencialni tlak DPF. Kdyz ho ECU nepodporuje,
    // UI zustane na -- a firmware nic nevymysli.
    unsigned long pressurePeriod = powerLowMode ? 15000UL :
                                   (dpf.regenActive || currentScreen == SCREEN_DPF ? 1000UL :
                                   (currentScreen == SCREEN_SENSORS ? 2500UL : 6000UL));
    if (vehicleCaps.standardDpfPressure && millis() - lastPressureAttemptMs >= pressurePeriod) {
      lastPressureAttemptMs = millis();
      pollStandardDpfPressure();
    }

    vTaskDelay(pdMS_TO_TICKS(smartObdLoopDelayMs(fastScreen)));
  }
}

// ============================ VAG DPF =========================

// V7 DPF DISCOVERY
// ----------------
// Nasledujici scan pouziva VYHRADNE UDS service 0x22 ReadDataByIdentifier.
// Nic nekoduje, nemaze ani nezapisuje. Je urcen pouze k nalezeni toho, jak
// konkretni SW varianta PCR2.1 mapuje merene hodnoty. Kandidati vychazeji z
// "Loc" cisel, ktera se v ruznych CAYC/PCR2.1 VCDS logach objevuji u DPF.
// DULEZITE: Loc NENI potvrzeny DID. Proto pozitivni odpoved pouze zalogujeme
// jako kandidat a NESNAZIME se ji automaticky interpretovat.

const uint16_t DPF_DISCOVERY_DIDS[] = {
  // EGT kandidati z beznych CAYC logu
  0x0089, // Loc 137
  0x008B, // Loc 139
  0x008D, // Loc 141

  // DPF sada - ruzne SW varianty pouzivaji Loc cca 226..262
  0x00E2, // 226
  0x00E5, // 229
  0x00E6, // 230
  0x00E7, // 231
  0x00E8, // 232
  0x00E9, // 233
  0x00EA, // 234
  0x00EB, // 235
  0x00EC, // 236
  0x00ED, // 237
  0x00EE, // 238
  0x00EF, // 239
  0x00F0, // 240
  0x00F1, // 241
  0x00FA, // 250
  0x00FB, // 251
  0x00FD, // 253
  0x00FE, // 254
  0x00FF, // 255
  0x0100, // 256
  0x0101, // 257
  0x0102, // 258
  0x0103, // 259
  0x0104, // 260
  0x0105, // 261
  0x0106, // 262

  // dalsi DPF-related location cisla, ktera se u PCR2.1 objevovala v logach
  0x0199, // Loc 409
  0x01B5  // Loc 437
};

bool udsNegativeResponse(const String &r, uint8_t service, uint8_t *nrc = nullptr) {
  String s = r;
  s.toUpperCase();
  s.replace(" ", "");
  s.replace("\r", "");
  s.replace("\n", "");
  s.replace(">", "");

  char prefix[5];
  snprintf(prefix, sizeof(prefix), "7F%02X", service);
  int p = s.indexOf(prefix);
  if (p < 0) return false;

  if (nrc && s.length() >= p + 6) {
    *nrc = (uint8_t)strtoul(s.substring(p + 4, p + 6).c_str(), nullptr, 16);
  }
  return true;
}

void printAsciiFromUdsPayload(const String &raw, uint16_t did) {
  String s = raw;
  s.toUpperCase();
  s.replace(" ", "");
  s.replace("\r", "");
  s.replace("\n", "");
  s.replace(">", "");

  char needle[8];
  snprintf(needle, sizeof(needle), "62%04X", did);
  int p = s.indexOf(needle);
  if (p < 0) return;
  p += 6;

  String ascii;
  for (int i = p; i + 1 < s.length(); i += 2) {
    uint8_t b = (uint8_t)strtoul(s.substring(i, i + 2).c_str(), nullptr, 16);
    if (b >= 32 && b <= 126) ascii += (char)b;
    else if (ascii.length()) ascii += '.';
  }

  if (ascii.length()) Serial.println("ASCII: " + ascii);
}

void runDpfDiscovery() {
  if (activeEngineProfile != ENGINE_PROFILE_CAYC_PCR21) {
    Serial.println("DPFSCAN: povoleno pouze pro overeny CAYC/PCR2.1 profil.");
    return;
  }
  if (!car.elmConnected || !car.ecuResponding) {
    Serial.println("DPFSCAN: ECU neni online.");
    return;
  }

  Serial.println();
  Serial.println("====================================================");
  Serial.println("CAYC / PCR2.1 DPF DISCOVERY - READ ONLY");
  Serial.println("Pouziva pouze UDS 0x22 ReadDataByIdentifier.");
  Serial.println("Vypisuji pouze identifikaci ECU a POZITIVNI DPF kandidaty.");
  Serial.println("====================================================");

  diagnosticExclusive = true;
  vTaskDelay(pdMS_TO_TICKS(250));  // nechame dobehnout pripadny prave bezici OBD dotaz

  selectEngineEcu();

  // Nejdřív znovu identifikace ECU, at je log kompletni v jednom bloku.
  const uint16_t idDids[] = {0xF190, 0xF187, 0xF189, 0xF19E, 0xF180, 0xF188, 0xF18A};
  for (uint16_t did : idDids) {
    char label[8];
    snprintf(label, sizeof(label), "%04X", did);
    String r = udsReadDid(did, 1800);
    Serial.println(String("ID 22") + label + " => " + r);
    if (udsPositive(r, did)) printAsciiFromUdsPayload(r, did);
    vTaskDelay(pdMS_TO_TICKS(40));
  }

  Serial.println("---------------- DPF candidate DID scan ----------------");

  int positives = 0;
  int negatives = 0;
  int timeouts = 0;

  for (uint16_t did : DPF_DISCOVERY_DIDS) {
    char didHex[8];
    snprintf(didHex, sizeof(didHex), "%04X", did);

    String r = udsReadDid(did, 1100);

    if (udsPositive(r, did)) {
      positives++;
      Serial.println(String("DPF_CANDIDATE POSITIVE 22") + didHex + " => " + r);
    } else {
      uint8_t nrc = 0;
      if (udsNegativeResponse(r, 0x22, &nrc)) {
        negatives++;
      } else {
        timeouts++;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }

  Serial.println("---------------- DPF discovery summary -----------------");
  Serial.printf("Positive: %d | Negative: %d | No response: %d\n", positives, negatives, timeouts);
  Serial.println("====================================================");
  Serial.println("Zkopiruj tento kratky blok - obsahuje jen identifikaci a pozitivni kandidaty.");
  Serial.println();

  diagnosticExclusive = false;
}

// Cileny read-only test znamych VAG diesel DID kandidatu pro DPF.
// Neprovadi zadny zapis do ECU. Behem testu je bezny OBD polling pozastaven,
// aby se odpovedi nemichaly s 010C/0105/ATRV atd.
void runDpfTargetTest() {
  if (activeEngineProfile != ENGINE_PROFILE_CAYC_PCR21) {
    Serial.println("DPFTEST: povoleno pouze pro overeny CAYC/PCR2.1 profil.");
    return;
  }
  if (!car.elmConnected || !car.ecuResponding) {
    Serial.println("DPFTEST: ECU neni online.");
    return;
  }

  static const uint16_t dids[] = {
    0x208B,
    0x208C,
    0x208D,
    0x2089,
    0x20D8,
    0x406C,
    0x207D
  };

  Serial.println();
  Serial.println("================ DPFTEST READ-ONLY ================");
  Serial.println("ECU: 03L906023LP / SW 9972 / CAYC PCR2.1");
  Serial.println("Vypisuji presne 7 odpovedi. Nic se do ECU nezapisuje.");
  Serial.println("===================================================");

  diagnosticExclusive = true;
  vTaskDelay(pdMS_TO_TICKS(250));
  selectEngineEcu();

  for (uint16_t did : dids) {
    char cmd[8];
    snprintf(cmd, sizeof(cmd), "22%04X", did);
    String r = udsReadDid(did, 2500);
    r.trim();
    Serial.print(cmd);
    Serial.print(" = ");
    Serial.println(r.length() ? r : "NO RESPONSE");
    vTaskDelay(pdMS_TO_TICKS(80));
  }

  diagnosticExclusive = false;
  Serial.println("================ DPFTEST HOTOVO ===================");
  Serial.println();
}

bool extractUdsDataBytes(const String &response, uint16_t did, uint8_t *out, int count) {
  String s = response;
  s.toUpperCase();
  s.replace(" ", "");
  s.replace("\r", "");
  s.replace("\n", "");
  s.replace(">", "");

  char needle[8];
  snprintf(needle, sizeof(needle), "62%04X", did);
  int pos = s.indexOf(needle);
  if (pos < 0) return false;
  pos += 6;
  if (s.length() < pos + count * 2) return false;

  for (int i = 0; i < count; i++) {
    out[i] = (uint8_t)strtoul(s.substring(pos + i * 2, pos + i * 2 + 2).c_str(), nullptr, 16);
  }
  return true;
}

uint32_t be32(const uint8_t *b) {
  return ((uint32_t)b[0] << 24) |
         ((uint32_t)b[1] << 16) |
         ((uint32_t)b[2] << 8)  |
         (uint32_t)b[3];
}

void pollDpfOneItem() {
  if (!vehicleCaps.advancedDpf) {
    dpf.available = false;
    dpf.regenActive = false;
    dpf.sootCalculatedG = NAN;
    dpf.sootMeasuredG = NAN;
    dpf.kmSinceRegen = NAN;
    dpf.fuelSinceRegenL = NAN;
    dpf.regenStatusAvailable = false;
    return;
  }
  static const uint16_t dids[] = {0x208B, 0x208C, 0x208D, 0x20D8, 0x207D};
  static uint8_t index = 0;
  static uint8_t validMask = 0;
  static unsigned long lastOkMs[5] = {0, 0, 0, 0, 0};
  static bool linkWasReady = false;
  static uint8_t regenOnConfirm = 0;
  static uint8_t regenOffConfirm = 0;

  bool linkReady = car.elmConnected && car.ecuResponding && car.udsResponding;

  // invalidateLiveTelemetry() nastavuje lastUpdateMs=0 i v okamziku, kdy tato
  // funkce zrovna nebezi. Tím spolehlive poznáme novou diagnostickou session
  // a nesmime do ni prenest validMask ze stareho spojeni.
  if (linkReady && dpf.lastUpdateMs == 0 && (validMask != 0 || linkWasReady)) {
    index = 0;
    validMask = 0;
    for (int i = 0; i < 5; i++) lastOkMs[i] = 0;
    regenOnConfirm = regenOffConfirm = 0;
    linkWasReady = false;
  }

  if (!linkReady) {
    if (linkWasReady) {
      index = 0;
      validMask = 0;
      for (int i = 0; i < 5; i++) lastOkMs[i] = 0;
      dpf.sootCalculatedG = NAN;
      dpf.sootMeasuredG = NAN;
      dpf.kmSinceRegen = NAN;
      dpf.fuelSinceRegenL = NAN;
      dpf.regenRaw = 0;
      dpf.regenStatusAvailable = false;
      dpf.regenStatusUpdateMs = 0;
      regenOnConfirm = regenOffConfirm = 0;
    }
    linkWasReady = false;
    dpf.available = false;
    dpf.regenActive = false;
    return;
  }

  linkWasReady = true;
  if (diagnosticExclusive) return;

  // Potvrzeno primo na 03L906023LP / SW 9972:
  // 208B = calculated soot, 0.01 g
  // 208C = measured soot, signed 0.01 g
  // 208D = distance since regeneration, 0.1 km
  // 20D8 = fuel since regeneration, 0.01 l
  // 207D = regen activity/raw counter (0 mimo regeneraci; definitivni chovani
  //        se dale overi pri skutecne aktivni regeneraci)
  // Stav regenerace pollujeme casteji nez ostatni DPF DIDy, aby popup
  // reagoval rychle i kdyz je otevrena jina obrazovka.
  static unsigned long lastFastRegenStatusMs = 0;
  bool forcedRegenStatus = index != 4 &&
                           millis() - lastFastRegenStatusMs >= (dpf.regenActive ? 1200UL : 2500UL);
  uint8_t slot = forcedRegenStatus ? 4 : index;
  uint16_t did = dids[slot];
  if (forcedRegenStatus) lastFastRegenStatusMs = millis();
  String r = udsReadDid(did, 900);
  uint8_t b[4] = {0, 0, 0, 0};
  bool ok = false;

  switch (did) {
    case 0x208B:
      ok = extractUdsDataBytes(r, did, b, 2);
      if (ok) {
        float v = (((uint16_t)b[0] << 8) | b[1]) / 100.0f;
        ok = telemetryValueValid(v, 0.0f, 80.0f);
        if (ok) dpf.sootCalculatedG = v;
      }
      break;

    case 0x208C:
      ok = extractUdsDataBytes(r, did, b, 2);
      if (ok) {
        int16_t raw = (int16_t)(((uint16_t)b[0] << 8) | b[1]);
        float v = raw / 100.0f;
        ok = telemetryValueValid(v, -20.0f, 80.0f);
        if (ok) dpf.sootMeasuredG = v;
      }
      break;

    case 0x208D:
      ok = extractUdsDataBytes(r, did, b, 4);
      if (ok) {
        float v = be32(b) / 10.0f;
        ok = telemetryValueValid(v, 0.0f, 3000.0f);
        if (ok) dpf.kmSinceRegen = v;
      }
      break;

    case 0x20D8:
      ok = extractUdsDataBytes(r, did, b, 2);
      if (ok) {
        float v = (((uint16_t)b[0] << 8) | b[1]) / 100.0f;
        ok = telemetryValueValid(v, 0.0f, 200.0f);
        if (ok) dpf.fuelSinceRegenL = v;
      }
      break;

    case 0x207D:
      ok = extractUdsDataBytes(r, did, b, 4);
      if (ok) {
        dpf.regenRaw = be32(b);
        bool rawActive = dpf.regenRaw > 0;
        if (rawActive) {
          regenOffConfirm = 0;
          if (regenOnConfirm < 3) regenOnConfirm++;
          if (regenOnConfirm >= 2) dpf.regenActive = true;
        } else {
          regenOnConfirm = 0;
          if (regenOffConfirm < 3) regenOffConfirm++;
          if (regenOffConfirm >= 2) dpf.regenActive = false;
        }
        dpf.regenStatusAvailable = true;
        dpf.regenStatusUpdateMs = millis();
      }
      break;
  }

  unsigned long now = millis();
  if (ok) {
    validMask |= (1u << slot);
    lastOkMs[slot] = now;
    dpf.lastUpdateMs = now;
  }

  // Nestaci, ze kazdy DID odpovedel nekdy v minulosti. Vsechny 4 zakladni
  // DPF hodnoty musi byt cerstve; jinak bychom mohli zobrazovat hodiny starou
  // hodnotu vedle nove aktualizovanych dat z ostatnich DIDu.
  bool fresh[4];
  bool baseFresh = true;
  for (int i = 0; i < 4; i++) {
    fresh[i] = lastOkMs[i] != 0 && now - lastOkMs[i] <= 12000UL;
    if (!fresh[i]) baseFresh = false;
  }
  // Kdyz konkretni hodnota zastara, radeji ukazeme -- nez stare cislo.
  if (!fresh[0]) dpf.sootCalculatedG = NAN;
  if (!fresh[1]) dpf.sootMeasuredG = NAN;
  if (!fresh[2]) dpf.kmSinceRegen = NAN;
  if (!fresh[3]) dpf.fuelSinceRegenL = NAN;
  dpf.available = baseFresh;

  if (!forcedRegenStatus)
    index = (index + 1) % (sizeof(dids) / sizeof(dids[0]));
}

void readVagDpfData() {
  // V10: skutecne ELM/UDS cteni probiha v obdBackgroundTask(), aby zadny
  // UDS timeout neblokoval dotyk ani vykreslovani. Tato funkce uz pouze
  // hlida ztratu spojeni/starych dat.
  if (!car.elmConnected || !car.ecuResponding || !car.udsResponding) {
    dpf.available = false;
    dpf.regenActive = false;
    return;
  }

  if (dpf.lastUpdateMs && millis() - dpf.lastUpdateMs > 12000UL) {
    dpf.available = false;
  }
  if (!dpf.regenStatusUpdateMs || millis() - dpf.regenStatusUpdateMs > 12000UL) {
    dpf.regenStatusAvailable = false;
    dpf.regenActive = false;
  }
}

// Serial diagnosticka konzole.
// V9 prijme Enter pri CR, LF i CR+LF, takze neni zavisla na nastaveni Line ending.
// Prikazy:
//   ID             -> znovu precte F190/F187/F189/F19E
//   ECU            -> test 0100
//   UDS F190       -> libovolny READ-ONLY DID (22F190)
//   RAW 22F190     -> read-only UDS raw prikaz; povolena je pouze service 0x22
//   DPFSCAN        -> stary sirsi discovery scan
//   DPFTEST        -> presne 7 cilenych read-only DPF kandidatu
//   QUIET          -> potvrdi, ze bezny OBD logger je vypnuty
void processSerialDiagnosticCommand(String line) {
  line.trim();
  if (!line.length()) return;

  String upper = line;
  upper.toUpperCase();

  if (upper == "ID") {
    diagnosticExclusive = true;
    vTaskDelay(pdMS_TO_TICKS(120));
    identifyEcuUds();
    diagnosticExclusive = false;
  } else if (upper == "ECU") {
    diagnosticExclusive = true;
    vTaskDelay(pdMS_TO_TICKS(120));
    bool online = probeEcu(3500);
    diagnosticExclusive = false;
    Serial.println(String("ECU 0100: ") + (online ? "ONLINE" : "NO RESPONSE"));
  } else if (upper.startsWith("UDS ")) {
    String hex = upper.substring(4);
    uint16_t did = (uint16_t)strtoul(hex.c_str(), nullptr, 16);
    diagnosticExclusive = true;
    vTaskDelay(pdMS_TO_TICKS(120));
    String resp = udsReadDid(did, 2500);
    diagnosticExclusive = false;
    Serial.println("UDS READ " + hex + " => " + resp);
  } else if (upper == "DPFSCAN") {
    runDpfDiscovery();
  } else if (upper == "DPFTEST") {
    runDpfTargetTest();
  } else if (upper == "QUIET") {
    Serial.println("OK: bezny OBD provoz je tichy. Pouzij DPFTEST pro 7 cilenych odpovedi.");
  } else if (upper.startsWith("RAW 22") && upper.length() == 10) {
    // Bezpecnost: povolime pouze UDS service 0x22 ReadDataByIdentifier (read-only).
    diagnosticExclusive = true;
    vTaskDelay(pdMS_TO_TICKS(100));
    selectEngineEcu();
    Serial.println("RAW READ => " + elmCommand(upper.substring(4), 2500));
    diagnosticExclusive = false;
  } else {
    Serial.println("Prikazy: ID | ECU | DPFTEST | DPFSCAN | QUIET | UDS F190 | RAW 22F190");
  }
}

void handleSerialDiagnosticConsole() {
  static String line;

  while (Serial.available()) {
    char c = (char)Serial.read();

    // V9: CR i LF samostatne ukoncuji prikaz. U CR+LF druhy znak pouze narazi
    // na prazdny buffer a nic znovu nespusti.
    if (c == '\r' || c == '\n') {
      if (line.length()) {
        String cmd = line;
        line = "";
        processSerialDiagnosticCommand(cmd);
      }
      continue;
    }

    if (line.length() < 80 && c >= 32 && c <= 126) {
      line += c;
    }
  }
}

static void captureWebVehicleSnapshot() {
  // Aktualizujeme jen pokud opravdu existuji data z auta; rucni odpojeni
  // v offline stavu tak neprepise posledni dobry snapshot prazdnymi hodnotami.
  if (!(car.elmConnected || car.ecuResponding || dpf.available || !isnan(car.coolantC))) return;

  refreshDpfPredictionHistory(false);
  webVehicleSnapshot.valid = true;
  webVehicleSnapshot.epoch = clockIsValid() ? time(nullptr) : 0;
  webVehicleSnapshot.rpm = car.rpm;
  webVehicleSnapshot.coolantC = car.coolantC;
  webVehicleSnapshot.boostBar = currentBoostBar();
  webVehicleSnapshot.moduleVoltageV = car.moduleVoltageAvailable ? car.moduleVoltageV : NAN;
  webVehicleSnapshot.ambientTempC = car.ambientAvailable ? car.ambientTempC : NAN;
  webVehicleSnapshot.regenActive = dpf.regenActive;
  webVehicleSnapshot.dpfPct = dpfLoadingPercent();
  webVehicleSnapshot.dpfCalcG = dpf.sootCalculatedG;
  webVehicleSnapshot.dpfMeasuredG = dpf.sootMeasuredG;
  webVehicleSnapshot.dpfDiffHpa = dpf.diffPressureAvailable ? dpf.diffPressureHpa : NAN;
  webVehicleSnapshot.dpfKmSince = dpf.kmSinceRegen;
  webVehicleSnapshot.dpfKmLeft = dpfEstimatedKmToRegen();
  webVehicleSnapshot.dpfHistoryAvgKm = dpfHistoricalIntervalKm;
}

// =========================== WI-FI / WEB ======================

String jsonNum(float v, uint8_t dec = 2) {
  if (isnan(v)) return "null";
  return String(v, static_cast<unsigned int>(dec));
}

String jsonEscape(String v) {
  v.replace("\\", "\\\\");
  v.replace("\"", "\\\"");
  v.replace("\n", "\\n");
  v.replace("\r", "");
  return v;
}

String pdfSafe(const String &src) {
  // Zakladni PDF Helvetica nema vlozeny Unicode font. Pro ceske reporty proto
  // prevedeme diakritiku na ASCII misto toho, aby pismena z PDF mizela.
  String clean = src;
  const char *from[] = {"á","č","ď","é","ě","í","ň","ó","ř","š","ť","ú","ů","ý","ž",
                        "Á","Č","Ď","É","Ě","Í","Ň","Ó","Ř","Š","Ť","Ú","Ů","Ý","Ž"};
  const char *to[]   = {"a","c","d","e","e","i","n","o","r","s","t","u","u","y","z",
                        "A","C","D","E","E","I","N","O","R","S","T","U","U","Y","Z"};
  for (int i = 0; i < 30; i++) clean.replace(from[i], to[i]);

  String out;
  for (size_t i = 0; i < clean.length(); i++) {
    uint8_t c = (uint8_t)clean[i];
    if (c == '(' || c == ')' || c == '\\') { out += '\\'; out += (char)c; }
    else if (c >= 32 && c <= 126) out += (char)c;
  }
  return out;
}

String pdfOffset10(unsigned long v) {
  String s = String(v);
  while (s.length() < 10) s = "0" + s;
  return s;
}

String buildSimplePdf(const String &title, String *lines, int count) {
  // Jednotna tiskova predloha: tmava hlavicka, akcentni linka, radky v kartach a footer.
  String stream;
  stream.reserve(7000);
  stream += "q\n0.035 0.075 0.095 rg\n0 754 595 88 re f\nQ\n";
  stream += "q\n0.10 0.78 0.88 rg\n0 748 595 6 re f\nQ\n";
  stream += "BT\n/F2 20 Tf\n1 1 1 rg\n40 805 Td\n(OCTAVIA II PC) Tj\n";
  stream += "/F1 9 Tf\n0.75 0.88 0.92 rg\n0 -19 Td\n(Vehicle monitor / service report) Tj\nET\n";
  stream += "BT\n/F2 16 Tf\n0.06 0.13 0.16 rg\n40 718 Td\n(" + pdfSafe(title) + ") Tj\nET\n";
  stream += "q\n0.10 0.78 0.88 RG\n1.3 w\n40 705 m 555 705 l S\nQ\n";

  int maxLines = min(count, 30);
  int y = 680;
  for (int i = 0; i < maxLines; i++) {
    if (lines[i].length() == 0) { y -= 12; continue; }
    if ((i & 1) == 0) stream += "q\n0.95 0.97 0.98 rg\n38 " + String(y-5) + " 519 20 re f\nQ\n";
    stream += "BT\n/F1 8.5 Tf\n0.08 0.12 0.14 rg\n45 " + String(y) + " Td\n(" + pdfSafe(lines[i]) + ") Tj\nET\n";
    y -= 21;
    if (y < 65) break;
  }

  String footer = "Firmware " + String(FW_VERSION) + "  |  " + developerName;
  if (developerWeb.length()) footer += "  |  " + developerWeb;
  stream += "q\n0.80 0.85 0.87 RG\n0.6 w\n40 45 m 555 45 l S\nQ\n";
  stream += "BT\n/F1 7.5 Tf\n0.35 0.42 0.45 rg\n40 29 Td\n(" + pdfSafe(footer) + ") Tj\nET\n";

  String pdf = "%PDF-1.4\n";
  unsigned long off[7] = {0};
  off[1] = pdf.length(); pdf += "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
  off[2] = pdf.length(); pdf += "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n";
  off[3] = pdf.length(); pdf += "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 4 0 R /F2 5 0 R >> >> /Contents 6 0 R >>\nendobj\n";
  off[4] = pdf.length(); pdf += "4 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n";
  off[5] = pdf.length(); pdf += "5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>\nendobj\n";
  off[6] = pdf.length(); pdf += "6 0 obj\n<< /Length " + String(stream.length()) + " >>\nstream\n" + stream + "endstream\nendobj\n";
  unsigned long xref = pdf.length();
  pdf += "xref\n0 7\n0000000000 65535 f \n";
  for (int i = 1; i <= 6; i++) pdf += pdfOffset10(off[i]) + " 00000 n \n";
  pdf += "trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n" + String(xref) + "\n%%EOF\n";
  return pdf;
}

String tripRecordSummary(const TripRecord &r) {
  String s = formatDateTime(r.startEpoch) + " - " + formatDateTime(r.endEpoch);
  s += " | " + String(r.distanceKm, 1) + " km";
  if (!isnan(r.fuelLiters)) s += " | " + String(r.fuelLiters, 2) + " L";
  if (!isnan(r.avgConsumption)) s += " | " + String(r.avgConsumption, 1) + " L/100";
  s += " | max " + String(r.maxSpeedKmh, 0) + " | avg " + String(r.avgSpeedKmh, 0);
  return s;
}

void sendTripsPdf() {
  TripRecord rec[24];
  int n = loadRecentTrips(rec, 24);
  TripStats st = getTripStats();
  TripDailySummary day = getTodayTripSummary();
  String lines[34];
  int count = 0;
  lines[count++] = "Octavia II PC | Kniha jizd | Firmware " + String(FW_VERSION);
  lines[count++] = "Vygenerovano: " + formatDateTime(clockIsValid() ? time(nullptr) : 0);
  lines[count++] = "Celkem: " + String(st.count) + " jizd | " + String(st.totalKm, 1) + " km | nejdelsi " + String(st.longestKm, 1) + " km";
  lines[count++] = "Nejlepsi spotreba: " + (isnan(st.bestConsumption) ? String("--") : String(st.bestConsumption, 1) + " L/100 km");
  if (clockIsValid()) {
    float dc = (day.fuelAvailable && day.distanceKm > 0.05f) ? day.fuelLiters * 100.0f / day.distanceKm : NAN;
    lines[count++] = "Dnes: " + String(day.count) + " jizd | " + String(day.distanceKm, 1) + " km | " + formatDuration(day.durationSec) +
                     " | " + (isnan(dc) ? String("--") : String(dc, 1) + " L/100");
  }
  lines[count++] = "";
  for (int i = n - 1; i >= 0 && count < 34; i--) {
    String flags = tripRecordIsShort(rec[i]) ? " [KRATKA]" : "";
    lines[count++] = tripAutoName(rec[i].startEpoch, rec[i].id) + flags + " | " + tripRecordSummary(rec[i]);
  }
  String pdf = buildSimplePdf("Prehled jizd - Octavia II PC", lines, count);
  server.sendHeader("Content-Disposition", "attachment; filename=prehled_jizd.pdf");
  server.send(200, "application/pdf", pdf);
}

void sendTripPdf(int id) {
  TripRecord r;
  if (!loadTripById(id, r)) { server.send(404, "text/plain", "Jizda nenalezena"); return; }
  String lines[18]; int n = 0;
  lines[n++] = tripAutoName(r.startEpoch, id) + (tripRecordIsShort(r) ? " | KRATKA JIZDA" : "");
  lines[n++] = "Zacatek: " + formatDateTime(r.startEpoch);
  lines[n++] = "Konec: " + formatDateTime(r.endEpoch);
  lines[n++] = "Vzdalenost: " + String(r.distanceKm, 2) + " km";
  lines[n++] = "Spalene palivo: " + (isnan(r.fuelLiters) ? String("--") : String(r.fuelLiters, 2) + " L");
  lines[n++] = "Prumerna spotreba: " + (isnan(r.avgConsumption) ? String("--") : String(r.avgConsumption, 2) + " L/100 km");
  lines[n++] = "Maximalni rychlost: " + String(r.maxSpeedKmh, 0) + " km/h";
  lines[n++] = "Prumerna rychlost: " + String(r.avgSpeedKmh, 0) + " km/h";
  lines[n++] = "Doba jizdy: " + formatDuration(r.durationSec);
  lines[n++] = "";
  lines[n++] = "Automaticky nazev: " + tripAutoName(r.startEpoch, id);
  lines[n++] = "Vytvoreno firmwarem " + String(FW_VERSION);
  String pdf = buildSimplePdf("Detail jizdy - Octavia II PC", lines, n);
  server.sendHeader("Content-Disposition", "attachment; filename=jizda_" + String(id) + ".pdf");
  server.send(200, "application/pdf", pdf);
}

void syncClockFromEpoch(time_t epoch) {
  if (epoch < 1700000000) return;
  timeval tv; tv.tv_sec = epoch; tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  if (trip.active && trip.startEpoch <= 0)
    trip.startEpoch = epoch - (time_t)currentTripElapsedSec();
}

static const char WEB_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="cs"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Octavia II PC</title>
<style>
:root{color-scheme:dark;--bg:#071015;--panel:#0d1a21;--card:#101f27;--line:#28434f;--text:#f1f8fa;--muted:#90a8b2;--cyan:#3de7ff;--green:#48d78d;--yellow:#ffd05a;--orange:#ff9f43;--red:#ff6678}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,-apple-system,Segoe UI,sans-serif}.app{max-width:880px;margin:auto;padding:12px 12px 80px}.top{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:6px 2px 12px}.title{font-weight:900;font-size:22px;letter-spacing:.3px}.badge{font-size:12px;color:var(--cyan);border:1px solid #245567;border-radius:999px;padding:5px 9px}.nav{display:grid;grid-template-columns:repeat(6,1fr);gap:5px;position:sticky;top:0;z-index:5;background:#071015e8;padding:6px 0 9px;backdrop-filter:blur(8px)}.nav button{min-height:42px;padding:7px 4px;border-radius:9px;border:1px solid var(--line);background:#0b171d;color:var(--muted);font-weight:800;font-size:12px}.nav button.on{background:#12313c;color:white;border-color:#2d7188}.page{display:none}.page.on{display:block}.hero,.panel{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:13px;margin:10px 0}.hero h2,.panel h2{margin:0 0 10px;font-size:18px}.muted{color:var(--muted)}.small{font-size:12px;color:var(--muted)}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(138px,1fr));gap:8px}.card{background:var(--card);border:1px solid #203a45;border-radius:11px;padding:11px;min-width:0}.lab{font-size:11px;text-transform:uppercase;letter-spacing:.45px;color:var(--muted)}.val{font-size:23px;font-weight:900;margin-top:3px;white-space:nowrap}.cyan{color:var(--cyan)}.green{color:var(--green)}.yellow{color:var(--yellow)}.orange{color:var(--orange)}.red{color:var(--red)}.bar{height:10px;background:#081217;border-radius:999px;overflow:hidden;margin-top:8px;border:1px solid #203640}.bar>i{display:block;height:100%;width:0;background:var(--cyan)}.actions{display:flex;gap:7px;flex-wrap:wrap;margin-top:10px}.btn,button.action{border:1px solid #2c6072;background:#12313c;color:#fff;border-radius:9px;padding:9px 11px;text-decoration:none;font-weight:800;font-size:13px}.list{display:grid;gap:7px}.row{background:#0a161c;border:1px solid #1e3741;border-radius:10px;padding:10px}.row summary{cursor:pointer;font-weight:850}.kv{display:grid;grid-template-columns:repeat(auto-fit,minmax(125px,1fr));gap:6px;margin-top:8px}.kv div{background:#071217;border-radius:8px;padding:7px}.tabs{display:flex;gap:5px;flex-wrap:wrap;margin-bottom:10px}.tabs button{border:1px solid var(--line);background:#0a171d;color:var(--muted);border-radius:8px;padding:7px 9px;font-weight:750}.tabs button.on{color:white;border-color:#2d7188;background:#12313c}.setgroup{display:none}.setgroup.on{display:block}.formgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:9px}.field{background:#0a161c;border:1px solid #203a45;border-radius:10px;padding:9px}.field label{display:block;font-size:12px;color:var(--muted);margin-bottom:4px}.field input,.field select,.field textarea{width:100%;border:1px solid #34515d;background:#061116;color:white;border-radius:8px;padding:9px;font:inherit}.field input[type=checkbox]{width:auto;transform:scale(1.25);margin-right:7px}.chart{width:100%;height:150px;background:#081419;border:1px solid #1f3944;border-radius:10px}.notice{border-left:3px solid var(--cyan);padding:8px 10px;background:#0a171d;border-radius:6px;margin:8px 0}.statusline{display:flex;justify-content:space-between;gap:8px;border-bottom:1px solid #18303a;padding:7px 0}.statusline:last-child{border:0}.spinner{opacity:.65}.danger{border-color:#76333c!important;background:#35161c!important}@media(max-width:560px){.nav{grid-template-columns:repeat(3,1fr)}.val{font-size:21px}}@media print{body{background:#fff;color:#000}.top,.nav,.noprint{display:none!important}.app{max-width:none;padding:0}.page{display:none!important}.page.on{display:block!important}.panel,.hero,.card,.row{background:#fff;color:#000;border:1px solid #bbb;break-inside:avoid}.muted,.small,.lab{color:#555!important}.chart{border:1px solid #bbb}}
</style></head><body><div class="app">
<div class="top"><div><div class="title">OCTAVIA II PC</div><div class="small">Servisní WEB V2 · lehké API · vizualizace v telefonu</div></div><div class="badge" id="fw">FW --</div></div>
<nav class="nav noprint"><button class="on" data-page="overview">PŘEHLED</button><button data-page="trips">JÍZDY</button><button data-page="dpf">DPF</button><button data-page="fuel">PALIVO</button><button data-page="settings">NASTAVENÍ</button><button data-page="system">SYSTÉM</button></nav>

<section class="page on" id="page-overview">
<div class="notice">Servisní Wi-Fi běží odděleně od Bluetooth ELM. Hodnoty auta jsou <b>poslední snapshot před odpojením ELM</b>; web tím nezatěžuje OBD.</div>
<div class="hero"><h2>Stav vozidla</h2><div class="grid"><div class="card"><div class="lab">Motor</div><div class="val" id="ovTemp">--</div></div><div class="card"><div class="lab">RPM</div><div class="val" id="ovRpm">--</div></div><div class="card"><div class="lab">Turbo</div><div class="val" id="ovBoost">--</div></div><div class="card"><div class="lab">Napětí ECU</div><div class="val" id="ovVolt">--</div></div></div><div class="small" id="snapshotTime" style="margin-top:8px">Snapshot: --</div></div>
<div class="panel"><h2>DPF</h2><div class="grid"><div class="card"><div class="lab">Zanesení</div><div class="val" id="ovDpf">--</div><div class="bar"><i id="ovDpfBar"></i></div></div><div class="card"><div class="lab">Do regenerace</div><div class="val" id="ovDpfLeft">--</div></div><div class="card"><div class="lab">Od regenerace</div><div class="val" id="ovDpfSince">--</div></div></div></div>
<div class="panel"><h2>Dnes</h2><div class="grid"><div class="card"><div class="lab">Jízdy</div><div class="val" id="dayCount">--</div></div><div class="card"><div class="lab">Vzdálenost</div><div class="val" id="dayKm">--</div></div><div class="card"><div class="lab">Doba</div><div class="val" id="dayTime">--</div></div><div class="card"><div class="lab">Spotřeba</div><div class="val" id="dayCons">--</div></div></div></div>
</section>

<section class="page" id="page-trips">
<div class="panel"><h2>Kniha jízd</h2><div class="small">Načítá se až po otevření této záložky. Graf kreslí prohlížeč, ne ESP32.</div><canvas class="chart" id="tripChart" width="820" height="150"></canvas><div class="actions noprint"><a class="btn" href="/trips_export.csv">CSV export</a><button class="action" id="printTrips">Tisk / PDF</button></div></div>
<div class="panel"><h2>Poslední jízdy</h2><div class="list" id="tripList"><div class="muted">Otevři záložku pro načtení…</div></div></div>
</section>

<section class="page" id="page-dpf">
<div class="notice" id="dpfSupportNote">DPF profil se načítá…</div>
<div class="panel"><h2>DPF PRO</h2><div class="grid"><div class="card"><div class="lab">Zanesení</div><div class="val" id="dpfPct">--</div><div class="bar"><i id="dpfBar"></i></div></div><div class="card"><div class="lab">Odhad do regen.</div><div class="val" id="dpfLeft">--</div></div><div class="card"><div class="lab">Od poslední regen.</div><div class="val" id="dpfSince">--</div></div><div class="card"><div class="lab">Dif. tlak</div><div class="val" id="dpfDiff">--</div></div></div></div>
<div class="panel"><h2>Saze</h2><div class="grid"><div class="card"><div class="lab">Vypočítané</div><div class="val" id="dpfCalc">--</div></div><div class="card"><div class="lab">Naměřené</div><div class="val" id="dpfMeasured">--</div></div><div class="card"><div class="lab">Historický interval</div><div class="val" id="dpfAvg">--</div></div><div class="card"><div class="lab">Regenerace</div><div class="val" id="dpfRegen">--</div></div></div></div>
<div class="panel"><h2>Intervaly regenerací</h2><canvas class="chart" id="dpfChart" width="820" height="150"></canvas><div class="list" id="dpfHistory"></div></div>
</section>

<section class="page" id="page-fuel">
<div class="panel"><h2>Tankování</h2><div class="grid"><div class="card"><div class="lab">Záznamů</div><div class="val" id="fuelCount">--</div></div><div class="card"><div class="lab">Celkem přidáno</div><div class="val" id="fuelTotal">--</div></div><div class="card"><div class="lab">Poslední tankování</div><div class="val" id="fuelLast">--</div></div></div><div class="actions noprint"><a class="btn" href="/fueling.csv">CSV data</a></div></div>
<div class="panel"><h2>Historie</h2><div class="list" id="fuelList"></div></div>
</section>

<section class="page" id="page-settings">
<div class="panel"><h2>Nastavení</h2><div class="small">Jedno uložení = jeden požadavek na ESP32. Hodnoty se nezapisují při každém kliknutí.</div><div class="tabs noprint"><button class="on" data-set="display">DISPLEJ</button><button data-set="obd">OBD</button><button data-set="dpf">DPF</button><button data-set="power">POWER</button><button data-set="vehicle">VOZIDLO</button><button data-set="text">TEXTY</button></div>
<form id="configForm">
<div class="setgroup on" data-group="display"><div class="formgrid"><div class="field"><label>Jas den (40–255)</label><input id="cfgDay" type="number" min="40" max="255"></div><div class="field"><label>Jas noc (10–180)</label><input id="cfgNight" type="number" min="10" max="180"></div><div class="field"><label><input id="cfgAutoDay" type="checkbox"> Automatický den / noc</label></div><div class="field"><label><input id="cfgTouch" type="checkbox"> Touch feedback</label></div><div class="field"><label>Intenzita touch feedbacku</label><input id="cfgTouchStr" type="number" min="8" max="60"></div></div></div>
<div class="setgroup" data-group="obd"><div class="formgrid"><div class="field"><label>Režim dat</label><select id="cfgObd"><option value="0">ECO</option><option value="1">NORMAL</option><option value="2">FAST</option></select></div><div class="field"><label><input id="cfgAutoElm" type="checkbox"> Automatické připojení ELM</label></div></div><div class="notice">Během servisní Wi-Fi je Bluetooth ELM záměrně vypnutý. Změna režimu se projeví po ukončení webu.</div></div>
<div class="setgroup" data-group="dpf"><div class="formgrid"><div class="field"><label>100 % DPF (g)</label><input id="cfgDpfLimit" type="number" step="0.5" min="15" max="45"></div><div class="field"><label>Varování do regenerace (km)</label><input id="cfgRegenKm" type="number" min="10" max="50"></div><div class="field"><label><input id="cfgAlarmRegen" type="checkbox"> Upozornit na začátek/konec regenerace</label></div><div class="field"><label><input id="cfgAlarmSoon" type="checkbox"> Upozornit na blížící se regeneraci</label></div></div></div>
<div class="setgroup" data-group="power"><div class="formgrid"><div class="field"><label><input id="cfgAutoDim" type="checkbox"> Automatická správa napájení</label></div><div class="field"><label>DIM po (min)</label><input id="cfgDim" type="number" min="1" max="10"></div><div class="field"><label>Displej OFF po (min)</label><input id="cfgOff" type="number" min="2" max="30"></div><div class="field"><label>LOW POWER po zhasnutí (min)</label><input id="cfgLow" type="number" min="1" max="10"></div></div></div>
<div class="setgroup" data-group="vehicle"><div class="notice"><b>Profil motoru:</b> AUTO je doporučené. U neznámé ECU zůstane aktivní bezpečný Generic OBD režim. Rozšířené DPF údaje se zapnou jen u ověřeného profilu.</div><div class="formgrid"><div class="field"><label>Profil motoru / ECU</label><select id="cfgEngineProfile"><option value="0">AUTO (doporučeno)</option><option value="1">1.6 TDI CAYC / PCR2.1</option><option value="2">2.0 TDI CR / EDC17</option><option value="3">1.9 / 2.0 TDI PD / EDC16</option><option value="4">Generic OBD-II</option></select></div><div class="field"><label>Aktivní / detekovaný profil</label><input id="cfgEngineActive" readonly></div><div class="field"><label>ECU / ODX</label><input id="cfgEngineEcu" readonly></div><div class="field"><label>Kapacita nádrže (l)</label><input id="cfgTank" type="number" step="1" min="35" max="80"></div><div class="field"><label>Záložní spotřeba (l/100 km)</label><input id="cfgRange" type="number" step="0.1" min="2" max="20"></div><div class="field"><label>Min. nárůst pro tankování (l)</label><input id="cfgFuelRise" type="number" step="0.5" min="2" max="12"></div><div class="field"><label>Tachometr (km)</label><input id="cfgOdo" type="number" step="1" min="0" max="2000000"></div></div></div>
<div class="setgroup" data-group="text"><div class="notice"><b>PERSONALIZACE:</b> výchozí texty jsou záměrně označené jako nenastavené. Nahraďte je názvem svého auta / vlastní instalace.</div><div class="formgrid"><div class="field"><label>Úvodní text / název vozidla (NASTAVTE)</label><input id="cfgSplash" maxlength="30"></div><div class="field"><label>Text v horní liště (NASTAVTE)</label><input id="cfgHeader" maxlength="22"></div><div class="field"><label>Jméno vlastníka / správce (volitelné)</label><input id="cfgOwner" maxlength="28" placeholder="Nenastaveno"></div><div class="field"><label>E-mail (volitelné)</label><input id="cfgEmail" maxlength="48" placeholder="Nenastaveno"></div><div class="field"><label>Telefon (volitelné)</label><input id="cfgPhone" maxlength="28" placeholder="Nenastaveno"></div><div class="field"><label>Web / poznámka (volitelné)</label><input id="cfgWeb" maxlength="64" placeholder="Nenastaveno"></div></div></div>
<div class="actions noprint"><button type="submit" class="action">ULOŽIT NASTAVENÍ</button><span class="small" id="saveState"></span></div></form></div>
<div class="panel"><h2>Záloha konfigurace</h2><div class="actions noprint"><a class="btn" href="/config.json">Export JSON</a></div><div class="field" style="margin-top:10px"><label>Import JSON</label><textarea id="importJson" rows="5" placeholder="Vlož obsah octavia_ii_pc_config.json"></textarea></div><button type="button" class="action noprint" id="importConfig">IMPORTOVAT</button></div>
</section>

<section class="page" id="page-system">
<div class="panel"><h2>Systém ESP32</h2><div id="systemLines"><div class="muted">Načítám…</div></div><div class="actions noprint"><button class="action" id="refreshSystem">Obnovit</button><button class="action" id="syncTime">Čas z telefonu</button><button class="action danger" id="wifiOff">Ukončit servisní Wi-Fi</button></div></div>
</section>
</div>
<script>
const $=id=>document.getElementById(id),fmt=(v,d=1,u='')=>v==null?'--':Number(v).toFixed(d)+u;let loaded={overview:false,trips:false,dpf:false,fuel:false,settings:false,system:false};
function drawLineChart(canvas,values,label){const c=canvas.getContext('2d'),w=canvas.width,h=canvas.height;c.clearRect(0,0,w,h);c.strokeStyle='#28434f';c.lineWidth=1;for(let i=1;i<4;i++){let y=20+i*(h-40)/4;c.beginPath();c.moveTo(32,y);c.lineTo(w-10,y);c.stroke()}if(!values.length){c.fillStyle='#90a8b2';c.font='14px system-ui';c.fillText('Zatím nejsou data',20,28);return}let min=Math.min(...values),max=Math.max(...values);if(max-min<.2){min-=.1;max+=.1}c.strokeStyle='#3de7ff';c.lineWidth=3;c.beginPath();values.forEach((v,i)=>{let x=35+i*(w-55)/Math.max(1,values.length-1),y=h-20-(v-min)/(max-min)*(h-45);i?c.lineTo(x,y):c.moveTo(x,y)});c.stroke();c.fillStyle='#90a8b2';c.font='12px system-ui';c.fillText(label+'  '+min.toFixed(1)+'–'+max.toFixed(1),10,14)}
async function json(url,opt){const r=await fetch(url,{cache:'no-store',...(opt||{})});if(!r.ok)throw new Error(await r.text());return r.json()}
async function loadOverview(){try{const d=await json('/api/status');const ds=!!d.vehicle.dpfSupported;$('fw').textContent='FW '+d.fw;$('ovTemp').textContent=fmt(d.vehicle.coolant,0,' °C');$('ovRpm').textContent=fmt(d.vehicle.rpm,0);$('ovBoost').textContent=fmt(d.vehicle.boost,2,' bar');$('ovVolt').textContent=fmt(d.vehicle.voltage,2,' V');$('snapshotTime').textContent='Snapshot: '+(d.vehicle.captured||'není k dispozici')+' · '+(d.vehicle.profile||'Generic OBD');$('ovDpf').textContent=ds?fmt(d.vehicle.dpfPct,0,' %'):'N/A';let p=ds?Math.max(0,Math.min(100,d.vehicle.dpfPct||0)):0;$('ovDpfBar').style.width=p+'%';$('ovDpfBar').style.background=p>=90?'#ff9f43':p>=70?'#ffd05a':'#48d78d';$('ovDpfLeft').textContent=ds?(d.vehicle.regen?'AKTIVNÍ':fmt(d.vehicle.dpfLeft,0,' km')):'N/A';$('ovDpfSince').textContent=ds?fmt(d.vehicle.dpfSince,0,' km'):'N/A';$('dayCount').textContent=d.today.count;$('dayKm').textContent=fmt(d.today.km,1,' km');$('dayTime').textContent=d.today.duration;$('dayCons').textContent=fmt(d.today.consumption,1,' l/100')}catch(e){$('snapshotTime').textContent='Snapshot se nepodařilo načíst.'}}
async function loadTrips(){try{const d=await json('/api/trips'),box=$('tripList');box.innerHTML=d.map(t=>`<details class="row"><summary>${t.name}${t.short?' · KRÁTKÁ':''} · ${t.km.toFixed(1)} km</summary><div class="kv"><div><span class="lab">Začátek</span><br><b>${t.start}</b></div><div><span class="lab">Konec</span><br><b>${t.end}</b></div><div><span class="lab">Spotřeba</span><br><b>${t.consumption==null?'--':t.consumption.toFixed(1)+' l/100'}</b></div><div><span class="lab">Palivo</span><br><b>${t.fuel==null?'--':t.fuel.toFixed(2)+' l'}</b></div><div><span class="lab">Průměr / max</span><br><b>${t.avgSpeed.toFixed(0)} / ${t.maxSpeed.toFixed(0)} km/h</b></div><div><span class="lab">Doba</span><br><b>${t.duration}</b></div></div></details>`).join('')||'<div class="muted">Žádné uložené jízdy.</div>';drawLineChart($('tripChart'),d.filter(x=>!x.short&&x.consumption!=null).slice().reverse().map(x=>x.consumption),'Spotřeba l/100 km')}catch(e){$('tripList').textContent='Načtení selhalo.'}}
async function loadDpf(){try{const d=await json('/api/dpf');const supported=!!d.supported;$('dpfSupportNote').innerHTML=supported?'<b>Rozšířené DPF:</b> ověřený profil · '+(d.profile||''):'<b>Rozšířené DPF není pro tuto ECU ověřeno.</b> Základní OBD data zůstávají aktivní; firmware nebude hádat saze ani regeneraci. Profil: '+(d.profile||'Generic OBD');$('dpfPct').textContent=supported?fmt(d.pct,0,' %'):'N/A';let p=supported?Math.max(0,Math.min(100,d.pct||0)):0;$('dpfBar').style.width=p+'%';$('dpfBar').style.background=p>=90?'#ff9f43':p>=70?'#ffd05a':'#48d78d';$('dpfLeft').textContent=supported?(d.regen?'AKTIVNÍ':fmt(d.kmLeft,0,' km')):'N/A';$('dpfSince').textContent=supported?fmt(d.kmSince,0,' km'):'N/A';$('dpfDiff').textContent=fmt(d.diff,1,' hPa');$('dpfCalc').textContent=supported?fmt(d.calc,1,' g'):'N/A';$('dpfMeasured').textContent=supported?fmt(d.measured,1,' g'):'N/A';$('dpfAvg').textContent=supported?fmt(d.historyAvg,0,' km'):'N/A';$('dpfRegen').textContent=supported?(d.regen?'AKTIVNÍ':'NE'):'N/A';$('dpfRegen').className='val '+(supported&&d.regen?'orange':supported?'green':'');$('dpfHistory').innerHTML=supported?(d.history.map(h=>`<div class="row"><b>${h.time}</b><div class="small">${h.soot} · ${h.duration} · ${h.km.toFixed(1)} km</div></div>`).join('')||'<div class="muted">Zatím žádná dokončená regenerace.</div>'):'<div class="muted">Historie regenerací je dostupná jen pro ověřený DPF profil.</div>';drawLineChart($('dpfChart'),supported?d.history.map(h=>h.km):[],'Interval km')}catch(e){$('dpfSupportNote').textContent='Načtení DPF stavu selhalo.';$('dpfHistory').textContent='Načtení DPF selhalo.'}}
async function loadFuel(){try{const d=await json('/api/fuelings');$('fuelCount').textContent=d.length;let total=d.reduce((a,f)=>a+f.added,0);$('fuelTotal').textContent=total.toFixed(1)+' l';$('fuelLast').textContent=d.length?d[0].added.toFixed(1)+' l':'--';$('fuelList').innerHTML=d.map(f=>`<div class="row"><b>${f.time} · +${f.added.toFixed(1)} l</b><div class="small">${f.before.toFixed(1)} → ${f.after.toFixed(1)} l${f.odometer==null?'':' · '+Math.round(f.odometer)+' km'}</div></div>`).join('')||'<div class="muted">Žádné uložené tankování.</div>'}catch(e){$('fuelList').textContent='Načtení tankování selhalo.'}}
async function loadSettings(){try{const d=await json('/api-lite');$('fw').textContent='FW '+d.fw;$('cfgDay').value=d.dayBrightness;$('cfgNight').value=d.nightBrightness;$('cfgAutoDay').checked=!!d.autoDayNight;$('cfgTouch').checked=!!d.touchFeedback;$('cfgTouchStr').value=d.touchStrength||24;$('cfgObd').value=String(d.obdMode);$('cfgAutoElm').checked=!!d.autoElm;$('cfgDpfLimit').value=d.dpfLimit;$('cfgRegenKm').value=d.regenSoonKm;$('cfgAlarmRegen').checked=!!d.alarmRegen;$('cfgAlarmSoon').checked=!!d.alarmRegenSoon;$('cfgAutoDim').checked=!!d.powerAutoDim;$('cfgDim').value=d.dimMin;$('cfgOff').value=d.offMin;$('cfgLow').value=d.lowMin;$('cfgEngineProfile').value=String(d.engineProfile||0);$('cfgEngineActive').value=d.engineActive||'--';$('cfgEngineEcu').value=(d.ecuPart||d.odxId||d.engineNote||'--');$('cfgTank').value=d.tankCapacity;$('cfgRange').value=d.rangeL100;$('cfgFuelRise').value=d.fuelRise;$('cfgOdo').value=d.odometerKm==null?'':Math.round(d.odometerKm);$('cfgSplash').value=d.splashText||'';$('cfgHeader').value=d.headerText||'';$('cfgOwner').value=d.devName||'';$('cfgEmail').value=d.devEmail||'';$('cfgPhone').value=d.devPhone||'';$('cfgWeb').value=d.devWeb||''}catch(e){$('saveState').textContent='Nastavení se nepodařilo načíst.'}}
async function loadSystem(){try{const d=await json('/api/system'),b=$('systemLines');b.innerHTML=Object.entries({'Firmware':'V'+d.fw,'Profil motoru':d.engine||'--','ECU':d.ecu||'--','Uptime':d.uptime,'Free heap':Math.round(d.heap/1024)+' kB','Min. heap':Math.round(d.minHeap/1024)+' kB','LittleFS':Math.round(d.fsUsed/1024)+' / '+Math.round(d.fsTotal/1024)+' kB','AP klienti':d.clients,'Power režim':d.power,'Reset':d.reset,'Čas':d.clock}).map(([k,v])=>`<div class="statusline"><span class="muted">${k}</span><b>${v}</b></div>`).join('')}catch(e){$('systemLines').textContent='Načtení systému selhalo.'}}
async function showPage(name){document.querySelectorAll('.page').forEach(x=>x.classList.toggle('on',x.id==='page-'+name));document.querySelectorAll('.nav button').forEach(x=>x.classList.toggle('on',x.dataset.page===name));if(!loaded[name]){loaded[name]=true;({overview:loadOverview,trips:loadTrips,dpf:loadDpf,fuel:loadFuel,settings:loadSettings,system:loadSystem}[name]||(()=>{}))()}}
document.querySelectorAll('.nav button').forEach(b=>b.addEventListener('click',()=>showPage(b.dataset.page)));document.querySelectorAll('.tabs button').forEach(b=>b.addEventListener('click',()=>{document.querySelectorAll('.tabs button').forEach(x=>x.classList.toggle('on',x===b));document.querySelectorAll('.setgroup').forEach(x=>x.classList.toggle('on',x.dataset.group===b.dataset.set))}));
$('configForm').addEventListener('submit',async e=>{e.preventDefault();const q=new URLSearchParams({dayBrightness:$('cfgDay').value,nightBrightness:$('cfgNight').value,autoDayNight:$('cfgAutoDay').checked?'1':'0',touchFeedback:$('cfgTouch').checked?'1':'0',touchStrength:$('cfgTouchStr').value,obdMode:$('cfgObd').value,autoElm:$('cfgAutoElm').checked?'1':'0',dpfLimit:$('cfgDpfLimit').value,regenSoonKm:$('cfgRegenKm').value,alarmRegen:$('cfgAlarmRegen').checked?'1':'0',alarmRegenSoon:$('cfgAlarmSoon').checked?'1':'0',powerAutoDim:$('cfgAutoDim').checked?'1':'0',dimMin:$('cfgDim').value,offMin:$('cfgOff').value,lowMin:$('cfgLow').value,engineProfile:$('cfgEngineProfile').value,tankCap:$('cfgTank').value,rangeL100:$('cfgRange').value,fuelRise:$('cfgFuelRise').value,odoKm:$('cfgOdo').value,splash:$('cfgSplash').value,header:$('cfgHeader').value,devName:$('cfgOwner').value,devEmail:$('cfgEmail').value,devPhone:$('cfgPhone').value,devWeb:$('cfgWeb').value});$('saveState').textContent='Ukládám…';try{await json('/api/config-save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:q.toString()});$('saveState').textContent='Uloženo.'}catch(err){$('saveState').textContent='Chyba ukládání.'}});
$('importConfig').addEventListener('click',async()=>{let v=$('importJson').value.trim();if(!v)return;try{const q=new URLSearchParams({json:v});await fetch('/config-import',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:q.toString()});loaded.settings=false;await loadSettings();$('saveState').textContent='Konfigurace importována.'}catch(e){$('saveState').textContent='Import selhal.'}});$('printTrips').addEventListener('click',()=>window.print());$('refreshSystem').addEventListener('click',loadSystem);$('syncTime').addEventListener('click',async()=>{try{await fetch('/time-sync',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'epoch='+Math.floor(Date.now()/1000)});await loadSystem()}catch(e){}});$('wifiOff').addEventListener('click',async()=>{try{await fetch('/wifi-off',{method:'POST'});document.body.innerHTML='<div style="font:16px system-ui;background:#071015;color:white;padding:28px"><h2>Servisní Wi-Fi se vypíná</h2><p>ESP32 obnoví Bluetooth část systému.</p></div>'}catch(e){}});showPage('overview');
</script></body></html>
)HTML";

void setupWebServer() {
  if (webRoutesConfigured) return;
  webRoutesConfigured = true;

  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    const size_t total = sizeof(WEB_PAGE) - 1;
    server.setContentLength(total);
    server.send(200, "text/html; charset=utf-8", "");
    char webChunk[512];
    size_t offset = 0;
    while (offset < total) {
      size_t n = min(sizeof(webChunk), total - offset);
      memcpy_P(webChunk, WEB_PAGE + offset, n);
      server.sendContent(webChunk, n);
      offset += n;
      delay(0);
    }
    server.setContentLength(CONTENT_LENGTH_NOT_SET);
  });

  server.on("/mini", HTTP_GET, []() {
    server.send(200, "text/html; charset=utf-8", "<!doctype html><meta charset='utf-8'><body><h2>Octavia II PC</h2><p>WEB LITE OK</p></body>");
  });

  server.on("/health", HTTP_GET, []() {
    String out;
    out.reserve(220);
    out += "OK\nFW: "; out += FW_VERSION;
    out += "\nMode: SERVICE_WIFI_EXCLUSIVE";
    out += "\nAP IP: "; out += WiFi.softAPIP().toString();
    out += "\nClients: "; out += String(WiFi.softAPgetStationNum());
    out += "\nFree heap: "; out += String(ESP.getFreeHeap()); out += " bytes";
    out += "\nMin heap: "; out += String(ESP.getMinFreeHeap()); out += " bytes";
    out += "\nBT stack during web: OFF";
    out += "\nVehicle clock: "; out += (vehicleClockAvailable ? vehicleClockText() : String("--"));
    out += "\nReset reason: "; out += resetReasonName(esp_reset_reason());
    out += "\nEngine profile: "; out += engineProfileLabel(activeEngineProfile);
    out += "\nECU: "; out += (car.ecuPartNumber.length() ? car.ecuPartNumber : engineDetectionNote);
    out += "\n";
    server.send(200, "text/plain; charset=utf-8", out);
  });

  server.on("/api/status", HTTP_GET, []() {
    TripDailySummary day = getTodayTripSummary();
    float dayCons = (day.fuelAvailable && day.distanceKm > 0.05f) ? day.fuelLiters * 100.0f / day.distanceKm : NAN;
    String j; j.reserve(850); j = "{";
    j += "\"fw\":\"" + String(FW_VERSION) + "\",";
    j += "\"vehicle\":{";
    j += "\"valid\":" + String(webVehicleSnapshot.valid ? "true" : "false") + ",";
    j += "\"profile\":\"" + jsonEscape(String(engineProfileLabel(activeEngineProfile))) + "\",";
    j += "\"dpfSupported\":" + String(vehicleCaps.advancedDpf ? "true" : "false") + ",";
    j += "\"captured\":\"" + jsonEscape(webVehicleSnapshot.epoch > 0 ? formatDateTime(webVehicleSnapshot.epoch) : String("--")) + "\",";
    j += "\"rpm\":" + jsonNum(webVehicleSnapshot.rpm,0) + ",";
    j += "\"coolant\":" + jsonNum(webVehicleSnapshot.coolantC,0) + ",";
    j += "\"boost\":" + jsonNum(webVehicleSnapshot.boostBar,2) + ",";
    j += "\"voltage\":" + jsonNum(webVehicleSnapshot.moduleVoltageV,2) + ",";
    j += "\"ambient\":" + jsonNum(webVehicleSnapshot.ambientTempC,0) + ",";
    j += "\"dpfPct\":" + jsonNum(webVehicleSnapshot.dpfPct,0) + ",";
    j += "\"dpfLeft\":" + jsonNum(webVehicleSnapshot.dpfKmLeft,0) + ",";
    j += "\"dpfSince\":" + jsonNum(webVehicleSnapshot.dpfKmSince,0) + ",";
    j += "\"regen\":" + String(webVehicleSnapshot.regenActive ? "true" : "false") + "},";
    j += "\"today\":{";
    j += "\"count\":" + String(day.count) + ",";
    j += "\"km\":" + String(day.distanceKm,2) + ",";
    j += "\"duration\":\"" + jsonEscape(formatDuration(day.durationSec)) + "\",";
    j += "\"consumption\":" + jsonNum(dayCons,2) + "}}";
    server.send(200, "application/json; charset=utf-8", j);
  });

  server.on("/api/dpf", HTTP_GET, []() {
    String hist[8]; uint8_t count = 0;
    fs::File f = LittleFS.open("/dpf.csv", "r");
    if (f) {
      while (f.available()) {
        String line = f.readStringUntil('\n'); line.trim(); if (!line.length()) continue;
        if (count < 8) hist[count++] = line;
        else { for (uint8_t i=0;i<7;i++) hist[i]=hist[i+1]; hist[7]=line; }
      }
      f.close();
    }
    String j; j.reserve(900 + count * 130); j = "{";
    j += "\"supported\":" + String(vehicleCaps.advancedDpf ? "true" : "false") + ",";
    j += "\"profile\":\"" + jsonEscape(String(engineProfileLabel(activeEngineProfile))) + "\",";
    j += "\"pct\":" + jsonNum(webVehicleSnapshot.dpfPct,0) + ",";
    j += "\"calc\":" + jsonNum(webVehicleSnapshot.dpfCalcG,1) + ",";
    j += "\"measured\":" + jsonNum(webVehicleSnapshot.dpfMeasuredG,1) + ",";
    j += "\"diff\":" + jsonNum(webVehicleSnapshot.dpfDiffHpa,1) + ",";
    j += "\"kmSince\":" + jsonNum(webVehicleSnapshot.dpfKmSince,0) + ",";
    j += "\"kmLeft\":" + jsonNum(webVehicleSnapshot.dpfKmLeft,0) + ",";
    j += "\"historyAvg\":" + jsonNum(webVehicleSnapshot.dpfHistoryAvgKm,0) + ",";
    j += "\"regen\":" + String(webVehicleSnapshot.regenActive ? "true" : "false") + ",\"history\":[";
    for (uint8_t i=0;i<count;i++) {
      if (i) j += ",";
      String line = hist[i];
      int c1=line.indexOf(','), c2=c1>=0?line.indexOf(',',c1+1):-1, c3=c2>=0?line.indexOf(',',c2+1):-1;
      String tm=c1>=0?line.substring(0,c1):line;
      String soot=(c1>=0&&c2>c1)?line.substring(c1+1,c2):String("--");
      String dur=(c2>=0&&c3>c2)?line.substring(c2+1,c3):String("--");
      String kms=c3>=0?line.substring(c3+1):String("0"); kms.replace("km",""); kms.trim();
      j += "{\"time\":\""+jsonEscape(tm)+"\",\"soot\":\""+jsonEscape(soot)+"\",\"duration\":\""+jsonEscape(dur)+"\",\"km\":"+String(kms.toFloat(),1)+"}";
    }
    j += "]}";
    server.send(200, "application/json; charset=utf-8", j);
  });

  server.on("/api/system", HTTP_GET, []() {
    String j; j.reserve(520); j = "{";
    j += "\"fw\":\"" + String(FW_VERSION) + "\",";
    j += "\"uptime\":\"" + jsonEscape(formatDuration(millis()/1000UL)) + "\",";
    j += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
    j += "\"minHeap\":" + String(ESP.getMinFreeHeap()) + ",";
    j += "\"fsUsed\":" + String(LittleFS.usedBytes()) + ",";
    j += "\"fsTotal\":" + String(LittleFS.totalBytes()) + ",";
    j += "\"clients\":" + String(WiFi.softAPgetStationNum()) + ",";
    j += "\"power\":\"" + String(powerModeLabel()) + "\",";
    j += "\"reset\":\"" + String(resetReasonName(esp_reset_reason())) + "\",";
    j += "\"engine\":\"" + jsonEscape(String(engineProfileLabel(activeEngineProfile))) + "\",";
    j += "\"ecu\":\"" + jsonEscape(car.ecuPartNumber.length()?car.ecuPartNumber:engineDetectionNote) + "\",";
    j += "\"clock\":\"" + jsonEscape(clockIsValid()?formatDateTime(time(nullptr)):String("--")) + "\"}";
    server.send(200, "application/json; charset=utf-8", j);
  });

  server.on("/api/config-save", HTTP_POST, []() {
    auto b = [&](const char *key, bool current)->bool { return server.hasArg(key) ? server.arg(key)=="1" : current; };
    if (server.hasArg("dayBrightness")) displayBrightness=(uint8_t)constrain(server.arg("dayBrightness").toInt(),40,255);
    if (server.hasArg("nightBrightness")) displayNightBrightness=(uint8_t)constrain(server.arg("nightBrightness").toInt(),10,180);
    autoDayNightBrightness=b("autoDayNight",autoDayNightBrightness);
    touchFeedbackEnabled=b("touchFeedback",touchFeedbackEnabled);
    if (server.hasArg("touchStrength")) touchFeedbackStrength=(uint8_t)constrain(server.arg("touchStrength").toInt(),8,60);
    if (server.hasArg("obdMode")) obdDataMode=(uint8_t)constrain(server.arg("obdMode").toInt(),0,2);
    fastDataMode = obdDataMode == 2;
    if (server.hasArg("engineProfile")) {
      uint8_t ep=(uint8_t)constrain(server.arg("engineProfile").toInt(),0,4);
      configuredEngineProfile=(EngineProfile)ep; prefs.putUChar("engineProf",ep);
    }
    autoElmReconnect=b("autoElm",autoElmReconnect);
    if (server.hasArg("dpfLimit")) dpfRegenLimitG=constrain(server.arg("dpfLimit").toFloat(),15.0f,45.0f);
    if (server.hasArg("regenSoonKm")) alarmRegenSoonKm=constrain(server.arg("regenSoonKm").toFloat(),10.0f,50.0f);
    alarmRegenEnabled=b("alarmRegen",alarmRegenEnabled);
    alarmRegenSoonEnabled=b("alarmRegenSoon",alarmRegenSoonEnabled);
    powerAutoDim=b("powerAutoDim",powerAutoDim);
    if (server.hasArg("dimMin")) powerDimDelayMs=(unsigned long)constrain(server.arg("dimMin").toInt(),1,10)*60000UL;
    if (server.hasArg("offMin")) powerDisplayOffDelayMs=(unsigned long)constrain(server.arg("offMin").toInt(),2,30)*60000UL;
    if (server.hasArg("lowMin")) powerLowDelayMs=(unsigned long)constrain(server.arg("lowMin").toInt(),1,10)*60000UL;
    if (server.hasArg("tankCap")) tankCapacityL=constrain(server.arg("tankCap").toFloat(),35.0f,80.0f);
    if (server.hasArg("rangeL100")) rangeFallbackL100=constrain(server.arg("rangeL100").toFloat(),2.0f,20.0f);
    if (server.hasArg("fuelRise")) { fuelingMinRiseL=constrain(server.arg("fuelRise").toFloat(),2.0f,12.0f); prefs.putFloat("fuelMinRise",fuelingMinRiseL); }
    if (server.hasArg("odoKm") && server.arg("odoKm").length()) { float v=server.arg("odoKm").toFloat(); if(v>=0&&v<=2000000){trackedOdometerKm=v;trackedOdometerConfigured=true;prefs.putBool("odoSet",true);prefs.putFloat("odoKm",v);lastOdometerPersistKm=v;} }
    if (server.hasArg("splash")) { splashTitle=sanitizeUiText(server.arg("splash"),DEFAULT_SPLASH_TITLE,30); prefs.putString("splashTxt",splashTitle); }
    if (server.hasArg("header")) { mainHeaderTitle=sanitizeUiText(server.arg("header"),DEFAULT_HEADER_TITLE,22); prefs.putString("headerTxt",mainHeaderTitle); }
    if (server.hasArg("devName")) developerName=server.arg("devName").substring(0,28);
    if (server.hasArg("devEmail")) developerEmail=server.arg("devEmail").substring(0,48);
    if (server.hasArg("devPhone")) developerPhone=server.arg("devPhone").substring(0,28);
    if (server.hasArg("devWeb")) developerWeb=server.arg("devWeb").substring(0,64);
    prefs.putString("devName",developerName); prefs.putString("devEmail",developerEmail); prefs.putString("devPhone",developerPhone); prefs.putString("devWeb",developerWeb);
    markSettingsDirty(); flushDeferredSettings(true); applyBrightness(activeUiBrightness());
    server.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
  });

  server.on("/api-lite", HTTP_GET, []() {
    String json;
    json.reserve(1150);
    json = "{";
    json += "\"fw\":\"" + jsonEscape(FW_VERSION) + "\",";
    json += "\"oilKm\":" + String(service.oilKm, 1) + ",";
    json += "\"oilLimit\":" + String(service.oilIntervalKm, 0) + ",";
    json += "\"inspKm\":" + String(service.inspectionKm, 1) + ",";
    json += "\"inspLimit\":" + String(service.inspectionIntervalKm, 0) + ",";
    json += "\"clock\":\"" + jsonEscape(clockIsValid() ? formatDateTime(time(nullptr)) : (vehicleClockAvailable ? (vehicleClockText() + " OBD") : String("--"))) + "\",";
    json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"dpfLimit\":" + String(dpfRegenLimitG, 1) + ",";
    json += "\"tankCapacity\":" + String(tankCapacityL, 1) + ",";
    json += "\"fuelRise\":" + String(fuelingMinRiseL, 1) + ",";
    json += "\"rangeL100\":" + String(rangeFallbackL100, 1) + ",";
    json += "\"manualFuelPct\":" + (manualFuelConfigured && !isnan(manualFuelPct) ? String(manualFuelPct, 1) : String("null")) + ",";
    json += "\"odometerKm\":" + (trackedOdometerConfigured && !isnan(trackedOdometerKm) ? String(trackedOdometerKm, 1) : String("null")) + ",";
    json += "\"vehicleClock\":\"" + jsonEscape(vehicleClockAvailable ? vehicleClockText() : String("--")) + "\",";
    json += "\"splashText\":\"" + jsonEscape(splashTitle) + "\",";
    json += "\"headerText\":\"" + jsonEscape(mainHeaderTitle) + "\",";
    json += "\"dayBrightness\":" + String(displayBrightness) + ",";
    json += "\"nightBrightness\":" + String(displayNightBrightness) + ",";
    json += "\"autoDayNight\":" + String(autoDayNightBrightness ? "true" : "false") + ",";
    json += "\"touchFeedback\":" + String(touchFeedbackEnabled ? "true" : "false") + ",";
    json += "\"touchStrength\":" + String(touchFeedbackStrength) + ",";
    json += "\"obdMode\":" + String(obdDataMode) + ",";
    json += "\"autoElm\":" + String(autoElmReconnect ? "true" : "false") + ",";
    json += "\"engineProfile\":" + String((uint8_t)configuredEngineProfile) + ",";
    json += "\"engineActive\":\"" + jsonEscape(String(engineProfileLabel(activeEngineProfile))) + "\",";
    json += "\"engineNote\":\"" + jsonEscape(engineDetectionNote) + "\",";
    json += "\"ecuPart\":\"" + jsonEscape(car.ecuPartNumber) + "\",";
    json += "\"odxId\":\"" + jsonEscape(car.odxId) + "\",";
    json += "\"regenSoonKm\":" + String(alarmRegenSoonKm, 0) + ",";
    json += "\"alarmRegen\":" + String(alarmRegenEnabled ? "true" : "false") + ",";
    json += "\"alarmRegenSoon\":" + String(alarmRegenSoonEnabled ? "true" : "false") + ",";
    json += "\"powerAutoDim\":" + String(powerAutoDim ? "true" : "false") + ",";
    json += "\"dimMin\":" + String(powerDimDelayMs / 60000UL) + ",";
    json += "\"offMin\":" + String(powerDisplayOffDelayMs / 60000UL) + ",";
    json += "\"lowMin\":" + String(powerLowDelayMs / 60000UL) + ",";
    json += "\"devName\":\"" + jsonEscape(developerName) + "\",";
    json += "\"devEmail\":\"" + jsonEscape(developerEmail) + "\",";
    json += "\"devPhone\":\"" + jsonEscape(developerPhone) + "\",";
    String apiDate = "", apiTime = "";
    if (clockIsValid()) { time_t now=time(nullptr); struct tm ti; localtime_r(&now,&ti); char db[11],tb[6]; strftime(db,sizeof(db),"%Y-%m-%d",&ti); strftime(tb,sizeof(tb),"%H:%M",&ti); apiDate=db; apiTime=tb; }
    json += "\"date\":\"" + apiDate + "\",";
    json += "\"time\":\"" + apiTime + "\",";
    json += "\"devWeb\":\"" + jsonEscape(developerWeb) + "\"}";
    server.send(200, "application/json; charset=utf-8", json);
  });

  server.on("/api/trips", HTTP_GET, []() {
    // Jen poslednich 12 zaznamu. Plny export zustava v PDF/CSV.
    TripRecord rec[12];
    int n = loadRecentTrips(rec, 12);
    String json;
    json.reserve(256 + n * 170);
    json = "[";
    for (int i = n - 1; i >= 0; i--) {
      if (i != n - 1) json += ",";
      const TripRecord &r = rec[i];
      json += "{";
      json += "\"id\":" + String(r.id) + ",";
      json += "\"name\":\"" + jsonEscape(tripAutoName(r.startEpoch, r.id)) + "\",";
      json += "\"short\":" + String(tripRecordIsShort(r) ? "true" : "false") + ",";
      json += "\"start\":\"" + jsonEscape(formatDateTime(r.startEpoch)) + "\",";
      json += "\"end\":\"" + jsonEscape(formatDateTime(r.endEpoch)) + "\",";
      json += "\"km\":" + String(r.distanceKm, 3) + ",";
      json += "\"fuel\":" + jsonNum(r.fuelLiters, 3) + ",";
      json += "\"consumption\":" + jsonNum(r.avgConsumption, 2) + ",";
      json += "\"maxSpeed\":" + String(r.maxSpeedKmh, 1) + ",";
      json += "\"avgSpeed\":" + String(r.avgSpeedKmh, 1) + ",";
      json += "\"duration\":\"" + jsonEscape(formatDuration(r.durationSec)) + "\"}";
    }
    json += "]";
    server.send(200, "application/json; charset=utf-8", json);
  });

  server.on("/api/fuelings", HTTP_GET, []() {
    FuelingRecord rec[20];
    int n = loadRecentFuelings(rec, 20);
    String json;
    json.reserve(128 + n * 120);
    json = "[";
    for (int i = n - 1; i >= 0; i--) {
      if (i != n - 1) json += ",";
      const FuelingRecord &r = rec[i];
      json += "{";
      json += "\"id\":" + String(r.id) + ",";
      json += "\"time\":\"" + jsonEscape(formatDateTime(r.epoch)) + "\",";
      json += "\"odometer\":" + (isnan(r.odometerKm) ? String("null") : String(r.odometerKm, 1)) + ",";
      json += "\"before\":" + String(r.beforeLiters, 2) + ",";
      json += "\"after\":" + String(r.afterLiters, 2) + ",";
      json += "\"added\":" + String(r.addedLiters, 2) + "}";
    }
    json += "]";
    server.send(200, "application/json; charset=utf-8", json);
  });

  server.on("/fueling.csv", HTTP_GET, []() {
    fs::File f = LittleFS.open("/fueling.csv", "r");
    if (!f) { server.send(404, "text/plain", "Zadne zaznamy"); return; }
    server.sendHeader("Content-Disposition", "attachment; filename=historie_tankovani.csv");
    server.streamFile(f, "text/csv"); f.close();
  });

  server.on("/trips_export.csv", HTTP_GET, []() {
    if (!buildTripsExportCsv()) { server.send(404, "text/plain", "Zadne zaznamy"); return; }
    fs::File f = LittleFS.open("/trips_export.csv", "r");
    if (!f) { server.send(500, "text/plain", "CSV export selhal"); return; }
    server.sendHeader("Content-Disposition", "attachment; filename=kniha_jizd_export.csv");
    server.streamFile(f, "text/csv; charset=utf-8"); f.close();
  });

  server.on("/trips_v2.csv", HTTP_GET, []() {
    fs::File f = LittleFS.open("/trips_v2.csv", "r");
    if (!f) { server.send(404, "text/plain", "Zadne zaznamy"); return; }
    server.sendHeader("Content-Disposition", "attachment; filename=kniha_jizd.csv");
    server.streamFile(f, "text/csv"); f.close();
  });
  server.on("/trips.pdf", HTTP_GET, []() { sendTripsPdf(); });
  server.on("/trip.pdf", HTTP_GET, []() {
    if (!server.hasArg("id")) { server.send(400, "text/plain", "Chybi id jizdy"); return; }
    sendTripPdf(server.arg("id").toInt());
  });

  server.on("/time-sync", HTTP_POST, []() {
    if (!server.hasArg("epoch")) { server.send(400, "text/plain", "Chybi epoch"); return; }
    time_t epoch = (time_t)strtoull(server.arg("epoch").c_str(), nullptr, 10);
    if (epoch < 1700000000) { server.send(400, "text/plain", "Neplatny cas"); return; }
    syncClockFromEpoch(epoch); server.send(200, "text/plain", "OK");
  });

  server.on("/clock-save", HTTP_POST, []() {
    if (!server.hasArg("date") || !server.hasArg("time")) { server.send(400,"text/plain","Chybi datum nebo cas"); return; }
    int yy=0,mm=0,dd=0,hh=0,mi=0;
    if (sscanf(server.arg("date").c_str(), "%d-%d-%d", &yy,&mm,&dd) != 3 ||
        sscanf(server.arg("time").c_str(), "%d:%d", &hh,&mi) != 2 ||
        yy < 2023 || yy > 2099 || mm < 1 || mm > 12 || dd < 1 || dd > 31 || hh < 0 || hh > 23 || mi < 0 || mi > 59) {
      server.send(400,"text/plain","Neplatne datum nebo cas"); return;
    }
    struct tm ti = {}; ti.tm_year=yy-1900; ti.tm_mon=mm-1; ti.tm_mday=dd; ti.tm_hour=hh; ti.tm_min=mi; ti.tm_sec=0; ti.tm_isdst=-1;
    time_t epoch=mktime(&ti);
    if (epoch < 1700000000) { server.send(400,"text/plain","Cas nelze nastavit"); return; }
    syncClockFromEpoch(epoch);
    server.sendHeader("Location","/",true); server.send(303,"text/plain","Cas nastaven");
  });

  server.on("/monitor-save", HTTP_POST, []() {
    bool changed = false;
    if (server.hasArg("dpfLimit")) { float v=server.arg("dpfLimit").toFloat(); if(v>=15&&v<=45){dpfRegenLimitG=v;prefs.putFloat("dpfLimitG",v);changed=true;} }
    if (server.hasArg("tankCap")) { float v=server.arg("tankCap").toFloat(); if(v>=35&&v<=80){tankCapacityL=v;prefs.putFloat("tankCapL",v);changed=true;} }
    if (server.hasArg("rangeL100")) { float v=server.arg("rangeL100").toFloat(); if(v>=2&&v<=20){rangeFallbackL100=v;prefs.putFloat("rangeL100",v);changed=true;} }
    if (server.hasArg("fuelPct")) {
      String fv=server.arg("fuelPct");
      if (!fv.length()) { manualFuelConfigured=false; manualFuelPct=NAN; prefs.putBool("fuelManual",false); changed=true; }
      else { float v=fv.toFloat(); if(v>=0&&v<=100){manualFuelConfigured=true;manualFuelPct=v;prefs.putBool("fuelManual",true);prefs.putFloat("fuelManualPct",v);changed=true;} }
    }
    if (server.hasArg("fuelRise")) { float v=server.arg("fuelRise").toFloat(); if(v>=2&&v<=12){fuelingMinRiseL=v;prefs.putFloat("fuelMinRise",v);changed=true;} }
    if (server.hasArg("odoKm") && server.arg("odoKm").length()) {
      float v=server.arg("odoKm").toFloat();
      if(v>=0&&v<=2000000){trackedOdometerKm=v;trackedOdometerConfigured=true;prefs.putBool("odoSet",true);prefs.putFloat("odoKm",v);lastOdometerPersistKm=v;changed=true;}
    }
    if (!changed) { server.send(400,"text/plain","Neplatne hodnoty"); return; }
    server.sendHeader("Location","/",true); server.send(303,"text/plain","Ulozeno");
  });

  server.on("/settings-v2-save", HTTP_POST, []() {
    if (server.hasArg("dayBrightness")) displayBrightness = (uint8_t)constrain(server.arg("dayBrightness").toInt(), 40, 255);
    if (server.hasArg("nightBrightness")) displayNightBrightness = (uint8_t)constrain(server.arg("nightBrightness").toInt(), 10, 180);
    autoDayNightBrightness = server.hasArg("autoDayNight");
    touchFeedbackEnabled = server.hasArg("touchFeedback");
    autoElmReconnect = server.hasArg("autoElm");
    if (server.hasArg("obdMode")) obdDataMode = (uint8_t)constrain(server.arg("obdMode").toInt(), 0, 2);
    if (server.hasArg("engineProfile")) configuredEngineProfile=(EngineProfile)constrain(server.arg("engineProfile").toInt(),0,4);
    fastDataMode = obdDataMode == 2;
    if (server.hasArg("regenSoonKm")) alarmRegenSoonKm = constrain(server.arg("regenSoonKm").toFloat(), 10.0f, 50.0f);
    if (server.hasArg("dimMin")) powerDimDelayMs = (unsigned long)constrain(server.arg("dimMin").toInt(), 1, 10) * 60000UL;
    if (server.hasArg("offMin")) powerDisplayOffDelayMs = (unsigned long)constrain(server.arg("offMin").toInt(), 2, 30) * 60000UL;
    if (server.hasArg("lowMin")) powerLowDelayMs = (unsigned long)constrain(server.arg("lowMin").toInt(), 1, 10) * 60000UL;
    markSettingsDirty(); flushDeferredSettings(true); applyBrightness(activeUiBrightness());
    server.sendHeader("Location","/",true); server.send(303,"text/plain","Ulozeno");
  });

  server.on("/config.json", HTTP_GET, []() {
    String j; j.reserve(900); j = "{";
    j += "\"fw\":\"" + String(FW_VERSION) + "\",";
    j += "\"dayBrightness\":" + String(displayBrightness) + ",";
    j += "\"nightBrightness\":" + String(displayNightBrightness) + ",";
    j += "\"autoDayNight\":" + String(autoDayNightBrightness ? "true" : "false") + ",";
    j += "\"touchFeedback\":" + String(touchFeedbackEnabled ? "true" : "false") + ",";
    j += "\"touchStrength\":" + String(touchFeedbackStrength) + ",";
    j += "\"autoElm\":" + String(autoElmReconnect ? "true" : "false") + ",";
    j += "\"obdMode\":" + String(obdDataMode) + ",";
    j += "\"engineProfile\":" + String((uint8_t)configuredEngineProfile) + ",";
    j += "\"dpfLimit\":" + String(dpfRegenLimitG,1) + ",";
    j += "\"regenSoonKm\":" + String(alarmRegenSoonKm,0) + ",";
    j += "\"alarmRegen\":" + String(alarmRegenEnabled ? "true" : "false") + ",";
    j += "\"alarmRegenSoon\":" + String(alarmRegenSoonEnabled ? "true" : "false") + ",";
    j += "\"powerAutoDim\":" + String(powerAutoDim ? "true" : "false") + ",";
    j += "\"dimMin\":" + String(powerDimDelayMs/60000UL) + ",";
    j += "\"offMin\":" + String(powerDisplayOffDelayMs/60000UL) + ",";
    j += "\"lowMin\":" + String(powerLowDelayMs/60000UL) + ",";
    j += "\"tankCapacity\":" + String(tankCapacityL,1) + ",";
    j += "\"rangeL100\":" + String(rangeFallbackL100,1) + ",";
    j += "\"splash\":\"" + jsonEscape(splashTitle) + "\",";
    j += "\"header\":\"" + jsonEscape(mainHeaderTitle) + "\"}";
    server.sendHeader("Content-Disposition", "attachment; filename=octavia_ii_pc_config.json");
    server.send(200, "application/json; charset=utf-8", j);
  });

  server.on("/config-import", HTTP_POST, []() {
    if (!server.hasArg("json")) { server.send(400,"text/plain","Chybi JSON"); return; }
    String j = server.arg("json");
    if (j.length() < 10 || j.length() > 5000) { server.send(400,"text/plain","Neplatny JSON"); return; }

    auto readNumber = [&](const char *key, float &out)->bool {
      String k = String("\"") + key + "\":"; int p=j.indexOf(k); if(p<0) return false; p += k.length();
      while(p<(int)j.length() && (j[p]==' '||j[p]=='\t')) p++;
      int e=p; while(e<(int)j.length() && ((j[e]>='0'&&j[e]<='9')||j[e]=='-'||j[e]=='+'||j[e]=='.')) e++;
      if(e<=p) return false; out=j.substring(p,e).toFloat(); return true;
    };
    auto readBool = [&](const char *key, bool &out)->bool {
      String k = String("\"") + key + "\":"; int p=j.indexOf(k); if(p<0) return false; p += k.length();
      while(p<(int)j.length() && j[p]==' ') p++; if(j.substring(p,p+4)=="true") {out=true;return true;} if(j.substring(p,p+5)=="false") {out=false;return true;} return false;
    };
    auto readString = [&](const char *key, String &out)->bool {
      String k = String("\"") + key + "\":\""; int p=j.indexOf(k); if(p<0) return false; p += k.length(); int e=j.indexOf('"',p); if(e<0) return false; out=j.substring(p,e); return true;
    };

    float n; bool b; String st;
    if(readNumber("dayBrightness",n)) displayBrightness=(uint8_t)constrain((int)n,40,255);
    if(readNumber("nightBrightness",n)) displayNightBrightness=(uint8_t)constrain((int)n,10,180);
    if(readBool("autoDayNight",b)) autoDayNightBrightness=b;
    if(readBool("touchFeedback",b)) touchFeedbackEnabled=b;
    if(readNumber("touchStrength",n)) touchFeedbackStrength=(uint8_t)constrain((int)n,8,60);
    if(readBool("autoElm",b)) autoElmReconnect=b;
    if(readNumber("obdMode",n)) obdDataMode=(uint8_t)constrain((int)n,0,2);
    fastDataMode = obdDataMode == 2;
    if(readNumber("engineProfile",n)) configuredEngineProfile=(EngineProfile)constrain((int)n,0,4);
    if(readNumber("dpfLimit",n)) dpfRegenLimitG=constrain(n,15.0f,45.0f);
    if(readNumber("regenSoonKm",n)) alarmRegenSoonKm=constrain(n,10.0f,50.0f);
    if(readBool("alarmRegen",b)) alarmRegenEnabled=b;
    if(readBool("alarmRegenSoon",b)) alarmRegenSoonEnabled=b;
    if(readBool("powerAutoDim",b)) powerAutoDim=b;
    if(readNumber("dimMin",n)) powerDimDelayMs=(unsigned long)constrain((int)n,1,10)*60000UL;
    if(readNumber("offMin",n)) powerDisplayOffDelayMs=(unsigned long)constrain((int)n,2,30)*60000UL;
    if(readNumber("lowMin",n)) powerLowDelayMs=(unsigned long)constrain((int)n,1,10)*60000UL;
    if(readNumber("tankCapacity",n)) tankCapacityL=constrain(n,35.0f,80.0f);
    if(readNumber("rangeL100",n)) rangeFallbackL100=constrain(n,2.0f,20.0f);
    if(readString("splash",st)) splashTitle=sanitizeUiText(st,DEFAULT_SPLASH_TITLE,30);
    if(readString("header",st)) mainHeaderTitle=sanitizeUiText(st,DEFAULT_HEADER_TITLE,22);
    markSettingsDirty(); flushDeferredSettings(true);
    prefs.putString("splashTxt",splashTitle); prefs.putString("headerTxt",mainHeaderTitle);
    applyBrightness(activeUiBrightness());
    server.sendHeader("Location","/",true); server.send(303,"text/plain","Konfigurace importovana");
  });

  server.on("/display-save", HTTP_POST, []() {
    if (server.hasArg("splash"))
      splashTitle = sanitizeUiText(server.arg("splash"), DEFAULT_SPLASH_TITLE, 30);
    if (server.hasArg("header"))
      mainHeaderTitle = sanitizeUiText(server.arg("header"), DEFAULT_HEADER_TITLE, 22);
    prefs.putString("splashTxt", splashTitle);
    prefs.putString("headerTxt", mainHeaderTitle);
    server.sendHeader("Location", "/", true);
    server.send(303, "text/plain", "Ulozeno");
  });

  server.on("/info-save", HTTP_POST, []() {
    if (server.hasArg("name")) developerName=server.arg("name").substring(0,28);
    if (server.hasArg("email")) developerEmail=server.arg("email").substring(0,48);
    if (server.hasArg("phone")) developerPhone=server.arg("phone").substring(0,28);
    if (server.hasArg("web")) developerWeb=server.arg("web").substring(0,64);
    prefs.putString("devName",developerName); prefs.putString("devEmail",developerEmail); prefs.putString("devPhone",developerPhone); prefs.putString("devWeb",developerWeb);
    server.sendHeader("Location","/",true); server.send(303,"text/plain","Ulozeno");
  });

  server.on("/wifi-off", HTTP_POST, []() {
    server.send(200, "text/plain", "OFF");
    // Skutecne vypnuti provede loop az odpoved odejde do telefonu.
    wifiConnectRequested = true;
    wifiConnectRequestedMs = millis();
  });

  server.onNotFound([](){ server.send(404,"text/plain; charset=utf-8","404 - nenalezeno"); });
}

void initWifi() {
  wifiSSID = prefs.getString("wifiSsid", "");
  wifiPassword = prefs.getString("wifiPass", "");
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_OFF);
  serviceWifiActive = false;
  webClientActive = false;
  apClientCount = 0;
  setupWebServer();
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  ntpConfigured = false;
  Serial.printf("WIFI service mode ready (OFF at boot), heap=%u\n", ESP.getFreeHeap());
}

void manualDisconnectElm() {
  captureWebVehicleSnapshot();
  // Rucni odpojeni zaroven zastavi automaticky reconnect, dokud uzivatel
  // nestiskne PRIPOJIT ELM. OBD task nejprve nechame dokoncit rozjety prikaz.
  offlineMode = true;
  connectUserChoseOffline = true;
  reconnectAbortRequested = true;
  reconnectRequested = false;
  reconnectFinished = false;
  diagnosticExclusive = true;

  bool locked = !btMutex || xSemaphoreTake(btMutex, pdMS_TO_TICKS(7500)) == pdTRUE;
  if (!locked) {
    diagnosticExclusive = false;
    showAlert("ELM327", "Odpojeni se nepodarilo", "Zkus to znovu za chvili", COL_RED, 3000);
    return;
  }

  SerialBT.disconnect();
  unsigned long waitStart = millis();
  while (SerialBT.connected(0) && millis() - waitStart < 1200UL) delay(40);
  if (btMutex) xSemaphoreGive(btMutex);

  car.elmConnected = false;
  car.ecuResponding = false;
  invalidateLiveTelemetry(true);
  warnedDisconnect = false;
  diagnosticExclusive = false;
  lastReconnectAttempt = millis();
}

void stopElmReconnectAttempts() {
  // STOP musi reagovat bez cekani na btMutex. Pokud je druhe jadro zrovna
  // uvnitr blokujiciho SerialBT.connect(), nastavime abort flag a UI se
  // okamzite prepne do ZASTAVUJI. Po navratu z connectu task session zahodi.
  captureWebVehicleSnapshot();
  offlineMode = true;
  connectUserChoseOffline = true;
  reconnectAbortRequested = true;
  reconnectRequested = false;
  reconnectFinished = false;
  warnedDisconnect = false;
  btRecoveryFailures = 0;
  lastReconnectAttempt = millis();
  car.elmConnected = false;
  car.ecuResponding = false;
  invalidateLiveTelemetry(true);

  // Pokud BT zrovna neni obsazene, odpojime ho hned. Nikdy tu necekame.
  if (!btMutex || xSemaphoreTake(btMutex, 0) == pdTRUE) {
    SerialBT.disconnect();
    if (btMutex) xSemaphoreGive(btMutex);
  }
}

void requestElmReconnect() {
  if (serviceWifiActive) return;
  reconnectAbortRequested = false;
  offlineMode = false;
  connectUserChoseOffline = false;
  warnedDisconnect = false;
  btRecoveryFailures = 0; // prvni neuspech spusti plny recovery BT stacku
  reconnectFinished = false;
  lastReconnectAttempt = 0;
  reconnectRequested = true;
}

bool startServiceWifi() {
  if (serviceWifiActive) return true;

  // Zamerne dvoukrokove: aktivni ELM nikdy neukoncujeme soucasne se startem Wi-Fi.
  // U levnych ELM / puvodniho ESP32 to muze shodit sdileny BT/Wi-Fi stack.
  if (car.elmConnected || reconnectInProgress || reconnectRequested || !offlineMode) {
    showAlert("SERVISNI WEB", "Nejdriv ODPOJIT ELM", "Pak znovu stiskni ZAPNOUT WEB", COL_YELLOW, 3500);
    return false;
  }

  Serial.printf("SERVICE WIFI start: heap before BT off=%u\n", ESP.getFreeHeap());
  reconnectRequested = false;
  reconnectFinished = false;
  diagnosticExclusive = true;

  // Protoze ELM je uz rucne odpojeny, OBD task je v offline rezimu. Presto
  // cekame na mutex, abychom SerialBT.end() nikdy neprovedli uprostred prikazu.
  bool locked = !btMutex || xSemaphoreTake(btMutex, pdMS_TO_TICKS(7500)) == pdTRUE;
  if (!locked) {
    diagnosticExclusive = false;
    showAlert("SERVISNI WEB", "Bluetooth je stale zaneprazdnen", "Zkus ZAPNOUT WEB znovu", COL_RED, 3500);
    return false;
  }

  SerialBT.disconnect();
  delay(120);
  SerialBT.end();
  if (btMutex) xSemaphoreGive(btMutex);
  delay(300);

  car.elmConnected = false;
  car.ecuResponding = false;
  invalidateLiveTelemetry(true);

  WiFi.mode(WIFI_OFF); delay(150);
  WiFi.mode(WIFI_AP); delay(150);
  IPAddress apIP(192,168,4,1), apGW(192,168,4,1), apMask(255,255,255,0);
  bool ipOk = WiFi.softAPConfig(apIP, apGW, apMask);
  bool apOk = WiFi.softAP(AP_SSID, AP_PASS, 6, false, 1);
  WiFi.setSleep(false);
  server.begin();
  serviceWifiActive = ipOk && apOk;
  webClientActive = false;
  apClientCount = 0;
  serviceWifiNoClientSince = millis();
  diagnosticExclusive = false;
  Serial.printf("SERVICE WIFI AP config=%s start=%s IP=%s heap=%u\n", ipOk?"OK":"FAIL", apOk?"OK":"FAIL", WiFi.softAPIP().toString().c_str(), ESP.getFreeHeap());
  return serviceWifiActive;
}

void stopServiceWifi(bool restartBt) {
  if (!serviceWifiActive && WiFi.getMode() == WIFI_OFF) return;
  Serial.printf("SERVICE WIFI stop: heap=%u\n", ESP.getFreeHeap());

  // Nejdriv ukoncime HTTP/AP a nechame radio stack skutecne dobehnout.
  // Bluetooth zde ZAMERNE nespoustime - drive se SerialBT.begin() volal
  // okamzite z touch handleru a na puvodnim ESP32 to mohlo zpusobit reset.
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);

  serviceWifiActive = false;
  webClientActive = false;
  apClientCount = 0;
  wifiStaConnected = false;
  serviceWifiNoClientSince = 0;
  diagnosticExclusive = false;

  if (restartBt) {
    // OBD zustane do obnoveni BT v OFFLINE rezimu, takze zadny reconnect task
    // nesahne na Bluetooth driv, nez je stack znovu pripraveny.
    offlineMode = true;
    reconnectRequested = false;
    reconnectInProgress = false;
    reconnectFinished = false;
    reconnectAbortRequested = true;
    btResumeAfterWebAttempts = 0;
    btResumeAfterWebAt = millis() + 1400UL;
    btResumeAfterWebPending = true;
    Serial.println("SERVICE WIFI stopped; BT resume scheduled");
  }
}

void serviceDeferredBluetoothResume() {
  if (!btResumeAfterWebPending || serviceWifiActive) return;
  if ((long)(millis() - btResumeAfterWebAt) < 0) return;

  btResumeAfterWebPending = false;
  btResumeAfterWebAttempts++;
  diagnosticExclusive = true;
  bool btOk = false;

  // SerialBT byl pri startu WEBu ukoncen pres end(). Zde ho pouze znovu
  // vytvorime; zadne dalsi end()/controller reset v te same chvili.
  if (!btMutex || xSemaphoreTake(btMutex, pdMS_TO_TICKS(2500)) == pdTRUE) {
    btOk = SerialBT.begin(BT_LOCAL_NAME, true);
    if (btOk) SerialBT.setPin(BT_PIN_1, 4);
    if (btMutex) xSemaphoreGive(btMutex);
  }
  diagnosticExclusive = false;

  Serial.printf("BT deferred resume after WEB=%s heap=%u\n", btOk?"OK":"FAIL", ESP.getFreeHeap());

  if (btOk) {
    reconnectAbortRequested = false;
    // Pokud je autoconnect aktivni, vratime se do bezneho rezimu a OBD task
    // provede spojeni s ELM. Jinak zustaneme offline, ale BT stack je pripraven.
    if (autoElmReconnect && hasDefaultDevice && defaultMac.length() == 17) {
      offlineMode = false;
      connectUserChoseOffline = false;
      lastReconnectAttempt = 0;
      reconnectRequested = true;
    } else {
      offlineMode = true;
      connectUserChoseOffline = true;
    }
  } else {
    // Jediny bezpecny retry s delsim odstupem. Zadny reboot ESP32.
    offlineMode = true;
    reconnectAbortRequested = true;
    if (btResumeAfterWebAttempts < 2) {
      btResumeAfterWebAt = millis() + 2500UL;
      btResumeAfterWebPending = true;
    }
  }

  if (currentScreen == SCREEN_WIFI && !alert.active) drawWifiScreen();
}

void updateWifiState() {
  if (!serviceWifiActive) {
    apClientCount = 0;
    webClientActive = false;
    return;
  }
  static unsigned long lastApClientCheck = 0;
  if (millis() - lastApClientCheck >= 500UL) {
    lastApClientCheck = millis();
    apClientCount = (uint8_t)WiFi.softAPgetStationNum();
    webClientActive = apClientCount > 0;
    if (webClientActive) serviceWifiNoClientSince = 0;
    else if (!serviceWifiNoClientSince) serviceWifiNoClientSince = millis();
  }

  // /wifi-off pouziva tento flag jako odlozene vypnuti po odeslani HTTP odpovedi.
  if (wifiConnectRequested && millis() - wifiConnectRequestedMs >= 700UL) {
    wifiConnectRequested = false;
    stopServiceWifi(true);
    if (currentScreen == SCREEN_WIFI) drawWifiScreen();
    return;
  }

  // Kdyz je web zapomenuty bez klienta 10 minut, automaticky ho vypneme.
  if (!webClientActive && serviceWifiNoClientSince && millis() - serviceWifiNoClientSince > SERVICE_WIFI_IDLE_OFF_MS) {
    stopServiceWifi(true);
    if (currentScreen == SCREEN_WIFI) drawWifiScreen();
  }
}


// =========================== POWER MANAGER ====================

bool ignitionIsOn() {
#if USE_IGNITION_SENSE
  return digitalRead(IGNITION_PIN) == IGNITION_ACTIVE_LEVEL;
#else
  // Bez samostatneho IGN wire bereme jako "klicek ON" pouze skutecne
  // aktualne odpovidajici ECU. Stare RPM uz nemuze drzet power manager vzhuru.
  return car.elmConnected && car.ecuResponding;
#endif
}

void enterDeepSleep() {
#if USE_IGNITION_SENSE
  finishTrip();
  persistServiceCounters(true);
  applyBrightness(0);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)IGNITION_PIN, IGNITION_ACTIVE_LEVEL);
  delay(100);
  esp_deep_sleep_start();
#endif
}

void updatePowerManager() {
  const bool ignitionOn = ignitionIsOn();
  const unsigned long now = millis();
  const unsigned long inactiveFor = now - lastUserActivityMs;

  // ACTIVE: po navratu ECU okamzite obnovime plny jas i rychly OBD scheduler.
  if (ignitionOn) {
    ignitionOffSinceMs = 0;
    displayOffSinceMs = 0;
    powerLowMode = false;
    powerMode = POWER_ACTIVE;
    if (displayOffAfterIgnition || displayDimmed) {
      displayOffAfterIgnition = false;
      displayDimmed = false;
      applyBrightness(activeUiBrightness());
      lastUserActivityMs = now;
    }
    return;
  }

  if (!ignitionOffSinceMs) ignitionOffSinceMs = now;

  // DIM: pouze podsvit, ostatni funkce zustavaji aktivni.
  if (powerAutoDim && inactiveFor > powerDimDelayMs && !displayDimmed && !displayOffAfterIgnition) {
    displayDimmed = true;
    powerMode = POWER_DIMMED;
    applyBrightness(25);
  }

  // DISPLAY OFF po 5 minutach bez ECU. ESP32 zustava vzhuru, aby umel
  // automaticky zachytit navrat ELM/ECU i bez samostatneho ACC dratu.
  if (!displayOffAfterIgnition && now - ignitionOffSinceMs >= powerDisplayOffDelayMs) {
    finishTrip();
    persistServiceCounters(true);
    displayOffAfterIgnition = true;
    displayDimmed = true;
    displayOffSinceMs = now;
    powerMode = POWER_DISPLAY_OFF;
    applyBrightness(0);
  }

  // LOW POWER: po dalsi minute zvolnime background OBD/probe smycku. BT zustava
  // dostupny kvuli automatickemu probuzeni; bez ACC dratu by deep sleep neumel
  // spolehlive poznat dalsi KEY ON.
  if (displayOffAfterIgnition && displayOffSinceMs && now - displayOffSinceMs >= powerLowDelayMs) {
    powerLowMode = true;
    powerMode = POWER_LOW;
  }

#if USE_IGNITION_SENSE
  // Se skutecnym IGN vstupem muzeme po zhasnuti jit do opravdoveho deep sleep.
  if (displayOffAfterIgnition) enterDeepSleep();
#endif
}

// =========================== RECONNECT ========================

bool restartBluetoothMaster() {
  diagnosticExclusive = true;
  bool ok = false;
  if (!btMutex || xSemaphoreTake(btMutex, pdMS_TO_TICKS(1500)) == pdTRUE) {
    // BLE je ve V14.3 uplne pryc, proto lze bezpecne resetovat cely lokalni
    // Bluetooth Classic stack a znovu vytvorit SPP master pro ELM327.
    SerialBT.disconnect();
    delay(350);
    SerialBT.end();
    delay(900);
    ok = SerialBT.begin(BT_LOCAL_NAME, true);
    if (ok) SerialBT.setPin(BT_PIN_1, 4);
    if (btMutex) xSemaphoreGive(btMutex);
  }
  diagnosticExclusive = false;
  return ok;
}

bool backgroundReconnectDefaultDevice() {
  if (reconnectAbortRequested || offlineMode || serviceWifiActive || btScanning || !hasDefaultDevice || defaultMac.length() != 17) return false;

  uint8_t addr[6];
  if (!macToBytes(defaultMac, addr)) return false;

  diagnosticExclusive = true;
  car.elmConnected = false;
  invalidateLiveTelemetry(true);

  auto tryConnect = [&]() -> bool {
    bool connected = false;
    if (!btMutex || xSemaphoreTake(btMutex, pdMS_TO_TICKS(1800)) == pdTRUE) {
      // Po resetu ESP32 muze ELM stale drzet starou SPP session. Lokalni
      // disconnect pred connectem pomaha uvolnit zbytky predchoziho spojeni.
      SerialBT.disconnect();
      delay(120);
      if (reconnectAbortRequested || offlineMode || serviceWifiActive) {
        if (btMutex) xSemaphoreGive(btMutex);
        return false;
      }
      SerialBT.setPin(BT_PIN_1, 4);
      connected = SerialBT.connect(addr);
      if (reconnectAbortRequested || offlineMode || serviceWifiActive) {
        if (connected) SerialBT.disconnect();
        if (btMutex) xSemaphoreGive(btMutex);
        return false;
      }
      if (!connected) {
        SerialBT.setPin(BT_PIN_2, 4);
        connected = SerialBT.connect(addr);
      }
      if (reconnectAbortRequested || offlineMode || serviceWifiActive) {
        if (connected) SerialBT.disconnect();
        if (btMutex) xSemaphoreGive(btMutex);
        return false;
      }
      if (btMutex) xSemaphoreGive(btMutex);
    }
    return connected;
  };

  bool ok = tryConnect();

  if (!ok) {
    if (btRecoveryFailures < 250) btRecoveryFailures++;

    // V16.10: po prvnim neuspechu a pak kazdem tretim provedeme stejny plny
    // BT recovery, ktery mela funkcni boot cesta V16.2. Dulezite pri startovani:
    // ESP32 se restartuje, ale ELM zustane pod proudem a muze drzet starou SPP session.
    if (reconnectAbortRequested || offlineMode || serviceWifiActive) {
      diagnosticExclusive = false;
      return false;
    }
    bool fullRecovery = (btRecoveryFailures == 1 || (btRecoveryFailures % 3) == 0);
    if (fullRecovery && !serviceWifiActive && !offlineMode && !reconnectAbortRequested) {
      Serial.printf("BT full recovery after %u failed reconnect(s)\n", btRecoveryFailures);
      if (restartBluetoothMaster()) {
        diagnosticExclusive = true;
        if (reconnectAbortRequested || offlineMode || serviceWifiActive) {
          diagnosticExclusive = false;
          return false;
        }
        delay(1200);
        if (reconnectAbortRequested || offlineMode || serviceWifiActive) {
          diagnosticExclusive = false;
          return false;
        }
        ok = tryConnect();
        if (!ok && !reconnectAbortRequested && !offlineMode && !serviceWifiActive) {
          // Dej klonu ELM cas, aby sam uvolnil starou RFCOMM/SPP session.
          delay(2500);
          if (!reconnectAbortRequested && !offlineMode && !serviceWifiActive) ok = tryConnect();
        }
      } else {
        diagnosticExclusive = true;
      }
    }
  }

  if (!ok) {
    diagnosticExclusive = false;
    return false;
  }

  if (reconnectAbortRequested || offlineMode || btScanning || serviceWifiActive) {
    if (!btMutex || xSemaphoreTake(btMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      SerialBT.disconnect();
      if (btMutex) xSemaphoreGive(btMutex);
    }
    diagnosticExclusive = false;
    return false;
  }

  car.elmConnected = true;
  car.ecuResponding = false;
  bool initOk = initElm(false);
  if (!initOk) {
    if (!btMutex || xSemaphoreTake(btMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      SerialBT.disconnect();
      if (btMutex) xSemaphoreGive(btMutex);
    }
    car.elmConnected = false;
    invalidateLiveTelemetry(true);
    diagnosticExclusive = false;
    return false;
  }

  btRecoveryFailures = 0;
  warnedDisconnect = false;
  diagnosticExclusive = false;
  return true;
}

void maintainBluetoothConnection() {
  // Dokonceni reconnectu zpracujeme i v OFFLINE rezimu. STOP tak po navratu
  // z blokujiciho connect() automaticky odemkne ZAPNOUT WEB bez dalsiho dotyku.
  if (reconnectFinished) {
    bool ok = reconnectResult;
    reconnectFinished = false;
    if (ok && !offlineMode && !reconnectAbortRequested) {
      warnedDisconnect = false;
      drawLiveScreen();
    } else if (currentScreen == SCREEN_WIFI && !serviceWifiActive) {
      drawWifiScreen();
    }
  }

  if (offlineMode || btScanning) return;
  if (currentScreen == SCREEN_BT_RESULTS ||
      currentScreen == SCREEN_BT_SCAN ||
      currentScreen == SCREEN_ASK_DEFAULT ||
      currentScreen == SCREEN_CONNECT_FAIL) return;

  static unsigned long lastLinkCheck = 0;
  if (millis() - lastLinkCheck < BT_LINK_CHECK_INTERVAL_MS) return;
  lastLinkCheck = millis();

  bool connected = car.elmConnected;
  if (btMutex && xSemaphoreTake(btMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
    connected = SerialBT.connected(0);
    xSemaphoreGive(btMutex);
  }

  static uint8_t linkFailCount = 0;
  if (car.elmConnected && !connected) {
    // Jednorazove false pri soubehu Wi-Fi/BT nepovazujeme za odpojeni.
    if (linkFailCount < 10) linkFailCount++;
    if (linkFailCount >= 5) {
      car.elmConnected = false;
      invalidateLiveTelemetry(true);
      linkFailCount = 0;
      if (!warnedDisconnect) {
        warnedDisconnect = true;
        if (alarmElmDisconnectEnabled)
          showAlert("ELM327 ODPOJEN", "Obnovuji spojeni...", defaultName, COL_RED, 3000);
      }
    }
  } else {
    linkFailCount = 0;
  }

  if (autoElmReconnect && !serviceWifiActive && !car.elmConnected && hasDefaultDevice && !reconnectInProgress &&
      millis() - lastReconnectAttempt > RECONNECT_INTERVAL_MS) {
    lastReconnectAttempt = millis();
    reconnectRequested = true;
  }
}

// =========================== REDRAW ===========================

void redrawCurrentScreen() {
  if (alert.active) return;

  switch (currentScreen) {
    case SCREEN_MENU: drawMainMenu(); break;
    case SCREEN_MENU_MORE: drawMoreMenu(); break;
    case SCREEN_MENU_SYSTEM: drawSystemMenu(); break;
    case SCREEN_SPECIALS: drawSpecialsScreen(); break;
    case SCREEN_SENSORS: drawSensorsScreen(); break;
    case SCREEN_DPF: drawDpfScreen(); break;
    case SCREEN_DPF_HISTORY: drawDpfHistory(); break;
    case SCREEN_ENGINE: drawEngineScreen(); break;
    case SCREEN_TURBO: drawTurboScreen(); break;
    case SCREEN_ELECTRICAL: drawElectricalScreen(); break;
    case SCREEN_DIAGNOSTICS: drawDiagnosticsScreen(); break;
    case SCREEN_LIVE: drawLiveScreen(); break;
    case SCREEN_TRIPS: drawTripsScreen(); break;
    case SCREEN_FUELING: drawFuelingScreen(); break;
    case SCREEN_SERVICE: drawServiceScreen(); break;
    case SCREEN_POWER: drawPowerScreen(); break;
    case SCREEN_ALARMS: drawAlarmsScreen(); break;
    case SCREEN_SETTINGS: drawSettingsScreen(); break;
    case SCREEN_SETTINGS_MORE: drawSettingsMoreScreen(); break;
    case SCREEN_SETTINGS_TEXT: drawSettingsTextScreen(); break;
    case SCREEN_SETTINGS_DPF: drawSettingsDpfScreen(); break;
    case SCREEN_SETTINGS_SYSTEM: drawSettingsSystemScreen(); break;
    case SCREEN_SETTINGS_ADVANCED: drawSettingsAdvancedScreen(); break;
    case SCREEN_INFO: drawInfoScreen(); break;
    case SCREEN_WIFI: drawWifiScreen(); break;
    case SCREEN_BT_RESULTS: drawBtResults(); break;
    case SCREEN_BT_SCAN: drawBtScanScreen(); break;
    case SCREEN_CONNECTING: drawConnectingScreen(); break;
    case SCREEN_CONNECT_FAIL: drawConnectFail(); break;
    default: drawMainMenu(); break;
  }
}

// =========================== TOUCH UI =========================

void handleMenuTouch(int x, int y) {
  // V16.8: hlavni menu je 3 sloupce x 2 radky, bez pomocnych popisku.
  if      (inside(x, y, 6,   42, 100, 78)) drawLiveScreen();
  else if (inside(x, y, 110, 42, 100, 78)) drawEngineScreen();
  else if (inside(x, y, 214, 42, 100, 78)) drawTurboScreen();
  else if (inside(x, y, 6,   126,100, 78)) drawDpfScreen();
  else if (inside(x, y, 110, 126,100, 78)) drawTripsScreen();
  else if (inside(x, y, 214, 126,100, 78)) drawFuelingScreen();
  else if (inside(x, y, 225, 208,95, 32)) drawMoreMenu();
}

void handleMoreMenuTouch(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) { drawMainMenu(); return; }
  if      (inside(x, y, 6,   42, 100, 78)) drawDiagnosticsScreen();
  else if (inside(x, y, 110, 42, 100, 78)) drawSensorsScreen();
  else if (inside(x, y, 214, 42, 100, 78)) drawElectricalScreen();
  else if (inside(x, y, 6,   126,100, 78)) drawServiceScreen();
  else if (inside(x, y, 110, 126,100, 78)) drawAlarmsScreen();
  else if (inside(x, y, 214, 126,100, 78)) drawSpecialsScreen();
  else if (inside(x, y, 0, 208, 95, 32)) drawMainMenu();
  else if (inside(x, y, 225, 208,95, 32)) drawSystemMenu();
}

void handleSystemMenuTouch(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) { drawMoreMenu(); return; }
  if      (inside(x, y, 8,   48, 148, 72)) { settingsSubpageContext = false; drawWifiScreen(); }
  else if (inside(x, y, 164, 48, 148, 72)) { settingsSubpageContext = false; drawPowerScreen(); }
  else if (inside(x, y, 8,   128,148, 72)) drawSettingsScreen();
  else if (inside(x, y, 164, 128,148, 72)) drawInfoScreen();
  else if (inside(x, y, 0, 208, 105, 32)) drawMoreMenu();
  else if (inside(x, y, 225,208,95,32)) drawMainMenu();
}


void handleSensorsTouch(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32) || inside(x, y, 0, 207, 95, 33)) {
    drawMoreMenu();
    return;
  }
  if (inside(x, y, 215, 207, 105, 33)) {
    sensorPage = sensorPage == 0 ? 1 : 0;
    drawSensorsScreen();
  }
}

void handleSpecialsTouch(int x, int y) {
  // Behem diagnosticke sekvence nenechame menit obrazovku.
  if (specialsRunning || specialsRequestPending) return;
  if (inside(x, y, 8, 5, 34, 32) || inside(x, y, 0, 212, 100, 28)) {
    drawMoreMenu();
    return;
  }
  if (inside(x, y, 40, 123, 240, 58)) {
    startThankYouSequence();
    drawSpecialsScreen();
  }
}

void handleAlarmsTouch(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) { drawMoreMenu(); return; }
  int row = -1;
  const int ys[6] = {45,76,107,138,169,200};
  for (int i=0;i<6;i++) if (inside(x,y,10,ys[i]-2,300,31)) { row=i; break; }
  if (row < 0) return;
  if      (row==0) alarmCoolantEnabled = !alarmCoolantEnabled;
  else if (row==1) alarmLowVoltageEnabled = !alarmLowVoltageEnabled;
  else if (row==2) alarmOverVoltageEnabled = !alarmOverVoltageEnabled;
  else if (row==3) alarmRegenEnabled = !alarmRegenEnabled;
  else if (row==4) { alarmRegenSoonEnabled = !alarmRegenSoonEnabled; warnedRegenSoon=false; }
  else if (row==5) alarmElmDisconnectEnabled = !alarmElmDisconnectEnabled;
  saveAlarmPreferences();
  drawAlarmsScreen();
}

void handleBtScanTouch(int x, int y) {
  if (inside(x, y, 215, 214, 100, 26)) {
    btScanning = false;
    SerialBT.discoverAsyncStop();
    SerialBT.discoverClear();
    enterOfflineMode();
    drawMainMenu();
    return;
  }
}

void handleDpfTouch(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) { drawMainMenu(); return; }
  if (!vehicleHasAdvancedDpf()) {
    if (inside(x, y, 60, 190, 200, 50)) drawMainMenu();
    return;
  }
  if (inside(x, y, 8, 198, 96, 38)) { dpfHistoryParent = SCREEN_DPF; drawDpfHistory(); }
}

void handleDpfHistoryTouch(int x, int y) {
  if (!inside(x, y, 8, 5, 34, 32)) return;
  if (dpfHistoryParent == SCREEN_DPF) drawDpfScreen();
  else drawMoreMenu();
}

void handleGenericBack(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) drawMainMenu();
}

void handleWifiTouch(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) { if (settingsSubpageContext) drawSettingsScreen(); else drawSystemMenu(); return; }

  if (!serviceWifiActive && inside(x, y, 48, 106, 224, 48)) {
    if (!car.elmConnected && (reconnectInProgress || reconnectRequested) && !offlineMode) {
      stopElmReconnectAttempts();
    } else if (car.elmConnected) {
      manualDisconnectElm();
    } else if (!offlineMode && !reconnectAbortRequested) {
      stopElmReconnectAttempts();
    } else {
      requestElmReconnect();
    }
    if (!alert.active) drawWifiScreen();
    return;
  }

  if (inside(x, y, 55, 184, 210, 54)) {
    if (serviceWifiActive) {
      stopServiceWifi(true);
      drawWifiScreen();
    } else {
      if (offlineMode && !reconnectInProgress && !reconnectRequested) {
        if (startServiceWifi()) drawWifiScreen();
      } else {
        showAlert("SERVISNI WEB", reconnectInProgress ? "ELM se jeste zastavuje" : "Nejdriv stiskni STOP",
                  "Pockej na stav ZASTAVENO", COL_YELLOW, 2500);
      }
    }
  }
}

void handleMoreBack(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) drawMoreMenu();
}

void handleSystemBack(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) drawSystemMenu();
}

void handleDiagnosticsTouch(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) {
    if (diagDetailIndex >= 0) { diagDetailIndex = -1; drawDiagnosticsScreen(); }
    else drawMoreMenu();
    return;
  }

  if (diagDetailIndex >= 0) {
    if (inside(x, y, 218, 50, 86, 45) && !dtc.busy) {
      requestDiagScanOne(diagDetailIndex);
      drawDiagnosticsScreen();
    }
    return;
  }

  if (inside(x, y, 218, 47, 88, 45) && !dtc.busy) {
    requestDiagScanAll();
    drawDiagnosticsScreen();
    return;
  }

  int first = diagPage * 4;
  int yy = 97;
  for (int row = 0; row < 4; row++) {
    int idx = first + row;
    if (idx >= DIAG_MODULE_COUNT) break;
    if (inside(x, y, 12, yy, 296, 28)) {
      diagDetailIndex = idx;
      drawDiagnosticsScreen();
      return;
    }
    yy += 31;
  }

  if (inside(x, y, 0, 210, 90, 30) && diagPage > 0) {
    diagPage--; drawDiagnosticsScreen(); return;
  }
  if (inside(x, y, 230, 210, 90, 30) && (diagPage + 1) * 4 < DIAG_MODULE_COUNT) {
    diagPage++; drawDiagnosticsScreen(); return;
  }
}

void handleServiceTouch(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) { drawMoreMenu(); return; }
  uint8_t target = 0;
  if (inside(x, y, 250, 62, 58, 50)) target = 1;
  if (inside(x, y, 250, 141, 58, 50)) target = 2;
  if (!target) return;

  if (serviceResetArmed && serviceResetTarget == target && millis() < serviceResetArmUntil) {
    resetServiceCounter(target);
  } else {
    serviceResetArmed = true;
    serviceResetTarget = target;
    serviceResetArmUntil = millis() + 5000UL;
  }
  drawServiceScreen();
}

void handlePowerTouch(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) {
    if (settingsSubpageContext) drawSettingsScreen(); else drawSystemMenu();
    return;
  }
  if (inside(x, y, 235, 82, 73, 42)) {
    powerAutoDim = !powerAutoDim;
    markSettingsDirty();
    drawPowerScreen();
    return;
  }
  if (inside(x, y, 208, 122, 50, 45)) {
    powerDimDelayMs = max(60000UL, powerDimDelayMs - 60000UL);
    markSettingsDirty(); drawPowerScreen(); return;
  }
  if (inside(x, y, 258, 122, 58, 45)) {
    powerDimDelayMs = min(600000UL, powerDimDelayMs + 60000UL);
    markSettingsDirty(); drawPowerScreen(); return;
  }
  if (inside(x, y, 208, 162, 50, 45)) {
    powerDisplayOffDelayMs = max(120000UL, powerDisplayOffDelayMs - 60000UL);
    markSettingsDirty(); drawPowerScreen(); return;
  }
  if (inside(x, y, 258, 162, 58, 45)) {
    powerDisplayOffDelayMs = min(1800000UL, powerDisplayOffDelayMs + 60000UL);
    markSettingsDirty(); drawPowerScreen(); return;
  }
  if (inside(x, y, 205, 207, 52, 33)) {
    powerLowDelayMs = max(60000UL, powerLowDelayMs - 60000UL);
    markSettingsDirty(); drawPowerScreen(); return;
  }
  if (inside(x, y, 258, 207, 58, 33)) {
    powerLowDelayMs = min(600000UL, powerLowDelayMs + 60000UL);
    markSettingsDirty(); drawPowerScreen(); return;
  }
}

void handleSettingsTouch(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) { flushDeferredSettings(true); drawSystemMenu(); return; }
  if      (inside(x, y, 6,   46, 100, 72)) drawSettingsMoreScreen();
  else if (inside(x, y, 110, 46, 100, 72)) drawSettingsTextScreen();
  else if (inside(x, y, 214, 46, 100, 72)) drawSettingsDpfScreen();
  else if (inside(x, y, 6,   124,100, 72)) { settingsSubpageContext = true; drawPowerScreen(); }
  else if (inside(x, y, 110, 124,100, 72)) drawSettingsSystemScreen();
  else if (inside(x, y, 214, 124,100, 72)) { settingsSubpageContext = true; drawWifiScreen(); }
}

void handleSettingsMoreTouch(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) { drawSettingsScreen(); return; }
  if (inside(x, y, 208, 42, 52, 45)) {
    displayBrightness = (uint8_t)max(40, (int)displayBrightness - 15);
    applyBrightness(activeUiBrightness()); markSettingsDirty(); drawSettingsMoreScreen(); return;
  }
  if (inside(x, y, 260, 42, 56, 45)) {
    displayBrightness = (uint8_t)min(255, (int)displayBrightness + 15);
    applyBrightness(activeUiBrightness()); markSettingsDirty(); drawSettingsMoreScreen(); return;
  }
  if (inside(x, y, 208, 84, 52, 45)) {
    displayNightBrightness = (uint8_t)max(10, (int)displayNightBrightness - 10);
    markSettingsDirty(); drawSettingsMoreScreen(); return;
  }
  if (inside(x, y, 260, 84, 56, 45)) {
    displayNightBrightness = (uint8_t)min(180, (int)displayNightBrightness + 10);
    markSettingsDirty(); drawSettingsMoreScreen(); return;
  }
  if (inside(x, y, 235, 126, 75, 45)) {
    autoDayNightBrightness = !autoDayNightBrightness;
    applyBrightness(activeUiBrightness()); markSettingsDirty(); drawSettingsMoreScreen(); return;
  }
  if (inside(x, y, 235, 168, 75, 45)) {
    touchFeedbackEnabled = !touchFeedbackEnabled;
    markSettingsDirty(); drawSettingsMoreScreen(); return;
  }
  if (inside(x, y, 205, 207, 52, 33)) {
    touchFeedbackStrength = (uint8_t)max(8, (int)touchFeedbackStrength - 4);
    markSettingsDirty(); drawSettingsMoreScreen(); return;
  }
  if (inside(x, y, 258, 207, 58, 33)) {
    touchFeedbackStrength = (uint8_t)min(60, (int)touchFeedbackStrength + 4);
    markSettingsDirty(); drawSettingsMoreScreen(); return;
  }
}

void handleSettingsTextTouch(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) { drawSettingsScreen(); return; }
  if (inside(x, y, 205, 42, 105, 45)) {
    if (car.elmConnected) manualDisconnectElm(); else requestElmReconnect();
    if (!alert.active) drawSettingsTextScreen();
    return;
  }
  if (inside(x, y, 235, 84, 75, 45)) {
    autoElmReconnect = !autoElmReconnect;
    if (!autoElmReconnect) reconnectRequested = false;
    markSettingsDirty(); drawSettingsTextScreen(); return;
  }
  if (inside(x, y, 205, 126, 105, 45)) {
    obdDataMode = (uint8_t)((obdDataMode + 1) % 3);
    fastDataMode = obdDataMode == 2;
    markSettingsDirty(); drawSettingsTextScreen(); return;
  }
  if (inside(x, y, 205, 168, 105, 45)) { startBtScan(); return; }
  if (inside(x, y, 80, 205, 160, 35)) { drawSettingsAdvancedScreen(); return; }
}

void handleSettingsDpfTouch(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) { drawSettingsScreen(); return; }
  if (inside(x, y, 235, 42, 75, 45)) {
    alarmRegenEnabled = !alarmRegenEnabled; markSettingsDirty(); drawSettingsDpfScreen(); return;
  }
  if (inside(x, y, 235, 84, 75, 45)) {
    alarmRegenSoonEnabled = !alarmRegenSoonEnabled; warnedRegenSoon = false;
    markSettingsDirty(); drawSettingsDpfScreen(); return;
  }
  if (inside(x, y, 208, 126, 52, 45)) {
    alarmRegenSoonKm = max(10.0f, alarmRegenSoonKm - 10.0f);
    warnedRegenSoon = false; markSettingsDirty(); drawSettingsDpfScreen(); return;
  }
  if (inside(x, y, 260, 126, 56, 45)) {
    alarmRegenSoonKm = min(50.0f, alarmRegenSoonKm + 10.0f);
    warnedRegenSoon = false; markSettingsDirty(); drawSettingsDpfScreen(); return;
  }
  if (inside(x, y, 208, 168, 52, 45)) {
    dpfRegenLimitG = max(15.0f, dpfRegenLimitG - 0.5f);
    markSettingsDirty(); drawSettingsDpfScreen(); return;
  }
  if (inside(x, y, 260, 168, 56, 45)) {
    dpfRegenLimitG = min(45.0f, dpfRegenLimitG + 0.5f);
    markSettingsDirty(); drawSettingsDpfScreen(); return;
  }
}

static void restoreSettingsDefaults() {
  displayBrightness = 220;
  displayNightBrightness = 70;
  autoDayNightBrightness = true;
  touchFeedbackEnabled = true;
  touchFeedbackStrength = 28;
  autoElmReconnect = true;
  obdDataMode = 2;
  fastDataMode = true;
  configuredEngineProfile = ENGINE_PROFILE_AUTO;
  dpfRegenLimitG = DEFAULT_DPF_REGEN_LIMIT_G;
  alarmRegenSoonKm = 10.0f;
  alarmRegenEnabled = true;
  alarmRegenSoonEnabled = true;
  powerAutoDim = true;
  powerDimDelayMs = DISPLAY_DIM_DELAY_MS;
  powerDisplayOffDelayMs = DISPLAY_OFF_DELAY_MS;
  powerLowDelayMs = POWER_LOW_DELAY_MS;
  tankCapacityL = DEFAULT_TANK_CAPACITY_L;
  rangeFallbackL100 = DEFAULT_RANGE_CONSUMPTION_L100;
  settingsResetArmed = false;
  markSettingsDirty();
  flushDeferredSettings(true);
  applyBrightness(activeUiBrightness());
}

void handleSettingsSystemTouch(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) { settingsResetArmed = false; drawSettingsScreen(); return; }
  if (inside(x, y, 8, 205, 150, 35)) { drawSettingsAdvancedScreen(); return; }
  if (inside(x, y, 160, 205, 160, 35)) {
    if (settingsResetArmed && millis() < settingsResetArmUntil) {
      restoreSettingsDefaults();
      showAlert("NASTAVENI", "Vychozi hodnoty obnoveny", "Jizdy a historie zustaly zachovany", COL_GREEN, 3000);
    } else {
      settingsResetArmed = true;
      settingsResetArmUntil = millis() + 5000UL;
      drawSettingsSystemScreen();
    }
    return;
  }
}

void handleSettingsAdvancedTouch(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) { drawSettingsSystemScreen(); return; }
  if (inside(x, y, 205, 42, 105, 45)) {
    configuredEngineProfile = (EngineProfile)(((uint8_t)configuredEngineProfile + 1) % 5);
    markSettingsDirty();
    drawSettingsAdvancedScreen();
    return;
  }
}

void handleBtResultsTouch(int x, int y) {
  if (inside(x, y, 8, 5, 34, 32)) {
    drawSettingsScreen();
    return;
  }

  int pageCount = max(1, (deviceCount + DEVICES_PER_PAGE - 1) / DEVICES_PER_PAGE);

  if (inside(x, y, 8, 190, 35, 28) && btPage > 0) {
    btPage--;
    drawBtResults();
    return;
  }

  if (inside(x, y, 65, 190, 40, 28) && btPage < pageCount - 1) {
    btPage++;
    drawBtResults();
    return;
  }

  int first = btPage * DEVICES_PER_PAGE;
  int yy = 65;

  for (int i = 0; i < DEVICES_PER_PAGE; i++) {
    int idx = first + i;
    if (idx >= deviceCount) break;

    if (inside(x, y, 10, yy, 300, 38)) {
      selectedDevice = idx;
      drawBtResults();
      return;
    }
    yy += 44;
  }

  if (inside(x, y, 95, 205, 110, 35)) {
    startBtScan();
    return;
  }

  if (selectedDevice >= 0 && inside(x, y, 200, 205, 120, 35)) {
    DeviceInfo &d = devices[selectedDevice];

    if (!connectToDevice(d.name, d.mac, true)) {
      if (connectUserChoseOther || connectUserChoseOffline) return;
      pendingDeviceName = d.name;
      pendingDeviceMac = d.mac;
      drawConnectFail();
    }
  }
}

void handleConnectingTouch(int x, int y) {
  if (inside(x, y, 70, 196, 180, 44)) {
    enterOfflineMode();
    drawMainMenu();
  }
}

void handleConnectFailTouch(int x, int y) {
  if (inside(x, y, 20, 150, 100, 65)) {
    startBtScan();
    return;
  }

  if (inside(x, y, 110, 150, 100, 65)) {
    if (!connectToDevice(pendingDeviceName, pendingDeviceMac, false)) {
      if (connectUserChoseOther || connectUserChoseOffline) return;
      drawConnectFail();
    }
    return;
  }

  if (inside(x, y, 200, 150, 110, 65)) {
    enterOfflineMode();
    drawMainMenu();
    return;
  }
}

void handleAskDefaultTouch(int x, int y) {
  if (inside(x, y, 55, 190, 105, 45)) { drawLiveScreen(); return; }
  if (inside(x, y, 160, 190, 110, 45)) {
    saveDefaultDevice(pendingDeviceName, pendingDeviceMac);
    drawLiveScreen();
  }
}

void handleTouch() {
  int x, y;
  if (!getTap(x, y)) return;

  if (alert.active) {
    alert.active = false;
    redrawCurrentScreen();
    return;
  }

  switch (currentScreen) {
    case SCREEN_MENU: handleMenuTouch(x, y); break;
    case SCREEN_MENU_MORE: handleMoreMenuTouch(x, y); break;
    case SCREEN_MENU_SYSTEM: handleSystemMenuTouch(x, y); break;
    case SCREEN_SPECIALS: handleSpecialsTouch(x, y); break;
    case SCREEN_SENSORS: handleSensorsTouch(x, y); break;
    case SCREEN_DPF: handleDpfTouch(x, y); break;
    case SCREEN_DPF_HISTORY: handleDpfHistoryTouch(x, y); break;
    case SCREEN_ENGINE: handleGenericBack(x, y); break;
    case SCREEN_TURBO: handleGenericBack(x, y); break;
    case SCREEN_ELECTRICAL: handleMoreBack(x, y); break;
    case SCREEN_DIAGNOSTICS: handleDiagnosticsTouch(x, y); break;
    case SCREEN_LIVE: handleGenericBack(x, y); break;
    case SCREEN_TRIPS: handleTripsTouch(x, y); break;
    case SCREEN_FUELING: handleMoreBack(x, y); break;
    case SCREEN_SERVICE: handleServiceTouch(x, y); break;
    case SCREEN_POWER: handlePowerTouch(x, y); break;
    case SCREEN_ALARMS: handleAlarmsTouch(x, y); break;
    case SCREEN_SETTINGS: handleSettingsTouch(x, y); break;
    case SCREEN_SETTINGS_MORE: handleSettingsMoreTouch(x, y); break;
    case SCREEN_SETTINGS_TEXT: handleSettingsTextTouch(x, y); break;
    case SCREEN_SETTINGS_DPF: handleSettingsDpfTouch(x, y); break;
    case SCREEN_SETTINGS_SYSTEM: handleSettingsSystemTouch(x, y); break;
    case SCREEN_SETTINGS_ADVANCED: handleSettingsAdvancedTouch(x, y); break;
    case SCREEN_INFO: handleSystemBack(x, y); break;
    case SCREEN_WIFI: handleWifiTouch(x, y); break;
    case SCREEN_BT_RESULTS: handleBtResultsTouch(x, y); break;
    case SCREEN_BT_SCAN: handleBtScanTouch(x, y); break;
    case SCREEN_CONNECTING: handleConnectingTouch(x, y); break;
    case SCREEN_CONNECT_FAIL: handleConnectFailTouch(x, y); break;
    case SCREEN_ASK_DEFAULT: handleAskDefaultTouch(x, y); break;
    default: break;
  }
}

// =========================== PREFS ============================

void loadPreferences() {
  prefs.begin("octavia2pc", false);


  displayBrightness = prefs.getUChar("brightness", 220);
  displayNightBrightness = prefs.getUChar("nightBright", 70);
  if (displayNightBrightness < 10 || displayNightBrightness > 180) displayNightBrightness = 70;
  autoDayNightBrightness = prefs.getBool("autoDayNight", true);
  touchFeedbackEnabled = prefs.getBool("touchFb", true);
  touchFeedbackStrength = prefs.getUChar("touchFbStr", 28);
  if (touchFeedbackStrength < 8 || touchFeedbackStrength > 60) touchFeedbackStrength = 28;
  autoElmReconnect = prefs.getBool("autoElm", true);
  if (prefs.isKey("obdMode")) obdDataMode = prefs.getUChar("obdMode", 2);
  else obdDataMode = prefs.getBool("fastData", true) ? 2 : 1;
  if (obdDataMode > 2) obdDataMode = 2;
  uint8_t savedEngineProfile = prefs.getUChar("engineProf", (uint8_t)ENGINE_PROFILE_AUTO);
  if (savedEngineProfile > (uint8_t)ENGINE_PROFILE_GENERIC_OBD) savedEngineProfile = (uint8_t)ENGINE_PROFILE_AUTO;
  configuredEngineProfile = (EngineProfile)savedEngineProfile;
  activeEngineProfile = configuredEngineProfile == ENGINE_PROFILE_AUTO ? ENGINE_PROFILE_GENERIC_OBD : configuredEngineProfile;
  tankCapacityL = prefs.getFloat("tankCapL", DEFAULT_TANK_CAPACITY_L);
  if (tankCapacityL < 35.0f || tankCapacityL > 80.0f) tankCapacityL = DEFAULT_TANK_CAPACITY_L;
  rangeFallbackL100 = prefs.getFloat("rangeL100", DEFAULT_RANGE_CONSUMPTION_L100);
  if (rangeFallbackL100 < 2.0f || rangeFallbackL100 > 20.0f) rangeFallbackL100 = DEFAULT_RANGE_CONSUMPTION_L100;
  manualFuelConfigured = prefs.getBool("fuelManual", false);
  manualFuelPct = prefs.getFloat("fuelManualPct", NAN);
  if (!manualFuelConfigured || isnan(manualFuelPct) || manualFuelPct < 0.0f || manualFuelPct > 100.0f) {
    manualFuelConfigured = false; manualFuelPct = NAN;
  }
  storedFuelValid = prefs.getBool("fuelLastOk", false);
  storedFuelPct = prefs.getFloat("fuelLastPct", NAN);
  if (!storedFuelValid || isnan(storedFuelPct) || storedFuelPct < 0.0f || storedFuelPct > 100.0f) {
    storedFuelValid = false; storedFuelPct = NAN;
  }
  lastStoredFuelPersistPct = storedFuelPct;
  fuelingMinRiseL = prefs.getFloat("fuelMinRise", DEFAULT_FUELING_MIN_RISE_L);
  if (fuelingMinRiseL < 2.0f || fuelingMinRiseL > 12.0f) fuelingMinRiseL = DEFAULT_FUELING_MIN_RISE_L;
  trackedOdometerConfigured = prefs.getBool("odoSet", false);
  trackedOdometerKm = prefs.getFloat("odoKm", NAN);
  if (!trackedOdometerConfigured || isnan(trackedOdometerKm) || trackedOdometerKm < 0.0f || trackedOdometerKm > 2000000.0f) {
    trackedOdometerConfigured = false;
    trackedOdometerKm = NAN;
  }
  lastOdometerPersistKm = trackedOdometerKm;
  fuelBaselineValid = prefs.getBool("fuelBaseOk", false);
  fuelBaselinePct = prefs.getFloat("fuelBasePct", NAN);
  if (!fuelBaselineValid || isnan(fuelBaselinePct) || fuelBaselinePct < 0.0f || fuelBaselinePct > 100.0f) {
    fuelBaselineValid = false;
    fuelBaselinePct = NAN;
  }
  dpfRegenLimitG = prefs.getFloat("dpfLimitG", DEFAULT_DPF_REGEN_LIMIT_G);
  if (dpfRegenLimitG < 15.0f || dpfRegenLimitG > 45.0f) dpfRegenLimitG = DEFAULT_DPF_REGEN_LIMIT_G;
  fastDataMode = (obdDataMode == 2);
  developerName = prefs.getString("devName", "");
  developerEmail = prefs.getString("devEmail", "");
  developerPhone = prefs.getString("devPhone", "");
  developerWeb = prefs.getString("devWeb", "");
  splashTitle = sanitizeUiText(prefs.getString("splashTxt", DEFAULT_SPLASH_TITLE), DEFAULT_SPLASH_TITLE, 30);
  mainHeaderTitle = sanitizeUiText(prefs.getString("headerTxt", DEFAULT_HEADER_TITLE), DEFAULT_HEADER_TITLE, 22);
  powerAutoDim = prefs.getBool("pwrAutoDim", true);
  alarmCoolantEnabled = prefs.getBool("alCoolant", true);
  alarmLowVoltageEnabled = prefs.getBool("alLowV", true);
  alarmOverVoltageEnabled = prefs.getBool("alHighV", true);
  alarmRegenEnabled = prefs.getBool("alRegen", true);
  alarmRegenSoonEnabled = prefs.getBool("alRegen10", true);
  alarmRegenSoonKm = prefs.getFloat("regenSoonKm", 10.0f);
  if (alarmRegenSoonKm < 10.0f || alarmRegenSoonKm > 50.0f) alarmRegenSoonKm = 10.0f;
  alarmElmDisconnectEnabled = prefs.getBool("alElm", true);
  powerDimDelayMs = prefs.getULong("pwrDimMs", DISPLAY_DIM_DELAY_MS);
  if (powerDimDelayMs < 60000UL) powerDimDelayMs = 60000UL;
  if (powerDimDelayMs > 600000UL) powerDimDelayMs = 600000UL;
  powerDisplayOffDelayMs = prefs.getULong("pwrOffMs", DISPLAY_OFF_DELAY_MS);
  if (powerDisplayOffDelayMs < 120000UL) powerDisplayOffDelayMs = 120000UL;
  if (powerDisplayOffDelayMs > 1800000UL) powerDisplayOffDelayMs = 1800000UL;
  powerLowDelayMs = prefs.getULong("pwrLowMs", POWER_LOW_DELAY_MS);
  if (powerLowDelayMs < 60000UL) powerLowDelayMs = 60000UL;
  if (powerLowDelayMs > 600000UL) powerLowDelayMs = 600000UL;

  service.oilKm = prefs.getFloat("svcOilKm", 0.0f);
  service.inspectionKm = prefs.getFloat("svcInspKm", 0.0f);
  service.oilEpoch = (time_t)prefs.getULong64("svcOilEpoch", 0);
  service.inspectionEpoch = (time_t)prefs.getULong64("svcInspEpoch", 0);
  lastServicePersistKm = service.oilKm;
  lastServicePersistInspectionKm = service.inspectionKm;

  // Verejna edice nema zadny prednastaveny ELM327.
  // Pokud uz uzivatel nekdy adapter vybral, nacteme jeho vlastni ulozenou volbu.
  hasDefaultDevice = prefs.getBool("hasDefault", false);
  defaultName = prefs.getString("defName", FACTORY_DEFAULT_NAME);
  defaultMac = prefs.getString("defMac", FACTORY_DEFAULT_MAC);
  if (!hasDefaultDevice || defaultMac.length() != 17) {
    hasDefaultDevice = false;
    defaultName = "";
    defaultMac = "";
  }
}

// ============================ SETUP ===========================

void setup() {
  Serial.begin(115200);
  delay(250);
  esp_reset_reason_t rr = esp_reset_reason();
  Serial.printf("BOOT reset_reason=%d (%s) heap=%u minHeap=%u\n",
                (int)rr, resetReasonName(rr), ESP.getFreeHeap(), ESP.getMinFreeHeap());
  // Potlaceni verbose warning logu Bluetooth stacku (napr. lc_task ASSERT_WARN).
  // Chybove zpravy zustavaji povolene.
  esp_log_level_set("*", ESP_LOG_ERROR);

  btMutex = xSemaphoreCreateMutex();
  diagMutex = xSemaphoreCreateMutex();
  if (!btMutex || !diagMutex) {
    Serial.println("CHYBA: nelze vytvorit FreeRTOS mutex");
  }

  pinMode(TFT_BL, OUTPUT);
  applyBrightness(220);

#if USE_IGNITION_SENSE
  pinMode(IGNITION_PIN, INPUT);
#endif

  // V16.16 SPECIALS nema zadne hardwarove vystupy; vse jde pres ELM/J519.

  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.fillScreen(TFT_BLACK);

  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSPI);
  touch.setRotation(SCREEN_ROTATION);

  loadPreferences();
  applyBrightness(activeUiBrightness());

  initStorage();
  restoreTripCheckpoint();
  initWifi();

  showSplash();

  if (!SerialBT.begin(BT_LOCAL_NAME, true)) {
    tft.fillScreen(COL_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_RED, COL_BG);
    tft.setTextSize(2);
    tft.drawString("BLUETOOTH ERROR", 160, 120);
    while (true) delay(1000);
  }

  SerialBT.setPin(BT_PIN_1, 4);


  lastUserActivityMs = millis();

  // OBD polling bezi na druhem jadre. Timeout ELM327 uz neblokuje dotyk ani TFT.
  BaseType_t taskOk = xTaskCreatePinnedToCore(
    obdBackgroundTask,
    "OBD_Task",
    8192,
    nullptr,
    1,
    &obdTaskHandle,
    1
  );

  if (taskOk != pdPASS) {
    Serial.println("CHYBA: OBD task se nepodarilo vytvorit");
  }

  // Auto-connect na vychozi ELM. V15.2 pred pripojenim vzdy nabidne kratke
  // okno pro vyber jineho zarizeni, aby nebylo nutne cekat na timeout.
  if (autoElmReconnect && hasDefaultDevice && defaultMac.length() == 17) {
    pendingDeviceName = defaultName;
    pendingDeviceMac = defaultMac;
    int startChoice = offerOtherDeviceBeforeAutoConnect(defaultName, defaultMac);
    if (startChoice == 1) {
      startBtScan();
    } else if (startChoice == 2) {
      drawMainMenu();
    } else {
      connectUserChoseOffline = false;
      offlineMode = false;
      drawConnectingScreen();
      lastReconnectAttempt = millis();
      reconnectRequested = true;
    }
  } else if (hasDefaultDevice && defaultMac.length() == 17) {
    // Automaticke pripojeni je vypnute v NASTAVENI -> OBD / DATA.
    offlineMode = true;
    connectUserChoseOffline = true;
    drawMainMenu();
  } else {
    startBtScan();
  }
}

// ============================= LOOP ===========================

void loop() {
  handleSerialDiagnosticConsole();
  if (serviceWifiActive) server.handleClient();
  updateWifiState();
  serviceDeferredBluetoothResume();
  flushDeferredSettings(false);
  updateScheduledBrightness();

  // V15.7 runtime diagnostika. Jedna radka za 30 s je dost malo na to, aby
  // nezaspamovala Serial, ale pri dalsim resetu ukaze heap a stack trend.
  if (millis() - lastHealthLogMs >= 30000UL) {
    lastHealthLogMs = millis();
    UBaseType_t obdWater = obdTaskHandle ? uxTaskGetStackHighWaterMark(obdTaskHandle) : 0;
    UBaseType_t loopWater = uxTaskGetStackHighWaterMark(nullptr);
    Serial.printf("HEALTH mode=%s heap=%u minHeap=%u APclients=%u BT=%d ECU=%d obdStack=%u loopStack=%u\n",
                  serviceWifiActive ? "WEB" : "DRIVE", ESP.getFreeHeap(), ESP.getMinFreeHeap(), apClientCount,
                  car.elmConnected ? 1 : 0, car.ecuResponding ? 1 : 0,
                  (unsigned)obdWater, (unsigned)loopWater);
  }

  // aktivni BT scan + animace
  if (btScanning) {
    if (millis() - lastRadarFrame > 65) {
      lastRadarFrame = millis();
      drawRadar();
    }

    if (millis() - btScanStart >= BT_SCAN_TIME_MS) {
      btScanning = false;
      SerialBT.discoverAsyncStop();
      delay(150);
      loadScanResults();
      drawBtResults();
    }

    handleTouch();
    updatePowerManager();
    return;
  }

  // Dotyk obslouzime jeste PRED reconnect logikou. I kdyby nektery BT stack
  // pokus trval dele, posledni klepnuti se zpracuje okamzite.
  handleTouch();

  // SPECIALS sekvenci vykonava OBD task; hlavni UI zustava responzivni.
  updateSpecialsSequence();
  updateSpecialsDynamic();

  maintainBluetoothConnection();

  if (diagNeedsRedraw) {
    diagNeedsRedraw = false;
    if (currentScreen == SCREEN_DIAGNOSTICS) drawDiagnosticsScreen();
  }

  // OBD dotazy uz nebezi zde - obsluhuje je obdBackgroundTask na druhem jadre.
  // Main loop proto neni blokovan timeouty ELM327.
  // Rozjetou jizdu musime nechat dobehnout i po ztrate ELM. Po invalidaci
  // telemetrie je RPM=0 a 60s ochrana rozhodne, zda slo o skutecny konec jizdy.
  if (car.elmConnected || trip.active) {
    updateTrip();
  }
  updateFuelingDetection();
  sampleSensorHistory();

  static unsigned long lastDpfStateCheck = 0;
  if (millis() - lastDpfStateCheck > 1000UL) {
    lastDpfStateCheck = millis();
    readVagDpfData();
  }

  updateRegenSession();
  if (serviceResetArmed && millis() >= serviceResetArmUntil) {
    serviceResetArmed = false;
    serviceResetTarget = 0;
    if (currentScreen == SCREEN_SERVICE) drawServiceScreen();
  }

  updateAlerts();


  // Zadny fillScreen() v pravidelnem refreshi. Prekresluji se jen hodnoty.
  if (!alert.active && millis() - lastUiRefresh >= UI_REFRESH_MS) {
    lastUiRefresh = millis();

    if      (currentScreen == SCREEN_LIVE)       updateLiveDynamic();
    else if (currentScreen == SCREEN_DPF)        updateDpfDynamic();
    else if (currentScreen == SCREEN_ENGINE)     updateEngineDynamic();
    else if (currentScreen == SCREEN_SENSORS)    updateSensorsDynamic();
    else if (currentScreen == SCREEN_TURBO)      updateTurboDynamic();
    else if (currentScreen == SCREEN_ELECTRICAL) updateElectricalDynamic();
    else if (currentScreen == SCREEN_TRIPS)      updateTripsDynamic();
    else                                         updateHeaderStatus();
  }

  updatePowerManager();
  if (powerLowMode && displayOffAfterIgnition && !serviceWifiActive) delay(40);
}
