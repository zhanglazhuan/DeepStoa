# t_wifi_host：ESP32-S3 被动 Wi-Fi 监听板

用第二块 ESP32-S3 观察故障板连接 AP 时的空中报文。默认固定 **信道 1**，
`WIFI_MODE_NULL` + 混杂模式，不连接 AP、不发起扫描，不需要 Wi-Fi 密码。
默认输出认证、关联/重关联、解除认证/解除关联、Probe、Beacon 和明文 EAPOL
的逐帧摘要；这不是全流量抓包器，不输出普通数据帧或解析加密载荷。

COM10/COM11 的实测结果与原理图分析见 [发射功率对照诊断](POWER_DIAGNOSIS.md)：
故障板在 19.5/10 dBm 上限下认证失败，降到 2 dBm 后可通过认证和关联，需进一步测量射频供电。

## 1. 配置、烧录

在 ESP-IDF v5.5.3 环境下：

```powershell
cd D:\Codes\DeepStoa\debug\t_wifi_host
idf.py menuconfig
idf.py build
idf.py -p COM4 flash monitor
```

`COM4` 换成**监听板**端口。`Wi-Fi sniffer test` 菜单可以设置：

| 配置 | 默认值 | 用途 |
| --- | --- | --- |
| Fixed primary channel | 1 | 必须等于目标 AP 的实际 2.4 GHz 主信道；不跳频 |
| Faulty board STA MAC | 空 | 故障板 Wi-Fi **STA MAC**，留空观察所有 STA |
| AP BSSID | 空 | 路由器/手机热点的 BSSID，留空观察所有 AP |
| Authentication observation window | 4400 ms | 从首次观察到 STA AUTH 起计时 |
| Log ACK frames | 关闭 | 设置至少一个 MAC 过滤器后才生效 |
| Print captured frame bytes | 关闭 | 输出 CAP 原始帧记录，用于 PCAP |
| Maximum captured bytes | 256 | PCAP 截取长度，可调到 1600 字节 |

MAC 格式 `aa:bb:cc:dd:ee:ff`。推荐同时填故障板 STA MAC 和 AP BSSID，
以免附近设备的流量淹没日志。STA MAC 不要用 BLE MAC、SoftAP MAC 或监听板 MAC。
AP BSSID 可从 `t_wifi` 的 `target scan` 日志获取；STA MAC 可从故障板启动日志
或 `esp_wifi_get_mac(WIFI_IF_STA, ...)` 获取。过滤器启用后需要重新编译和烧录。

本工程沿用根目录 16 MB Flash 分区表，应用偏移 `0x20000`，Flash 时钟 20 MHz。
适用于与当前 DeepStoa 布局匹配的监听板；其他容量/布局的开发板应先调整配置。
烧录会替换监听板固件及 OTA 选择数据，测试后可重新烧录原工程。
不会自动擦除 NVS 或 FlashDB；NVS 初始化失败则停止。不要执行 `erase-flash`。

## 2. 两块板的操作顺序

1. 固定路由器/热点在 2.4 GHz 信道 1，或把监听板配置成 AP 的实际主信道。
   手机热点可能自动换信道，每轮测试前核对故障板扫描日志。
2. 把监听板放在故障板旁边，先启动监听板，等串口显示 `READY`。
3. 再让故障板执行 `t_wifi` 连接测试，同时保存故障板自身的连接/超时日志。
4. 观察 `AUTH`、`ASSOC_REQ/RESP`、`EAPOL` 和 `WINDOW`；每 5 秒输出一次 `STATS`。
5. 重复几轮；必要时把监听板移近 AP 再试，排除监听位置造成的单向漏抓。

不设置 MAC 时可先观察附近认证流量，再据 `ta/ra/bssid` 找到目标。
设置 STA 过滤器后，仅保留与该 STA 有关的报文；同时设置 AP BSSID 时，
额外保留这个 AP 的 Beacon 作为监听可见性参考。Beacon 摘要每 BSSID
最多每 10 秒一条（最多缓存 16 个 AP）；CAP 模式仍输出每个选中的 Beacon。
设置 AP 过滤器会排除 BSSID 为广播地址的普通 Probe Request。

## 3. 看懂日志

### 接收错误与认证瞬间（增强诊断）

`Observe driver-delivered receive errors` 打开 FCSFAIL 过滤器。驱动上交错误帧时，
输出 `RX_ERROR`：接收回调时间 `t`、硬件接收时间 `hw_t`、信道、RSSI、
`rx_state` 和包含 FCS 的 `sig_len`。错误码含义并未公开，不将某个码直接解释为 FCS 错误。
只保存元数据，不解析损坏帧地址，不写入普通 CAP/PCAP，不归属到目标 STA。
错误记录最多 10 条/秒；所有已上交错误仍计入 `rx_error`，限流数和错误队列丢弃数
在 `ERROR_STATS suppressed/error_queue_drops` 中独立报告，避免与正常目标帧丢包混淆。
错误记录只使用队列前半容量，给正常报文留空间。

有效 AUTH 日志新增 `rx_callback_us`、`rx_hw_us`、`output_delay_us`，
可区别驱动接收和串口打印延迟。硬件时间是 32 位微秒计数，会回绕；
不直接与 `esp_timer` 或另一块板的时间相减。监听板关闭省电以保持接收时间精度。
这些时间描述接收事件，不是故障板实际发射时间。

`RX_ERROR` 只说明某个信号被驱动以错误帧形式上交，来源可能是其他设备。
没有 `RX_ERROR` 也不能证明没有发射：不可解码信号不一定被驱动上交。
启用后应做同板高低功率回切，对照相同时间窗内的错误量与有效 AUTH。

```text
RX t=1234567us ch=1 rssi=-40 AUTH ra=<AP> ta=<STA> bssid=<AP> seq=123 retry=0 protected=0 len=30
  AUTH direction=STA->AP alg=0 transaction=1 status=0
WINDOW_START sta=<STA> ap=<AP> round=1
RX ... AUTH ra=<STA> ta=<AP> ...
  AUTH direction=AP->STA alg=0 transaction=2 status=0
```

- `t` 是监听板开机后的微秒时间，不能直接与另一块板的开机时间相减。
- `ra` 是接收地址，`ta` 是发送地址；`rssi` 是**监听板收到该帧**的信号强度。
- `seq` / `retry` 可识别重复发送；计数包含重传，没有去重。
- Open System 认证通常为 `alg=0`，STA 发 `transaction=1`，AP 回 `2`。
  WPA3-SAE 为 `alg=3`，双方均可出现事务 1/2；不能按奇偶认定请求/回复。
  此处根据 BSSID 与发送地址判断方向，窗口内匹配相同算法。
- Open System AUTH 和关联响应的 `status=0` 表示成功；其他值需结合认证算法解释。
  SAE 的特殊状态（例如 76）不宜直接等同于最终认证失败。
- `reason` 是解除认证/解除关联帧内的原因码，和 ESP-IDF 本地断线事件不是同一套上下文。
- `protected=1` 时不把加密报文体误当作状态码/原因码，不参与窗口推断。
- EAPOL 日志显示方向及类型（3 为 Key）；不根据方向直接标注 M1-M4。
- ACK 只有接收地址，没有发送地址/BSSID，日志中的 `ta/bssid` 为零；
  仅凭 ACK 的 RA 不能确认是哪一台设备发出，也不能直接绑定某次认证。

从观察到 STA AUTH 开始，按 `(STA, BSSID)` 统计一个固定 4.4 秒窗口。
窗口内重传和 SAE 后续交互不会重置计时；它是**观察窗口，不是精确的驱动连接尝试边界**。
若 STA 切换认证算法，则提前结束旧窗口（`end=algorithm_changed`），再开启新窗口；
`elapsed_ms` 显示实际统计时长，不能把提前结束的窗口当作完整 4.4 秒超时。
窗口结束输出以下结果；响应若在窗口之后到达仍会逐帧记录，但不会计入该窗口：

| 输出/现象 | 能说明什么 | 下一步 |
| --- | --- | --- |
| `No target STA authentication observed yet` | 尚未观察到目标 STA 发认证 | 核对 MAC、信道、开始时间及覆盖，再与正常板对照 |
| `STA_AUTH_SEEN_NO_AP_AUTH_OBSERVED` | STA 的认证到达监听板，窗口内没观察到匹配 AP 认证 | 靠近 AP 重试，检查 AP 是否拒绝/漏收及监听漏包 |
| `AP_AUTH_SEEN_NO_STA_ASSOC_OBSERVED` | 匹配 AP 认证也到达监听板，未观察到 STA 关联 | 先检查 AP 状态码、SAE 是否完成，再结合故障板日志判断 |
| `AP_AUTH_AND_STA_ASSOC_SEEN` | 双向认证及后续 STA 关联均被观察到 | 看关联响应状态和 EAPOL 是否继续 |

`WINDOW` 同时列出 `sta_auth/ap_auth/assoc_req/assoc_resp/eapol` 数量。
最多维护 16 对设备；表满且都在活动窗口时，新增分析会计入 `peer_overflows`。
过滤到目标设备可以避免这种情况。

`STATS rx` 为驱动上交帧数（不是所有空中帧数），`selected` 为已处理目标帧数；
`rx_error` 为已上交但 RX 状态异常/长度异常的帧数，`queue_drops` 为输出跟不上
导致的软件丢包，`queue_pending` 为尚未输出的记录。默认不启用 FCS 错帧过滤选项，
因此 `rx_error=0` **不代表空中没有坏帧**。`queue_drops>0` 时不要用“没抓到”作否定证据；
应收紧 MAC 过滤、关闭 RAW/ACK 或提高串口波特率后重试。

**不能仅凭一次漏抓认定发射硬件损坏、频偏或故障板接收失灵。**
监听板能解码不代表 AP 也能解码；AP 回复到达监听板不代表到达故障板。
单信道、天线位置、干扰、驱动过滤、输出丢包都会影响可见性。ESP32-S3 也不能
直接测量频偏。应结合两边日志、正常板对照和重复实验定位。

## 4. 保存 PCAP，用 Wireshark 复查

先在 `menuconfig` 打开 `Print captured frame bytes`，建议先设置 MAC 过滤器，
再编译烧录。串口出现 `CAP ...` 表示已启用。关闭 `idf.py monitor` 释放串口，
在 ESP-IDF Python 环境执行：

```powershell
python tools/capture.py --port COM4 --seconds 60 --output build/session.pcap --log build/session.log
```

然后触发故障板连接；60 秒后结束，也可 Ctrl+C 提前停止。
工具通过 pyserial 读取串口，保存原始日志以及 PCAP。pyserial 已随 ESP-IDF 环境安装。
若已有 UTF-8 串口日志，可离线转换：

```powershell
python tools/capture.py --input build/session.log --output build/session.pcap
```

PCAP 是标准 `LINKTYPE_IEEE802_11 (105)`，不含 FCS、Radiotap；时间戳为监听板
开机时间，不是 UTC。RSSI/信道在 `.log` 中，PCAP 只保留 802.11 帧。
默认最多截取 256 字节，因此较长 SAE/关联/EAPOL 报文可能被截断；要进一步
检查报文体可把 snaplen 调大。输出仅包含本工程选中的帧，并不包含所有数据流量。
PCAP 转换会校验长度并报告损坏的 CAP 记录；同时检查日志中的队列丢包统计。

Wireshark 可使用显示过滤器（换成实际 MAC）：

```text
wlan.addr == aa:bb:cc:dd:ee:ff
wlan.fc.type_subtype == 0x0b
eapol
```

## 5. 本机验证

在本目录，使用系统 GCC 和 Python：

```powershell
gcc -std=c11 -Wall -Wextra -Werror -I main main/frame_parser.c tests/test_parser.c -o build/test_parser.exe
./build/test_parser.exe
python -m unittest discover -s tests -p test_capture.py -v
```

覆盖短帧、认证/状态/原因字段、ACK、受保护载荷、QoS/HT EAPOL、分片/WDS
排除和 PCAP 文件头/时间/长度校验。固件还需在实际监听板上验证射频接收效果。

参考：[ESP-IDF Wi-Fi Sniffer 文档](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-guides/wifi.html#wi-fi-sniffer-mode)。
