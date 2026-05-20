# Native Firmware Architecture

## Goal

The firmware is moving from an Arduino sketch into a native ESP-IDF component graph so board support, driver work, configuration, and telemetry transport can be managed explicitly.

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
* payload creation
* status LED pulse handling

### `akita_config`

Owns configuration state and the built-in configuration portal.

Responsibilities:

* NVS-backed runtime config load/save
* WiFi soft AP startup
* HTTP UI and save path

### `akita_gps`

Owns the UART GPS path.

Responsibilities:

* UART driver setup
* NMEA buffering
* GGA and RMC parsing
* normalized GPS snapshot output

### `akita_obd`

Owns native OBD logic boundaries.

Current responsibilities:

* PID request formatting
* PID response parsing for core telemetry fields
* isolation point for the future BLE GATT client

### `akita_transport`

Owns the uplink boundary.

Current responsibilities:

* abstract transport mode selection
* hold the place where native WiFi, LoRa, and Reticulum backends will land

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

The Heltec profile seeds LoRa pins and a status LED default. Generic profiles intentionally stay conservative and expect runtime pin configuration.

## Why The Arduino Tree Was Archived

The Arduino code carried useful logic, but it also kept transport, board configuration, and application flow tied to sketch semantics and external Arduino libraries. The native project keeps the legacy tree available only as a migration reference under `legacy/arduino_reference/`.

## Remaining Native Work

The next meaningful implementation steps are:

1. Native BLE OBD GATT client for adapter discovery, command dispatch, and notifications.
2. Native LoRa radio backend for supported boards.
3. Native Reticulum transport integration on top of the new transport abstraction.
4. Optional OTA and richer status endpoints once the transport path is stable.