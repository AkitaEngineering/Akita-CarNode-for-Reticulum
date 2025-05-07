# Akita CarNode - Reticulum Vehicle Node

**Organization:** Akita Engineering
**License:** GPLv3
**Version:** 1.0.0 (Updated: 2025-05-07)
**Project Status:** Development - Core functionality implemented, API verification for Reticulum and extensive real-world testing pending.

This project transforms your vehicle into a mobile [Reticulum Network Stack](https://github.com/markqvist/Reticulum) node. Utilizing an ESP32 microcontroller, it interfaces with a Bluetooth Low Energy (BLE) OBD-II adapter to gather real-time vehicle diagnostics and a GPS module for location tracking. This data is then formatted (preferably as JSON) and transmitted over the Reticulum network using either WiFi or LoRa.

This allows for a decentralized, resilient way to monitor vehicle telemetry, location, and other data without relying on centralized servers or conventional internet connectivity where Reticulum networks are deployed.

## Features

* **Vehicle Data Acquisition:** Connects to standard BLE OBD-II adapters.
* **Configurable OBD-II PIDs:** Queries a user-defined list of OBD-II Parameter IDs (PIDs) like RPM, speed, coolant temperature, etc.
* **GPS Integration:** Reads NMEA data from a serial GPS module (e.g., NEO-6M) for location (latitude, longitude, altitude), speed, and satellite count.
* **Reticulum Network Node:** Acts as a fully-fledged Reticulum node.
    * **Flexible Transport:** Supports WiFi or LoRa (e.g., via Heltec LoRa 32 boards) for Reticulum communication.
    * **Data Transmission:** Sends collected vehicle and GPS data over Reticulum.
        * Can announce data for general consumption.
        * Can send data to a specific, pre-configured Reticulum destination.
* **JSON Data Format:** Transmits data in JSON format for easy parsing and integration.
* **Robust Connectivity:**
    * Exponential backoff with jitter for BLE (OBD-II) and WiFi connection retries.
    * Graceful handling of disconnections.
* **User Configuration:** Most parameters are easily configurable via a central `config.h` file.
* **Status Indication:** Utilizes an optional status LED for quick diagnostics (e.g., BLE status, GPS fix, Reticulum connection).
* **Modular Codebase:** Organized into logical C++ modules for easier maintenance and extension.
* **Serial Debugging:** Comprehensive serial output for monitoring and troubleshooting, configurable via `config.h`.

## Use Cases

* Remote vehicle monitoring in off-grid or unreliable network areas.
* Building decentralized fleet management systems.
* Personal vehicle tracking and diagnostics for enthusiasts.
* Data collection for community-based environmental or traffic monitoring.
* Emergency communication and location sharing for vehicles.

## Hardware Requirements

1.  **ESP32 Development Board:**
    * Any standard ESP32 board (e.g., ESP32-DevKitC, WEMOS LOLIN D32).
    * For LoRa: A board with an integrated LoRa chip like Heltec WiFi LoRa 32 (V1, V2, or V3), TTGO LoRa32. Ensure pin definitions in `config.h` match your board.
2.  **Bluetooth Low Energy (BLE) OBD-II Adapter:** A generic ELM327-compatible OBD-II adapter that uses BLE for communication.
    * **Crucial:** You will need to determine its BLE device name or, preferably, its specific Service and Characteristic UUIDs for data communication.
3.  **GPS Module:** Any NMEA-compatible serial GPS module (e.g., u-blox NEO-6M, NEO-7M, NEO-M8N). Typically connects via UART.
4.  **LoRa Antenna (if using LoRa):** An appropriate antenna for the configured LoRa frequency band (e.g., 433MHz, 868MHz, 915MHz).
5.  **Wiring & Connectors:** Jumper wires, breadboard (for prototyping), or a custom PCB.
6.  **Power Supply:** A stable 5V power source.
    * USB power bank.
    * Vehicle 12V/24V to 5V DC-DC converter (recommended for in-car installation, connect to an ignition-switched source to avoid battery drain).
7.  **(Optional) Status LED:** A standard LED and a current-limiting resistor (e.g., 220-330 Ohm).

## Software Requirements

1.  **Arduino IDE** or **PlatformIO IDE**.
2.  **ESP32 Board Support Package:** Installed in Arduino IDE or via PlatformIO.
3.  **Required Arduino Libraries:**
    * `BLEDevice` (Part of ESP32 BLE Arduino library, usually comes with ESP32 core)
    * `TinyGPS++` by Mikal Hart (for GPS NMEA parsing)
    * `LoRa` by Sandeep Mistry (if using LoRa, for Heltec/TTGO boards)
    * `Reticulum` by Markqvist (the RNS stack for ESP32 - **ensure you get the correct ESP32-compatible version/port**)
    * `ArduinoJson` by Benoit Blanchon (for formatting data payloads)
4.  **Reticulum Network:** A functional Reticulum network to send data to/through. This could be other ESP32 nodes, Raspberry Pis, or computers running Reticulum.

## Project Structure
```
Akita-CarNode-for-Reticulum/
├── Akita_CarNode_Reticulum/    # Main Arduino sketch folder
│   ├── Akita_CarNode_Reticulum.ino # Main sketch file
│   ├── config.h                # User configurations (WiFi, BLE UUIDs, PIDs, etc.)
│   ├── ble_handler.h/cpp       # BLE connection and OBD-II communication
│   ├── obd_parser.h/cpp        # OBD-II PID parsing logic
│   ├── gps_handler.h/cpp       # GPS data acquisition
│   ├── reticulum_handler.h/cpp # Reticulum network communication
│   └── status_led.h/cpp        # (Optional) For LED status indicators
├── docs/                       # Detailed documentation (to be expanded)
│   ├── hardware_setup.md
│   └── troubleshooting.md
├── LICENSE                     # GPLv3 License file
└── README.
```
## Setup and Configuration

1.  **Hardware Assembly:**
    * Connect the OBD-II adapter to your vehicle's OBD-II port.
    * Connect the GPS module to the ESP32:
        * GPS TX to ESP32 `GPS_RX_PIN` (defined in `config.h`).
        * GPS RX to ESP32 `GPS_TX_PIN` (defined in `config.h`, often not strictly needed if only reading from GPS).
        * GPS VCC to ESP32 3.3V or 5V (check module specs).
        * GPS GND to ESP32 GND.
    * If using LoRa, connect the antenna to your ESP32 LoRa board.
    * (Optional) Connect the status LED (Anode via resistor to a GPIO pin defined as `STATUS_LED_PIN`, Cathode to GND).
    * Power the ESP32 using a suitable source.

2.  **Software Installation:**
    * Install Arduino IDE or PlatformIO.
    * Install the ESP32 board support package.
    * Install all libraries listed under "Software Requirements" using the Arduino Library Manager or PlatformIO's library management. **For Reticulum, ensure you are using a version compatible with ESP32.**

3.  **Configuration (`Akita_CarNode_Reticulum/config.h`):**
    * Open `config.h` in a text editor.
    * **Serial Debugging:**
        * `ENABLE_SERIAL_DEBUG`: Set to `true` for verbose output, `false` for less, or `2` for maximum Reticulum debug.
        * `SERIAL_BAUD_RATE`: Typically `115200`.
    * **Vehicle ID:**
        * `VEHICLE_ID`: A unique name for this node (e.g., "MyTruck", "AkitaCar01").
    * **BLE OBD-II Adapter:**
        * **Option 1 (Recommended for reliability):** Find your OBD-II adapter's actual Service and Characteristic UUIDs using a BLE scanner app (like nRF Connect, LightBlue).
            * Uncomment `#define USE_OBDII_UUIDS`.
            * Set `OBDII_SERVICE_UUID` and `OBDII_CHARACTERISTIC_UUID`.
        * **Option 2 (Simpler, but can be less reliable):** Connect by device name.
            * Set `OBDII_DEVICE_NAME` to your adapter's advertised name (e.g., "OBDII", "VEEPEAK", "OBDLINK").
        * `BLE_SCAN_TIME_SECONDS`, `BLE_INITIAL_RETRY_DELAY_MS`, etc., for connection timing.
    * **OBD-II PIDs:**
        * Modify the `OBD_PIDS_TO_QUERY` array to include the PIDs you want to read. Ensure your vehicle and adapter support them. Format: `{"PID_CODE", "PID_NAME_FOR_DEBUG", "EXPECTED_RESPONSE_PREFIX"}`.
    * **GPS Module:**
        * `ENABLE_GPS`: Set to `true` or `false`.
        * `GPS_RX_PIN`, `GPS_TX_PIN`, `GPS_BAUD_RATE`.
    * **Reticulum Network:**
        * `RETICULUM_APP_NAME`: An application name for Reticulum (e.g., "akita_carrnode").
        * **WiFi or LoRa:**
            * Set `USE_WIFI_FOR_RETICULUM` to `true` for WiFi, or `false` for LoRa.
            * If WiFi: Set `WIFI_SSID` and `WIFI_PASSWORD`.
            * If LoRa: Set `USE_LORA_FOR_RETICULUM` to `true`. Configure LoRa pins (`LORA_SCK_PIN`, etc. - **verify these for your specific Heltec/TTGO board version**) and `LORA_BAND`.
        * **Reticulum Destination (Optional):**
            * If you want to send data directly to a specific Reticulum node, set `RETICULUM_DESTINATION_ADDRESS` to its 16-byte hex hash.
            * If left empty or as the placeholder, data will be announced.
        * `RETICULUM_SEND_INTERVAL_MS`: How often to transmit data.
    * **Status LED:**
        * `ENABLE_STATUS_LED`: `true` or `false`.
        * `STATUS_LED_PIN`: The GPIO pin for the LED.
    * **JSON Payload:**
        * `JSON_PAYLOAD_BUFFER_SIZE`: Adjust if your JSON data is larger or smaller.

4.  **Build and Upload:**
    * Select the correct ESP32 board in your IDE.
    * Select the correct COM port.
    * Compile/Verify the sketch to check for errors.
    * Upload the sketch to your ESP32.

5.  **Testing and Deployment:**
    * Open the Arduino Serial Monitor (or PlatformIO serial monitor) at the configured `SERIAL_BAUD_RATE`.
    * Observe debug messages for:
        * BLE connection attempts and success/failure.
        * OBD-II PID queries and responses.
        * GPS fix status and data.
        * WiFi/LoRa connection status.
        * Reticulum initialization and data transmission.
    * On another Reticulum node, use tools like `rncat` or a custom script to listen for announcements or packets from your CarNode's application name and aspects.
    * Verify the JSON payload is correct.
    * Test connection retry logic (e.g., by temporarily disabling OBD-II adapter or WiFi).
    * Once confirmed working, securely mount the components in your vehicle. Ensure good GPS antenna placement (clear sky view).

## Troubleshooting

* **No Serial Output:**
    * Check baud rate, COM port selection, and USB cable.
    * Ensure `ENABLE_SERIAL_DEBUG` is `true` in `config.h`.
* **BLE OBD-II Connection Issues:**
    * **UUIDs/Name are CRITICAL:** Double, triple-check `OBDII_DEVICE_NAME` or the `OBDII_SERVICE_UUID` and `OBDII_CHARACTERISTIC_UUID` in `config.h`. Use a BLE scanner app (nRF Connect, LightBlue) on your phone to discover these from *your specific adapter*.
    * Ensure the OBD-II adapter is powered (vehicle ignition may need to be on or accessory mode).
    * Check BLE range.
    * Restart the OBD-II adapter and ESP32.
    * Some adapters are BLE Classic + BLE, ensure you are targeting the BLE part.
* **No OBD-II Data / Incorrect Data:**
    * Verify the PIDs in `OBD_PIDS_TO_QUERY` are supported by your vehicle and adapter.
    * Check the `responsePrefix` in `config.h` for each PID against actual responses.
    * Inspect `obd_parser.cpp` if data is received but parsed incorrectly.
* **GPS Issues (No Fix / No Data):**
    * Ensure the GPS module has a clear view of the sky (can take several minutes for first fix).
    * Verify `GPS_RX_PIN`, `GPS_TX_PIN`, and `GPS_BAUD_RATE` in `config.h`. Check wiring.
    * Some GPS modules have a status LED indicating fix status.
* **WiFi Connection Issues:**
    * Verify `WIFI_SSID` and `WIFI_PASSWORD` in `config.h`.
    * Check WiFi signal strength. ESP32 antennas can be sensitive.
* **LoRa Issues:**
    * **CRITICAL: Verify `LORA_BAND` (frequency) and antenna match your region and hardware.**
    * **CRITICAL: Verify LoRa pin definitions (`LORA_SCK_PIN`, etc.) in `config.h` match your specific ESP32 LoRa board model and version (e.g., Heltec V1 vs V2 vs V3 have different pinouts).**
    * Ensure a proper antenna is securely connected.
    * Check for interference from other devices.
* **Reticulum Issues:**
    * **VERIFY API CALLS:** The `reticulum_handler.cpp` contains logic for interfacing with Reticulum. The exact API calls for creating interfaces, destinations, announcing, and sending packets **must be verified against the specific Reticulum ESP32 library version you are using.** Consult its documentation and examples.
    * Ensure your other Reticulum nodes (receivers) are properly configured to listen for the `RETICULUM_APP_NAME` and aspects defined.
    * Use Reticulum's own debugging tools or log levels if available on the ESP32 port.
* **JSON Payload Issues:**
    * If data is truncated or missing, increase `JSON_PAYLOAD_BUFFER_SIZE` in `config.h`.
    * Verify JSON structure on the receiving end.

## Contributing

Contributions are welcome! This project is in active development. Please feel free to submit pull requests for bug fixes, new features, or improvements. You can also open issues for discussion or to report problems.

**Areas for Future Development:**
* Persistent storage for Reticulum identity.
* Data buffering on the ESP32 if Reticulum network is temporarily unavailable (e.g., using SPIFFS/LittleFS).
* Over-The-Air (OTA) firmware updates via WiFi or Reticulum.
* Web UI for configuration and status (when on WiFi).
* More sophisticated power management modes.
* Support for more OBD-II PIDs and advanced diagnostic messages.
* More detailed documentation for hardware setup and advanced configuration.

## License

This project is licensed under the GNU General Public License v3.0. See the `LICENSE` file for details.
