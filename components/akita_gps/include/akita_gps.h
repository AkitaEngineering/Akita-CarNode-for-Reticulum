#ifndef AKITA_GPS_H
#define AKITA_GPS_H

#include "akita_types.h"
#include "esp_err.h"

esp_err_t akita_gps_init(const akita_runtime_config_t *config);
void akita_gps_poll(akita_gps_snapshot_t *snapshot);

#endif