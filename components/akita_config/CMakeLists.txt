idf_component_register(
    SRCS
        "src/akita_config_store.c"
        "src/akita_config_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES akita_common akita_transport nvs_flash esp_http_server esp_wifi esp_event esp_netif freertos
)