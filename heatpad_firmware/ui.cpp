#include "ui.h"

#include <Arduino.h>
#ifndef TFT_I2C_POWER
#error "Select the Adafruit Feather ESP32-S3 TFT board. TFT_I2C_POWER is missing."
#endif
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

static Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

static const int SCREEN_W = 240;
static const int SCREEN_H = 135;
static const int BATT_BAND_H = 32;
static const int DIST_Y = 34;
static const int DIST_H = 64;
static const int HEART_CX = 214;
static const int HEART_CY = 116;
static const int HEART_SCALE = 12;

static bool painted = false;
static char lastDist[16] = "";
static char lastState[16] = "";
static int lastPct = -999;
static int lastCentivolts = -1;
static bool lastBattPresent = false;
static int lastRevealBucket = -1;
static HeaterState lastHeartState = HEATER_IDLE;

const char* heaterStateName(HeaterState state) {
  switch (state) {
    case HEATER_HEATING:
      return "HEATING";
    case HEATER_COOLDOWN:
      return "COOLDOWN";
    case HEATER_LOW_BATT:
      return "LOW BATT";
    case HEATER_SENSOR_ERR:
      return "SENSOR ERR";
    case HEATER_IDLE:
    default:
      return "IDLE";
  }
}

static bool heartContains(float nx, float ny) {
  float x2 = nx * nx;
  float y2 = ny * ny;
  float a = x2 + y2 - 1.0f;
  return (a * a * a - x2 * y2 * ny) <= 0.0f;
}

static void paintHeartRun(int x0, int x1, int y, bool filled, uint16_t fillColor, uint16_t edgeColor) {
  if (x1 < x0) return;
  if (filled) {
    tft.drawFastHLine(x0, y, x1 - x0 + 1, fillColor);
    return;
  }
  tft.drawPixel(x0, y, edgeColor);
  if (x1 - x0 >= 2) {
    tft.drawFastHLine(x0 + 1, y, x1 - x0 - 1, ST77XX_BLACK);
  }
  if (x1 != x0) {
    tft.drawPixel(x1, y, edgeColor);
  }
}

// fill01 grows from the point of the heart upward. 0 is outline only.
static void drawHeart(int cx, int cy, int scale, float fill01, uint16_t fillColor, uint16_t edgeColor) {
  if (scale < 1) return;
  if (fill01 < 0.0f) fill01 = 0.0f;
  if (fill01 > 1.0f) fill01 = 1.0f;

  int x0 = cx - (int)(scale * 1.25f) - 2;
  int x1 = cx + (int)(scale * 1.25f) + 2;
  int y0 = cy - (int)(scale * 1.40f) - 2;
  int y1 = cy + (int)(scale * 1.10f) + 2;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 >= tft.width()) x1 = tft.width() - 1;
  if (y1 >= tft.height()) y1 = tft.height() - 1;

  // ny is up. The point is near ny = -1, the lobes near ny = +1.2.
  const float nyCut = -1.05f + fill01 * 2.45f;

  for (int py = y0; py <= y1; py++) {
    float ny = (float)(cy - py) / (float)scale;
    int runStart = -1;
    for (int px = x0; px <= x1; px++) {
      float nx = (float)(px - cx) / (float)scale;
      bool inside = heartContains(nx, ny);
      if (inside && runStart < 0) runStart = px;
      if (runStart >= 0 && (!inside || px == x1)) {
        int runEnd = inside ? px : px - 1;
        paintHeartRun(runStart, runEnd, py, ny <= nyCut, fillColor, edgeColor);
        runStart = -1;
      }
    }
  }
}

static void drawCentered(const char* text, int y, uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color);
  int16_t bx, by;
  uint16_t bw, bh;
  tft.getTextBounds(text, 0, y, &bx, &by, &bw, &bh);
  int x = (tft.width() - (int)bw) / 2;
  if (x < 0) x = 0;
  tft.setCursor(x, y);
  tft.print(text);
}

static uint16_t batteryColor(float pct, float volts) {
  if (pct < LOW_BATT_PCT || volts < BATT_CUTOFF_V) return ST77XX_RED;
  if (pct < 30.0f) return ST77XX_YELLOW;
  return ST77XX_GREEN;
}

static uint16_t stateColor(HeaterState state) {
  switch (state) {
    case HEATER_HEATING:
    case HEATER_SENSOR_ERR:
      return ST77XX_RED;
    case HEATER_COOLDOWN:
    case HEATER_LOW_BATT:
      return ST77XX_YELLOW;
    case HEATER_IDLE:
    default:
      return ST77XX_WHITE;
  }
}

static void drawBatteryIcon(int x, int y, float pct, uint16_t color) {
  const int w = 26;
  const int h = 12;
  tft.drawRect(x, y, w, h, ST77XX_WHITE);
  tft.fillRect(x + w, y + 3, 3, h - 6, ST77XX_WHITE);
  int inner = w - 4;
  int fill = (int)((pct / 100.0f) * (float)inner + 0.5f);
  if (fill < 0) fill = 0;
  if (fill > inner) fill = inner;
  tft.fillRect(x + 2, y + 2, inner, h - 4, ST77XX_BLACK);
  if (fill > 0) {
    tft.fillRect(x + 2, y + 2, fill, h - 4, color);
  }
}

static void drawBattery(const UiStatus& status) {
  tft.fillRect(0, 0, SCREEN_W, BATT_BAND_H, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextWrap(false);

  if (!status.batteryPresent) {
    drawBatteryIcon(8, 10, 0, ST77XX_WHITE);
    tft.setCursor(44, 10);
    tft.setTextColor(ST77XX_WHITE);
    tft.print("n/a");
    return;
  }

  uint16_t color = batteryColor(status.batteryPct, status.batteryVolts);
  drawBatteryIcon(8, 10, status.batteryPct, color);

  int pct = (int)(status.batteryPct + 0.5f);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  int centivolts = (int)(status.batteryVolts * 100.0f + 0.5f);
  if (centivolts < 0) centivolts = 0;

  char line[20];
  snprintf(line, sizeof(line), "%d%%", pct);
  tft.setCursor(44, 10);
  tft.setTextColor(color);
  tft.print(line);

  snprintf(line, sizeof(line), "%d.%02dV", centivolts / 100, centivolts % 100);
  tft.setCursor(150, 10);
  tft.print(line);
}

static void formatDistance(const UiStatus& status, char* buf, size_t len) {
  if (!status.sensorOk) {
    snprintf(buf, len, "SENSOR ERR");
    return;
  }
  if (!status.rangeValid) {
    snprintf(buf, len, "---");
    return;
  }
  unsigned cm = (unsigned)((status.distanceMm + 5U) / 10U);
  snprintf(buf, len, "%u cm", cm);
}

static void drawDistance(const UiStatus& status) {
  tft.fillRect(0, DIST_Y, SCREEN_W, DIST_H, ST77XX_BLACK);
  if (status.state == HEATER_LOW_BATT) {
    int y = DIST_Y + (DIST_H - 24) / 2;
    drawCentered("LOW BATTERY", y, 3, ST77XX_YELLOW);
    return;
  }
  char buf[16];
  formatDistance(status, buf, sizeof(buf));
  bool fault = !status.sensorOk;
  uint8_t size = fault ? 3 : 4;
  int textH = fault ? 24 : 32;
  int y = DIST_Y + (DIST_H - textH) / 2;
  uint16_t color = ST77XX_WHITE;
  if (fault) color = ST77XX_RED;
  else if (status.state == HEATER_HEATING) color = ST77XX_RED;
  drawCentered(buf, y, size, color);
}

static void drawState(const UiStatus& status) {
  tft.fillRect(0, 100, 188, SCREEN_H - 100, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(stateColor(status.state));
  tft.setCursor(8, 108);
  tft.print(heaterStateName(status.state));
}

static void drawStatusHeart(const UiStatus& status) {
  uint16_t fill = ST77XX_RED;
  uint16_t edge = ST77XX_WHITE;
  if (status.state == HEATER_COOLDOWN) edge = ST77XX_YELLOW;
  if (status.state == HEATER_LOW_BATT || status.state == HEATER_SENSOR_ERR) {
    edge = ST77XX_YELLOW;
  }
  drawHeart(HEART_CX, HEART_CY, HEART_SCALE, status.reveal, fill, edge);
}

void uiBegin() {
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);
  tft.init(135, 240);
  // Landscape, 180 degrees from rotation 3, so the picture matches the board.
  tft.setRotation(1);
  tft.setTextWrap(false);
  tft.fillScreen(ST77XX_BLACK);
  painted = false;
}

static void traceHeart(int cx, int cy, float scale) {
  const int steps = 90;
  for (int i = 0; i <= steps; i++) {
    float t = (float)i / (float)steps * 2.0f * PI;
    float s = sinf(t);
    float x = 16.0f * s * s * s;
    float y = 13.0f * cosf(t) - 5.0f * cosf(2.0f * t) - 2.0f * cosf(3.0f * t) - cosf(4.0f * t);
    int px = cx + (int)(x * scale / 16.0f);
    int py = cy - (int)(y * scale / 17.0f);
    tft.fillCircle(px, py, 2, ST77XX_RED);
    delay(5);
  }
}

void uiSplash() {
  tft.fillScreen(ST77XX_BLACK);
  const int cx = 120;
  const int cy = 48;
  const int scale = 34;

  traceHeart(cx, cy, (float)scale);
  tft.fillScreen(ST77XX_BLACK);

  for (int step = 0; step <= 8; step++) {
    drawHeart(cx, cy, scale, (float)step / 8.0f, ST77XX_RED, ST77XX_RED);
    delay(30);
  }

  for (int pulse = 0; pulse < 2; pulse++) {
    drawHeart(cx, cy, scale, 1.0f, ST77XX_WHITE, ST77XX_WHITE);
    delay(150);
    drawHeart(cx, cy, scale, 1.0f, ST77XX_RED, ST77XX_RED);
    delay(150);
  }

  drawCentered("Heat Pad", 96, 3, ST77XX_WHITE);
  char version[16];
  snprintf(version, sizeof(version), "v%s", FW_VERSION);
  drawCentered(version, 122, 1, ST77XX_WHITE);
  delay(700);

  tft.fillScreen(ST77XX_BLACK);
  painted = false;
  lastDist[0] = '\0';
  lastState[0] = '\0';
  lastPct = -999;
  lastCentivolts = -1;
  lastBattPresent = false;
  lastRevealBucket = -1;
}

void uiUpdate(const UiStatus& status) {
  char dist[16];
  formatDistance(status, dist, sizeof(dist));
  const char* stateName = heaterStateName(status.state);
  int pct = status.batteryPresent ? (int)(status.batteryPct + 0.5f) : 0;
  int centivolts = status.batteryPresent ? (int)(status.batteryVolts * 100.0f + 0.5f) : 0;
  int revealBucket = (int)(status.reveal * 40.0f + 0.5f);

  bool battChanged = !painted || status.batteryPresent != lastBattPresent || pct != lastPct ||
                     centivolts != lastCentivolts;
  bool distChanged = !painted || strcmp(dist, lastDist) != 0 || status.state != lastHeartState;
  bool stateChanged = !painted || strcmp(stateName, lastState) != 0;
  bool heartChanged = !painted || revealBucket != lastRevealBucket || status.state != lastHeartState;

  if (!painted) {
    tft.fillScreen(ST77XX_BLACK);
  }
  if (battChanged) drawBattery(status);
  if (distChanged) drawDistance(status);
  if (stateChanged) drawState(status);
  if (heartChanged) drawStatusHeart(status);

  strncpy(lastDist, dist, sizeof(lastDist) - 1);
  lastDist[sizeof(lastDist) - 1] = '\0';
  strncpy(lastState, stateName, sizeof(lastState) - 1);
  lastState[sizeof(lastState) - 1] = '\0';
  lastPct = pct;
  lastCentivolts = centivolts;
  lastBattPresent = status.batteryPresent;
  lastRevealBucket = revealBucket;
  lastHeartState = status.state;
  painted = true;
}
