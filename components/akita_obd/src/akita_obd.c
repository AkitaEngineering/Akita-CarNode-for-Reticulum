#include "akita_obd.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_hs_mbuf.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#define AKITA_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))
#define AKITA_UUID_STRING_LENGTH 37U
#define AKITA_OBD_RX_BUFFER_SIZE 256U
#define AKITA_OBD_CONN_HANDLE_NONE UINT16_MAX
#define AKITA_OBD_CONNECT_TIMEOUT_MS 30000
#define AKITA_OBD_RESPONSE_TIMEOUT_MS 1500U
#define AKITA_OBD_INIT_DELAY_MS 250U
#define AKITA_OBD_PID_DELAY_MS 125U
#define AKITA_OBD_READ_DELAY_MS 80U

static const char *TAG = "akita_obd";

static const char *kElmServiceUuid = "0000ffe0-0000-1000-8000-00805f9b34fb";
static const char *kElmCharacteristicUuid = "0000ffe1-0000-1000-8000-00805f9b34fb";
static const char *kNusServiceUuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char *kNusWriteUuid = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static const char *kNusNotifyUuid = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

static const char *kInitCommands[] = {
    "ATZ",
    "ATE0",
    "ATL0",
    "ATS0",
    "ATH0",
    "ATSP0",
};

static const char *kTelemetryCommands[] = {
    "010C",
    "010D",
    "0105",
};

typedef enum {
    AKITA_OBD_PROFILE_UNKNOWN = 0,
    AKITA_OBD_PROFILE_ELM327_SERIAL,
    AKITA_OBD_PROFILE_NUS,
} akita_obd_profile_t;

static akita_runtime_config_t g_config;
static akita_obd_snapshot_t g_obd_state;
static bool g_stack_started;
static bool g_host_synced;
static bool g_scan_active;
static bool g_connecting;
static bool g_connect_after_scan;
static bool g_obd_ready;
static bool g_pending_response;
static bool g_read_in_flight;
static bool g_use_read_fallback;
static bool g_notifications_enabled;
static uint8_t g_own_addr_type;
static ble_addr_t g_pending_peer_addr;
static uint16_t g_conn_handle = AKITA_OBD_CONN_HANDLE_NONE;
static uint16_t g_service_start_handle;
static uint16_t g_service_end_handle;
static uint16_t g_write_handle;
static uint16_t g_notify_handle;
static uint16_t g_cccd_handle;
static uint8_t g_write_properties;
static uint8_t g_notify_properties;
static akita_obd_profile_t g_profile;
static size_t g_init_command_index;
static size_t g_pid_command_index;
static uint64_t g_last_sample_ms;
static uint64_t g_next_command_at_ms;
static uint64_t g_command_started_ms;
static uint64_t g_read_due_ms;
static char g_target_service_uuid[AKITA_UUID_STRING_LENGTH];
static char g_target_characteristic_uuid[AKITA_UUID_STRING_LENGTH];
static char g_rx_buffer[AKITA_OBD_RX_BUFFER_SIZE];
static size_t g_rx_length;

static void akita_obd_host_task(void *param);
static int akita_obd_gap_event(struct ble_gap_event *event, void *arg);
static int akita_obd_on_mtu_exchanged(uint16_t conn_handle, const struct ble_gatt_error *error,
                                      uint16_t mtu, void *arg);
static int akita_obd_on_service_discovered(uint16_t conn_handle, const struct ble_gatt_error *error,
                                           const struct ble_gatt_svc *service, void *arg);
static int akita_obd_on_characteristic_discovered(uint16_t conn_handle, const struct ble_gatt_error *error,
                                                  const struct ble_gatt_chr *chr, void *arg);
static int akita_obd_on_descriptor_discovered(uint16_t conn_handle, const struct ble_gatt_error *error,
                                              uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                                              void *arg);
static int akita_obd_on_write_complete(uint16_t conn_handle, const struct ble_gatt_error *error,
                                       struct ble_gatt_attr *attr, void *arg);
static int akita_obd_on_read_complete(uint16_t conn_handle, const struct ble_gatt_error *error,
                                      struct ble_gatt_attr *attr, void *arg);

static uint64_t akita_now_ms(void) {
    return (uint64_t) (esp_timer_get_time() / 1000ULL);
}

static void akita_clear_response_buffer(void) {
    g_rx_length = 0;
    g_rx_buffer[0] = '\0';
}

static void akita_obd_stop_link_activity(void) {
    int rc;

    if (g_scan_active) {
        rc = ble_gap_disc_cancel();
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            ESP_LOGW(TAG, "BLE scan cancel returned %d during config reapply", rc);
        }
    }

    if (g_conn_handle != AKITA_OBD_CONN_HANDLE_NONE) {
        rc = ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0) {
            ESP_LOGW(TAG, "BLE disconnect returned %d during config reapply", rc);
        }
    }
}

static void akita_reset_link_state(void) {
    g_scan_active = false;
    g_connecting = false;
    g_connect_after_scan = false;
    g_obd_ready = false;
    g_pending_response = false;
    g_read_in_flight = false;
    g_use_read_fallback = false;
    g_notifications_enabled = false;
    g_conn_handle = AKITA_OBD_CONN_HANDLE_NONE;
    g_service_start_handle = 0;
    g_service_end_handle = 0;
    g_write_handle = 0;
    g_notify_handle = 0;
    g_cccd_handle = 0;
    g_write_properties = 0;
    g_notify_properties = 0;
    g_profile = AKITA_OBD_PROFILE_UNKNOWN;
    g_init_command_index = 0;
    g_pid_command_index = 0;
    g_next_command_at_ms = 0;
    g_command_started_ms = 0;
    g_read_due_ms = 0;
    g_obd_state.connected = false;
    akita_clear_response_buffer();
}

static void akita_uuid_to_string(const ble_uuid_t *uuid, char *buffer, size_t buffer_size) {
    const uint8_t *value;

    if (buffer == NULL || buffer_size < AKITA_UUID_STRING_LENGTH) {
        return;
    }

    buffer[0] = '\0';
    if (uuid == NULL) {
        return;
    }

    switch (uuid->type) {
        case BLE_UUID_TYPE_16:
            (void) snprintf(buffer, buffer_size, "0000%04x-0000-1000-8000-00805f9b34fb",
                            BLE_UUID16(uuid)->value);
            break;
        case BLE_UUID_TYPE_32:
            (void) snprintf(buffer, buffer_size, "%08" PRIx32 "-0000-1000-8000-00805f9b34fb",
                            BLE_UUID32(uuid)->value);
            break;
        case BLE_UUID_TYPE_128:
            value = BLE_UUID128(uuid)->value;
            (void) snprintf(buffer, buffer_size,
                            "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                            value[15], value[14], value[13], value[12],
                            value[11], value[10], value[9], value[8],
                            value[7], value[6], value[5], value[4],
                            value[3], value[2], value[1], value[0]);
            break;
        default:
            break;
    }
}

static bool akita_normalize_uuid_string(const char *input, char *output, size_t output_size) {
    char digits[33];
    size_t digit_count = 0;
    const char *cursor;

    if (output == NULL || output_size < AKITA_UUID_STRING_LENGTH) {
        return false;
    }

    output[0] = '\0';
    if (input == NULL || input[0] == '\0') {
        return false;
    }

    cursor = input;
    while (*cursor != '\0') {
        if (*cursor == '-') {
            ++cursor;
            continue;
        }

        if (!isxdigit((unsigned char) *cursor) || digit_count >= (sizeof(digits) - 1U)) {
            return false;
        }

        digits[digit_count++] = (char) tolower((unsigned char) *cursor);
        ++cursor;
    }

    digits[digit_count] = '\0';

    if (digit_count == 4U) {
        (void) snprintf(output, output_size, "0000%.4s-0000-1000-8000-00805f9b34fb", digits);
        return true;
    }

    if (digit_count == 8U) {
        (void) snprintf(output, output_size, "%.8s-0000-1000-8000-00805f9b34fb", digits);
        return true;
    }

    if (digit_count == 32U) {
        (void) snprintf(output, output_size, "%.8s-%.4s-%.4s-%.4s-%.12s",
                        digits, digits + 8, digits + 12, digits + 16, digits + 20);
        return true;
    }

    return false;
}

static void akita_copy_normalized_uuid(const char *input, char *output, size_t output_size) {
    if (!akita_normalize_uuid_string(input, output, output_size) && input != NULL && input[0] != '\0') {
        ESP_LOGW(TAG, "Ignoring invalid UUID string: %s", input);
    }
}

static bool akita_uuid_equals_text(const ble_uuid_t *uuid, const char *target_uuid) {
    char candidate[AKITA_UUID_STRING_LENGTH];

    if (uuid == NULL || target_uuid == NULL || target_uuid[0] == '\0') {
        return false;
    }

    akita_uuid_to_string(uuid, candidate, sizeof(candidate));
    return candidate[0] != '\0' && strcasecmp(candidate, target_uuid) == 0;
}

static akita_obd_profile_t akita_profile_from_uuid(const ble_uuid_t *uuid) {
    if (akita_uuid_equals_text(uuid, kElmServiceUuid)) {
        return AKITA_OBD_PROFILE_ELM327_SERIAL;
    }

    if (akita_uuid_equals_text(uuid, kNusServiceUuid)) {
        return AKITA_OBD_PROFILE_NUS;
    }

    return AKITA_OBD_PROFILE_UNKNOWN;
}

static bool akita_service_matches_target(const ble_uuid_t *uuid, akita_obd_profile_t *profile) {
    akita_obd_profile_t resolved_profile;

    if (uuid == NULL) {
        return false;
    }

    resolved_profile = akita_profile_from_uuid(uuid);

    if (g_target_service_uuid[0] != '\0') {
        if (akita_uuid_equals_text(uuid, g_target_service_uuid)) {
            if (profile != NULL) {
                *profile = resolved_profile;
            }
            return true;
        }
        return false;
    }

    if (resolved_profile != AKITA_OBD_PROFILE_UNKNOWN) {
        if (profile != NULL) {
            *profile = resolved_profile;
        }
        return true;
    }

    return false;
}

static bool akita_adv_contains_target_service(const struct ble_hs_adv_fields *fields) {
    size_t index;

    if (fields == NULL) {
        return false;
    }

    for (index = 0; index < fields->num_uuids16; ++index) {
        if (akita_service_matches_target(&fields->uuids16[index].u, NULL)) {
            return true;
        }
    }

    for (index = 0; index < fields->num_uuids32; ++index) {
        if (akita_service_matches_target(&fields->uuids32[index].u, NULL)) {
            return true;
        }
    }

    for (index = 0; index < fields->num_uuids128; ++index) {
        if (akita_service_matches_target(&fields->uuids128[index].u, NULL)) {
            return true;
        }
    }

    return false;
}

static bool akita_adv_name_matches(const struct ble_hs_adv_fields *fields) {
    size_t target_length;

    if (g_config.obd_device_name[0] == '\0') {
        return false;
    }

    if (fields == NULL || fields->name == NULL || fields->name_len == 0U) {
        return false;
    }

    target_length = strlen(g_config.obd_device_name);
    return target_length == fields->name_len &&
           strncasecmp((const char *) fields->name, g_config.obd_device_name, target_length) == 0;
}

static bool akita_should_connect(const struct ble_gap_disc_desc *disc) {
    struct ble_hs_adv_fields fields;
    bool name_match = false;
    bool uuid_match = false;
    int rc;

    if (disc == NULL) {
        return false;
    }

    memset(&fields, 0, sizeof(fields));
    rc = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
    if (rc != 0) {
        return false;
    }

    if (g_config.obd_device_name[0] != '\0') {
        name_match = akita_adv_name_matches(&fields);
    }

    if (g_config.use_obd_uuid || g_target_service_uuid[0] != '\0') {
        uuid_match = akita_adv_contains_target_service(&fields);
    }

    if (g_config.obd_device_name[0] != '\0' && !g_config.use_obd_uuid && g_target_service_uuid[0] == '\0') {
        return name_match;
    }

    if (g_config.obd_device_name[0] == '\0' && (g_config.use_obd_uuid || g_target_service_uuid[0] != '\0')) {
        return uuid_match;
    }

    if (g_config.obd_device_name[0] != '\0' && (g_config.use_obd_uuid || g_target_service_uuid[0] != '\0')) {
        return name_match || uuid_match;
    }

    return true;
}

static const char *akita_current_command(void) {
    if (g_init_command_index < AKITA_ARRAY_LEN(kInitCommands)) {
        return kInitCommands[g_init_command_index];
    }

    return kTelemetryCommands[g_pid_command_index % AKITA_ARRAY_LEN(kTelemetryCommands)];
}

static void akita_schedule_retry(uint32_t delay_ms) {
    g_pending_response = false;
    g_read_in_flight = false;
    g_read_due_ms = 0;
    g_command_started_ms = 0;
    akita_clear_response_buffer();
    g_next_command_at_ms = akita_now_ms() + delay_ms;
}

static void akita_complete_pending_command(void) {
    bool init_phase;

    if (!g_pending_response) {
        return;
    }

    init_phase = g_init_command_index < AKITA_ARRAY_LEN(kInitCommands);
    if (init_phase) {
        ++g_init_command_index;
    } else {
        g_pid_command_index = (g_pid_command_index + 1U) % AKITA_ARRAY_LEN(kTelemetryCommands);
    }

    g_pending_response = false;
    g_read_in_flight = false;
    g_read_due_ms = 0;
    g_command_started_ms = 0;
    akita_clear_response_buffer();
    g_next_command_at_ms = akita_now_ms() + (init_phase ? AKITA_OBD_INIT_DELAY_MS : AKITA_OBD_PID_DELAY_MS);
}

static void akita_process_response_text(const char *text, size_t length, bool force_complete) {
    size_t available;
    size_t copy_length = 0;
    bool parsed = false;
    bool has_prompt = false;

    if (text != NULL && length > 0U) {
        available = (sizeof(g_rx_buffer) - 1U) - g_rx_length;
        copy_length = length < available ? length : available;
        if (copy_length > 0U) {
            memcpy(g_rx_buffer + g_rx_length, text, copy_length);
            g_rx_length += copy_length;
            g_rx_buffer[g_rx_length] = '\0';
        }
        has_prompt = memchr(text, '>', length) != NULL;
    }

    if (g_rx_length > 0U) {
        parsed = akita_obd_apply_response(&g_obd_state, g_rx_buffer);
        if (strchr(g_rx_buffer, '>') != NULL) {
            has_prompt = true;
        }
    }

    if ((parsed || has_prompt || force_complete) && g_pending_response) {
        akita_complete_pending_command();
    }
}

static void akita_complete_link_setup(void) {
    uint8_t response_properties;

    if (g_notify_handle == 0U && g_write_handle != 0U &&
        (g_write_properties & (BLE_GATT_CHR_PROP_NOTIFY | BLE_GATT_CHR_PROP_INDICATE | BLE_GATT_CHR_PROP_READ)) != 0U) {
        g_notify_handle = g_write_handle;
        g_notify_properties = g_write_properties;
    }

    response_properties = g_notify_handle != 0U ? g_notify_properties : g_write_properties;
    g_use_read_fallback = !g_notifications_enabled &&
                          (response_properties & BLE_GATT_CHR_PROP_READ) != 0U &&
                          g_notify_handle != 0U;
    g_obd_ready = g_write_handle != 0U && (g_notifications_enabled || g_use_read_fallback);
    g_obd_state.connected = g_conn_handle != AKITA_OBD_CONN_HANDLE_NONE;

    if (!g_obd_ready) {
        ESP_LOGW(TAG, "Connected to adapter, but no usable OBD response characteristic was found");
        return;
    }

    g_init_command_index = 0;
    g_pid_command_index = 0;
    g_pending_response = false;
    g_read_in_flight = false;
    g_read_due_ms = 0;
    g_command_started_ms = 0;
    akita_clear_response_buffer();
    g_next_command_at_ms = akita_now_ms();

    ESP_LOGI(TAG, "OBD adapter ready over BLE (%s)",
             g_profile == AKITA_OBD_PROFILE_NUS ? "NUS" : "serial characteristic");
}

static void akita_on_reset(int reason) {
    ESP_LOGW(TAG, "NimBLE host reset: %d", reason);
    g_host_synced = false;
    akita_reset_link_state();
}

static esp_err_t akita_start_scan(void) {
    struct ble_gap_disc_params disc_params = {0};
    int rc;

    if (!g_host_synced || g_scan_active || g_connecting || g_conn_handle != AKITA_OBD_CONN_HANDLE_NONE) {
        return ESP_OK;
    }

    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Unable to infer BLE address type: %d", rc);
        return ESP_FAIL;
    }

    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;
    disc_params.passive = 0;
    disc_params.filter_duplicates = 1;
    disc_params.disable_observer_mode = 0;

    rc = ble_gap_disc(g_own_addr_type, BLE_HS_FOREVER, &disc_params, akita_obd_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Unable to start BLE scan: %d", rc);
        return ESP_FAIL;
    }

    g_scan_active = true;
    ESP_LOGI(TAG, "Scanning for OBD adapters");
    return ESP_OK;
}

static void akita_connect_to_peer(const ble_addr_t *peer_addr) {
    int rc;

    if (peer_addr == NULL) {
        return;
    }

    rc = ble_gap_connect(g_own_addr_type, peer_addr, AKITA_OBD_CONNECT_TIMEOUT_MS, NULL,
                         akita_obd_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "BLE connect failed to start: %d", rc);
        g_connecting = false;
        (void) akita_start_scan();
        return;
    }

    g_connecting = true;
    ESP_LOGI(TAG, "Connecting to BLE OBD adapter");
}

static void akita_on_sync(void) {
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Unable to provision BLE identity address: %d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Unable to infer BLE address type: %d", rc);
        return;
    }

    g_host_synced = true;
    (void) akita_start_scan();
}

static bool akita_characteristic_matches_target(const struct ble_gatt_chr *chr,
                                                uint16_t *write_handle, uint8_t *write_properties,
                                                uint16_t *notify_handle, uint8_t *notify_properties) {
    bool matched = false;

    if (chr == NULL) {
        return false;
    }

    if (g_target_characteristic_uuid[0] != '\0') {
        if (akita_uuid_equals_text(&chr->uuid.u, g_target_characteristic_uuid)) {
            matched = true;
            if ((chr->properties & (BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_WRITE_NO_RSP)) != 0U) {
                *write_handle = chr->val_handle;
                *write_properties = chr->properties;
            }
            if ((chr->properties & (BLE_GATT_CHR_PROP_NOTIFY | BLE_GATT_CHR_PROP_INDICATE | BLE_GATT_CHR_PROP_READ)) != 0U) {
                *notify_handle = chr->val_handle;
                *notify_properties = chr->properties;
            }
        }
        return matched;
    }

    if (g_profile == AKITA_OBD_PROFILE_ELM327_SERIAL && akita_uuid_equals_text(&chr->uuid.u, kElmCharacteristicUuid)) {
        *write_handle = chr->val_handle;
        *write_properties = chr->properties;
        *notify_handle = chr->val_handle;
        *notify_properties = chr->properties;
        return true;
    }

    if (g_profile == AKITA_OBD_PROFILE_NUS) {
        if (akita_uuid_equals_text(&chr->uuid.u, kNusWriteUuid)) {
            *write_handle = chr->val_handle;
            *write_properties = chr->properties;
            matched = true;
        }
        if (akita_uuid_equals_text(&chr->uuid.u, kNusNotifyUuid)) {
            *notify_handle = chr->val_handle;
            *notify_properties = chr->properties;
            matched = true;
        }
        if (matched) {
            return true;
        }
    }

    if (*write_handle == 0U && (chr->properties & (BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_WRITE_NO_RSP)) != 0U) {
        *write_handle = chr->val_handle;
        *write_properties = chr->properties;
        matched = true;
    }

    if (*notify_handle == 0U && (chr->properties & (BLE_GATT_CHR_PROP_NOTIFY | BLE_GATT_CHR_PROP_INDICATE | BLE_GATT_CHR_PROP_READ)) != 0U) {
        *notify_handle = chr->val_handle;
        *notify_properties = chr->properties;
        matched = true;
    }

    return matched;
}

static int akita_obd_on_mtu_exchanged(uint16_t conn_handle, const struct ble_gatt_error *error,
                                      uint16_t mtu, void *arg) {
    int rc;

    (void) arg;

    if (conn_handle != g_conn_handle) {
        return 0;
    }

    if (error != NULL && error->status != 0U) {
        ESP_LOGW(TAG, "MTU exchange failed: %u", error->status);
    } else {
        ESP_LOGI(TAG, "BLE MTU negotiated to %u", mtu);
    }

    rc = ble_gattc_disc_all_svcs(conn_handle, akita_obd_on_service_discovered, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Service discovery failed to start: %d", rc);
    }
    return 0;
}

static int akita_obd_on_service_discovered(uint16_t conn_handle, const struct ble_gatt_error *error,
                                           const struct ble_gatt_svc *service, void *arg) {
    int rc;
    akita_obd_profile_t profile;

    (void) arg;

    if (conn_handle != g_conn_handle || error == NULL) {
        return 0;
    }

    if (error->status == 0U && service != NULL) {
        if (g_service_start_handle == 0U && akita_service_matches_target(&service->uuid.u, &profile)) {
            g_service_start_handle = service->start_handle;
            g_service_end_handle = service->end_handle;
            g_profile = profile;
        }
        return 0;
    }

    if (error->status != BLE_HS_EDONE) {
        ESP_LOGW(TAG, "Service discovery ended with status %u", error->status);
        return 0;
    }

    if (g_service_start_handle == 0U) {
        ESP_LOGW(TAG, "No matching BLE OBD service found on the connected adapter");
        return 0;
    }

    rc = ble_gattc_disc_all_chrs(conn_handle, g_service_start_handle, g_service_end_handle,
                                 akita_obd_on_characteristic_discovered, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Characteristic discovery failed to start: %d", rc);
    }

    return 0;
}

static int akita_obd_on_characteristic_discovered(uint16_t conn_handle, const struct ble_gatt_error *error,
                                                  const struct ble_gatt_chr *chr, void *arg) {
    int rc;

    (void) arg;

    if (conn_handle != g_conn_handle || error == NULL) {
        return 0;
    }

    if (error->status == 0U && chr != NULL) {
        (void) akita_characteristic_matches_target(chr, &g_write_handle, &g_write_properties,
                                                   &g_notify_handle, &g_notify_properties);
        return 0;
    }

    if (error->status != BLE_HS_EDONE) {
        ESP_LOGW(TAG, "Characteristic discovery ended with status %u", error->status);
        return 0;
    }

    if (g_notify_handle == 0U && g_write_handle != 0U &&
        (g_write_properties & (BLE_GATT_CHR_PROP_NOTIFY | BLE_GATT_CHR_PROP_INDICATE | BLE_GATT_CHR_PROP_READ)) != 0U) {
        g_notify_handle = g_write_handle;
        g_notify_properties = g_write_properties;
    }

    if (g_write_handle == 0U) {
        ESP_LOGW(TAG, "No writable OBD characteristic found");
        return 0;
    }

    if (g_notify_handle != 0U &&
        (g_notify_properties & (BLE_GATT_CHR_PROP_NOTIFY | BLE_GATT_CHR_PROP_INDICATE)) != 0U) {
        rc = ble_gattc_disc_all_dscs(conn_handle, g_notify_handle, g_service_end_handle,
                                     akita_obd_on_descriptor_discovered, NULL);
        if (rc != 0) {
            ESP_LOGW(TAG, "Descriptor discovery failed to start: %d", rc);
            akita_complete_link_setup();
        }
        return 0;
    }

    akita_complete_link_setup();
    return 0;
}

static int akita_obd_on_descriptor_discovered(uint16_t conn_handle, const struct ble_gatt_error *error,
                                              uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                                              void *arg) {
    uint8_t cccd_value[2] = {0x01, 0x00};
    int rc;

    (void) arg;
    (void) chr_val_handle;

    if (conn_handle != g_conn_handle || error == NULL) {
        return 0;
    }

    if (error->status == 0U && dsc != NULL) {
        if (dsc->uuid.u.type == BLE_UUID_TYPE_16 && BLE_UUID16(&dsc->uuid.u)->value == BLE_GATT_DSC_CLT_CFG_UUID16) {
            g_cccd_handle = dsc->handle;
        }
        return 0;
    }

    if (error->status != BLE_HS_EDONE) {
        ESP_LOGW(TAG, "Descriptor discovery ended with status %u", error->status);
        akita_complete_link_setup();
        return 0;
    }

    if (g_cccd_handle == 0U) {
        akita_complete_link_setup();
        return 0;
    }

    if ((g_notify_properties & BLE_GATT_CHR_PROP_INDICATE) != 0U &&
        (g_notify_properties & BLE_GATT_CHR_PROP_NOTIFY) == 0U) {
        cccd_value[0] = 0x02;
    }

    rc = ble_gattc_write_flat(conn_handle, g_cccd_handle, cccd_value, sizeof(cccd_value),
                              akita_obd_on_write_complete, (void *) "cccd");
    if (rc != 0) {
        ESP_LOGW(TAG, "CCCD subscription failed to start: %d", rc);
        akita_complete_link_setup();
    }

    return 0;
}

static int akita_obd_on_write_complete(uint16_t conn_handle, const struct ble_gatt_error *error,
                                       struct ble_gatt_attr *attr, void *arg) {
    const char *write_kind = (const char *) arg;

    (void) attr;

    if (conn_handle != g_conn_handle || error == NULL) {
        return 0;
    }

    if (write_kind != NULL && strcmp(write_kind, "cccd") == 0) {
        if (error->status == 0U) {
            g_notifications_enabled = true;
        } else {
            ESP_LOGW(TAG, "Notification subscription write failed: %u", error->status);
        }
        akita_complete_link_setup();
        return 0;
    }

    if (error->status != 0U) {
        ESP_LOGW(TAG, "OBD command write failed: %u", error->status);
        akita_schedule_retry(500U);
        return 0;
    }

    if (g_use_read_fallback) {
        g_read_due_ms = akita_now_ms() + AKITA_OBD_READ_DELAY_MS;
    }

    return 0;
}

static int akita_obd_on_read_complete(uint16_t conn_handle, const struct ble_gatt_error *error,
                                      struct ble_gatt_attr *attr, void *arg) {
    char payload[AKITA_OBD_RX_BUFFER_SIZE];
    uint16_t copied_length = 0;
    int rc;

    (void) arg;

    if (conn_handle != g_conn_handle || error == NULL) {
        return 0;
    }

    g_read_in_flight = false;
    if (error->status != 0U || attr == NULL || attr->om == NULL) {
        if (g_pending_response) {
            akita_complete_pending_command();
        }
        return 0;
    }

    rc = ble_hs_mbuf_to_flat(attr->om, payload, sizeof(payload) - 1U, &copied_length);
    if (rc != 0) {
        if (g_pending_response) {
            akita_complete_pending_command();
        }
        return 0;
    }

    payload[copied_length] = '\0';
    akita_process_response_text(payload, copied_length, true);
    return 0;
}

static int akita_obd_gap_event(struct ble_gap_event *event, void *arg) {
    int rc;
    char payload[AKITA_OBD_RX_BUFFER_SIZE];
    uint16_t copied_length = 0;

    (void) arg;

    if (event == NULL) {
        return 0;
    }

    switch (event->type) {
        case BLE_GAP_EVENT_DISC:
            if (!g_connecting && akita_should_connect(&event->disc)) {
                g_pending_peer_addr = event->disc.addr;
                g_connect_after_scan = true;
                (void) ble_gap_disc_cancel();
            }
            return 0;

        case BLE_GAP_EVENT_DISC_COMPLETE:
            g_scan_active = false;
            if (g_connect_after_scan) {
                g_connect_after_scan = false;
                akita_connect_to_peer(&g_pending_peer_addr);
            } else if (g_conn_handle == AKITA_OBD_CONN_HANDLE_NONE) {
                (void) akita_start_scan();
            }
            return 0;

        case BLE_GAP_EVENT_CONNECT:
            g_connecting = false;
            if (event->connect.status != 0) {
                ESP_LOGW(TAG, "BLE connection failed: %d", event->connect.status);
                (void) akita_start_scan();
                return 0;
            }

            g_conn_handle = event->connect.conn_handle;
            g_obd_state.connected = true;
            ESP_LOGI(TAG, "Connected to BLE OBD adapter");

            rc = ble_gattc_exchange_mtu(g_conn_handle, akita_obd_on_mtu_exchanged, NULL);
            if (rc != 0) {
                ESP_LOGW(TAG, "MTU exchange failed to start: %d", rc);
                rc = ble_gattc_disc_all_svcs(g_conn_handle, akita_obd_on_service_discovered, NULL);
                if (rc != 0) {
                    ESP_LOGE(TAG, "Service discovery failed to start: %d", rc);
                }
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGW(TAG, "BLE disconnected: %d", event->disconnect.reason);
            akita_reset_link_state();
            if (g_host_synced) {
                (void) akita_start_scan();
            }
            return 0;

        case BLE_GAP_EVENT_NOTIFY_RX:
            if (event->notify_rx.conn_handle != g_conn_handle ||
                (event->notify_rx.attr_handle != g_notify_handle &&
                 event->notify_rx.attr_handle != g_write_handle)) {
                return 0;
            }

            rc = ble_hs_mbuf_to_flat(event->notify_rx.om, payload, sizeof(payload) - 1U, &copied_length);
            if (rc == 0) {
                payload[copied_length] = '\0';
                akita_process_response_text(payload, copied_length, false);
            }
            return 0;

        default:
            return 0;
    }
}

static void akita_obd_host_task(void *param) {
    (void) param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static uint8_t akita_hex_u8(const char *text) {
    char scratch[3] = { text[0], text[1], '\0' };
    return (uint8_t) strtoul(scratch, NULL, 16);
}

esp_err_t akita_obd_init(const akita_runtime_config_t *config) {
    bool host_synced;

    memset(&g_obd_state, 0, sizeof(g_obd_state));
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    host_synced = g_host_synced;
    if (g_stack_started) {
        akita_obd_stop_link_activity();
    }

    memcpy(&g_config, config, sizeof(g_config));
    akita_reset_link_state();
    akita_copy_normalized_uuid(g_config.obd_service_uuid, g_target_service_uuid, sizeof(g_target_service_uuid));
    akita_copy_normalized_uuid(g_config.obd_characteristic_uuid, g_target_characteristic_uuid,
                               sizeof(g_target_characteristic_uuid));

    if (!g_stack_started) {
        int rc = nimble_port_init();
        if (rc != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize NimBLE host: %d", rc);
            return ESP_FAIL;
        }

        ble_hs_cfg.reset_cb = akita_on_reset;
        ble_hs_cfg.sync_cb = akita_on_sync;
        nimble_port_freertos_init(akita_obd_host_task);
        g_stack_started = true;
    } else if (host_synced) {
        g_host_synced = true;
        (void) akita_start_scan();
    }

    ESP_LOGI(TAG, "Starting native BLE OBD client");
    return ESP_OK;
}

void akita_obd_poll(akita_obd_snapshot_t *snapshot) {
    uint64_t now_ms = (uint64_t) (esp_timer_get_time() / 1000ULL);
    const char *command;
    char request[16];
    size_t request_length;
    int rc;

    if (snapshot == NULL) {
        return;
    }

    if (g_host_synced && !g_scan_active && !g_connecting && g_conn_handle == AKITA_OBD_CONN_HANDLE_NONE) {
        (void) akita_start_scan();
    }

    if (g_pending_response && g_command_started_ms > 0U &&
        (now_ms - g_command_started_ms) >= AKITA_OBD_RESPONSE_TIMEOUT_MS) {
        ESP_LOGW(TAG, "Timed out waiting for OBD response");
        akita_complete_pending_command();
    }

    if (g_pending_response && g_use_read_fallback && !g_read_in_flight && g_read_due_ms > 0U && now_ms >= g_read_due_ms) {
        rc = ble_gattc_read(g_conn_handle, g_notify_handle, akita_obd_on_read_complete, NULL);
        if (rc == 0) {
            g_read_in_flight = true;
            g_read_due_ms = 0;
        } else {
            ESP_LOGW(TAG, "OBD read fallback failed to start: %d", rc);
            akita_complete_pending_command();
        }
    }

    if (g_obd_ready && !g_pending_response && g_next_command_at_ms > 0U && now_ms >= g_next_command_at_ms) {
        command = akita_current_command();
        request_length = akita_obd_build_request(command, request, sizeof(request));
        if (request_length == 0U) {
            akita_schedule_retry(500U);
        } else if ((g_write_properties & BLE_GATT_CHR_PROP_WRITE_NO_RSP) != 0U) {
            rc = ble_gattc_write_no_rsp_flat(g_conn_handle, g_write_handle, request, request_length);
            if (rc != 0) {
                ESP_LOGW(TAG, "OBD write without response failed: %d", rc);
                akita_schedule_retry(500U);
            } else {
                g_pending_response = true;
                g_command_started_ms = now_ms;
                g_read_due_ms = g_use_read_fallback ? (now_ms + AKITA_OBD_READ_DELAY_MS) : 0U;
                akita_clear_response_buffer();
            }
        } else {
            rc = ble_gattc_write_flat(g_conn_handle, g_write_handle, request, request_length,
                                      akita_obd_on_write_complete, (void *) "command");
            if (rc != 0) {
                ESP_LOGW(TAG, "OBD write failed: %d", rc);
                akita_schedule_retry(500U);
            } else {
                g_pending_response = true;
                g_command_started_ms = now_ms;
                akita_clear_response_buffer();
            }
        }
    }

    if (g_last_sample_ms > 0U) {
        g_obd_state.age_ms = (uint32_t) (now_ms - g_last_sample_ms);
    }

    g_obd_state.connected = g_conn_handle != AKITA_OBD_CONN_HANDLE_NONE;
    *snapshot = g_obd_state;
}

size_t akita_obd_build_request(const char *pid, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0 || pid == NULL) {
        return 0;
    }

    return (size_t) snprintf(buffer, buffer_size, "%s\r", pid);
}

bool akita_obd_apply_response(akita_obd_snapshot_t *snapshot, const char *response) {
    char cleaned[AKITA_OBD_RX_BUFFER_SIZE];
    const char *frame;
    size_t in_index = 0;
    size_t out_index = 0;

    if (snapshot == NULL || response == NULL) {
        return false;
    }

    while (response[in_index] != '\0' && out_index < (sizeof(cleaned) - 1U)) {
        if (response[in_index] != ' ' && response[in_index] != '>' && response[in_index] != '\r' && response[in_index] != '\n') {
            cleaned[out_index++] = (char) toupper((unsigned char) response[in_index]);
        }
        ++in_index;
    }
    cleaned[out_index] = '\0';

    frame = strstr(cleaned, "410C");
    if (frame != NULL && strlen(frame) >= 8U) {
        snapshot->rpm = (float) (((akita_hex_u8(frame + 4) * 256U) + akita_hex_u8(frame + 6)) / 4.0f);
    } else {
        frame = strstr(cleaned, "410D");
        if (frame != NULL && strlen(frame) >= 6U) {
            snapshot->speed_kmh = (float) akita_hex_u8(frame + 4);
        } else {
            frame = strstr(cleaned, "4105");
            if (frame != NULL && strlen(frame) >= 6U) {
                snapshot->coolant_c = (float) akita_hex_u8(frame + 4) - 40.0f;
            } else {
                return false;
            }
        }
    }

    snapshot->connected = g_conn_handle != AKITA_OBD_CONN_HANDLE_NONE;
    g_obd_state = *snapshot;
    g_last_sample_ms = akita_now_ms();
    return true;
}