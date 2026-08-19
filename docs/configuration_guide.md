# Native Configuration Guide

The firmware uses two configuration layers.

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

The default config portal password is `akita-setup`. Change it before deploying a node in the field. WPA2 requires 8 to 63 characters.

## Runtime Configuration

Runtime settings are stored in NVS and edited through the built-in HTTP portal.

Current runtime fields:

* vehicle ID
* transport mode
* WiFi SSID and password
* telemetry endpoint for `http://`, `https://`, `udp://host:port`, or `rns+udp://host:port`
* optional Reticulum destination hash for bridge delivery
* OBD adapter name
* optional OBD service UUID and characteristic UUID overrides
* GPS RX pin
* GPS TX pin
* GPS UART baud
* telemetry interval
* GPS enable flag
* LoRa frequency in Hz

The config portal also exposes a live runtime status panel for:

* transport ready
* WiFi uplink connected
* WiFi RSSI
* LoRa radio ready
* Reticulum bridge ready
* Reticulum bridge mode
* last Reticulum bridge error

The status panel is backed by the read-only `GET /api/status` endpoint, which is also useful for headless checks during bring-up.

Leave the WiFi password field blank to keep the currently stored station password.

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

* The transport component supports native WiFi uplink to `http://`, `https://`, `udp://host:port`, and `rns+udp://host:port` endpoints.
* `rns+udp://host:port` expects the bundled `tools/akita_reticulum_bridge.py` utility or another compatible bridge on the target host.
* The bundled bridge responds to `ping` and `telemetry` requests. The firmware treats the Reticulum bridge endpoint as ready only after one of those requests is acknowledged successfully.
* Directed bridge delivery retries with exponential backoff and a delivery deadline. Use the bridge flags `--delivery-attempts`, `--delivery-backoff-seconds`, `--delivery-backoff-factor`, `--delivery-backoff-max`, and `--delivery-deadline-seconds` to tune that behavior.
* The LoRa transport path uses compact JSON frames so a full telemetry snapshot fits in a single SX127x packet. The radio returns to receive after transmit.
* The OBD component uses a native BLE GATT client for common ELM327-style and Nordic UART style adapters.
* For non-default adapters, the config portal can store custom OBD service and characteristic UUID values.
* The archived Arduino implementation remains under `legacy/arduino_reference/` only as migration reference.
