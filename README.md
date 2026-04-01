# lorawan-firmware

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

Jedes Messgerät sendet abwechselnd Pings an zwei LoRaWAN-Netzwerke: **The Things Network (TTN)** und einen **ChirpStack**-Server. Durch den Wechsel erhält jedes Netz alle ~30 Sekunden einen Ping, also etwa einen Ping pro Minute pro Netzwerk.

Jeder Ping enthält:
- **Board-ID** zur Identifikation des Geräts
- **GPS-Koordinaten** (aktuelle Position + die 8 vorherigen Pings als Historie)
- **Akkuspannung** in mV

Anhand der empfangenen Pings und der am Gateway gemessenen Signalstärke (RSSI/SNR) lässt sich die LoRaWAN-Netzabdeckung flächendeckend kartieren.

### Bewegungsschwelle

Um stationäre Dauerpings (z.B. beim Liegen auf einem Schreibtisch) zu vermeiden und Indoor-Messungen zu unterdrücken, wird ein Ping erst gesendet, wenn das Gerät sich seit dem letzten Ping **mindestens 9 Meter** fortbewegt hat. Die ersten 4 Pings nach dem Einschalten werden ohne Bewegungsprüfung gesendet, damit das Gerät nach dem Start korrekt initialisiert wird.

### EEPROM-Pufferung für Funklöcher

Jeder TTN-Ping wird zusätzlich im internen EEPROM des Boards gespeichert (Ringpuffer, ~50 Einträge). So gehen Messdaten nicht verloren, wenn das Gerät sich kurzzeitig außerhalb der Netzabdeckung befindet. Dabei wird pro Standort nur ein Ping gespeichert (Mindestabstand 10 m), um den begrenzten Speicher optimal zu nutzen.

Die gespeicherten Pings können über den seriellen Monitor ausgelesen werden: Innerhalb der **ersten 15 Sekunden** nach dem Einschalten `d` senden → das Board gibt alle gespeicherten Pings als CSV aus, kompatibel mit dem Funkloch-Upload-Tool.

---

## Einrichtung der Arduino IDE

### 1. Board-Support installieren

In der Arduino IDE unter **File → Preferences → Settings → Additional boards manager URLs** folgende URL eintragen:
```
https://resource.heltec.cn/download/package_heltec_esp32_index.json
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
| LORAWAN_ADR | ON |
| LORAWAN_AT_SUPPORT | ON |
| LORAWAN_CLASS | CLASS_A |
| LoRaWAN Debug Level | None |
| LORAWAN_DEVEUI | CUSTOM |
| LORAWAN_Net_Reservation | OFF |
| LORAWAN_NETMODE | ABP |
| LORAWAN_PREAMBLE_LENGTH | 8 (default) |
| LORAWAN_REGION | REGION_EU868 |
| LORAWAN_RGB | ACTIVE |
| LORAWAN_UPLINK_MODE | CONFIRMED |

---

## Keys eintragen & Geräte anlegen

Die Firmware nutzt **ABP (Activation By Personalization)** – die Session-Keys werden also fest in den Code eingetragen, es findet kein automatischer Join-Vorgang statt.

### Schritt 1: Geräte anlegen

Lege in **beiden** Netzwerken je ein neues ABP-Gerät an:
- **The Things Network (TTN):** [console.cloud.thethings.network](https://console.cloud.thethings.network)
- **ChirpStack:** Über die jeweilige Server-Instanz

Stelle bei beiden Geräten sicher, dass ABP als Aktivierungstyp gewählt ist und der Rahmen-Counter (Frame Counter) auf 0 startet bzw. **Frame Counter Checks deaktiviert** sind.

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

### Schritt 3: Board-ID setzen

Jedes Board sollte eine eindeutige ID erhalten, um die Messdaten später zuordnen zu können:
```cpp
int boardID = 1; // Eindeutige ID für dieses Board
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
