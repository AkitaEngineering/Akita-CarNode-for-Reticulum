#ifndef BLE_HANDLER_H
#define BLE_HANDLER_H

#include "config.h" // For OBD_PID definition and BLE configuration constants
#include <Arduino.h>

// Initializes the BLE system and prepares for scanning.
void initBLE();

// Attempts to scan for and connect to the configured OBD-II adapter.
// This function is non-blocking and manages its own timing for retries
// using exponential backoff. Call regularly in the main loop.
// Returns true if currently trying to connect or already connected, false otherwise.
bool connectBLE();

// Returns true if BLE is connected to the OBD-II adapter, false otherwise.
bool isBLEConnected();

// Disconnects from the BLE OBD-II adapter if connected.
void disconnectBLE();

// Sends a request for a specific OBD-II PID to the connected adapter.
// pid: The OBD_PID structure containing the code and name of the PID to request.
void requestOBDPID(const OBD_PID& pid);

// Processes any received OBD-II response data from the internal buffer.
// This function is non-blocking and should be called regularly in the main loop.
// It parses the response and updates the provided pointers with the data.
void processOBDResponse(float* rpm, float* speed_kmh, float* coolant_temp_c /*, add other relevant data pointers */);

#endif // BLE_HANDLER_H
