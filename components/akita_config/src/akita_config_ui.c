#include "akita_config_ui.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "akita_board.h"
#include "akita_config_store.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

static const char *TAG = "akita_config_ui";
static httpd_handle_t g_httpd_handle;
static akita_runtime_config_t *g_runtime_config;
static bool g_wifi_started;
static bool g_http_running;
static akita_config_apply_callback_t g_apply_callback;
static void *g_apply_callback_context;

static const char kConfigPage[] =
"<!doctype html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"  <meta charset=\"utf-8\">\n"
"  <meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
"  <title>Akita CarNode Setup</title>\n"
"  <style>\n"
"    :root { color-scheme: light; --bg:#f3efe7; --panel:#fffdf8; --ink:#162126; --muted:#637177; --accent:#c85b2b; --accent-2:#315c73; --line:#d8d0c2; }\n"
"    body { margin:0; font-family: 'Trebuchet MS', 'Segoe UI', sans-serif; background: radial-gradient(circle at top, #fff7ea 0, var(--bg) 45%, #e6ecef 100%); color:var(--ink); }\n"
"    .shell { max-width: 980px; margin: 0 auto; padding: 32px 20px 56px; }\n"
"    .hero { display:grid; gap:12px; margin-bottom:24px; }\n"
"    .eyebrow { letter-spacing:.18em; text-transform:uppercase; color:var(--accent-2); font-size:.78rem; }\n"
"    h1 { margin:0; font-size: clamp(2rem, 5vw, 3.7rem); line-height: .95; }\n"
"    .lead { max-width: 52rem; color:var(--muted); margin:0; }\n"
"    form { display:grid; gap:18px; }\n"
"    .grid { display:grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap:16px; }\n"
"    .panel { background: color-mix(in srgb, var(--panel) 92%, white); border:1px solid var(--line); border-radius: 20px; padding:18px; box-shadow: 0 18px 45px rgba(22,33,38,.08); }\n"
"    .panel h2 { margin:0 0 12px; font-size:1rem; text-transform:uppercase; letter-spacing:.12em; color:var(--accent-2); }\n"
"    label { display:grid; gap:6px; font-size:.92rem; margin-bottom:12px; }\n"
"    input, select { border:1px solid #c9bfae; background:#fff; border-radius:12px; padding:11px 13px; font-size:1rem; color:var(--ink); }\n"
"    .checkbox { display:flex; align-items:center; gap:10px; }\n"
"    .checkbox input { width:18px; height:18px; }\n"
"    .actions { display:flex; gap:12px; align-items:center; flex-wrap:wrap; }\n"
"    button { border:0; border-radius:999px; padding:12px 18px; background:linear-gradient(135deg, var(--accent), #e48a3a); color:#fff; font-weight:700; cursor:pointer; }\n"
"    .note { color:var(--muted); font-size:.92rem; }\n"
"    #status { min-height:1.4em; color:var(--accent-2); font-weight:600; }\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"  <div class=\"shell\">\n"
"    <div class=\"hero\">\n"
"      <div class=\"eyebrow\">Akita native ESP-IDF firmware</div>\n"
"      <h1>CarNode control surface</h1>\n"
"      <p class=\"lead\">Use this page to set board identity, telemetry cadence, GPS, OBD and uplink defaults without any Arduino dependency. Runtime configuration is saved to NVS and reapplied live after save.</p>\n"
"    </div>\n"
"    <form id=\"config-form\">\n"
"      <div class=\"grid\">\n"
"        <section class=\"panel\">\n"
"          <h2>Identity</h2>\n"
"          <label>Vehicle ID<input name=\"vehicle_id\" maxlength=\"31\"></label>\n"
"          <label>Board profile<input name=\"board_name\" disabled></label>\n"
"          <label>Telemetry interval (ms)<input name=\"telemetry_interval_ms\" type=\"number\" min=\"1000\" max=\"600000\"></label>\n"
"        </section>\n"
"        <section class=\"panel\">\n"
"          <h2>Uplink</h2>\n"
"          <label>Transport<select name=\"transport_mode\"><option value=\"wifi\">WiFi uplink</option><option value=\"lora\">LoRa uplink</option><option value=\"none\">Local only</option></select></label>\n"
"          <label>WiFi SSID<input name=\"wifi_ssid\" maxlength=\"63\"></label>\n"
"          <label>WiFi password<input name=\"wifi_password\" type=\"password\" maxlength=\"63\"></label>\n"
"          <label>Telemetry endpoint<input name=\"telemetry_endpoint\" maxlength=\"95\" placeholder=\"http://host/path, udp://host:port, rns+udp://host:port\"></label>\n"
"          <label>Reticulum destination<input name=\"reticulum_destination\" maxlength=\"63\" placeholder=\"32 hex chars, or leave empty to broadcast\"></label>\n"
"        </section>\n"
"        <section class=\"panel\">\n"
"          <h2>Vehicle I/O</h2>\n"
"          <label>OBD adapter name<input name=\"obd_device_name\" maxlength=\"63\"></label>\n"
"          <label class=\"checkbox\"><input type=\"checkbox\" name=\"use_obd_uuid\">Use service UUID during BLE scan</label>\n"
"          <label>OBD service UUID<input name=\"obd_service_uuid\" maxlength=\"39\" placeholder=\"0000ffe0-0000-1000-8000-00805f9b34fb\"></label>\n"
"          <label>OBD characteristic UUID<input name=\"obd_characteristic_uuid\" maxlength=\"39\" placeholder=\"0000ffe1-0000-1000-8000-00805f9b34fb\"></label>\n"
"          <label>GPS UART RX pin<input name=\"gps_rx_pin\" type=\"number\"></label>\n"
"          <label>GPS UART TX pin<input name=\"gps_tx_pin\" type=\"number\"></label>\n"
"          <label>GPS baud<input name=\"gps_uart_baud\" type=\"number\" min=\"1200\" max=\"921600\"></label>\n"
"          <label class=\"checkbox\"><input type=\"checkbox\" name=\"enable_gps\">Enable GPS reader</label>\n"
"        </section>\n"
"      </div>\n"
"      <div class=\"actions\">\n"
"        <button type=\"submit\">Save configuration</button>\n"
"        <div id=\"status\"></div>\n"
"      </div>\n"
"      <div class=\"note\">This portal is intentionally small and self-contained so it stays predictable on ESP32-C5, ESP32-C6, ESP32-S3 and Heltec-class LoRa boards.</div>\n"
"    </form>\n"
"  </div>\n"
"  <script>\n"
"    const form = document.getElementById('config-form');\n"
"    const statusNode = document.getElementById('status');\n"
"    fetch('/api/config').then(r => r.json()).then(data => {\n"
"      Object.entries(data).forEach(([key, value]) => {\n"
"        const field = form.elements.namedItem(key);\n"
"        if (!field) return;\n"
"        if (field.type === 'checkbox') field.checked = Boolean(value);\n"
"        else field.value = value ?? '';\n"
"      });\n"
"    }).catch(() => { statusNode.textContent = 'Unable to load saved configuration.'; });\n"
"    form.addEventListener('submit', async event => {\n"
"      event.preventDefault();\n"
"      statusNode.textContent = 'Saving...';\n"
"      const payload = new URLSearchParams(new FormData(form));\n"
"      const response = await fetch('/api/config', { method: 'POST', body: payload });\n"
"      statusNode.textContent = await response.text();\n"
"    });\n"
"  </script>\n"
"</body>\n"
"</html>\n";

static void akita_copy_string(char *destination, size_t destination_size, const char *source) {
    if (destination == NULL || destination_size == 0) {
        return;
    }

    snprintf(destination, destination_size, "%s", source != NULL ? source : "");
}

static void akita_url_decode(char *text) {
    char *src = text;
    char *dst = text;

    while (src != NULL && *src != '\0') {
        if (*src == '+' ) {
            *dst++ = ' ';
            ++src;
        } else if (*src == '%' && isxdigit((unsigned char) src[1]) && isxdigit((unsigned char) src[2])) {
            char scratch[3] = { src[1], src[2], '\0' };
            *dst++ = (char) strtol(scratch, NULL, 16);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }

    *dst = '\0';
}

static bool akita_form_contains(const char *body, const char *key) {
    size_t key_len;
    const char *cursor;

    if (body == NULL || key == NULL) {
        return false;
    }

    key_len = strlen(key);
    cursor = body;
    while ((cursor = strstr(cursor, key)) != NULL) {
        if ((cursor == body || cursor[-1] == '&') && cursor[key_len] == '=') {
            return true;
        }
        cursor += key_len;
    }

    return false;
}

static bool akita_form_get_value(char *body, const char *key, char *output, size_t output_size) {
    size_t key_len;
    char *cursor;

    if (body == NULL || key == NULL || output == NULL || output_size == 0) {
        return false;
    }

    key_len = strlen(key);
    cursor = body;
    while (cursor != NULL && *cursor != '\0') {
        char *segment_end = strchr(cursor, '&');
        if (segment_end != NULL) {
            *segment_end = '\0';
        }

        if ((strncmp(cursor, key, key_len) == 0) && cursor[key_len] == '=') {
            akita_copy_string(output, output_size, cursor + key_len + 1);
            akita_url_decode(output);
            if (segment_end != NULL) {
                *segment_end = '&';
            }
            return true;
        }

        if (segment_end != NULL) {
            *segment_end = '&';
            cursor = segment_end + 1;
        } else {
            cursor = NULL;
        }
    }

    output[0] = '\0';
    return false;
}

static void akita_json_escape(const char *input, char *output, size_t output_size) {
    size_t used = 0;

    if (output == NULL || output_size == 0) {
        return;
    }

    while (input != NULL && *input != '\0' && (used + 1) < output_size) {
        if ((*input == '"' || *input == '\\') && (used + 2) < output_size) {
            output[used++] = '\\';
            output[used++] = *input;
        } else {
            output[used++] = *input;
        }
        ++input;
    }

    output[used] = '\0';
}

static esp_err_t akita_root_handler(httpd_req_t *request) {
    httpd_resp_set_type(request, "text/html");
    return httpd_resp_send(request, kConfigPage, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t akita_config_json_handler(httpd_req_t *request) {
    char vehicle_id[80];
    char wifi_ssid[96];
    char endpoint[128];
    char reticulum_destination[160];
    char obd_name[96];
    char obd_service_uuid[64];
    char obd_characteristic_uuid[64];
    char response[1184];

    akita_json_escape(g_runtime_config->vehicle_id, vehicle_id, sizeof(vehicle_id));
    akita_json_escape(g_runtime_config->wifi_ssid, wifi_ssid, sizeof(wifi_ssid));
    akita_json_escape(g_runtime_config->telemetry_endpoint, endpoint, sizeof(endpoint));
    akita_json_escape(g_runtime_config->reticulum_destination, reticulum_destination, sizeof(reticulum_destination));
    akita_json_escape(g_runtime_config->obd_device_name, obd_name, sizeof(obd_name));
    akita_json_escape(g_runtime_config->obd_service_uuid, obd_service_uuid, sizeof(obd_service_uuid));
    akita_json_escape(g_runtime_config->obd_characteristic_uuid, obd_characteristic_uuid, sizeof(obd_characteristic_uuid));

    snprintf(
        response,
        sizeof(response),
        "{\"vehicle_id\":\"%s\",\"board_name\":\"%s\",\"transport_mode\":\"%s\","
        "\"wifi_ssid\":\"%s\",\"telemetry_endpoint\":\"%s\",\"reticulum_destination\":\"%s\",\"obd_device_name\":\"%s\","
        "\"use_obd_uuid\":%s,\"obd_service_uuid\":\"%s\",\"obd_characteristic_uuid\":\"%s\","
        "\"telemetry_interval_ms\":%lu,\"gps_rx_pin\":%ld,\"gps_tx_pin\":%ld,\"gps_uart_baud\":%lu,"
        "\"enable_gps\":%s}",
        vehicle_id,
        akita_board_get_name(g_runtime_config->board_profile),
        (g_runtime_config->transport_mode == AKITA_TRANSPORT_LORA) ? "lora" :
        (g_runtime_config->transport_mode == AKITA_TRANSPORT_WIFI) ? "wifi" : "none",
        wifi_ssid,
        endpoint,
        reticulum_destination,
        obd_name,
        g_runtime_config->use_obd_uuid ? "true" : "false",
        obd_service_uuid,
        obd_characteristic_uuid,
        (unsigned long) g_runtime_config->telemetry_interval_ms,
        (long) g_runtime_config->gps_rx_pin,
        (long) g_runtime_config->gps_tx_pin,
        (unsigned long) g_runtime_config->gps_uart_baud,
        g_runtime_config->enable_gps ? "true" : "false"
    );

    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, response, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t akita_config_post_handler(httpd_req_t *request) {
    char body[1024];
    char scratch[128];
    char response[160];
    esp_err_t apply_err = ESP_OK;
    int received;

    if (request->content_len >= (int) sizeof(body)) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Config payload too large");
    }

    received = httpd_req_recv(request, body, request->content_len);
    if (received <= 0) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Config payload missing");
    }

    body[received] = '\0';

    if (akita_form_get_value(body, "vehicle_id", scratch, sizeof(scratch))) {
        akita_copy_string(g_runtime_config->vehicle_id, sizeof(g_runtime_config->vehicle_id), scratch);
    }
    if (akita_form_get_value(body, "wifi_ssid", scratch, sizeof(scratch))) {
        akita_copy_string(g_runtime_config->wifi_ssid, sizeof(g_runtime_config->wifi_ssid), scratch);
    }
    if (akita_form_get_value(body, "wifi_password", scratch, sizeof(scratch))) {
        akita_copy_string(g_runtime_config->wifi_password, sizeof(g_runtime_config->wifi_password), scratch);
    }
    if (akita_form_get_value(body, "telemetry_endpoint", scratch, sizeof(scratch))) {
        akita_copy_string(g_runtime_config->telemetry_endpoint, sizeof(g_runtime_config->telemetry_endpoint), scratch);
    }
    if (akita_form_get_value(body, "reticulum_destination", scratch, sizeof(scratch))) {
        akita_copy_string(g_runtime_config->reticulum_destination, sizeof(g_runtime_config->reticulum_destination), scratch);
    }
    if (akita_form_get_value(body, "obd_device_name", scratch, sizeof(scratch))) {
        akita_copy_string(g_runtime_config->obd_device_name, sizeof(g_runtime_config->obd_device_name), scratch);
    }
    if (akita_form_get_value(body, "obd_service_uuid", scratch, sizeof(scratch))) {
        akita_copy_string(g_runtime_config->obd_service_uuid, sizeof(g_runtime_config->obd_service_uuid), scratch);
    }
    if (akita_form_get_value(body, "obd_characteristic_uuid", scratch, sizeof(scratch))) {
        akita_copy_string(g_runtime_config->obd_characteristic_uuid, sizeof(g_runtime_config->obd_characteristic_uuid), scratch);
    }
    if (akita_form_get_value(body, "telemetry_interval_ms", scratch, sizeof(scratch))) {
        g_runtime_config->telemetry_interval_ms = (uint32_t) strtoul(scratch, NULL, 10);
    }
    if (akita_form_get_value(body, "gps_rx_pin", scratch, sizeof(scratch))) {
        g_runtime_config->gps_rx_pin = (int32_t) strtol(scratch, NULL, 10);
    }
    if (akita_form_get_value(body, "gps_tx_pin", scratch, sizeof(scratch))) {
        g_runtime_config->gps_tx_pin = (int32_t) strtol(scratch, NULL, 10);
    }
    if (akita_form_get_value(body, "gps_uart_baud", scratch, sizeof(scratch))) {
        g_runtime_config->gps_uart_baud = (uint32_t) strtoul(scratch, NULL, 10);
    }
    if (akita_form_get_value(body, "transport_mode", scratch, sizeof(scratch))) {
        if (strcmp(scratch, "lora") == 0) {
            g_runtime_config->transport_mode = AKITA_TRANSPORT_LORA;
        } else if (strcmp(scratch, "wifi") == 0) {
            g_runtime_config->transport_mode = AKITA_TRANSPORT_WIFI;
        } else {
            g_runtime_config->transport_mode = AKITA_TRANSPORT_NONE;
        }
    }

    g_runtime_config->enable_gps = akita_form_contains(body, "enable_gps");
    g_runtime_config->use_obd_uuid = akita_form_contains(body, "use_obd_uuid");
    ESP_ERROR_CHECK(akita_config_save(g_runtime_config));

    if (g_apply_callback != NULL) {
        apply_err = g_apply_callback(g_runtime_config, g_apply_callback_context);
    }

    httpd_resp_set_type(request, "text/plain");
    if (apply_err == ESP_OK) {
        return httpd_resp_sendstr(request, "Saved. Runtime changes were applied live.");
    }

    snprintf(response, sizeof(response),
             "Saved, but live apply failed: %s. Reboot the node to fully apply changes.",
             esp_err_to_name(apply_err));
    return httpd_resp_sendstr(request, response);
}

static esp_err_t akita_config_ui_start_wifi(const akita_runtime_config_t *config) {
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t wifi_config = { 0 };
    esp_err_t err;

    if (g_wifi_started) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));

    snprintf((char *) wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), "%s", CONFIG_AKITA_CONFIG_PORTAL_SSID);
    wifi_config.ap.ssid_len = strlen((char *) wifi_config.ap.ssid);
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.channel = 1;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;

    if (strlen(CONFIG_AKITA_CONFIG_PORTAL_PASSWORD) > 0) {
        snprintf((char *) wifi_config.ap.password, sizeof(wifi_config.ap.password), "%s", CONFIG_AKITA_CONFIG_PORTAL_PASSWORD);
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Config AP ready: %s", (char *) wifi_config.ap.ssid);
    ESP_LOGI(TAG, "Board defaults loaded for %s", akita_board_get_name(config->board_profile));
    g_wifi_started = true;

    return ESP_OK;
}

esp_err_t akita_config_ui_start(akita_runtime_config_t *config) {
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = akita_root_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t api_get_uri = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = akita_config_json_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t api_post_uri = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = akita_config_post_handler,
        .user_ctx = NULL,
    };

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    g_runtime_config = config;
    ESP_ERROR_CHECK(akita_config_ui_start_wifi(config));

    if (g_http_running) {
        return ESP_OK;
    }

    server_config.server_port = config->config_http_port;
    ESP_ERROR_CHECK(httpd_start(&g_httpd_handle, &server_config));
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_httpd_handle, &root_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_httpd_handle, &api_get_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_httpd_handle, &api_post_uri));
    g_http_running = true;
    return ESP_OK;
}

void akita_config_ui_set_apply_callback(akita_config_apply_callback_t callback, void *context) {
    g_apply_callback = callback;
    g_apply_callback_context = context;
}

bool akita_config_ui_is_running(void) {
    return g_http_running;
}