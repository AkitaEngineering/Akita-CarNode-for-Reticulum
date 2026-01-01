#include "reticulum_handler.h"
#include "config.h"
#include "config_manager.h" // Runtime configuration
#include <Reticulum.h> // Main Reticulum header for ESP32
#include <Arduino.h>   // For ESP32 specifics, String, min, pow, random, DEBUG_PRINT macros

// Reticulum's ESP32 port provides interface classes for WiFi and LoRa
// These are typically included via Reticulum.h or may require specific interface includes
// Adjust include paths based on your Reticulum library version if needed
#if USE_WIFI_FOR_RETICULUM
    #include <WiFi.h>
    // WiFiInterface is typically provided by Reticulum.h
#elif USE_LORA_FOR_RETICULUM
    #include <SPI.h>
    #include <LoRa.h>
    // LoRaInterface is typically provided by Reticulum.h
#endif

Reticulum reticulum_instance; // The main Reticulum stack instance
Identity local_node_identity; // This node's cryptographic identity, cached once available
Identity* data_announcement_identity_ptr = nullptr; // Pointer to identity used for announcing data
Destination* specific_rns_destination_ptr = nullptr; // Pointer for sending to a specific RNS destination

// Interface Pointers - these store the Reticulum interface objects
#if USE_WIFI_FOR_RETICULUM
    WiFiInterface* wifi_rns_interface_ptr = nullptr;
#elif USE_LORA_FOR_RETICULUM
    LoRaInterface* lora_rns_interface_ptr = nullptr;
#endif

static bool reticulumPhysicalLayerIsUp = false; // e.g., WiFi connected, LoRa hardware initialized
static bool reticulumTransportIsActive = false; // Reticulum stack reports at least one interface is active and ready

// WiFi Reconnection Logic (only if WiFi is used)
#if USE_WIFI_FOR_RETICULUM
static unsigned long lastWifiConnectionAttemptTime = 0;
static unsigned long currentWifiRetryIntervalMs = WIFI_INITIAL_RETRY_DELAY_MS;
static uint8_t wifiConnectionRetryCount = 0;
static bool wifiConnectionInProgress = false;
static unsigned long wifiConnectionStartTime = 0;
#endif

// Reticulum status callback function
// This function is called by the Reticulum stack to report status changes and log messages.
void rns_status_and_log_callback(Reticulum::LOG_LEVEL level, Reticulum::CONTEXT context, const char* message_cst) {
    // Filter verbose messages unless full debug is enabled in config.h
    if (level > Reticulum::LOG_NOTICE && ENABLE_SERIAL_DEBUG < 2) {
         // return; // Uncomment to hide DEBUG/VERBOSE messages from Reticulum
    }
    // Use a String for manipulation if needed, but be mindful of memory on ESP32
    // String message_str = String(message_cst);
    DEBUG_PRINTF("[RNS_CB Lvl:%X Ctx:%X] %s\n", level, context, message_cst);

    if (context == Reticulum::CONTEXT_TRANSPORT) {
        if (strstr(message_cst, "Transport instance became ready") || 
            strstr(message_cst, "Interface became active") ||
            strstr(message_cst, "Transport interface started")) { // Common phrases indicating an interface is up
            DEBUG_PRINTLN(F("[RNS_CB] A transport interface is active. Reticulum operational."));
            reticulumTransportIsActive = true; 
            
            Identity* identity = reticulum_instance.getIdentity();
            if (identity != nullptr && identity->isValid()) {
                local_node_identity = *identity; // Cache local identity
                DEBUG_PRINTF("[RNS_CB] Node RNS Address (Identity Hash): %s\n", local_node_identity.getHexHash().c_str());
                
                // Announce this node's presence with its application name
                // Note: announceName API may vary by Reticulum version - adjust parameters if needed
                const char* appName = getReticulumAppName();
                DEBUG_PRINTF("[RNS_CB] Announcing node presence for app_name: %s\n", appName);
                reticulum_instance.announceName(appName, true); // Full announcement

                // If not using a specific destination, set up for announcing data from this node's identity
                if (specific_rns_destination_ptr == nullptr) {
                    data_announcement_identity_ptr = &local_node_identity;
                    DEBUG_PRINTF("[RNS_CB] Data will be ANNOUNCED from identity: %s\n", data_announcement_identity_ptr->getHexHash().c_str());
                }
            } else {
                DEBUG_PRINTLN(F("[RNS_CB_WARN] Transport UP, but local RNS identity not yet valid/available."));
            }
        } else if (strstr(message_cst, "All transport interfaces are down") || 
                   strstr(message_cst, "Transport instance is offline") ||
                   strstr(message_cst, "Transport interface stopped")) {
            DEBUG_PRINTLN(F("[RNS_CB] All Transport Interfaces are DOWN. Reticulum is offline."));
            reticulumTransportIsActive = false;
        }
    } else if (context == Reticulum::CONTEXT_IDENTITY) {
         if (strstr(message_cst, "Identity created") || strstr(message_cst, "Loaded identity from storage")) {
            Identity* identity = reticulum_instance.getIdentity();
            if (identity != nullptr && identity->isValid()) {
                local_node_identity = *identity; // Cache it
                DEBUG_PRINTF("[RNS_CB] Local RNS Identity available: %s\n", local_node_identity.getHexHash().c_str());
            }
         }
    }
    // Add more context checks if needed (e.g., CONTEXT_PACKETQUEUE, CONTEXT_LINK, etc.)
}


void initReticulum() {
    DEBUG_PRINTLN(F("[RNS_INIT] Initializing Reticulum stack..."));
    // Set log level based on debug preference from config.h
    reticulum_instance.setLogLevel(ENABLE_SERIAL_DEBUG >= 2 ? Reticulum::LOG_DEBUG : Reticulum::LOG_NOTICE);
    reticulum_instance.setStatusCallback(rns_status_and_log_callback);
    // Reticulum automatically generates/loads an identity.
    // For persistent identity, Reticulum needs a storage backend (e.g., SPIFFS/LittleFS),
    // which requires additional setup not covered in this basic implementation.
    // Without it, an ephemeral identity is generated on each boot.

#if USE_WIFI_FOR_RETICULUM
    DEBUG_PRINTLN(F("[RNS_INIT] WiFi mode selected. Connection will be managed in reticulumLoop()."));
        WiFi.mode(WIFI_STA);          // Set ESP32 to WiFi Station mode
        WiFi.setAutoReconnect(false); // Disable ESP32's built-in auto-reconnect; we manage it for Reticulum interface state
        lastWifiConnectionAttemptTime = millis() - currentWifiRetryIntervalMs - 1; // Ensure first connection attempt is soon
    }
#endif

#if USE_LORA_FOR_RETICULUM || defined(USE_LORA_FOR_RETICULUM)
    if (!useWiFi) {
        DEBUG_PRINTLN(F("[RNS_INIT] LoRa mode selected. Initializing LoRa hardware..."));
        SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_SS_PIN); // Initialize SPI for LoRa
        LoRa.setPins(LORA_SS_PIN, LORA_RST_PIN, LORA_DI0_PIN);             // Set LoRa chip pins

        RuntimeConfig* cfg = getRuntimeConfig();
        uint32_t loraBand = (cfg != nullptr) ? cfg->lora_band : LORA_BAND;
        
        if (!LoRa.begin(loraBand)) { // Initialize LoRa module at specified frequency
            DEBUG_PRINTLN(F("[RNS_INIT_ERROR] Starting LoRa failed! Check hardware, pins, and antenna."));
            // Don't halt - allow system to continue, LoRa will be retried if needed
            reticulumPhysicalLayerIsUp = false;
            return;
        }
        // Optional: Configure LoRa parameters like TxPower, SpreadingFactor, Bandwidth, SyncWord, CRC
        // LoRa.setTxPower(17); // Example: 17dBm, check regional limits and module capability
        // LoRa.setSpreadingFactor(8);
        // LoRa.setSignalBandwidth(125E3);
        // LoRa.enableCrc();
        DEBUG_PRINTLN(F("[RNS_INIT] LoRa hardware initialized successfully."));

        // Create and add LoRaInterface to Reticulum.
        // Note: LoRaInterface constructor may vary by Reticulum version
        // Common patterns: LoRaInterface(Reticulum* rns) or LoRaInterface(Reticulum* rns, LoRaClass* lora)
        if (lora_rns_interface_ptr == nullptr) {
            // Try the most common constructor pattern first
            lora_rns_interface_ptr = new LoRaInterface(&reticulum_instance);
            if (lora_rns_interface_ptr != nullptr) {
                reticulum_instance.addInterface(lora_rns_interface_ptr);
                DEBUG_PRINTLN(F("[RNS_INIT] LoRaInterface added to Reticulum."));
            } else {
                DEBUG_PRINTLN(F("[RNS_INIT_ERROR] Failed to create LoRaInterface object."));
            }
        }
        reticulumPhysicalLayerIsUp = true; // For LoRa, "physical layer up" means hardware initialized and interface added
                                       // Reticulum's status callback will confirm when transport is fully active.
    }
#endif

    // Setup specific RNS destination if configured
    const char* destAddr = getReticulumDestinationAddress();
    if (strlen(destAddr) > 0 && 
        strcmp(destAddr, "destination_hash_here_16_bytes_hex") != 0 &&
        strcmp(destAddr, "") != 0) {
        
        DEBUG_PRINTF("[RNS_INIT] Configured to send data to specific RNS destination: %s\n", destAddr);
        
        Identity* targetIdentityForDest = new Identity(false); // false = not creating a new local identity
        if (targetIdentityForDest != nullptr && targetIdentityForDest->from_hex(destAddr)) {
            DEBUG_PRINTF("[RNS_INIT] Target Identity for destination successfully created from hex: %s\n", targetIdentityForDest->getHexHash().c_str());
            
            // Create Destination with aspects that match receiver expectations
            // Note: Destination constructor parameters may vary by Reticulum version
            specific_rns_destination_ptr = new Destination(
                *targetIdentityForDest,   // The resolved Identity of the target
                Destination::SINGLE,      // Type of destination (SINGLE, GROUP, or LINK)
                getReticulumAppName(),    // Primary application name context
                "vehicle_data",           // Aspect 1: data category
                "stream"                  // Aspect 2: sub-type or endpoint name
            );

            if (specific_rns_destination_ptr != nullptr && specific_rns_destination_ptr->isValid()) {
                DEBUG_PRINTF("[RNS_INIT] RNS Destination object created. Target RNS Addr: %s\n",
                             specific_rns_destination_ptr->getAddress().c_str());
            } else {
                DEBUG_PRINTLN(F("[RNS_INIT_ERROR] Failed to create a valid RNS Destination object."));
                if (specific_rns_destination_ptr != nullptr) {
                    delete specific_rns_destination_ptr;
                }
                specific_rns_destination_ptr = nullptr;
            }
            // Clean up temporary identity (Destination typically copies the identity data)
            delete targetIdentityForDest;
            targetIdentityForDest = nullptr; 
        } else {
            DEBUG_PRINTF("[RNS_INIT_ERROR] Failed to create Identity from hex hash: %s. Data will be announced instead.\n", destAddr);
            if (targetIdentityForDest != nullptr) {
                delete targetIdentityForDest;
                targetIdentityForDest = nullptr;
            }
        }
    }
    
    if (specific_rns_destination_ptr == nullptr) { // Fallback if specific destination setup failed or not configured
        DEBUG_PRINTLN(F("[RNS_INIT] No specific RNS destination configured or setup failed. Data will be ANNOUNCED."));
        // data_announcement_identity_ptr will be set from local_node_identity once transport is active and identity is known.
    }
    DEBUG_PRINTLN(F("[RNS_INIT] Reticulum initialization sequence complete."));
}


void reticulumLoop() {
    bool useWiFi = getUseWiFiForReticulum();
#if USE_WIFI_FOR_RETICULUM || defined(USE_WIFI_FOR_RETICULUM)
    if (useWiFi && !WiFi.isConnected()) { // Check if WiFi is currently disconnected
        if (reticulumPhysicalLayerIsUp) { // WiFi link was previously up, but just dropped
            DEBUG_PRINTLN(F("[RNS_LOOP_WiFi] WiFi link lost."));
            reticulumPhysicalLayerIsUp = false;
            // Reticulum's status callback (rns_status_and_log_callback) should eventually indicate transport down
            // if this was the only active interface.
            
            // Note: Reticulum WiFiInterface typically auto-detects WiFi state changes
            // If your Reticulum version requires explicit notification, uncomment and adjust:
            // if (wifi_rns_interface_ptr != nullptr) {
            //    wifi_rns_interface_ptr->notifyLinkDown(); // API may vary by version
            // }

            // Reset WiFi connection retry logic for a fresh sequence
            currentWifiRetryIntervalMs = WIFI_INITIAL_RETRY_DELAY_MS;
            wifiConnectionRetryCount = 0;
            lastWifiConnectionAttemptTime = millis(); // Start timing for the first retry attempt from now
        }

        // Attempt to reconnect WiFi if it's time based on exponential backoff
        unsigned long currentMillis = millis();
        
        // Check if a connection attempt is in progress
        if (wifiConnectionInProgress) {
            // Check if connection succeeded
            if (WiFi.status() == WL_CONNECTED) {
                DEBUG_PRINTLN(F("[RNS_LOOP_WiFi] WiFi (Re)Connected successfully!"));
                DEBUG_PRINTF("[RNS_LOOP_WiFi] IP Address: %s, RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
                reticulumPhysicalLayerIsUp = true;
                wifiConnectionInProgress = false;
                currentWifiRetryIntervalMs = WIFI_INITIAL_RETRY_DELAY_MS; // Reset retry delay for next time
                wifiConnectionRetryCount = 0; // Reset retry count

                // Manage Reticulum WiFiInterface object
                // Most Reticulum versions auto-detect WiFi state changes, but some may need explicit handling
                if (wifi_rns_interface_ptr == nullptr) {
                    // First time connecting - create new interface
                    DEBUG_PRINTLN(F("[RNS_LOOP_WiFi] Creating and adding new WiFiInterface to Reticulum."));
                    wifi_rns_interface_ptr = new WiFiInterface(&reticulum_instance, &WiFi);
                    if (wifi_rns_interface_ptr != nullptr) {
                        reticulum_instance.addInterface(wifi_rns_interface_ptr);
                        DEBUG_PRINTLN(F("[RNS_LOOP_WiFi] WiFiInterface created and added successfully."));
                    } else {
                        DEBUG_PRINTLN(F("[RNS_LOOP_WiFi_ERROR] Failed to allocate WiFiInterface."));
                    }
                } else {
                    // Interface exists - most versions auto-detect WiFi reconnection
                    // If your Reticulum version requires explicit notification, uncomment:
                    // wifi_rns_interface_ptr->notifyLinkUp(); // API may vary by version
                    DEBUG_PRINTLN(F("[RNS_LOOP_WiFi] WiFiInterface exists. Reticulum should detect WiFi reconnection."));
                }
                // Reticulum's status callback (rns_status_and_log_callback) should set reticulumTransportIsActive = true
                // when the interface is fully up and operational within the RNS stack.
            } 
            // Check if connection attempt timed out
            else if (currentMillis - wifiConnectionStartTime > WIFI_CONNECT_TIMEOUT_MS) {
                DEBUG_PRINTLN(F("[RNS_LOOP_WiFi_ERROR] WiFi connection attempt timed out."));
                WiFi.disconnect(true); // Disconnect fully to clean up state
                wifiConnectionInProgress = false;

                wifiConnectionRetryCount++;
                unsigned long delayCalculation = WIFI_INITIAL_RETRY_DELAY_MS * pow(2, wifiConnectionRetryCount > 0 ? wifiConnectionRetryCount - 1 : 0);
                currentWifiRetryIntervalMs = min(delayCalculation, (unsigned long)WIFI_MAX_RETRY_DELAY_MS);
                currentWifiRetryIntervalMs += random(0, WIFI_RETRY_JITTER_MS + 1); // Add jitter
                DEBUG_PRINTF("[RNS_LOOP_WiFi] Next WiFi connection attempt in approx %lums.\n", currentWifiRetryIntervalMs);
                lastWifiConnectionAttemptTime = currentMillis; // Update for next retry timing
            }
            // Connection still in progress, continue waiting (non-blocking)
        }
        // Start a new connection attempt if it's time
        else if (currentMillis - lastWifiConnectionAttemptTime >= currentWifiRetryIntervalMs) {
            lastWifiConnectionAttemptTime = currentMillis; // Timestamp this connection attempt
            wifiConnectionStartTime = currentMillis;
            wifiConnectionInProgress = true;
            const char* ssid = getWiFiSSID();
            const char* pass = getWiFiPassword();
            DEBUG_PRINTF("[RNS_LOOP_WiFi] Attempting WiFi connection (Retry #%d, Current Delay %lums) to SSID: %s\n",
                         wifiConnectionRetryCount, currentWifiRetryIntervalMs, ssid);
            
            WiFi.begin(ssid, pass); // Initiate WiFi connection (non-blocking start)
        }
    }
#endif // END USE_WIFI_FOR_RETICULUM

    reticulum_instance.loop(); // Essential: This processes Reticulum's internal tasks, packet queues, timers, etc.
}


void sendDataOverReticulum(const char* jsonDataPayload) {
    if (!isReticulumReadyToSend()) { // Checks both physical layer (WiFi/LoRa) and RNS transport active status
        DEBUG_PRINTLN(F("[RNS_SEND_ERROR] Cannot send: Reticulum not ready (physical layer down or RNS transport not active)."));
        return;
    }
    size_t payloadLen = strlen(jsonDataPayload);
    if (payloadLen == 0) {
        DEBUG_PRINTLN(F("[RNS_SEND_ERROR] Cannot send: Empty JSON payload."));
        return;
    }
    if (payloadLen > RNS_MAX_PAYLOAD_SIZE_AFTER_HEADER) { // Check against Reticulum's actual max payload for a single packet
         DEBUG_PRINTF("[RNS_SEND_ERROR] Payload too large (%u bytes). Max is approx %d. Consider LXMF or fragmentation.\n", payloadLen, RNS_MAX_PAYLOAD_SIZE_AFTER_HEADER);
         return;
    }


    // Option 1: Send to a specific, pre-resolved RNS Destination
    if (specific_rns_destination_ptr != nullptr && specific_rns_destination_ptr->isValid()) {
        DEBUG_PRINTF("[RNS_SEND] Sending Packet (%u bytes) to specific RNS destination: %s\n",
                     payloadLen, specific_rns_destination_ptr->getAddress().c_str());
        
        // Create Packet for unicast transmission
        // Note: Packet constructor parameters may vary by Reticulum version
        // Common pattern: Packet(Destination*, const uint8_t* data, size_t length)
        Packet data_packet(
            specific_rns_destination_ptr,       // Pointer to the Destination object
            (const uint8_t*)jsonDataPayload,    // Data buffer (cast to uint8_t*)
            payloadLen                          // Length of data
        );
        
        // Send the packet and check delivery status
        Packet::LXMFDeliveryStatus deliveryStatus = data_packet.send();
        
        // Check delivery status
        if (deliveryStatus == Packet::SENT || deliveryStatus == Packet::QUEUED) {
            DEBUG_PRINTF("[RNS_SEND] Packet sent/queued to destination. Status: %d\n", deliveryStatus);
        } else {
            // Other statuses could be FAILED, TIMEOUT, NO_LINK, NO_ROUTE etc.
            DEBUG_PRINTF("[RNS_SEND_ERROR] Failed to send/queue packet to destination. Status: %d\n", deliveryStatus);
        }
    }
    // Option 2: Announce the data (broadcast-like, for general consumption by interested nodes)
    else if (data_announcement_identity_ptr != nullptr && data_announcement_identity_ptr->isValid()) {
        const char* appName = getReticulumAppName();
        DEBUG_PRINTF("[RNS_SEND] Announcing data (%u bytes) from Identity %s, AppName: %s, Aspects: vehicle_data, live_update\n",
                     payloadLen, data_announcement_identity_ptr->getHexHash().c_str(), appName);
        
        // Announce data for general consumption by interested nodes
        // Note: announceData API may vary by Reticulum version - adjust parameters if needed
        // Common pattern: announceData(Identity&, const uint8_t* data, size_t len, const char* app_name, ...aspects)
        reticulum_instance.announceData(
            *data_announcement_identity_ptr,    // The Identity making this announcement
            (const uint8_t*)jsonDataPayload,    // Data buffer
            payloadLen,                         // Length of data
            appName,                            // Primary application name
            "vehicle_data",                     // Aspect 1: data category
            "live_update"                       // Aspect 2: sub-type
        );
        // Announce is fire-and-forget (no delivery confirmation)
        DEBUG_PRINTLN(F("[RNS_SEND] Data announced on the network."));
    } else { // Fallback if neither specific destination nor announcement identity is ready
        DEBUG_PRINTLN(F("[RNS_SEND_ERROR] No specific RNS destination and no valid local identity for announcement. Cannot send data."));
        // This might happen if transport is active but local_node_identity hasn't been fully established/cached yet.
        // Try to re-cache local identity if it seems available now.
        Identity* identity = reticulum_instance.getIdentity();
        if (!local_node_identity.isValid() && identity != nullptr && identity->isValid()){
            local_node_identity = *identity;
            if (specific_rns_destination_ptr == nullptr) { // If still no specific dest, set up for announce
                 data_announcement_identity_ptr = &local_node_identity;
            }
            DEBUG_PRINTLN(F("[RNS_SEND_INFO] Local RNS identity just became valid. Data send will be attempted next cycle."));
        }
    }
}

// Returns true if the physical network layer (WiFi connected or LoRa initialized and RNS interface added) is considered up.
bool isReticulumConnected() {
    return reticulumPhysicalLayerIsUp;
}

// Returns true if Reticulum's transport layer is active and ready to send/receive data.
// This implies the physical layer is up AND the Reticulum stack has successfully started and recognized an interface.
bool isReticulumReadyToSend() {
    return reticulumPhysicalLayerIsUp && reticulumTransportIsActive;
}
