idf_component_register(
    SRCS
        "device_scanner.c"
        "register_classifier.c"
    INCLUDE_DIRS "."
    REQUIRES modbus_core common_config
)
