# Native Troubleshooting Guide

This guide covers the ESP-IDF-native firmware path.

## Build Issues

### `idf.py` is missing

The native build depends on ESP-IDF. If `idf.py` is not available, install ESP-IDF 5.x and export the environment before building.

### The wrong target is selected

Set the chip target explicitly before building.

Examples:

* `idf.py set-target esp32s3`
* `idf.py set-target esp32c6`
* `idf.py set-target esp32c5`
* `idf.py set-target esp32` for Heltec LoRa 32 V2

### Partition errors or firmware too large

The repository includes a custom `partitions.csv` sized for a 4 MB flash image. If the wrong partition table is used, check that `sdkconfig.defaults` or your active `sdkconfig` still points to the custom partition file.

## Board Bring-Up Issues

### Config portal does not appear

Check the following:

* The build has `Akita CarNode -> Enable built-in config portal` turned on.
* The node booted successfully and did not reset.
* The device is using the expected board profile in `menuconfig`.
* The power supply is stable enough for WiFi AP startup.
* You are using the portal password from `menuconfig`. The default is `akita-setup`.

The portal AP name comes from `Akita CarNode -> Config portal SSID`.

### GPS does not report a fix

Check the following:

* Runtime GPS RX pin is correct for the board. GPS TX may be left unassigned.
* The GPS baud matches the module.
* The GPS antenna has a clear sky view.
* The selected board profile is not applying invalid defaults for your wiring.
* The live-apply response in the portal did not report a runtime apply failure.

The native GPS component is active, so GPS issues are usually pin, baud, wiring, or antenna issues.

## Runtime Issues

### OBD telemetry does not update

Check the following:

* The adapter is powered and advertising over BLE.
* The configured OBD adapter name matches what the adapter actually advertises.
* The adapter exposes a common ELM327-style serial BLE service or a Nordic UART style service.
* If the adapter uses custom BLE UUIDs, the runtime OBD service and characteristic UUID fields are set in the config portal.
* The live-apply response in the portal did not report a runtime apply failure.

The native OBD component scans, connects, discovers GATT characteristics, and issues PID requests over BLE. Clearing both the adapter name and UUID filters prevents accidental connections to unrelated BLE devices.

### WiFi transport does not publish

Check the following:

* Transport mode is set to WiFi.
* The WiFi SSID fits normal station limits and matches the target network.
* The endpoint uses a supported scheme: `http://`, `https://`, `udp://host:port`, or `rns+udp://host:port`.
* Saving the portal form without typing a WiFi password keeps the stored password.
* The live-apply response in the portal did not report a runtime apply failure.

With the config portal enabled, the firmware runs the portal soft AP and the WiFi station uplink together.

### LoRa transport does not publish

Check the following:

* Transport mode is set to LoRa.
* The board really uses an SX1276/SX1278-class radio on the configured SPI pins.
* The configured LoRa frequency matches the region and radio setup.
* The firmware is sending the compact LoRa JSON payload, which is sized for a single 255-byte frame.

The LoRa backend transmits compact telemetry frames and returns to receive after transmit. It does not implement a full Reticulum-over-LoRa mesh.

### Reticulum bridge does not deliver

Check the following:

* Transport mode is set to WiFi.
* The telemetry endpoint is `rns+udp://host:port` and points to the machine running `tools/akita_reticulum_bridge.py`.
* The bridge host can reach the same WiFi network as the device.
* The Reticulum destination hash is either empty for plain broadcast or matches a reachable Reticulum destination with a known path.
* The bridge is replying to UDP requests. The firmware waits for a bridge acknowledgement before reporting the Reticulum bridge path as ready.
* The config portal runtime status panel does not show a persistent bridge mode of `error` with a useful last-error string.

The bundled bridge uses the Python Reticulum stack on a host machine. Directed delivery retries with exponential backoff and a delivery deadline so the firmware timeout is not exceeded. Tune that behavior with `--delivery-attempts`, `--delivery-backoff-seconds`, `--delivery-backoff-factor`, `--delivery-backoff-max`, and `--delivery-deadline-seconds`.

If the bridge host logs an `AutoInterface[Default Interface] No multicast echoes received` warning, that warning is about Reticulum multicast discovery on the host and does not by itself mean the local UDP bridge path is broken.

## Power And Stability

If the board reboots during WiFi AP startup, GPS reads, or general bring-up:

* Check USB cable quality.
* Check the regulator and vehicle power path.
* Reduce peripheral count during first bring-up.
* Confirm the selected board profile matches the actual hardware.

## Legacy Reference

The archived Arduino implementation now lives in `legacy/arduino_reference/`. Use it only for migration comparison, not as the active build target.
