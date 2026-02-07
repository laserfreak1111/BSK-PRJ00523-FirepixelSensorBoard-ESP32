#include <Arduino.h>
#include <Wire.h>
#include <opt3001.h>

#include <ETH.h>
#include <WebServer.h>

// ----------------------------------------------------
// Ethernet-Konfiguration
// ----------------------------------------------------
#define ETH_PHY_TYPE  ETH_PHY_LAN8720
#define ETH_PHY_ADDR  0
#define ETH_PHY_MDC   23
#define ETH_PHY_MDIO  18
#define ETH_PHY_POWER 12
#define ETH_CLK_MODE  ETH_CLOCK_GPIO17_OUT  // 50 MHz aus ESP32 auf GPIO17

WebServer server(80);
bool eth_connected = false;

// ----------------------------------------------------
// Sensor- / Mux-Konfiguration
// ----------------------------------------------------

// Drei I2C-Muxer:
//  0x70 → 8 Kanäle
//  0x71 → 8 Kanäle
//  0x72 → 4 Kanäle
const uint8_t NUM_MUXES = 3;
const uint8_t MUX_ADDR[NUM_MUXES]          = { 0x70, 0x71, 0x72 };
const uint8_t MUX_CHANNEL_COUNT[NUM_MUXES] = { 8,    8,    4    };

const uint8_t NUM_SENSORS_PER_CHANNEL = 3;
const uint8_t SENSOR_ADDR[NUM_SENSORS_PER_CHANNEL] = { 0x44, 0x45, 0x46 };

// Insgesamt 8 + 8 + 4 = 20 Zeilen
#define TOTAL_ROWS 20

opt3001 sensor;

// Matrix für die aktuellen Lux-Werte
float luxMatrix[TOTAL_ROWS][NUM_SENSORS_PER_CHANNEL];

// ----------------------------------------------------
// Terminal "clearen" durch Scrollen
// ----------------------------------------------------
void clearTerminal() {
  for (int i = 0; i < 40; i++) {
    Serial.println();
  }
}

// ----------------------------------------------------
// I2C-Mux-Kanal auswählen / deaktivieren
// ----------------------------------------------------
void selectMuxChannel(uint8_t muxIndex, uint8_t channel) {
  if (muxIndex >= NUM_MUXES) return;
  if (channel >= MUX_CHANNEL_COUNT[muxIndex]) return;

  Wire.beginTransmission(MUX_ADDR[muxIndex]);
  Wire.write(1 << channel);   // genau ein Kanal aktiv
  Wire.endTransmission();
}

void disableAllMuxChannels(uint8_t muxIndex) {
  if (muxIndex >= NUM_MUXES) return;

  Wire.beginTransmission(MUX_ADDR[muxIndex]);
  Wire.write(0x00);           // kein Kanal aktiv
  Wire.endTransmission();
}

// ----------------------------------------------------
// OPT3001 Reset auf Power-On-Default (0xC810)
// ----------------------------------------------------
void resetOpt3001(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write(0x01);   // Config-Register
  Wire.write(0xC8);   // MSB
  Wire.write(0x10);   // LSB
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
// Ethernet-Event-Handler
// ----------------------------------------------------
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_ETH_START:
      ETH.setHostname("esp32-lux-matrix");
      Serial.println("ETH gestartet");
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      Serial.println("ETH verbunden");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
      eth_connected = true;
      Serial.print("ETH IPv4: ");
      Serial.println(ETH.localIP());
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      eth_connected = false;
      Serial.println("ETH getrennt");
      break;
    case SYSTEM_EVENT_ETH_STOP:
      eth_connected = false;
      Serial.println("ETH gestoppt");
      break;
    default:
      break;
  }
}

// ----------------------------------------------------
// HTTP-Handler: Root-Seite mit Live-Web-UI
// ----------------------------------------------------
void handleRoot() {
  String html;
  html.reserve(9000);

  html += F(
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Lux-Matrix</title>"
    "<style>"
    "body{background:#111;color:#eee;font-family:Arial,sans-serif;text-align:center;margin:0;padding:0;}"
    "h1{margin-top:20px;margin-bottom:5px;}"
    "p{margin:4px;}"
    "#info{font-size:12px;color:#aaa;}"
    "svg{margin:20px auto;background:#222;border-radius:8px;display:block;}"
    "circle{stroke:#444;stroke-width:1;}"
    ".label{fill:#ccc;font-size:12px;}"
    "</style>"
    "</head><body>"
    "<h1>Lux-Matrix</h1>"
    "<p>Grün: &ge; 200 lx &nbsp;&nbsp; Schwarz: &lt; 200 lx</p>"
    "<div id='info'>Verbunden zum ESP32 &ndash; Live-Update ohne Reload</div>"
  );

  const int cellSize = 40;
  const int radius   = 14;
  const int cols     = NUM_SENSORS_PER_CHANNEL; // 3
  const int rows     = TOTAL_ROWS;              // 20
  const int svgWidth  = cols * cellSize + 60;
  const int svgHeight = rows * cellSize + 60;

  html += "<svg id='luxSvg' width='" + String(svgWidth) + "' height='" + String(svgHeight) + "'>";

  // Spalten-Labels (S1, S2, S3)
  for (int c = 0; c < cols; c++) {
    int cx = 40 + c * cellSize;
    html += "<text class='label' x='" + String(cx) + "' y='20' text-anchor='middle'>S";
    html += String(c + 1);
    html += "</text>";
  }

  // Zeilen + Kreise mit IDs
  for (int r = 0; r < rows; r++) {
    int cy = 40 + r * cellSize;

    // Zeilen-Label links
    html += "<text class='label' x='10' y='" + String(cy + 4) + "'>";
    html += String(r);
    html += "</text>";

    for (int c = 0; c < cols; c++) {
      int cx = 40 + c * cellSize;

      String id = "cell-" + String(r) + "-" + String(c);
      html += "<circle id='" + id + "' cx='" + String(cx) +
              "' cy='" + String(cy) + "' r='" + String(radius) +
              "' fill='#000000'></circle>";
    }
  }

  html += "</svg>";

  // JavaScript für Live-Update (ohne STR-Makros)
  html += F("<script>");
  html += "const rows = " + String(TOTAL_ROWS) + ";\n";
  html += "const cols = " + String(NUM_SENSORS_PER_CHANNEL) + ";\n";
  html += F(
    "function updateMatrix(){"
      "fetch('/data').then(r=>r.json()).then(data=>{"
        "for(let r=0;r<rows;r++){"
          "for(let c=0;c<cols;c++){"
            "let lux=data[r][c];"
            "let id='cell-'+r+'-'+c;"
            "let circle=document.getElementById(id);"
            "if(!circle) continue;"
            "let color='#000000';"
            "if(lux!==null && lux>=200.0){color='#00ff00';}"
            "circle.setAttribute('fill',color);"
            "circle.setAttribute('title',lux!==null?lux.toFixed(1)+' lx':'ERR');"
          "}"
        "}"
      "}).catch(e=>{console.log(e);});"
    "}"
    "setInterval(updateMatrix,500);"
    "window.onload=updateMatrix;"
    "</script>"
    "</body></html>"
  );

  server.send(200, "text/html", html);
}

// ----------------------------------------------------
// HTTP-Handler: /data → JSON der Lux-Matrix
// ----------------------------------------------------
void handleData() {
  String json;
  json.reserve(4000);
  json += "[";

  for (uint8_t r = 0; r < TOTAL_ROWS; r++) {
    if (r > 0) json += ",";
    json += "[";

    for (uint8_t c = 0; c < NUM_SENSORS_PER_CHANNEL; c++) {
      if (c > 0) json += ",";
      float v = luxMatrix[r][c];
      if (isnan(v)) {
        json += "null";
      } else {
        json += String(v, 1);
      }
    }

    json += "]";
  }

  json += "]";
  server.send(200, "application/json", json);
}

// ----------------------------------------------------
// Serielle Tabellen-Ausgabe (zum Debuggen)
// ----------------------------------------------------
void printTableToSerial() {
  clearTerminal();

  Serial.println(" Zeile |   S1 (lx) |   S2 (lx) |   S3 (lx)");
  Serial.println("-------+-----------+-----------+-----------");

  for (uint8_t r = 0; r < TOTAL_ROWS; r++) {
    Serial.printf(" %5u |", r);
    for (uint8_t c = 0; c < NUM_SENSORS_PER_CHANNEL; c++) {
      float lux = luxMatrix[r][c];
      if (!isnan(lux)) {
        Serial.printf(" %9.1f |", lux);
      } else {
        Serial.print("    ERR    |");
      }
    }
    Serial.println();
  }

  Serial.println();
}

// ----------------------------------------------------
// Sensorwerte aktualisieren
// ----------------------------------------------------
void updateLuxMatrix() {
  uint8_t globalRow = 0;

  for (uint8_t m = 0; m < NUM_MUXES; m++) {
    for (uint8_t ch = 0; ch < MUX_CHANNEL_COUNT[m]; ch++) {
      if (globalRow >= TOTAL_ROWS) break;

      selectMuxChannel(m, ch);
      delay(2);

      for (uint8_t i = 0; i < NUM_SENSORS_PER_CHANNEL; i++) {
        float lux = NAN;

        if (sensor.setup(Wire, SENSOR_ADDR[i]) == 0) {
          if (sensor.lux_read(&lux) != 0) {
            lux = NAN;
          }
        }

        luxMatrix[globalRow][i] = lux;
      }

      globalRow++;
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

  // I2C starten (Pins ggf. anpassen)
  Wire.begin();

  clearTerminal();
  Serial.println("Start: OPT3001 Lux-Matrix mit Ethernet-Webserver (Live-UI)");

  // Sensoren zurücksetzen & konfigurieren
  resetAllSensors();

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

  // Ethernet starten
  WiFi.onEvent(WiFiEvent);
  ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER, ETH_PHY_MDC, ETH_PHY_MDIO,
            ETH_PHY_TYPE, ETH_CLK_MODE);

  // Webserver-Routen
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("HTTP-Server gestartet (Port 80)");
}

// ----------------------------------------------------
// LOOP
// ----------------------------------------------------
void loop() {
  // Webserver bearbeiten
  server.handleClient();

  // Sensoren regelmäßig aktualisieren
  static unsigned long lastUpdate = 0;
  const unsigned long updateIntervalMs = 200; // alle 200 ms

  unsigned long now = millis();
  if (now - lastUpdate >= updateIntervalMs) {
    lastUpdate = now;
    updateLuxMatrix();
    printTableToSerial();   // wenn du keine serielle Ausgabe brauchst: auskommentieren
  }
}
