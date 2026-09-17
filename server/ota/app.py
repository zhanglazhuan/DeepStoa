"""DeepStoa OTA 服务 —— 独立入口。

    pip install -r requirements.txt
    python app.py                 # 0.0.0.0:8010
    python app.py --port 8000     # 换端口

要挂进别的 Flask 服务（比如 server/news）里，直接注册 blueprint:

    from ota.api import ota_bp
    app.register_blueprint(ota_bp)
"""
from __future__ import annotations

import argparse

from flask import Flask, jsonify

from api import ota_bp
import store

app = Flask(__name__)
app.json.compact = True
app.register_blueprint(ota_bp)


@app.get("/")
def index():
    fw = store.latest()
    return jsonify({
        "service": "DeepStoa OTA",
        "firmware_dir": str(store.FIRMWARE_DIR),
        "latest": fw.version if fw else None,
        "endpoints": {
            "GET  /api/ota/check?version=&channel=&device=": "查更新，返回 manifest",
            "GET  /api/ota/download?version=": "下载固件（支持 Range 续传）",
            "GET  /api/ota/versions": "列出仓库里全部固件",
            "POST /api/ota/report": "端侧回报升级结果",
            "POST /api/ota/publish": "发布新固件（multipart: file/version/changelog/channel）",
        },
    })


def main() -> None:
    parser = argparse.ArgumentParser(description="DeepStoa OTA 服务")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8010)
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()

    store.FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)
    app.run(host=args.host, port=args.port, debug=args.debug, threaded=True)


if __name__ == "__main__":
    main()
