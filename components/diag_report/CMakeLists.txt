idf_component_register(
    SRCS
        "report_generator.c"
        "health_metrics.c"
        "report_export.c"
    INCLUDE_DIRS "."
    REQUIRES modbus_core modbus_scanner protocol_analyzer wifi_service
)
