# DeepStoa OTA 服务端

给设备提供固件检查 / 下载 / 结果回报的最小服务。端侧实现在 `system/ota/`，
UI 在 `apps/settings/subpages/view_update.c`。

## 跑起来

平时**不用单独跑这个**。总入口会把 news / ota / logs 挂在同一个端口（默认 8000）：

```bash
pip install -r server/requirements.txt
python server/run.py
```

固件的 `OTA_DEFAULT_MANIFEST_URL`（`system/ota/ota.h`）就是指向那个聚合端口的。
完整链路说明（热点、防火墙、固件常量）见 [`server/README.md`](../README.md)。

只想单独调 OTA 时：

```bash
cd server/ota
python app.py                # 监听 0.0.0.0:8010
```

注意单独跑在 8010 时，固件那边的 `OTA_DEFAULT_MANIFEST_URL` 要跟着改端口，
或者在设备 Settings 里覆盖 `manifest_url`（不用重编）。

聚合是怎么挂的：`server/gateway.py` 把这里的 blueprint 包成 WSGI 应用挂进
FastAPI。想挂进别的 Flask 服务也一样：

```python
from api import ota_bp
app.register_blueprint(ota_bp)
```

## 发布一版固件

```bash
# 1. 改工程根目录 version.txt（ESP-IDF 用它当 PROJECT_VER，端侧比的就是这个）
echo 0.2.0 > ../../version.txt

# 2. 重新构建
idf.py build

# 3. 发布（本机直接写 firmware/ 目录）
python publish.py --bin ../../build/DeepStoa.bin --changelog notes.txt

# 或者发给远端已经在跑的服务
OTA_PUBLISH_TOKEN=xxx python publish.py --server http://192.168.101.41:8010
```

**version.txt 必须和发布的版本号一致。** 端侧拿 `esp_app_get_description()->version`
和 manifest 里的 `version` 比大小，两边不一致会导致设备永远认为"已是最新"，
或者反复重装同一个版本。

## 接口

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/api/ota/check?version=&channel=&device=` | 查更新，返回 manifest |
| GET | `/api/ota/download?version=` | 下载固件，支持 `Range` 续传 |
| GET | `/api/ota/versions` | 列出仓库里全部固件 |
| POST | `/api/ota/report` | 端侧回报升级结果 |
| POST | `/api/ota/publish` | 发布新固件（multipart） |

### GET /api/ota/check

```json
{
  "version": "0.2.0",
  "url": "http://192.168.101.41:8010/api/ota/download?version=0.2.0",
  "size": 1043712,
  "sha256": "9f2c…",
  "date": "2026-09-01",
  "changelog": "- 修复墨水屏残影\n- 新增 OTA 升级",
  "mandatory": false,
  "min_battery": 30,
  "channel": "stable",
  "update_available": true
}
```

`update_available` 只在请求带了 `?version=` 时出现，供服务端统计用；
**端侧不依赖它**，自己会再比一次版本号，所以服务端只管返回当前通道的最新版即可。

`min_battery` / `mandatory` 由服务端下发，端侧照着做二次确认的拦截和文案：
电量不足 `min_battery` 且没在充电时不让点确认；`mandatory` 为真时确认弹窗
会提示这是必装更新。没有固件时返回 404 `{"error": "no firmware published"}`。

### GET /api/ota/download

`conditional=True` 让 Flask 处理 `Range` 请求。`esp_https_ota` 断线重连后会发
`Range: bytes=<已写字节>-` 续传，缺了这个会重新拉整包。

### POST /api/ota/report

```json
{"device":"a0:b1:…","from":"0.1.0","to":"0.2.0","status":"success","error":""}
```

`status` 取 `success` / `failed` / `rollback`，一行一条追加进 `firmware/reports.ndjson`。

### POST /api/ota/publish

multipart 表单：`file`（.bin）、`version`、`changelog`、`channel`。

鉴权：设了环境变量 `OTA_PUBLISH_TOKEN` 就校验请求头 `X-OTA-Token`；没设时只接受
本机回环地址的请求。**公网部署一定要设 token。**

## 固件仓库布局

```
firmware/
    DeepStoa_v0.1.0.bin        版本号从文件名解析，解析不出的文件会被忽略
    DeepStoa_v0.2.0.bin
    changelog/0.2.0.txt        可选，该版本的更新说明
    channels.json              可选，通道钉版本 + 升级策略
    reports.ndjson             端侧回报（追加写）
```

`channels.json`：

```json
{
  "channels": {"stable": "0.1.0", "beta": "0.2.0"},
  "policy":   {"min_battery": 30, "mandatory": false},
  "versions": {"0.2.0": {"mandatory": true, "min_battery": 50}}
}
```

没有 `channels.json` 时任何通道都取版本号最大的那个 bin。要回滚，把
`channels.stable` 改回旧版本号即可 —— 但端侧只在远端版本**更大**时才提示升级，
所以回滚需要发一个版本号更大的包，不能靠改钉版本把设备降下去。
