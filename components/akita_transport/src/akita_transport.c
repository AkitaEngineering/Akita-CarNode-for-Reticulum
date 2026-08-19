#include "akita_transport.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#if __has_include("esp_crt_bundle.h")
#include "esp_crt_bundle.h"
#define AKITA_TRANSPORT_HAS_CRT_BUNDLE 1
#endif
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#define AKITA_TRANSPORT_WIFI_CONNECTED_BIT BIT0
#define AKITA_TRANSPORT_HTTP_TIMEOUT_MS 8000
#define AKITA_TRANSPORT_UDP_HOST_MAX_LEN 80
#define AKITA_TRANSPORT_UDP_PORT_MAX_LEN 8
#define AKITA_TRANSPORT_BRIDGE_MODE_MAX_LEN 16
#define AKITA_TRANSPORT_BRIDGE_ERROR_MAX_LEN 64
#define AKITA_TRANSPORT_RNS_PROTOCOL "akita-rns-udp-v2"
#define AKITA_TRANSPORT_RNS_RESPONSE_MAX_LEN 256
#define AKITA_TRANSPORT_RNS_TIMEOUT_MS 12000
#define AKITA_TRANSPORT_RNS_PING_INTERVAL_MS 5000U
#define AKITA_TRANSPORT_WIFI_RETRY_MIN_MS 1000U
#define AKITA_TRANSPORT_WIFI_RETRY_MAX_MS 30000U
#define AKITA_TRANSPORT_LORA_SPI_HOST SPI2_HOST
#define AKITA_TRANSPORT_LORA_SPI_CLOCK_HZ (1000 * 1000)
#define AKITA_TRANSPORT_LORA_TX_TIMEOUT_MS 5000
#define AKITA_TRANSPORT_LORA_MAX_PAYLOAD_LEN 255U
#define AKITA_TRANSPORT_LORA_FREQUENCY_STEP_HZ 61.03515625

#define AKITA_LORA_REG_FIFO 0x00
#define AKITA_LORA_REG_OP_MODE 0x01
#define AKITA_LORA_REG_FRF_MSB 0x06
#define AKITA_LORA_REG_FRF_MID 0x07
#define AKITA_LORA_REG_FRF_LSB 0x08
#define AKITA_LORA_REG_PA_CONFIG 0x09
#define AKITA_LORA_REG_OCP 0x0B
#define AKITA_LORA_REG_LNA 0x0C
#define AKITA_LORA_REG_FIFO_ADDR_PTR 0x0D
#define AKITA_LORA_REG_FIFO_TX_BASE_ADDR 0x0E
#define AKITA_LORA_REG_FIFO_RX_BASE_ADDR 0x0F
#define AKITA_LORA_REG_FIFO_RX_CURRENT_ADDR 0x10
#define AKITA_LORA_REG_IRQ_FLAGS 0x12
#define AKITA_LORA_REG_RX_NB_BYTES 0x13
#define AKITA_LORA_REG_MODEM_CONFIG_1 0x1D
#define AKITA_LORA_REG_MODEM_CONFIG_2 0x1E
#define AKITA_LORA_REG_PREAMBLE_MSB 0x20
#define AKITA_LORA_REG_PREAMBLE_LSB 0x21
#define AKITA_LORA_REG_PAYLOAD_LENGTH 0x22
#define AKITA_LORA_REG_MODEM_CONFIG_3 0x26
#define AKITA_LORA_REG_DETECTION_OPTIMIZE 0x31
#define AKITA_LORA_REG_DETECTION_THRESHOLD 0x37
#define AKITA_LORA_REG_SYNC_WORD 0x39
#define AKITA_LORA_REG_DIO_MAPPING_1 0x40
#define AKITA_LORA_REG_VERSION 0x42
#define AKITA_LORA_REG_PA_DAC 0x4D

#define AKITA_LORA_MODE_LONG_RANGE 0x80
#define AKITA_LORA_MODE_SLEEP 0x00
#define AKITA_LORA_MODE_STDBY 0x01
#define AKITA_LORA_MODE_TX 0x03
#define AKITA_LORA_MODE_RX_CONTINUOUS 0x05

#define AKITA_LORA_PA_BOOST 0x80
#define AKITA_LORA_PA_DAC_ENABLE 0x07
#define AKITA_LORA_PA_DAC_DISABLE 0x04
#define AKITA_LORA_IRQ_TX_DONE 0x08
#define AKITA_LORA_IRQ_RX_DONE 0x40
#define AKITA_LORA_IRQ_PAYLOAD_CRC_ERROR 0x20
#define AKITA_LORA_EXPECTED_VERSION 0x12

static const char *TAG = "akita_transport";
typedef enum {
    AKITA_TRANSPORT_ENDPOINT_NONE = 0,
    AKITA_TRANSPORT_ENDPOINT_HTTP,
    AKITA_TRANSPORT_ENDPOINT_UDP,
    AKITA_TRANSPORT_ENDPOINT_RNS_UDP,
    AKITA_TRANSPORT_ENDPOINT_LORA,
} akita_transport_endpoint_t;

static EventGroupHandle_t g_wifi_event_group;
static esp_event_handler_instance_t g_wifi_event_handler;
static esp_event_handler_instance_t g_ip_event_handler;
static esp_netif_t *g_sta_netif;
static spi_device_handle_t g_lora_spi;
static bool g_event_handlers_registered;
static bool g_lora_spi_bus_initialized;
static bool g_lora_ready;
static bool g_rns_bridge_ready;
static bool g_wifi_connected;
static bool g_transport_ready;
static bool g_wifi_transport_enabled;
static uint32_t g_rns_bridge_sequence;
static akita_transport_mode_t g_transport_mode;
static akita_transport_endpoint_t g_endpoint_type;
static char g_rns_bridge_mode[AKITA_TRANSPORT_BRIDGE_MODE_MAX_LEN] = "inactive";
static char g_rns_bridge_last_error[AKITA_TRANSPORT_BRIDGE_ERROR_MAX_LEN];
static SemaphoreHandle_t g_transport_lock;
static uint32_t g_wifi_backoff_ms = AKITA_TRANSPORT_WIFI_RETRY_MIN_MS;
static uint64_t g_wifi_retry_at_ms;
static uint64_t g_rns_next_ping_ms;
static int8_t g_wifi_rssi;

static void akita_transport_copy_string(char *destination, size_t destination_size, const char *source) {
    if (destination == NULL || destination_size == 0U) {
        return;
    }

    snprintf(destination, destination_size, "%s", source != NULL ? source : "");
}

static uint64_t akita_transport_now_ms(void) {
    return (uint64_t) (esp_timer_get_time() / 1000ULL);
}

static void akita_transport_lock(void) {
    if (g_transport_lock == NULL) {
        g_transport_lock = xSemaphoreCreateMutex();
    }
    if (g_transport_lock != NULL) {
        xSemaphoreTake(g_transport_lock, portMAX_DELAY);
    }
}

static void akita_transport_unlock(void) {
    if (g_transport_lock != NULL) {
        xSemaphoreGive(g_transport_lock);
    }
}

static void akita_transport_update_ready_state(void) {
    switch (g_transport_mode) {
        case AKITA_TRANSPORT_WIFI:
            if (g_endpoint_type == AKITA_TRANSPORT_ENDPOINT_RNS_UDP) {
                g_transport_ready = g_wifi_transport_enabled &&
                                    g_wifi_connected &&
                                    g_rns_bridge_ready;
            } else {
                g_transport_ready = g_wifi_transport_enabled &&
                                    g_wifi_connected &&
                                    g_endpoint_type != AKITA_TRANSPORT_ENDPOINT_NONE &&
                                    g_endpoint_type != AKITA_TRANSPORT_ENDPOINT_LORA;
            }
            break;

        case AKITA_TRANSPORT_LORA:
            g_transport_ready = g_lora_ready;
            break;

        default:
            g_transport_ready = false;
            break;
    }
}

static void akita_transport_disable_wifi_uplink(void) {
    esp_err_t err;

    g_rns_bridge_ready = false;
    akita_transport_copy_string(g_rns_bridge_mode, sizeof(g_rns_bridge_mode), "inactive");
    g_rns_bridge_last_error[0] = '\0';
    g_wifi_transport_enabled = false;
    g_wifi_connected = false;
    if (g_wifi_event_group != NULL) {
        xEventGroupClearBits(g_wifi_event_group, AKITA_TRANSPORT_WIFI_CONNECTED_BIT);
    }
    akita_transport_update_ready_state();

    err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED && err != ESP_ERR_WIFI_MODE) {
        ESP_LOGW(TAG, "WiFi uplink disconnect failed: %s", esp_err_to_name(err));
    }
}

static void akita_transport_set_rns_bridge_state(bool ready, const char *mode, const char *last_error) {
    g_rns_bridge_ready = ready;
    akita_transport_copy_string(g_rns_bridge_mode, sizeof(g_rns_bridge_mode), mode);
    akita_transport_copy_string(g_rns_bridge_last_error, sizeof(g_rns_bridge_last_error), last_error);
    akita_transport_update_ready_state();
}

static bool akita_transport_json_extract_string(
    const char *json,
    const char *key,
    char *output,
    size_t output_size
) {
    char pattern[32];
    const char *cursor;
    size_t used = 0;

    if (json == NULL || key == NULL || output == NULL || output_size == 0U) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    cursor = strstr(json, pattern);
    if (cursor == NULL) {
        output[0] = '\0';
        return false;
    }

    cursor += strlen(pattern);
    while (*cursor != '\0' && *cursor != '"' && (used + 1U) < output_size) {
        if (*cursor == '\\' && cursor[1] != '\0') {
            ++cursor;
        }

        output[used++] = *cursor++;
    }

    output[used] = '\0';
    return used > 0U;
}

static bool akita_transport_pin_is_valid(int32_t pin) {
    return pin >= 0 && pin < GPIO_NUM_MAX;
}

static void akita_transport_disable_lora_uplink(void) {
    esp_err_t err;

    g_lora_ready = false;
    if (g_endpoint_type == AKITA_TRANSPORT_ENDPOINT_LORA) {
        g_endpoint_type = AKITA_TRANSPORT_ENDPOINT_NONE;
    }
    akita_transport_update_ready_state();

    if (g_lora_spi != NULL) {
        err = spi_bus_remove_device(g_lora_spi);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "LoRa SPI device removal failed: %s", esp_err_to_name(err));
        }
        g_lora_spi = NULL;
    }

    if (g_lora_spi_bus_initialized) {
        err = spi_bus_free(AKITA_TRANSPORT_LORA_SPI_HOST);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "LoRa SPI bus free failed: %s", esp_err_to_name(err));
        }
        g_lora_spi_bus_initialized = false;
    }
}

static esp_err_t akita_transport_lora_transfer(
    const uint8_t *tx_data,
    uint8_t *rx_data,
    size_t byte_count
) {
    spi_transaction_t transaction = {0};

    if (g_lora_spi == NULL || tx_data == NULL || byte_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (byte_count <= 4U) {
        size_t index;
        transaction.flags = SPI_TRANS_USE_TXDATA;
        if (rx_data != NULL) {
            transaction.flags |= SPI_TRANS_USE_RXDATA;
        }
        transaction.length = byte_count * 8U;
        for (index = 0; index < byte_count; ++index) {
            transaction.tx_data[index] = tx_data[index];
        }
        esp_err_t err = spi_device_polling_transmit(g_lora_spi, &transaction);
        if (err == ESP_OK && rx_data != NULL) {
            for (index = 0; index < byte_count; ++index) {
                rx_data[index] = transaction.rx_data[index];
            }
        }
        return err;
    }

    transaction.length = byte_count * 8U;
    transaction.tx_buffer = tx_data;
    transaction.rx_buffer = rx_data;
    return spi_device_polling_transmit(g_lora_spi, &transaction);
}

static esp_err_t akita_transport_lora_write_register(uint8_t address, uint8_t value) {
    const uint8_t tx_data[2] = { (uint8_t) (address | 0x80U), value };
    return akita_transport_lora_transfer(tx_data, NULL, sizeof(tx_data));
}

static esp_err_t akita_transport_lora_read_register(uint8_t address, uint8_t *value) {
    uint8_t tx_data[2] = { (uint8_t) (address & 0x7FU), 0x00 };
    uint8_t rx_data[2] = {0};
    esp_err_t err;

    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = akita_transport_lora_transfer(tx_data, rx_data, sizeof(tx_data));
    if (err != ESP_OK) {
        return err;
    }

    *value = rx_data[1];
    return ESP_OK;
}

static esp_err_t akita_transport_lora_write_fifo(const uint8_t *payload, size_t payload_len) {
    uint8_t *buffer;
    esp_err_t err;

    if (payload == NULL || payload_len == 0U || payload_len > AKITA_TRANSPORT_LORA_MAX_PAYLOAD_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    buffer = heap_caps_malloc(payload_len + 1U, MALLOC_CAP_DMA);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    buffer[0] = (uint8_t) (AKITA_LORA_REG_FIFO | 0x80U);
    memcpy(buffer + 1U, payload, payload_len);
    err = akita_transport_lora_transfer(buffer, NULL, payload_len + 1U);
    heap_caps_free(buffer);
    return err;
}

static esp_err_t akita_transport_lora_read_fifo(uint8_t *payload, size_t payload_len) {
    uint8_t *tx_buffer;
    uint8_t *rx_buffer;
    esp_err_t err;

    if (payload == NULL || payload_len == 0U || payload_len > AKITA_TRANSPORT_LORA_MAX_PAYLOAD_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    tx_buffer = heap_caps_calloc(payload_len + 1U, 1U, MALLOC_CAP_DMA);
    rx_buffer = heap_caps_calloc(payload_len + 1U, 1U, MALLOC_CAP_DMA);
    if (tx_buffer == NULL || rx_buffer == NULL) {
        heap_caps_free(tx_buffer);
        heap_caps_free(rx_buffer);
        return ESP_ERR_NO_MEM;
    }

    tx_buffer[0] = (uint8_t) (AKITA_LORA_REG_FIFO & 0x7FU);
    err = akita_transport_lora_transfer(tx_buffer, rx_buffer, payload_len + 1U);
    if (err == ESP_OK) {
        memcpy(payload, rx_buffer + 1U, payload_len);
    }

    heap_caps_free(tx_buffer);
    heap_caps_free(rx_buffer);
    return err;
}

static esp_err_t akita_transport_lora_enter_rx(void) {
    ESP_RETURN_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_IRQ_FLAGS, 0xFF), TAG, "LoRa IRQ clear before RX failed");
    ESP_RETURN_ON_ERROR(
        akita_transport_lora_write_register(AKITA_LORA_REG_OP_MODE, AKITA_LORA_MODE_LONG_RANGE | AKITA_LORA_MODE_RX_CONTINUOUS),
        TAG,
        "LoRa RX continuous mode failed"
    );
    return ESP_OK;
}

static void akita_transport_lora_harvest_rx(void) {
    uint8_t irq_flags = 0;
    uint8_t current_addr = 0;
    uint8_t byte_count = 0;
    uint8_t payload[AKITA_TRANSPORT_LORA_MAX_PAYLOAD_LEN + 1U];
    esp_err_t err;

    if (!g_lora_ready || g_lora_spi == NULL) {
        return;
    }

    err = akita_transport_lora_read_register(AKITA_LORA_REG_IRQ_FLAGS, &irq_flags);
    if (err != ESP_OK) {
        return;
    }

    if ((irq_flags & AKITA_LORA_IRQ_PAYLOAD_CRC_ERROR) != 0U) {
        ESP_LOGW(TAG, "Dropping LoRa frame with CRC error");
        (void) akita_transport_lora_write_register(AKITA_LORA_REG_IRQ_FLAGS, irq_flags);
        return;
    }

    if ((irq_flags & AKITA_LORA_IRQ_RX_DONE) == 0U) {
        return;
    }

    err = akita_transport_lora_read_register(AKITA_LORA_REG_FIFO_RX_CURRENT_ADDR, &current_addr);
    if (err == ESP_OK) {
        err = akita_transport_lora_read_register(AKITA_LORA_REG_RX_NB_BYTES, &byte_count);
    }
    if (err != ESP_OK || byte_count == 0U) {
        (void) akita_transport_lora_write_register(AKITA_LORA_REG_IRQ_FLAGS, irq_flags);
        return;
    }

    err = akita_transport_lora_write_register(AKITA_LORA_REG_FIFO_ADDR_PTR, current_addr);
    if (err == ESP_OK) {
        err = akita_transport_lora_read_fifo(payload, byte_count);
    }
    (void) akita_transport_lora_write_register(AKITA_LORA_REG_IRQ_FLAGS, irq_flags);
    if (err != ESP_OK) {
        return;
    }

    payload[byte_count] = '\0';
    ESP_LOGI(TAG, "LoRa received %u bytes: %s", (unsigned) byte_count, (const char *) payload);
}

static esp_err_t akita_transport_lora_reset(const akita_runtime_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!akita_transport_pin_is_valid(config->lora_reset_pin)) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(gpio_reset_pin((gpio_num_t) config->lora_reset_pin), TAG, "LoRa reset pin reset failed");
    ESP_RETURN_ON_ERROR(gpio_set_direction((gpio_num_t) config->lora_reset_pin, GPIO_MODE_OUTPUT), TAG, "LoRa reset pin direction failed");
    ESP_RETURN_ON_ERROR(gpio_set_level((gpio_num_t) config->lora_reset_pin, 0), TAG, "LoRa reset pin assert failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level((gpio_num_t) config->lora_reset_pin, 1), TAG, "LoRa reset pin release failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

static esp_err_t akita_transport_lora_set_frequency(uint32_t frequency_hz) {
    uint32_t frf_value;

    if (frequency_hz == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    frf_value = (uint32_t) (((double) frequency_hz) / AKITA_TRANSPORT_LORA_FREQUENCY_STEP_HZ);
    ESP_RETURN_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_FRF_MSB, (uint8_t) (frf_value >> 16)), TAG, "LoRa FRF MSB write failed");
    ESP_RETURN_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_FRF_MID, (uint8_t) (frf_value >> 8)), TAG, "LoRa FRF MID write failed");
    ESP_RETURN_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_FRF_LSB, (uint8_t) frf_value), TAG, "LoRa FRF LSB write failed");
    return ESP_OK;
}

static esp_err_t akita_transport_configure_lora(const akita_runtime_config_t *config) {
    spi_bus_config_t bus_config = {0};
    spi_device_interface_config_t device_config = {0};
    uint8_t version = 0;
    esp_err_t err;
    esp_err_t ret = ESP_OK;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    akita_transport_disable_lora_uplink();

    if (config->transport_mode != AKITA_TRANSPORT_LORA) {
        return ESP_OK;
    }

    if (!akita_transport_pin_is_valid(config->lora_sck_pin) ||
        !akita_transport_pin_is_valid(config->lora_miso_pin) ||
        !akita_transport_pin_is_valid(config->lora_mosi_pin) ||
        !akita_transport_pin_is_valid(config->lora_cs_pin) ||
        config->lora_frequency_hz == 0U) {
        ESP_LOGW(TAG, "LoRa transport selected, but pins or frequency are not configured");
        return ESP_ERR_INVALID_ARG;
    }

    bus_config.mosi_io_num = config->lora_mosi_pin;
    bus_config.miso_io_num = config->lora_miso_pin;
    bus_config.sclk_io_num = config->lora_sck_pin;
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.max_transfer_sz = AKITA_TRANSPORT_LORA_MAX_PAYLOAD_LEN + 1U;

    device_config.clock_speed_hz = AKITA_TRANSPORT_LORA_SPI_CLOCK_HZ;
    device_config.mode = 0;
    device_config.spics_io_num = config->lora_cs_pin;
    device_config.queue_size = 1;

    err = spi_bus_initialize(AKITA_TRANSPORT_LORA_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }
    g_lora_spi_bus_initialized = true;

    err = spi_bus_add_device(AKITA_TRANSPORT_LORA_SPI_HOST, &device_config, &g_lora_spi);
    if (err != ESP_OK) {
        akita_transport_disable_lora_uplink();
        return err;
    }

    err = akita_transport_lora_reset(config);
    if (err != ESP_OK) {
        akita_transport_disable_lora_uplink();
        return err;
    }

    err = akita_transport_lora_read_register(AKITA_LORA_REG_VERSION, &version);
    if (err != ESP_OK) {
        akita_transport_disable_lora_uplink();
        return err;
    }

    if (version == 0x00U || version == 0xFFU) {
        ESP_LOGE(TAG, "No SX127x radio detected (version 0x%02x)", version);
        akita_transport_disable_lora_uplink();
        return ESP_ERR_NOT_FOUND;
    }

    if (version != AKITA_LORA_EXPECTED_VERSION) {
        ESP_LOGW(TAG, "Unexpected SX127x version 0x%02x while configuring LoRa transport", version);
    }

    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_OP_MODE, AKITA_LORA_MODE_LONG_RANGE | AKITA_LORA_MODE_SLEEP), fail, TAG, "LoRa sleep mode failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_FIFO_TX_BASE_ADDR, 0x00), fail, TAG, "LoRa TX base setup failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_FIFO_RX_BASE_ADDR, 0x00), fail, TAG, "LoRa RX base setup failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_set_frequency(config->lora_frequency_hz), fail, TAG, "LoRa frequency setup failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_PA_CONFIG, AKITA_LORA_PA_BOOST | 0x0F), fail, TAG, "LoRa PA config failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_OCP, 0x2B), fail, TAG, "LoRa OCP config failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_LNA, 0x23), fail, TAG, "LoRa LNA config failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_MODEM_CONFIG_1, 0x72), fail, TAG, "LoRa modem config1 failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_MODEM_CONFIG_2, 0x74), fail, TAG, "LoRa modem config2 failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_MODEM_CONFIG_3, 0x04), fail, TAG, "LoRa modem config3 failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_PREAMBLE_MSB, 0x00), fail, TAG, "LoRa preamble MSB failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_PREAMBLE_LSB, 0x08), fail, TAG, "LoRa preamble LSB failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_DETECTION_OPTIMIZE, 0xC3), fail, TAG, "LoRa detect optimize failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_DETECTION_THRESHOLD, 0x0A), fail, TAG, "LoRa detect threshold failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_SYNC_WORD, 0x12), fail, TAG, "LoRa sync word failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_DIO_MAPPING_1, 0x00), fail, TAG, "LoRa DIO mapping failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_PA_DAC, AKITA_LORA_PA_DAC_DISABLE), fail, TAG, "LoRa PA DAC failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_IRQ_FLAGS, 0xFF), fail, TAG, "LoRa IRQ clear failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_OP_MODE, AKITA_LORA_MODE_LONG_RANGE | AKITA_LORA_MODE_STDBY), fail, TAG, "LoRa standby mode failed");
    ESP_GOTO_ON_ERROR(akita_transport_lora_enter_rx(), fail, TAG, "LoRa RX mode failed");

    g_endpoint_type = AKITA_TRANSPORT_ENDPOINT_LORA;
    g_lora_ready = true;
    akita_transport_update_ready_state();
    ESP_LOGI(TAG, "LoRa transport configured at %lu Hz", (unsigned long) config->lora_frequency_hz);
    return ESP_OK;

fail:
    akita_transport_disable_lora_uplink();
    return ret;
}

static esp_err_t akita_transport_publish_lora(const char *payload) {
    size_t payload_len;
    int64_t deadline_us;
    uint8_t irq_flags = 0;
    esp_err_t err;

    if (payload == NULL || payload[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_lora_ready || g_lora_spi == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    payload_len = strlen(payload);
    if (payload_len > AKITA_TRANSPORT_LORA_MAX_PAYLOAD_LEN) {
        ESP_LOGW(TAG, "LoRa payload is %u bytes, exceeding the SX127x maximum of %u bytes", (unsigned) payload_len, (unsigned) AKITA_TRANSPORT_LORA_MAX_PAYLOAD_LEN);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_RETURN_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_OP_MODE, AKITA_LORA_MODE_LONG_RANGE | AKITA_LORA_MODE_STDBY), TAG, "LoRa standby before TX failed");
    ESP_RETURN_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_IRQ_FLAGS, 0xFF), TAG, "LoRa IRQ clear before TX failed");
    ESP_RETURN_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_FIFO_ADDR_PTR, 0x00), TAG, "LoRa FIFO pointer reset failed");
    ESP_RETURN_ON_ERROR(akita_transport_lora_write_fifo((const uint8_t *) payload, payload_len), TAG, "LoRa FIFO write failed");
    ESP_RETURN_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_PAYLOAD_LENGTH, (uint8_t) payload_len), TAG, "LoRa payload length write failed");
    ESP_RETURN_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_OP_MODE, AKITA_LORA_MODE_LONG_RANGE | AKITA_LORA_MODE_TX), TAG, "LoRa TX mode failed");

    deadline_us = esp_timer_get_time() + ((int64_t) AKITA_TRANSPORT_LORA_TX_TIMEOUT_MS * 1000LL);
    while (esp_timer_get_time() < deadline_us) {
        err = akita_transport_lora_read_register(AKITA_LORA_REG_IRQ_FLAGS, &irq_flags);
        if (err != ESP_OK) {
            akita_transport_lora_write_register(AKITA_LORA_REG_OP_MODE, AKITA_LORA_MODE_LONG_RANGE | AKITA_LORA_MODE_STDBY);
            return err;
        }

        if ((irq_flags & AKITA_LORA_IRQ_TX_DONE) != 0U) {
            ESP_RETURN_ON_ERROR(akita_transport_lora_write_register(AKITA_LORA_REG_IRQ_FLAGS, irq_flags), TAG, "LoRa IRQ clear after TX failed");
            ESP_RETURN_ON_ERROR(akita_transport_lora_enter_rx(), TAG, "LoRa RX after TX failed");
            return ESP_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(akita_transport_lora_enter_rx());
    return ESP_ERR_TIMEOUT;
}

static akita_transport_endpoint_t akita_transport_endpoint_type(const char *endpoint) {
    if (endpoint == NULL || endpoint[0] == '\0') {
        return AKITA_TRANSPORT_ENDPOINT_NONE;
    }

    if (strncasecmp(endpoint, "rns+udp://", 10) == 0) {
        return AKITA_TRANSPORT_ENDPOINT_RNS_UDP;
    }

    if (strncasecmp(endpoint, "http://", 7) == 0 || strncasecmp(endpoint, "https://", 8) == 0) {
        return AKITA_TRANSPORT_ENDPOINT_HTTP;
    }

    if (strncasecmp(endpoint, "udp://", 6) == 0) {
        return AKITA_TRANSPORT_ENDPOINT_UDP;
    }

    return AKITA_TRANSPORT_ENDPOINT_NONE;
}

static void akita_transport_json_escape(const char *input, char *output, size_t output_size) {
    size_t used = 0;

    if (output == NULL || output_size == 0U) {
        return;
    }

    while (input != NULL && *input != '\0' && (used + 1U) < output_size) {
        if ((*input == '"' || *input == '\\') && (used + 2U) < output_size) {
            output[used++] = '\\';
            output[used++] = *input;
        } else if (*input == '\n' && (used + 2U) < output_size) {
            output[used++] = '\\';
            output[used++] = 'n';
        } else if (*input == '\r' && (used + 2U) < output_size) {
            output[used++] = '\\';
            output[used++] = 'r';
        } else if (*input == '\t' && (used + 2U) < output_size) {
            output[used++] = '\\';
            output[used++] = 't';
        } else {
            output[used++] = *input;
        }

        ++input;
    }

    output[used] = '\0';
}

static void akita_transport_wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
) {
    (void) arg;
    (void) event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        g_wifi_connected = false;
        if (g_wifi_event_group != NULL) {
            xEventGroupClearBits(g_wifi_event_group, AKITA_TRANSPORT_WIFI_CONNECTED_BIT);
        }
        if (g_endpoint_type == AKITA_TRANSPORT_ENDPOINT_RNS_UDP) {
            akita_transport_set_rns_bridge_state(false, "waiting", "wifi_disconnected");
        }
        akita_transport_update_ready_state();

        if (g_wifi_transport_enabled) {
            g_wifi_retry_at_ms = akita_transport_now_ms() + g_wifi_backoff_ms;
            if (g_wifi_backoff_ms < AKITA_TRANSPORT_WIFI_RETRY_MAX_MS) {
                g_wifi_backoff_ms = g_wifi_backoff_ms * 2U;
                if (g_wifi_backoff_ms > AKITA_TRANSPORT_WIFI_RETRY_MAX_MS) {
                    g_wifi_backoff_ms = AKITA_TRANSPORT_WIFI_RETRY_MAX_MS;
                }
            }
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        g_wifi_connected = true;
        g_wifi_backoff_ms = AKITA_TRANSPORT_WIFI_RETRY_MIN_MS;
        g_wifi_retry_at_ms = 0;
        g_rns_next_ping_ms = 0;
        if (g_wifi_event_group != NULL) {
            xEventGroupSetBits(g_wifi_event_group, AKITA_TRANSPORT_WIFI_CONNECTED_BIT);
        }
        akita_transport_update_ready_state();
        ESP_LOGI(TAG, "WiFi station connected for telemetry uplink");
    }
}

static esp_err_t akita_transport_register_handlers(void) {
    esp_err_t err;

    if (g_event_handlers_registered) {
        return ESP_OK;
    }

    err = esp_event_handler_instance_register(
        WIFI_EVENT,
        WIFI_EVENT_STA_DISCONNECTED,
        akita_transport_wifi_event_handler,
        NULL,
        &g_wifi_event_handler
    );
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        akita_transport_wifi_event_handler,
        NULL,
        &g_ip_event_handler
    );
    if (err != ESP_OK) {
        return err;
    }

    g_event_handlers_registered = true;
    return ESP_OK;
}

static esp_err_t akita_transport_ensure_network_base(void) {
    esp_err_t err;

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (g_wifi_event_group == NULL) {
        g_wifi_event_group = xEventGroupCreate();
        if (g_wifi_event_group == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (g_sta_netif == NULL) {
        g_sta_netif = esp_netif_create_default_wifi_sta();
        if (g_sta_netif == NULL) {
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

static esp_err_t akita_transport_configure_wifi(const akita_runtime_config_t *config) {
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t wifi_config = {0};
    wifi_mode_t current_mode = WIFI_MODE_NULL;
    wifi_mode_t target_mode;
    esp_err_t err;
    size_t ssid_length;
    size_t password_length;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    g_endpoint_type = akita_transport_endpoint_type(config->telemetry_endpoint);
    akita_transport_disable_wifi_uplink();

    if (config->transport_mode != AKITA_TRANSPORT_WIFI) {
        return ESP_OK;
    }

    if (config->wifi_ssid[0] == '\0') {
        ESP_LOGW(TAG, "WiFi transport selected, but WiFi SSID is empty");
        return ESP_OK;
    }

    ssid_length = strlen(config->wifi_ssid);
    password_length = strlen(config->wifi_password);
    if (ssid_length >= sizeof(wifi_config.sta.ssid)) {
        ESP_LOGW(TAG, "Configured WiFi SSID is too long for the ESP-IDF station driver");
        return ESP_ERR_INVALID_ARG;
    }

    if (password_length >= sizeof(wifi_config.sta.password)) {
        ESP_LOGW(TAG, "Configured WiFi password is too long for the ESP-IDF station driver");
        return ESP_ERR_INVALID_ARG;
    }

    if (g_endpoint_type == AKITA_TRANSPORT_ENDPOINT_NONE) {
        if (config->telemetry_endpoint[0] == '\0') {
            ESP_LOGW(TAG, "WiFi transport selected, but no telemetry endpoint is configured");
        } else {
            ESP_LOGW(TAG, "Unsupported telemetry endpoint scheme: %s", config->telemetry_endpoint);
        }
        return ESP_OK;
    }

    if (g_endpoint_type == AKITA_TRANSPORT_ENDPOINT_RNS_UDP) {
        akita_transport_set_rns_bridge_state(false, "waiting", "");
    }

    err = akita_transport_ensure_network_base();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_get_mode(&current_mode);
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        err = esp_wifi_init(&init_config);
        if (err != ESP_OK) {
            return err;
        }
        current_mode = WIFI_MODE_NULL;
    } else if (err != ESP_OK) {
        return err;
    }

    err = akita_transport_register_handlers();
    if (err != ESP_OK) {
        return err;
    }

    target_mode = config->enable_config_ap ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    if (current_mode != target_mode) {
        err = esp_wifi_set_mode(target_mode);
        if (err != ESP_OK) {
            return err;
        }
    }

    err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED && err != ESP_ERR_WIFI_MODE) {
        if (err != ESP_ERR_WIFI_NOT_INIT) {
            ESP_LOGW(TAG, "Existing WiFi uplink disconnect returned: %s", esp_err_to_name(err));
        }
    }

    memcpy(wifi_config.sta.ssid, config->wifi_ssid, ssid_length);
    memcpy(wifi_config.sta.password, config->wifi_password, password_length);
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
        return err;
    }

    if (g_wifi_event_group != NULL) {
        xEventGroupClearBits(g_wifi_event_group, AKITA_TRANSPORT_WIFI_CONNECTED_BIT);
    }

    err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        err = esp_wifi_start();
        if (err != ESP_OK) {
            return err;
        }
        err = esp_wifi_connect();
    }

    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
        return err;
    }

    g_wifi_transport_enabled = true;
    g_wifi_backoff_ms = AKITA_TRANSPORT_WIFI_RETRY_MIN_MS;
    g_wifi_retry_at_ms = 0;
    g_rns_next_ping_ms = 0;
    akita_transport_update_ready_state();
    ESP_LOGI(TAG, "WiFi transport configured for %s", config->telemetry_endpoint);
    return ESP_OK;
}

static esp_err_t akita_transport_publish_http(const char *endpoint, const char *payload) {
    esp_http_client_config_t client_config = {
        .url = endpoint,
        .method = HTTP_METHOD_POST,
        .timeout_ms = AKITA_TRANSPORT_HTTP_TIMEOUT_MS,
    };

#if defined(AKITA_TRANSPORT_HAS_CRT_BUNDLE)
    if (strncasecmp(endpoint, "https://", 8) == 0) {
        client_config.crt_bundle_attach = esp_crt_bundle_attach;
    }
#endif
    esp_http_client_handle_t client;
    esp_err_t err;
    int status_code;

    client = esp_http_client_init(&client_config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_http_client_set_header(client, "Content-Type", "application/json"));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_http_client_set_header(client, "User-Agent", "akita-carnode/esp-idf"));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_http_client_set_post_field(client, payload, (int) strlen(payload)));

    err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (status_code < 200 || status_code >= 300) {
        ESP_LOGW(TAG, "HTTP uplink returned status %d", status_code);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t akita_transport_parse_host_port_endpoint(
    const char *endpoint,
    const char *scheme,
    char *host,
    size_t host_size,
    char *port,
    size_t port_size
) {
    const char *host_start;
    const char *host_end;
    const char *port_start;
    const char *port_end;
    size_t host_length;
    size_t port_length;

    size_t scheme_len;

    if (endpoint == NULL || scheme == NULL || host == NULL || port == NULL ||
        host_size == 0U || port_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    scheme_len = strlen(scheme);
    if (strncasecmp(endpoint, scheme, scheme_len) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    host_start = endpoint + scheme_len;
    port_start = strrchr(host_start, ':');
    if (port_start == NULL || port_start == host_start) {
        return ESP_ERR_INVALID_ARG;
    }

    host_end = port_start;
    port_start += 1;
    port_end = endpoint + strlen(endpoint);

    host_length = (size_t) (host_end - host_start);
    port_length = (size_t) (port_end - port_start);
    if (host_length == 0U || host_length >= host_size || port_length == 0U || port_length >= port_size) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(host, host_start, host_length);
    host[host_length] = '\0';
    memcpy(port, port_start, port_length);
    port[port_length] = '\0';
    return ESP_OK;
}

static esp_err_t akita_transport_publish_udp_datagram(
    const char *host,
    const char *port,
    const char *payload,
    size_t payload_len
) {
    struct addrinfo hints = {0};
    struct addrinfo *result = NULL;
    int socket_fd = -1;
    int sent_bytes;

    if (host == NULL || port == NULL || payload == NULL || payload_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, port, &hints, &result) != 0 || result == NULL) {
        return ESP_FAIL;
    }

    socket_fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (socket_fd < 0) {
        freeaddrinfo(result);
        return ESP_FAIL;
    }

    sent_bytes = (int) sendto(socket_fd, payload, payload_len, 0, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);
    close(socket_fd);
    if (sent_bytes < 0 || (size_t) sent_bytes != payload_len) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t akita_transport_exchange_udp_datagram(
    const char *host,
    const char *port,
    const char *payload,
    size_t payload_len,
    char *response,
    size_t response_size
) {
    struct addrinfo hints = {0};
    struct addrinfo *result = NULL;
    struct timeval timeout = {
        .tv_sec = AKITA_TRANSPORT_RNS_TIMEOUT_MS / 1000,
        .tv_usec = (AKITA_TRANSPORT_RNS_TIMEOUT_MS % 1000) * 1000,
    };
    int socket_fd = -1;
    int received_bytes;
    int sent_bytes;

    if (host == NULL || port == NULL || payload == NULL || payload_len == 0U ||
        response == NULL || response_size < 2U) {
        return ESP_ERR_INVALID_ARG;
    }

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, port, &hints, &result) != 0 || result == NULL) {
        return ESP_FAIL;
    }

    socket_fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (socket_fd < 0) {
        freeaddrinfo(result);
        return ESP_FAIL;
    }

    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        freeaddrinfo(result);
        close(socket_fd);
        return ESP_FAIL;
    }

    if (connect(socket_fd, result->ai_addr, result->ai_addrlen) != 0) {
        freeaddrinfo(result);
        close(socket_fd);
        return ESP_FAIL;
    }

    sent_bytes = (int) send(socket_fd, payload, payload_len, 0);
    if (sent_bytes < 0 || (size_t) sent_bytes != payload_len) {
        freeaddrinfo(result);
        close(socket_fd);
        return ESP_FAIL;
    }

    received_bytes = (int) recv(socket_fd, response, response_size - 1U, 0);
    freeaddrinfo(result);
    close(socket_fd);
    if (received_bytes <= 0) {
        return ESP_ERR_TIMEOUT;
    }

    response[received_bytes] = '\0';
    return ESP_OK;
}

static bool akita_transport_rns_response_ok(const char *response) {
    return response != NULL && strstr(response, "\"status\":\"ok\"") != NULL;
}

static esp_err_t akita_transport_exchange_rns_udp(
    const akita_runtime_config_t *config,
    const char *kind,
    const char *payload,
    char *response,
    size_t response_size
) {
    char host[AKITA_TRANSPORT_UDP_HOST_MAX_LEN];
    char port[AKITA_TRANSPORT_UDP_PORT_MAX_LEN];
    char escaped_vehicle_id[80];
    char escaped_destination[160];
    char *request = NULL;
    int written;
    size_t request_size;
    uint32_t sequence;
    esp_err_t err;

    if (config == NULL || kind == NULL || response == NULL || response_size < 2U) {
        return ESP_ERR_INVALID_ARG;
    }

    err = akita_transport_parse_host_port_endpoint(
        config->telemetry_endpoint,
        "rns+udp://",
        host,
        sizeof(host),
        port,
        sizeof(port)
    );
    if (err != ESP_OK) {
        return err;
    }

    akita_transport_json_escape(config->vehicle_id, escaped_vehicle_id, sizeof(escaped_vehicle_id));
    sequence = ++g_rns_bridge_sequence;

    if (payload == NULL) {
        request_size = strlen(AKITA_TRANSPORT_RNS_PROTOCOL) + strlen(kind) + strlen(escaped_vehicle_id) + 96U;
        request = malloc(request_size);
        if (request == NULL) {
            return ESP_ERR_NO_MEM;
        }

        written = snprintf(
            request,
            request_size,
            "{\"bridge\":\"%s\",\"kind\":\"%s\",\"sequence\":%lu,\"vehicle_id\":\"%s\"}",
            AKITA_TRANSPORT_RNS_PROTOCOL,
            kind,
            (unsigned long) sequence,
            escaped_vehicle_id
        );
    } else {
        akita_transport_json_escape(config->reticulum_destination, escaped_destination, sizeof(escaped_destination));
        request_size = strlen(AKITA_TRANSPORT_RNS_PROTOCOL) + strlen(kind) + strlen(escaped_vehicle_id) +
                       strlen(escaped_destination) + strlen(payload) + 128U;
        request = malloc(request_size);
        if (request == NULL) {
            return ESP_ERR_NO_MEM;
        }

        written = snprintf(
            request,
            request_size,
            "{\"bridge\":\"%s\",\"kind\":\"%s\",\"sequence\":%lu,\"vehicle_id\":\"%s\",\"destination\":\"%s\",\"payload\":%s}",
            AKITA_TRANSPORT_RNS_PROTOCOL,
            kind,
            (unsigned long) sequence,
            escaped_vehicle_id,
            escaped_destination,
            payload
        );
    }

    if (written < 0 || (size_t) written >= request_size) {
        free(request);
        return ESP_ERR_INVALID_SIZE;
    }

    err = akita_transport_exchange_udp_datagram(host, port, request, (size_t) written, response, response_size);
    free(request);
    return err;
}

static esp_err_t akita_transport_ping_rns_bridge(const akita_runtime_config_t *config) {
    char response[AKITA_TRANSPORT_RNS_RESPONSE_MAX_LEN];
    esp_err_t err;

    if (config == NULL || !g_wifi_transport_enabled || !g_wifi_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    err = akita_transport_exchange_rns_udp(config, "ping", NULL, response, sizeof(response));
    if (err != ESP_OK) {
        akita_transport_set_rns_bridge_state(false, "error", esp_err_to_name(err));
        return err;
    }

    if (!akita_transport_rns_response_ok(response)) {
        char message[AKITA_TRANSPORT_BRIDGE_ERROR_MAX_LEN];

        if (!akita_transport_json_extract_string(response, "message", message, sizeof(message))) {
            akita_transport_copy_string(message, sizeof(message), "bridge_error");
        }
        ESP_LOGW(TAG, "Reticulum bridge ping failed: %s", response);
        akita_transport_set_rns_bridge_state(false, "error", message);
        return ESP_FAIL;
    }

    {
        char mode[AKITA_TRANSPORT_BRIDGE_MODE_MAX_LEN];

        if (!akita_transport_json_extract_string(response, "mode", mode, sizeof(mode))) {
            akita_transport_copy_string(mode, sizeof(mode), "bridge_ready");
        }

        akita_transport_set_rns_bridge_state(true, mode, "");
    }
    return ESP_OK;
}

static esp_err_t akita_transport_publish_udp(const char *endpoint, const char *payload) {
    char host[AKITA_TRANSPORT_UDP_HOST_MAX_LEN];
    char port[AKITA_TRANSPORT_UDP_PORT_MAX_LEN];
    esp_err_t err;

    err = akita_transport_parse_host_port_endpoint(endpoint, "udp://", host, sizeof(host), port, sizeof(port));
    if (err != ESP_OK) {
        return err;
    }

    return akita_transport_publish_udp_datagram(host, port, payload, strlen(payload));
}

static esp_err_t akita_transport_publish_rns_udp(const akita_runtime_config_t *config, const char *payload) {
    char response[AKITA_TRANSPORT_RNS_RESPONSE_MAX_LEN];
    esp_err_t err;

    if (config == NULL || payload == NULL || payload[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    err = akita_transport_exchange_rns_udp(config, "telemetry", payload, response, sizeof(response));
    if (err != ESP_OK) {
        akita_transport_set_rns_bridge_state(false, "error", esp_err_to_name(err));
        return err;
    }

    if (!akita_transport_rns_response_ok(response)) {
        char message[AKITA_TRANSPORT_BRIDGE_ERROR_MAX_LEN];

        if (!akita_transport_json_extract_string(response, "message", message, sizeof(message))) {
            akita_transport_copy_string(message, sizeof(message), "bridge_error");
        }
        ESP_LOGW(TAG, "Reticulum bridge rejected telemetry: %s", response);
        akita_transport_set_rns_bridge_state(false, "error", message);
        return ESP_FAIL;
    }

    {
        char mode[AKITA_TRANSPORT_BRIDGE_MODE_MAX_LEN];

        if (!akita_transport_json_extract_string(response, "mode", mode, sizeof(mode))) {
            akita_transport_copy_string(mode, sizeof(mode), "ok");
        }

        akita_transport_set_rns_bridge_state(true, mode, "");
    }
    return ESP_OK;
}

esp_err_t akita_transport_init(const akita_runtime_config_t *config) {
    esp_err_t err;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    g_transport_mode = config->transport_mode;

    if (config->transport_mode == AKITA_TRANSPORT_NONE) {
        akita_transport_disable_wifi_uplink();
        akita_transport_disable_lora_uplink();
        g_endpoint_type = AKITA_TRANSPORT_ENDPOINT_NONE;
        ESP_LOGI(TAG, "Transport disabled; telemetry will stay local");
        return ESP_OK;
    }

    if (config->transport_mode == AKITA_TRANSPORT_LORA) {
        akita_transport_disable_wifi_uplink();
        err = akita_transport_configure_lora(config);
        if (err != ESP_OK) {
            akita_transport_disable_lora_uplink();
            return err;
        }

        if (!g_transport_ready) {
            ESP_LOGI(TAG, "LoRa transport is initializing; telemetry will publish when the radio is ready");
        }

        return ESP_OK;
    }

    akita_transport_disable_lora_uplink();

    err = akita_transport_configure_wifi(config);
    if (err != ESP_OK) {
        akita_transport_disable_wifi_uplink();
        return err;
    }

    if (g_endpoint_type == AKITA_TRANSPORT_ENDPOINT_RNS_UDP && g_wifi_connected) {
        err = akita_transport_ping_rns_bridge(config);
        if (err != ESP_OK) {
            ESP_LOGI(TAG, "Reticulum bridge is not ready yet; telemetry will publish when the bridge responds");
        }
    }

    if (!g_transport_ready) {
        ESP_LOGI(TAG, "WiFi transport is initializing; telemetry will publish when the uplink is ready");
    }

    return ESP_OK;
}

esp_err_t akita_transport_publish(const akita_runtime_config_t *config, const char *payload) {
    akita_transport_endpoint_t endpoint_type;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    g_transport_mode = config->transport_mode;

    if (config->transport_mode == AKITA_TRANSPORT_LORA) {
        return akita_transport_publish_lora(payload);
    }

    if (config->transport_mode != AKITA_TRANSPORT_WIFI) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    endpoint_type = akita_transport_endpoint_type(config->telemetry_endpoint);
    if (endpoint_type == AKITA_TRANSPORT_ENDPOINT_NONE) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (payload == NULL || payload[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    g_endpoint_type = endpoint_type;
    akita_transport_update_ready_state();

    if (endpoint_type == AKITA_TRANSPORT_ENDPOINT_RNS_UDP) {
        if (!g_wifi_transport_enabled || !g_wifi_connected) {
            return ESP_ERR_INVALID_STATE;
        }

        return akita_transport_publish_rns_udp(config, payload);
    }

    if (!g_transport_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    switch (endpoint_type) {
        case AKITA_TRANSPORT_ENDPOINT_HTTP:
            return akita_transport_publish_http(config->telemetry_endpoint, payload);
        case AKITA_TRANSPORT_ENDPOINT_UDP:
            return akita_transport_publish_udp(config->telemetry_endpoint, payload);
        case AKITA_TRANSPORT_ENDPOINT_RNS_UDP:
            return akita_transport_publish_rns_udp(config, payload);
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
}

bool akita_transport_ready(void) {
    return g_transport_ready;
}

void akita_transport_poll(const akita_runtime_config_t *config) {
    uint64_t now_ms = akita_transport_now_ms();
    wifi_ap_record_t ap_info;
    esp_err_t err;

    if (g_lora_ready) {
        akita_transport_lora_harvest_rx();
    }

    if (g_wifi_transport_enabled && !g_wifi_connected && g_wifi_retry_at_ms > 0U && now_ms >= g_wifi_retry_at_ms) {
        g_wifi_retry_at_ms = now_ms + g_wifi_backoff_ms;
        err = esp_wifi_connect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_STATE && err != ESP_ERR_WIFI_CONN) {
            ESP_LOGW(TAG, "WiFi reconnect attempt failed: %s", esp_err_to_name(err));
        }
    }

    if (g_wifi_connected && esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        g_wifi_rssi = ap_info.rssi;
    } else if (!g_wifi_connected) {
        g_wifi_rssi = 0;
    }

    if (config != NULL &&
        config->transport_mode == AKITA_TRANSPORT_WIFI &&
        g_endpoint_type == AKITA_TRANSPORT_ENDPOINT_RNS_UDP &&
        g_wifi_transport_enabled &&
        g_wifi_connected &&
        now_ms >= g_rns_next_ping_ms) {
        err = akita_transport_ping_rns_bridge(config);
        g_rns_next_ping_ms = now_ms + (err == ESP_OK ? (AKITA_TRANSPORT_RNS_PING_INTERVAL_MS * 3U)
                                                    : AKITA_TRANSPORT_RNS_PING_INTERVAL_MS);
    }
}

void akita_transport_get_status(akita_transport_status_t *status) {
    if (status == NULL) {
        return;
    }

    akita_transport_lock();
    memset(status, 0, sizeof(*status));
    status->transport_ready = g_transport_ready;
    status->bridge_ready = g_rns_bridge_ready;
    status->lora_ready = g_lora_ready;
    status->wifi_connected = g_wifi_connected;
    status->wifi_rssi = g_wifi_rssi;
    akita_transport_copy_string(status->bridge_mode, sizeof(status->bridge_mode), g_rns_bridge_mode);
    akita_transport_copy_string(status->bridge_last_error, sizeof(status->bridge_last_error), g_rns_bridge_last_error);
    akita_transport_unlock();
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