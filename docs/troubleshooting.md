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

## Current Porting Gaps

### OBD telemetry is not live yet

The native OBD component currently contains the request builder and PID response parser, but the BLE GATT client is still being ported. Until that is finished, OBD values will not update from a real adapter.

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
