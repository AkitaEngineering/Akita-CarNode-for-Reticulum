#ifndef AKITA_TRANSPORT_H
#define AKITA_TRANSPORT_H

#include <stdbool.h>

#include "akita_types.h"
#include "esp_err.h"

typedef struct {
	bool transport_ready;
	bool bridge_ready;
	bool lora_ready;
	bool wifi_connected;
	int8_t wifi_rssi;
	char bridge_mode[16];
	char bridge_last_error[64];
} akita_transport_status_t;

esp_err_t akita_transport_init(const akita_runtime_config_t *config);
esp_err_t akita_transport_publish(const akita_runtime_config_t *config, const char *payload);
void akita_transport_poll(const akita_runtime_config_t *config);
bool akita_transport_ready(void);
void akita_transport_get_status(akita_transport_status_t *status);
const char *akita_transport_name(const akita_runtime_config_t *config);

#endif
