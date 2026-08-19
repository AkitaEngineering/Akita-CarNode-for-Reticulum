idf_component_register(
    SRCS "src/akita_obd.c"
    INCLUDE_DIRS "include"
    REQUIRES akita_common bt esp_timer freertos
)