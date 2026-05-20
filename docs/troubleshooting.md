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
* The live-apply response in the portal did not report a runtime apply failure.

The native GPS component is active now, so GPS issues are usually pin, baud, wiring, or antenna issues.

## Current Native Limits

### OBD telemetry does not update

Check the following:

* The adapter is powered and advertising over BLE.
* The configured OBD adapter name matches what the adapter actually advertises.
* The adapter exposes a common ELM327-style serial BLE service or a Nordic UART style service.
* If the adapter uses custom BLE UUIDs, the runtime OBD service and characteristic UUID fields are set in the config portal.
* The live-apply response in the portal did not report a runtime apply failure.

The native OBD component now scans, connects, discovers GATT characteristics, and issues PID requests over BLE. For non-standard adapters, custom UUID configuration in the portal may be required, and some adapters can still need additional tuning beyond UUID overrides.

### WiFi transport does not publish

Check the following:

* Transport mode is set to WiFi.
* The WiFi SSID fits normal station limits and matches the target network.
* The endpoint uses a currently supported scheme: `http://`, `udp://host:port`, or `rns+udp://host:port`.
* The live-apply response in the portal did not report a runtime apply failure.

With the config portal enabled, the firmware runs the portal soft AP and the WiFi station uplink together.

### LoRa transport does not publish

Check the following:

* Transport mode is set to LoRa.
* The board really uses an SX1276/SX1278-class radio on the configured SPI pins.
* The configured LoRa frequency matches the region and radio setup.
* The JSON payload fits within a single LoRa frame. The current SX127x path rejects payloads above 255 bytes.

The current LoRa backend is a native transmit-only path. It does not implement receive handling or a full Reticulum-over-LoRa interface yet.

### Reticulum bridge does not deliver

Check the following:

* Transport mode is set to WiFi.
* The telemetry endpoint is `rns+udp://host:port` and points to the machine running `tools/akita_reticulum_bridge.py`.
* The bridge host can reach the same WiFi network as the device.
* The Reticulum destination hash is either empty for plain broadcast or matches a reachable Reticulum destination with a known path.

The bundled bridge uses the Python Reticulum stack on a host machine. A full native Reticulum implementation on the ESP target is still pending.

## Power And Stability

If the board reboots during WiFi AP startup, GPS reads, or general bring-up:

* Check USB cable quality.
* Check the regulator and vehicle power path.
* Reduce peripheral count during first bring-up.
* Confirm the selected board profile matches the actual hardware.

## Legacy Reference

The archived Arduino implementation now lives in `legacy/arduino_reference/`. Use it only for migration comparison, not as the active build target.
