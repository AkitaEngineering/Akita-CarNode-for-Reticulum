# Akita CarNode - Troubleshooting Guide

This guide provides solutions to common issues encountered with the Akita CarNode project.

## General Troubleshooting Steps

1.  **Enable Max Serial Debug:**
    * In `config.h`, set `ENABLE_SERIAL_DEBUG` to `2`.
    * Set `SERIAL_BAUD_RATE` (e.g., `115200`) and ensure your Arduino Serial Monitor or PlatformIO serial monitor is set to the same baud rate.
    * Observe the serial output for error messages, status updates, and debug information from the CarNode and Reticulum.
2.  **Check Connections:** Double-check all wiring against the `hardware_setup.md` guide and your specific component datasheets. Loose connections are a common source of problems.
3.  **Power Supply:** Ensure your ESP32 and peripherals are receiving stable and sufficient power (5V, at least 1A-1.5A recommended, especially with WiFi/BLE active). Unstable power can cause random resets or erratic behavior.
4.  **Simplify:** If facing multiple issues, try to isolate components. For example, test BLE OBD-II connection alone, then GPS alone, then network connectivity.
5.  **Restart Everything:** Sometimes, a simple power cycle of the ESP32, OBD-II adapter, and even your vehicle's ignition can resolve temporary glitches.
6.  **Library Versions:** Ensure you are using compatible versions of all libraries, especially Reticulum for ESP32 and ArduinoJson. Check the "C++ Library Dependencies" document.

## Specific Issues

---
### No Serial Output / Gibberish on Serial Monitor
* **Check Baud Rate:** Ensure the baud rate in your Serial Monitor matches `SERIAL_BAUD_RATE` in `config.h`.
* **Correct COM Port:** Verify that you have selected the correct COM port for your ESP32 in the Arduino IDE or PlatformIO.
* **USB Cable:** Try a different USB cable; some are for charging only and don't carry data.
* **ESP32 Boot Mode:** Ensure the ESP32 is not stuck in bootloader mode (usually by holding the BOOT button during reset). A normal reset should suffice.
* **Driver Issues:** Ensure your computer has the necessary USB-to-Serial drivers (e.g., CP210x, CH340) for your ESP32 board.

---
### BLE OBD-II Connection Problems
* **"Device not found" / Scan fails:**
    * **CRITICAL: Verify `OBDII_DEVICE_NAME` or `OBDII_SERVICE_UUID` & `OBDII_CHARACTERISTIC_UUID` in `config.h`.** This is the most common cause of failure. Use a BLE scanner app (nRF Connect, LightBlue) on your smartphone to:
        1.  Find the exact advertised name of your OBD-II adapter.
        2.  Discover its services and characteristics. Look for services related to serial communication or custom data channels. Note down the Service UUID and the relevant Characteristic UUID (often one for writing commands, another for receiving notifications, or a single one for both).
    * Ensure the OBD-II adapter is powered: Vehicle ignition might need to be in "ON" or "Accessory" mode. Check if the adapter has its own power LED.
    * **BLE Range:** Keep the ESP32 close to the OBD-II adapter during initial testing.
    * **Adapter Type:** Confirm your adapter is BLE (Bluetooth Low Energy / Bluetooth 4.0+) and not a "Classic" Bluetooth adapter. The ESP32 BLE library only works with BLE.
* **Connects but no data / "Characteristic not found":**
    * If using specific UUIDs, ensure they are correct for *data transfer*, not just device information.
    * If connecting by name, the heuristic discovery in `ble_handler.cpp` might not find the correct characteristics for your specific adapter. **Using explicit, correct UUIDs is always more reliable.**
    * Some adapters require specific initialization commands beyond the standard AT commands sent by the CarNode. Consult your adapter's documentation if available.
* **Frequent Disconnections:**
    * Power stability issues for ESP32 or OBD-II adapter.
    * BLE interference (try moving away from other 2.4GHz devices).
    * Software bugs or BLE stack issues on ESP32 (ensure ESP32 core is up-to-date).

---
### No OBD-II Data or Incorrect Data Values
* **PID Support:** Verify that the PIDs listed in `OBD_PIDS_TO_QUERY` (in `config.h`) are actually supported by your specific vehicle model/year and your OBD-II adapter. Not all vehicles support all PIDs.
* **Response Prefix:** The `responsePrefix` in `config.h` (e.g., "410C" for RPM) must exactly match the beginning of the OBD-II response for that PID *after spaces are removed*.
* **Parsing Logic:** If data is received but values are nonsensical, there might be an error in `obd_parser.cpp` for that specific PID's formula or byte interpretation. Double-check the OBD-II standards for the PID's data encoding.
* **ELM327 Init Commands:** The `AT` commands sent in `ble_handler.cpp` (e.g., `ATSP0`) set the protocol. If your vehicle requires a specific OBD protocol not automatically detected by `ATSP0`, issues can arise.

---
### GPS Issues (No Fix, No Data)
* **Sky View:** The GPS antenna **must** have a clear, unobstructed view of the sky. Indoors or in urban canyons, getting a fix can be difficult or impossible. Allow several minutes for the first fix (cold start).
* **Wiring:** Double-check `GPS_RX_PIN` (ESP32) to GPS TX, `GPS_TX_PIN` (ESP32) to GPS RX, VCC, and GND connections.
* **Baud Rate:** Ensure `GPS_BAUD_RATE` in `config.h` matches your GPS module's default or configured baud rate (9600 is common).
* **GPS Module Power:** Verify the module is receiving correct voltage (3.3V or 5V, check datasheet). Some ESP32 3.3V pins might not supply enough current for some GPS modules; try powering from a separate 3.3V regulator if unsure.
* **Antenna Connection:** If using an external active antenna, ensure it's properly connected and, if it requires power, that the module provides it.
* **Module Status LED:** Many GPS modules have an LED that blinks when it has a satellite fix.

---
### WiFi Connection Issues (if `USE_WIFI_FOR_RETICULUM` is true)
* **Credentials:** **Verify `WIFI_SSID` and `WIFI_PASSWORD` in `config.h` are 100% correct (case-sensitive).**
* **Signal Strength:** The ESP32 might have poor WiFi range, especially with its small onboard antenna. Try moving closer to your WiFi router.
* **Router Configuration:** Ensure your WiFi router is not blocking new devices (e.g., MAC filtering). Check if it's 2.4GHz (ESP32 typically doesn't support 5GHz).
* **Too Many Devices:** Some routers have a limit on connected devices.

---
### LoRa Communication Issues (if `USE_LORA_FOR_RETICULUM` is true)
* **CRITICAL - Pin Mapping:** **Verify that `LORA_SCK_PIN`, `LORA_MISO_PIN`, `LORA_MOSI_PIN`, `LORA_SS_PIN`, `LORA_RST_PIN`, and `LORA_DI0_PIN` in `config.h` exactly match the schematic for your specific ESP32 LoRa board model AND VERSION (e.g., Heltec V1, V2, V3 have different pinouts).** This is a very common point of failure.
* **CRITICAL - Frequency Band & Antenna:** Ensure `LORA_BAND` in `config.h` (e.g., `915E6`, `868E6`, `433E6`) is correct for your geographical region and that you are using an antenna designed for that frequency. A mismatched antenna will result in very poor or no communication.
* **Antenna Connection:** Ensure the LoRa antenna is securely connected to the board. **Never transmit without an antenna connected, as it can damage the LoRa chip.**
* **Range & Obstructions:** LoRa range is affected by environment (buildings, hills). Test with clear line-of-sight first.
* **Matching Configurations:** Ensure your other LoRa Reticulum node(s) are configured with the same LoRa parameters (frequency, bandwidth, spreading factor, coding rate, sync word if customized). The default LoRa library settings usually work for basic communication if both ends use them.

---
### Reticulum Network Issues
* **CRITICAL - Reticulum Library & API:** The `reticulum_handler.cpp` code contains API calls (e.g., for `WiFiInterface`, `LoRaInterface`, `Destination`, `Identity::from_hex`, `announceName`, `announceData`, `Packet::send`) that **MUST be verified against the specific version of the `markqvist/Reticulum` library you have installed for ESP32.** API calls can change between library versions or ports. Consult Reticulum's official documentation and ESP32 examples.
* **No Other Nodes:** Reticulum needs other nodes to communicate with. Ensure you have at least one other Reticulum node running on the same network (WiFi or LoRa "channel") and configured to listen for or route traffic.
* **Reticulum Identity/Destination:**
    * If sending to a specific `RETICULUM_DESTINATION_ADDRESS`, ensure the hash is correct and the destination node is reachable and listening for the specified `RETICULUM_APP_NAME` and aspects.
    * If announcing, ensure your receiving node is set up to discover or receive announcements for that `RETICULUM_APP_NAME` and aspects.
* **Interface Not Active:** Check serial logs for messages from `rns_status_and_log_callback`. Messages like "Transport interface started" or "Interface became active" indicate Reticulum is using the network hardware. If not, there's an issue with the interface setup (e.g., WiFi not connecting, LoRa init failed, or Reticulum interface object not added/initialized correctly).
* **Firewall (if PC is a node):** If one of your Reticulum nodes is a PC, ensure its firewall isn't blocking Reticulum's network traffic (UDP ports, etc.).

---
### JSON Payload / Data Transmission
* **Buffer Size:** If serial output shows `[JSON_ERROR] Failed to serialize JSON or buffer too small`, increase `JSON_PAYLOAD_BUFFER_SIZE` in `config.h`.
* **Data Reception:** Use a tool like `rncat` (if available in your Reticulum distribution) or a custom Reticulum application on another node to verify that packets are being received and that the JSON content is correct.
    * Example `rncat` usage (conceptual, actual command may vary):
        `rncat -l your_car_node_app_name/vehicle_data/stream`
* **Max Packet Size:** Reticulum packets have a maximum payload size. If `JSON_PAYLOAD_BUFFER_SIZE` is very large, the serialized JSON might exceed this, causing send failures. The code has a basic check against `RNS_MAX_PAYLOAD_SIZE_AFTER_HEADER`. For very large data, LXMF or application-level fragmentation would be needed.

---
### ESP32 Crashing or Resetting (Watchdog Timeout - WDT)
* **Blocking Code:** Long delays (`delay()`) or lengthy blocking operations in the `loop()` or in library calls can trigger the ESP32's watchdog timer, causing a reset. The `delay(1)` at the end of `loop()` is a minimal yield. If crashes occur, look for any part of your code or library functions that might be taking too long to execute without yielding.
* **Stack Overflow:** Large local variables (especially large arrays or `String` objects manipulated extensively in functions) can cause stack overflows. Increase stack size if necessary (advanced) or refactor to use heap allocation (`new`/`delete`) carefully, or reduce local variable sizes. `StaticJsonDocument` helps by using a statically allocated buffer.
* **Power Issues:** Brownouts or unstable power can cause resets.
* **Unhandled Exceptions:** Use `monitor_filters = esp32_exception_decoder` in `platformio.ini` or an equivalent ESP32 exception decoder tool to get more meaningful crash reports instead of just a Guru Meditation Error.

If you encounter issues not listed here, carefully review your serial debug output, check all configurations, and try to isolate the problematic component or code section.
