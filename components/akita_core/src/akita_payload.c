#include "akita_app.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "akita_board.h"
#include "esp_timer.h"

static size_t akita_append_text(char *buffer, size_t buffer_size, size_t used, const char *text) {
    size_t remaining;
    int written;

    if (used >= buffer_size) {
        return used;
    }

    remaining = buffer_size - used;
    written = snprintf(buffer + used, remaining, "%s", text);
    if (written < 0) {
        return used;
    }

    if ((size_t) written >= remaining) {
        return buffer_size;
    }

    return used + (size_t) written;
}

static size_t akita_append_format(char *buffer, size_t buffer_size, size_t used, const char *format, ...) {
    va_list args;
    size_t remaining;
    int written;

    if (used >= buffer_size) {
        return used;
    }

    remaining = buffer_size - used;
    va_start(args, format);
    written = vsnprintf(buffer + used, remaining, format, args);
    va_end(args);

    if (written < 0) {
        return used;
    }

    if ((size_t) written >= remaining) {
        return buffer_size;
    }

    return used + (size_t) written;
}

static size_t akita_append_json_string(char *buffer, size_t buffer_size, size_t used, const char *text) {
    const char *cursor = text != NULL ? text : "";

    used = akita_append_text(buffer, buffer_size, used, "\"");
    while (*cursor != '\0' && used < buffer_size) {
        if (*cursor == '"' || *cursor == '\\') {
            used = akita_append_format(buffer, buffer_size, used, "\\%c", *cursor);
        } else if ((unsigned char) *cursor < 32U) {
            used = akita_append_format(buffer, buffer_size, used, "\\u%04x", (unsigned char) *cursor);
        } else {
            char scratch[2] = { *cursor, '\0' };
            used = akita_append_text(buffer, buffer_size, used, scratch);
        }
        ++cursor;
    }
    used = akita_append_text(buffer, buffer_size, used, "\"");

    return used;
}

size_t akita_payload_write_json(
    const akita_runtime_config_t *config,
    const akita_vehicle_telemetry_t *telemetry,
    char *buffer,
    size_t buffer_size
) {
    size_t used = 0;
    uint64_t timestamp_ms = (uint64_t) (esp_timer_get_time() / 1000ULL);

    if (buffer == NULL || buffer_size == 0 || config == NULL || telemetry == NULL) {
        return 0;
    }

    buffer[0] = '\0';
    used = akita_append_text(buffer, buffer_size, used, "{");
    used = akita_append_text(buffer, buffer_size, used, "\"node_id\":");
    used = akita_append_json_string(buffer, buffer_size, used, config->vehicle_id);
    used = akita_append_format(buffer, buffer_size, used, ",\"timestamp_ms\":%" PRIu64, timestamp_ms);
    used = akita_append_text(buffer, buffer_size, used, ",\"board\":");
    used = akita_append_json_string(buffer, buffer_size, used, akita_board_get_name(config->board_profile));
    used = akita_append_text(buffer, buffer_size, used, ",\"obd\":{");
    used = akita_append_format(buffer, buffer_size, used, "\"connected\":%s", telemetry->obd.connected ? "true" : "false");
    used = akita_append_format(buffer, buffer_size, used, ",\"rpm\":%.1f", telemetry->obd.rpm);
    used = akita_append_format(buffer, buffer_size, used, ",\"speed_kmh\":%.1f", telemetry->obd.speed_kmh);
    used = akita_append_format(buffer, buffer_size, used, ",\"coolant_c\":%.1f", telemetry->obd.coolant_c);
    used = akita_append_text(buffer, buffer_size, used, "}");
    used = akita_append_text(buffer, buffer_size, used, ",\"gps\":{");
    used = akita_append_format(buffer, buffer_size, used, "\"fix\":%s", telemetry->gps.fix ? "true" : "false");
    used = akita_append_format(buffer, buffer_size, used, ",\"lat\":%.6f", telemetry->gps.latitude);
    used = akita_append_format(buffer, buffer_size, used, ",\"lon\":%.6f", telemetry->gps.longitude);
    used = akita_append_format(buffer, buffer_size, used, ",\"alt_m\":%.1f", telemetry->gps.altitude_m);
    used = akita_append_format(buffer, buffer_size, used, ",\"speed_kmh\":%.1f", telemetry->gps.speed_kmh);
    used = akita_append_format(buffer, buffer_size, used, ",\"sats\":%u", telemetry->gps.satellites);
    used = akita_append_text(buffer, buffer_size, used, "}");
    used = akita_append_text(buffer, buffer_size, used, ",\"system\":{");
    used = akita_append_format(buffer, buffer_size, used, "\"config_portal_ready\":%s", telemetry->system.config_portal_ready ? "true" : "false");
    used = akita_append_format(buffer, buffer_size, used, ",\"transport_ready\":%s", telemetry->system.transport_ready ? "true" : "false");
    used = akita_append_format(buffer, buffer_size, used, ",\"wifi_ready\":%s", telemetry->system.wifi_ready ? "true" : "false");
    used = akita_append_format(buffer, buffer_size, used, ",\"lora_ready\":%s", telemetry->system.lora_ready ? "true" : "false");
    used = akita_append_format(buffer, buffer_size, used, ",\"wifi_rssi\":%d", (int) telemetry->system.wifi_rssi);
    used = akita_append_format(buffer, buffer_size, used, ",\"free_heap\":%lu", (unsigned long) telemetry->system.free_heap);
    used = akita_append_text(buffer, buffer_size, used, "}");
    used = akita_append_text(buffer, buffer_size, used, "}");

    if (used >= buffer_size) {
        buffer[buffer_size - 1] = '\0';
        return 0;
    }

    return used;
}

size_t akita_payload_write_compact_json(
    const akita_runtime_config_t *config,
    const akita_vehicle_telemetry_t *telemetry,
    char *buffer,
    size_t buffer_size
) {
    size_t used = 0;
    uint64_t timestamp_ms = (uint64_t) (esp_timer_get_time() / 1000ULL);

    if (buffer == NULL || buffer_size == 0 || config == NULL || telemetry == NULL) {
        return 0;
    }

    buffer[0] = '\0';
    used = akita_append_text(buffer, buffer_size, used, "{");
    used = akita_append_text(buffer, buffer_size, used, "\"n\":");
    used = akita_append_json_string(buffer, buffer_size, used, config->vehicle_id);
    used = akita_append_format(buffer, buffer_size, used, ",\"t\":%" PRIu64, timestamp_ms);
    used = akita_append_format(
        buffer,
        buffer_size,
        used,
        ",\"o\":{\"c\":%u,\"r\":%.1f,\"s\":%.1f,\"k\":%.1f}",
        telemetry->obd.connected ? 1U : 0U,
        telemetry->obd.rpm,
        telemetry->obd.speed_kmh,
        telemetry->obd.coolant_c
    );
    used = akita_append_format(
        buffer,
        buffer_size,
        used,
        ",\"g\":{\"f\":%u,\"la\":%.6f,\"lo\":%.6f,\"v\":%.1f,\"s\":%u}",
        telemetry->gps.fix ? 1U : 0U,
        telemetry->gps.latitude,
        telemetry->gps.longitude,
        telemetry->gps.speed_kmh,
        telemetry->gps.satellites
    );
    used = akita_append_format(
        buffer,
        buffer_size,
        used,
        ",\"h\":%lu,\"q\":%d}",
        (unsigned long) telemetry->system.free_heap,
        (int) telemetry->system.wifi_rssi
    );

    if (used >= buffer_size) {
        buffer[buffer_size - 1] = '\0';
        return 0;
    }

    return used;
}