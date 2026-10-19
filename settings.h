#pragma once

// Load saved timings from flash, or keep the factory defaults.
void settingsLoad();

// Print the live values. Says whether they were loaded from flash.
void settingsPrint();

// Read one serial line, if the monitor has sent one, and apply it.
void settingsPoll();
