#include <Arduino.h>
#include <Wire.h>
#include <opt3001.h>

// ----------------------------------------------------
// Hardware-Konfiguration
// ----------------------------------------------------
#define I2C_MUX_ADDR 0x70

const uint8_t NUM_MUX_CHANNELS        = 8;                 // 0..7
const uint8_t NUM_SENSORS_PER_CHANNEL = 3;                 // 3 Sensoren je Kanal
const uint8_t SENSOR_ADDR[NUM_SENSORS_PER_CHANNEL] = {
  0x44, 0x45, 0x46
};

// OPT3001 Objekt (wird für alle Sensoren wiederverwendet)
opt3001 sensor;

// ----------------------------------------------------
// I2C-Mux-Kanal auswählen
// ----------------------------------------------------
void selectMuxChannel(uint8_t channel) {
  if (channel > 7) return;

  Wire.beginTransmission(I2C_MUX_ADDR);
  Wire.write(1 << channel);
  Wire.endTransmission();
}

// ----------------------------------------------------
// OPT3001 hart auf Power-On-Default zurücksetzen
// Config-Register = 0xC810 (laut TI-Datenblatt)
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
// Alle Sensoren auf allen Mux-Kanälen resetten
// ----------------------------------------------------
void resetAllSensors() {
  for (uint8_t ch = 0; ch < NUM_MUX_CHANNELS; ch++) {
    selectMuxChannel(ch);
    delay(2);

    for (uint8_t i = 0; i < NUM_SENSORS_PER_CHANNEL; i++) {
      resetOpt3001(SENSOR_ADDR[i]);
    }
  }
}

// ----------------------------------------------------
// SETUP
// ----------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  // ESP32 I2C starten (Pins ggf. anpassen)
  Wire.begin();

  Serial.println();
  Serial.println("OPT3001 Matrix Star56345163165t");

  // Alle Sensoren in definierten Zustand bringen
  resetAllSensors();

  // Sensoren initial konfigurieren
  for (uint8_t ch = 0; ch < NUM_MUX_CHANNELS; ch++) {
    selectMuxChannel(ch);
    delay(2);

    for (uint8_t i = 0; i < NUM_SENSORS_PER_CHANNEL; i++) {
      uint8_t addr = SENSOR_ADDR[i];

      if (sensor.setup(Wire, addr) == 0 && sensor.detect() == 0) {
        sensor.config_set(OPT3001_CONVERSION_TIME_800MS);
      } else {
        Serial.print("Init-Fehler an Kanal ");
        Serial.print(ch);
        Serial.print(" Addr 0x");
        Serial.println(addr, HEX);
      }
    }
  }

  Serial.println("Initialisierung abgeschlossen.");
}

// ----------------------------------------------------
// LOOP – alle 24 Sensoren nacheinander auslesen
// ----------------------------------------------------
void loop() {
  for (uint8_t ch = 0; ch < NUM_MUX_CHANNELS; ch++) {
    selectMuxChannel(ch);
    delay(5);

    Serial.println();
    Serial.print("=== MUX Kanal ");
    Serial.print(ch);
    Serial.println(" ===");

    for (uint8_t i = 0; i < NUM_SENSORS_PER_CHANNEL; i++) {
      uint8_t addr = SENSOR_ADDR[i];

      if (sensor.setup(Wire, addr) != 0) {
        Serial.print("Setup-Fehler an Kanal ");
        Serial.print(ch);
        Serial.print(" Addr 0x");
        Serial.println(addr, HEX);
        continue;
      }

      // Single-Shot Messung starten
      sensor.conversion_singleshot_trigger();

      // Warten > 800ms
      delay(900);

      float lux = 0.0f;
      int rc = sensor.lux_read(&lux);

      Serial.print("Kanal ");
      Serial.print(ch);
      Serial.print("  Addr 0x");
      Serial.print(addr, HEX);
      Serial.print("  -> ");

      if (rc == 0) {
        Serial.print(lux, 2);
        Serial.println(" lx");
      } else {
        Serial.print("Lesefehler (rc=");
        Serial.print(rc);
        Serial.println(")");
      }

      delay(10);
    }
  }

  Serial.println();
  Serial.println("---- kompletter Durchlauf abgeschlossen ----");
  Serial.println();

  delay(1000);
}
