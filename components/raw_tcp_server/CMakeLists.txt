idf_component_register(
    SRCS "raw_tcp_server.c"
    INCLUDE_DIRS "include"
    REQUIRES main printer_uart lwip
)
