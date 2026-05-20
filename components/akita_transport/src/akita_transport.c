#include "akita_transport.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "akita_transport";
static bool g_transport_ready;

esp_err_t akita_transport_init(const akita_runtime_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    g_transport_ready = false;
    ESP_LOGW(
        TAG,
        "Native transport layer still needs target-specific work. Current mode: %s",
        akita_transport_name(config)
    );
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t akita_transport_publish(const akita_runtime_config_t *config, const char *payload) {
    (void) config;
    if (payload == NULL || payload[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_transport_ready) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(TAG, "publish: %s", payload);
    return ESP_OK;
}

bool akita_transport_ready(void) {
    return g_transport_ready;
}

const char *akita_transport_name(const akita_runtime_config_t *config) {
    if (config == NULL) {
        return "unknown";
    }

    switch (config->transport_mode) {
        case AKITA_TRANSPORT_WIFI:
            return "wifi";
        case AKITA_TRANSPORT_LORA:
            return "lora";
        default:
            return "none";
    }
}