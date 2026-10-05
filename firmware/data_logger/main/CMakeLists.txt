idf_component_register(
    SRCS "data_logger_main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_adc esp_timer esp_driver_usb_serial_jtag vfs esp_psram buzzer board
)
