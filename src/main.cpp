#include <Arduino.h>
#include <Wire.h>
#include <opt3001.h>

#include <ETH.h>
#include <WebServer.h>
#include <FastLED.h>

// ----------------------------------------------------
// Ethernet-Konfiguration
// ----------------------------------------------------
#define ETH_PHY_TYPE  ETH_PHY_LAN8720
#define ETH_PHY_ADDR  0
#define ETH_PHY_MDC   23
#define ETH_PHY_MDIO  18
#define ETH_PHY_POWER 12
#define ETH_CLK_MODE  ETH_CLOCK_GPIO17_OUT

WebServer server(80);

// ----------------------------------------------------
// WS2815 Status-LEDs (FastLED)
// ----------------------------------------------------
#define LED_PIN        4
#define LED_COUNT      2
#define LED_TYPE       WS2812B
#define COLOR_ORDER    GRB
#define LED_BRIGHTNESS 255

CRGB statusLeds[LED_COUNT];

// LED-Zustand per GET steuerbar
bool ledR = false;
bool ledG = false;
bool ledB = false;

// ----------------------------------------------------
// Sensor- / Mux-Konfiguration
// ----------------------------------------------------
const uint8_t NUM_MUXES = 3;
const uint8_t MUX_ADDR[NUM_MUXES]          = { 0x70, 0x71, 0x72 };
const uint8_t MUX_CHANNEL_COUNT[NUM_MUXES] = { 8,    8,    4    };

const uint8_t NUM_SENSORS_PER_CHANNEL = 3;
const uint8_t SENSOR_ADDR[NUM_SENSORS_PER_CHANNEL] = { 0x44, 0x45, 0x46 };

#define TOTAL_ROWS 20   // 0 .. 19

opt3001 sensor;
float luxMatrix[TOTAL_ROWS][NUM_SENSORS_PER_CHANNEL];

// ----------------------------------------------------
// I2C-Mux Helfer
// ----------------------------------------------------
void selectMuxChannel(uint8_t mux, uint8_t ch) {
  if (mux >= NUM_MUXES) return;
  if (ch >= MUX_CHANNEL_COUNT[mux]) return;
  Wire.beginTransmission(MUX_ADDR[mux]);
  Wire.write(1 << ch);
  Wire.endTransmission();
}

void disableMux(uint8_t mux) {
  if (mux >= NUM_MUXES) return;
  Wire.beginTransmission(MUX_ADDR[mux]);
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
      for (uint8_t i = 0; i < NUM_SENSORS_PER_CHANNEL; i++) {
        resetOpt3001(SENSOR_ADDR[i]);
      }
    }
    disableMux(m);
  }
}

// ----------------------------------------------------
// LEDs aus R/G/B-Flags setzen
// ----------------------------------------------------
void applyLedColor() {
  CRGB c(ledR ? 255 : 0, ledG ? 255 : 0, ledB ? 255 : 0);
  for (uint8_t i = 0; i < LED_COUNT; i++) statusLeds[i] = c;
  FastLED.show();
}

// ----------------------------------------------------
// Sensorwerte aktualisieren
// ----------------------------------------------------
void updateLuxMatrix() {
  uint8_t row = 0;

  for (uint8_t m = 0; m < NUM_MUXES; m++) {
    for (uint8_t ch = 0; ch < MUX_CHANNEL_COUNT[m]; ch++) {
      if (row >= TOTAL_ROWS) break;

      selectMuxChannel(m, ch);
      delayMicroseconds(500);

      for (uint8_t i = 0; i < NUM_SENSORS_PER_CHANNEL; i++) {
        float lux = NAN;
        if (sensor.setup(Wire, SENSOR_ADDR[i]) == 0) {
          if (sensor.lux_read(&lux) != 0) lux = NAN;
        }
        luxMatrix[row][i] = lux;
      }
      row++;
    }
    disableMux(m);
  }
}

// ----------------------------------------------------
// /data → flaches Array UMGEKEHRT (Index 0 = oben)
// ----------------------------------------------------
void handleData() {
  String json;
  json.reserve(4000);
  json += "[";
  bool first = true;

  for (int r = TOTAL_ROWS - 1; r >= 0; r--) {
    for (uint8_t c = 0; c < NUM_SENSORS_PER_CHANNEL; c++) {
      if (!first) json += ",";
      first = false;
      float v = luxMatrix[r][c];
      json += isnan(v) ? "null" : String(v, 1);
    }
  }

  json += "]";
  server.send(200, "application/json", json);
}

// ----------------------------------------------------
// /led?r=1&g=0&b=1
// ----------------------------------------------------
bool parseBool(const String &s, bool cur) {
  if (!s.length()) return cur;
  String v = s; v.toLowerCase();
  if (v == "1" || v == "true" || v == "on") return true;
  if (v == "0" || v == "false" || v == "off") return false;
  return cur;
}

void handleLed() {
  if (server.hasArg("r")) ledR = parseBool(server.arg("r"), ledR);
  if (server.hasArg("g")) ledG = parseBool(server.arg("g"), ledG);
  if (server.hasArg("b")) ledB = parseBool(server.arg("b"), ledB);
  applyLedColor();

  server.send(200, "application/json",
    "{\"r\":" + String(ledR ? "true" : "false") +
    ",\"g\":" + String(ledG ? "true" : "false") +
    ",\"b\":" + String(ledB ? "true" : "false") + "}"
  );
}

// ----------------------------------------------------
// WebUI – Grafik + Werte nebeneinander
// Mit Index-Zahlen 1..60 über jedem Kreis
// ----------------------------------------------------
void handleRoot() {
  String html;
  html.reserve(17000);

  const int cell = 40;
  const int rad  = 14;
  const int rows = TOTAL_ROWS;
  const int cols = NUM_SENSORS_PER_CHANNEL;
  const int topMargin = 70;

  html += F(
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{background:#111;color:#eee;font-family:Arial,sans-serif;margin:0;padding:0;text-align:center}"
    "h2{margin-top:12px;margin-bottom:4px}"
    "p{margin:4px;font-size:13px}"
    ".wrap{display:flex;justify-content:center;align-items:flex-start;gap:24px;margin:10px;flex-wrap:wrap}"
    "svg{background:#222;border-radius:8px}"
    "circle{stroke:#444;stroke-width:1}"
    ".lbl{fill:#ccc;font-size:11px}"
    ".title{fill:#0f0;font-size:14px;font-weight:bold}"
    ".idx{fill:#ddd;font-size:10px;font-weight:bold;text-anchor:middle;dominant-baseline:middle}"
    "table{border-collapse:collapse;background:#222;border-radius:8px;overflow:hidden;font-size:12px}"
    "th,td{border:1px solid #444;padding:2px 6px;text-align:right}"
    "th{background:#333;font-weight:bold}"
    "tr:nth-child(even){background:#262626}"
    "tr:nth-child(odd){background:#1d1d1d}"
    ".rowlabel{text-align:center;font-weight:bold;color:#aaa}"
    "</style></head><body>"
    "<h2>Lux-Matrix (logarithmisch)</h2>"
    "<p>0 lx = schwarz → grün → gelb → rot</p>"
    "<p>/data: flaches Array (60) – Index 0 = oberste Reihe im GUI</p>"
  );

  html += "<div class='wrap'>";

  // --- SVG ---
  html += "<svg width='240' height='900'>";
  html += "<text class='title' x='120' y='30' text-anchor='middle'>LOGIC SIDE</text>";

  // Anzeige-Reihen (oben->unten): Row 19..0
  // Datenindex im flachen Array: dispRow*3 + col   (dispRow=0 ist oben)
  // Index-Label: +1 für 1..60
  for (int dispRow = 0; dispRow < rows; dispRow++) {
    int logicalRow = rows - 1 - dispRow;  // 19..0
    int cy = topMargin + dispRow * cell;

    // Row-Label links
    html += "<text class='lbl' x='15' y='" + String(cy + 4) + "'>" + String(logicalRow) + "</text>";

    for (int col = 0; col < cols; col++) {
      int cx = 70 + col * cell;

      // Kreis-ID bleibt logisch (Row/Col), damit JS einfach bleibt
      String cid = "c" + String(logicalRow) + "_" + String(col);
      html += "<circle id='" + cid +
              "' cx='" + String(cx) +
              "' cy='" + String(cy) +
              "' r='" + String(rad) + "' fill='#000'/>";

      // Index-Zahl 1..60 (statisch)
      int idxLabel = dispRow * cols + col + 1; // 1..60 von oben nach unten
      html += "<text class='idx' x='" + String(cx) + "' y='" + String(cy - 22) + "'>";
      html += String(idxLabel);
      html += "</text>";
    }
  }
  html += "</svg>";

  // --- Tabelle ---
  html += "<table><thead><tr>";
  html += "<th>Row</th><th>S0</th><th>S1</th><th>S2</th>";
  html += "</tr></thead><tbody>";

  for (int dispRow = 0; dispRow < rows; dispRow++) {
    int logicalRow = rows - 1 - dispRow;  // 19..0
    html += "<tr>";
    html += "<td class='rowlabel'>" + String(logicalRow) + "</td>";
    for (int col = 0; col < cols; col++) {
      String id = "v" + String(logicalRow) + "_" + String(col);
      html += "<td id='" + id + "'>--.-</td>";
    }
    html += "</tr>";
  }
  html += "</tbody></table>";

  html += "</div>"; // wrap

  // --- JS: /data lesen und aktualisieren ---
  html += F(
    "<script>"
    "const rows=20, cols=3;"
    "function luxColor(v){"
      "if(v===null||isNaN(v)||v<=0)return '#000';"
      "if(v>10000)v=10000;"
      "let t=Math.log10(v)/4;"
      "let r=0,g=0;"
      "if(t<0.33){"
        "let u=t/0.33;"
        "g=255*u;"
      "}else if(t<0.66){"
        "let u=(t-0.33)/0.33;"
        "r=255*u;g=255;"
      "}else{"
        "let u=(t-0.66)/0.34;"
        "r=255;g=255*(1-u);"
      "}"
      "return `rgb(${r|0},${g|0},0)`;"
    "}"
    "function update(){"
      "fetch('/data').then(r=>r.json()).then(a=>{"
        "for(let dispRow=0;dispRow<rows;dispRow++){"
          "let logicalRow=(rows-1)-dispRow;"   // 19..0"
          "for(let col=0;col<cols;col++){"
            "let idx=dispRow*cols+col;"
            "let v=a[idx];"
            "let ce=document.getElementById(`c${logicalRow}_${col}`);"
            "let ve=document.getElementById(`v${logicalRow}_${col}`);"
            "if(ce)ce.setAttribute('fill',luxColor(v));"
            "if(ve)ve.textContent=(v===null||isNaN(v))?'ERR':v.toFixed(1);"
          "}"
        "}"
      "}).catch(e=>console.error(e));"
    "}"
    "setInterval(update,300);update();"
    "</script></body></html>"
  );

  server.send(200, "text/html", html);
}

// ----------------------------------------------------
// SETUP / LOOP
// ----------------------------------------------------
void setup() {
  Wire.begin();

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(statusLeds, LED_COUNT);
  FastLED.setBrightness(LED_BRIGHTNESS);
  applyLedColor();  // Start: alles aus

  resetAllSensors();

  // Sensoren konfigurieren (Continuous Mode)
  for (uint8_t m = 0; m < NUM_MUXES; m++) {
    for (uint8_t ch = 0; ch < MUX_CHANNEL_COUNT[m]; ch++) {
      selectMuxChannel(m, ch);
      for (uint8_t i = 0; i < NUM_SENSORS_PER_CHANNEL; i++) {
        if (sensor.setup(Wire, SENSOR_ADDR[i]) == 0 &&
            sensor.detect() == 0) {
          sensor.config_set(OPT3001_CONVERSION_TIME_100MS);
          sensor.conversion_continuous_enable();
        }
      }
    }
    disableMux(m);
  }

  ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER, ETH_PHY_MDC, ETH_PHY_MDIO,
            ETH_PHY_TYPE, ETH_CLK_MODE);

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/led", handleLed);
  server.begin();
}

void loop() {
  server.handleClient();

  static uint32_t last = 0;
  if (millis() - last > 100) {
    last = millis();
    updateLuxMatrix();
  }
}
