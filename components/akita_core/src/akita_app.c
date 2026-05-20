#include "akita_app.h"

#include <string.h>

#include "akita_board.h"
#include "akita_config_store.h"
#include "akita_config_ui.h"
#include "akita_gps.h"
#include "akita_obd.h"
#include "akita_transport.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "akita_app";
static akita_runtime_config_t g_runtime_config;
static akita_vehicle_telemetry_t g_telemetry;
static bool g_led_ready;

static void akita_status_led_init(void) {
    if (g_runtime_config.status_led_pin < 0) {
        g_led_ready = false;
        return;
    }

    gpio_reset_pin((gpio_num_t) g_runtime_config.status_led_pin);
    gpio_set_direction((gpio_num_t) g_runtime_config.status_led_pin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t) g_runtime_config.status_led_pin, 0);
    g_led_ready = true;
}

static void akita_status_led_pulse(void) {
    if (!g_led_ready) {
        return;
    }

    gpio_set_level((gpio_num_t) g_runtime_config.status_led_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(40));
    gpio_set_level((gpio_num_t) g_runtime_config.status_led_pin, 0);
}

static void akita_main_task(void *arg) {
    char payload[768];
    uint64_t last_publish_ms = 0;
    (void) arg;

    while (true) {
        uint64_t now_ms = (uint64_t) (esp_timer_get_time() / 1000ULL);
        esp_err_t publish_status;
        size_t payload_len;

        akita_gps_poll(&g_telemetry.gps);
        akita_obd_poll(&g_telemetry.obd);

        g_telemetry.system.config_portal_ready = akita_config_ui_is_running();
        g_telemetry.system.transport_ready = akita_transport_ready();
        g_telemetry.system.free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        g_telemetry.system.wifi_ready = g_telemetry.system.config_portal_ready;
        g_telemetry.system.lora_ready = (g_runtime_config.transport_mode == AKITA_TRANSPORT_LORA);
        g_telemetry.system.wifi_rssi = 0;

        if ((now_ms - last_publish_ms) >= g_runtime_config.telemetry_interval_ms) {
            payload_len = akita_payload_write_json(&g_runtime_config, &g_telemetry, payload, sizeof(payload));
            if (payload_len > 0) {
                publish_status = akita_transport_publish(&g_runtime_config, payload);
                if (publish_status != ESP_OK) {
                    ESP_LOGW(TAG, "Native transport backend not ready yet, telemetry will stay local");
                    ESP_LOGI(TAG, "%s", payload);
                } else {
                    akita_status_led_pulse();
                }
            }
            last_publish_ms = now_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

esp_err_t akita_app_start(void) {
    esp_err_t err;

    akita_board_apply_defaults(&g_runtime_config);
    memset(&g_telemetry, 0, sizeof(g_telemetry));

    err = akita_config_load(&g_runtime_config);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND && err != ESP_ERR_INVALID_VERSION) {
        ESP_LOGW(TAG, "Config load failed: %s", esp_err_to_name(err));
    }

    if (g_runtime_config.schema_version != AKITA_CONFIG_SCHEMA_VERSION) {
        akita_board_apply_defaults(&g_runtime_config);
        ESP_ERROR_CHECK(akita_config_save(&g_runtime_config));
    }

    ESP_LOGI(TAG, "Board profile: %s", akita_board_get_name(g_runtime_config.board_profile));
    akita_status_led_init();

    if (g_runtime_config.enable_config_ap) {
        err = akita_config_ui_start(&g_runtime_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Config UI start failed: %s", esp_err_to_name(err));
        }
    }

    err = akita_gps_init(&g_runtime_config);
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "GPS init failed: %s", esp_err_to_name(err));
    }

    err = akita_obd_init(&g_runtime_config);
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "OBD init failed: %s", esp_err_to_name(err));
    }

    err = akita_transport_init(&g_runtime_config);
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Transport init failed: %s", esp_err_to_name(err));
    }

    if (xTaskCreate(akita_main_task, "akita_main_task", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

const akita_runtime_config_t *akita_app_get_config(void) {
    return &g_runtime_config;
}

const akita_vehicle_telemetry_t *akita_app_get_telemetry(void) {
    return &g_telemetry;
}