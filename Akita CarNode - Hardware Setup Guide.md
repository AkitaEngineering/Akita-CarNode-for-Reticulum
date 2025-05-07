# Akita CarNode - Hardware Setup Guide

This guide details the hardware components and connections required for the Akita CarNode project.

## Required Components

1.  **ESP32 Development Board:**
    * Any standard ESP32 board (e.g., ESP32-DevKitC, ESP32-WROOM-32 based boards).
    * **For LoRa:** An ESP32 board with an integrated LoRa chip is required (e.g., Heltec WiFi LoRa 32 series, TTGO LoRa32 series). Ensure the LoRa chip is supported by the "LoRa by Sandeep Mistry" library (typically Semtech SX1276/SX1278).
2.  **Bluetooth Low Energy (BLE) OBD-II Adapter:**
    * Must be a **BLE (Bluetooth 4.0+)** type, not a classic Bluetooth adapter.
    * Generic ELM327-compatible BLE adapters are common.
3.  **GPS Module:**
    * NMEA-compatible serial GPS module.
    * Commonly used modules: u-blox NEO-6M, NEO-7M, NEO-M8N.
    * Ensure the module has a UART (TX/RX) interface.
    * An active or passive GPS antenna appropriate for the module.
4.  **LoRa Antenna (If using LoRa):**
    * An antenna specifically designed for the LoRa frequency band configured in `config.h` (e.g., 433MHz, 868MHz, 915MHz).
    * Must have the correct connector for your ESP32 LoRa board (e.g., SMA, U.FL/IPEX).
5.  **Power Supply:**
    * A stable 5V DC power source capable of providing at least 1A (ESP32 with peripherals can draw significant current, especially during WiFi/BLE transmission).
    * **Options:**
        * USB Power Bank (for testing).
        * Vehicle 12V/24V to 5V DC-DC Buck Converter: Recommended for in-vehicle installation. Connect this to an **ignition-switched power source** in your vehicle to prevent draining the car battery when the vehicle is off.
6.  **Wiring & Connectors:**
    * Jumper wires (Dupont cables) for prototyping.
    * Breadboard (optional, for initial testing).
    * Soldering equipment if creating a more permanent setup.
7.  **(Optional) Status LED:**
    * One standard LED (any color).
    * One current-limiting resistor (typically 220 Ohm to 330 Ohm for a 3.3V GPIO pin).


### 1. ESP32 Power:
* Connect your 5V power supply to the ESP32's 5V/VIN pin and GND to an ESP32 GND pin.

### 2. GPS Module to ESP32:
* **GPS VCC** -> ESP32 **3.3V** (Most NEO-6M modules can run on 3.3V; some might require/tolerate 5V. **Check your GPS module's datasheet!**)
* **GPS GND** -> ESP32 **GND**
* **GPS TX Output** -> ESP32 **`GPS_RX_PIN`** (As defined in `config.h`, e.g., GPIO34)
* **GPS RX Input** -> ESP32 **`GPS_TX_PIN`** (As defined in `config.h`, e.g., GPIO12. This is often not strictly necessary if you are only reading data from the GPS and not sending configuration commands to it.)
    * **Note on Logic Levels:** Ensure your GPS module's UART signals are 3.3V compatible with the ESP32. Most modern modules are. If you have a 5V UART GPS, a logic level shifter would be needed for the GPS TX to ESP32 RX line.

### 3. LoRa Module (If using an ESP32 LoRa Board):
* The LoRa chip is typically pre-wired on these boards.
* **Crucially, ensure the LoRa pin definitions in `config.h` (`LORA_SCK_PIN`, `LORA_MISO_PIN`, `LORA_MOSI_PIN`, `LORA_SS_PIN`, `LORA_RST_PIN`, `LORA_DI0_PIN`) exactly match the schematic of your specific ESP32 LoRa board model and version.** These can vary significantly (e.g., Heltec V1 vs V2 vs V3).
* Connect the LoRa antenna securely to the board's LoRa antenna connector. **Never operate the LoRa module at high power without an antenna connected, as it can damage the chip.**

### 4. BLE OBD-II Adapter:
* This is a wireless connection. No physical wiring to the ESP32 is needed for BLE.
* Plug the OBD-II adapter into your vehicle's OBD-II diagnostic port.
* The vehicle's ignition may need to be in the "ON" or "Accessory" position for the OBD-II port (and thus the adapter) to be powered.

### 5. (Optional) Status LED to ESP32:
* LED Anode (longer leg) -> Current-limiting Resistor (e.g., 220 Ohm)
* Other end of Resistor -> ESP32 **`STATUS_LED_PIN`** (As defined in `config.h`, e.g., GPIO2)
* LED Cathode (shorter leg, flat side) -> ESP32 **GND**

## Assembly Steps & Considerations:

1.  **Prototyping:** It's highly recommended to first assemble and test the system on a breadboard (where applicable) or with reliable jumper wire connections before any permanent installation.
2.  **Soldering (Permanent Setup):** For a durable in-vehicle installation, solder connections to a perfboard or a custom PCB. Use appropriate strain relief for wires.
3.  **Enclosure:** Consider a 3D-printed or off-the-shelf enclosure to protect the electronics from dust, moisture (if applicable), and physical damage. Ensure adequate ventilation if the ESP32 runs warm.
4.  **GPS Antenna Placement:** For best GPS reception, the antenna needs a clear view of the sky. Place it on the dashboard, rear parcel shelf, or externally (if using a weatherproof external antenna). Avoid metallic obstructions.
5.  **LoRa Antenna Placement (if used):** Position for optimal range. Avoid placing it directly against large metal surfaces. Vertical orientation is often best.
6.  **Power Stability:** Ensure your chosen power supply is stable and can handle current spikes from the ESP32 (especially during WiFi/BLE transmissions). Poor power can lead to instability and resets.
7.  **Vehicle Integration:**
    * Securely mount the CarNode unit in the vehicle.
    * Route power cables safely, avoiding interference with vehicle operation or airbags.
    * If using an external 12V-to-5V converter, ensure it's fused appropriately on the 12V side.

By following these hardware setup guidelines, you should be able to establish the necessary physical connections for the Akita CarNode. Always double-check pinouts for your specific ESP32 board and peripherals from their datasheets or schematics.
