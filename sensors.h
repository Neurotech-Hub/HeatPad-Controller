#pragma once

#include <stdbool.h>
#include <stdint.h>

// Starts whichever single time-of-flight sensor is on the bus
// (VL53L0X, VL53L1X, or VL53L4CD) and the fuel gauge.
// Returns false when no supported sensor answers.
bool sensorsBegin();

// Restarts the "sensor went quiet" timer. Call after the boot splash,
// which ranges in the background without reading the results.
void sensorsStartWatchdog();

// False until the first sample, and false again if ranging goes quiet.
bool sensorsTofOk();

// Writes the latest sample and returns true only when a new one arrived.
// valid is false for out-of-range (nobody in front of the sensor).
bool sensorsPollRange(bool* valid, uint16_t* mm);

bool sensorsBatteryPresent();
void sensorsPollBattery();
float sensorsBatteryPercent();
float sensorsBatteryVolts();
const char* sensorsBatteryName();
const char* sensorsTofName();
