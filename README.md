# ELM-327-ESP-32-2432S028R---Skoda-Octavia-II-Custom-computer-
Custom Computer / Vlastní palubní počítač 

Obsah
1 Architektura a hlavní režimy
2 Hlavní menu - JÍZDA
3 DPF PRO a historie regenerací
4 Motor, turbo a elektrika
5 Kniha jízd
6 Historie tankování
7 Diagnostika vozidla
8 Servis a alarmy
9 Power Manager
10 Wi-Fi / servisní web
11 Nastavení, čas a personalizace
12 Bluetooth a ELM327
13 Ukládání dat a ochrany
14 Limity a co firmware záměrně nedělá LUKE | OCTAVIA II FW 16.5
Strana 2
1. Architektura a hlavní režimy
Firmware pracuje ve dvou oddělených režimech, aby se Wi-Fi a Bluetooth vzájemně nerušily:
Režim Aktivní rozhraní Účel
JÍZDA Bluetooth Classic + ELM327 Živá data z ECU, DPF, jízdy, 
diagnostika, alarmy.
SERVISNÍ WEB Wi-Fi AP, Bluetooth je dočasně 
vypnutý
Historie jízd a tankování, exporty, 
nastavení, informace a servisní 
konfigurace.
 Po úspěšném připojení ELM327 se automaticky otevře obrazovka PŘEHLED.
 Firmware průběžně hlídá odpojení ELM a zkouší automatický reconnect.
 Zastaralé hodnoty po výpadku komunikace nejsou vydávány za aktuální; neplatná data se přepnou na 
--.
2. Hlavní menu - JÍZDA
Položka Co zobrazuje
PŘEHLED Otáčkoměr 0-6000 rpm, teplota motoru, tlak turba, 
nádrž %, odhad dojezdu a venkovní teplota.
DPF PRO Saze, stav regenerace, odhad do regenerace, diferenční 
tlak a km od poslední regenerace.
MOTOR RPM, chladicí kapalina, zatížení motoru, MAF, teplota 
sání, MAP a tlak palivové rampy.
TURBO Analogový ukazatel plnicího tlaku, MAP absolutní, BARO 
a RPM.
KNIHA JÍZD Aktuální jízda a poslední uložené jízdy; kompletní 
historie je ve webu.
DALŠÍ Otevře druhou stránku FUNKCE.
PŘEHLED
 RPM má vlastní funkční mini otáčkoměr s pohyblivou ručičkou.
 Odhad dojezdu používá skutečnou spotřebu aktuální jízdy. Když aktuální jízda nemá dost dat, může 
použít spotřebu od poslední DPF regenerace.
 Pokud ECU některý PID neposkytuje, zobrazí se -- místo vymyšlené hodnoty.
 Venkovní teplota se čte přes SAE PID 01 46, pokud jej ECU podporuje.
3. DPF PRO a historie regenerací
DPF část je přizpůsobena motoru CAYC / ECU PCR2.1 a používá potvrzené VAG diagnostické hodnoty tam, 
kde byly pro tento software ověřeny.
Hodnota Zdroj / význam
Vypočítané saze VAG DID 22 208B - hlavní hodnota pro indikaci zanesení. LUKE | OCTAVIA II FW 16.5
Strana 3
Hodnota Zdroj / význam
Naměřené saze VAG DID 22 208C.
Km od poslední regenerace VAG DID 22 208D.
Palivo od poslední regenerace VAG DID 22 20D8.
Stav / čítač regenerace VAG DID 22 207D; chování při aktivní regeneraci je dále 
sledováno.
Diferenční tlak DPF Standardní PID 01 7A, pouze pokud jej konkrétní ECU 
vrací v očekávaném formátu.
 Hlavní DPF budík ukazuje procenta vůči nastavitelnému referenčnímu limitu; výchozí limit je 24,0 g.
 Obrazovka ukazuje AKTUÁLNÍ / MAX, odhad km do regenerace, diferenční tlak, zanesení sazemi a km 
od poslední regenerace.
 Historie regenerací se ukládá do LittleFS a graf zobrazuje poslední intervaly mezi regeneracemi.
 Při začátku a dokončení regenerace může vyskočit upozornění.
 ZANESENÍ SAZEMI není hodnota popela; firmware záměrně neprezentuje neověřený scaling popela 
jako jistou hodnotu.
4. Motor, turbo a elektrika
MOTOR
 Otáčky motoru (RPM).
 Teplota chladicí kapaliny.
 Zatížení motoru v %.
 MAF - hmotnostní průtok vzduchu v g/s.
 Teplota nasávaného vzduchu.
 MAP - absolutní tlak v sání.
 Rail pressure - tlak paliva v MPa.
TURBO
 Plnicí tlak je vypočten jako MAP minus barometrický tlak.
 Když není BARO dostupné, firmware použije orientačních 100 kPa.
 Budík je rozsahu přibližně 0 až 2 bar a zvýrazní vysoký tlak.
 Vedle budíku se zobrazuje MAP, BARO a RPM.
ELEKTRIKA
 Preferuje napětí palubní sítě měřené ECU přes PID 01 42; pokud není dostupné, používá napájecí 
napětí ELM jako fallback.
 Při stojícím motoru hodnotí stav baterie, při běžícím motoru stejnou 12V síť interpretuje jako dobíjecí 
napětí alternátoru.
 Stavy zahrnují BATERIE OK, BATERIE SLABŠÍ, NÍZKÉ NAPĚTÍ, DOBÍJENÍ OK, SLABÉ DOBÍJENÍ a PŘEPĚTÍ.
5. Kniha jízd
 Automatické zahájení a ukončení jízdy podle aktuálních dat motoru/vozidla.
 Ukládá začátek, konec, vzdálenost, spotřebované palivo, průměrnou spotřebu, maximální rychlost, 
průměrnou rychlost a dobu.
 Palivo se u CAYC počítá z rozdílu potvrzeného kumulativního VAG DID 20D8; standardní PID 01 5E u 
této ECU vrací NO DATA. LUKE | OCTAVIA II FW 16.5
Strana 4
 60 sekund po zastavení slouží jen jako debounce, aby krátké stop/start nerozdělilo jednu jízdu. Tato 
minuta se nepřičítá k uložené době.
 Na displeji jsou dvě poslední uložené jízdy. Detailní historie je v servisním webu.
 Web umí PDF souhrn všech jízd, PDF jedné konkrétní jízdy a CSV.
Soubor Obsah
/trips_v2.csv Aktuální formát knihy jízd s kompletními údaji.
/trips.csv Doplňkový soubor knihy jízd používaný firmwarem.
6. Historie tankování
 HISTORIE TANKOVÁNÍ je dostupná na stránce FUNKCE.
 Zdroj hladiny paliva je standardní OBD-II PID 01 2F (Fuel Tank Level Input).
 Výchozí minimální nárůst pro záznam je +4,0 l; ve webu lze nastavit 2 až 12 l.
 Aby se neuložil pohyb plováku v zatáčce nebo na svahu, změna musí být potvrzena nejméně 3 vzorky 
a trvat alespoň 25 sekund.
 Zapisuje se datum/čas, stav km, hladina před, hladina po a odhad přidaných litrů.
 Na displeji jsou poslední čtyři záznamy; ve webu je celá historie a CSV export.
 Celkový stav tachometru se jednou zadá ve webu a potom se navyšuje podle vypočítané ujeté 
vzdálenosti.
Poznámka k přesnosti
Litry jsou odhad z procenta plováku a nastavené kapacity nádrže. Nejde o měřidlo množství načerpaného 
paliva z čerpacího stojanu.
7. Diagnostika vozidla
Diagnostika je pouze čtecí. Umí scan nakonfigurovaných jednotek a zobrazení chybových kódů, ale nic v 
autě nemaže ani nekóduje.
 01 Motor
 02 Převodovka
 03 ABS / ESP
 08 Klimatizace
 09 Centrální elektrika
 10 Parkovací asistent
 15 Airbag
 16 Volant
 17 Přístroje
 19 CAN Gateway
 25 Imobilizer
 42 Dveře řidiče
 44 Posilovač řízení
 46 Komfort
 52 Dveře spolujezdce
 55 Naklápění světel
 56 Rádio
 62 Zadní levé dveře
 69 Tažné zařízení
 72 Zadní pravé dveře
 77 Telefon LUKE | OCTAVIA II FW 16.5
Strana 5
Omezení ELM327
Levný ELM327 není VCDS/ODIS. Některé starší PQ35 jednotky používající KWP/TP2.0 nemusí být přes použitý 
UDS/OBD přístup dostupné. „Bez odpovědi“ proto automaticky neznamená vadnou jednotku.
8. Servis a alarmy
SERVIS
Položka Výchozí interval Co se sleduje
Olej / filtr 15 000 km nebo 365 dní Kilometry a čas od posledního 
ručního resetu.
Inspekce 30 000 km nebo 730 dní Kilometry a čas od posledního 
ručního resetu.
Reset vyžaduje druhé potvrzení. Kilometry servisního počítadla se počítají jen v době, kdy monitor běží.
ALARMY
Alarm Výchozí podmínka
Teplota motoru 105 °C a více.
Nízké napětí Při běžícím motoru méně než 12,2 V.
Přepětí Při běžícím motoru více než 15,2 V.
DPF regenerace Začátek / dokončení aktivní regenerace.
DPF do 10 km Odhad do další regenerace <= 10 km.
ELM odpojení Potvrzená ztráta Bluetooth linku; firmware následně 
zkouší reconnect.
Každý alarm lze samostatně zapnout nebo vypnout a nastavení se ukládá do Preferences.
9. Power Manager
 Ve výchozím nastavení není použit samostatný vodič zapalování; stav zapalování se odvozuje z 
aktuální odpovědi ECU/ELM.
 Automatické ztlumení displeje je výchozím nastavením zapnuté. Základní zpoždění je 120 sekund a lze
ho měnit v rozsahu 30 až 600 sekund.
 Pokud by se později zapnul USE_IGNITION_SENSE a přivedl signál na GPIO27, firmware umí po vypnutí 
zapalování přejít do deep sleep a probudit se externím signálem.
 Ve výchozím nastavení je USE_IGNITION_SENSE = 0, takže deep sleep přes GPIO není aktivní.
10. Wi-Fi / servisní web
Položka Výchozí hodnota
SSID Luke-Octavia-II LUKE | OCTAVIA II FW 16.5
Strana 6
Položka Výchozí hodnota
Heslo Octavia2
Adresa webu http://192.168.4.1
Health test http://192.168.4.1/health
 Web se zapíná ručně ze SYSTEM -> WI-FI / WEB.
 Po zapnutí se Bluetooth/ELM dočasně odpojí. Po vypnutí webu se Bluetooth znovu spustí.
 Web zobrazuje historii jízd a tankování.
 Exportuje trips_v2.csv a fueling.csv.
 Generuje PDF přehled jízd a PDF detail jedné jízdy přímo v ESP32.
 Umožní změnit DPF limit, kapacitu nádrže, práh detekce tankování a aktuální stav tachometru.
 Umožní upravit úvodní text, text záhlaví a kontakty programátora.
 Tlačítko synchronizace z telefonu slouží hlavně pro datum.
 Web je úmyslně „LITE“ - živá DPF data, Live Data a diagnostika byly z webu odstraněny kvůli stabilitě.
11. Nastavení, čas a personalizace
Nastavení Možnosti
Bluetooth / výchozí ELM Změna výchozího adaptéru přes scan.
Jas displeje Ruční +/-.
DPF limit pro 100 % 15 až 45 g; výchozí 24 g.
Kapacita nádrže 35 až 80 l; výchozí 55 l.
Rychlá data RPM / Turbo Zapnout / vypnout.
Automatické ztlumení Zapnout / vypnout.
Čas / zdroj OBD přístrojový štít nebo ruční webová synchronizace.
Úvodní text Měnitelný přes web.
Text hlavičky Měnitelný přes web.
 Firmware zkouší číst HH:MM z přístrojového štítu J285 přes adresu 17, CAN TX 0x714 / RX 0x77E a UDS
ReadDataByIdentifier 0x2216.
 Po úspěchu se OBD čas načte po spojení a potom nejvýše jednou za 5 minut; mezi tím se dopočítává 
lokálně.
 Ověřený DID poskytuje hodiny a minuty, ne potvrzené datum. Datum se proto synchronizuje z 
telefonu přes servisní web.
 Úvodní obrazovka obsahuje ŠKODA logo a uživatelsky měnitelný text.
12. Bluetooth a ELM327
Vlastnost Aktuální nastavení
Technologie Bluetooth Classic SPP.
Výchozí jméno OBDII LUKE | OCTAVIA II FW 16.5
Strana 7
Vlastnost Aktuální nastavení
Výchozí MAC 19:05:20:02:6e:68
Párovací PIN Zkouší 1234, při neúspěchu i 0000.
Protokol motoru ISO 15765-4 CAN 11-bit, 500 kbit/s; ELM protokol 6.
Režim bez adaptéru OFFLINE je dostupný ještě před automatickým 
připojením i během scanování.
13. Ukládání dat a ochrany
 LittleFS uchovává knihy jízd, historii DPF regenerací a tankování.
 Preferences/NVS uchovává nastavení, servisní počítadla, alarmy, výchozí ELM, texty, odometr a další 
konfiguraci.
 Při běžné chybě mountu LittleFS firmware automaticky neformátuje úložiště, aby se historie 
nesmazala.
14. Limity a co firmware záměrně nedělá
 Nenahrazuje homologovaný přístrojový štít ani profesionální diagnostiku.
 Nemaže DTC, nekóduje jednotky a neprovádí Security Access.
 Ne všechny standardní OBD PIDy musí ECU CAYC podporovat. Chybějící hodnoty se zobrazí jako --.
 DPF „zanesení“ je založené na sazích, nikoli na popelu.
 Celkový odometr není čten univerzálním OBD PIDem; aktuální km se jednou zadá ve webu a dále se 
průběžně dopočítává.
 Historie tankování pracuje s odhadem hladiny z plováku, proto se může lišit od množství na účtence.
 Wi-Fi a Bluetooth ELM neběží současně; je to záměr kvůli stabilitě.
