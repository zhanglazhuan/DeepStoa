# DeepStoa 开发服务端

板子在开发期会访问的全部接口，聚在**一个进程、一个端口**上。

```bash
pip install -r server/requirements.txt
python server/run.py
```

就这一条命令。启动后会打印本机地址、热点状态、防火墙状态，以及固件里该填的
地址。

## 为什么要聚合

板子通过本机热点接进来。每多一个端口，就多一条要开的防火墙规则、多一个要在
固件里改的常量、多一个"忘了起"的进程。聚合之后固件只认一个 base URL：

| 路径 | 来源 | 框架 |
| --- | --- | --- |
| `/api/news/*` | `server/news/app.py` | FastAPI |
| `/api/logs/*` | `server/logs_api.py` | FastAPI |
| `/api/ota/*` | `server/ota/api.py` | Flask（WSGI 挂载） |

OTA 那套是 Flask 写的，固件下载的 Range 续传依赖 Flask 的 `send_file(conditional=True)`
（`esp_https_ota` 断点续传会发 `Range: bytes=<已写字节>-`）。不值得为了统一框架
重写一遍，所以 `gateway.py` 用 WSGI 适配层把它挂在路由表最后兜底。

各子服务仍可单独跑（`server/news/app.py`、`server/ota/app.py`），只在单独调试
某一块时才需要。

## 完整链路：板子 → 本机热点 → 服务端

### 1. 移动热点（`run.py` 自动开，不用管）

启动时会自己把 Windows 移动热点打开，并把 SSID / 密码打在横幅里 —— 那两项就是
待会儿要在设备 Settings 里填的。已经开着就跳过。

**网关恒为 `192.168.137.1`**（ICS 的默认网段），板子连上后拿到 `192.168.137.x`。
固件里就是写死这个地址，不用每次开机去查本机 IP。

用路由器组网、不想让脚本碰热点：

```bash
python server/run.py --no-hotspot
```

实现细节和坑都在 `server/hotspot.py` 的模块注释里，几条要点：

* 走的是 WinRT 的 `NetworkOperatorTetheringManager`。移动热点没有命令行接口，
  `netsh wlan start hostednetwork` 是被废弃的旧 API，现在多数网卡驱动不支持。
* 内部固定调用 **`powershell.exe`（Windows PowerShell 5.1）**，不是 `pwsh` 7 ——
  PowerShell Core 6+ 移除了 WinRT 类型投影，在 7.x 里调不了。
  你本机装的 pwsh 版本不影响这里。
* **PC 自己必须联网。** 断网时 `GetInternetConnectionProfile()` 返回 null，
  Windows 根本创建不出热点。这是系统限制，脚本只能如实报错。
* 热点频段若被设成 5GHz 会警告 —— ESP32-S3 只有 2.4GHz，连不上。
* 开热点失败一律只警告不中断，服务照常起。

> 横幅会**明文打印热点密码**（不打印就没法照着填设备）。演示/录屏时注意，
> 或者加 `--no-hotspot` 自己开。

### 2. 放行防火墙

这是最容易踩的坑：Windows 默认丢弃入站连接，板子的请求会**静默超时**，服务端
这边一条日志都看不到。

```powershell
# 管理员 PowerShell
python server\run.py --setup-firewall
```

或手动：

```powershell
netsh advfirewall firewall add rule name="DeepStoa Dev Server" dir=in action=allow protocol=TCP localport=8000
```

`run.py` 启动时会检查这条规则在不在，不在就把命令打出来。

### 3. 起服务端

```bash
python server/run.py
```

### 4. 板子连热点

在设备的 Settings 里填热点的 SSID / 密码 —— 就是第 3 步横幅里打出来的那两项。
固件没有内置默认凭据（`wifi_manager` 从 NVS 读上次保存的），第一次要手动配一遍，
之后会自动重连。

### 5. 编固件时打开 http 数据源

news 默认用固件内置的 mock 数据，不走网络：

```bash
idf.py -DNEWS_SVC=http build flash monitor
```

（`NEWS_SVC` 是 CMake cache 变量，换回 mock 要显式写 `-DNEWS_SVC=mock`。）

### 6. 验

服务端这边应该能看到请求日志；板子上进 News，列表应该出真实数据。
浏览器打开 <http://192.168.137.1:8000/docs> 可以手动点各个接口。

## 固件里对应的常量

改了 `--port` 或换了组网方式（路由器 / Linux 热点），这三处要跟着改成
`run.py` 启动横幅里打印的那个地址：

| 常量 | 文件 | 默认值 |
| --- | --- | --- |
| `NEWS_API_BASE` | `apps/news/service/news_svc_http.c` | `http://192.168.137.1:8000` |
| `OTA_DEFAULT_MANIFEST_URL` | `system/ota/ota.h` | `http://192.168.137.1:8000/api/ota/check` |
| `PODCAST_SERVER` | `system/logs/log_uploader.c` | `http://192.168.137.1:8000` |

OTA 的地址还可以在设备的 Settings 里覆盖（`settings_model` 的 `manifest_url`），
不用重新编译。

## 接口

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/` | 服务清单 |
| GET | `/docs` | 交互式接口文档 |
| GET | `/api/health` | 存活探测 + 文章总数 |
| GET | `/api/news?page=1&page_size=6` | 新闻列表，**不含正文** |
| GET | `/api/news/{id}` | 新闻详情，含正文 |
| GET | `/api/ota/check?version=&channel=` | 查更新，返回 manifest |
| GET | `/api/ota/download?version=` | 下载固件（Range 续传） |
| GET | `/api/ota/versions` | 固件列表 |
| POST | `/api/ota/report` | 端侧回报升级结果 |
| POST | `/api/ota/publish` | 发布新固件 |
| POST | `/api/logs/upload` | 设备日志上传 |
| GET | `/api/logs/sessions` | 已收到的日志会话 |

细节见 `server/news/README.md`、`server/ota/README.md`。

## 量产 / 云端部署

热点、防火墙、本机 IP 这些全是**开发机**的事，只存在于 `run.py` 和
`hotspot.py`。真正的应用是 `gateway.py`，它不 import 这两个模块，没有任何
Windows 依赖，直接就能丢到云主机上：

```bash
uvicorn gateway:app --host 0.0.0.0 --port 8000
# 或者多进程
gunicorn gateway:app -k uvicorn.workers.UvicornWorker -w 4 -b 0.0.0.0:8000
```

前面套 nginx / Caddy 做 TLS 和域名即可。

固件侧要改的：

* **OTA** 的 manifest 地址可以在设备 Settings 里改（`settings_model` 的
  `manifest_url`），**不用重新编译**。
* **news** 的 `NEWS_API_BASE` 是编译期常量，换域名要重编。想做成可配置的话，
  参照 OTA 那样走一遍 settings。

**上云前必须补的一个缺口：`news_svc_http.c` 没有挂证书 bundle。**
`ota.c` 和 `log_uploader.c` 的 `esp_http_client_config_t` 里都有
`.crt_bundle_attach = esp_crt_bundle_attach`，news 没有 —— 现在走 `http://`
没事，一旦换成 `https://` 会直接握手失败。要加的是两行（include + 该字段），
外加组件的 `PRIV_REQUIRES` 补上 `mbedtls`。

另外两处目前是本地文件、上量要换掉：新闻数据是 `news/articles.json`
（每请求重读），固件仓库是 `ota/firmware/` 目录。

## 常见问题

**板子请求超时，服务端没有任何日志** —— 防火墙。见上面第 2 步。请求根本没到
进程，所以服务端看不到。

**`curl http://127.0.0.1:8000/api/health` 通，板子不通** —— 同样是防火墙，或者
服务端绑在了 `127.0.0.1`。默认的 `0.0.0.0` 才对，别加 `--host 127.0.0.1`。

**热点开着但板子拿不到 IP** —— Windows 移动热点默认只允许 2.4GHz 客户端接入，
ESP32-S3 也只有 2.4GHz，正常不冲突；先确认热点的"网络频段"没被设成 5GHz。

**News 里还是 mock 数据** —— 固件编的是默认的 mock 数据源，重编时加
`-DNEWS_SVC=http`。

**日志上传没反应** —— 截至目前 `log_uploader.c` 没有编进固件
（`system/logs/CMakeLists.txt` 的 SRCS 里没有它），端点是留好的但端侧不会发。

## 目录

```
server/
  run.py            开发入口：开热点 + 查防火墙 + 起服务      ← 只在开发机上用
  hotspot.py        Windows 移动热点控制（WinRT）             ← 只在开发机上用
  gateway.py        把三套接口组装成一个 FastAPI 应用          ← 云端部署跑这个
  logs_api.py       设备日志接收
  requirements.txt  全部依赖
  news/             新闻服务 + articles.json
  ota/              OTA 服务 + firmware/ 固件仓库
  logs_data/        收到的设备日志（运行产物，已 gitignore）
```

上面两个标了「只在开发机上用」的文件，`gateway.py` 一个都不 import ——
这条边界是刻意画的，别让热点/防火墙逻辑漏进应用层。
