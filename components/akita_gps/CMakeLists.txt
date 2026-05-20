idf_component_register(
    SRCS "src/akita_gps.c"
    INCLUDE_DIRS "include"
    REQUIRES akita_common driver esp_timer
)