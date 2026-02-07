#include <Arduino.h>
#include <Wire.h>
#include <opt3001.h>

// ----------------------------------------------------
// Hardware-Konfiguration
// ----------------------------------------------------

// Muxer:
// 0x70 → 8 Kanäle
// 0x71 → 8 Kanäle
// 0x72 → 4 Kanäle
const uint8_t NUM_MUXES = 3;
const uint8_t MUX_ADDR[NUM_MUXES]          = { 0x70, 0x71, 0x72 };
const uint8_t MUX_CHANNEL_COUNT[NUM_MUXES] = { 8,    8,    4    };

const uint8_t NUM_SENSORS_PER_CHANNEL = 3;
const uint8_t SENSOR_ADDR[NUM_SENSORS_PER_CHANNEL] = {
  0x44, 0x45, 0x46
};

opt3001 sensor;

// ----------------------------------------------------
// Terminal "clearen" (scroll)
// ----------------------------------------------------
void clearTerminal() {
  for (int i = 0; i < 40; i++) Serial.println();
}

// ----------------------------------------------------
// I2C-Mux-Kanal auswählen
// ----------------------------------------------------
void selectMuxChannel(uint8_t muxIndex, uint8_t channel) {
  if (muxIndex >= NUM_MUXES) return;
  if (channel >= MUX_CHANNEL_COUNT[muxIndex]) return;

  Wire.beginTransmission(MUX_ADDR[muxIndex]);
  Wire.write(1 << channel);
  Wire.endTransmission();
}

void disableAllMuxChannels(uint8_t muxIndex) {
  if (muxIndex >= NUM_MUXES) return;

  Wire.beginTransmission(MUX_ADDR[muxIndex]);
  Wire.write(0x00);
  Wire.endTransmission();
}

// ----------------------------------------------------
// OPT3001 Reset
// ----------------------------------------------------
void resetOpt3001(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write(0x01);
  Wire.write(0xC8);
  Wire.write(0x10);
  Wire.endTransmission();
  delay(5);
}

void resetAllSensors() {
  for (uint8_t m = 0; m < NUM_MUXES; m++) {
    for (uint8_t ch = 0; ch < MUX_CHANNEL_COUNT[m]; ch++) {
      selectMuxChannel(m, ch);
      delay(2);
      for (uint8_t i = 0; i < NUM_SENSORS_PER_CHANNEL; i++) {
        resetOpt3001(SENSOR_ADDR[i]);
      }
    }
    disableAllMuxChannels(m);
  }
}

// ----------------------------------------------------
// SETUP
// ----------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin();

  clearTerminal();
  Serial.println("OPT3001 Lux-Matrix (60 Sensoren)");

  resetAllSensors();

  // Sensoren konfigurieren
  for (uint8_t m = 0; m < NUM_MUXES; m++) {
    for (uint8_t ch = 0; ch < MUX_CHANNEL_COUNT[m]; ch++) {
      selectMuxChannel(m, ch);
      delay(2);

      for (uint8_t i = 0; i < NUM_SENSORS_PER_CHANNEL; i++) {
        if (sensor.setup(Wire, SENSOR_ADDR[i]) == 0 &&
            sensor.detect() == 0) {
          sensor.config_set(OPT3001_CONVERSION_TIME_100MS);
          sensor.conversion_continuous_enable();
        }
      }
    }
    disableAllMuxChannels(m);
  }
}

// ----------------------------------------------------
// LOOP – Ausgabe als eine Tabelle
// ----------------------------------------------------
void loop() {
  clearTerminal();

  Serial.println(" Zeile |   S1 (lx) |   S2 (lx) |   S3 (lx)");
  Serial.println("-------+-----------+-----------+-----------");

  uint8_t globalRow = 0;

  for (uint8_t m = 0; m < NUM_MUXES; m++) {
    for (uint8_t ch = 0; ch < MUX_CHANNEL_COUNT[m]; ch++) {
      selectMuxChannel(m, ch);
      delay(2);

      Serial.printf(" %5u |", globalRow);

      for (uint8_t i = 0; i < NUM_SENSORS_PER_CHANNEL; i++) {
        float lux = 0.0f;

        if (sensor.setup(Wire, SENSOR_ADDR[i]) == 0 &&
            sensor.lux_read(&lux) == 0) {
          Serial.printf(" %9.1f |", lux);
        } else {
          Serial.print("    ERR    |");
        }
      }

      Serial.println();
      globalRow++;
    }
    disableAllMuxChannels(m);
  }

  Serial.println();
  Serial.println("Update-Rate: ~10 Hz");

  delay(100);
}
