#include "ble_handler.h"
#include "config.h"
#include "obd_parser.h"
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLEUtils.h>
#include <Arduino.h>

// BLE Client and Characteristic pointers
static BLEClient* pClient = nullptr;
static BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
static BLERemoteService* pRemoteService = nullptr;

// Connection state
static bool bleConnected = false;
static bool bleConnecting = false;
static unsigned long lastBLEConnectionAttemptTime = 0;
static unsigned long currentBLERetryIntervalMs = BLE_INITIAL_RETRY_DELAY_MS;
static uint8_t bleConnectionRetryCount = 0;

// BLE Device information
static BLEAddress* serverAddress = nullptr;
static String targetServiceUUID = "";
static String targetCharacteristicUUID = "";

// OBD Response buffer
static String obdResponseBuffer = "";
static unsigned long lastOBDResponseTime = 0;
static const unsigned long OBD_RESPONSE_TIMEOUT_MS = 2000;

// BLE Scan callback class
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        // Check if this device matches our target
        bool matches = false;
        
#ifdef USE_OBDII_UUIDS
        // If using UUIDs, check if device has the service UUID
        if (advertisedDevice.haveServiceUUID()) {
            BLEUUID serviceUUID = advertisedDevice.getServiceUUID();
            if (serviceUUID.toString() == String(OBDII_SERVICE_UUID)) {
                matches = true;
                targetServiceUUID = String(OBDII_SERVICE_UUID);
                targetCharacteristicUUID = String(OBDII_CHARACTERISTIC_UUID);
            }
        }
#elif defined(USE_OBDII_NORDIC_UART_SERVICE)
        // Check for Nordic UART Service
        if (advertisedDevice.haveServiceUUID()) {
            BLEUUID serviceUUID = advertisedDevice.getServiceUUID();
            if (serviceUUID.toString() == String(NUS_SERVICE_UUID_STR)) {
                matches = true;
                targetServiceUUID = String(NUS_SERVICE_UUID_STR);
                targetCharacteristicUUID = String(NUS_CHARACTERISTIC_TX_UUID_STR);
            }
        }
#else
        // Connect by device name
        if (advertisedDevice.getName() == String(OBDII_DEVICE_NAME)) {
            matches = true;
        }
#endif

        if (matches && serverAddress == nullptr) {
            DEBUG_PRINTF("[BLE_SCAN] Found target device: %s\n", advertisedDevice.toString().c_str());
            serverAddress = new BLEAddress(advertisedDevice.getAddress());
            BLEDevice::getScan()->stop(); // Stop scanning once we found our device
        }
    }
};

// BLE Client callback class
class MyClientCallbacks : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
        DEBUG_PRINTLN(F("[BLE_CLIENT] Connected to OBD-II adapter."));
        bleConnected = true;
        bleConnecting = false;
        bleConnectionRetryCount = 0;
        currentBLERetryIntervalMs = BLE_INITIAL_RETRY_DELAY_MS;
    }

    void onDisconnect(BLEClient* pclient) {
        DEBUG_PRINTLN(F("[BLE_CLIENT] Disconnected from OBD-II adapter."));
        bleConnected = false;
        bleConnecting = false;
        pRemoteCharacteristic = nullptr;
        pRemoteService = nullptr;
        
        // Clean up server address to allow re-scanning
        if (serverAddress != nullptr) {
            delete serverAddress;
            serverAddress = nullptr;
        }
    }
};

// Notification callback for receiving OBD responses
static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic,
                           uint8_t* pData, size_t length, bool isNotify) {
    // Append received data to buffer
    for (size_t i = 0; i < length; i++) {
        char c = (char)pData[i];
        if (c == '\r' || c == '\n') {
            // End of line, process if buffer has content
            if (obdResponseBuffer.length() > 0) {
                lastOBDResponseTime = millis();
                DEBUG_PRINTF("[BLE_RX] OBD Response: %s\n", obdResponseBuffer.c_str());
            }
        } else if (c >= 32 && c <= 126) { // Printable ASCII characters
            obdResponseBuffer += c;
        }
    }
}

void initBLE() {
    DEBUG_PRINTLN(F("[BLE_INIT] Initializing BLE system..."));
    BLEDevice::init("");
    DEBUG_PRINTLN(F("[BLE_INIT] BLE initialized. Ready to scan for OBD-II adapter."));
}

bool connectBLE() {
    unsigned long currentMillis = millis();

    // If already connected, return true
    if (bleConnected && pClient != nullptr && pClient->isConnected()) {
        return true;
    }

    // If currently connecting, wait
    if (bleConnecting) {
        return true;
    }

    // Check if it's time to attempt connection (exponential backoff)
    if (currentMillis - lastBLEConnectionAttemptTime < currentBLERetryIntervalMs) {
        return false; // Not time yet
    }

    lastBLEConnectionAttemptTime = currentMillis;
    bleConnecting = true;

    // If we don't have a server address, scan for the device
    if (serverAddress == nullptr) {
        DEBUG_PRINTLN(F("[BLE_SCAN] Starting BLE scan for OBD-II adapter..."));
        BLEScan* pBLEScan = BLEDevice::getScan();
        pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
        pBLEScan->setActiveScan(true); // Active scan uses more power but gets results faster
        pBLEScan->setInterval(1349);
        pBLEScan->setWindow(449);
        
        BLEScanResults foundDevices = pBLEScan->start(BLE_SCAN_TIME_SECONDS, false);
        DEBUG_PRINTF("[BLE_SCAN] Scan complete. Found %d devices.\n", foundDevices.getCount());
        
        if (serverAddress == nullptr) {
            DEBUG_PRINTLN(F("[BLE_SCAN] Target OBD-II adapter not found in scan."));
            bleConnecting = false;
            
            // Exponential backoff for retry
            bleConnectionRetryCount++;
            unsigned long delayCalculation = BLE_INITIAL_RETRY_DELAY_MS * pow(2, bleConnectionRetryCount > 0 ? bleConnectionRetryCount - 1 : 0);
            currentBLERetryIntervalMs = min(delayCalculation, (unsigned long)BLE_MAX_RETRY_DELAY_MS);
            currentBLERetryIntervalMs += random(0, BLE_RETRY_JITTER_MS + 1);
            DEBUG_PRINTF("[BLE_SCAN] Next scan attempt in approx %lums.\n", currentBLERetryIntervalMs);
            return false;
        }
    }

    // Create BLE client if it doesn't exist
    if (pClient == nullptr) {
        pClient = BLEDevice::createClient();
        pClient->setClientCallbacks(new MyClientCallbacks());
    }

    // Connect to the server
    DEBUG_PRINTF("[BLE_CONNECT] Connecting to OBD-II adapter at address: %s\n", serverAddress->toString().c_str());
    
    if (!pClient->connect(*serverAddress)) {
        DEBUG_PRINTLN(F("[BLE_CONNECT_ERROR] Failed to connect to OBD-II adapter."));
        bleConnecting = false;
        delete serverAddress;
        serverAddress = nullptr;
        
        // Exponential backoff
        bleConnectionRetryCount++;
        unsigned long delayCalculation = BLE_INITIAL_RETRY_DELAY_MS * pow(2, bleConnectionRetryCount > 0 ? bleConnectionRetryCount - 1 : 0);
        currentBLERetryIntervalMs = min(delayCalculation, (unsigned long)BLE_MAX_RETRY_DELAY_MS);
        currentBLERetryIntervalMs += random(0, BLE_RETRY_JITTER_MS + 1);
        return false;
    }

    DEBUG_PRINTLN(F("[BLE_CONNECT] Connected to server. Discovering services..."));

    // Discover services and characteristics
    if (targetServiceUUID.length() == 0) {
        // If we don't have specific UUIDs, try to find common OBD-II service UUIDs
        // Get the services
        std::map<std::string, BLERemoteService*>* services = pClient->getServices();
        if (services != nullptr && services->size() > 0) {
            // Try to find a service that looks like a data service
            for (auto const& service : *services) {
                String serviceUUID = String(service.first.c_str());
                DEBUG_PRINTF("[BLE_SERVICE] Found service: %s\n", serviceUUID.c_str());
                
                // Check if this service has characteristics
                pRemoteService = pClient->getService(service.first.c_str());
                if (pRemoteService != nullptr) {
                    std::map<std::string, BLERemoteCharacteristic*>* characteristics = pRemoteService->getCharacteristics();
                    if (characteristics != nullptr && characteristics->size() > 0) {
                        // Use the first characteristic that supports write/notify
                        for (auto const& characteristic : *characteristics) {
                            BLERemoteCharacteristic* pChar = characteristic.second;
                            if (pChar != nullptr && (pChar->canWrite() || pChar->canNotify() || pChar->canIndicate())) {
                                pRemoteCharacteristic = pChar;
                                targetServiceUUID = serviceUUID;
                                targetCharacteristicUUID = String(characteristic.first.c_str());
                                DEBUG_PRINTF("[BLE_CHAR] Using characteristic: %s\n", targetCharacteristicUUID.c_str());
                                break;
                            }
                        }
                        if (pRemoteCharacteristic != nullptr) break;
                    }
                }
            }
        }
    } else {
        // Use the specific service UUID we found
        pRemoteService = pClient->getService(targetServiceUUID.c_str());
        if (pRemoteService == nullptr) {
            DEBUG_PRINTF("[BLE_SERVICE_ERROR] Service not found: %s\n", targetServiceUUID.c_str());
            pClient->disconnect();
            bleConnecting = false;
            delete serverAddress;
            serverAddress = nullptr;
            return false;
        }
        
        pRemoteCharacteristic = pRemoteService->getCharacteristic(targetCharacteristicUUID.c_str());
        if (pRemoteCharacteristic == nullptr) {
            DEBUG_PRINTF("[BLE_CHAR_ERROR] Characteristic not found: %s\n", targetCharacteristicUUID.c_str());
            pClient->disconnect();
            bleConnecting = false;
            delete serverAddress;
            serverAddress = nullptr;
            return false;
        }
    }

    if (pRemoteCharacteristic == nullptr) {
        DEBUG_PRINTLN(F("[BLE_CHAR_ERROR] No suitable characteristic found."));
        pClient->disconnect();
        bleConnecting = false;
        delete serverAddress;
        serverAddress = nullptr;
        return false;
    }

    // Register for notifications if supported
    if (pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(notifyCallback);
        DEBUG_PRINTLN(F("[BLE_CHAR] Registered for notifications."));
    } else if (pRemoteCharacteristic->canIndicate()) {
        pRemoteCharacteristic->registerForNotify(notifyCallback);
        DEBUG_PRINTLN(F("[BLE_CHAR] Registered for indications."));
    }

    // Initialize OBD-II adapter with AT commands (non-blocking, send once)
    DEBUG_PRINTLN(F("[BLE_OBD] Initializing OBD-II adapter..."));
    String initCommands[] = {"ATZ\r", "ATE0\r", "ATL0\r", "ATH0\r", "ATSP0\r"};
    for (int i = 0; i < 5; i++) {
        if (pRemoteCharacteristic->canWrite()) {
            pRemoteCharacteristic->writeValue(initCommands[i].c_str(), initCommands[i].length());
            delay(100); // Small delay between commands
        }
    }

    bleConnecting = false;
    bleConnected = true;
    DEBUG_PRINTLN(F("[BLE_CONNECT] Successfully connected and initialized OBD-II adapter."));
    return true;
}

bool isBLEConnected() {
    if (pClient == nullptr) {
        return false;
    }
    // Check if client is still connected (BLE can disconnect without callback in some cases)
    if (!pClient->isConnected()) {
        bleConnected = false;
        return false;
    }
    return bleConnected;
}

void disconnectBLE() {
    if (pClient != nullptr && pClient->isConnected()) {
        pClient->disconnect();
    }
    bleConnected = false;
    bleConnecting = false;
    pRemoteCharacteristic = nullptr;
    pRemoteService = nullptr;
    
    if (serverAddress != nullptr) {
        delete serverAddress;
        serverAddress = nullptr;
    }
}

void requestOBDPID(const OBD_PID& pid) {
    if (!isBLEConnected() || pRemoteCharacteristic == nullptr || pid.code == nullptr) {
        return;
    }

    // Format OBD-II request: "01" + PID code + "\r"
    String request = "01" + String(pid.code) + "\r";
    
    if (pRemoteCharacteristic->canWrite()) {
        pRemoteCharacteristic->writeValue(request.c_str(), request.length());
        obdResponseBuffer = ""; // Clear previous response
        lastOBDResponseTime = millis();
        DEBUG_PRINTF("[BLE_TX] Sent OBD request: %s (for %s)\n", request.c_str(), pid.name ? pid.name : "Unknown");
    } else {
        DEBUG_PRINTLN(F("[BLE_TX_ERROR] Characteristic does not support write."));
    }
}

void processOBDResponse(float* rpm, float* speed_kmh, float* coolant_temp_c) {
    if (!isBLEConnected()) {
        return;
    }

    // Check for timeout on incomplete responses
    if (obdResponseBuffer.length() > 0 && 
        (millis() - lastOBDResponseTime > OBD_RESPONSE_TIMEOUT_MS)) {
        DEBUG_PRINTLN(F("[OBD_RESPONSE] Response timeout, clearing buffer."));
        obdResponseBuffer = "";
    }

    // Process complete responses (those ending with newline or that are ready)
    // Look for complete responses in the buffer
    int newlinePos = obdResponseBuffer.indexOf('\r');
    if (newlinePos == -1) {
        newlinePos = obdResponseBuffer.indexOf('\n');
    }
    
    if (newlinePos >= 0) {
        String completeResponse = obdResponseBuffer.substring(0, newlinePos);
        obdResponseBuffer = obdResponseBuffer.substring(newlinePos + 1);
        
        // Clean the response: remove spaces, convert to uppercase
        completeResponse.trim();
        completeResponse.toUpperCase();
        completeResponse.replace(" ", "");
        completeResponse.replace(">", ""); // Remove prompt character if present
        
        // Try to match against known PID response prefixes
        for (int i = 0; i < NUM_OBD_PIDS; i++) {
            const OBD_PID& pid = OBD_PIDS_TO_QUERY[i];
            if (pid.responsePrefix != nullptr && completeResponse.startsWith(pid.responsePrefix)) {
                // Found a matching PID response, parse it
                DEBUG_PRINTF("[OBD_PARSE] Matched PID %s (%s): %s\n", 
                            pid.code ? pid.code : "Unknown", 
                            pid.name ? pid.name : "Unknown", 
                            completeResponse.c_str());
                parseOBDResponse(completeResponse.c_str(), pid, rpm, speed_kmh, coolant_temp_c);
                return;
            }
        }
        
        // If no match, it might be an AT command response or error
        if (completeResponse.length() > 0) {
            DEBUG_PRINTF("[OBD_RESPONSE] Unmatched response: %s\n", completeResponse.c_str());
        }
    }
    
    // Also check if we have a complete response without newline (some adapters don't send newlines)
    if (obdResponseBuffer.length() >= 8 && 
        (millis() - lastOBDResponseTime > 500)) { // Wait 500ms for more data
        String completeResponse = obdResponseBuffer;
        obdResponseBuffer = "";
        completeResponse.trim();
        completeResponse.toUpperCase();
        completeResponse.replace(" ", "");
        completeResponse.replace(">", "");
        
        for (int i = 0; i < NUM_OBD_PIDS; i++) {
            const OBD_PID& pid = OBD_PIDS_TO_QUERY[i];
            if (pid.responsePrefix != nullptr && completeResponse.startsWith(pid.responsePrefix)) {
                DEBUG_PRINTF("[OBD_PARSE] Matched PID %s (%s): %s\n", 
                            pid.code ? pid.code : "Unknown", 
                            pid.name ? pid.name : "Unknown", 
                            completeResponse.c_str());
                parseOBDResponse(completeResponse.c_str(), pid, rpm, speed_kmh, coolant_temp_c);
                return;
            }
        }
    }
}
