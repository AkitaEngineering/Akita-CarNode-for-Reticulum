#include "akita_board.h"

#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"

static const akita_board_defaults_t kBoardDefaults[] = {
    {
        .profile = AKITA_BOARD_GENERIC_ESP32S3,
        .name = "Generic ESP32-S3",
        .status_led_pin = AKITA_INVALID_PIN,
        .gps_tx_pin = AKITA_INVALID_PIN,
        .gps_rx_pin = AKITA_INVALID_PIN,
        .lora_sck_pin = AKITA_INVALID_PIN,
        .lora_miso_pin = AKITA_INVALID_PIN,
        .lora_mosi_pin = AKITA_INVALID_PIN,
        .lora_cs_pin = AKITA_INVALID_PIN,
        .lora_reset_pin = AKITA_INVALID_PIN,
        .lora_dio0_pin = AKITA_INVALID_PIN,
        .lora_frequency_hz = 915000000U,
        .has_lora = false,
    },
    {
        .profile = AKITA_BOARD_GENERIC_ESP32C6,
        .name = "Generic ESP32-C6",
        .status_led_pin = AKITA_INVALID_PIN,
        .gps_tx_pin = AKITA_INVALID_PIN,
        .gps_rx_pin = AKITA_INVALID_PIN,
        .lora_sck_pin = AKITA_INVALID_PIN,
        .lora_miso_pin = AKITA_INVALID_PIN,
        .lora_mosi_pin = AKITA_INVALID_PIN,
        .lora_cs_pin = AKITA_INVALID_PIN,
        .lora_reset_pin = AKITA_INVALID_PIN,
        .lora_dio0_pin = AKITA_INVALID_PIN,
        .lora_frequency_hz = 915000000U,
        .has_lora = false,
    },
    {
        .profile = AKITA_BOARD_GENERIC_ESP32C5,
        .name = "Generic ESP32-C5",
        .status_led_pin = AKITA_INVALID_PIN,
        .gps_tx_pin = AKITA_INVALID_PIN,
        .gps_rx_pin = AKITA_INVALID_PIN,
        .lora_sck_pin = AKITA_INVALID_PIN,
        .lora_miso_pin = AKITA_INVALID_PIN,
        .lora_mosi_pin = AKITA_INVALID_PIN,
        .lora_cs_pin = AKITA_INVALID_PIN,
        .lora_reset_pin = AKITA_INVALID_PIN,
        .lora_dio0_pin = AKITA_INVALID_PIN,
        .lora_frequency_hz = 915000000U,
        .has_lora = false,
    },
    {
        .profile = AKITA_BOARD_HELTEC_LORA32_V2,
        .name = "Heltec LoRa 32 V2",
        .status_led_pin = 25,
        .gps_tx_pin = 12,
        .gps_rx_pin = 34,
        .lora_sck_pin = 5,
        .lora_miso_pin = 19,
        .lora_mosi_pin = 27,
        .lora_cs_pin = 18,
        .lora_reset_pin = 14,
        .lora_dio0_pin = 26,
        .lora_frequency_hz = 915000000U,
        .has_lora = true,
    },
};

static akita_board_profile_t akita_board_active_profile(void) {
#if CONFIG_AKITA_BOARD_HELTEC_LORA32_V2
    return AKITA_BOARD_HELTEC_LORA32_V2;
#elif CONFIG_AKITA_BOARD_GENERIC_ESP32C6
    return AKITA_BOARD_GENERIC_ESP32C6;
#elif CONFIG_AKITA_BOARD_GENERIC_ESP32C5
    return AKITA_BOARD_GENERIC_ESP32C5;
#else
    return AKITA_BOARD_GENERIC_ESP32S3;
#endif
}

const akita_board_defaults_t *akita_board_get_defaults(void) {
    size_t index;
    akita_board_profile_t profile = akita_board_active_profile();

    for (index = 0; index < (sizeof(kBoardDefaults) / sizeof(kBoardDefaults[0])); ++index) {
        if (kBoardDefaults[index].profile == profile) {
            return &kBoardDefaults[index];
        }
    }

    return &kBoardDefaults[0];
}

const char *akita_board_get_name(akita_board_profile_t profile) {
    size_t index;

    for (index = 0; index < (sizeof(kBoardDefaults) / sizeof(kBoardDefaults[0])); ++index) {
        if (kBoardDefaults[index].profile == profile) {
            return kBoardDefaults[index].name;
        }
    }

    return "Unknown board";
}

void akita_board_apply_defaults(akita_runtime_config_t *config) {
    const akita_board_defaults_t *defaults;

    if (config == NULL) {
        return;
    }

    defaults = akita_board_get_defaults();
    memset(config, 0, sizeof(*config));

    config->schema_version = AKITA_CONFIG_SCHEMA_VERSION;
    config->board_profile = defaults->profile;
    config->transport_mode = defaults->has_lora ? AKITA_TRANSPORT_LORA : AKITA_TRANSPORT_WIFI;
    snprintf(config->vehicle_id, sizeof(config->vehicle_id), "%s", "AkitaCarNode");
    snprintf(config->obd_device_name, sizeof(config->obd_device_name), "%s", "OBDII");
    config->enable_gps = true;
    config->enable_config_ap = CONFIG_AKITA_ENABLE_CONFIG_PORTAL;
    config->telemetry_interval_ms = 10000U;
    config->gps_uart_baud = 9600U;
    config->gps_uart_port = 1;
    config->gps_tx_pin = defaults->gps_tx_pin;
    config->gps_rx_pin = defaults->gps_rx_pin;
    config->status_led_pin = defaults->status_led_pin;
    config->lora_sck_pin = defaults->lora_sck_pin;
    config->lora_miso_pin = defaults->lora_miso_pin;
    config->lora_mosi_pin = defaults->lora_mosi_pin;
    config->lora_cs_pin = defaults->lora_cs_pin;
    config->lora_reset_pin = defaults->lora_reset_pin;
    config->lora_dio0_pin = defaults->lora_dio0_pin;
    config->lora_frequency_hz = defaults->lora_frequency_hz;
    config->config_http_port = 80U;
}