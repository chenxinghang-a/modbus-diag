idf_component_register(
    SRCS
        "wifi_manager.c"
        "web_server.c"
        "modbus_tcp_client.c"
        "mdns_service.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif nvs_flash espressif__mdns esp_http_server common_config
)
