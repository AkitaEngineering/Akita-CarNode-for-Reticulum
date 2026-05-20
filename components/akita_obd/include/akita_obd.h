#ifndef AKITA_OBD_H
#define AKITA_OBD_H

#include <stdbool.h>
#include <stddef.h>

#include "akita_types.h"
#include "esp_err.h"

esp_err_t akita_obd_init(const akita_runtime_config_t *config);
void akita_obd_poll(akita_obd_snapshot_t *snapshot);
size_t akita_obd_build_request(const char *pid, char *buffer, size_t buffer_size);
bool akita_obd_apply_response(akita_obd_snapshot_t *snapshot, const char *response);

#endif