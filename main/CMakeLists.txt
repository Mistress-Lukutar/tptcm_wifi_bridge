idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "include"
    REQUIRES
        printer_uart
        raw_tcp_server
        wifi_manager
        log_stream
        ota_server
        app_update
        nvs_flash
)
