idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        modbus_core
        modbus_scanner
        protocol_analyzer
        waveform
        diag_report
        display
        input
        storage
        wifi_service
        common_config
        driver
        esp_wifi
        efuse
        nvs_flash
        spiffs
)
