"""发布一次固件 —— 把 idf.py build 出来的 bin 放进 OTA 仓库。

本机发布（直接写 firmware/ 目录，不需要服务在跑）:

    python publish.py --bin ../../build/DeepStoa.bin

远端发布（POST 给已经跑起来的 OTA 服务）:

    OTA_PUBLISH_TOKEN=xxx python publish.py --bin ../../build/DeepStoa.bin \
        --server http://192.168.101.41:8010

版本号默认读工程根目录的 version.txt（ESP-IDF 也是用它当 PROJECT_VER，
两边必须是同一个值，否则端侧比版本会一直认为"已是最新"）。
更新说明默认读 --changelog 指向的文件，没给就留空。
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import store

PROJECT_ROOT = Path(__file__).resolve().parents[2]


def _default_version() -> str:
    path = PROJECT_ROOT / "version.txt"
    if path.exists():
        return path.read_text(encoding="utf-8").strip()
    return ""


def main() -> int:
    parser = argparse.ArgumentParser(description="发布 DeepStoa 固件")
    parser.add_argument("--bin", default=str(PROJECT_ROOT / "build" / "DeepStoa.bin"),
                        help="固件路径，默认 build/DeepStoa.bin")
    parser.add_argument("--version", default="", help="版本号 x.y.z，默认读 version.txt")
    parser.add_argument("--changelog", default="", help="更新说明文件路径")
    parser.add_argument("--channel", default=store.DEFAULT_CHANNEL, help="发布通道，默认 stable")
    parser.add_argument("--server", default="", help="远端 OTA 服务地址，不给则写本地 firmware/")
    args = parser.parse_args()

    bin_path = Path(args.bin)
    if not bin_path.exists():
        print(f"找不到固件: {bin_path}", file=sys.stderr)
        return 1

    version = args.version.strip() or _default_version()
    if store.parse_version(version) is None:
        print(f"版本号必须形如 x.y.z（--version 或 version.txt），当前: {version!r}", file=sys.stderr)
        return 1

    changelog = ""
    if args.changelog:
        changelog = Path(args.changelog).read_text(encoding="utf-8")

    data = bin_path.read_bytes()

    if args.server:
        import requests
        headers = {}
        token = os.environ.get("OTA_PUBLISH_TOKEN", "")
        if token:
            headers["X-OTA-Token"] = token
        resp = requests.post(
            f"{args.server.rstrip('/')}/api/ota/publish",
            headers=headers,
            files={"file": (bin_path.name, data, "application/octet-stream")},
            data={"version": version, "changelog": changelog, "channel": args.channel},
            timeout=120,
        )
        if resp.status_code != 200:
            print(f"发布失败 HTTP {resp.status_code}: {resp.text}", file=sys.stderr)
            return 1
        print(resp.json())
        return 0

    fw = store.publish(data, version, changelog, args.channel)
    print(f"已发布 v{fw.version} ({fw.size} 字节) → {fw.path}")
    print(f"  sha256  {fw.sha256}")
    print(f"  通道    {args.channel}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
