#ifndef AKITA_APP_H
#define AKITA_APP_H

#include <stddef.h>

#include "akita_types.h"
#include "esp_err.h"

esp_err_t akita_app_start(void);
const akita_runtime_config_t *akita_app_get_config(void);
const akita_vehicle_telemetry_t *akita_app_get_telemetry(void);
size_t akita_payload_write_json(
    const akita_runtime_config_t *config,
    const akita_vehicle_telemetry_t *telemetry,
    char *buffer,
    size_t buffer_size
);
size_t akita_payload_write_compact_json(
    const akita_runtime_config_t *config,
    const akita_vehicle_telemetry_t *telemetry,
    char *buffer,
    size_t buffer_size
);

#endif