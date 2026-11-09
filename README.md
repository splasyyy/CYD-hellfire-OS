# CYD Hellfire OS

Initial firmware for the ESP32-2432S028: reliable display and touch bring-up,
interactive touch calibration, and a microSD card check.

This is a defensive diagnostics foundation. It does not transmit Wi-Fi or
Bluetooth traffic.

## Hardware assumptions

The default pin map targets the common 2.8-inch ESP32-2432S028 with an ILI9341
display, XPT2046 resistive touch controller, and microSD slot:

| Function | GPIO |
| --- | --- |
| Display SCLK / MISO / MOSI | 14 / 12 / 13 |
| Display CS / DC / backlight | 15 / 2 / 21 |
| Touch SCLK / MISO / MOSI | 25 / 39 / 32 |
| Touch CS / IRQ | 33 / 36 |
| microSD SCLK / MISO / MOSI / CS | 18 / 19 / 23 / 5 |

The USB-C and micro-USB connectors are board-level power/programming options;
the firmware uses the ESP32 serial interface exposed by the USB-to-UART bridge.
Board batches can differ, so confirm the controller and pinout against your
board before changing `include/board_config.h`.

## Build and flash

1. Install [PlatformIO Core](https://platformio.org/install/cli).
2. Connect the board with a data-capable USB cable.
3. Run `pio run -e cyd_esp32_2432s028` to build.
4. Run `pio run -e cyd_esp32_2432s028 -t upload` to flash. If upload fails,
   hold BOOT while resetting the board, then retry.
5. Open `pio device monitor -b 115200`.

## First boot

The screen starts a four-corner touch calibration when no saved calibration is
available. Tap each crosshair when it appears; calibration is saved in ESP32
non-volatile storage. The main screen then provides a recalibration button and
an SD check button. The SD check mounts the card and appends a boot record to
`/cyd-health.log`; it does not format the card.

If the display remains blank, check the selected board variant, TFT controller,
backlight wiring, and pin definitions before flashing again. Calibration data
can be cleared by erasing the ESP32 NVS partition.

## Project layout

- `include/board_config.h`: board pin assignments and display dimensions.
- `src/main.cpp`: startup diagnostics, touch calibration, status UI, and SD check.
