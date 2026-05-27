idf_component_register(
    SRCS
        "key_scan.c"
        "input_event.c"
    INCLUDE_DIRS "."
    REQUIRES driver common_config
)
