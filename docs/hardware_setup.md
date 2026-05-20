# Native Hardware Setup Guide

This guide covers the active ESP-IDF-native firmware path.

## Required Components

1. **ESP32 board**
   * Generic ESP32-S3, ESP32-C6, or ESP32-C5 board, or
   * Heltec LoRa 32 V2 for the built-in LoRa profile
2. **BLE OBD-II adapter**
   * BLE only, not classic Bluetooth
3. **GPS module**
   * UART/NMEA compatible module such as NEO-6M, NEO-7M, or NEO-M8N
4. **LoRa antenna**
   * Required if you are bringing up a LoRa-capable board
5. **Stable 5V supply**
   * Vehicle buck converter recommended for in-car installs

## Wiring Model

### GPS

The native firmware uses runtime-configured UART pins rather than a fixed Arduino header.

Wire the GPS module as follows:

* GPS VCC -> board 3.3V or 5V as required by the module
* GPS GND -> board GND
* GPS TX -> configured GPS RX pin
* GPS RX -> configured GPS TX pin

Set the final RX, TX, and baud values in the config portal after boot.

### BLE OBD-II

The OBD adapter remains wireless.

* Plug the adapter into the vehicle OBD-II port.
* Ensure the vehicle ignition state powers the adapter.

### LoRa

The native LoRa backend is not finished yet, but wiring can still be prepared.

For Heltec LoRa 32 V2, the current board profile seeds these defaults:

* SCK -> GPIO5
* MISO -> GPIO19
* MOSI -> GPIO27
* CS -> GPIO18
* RESET -> GPIO14
* DIO0 -> GPIO26
* LED -> GPIO25

Always verify the actual board revision before trusting defaults.

### Status LED

If your board does not expose a built-in LED or uses a different LED pin, change the pin in runtime configuration or adjust the board profile.

## Board Profile Notes

### Generic ESP32-S3 / ESP32-C6 / ESP32-C5

These profiles are intentionally conservative. Expect to set UART pins and any external LED or LoRa wiring yourself during bring-up.

### Heltec LoRa 32 V2

This profile seeds known LoRa and LED defaults, making it the quickest target for LoRa-oriented bring-up once the native radio backend lands.

## Bring-Up Recommendations

1. Power the board from a stable bench supply or good USB source first.
2. Boot with the config portal enabled.
3. Set GPS pins and baud through the portal.
4. Confirm GPS output in the serial log.
5. Add BLE OBD validation after the native GATT client is completed.
6. Add LoRa validation after the native radio backend is completed.
