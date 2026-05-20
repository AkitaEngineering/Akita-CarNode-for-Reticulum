#include "akita_transport.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#define AKITA_TRANSPORT_WIFI_CONNECTED_BIT BIT0
#define AKITA_TRANSPORT_HTTP_TIMEOUT_MS 8000
#define AKITA_TRANSPORT_UDP_HOST_MAX_LEN 80
#define AKITA_TRANSPORT_UDP_PORT_MAX_LEN 8

static const char *TAG = "akita_transport";
typedef enum {
    AKITA_TRANSPORT_ENDPOINT_NONE = 0,
    AKITA_TRANSPORT_ENDPOINT_HTTP,
    AKITA_TRANSPORT_ENDPOINT_UDP,
} akita_transport_endpoint_t;

static EventGroupHandle_t g_wifi_event_group;
static esp_event_handler_instance_t g_wifi_event_handler;
static esp_event_handler_instance_t g_ip_event_handler;
static esp_netif_t *g_sta_netif;
static bool g_event_handlers_registered;
static bool g_wifi_connected;
static bool g_transport_ready;
static bool g_wifi_transport_enabled;
static akita_transport_endpoint_t g_endpoint_type;

static void akita_transport_update_ready_state(void) {
    g_transport_ready = g_wifi_transport_enabled && g_wifi_connected && g_endpoint_type != AKITA_TRANSPORT_ENDPOINT_NONE;
}

static void akita_transport_disable_wifi_uplink(void) {
    esp_err_t err;

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

static akita_transport_endpoint_t akita_transport_endpoint_type(const char *endpoint) {
    if (endpoint == NULL || endpoint[0] == '\0') {
        return AKITA_TRANSPORT_ENDPOINT_NONE;
    }

    if (strncasecmp(endpoint, "http://", 7) == 0) {
        return AKITA_TRANSPORT_ENDPOINT_HTTP;
    }

    if (strncasecmp(endpoint, "udp://", 6) == 0) {
        return AKITA_TRANSPORT_ENDPOINT_UDP;
    }

    return AKITA_TRANSPORT_ENDPOINT_NONE;
}

static void akita_transport_wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
) {
    esp_err_t err;

    (void) arg;
    (void) event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        g_wifi_connected = false;
        if (g_wifi_event_group != NULL) {
            xEventGroupClearBits(g_wifi_event_group, AKITA_TRANSPORT_WIFI_CONNECTED_BIT);
        }
        akita_transport_update_ready_state();

        if (g_wifi_transport_enabled) {
            err = esp_wifi_connect();
            if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
                ESP_LOGW(TAG, "WiFi reconnect attempt failed: %s", esp_err_to_name(err));
            }
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        g_wifi_connected = true;
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

static esp_err_t akita_transport_parse_udp_endpoint(
    const char *endpoint,
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

    if (endpoint == NULL || host == NULL || port == NULL ||
        host_size == 0U || port_size == 0U || strncasecmp(endpoint, "udp://", 6) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    host_start = endpoint + 6;
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

static esp_err_t akita_transport_publish_udp(const char *endpoint, const char *payload) {
    struct addrinfo hints = {0};
    struct addrinfo *result = NULL;
    int socket_fd = -1;
    int sent_bytes;
    char host[AKITA_TRANSPORT_UDP_HOST_MAX_LEN];
    char port[AKITA_TRANSPORT_UDP_PORT_MAX_LEN];
    esp_err_t err;

    err = akita_transport_parse_udp_endpoint(endpoint, host, sizeof(host), port, sizeof(port));
    if (err != ESP_OK) {
        return err;
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

    sent_bytes = (int) sendto(socket_fd, payload, strlen(payload), 0, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);
    close(socket_fd);
    if (sent_bytes < 0) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t akita_transport_init(const akita_runtime_config_t *config) {
    esp_err_t err;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->transport_mode == AKITA_TRANSPORT_NONE) {
        akita_transport_disable_wifi_uplink();
        g_endpoint_type = AKITA_TRANSPORT_ENDPOINT_NONE;
        ESP_LOGI(TAG, "Transport disabled; telemetry will stay local");
        return ESP_OK;
    }

    if (config->transport_mode == AKITA_TRANSPORT_LORA) {
        akita_transport_disable_wifi_uplink();
        g_endpoint_type = AKITA_TRANSPORT_ENDPOINT_NONE;
        ESP_LOGW(TAG, "LoRa transport backend is not implemented yet");
        return ESP_ERR_NOT_SUPPORTED;
    }

    err = akita_transport_configure_wifi(config);
    if (err != ESP_OK) {
        akita_transport_disable_wifi_uplink();
        return err;
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

    if (!g_transport_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    switch (endpoint_type) {
        case AKITA_TRANSPORT_ENDPOINT_HTTP:
            return akita_transport_publish_http(config->telemetry_endpoint, payload);
        case AKITA_TRANSPORT_ENDPOINT_UDP:
            return akita_transport_publish_udp(config->telemetry_endpoint, payload);
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
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