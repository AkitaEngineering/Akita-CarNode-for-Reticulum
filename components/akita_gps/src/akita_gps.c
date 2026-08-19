#include "akita_gps.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "akita_gps";
static bool g_gps_ready;
static bool g_uart_driver_ready;
static bool g_sentence_overflow;
static uart_port_t g_uart_port;
static char g_sentence[128];
static size_t g_sentence_len;
static akita_gps_snapshot_t g_latest_fix;
static uint64_t g_last_fix_ms;
static SemaphoreHandle_t g_gps_lock;

static esp_err_t akita_gps_ensure_lock(void) {
    if (g_gps_lock == NULL) {
        g_gps_lock = xSemaphoreCreateMutex();
        if (g_gps_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

static void akita_gps_reset_parser_state(void) {
    memset(&g_latest_fix, 0, sizeof(g_latest_fix));
    memset(g_sentence, 0, sizeof(g_sentence));
    g_sentence_len = 0;
    g_sentence_overflow = false;
    g_last_fix_ms = 0;
}

static void akita_gps_stop_locked(void) {
    esp_err_t err;

    g_gps_ready = false;
    if (g_uart_driver_ready) {
        err = uart_driver_delete(g_uart_port);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "GPS UART driver delete returned %s", esp_err_to_name(err));
        }
        g_uart_driver_ready = false;
    }

    akita_gps_reset_parser_state();
}

static bool akita_nmea_checksum_ok(const char *sentence) {
    const char *star;
    unsigned expected;
    unsigned calculated = 0;
    const char *cursor;

    if (sentence == NULL || sentence[0] != '$') {
        return false;
    }

    star = strrchr(sentence, '*');
    if (star == NULL || !isxdigit((unsigned char) star[1]) || !isxdigit((unsigned char) star[2])) {
        return true;
    }

    expected = (unsigned) strtoul(star + 1, NULL, 16);
    for (cursor = sentence + 1; cursor < star; ++cursor) {
        calculated ^= (unsigned char) *cursor;
    }

    return calculated == expected;
}

static bool akita_nmea_is_type(const char *sentence, const char *type) {
    return sentence != NULL &&
           type != NULL &&
           sentence[0] == '$' &&
           strlen(sentence) >= 6U &&
           strncmp(sentence + 3, type, 3) == 0;
}

static float akita_nmea_to_decimal(const char *text, char hemisphere) {
    double raw;
    int degrees;
    double minutes;
    double decimal;

    if (text == NULL || text[0] == '\0') {
        return 0.0f;
    }

    raw = atof(text);
    degrees = (int) (raw / 100.0);
    minutes = raw - ((double) degrees * 100.0);
    decimal = (double) degrees + (minutes / 60.0);

    if (hemisphere == 'S' || hemisphere == 'W') {
        decimal = -decimal;
    }

    return (float) decimal;
}

static size_t akita_split_csv(char *text, char *tokens[], size_t max_tokens) {
    size_t count = 0;
    char *cursor = text;

    if (text == NULL || tokens == NULL || max_tokens == 0) {
        return 0;
    }

    tokens[count++] = cursor;
    while (*cursor != '\0' && count < max_tokens) {
        if (*cursor == ',') {
            *cursor = '\0';
            tokens[count++] = cursor + 1;
        }
        ++cursor;
    }

    return count;
}

static void akita_gps_parse_gga(char *sentence) {
    char *tokens[16] = { 0 };
    size_t count = akita_split_csv(sentence, tokens, 16);

    if (count < 10 || tokens[2][0] == '\0' || tokens[4][0] == '\0') {
        return;
    }

    if (tokens[6][0] == '0' || tokens[6][0] == '\0') {
        g_latest_fix.fix = false;
        return;
    }

    g_latest_fix.fix = true;
    g_latest_fix.latitude = akita_nmea_to_decimal(tokens[2], tokens[3][0]);
    g_latest_fix.longitude = akita_nmea_to_decimal(tokens[4], tokens[5][0]);
    g_latest_fix.satellites = (uint8_t) atoi(tokens[7]);
    g_latest_fix.altitude_m = (float) atof(tokens[9]);
    g_last_fix_ms = (uint64_t) (esp_timer_get_time() / 1000ULL);
}

static void akita_gps_parse_rmc(char *sentence) {
    char *tokens[16] = { 0 };
    size_t count = akita_split_csv(sentence, tokens, 16);

    if (count < 8 || tokens[3][0] == '\0' || tokens[5][0] == '\0') {
        return;
    }

    if (tokens[2][0] != 'A') {
        g_latest_fix.fix = false;
        return;
    }

    g_latest_fix.fix = true;
    g_latest_fix.latitude = akita_nmea_to_decimal(tokens[3], tokens[4][0]);
    g_latest_fix.longitude = akita_nmea_to_decimal(tokens[5], tokens[6][0]);
    g_latest_fix.speed_kmh = (float) atof(tokens[7]) * 1.852f;
    g_last_fix_ms = (uint64_t) (esp_timer_get_time() / 1000ULL);
}

static void akita_gps_process_sentence(char *sentence) {
    if (!akita_nmea_checksum_ok(sentence)) {
        return;
    }

    if (akita_nmea_is_type(sentence, "GGA")) {
        akita_gps_parse_gga(sentence);
    } else if (akita_nmea_is_type(sentence, "RMC")) {
        akita_gps_parse_rmc(sentence);
    }
}

esp_err_t akita_gps_init(const akita_runtime_config_t *config) {
    uart_config_t uart_config = { 0 };
    int tx_pin;
    esp_err_t err;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = akita_gps_ensure_lock();
    if (err != ESP_OK) {
        return err;
    }

    xSemaphoreTake(g_gps_lock, portMAX_DELAY);
    akita_gps_stop_locked();

    if (!config->enable_gps || config->gps_rx_pin < 0) {
        xSemaphoreGive(g_gps_lock);
        return ESP_ERR_NOT_SUPPORTED;
    }

    g_uart_port = (uart_port_t) config->gps_uart_port;
    tx_pin = config->gps_tx_pin >= 0 ? config->gps_tx_pin : UART_PIN_NO_CHANGE;
    uart_config.baud_rate = (int) config->gps_uart_baud;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.rx_flow_ctrl_thresh = 0;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    err = uart_driver_install(g_uart_port, 2048, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        xSemaphoreGive(g_gps_lock);
        return err;
    }

    g_uart_driver_ready = true;
    err = uart_param_config(g_uart_port, &uart_config);
    if (err == ESP_OK) {
        err = uart_set_pin(g_uart_port, tx_pin, config->gps_rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }

    if (err != ESP_OK) {
        akita_gps_stop_locked();
        xSemaphoreGive(g_gps_lock);
        return err;
    }

    g_gps_ready = true;
    ESP_LOGI(
        TAG,
        "GPS UART ready on port %ld RX=%ld TX=%ld",
        (long) config->gps_uart_port,
        (long) config->gps_rx_pin,
        (long) config->gps_tx_pin
    );
    xSemaphoreGive(g_gps_lock);
    return ESP_OK;
}

void akita_gps_poll(akita_gps_snapshot_t *snapshot) {
    uint8_t rx_buffer[64];
    int bytes_read;
    int index;
    uint64_t now_ms;
    esp_err_t err;

    if (snapshot == NULL) {
        return;
    }

    err = akita_gps_ensure_lock();
    if (err != ESP_OK) {
        memset(snapshot, 0, sizeof(*snapshot));
        return;
    }

    xSemaphoreTake(g_gps_lock, portMAX_DELAY);

    if (!g_gps_ready) {
        memset(snapshot, 0, sizeof(*snapshot));
        xSemaphoreGive(g_gps_lock);
        return;
    }

    bytes_read = uart_read_bytes(g_uart_port, rx_buffer, sizeof(rx_buffer), 0);
    if (bytes_read < 0) {
        *snapshot = g_latest_fix;
        xSemaphoreGive(g_gps_lock);
        return;
    }

    for (index = 0; index < bytes_read; ++index) {
        char current = (char) rx_buffer[index];
        if (current == '\n') {
            g_sentence[g_sentence_len] = '\0';
            if (!g_sentence_overflow && g_sentence_len > 6) {
                akita_gps_process_sentence(g_sentence);
            }
            g_sentence_len = 0;
            g_sentence_overflow = false;
        } else if (current != '\r') {
            if (g_sentence_len < (sizeof(g_sentence) - 1)) {
                g_sentence[g_sentence_len++] = current;
            } else {
                g_sentence_overflow = true;
            }
        }
    }

    now_ms = (uint64_t) (esp_timer_get_time() / 1000ULL);
    if (g_latest_fix.fix && g_last_fix_ms > 0U) {
        g_latest_fix.age_ms = (uint32_t) (now_ms - g_last_fix_ms);
    }

    *snapshot = g_latest_fix;
    xSemaphoreGive(g_gps_lock);
}
