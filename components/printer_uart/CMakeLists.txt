idf_component_register(
    SRCS "printer_uart.c"
    INCLUDE_DIRS "include"
    REQUIRES main esp_driver_uart
)
