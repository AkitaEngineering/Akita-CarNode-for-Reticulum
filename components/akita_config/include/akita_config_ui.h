#ifndef AKITA_CONFIG_UI_H
#define AKITA_CONFIG_UI_H

#include <stdbool.h>

#include "akita_types.h"
#include "esp_err.h"

esp_err_t akita_config_ui_start(akita_runtime_config_t *config);
bool akita_config_ui_is_running(void);

#endif