#ifndef AKITA_TRANSPORT_H
#define AKITA_TRANSPORT_H

#include <stdbool.h>

#include "akita_types.h"
#include "esp_err.h"

esp_err_t akita_transport_init(const akita_runtime_config_t *config);
esp_err_t akita_transport_publish(const akita_runtime_config_t *config, const char *payload);
bool akita_transport_ready(void);
const char *akita_transport_name(const akita_runtime_config_t *config);

#endif