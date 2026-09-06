idf_component_register(
    SRCS "wifi_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES main esp_wifi esp_event esp_netif esp_http_server nvs_flash
             esp_timer lwip
)
