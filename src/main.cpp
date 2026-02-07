#include <Arduino.h>
#include <Wire.h>
#include <opt3001.h>

// ----------------------------------------------------
// Hardware-Konfiguration
// ----------------------------------------------------

// Drei I2C-Muxer:
//  - 0x70: Kanäle 0..7
//  - 0x71: Kanäle 0..7
//  - 0x72: Kanäle 0..3
const uint8_t NUM_MUXES = 3;
const uint8_t MUX_ADDR[NUM_MUXES]          = { 0x70, 0x71, 0x72 };
const uint8_t MUX_CHANNEL_COUNT[NUM_MUXES] = { 8,    8,    4    };

const uint8_t NUM_SENSORS_PER_CHANNEL = 3;   // 3 Sensoren je Kanal
const uint8_t SENSOR_ADDR[NUM_SENSORS_PER_CHANNEL] = {
  0x44, 0x45, 0x46
};

// OPT3001-Objekt (wird für alle Sensoren wiederverwendet)
opt3001 sensor;

// ----------------------------------------------------
// "Terminal löschen" durch viele Leerzeilen
// (funktioniert in jedem Serial-Monitor)
// ----------------------------------------------------
void clearTerminal() {
  for (int i = 0; i < 40; i++) {
    Serial.println();
  }
}

// ----------------------------------------------------
// I2C-Mux-Kanal auswählen
// muxIndex: 0 -> 0x70, 1 -> 0x71, 2 -> 0x72
// channel:  0..7 (siehe MUX_CHANNEL_COUNT)
// ----------------------------------------------------
void selectMuxChannel(uint8_t muxIndex, uint8_t channel) {
  if (muxIndex >= NUM_MUXES) return;
  if (channel >= MUX_CHANNEL_COUNT[muxIndex]) return;  // nur gültige Kanäle

  uint8_t addr = MUX_ADDR[muxIndex];

  Wire.beginTransmission(addr);
  Wire.write(1 << channel);   // genau ein Kanal aktiv
  Wire.endTransmission();
}

// Alle Kanäle eines Mux deaktivieren
void disableAllMuxChannels(uint8_t muxIndex) {
  if (muxIndex >= NUM_MUXES) return;

  uint8_t addr = MUX_ADDR[muxIndex];

  Wire.beginTransmission(addr);
  Wire.write(0x00);           // kein Kanal aktiv
  Wire.endTransmission();
}

// ----------------------------------------------------
// OPT3001 hart auf Power-On-Default zurücksetzen
// Config-Register = 0xC810
// ----------------------------------------------------
void resetOpt3001(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write(0x01);   // Config-Register
  Wire.write(0xC8);   // MSB
  Wire.write(0x10);   // LSB
  Wire.endTransmission();
  delay(5);
}

// ----------------------------------------------------
// Alle Sensoren auf allen Muxen und Kanälen resetten
// ----------------------------------------------------
void resetAllSensors() {
  for (uint8_t m = 0; m < NUM_MUXES; m++) {
    for (uint8_t ch = 0; ch < MUX_CHANNEL_COUNT[m]; ch++) {
      selectMuxChannel(m, ch);
      delay(2);

      for (uint8_t i = 0; i < NUM_SENSORS_PER_CHANNEL; i++) {
        resetOpt3001(SENSOR_ADDR[i]);
      }
    }

    // nach dem Durchlauf alle Kanäle des Mux deaktivieren
    disableAllMuxChannels(m);
  }
}

// ----------------------------------------------------
// SETUP
// ----------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  // ESP32 I2C starten (Pins ggf. anpassen)
  // Häufig: SDA = 21, SCL = 22
  Wire.begin();

  clearTerminal();
  Serial.println("OPT3001 Matrix Start (3x I2C-Mux, Continuous Mode, schnell)");

  // Alle Sensoren in definierten Zustand bringen
  resetAllSensors();

  // ------------------------------------------------
  // Alle Sensoren auf allen Muxen konfigurieren:
  // - Conversion Time: 100 ms (schnell)
  // - Continuous Mode: läuft permanent im Hintergrund
  // ------------------------------------------------
  for (uint8_t m = 0; m < NUM_MUXES; m++) {
    Serial.print("Konfiguriere Mux @ 0x");
    Serial.println(MUX_ADDR[m], HEX);

    for (uint8_t ch = 0; ch < MUX_CHANNEL_COUNT[m]; ch++) {
      selectMuxChannel(m, ch);
      delay(2);

      for (uint8_t i = 0; i < NUM_SENSORS_PER_CHANNEL; i++) {
        uint8_t addr = SENSOR_ADDR[i];

        if (sensor.setup(Wire, addr) != 0) {
          Serial.print("  Setup-Fehler an Mux 0x");
          Serial.print(MUX_ADDR[m], HEX);
          Serial.print(" Kanal ");
          Serial.print(ch);
          Serial.print(" Addr 0x");
          Serial.println(addr, HEX);
          continue;
        }

        if (sensor.detect() != 0) {
          Serial.print("  Sensor nicht erkannt an Mux 0x");
          Serial.print(MUX_ADDR[m], HEX);
          Serial.print(" Kanal ");
          Serial.print(ch);
          Serial.print(" Addr 0x");
          Serial.println(addr, HEX);
          continue;
        }

        // schnelle, stabile Messung
        sensor.config_set(OPT3001_CONVERSION_TIME_100MS);
        sensor.conversion_continuous_enable();

        Serial.print("  Init OK: Mux 0x");
        Serial.print(MUX_ADDR[m], HEX);
        Serial.print(" Kanal ");
        Serial.print(ch);
        Serial.print(" Addr 0x");
        Serial.println(addr, HEX);
      }
    }

    // alle Kanäle dieses Mux deaktivieren
    disableAllMuxChannels(m);
  }

  Serial.println("Initialisierung aller Sensoren abgeschlossen.");
}

// ----------------------------------------------------
// LOOP – alle Sensoren nacheinander schnell auslesen
// ----------------------------------------------------
void loop() {
  clearTerminal();

  Serial.println("OPT3001 Lux-Matrix (3x Mux, insgesamt 60 Sensoren)");
  Serial.println("===================================================");

  for (uint8_t m = 0; m < NUM_MUXES; m++) {
    Serial.println();
    Serial.print("=== I2C-Mux @ 0x");
    Serial.print(MUX_ADDR[m], HEX);
    Serial.println(" ===");

    for (uint8_t ch = 0; ch < MUX_CHANNEL_COUNT[m]; ch++) {
      // Mux + Kanal anwählen
      selectMuxChannel(m, ch);
      delay(2);

      Serial.print("MUX ");
      Serial.print(m);
      Serial.print(", CH ");
      Serial.print(ch);
      Serial.print(": ");

      for (uint8_t i = 0; i < NUM_SENSORS_PER_CHANNEL; i++) {
        uint8_t addr = SENSOR_ADDR[i];

        // Sensor-Objekt auf diese Adresse "umstellen"
        if (sensor.setup(Wire, addr) != 0) {
          Serial.print(" --- ");
          if (i < NUM_SENSORS_PER_CHANNEL - 1) {
            Serial.print(" | ");
          }
          continue;
        }

        float lux = 0.0f;
        int rc = sensor.lux_read(&lux);

        if (rc == 0) {
          Serial.print(lux, 1);
          Serial.print(" lx");
        } else {
          Serial.print("ERR");
        }

        if (i < NUM_SENSORS_PER_CHANNEL - 1) {
          Serial.print(" | ");
        }
      }

      Serial.println();
    }

    // nach Auslesen alle Kanäle am Mux wieder deaktivieren
    disableAllMuxChannels(m);
  }

  Serial.println();
  Serial.println("Update-Rate: ~10 Hz");

  delay(100); // 10 Updates pro Sekunde
}
