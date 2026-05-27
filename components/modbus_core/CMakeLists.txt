idf_component_register(
    SRCS
        "modbus_rtu.c"
        "modbus_tcp.c"
        "modbus_master.c"
    INCLUDE_DIRS "."
    REQUIRES driver common_config esp_rom
)
