#pragma once

#include <stdint.h>

// Heat Pad firmware configuration.
// Board: Adafruit ESP32-S3 TFT Feather (#5483), Arduino IDE,
// board menu "Adafruit Feather ESP32-S3 TFT" (Espressif esp32 core).
//
// Wiring
// ------
// One LiPo feeds the Feather JST connector and the Pololu U3V70A VIN.
// U3V70A VOUT (V_PAD) feeds the heat pad. Common ground between
// the Feather, the regulator, and the pad.
//
//   Time-of-flight       Feather STEMMA QT (I2C). One of VL53L0X,
//                        VL53L1X, or VL53L4CD. Default address 0x29.
//   U3V70A ENABLE        PIN_HEAT_EN. High = V_PAD on, low = off.
//   Indicator LED        GPIO 13 (on-board red LED, or an external LED)
//
// Set the U3V70A VOUT to V_PAD with its potentiometer BEFORE connecting
// the pad. Confirm VOUT with a meter. Clockwise raises the output. Do not
// let VIN exceed the V_PAD setting or the pad will see the battery voltage.
//
// V_PAD is present only while the enable pin is driven high. The firmware
// holds that pin low until a person is within ON_CM, drives it high until
// the pad is up to temperature, then pulses it while they remain in range.
//
// The boost draws more current from the cell than the pad draws at V_PAD.
// The pack has to be rated for that. There is no temperature sensor:
// a short heat-up, then on-off, keeps the pad from running away.

constexpr char FW_VERSION[] = "1.0.0";

// V_PAD enable. High turns the regulator output on.
constexpr uint8_t PIN_HEAT_EN = 16;
constexpr uint8_t PIN_LED = 13;

// Factory defaults. Serial "set" replaces the live copies and stores them
// in flash. See settings.cpp. Distances are centimeters.
constexpr uint16_t DEFAULT_ON_CM = 35;
constexpr uint16_t DEFAULT_OFF_CM = 40;
constexpr uint32_t DEFAULT_HEAT_UP_MS = 20000UL;
constexpr uint32_t DEFAULT_HOLD_ON_MS = 100UL;
constexpr uint32_t DEFAULT_HOLD_OFF_MS = 100UL;
constexpr uint32_t DEFAULT_COOL_MS = 10000UL;

extern uint16_t ON_CM;
extern uint16_t OFF_CM;
extern uint32_t HEAT_UP_MS;
extern uint32_t HOLD_ON_MS;
extern uint32_t HOLD_OFF_MS;
extern uint32_t COOL_MS;

// Consecutive fresh range samples required before the output changes.
constexpr uint8_t NEAR_COUNT = 3;
constexpr uint8_t FAR_COUNT = 5;

// During a hold-off interval the heart eases down by this fraction only.
constexpr float HOLD_DIP = 0.08f;

// Continuous ranging period. 50 ms is about 20 Hz.
constexpr uint16_t TOF_PERIOD_MS = 50;
// No completed range for this long means the sensor is dead.
constexpr uint32_t SENSOR_STALE_MS = 1000UL;

constexpr uint32_t BATT_POLL_MS = 2000UL;
// Faster gauge reads while V_PAD is on, to catch the sag under load.
constexpr uint32_t BATT_FAST_POLL_MS = 250UL;
constexpr uint32_t UI_PERIOD_MS = 100UL;

// Cut the heater off below LOW_BATT_PCT and allow it again at this level.
constexpr float LOW_BATT_PCT = 10.0f;
constexpr float BATT_RECOVER_PCT = 15.0f;
// Cut the heater off when the cell (under load) reads below BATT_CUTOFF_V.
// Allow it again only after BATT_LOCKOUT_MS and once the resting cell is
// back above BATT_RECOVER_V. Keeps the pack protection from tripping.
constexpr float BATT_CUTOFF_V = 3.60f;
constexpr float BATT_RECOVER_V = 3.85f;
constexpr uint32_t BATT_LOCKOUT_MS = 30000UL;
// Ignore a fuel-gauge sample outside this window (USB-only, no cell).
constexpr float BATT_V_MIN_VALID = 2.50f;
constexpr float BATT_V_MAX_VALID = 4.50f;

// LC709203F pack profile (early Feathers). Snaps to 100, 200, 500,
// 1000, 2000, or 3000 mAh. Ignored when the board has a MAX17048.
constexpr uint16_t BATT_PACK_MAH = 2000;

enum HeaterState : uint8_t {
  HEATER_IDLE = 0,
  HEATER_HEATING,
  HEATER_COOLDOWN,
  HEATER_LOW_BATT,
  HEATER_SENSOR_ERR
};
