"""固件仓库 —— 磁盘上的固件文件 + 版本选择 / 发布 / 回报落盘。

目录结构（默认 server/ota/firmware/）:

    firmware/
        DeepStoa_v0.1.0.bin        固件本体，版本号从文件名解析
        DeepStoa_v0.2.0.bin
        changelog/0.2.0.txt        可选，该版本的更新说明
        channels.json              可选，通道钉版本 + 升级策略
        reports.ndjson             端侧升级结果回报（追加写）

channels.json:

    {
      "channels": {"stable": "0.1.0", "beta": "0.2.0"},
      "policy":   {"min_battery": 30, "mandatory": false},
      "versions": {"0.2.0": {"mandatory": true, "min_battery": 50}}
    }

没有 channels.json 时，任何通道都取版本号最大的那个 .bin。
"""
from __future__ import annotations

import datetime
import hashlib
import os
import json
import re
import threading
from dataclasses import dataclass
from pathlib import Path

# 固件目录，可用环境变量 OTA_FIRMWARE_DIR 换掉（测试 / 多实例部署）
FIRMWARE_DIR = Path(os.environ.get(
    "OTA_FIRMWARE_DIR", Path(__file__).resolve().parent / "firmware"))
CHANNELS_FILE = "channels.json"
REPORTS_FILE = "reports.ndjson"
CHANGELOG_DIR = "changelog"
DEFAULT_CHANNEL = "stable"

DEFAULT_POLICY = {"min_battery": 30, "mandatory": False}

_VERSION_RE = re.compile(r"(\d+)\.(\d+)\.(\d+)")
_WRITE_LOCK = threading.Lock()
_SHA_CACHE: dict[tuple[str, int, int], str] = {}


# ─── 版本号 ────────────────────────────────────────────────────────────

def parse_version(text: str) -> tuple[int, int, int] | None:
    """从任意字符串里抠出 x.y.z。抠不出返回 None。"""
    m = _VERSION_RE.search(text or "")
    if not m:
        return None
    return int(m.group(1)), int(m.group(2)), int(m.group(3))


def version_key(version: str) -> tuple[int, int, int]:
    return parse_version(version) or (0, 0, 0)


def is_newer(candidate: str, current: str) -> bool:
    """candidate 是否比 current 新。任一侧解析不出就退化成字符串不等判断。"""
    a, b = parse_version(candidate), parse_version(current)
    if a is None or b is None:
        return (candidate or "") != (current or "")
    return a > b


# ─── 固件条目 ──────────────────────────────────────────────────────────

@dataclass
class Firmware:
    version: str
    path: Path
    size: int
    sha256: str
    date: str
    changelog: str
    mandatory: bool
    min_battery: int


def _sha256(path: Path) -> str:
    st = path.stat()
    key = (str(path), st.st_size, int(st.st_mtime))
    cached = _SHA_CACHE.get(key)
    if cached:
        return cached
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    digest = h.hexdigest()
    _SHA_CACHE[key] = digest
    return digest


def _config(root: Path) -> dict:
    path = root / CHANNELS_FILE
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return {}


def _changelog(root: Path, version: str) -> str:
    """优先 changelog/<version>.txt，退回 changelog.txt，都没有就空串。"""
    for candidate in (root / CHANGELOG_DIR / f"{version}.txt", root / "changelog.txt"):
        if candidate.exists():
            try:
                return candidate.read_text(encoding="utf-8").strip()
            except OSError:
                pass
    return ""


def _policy_for(cfg: dict, version: str) -> dict:
    policy = dict(DEFAULT_POLICY)
    policy.update(cfg.get("policy") or {})
    policy.update((cfg.get("versions") or {}).get(version) or {})
    return policy


def _build(root: Path, path: Path, version: str, cfg: dict) -> Firmware:
    st = path.stat()
    policy = _policy_for(cfg, version)
    return Firmware(
        version=version,
        path=path,
        size=st.st_size,
        sha256=_sha256(path),
        date=datetime.datetime.fromtimestamp(st.st_mtime).strftime("%Y-%m-%d"),
        changelog=_changelog(root, version),
        mandatory=bool(policy.get("mandatory", False)),
        min_battery=int(policy.get("min_battery", DEFAULT_POLICY["min_battery"])),
    )


def _root(root: Path | None) -> Path:
    """None 时取当前的 FIRMWARE_DIR —— 用默认形参会在 import 时绑死。"""
    return root if root is not None else FIRMWARE_DIR


def scan(root: Path | None = None) -> list[Firmware]:
    """列出全部可用固件，从新到旧。文件名解析不出版本号的忽略。"""
    root = _root(root)
    if not root.exists():
        return []
    cfg = _config(root)
    out: list[Firmware] = []
    for path in root.glob("*.bin"):
        parsed = parse_version(path.name)
        if parsed is None:
            continue
        out.append(_build(root, path, "%d.%d.%d" % parsed, cfg))
    out.sort(key=lambda fw: version_key(fw.version), reverse=True)
    return out


def find(version: str, root: Path | None = None) -> Firmware | None:
    return next((fw for fw in scan(root) if fw.version == version), None)


def latest(channel: str = DEFAULT_CHANNEL, root: Path | None = None) -> Firmware | None:
    """通道钉了版本就用钉的那个；没钉（或钉的文件不在）就用版本号最大的。"""
    root = _root(root)
    builds = scan(root)
    if not builds:
        return None
    pinned = (_config(root).get("channels") or {}).get(channel)
    if pinned:
        hit = next((fw for fw in builds if fw.version == pinned), None)
        if hit:
            return hit
    return builds[0]


# ─── 发布 / 回报 ───────────────────────────────────────────────────────

def publish(data: bytes, version: str, changelog: str = "",
            channel: str = DEFAULT_CHANNEL, root: Path | None = None) -> Firmware:
    """落盘一份新固件，并把通道指向它。版本号必须是 x.y.z。"""
    root = _root(root)
    parsed = parse_version(version)
    if parsed is None:
        raise ValueError(f"版本号必须形如 x.y.z，收到 {version!r}")
    version = "%d.%d.%d" % parsed
    if not data:
        raise ValueError("固件内容为空")

    with _WRITE_LOCK:
        root.mkdir(parents=True, exist_ok=True)
        (root / CHANGELOG_DIR).mkdir(exist_ok=True)

        (root / f"DeepStoa_v{version}.bin").write_bytes(data)
        if changelog.strip():
            (root / CHANGELOG_DIR / f"{version}.txt").write_text(
                changelog.strip() + "\n", encoding="utf-8")

        cfg = _config(root)
        cfg.setdefault("channels", {})[channel] = version
        (root / CHANNELS_FILE).write_text(
            json.dumps(cfg, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    fw = find(version, root)
    assert fw is not None
    return fw


def record_report(entry: dict, root: Path | None = None) -> None:
    """端侧升级结果追加进 reports.ndjson，一行一条。"""
    root = _root(root)
    entry = dict(entry)
    entry["received_at"] = datetime.datetime.now().isoformat(timespec="seconds")
    with _WRITE_LOCK:
        root.mkdir(parents=True, exist_ok=True)
        with (root / REPORTS_FILE).open("a", encoding="utf-8") as f:
            f.write(json.dumps(entry, ensure_ascii=False) + "\n")
