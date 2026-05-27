idf_component_register(
    SRCS
        "adc_sampler.c"
        "signal_process.c"
        "waveform_render.c"
    INCLUDE_DIRS "."
    REQUIRES driver display esp_adc common_config
)
