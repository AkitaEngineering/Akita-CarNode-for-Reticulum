#include "gps_handler.h"
#include "config.h"     // For ENABLE_GPS, GPS pins, baud rate, DEBUG_PRINT macros
#include <TinyGPS++.h>  // GPS parsing library
#include <Arduino.h>    // For Serial, SoftwareSerial (if used)

#if ENABLE_GPS

// GPS object instance
TinyGPSPlus gpsParser;

// Serial interface for GPS
// Option 1: SoftwareSerial (default as per typical config)
// SoftwareSerial gpsHwSerial(GPS_RX_PIN, GPS_TX_PIN); // Name it gpsHwSerial to avoid conflict if HardwareSerial is also used
// #define GPS_SERIAL_PORT gpsHwSerial

// Option 2: HardwareSerial (more reliable, if available and pins match)
// Example: if GPS_RX_PIN is 16 and GPS_TX_PIN is 17 for ESP32's Serial2
// HardwareSerial& gpsHwSerial = Serial2; // Use Serial1 or Serial2 as appropriate
// #define GPS_SERIAL_PORT gpsHwSerial

// For this implementation, we will stick to SoftwareSerial as per the typical config.h structure.
// If you use HardwareSerial, adjust the definition of GPS_SERIAL_PORT and the initGPS().
SoftwareSerial softwareGpsSerial(GPS_RX_PIN, GPS_TX_PIN);
#define GPS_SERIAL_PORT softwareGpsSerial


void initGPS() {
#if ENABLE_SERIAL_DEBUG > 0
    DEBUG_PRINTF("[GPS_INIT] Initializing GPS module: Serial interface on RX:%d, TX:%d at %d baud.\n",
                 GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD_RATE);
#endif
    GPS_SERIAL_PORT.begin(GPS_BAUD_RATE);
    DEBUG_PRINTLN(F("[GPS_INIT] GPS Serial Interface started. Waiting for NMEA data..."));
}

void readGPS(bool* fix_status, float* lat_val, float* lon_val,
             float* alt_m_val, uint8_t* sats_val, float* speed_kmh_val) {
    
    if (!ENABLE_GPS) { // Should not be called if disabled, but as a safeguard
        if(fix_status) *fix_status = false;
        return;
    }

    bool sentenceProcessedThisCall = false;
    // Process all available characters from the GPS serial port
    while (GPS_SERIAL_PORT.available() > 0) {
        if (gpsParser.encode(GPS_SERIAL_PORT.read())) {
            // A new NMEA sentence was successfully parsed by TinyGPS++.
            // We will process the collected data once after checking all available chars.
            sentenceProcessedThisCall = true;
        }
    }

    // If any new sentence was processed, update the output variables with the latest data from gpsParser object
    if (sentenceProcessedThisCall) {
        if (gpsParser.location.isValid() && gpsParser.location.isUpdated() && gpsParser.location.age() < 2000) { // Check for valid, updated, and recent fix
            *fix_status = true;
            *lat_val = gpsParser.location.lat();
            *lon_val = gpsParser.location.lng();
            
            if (gpsParser.altitude.isValid() && gpsParser.altitude.isUpdated()) {
                *alt_m_val = gpsParser.altitude.meters();
            } else {
                *alt_m_val = 0.0f; // Or some indicator for invalid altitude
            }

            if (gpsParser.satellites.isValid() && gpsParser.satellites.isUpdated()) {
                *sats_val = gpsParser.satellites.value();
            } else {
                *sats_val = 0;
            }

            if (gpsParser.speed.isValid() && gpsParser.speed.isUpdated()) {
                *speed_kmh_val = gpsParser.speed.kmph();
            } else {
                *speed_kmh_val = 0.0f;
            }

            DEBUG_PRINTF("[GPS_DATA] Fix: YES, Lat:%.6f, Lon:%.6f, Alt:%.1fm, Sats:%u, Speed:%.1fkm/h, Age:%lums\n",
                         *lat_val, *lon_val, *alt_m_val, *sats_val, *speed_kmh_val, gpsParser.location.age());
        } else {
            // No valid fix, or location data is stale or not updated in this batch of sentences.
            // Keep previous valid values for lat/lon/alt etc., but report fix_status as false.
            *fix_status = false;
            DEBUG_PRINTLN(F("[GPS_DATA] No valid GPS fix, or location data stale/not updated in this cycle."));
        }
    }
    // If no new NMEA sentence was fully parsed in this call (sentenceProcessedThisCall is false),
    // the values pointed to by fix_status, lat_val etc., remain unchanged from the previous call.
    // The main loop uses *fix_status to know the current state.
}

#else // ENABLE_GPS is false in config.h

// Provide stub functions if GPS is disabled to allow compilation and maintain consistent API.
void initGPS() {
    DEBUG_PRINTLN(F("[GPS_INIT] GPS functionality is DISABLED in config.h."));
}

void readGPS(bool* fix_status, float* lat_val, float* lon_val,
             float* alt_m_val, uint8_t* sats_val, float* speed_kmh_val) {
    // Set default "no data" values if GPS is disabled.
    if (fix_status) *fix_status = false;
    if (lat_val) *lat_val = 0.0f;
    if (lon_val) *lon_val = 0.0f;
    if (alt_m_val) *alt_m_val = 0.0f;
    if (sats_val) *sats_val = 0;
    if (speed_kmh_val) *speed_kmh_val = 0.0f;
}

#endif // ENABLE_GPS
