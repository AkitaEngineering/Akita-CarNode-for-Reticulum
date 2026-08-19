#include "akita_config_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "akita_board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "akita_config";
static const char *AKITA_NAMESPACE = "akita_cfg";
static const char *AKITA_CONFIG_KEY = "runtime_cfg";
static SemaphoreHandle_t g_config_lock;

static void akita_config_terminate_string(char *text, size_t size) {
    if (text == NULL || size == 0U) {
        return;
    }

    text[size - 1U] = '\0';
}

static void akita_config_ensure_lock(void) {
    if (g_config_lock == NULL) {
        g_config_lock = xSemaphoreCreateMutex();
    }
}

void akita_config_lock(void) {
    akita_config_ensure_lock();
    if (g_config_lock != NULL) {
        xSemaphoreTake(g_config_lock, portMAX_DELAY);
    }
}

void akita_config_unlock(void) {
    if (g_config_lock != NULL) {
        xSemaphoreGive(g_config_lock);
    }
}

void akita_config_sanitize(akita_runtime_config_t *config) {
    const akita_board_defaults_t *defaults;

    if (config == NULL) {
        return;
    }

    defaults = akita_board_get_defaults();

    akita_config_terminate_string(config->vehicle_id, sizeof(config->vehicle_id));
    akita_config_terminate_string(config->wifi_ssid, sizeof(config->wifi_ssid));
    akita_config_terminate_string(config->wifi_password, sizeof(config->wifi_password));
    akita_config_terminate_string(config->obd_device_name, sizeof(config->obd_device_name));
    akita_config_terminate_string(config->obd_service_uuid, sizeof(config->obd_service_uuid));
    akita_config_terminate_string(config->obd_characteristic_uuid, sizeof(config->obd_characteristic_uuid));
    akita_config_terminate_string(config->reticulum_destination, sizeof(config->reticulum_destination));
    akita_config_terminate_string(config->telemetry_endpoint, sizeof(config->telemetry_endpoint));

    if (config->vehicle_id[0] == '\0') {
        snprintf(config->vehicle_id, sizeof(config->vehicle_id), "%s", "AkitaCarNode");
    }

    if (config->telemetry_interval_ms < 1000U) {
        config->telemetry_interval_ms = 1000U;
    } else if (config->telemetry_interval_ms > 600000U) {
        config->telemetry_interval_ms = 600000U;
    }

    if (config->gps_uart_baud < 1200U || config->gps_uart_baud > 921600U) {
        config->gps_uart_baud = 9600U;
    }

    if (config->gps_uart_port < 0 || config->gps_uart_port > 2) {
        config->gps_uart_port = 1;
    }

    if (config->lora_frequency_hz < 137000000U || config->lora_frequency_hz > 1020000000U) {
        config->lora_frequency_hz = defaults->lora_frequency_hz;
    }

    if (config->config_http_port == 0U) {
        config->config_http_port = 80U;
    }

    if (config->transport_mode > AKITA_TRANSPORT_LORA) {
        config->transport_mode = defaults->has_lora ? AKITA_TRANSPORT_LORA : AKITA_TRANSPORT_WIFI;
    }

    if (config->board_profile != defaults->profile) {
        ESP_LOGW(
            TAG,
            "Stored board profile does not match this firmware build; reseeding board pins for %s",
            defaults->name
        );
        config->board_profile = defaults->profile;
        config->gps_tx_pin = defaults->gps_tx_pin;
        config->gps_rx_pin = defaults->gps_rx_pin;
        config->status_led_pin = defaults->status_led_pin;
        config->lora_sck_pin = defaults->lora_sck_pin;
        config->lora_miso_pin = defaults->lora_miso_pin;
        config->lora_mosi_pin = defaults->lora_mosi_pin;
        config->lora_cs_pin = defaults->lora_cs_pin;
        config->lora_reset_pin = defaults->lora_reset_pin;
        config->lora_dio0_pin = defaults->lora_dio0_pin;
        config->lora_frequency_hz = defaults->lora_frequency_hz;
    }

    config->schema_version = AKITA_CONFIG_SCHEMA_VERSION;
}

void akita_config_reset(akita_runtime_config_t *config) {
    akita_board_apply_defaults(config);
    akita_config_sanitize(config);
}

esp_err_t akita_config_load(akita_runtime_config_t *config) {
    nvs_handle_t handle;
    size_t blob_size = 0;
    uint8_t *raw = NULL;
    akita_runtime_config_t stored;
    esp_err_t err;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(AKITA_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(handle, AKITA_CONFIG_KEY, NULL, &blob_size);
    if (err != ESP_OK || blob_size == 0U) {
        nvs_close(handle);
        return err != ESP_OK ? err : ESP_ERR_NVS_NOT_FOUND;
    }

    raw = calloc(1, blob_size);
    if (raw == NULL) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_blob(handle, AKITA_CONFIG_KEY, raw, &blob_size);
    nvs_close(handle);
    if (err != ESP_OK) {
        free(raw);
        return err;
    }

    akita_board_apply_defaults(&stored);
    memcpy(&stored, raw, blob_size < sizeof(stored) ? blob_size : sizeof(stored));
    free(raw);

    if (stored.schema_version == 0U || stored.schema_version > AKITA_CONFIG_SCHEMA_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }

    akita_config_sanitize(&stored);
    *config = stored;
    return ESP_OK;
}

esp_err_t akita_config_save(const akita_runtime_config_t *config) {
    nvs_handle_t handle;
    akita_runtime_config_t sanitized;
    esp_err_t err;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sanitized = *config;
    akita_config_sanitize(&sanitized);

    err = nvs_open(AKITA_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, AKITA_CONFIG_KEY, &sanitized, sizeof(sanitized));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}
