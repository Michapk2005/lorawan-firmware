#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <TinyGPS++.h>
#include <softSerial.h>
#include <EEPROM.h>
#define SLEEP_SWITCH_PIN GPIO1

/*
 * set LoraWan_RGB to Active,the RGB active in loraWan
 * RGB red means sending;
 * RGB purple means joined done;
 * RGB blue means RxWindow1;
 * RGB yellow means RxWindow2;
 * RGB green means received done;
 */

// Board-ID for internal analysis - PLEASE EDIT THIS VALUE FOR YOUR BOARD!
int boardID = 999; 

// --- Dummy OTAA Keys (Werden vom Compiler zwingend verlangt, auch bei ABP) ---
uint8_t devEui[] = { 0x0A, 0xE3, 0x1A, 0xFB, 0x09, 0x38, 0x03, 0x8F };    //Wert aus dem SWP Chirpstack
uint8_t appEui[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t appKey[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };


// --- ABP-Key-Management für zwei Netzwerke ---
// 1. TTN Keys
uint8_t nwkSKey_TTN[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF }; 
uint8_t appSKey_TTN[] = { 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00 };
uint32_t devAddr_TTN  = ( uint32_t )0x01234567;

// 2. ChirpStack Keys
uint8_t nwkSKey_CS[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB };
uint8_t appSKey_CS[] = { 0xCA, 0xFE, 0xBA, 0xBE, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x11, 0x22, 0x33, 0x44 };
uint32_t devAddr_CS  = ( uint32_t )0x89ABCDEF;


// Aktive Keys, die von der Library genutzt werden (werden in der Loop überschrieben)
uint8_t nwkSKey[16];
uint8_t appSKey[16];
uint32_t devAddr;

bool sendToTTN = false; // Toggle-Variable für den Netzwerkwechsel

// NEU: Zähler damit TTN nur jedes 2. Mal gesendet wird (alle 90s statt 45s)
int cycleCounter = 0;

// Speichern von Pings für Funklöcher:
int eepromAddr = 0; // Aktuelle Schreibadresse
const int MAX_EEPROM_ADDR = 512; // Das AB01 hat ca. 512-1024 Bytes emuliertes EEPROM. Wir nutzen 512 davon für ca. 50 Pings.

uint8_t deviceStatusBatteryLevel = 0;

/*LoraWan channelsmask, default channels 0-7*/ 
uint16_t userChannelsMask[6]={ 0x00FF,0x0000,0x0000,0x0000,0x0000,0x0000 };

/*LoraWan region, select in arduino IDE tools*/
LoRaMacRegion_t loraWanRegion = ACTIVE_REGION;

/*LoraWan Class, Class A and Class C are supported*/
DeviceClass_t  loraWanClass = LORAWAN_CLASS;

// ANGEPASST: 45s Intervall - CS sendet jeden Zyklus (alle 45s), TTN jeden 2. Zyklus (= alle 90s)
// Begründung: Duty Cycle (EU 1%) und TTN Fair Use Policy (30s/Tag bei geschätzt 7h Betrieb)
uint32_t appTxDutyCycle = 45000;

/*OTAA or ABP*/
bool overTheAirActivation = false; // Fix auf ABP gesetzt

/*ADR enable*/
bool loraWanAdr = LORAWAN_ADR;

/* ANGEPASST: MUSS false sein! Da wir den MAC-State alle 45s neu initiieren, würde true den Flash zerstören. */
bool keepNet = false; 

// ANGEPASST: Unconfirmed Mode - kein Retry, kein ACK-Downlink
// Begründung: Retries würden Duty Cycle und Fair Use Budget sprengen.
// Für ein Coverage-Mapping-Projekt ist Unconfirmed korrekt - ein verlorenes Paket ist die gesuchte Information.
bool isTxConfirmed = false;

/* Application port */
uint8_t appPort = 2;
uint8_t confirmedNbTrials = 2;

//GPS part
static const uint32_t GPSBaud = 9600;

// The TinyGPS++ object
TinyGPSPlus gps;

// Letzte GPS-Koordinate zum prüfen, ob man sich bewegt
long lastSentLonTTN = 0;
long lastSentLatTTN = 0;

long lastSentLonCS = 0;
long lastSentLatCS = 0;


// Struktur für die History
struct PingEntry {
  uint16_t counter;
  long lon;
  long lat;
};

// ANGEPASST: TTN History auf 8 Einträge, CS History auf 11 Einträge
// Begründung: Unterschiedliche Payload-Größen je nach Duty Cycle Budget pro Netzwerk
// TTN SF7: max. 83 Bytes → 1 + (8×10) + 2 = 83 Bytes
// CS  SF9 fest: 113 Bytes → 1 + (11×10) + 2 = 113 Bytes (SF9 Limit: 115 Bytes)
PingEntry historyTTN[8]; 
PingEntry historyCS[11];


// The serial connection to the GPS device
softSerial ss(GPIO5 /*RX pin*/, GPIO3 /*TX pin*/);

// Ping-Counter for internal analysis
int countTTN = 0; 
int countCS = 0; 


// This custom version of delay() ensures that the gps object
// is being "fed".
static void smartDelay(unsigned long ms)
{
  unsigned long start = millis();
  do 
  {
    while (ss.available())
      gps.encode(ss.read());
  } while (millis() - start < ms);
}

static void printFloat(float val, bool valid, int len, int prec)
{
  if (!valid)
  {
    while (len-- > 1)
      Serial.print('*');
    Serial.print(' ');
  }
  else
  {
    Serial.print(val, prec);
    int vi = abs((int)val);
    int flen = prec + (val < 0.0 ? 2 : 1); // . and -
    flen += vi >= 1000 ? 4 : vi >= 100 ? 3 : vi >= 10 ? 2 : 1;
    for (int i=flen; i<len; ++i)
      Serial.print(' ');
  }
  smartDelay(0);
}


// Save every ping in a ring-memory to the EEPROM for Funkloch-Upload
void savePingToEEPROM(uint16_t c, long lon, long lat) {
  if (eepromAddr + 11 > 500) {   
    eepromAddr = 0; //Start memory-adress from the beginning, when EEPROM-end is reached 
  }

  EEPROM.write(eepromAddr,     (uint8_t)(c >> 8));
  EEPROM.write(eepromAddr + 1, (uint8_t)(c & 0xFF));
  EEPROM.write(eepromAddr + 2, (uint8_t)(lon >> 24));
  EEPROM.write(eepromAddr + 3, (uint8_t)(lon >> 16));
  EEPROM.write(eepromAddr + 4, (uint8_t)(lon >> 8));
  EEPROM.write(eepromAddr + 5, (uint8_t)(lon & 0xFF));
  EEPROM.write(eepromAddr + 6, (uint8_t)(lat >> 24));
  EEPROM.write(eepromAddr + 7, (uint8_t)(lat >> 16));
  EEPROM.write(eepromAddr + 8, (uint8_t)(lat >> 8));
  EEPROM.write(eepromAddr + 9, (uint8_t)(lat & 0xFF));
  EEPROM.write(eepromAddr + 10, 0xFF);
  
  delay(50); 
  
  if(EEPROM.commit()) {
    Serial.print("Gespeichert bei Adresse: ");
    Serial.println(eepromAddr);
    eepromAddr += 11;
  } else {
    delay(100);
    if(EEPROM.commit()) {
       Serial.println("Gespeichert im 2. Versuch!");
       eepromAddr += 11;
    } else {
       Serial.println("Flash Hardware-Fehler.");
    }
  }
}

 //Calculates the distance between 2 gps coordinates
float getDistance(long lon1, long lat1, long lon2, long lat2) {
  double lat1Rad = lat1 * 0.0000000174532925;
  double lat2Rad = lat2 * 0.0000000174532925;
  double lon1Rad = lon1 * 0.0000000174532925;
  double lon2Rad = lon2 * 0.0000000174532925;

  float meanLat = (lat1Rad + lat2Rad) / 2.0;
  const float R = 6371000.0;

  float dx = (lon2Rad - lon1Rad) * cos(meanLat);
  float dy = (lat2Rad - lat1Rad);
  
  return sqrt(dx * dx + dy * dy) * R;
}

 //A ping is only saved to the EEPROM, if there is no other ping in a radius of 10 meters saved already. This is to maximize the spread of saved Funkloch-Pings.
bool isTooClose(long newLon, long newLat) {
  for (int i = 0; i < MAX_EEPROM_ADDR - 10; i += 11) {
    uint16_t c = (EEPROM.read(i) << 8) | EEPROM.read(i + 1);
    if (c == 0xFFFF) break; 

    long oldLon = ((long)EEPROM.read(i + 2) << 24) | ((long)EEPROM.read(i + 3) << 16) | 
                  ((long)EEPROM.read(i + 4) << 8) | (long)EEPROM.read(i + 5);
    long oldLat = ((long)EEPROM.read(i + 6) << 24) | ((long)EEPROM.read(i + 7) << 16) | 
                  ((long)EEPROM.read(i + 8) << 8) | (long)EEPROM.read(i + 9);

    if (getDistance(newLon, newLat, oldLon, oldLat) < 10.0) {
      return true; 
    }
  }
  return false;
}

//Gets new GPS-Coordinates and sends it via LoRaWAN, if the previous ping is more than 8m away.
static bool prepareTxFrame( uint8_t port ) 
{
  //Get new coordinate
  long laenge = gps.location.lng() * 1000000;
  long breite = gps.location.lat() * 1000000;

  //If the current ping is at least the fifth and not further than 9.0m away from the prior ping, it is canceled
  if (sendToTTN) {
    if (lastSentLonTTN != 0 && lastSentLatTTN != 0 && countTTN > 4 && laenge != 0 && breite != 0) {
      float distance = getDistance(laenge, breite, lastSentLonTTN, lastSentLatTTN);  
      if (distance < 9.0) {
        Serial.println();
        Serial.print(F("Bewegung TTN ("));
        Serial.print(distance, 1);
        Serial.println(F("m) < 9m -> Sendevorgang unterdrückt."));
        return false; 
      }
    }
  } else {
    if (lastSentLonCS != 0 && lastSentLatCS != 0 && countCS > 4 && laenge != 0 && breite != 0) {
      float distance = getDistance(laenge, breite, lastSentLonCS, lastSentLatCS);  
      if (distance < 9.0) {
        Serial.println();
        Serial.print(F("Bewegung CS("));
        Serial.print(distance, 1);
        Serial.println(F("m) < 9m -> Sendevorgang unterdrückt."));
        return false; 
      }
    }
  }

  
  //From this point on a ping will be send via LoRaWAN:

  // ANGEPASST: History-Arrays haben unterschiedliche Größen (TTN: 8, CS: 11)
  if (laenge != 0 && breite != 0){
    if (sendToTTN) {
      for (int i = 7; i > 0; i--) historyTTN[i] = historyTTN[i-1];
      historyTTN[0] = { (uint16_t)countTTN, laenge, breite };
    } else {
      for (int i = 10; i > 0; i--) historyCS[i] = historyCS[i-1];
      historyCS[0] = { (uint16_t)countCS, laenge, breite };
    }
  } 
  
  //Debugging
  Serial.println();
  if (sendToTTN){
    Serial.println("Sende Daten an TTN...");
    Serial.print("Counter: ");
    Serial.println(countTTN);    
  } else {
    Serial.println("Sende Daten an CS...");
    Serial.print("Counter: ");
    Serial.println(countCS);
  }
  printFloat(gps.location.lat(), gps.location.isValid(), 11, 6);
  printFloat(gps.location.lng(), gps.location.isValid(), 12, 6);
  Serial.println();

  deviceStatusBatteryLevel = map(getBatteryVoltage(), 3300, 4200, 1, 254); //Für TTN und Chirpstack Batteriewert 1-254
  uint16_t currentBattery = getBatteryVoltage(); // Batteriewert für Payload

  // ANGEPASST: Unterschiedliche Payload-Größen je nach Netzwerk
  // TTN: 1 + (8 × 10) + 2 = 83 Bytes  → SF7 konform
  // CS:  1 + (11 × 10) + 2 = 113 Bytes → SF9 konform (SF9 Limit: 115 Bytes)
  if (sendToTTN) {
    appDataSize = 83;
  } else {
    appDataSize = 113;
  }

  appData[0] = (uint8_t)(boardID & 0xFF);

  // ANGEPASST: Loop-Länge je nach Netzwerk (8 für TTN, 11 für CS)
  if (sendToTTN) {
    for (int i = 0; i < 8; i++) {
      int base = 1 + (i * 10);
      appData[base]     = historyTTN[i].counter >> 8;
      appData[base + 1] = historyTTN[i].counter & 0xFF;
      appData[base + 2] = (uint32_t)historyTTN[i].lon;
      appData[base + 3] = (uint32_t)historyTTN[i].lon >> 8;
      appData[base + 4] = (uint32_t)historyTTN[i].lon >> 16;
      appData[base + 5] = (uint32_t)historyTTN[i].lon >> 24;
      appData[base + 6] = (uint32_t)historyTTN[i].lat;
      appData[base + 7] = (uint32_t)historyTTN[i].lat >> 8;
      appData[base + 8] = (uint32_t)historyTTN[i].lat >> 16;
      appData[base + 9] = (uint32_t)historyTTN[i].lat >> 24;
    }
    appData[81] = (uint8_t)(currentBattery >> 8);
    appData[82] = (uint8_t)(currentBattery & 0xFF);
  } else {
    for (int i = 0; i < 11; i++) {
      int base = 1 + (i * 10);
      appData[base]     = historyCS[i].counter >> 8;
      appData[base + 1] = historyCS[i].counter & 0xFF;
      appData[base + 2] = (uint32_t)historyCS[i].lon;
      appData[base + 3] = (uint32_t)historyCS[i].lon >> 8;
      appData[base + 4] = (uint32_t)historyCS[i].lon >> 16;
      appData[base + 5] = (uint32_t)historyCS[i].lon >> 24;
      appData[base + 6] = (uint32_t)historyCS[i].lat;
      appData[base + 7] = (uint32_t)historyCS[i].lat >> 8;
      appData[base + 8] = (uint32_t)historyCS[i].lat >> 16;
      appData[base + 9] = (uint32_t)historyCS[i].lat >> 24;
    }
    appData[111] = (uint8_t)(currentBattery >> 8);
    appData[112] = (uint8_t)(currentBattery & 0xFF);
  }
  
  // Only save TTN Pings for upload
  if (laenge != 0 && breite != 0 && sendToTTN) {      
    if (!isTooClose(laenge, breite)) {
      savePingToEEPROM((uint16_t)countTTN, laenge, breite);
      Serial.println("Punkt neu -> Im EEPROM gesichert (10m Check OK).");
    } else {
      Serial.println("Punkt im EEPROM bekannt -> Nicht gespeichert.");
    }
  }
  
  if (sendToTTN) {
    lastSentLonTTN = laenge;
    lastSentLatTTN = breite;
    countTTN = countTTN + 1;
  } else {
    lastSentLonCS = laenge;
    lastSentLatCS = breite;
    countCS = countCS + 1;
  }
  
  return true;
}


// Function can be called by writing 'd' into the serial monitor in the first 15 seconds of booting the board. 
// dumpEEPORM writes the saved pings out for the Funkloch-Upload
void dumpEEPROM() {
  Serial.println("--- START EEPROM DUMP (CSV) ---");
  Serial.print("BoardID:"); 
  Serial.println(boardID); 
  Serial.println("Counter;Longitude;Latitude");
  
  for (int i = 0; i < MAX_EEPROM_ADDR - 10; i += 11) {
    uint16_t c = (EEPROM.read(i) << 8) | EEPROM.read(i + 1);
    
    long lon = ((long)EEPROM.read(i + 2) << 24) | ((long)EEPROM.read(i + 3) << 16) | 
               ((long)EEPROM.read(i + 4) << 8) | (long)EEPROM.read(i + 5);
               
    long lat = ((long)EEPROM.read(i + 6) << 24) | ((long)EEPROM.read(i + 7) << 16) | 
               ((long)EEPROM.read(i + 8) << 8) | (long)EEPROM.read(i + 9);
    
    if (c == 0xFFFF) continue; 

    Serial.print(c); Serial.print(";");
    Serial.print(lon / 1000000.0, 6); Serial.print(";");
    Serial.println(lat / 1000000.0, 6);
  }
  Serial.println("--- END DUMP ---");
}


// Helper: Loggt den MCPS-Status im Klartext
void logMcpsStatus(const char* netzwerk, LoRaMacStatus_t status) {
  Serial.print("MCPS Status ");
  Serial.print(netzwerk);
  Serial.print(": ");
  Serial.print(status);
  Serial.print(" -> ");
  switch (status) {
    case LORAMAC_STATUS_OK:
      Serial.println("OK, Paket in Sende-Queue.");
      break;
    case LORAMAC_STATUS_BUSY:
      Serial.println("FEHLER: MAC-Stack noch busy.");
      break;
    case LORAMAC_STATUS_DUTYCYCLE_RESTRICTED:
      Serial.println("FEHLER: Duty-Cycle blockiert.");
      break;
    case LORAMAC_STATUS_LENGTH_ERROR:
      Serial.println("FEHLER: Payload zu gross.");
      break;
    default:
      Serial.println("FEHLER: Unbekannter Code.");
      break;
  }
}


// Setup function which is called when the Heltec-Board boots up
void setup() {
  Serial.begin(115200);
  // Pin mit internem Widerstand auf HIGH setzen
  pinMode(SLEEP_SWITCH_PIN, INPUT_PULLUP);
  digitalWrite(Vext, LOW);
  EEPROM.begin(MAX_EEPROM_ADDR);

  Serial.println("Sende 'd' innerhalb von 15s, um den Speicher auszulesen...");
  unsigned long startWait = millis();
  while (millis() - startWait < 15000) {
    if (Serial.available() > 0) {
      char c = Serial.read();
      if (c == 'd') {
        dumpEEPROM();
      }
    }
  }

  // Akkustand beim Booten auslesen
  uint16_t bootVoltage = getBatteryVoltage();
  float voltageV = bootVoltage / 1000.0;
  
  Serial.println("--- Boot Vorgang ---");
  Serial.print("Akkuspannung: ");
  Serial.print(voltageV);
  Serial.println(" V");

  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
  delay(500);
  
  ss.begin(GPSBaud);

#if(AT_SUPPORT)
  enableAt();
#endif

  // Initialisiere mit CS Keys für den ersten Durchlauf (CS sendet zuerst)
  memcpy(nwkSKey, nwkSKey_CS, 16);
  memcpy(appSKey, appSKey_CS, 16);
  devAddr = devAddr_CS;

  deviceState = DEVICE_STATE_INIT;
  LoRaWAN.ifskipjoin();
}

bool isAwake = false;
 
void loop()
{
  // =========================================================================
  // FIX 1: GPIO-Pullup NACH JEDEM Deep-Sleep-Aufwachen neu konfigurieren.
  // Der CubeCell AB01 verliert die GPIO-Config im Deep Sleep.
  // Ohne das floatet GPIO1 und digitalRead() liefert zufällige Werte.
  // =========================================================================
  pinMode(SLEEP_SWITCH_PIN, INPUT_PULLUP);
  delayMicroseconds(200); // Pullup stabilisieren lassen
 
  // Schalter-Logik:
  //   Gedrückt (geschlossen) = GPIO1 mit GND verbunden = LOW  → Board AN
  //   Losgelassen (offen)    = Pullup zieht auf HIGH           → Board AUS / Schlafen
  bool schalterGedrueckt = (digitalRead(SLEEP_SWITCH_PIN) == LOW);
 
  // --- SCHLAF-MODUS (Schalter offen / Pin HIGH) ---
  if (!schalterGedrueckt) {
    digitalWrite(Vext, HIGH);
    if (isAwake) {
      pinMode(GPIO5, INPUT);
      pinMode(GPIO3, INPUT);
      isAwake = false;
      Serial.println("Schalter AUS: Gehe schlafen...");
      Serial.flush();
    }

    if (deviceState == DEVICE_STATE_SLEEP) {
      LoRaWAN.cycle(appTxDutyCycle);
      LoRaWAN.sleep();
    } else {
      delay(appTxDutyCycle);
    }
    return;
  }
 
  // --- AUFWACH-PHASE (Schalter gedrückt / Pin LOW) ---
  digitalWrite(Vext, LOW);

  // ANGEPASST: ss.begin() nur beim echten Aufwachen aufrufen, nicht bei jedem Loop-Durchlauf.
  // Verhindert unnötigen UART-Reset und Buffer-Verlust bei laufendem GPS.
  if (!isAwake) {
    ss.begin(GPSBaud);
    Serial.println("Schalter AN: Initialisiere System einmalig...");
    delay(2000);                // GPS-Modul braucht Anlaufzeit beim Kaltstart
    isAwake = true;
    deviceState = DEVICE_STATE_INIT;
    cycleCounter = 0;           // Zähler beim Aufwachen zurücksetzen
  }
 
  // --- NORMALER BETRIEB ---
  delay(400);
  smartDelay(750);
 
  switch( deviceState )
  {
    case DEVICE_STATE_INIT:
    {
#if(LORAWAN_DEVEUI_AUTO)
      LoRaWAN.generateDeveuiByChipID();
#endif
#if(AT_SUPPORT)
      getDevParam();
#endif
      printDevParam();
      LoRaWAN.init(loraWanClass,loraWanRegion);
      deviceState = DEVICE_STATE_JOIN;
      break;
    }
    case DEVICE_STATE_JOIN:
    {
      LoRaWAN.join();
      break;
    }
    case DEVICE_STATE_SEND:
    {
      cycleCounter++;
      bool doSendTTN = (cycleCounter % 2 == 0);

      MibRequestConfirm_t mibReq;

      // =============================================================
      // SCHRITT 1: CS sendet IMMER (jeden Zyklus = alle 45s)
      // =============================================================
      sendToTTN = false;

      // Device Address -> CS
      mibReq.Type = MIB_DEV_ADDR;
      mibReq.Param.DevAddr = devAddr_CS;
      LoRaMacMibSetRequestConfirm(&mibReq);

      // Network Session Key -> CS
      mibReq.Type = MIB_NWK_SKEY;
      mibReq.Param.NwkSKey = nwkSKey_CS;
      LoRaMacMibSetRequestConfirm(&mibReq);

      // Application Session Key -> CS
      mibReq.Type = MIB_APP_SKEY;
      mibReq.Param.AppSKey = appSKey_CS;
      LoRaMacMibSetRequestConfirm(&mibReq);

      // SF9 (DR_3) fest für CS, ADR deaktiviert
      mibReq.Type = MIB_CHANNELS_DATARATE;
      mibReq.Param.ChannelsDatarate = DR_3;
      LoRaMacMibSetRequestConfirm(&mibReq);

      mibReq.Type = MIB_CHANNELS_DEFAULT_DATARATE;
      mibReq.Param.ChannelsDefaultDatarate = DR_3;
      LoRaMacMibSetRequestConfirm(&mibReq);

      mibReq.Type = MIB_ADR;
      mibReq.Param.AdrEnable = false;
      LoRaMacMibSetRequestConfirm(&mibReq);

      Serial.println("\n>> MAPPING: ChirpStack (SF9 fest, kein ADR) <<");

      if (prepareTxFrame(appPort)) {
        McpsReq_t mcpsReq;
        mcpsReq.Type = MCPS_UNCONFIRMED;
        mcpsReq.Req.Unconfirmed.fPort = appPort;
        mcpsReq.Req.Unconfirmed.fBuffer = appData;
        mcpsReq.Req.Unconfirmed.fBufferSize = appDataSize;
        mcpsReq.Req.Unconfirmed.Datarate = DR_3; // SF9 direkt
        LoRaMacStatus_t statusCS = LoRaMacMcpsRequest(&mcpsReq);
        logMcpsStatus("CS", statusCS);
      }

      // =============================================================
      // SCHRITT 2: TTN sendet nur jeden 2. Zyklus (alle 90s)
      // Pause dazwischen, damit RX-Windows vom CS-Paket abgeschlossen sind
      // =============================================================
      if (doSendTTN) {
        // Warte damit der MAC-Stack das CS-Paket abschließen kann
        // (TX + RX1 + RX2 dauert ca. 2-3s bei SF9)
        delay(3000);

        sendToTTN = true;

        // Device Address -> TTN
        mibReq.Type = MIB_DEV_ADDR;
        mibReq.Param.DevAddr = devAddr_TTN;
        LoRaMacMibSetRequestConfirm(&mibReq);

        // Network Session Key -> TTN
        mibReq.Type = MIB_NWK_SKEY;
        mibReq.Param.NwkSKey = nwkSKey_TTN;
        LoRaMacMibSetRequestConfirm(&mibReq);

        // Application Session Key -> TTN
        mibReq.Type = MIB_APP_SKEY;
        mibReq.Param.AppSKey = appSKey_TTN;
        LoRaMacMibSetRequestConfirm(&mibReq);

        // SF7 (DR_5) fest für TTN, ADR deaktiviert
        mibReq.Type = MIB_CHANNELS_DATARATE;
        mibReq.Param.ChannelsDatarate = DR_5;
        LoRaMacMibSetRequestConfirm(&mibReq);

        mibReq.Type = MIB_CHANNELS_DEFAULT_DATARATE;
        mibReq.Param.ChannelsDefaultDatarate = DR_5;
        LoRaMacMibSetRequestConfirm(&mibReq);

        mibReq.Type = MIB_ADR;
        mibReq.Param.AdrEnable = false;
        LoRaMacMibSetRequestConfirm(&mibReq);

        Serial.println("\n>> MAPPING: TTN (SF7 fest, kein ADR) <<");

        if (prepareTxFrame(appPort)) {
          McpsReq_t mcpsReq;
          mcpsReq.Type = MCPS_UNCONFIRMED;
          mcpsReq.Req.Unconfirmed.fPort = appPort;
          mcpsReq.Req.Unconfirmed.fBuffer = appData;
          mcpsReq.Req.Unconfirmed.fBufferSize = appDataSize;
          mcpsReq.Req.Unconfirmed.Datarate = DR_5; // SF7 direkt
          LoRaMacStatus_t statusTTN = LoRaMacMcpsRequest(&mcpsReq);
          logMcpsStatus("TTN", statusTTN);
        }
      }

      deviceState = DEVICE_STATE_CYCLE;
      break;
    }
    case DEVICE_STATE_CYCLE:
    {
      txDutyCycleTime = appTxDutyCycle + randr( 0, APP_TX_DUTYCYCLE_RND );
      LoRaWAN.cycle(txDutyCycleTime);
      deviceState = DEVICE_STATE_SLEEP;
      break;
    }
    case DEVICE_STATE_SLEEP:
    {
      LoRaWAN.sleep();
      break;
    }
    default:
    {
      deviceState = DEVICE_STATE_INIT;
      break;
    }
  }
}