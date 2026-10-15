# debug/t_display/main/CMakeLists.txt
#
# 当前 app_main：t_display.c —— 完整显示功能测试（白/黑/棋盘格/边框）
#
# 下面三个诊断程序仍在同级目录，但已不进编译。需要时把名字换进 SRCS 即可
# （都是未跟踪文件，删掉就永久没了，先留着）：
#   t_signal.c    —— 波形测试：反复跑完整驱动，供逻辑分析仪抓 CS/SCLK/SDI
#   t_epd_diag.c  —— 诊断 POWER/RESET/DC/BUSY 四根线（屏点不亮时用）
#   t_busy_diag.c —— 专查 BUSY（谁在驱动、什么时候变）
idf_component_register(
    SRCS "t_display.c" "epd_display.c"
    INCLUDE_DIRS "." "../../../boards/esp32s3"
    REQUIRES driver
    PRIV_REQUIRES aw9523
)
