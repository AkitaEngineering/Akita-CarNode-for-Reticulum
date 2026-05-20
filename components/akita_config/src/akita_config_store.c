#include "akita_config_store.h"

#include <string.h>

#include "akita_board.h"
#include "nvs.h"

static const char *AKITA_NAMESPACE = "akita_cfg";
static const char *AKITA_CONFIG_KEY = "runtime_cfg";

void akita_config_reset(akita_runtime_config_t *config) {
    akita_board_apply_defaults(config);
}

esp_err_t akita_config_load(akita_runtime_config_t *config) {
    akita_runtime_config_t stored;
    nvs_handle_t handle;
    size_t required = sizeof(stored);
    esp_err_t err;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(AKITA_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    memset(&stored, 0, sizeof(stored));
    err = nvs_get_blob(handle, AKITA_CONFIG_KEY, &stored, &required);
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }

    if (required != sizeof(stored) || stored.schema_version != AKITA_CONFIG_SCHEMA_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }

    *config = stored;
    return ESP_OK;
}

esp_err_t akita_config_save(const akita_runtime_config_t *config) {
    nvs_handle_t handle;
    esp_err_t err;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(AKITA_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, AKITA_CONFIG_KEY, config, sizeof(*config));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}