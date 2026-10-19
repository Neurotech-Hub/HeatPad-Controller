#include "settings.h"

#include <Arduino.h>
#include <Preferences.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

uint16_t ON_CM = DEFAULT_ON_CM;
uint16_t OFF_CM = DEFAULT_OFF_CM;
uint32_t HEAT_UP_MS = DEFAULT_HEAT_UP_MS;
uint32_t HOLD_ON_MS = DEFAULT_HOLD_ON_MS;
uint32_t HOLD_OFF_MS = DEFAULT_HOLD_OFF_MS;
uint32_t COOL_MS = DEFAULT_COOL_MS;

static bool fromFlash = false;
static Preferences prefs;

static const char* NVS_NS = "heatpad";
static const uint32_t TIME_MIN_MS = 50;
static const uint32_t TIME_MAX_MS = 60000;
static const uint16_t DIST_MIN_CM = 1;
static const uint16_t DIST_MAX_CM = 200;

static char line[64];
static uint8_t lineLen = 0;
static bool discardLine = false;

static void applyDefaults() {
  ON_CM = DEFAULT_ON_CM;
  OFF_CM = DEFAULT_OFF_CM;
  HEAT_UP_MS = DEFAULT_HEAT_UP_MS;
  HOLD_ON_MS = DEFAULT_HOLD_ON_MS;
  HOLD_OFF_MS = DEFAULT_HOLD_OFF_MS;
  COOL_MS = DEFAULT_COOL_MS;
}

static bool timeOk(uint32_t ms) {
  return ms >= TIME_MIN_MS && ms <= TIME_MAX_MS;
}

static bool distOk(uint32_t cm) {
  return cm >= DIST_MIN_CM && cm <= DIST_MAX_CM;
}

static bool valuesOk() {
  return timeOk(HEAT_UP_MS) && timeOk(HOLD_ON_MS) && timeOk(HOLD_OFF_MS) &&
         timeOk(COOL_MS) && distOk(ON_CM) && distOk(OFF_CM) && ON_CM < OFF_CM;
}

static void saveAll() {
  prefs.begin(NVS_NS, false);
  prefs.putUInt("heat", HEAT_UP_MS);
  prefs.putUInt("on", HOLD_ON_MS);
  prefs.putUInt("off", HOLD_OFF_MS);
  prefs.putUInt("near", ON_CM);
  prefs.putUInt("far", OFF_CM);
  prefs.putUInt("cool", COOL_MS);
  prefs.putBool("saved", true);
  prefs.end();
  fromFlash = true;
}

void settingsLoad() {
  applyDefaults();
  fromFlash = false;

  prefs.begin(NVS_NS, true);
  bool saved = prefs.getBool("saved", false);
  if (saved) {
    HEAT_UP_MS = prefs.getUInt("heat", DEFAULT_HEAT_UP_MS);
    HOLD_ON_MS = prefs.getUInt("on", DEFAULT_HOLD_ON_MS);
    HOLD_OFF_MS = prefs.getUInt("off", DEFAULT_HOLD_OFF_MS);
    ON_CM = (uint16_t)prefs.getUInt("near", DEFAULT_ON_CM);
    OFF_CM = (uint16_t)prefs.getUInt("far", DEFAULT_OFF_CM);
    COOL_MS = prefs.getUInt("cool", DEFAULT_COOL_MS);
    fromFlash = true;
  }
  prefs.end();

  if (fromFlash && !valuesOk()) {
    applyDefaults();
    fromFlash = false;
  }
}

void settingsPrint() {
  Serial.println(fromFlash ? F("source flash") : F("source defaults"));
  Serial.print(F("heat "));
  Serial.print(HEAT_UP_MS);
  Serial.println(F(" ms"));
  Serial.print(F("on "));
  Serial.print(HOLD_ON_MS);
  Serial.println(F(" ms"));
  Serial.print(F("off "));
  Serial.print(HOLD_OFF_MS);
  Serial.println(F(" ms"));
  Serial.print(F("near "));
  Serial.print(ON_CM);
  Serial.println(F(" cm"));
  Serial.print(F("far "));
  Serial.print(OFF_CM);
  Serial.println(F(" cm"));
  Serial.print(F("cool "));
  Serial.print(COOL_MS);
  Serial.println(F(" ms"));
}

static void printHelp() {
  Serial.println(F("get"));
  Serial.println(F("set heat <ms>"));
  Serial.println(F("set on <ms>"));
  Serial.println(F("set off <ms>"));
  Serial.println(F("set near <cm>"));
  Serial.println(F("set far <cm>"));
  Serial.println(F("set cool <ms>"));
  Serial.println(F("defaults"));
  Serial.println(F("help"));
  Serial.println(F("Times 50-60000 ms. Distances 1-200 cm. near must be below far."));
}

static bool sameWord(const char* a, const char* b) {
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

static bool parseU32(const char* token, uint32_t* out) {
  if (token == nullptr || !isdigit((unsigned char)token[0])) return false;
  char* end = nullptr;
  unsigned long value = strtoul(token, &end, 10);
  if (end == token || *end != '\0') return false;
  *out = (uint32_t)value;
  return true;
}

static void restoreDefaults() {
  prefs.begin(NVS_NS, false);
  prefs.clear();
  prefs.end();
  applyDefaults();
  fromFlash = false;
  Serial.println(F("defaults restored"));
  settingsPrint();
}

static void handleSet(char* name, char* valueToken) {
  uint32_t value = 0;
  if (name == nullptr || valueToken == nullptr || !parseU32(valueToken, &value)) {
    Serial.println(F("error usage: set <name> <number>"));
    return;
  }

  if (sameWord(name, "heat") || sameWord(name, "on") || sameWord(name, "off") ||
      sameWord(name, "cool")) {
    if (!timeOk(value)) {
      Serial.println(F("error time must be 50-60000 ms"));
      return;
    }
    if (sameWord(name, "heat")) HEAT_UP_MS = value;
    else if (sameWord(name, "on")) HOLD_ON_MS = value;
    else if (sameWord(name, "off")) HOLD_OFF_MS = value;
    else COOL_MS = value;
  } else if (sameWord(name, "near") || sameWord(name, "far")) {
    if (!distOk(value)) {
      Serial.println(F("error distance must be 1-200 cm"));
      return;
    }
    uint16_t cm = (uint16_t)value;
    if (sameWord(name, "near") && cm >= OFF_CM) {
      Serial.println(F("error near must be below far"));
      return;
    }
    if (sameWord(name, "far") && cm <= ON_CM) {
      Serial.println(F("error near must be below far"));
      return;
    }
    if (sameWord(name, "near")) ON_CM = cm;
    else OFF_CM = cm;
  } else {
    Serial.println(F("error unknown setting"));
    return;
  }

  saveAll();
  Serial.println(F("saved"));
  settingsPrint();
}

static void handleLine(char* text) {
  char* cmd = strtok(text, " \t");
  if (cmd == nullptr) return;

  if (sameWord(cmd, "get")) {
    settingsPrint();
    return;
  }
  if (sameWord(cmd, "help")) {
    printHelp();
    return;
  }
  if (sameWord(cmd, "defaults")) {
    restoreDefaults();
    return;
  }
  if (sameWord(cmd, "set")) {
    char* name = strtok(nullptr, " \t");
    char* value = strtok(nullptr, " \t");
    char* extra = strtok(nullptr, " \t");
    if (extra != nullptr) {
      Serial.println(F("error usage: set <name> <number>"));
      return;
    }
    handleSet(name, value);
    return;
  }
  Serial.println(F("error unknown command"));
}

void settingsPoll() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (discardLine) continue;
      if (lineLen < sizeof(line) - 1) {
        line[lineLen++] = c;
      } else {
        discardLine = true;
        lineLen = 0;
        Serial.println(F("error line too long"));
      }
      continue;
    }
    if (discardLine) {
      discardLine = false;
      lineLen = 0;
      continue;
    }
    line[lineLen] = '\0';
    lineLen = 0;
    handleLine(line);
  }
}
