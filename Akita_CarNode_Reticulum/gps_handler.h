#ifndef GPS_HANDLER_H
#define GPS_HANDLER_H

#include "config.h" // For ENABLE_GPS and pin configurations
#include <Arduino.h> // For basic types like bool, uint8_t

// Initializes the GPS module and the serial communication interface.
// Call this once in the main setup() function.
void initGPS();

// Reads available data from the GPS module, parses NMEA sentences,
// and updates the provided pointers with the latest valid GPS information.
// This function is non-blocking and should be called regularly in the main loop.
// - fix_status: Pointer to a boolean that will be set to true if a valid GPS fix is obtained, false otherwise.
// - lat_val: Pointer to a float for storing latitude.
// - lon_val: Pointer to a float for storing longitude.
// - alt_m_val: Pointer to a float for storing altitude in meters.
// - sats_val: Pointer to a uint8_t for storing the number of satellites in view.
// - speed_kmh_val: Pointer to a float for storing speed in km/h (from GPS).
void readGPS(bool* fix_status, float* lat_val, float* lon_val,
             float* alt_m_val, uint8_t* sats_val, float* speed_kmh_val);

#endif // GPS_HANDLER_H
