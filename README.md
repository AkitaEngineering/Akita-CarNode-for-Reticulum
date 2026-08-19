# Akita CarNode Native Firmware

**Organization:** Akita Engineering  
**License:** GPLv3  
**Status:** Production-ready ESP-IDF firmware

This repository is a native ESP-IDF firmware tree for a vehicle telemetry node. The firmware owns GPS parsing, BLE OBD collection, payload serialization, NVS-backed runtime configuration, a built-in HTTP configuration portal, and native uplinks over WiFi, LoRa, and a host-side Reticulum bridge.

The old Arduino code is archived under `legacy/arduino_reference/` and is not part of the production build.

## What The Native Firmware Provides

* No Arduino core or Arduino libraries in the active firmware path.
* ESP-IDF root project with `CMakeLists.txt`, `main/`, `components/`, `sdkconfig.defaults`, and `partitions.csv`.
* Build-time board selection through `menuconfig`.
* Runtime configuration through a built-in WiFi access point and HTTP UI.
* Custom NMEA parsing and JSON payload generation.
* Native BLE OBD GATT client for common ELM327-style and Nordic UART adapters.
* WiFi telemetry uplink for `http://`, `https://`, `udp://host:port`, and `rns+udp://host:port`.
* Native SX127x LoRa telemetry path with compact frames and receive harvesting.
* Host-side Reticulum bridge for production Reticulum delivery.

## Supported Targets

* Generic ESP32-S3
* Generic ESP32-C6
* Generic ESP32-C5
* Heltec LoRa 32 V2 (`idf.py set-target esp32`)

The board profile sets sane defaults for pins and transport mode. Runtime configuration remains available after boot through the config portal.

## Production Status

Implemented and intended for field use:

* ESP-IDF application bootstrap with `app_main()`
* Custom partition table sized for a 4 MB flash image
* Board profiles and defaults
* NVS-backed runtime configuration store with sanitization and live apply
* Built-in HTTP configuration UI on a WPA2 soft AP
* Native WiFi telemetry uplink for HTTP, HTTPS, UDP, and the Reticulum bridge
* Native SX127x LoRa transmit and receive harvesting for `AKITA_TRANSPORT_LORA`
* Compact LoRa JSON frames that fit a single 255-byte SX127x packet
* Reticulum bridge uplink for `rns+udp://host:port`
* Native BLE OBD GATT client
* UART GPS reader with NMEA checksum handling
* Service orchestration, watchdog subscription, and periodic telemetry

Reticulum delivery on the device is implemented as a host bridge, not a full on-device Reticulum stack. That is the production path: the node forwards telemetry to `tools/akita_reticulum_bridge.py`, and the bridge injects it into a Reticulum network.

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
│   ├── akita_obd/        # Native OBD BLE client and PID parser
│   └── akita_transport/  # WiFi, LoRa, and Reticulum bridge uplinks
├── tools/
│   ├── akita_reticulum_bridge.py      # Host-side Reticulum bridge
│   └── test_akita_reticulum_bridge.py # Bridge unit tests
├── docs/
│   ├── configuration_guide.md
│   ├── hardware_setup.md
│   ├── native_architecture.md
│   └── troubleshooting.md
├── legacy/
│   └── arduino_reference/ # Archived Arduino implementation
├── partitions.csv
├── sdkconfig.defaults
└── README.md
```

## Build Requirements

* ESP-IDF 5.x installed and exported in your shell
* A target board supported by the ESP32, ESP32-S3, ESP32-C6, or ESP32-C5 profiles
* USB serial access to the device

The local workspace copy of `Reticulum/` is useful as a protocol reference and as the Python stack used by the host bridge. It does not replace the native firmware transport.

## Quick Start

1. Install and export ESP-IDF.
2. From the repository root, set the chip target that matches your board.
3. Open `menuconfig` and choose the board profile under `Akita CarNode`.
4. Build and flash the firmware.
5. Connect to the configuration AP and open the built-in configuration UI.
6. Save runtime settings and confirm the live runtime reapply in the serial log.

Typical command flow:

```bash
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

For Heltec LoRa 32 V2:

```bash
idf.py set-target esp32
```

For ESP32-C6 or ESP32-C5 boards, change the target accordingly.

## Configuration Model

There are two layers of configuration:

* Build-time configuration in `menuconfig`
* Runtime configuration in NVS through the config portal

The config portal starts a soft AP using the SSID and password from `menuconfig` and serves a small HTTP UI that lets you edit:

* Vehicle ID
* Transport mode
* WiFi credentials
* GPS UART pins and baud
* OBD adapter name
* Optional OBD service and characteristic UUID overrides
* Telemetry cadence
* Telemetry endpoint for `http://`, `https://`, `udp://`, or `rns+udp://` uplinks
* Optional Reticulum destination hash for bridge delivery
* LoRa frequency

The default setup AP password is `akita-setup`. Change it in `menuconfig` before field deployment.

The config portal also exposes a live runtime status panel for transport readiness, WiFi RSSI, LoRa radio state, Reticulum bridge readiness, bridge mode, and the last bridge error.

See `docs/configuration_guide.md` for the full flow.

## Reticulum Bridge

The production Reticulum integration is a host bridge.

Set the firmware telemetry endpoint to `rns+udp://host:port` and run the bundled bridge utility on a machine that has access to a Reticulum instance:

```bash
python3 tools/akita_reticulum_bridge.py --listen-port 4242 --config ~/.reticulum
```

The firmware forwards telemetry to that UDP bridge. The bridge injects it into Reticulum either as a directed packet to the configured destination hash or as a plain broadcast when the destination field is empty.

The bridge answers `ping` and `telemetry` requests with structured acknowledgements. For `rns+udp://` endpoints, the firmware only reports the transport as ready after the bridge has acknowledged a request.

Directed bridge delivery retries with exponential backoff and a delivery deadline so the firmware is not left waiting past its UDP timeout. Tune that behavior with `--delivery-attempts`, `--delivery-backoff-seconds`, `--delivery-backoff-factor`, `--delivery-backoff-max`, and `--delivery-deadline-seconds`.

## Design Direction

The firmware is intentionally a thin, predictable ESP-IDF base:

* Own the control flow instead of depending on Arduino sketch semantics.
* Keep transport, sensor, and board logic in separate components.
* Prefer small, explicit parsers and serializers over large convenience libraries.
* Make board support visible through profiles instead of hidden in preprocessor sprawl.

See `docs/native_architecture.md` for the native component layout.

## Legacy Code

`legacy/arduino_reference/` exists only as migration reference. It is no longer the primary entry point, and it should not be treated as the active firmware architecture.

## License

This project is licensed under the GNU General Public License v3.0. See `LICENSE` for details.
