#ifndef AKITA_BOARD_H
#define AKITA_BOARD_H

#include "akita_types.h"

typedef struct {
    akita_board_profile_t profile;
    const char *name;
    int32_t status_led_pin;
    int32_t gps_tx_pin;
    int32_t gps_rx_pin;
    int32_t lora_sck_pin;
    int32_t lora_miso_pin;
    int32_t lora_mosi_pin;
    int32_t lora_cs_pin;
    int32_t lora_reset_pin;
    int32_t lora_dio0_pin;
    uint32_t lora_frequency_hz;
    bool has_lora;
} akita_board_defaults_t;

const akita_board_defaults_t *akita_board_get_defaults(void);
const char *akita_board_get_name(akita_board_profile_t profile);
void akita_board_apply_defaults(akita_runtime_config_t *config);

#endif