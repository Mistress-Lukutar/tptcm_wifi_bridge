idf_component_register(
    SRCS "ota_server.c"
    INCLUDE_DIRS "include"
    REQUIRES main log_stream
    PRIV_REQUIRES app_update esp_app_format esp_http_server log freertos
)
