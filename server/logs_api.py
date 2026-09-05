"""
设备日志接收端 —— 对应 system/logs/log_uploader.c。

端侧行为（upload_chunk()）：
    POST /api/logs/upload
    Content-Type: application/json
    {"session": "s_20260901_013000", "lines": ["...", "..."]}

    只看 HTTP 状态码：200 就认为这批行上传成功，把 .idx 的水位往前推；
    非 200 会重试，累计 3 次失败后放弃该 session。响应体只进日志，不解析。

注意：截至目前 log_uploader.c 并没有编进固件（system/logs/CMakeLists.txt 的
SRCS 里没有它），所以这个端点暂时不会有请求进来。留着是为了端侧一打开就有
东西接着，格式以 log_uploader.c 为准。
"""

from __future__ import annotations

import re
from datetime import datetime
from pathlib import Path

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

LOG_DIR = Path(__file__).resolve().parent / "logs_data"

# session 名直接来自设备，会被拼进文件路径 —— 只放行 log_uploader.c 真正会
# 生成的那种形状（s_YYYYMMDD_HHMMSS），杜绝 ../ 之类的路径穿越。
SESSION_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")

router = APIRouter(tags=["logs"])


class LogUpload(BaseModel):
    session: str = Field(..., description="会话名，形如 s_20260901_013000")
    lines: list[str] = Field(default_factory=list)


@router.post("/api/logs/upload")
def upload(body: LogUpload) -> dict:
    if not SESSION_RE.match(body.session):
        raise HTTPException(status_code=400, detail="bad session name")

    LOG_DIR.mkdir(parents=True, exist_ok=True)
    target = LOG_DIR / f"{body.session}.log"

    with target.open("a", encoding="utf-8") as f:
        for line in body.lines:
            f.write(line.rstrip("\n") + "\n")

    print(f"[logs] +{len(body.lines):3d} lines -> {target.name}")
    return {
        "ok": True,
        "received": len(body.lines),
        "session": body.session,
        "at": datetime.now().isoformat(timespec="seconds"),
    }


@router.get("/api/logs/sessions")
def sessions() -> dict:
    """收到过哪些 session —— 纯粹方便在浏览器里核对上传是否真的到了。"""
    if not LOG_DIR.exists():
        return {"count": 0, "items": []}

    items = [
        {"session": p.stem, "bytes": p.stat().st_size,
         "mtime": datetime.fromtimestamp(p.stat().st_mtime).isoformat(timespec="seconds")}
        for p in sorted(LOG_DIR.glob("*.log"))
    ]
    return {"count": len(items), "items": items}
