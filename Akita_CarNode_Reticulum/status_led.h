#ifndef STATUS_LED_H
#define STATUS_LED_H

#include "config.h" // For ENABLE_STATUS_LED and STATUS_LED_PIN
#include <Arduino.h> // For basic types

#if ENABLE_STATUS_LED

// Enum defining the different states the system can be in, for LED indication.
typedef enum {
    LED_STATUS_OFF,                 // LED is completely off.
    LED_STATUS_INITIALIZING,        // System is booting up (e.g., solid ON or specific init pattern).
    LED_STATUS_IDLE,                // System is idle, awaiting connections or actions (e.g., slow pulse or off).
    LED_STATUS_BLE_CONNECTING,      // Actively trying to connect to BLE OBD-II (e.g., fast blink).
    LED_STATUS_GPS_NO_FIX,          // BLE connected, but GPS has no valid fix (e.g., specific blink pattern).
    LED_STATUS_RETICULUM_CONNECTING,// BLE & GPS OK (if enabled), but Reticulum not ready (e.g., different fast blink).
    LED_STATUS_OPERATIONAL,         // All systems nominal and connected (e.g., heartbeat or solid ON).
    LED_STATUS_ERROR                // A general error state (e.g., rapid irregular blink).
} LedStatusType;

// Initializes the status LED GPIO pin. Call once in setup().
void setupStatusLed();

// Sets the current visual status of the LED.
// This function might set a solid state or trigger a pattern handled by updateLed().
// newStatus: The LedStatusType to display.
void setLedStatus(LedStatusType newStatus);

// Updates the LED for blinking patterns. Call regularly in the main loop().
// This function handles time-based blinking based on the currentLedStatus.
void updateLed();

#endif // ENABLE_STATUS_LED
#endif // STATUS_LED_H
