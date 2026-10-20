idf_component_register(
    SRCS
        "audio_service.c"
        "audio_capture.c"
        "audio_out_i2s.c"
        "audio_meta.c"
        "pcm_wav.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl esp_timer
    PRIV_REQUIRES controller events board_audio esp-sr
)
