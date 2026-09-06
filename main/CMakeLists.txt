idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "include"
    REQUIRES
        printer_uart
        raw_tcp_server
        wifi_manager
        nvs_flash
)
