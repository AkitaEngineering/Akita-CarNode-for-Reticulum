#ifndef CONFIG_H
#define CONFIG_H

// -----------------------------------------------------------------------------
// Akita CarNode Configuration
// -----------------------------------------------------------------------------
// Project: Akita-CarNode-for-Reticulum
// Organization: Akita Engineering
// License: GPLv3
// -----------------------------------------------------------------------------

// --- Serial Debugging ---
// Level 0: No debug output
// Level 1: Standard debug messages (INFO, WARN, ERROR)
// Level 2: Verbose debug, including Reticulum's DEBUG/VERBOSE messages (if library supports it)
#define ENABLE_SERIAL_DEBUG 1
#define SERIAL_BAUD_RATE 115200

// --- Vehicle Identification (For Reticulum announcement & payload) ---
#define VEHICLE_ID "AkitaCar01" // Unique identifier for this vehicle/node

// --- Bluetooth Low Energy (BLE) OBD-II Adapter Configuration ---

// Option 1 (RECOMMENDED FOR RELIABILITY): Connect by Specific Service & Characteristic UUIDs
// Find these using a BLE Scanner app (e.g., nRF Connect, LightBlue) for YOUR adapter.
// #define USE_OBDII_UUIDS
// #define OBDII_SERVICE_UUID         "0000ffe0-0000-1000-8000-00805f9b34fb" // EXAMPLE for some ELM327 clones
// #define OBDII_CHARACTERISTIC_UUID  "0000ffe1-0000-1000-8000-00805f9b34fb" // EXAMPLE for some ELM327 clones (often R/W/N)

// Option 2: Connect by OBD-II Adapter Name (Simpler, but can be less reliable if name changes or is generic)
// If USE_OBDII_UUIDS is commented out, this name will be used.
#define OBDII_DEVICE_NAME "OBDII" // <<!>> YOUR OBD-II ADAPTER'S ADVERTISED NAME <<!>>
                                  // Common examples: "OBDII", "VEEPEAK", "OBDLINK", "Viecar BLE", "KONNWEI"

// Option 3 (Alternative for some adapters like Nordic UART Service based ones):
// #define USE_OBDII_NORDIC_UART_SERVICE
// #define NUS_SERVICE_UUID_STR           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
// #define NUS_CHARACTERISTIC_TX_UUID_STR "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // ESP32 writes to this (OBD RX)
// #define NUS_CHARACTERISTIC_RX_UUID_STR "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // ESP32 receives from this (OBD TX)


#define BLE_SCAN_TIME_SECONDS 7       // How long to scan for the BLE OBD-II adapter
#define BLE_INITIAL_RETRY_DELAY_MS 3000 // Initial delay before first BLE reconnect/rescan attempt
#define BLE_MAX_RETRY_DELAY_MS 60000  // Maximum delay between BLE reconnect/rescan attempts (1 minute)
#define BLE_RETRY_JITTER_MS 500       // Max random jitter to add to BLE retry delay

// --- OBD-II PIDs to Query ---
// Format: { "PID_CODE", "PID_NAME_FOR_DEBUG", "EXPECTED_RESPONSE_PREFIX_NO_SPACES" }
// Example: Engine RPM: "010C", Vehicle Speed: "010D", Coolant Temp: "0105"
// The response prefix (e.g., "410C" for RPM) helps validate the correct PID response.
typedef struct {
    const char* code;           // e.g., "010C"
    const char* name;           // e.g., "Engine RPM"
    const char* responsePrefix; // e.g., "410C" (no spaces)
} OBD_PID;

const OBD_PID OBD_PIDS_TO_QUERY[] = {
    {"010C", "Engine RPM", "410C"},
    {"010D", "Vehicle Speed", "410D"},
    {"0105", "Coolant Temp", "4105"},
    // {"010F", "Intake Air Temp", "410F"}, // Example: Add more PIDs if your vehicle/adapter supports them
    // {"0104", "Engine Load", "4104"}
};
const int NUM_OBD_PIDS = sizeof(OBD_PIDS_TO_QUERY) / sizeof(OBD_PIDS_TO_QUERY[0]);
#define OBD_QUERY_INTERVAL_MS 750    // Time between querying consecutive PIDs
#define OBD_RESPONSE_TIMEOUT_MS 1500 // Max time to wait for an OBD response (currently unused, relies on BLE stack timeouts)

// --- GPS Module Configuration ---
#define ENABLE_GPS true
#define GPS_RX_PIN 34            // ESP32 pin connected to GPS TX output
#define GPS_TX_PIN 12            // ESP32 pin connected to GPS RX input (often not needed if only reading)
#define GPS_BAUD_RATE 9600
#define GPS_READ_INTERVAL_MS 1000 // How often to check for new GPS data

// --- Reticulum Network Configuration ---
#define RETICULUM_APP_NAME "akita_car_node" // App name for service discovery and packet context

// Option 1: WiFi Interface
#define USE_WIFI_FOR_RETICULUM true       // Set to true for WiFi, false for LoRa
#define WIFI_SSID "YOUR_WIFI_SSID"         // <<!>> YOUR WIFI SSID <<!>>
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD" // <<!>> YOUR WIFI PASSWORD <<!>>
#define WIFI_CONNECT_TIMEOUT_MS 20000     // Timeout for each WiFi connection attempt
#define WIFI_INITIAL_RETRY_DELAY_MS 5000  // Initial delay before first WiFi reconnect attempt
#define WIFI_MAX_RETRY_DELAY_MS 120000    // Maximum delay between WiFi reconnect attempts (2 minutes)
#define WIFI_RETRY_JITTER_MS 1000         // Max random jitter to add to WiFi retry delay

// Option 2: LoRa Interface (Typically for Heltec WiFi LoRa 32 boards or similar)
// If USE_WIFI_FOR_RETICULUM is false, these settings are used.
#define USE_LORA_FOR_RETICULUM false
// **CRITICAL**: Verify these pins for YOUR specific ESP32 LoRa board model and version!
// Common for Heltec WiFi LoRa 32 V2 (check V1, V3, or other boards' schematics)
#define LORA_SCK_PIN  5   // SPI SCK
#define LORA_MISO_PIN 19  // SPI MISO
#define LORA_MOSI_PIN 27  // SPI MOSI
#define LORA_SS_PIN   18  // LoRa Chip Select (NSS)
#define LORA_RST_PIN  14  // LoRa Reset
#define LORA_DI0_PIN  26  // LoRa IRQ (DIO0)
// LoRa Frequency Band (e.g., 915E6 for US/AU, 868E6 for EU, 433E6 for Asia/others)
#define LORA_BAND 915E6   // <<!>> ADJUST FOR YOUR REGION AND ANTENNA <<!>>

// Reticulum Destination (Optional)
// If defined and not empty, data packets will be sent as unicast to this specific RNS destination address (16-byte hex hash).
// If commented out or empty, data will be announced using RNS.announceData.
// Get this hash from the destination node's `rnstatus` or similar Reticulum utility.
#define RETICULUM_DESTINATION_ADDRESS "" // Example: "abcdef0123456789abcdef0123456789"
                                         // Set to "" or comment out to use announce by default.

#define RETICULUM_SEND_INTERVAL_MS 10000 // How often to send data over Reticulum (10 seconds)

// --- Payload Configuration ---
// Adjust based on the number of fields and typical data length for Reticulum JSON payload.
// Includes vehicle ID, timestamp, OBD data, GPS data, and status flags.
#define JSON_PAYLOAD_BUFFER_SIZE 512

// --- Status LED Configuration (Optional) ---
#define ENABLE_STATUS_LED true
// Common built-in LED pins: 2 for ESP32-DevKitC, 25 for Heltec WiFi LoRa 32
#define STATUS_LED_PIN 2 // <<!>> VERIFY FOR YOUR BOARD (e.g., 25 for Heltec LoRa boards) <<!>>

// --- Debug Macros ---
#if ENABLE_SERIAL_DEBUG > 0
    #define DEBUG_PRINT(x) Serial.print(x)
    #define DEBUG_PRINTLN(x) Serial.println(x)
    #define DEBUG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__) // Use ## for C++20 compatibility with zero variadic args
#else
    #define DEBUG_PRINT(x)
    #define DEBUG_PRINTLN(x)
    #define DEBUG_PRINTF(fmt, ...)
#endif

#endif // CONFIG_H
