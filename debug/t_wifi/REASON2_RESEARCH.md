# DeepStoa v1 Wi-Fi `reason=2` 调查

## 结论与证据边界

ESP-IDF v5.5.3 把 `reason=2` 定义为 `WIFI_REASON_AUTH_EXPIRE`。对 STA 而言，它可能表示**认证响应在计时器到期前未收到**，也可能是**AP 发来了该原因码**。现有串口日志无法区分两者，不能仅凭这个数字判定密码错误、热点拒绝或射频故障。[ESP-IDF v5.5.3 连接阶段与原因码](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-guides/wifi.html#esp32-s3-wi-fi-station-connecting-scenario)、[原因码表](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-guides/wifi.html#wi-fi-reason-code)

| 设备及条件 | 观测 | 能支持的结论 |
| --- | --- | --- |
| DeepStoa v1，STA MAC `38:44:be:c9:d7:d0`，同一热点 BSSID `42:d1:33:36:96:51` | 扫描 `auth=7`、信道 1、RSSI `-32 dBm`；项目配置和 IDF 默认配置都在 `auth` 阶段约 4.4 秒后报 `reason=2` | AP 可被扫描到；两套 STA 参数均未完成认证，尚未进入 `assoc` |
| 同一问题板，此前热点关闭 WPA3 的试验 | `auth=3`（WPA2），两套配置仍报 `reason=2` | 不是单独由 WPA3-SAE 或 H2E 设置造成；仍需核对该试验确实使用同一板、同一热点与构建 |
| ESP32-S3 DevKitC，STA MAC `1c:29:04:22:b1:38`，同一热点及凭据 | 项目配置进入 `auth → assoc → run`，以 WPA3-SAE 接入并获取 IP | 热点、凭据和项目配置的组合在 DevKitC 上可用；自制板差异成为首要调查方向 |

这些观测来自本仓库的 [测试记录](README.md) 和用户提供的串口日志。`auth=7` 是扫描到的 WPA2/WPA3 混合模式；在过渡模式下，IDF 可选 WPA3，因此 DevKitC 日志显示 WPA3-SAE。[ESP-IDF v5.5.3 WPA3 说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-guides/wifi-security.html#wpa3-personal)

DevKitC 在项目配置成功并主动断开后，第二次以 IDF 默认配置重连报过 `reason=49`（invalid PMKID）；这是另一阶段、另一原因码，不能拿来解释问题板两次 `reason=2`。

**不要把另一块 MAC 尾号 `d8:08` 板子的 Flash 校验异常归因到本次 MAC 尾号 `d7:d0` 板子。**目前没有证据表明 `d7:d0` 板子存在 Flash 读错。

## 原因排序

1. **外接天线及 U.FL 射频通路，优先级最高。** [DeepStoa v1 原理图第 3 页](../../design/墨水屏-原理图V1.0.pdf) 中，U7 裸片 ESP32-S3R16V 的 `LNA_IN` 经天线网络 `C32/L3/C33` 到 U10 `UFL-R-SMT001` 接口；未见并联的板载 PCB 天线路径。用户已确认外接天线接好，因此重点检查 U.FL 插头与座子是否可靠接触、座子中心针/焊点、天线本身及 `C32/L3/C33` 的装配。`L2/C19` 位于 U7 的射频相关 3.3 V 供电线上，应归入供电检查。扫描 RSSI 是**本站收到 AP 的信号强度**，即使很高，也不能证明 AP 收到了本站发出的认证帧。Espressif 要求射频走线和天线接口约 50 Ω，并指出失配可使发射功率与 EVM 变差。[ESP32-S3 原理图射频要求](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html#rf)、[PCB 射频失配说明](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/pcb-layout-design.html#typical-layout-problems-and-solutions)
2. **板级供电或 40 MHz 晶振偏差，次优先。** 认证需要双向收发；发射瞬态供电波动或频率偏移可损坏发射质量。Espressif 要求 3.3 V 电源能力至少 500 mA，射频相关供电脚旁布去耦，并建议晶振精度在 ±10 ppm 内；其硬件指南直接把 Wi-Fi 连接失败与晶振频偏排查联系起来。但目前没有这块板的供电波形或 RF 测量，不能下结论。[电源与晶振要求](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html#power-supply)、[晶振要求](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html#external-crystal-clock-source-compulsory)
3. **热点针对该 STA 的响应或策略，保留可能性。** AP 也可能直接发 `reason=2`；需要空口或 AP 侧日志确认。DevKitC 成功使“热点整体不兼容”不太可能，但不排除针对 `d7:d0` MAC 的限制。项目配置和默认配置都失败、WPA2 试验也失败，使单一 PMF/SAE 参数错误的解释较弱。[ESP-IDF 原因码表](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-guides/wifi.html#wi-fi-reason-code)
4. **密码或测试镜像不一致，低优先但应排除。** 仅记录“密码长度 11”并不证明两块板使用的字节相同。DevKitC 成功大幅降低密码本身错误的可能性；若镜像或本地未跟踪的 `wifi_credentials.h` 不同，仍有例外。WPA3-SAE 使用密码参与认证，所以不能单靠“尚未进入四次握手”排除密码；WPA2 失败也提示应先查是否收到认证响应，而非直接修改密码。[ESP-IDF WPA3-SAE 说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-guides/wifi-security.html#wpa3-personal)

## 最有区分力的下一步

1. **先做无须改固件的检查。** 已知 `d7:d0` 板接了外置天线；在板子断电时重新扣合 U.FL，交换一根已知良好的 2.4 GHz 天线，再与 DevKitC 放在相同位置、使用相同热点复测。检查 U10 和 `C32/L3/C33` 是否漏焊、虚焊或值装错；另检查射频供电滤波 `L2/C19`。不要把天线直接焊到未匹配的 RF 脚。
2. **分清“本站没发出”还是“AP 没回应”。** 固定热点在信道 1，用独立的 2.4 GHz 监听设备抓 802.11 管理帧，或查看热点/AP 认证日志；比较 `d7:d0` 与 DevKitC 的 authentication request、response、deauth 帧。若监听端看不到问题板的认证请求，重点查 TX/天线路径；看到请求而无响应，查 AP 是否接收/策略；看到 AP 发 `reason=2`，按 AP 侧信息定位；看到 AP 响应而问题板仍超时，查 RX/频偏。单靠目前的串口原因码无法完成这个区分。[ESP-IDF 认证阶段说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-guides/wifi.html#auth-phase)
3. **再做受控硬件测量。** 认证尝试期间测 U7 的 3.3 V 电源脚及纹波；有 RF 测试设备时，按 Espressif 的 Wi-Fi 非信令测试分别测 TX 功率/EVM、频偏和 RX 灵敏度，并与 DevKitC 对照。该官方测试需相应 RF 仪器及屏蔽环境，不应从扫描 RSSI 推断这些指标。[ESP32-S3 Wi-Fi 非信令测试](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32s3/development_stage/rf_test_items/wifi_non_signaling_test.html)、[生产测试指标](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32s3/production_stage/tools/esp_production_testing_guide.html)
4. **排除固件差异。** 确认两块板烧录同一份 `t_wifi` 应用及 `sdkconfig`，比较生成镜像的哈希；检查两份本地 `wifi_credentials.h` 的值确实相同，避免打印密码。此项是对实验控制条件的检查，不是当前最可能的原因。

`reason=2` 的发生阶段说明问题在 DHCP/IP 之前；调整 DHCP、UI 返回流程、Wi-Fi 保存逻辑不会解释这段最小工程日志。[ESP-IDF 连接阶段](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-guides/wifi.html#esp32-s3-wi-fi-station-connecting-scenario)
