idf_component_register(
    SRCS
        "tft_driver.c"
        "gui_manager.c"
        "ui_widgets.c"
        "pages/page_home.c"
        "pages/page_rtu_scan.c"
        "pages/page_tcp.c"
        "pages/page_capture.c"
        "pages/page_waveform.c"
        "pages/page_diag.c"
        "pages/page_settings.c"
        "pages/page_device_info.c"
    INCLUDE_DIRS "." "pages"
    REQUIRES driver esp_driver_spi input common_config
        modbus_core
        modbus_scanner
        protocol_analyzer
        waveform
        diag_report
        wifi_service
        storage
)
