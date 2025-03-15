# Akita CarNode - Reticulum Vehicle Node

This project turns your vehicle into a Reticulum network node using an ESP32, an open-source Bluetooth OBD-II adapter, a GPS module, and either WiFi or LoRa (via a Heltec V1.1). It gathers vehicle data (RPM, speed, temperature), GPS location, and transmits it over the Reticulum network.

## Hardware Requirements

* ESP32 development board
* Open-source Bluetooth OBD-II adapter
* GPS module (e.g., NEO-6M)
* Heltec V1.1 (for LoRa Reticulum, optional)
* Wiring and power supply (e.g., USB power bank or vehicle 12V-to-5V adapter)

## Software Requirements

* Arduino IDE
* ESP32 board support package
* Arduino libraries:
    * `BLEDevice` (part of ESP32 BLE Arduino)
    * `TinyGPS++`
    * `LoRa` (if using LoRa Reticulum)
    * `Reticulum`

## Setup Instructions

1.  **Hardware Setup:**
    * Connect the OBD-II adapter to your vehicle's OBD-II port.
    * Connect the GPS module to the ESP32 (RX to pin 34, TX to pin 12, or adjust as needed).
    * If using LoRa Reticulum, connect the Heltec V1.1 to the ESP32.
    * Power the ESP32 using a suitable power source.

2.  **Software Setup:**
    * Install the Arduino IDE and the ESP32 board support package.
    * Install the required libraries through the Arduino IDE's Library Manager.
    * Install and configure the Reticulum software on your network.

3.  **Configuration:**
    * Open the `AkitaCarNode_Reticulum.ino` file in the Arduino IDE.
    * **OBD-II UUIDs:** Replace `"YOUR_OBDII_SERVICE_UUID"` and `"YOUR_OBDII_CHARACTERISTIC_UUID"` with the actual UUIDs of your OBD-II adapter. Use a BLE scanner app on your smartphone to find these if needed.
    * **WiFi Credentials:** If using WiFi Reticulum, replace `"YOUR_WIFI_SSID"` and `"YOUR_WIFI_PASSWORD"` with your network credentials.
    * **LoRa Band:** If using LoRa Reticulum, ensure that `LORA_BAND` is set to the correct frequency for your region.

4.  **Upload the Code:**
    * Connect your ESP32 to your computer.
    * Select the correct board and port in the Arduino IDE.
    * Upload the code to the ESP32.

5.  **Testing and Deployment:**
    * Open the Serial Monitor in the Arduino IDE to observe the output and check for any errors.
    * Use Reticulum's command-line tools or monitoring tools to verify that the ESP32 node is visible on the network and that data is being transmitted.
    * Confirm that the GPS and OBD-II data is being read correctly.
    * Securely mount the ESP32 and other components in your vehicle.
    * Test the node in various locations to ensure reliable connectivity.

## Code Explanation

The code performs the following tasks:

* **BLE Scanning and Connection:** Scans for and connects to the OBD-II Bluetooth adapter.
* **OBD-II Data Retrieval:** Retrieves vehicle data (RPM, speed, temperature) using OBD-II PIDs.
* **GPS Data Retrieval:** Reads GPS location data from the connected GPS module.
* **Reticulum Initialization:** Initializes the Reticulum network interface (WiFi or LoRa).
* **Node Announcement:** Announces the node on the Reticulum network.
* **Data Transmission:** Transmits the collected data over the Reticulum network.

## Troubleshooting

* **BLE Connection Issues:**
    * Ensure the OBD-II adapter is powered on and within range.
    * Double-check the UUIDs.
    * Try restarting the OBD-II adapter.
* **GPS Issues:**
    * Ensure the GPS module has a clear view of the sky.
    * Check the wiring connections.
* **LoRa Issues:**
    * Verify the LoRa band and antenna connections.
    * Check for interference.
* **WiFi Issues:**
    * Confirm WiFi credentials are correct.
    * Check WiFi signal strength.
* **Reticulum Issues:**
    * Verify that your Reticulum network is properly configured.
    * Check that other Reticulum nodes are reachable.
    * Use Reticulum's debugging tools.
* **Serial Monitor:** Use the serial monitor to debug and look for error messages.
* **UUID Errors:** Double check that the UUID's are correct.
* **Parsing Errors:** If the OBD data is not properly parsed, double check the PID's and the parsing code.

## Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues for bug fixes, feature requests, or improvements.
