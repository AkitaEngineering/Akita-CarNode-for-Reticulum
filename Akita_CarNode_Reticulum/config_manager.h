#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "config.h"
#include <Arduino.h>
#include <Preferences.h> // ESP32 non-volatile storage

// Configuration structure that mirrors config.h settings but can be changed at runtime
typedef struct {
    // Vehicle ID
    char vehicle_id[32];
    
    // BLE Configuration
    char obdii_device_name[64];
    char obdii_service_uuid[64];
    char obdii_characteristic_uuid[64];
    bool use_obdii_uuids;
    bool use_obdii_nordic_uart;
    
    // WiFi Configuration
    char wifi_ssid[64];
    char wifi_password[64];
    
    // Reticulum Configuration
    char reticulum_app_name[32];
    char reticulum_destination_address[64];
    bool use_wifi_for_reticulum;
    
    // LoRa Configuration (if used)
    uint32_t lora_band;
    
    // Status LED
    bool enable_status_led;
    uint8_t status_led_pin;
    
    // GPS Configuration
    bool enable_gps;
    uint8_t gps_rx_pin;
    uint8_t gps_tx_pin;
    uint32_t gps_baud_rate;
    
    // Timing Configuration
    uint32_t reticulum_send_interval_ms;
    uint32_t obd_query_interval_ms;
    uint32_t gps_read_interval_ms;
    
    // Configuration version (for migration)
    uint8_t config_version;
} RuntimeConfig;

// Initialize configuration system - loads from EEPROM or uses defaults from config.h
void initConfigManager();

// Load configuration from persistent storage (EEPROM/Preferences)
bool loadConfig();

// Save current configuration to persistent storage
bool saveConfig();

// Reset configuration to defaults from config.h
void resetConfigToDefaults();

// Get current runtime configuration
RuntimeConfig* getRuntimeConfig();

// Apply runtime configuration (updates active settings)
void applyRuntimeConfig();

// Configuration menu via Serial (interactive setup)
void showConfigMenu();
void processConfigCommand(const String& command);

// Helper functions to get configuration values (use these instead of direct config.h access)
const char* getVehicleID();
const char* getOBDIIDeviceName();
const char* getWiFiSSID();
const char* getWiFiPassword();
const char* getReticulumAppName();
const char* getReticulumDestinationAddress();
bool getUseWiFiForReticulum();
bool getEnableGPS();
bool getEnableStatusLED();
uint8_t getStatusLEDPin();

#endif // CONFIG_MANAGER_H

