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
* telemetry endpoint for `http://`, `udp://host:port`, or `rns+udp://host:port`
* optional Reticulum destination hash for bridge delivery
* OBD adapter name
* optional OBD service UUID and characteristic UUID overrides
* GPS RX pin
* GPS TX pin
* GPS UART baud
* telemetry interval
* GPS enable flag

The config portal also exposes a live runtime status panel for:

* transport ready
* Reticulum bridge ready
* Reticulum bridge mode
* last Reticulum bridge error

## Config Portal Flow

1. Flash the firmware.
2. Boot the node.
3. Join the WiFi AP configured in `menuconfig`.
4. Open `http://192.168.4.1/`.
5. Edit settings and save.
6. Runtime changes apply immediately after save.

## Recommended Bring-Up Order

1. Select the correct chip target with `idf.py set-target`.
2. Select the board profile in `menuconfig`.
3. Boot with the config portal enabled.
4. Set GPS pins and telemetry cadence.
5. Confirm GPS lock and payload generation in the serial log.
6. Set the OBD adapter name, WiFi telemetry endpoint, or GPS UART settings, save, and confirm the live reapply in the serial log.

## Notes

* The transport component now supports native WiFi uplink to `http://`, `udp://host:port`, and `rns+udp://host:port` endpoints.
* `rns+udp://host:port` expects the bundled `tools/akita_reticulum_bridge.py` utility or another compatible bridge on the target host.
* The bundled bridge responds to `ping` and `telemetry` requests. The firmware treats the Reticulum bridge endpoint as ready only after one of those requests is acknowledged successfully.
* Directed bridge delivery retries with exponential backoff before returning an error. Use the bridge flags `--delivery-attempts`, `--delivery-backoff-seconds`, `--delivery-backoff-factor`, and `--delivery-backoff-max` to tune that behavior.
* The LoRa transport path is currently transmit-only and targets SX127x-class boards such as Heltec LoRa 32 V2.
* A full native Reticulum implementation on-device is still pending.
* The OBD component now uses a native BLE GATT client for common ELM327-style and Nordic UART style adapters.
* For non-default adapters, the config portal can now store custom OBD service and characteristic UUID values.
* The archived Arduino implementation remains under `legacy/arduino_reference/` only as migration reference.

