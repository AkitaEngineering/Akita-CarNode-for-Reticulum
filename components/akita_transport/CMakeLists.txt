idf_component_register(
    SRCS "src/akita_transport.c"
    INCLUDE_DIRS "include"
    REQUIRES akita_common driver esp_event esp_http_client esp_netif esp_timer esp_wifi lwip mbedtls freertos
)