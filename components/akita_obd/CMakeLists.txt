idf_component_register(
    SRCS "src/akita_obd.c"
    INCLUDE_DIRS "include"
    REQUIRES akita_common esp_timer
)