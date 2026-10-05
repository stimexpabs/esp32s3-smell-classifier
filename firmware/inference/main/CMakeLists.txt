idf_component_register(
    SRCS "inference_main.cc" "model_data.cc"
    INCLUDE_DIRS "."
    REQUIRES esp_adc esp_timer esp_driver_usb_serial_jtag vfs esp_psram buzzer board esp-tflite-micro
)
