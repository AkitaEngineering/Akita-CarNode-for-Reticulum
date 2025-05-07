#ifndef RETICULUM_HANDLER_H
#define RETICULUM_HANDLER_H

#include "config.h" // For Reticulum configuration constants
#include <Arduino.h> // For basic types

// Initializes the Reticulum stack, configured interfaces (WiFi or LoRa),
// and prepares for network communication. Call once in setup().
void initReticulum();

// Main loop function for Reticulum. Processes incoming/outgoing packets,
// manages interface states (like WiFi reconnection), and handles other stack tasks.
// Call this repeatedly and frequently in the main loop().
void reticulumLoop();

// Sends the provided data payload over the Reticulum network.
// - jsonDataPayload: A C-string containing the JSON formatted data to send.
// Data will be sent to a specific RNS destination if configured in config.h,
// otherwise, it will be announced on the network.
void sendDataOverReticulum(const char* jsonDataPayload);

// Returns true if the physical network layer (WiFi connected or LoRa initialized) is up.
bool isReticulumConnected();

// Returns true if Reticulum's transport layer is active and ready to send/receive data.
// This implies the physical layer is up AND the Reticulum stack has successfully started an interface.
bool isReticulumReadyToSend();

#endif // RETICULUM_HANDLER_H
