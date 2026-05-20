#include "akita_obd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "akita_obd";
static bool g_obd_ready;
static akita_obd_snapshot_t g_obd_state;
static uint64_t g_last_sample_ms;

static uint8_t akita_hex_u8(const char *text) {
    char scratch[3] = { text[0], text[1], '\0' };
    return (uint8_t) strtoul(scratch, NULL, 16);
}

esp_err_t akita_obd_init(const akita_runtime_config_t *config) {
    (void) config;
    memset(&g_obd_state, 0, sizeof(g_obd_state));
    g_obd_ready = false;
    ESP_LOGW(TAG, "Native BLE OBD client is not wired yet. Parser and request builder are ready for ESP-IDF transport integration.");
    return ESP_ERR_NOT_SUPPORTED;
}

void akita_obd_poll(akita_obd_snapshot_t *snapshot) {
    uint64_t now_ms = (uint64_t) (esp_timer_get_time() / 1000ULL);

    if (snapshot == NULL) {
        return;
    }

    if (g_last_sample_ms > 0) {
        g_obd_state.age_ms = (uint32_t) (now_ms - g_last_sample_ms);
    }

    *snapshot = g_obd_state;
}

size_t akita_obd_build_request(const char *pid, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0 || pid == NULL) {
        return 0;
    }

    return (size_t) snprintf(buffer, buffer_size, "%s\r", pid);
}

bool akita_obd_apply_response(akita_obd_snapshot_t *snapshot, const char *response) {
    char cleaned[32];
    size_t in_index = 0;
    size_t out_index = 0;

    if (snapshot == NULL || response == NULL) {
        return false;
    }

    while (response[in_index] != '\0' && out_index < (sizeof(cleaned) - 1)) {
        if (response[in_index] != ' ' && response[in_index] != '>' && response[in_index] != '\r' && response[in_index] != '\n') {
            cleaned[out_index++] = response[in_index];
        }
        ++in_index;
    }
    cleaned[out_index] = '\0';

    if (strncmp(cleaned, "410C", 4) == 0 && strlen(cleaned) >= 8) {
        snapshot->rpm = (float) (((akita_hex_u8(cleaned + 4) * 256U) + akita_hex_u8(cleaned + 6)) / 4.0f);
    } else if (strncmp(cleaned, "410D", 4) == 0 && strlen(cleaned) >= 6) {
        snapshot->speed_kmh = (float) akita_hex_u8(cleaned + 4);
    } else if (strncmp(cleaned, "4105", 4) == 0 && strlen(cleaned) >= 6) {
        snapshot->coolant_c = (float) akita_hex_u8(cleaned + 4) - 40.0f;
    } else {
        return false;
    }

    snapshot->connected = g_obd_ready;
    g_obd_state = *snapshot;
    g_last_sample_ms = (uint64_t) (esp_timer_get_time() / 1000ULL);
    return true;
}