#ifndef OBD_PARSER_H
#define OBD_PARSER_H

#include "config.h" // For OBD_PID struct definition
#include <Arduino.h> // For String if used by parser, and basic types

// Initializes the OBD parser module (if any specific setup is needed).
void initOBDParser();

// Parses a raw OBD-II response string based on the provided PID rule.
// Updates the data pointers (rpm, speed_kmh, coolant_temp_c, etc.) with the parsed values.
// - rawResponse: A string containing the cleaned OBD-II response for a single PID (e.g., "410C0A6B").
// - pidRule: The OBD_PID structure that matches this response, used to determine parsing logic.
// - rpm, speed_kmh, coolant_temp_c: Pointers to float variables where parsed data will be stored.
//   Add more pointers here if parsing more PIDs (e.g., float* intake_air_temp_c).
void parseOBDResponse(const char* rawResponse, const OBD_PID& pidRule,
                      float* rpm, float* speed_kmh, float* coolant_temp_c
                      /*, float* intake_air_temp_c, float* engine_load_percent ... */);

#endif // OBD_PARSER_H
