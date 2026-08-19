#ifndef AKITA_CONFIG_STORE_H
#define AKITA_CONFIG_STORE_H

#include "akita_types.h"
#include "esp_err.h"

esp_err_t akita_config_load(akita_runtime_config_t *config);
esp_err_t akita_config_save(const akita_runtime_config_t *config);
void akita_config_reset(akita_runtime_config_t *config);
void akita_config_sanitize(akita_runtime_config_t *config);
void akita_config_lock(void);
void akita_config_unlock(void);

#endif
