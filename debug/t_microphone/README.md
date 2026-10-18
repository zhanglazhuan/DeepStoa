# t_microphone：双工录音与回声消除测试

独立 ESP-IDF 5.5.3 / ESP32-S3 工程。不修改主应用，不需要 SD 卡或网络。

## 使用

在 ESP-IDF PowerShell 中执行（串口按实际设备替换）：

```powershell
cd D:\Codes\DeepStoa\debug\t_microphone
idf.py build
idf.py -p COM4 flash monitor
```

首次构建会下载 `esp_codec_dev 1.6.2` 和 `esp-sr 2.4.7`。
需要正常的 Octal PSRAM；启动失败时先运行 `debug/t_psram`。
本工程使用自己的 16MB Flash 配置和 4MB 应用分区；刷入时使用完整 `flash`，不能只烧应用文件。

测试流程：

1. 初始化后串口提示准备，等待 3 秒。
2. `DUPLEX START` 后同时播放低电平宽带测试信号，并采集麦克风 20 秒；请在播放期间对麦克风说话。
3. `AEC_OUT` 日志显示消回声后的信号电平；随后播放这段 AEC 输出供主观检查。
4. `TEST DONE` 后按 RESET 重测。

录音为双工期间 AFE/AEC 处理后的 16kHz、16bit、单声道 PCM，存于 PSRAM，大小 640,000 字节。复位即丢失。测试噪声幅度约为满量程的 1/16。
初次测试不要把耳机戴在耳朵上，以免意外高声压。

## 原理图对应

依据 `design/墨水屏-原理图V1.0.pdf` 音频页和 `boards/esp32s3/deepstoa_v1.h`：

| 功能 | 连接 |
| --- | --- |
| I2C SDA/SCL | GPIO17 / GPIO18；ES8311 7-bit 地址 0x18，ES7210 0x40 |
| MCLK/BCLK/LRCLK | GPIO8 / GPIO7 / GPIO6 |
| 播放数据 MCU TX | GPIO1 → ES8311 DSDIN |
| 录音数据 MCU RX | GPIO5 ← ES7210 SDOUT1 |
| U14 麦克风 | ES7210 MIC1，TDM slot0 |
| U13 麦克风 | ES7210 MIC2，TDM slot2 |
| 硬件回声参考 | ES7210 MIC3，TDM slot1，来自 ES8311 模拟输出支路 |
| 空闲通道 | MIC4，TDM slot3，不送入 AFE |
| 音频电源 / 扬声器使能 | AW9523 P0.0 EN_POWER / P1.1 AP_EN |

`AMP_EN` 是耳机插座检测输入，不把它配置成输出。
两路物理麦克风 PGA 为 24dB，MIC3 参考为 0dB；物理增益编号与 TDM 槽位不同。

## AEC 实现与限制

参考小智的 [BoxAudioCodec](https://github.com/78/xiaozhi-esp32/blob/main/main/audio/codecs/box_audio_codec.cc)
和 [AfeAudioEngine](https://github.com/78/xiaozhi-esp32/blob/main/main/audio/engines/afe_audio_engine.cc)：
使用官方 codec 驱动；四槽数据重排成 `MIC1, MIC2, MIC3参考`，送入 ESP-SR `MMR` 前端。播放期间 ES8311 的模拟输出经板上回路进入 ES7210 MIC3，作为与麦克风同步采集的播放参考。
启用 `AFE_TYPE_FD`、`AEC_MODE_FD_LOW_COST`、`AEC_NLP_LEVEL_VERYAGGR`；关闭 NS、VAD、AGC、唤醒词。
feed、fetch、播放使用独立任务；录音与播放同步运行，完成后再回放 AEC 输出。
ESP-SR 官方约定 AFE 多通道输入按交错格式排列，最后一个有效通道为播放参考；`MMR` 正好对应两路麦克风加一路参考。

此测试覆盖真实板级参考链路与双工处理，但宽带噪声测试不能替代语音回声衰减、双讲保真和不同音量/距离下的上板评估。日志电平也不是 ERLE 指标。

## 看日志定位问题

每秒打印 `MIC1(slot0)`、`MIC2(slot2)`、`REF_MIC3(slot1)`、`NC_MIC4(slot3)` 和 `AEC_OUT`：

- `AC_RMS` 是去除直流后的幅度；`dBFS` 越接近 0 越大。说话时两路 MIC 应有明显变化。
- `DC` 为直流偏移，`peak` 为样本绝对峰值，`clip` 为接近满量程的样本数。持续削顶时降低麦克风增益。
- 播放时 `REF_MIC3(slot1)` 应明显高于静音底噪；空闲 MIC4 数值不用于判断麦克风好坏。
- 两路 MIC 都不随说话变化：检查 ES7210、MICBIAS12、U13/U14 供电、GPIO5 及共享时钟。
- MIC 有变化但 AEC_OUT 一直为零：提供启动时的 AFE pipeline 日志及这五路电平日志。
- 耳机能回放语音而喇叭不能：采集与 DAC 链路基本工作，继续检查 HT6872 功放/喇叭路径，不要把问题归因于 AEC。

电平日志没有时间对齐与远近端分离，不是 ERLE 指标。当前喇叭不出声的问题不会由创建此工程自动解决。

已进行软件编译验证；实际录音质量、通道映射和扬声器回放仍需上板验证。
