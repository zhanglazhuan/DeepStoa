# t_display: 墨水屏显示调试工程 设计文档

日期：2026-07-29

## 目标

在 `debug/t_display` 创建 ESP-IDF 子工程，验证墨水屏 (GDEM0397T81P) 能正常显示。

当前开发板 ESP32S3 通过 AW9523BTQR I2C IO 扩展器连接墨水屏，SPI 信号完全走扩展器。后续量产板改为 ESP32 GPIO SPI 直连。

## 项目结构

```
drivers/
├── aw9523/                              # 新增: AW9523BTQR I2C GPIO 扩展器驱动
│   ├── CMakeLists.txt
│   ├── aw9523.h
│   └── aw9523.c
└── gdem0397t81p/                        # 重构: Zephyr RTOS → ESP-IDF
    ├── CMakeLists.txt
    ├── Display_EPD_W21.h                # 公开 API 不变
    └── Display_EPD_W21.c                # 底层 API 替换, 逻辑不变

debug/t_display/
├── CMakeLists.txt                       # project(t_display), EXTRA_COMPONENT_DIRS 指向 drivers
├── sdkconfig.defaults                   # 项目默认 Kconfig
└── main/
    ├── CMakeLists.txt                   # idf_component_register
    └── t_display.c                      # 测试流程 app_main
```

`debug/t_display/CMakeLists.txt` 通过 `set(EXTRA_COMPONENT_DIRS ...)` 将 `drivers/` 下的每个子目录注册为独立组件。

## 引脚映射

```
ESP32S3                    AW9523BTQR              墨水屏 (GDEM0397T81P)
┌────────┐                ┌──────────┐             ┌──────────────┐
│ GPIO18 │──I2C SCL──────→│ SCL      │             │              │
│ GPIO17 │──I2C SDA──────→│ SDA      │             │              │
│ GPIO3  │←──INT──────────│ INTN     │             │              │
│ GPIO46 │──RESET────────→│ RSTN     │             │              │
│        │                │          │             │              │
│        │                │ P1.4 ────┼────────────→│ CS           │
│        │                │ P1.5 ────┼────────────→│ DC           │
│        │                │ P1.6 ←───┼─────────────│ BUSY         │
│        │                │ P0.5 ←───┼─────────────│ INT (不实际用)│
│        │                │ P0.6 ────┼────────────→│ SDI (MOSI)   │
│        │                │ P0.7 ────┼────────────→│ SCLK         │
└────────┘                └──────────┘             └──────────────┘
```

- AW9523 I2C 地址: `0x58` (AD1=0, AD0=0, 7-bit)
- AW9523 硬件复位: GPIO46 (低有效)
- AW9523 中断: GPIO3 (输入, 当前不使用)
- 墨水屏 SPI 完全通过 AW9523 GPIO bit-bang

## 组件设计

### 1. aw9523 组件 (新增)

独立 I2C GPIO 扩展器驱动。API:

| 函数 | 说明 |
|------|------|
| `aw9523_init(i2c_port, sda, scl, rst_gpio, addr)` | 初始化 I2C, 硬件复位, 配置默认方向 |
| `aw9523_set_pin(pin, level)` | 设置单 pin 输出电平 |
| `aw9523_get_pin(pin)` | 读取单 pin 输入电平 |
| `aw9523_write_port(port, value)` | 写整个端口 (P0 或 P1) 8 bit |
| `aw9523_read_port(port)` | 读整个端口 (P0 或 P1) 8 bit |
| `aw9523_get_chip_id()` | 读芯片 ID (寄存器 0x10, 期望 0x23) |
| `aw9523_reset()` | 软件复位 |

关键寄存器:

| 地址 | 说明 |
|------|------|
| 0x00 | P0 输入 |
| 0x01 | P1 输入 |
| 0x02 | P0 输出 |
| 0x03 | P1 输出 |
| 0x04 | P0 方向 (0=输出, 1=输入) |
| 0x05 | P1 方向 |
| 0x10 | 芯片 ID (0x23) |
| 0x11 | P0 控制 (push-pull / LED) |
| 0x7F | 软件复位 |

端口方向配置:
- P0: bit 7=SCLK(输出), bit 6=SDI(输出), bit 5=INT(输入)
- P1: bit 6=BUSY(输入), bit 5=DC(输出), bit 4=CS(输出)

### 2. gdem0397t81p 组件 (重构)

#### 原则

- `Display_EPD_W21.h` 公开 API **完全不变**
- `Display_EPD_W21.c` 内部实现：Zephyr API → ESP-IDF API，**逻辑序列不变**

#### 替换映射

| 原实现 (Zephyr) | 替换为 (ESP-IDF) |
|-----------------|-------------------|
| `spi_dt_spec` 结构体 + DeviceTree | 全局存储 I2C 端口号、pin 映射 |
| `SPI_DT_SPEC_GET(DT_NODELABEL(epd), …)` | `aw9523_spi_init()` 手动配置 |
| `spi_write_dt(&epd, …)` (单字节) | `aw9523_spi_write_byte(data)` |
| `spi_write_dt(&epd, …)` (多字节) | `aw9523_spi_write_bytes(data, len)` |
| `spi_write(…)` (DMA 分块) | `aw9523_spi_write_bytes()` 降级为逐字节 |
| `gpio_pin_set_dt(&dc, x)` | `aw9523_set_pin(PIN_LCD_DC, x)` |
| `gpio_pin_get_dt(&busy)` | `aw9523_get_pin(PIN_LCD_BUSY)` |
| `k_msleep(x)` | `vTaskDelay(pdMS_TO_TICKS(x))` |
| `k_yield()` | `taskYIELD()` |

#### Bit-bang SPI 时序 (SPI Mode 0)

SDI 和 SCLK 都在 AW9523 P0 端口，一次 I2C 写可同时更新：

```
每字节 8 bit, MSB first:
  for bit = 7..0:
    1. 设 P0 = (SDI=bit_data, SCLK=0, 其他不变)  → I2C 写 0x02
    2. 设 P0 = (SDI=bit_data, SCLK=1, 其他不变)  → I2C 写 0x02 (上升沿锁存)
```

- 每 bit 2 次 I2C 事务，每字节约 32 字节 I2C 数据
- @400kHz I2C，每字节 ~1.2ms
- 全屏 48KB ~ 60 秒。调试可接受；量产板切硬件 SPI 到毫秒级

#### CS 控制

- `EPD_W21_WriteCMD` / `EPD_W21_WriteDATA`: 调用前 CS=0, 调用后 CS=1
- `EPD_W21_WriteDATA_Package` / `EPD_W21_WriteDATA_Batch`: 整个数据块内 CS=0, 结束后 CS=1

### 3. boards/esp32s3/deepstoa_v1.h (填写)

板级定义头文件，集中管理引脚宏:

```c
// I2C (AW9523 通信)
#define DEEPV1_I2C_PORT         0
#define DEEPV1_PIN_I2C_SCL      GPIO_NUM_18
#define DEEPV1_PIN_I2C_SDA      GPIO_NUM_17
#define DEEPV1_PIN_IO_INT       GPIO_NUM_3
#define DEEPV1_PIN_IO_RESET     GPIO_NUM_46

// AW9523 I2C 地址
#define DEEPV1_AW9523_ADDR      0x58

// LCD 引脚 (映射到 AW9523 端口)
#define DEEPV1_PIN_LCD_CS       (AW9523_PIN_P1_4)
#define DEEPV1_PIN_LCD_DC       (AW9523_PIN_P1_5)
#define DEEPV1_PIN_LCD_BUSY     (AW9523_PIN_P1_6)
#define DEEPV1_PIN_LCD_INT      (AW9523_PIN_P0_5)
#define DEEPV1_PIN_LCD_SDI      (AW9523_PIN_P0_6)
#define DEEPV1_PIN_LCD_SCLK     (AW9523_PIN_P0_7)
```

## 测试流程 (t_display.c)

```
app_main():
  ① 初始化 I2C 总线 (GPIO17, GPIO18, 400kHz)
     → 验证: I2C 驱动安装成功
  
  ② 初始化 AW9523
     ├─ GPIO46 复位 AW9523
     ├─ 配置 P0/P1 方向
     └─ 验证: 读芯片 ID == 0x23
  
  ③ 初始化墨水屏
     ├─ EPD_GPIO_Config()   (适配后的版本)
     ├─ EPD_HW_Init()
     └─ 验证: EPD_IsBusy() 读数正常
  
  ④ 全屏清屏测试
     ├─ EPD_WhiteScreen_White() → 延时 2s
     ├─ EPD_WhiteScreen_Black() → 延时 2s
     └─ 每步打印 PASS/FAIL
  
  ⑤ 图案测试
     ├─ malloc() EPD_ARRAY 字节 (heap, 自动映射到 PSRAM)
     ├─ 填充棋盘格 / 矩形边框图案
     ├─ EPD_WhiteScreen_ALL(buf)
     └─ 打印 PASS/FAIL
  
  ⑥ EPD_DeepSleep()
     打印 "TEST COMPLETE"
```

每步通过串口输出状态，方便定位失败点。无需 FreeRTOS 多任务，顺序执行。

## 风险与对策

| 风险 | 对策 |
|------|------|
| AW9523 初始化失败 (芯片 ID 读不到) | 检查 I2C 引脚焊接和上拉电阻，输出详细错误日志 |
| Bit-bang SPI 时序不正确 | 逻辑分析仪抓 SCLK/SDI 验证 bit 顺序和极性 |
| 墨水屏不刷新 | 检查 BUSY 信号变化，确认 DC/CS 信号顺序 |
| PSRAM 分配失败 | 降级用栈静态 buffer (需确认栈大小) |

## 后续量产板迁移路径

当 PCB 改为 ESP32 GPIO 直连 SPI 后:
1. `Display_EPD_W21.c` 中的 bit-bang 函数替换为 ESP-IDF `spi_device_transmit()`
2. DC/CS/BUSY 改为 `gpio_set_level()` / `gpio_get_level()`
3. `aw9523` 组件保留 (可能有其他 IO 扩展需求)
4. 显示驱动公开 API (`Display_EPD_W21.h`) 无需修改
