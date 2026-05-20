#include "akita_gps.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "akita_gps";
static bool g_gps_ready;
static uart_port_t g_uart_port;
static char g_sentence[128];
static size_t g_sentence_len;
static akita_gps_snapshot_t g_latest_fix;
static uint64_t g_last_fix_ms;

static float akita_nmea_to_decimal(const char *text, char hemisphere) {
    double raw = atof(text);
    int degrees = (int) (raw / 100.0);
    double minutes = raw - ((double) degrees * 100.0);
    double decimal = (double) degrees + (minutes / 60.0);

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

    if (count < 10) {
        return;
    }

    if (tokens[6][0] == '0') {
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

    if (count < 8) {
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
    if (strncmp(sentence, "$GPGGA", 6) == 0 || strncmp(sentence, "$GNGGA", 6) == 0) {
        akita_gps_parse_gga(sentence);
    } else if (strncmp(sentence, "$GPRMC", 6) == 0 || strncmp(sentence, "$GNRMC", 6) == 0) {
        akita_gps_parse_rmc(sentence);
    }
}

esp_err_t akita_gps_init(const akita_runtime_config_t *config) {
    uart_config_t uart_config;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!config->enable_gps || config->gps_rx_pin < 0 || config->gps_tx_pin < 0) {
        g_gps_ready = false;
        return ESP_ERR_NOT_SUPPORTED;
    }

    g_uart_port = (uart_port_t) config->gps_uart_port;
    uart_config.baud_rate = (int) config->gps_uart_baud;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.rx_flow_ctrl_thresh = 0;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(uart_driver_install(g_uart_port, 2048, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(g_uart_port, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(g_uart_port, config->gps_tx_pin, config->gps_rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    g_gps_ready = true;
    ESP_LOGI(TAG, "GPS UART ready on port %ld RX=%ld TX=%ld", (long) config->gps_uart_port, (long) config->gps_rx_pin, (long) config->gps_tx_pin);
    return ESP_OK;
}

void akita_gps_poll(akita_gps_snapshot_t *snapshot) {
    uint8_t rx_buffer[64];
    int bytes_read;
    int index;
    uint64_t now_ms;

    if (snapshot == NULL) {
        return;
    }

    if (!g_gps_ready) {
        snapshot->fix = false;
        return;
    }

    bytes_read = uart_read_bytes(g_uart_port, rx_buffer, sizeof(rx_buffer), 0);
    for (index = 0; index < bytes_read; ++index) {
        char current = (char) rx_buffer[index];
        if (current == '\n') {
            g_sentence[g_sentence_len] = '\0';
            if (g_sentence_len > 6) {
                akita_gps_process_sentence(g_sentence);
            }
            g_sentence_len = 0;
        } else if (current != '\r' && g_sentence_len < (sizeof(g_sentence) - 1)) {
            g_sentence[g_sentence_len++] = current;
        }
    }

    now_ms = (uint64_t) (esp_timer_get_time() / 1000ULL);
    if (g_latest_fix.fix) {
        g_latest_fix.age_ms = (uint32_t) (now_ms - g_last_fix_ms);
    }

    *snapshot = g_latest_fix;
}