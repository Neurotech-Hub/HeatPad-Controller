#include "sensors.h"

#include <Wire.h>
#include <stdio.h>

#include <Adafruit_VL53L0X.h>
#include <Adafruit_VL53L1X.h>
#include <vl53l4cd_class.h>
#include <Adafruit_MAX1704X.h>
#include <Adafruit_LC709203F.h>

#include "config.h"

// One distance sensor is expected. These are the single-point parts that
// share the STEMMA QT bus; the fuel-gauge addresses are skipped while scanning.
enum TofKind : uint8_t {
  TOF_NONE = 0,
  TOF_VL53L0X,
  TOF_VL53L1X,
  TOF_VL53L4CD,
  TOF_UNKNOWN
};

static Adafruit_VL53L0X tofL0;
static Adafruit_VL53L1X tofL1;
static VL53L4CD tofL4(&Wire, -1);

static TofKind tofKind = TOF_NONE;
static uint8_t tofAddr = 0;
static uint16_t tofId = 0;
static bool tofStarted = false;
static uint32_t lastSampleMs = 0;

static Adafruit_MAX17048 max17048;
static Adafruit_LC709203F lc709203;

enum GaugeKind : uint8_t { GAUGE_NONE = 0, GAUGE_MAX17048, GAUGE_LC709203F };
static GaugeKind gauge = GAUGE_NONE;
static float battPct = 0.0f;
static float battVolts = 0.0f;

static lc709203_adjustment_t packProfile() {
  if (BATT_PACK_MAH <= 100) return LC709203F_APA_100MAH;
  if (BATT_PACK_MAH <= 200) return LC709203F_APA_200MAH;
  if (BATT_PACK_MAH <= 500) return LC709203F_APA_500MAH;
  if (BATT_PACK_MAH <= 1000) return LC709203F_APA_1000MAH;
  if (BATT_PACK_MAH <= 2000) return LC709203F_APA_2000MAH;
  return LC709203F_APA_3000MAH;
}

static float clampPercent(float pct) {
  if (pct < 0.0f) return 0.0f;
  if (pct > 100.0f) return 100.0f;
  return pct;
}

static void prepareBus() {
#if defined(ARDUINO_ARCH_ESP32)
  // Init and calibration clock-stretch longer than the default timeout.
  Wire.setTimeOut(500);
#endif
  Wire.setClock(100000);
}

static bool isGaugeAddress(uint8_t addr) {
  return addr == 0x0B || addr == 0x36;
}

static bool i2cRead(uint8_t addr, const uint8_t* index, uint8_t indexLen, uint8_t* data, uint8_t dataLen) {
  Wire.beginTransmission(addr);
  for (uint8_t i = 0; i < indexLen; i++) Wire.write(index[i]);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, dataLen) != dataLen) return false;
  for (uint8_t i = 0; i < dataLen; i++) data[i] = (uint8_t)Wire.read();
  return true;
}

static void i2cIdle(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.endTransmission();
}

// VL53L0X is probed first. Its register address is one byte, and a two-byte
// index written to it would land in a register.
static TofKind identifyTof(uint8_t addr, uint16_t* idOut) {
  uint8_t model = 0;
  const uint8_t l0ModelReg = 0xC0;
  if (i2cRead(addr, &l0ModelReg, 1, &model, 1) && model == 0xEE) {
    *idOut = 0x00EE;
    return TOF_VL53L0X;
  }
  i2cIdle(addr);

  const uint8_t familyReg[] = {0x01, 0x0F};
  uint8_t idBytes[2] = {0, 0};
  if (!i2cRead(addr, familyReg, 2, idBytes, 2)) {
    i2cIdle(addr);
    return TOF_NONE;
  }
  uint16_t id = ((uint16_t)idBytes[0] << 8) | idBytes[1];
  *idOut = id;
  if (id == 0xEACC) return TOF_VL53L1X;
  if (id == 0xEBAA) return TOF_VL53L4CD;
  if (id == 0x0000 || id == 0xFFFF) return TOF_NONE;
  return TOF_UNKNOWN;
}

static bool findTof() {
  tofKind = TOF_NONE;
  tofAddr = 0;
  tofId = 0;
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    if (isGaugeAddress(addr)) continue;
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) continue;

    uint16_t id = 0;
    TofKind kind = identifyTof(addr, &id);
    if (kind == TOF_NONE) continue;
    tofKind = kind;
    tofAddr = addr;
    tofId = id;
    return kind != TOF_UNKNOWN;
  }
  return false;
}

static uint16_t l1BudgetMs() {
  const uint16_t presets[] = {15, 20, 33, 50, 100, 200, 500};
  uint16_t budget = 15;
  for (uint8_t i = 0; i < sizeof(presets) / sizeof(presets[0]); i++) {
    if (presets[i] <= TOF_PERIOD_MS) budget = presets[i];
  }
  return budget;
}

static bool startDetectedTof() {
  switch (tofKind) {
    case TOF_VL53L0X:
      return tofL0.begin(tofAddr, false, &Wire, Adafruit_VL53L0X::VL53L0X_SENSE_DEFAULT) &&
             tofL0.startRangeContinuous(TOF_PERIOD_MS);
    case TOF_VL53L1X: {
      if (!tofL1.begin(tofAddr, &Wire, false)) return false;
      uint16_t budget = l1BudgetMs();
      if (!tofL1.setTimingBudget(budget)) return false;
      if (tofL1.VL53L1X_SetInterMeasurementInMs(budget) != VL53L1X_ERROR_NONE) return false;
      return tofL1.startRanging();
    }
    case TOF_VL53L4CD: {
      // The driver stores the 8-bit address (0x29 << 1).
      uint32_t budget = TOF_PERIOD_MS;
      if (budget < 10) budget = 10;
      if (budget > 200) budget = 200;
      if (tofL4.InitSensor((uint8_t)(tofAddr << 1)) != VL53L4CD_ERROR_NONE) return false;
      if (tofL4.VL53L4CD_SetRangeTiming(budget, 0) != VL53L4CD_ERROR_NONE) return false;
      return tofL4.VL53L4CD_StartRanging() == VL53L4CD_ERROR_NONE;
    }
    default:
      return false;
  }
}

static bool acceptSample(bool trustworthy, uint16_t mm, bool* valid, uint16_t* outMm) {
  lastSampleMs = millis();
  *valid = trustworthy && mm < 8000;
  *outMm = mm;
  return true;
}

static bool pollDetectedTof(bool* valid, uint16_t* mm) {
  switch (tofKind) {
    case TOF_VL53L0X: {
      // A failed ready-check also returns true. Reject that before reading.
      if (!tofL0.isRangeComplete() || tofL0.Status != VL53L0X_ERROR_NONE) return false;
      uint16_t rangeMm = tofL0.readRangeResult();
      if (tofL0.Status != VL53L0X_ERROR_NONE) return false;
      uint8_t status = tofL0.readRangeStatus();
      // 0 is range-valid. 3 is range-valid but closer than the minimum.
      return acceptSample((status == 0 || status == 3), rangeMm, valid, mm);
    }
    case TOF_VL53L1X: {
      uint8_t ready = 0;
      if (tofL1.VL53L1X_CheckForDataReady(&ready) != VL53L1X_ERROR_NONE || ready == 0) return false;
      uint8_t status = 255;
      uint16_t rangeMm = 0;
      if (tofL1.VL53L1X_GetRangeStatus(&status) != VL53L1X_ERROR_NONE) return false;
      if (tofL1.VL53L1X_GetDistance(&rangeMm) != VL53L1X_ERROR_NONE) return false;
      if (tofL1.VL53L1X_ClearInterrupt() != VL53L1X_ERROR_NONE) return false;
      return acceptSample((status == 0 || status == 3), rangeMm, valid, mm);
    }
    case TOF_VL53L4CD: {
      uint8_t ready = 0;
      if (tofL4.VL53L4CD_CheckForDataReady(&ready) != VL53L4CD_ERROR_NONE || ready == 0) return false;
      VL53L4CD_Result_t result;
      if (tofL4.VL53L4CD_GetResult(&result) != VL53L4CD_ERROR_NONE) return false;
      if (tofL4.VL53L4CD_ClearInterrupt() != VL53L4CD_ERROR_NONE) return false;
      return acceptSample(result.range_status == 0, result.distance_mm, valid, mm);
    }
    default:
      return false;
  }
}

bool sensorsBegin() {
  tofStarted = false;
  tofKind = TOF_NONE;
  gauge = GAUGE_NONE;
  prepareBus();

  if (findTof() && startDetectedTof()) {
    tofStarted = true;
    lastSampleMs = millis();
  }

  if (max17048.begin(&Wire)) {
    gauge = GAUGE_MAX17048;
    // HibThr and ActThr at 0 disable auto-hibernate, so the gauge measures
    // about every 250 ms instead of about every 45 s.
    max17048.wake();
  } else if (lc709203.begin(&Wire)) {
    gauge = GAUGE_LC709203F;
    // Feather battery jack uses a 10 kOhm thermistor, B ~= 3950.
    lc709203.setThermistorB(3950);
    lc709203.setPackSize(packProfile());
  }

  if (gauge != GAUGE_NONE) {
    sensorsPollBattery();
  }
  return tofStarted;
}

bool sensorsTofOk() {
  if (!tofStarted) return false;
  return (millis() - lastSampleMs) < SENSOR_STALE_MS;
}

void sensorsStartWatchdog() {
  lastSampleMs = millis();
}

bool sensorsPollRange(bool* valid, uint16_t* mm) {
  if (!tofStarted || valid == nullptr || mm == nullptr) return false;
  return pollDetectedTof(valid, mm);
}

bool sensorsBatteryPresent() {
  return gauge != GAUGE_NONE;
}

void sensorsPollBattery() {
  if (gauge == GAUGE_MAX17048) {
    battVolts = max17048.cellVoltage();
    battPct = clampPercent(max17048.cellPercent());
  } else if (gauge == GAUGE_LC709203F) {
    battVolts = lc709203.cellVoltage();
    battPct = clampPercent(lc709203.cellPercent());
  }
}

float sensorsBatteryPercent() {
  return battPct;
}

float sensorsBatteryVolts() {
  return battVolts;
}

const char* sensorsBatteryName() {
  if (gauge == GAUGE_MAX17048) return "MAX17048";
  if (gauge == GAUGE_LC709203F) return "LC709203F";
  return "none";
}

const char* sensorsTofName() {
  switch (tofKind) {
    case TOF_VL53L0X:
      return "VL53L0X";
    case TOF_VL53L1X:
      return "VL53L1X";
    case TOF_VL53L4CD:
      return "VL53L4CD";
    case TOF_UNKNOWN: {
      static char name[16];
      snprintf(name, sizeof(name), "id 0x%04X", tofId);
      return name;
    }
    case TOF_NONE:
    default:
      return "none";
  }
}
