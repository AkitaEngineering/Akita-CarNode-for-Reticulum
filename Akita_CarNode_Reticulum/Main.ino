// -----------------------------------------------------------------------------
// Akita CarNode for Reticulum - Main Sketch
// -----------------------------------------------------------------------------
// Organization: Akita Engineering
// License: GPLv3
// Description: Turns a vehicle into a Reticulum network node, gathering OBD-II
//              and GPS data, and transmitting it via WiFi or LoRa.
// Version: 1.0.0 (Conceptual Completion: 2025-05-07)
// -----------------------------------------------------------------------------

#include "config.h" // All user configurations

#include <Arduino.h>
#include <ArduinoJson.h> // For creating JSON payloads

// Core Application Modules
#include "ble_handler.h"
#include "obd_parser.h"
#include "gps_handler.h"
#include "reticulum_handler.h"
#include "status_led.h"     // Handles status LED logic

// Data structure to hold all sensor values and statuses
typedef struct {
    // OBD-II Data
    float rpm;
    float speed_kmh;
    float coolant_temp_c;
    // Add more OBD fields here if you add PIDs in config.h
    // float intake_air_temp_c;
    // float engine_load_percent;

    // GPS Data
    bool gps_fix;
    float latitude;
    float longitude;
    float altitude_m;
    uint8_t satellites;
    float speed_gps_kmh; // Speed from GPS

    // System Status
    bool ble_connected;       // OBD-II BLE link status
    // reticulum_connected (physical layer) is handled by isReticulumConnected()
    // reticulum_transport_active (RNS stack ready) is handled by isReticulumReadyToSend()

} VehicleData;

VehicleData currentVehicleData = {0}; // Initialize all numeric to zero, bool to false

// Timers for non-blocking operations
unsigned long lastReticulumSendTime = 0;
unsigned long lastOBDQueryTime = 0;
unsigned long lastGPSReadTime = 0;
unsigned long lastStatusLedUpdateTime = 0;

int currentPIDIndex = 0; // To cycle through OBD_PIDS_TO_QUERY

void setup() {
#if ENABLE_SERIAL_DEBUG > 0
    Serial.begin(SERIAL_BAUD_RATE);
    unsigned long serialStartTime = millis();
    while (!Serial && (millis() - serialStartTime < 2000)) { // Wait up to 2s for serial port to connect
        delay(10);
    }
    DEBUG_PRINTLN(F("\n\n[INFO] Akita CarNode Initializing..."));
    DEBUG_PRINTF("[INFO] Sketch Version: 1.0.0, Compiled: %s %s\n", __DATE__, __TIME__);
    DEBUG_PRINTF("[INFO] ESP32 Chip Revision: %d, CPU Freq: %d MHz\n", ESP.getChipRevision(), ESP.getCpuFreqMHz());
    DEBUG_PRINTF("[INFO] Free Heap: %u bytes\n", ESP.getFreeHeap());
    DEBUG_PRINTF("[INFO] Target Vehicle ID: %s\n", VEHICLE_ID);
#endif

    setupStatusLed(); // Initialize status LED pins and default state
    setLedStatus(LED_STATUS_INITIALIZING);

    initOBDParser();  // Initialize OBD parser module (if any setup needed)
    initGPS();        // Initialize GPS module and serial communication
    initBLE();        // Initialize BLE system (but doesn't connect yet)
    initReticulum();  // Initialize Reticulum stack and chosen interface (WiFi/LoRa)

    DEBUG_PRINTLN(F("[INFO] Core setup complete. Starting main loop."));
    // Initial status will be attempting connections
    setLedStatus(LED_STATUS_IDLE); // Or a specific "connecting" state
}

void loop() {
    unsigned long currentMillis = millis();

    // 1. Handle BLE Connection and OBD-II Data Acquisition
    if (!isBLEConnected()) {
        currentVehicleData.ble_connected = false;
        connectBLE(); // Non-blocking, manages its own timing and retries
        setLedStatus(LED_STATUS_BLE_CONNECTING); // Update LED status
    } else { // BLE is connected
        if (!currentVehicleData.ble_connected) { // Just connected
            DEBUG_PRINTLN(F("[MAIN] BLE OBD-II Connected."));
            currentVehicleData.ble_connected = true;
            // LED status will be updated based on overall system state later
        }
        // Query PIDs sequentially with an interval
        if (currentMillis - lastOBDQueryTime >= OBD_QUERY_INTERVAL_MS) {
            lastOBDQueryTime = currentMillis;
            if (NUM_OBD_PIDS > 0) {
                requestOBDPID(OBD_PIDS_TO_QUERY[currentPIDIndex]);
                currentPIDIndex = (currentPIDIndex + 1) % NUM_OBD_PIDS;
            }
        }
        // Check for and process OBD responses (non-blocking)
        // OBD data (rpm, speed_kmh, coolant_temp_c) is updated directly in currentVehicleData by processOBDResponse
        processOBDResponse(&currentVehicleData.rpm, &currentVehicleData.speed_kmh, &currentVehicleData.coolant_temp_c);
    }

    // 2. Handle GPS Data Acquisition
#if ENABLE_GPS
    if (currentMillis - lastGPSReadTime >= GPS_READ_INTERVAL_MS) {
        lastGPSReadTime = currentMillis;
        // GPS data is updated directly in currentVehicleData by readGPS
        readGPS(&currentVehicleData.gps_fix, &currentVehicleData.latitude, &currentVehicleData.longitude,
                &currentVehicleData.altitude_m, &currentVehicleData.satellites, &currentVehicleData.speed_gps_kmh);
    }
#else
    currentVehicleData.gps_fix = false; // Ensure GPS fix is false if GPS is disabled
#endif

    // 3. Handle Reticulum Communication (Loop for processing stack and interface management)
    reticulumLoop(); // Essential for Reticulum to process its tasks, includes WiFi reconnection logic

    // 4. Update Overall System Status LED (less frequently than main loop)
    if (currentMillis - lastStatusLedUpdateTime >= 500) { // Update LED status every 500ms
        lastStatusLedUpdateTime = currentMillis;
        if (!currentVehicleData.ble_connected) {
            setLedStatus(LED_STATUS_BLE_CONNECTING);
        } else if (ENABLE_GPS && !currentVehicleData.gps_fix) {
            setLedStatus(LED_STATUS_GPS_NO_FIX);
        } else if (!isReticulumReadyToSend()) { // Checks physical layer (WiFi/LoRa) and RNS transport active
            setLedStatus(LED_STATUS_RETICULUM_CONNECTING);
        } else {
            setLedStatus(LED_STATUS_OPERATIONAL); // All key systems are go
        }
    }
    updateLed(); // Handle blinking patterns for the current LED status

    // 5. Send Data over Reticulum at specified intervals
    if (currentMillis - lastReticulumSendTime >= RETICULUM_SEND_INTERVAL_MS) {
        lastReticulumSendTime = currentMillis;

        if (isReticulumReadyToSend()) { // Check if Reticulum is connected and transport is active
            StaticJsonDocument<JSON_PAYLOAD_BUFFER_SIZE> jsonDoc;

            // Populate JSON document
            jsonDoc["node_id"] = VEHICLE_ID;
            jsonDoc["timestamp_ms"] = millis(); // Milliseconds since boot as a simple timestamp

            if (currentVehicleData.ble_connected) {
                JsonObject obdData = jsonDoc.createNestedObject("obd");
                obdData["rpm"] = currentVehicleData.rpm > 0 ? round(currentVehicleData.rpm) : 0;
                obdData["speed_kmh"] = currentVehicleData.speed_kmh > 0 ? round(currentVehicleData.speed_kmh) : 0;
                obdData["coolant_c"] = currentVehicleData.coolant_temp_c;
                // Add other OBD fields here if they are parsed and stored in currentVehicleData
            }

#if ENABLE_GPS
            if (currentVehicleData.gps_fix) {
                JsonObject gpsData = jsonDoc.createNestedObject("gps");
                gpsData["lat"] = currentVehicleData.latitude;
                gpsData["lon"] = currentVehicleData.longitude;
                gpsData["alt_m"] = currentVehicleData.altitude_m;
                gpsData["sats"] = currentVehicleData.satellites;
                gpsData["speed_kmh_gps"] = currentVehicleData.speed_gps_kmh; // Speed from GPS
            }
#endif
            JsonObject statusInfo = jsonDoc.createNestedObject("sys_status");
            statusInfo["ble_ok"] = currentVehicleData.ble_connected;
            statusInfo["gps_fix"] = currentVehicleData.gps_fix;
            statusInfo["rns_ready"] = isReticulumReadyToSend();
            statusInfo["free_heap"] = ESP.getFreeHeap();


            char jsonOutputBuffer[JSON_PAYLOAD_BUFFER_SIZE];
            size_t jsonLen = serializeJson(jsonDoc, jsonOutputBuffer, sizeof(jsonOutputBuffer));

            if (jsonLen > 0 && jsonLen < sizeof(jsonOutputBuffer)) {
                sendDataOverReticulum(jsonOutputBuffer);
                DEBUG_PRINTLN(F("[DATA_TX] Sent JSON to Reticulum:"));
                DEBUG_PRINTLN(jsonOutputBuffer);
            } else {
                DEBUG_PRINTLN(F("[JSON_ERROR] Failed to serialize JSON or buffer too small for payload."));
                DEBUG_PRINTF("[JSON_ERROR] Required size: %u, Buffer size: %d\n", jsonDoc.memoryUsage(), JSON_PAYLOAD_BUFFER_SIZE);
            }
        } else {
            DEBUG_PRINTLN(F("[DATA_TX_WARN] Reticulum not ready to send data."));
        }
    }

    // Yield for other tasks, especially important for ESP32 WiFi and BLE stability
    // A small delay can also help prevent watchdog timeouts if any library call is unexpectedly blocking.
    delay(1); // Use with caution. Prefer fully non-blocking code.
              // If issues arise, this can be increased slightly, but investigate root cause.
}

