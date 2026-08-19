# Native Firmware Architecture

## Goal

The firmware is a native ESP-IDF component graph so board support, driver work, configuration, and telemetry transport can be managed explicitly.

## Active Components

### `akita_common`

Owns shared platform definitions.

Responsibilities:

* shared runtime types
* board profile defaults
* default pin and transport seeding

### `akita_core`

Owns the application lifecycle.

Responsibilities:

* runtime bootstrap
* periodic polling loop
* full JSON and compact LoRa payload creation
* non-blocking status LED pulse handling
* task watchdog subscription

### `akita_config`

Owns configuration state and the built-in configuration portal.

Responsibilities:

* NVS-backed runtime config load/save with sanitization
* WiFi soft AP startup
* HTTP UI and save path
* live runtime status JSON for the config portal
* live reapply of GPS, OBD, and transport after save

### `akita_gps`

Owns the UART GPS path.

Responsibilities:

* UART driver setup
* NMEA buffering
* checksum handling
* GGA and RMC parsing for common talker IDs
* normalized GPS snapshot output

### `akita_obd`

Owns native OBD logic.

Responsibilities:

* BLE scan and connection lifecycle
* GATT service and characteristic discovery
* command dispatch for common ELM327-style adapters
* PID request formatting
* PID response parsing for core telemetry fields
* retries on timed-out PID requests

### `akita_transport`

Owns the uplink boundary.

Responsibilities:

* abstract transport mode selection
* WiFi station setup with AP+STA coexistence when the config portal is enabled
* HTTP and HTTPS POST uplink
* UDP uplink for `udp://host:port` endpoints
* native SX127x LoRa transmit and receive harvesting
* compact-frame publish for LoRa
* Reticulum bridge envelopes for `rns+udp://host:port` endpoints
* bridge request/response acknowledgements and bridge readiness/error tracking

### `tools/akita_reticulum_bridge.py`

Owns the host-side bridge between the firmware and the Python Reticulum stack.

Current responsibilities:

* accept UDP bridge requests from the firmware
* answer `ping` and `telemetry` acknowledgements
* inject telemetry into Reticulum as a plain broadcast or directed packet
* retry directed delivery with exponential backoff and a delivery deadline

## Configuration Strategy

Two layers are used on purpose:

* build-time board profile selection through `menuconfig`
* runtime operational settings through NVS and the HTTP portal

This keeps physical pin defaults and board personality separate from field configuration.

## Board Profiles

The current project defines profiles for:

* Generic ESP32-S3
* Generic ESP32-C6
* Generic ESP32-C5
* Heltec LoRa 32 V2

The Heltec profile seeds LoRa pins and a status LED default. Generic profiles stay conservative and expect runtime pin configuration. Heltec LoRa 32 V2 is an ESP32 target, not ESP32-S3.

## Why The Arduino Tree Was Archived

The Arduino code carried useful logic, but it also kept transport, board configuration, and application flow tied to sketch semantics and external Arduino libraries. The native project keeps the legacy tree available only as a migration reference under `legacy/arduino_reference/`.

## Production Transport Model

WiFi, LoRa, and the Reticulum host bridge are the production uplinks.

A full on-device Reticulum protocol stack is intentionally not part of this firmware. The ESP node collects vehicle telemetry and hands Reticulum delivery to `tools/akita_reticulum_bridge.py`, which already has a complete Python Reticulum implementation.
