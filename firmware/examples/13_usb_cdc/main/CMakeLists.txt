idf_component_register(SRCS "usb_cdc_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_i2c esp_driver_gpio nvs_flash esp_timer app_update
                       PRIV_REQUIRES esp_tinyusb)
