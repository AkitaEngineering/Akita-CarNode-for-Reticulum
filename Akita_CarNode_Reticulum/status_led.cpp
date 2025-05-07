#include "status_led.h"
#include "config.h"     // For ENABLE_STATUS_LED, STATUS_LED_PIN, DEBUG_PRINT macros
#include <Arduino.h>    // For digitalWrite, pinMode, millis, delay

#if ENABLE_STATUS_LED

static LedStatusType currentSystemLedStatus = LED_STATUS_OFF;
static LedStatusType previousPrintedLedStatus = LED_STATUS_OFF; // To avoid spamming serial log with status changes
static unsigned long ledPatternPreviousMillis = 0; // Timer for blinking patterns
static int ledCurrentBlinkPhase = 0; // State for multi-phase blink patterns
static bool ledPhysicalState = LOW;   // Current physical state of the LED (HIGH or LOW)

// Define blink intervals (ms) for different patterns
#define BLINK_INTERVAL_FAST 200         // For rapid blinking (e.g., connecting)
#define BLINK_INTERVAL_SLOW 750         // For slower attention signals
#define BLINK_INTERVAL_ERROR 100        // Very rapid for errors

#define HEARTBEAT_PULSE_ON_TIME 80      // Duration of the "on" part of a heartbeat
#define HEARTBEAT_PULSE_OFF_TIME 1920   // Duration of the "off" part (total period ~2s)

#define GPS_NO_FIX_BLINK_ON 200
#define GPS_NO_FIX_BLINK_OFF1 200
#define GPS_NO_FIX_BLINK_OFF2 1000 // Longer pause for double blink (on-off-on-long_off)


void setupStatusLed() {
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW); // Start with LED off
    currentSystemLedStatus = LED_STATUS_OFF; // Initialize status
    DEBUG_PRINTLN(F("[LED_SETUP] Status LED initialized."));
}

void setLedStatus(LedStatusType newStatus) {
    if (currentSystemLedStatus == newStatus) {
        return; // No change in requested status
    }

    currentSystemLedStatus = newStatus;
    ledPatternPreviousMillis = millis(); // Reset timer for new pattern
    ledCurrentBlinkPhase = 0;            // Reset blink phase for new pattern
    ledPhysicalState = LOW;              // Default to off before pattern starts

    if (currentSystemLedStatus != previousPrintedLedStatus) {
        DEBUG_PRINTF("[LED_STATUS] System LED status changed to: %d\n", newStatus);
        previousPrintedLedStatus = currentSystemLedStatus;
    }

    // Handle immediate solid states here; blinking patterns are managed by updateLed()
    switch (newStatus) {
        case LED_STATUS_OFF:
            digitalWrite(STATUS_LED_PIN, LOW);
            break;
        case LED_STATUS_INITIALIZING: // Solid ON during initialization phase
            digitalWrite(STATUS_LED_PIN, HIGH);
            break;
        // For blinking states, updateLed() will take over.
        // We can set an initial state for the blink here if desired.
        case LED_STATUS_IDLE: // Example: solid off for idle, or could be a very slow blink
            digitalWrite(STATUS_LED_PIN, LOW);
            break;
        default:
            // Most patterns will start with the LED off or based on the first phase in updateLed()
            digitalWrite(STATUS_LED_PIN, LOW); 
            break;
    }
}

void updateLed() {
    unsigned long currentMillis = millis();
    long intervalForCurrentPhase = 0;

    switch (currentSystemLedStatus) {
        case LED_STATUS_OFF:
        case LED_STATUS_INITIALIZING: // Solid states are set by setLedStatus, no update needed here
        case LED_STATUS_IDLE:         // If IDLE is solid off, no update needed. If it blinks, add logic.
            return; 

        case LED_STATUS_BLE_CONNECTING:
        case LED_STATUS_RETICULUM_CONNECTING:
            intervalForCurrentPhase = BLINK_INTERVAL_FAST;
            if (currentMillis - ledPatternPreviousMillis >= intervalForCurrentPhase) {
                ledPatternPreviousMillis = currentMillis;
                ledPhysicalState = !ledPhysicalState; // Toggle state
                digitalWrite(STATUS_LED_PIN, ledPhysicalState);
            }
            break;

        case LED_STATUS_GPS_NO_FIX: // Double blink pattern: ON - OFF - ON - LONG_OFF
            switch (ledCurrentBlinkPhase) {
                case 0: // ON
                    digitalWrite(STATUS_LED_PIN, HIGH);
                    ledPhysicalState = HIGH;
                    intervalForCurrentPhase = GPS_NO_FIX_BLINK_ON;
                    break;
                case 1: // OFF1
                    digitalWrite(STATUS_LED_PIN, LOW);
                    ledPhysicalState = LOW;
                    intervalForCurrentPhase = GPS_NO_FIX_BLINK_OFF1;
                    break;
                case 2: // ON
                    digitalWrite(STATUS_LED_PIN, HIGH);
                    ledPhysicalState = HIGH;
                    intervalForCurrentPhase = GPS_NO_FIX_BLINK_ON;
                    break;
                case 3: // LONG_OFF
                    digitalWrite(STATUS_LED_PIN, LOW);
                    ledPhysicalState = LOW;
                    intervalForCurrentPhase = GPS_NO_FIX_BLINK_OFF2;
                    break;
            }
            if (currentMillis - ledPatternPreviousMillis >= intervalForCurrentPhase) {
                ledPatternPreviousMillis = currentMillis;
                ledCurrentBlinkPhase = (ledCurrentBlinkPhase + 1) % 4; // Cycle through 4 phases
                // State for next phase is set at the beginning of the next switch case
            }
            break;

        case LED_STATUS_OPERATIONAL: // Heartbeat pattern: Brief ON, longer OFF
            if (ledPhysicalState == LOW) { // Currently OFF, check if time to turn ON
                intervalForCurrentPhase = HEARTBEAT_PULSE_OFF_TIME;
                if (currentMillis - ledPatternPreviousMillis >= intervalForCurrentPhase) {
                    ledPatternPreviousMillis = currentMillis;
                    ledPhysicalState = HIGH;
                    digitalWrite(STATUS_LED_PIN, ledPhysicalState);
                }
            } else { // Currently ON, check if time to turn OFF
                intervalForCurrentPhase = HEARTBEAT_PULSE_ON_TIME;
                if (currentMillis - ledPatternPreviousMillis >= intervalForCurrentPhase) {
                    ledPatternPreviousMillis = currentMillis;
                    ledPhysicalState = LOW;
                    digitalWrite(STATUS_LED_PIN, ledPhysicalState);
                }
            }
            break;

        case LED_STATUS_ERROR: // Rapid, continuous blinking
            intervalForCurrentPhase = BLINK_INTERVAL_ERROR;
            if (currentMillis - ledPatternPreviousMillis >= intervalForCurrentPhase) {
                ledPatternPreviousMillis = currentMillis;
                ledPhysicalState = !ledPhysicalState; // Toggle state
                digitalWrite(STATUS_LED_PIN, ledPhysicalState);
            }
            break;
        
        default: // Should not be reached if all statuses are handled
            digitalWrite(STATUS_LED_PIN, LOW); // Default to off
            break;
    }
}

#else // ENABLE_STATUS_LED is false in config.h

// Provide stub functions if LED is disabled to allow compilation and maintain consistent API.
void setupStatusLed() {
    // DEBUG_PRINTLN(F("[LED_SETUP] Status LED functionality is DISABLED in config.h."));
}
void setLedStatus(LedStatusType newStatus) {
    (void)newStatus; // Suppress unused parameter warning
}
void updateLed() {
    // Nothing to do
}

#endif // ENABLE_STATUS_LED
