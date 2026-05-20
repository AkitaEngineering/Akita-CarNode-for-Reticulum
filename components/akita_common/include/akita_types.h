#ifndef AKITA_TYPES_H
#define AKITA_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AKITA_CONFIG_SCHEMA_VERSION 1U
#define AKITA_INVALID_PIN (-1)

typedef enum {
    AKITA_BOARD_GENERIC_ESP32S3 = 0,
    AKITA_BOARD_GENERIC_ESP32C6,
    AKITA_BOARD_GENERIC_ESP32C5,
    AKITA_BOARD_HELTEC_LORA32_V2,
} akita_board_profile_t;

typedef enum {
    AKITA_TRANSPORT_NONE = 0,
    AKITA_TRANSPORT_WIFI,
    AKITA_TRANSPORT_LORA,
} akita_transport_mode_t;

typedef struct {
    bool fix;
    float latitude;
    float longitude;
    float altitude_m;
    float speed_kmh;
    uint8_t satellites;
    uint32_t age_ms;
} akita_gps_snapshot_t;

typedef struct {
    bool connected;
    float rpm;
    float speed_kmh;
    float coolant_c;
    uint32_t age_ms;
} akita_obd_snapshot_t;

typedef struct {
    bool wifi_ready;
    bool lora_ready;
    bool config_portal_ready;
    bool transport_ready;
    int8_t wifi_rssi;
    uint32_t free_heap;
} akita_system_snapshot_t;

typedef struct {
    uint32_t schema_version;
    akita_board_profile_t board_profile;
    akita_transport_mode_t transport_mode;
    char vehicle_id[32];
    char wifi_ssid[64];
    char wifi_password[64];
    char obd_device_name[64];
    char obd_service_uuid[40];
    char obd_characteristic_uuid[40];
    char reticulum_destination[64];
    char telemetry_endpoint[96];
    bool enable_gps;
    bool enable_config_ap;
    bool use_obd_uuid;
    uint32_t telemetry_interval_ms;
    uint32_t gps_uart_baud;
    int32_t gps_uart_port;
    int32_t gps_tx_pin;
    int32_t gps_rx_pin;
    int32_t status_led_pin;
    int32_t lora_sck_pin;
    int32_t lora_miso_pin;
    int32_t lora_mosi_pin;
    int32_t lora_cs_pin;
    int32_t lora_reset_pin;
    int32_t lora_dio0_pin;
    uint32_t lora_frequency_hz;
    uint16_t config_http_port;
} akita_runtime_config_t;

typedef struct {
    akita_gps_snapshot_t gps;
    akita_obd_snapshot_t obd;
    akita_system_snapshot_t system;
} akita_vehicle_telemetry_t;

#endif