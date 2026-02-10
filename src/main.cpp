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

// ----------------------------------------------------
// Sensor- / Mux-Konfiguration
// ----------------------------------------------------
const uint8_t NUM_MUXES = 3;
const uint8_t MUX_ADDR[NUM_MUXES]          = { 0x70, 0x71, 0x72 };
const uint8_t MUX_CHANNEL_COUNT[NUM_MUXES] = { 8,    8,    4    };

const uint8_t NUM_SENSORS_PER_CHANNEL = 3;
const uint8_t SENSOR_ADDR[NUM_SENSORS_PER_CHANNEL] = { 0x44, 0x45, 0x46 };

#define TOTAL_ROWS 20

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
// Status-LEDs (grün wenn irgendein Sensor >= 200 lx)
// ----------------------------------------------------
void updateStatusLeds() {
  bool anyAbove = false;

  for (uint8_t r = 0; r < TOTAL_ROWS && !anyAbove; r++) {
    for (uint8_t c = 0; c < NUM_SENSORS_PER_CHANNEL; c++) {
      float v = luxMatrix[r][c];
      if (!isnan(v) && v >= 200.0f) {
        anyAbove = true;
        break;
      }
    }
  }

  CRGB color = anyAbove ? CRGB::Green : CRGB::Red;
  fill_solid(statusLeds, LED_COUNT, color);
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

  updateStatusLeds();
}

// ----------------------------------------------------
// JSON-Daten
// ----------------------------------------------------
void handleData() {
  String json = "[";
  for (uint8_t r = 0; r < TOTAL_ROWS; r++) {
    if (r) json += ",";
    json += "[";
    for (uint8_t c = 0; c < NUM_SENSORS_PER_CHANNEL; c++) {
      if (c) json += ",";
      float v = luxMatrix[r][c];
      json += isnan(v) ? "null" : String(v, 1);
    }
    json += "]";
  }
  json += "]";
  server.send(200, "application/json", json);
}

// ----------------------------------------------------
// WebUI – LOG-Skala: schwarz → grün → gelb → rot
// ----------------------------------------------------
void handleRoot() {
  String html;
  html.reserve(9000);

  html += F(
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{background:#111;color:#eee;font-family:Arial;text-align:center}"
    "svg{background:#222;margin:16px auto;border-radius:8px;display:block}"
    "circle{stroke:#444;stroke-width:1}"
    ".lbl{fill:#ccc;font-size:11px}"
    "</style></head><body>"
    "<h2>Lux-Matrix (logarithmisch)</h2>"
    "<p>0 lx = schwarz → grün → gelb → rot</p>"
  );

  const int cell = 40, rad = 14;
  html += "<svg width='180' height='860'>";

  for (int y = 0; y < TOTAL_ROWS; y++) {
    html += "<text class='lbl' x='5' y='" + String(40 + y * cell + 4) + "'>" + String(y) + "</text>";
    for (int x = 0; x < 3; x++) {
      String id = "c" + String(y) + "_" + String(x);
      html += "<circle id='" + id + "' cx='" + String(40 + x * cell) +
              "' cy='" + String(40 + y * cell) +
              "' r='" + String(rad) + "' fill='#000'/>";
    }
  }
  html += "</svg>";

  // ---------- JavaScript ----------
  html += F(
    "<script>"
    "function luxColor(v){"
      "if(v===null||isNaN(v)||v<=0)return '#000000';"
      "if(v>10000)v=10000;"
      // logarithmische Normierung
      "let t=Math.log10(v)/4.0;"   // 1..10000 → 0..1
      "if(t<0)t=0;if(t>1)t=1;"
      "let r,g,b;"
      "if(t<0.33){"              // schwarz -> grün
        "let u=t/0.33;"
        "r=0;"
        "g=Math.round(255*u);"
        "b=0;"
      "}else if(t<0.66){"        // grün -> gelb
        "let u=(t-0.33)/0.33;"
        "r=Math.round(255*u);"
        "g=255;"
        "b=0;"
      "}else{"                   // gelb -> rot
        "let u=(t-0.66)/0.34;"
        "r=255;"
        "g=Math.round(255*(1-u));"
        "b=0;"
      "}"
      "return 'rgb('+r+','+g+','+b+')';"
    "}"
    "function upd(){fetch('/data').then(r=>r.json()).then(d=>{"
      "for(let y=0;y<d.length;y++){"
        "for(let x=0;x<d[y].length;x++){"
          "let v=d[y][x];"
          "let e=document.getElementById('c'+y+'_'+x);"
          "if(!e)continue;"
          "e.setAttribute('fill',luxColor(v));"
          "if(v!==null)e.setAttribute('title',v.toFixed(1)+' lx');"
        "}"
      "}"
    "});}"
    "setInterval(upd,300);upd();"
    "</script></body></html>"
  );

  server.send(200, "text/html", html);
}

// ----------------------------------------------------
// SETUP
// ----------------------------------------------------
void setup() {
  Wire.begin();

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(statusLeds, LED_COUNT);
  FastLED.setBrightness(LED_BRIGHTNESS);
  fill_solid(statusLeds, LED_COUNT, CRGB::Red);
  FastLED.show();

  resetAllSensors();

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
  server.begin();
}

// ----------------------------------------------------
// LOOP
// ----------------------------------------------------
void loop() {
  server.handleClient();

  static uint32_t last = 0;
  if (millis() - last > 100) {
    last = millis();
    updateLuxMatrix();
  }
}
