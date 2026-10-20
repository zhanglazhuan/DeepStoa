# main/CMakeLists.txt
idf_component_register(
    SRCS "DeepStoa.c"
    INCLUDE_DIRS "." "../boards/esp32s3"
    REQUIRES driver lvgl
    PRIV_REQUIRES aw9523 monitor controller appmgr uilv launcher settings chat clock todolist anki calendar news player reader ft6336 audio wifi ota flash time alarm tovo pomodoro mistakebook weather
)
