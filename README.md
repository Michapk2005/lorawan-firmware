# Lorawan-Firmware

Firmware für [Heltec CubeCell HTCC-AB01](https://heltec.org/project/htcc-ab01/) Boards zur LoRaWAN-Netzwerkvermessung im Rahmen eines Smart-City-Projektes mit der **Landeshauptstadt Potsdam**. Geschrieben von Frank Hübner und Michael Vesper.

---

## Inhalt

- [Hardware](#hardware)
- [Firmware-Varianten](#firmware-varianten)
- [Funktionsweise](#funktionsweise)
- [Einrichtung der Arduino IDE](#einrichtung-der-arduino-ide)
- [Keys eintragen & Geräte anlegen](#keys-eintragen--geräte-anlegen)
- [Board flashen](#board-flashen)

---

## Hardware

### Pflichtkomponenten

| Komponente | Beschreibung |
|---|---|
| Heltec CubeCell HTCC-AB01 | Mikrocontroller mit integriertem LoRa-Chip (SX1262) |
| NEO-6M GPS-Modul | GPS-Empfänger zur Positionsbestimmung (UART, 9600 Baud) |

### Optionale Komponenten

| Komponente | Beschreibung |
|---|---|
| LiPo-Akku | Passender Akku für das HTCC-AB01 (JST 1.25mm, 3.7V) für mobilen Betrieb |
| Schalter / Druckknopf | Ermöglicht das manuelle An- und Ausschalten des Boards ohne Stromtrennung – siehe [Firmware-Varianten](#firmware-varianten) |

---

## Firmware-Varianten

Im Repository liegen zwei `.ino`-Dateien für die Arduino IDE:

### `lorawan_gps_messgeraet_firmware_ohne_knopf.ino`
Das Board ist aktiv, solange es mit Strom versorgt wird. Kein Schalter erforderlich. Geeignet für Dauerbetrieb z.B. an einer Powerbank oder USB-Stromversorgung.

### `lorawan_gps_messgeraet_firmware_mit_knopf.ino`
Ermöglicht das Ein- und Ausschalten des Boards über einen Hardware-Schalter oder Druckknopf. Der Schalter wird zwischen **GPIO1** und **GND** angeschlossen und über den internen Pullup-Widerstand ausgelesen:
```
Board GPIO1 ──── Schalter ──── GND
```

- Schalter **geschlossen** (GPIO1 = LOW) → Board aktiv, GPS läuft, Pings werden gesendet  
- Schalter **offen** (GPIO1 = HIGH) → Board schläft, GPS stromlos, minimaler Stromverbrauch

> **Hinweis:** Der GPIO-Pullup wird nach jedem Deep-Sleep-Aufwachen neu konfiguriert, da der CubeCell AB01 die GPIO-Konfiguration im Deep Sleep verliert.

---

## Funktionsweise

Jedes Messgerät sendet abwechselnd Pings an zwei LoRaWAN-Netzwerke: **The Things Network (TTN)** und einen **ChirpStack**-Server. Dabei wird alle 45 Sekunden an den Chirpstack-Server gesendet und alle 90 Sekunden an TTN. Dies hängt mit dem europäischen Duty Cycle und der TTN [Fair Use Policy](https://www.thethingsnetwork.org/forum/t/fair-use-policy-explained/1300) zusammen.

Jeder Ping enthält:
- **Board-ID** zur Identifikation des Geräts
- **GPS-Koordinaten** (aktuelle Position + vorherigen Pings als Historie zum finden von Funklöchern)
- **Akkuspannung** in mV

Anhand der empfangenen Pings und der am Gateway gemessenen Signalstärke (RSSI/SNR) lässt sich die LoRaWAN-Netzabdeckung flächendeckend kartieren.

### Bewegungsschwelle

Um stationäre Dauerpings (z.B. beim Liegen auf einem Schreibtisch) zu vermeiden und Indoor-Messungen zu unterdrücken, wird ein Ping erst gesendet, wenn das Gerät sich seit dem letzten Ping **mindestens 10 Meter** fortbewegt hat. Die ersten 4 Pings nach dem Einschalten werden ohne Bewegungsprüfung gesendet, damit das Gerät nach dem Start korrekt initialisiert wird und man dies auch ohne GPS-Daten im TTN / Chirpstack Dashboard einsehen kann.

### EEPROM-Pufferung für Funklöcher

Jeder TTN-Ping wird zusätzlich im internen EEPROM des Boards gespeichert (Ringpuffer, ~50 Einträge). So gehen Messdaten nicht verloren, wenn das Gerät sich kurzzeitig außerhalb der Netzabdeckung befindet. Dabei wird pro Standort nur ein Ping gespeichert (Mindestabstand 10 m), um den begrenzten Speicher optimal zu nutzen.

Die gespeicherten Pings können über den seriellen Monitor ausgelesen werden: Innerhalb der **ersten 15 Sekunden** nach dem Einschalten `d` senden → das Board gibt alle gespeicherten Pings als CSV aus, kompatibel mit dem Funkloch-Upload-Tool.

---

## Einrichtung der Arduino IDE

### 1. Board-Support installieren

In der Arduino IDE unter **File → Preferences → Settings → Additional boards manager URLs** folgende URL eintragen:
```
https://github.com/HelTecAutomation/CubeCell-Arduino/releases/download/V1.5.0/package_CubeCell_index.json
```

Anschließend im **Boards Manager** nach `CubeCell` suchen und das **CubeCell Development Framework** installieren.  
> Getestet mit Version **1.5.0**

### 2. Bibliothek installieren

Im **Library Manager** die Bibliothek **TinyGPSPlus** by *Mikal Hart* installieren.  
> Getestet mit Version **1.0.3**

### 3. Board auswählen

**Tools → Board → Select Other Board and Port** → `CubeCell-Board-V2 (HTCC-AB01-V2)`

### 4. Board-Einstellungen

Unter **Tools** folgende Einstellungen vornehmen:

| Einstellung | Wert |
|---|---|
| LORAWAN_ADR | OFF |
| LORAWAN_AT_SUPPORT | ON |
| LORAWAN_CLASS | CLASS_A |
| LoRaWAN Debug Level | None |
| LORAWAN_DEVEUI | CUSTOM |
| LORAWAN_Net_Reservation | OFF |
| LORAWAN_NETMODE | ABP |
| LORAWAN_PREAMBLE_LENGTH | 8 (default) |
| LORAWAN_REGION | REGION_EU868 |
| LORAWAN_RGB | ACTIVE |
| LORAWAN_UPLINK_MODE | UNCONFIRMED |

> **Hinweis:** ADR wird in der Firmware netzwerkspezifisch gesteuert:
> TTN sendet immer mit SF7 (ADR deaktiviert), CS startet mit SF9 und 
> passt sich per ADR dynamisch an die Empfangssituation an.
> Die IDE-Einstellung wird durch den Code überschrieben.

---

## Keys eintragen & Geräte anlegen

Die Firmware nutzt **ABP (Activation By Personalization)** – die Session-Keys werden also fest in den Code eingetragen, es findet kein automatischer Join-Vorgang statt.

### Schritt 1: Geräte anlegen

Lege in **beiden** Netzwerken je ein neues ABP-Gerät an:
- **The Things Network (TTN):** [console.cloud.thethings.network](https://console.cloud.thethings.network)
- **ChirpStack:** Über die jeweilige Server-Instanz

Stelle bei beiden Geräten sicher, dass ABP als Aktivierungstyp gewählt ist und der Rahmen-Counter (Frame Counter) auf 0 startet **und die Frame Counter Checks deaktiviert** sind.

### Schritt 2: Keys in die Firmware eintragen

Die generierten Keys in der `.ino`-Datei im entsprechenden Abschnitt eintragen:
```cpp
// 1. TTN Keys
uint8_t nwkSKey_TTN[] = { 0x00, ... };
uint8_t appSKey_TTN[] = { 0x00, ... };
uint32_t devAddr_TTN  = ( uint32_t )0x00000000;

// 2. ChirpStack Keys
uint8_t nwkSKey_CS[]  = { 0x00, ... };
uint8_t appSKey_CS[]  = { 0x00, ... };
uint32_t devAddr_CS   = ( uint32_t )0x00000000;
```

### Schritt 3: Payload-Formatter

Der folgende JavaScript-Formatter muss in **beiden** Netzwerken (TTN und ChirpStack) als Uplink-Decoder hinterlegt werden, damit die Rohdaten korrekt dekodiert und als JSON weitergegeben werden.

### Paketstruktur
 
| Byte(s) | Inhalt | Hinweis |
|---|---|---|
| `0` | Board-ID | 1 Byte |
| `1–80` / `1–110` | 8 × bzw. 11 × GPS-Ping (je 10 Bytes) | TTN: 8 Pings, CS: 11 Pings |
| `81–82` / `111–112` | Akkuspannung in mV | Big Endian, `/ 1000` → Volt |
 
**Aufbau eines einzelnen Pings (10 Bytes):**
 
| Byte(s) | Inhalt | Hinweis |
|---|---|---|
| `+0–1` | Frame-Counter | Big Endian, 2 Bytes |
| `+2–5` | Längengrad (longitude) | Little Endian, Signed 32-Bit, `/ 1000000` → Dezimalgrad |
| `+6–9` | Breitengrad (latitude) | Little Endian, Signed 32-Bit, `/ 1000000` → Dezimalgrad |
 
Einträge mit `lat == 0` und `lon == 0` werden als leer behandelt und nicht ins Array aufgenommen – mit Ausnahme des ersten Eintrags (Index 0), der immer enthalten ist.

### Formatter-Code
- **TTN:** Konsole → Anwendung → Payload formatters → Uplink → JavaScript
- **ChirpStack:** Device Profile → Codec → JavaScript functions → `Decode`-Funktion

**TTN (8 Pings, 83 Bytes):**
```javascript
function decodeUplink(input) {
  var bytes = input.bytes;
  var data = {};

  data.boardID = bytes[0];
  data.pings = [];

  for (var i = 0; i < 8; i++) {
    var base = 1 + (i * 10);
    if (bytes.length < base + 10) break;

    var counter = (bytes[base] << 8) | bytes[base + 1];
    var lonRaw = (bytes[base + 5] << 24 | bytes[base + 4] << 16 | bytes[base + 3] << 8 | bytes[base + 2]) | 0;
    var latRaw = (bytes[base + 9] << 24 | bytes[base + 8] << 16 | bytes[base + 7] << 8 | bytes[base + 6]) | 0;

    if (latRaw == 0 && lonRaw == 0) {
      if (i == 0) {
        data.pings.push({ counter: counter, longitude: lonRaw / 1000000, latitude: latRaw / 1000000 });
      }
    } else {
      data.pings.push({ counter: counter, longitude: lonRaw / 1000000, latitude: latRaw / 1000000 });
    }
  }

  if (bytes.length >= 83) {
    var voltRaw = (bytes[81] << 8) | bytes[82];
    data.batteryVoltage = voltRaw / 1000;
  }

  return { data: data };
}
```

**ChirpStack (11 Pings, 113 Bytes):**
```javascript
function decodeUplink(input) {
  var bytes = input.bytes;
  var data = {};

  data.boardID = bytes[0];
  data.pings = [];

  for (var i = 0; i < 11; i++) {
    var base = 1 + (i * 10);
    if (bytes.length < base + 10) break;

    var counter = (bytes[base] << 8) | bytes[base + 1];
    var lonRaw = (bytes[base + 5] << 24 | bytes[base + 4] << 16 | bytes[base + 3] << 8 | bytes[base + 2]) | 0;
    var latRaw = (bytes[base + 9] << 24 | bytes[base + 8] << 16 | bytes[base + 7] << 8 | bytes[base + 6]) | 0;

    if (latRaw == 0 && lonRaw == 0) {
      if (i == 0) {
        data.pings.push({ counter: counter, longitude: lonRaw / 1000000, latitude: latRaw / 1000000 });
      }
    } else {
      data.pings.push({ counter: counter, longitude: lonRaw / 1000000, latitude: latRaw / 1000000 });
    }
  }

  if (bytes.length >= 113) {
    var voltRaw = (bytes[111] << 8) | bytes[112];
    data.batteryVoltage = voltRaw / 1000;
  }

  return { data: data };
}
```

---

## Board flashen

1. Board per USB verbinden
2. Korrekten Port unter **Tools → Port** auswählen
3. Sketch hochladen (**Upload**)

Nach dem Flashen sollte die RGB-LED des Boards aufleuchten und das Board sendet automatisch Pings an beide Netzwerke. Der erste GPS-Fix kann je nach Empfang einige Minuten dauern (Kaltstart).

> **Tipp:** Über den seriellen Monitor (115200 Baud) lässt sich der Betrieb live verfolgen – inkl. Koordinaten, Netzwerkwechsel und EEPROM-Status.

---

## Lizenz

Dieses Projekt entstand im Rahmen eines Kooperationsprojektes zwischen dem **Lehrstuhl für Wirtschaftsinformatik der Universität Potsdam**, der **Landeshauptstadt Potsdam**, und den **Stadtwerken Potsdam GmbH**.
