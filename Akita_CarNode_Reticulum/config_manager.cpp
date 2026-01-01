#include "config_manager.h"
#include "config.h"
#include <Preferences.h>
#include <Arduino.h>

static RuntimeConfig runtimeConfig;
static Preferences preferences;
static bool configLoaded = false;
static const char* PREF_NAMESPACE = "carnode_cfg";
static const uint8_t CURRENT_CONFIG_VERSION = 1;

// Initialize configuration system
void initConfigManager() {
    preferences.begin(PREF_NAMESPACE, false); // false = read-write mode
    
    if (!loadConfig()) {
        DEBUG_PRINTLN(F("[CONFIG] No saved configuration found, using defaults from config.h"));
        resetConfigToDefaults();
        saveConfig(); // Save defaults for first time
    } else {
        DEBUG_PRINTLN(F("[CONFIG] Configuration loaded from persistent storage"));
    }
    
    applyRuntimeConfig();
    configLoaded = true;
}

// Load configuration from persistent storage
bool loadConfig() {
    if (!preferences.isKey("config_ver")) {
        return false; // No saved config
    }
    
    uint8_t savedVersion = preferences.getUChar("config_ver", 0);
    if (savedVersion != CURRENT_CONFIG_VERSION) {
        DEBUG_PRINTF("[CONFIG] Config version mismatch (saved: %d, current: %d), using defaults\n", 
                    savedVersion, CURRENT_CONFIG_VERSION);
        return false;
    }
    
    // Load all configuration values
    preferences.getString("vehicle_id", runtimeConfig.vehicle_id, VEHICLE_ID, sizeof(runtimeConfig.vehicle_id));
    
    preferences.getString("obdii_name", runtimeConfig.obdii_device_name, OBDII_DEVICE_NAME, sizeof(runtimeConfig.obdii_device_name));
    preferences.getString("obdii_svc", runtimeConfig.obdii_service_uuid, "", sizeof(runtimeConfig.obdii_service_uuid));
    preferences.getString("obdii_char", runtimeConfig.obdii_characteristic_uuid, "", sizeof(runtimeConfig.obdii_characteristic_uuid));
    runtimeConfig.use_obdii_uuids = preferences.getBool("use_uuids", false);
    runtimeConfig.use_obdii_nordic_uart = preferences.getBool("use_nus", false);
    
    preferences.getString("wifi_ssid", runtimeConfig.wifi_ssid, WIFI_SSID, sizeof(runtimeConfig.wifi_ssid));
    preferences.getString("wifi_pass", runtimeConfig.wifi_password, WIFI_PASSWORD, sizeof(runtimeConfig.wifi_password));
    
    preferences.getString("rns_app", runtimeConfig.reticulum_app_name, RETICULUM_APP_NAME, sizeof(runtimeConfig.reticulum_app_name));
    preferences.getString("rns_dest", runtimeConfig.reticulum_destination_address, RETICULUM_DESTINATION_ADDRESS, sizeof(runtimeConfig.reticulum_destination_address));
    runtimeConfig.use_wifi_for_reticulum = preferences.getBool("use_wifi", USE_WIFI_FOR_RETICULUM);
    
    runtimeConfig.lora_band = preferences.getULong("lora_band", LORA_BAND);
    
    runtimeConfig.enable_status_led = preferences.getBool("enable_led", ENABLE_STATUS_LED);
    runtimeConfig.status_led_pin = preferences.getUChar("led_pin", STATUS_LED_PIN);
    
    runtimeConfig.enable_gps = preferences.getBool("enable_gps", ENABLE_GPS);
    runtimeConfig.gps_rx_pin = preferences.getUChar("gps_rx", GPS_RX_PIN);
    runtimeConfig.gps_tx_pin = preferences.getUChar("gps_tx", GPS_TX_PIN);
    runtimeConfig.gps_baud_rate = preferences.getULong("gps_baud", GPS_BAUD_RATE);
    
    runtimeConfig.reticulum_send_interval_ms = preferences.getULong("rns_int", RETICULUM_SEND_INTERVAL_MS);
    runtimeConfig.obd_query_interval_ms = preferences.getULong("obd_int", OBD_QUERY_INTERVAL_MS);
    runtimeConfig.gps_read_interval_ms = preferences.getULong("gps_int", GPS_READ_INTERVAL_MS);
    
    runtimeConfig.config_version = CURRENT_CONFIG_VERSION;
    
    return true;
}

// Save configuration to persistent storage
bool saveConfig() {
    preferences.putUChar("config_ver", CURRENT_CONFIG_VERSION);
    
    preferences.putString("vehicle_id", runtimeConfig.vehicle_id);
    
    preferences.putString("obdii_name", runtimeConfig.obdii_device_name);
    preferences.putString("obdii_svc", runtimeConfig.obdii_service_uuid);
    preferences.putString("obdii_char", runtimeConfig.obdii_characteristic_uuid);
    preferences.putBool("use_uuids", runtimeConfig.use_obdii_uuids);
    preferences.putBool("use_nus", runtimeConfig.use_obdii_nordic_uart);
    
    preferences.putString("wifi_ssid", runtimeConfig.wifi_ssid);
    preferences.putString("wifi_pass", runtimeConfig.wifi_password);
    
    preferences.putString("rns_app", runtimeConfig.reticulum_app_name);
    preferences.putString("rns_dest", runtimeConfig.reticulum_destination_address);
    preferences.putBool("use_wifi", runtimeConfig.use_wifi_for_reticulum);
    
    preferences.putULong("lora_band", runtimeConfig.lora_band);
    
    preferences.putBool("enable_led", runtimeConfig.enable_status_led);
    preferences.putUChar("led_pin", runtimeConfig.status_led_pin);
    
    preferences.putBool("enable_gps", runtimeConfig.enable_gps);
    preferences.putUChar("gps_rx", runtimeConfig.gps_rx_pin);
    preferences.putUChar("gps_tx", runtimeConfig.gps_tx_pin);
    preferences.putULong("gps_baud", runtimeConfig.gps_baud_rate);
    
    preferences.putULong("rns_int", runtimeConfig.reticulum_send_interval_ms);
    preferences.putULong("obd_int", runtimeConfig.obd_query_interval_ms);
    preferences.putULong("gps_int", runtimeConfig.gps_read_interval_ms);
    
    DEBUG_PRINTLN(F("[CONFIG] Configuration saved to persistent storage"));
    return true;
}

// Reset to defaults from config.h
void resetConfigToDefaults() {
    strncpy(runtimeConfig.vehicle_id, VEHICLE_ID, sizeof(runtimeConfig.vehicle_id) - 1);
    runtimeConfig.vehicle_id[sizeof(runtimeConfig.vehicle_id) - 1] = '\0';
    
    strncpy(runtimeConfig.obdii_device_name, OBDII_DEVICE_NAME, sizeof(runtimeConfig.obdii_device_name) - 1);
    runtimeConfig.obdii_device_name[sizeof(runtimeConfig.obdii_device_name) - 1] = '\0';
    
    runtimeConfig.obdii_service_uuid[0] = '\0';
    runtimeConfig.obdii_characteristic_uuid[0] = '\0';
    runtimeConfig.use_obdii_uuids = false;
    runtimeConfig.use_obdii_nordic_uart = false;
    
    strncpy(runtimeConfig.wifi_ssid, WIFI_SSID, sizeof(runtimeConfig.wifi_ssid) - 1);
    runtimeConfig.wifi_ssid[sizeof(runtimeConfig.wifi_ssid) - 1] = '\0';
    
    strncpy(runtimeConfig.wifi_password, WIFI_PASSWORD, sizeof(runtimeConfig.wifi_password) - 1);
    runtimeConfig.wifi_password[sizeof(runtimeConfig.wifi_password) - 1] = '\0';
    
    strncpy(runtimeConfig.reticulum_app_name, RETICULUM_APP_NAME, sizeof(runtimeConfig.reticulum_app_name) - 1);
    runtimeConfig.reticulum_app_name[sizeof(runtimeConfig.reticulum_app_name) - 1] = '\0';
    
    strncpy(runtimeConfig.reticulum_destination_address, RETICULUM_DESTINATION_ADDRESS, sizeof(runtimeConfig.reticulum_destination_address) - 1);
    runtimeConfig.reticulum_destination_address[sizeof(runtimeConfig.reticulum_destination_address) - 1] = '\0';
    
    runtimeConfig.use_wifi_for_reticulum = USE_WIFI_FOR_RETICULUM;
    runtimeConfig.lora_band = LORA_BAND;
    runtimeConfig.enable_status_led = ENABLE_STATUS_LED;
    runtimeConfig.status_led_pin = STATUS_LED_PIN;
    runtimeConfig.enable_gps = ENABLE_GPS;
    runtimeConfig.gps_rx_pin = GPS_RX_PIN;
    runtimeConfig.gps_tx_pin = GPS_TX_PIN;
    runtimeConfig.gps_baud_rate = GPS_BAUD_RATE;
    runtimeConfig.reticulum_send_interval_ms = RETICULUM_SEND_INTERVAL_MS;
    runtimeConfig.obd_query_interval_ms = OBD_QUERY_INTERVAL_MS;
    runtimeConfig.gps_read_interval_ms = GPS_READ_INTERVAL_MS;
    runtimeConfig.config_version = CURRENT_CONFIG_VERSION;
}

// Get runtime configuration
RuntimeConfig* getRuntimeConfig() {
    return &runtimeConfig;
}

// Apply runtime configuration (updates active settings)
// Note: Some settings require restart to take full effect
void applyRuntimeConfig() {
    // Configuration is applied through getter functions
    // This function can be extended to apply settings that don't require restart
    DEBUG_PRINTLN(F("[CONFIG] Runtime configuration applied"));
}

// Show configuration menu
void showConfigMenu() {
    DEBUG_PRINTLN(F("\n=== Akita CarNode Configuration Menu ==="));
    DEBUG_PRINTLN(F("Commands:"));
    DEBUG_PRINTLN(F("  show          - Show current configuration"));
    DEBUG_PRINTLN(F("  vehicle <id>  - Set vehicle ID"));
    DEBUG_PRINTLN(F("  wifi <ssid> <pass> - Set WiFi credentials"));
    DEBUG_PRINTLN(F("  obdname <name> - Set OBD-II adapter name"));
    DEBUG_PRINTLN(F("  obduuid <svc> <char> - Set OBD-II UUIDs"));
    DEBUG_PRINTLN(F("  rnsapp <name> - Set Reticulum app name"));
    DEBUG_PRINTLN(F("  rnsdest <addr> - Set Reticulum destination (or empty to announce)"));
    DEBUG_PRINTLN(F("  use_wifi <0|1> - Use WiFi (1) or LoRa (0)"));
    DEBUG_PRINTLN(F("  enable_gps <0|1> - Enable/disable GPS"));
    DEBUG_PRINTLN(F("  enable_led <0|1> - Enable/disable status LED"));
    DEBUG_PRINTLN(F("  led_pin <pin> - Set status LED pin"));
    DEBUG_PRINTLN(F("  save          - Save configuration"));
    DEBUG_PRINTLN(F("  reset         - Reset to defaults"));
    DEBUG_PRINTLN(F("  help          - Show this menu"));
    DEBUG_PRINTLN(F("========================================\n"));
}

// Process configuration command
void processConfigCommand(const String& command) {
    command.trim();
    if (command.length() == 0) return;
    
    int spaceIndex = command.indexOf(' ');
    String cmd = spaceIndex > 0 ? command.substring(0, spaceIndex) : command;
    cmd.toLowerCase();
    
    if (cmd == "show" || cmd == "list") {
        DEBUG_PRINTLN(F("\n--- Current Configuration ---"));
        DEBUG_PRINTF("Vehicle ID: %s\n", runtimeConfig.vehicle_id);
        DEBUG_PRINTF("WiFi SSID: %s\n", runtimeConfig.wifi_ssid);
        DEBUG_PRINTF("WiFi Password: %s\n", strlen(runtimeConfig.wifi_password) > 0 ? "***" : "(not set)");
        DEBUG_PRINTF("OBD-II Device Name: %s\n", runtimeConfig.obdii_device_name);
        DEBUG_PRINTF("OBD-II Use UUIDs: %s\n", runtimeConfig.use_obdii_uuids ? "Yes" : "No");
        if (runtimeConfig.use_obdii_uuids) {
            DEBUG_PRINTF("OBD-II Service UUID: %s\n", runtimeConfig.obdii_service_uuid);
            DEBUG_PRINTF("OBD-II Characteristic UUID: %s\n", runtimeConfig.obdii_characteristic_uuid);
        }
        DEBUG_PRINTF("Reticulum App Name: %s\n", runtimeConfig.reticulum_app_name);
        DEBUG_PRINTF("Reticulum Destination: %s\n", 
                    strlen(runtimeConfig.reticulum_destination_address) > 0 ? runtimeConfig.reticulum_destination_address : "(announce mode)");
        DEBUG_PRINTF("Use WiFi: %s\n", runtimeConfig.use_wifi_for_reticulum ? "Yes" : "No (LoRa)");
        DEBUG_PRINTF("GPS Enabled: %s\n", runtimeConfig.enable_gps ? "Yes" : "No");
        DEBUG_PRINTF("Status LED Enabled: %s\n", runtimeConfig.enable_status_led ? "Yes" : "No");
        if (runtimeConfig.enable_status_led) {
            DEBUG_PRINTF("Status LED Pin: %d\n", runtimeConfig.status_led_pin);
        }
        DEBUG_PRINTF("Reticulum Send Interval: %lu ms\n", runtimeConfig.reticulum_send_interval_ms);
        DEBUG_PRINTF("OBD Query Interval: %lu ms\n", runtimeConfig.obd_query_interval_ms);
        DEBUG_PRINTF("GPS Read Interval: %lu ms\n", runtimeConfig.gps_read_interval_ms);
        DEBUG_PRINTLN(F("-----------------------------\n"));
    }
    else if (cmd == "vehicle") {
        if (spaceIndex > 0) {
            String value = command.substring(spaceIndex + 1);
            value.trim();
            strncpy(runtimeConfig.vehicle_id, value.c_str(), sizeof(runtimeConfig.vehicle_id) - 1);
            runtimeConfig.vehicle_id[sizeof(runtimeConfig.vehicle_id) - 1] = '\0';
            DEBUG_PRINTF("[CONFIG] Vehicle ID set to: %s\n", runtimeConfig.vehicle_id);
        }
    }
    else if (cmd == "wifi") {
        if (spaceIndex > 0) {
            String rest = command.substring(spaceIndex + 1);
            int passIndex = rest.indexOf(' ');
            if (passIndex > 0) {
                String ssid = rest.substring(0, passIndex);
                String pass = rest.substring(passIndex + 1);
                ssid.trim();
                pass.trim();
                strncpy(runtimeConfig.wifi_ssid, ssid.c_str(), sizeof(runtimeConfig.wifi_ssid) - 1);
                runtimeConfig.wifi_ssid[sizeof(runtimeConfig.wifi_ssid) - 1] = '\0';
                strncpy(runtimeConfig.wifi_password, pass.c_str(), sizeof(runtimeConfig.wifi_password) - 1);
                runtimeConfig.wifi_password[sizeof(runtimeConfig.wifi_password) - 1] = '\0';
                DEBUG_PRINTF("[CONFIG] WiFi SSID set to: %s\n", runtimeConfig.wifi_ssid);
                DEBUG_PRINTLN(F("[CONFIG] WiFi password set"));
            }
        }
    }
    else if (cmd == "obdname") {
        if (spaceIndex > 0) {
            String value = command.substring(spaceIndex + 1);
            value.trim();
            strncpy(runtimeConfig.obdii_device_name, value.c_str(), sizeof(runtimeConfig.obdii_device_name) - 1);
            runtimeConfig.obdii_device_name[sizeof(runtimeConfig.obdii_device_name) - 1] = '\0';
            runtimeConfig.use_obdii_uuids = false;
            runtimeConfig.use_obdii_nordic_uart = false;
            DEBUG_PRINTF("[CONFIG] OBD-II device name set to: %s\n", runtimeConfig.obdii_device_name);
        }
    }
    else if (cmd == "obduuid") {
        if (spaceIndex > 0) {
            String rest = command.substring(spaceIndex + 1);
            int charIndex = rest.indexOf(' ');
            if (charIndex > 0) {
                String svc = rest.substring(0, charIndex);
                String chr = rest.substring(charIndex + 1);
                svc.trim();
                chr.trim();
                strncpy(runtimeConfig.obdii_service_uuid, svc.c_str(), sizeof(runtimeConfig.obdii_service_uuid) - 1);
                runtimeConfig.obdii_service_uuid[sizeof(runtimeConfig.obdii_service_uuid) - 1] = '\0';
                strncpy(runtimeConfig.obdii_characteristic_uuid, chr.c_str(), sizeof(runtimeConfig.obdii_characteristic_uuid) - 1);
                runtimeConfig.obdii_characteristic_uuid[sizeof(runtimeConfig.obdii_characteristic_uuid) - 1] = '\0';
                runtimeConfig.use_obdii_uuids = true;
                DEBUG_PRINTF("[CONFIG] OBD-II UUIDs set (Service: %s, Characteristic: %s)\n", 
                            runtimeConfig.obdii_service_uuid, runtimeConfig.obdii_characteristic_uuid);
            }
        }
    }
    else if (cmd == "rnsapp") {
        if (spaceIndex > 0) {
            String value = command.substring(spaceIndex + 1);
            value.trim();
            strncpy(runtimeConfig.reticulum_app_name, value.c_str(), sizeof(runtimeConfig.reticulum_app_name) - 1);
            runtimeConfig.reticulum_app_name[sizeof(runtimeConfig.reticulum_app_name) - 1] = '\0';
            DEBUG_PRINTF("[CONFIG] Reticulum app name set to: %s\n", runtimeConfig.reticulum_app_name);
        }
    }
    else if (cmd == "rnsdest") {
        if (spaceIndex > 0) {
            String value = command.substring(spaceIndex + 1);
            value.trim();
            if (value == "clear" || value == "empty" || value == "") {
                runtimeConfig.reticulum_destination_address[0] = '\0';
                DEBUG_PRINTLN(F("[CONFIG] Reticulum destination cleared (will use announce mode)"));
            } else {
                strncpy(runtimeConfig.reticulum_destination_address, value.c_str(), sizeof(runtimeConfig.reticulum_destination_address) - 1);
                runtimeConfig.reticulum_destination_address[sizeof(runtimeConfig.reticulum_destination_address) - 1] = '\0';
                DEBUG_PRINTF("[CONFIG] Reticulum destination set to: %s\n", runtimeConfig.reticulum_destination_address);
            }
        }
    }
    else if (cmd == "use_wifi") {
        if (spaceIndex > 0) {
            String value = command.substring(spaceIndex + 1);
            value.trim();
            runtimeConfig.use_wifi_for_reticulum = (value == "1" || value == "true" || value == "yes");
            DEBUG_PRINTF("[CONFIG] Use WiFi set to: %s\n", runtimeConfig.use_wifi_for_reticulum ? "Yes" : "No (LoRa)");
        }
    }
    else if (cmd == "enable_gps") {
        if (spaceIndex > 0) {
            String value = command.substring(spaceIndex + 1);
            value.trim();
            runtimeConfig.enable_gps = (value == "1" || value == "true" || value == "yes");
            DEBUG_PRINTF("[CONFIG] GPS enabled: %s\n", runtimeConfig.enable_gps ? "Yes" : "No");
        }
    }
    else if (cmd == "enable_led") {
        if (spaceIndex > 0) {
            String value = command.substring(spaceIndex + 1);
            value.trim();
            runtimeConfig.enable_status_led = (value == "1" || value == "true" || value == "yes");
            DEBUG_PRINTF("[CONFIG] Status LED enabled: %s\n", runtimeConfig.enable_status_led ? "Yes" : "No");
        }
    }
    else if (cmd == "led_pin") {
        if (spaceIndex > 0) {
            String value = command.substring(spaceIndex + 1);
            value.trim();
            int pin = value.toInt();
            if (pin >= 0 && pin <= 39) {
                runtimeConfig.status_led_pin = pin;
                DEBUG_PRINTF("[CONFIG] Status LED pin set to: %d\n", runtimeConfig.status_led_pin);
            }
        }
    }
    else if (cmd == "save") {
        if (saveConfig()) {
            DEBUG_PRINTLN(F("[CONFIG] Configuration saved successfully!"));
            DEBUG_PRINTLN(F("[CONFIG] Note: Some settings require restart to take effect."));
        } else {
            DEBUG_PRINTLN(F("[CONFIG] Error saving configuration!"));
        }
    }
    else if (cmd == "reset") {
        resetConfigToDefaults();
        DEBUG_PRINTLN(F("[CONFIG] Configuration reset to defaults from config.h"));
        saveConfig();
    }
    else if (cmd == "help" || cmd == "?") {
        showConfigMenu();
    }
    else {
        DEBUG_PRINTF("[CONFIG] Unknown command: %s (type 'help' for menu)\n", cmd.c_str());
    }
}

// Getter functions for runtime configuration
const char* getVehicleID() {
    return runtimeConfig.vehicle_id;
}

const char* getOBDIIDeviceName() {
    return runtimeConfig.obdii_device_name;
}

const char* getWiFiSSID() {
    return runtimeConfig.wifi_ssid;
}

const char* getWiFiPassword() {
    return runtimeConfig.wifi_password;
}

const char* getReticulumAppName() {
    return runtimeConfig.reticulum_app_name;
}

const char* getReticulumDestinationAddress() {
    return runtimeConfig.reticulum_destination_address;
}

bool getUseWiFiForReticulum() {
    return runtimeConfig.use_wifi_for_reticulum;
}

bool getEnableGPS() {
    return runtimeConfig.enable_gps;
}

bool getEnableStatusLED() {
    return runtimeConfig.enable_status_led;
}

uint8_t getStatusLEDPin() {
    return runtimeConfig.status_led_pin;
}

