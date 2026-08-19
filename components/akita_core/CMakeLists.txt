idf_component_register(
    SRCS
        "src/akita_app.c"
        "src/akita_payload.c"
    INCLUDE_DIRS "include"
    REQUIRES akita_common akita_config akita_gps akita_obd akita_transport driver esp_timer esp_system freertos nvs_flash
)