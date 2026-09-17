# DeepStoa News Service

给设备端 `apps/news` 供稿的最小 FastAPI 服务。文章数据放在 `articles.json`，
改完不用重启（每次请求重读）。

## 跑起来

平时**不用单独跑这个**。总入口会把 news / ota / logs 挂在同一个端口上：

```bash
pip install -r server/requirements.txt
python server/run.py
```

完整的链路说明（热点、防火墙、固件常量）见 [`server/README.md`](../README.md)。

只想单独调新闻这一块时：

```bash
cd server/news
uvicorn app:app --host 0.0.0.0 --port 8000
```

`--host 0.0.0.0` 是必须的：设备要从局域网访问，绑 127.0.0.1 它连不上。
交互文档在 <http://localhost:8000/docs>。

## 接口

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/api/health` | 存活探测，返回文章总数 |
| GET | `/api/news?page=1&page_size=6` | 列表页，**不含正文** |
| GET | `/api/news/{id}` | 详情，含正文 |

```jsonc
// GET /api/news?page=1&page_size=6
{
  "page": 1, "page_size": 6, "total": 18, "has_more": true,
  "items": [
    { "id": "n01", "title": "...", "source": "The Verge",
      "date": "2026-09-01", "summary": "..." }
  ]
}

// GET /api/news/n01
{ "id": "n01", "title": "...", "source": "...", "date": "...", "content": "..." }
```

列表接口刻意不带正文：设备一屏只放三张卡片，正文按需再拉，省一次传输和
同样大小的一块堆内存。

## 接设备

`NEWS_API_BASE`（`apps/news/service/news_svc_http.c` 顶部）默认已经指向
`http://192.168.137.1:8000` —— Windows 移动热点的固定网关，板子连上本机热点后
直接就能访问，不用改。换了组网方式再按 `run.py` 启动横幅打印的地址改。

换数据源重新编译：

```bash
idf.py -DNEWS_SVC=http build
```

默认是 `mock`，用固件内置假数据，不需要这个服务也能跑通全部 UI 和交互。
`NEWS_SVC` 是 CMake cache 变量，切过去之后会一直留在 `build/` 里，换回来要显式写
`idf.py -DNEWS_SVC=mock build`。

## 改数据

直接编辑 `articles.json`，字段就是接口里那几个。注意设备端的字节上限
（见 `apps/news/model.h`）：

| 字段 | 上限 | 超出后 |
| --- | --- | --- |
| `title` | 128 | 截断 |
| `source` | 48 | 截断 |
| `date` | 24 | 截断 |
| `summary` | 192 | 截断（卡片上还会再收到约 100 字符） |
| `content` | 4096 | 截断 |

**只用 ASCII。** 固件里只编了 Montserrat 字库，没有 CJK 字形，中文会渲染成方框。
要上中文得先在 `system/uilv` 里加一份中文字库。

列表按 `date` 倒序返回，同日期按 `id` 倒序，保证翻页顺序稳定。
