# DeepStoa V1 震动马达测试

独立 ESP-IDF v5.5.3 工程，复用 `drivers/aw9523`，引脚取自
`boards/esp32s3/deepstoa_v1.h`。已核对原理图第 2、3、4 页。

- 控制：AW9523 P1.0，`DEEPV1_AW_PIN_EN_MOTOR`，网络 `EN_Motor`，测试点 TP27。
- 高电平开启，低电平关闭；经 R52（4.7kΩ）驱动 Q5（S8050）。
- 马达接 J5，供电路径为 3V3 → R50（100Ω）→ J5 → Q5 → GND，D10 为续流二极管。
- I2C：SDA GPIO17、SCL GPIO18、地址 0x58；AW9523 复位 GPIO13。
- 马达使用 3V3，无需使能 `EN_POWER` 音频电源。

## 运行

```powershell
$env:IDF_TOOLS_PATH = 'C:/Espressif'
. C:/Espressif/frameworks/esp-idf-v5.5.3/export.ps1
cd D:/Codes/DeepStoa/debug/t_motor
idf.py build
idf.py -p <PORT> flash monitor
```

默认串口为原生 USB Serial/JTAG（GPIO19/20）。
启动后初始化 AW9523 并关闭马达，等待 2 秒，依次输出 200、400、800ms
震动脉冲，各间隔 1 秒。运行一轮后保持关闭；复位可重复测试。
脉冲时长在 `main/t_motor.c` 的 `pulse_ms` 中调整。

## 上板检查

应感到三次时长递增的震动，间隔和结束后停止。
无震动时检查 AW9523 初始化日志、TP27 高低电平、J5 接线、3V3、R50 和 Q5。
此测试不使用 PWM 调速，也不初始化屏幕或音频。

现有 AW9523 设置引脚接口不返回 I2C 错误，日志仅表示发送了开关命令，
不证明写入成功或马达实际转动；需结合实物观察或测量确认。
