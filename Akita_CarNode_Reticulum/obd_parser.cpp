#include "obd_parser.h"
#include "config.h"     // For OBD_PIDS_TO_QUERY, NUM_OBD_PIDS, DEBUG_PRINT macros
#include <stdlib.h>     // For strtol for hex to long conversion
#include <string.h>     // For strcmp, strlen, strncpy
#include <Arduino.h>    // For String class (used for cleaning response), and DEBUG_PRINT macros

void initOBDParser() {
    DEBUG_PRINTLN(F("[OBD_PARSER] Initialized."));
}

// Helper function to convert a hexadecimal string segment of a specific number of characters
// from a larger string into a long integer.
// hexStringStart: Pointer to the start of the hex characters within the larger string.
// numChars: Number of hex characters to convert (e.g., 2 for one byte, 4 for two bytes).
static long hexCharsToLong(const char* hexStringStart, int numChars) {
    if (hexStringStart == nullptr || numChars <= 0 || numChars > 8) { // Max 8 hex chars for a 32-bit long
        DEBUG_PRINTF("[OBD_HEX_UTIL_ERROR] Invalid input to hexCharsToLong: numChars=%d\n", numChars);
        return 0; // Or some error indicator
    }
    char tempBuf[9]; // Max 8 hex characters + null terminator
    strncpy(tempBuf, hexStringStart, numChars);
    tempBuf[numChars] = '\0'; // Ensure null termination
    return strtol(tempBuf, NULL, 16); // Convert hex string to long
}

// Main parsing function
void parseOBDResponse(const char* rawSinglePIDResponse, const OBD_PID& pidRule,
                      float* rpm_val, float* speed_kmh_val, float* coolant_temp_c_val
                      /*, float* intake_air_temp_c_val, ... */) {

    // Null pointer checks
    if (rawSinglePIDResponse == nullptr || pidRule.code == nullptr || 
        pidRule.responsePrefix == nullptr || rpm_val == nullptr || 
        speed_kmh_val == nullptr || coolant_temp_c_val == nullptr) {
        DEBUG_PRINTLN(F("[OBD_PARSE_ERROR] Null pointer passed to parseOBDResponse."));
        return;
    }

    // rawSinglePIDResponse is expected to be a cleaned string for ONE PID,
    // e.g., "410C0A6B" (no spaces, already matched by pidRule.responsePrefix).
    // pidRule.responsePrefix is like "410C".
    // pidRule.code is like "010C".

    // Data bytes start after the prefix (e.g., after "410C").
    // Length of prefix is 4 characters (e.g. "41" + two hex chars of PID like "0C").
    size_t prefixLen = strlen(pidRule.responsePrefix);
    if (strlen(rawSinglePIDResponse) < prefixLen) {
        DEBUG_PRINTLN(F("[OBD_PARSE_ERROR] Response too short for prefix."));
        return;
    }
    const char* dataBytesStart = rawSinglePIDResponse + prefixLen;
    int dataBytesHexLength = strlen(dataBytesStart);

    // --- Engine RPM (PID 010C) ---
    // Formula: (256 * A + B) / 4
    // Response: 410C AA BB (AA BB are 2 data bytes)
    if (strcmp(pidRule.code, "010C") == 0) {
        if (dataBytesHexLength >= 4) { // Need 2 bytes = 4 hex characters (AA BB)
            long valA = hexCharsToLong(dataBytesStart, 2);      // First byte (AA)
            long valB = hexCharsToLong(dataBytesStart + 2, 2);  // Second byte (BB)
            *rpm_val = ((valA * 256.0f) + valB) / 4.0f;
            DEBUG_PRINTF("[OBD_PARSE] %s: %.0f RPM (Hex: %s -> A=0x%02lX, B=0x%02lX)\n",
                         pidRule.name, *rpm_val, dataBytesStart, valA, valB);
        } else {
            DEBUG_PRINTF("[OBD_PARSE_ERROR] %s: Not enough data bytes (got %d hex chars, need 4 for AA BB).\n",
                         pidRule.name, dataBytesHexLength);
        }
    }
    // --- Vehicle Speed (PID 010D) ---
    // Formula: A
    // Response: 410D AA (AA is 1 data byte)
    else if (strcmp(pidRule.code, "010D") == 0) {
        if (dataBytesHexLength >= 2) { // Need 1 byte = 2 hex characters (AA)
            long valA = hexCharsToLong(dataBytesStart, 2); // Byte A
            *speed_kmh_val = (float)valA;
            DEBUG_PRINTF("[OBD_PARSE] %s: %.0f km/h (Hex: %s -> A=0x%02lX)\n",
                         pidRule.name, *speed_kmh_val, dataBytesStart, valA);
        } else {
            DEBUG_PRINTF("[OBD_PARSE_ERROR] %s: Not enough data bytes (got %d hex chars, need 2 for AA).\n",
                         pidRule.name, dataBytesHexLength);
        }
    }
    // --- Engine Coolant Temperature (PID 0105) ---
    // Formula: A - 40
    // Response: 4105 AA (AA is 1 data byte)
    else if (strcmp(pidRule.code, "0105") == 0) {
        if (dataBytesHexLength >= 2) { // Need 1 byte = 2 hex characters (AA)
            long valA = hexCharsToLong(dataBytesStart, 2); // Byte A
            *coolant_temp_c_val = (float)valA - 40.0f;
            DEBUG_PRINTF("[OBD_PARSE] %s: %.1f C (Hex: %s -> A=0x%02lX)\n",
                         pidRule.name, *coolant_temp_c_val, dataBytesStart, valA);
        } else {
            DEBUG_PRINTF("[OBD_PARSE_ERROR] %s: Not enough data bytes (got %d hex chars, need 2 for AA).\n",
                         pidRule.name, dataBytesHexLength);
        }
    }
    // --- Intake Air Temperature (PID 010F) - Example for Expansion ---
    // Formula: A - 40
    // Response: 410F AA (AA is 1 data byte)
    // else if (strcmp(pidRule.code, "010F") == 0) {
    //     if (dataBytesHexLength >= 2) {
    //         long valA = hexCharsToLong(dataBytesStart, 2);
    //         // *intake_air_temp_c_val = (float)valA - 40.0f; // Assuming a pointer is passed for this
    //         DEBUG_PRINTF("[OBD_PARSE] %s: %.1f C (A=0x%02lX)\n", pidRule.name, (float)valA - 40.0f, valA);
    //     } else {
    //         DEBUG_PRINTF("[OBD_PARSE_ERROR] %s: Not enough data bytes.\n", pidRule.name);
    //     }
    // }
    // --- Engine Load (PID 0104) - Example for Expansion ---
    // Formula: A * 100 / 255
    // Response: 4104 AA (AA is 1 data byte)
    // else if (strcmp(pidRule.code, "0104") == 0) {
    //     if (dataBytesHexLength >= 2) {
    //         long valA = hexCharsToLong(dataBytesStart, 2);
    //         // *engine_load_percent_val = (float)valA * 100.0f / 255.0f;
    //         DEBUG_PRINTF("[OBD_PARSE] %s: %.1f %% (A=0x%02lX)\n", pidRule.name, (float)valA * 100.0f / 255.0f, valA);
    //     } else {
    //         DEBUG_PRINTF("[OBD_PARSE_ERROR] %s: Not enough data bytes.\n", pidRule.name);
    //     }
    // }
    else {
        DEBUG_PRINTF("[OBD_PARSE_WARN] No specific parsing logic implemented in obd_parser.cpp for PID Code %s (%s).\n",
                     pidRule.code, pidRule.name);
    }
}
