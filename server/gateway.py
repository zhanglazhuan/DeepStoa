"""
DeepStoa 开发服务端 —— 把设备会访问的所有接口聚到一个进程、一个端口。

    from gateway import app        # uvicorn gateway:app
    python run.py                  # 正常用这个，见 run.py

为什么要聚合：板子通过本机热点接进来，每多一个端口就多一条防火墙规则、
多一个要在固件里改的常量。聚合之后固件只认一个 base URL。

组成：
    /api/news/*    server/news/app.py   FastAPI，直接 include_router
    /api/logs/*    server/logs_api.py   FastAPI
    /api/ota/*     server/ota/api.py    Flask blueprint，用 WSGI 适配挂进来

OTA 那套是 Flask 写的（固件下载要用 Flask 的 Range 续传支持，
esp_https_ota 断点续传依赖它），不值得为了统一框架重写一遍，
所以这里直接把它当 WSGI 应用挂在最后兜底。
"""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

from fastapi import FastAPI

# a2wsgi 是 starlette 官方推荐的替代品；退化到 starlette 自带的那份也能跑，
# 只是会打一条 deprecation 警告。
try:
    from a2wsgi import WSGIMiddleware
except ImportError:  # pragma: no cover
    from starlette.middleware.wsgi import WSGIMiddleware

HERE = Path(__file__).resolve().parent


def _load(mod_name: str, path: Path, import_root: Path):
    """按文件路径加载模块，并给它一个唯一的模块名。

    news 和 ota 目录下都有 app.py，直接 `import app` 第二次会命中第一次的
    缓存，拿到错的模块。这里显式指定模块名绕开。
    import_root 要加进 sys.path —— ota/api.py 里是 `import store` 这种
    平级导入，找不到路径会直接 ImportError。
    """
    root = str(import_root)
    if root not in sys.path:
        sys.path.insert(0, root)

    spec = importlib.util.spec_from_file_location(mod_name, path)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load {path}")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[mod_name] = mod
    spec.loader.exec_module(mod)
    return mod


def _build_ota_wsgi():
    """把 ota 的 Flask blueprint 包成一个可挂载的 WSGI 应用。"""
    from flask import Flask, jsonify

    api = _load("deepstoa_ota_api", HERE / "ota" / "api.py", HERE / "ota")
    store = sys.modules["store"]          # api.py 里 import 进来的那份

    store.FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)

    flask_app = Flask("deepstoa_ota")
    flask_app.json.compact = True
    flask_app.register_blueprint(api.ota_bp)

    @flask_app.errorhandler(404)
    def _not_found(_e):
        # 走到这里说明路径既不是 /api/news 也不是 /api/logs 也不是 /api/ota
        return jsonify({"error": "no such endpoint", "hint": "GET / 看接口清单"}), 404

    return WSGIMiddleware(flask_app), store


def create_app() -> FastAPI:
    news = _load("deepstoa_news", HERE / "news" / "app.py", HERE / "news")
    sys.path.insert(0, str(HERE))
    import logs_api

    ota_wsgi, ota_store = _build_ota_wsgi()

    app = FastAPI(
        title="DeepStoa Dev Server",
        description="板子在开发期会访问的全部接口：news / ota / logs。",
        version="1.0.0",
    )

    app.include_router(news.router)
    app.include_router(logs_api.router)

    @app.get("/", tags=["meta"])
    def index() -> dict:
        latest = ota_store.latest()
        return {
            "service": "DeepStoa Dev Server",
            "news_articles": len(news.load_articles()),
            "ota_latest": latest.version if latest else None,
            "endpoints": {
                "GET  /api/health": "存活探测 + 文章总数",
                "GET  /api/news?page=&page_size=": "新闻列表（不含正文）",
                "GET  /api/news/{id}": "新闻详情（含正文）",
                "GET  /api/ota/check?version=&channel=": "查更新，返回 manifest",
                "GET  /api/ota/download?version=": "下载固件（支持 Range 续传）",
                "GET  /api/ota/versions": "列出仓库里全部固件",
                "POST /api/ota/report": "端侧回报升级结果",
                "POST /api/ota/publish": "发布新固件",
                "POST /api/logs/upload": "设备日志上传",
                "GET  /api/logs/sessions": "收到过哪些日志会话",
                "GET  /docs": "交互式接口文档",
            },
        }

    # 必须最后挂：Starlette 按注册顺序匹配，"/" 会吃掉一切未匹配的路径。
    # 放在所有 FastAPI 路由之后，/api/news 之类才不会被它抢走。
    app.mount("/", ota_wsgi)
    return app


app = create_app()
