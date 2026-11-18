# t_ble

ESP-IDF v5.5.3 / ESP32-S3 独立 BLE 外设测试，不启动屏幕、Wi-Fi 或主应用。
设备启动后持续广播 `DeepStoa-BLE`，允许一台手机连接；断开后自动恢复广播。
无需配对、密码或绑定。

## 编译和烧录

在已激活 ESP-IDF 的终端执行（将 COM4 换成实际端口）：

```powershell
cd D:\Codes\DeepStoa\debug\t_ble
idf.py build
idf.py -p COM4 flash monitor
```

使用根目录 `partitions.csv`（16 MB Flash，应用偏移 `0x20000`），Flash
时钟为 20 MHz，与现有 Wi-Fi 诊断工程一致。烧录会替换设备上的应用，
测试后可重新烧录主工程。不要执行 `erase-flash`，避免清除已有用户数据。
NVS 初始化失败时程序会报错停止，不会自动擦除 NVS。

## 手机测试

1. 打开 nRF Connect 或 LightBlue，授予蓝牙/附近设备权限（旧版 Android
   可能还需要位置权限及开启定位），扫描 `DeepStoa-BLE`。
   请在 BLE App 内连接，系统蓝牙设置页可能不显示此类设备。
2. 点击连接，串口应出现 `CONNECTED handle=...`。
3. 找到服务 `7b910001-2e56-4a90-8d74-6079ba31c8e0`，其特征为
   `7b910002-2e56-4a90-8d74-6079ba31c8e0`。
4. Read 初始值为 UTF-8 文本 `Hello from DeepStoa`。
5. 使用 Write Request 写入短文本（例如 `hello`），再 Read 应得到相同数据。
   值最大 128 字节；第一次测试建议不超过 20 字节，避免 MTU 限制。
   值仅保存在 RAM，重启后恢复初始文本。
6. 断开后再次扫描并连接，串口应显示 `DISCONNECTED` 和恢复广播日志。

启动成功标志：`Advertising as DeepStoa-BLE`。连接成功以 App 显示已连接和
串口 `CONNECTED` 为准，读写成功可进一步确认 GATT 通信正常。
