# Akita CarNode Native Firmware

**Organization:** Akita Engineering  
**License:** GPLv3  
**Status:** ESP-IDF native refactor in progress

This repository now uses a native ESP-IDF project layout instead of an Arduino sketch. The active firmware path is built around our own components, our own GPS parser, our own payload serializer, NVS-backed runtime configuration, and a built-in HTTP configuration portal.

The old Arduino code is no longer the project root or the primary build surface. It has been archived under `legacy/arduino_reference/` while the native BLE OBD, LoRa radio, and Reticulum backends continue to be ported.

## What This Refactor Changes

* No Arduino core in the active firmware path.
* No Arduino libraries in the active firmware path.
* ESP-IDF root project with `CMakeLists.txt`, `main/`, `components/`, `sdkconfig.defaults`, and `partitions.csv`.
* Build-time board selection through `menuconfig`.
* Runtime configuration through a built-in WiFi access point and HTTP UI.
* Custom NMEA parsing and custom JSON payload generation.

## Supported Targets

* Generic ESP32-S3
* Generic ESP32-C6
* Generic ESP32-C5
* Heltec LoRa 32 V2

The board profile sets sane defaults for pins and transport mode, but runtime configuration is still available after boot through the config portal.

## Current Native Status

Implemented now:

* ESP-IDF project scaffold
* Custom partition table sized for a larger single app image
* Native application bootstrap with `app_main()`
* Board profiles and defaults
* NVS-backed runtime configuration store
* Built-in HTTP configuration UI on a soft AP
* Custom UART-based GPS reader with lightweight NMEA parsing
* Custom JSON payload formatter
* Service orchestration and periodic telemetry loop

Still being ported:

* Native BLE OBD-II GATT client
* Native LoRa radio backend
* Native Reticulum transport/backend

That means the firmware structure is now native-first and ready for target-specific driver work, but the Reticulum transport path is not finished yet.

## Repository Layout

```
Akita-CarNode-for-Reticulum/
├── CMakeLists.txt
├── main/
│   ├── app_main.c
│   └── Kconfig.projbuild
├── components/
│   ├── akita_common/     # Shared types and board defaults
│   ├── akita_core/       # App runtime and payload builder
│   ├── akita_config/     # NVS config store and HTTP config portal
│   ├── akita_gps/        # Native UART GPS reader and NMEA parsing
│   ├── akita_obd/        # Native OBD abstraction and PID parser helpers
│   └── akita_transport/  # Transport abstraction for WiFi/LoRa/Reticulum
├── docs/
│   ├── configuration_guide.md
│   ├── hardware_setup.md
│   ├── native_architecture.md
│   └── troubleshooting.md
├── legacy/
│   └── arduino_reference/ # Archived Arduino implementation for migration reference
├── partitions.csv
├── sdkconfig.defaults
└── README.md
```

## Build Requirements

* ESP-IDF 5.x installed and exported in your shell
* A target board supported by the ESP32-S3, ESP32-C6, ESP32-C5, or Heltec LoRa 32 V2 profile
* USB serial access to the device

The local workspace copy of `Reticulum/` is the Python implementation and protocol reference. It is useful for transport-porting work, but it does not supply a native ESP-IDF transport backend by itself.

## Quick Start

1. Install and export ESP-IDF.
2. From the repository root, set the chip target that matches your board.
3. Open `menuconfig` and choose the board profile under `Akita CarNode`.
4. Build and flash the firmware.
5. Connect to the configuration AP and open the built-in configuration UI.
6. Save runtime settings, reboot, and continue board-specific bring-up.

Typical command flow:

```bash
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

For ESP32-C6 or ESP32-C5 boards, change the target accordingly.

## Configuration Model

There are now two layers of configuration:

* Build-time configuration in `menuconfig`
* Runtime configuration in NVS through the config portal

The config portal starts a soft AP using the SSID from `menuconfig` and serves a small HTTP UI that lets you edit:

* Vehicle ID
* Transport mode
* WiFi credentials
* GPS UART pins and baud
* OBD adapter name
* Telemetry cadence
* Native endpoint/destination placeholder

See `docs/configuration_guide.md` for the full flow.

## Design Direction

This refactor is intentionally pushing the repository toward a thinner, more predictable firmware base:

* Own the control flow instead of depending on Arduino sketch semantics.
* Keep transport, sensor, and board logic in separate components.
* Prefer small, explicit parsers and serializers over large convenience libraries.
* Make board support visible through profiles instead of hidden in preprocessor sprawl.

See `docs/native_architecture.md` for the native component layout and remaining porting work.

## Legacy Code

`legacy/arduino_reference/` exists only as migration reference. It is no longer the primary entry point, and it should not be treated as the active firmware architecture.

## License

This project is licensed under the GNU General Public License v3.0. See `LICENSE` for details.
