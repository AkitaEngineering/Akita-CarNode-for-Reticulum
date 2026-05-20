#ifndef AKITA_CONFIG_UI_H
#define AKITA_CONFIG_UI_H

#include <stdbool.h>

#include "akita_types.h"
#include "esp_err.h"

typedef esp_err_t (*akita_config_apply_callback_t)(const akita_runtime_config_t *config, void *context);

esp_err_t akita_config_ui_start(akita_runtime_config_t *config);
void akita_config_ui_set_apply_callback(akita_config_apply_callback_t callback, void *context);
bool akita_config_ui_is_running(void);

#endif