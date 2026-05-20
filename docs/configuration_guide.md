# Native Configuration Guide

The active firmware now uses two clear configuration layers instead of a single Arduino header.

## Build-Time Configuration

Use ESP-IDF `menuconfig` for hardware personality and portal defaults.

Relevant settings:

* `Akita CarNode -> Primary board profile`
* `Akita CarNode -> Enable built-in config portal`
* `Akita CarNode -> Config portal SSID`
* `Akita CarNode -> Config portal password`

Board profiles currently available:

* Generic ESP32-S3
* Generic ESP32-C6
* Generic ESP32-C5
* Heltec LoRa 32 V2

The profile sets board defaults such as LoRa pins, LED pin, and initial transport mode.

## Runtime Configuration

Runtime settings are stored in NVS and edited through the built-in HTTP portal.

Current runtime fields:

* vehicle ID
* transport mode
* WiFi SSID and password
* native endpoint or destination placeholder
* OBD adapter name
* GPS RX pin
* GPS TX pin
* GPS UART baud
* telemetry interval
* GPS enable flag

## Config Portal Flow

1. Flash the firmware.
2. Boot the node.
3. Join the WiFi AP configured in `menuconfig`.
4. Open `http://192.168.4.1/`.
5. Edit settings and save.
6. Reboot the node so UART and transport changes are applied cleanly.

## Recommended Bring-Up Order

1. Select the correct chip target with `idf.py set-target`.
2. Select the board profile in `menuconfig`.
3. Boot with the config portal enabled.
4. Set GPS pins and telemetry cadence.
5. Confirm GPS lock and payload generation in the serial log.
6. Finish the native BLE OBD and transport bring-up for your board.

## Notes

* The transport component is a native abstraction layer, not a finished Reticulum backend yet.
* The OBD component already owns request and PID parsing logic, but the BLE GATT client still needs to be completed.
* The archived Arduino implementation remains under `legacy/arduino_reference/` only as migration reference.

