idf_component_register(SRCS "app_main.c" "sx1278_fsk.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_spi esp_driver_gpio esp_timer freertos log)

