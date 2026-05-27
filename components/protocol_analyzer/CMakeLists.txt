idf_component_register(
    SRCS
        "packet_capture.c"
        "frame_parser.c"
        "traffic_stats.c"
    INCLUDE_DIRS "."
    REQUIRES driver modbus_core common_config esp_rom
)
