# COM8 / COM11：19 dBm 请求上限故障复测

测试日期：2026-10-28，Asia/Shanghai。主记录于 13:22:19 开始。

后续硬件变更：用户去除并短接 L2 后的两轮复测见
[L2 短接报告](L2_SHORT_CAPTURE_20261028.md)。高功率仍失败，但其中一次出现了
AP 成功 AUTH 回复及重传，是与本页早先记录不同的新证据。

## 13:49 增强嗅探固件复测

已将增强固件烧录到 COM8，COM11 沿用五档回切诊断镜像。
新增 FCSFAIL 过滤、错误接收元数据日志、错误限流/队列丢弃计数，
以及有效 AUTH 的硬件时间、回调时间、输出延迟。配置 `CONFIG_SNIFFER_RX_ERRORS=y`。
编译成功，串口验证 `ERROR_STATS enabled=1`；抓包转换的三项测试通过。
监听镜像 SHA256：`e62482cd2b25b7383175527da7bf167e60e2e3777a0ecd6dc17a2c7b1f819b99`。

结果仍为高功率 **6/6 认证失败**、低功率 **4/4 获取 IP**。
高功率六个主机窗口及窗口内 Beacon 数（含结果后的 250 ms）：

| 请求上限 | 配置 | 窗口 s | Beacon | STA AUTH | AP AUTH | RX_ERROR |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| 19 dBm | DeepStoa | 7.094–8.281 | 14 | 0 | 0 | 0 |
| 19 dBm | IDF默认 | 8.766–9.828 | 13 | 0 | 0 | 0 |
| 19 dBm | DeepStoa | 30.922–32.047 | 14 | 0 | 0 | 0 |
| 19 dBm | IDF默认 | 32.547–33.687 | 14 | 0 | 0 | 0 |
| 19 dBm | DeepStoa | 58.703–59.797 | 14 | 0 | 0 | 0 |
| 19 dBm | IDF默认 | 60.297–61.437 | 14 | 0 | 0 | 0 |

全程没有驱动上交的 RX_ERROR，`rx_error=0`、`suppressed=0`、
`error_queue_drops=0`、`queue_drops=0`。最后一次统计 `rx=10313 selected=697
sta_auth_seen=4 queue_pending=0`。PCAP 有 **729 帧，损坏 CAP 为 0**。

低功率有效认证的新日志示例：

```text
AUTH direction=STA->AP alg=0 transaction=1 status=0 rx_callback_us=13970962 rx_hw_us=14495104 output_delay_us=112441
```

硬件时间与回调时钟不是同一原点；不能直接相减或跨板比较。
本次确实观察到串口输出延迟，因此用记录内的接收时间描述瞬间，不能把打印时刻当作接收时刻。

**增强后仍没有观察到高功率认证请求或错误帧，不能据此断言故障板没有发射。**
此结果说明软件嗅探未获得更多可解码或驱动上交的错误接收证据；
不排除不能被驱动上交的畸变/弱信号。错误帧来源也不能仅靠时间相关性认定。

完整证据：[增强会话目录](evidence/20261028_19dbm/enhanced/)，其中含
[监听原始日志](evidence/20261028_19dbm/enhanced/sniffer_COM8.log)、
[故障板日志](evidence/20261028_19dbm/enhanced/COM11.log)、
[PCAP](evidence/20261028_19dbm/enhanced/sniffer.pcap)、
[统一时间线](evidence/20261028_19dbm/enhanced/timeline.jsonl)、
逐帧 CSV、逐次连接 JSON 和独立 `SHA256SUMS.txt`。
COM8 保留增强固件，COM11 保留原诊断镜像。

## 已复现的故障表现

COM11 在请求上限 19 dBm 时，三轮、每轮两套配置，共 **6/6 次认证失败**。
六次均为 `init -> auth -> init`，在 `auth` 状态停留约 1000 ms，
本地断线事件 `reason=2`；未进入 `assoc`，没有获取 IP。

COM8 在这些尝试期间持续收到目标热点 Beacon，但没有解码出目标 STA 的
AUTH 请求，也没有观察到热点发给该 STA 的 AUTH 回复、关联交互或 EAPOL。
两次切回 2 dBm 的四次连接 **4/4 获取 IP**，监听侧能看到双向 AUTH、关联和 EAPOL。
同板、同热点、不重新启动 Wi-Fi 的高→低→高→低→高回切，确认本次故障表现与
发射功率设置可重复相关。硬件根因尚未测定。

## 条件和功率定义

- 监听板 COM8，固定信道 1，被动混杂模式；故障板 COM11，STA `38:44:be:c9:d7:d0`。
- 热点 `my_test_wifi`，BSSID `42:d1:33:36:96:51`，扫描为 WPA2 (`auth=3`)、信道 1。
  COM11 各轮目标扫描 RSSI 约 -33 至 -30 dBm。
- COM8 按上述 STA/BSSID 过滤，RAW 开启，snaplen 256，ACK 关闭。
  保留目标 AP Beacon，便于确认监听可见性；不采集全部普通数据帧。
- 两套配置为 DeepStoa 配置和 ESP-IDF 默认 STA 配置，使用同一份本地凭据。
  密码不写入报告、日志或证据目录。
- 独立诊断镜像，ESP-IDF v5.5.3，完整 RF 校准；顺序 `{76,8,76,8,76}`。
  每档设置后读回，再扫描并测试两套配置；跳过 Internet 测试。
- **19 dBm 是 `esp_wifi_set_max_tx_power(76)` 请求值**，日志读回为 76；
  2 dBm 请求/读回为 8。本机 IDF `esp_wifi.h:1127` 明确列出 `[72,79] → 72`
  的离散映射，即此请求落在文档标注的 18 dBm 档位。
  读回值不是实测 RF 输出，本实验不能宣称“实际发射恰为 19 dBm”。
- 镜像 SHA256：`a19e02745ce66f2cd861aaf03956d0016a00ac6d36be97f43ea464ac5797ed52`。
  临时工程位于 `build/tx_19dbm_20261028/`，未修改主工程及现有 t_wifi 源码。

## 主记录：逐次连接与空中帧计数

证据目录：[evidence/20261028_19dbm/main](evidence/20261028_19dbm/main/)。
时间是采集电脑的统一单调时钟秒数，从采集开始计时；并非两块板的启动时间。
统计窗口从 COM11 输出 connecting 到 PASS/FAIL，再增加 250 ms 以容纳串口输出延迟。
帧数包含重传，没有去重；不是精确到空中时刻的协议边界。

| 顺序 | 请求上限 | 配置 | 主机窗口起止 s（不含尾部 0.25 s） | STA AUTH | AP AUTH | STA关联请求 | AP关联响应 | EAPOL双向合计 | Beacon | COM11结果 |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1 | 19 dBm | DeepStoa | 6.907–8.032 | 0 | 0 | 0 | 0 | 0 | 13 | reason=2 |
| 2 | 19 dBm | IDF默认 | 8.532–9.578 | 0 | 0 | 0 | 0 | 0 | 12 | reason=2 |
| 3 | 2 dBm | DeepStoa | 14.125–15.344 | 1 | 2 | 1 | 2 | 6 | 15 | 获取IP |
| 4 | 2 dBm | IDF默认 | 15.875–18.828 | 1 | 2 | 1 | 2 | 5 | 32 | 获取IP |
| 5 | 19 dBm | DeepStoa | 23.391–24.438 | 0 | 0 | 0 | 0 | 0 | 13 | reason=2 |
| 6 | 19 dBm | IDF默认 | 24.938–25.969 | 0 | 0 | 0 | 0 | 0 | 13 | reason=2 |
| 7 | 2 dBm | DeepStoa | 30.969–33.672 | 1 | 2 | 1 | 2 | 6 | 28 | 获取IP |
| 8 | 2 dBm | IDF默认 | 34.188–36.328 | 1 | 2 | 2 | 2 | 6 | 24 | 获取IP |
| 9 | 19 dBm | DeepStoa | 40.922–42.047 | 0 | 0 | 0 | 0 | 0 | 13 | reason=2 |
| 10 | 19 dBm | IDF默认 | 42.547–43.578 | 0 | 0 | 0 | 0 | 0 | 13 | reason=2 |

例如第二个高功率档，COM11 原始日志：

```text
W (17244) t_wifi: TX_DIAG full_calibration=1 requested_qdbm=76 actual_qdbm=76
I (21304) wifi:state: init -> auth (0xb0)
I (22304) wifi:state: auth -> init (0x200)
W (22314) t_wifi: disconnected: reason=2 rssi=-32 bssid=42:d1:33:36:96:51
I (22844) wifi:state: init -> auth (0xb0)
I (23844) wifi:state: auth -> init (0x200)
W (23854) t_wifi: disconnected: reason=2 rssi=-31 bssid=42:d1:33:36:96:51
```

低功率下 AP AUTH 为 Open System `alg=0 transaction=2 status=0`，
重复帧在 COM8.log 中标为 `retry=1`。成功后出现的 `reason=8` 是测试程序主动断开，
不计为认证失败。低功率也有重传，因此不能用此实验保证低功率长期稳定性。

## 证据文件和复查方式

- [sniffer_COM8.log](evidence/20261028_19dbm/main/sniffer_COM8.log)：COM8 原始串口字节，包含逐帧 RSSI、信道、seq/retry、协议摘要、CAP、STATS；归档时更名以避免 Windows 的 COM8 保留设备名。
- [COM11.log](evidence/20261028_19dbm/main/COM11.log)：故障板完整启动、功率设置、扫描、状态转换和 IP 结果。
- [timeline.jsonl](evidence/20261028_19dbm/main/timeline.jsonl)：两串口逐行主机接收时间，含 UTC 和控制动作。
- [sniffer.pcap](evidence/20261028_19dbm/main/sniffer.pcap)：LINKTYPE_IEEE802_11 (105)，不含 Radiotap/FCS；时间为 COM8 启动微秒，不是 UTC。
- [frames.csv](evidence/20261028_19dbm/main/frames.csv)：逐帧地址、类型、seq/retry、原长/截长和前六字节报文体。
- [attempts.json](evidence/20261028_19dbm/main/attempts.json)：逐次窗口、设置/读回值、结果和帧计数。
- `SHA256SUMS.txt`：证据文件校验值。

主记录 PCAP 保留 **937 帧**。原始串口在采集结束时截断了最后一条 Beacon CAP：
监听时间 `89,436,195 us`，声明 312 字节、截取 256 字节，实际串口行未收完整，
转换器报告损坏 CAP **1 条**并跳过。该条发生在全部连接测试结束后约 46 秒，
不是 AUTH 帧，也不落在上表任何窗口；原始残行仍保留于 COM8.log，未补造数据。
timeline 仅含完整行。帧计数与 PCAP 对完整 CAP 的校验一致。
主记录最后一条 STATS 为 `rx=10455 selected=899 rx_error=0 queue_drops=0
peer_overflows=0 sta_auth_seen=4 queue_pending=0`；检查全程 STATS，软件队列丢包均为 0。

Wireshark 过滤 `wlan.addr == 38:44:be:c9:d7:d0`，再看
`wlan.fc.type_subtype == 0x0b`（AUTH）。高功率区间需要同时参考 timeline，
不能把 PCAP 中较晚的低功率成功认证误判为高功率回应。

辅助记录：[13:19:39 会话](evidence/20261028_19dbm/preliminary/)。
该会话开始时 COM11 已运行，第一档启动与功率行缺失，不把第一档计入上述主记录；
后两档高功率的四次失败和两档低功率的四次获取 IP 均有完整设置日志。
其 PCAP 转换为 1513 帧，损坏 CAP 为 0。

复现工具：[capture_pair.py](tools/capture_pair.py)、[analyze_pair.py](tools/analyze_pair.py)。
采集脚本使用 esptool 的 Windows DTR/RTS 复位处理，避免仅改 RTS 导致 COM11 未复位。

## 证据边界和后续定位

已经坐实的是：**本板在高请求功率档稳定复现认证失败，低功率回切可恢复认证、关联和 IP；
高功率失败时旁路监听未捕获到可解码的目标认证帧。**

不能据此断言热点完全没有收到电磁信号，也不能区分未实际发射、发射失真、
发射时电源异常或监听漏收。`init -> auth` 不证明天线已经输出合格请求；
本地 `reason=2` 也不是抓到了热点发来的拒绝帧。
`queue_drops=0` 只排除已上交帧的软件输出队列丢包，`rx_error=0` 不排除空中坏帧。

下一步沿用 [POWER_DIAGNOSIS.md](POWER_DIAGNOSIS.md) 的测量顺序：
示波器同步对比高低功率认证瞬间的 TP7 主 3V3 和 C19（U7 VDD3P3 支路），
再根据波形区分上游供电与 L2/本地去耦；供电正常后检查天线、匹配和实际 RF 输出。

本次烧录替换 COM11 的诊断固件，保留 NVS/FlashDB，未执行整片擦除或 eFuse 写入。
测试结束后 COM11 保留该镜像（下一次复位会再次执行五档测试），COM8 保留原监听固件。
