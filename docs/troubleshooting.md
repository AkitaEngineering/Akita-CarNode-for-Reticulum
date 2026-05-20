# Native Troubleshooting Guide

This guide covers the new ESP-IDF-native firmware path.

## Build Issues

### `idf.py` is missing

The native build now depends on ESP-IDF. If `idf.py` is not available, install ESP-IDF 5.x and export the environment before building.

### The wrong target is selected

Set the chip target explicitly before building.

Examples:

* `idf.py set-target esp32s3`
* `idf.py set-target esp32c6`
* `idf.py set-target esp32c5`

### Partition errors or firmware too large

The repository includes a custom `partitions.csv` sized for a larger single application image. If the wrong partition table is used, check that `sdkconfig.defaults` or your active `sdkconfig` still points to the custom partition file.

## Board Bring-Up Issues

### Config portal does not appear

Check the following:

* The build has `Akita CarNode -> Enable built-in config portal` turned on.
* The node booted successfully and did not reset.
* The device is using the expected board profile in `menuconfig`.
* The power supply is stable enough for WiFi AP startup.

The portal AP name comes from `Akita CarNode -> Config portal SSID`.

### GPS does not report a fix

Check the following:

* Runtime GPS RX and TX pins are correct for the board.
* The GPS baud matches the module.
* The GPS antenna has a clear sky view.
* The selected board profile is not applying invalid defaults for your wiring.

The native GPS component is active now, so GPS issues are usually pin, baud, wiring, or antenna issues.

## Current Native Limits

### OBD telemetry does not update

Check the following:

* The adapter is powered and advertising over BLE.
* The configured OBD adapter name matches what the adapter actually advertises.
* The adapter exposes a common ELM327-style serial BLE service or a Nordic UART style service.
* If the adapter uses custom BLE UUIDs, the runtime OBD service and characteristic UUID fields are set in the config portal.
* The node was rebooted after changing OBD or transport settings in the portal.

The native OBD component now scans, connects, discovers GATT characteristics, and issues PID requests over BLE. For non-standard adapters, custom UUID configuration in the portal may be required, and some adapters can still need additional tuning beyond UUID overrides.

### WiFi transport does not publish

Check the following:

* Transport mode is set to WiFi.
* The WiFi SSID fits normal station limits and matches the target network.
* The endpoint uses a currently supported scheme: `http://` or `udp://host:port`.
* The node was rebooted after changing WiFi or endpoint settings.

With the config portal enabled, the firmware runs the portal soft AP and the WiFi station uplink together.

### LoRa transport is not live yet

Board defaults and pin storage exist, but the native LoRa backend still needs to be implemented.

### Reticulum transport is not live yet

This repository now targets a native ESP-IDF transport layer, but the actual native Reticulum backend is still pending. The workspace copy of `Reticulum/` is the Python reference implementation, not a drop-in ESP-IDF transport.

## Power And Stability

If the board reboots during WiFi AP startup, GPS reads, or general bring-up:

* Check USB cable quality.
* Check the regulator and vehicle power path.
* Reduce peripheral count during first bring-up.
* Confirm the selected board profile matches the actual hardware.

## Legacy Reference

The archived Arduino implementation now lives in `legacy/arduino_reference/`. Use it only for migration comparison, not as the active build target.
