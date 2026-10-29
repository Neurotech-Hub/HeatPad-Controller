#pragma once

#include <stdint.h>
#include "config.h"

struct UiStatus {
  bool sensorOk;
  bool rangeValid;
  uint16_t distanceMm;
  bool batteryPresent;
  float batteryPct;
  float batteryVolts;
  HeaterState state;
  float reveal;
};

void uiBegin();
void uiSplash();
void uiUpdate(const UiStatus& status);
const char* heaterStateName(HeaterState state);
