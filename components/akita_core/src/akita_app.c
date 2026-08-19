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
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "akita_app";
static akita_runtime_config_t g_runtime_config;
static akita_vehicle_telemetry_t g_telemetry;
static bool g_led_ready;
static uint64_t g_led_off_at_ms;

static void akita_status_led_init(void) {
    if (g_runtime_config.status_led_pin < 0) {
        g_led_ready = false;
        return;
    }

    gpio_reset_pin((gpio_num_t) g_runtime_config.status_led_pin);
    gpio_set_direction((gpio_num_t) g_runtime_config.status_led_pin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t) g_runtime_config.status_led_pin, 0);
    g_led_ready = true;
    g_led_off_at_ms = 0;
}

static void akita_status_led_pulse(void) {
    if (!g_led_ready) {
        return;
    }

    gpio_set_level((gpio_num_t) g_runtime_config.status_led_pin, 1);
    g_led_off_at_ms = (uint64_t) (esp_timer_get_time() / 1000ULL) + 40ULL;
}

static void akita_status_led_service(uint64_t now_ms) {
    if (!g_led_ready || g_led_off_at_ms == 0U) {
        return;
    }

    if (now_ms >= g_led_off_at_ms) {
        gpio_set_level((gpio_num_t) g_runtime_config.status_led_pin, 0);
        g_led_off_at_ms = 0;
    }
}

static esp_err_t akita_apply_runtime_config(const akita_runtime_config_t *config, void *context) {
    esp_err_t err;
    esp_err_t result = ESP_OK;

    (void) context;
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    akita_config_lock();
    g_runtime_config = *config;
    akita_config_sanitize(&g_runtime_config);
    akita_config_unlock();
    akita_status_led_init();

    err = akita_gps_init(&g_runtime_config);
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED && result == ESP_OK) {
        result = err;
    }

    err = akita_obd_init(&g_runtime_config);
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED && result == ESP_OK) {
        result = err;
    }

    err = akita_transport_init(&g_runtime_config);
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED && result == ESP_OK) {
        result = err;
    }

    return result;
}

static void akita_refresh_system_snapshot(const akita_runtime_config_t *config) {
    akita_transport_status_t transport_status = {0};

    akita_transport_get_status(&transport_status);
    g_telemetry.system.config_portal_ready = akita_config_ui_is_running();
    g_telemetry.system.transport_ready = transport_status.transport_ready;
    g_telemetry.system.wifi_ready = transport_status.wifi_connected || g_telemetry.system.config_portal_ready;
    g_telemetry.system.lora_ready = transport_status.lora_ready;
    g_telemetry.system.wifi_rssi = transport_status.wifi_rssi;
    g_telemetry.system.free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    (void) config;
}

static void akita_main_task(void *arg) {
    char payload[768];
    uint64_t last_publish_ms = 0;
    bool watchdog_attached = false;
    (void) arg;

    if (esp_task_wdt_add(NULL) == ESP_OK) {
        watchdog_attached = true;
    } else {
        ESP_LOGW(TAG, "Task watchdog subscription failed; continuing without it");
    }

    while (true) {
        uint64_t now_ms = (uint64_t) (esp_timer_get_time() / 1000ULL);
        akita_runtime_config_t config;
        esp_err_t publish_status;
        size_t payload_len;

        if (watchdog_attached) {
            esp_task_wdt_reset();
        }

        akita_config_lock();
        config = g_runtime_config;
        akita_config_unlock();

        akita_gps_poll(&g_telemetry.gps);
        akita_obd_poll(&g_telemetry.obd);
        akita_transport_poll(&config);
        akita_refresh_system_snapshot(&config);
        akita_status_led_service(now_ms);

        if ((now_ms - last_publish_ms) >= config.telemetry_interval_ms) {
            if (config.transport_mode == AKITA_TRANSPORT_LORA) {
                payload_len = akita_payload_write_compact_json(&config, &g_telemetry, payload, 256);
            } else {
                payload_len = akita_payload_write_json(&config, &g_telemetry, payload, sizeof(payload));
            }

            if (payload_len > 0) {
                publish_status = akita_transport_publish(&config, payload);
                if (publish_status != ESP_OK) {
                    ESP_LOGW(
                        TAG,
                        "Telemetry publish failed (%s); keeping a local copy",
                        esp_err_to_name(publish_status)
                    );
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

    akita_config_sanitize(&g_runtime_config);
    if (g_runtime_config.schema_version != AKITA_CONFIG_SCHEMA_VERSION) {
        akita_board_apply_defaults(&g_runtime_config);
    }

    err = akita_config_save(&g_runtime_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to persist runtime configuration: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Board profile: %s", akita_board_get_name(g_runtime_config.board_profile));
    akita_status_led_init();

    if (g_runtime_config.enable_config_ap) {
        akita_config_ui_set_apply_callback(akita_apply_runtime_config, NULL);
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

    if (xTaskCreate(akita_main_task, "akita_main_task", 8192, NULL, 5, NULL) != pdPASS) {
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
