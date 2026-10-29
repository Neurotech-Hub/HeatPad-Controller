# Heat Pad

Firmware for an Adafruit ESP32-S3 TFT Feather that warms a heat pad when someone stands close. A white heart printed on black fabric is covered with black thermochromic paint. When the pad is warm the paint turns clear and the heart shows. When it cools, the paint goes black again and the heart disappears.

The Feather reads a time-of-flight sensor, drives a Pololu U3V70A boost regulator, and draws the live distance, battery level, and heart on its built-in screen.

## Hardware

- Adafruit ESP32-S3 TFT Feather (#5483)
- VL53L0X, VL53L1X, or VL53L4CD on the STEMMA QT port (I2C address 0x29)
- Pololu U3V70A step-up regulator (#2890), output set to V_PAD
- Heat pad on the regulator output
- One LiPo for the Feather JST connector and the regulator VIN
- Indicator on GPIO 13 (the on-board red LED, or an external LED)

V_PAD is whatever voltage the regulator potentiometer is set to. Set it with a meter before connecting the pad. Clockwise raises the output. Do not let the battery voltage exceed V_PAD, or the pad sees the raw battery instead of the regulated output.

The boost draws more current from the cell than the pad draws at V_PAD. The pack has to be rated for that.

## Wiring

Common ground between the Feather, the regulator, and the pad.

| Signal | Connection |
| --- | --- |
| Time-of-flight | Feather STEMMA QT |
| U3V70A ENABLE | GPIO 16 (`PIN_HEAT_EN`). High turns V_PAD on. Low turns it off. |
| Indicator LED | GPIO 13 |
| LiPo | Feather JST and U3V70A VIN |
| Heat pad | U3V70A VOUT |

V_PAD is present only while GPIO 16 is driven high. Leaving that pin low, or as an input, keeps the pad off.

## Behavior

There is no temperature sensor. Timing stands in for the paint.

1. Three readings at or under 35 cm start a heat-up. V_PAD stays on continuously for 20 s. The on-screen heart fills to solid red.
2. The hold loop starts with an off step, because the heat-up just ended. Factory timing is 100 ms on and 100 ms off, repeating while the person stays in range. During each off step the heart eases down slightly (about 8 percent) and the next on step brings it back to full red. GPIO 13 follows the enable pin.
3. Five readings beyond 40 cm turn V_PAD off. The heart empties over 10 s. Stepping back in before it has cooled resumes from the level still on screen.

The heater also stays off when the cell reads below 3.6 V while the pad is drawing current. Heating is allowed again only after 30 seconds and once the resting cell is back above 3.85 V. It also stays off when the battery is under 10 percent (heating is allowed again above 15 percent) or when the distance sensor stops answering. A low battery shows `LOW BATTERY` in the center of the screen and `LOW BATT` on the state line. A dead sensor shows `SENSOR ERR`.

## Display

At boot the screen traces a heart, fills it, pulses it, and shows the title. The main screen shows battery percent and voltage, distance in centimeters (`---` when nobody is in range), the heater state, and the heart.

## Build

Open this folder in the Arduino IDE. The sketch name matches the folder name.

1. Install the Espressif ESP32 boards package.
2. Select **Adafruit Feather ESP32-S3 TFT**.
3. Install these libraries from the Library Manager:
   - Adafruit ST7735 and ST7789 Library (pulls in Adafruit GFX)
   - Adafruit BusIO
   - Adafruit VL53L0X
   - Adafruit VL53L1X
   - STM32duino VL53L4CD
   - Adafruit MAX1704X
   - Adafruit LC709203F

Upload, then open the Serial Monitor at **115200** baud with a newline ending.

## Serial settings

`heat`, `pwm-on`, `pwm-off`, `near`, `far`, and `cool` can be changed without reflashing. Each accepted `set` takes effect immediately and is stored in flash, so it remains after reset and after the battery is unplugged. A full flash erase (Arduino option "Erase all flash before sketch upload") or the `defaults` command clears the saved copy. A copy already stored in flash keeps winning over new factory defaults until one of those clears it.

| Command | Effect |
| --- | --- |
| `get` | Print the active values and whether they came from flash |
| `set heat 4000` | Continuous on time after a new detection, in milliseconds |
| `set pwm-on 1000` | Hold on time, in milliseconds |
| `set pwm-off 500` | Hold off time, in milliseconds |
| `set near 35` | Turn on at or under this distance, in centimeters |
| `set far 45` | Turn off beyond this distance, in centimeters |
| `set cool 2500` | Heart cool-down after the person leaves, in milliseconds |
| `defaults` | Restore the factory numbers and erase the saved copy |
| `help` | List the commands |

Times must be 50–60000 ms. Distances must be 1–200 cm, and `near` must stay below `far`. A rejected value leaves the current setting unchanged. A pulse already in progress finishes its current pwm-on or pwm-off interval, then uses the new time.

Factory defaults are 20000 ms heat, 100 ms pwm-on, 100 ms pwm-off, 35 cm near, 40 cm far, and 10000 ms cool.

Pins, sample counts, and the battery cutoff stay in `config.h` and need a recompile to change.

## Files

| File | Role |
| --- | --- |
| `Heat_Pad.ino` | Setup, heater state machine, V_PAD enable and LED |
| `config.h` | Pins, factory defaults, and fixed limits |
| `settings.cpp` | Serial commands and flash storage |
| `sensors.cpp` | Time-of-flight ranging and the fuel gauge |
| `ui.cpp` | Splash animation and the main screen |
