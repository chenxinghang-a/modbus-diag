idf_component_register(
    SRCS
        "nvs_config.c"
        "log_storage.c"
        "file_export.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash spiffs
)
