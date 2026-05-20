idf_component_register(
    SRCS "src/akita_transport.c"
    INCLUDE_DIRS "include"
    REQUIRES akita_common esp_event esp_http_client esp_netif esp_wifi lwip
)