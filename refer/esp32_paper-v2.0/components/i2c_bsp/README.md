# PhotoPaint V2：ES7210 SDA/SCL 反接兼容

当前状态：按用户要求，已在 `sdkconfig` 关闭反接选项，Kconfig 默认值也改为关闭。
下次编译时不会编译或链接反接兼容层，全部设备使用 SDA=GPIO17、SCL=GPIO18。
本次未烧录，板上固件不会随源代码修改自动变化。
以下保留此前启用该选项时的设计和测试记录。

此板 ES7210 的 CDATA/CCLK 反接，但 AW9523、ES8311、AHT20 和 QMI8658
仍按正常方向连接在同一组 GPIO17/18 上。

| 访问目标（7 位地址） | SDA | SCL |
| --- | --- | --- |
| ES7210，0x40 | GPIO18 | GPIO17 |
| 其余设备 | GPIO17 | GPIO18 |

`CONFIG_PHOTOPAINT_ES7210_SWAPPED_I2C` 控制该兼容层，依赖 PhotoPaint V2 板型。
修正硬件接线后，应在 menuconfig 的 **PhotoPaint I2C wiring** 中关闭它。
传给 `esp_codec_dev` 的 ES7210 地址仍为 **0x80**，该库内部转换为 7 位 0x40。

`pin_swap.cmake` 同时编译 `i2c_pin_swap.c` 并添加链接器 `--wrap`。
这会覆盖 BSP、音频库及其他组件的阻塞式 I2C 主机调用，无需修改第三方源码。
所有调用共用递归互斥锁；0x40 的一次完整读、写或 repeated-start 事务在锁内
交换 GPIO matrix 路由，返回（包括报错）前恢复正常方向。设备句柄不销毁重建。

限制：仅针对 I2C0、正常 SDA17/SCL18、ES7210 地址 0x40。
共享总线拒绝非零 `trans_queue_depth`，不支持异步传输。
不要绕过 IDF 主机 API 直接控制这两根引脚；新增底层访问路径需要纳入互斥。
这是一项板级软件兼容方案，不能使两种接线在物理上隔离，仍需验证实际工作负载。

实板验证（2026-09-12）：

- 独立切换测试：5 次往返，每次 ES7210 ID 读取 50 次，250 次均为 0x7210；
  恢复后四个正常方向地址均回应，AW9523 输出/方向/模式读数保持不变。
- 接入统一路由后，两核同时运行：ES7210 ID 读取 500 次、multi-buffer 写入
  500 次，另一核执行 AW9523 读取、EN_POWER 写高和 QMI8658 探测 500 轮，均无错误。
- 日志：`../../tmp/i2c-swap-runtime.log`、`../../tmp/i2c-router-runtime.log`。

完整音频初始化尚未通过，不能把以上结果视为录音功能正常：

- 业务启动可以发现 0x40，随后 ES7210 初始化写入 `0x4B=0x00` 时超时，
  GPIO17/18 读值变为低。该寄存器控制 MIC1/2 的 MICBias、ADC、PGA 供电。
- 独立程序固定 SDA18/SCL17、不进行方向切换时也复现；提前输出 MCLK、
  每条初始化写入之间等待 100 ms、SCL 等待时间加至 20 ms 均未解决。
- 断开 ESP32 两脚的外设输出路由、开漏释放两线并发送恢复时钟后，两线仍读为低。
  需要在故障发生时测量 A3V3 和芯片供电脚、检查线路；目前不能确定具体硬件原因。
- 相关日志：`../../tmp/i2c-router-full-runtime.log`、
  `../../tmp/i2c-init-mclk-runtime.log`、`../../tmp/i2c-init-direct-runtime.log`、
  `../../tmp/i2c-init-release2-runtime.log`。板上恢复的是带本兼容层的业务固件，
  当前会停滞于上述音频初始化阶段。

ESP-IDF 的 SDA/SCL 属于总线配置，而不是设备配置，见
[官方 I2C 文档](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/i2c.html)。
