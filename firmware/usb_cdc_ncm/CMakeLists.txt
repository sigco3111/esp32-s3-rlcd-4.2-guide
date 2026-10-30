idf_component_register(SRCS "usb_ncm_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES lwip nvs_flash esp_timer
                       PRIV_REQUIRES esp_tinyusb)
