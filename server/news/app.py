"""
DeepStoa news service —— 给设备端 apps/news 供稿。

平时不用单独跑这个文件：`python server/run.py` 会把它和 OTA、日志一起挂在
同一个端口上（见 server/gateway.py）。这里保留独立入口只是为了单独调试：

    uvicorn app:app --host 0.0.0.0 --port 8000

接口（与 apps/news/service/news_svc_http.c 一一对应）：
    GET /api/health
        {"status": "ok", "count": 18}

    GET /api/news?page=1&page_size=6
        {"page": 1, "page_size": 6, "total": 18, "has_more": true,
         "items": [{"id", "title", "source", "date", "summary"}, ...]}

    GET /api/news/{article_id}
        {"id", "title", "source", "date", "content"}

设计上刻意为墨水屏设备做了两件事：
  * 列表接口不返回 content。设备一屏只放三张卡片，正文按需再拉，
    省一次几十 KB 的传输和同样大小的一块堆内存。
  * 字段全是扁平字符串，设备端解析不用递归。

固件的字段上限见 apps/news/model.h（title 128 / summary 192 / content 4096 字节），
超出部分会被设备截断，这里不做校验，方便你随手改数据试布局。
"""

from __future__ import annotations

import json
from pathlib import Path

from fastapi import APIRouter, FastAPI, HTTPException, Query
from fastapi.middleware.cors import CORSMiddleware

DATA_FILE = Path(__file__).with_name("articles.json")

router = APIRouter(tags=["news"])


def load_articles() -> list[dict]:
    """每次请求都重读一遍：改完 articles.json 不用重启服务。

    数据量只有几十 KB，重读的成本远低于开发时反复重启的成本。
    真上量了再换成启动时加载 + 文件监听。
    """
    with DATA_FILE.open(encoding="utf-8") as f:
        articles = json.load(f)

    seen: set[str] = set()
    for art in articles:
        aid = art.get("id")
        if not aid:
            raise ValueError(f"article without id: {art.get('title')!r}")
        if aid in seen:
            raise ValueError(f"duplicate article id: {aid}")
        seen.add(aid)
    return articles


@router.get("/api/health")
def health() -> dict:
    return {"status": "ok", "count": len(load_articles())}


@router.get("/api/news")
def list_news(
    page: int = Query(1, ge=1, description="页号，从 1 开始"),
    page_size: int = Query(6, ge=1, le=50, description="每页条数，设备端默认 6"),
) -> dict:
    """列表页。按 date 倒序，同日期的按 id 正序，保证翻页顺序稳定。

    两趟排序而不是一个 reverse=True 的复合 key：那样同日期的 id 会跟着倒过来，
    n01 排到 n02 后面，读者看到的顺序和数据文件里的顺序对不上。
    """
    articles = sorted(load_articles(), key=lambda a: a.get("id", ""))
    articles.sort(key=lambda a: a.get("date", ""), reverse=True)

    start = (page - 1) * page_size
    window = articles[start : start + page_size]

    return {
        "page": page,
        "page_size": page_size,
        "total": len(articles),
        "has_more": start + len(window) < len(articles),
        "items": [
            {
                "id": a["id"],
                "title": a.get("title", ""),
                "source": a.get("source", ""),
                "date": a.get("date", ""),
                "summary": a.get("summary", ""),
            }
            for a in window
        ],
    }


@router.get("/api/news/{article_id}")
def get_news(article_id: str) -> dict:
    """详情页。正文单独拉，列表接口里没有。"""
    for a in load_articles():
        if a["id"] == article_id:
            return {
                "id": a["id"],
                "title": a.get("title", ""),
                "source": a.get("source", ""),
                "date": a.get("date", ""),
                "content": a.get("content", ""),
            }
    raise HTTPException(status_code=404, detail=f"no such article: {article_id}")


# ── 独立入口（聚合服务不会走到这里，它只 import 上面的 router） ──────────
app = FastAPI(
    title="DeepStoa News",
    description="Article feed for the DeepStoa e-ink device.",
    version="1.0.0",
)

# 设备端不受同源策略约束，这里放开只是方便用浏览器直接看接口
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["GET"],
    allow_headers=["*"],
)

app.include_router(router)
