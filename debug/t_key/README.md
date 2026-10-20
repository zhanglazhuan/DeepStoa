# DeepStoa V1 按键调试

独立 ESP-IDF v5.5.3 工程，参考 NomadCast 的 `debug/t_key` 事件日志，
引脚直接引用 `boards/esp32s3/deepstoa_v1.h`。
已核对 `design/墨水屏-原理图V1.0.pdf` 第 3、7 页。

| 按键 | GPIO | 测试点 | 按下电平 |
| --- | --- | --- | --- |
| K3 / Vol+ | 38 | TP45 | 高 |
| K4 / Vol- | 47 | TP46 | 高 |
| K5 / Record | 48 | TP47 | 高 |
| K6 / OnOff | 39 | TP49 | 高 |

按键由 3V3 经 R66（1kΩ）供电，各信号有外部 100kΩ 下拉。
固件关闭内部上下拉，每 10ms 采样，电平连续稳定 50ms 才确认变化。
每键独立防抖，串口输出 `pressed` / `released` 和按住时长。
启动时输出原始电平；启动时已按住的键也会在防抖后输出按下事件。

## 编译和运行

在 PowerShell 中：

```powershell
$env:IDF_TOOLS_PATH = 'C:/Espressif'
. C:/Espressif/frameworks/esp-idf-v5.5.3/export.ps1
cd D:/Codes/DeepStoa/debug/t_key
idf.py build
idf.py -p <PORT> flash monitor
```

默认日志接口是原生 USB Serial/JTAG（GPIO19/20），选择对应端口。
若使用外接 UART，先在 `idf.py menuconfig` 中修改控制台配置。

## 上板检查

1. 不按键启动：四个 `initial raw` 应为 0，无按下事件。
2. 逐个短按（超过 50ms）：每次只有一条 `pressed` 和一条 `released`。
3. 长按 2 秒：不重复打印按下事件，松开后时长约 2000ms。
4. 同时按两个键并分别松开：每个键均独立输出成对事件。
5. 按住一个键启动：防抖后输出按下，松开时正常输出释放。

此工程仅检查四个按键，不执行关机、录音或音量控制，也不初始化屏幕、
AW9523、音频或 SD 卡。K6 的另一路 WAKEUP_MCU（GPIO4 / TP48）
不作为第五个按键；BOOT、RESET 不在此测试范围内。
NomadCast 的 LCD、耳机检测和 USB VBUS ADC 逻辑未迁入此工程。
