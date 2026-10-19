// Heat pad controller for the Adafruit ESP32-S3 TFT Feather.
//
// Libraries (Arduino Library Manager):
//   Adafruit ST7735 and ST7789 Library (pulls in Adafruit GFX)
//   Adafruit BusIO
//   Adafruit VL53L0X
//   Adafruit VL53L1X
//   STM32duino VL53L4CD
//   Adafruit MAX1704X
//   Adafruit LC709203F
//
// Board: "Adafruit Feather ESP32-S3 TFT"

#include <Arduino.h>
#include <Wire.h>
#include "driver/gpio.h"
#include <Adafruit_VL53L0X.h>
#include <Adafruit_VL53L1X.h>
#include <vl53l4cd_class.h>

#include "config.h"
#include "sensors.h"
#include "settings.h"
#include "ui.h"

#ifndef TFT_I2C_POWER
#error "Select the Adafruit Feather ESP32-S3 TFT board. TFT_I2C_POWER is missing."
#endif

static HeaterState heaterState = HEATER_IDLE;
static uint8_t nearCount = 0;
static uint8_t farCount = 0;
static uint32_t heatStartMs = 0;
static uint32_t phaseMs = 0;
static uint32_t coolStartMs = 0;
static bool holdCycle = false;
static bool holdOn = false;
static float coolFrom = 1.0f;
static bool pctTripped = false;
static bool voltTripped = false;
static uint32_t voltTripMs = 0;
static bool batteryTripped = false; 
static bool padEnabled = false;
static bool haveRange = false;
static bool rangeValid = false;
static uint16_t rangeMm = 0;

void setHeater(bool on) {
  // Push-pull. V_PAD is present only while the enable pin is driven high.
  pinMode(PIN_HEAT_EN, OUTPUT);
  digitalWrite(PIN_HEAT_EN, on ? HIGH : LOW);
  gpio_set_drive_capability((gpio_num_t)PIN_HEAT_EN, GPIO_DRIVE_CAP_3);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, on ? HIGH : LOW);
  padEnabled = on;

  static bool reported = false;
  static bool reportedOn = false;
  if (!reported || reportedOn != on) {
    reported = true;
    reportedOn = on;
    Serial.println(on ? F("V_PAD EN HIGH") : F("V_PAD EN LOW"));
  }
}

void heaterPinsInit() {
  // Latch low before the driver turns on so V_PAD cannot glitch on.
  digitalWrite(PIN_HEAT_EN, LOW);
  pinMode(PIN_HEAT_EN, OUTPUT);
  digitalWrite(PIN_HEAT_EN, LOW);
  gpio_set_drive_capability((gpio_num_t)PIN_HEAT_EN, GPIO_DRIVE_CAP_3);
  digitalWrite(PIN_LED, LOW);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
}

void railsOn() {
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);
#ifdef NEOPIXEL_POWER
  // The RGB LED is unused. Hold its power rail off.
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON ? LOW : HIGH);
#endif
  delay(10);
}

void applyBatteryCutoff() {
  if (!sensorsBatteryPresent()) {
    pctTripped = false;
    voltTripped = false;
    batteryTripped = false;
    return;
  }
  float volts = sensorsBatteryVolts();
  float pct = sensorsBatteryPercent();
  if (volts < BATT_V_MIN_VALID || volts > BATT_V_MAX_VALID) return;

  if (!pctTripped && pct < LOW_BATT_PCT) pctTripped = true;
  if (pctTripped && pct >= BATT_RECOVER_PCT) pctTripped = false;

  if (!voltTripped && volts < BATT_CUTOFF_V) {
    voltTripped = true;
    voltTripMs = millis();
    Serial.print(F("low voltage trip "));
    Serial.print(volts, 2);
    Serial.println(F(" V"));
  }
  if (voltTripped && (millis() - voltTripMs) >= BATT_LOCKOUT_MS &&
      volts >= BATT_RECOVER_V) {
    voltTripped = false;
    Serial.print(F("low voltage recovered "));
    Serial.print(volts, 2);
    Serial.println(F(" V"));
  }

  batteryTripped = pctTripped || voltTripped;
}

float heatReveal(uint32_t now) {
  if (!holdCycle) {
    if (HEAT_UP_MS == 0) return 1.0f;
    float reveal = (float)(now - heatStartMs) / (float)HEAT_UP_MS;
    return reveal > 1.0f ? 1.0f : reveal;
  }
  uint32_t window = holdOn ? HOLD_ON_MS : HOLD_OFF_MS;
  float t = window == 0 ? 1.0f : (float)(now - phaseMs) / (float)window;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  if (holdOn) return (1.0f - HOLD_DIP) + HOLD_DIP * t;
  return 1.0f - HOLD_DIP * t;
}

float coolReveal(uint32_t now) {
  if (COOL_MS == 0) return 0.0f;
  float t = (float)(now - coolStartMs) / (float)COOL_MS;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return coolFrom * (1.0f - t);
}

void beginHeating(uint32_t now, float fromLevel) {
  if (fromLevel < 0.0f) fromLevel = 0.0f;
  if (fromLevel > 1.0f) fromLevel = 1.0f;
  heaterState = HEATER_HEATING;
  nearCount = 0;
  farCount = 0;
  if (fromLevel >= 1.0f - HOLD_DIP) {
    // Still up to temperature, so keep pulsing instead of another heat-up.
    holdCycle = true;
    holdOn = true;
    phaseMs = now;
    heatStartMs = now - HEAT_UP_MS;
    return;
  }
  holdCycle = false;
  holdOn = true;
  if (fromLevel <= 0.0f || HEAT_UP_MS == 0) {
    heatStartMs = now;
  } else {
    heatStartMs = now - (uint32_t)(fromLevel * (float)HEAT_UP_MS);
  }
}

bool padCommand(uint32_t now) {
  if (!holdCycle) {
    if (now - heatStartMs >= HEAT_UP_MS) {
      holdCycle = true;
      holdOn = false;
      phaseMs = now;
      return false;
    }
    return true;
  }
  uint32_t window = holdOn ? HOLD_ON_MS : HOLD_OFF_MS;
  if (now - phaseMs >= window) {
    holdOn = !holdOn;
    phaseMs = now;
  }
  return holdOn;
}

void updateHeater(bool fresh) {
  HeaterState previous = heaterState;
  uint32_t now = millis();
  bool near = fresh && rangeValid && rangeMm <= (uint16_t)ON_CM * 10;
  bool far = fresh && (!rangeValid || rangeMm > (uint16_t)OFF_CM * 10);
  bool drivePad = false;

  if (!sensorsTofOk()) {
    heaterState = HEATER_SENSOR_ERR;
    holdCycle = false;
    nearCount = 0;
    farCount = 0;
  } else if (batteryTripped) {
    heaterState = HEATER_LOW_BATT;
    holdCycle = false;
    nearCount = 0;
    farCount = 0;
  } else if (heaterState == HEATER_COOLDOWN) {
    if (now - coolStartMs >= COOL_MS) {
      heaterState = HEATER_IDLE;
      holdCycle = false;
      nearCount = 0;
    } else if (near) {
      if (nearCount < 255) nearCount++;
      if (nearCount >= NEAR_COUNT) {
        beginHeating(now, coolReveal(now));
        drivePad = padCommand(now);
      }
    } else if (fresh) {
      nearCount = 0;
    }
  } else if (heaterState == HEATER_HEATING) {
    if (far) {
      if (farCount < 255) farCount++;
      if (farCount >= FAR_COUNT) {
        coolFrom = heatReveal(now);
        coolStartMs = now;
        heaterState = HEATER_COOLDOWN;
        holdCycle = false;
        farCount = 0;
        nearCount = 0;
      }
    } else if (fresh) {
      farCount = 0;
    }
    if (heaterState == HEATER_HEATING) drivePad = padCommand(now);
  } else {
    if (heaterState != HEATER_IDLE) heaterState = HEATER_IDLE;
    holdCycle = false;
    if (near) {
      if (nearCount < 255) nearCount++;
      if (nearCount >= NEAR_COUNT) beginHeating(now, 0.0f);
    } else if (fresh) {
      nearCount = 0;
    }
    if (heaterState == HEATER_HEATING) drivePad = true;
  }

  // High only while V_PAD should be on, including each hold-on second.
  setHeater(drivePad);

  if (heaterState != previous) {
    Serial.print(F("state "));
    Serial.print(heaterStateName(previous));
    Serial.print(F(" -> "));
    Serial.println(heaterStateName(heaterState));
  }
}

float revealLevel() {
  uint32_t now = millis();
  if (heaterState == HEATER_HEATING) return heatReveal(now);
  if (heaterState == HEATER_COOLDOWN) return coolReveal(now);
  return 0.0f;
}

void publishUi() {
  UiStatus status;
  status.sensorOk = sensorsTofOk();
  status.rangeValid = haveRange && rangeValid;
  status.distanceMm = rangeMm;
  status.batteryPresent = sensorsBatteryPresent();
  status.batteryPct = sensorsBatteryPercent();
  status.batteryVolts = sensorsBatteryVolts();
  status.state = heaterState;
  status.reveal = revealLevel();
  uiUpdate(status);
}

void setup() {
  heaterPinsInit();

  Serial.begin(115200);
  settingsLoad();
  railsOn();
  Wire.begin();

  bool tofOk = sensorsBegin();
  applyBatteryCutoff();

  uiBegin();
  uiSplash();

  uint32_t waitUntil = millis() + 200;
  while ((int32_t)(millis() - waitUntil) < 0) {
    if (sensorsPollRange(&rangeValid, &rangeMm)) {
      haveRange = true;
      break;
    }
    delay(5);
  }
  sensorsStartWatchdog();

  Serial.print(F("Heat Pad "));
  Serial.println(FW_VERSION);
  Serial.print(F("ToF: "));
  Serial.print(sensorsTofName());
  Serial.println(tofOk ? F(" ok") : F(" missing"));
  Serial.print(F("Battery: "));
  Serial.println(sensorsBatteryName());
  Serial.print(F("V_PAD EN GPIO "));
  Serial.print(PIN_HEAT_EN);
  Serial.println(F(" driven high to heat"));
  settingsPrint();

  updateHeater(false);
  publishUi();
}

void loop() {
  settingsPoll();

  bool fresh = sensorsPollRange(&rangeValid, &rangeMm);
  if (fresh) haveRange = true;

  static uint32_t lastBattMs = 0;
  uint32_t now = millis();
  uint32_t battPeriod = padEnabled ? BATT_FAST_POLL_MS : BATT_POLL_MS;
  if (now - lastBattMs >= battPeriod) {
    lastBattMs = now;
    sensorsPollBattery();
  }
  applyBatteryCutoff();
  updateHeater(fresh);

  static uint32_t lastUiMs = 0;
  if (now - lastUiMs >= UI_PERIOD_MS) {
    lastUiMs = now;
    publishUi();
  }
  delay(5);
}
