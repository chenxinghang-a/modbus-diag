idf_component_register(
    SRCS
        "tft_driver.c"
        "gui_manager.c"
        "ui_widgets.c"
        "ui_edit.c"
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
    # 页面靠 __attribute__((constructor)) 自注册，而静态库只会把「被引用到的」.o 拉进镜像。
    # 不整库链接的话，除 page_home（gui_manager 里 extern 引用了符号）之外**全部页面都会被丢掉**，
    # 编译照样过、map 里却查不到 page_diag.c.obj —— 上板才发现翻页是空的。
    WHOLE_ARCHIVE
)
