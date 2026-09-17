"""OTA 服务端接口 —— Flask blueprint，可独立跑（app.py）也可挂进别的服务。

    from server.ota.api import ota_bp
    app.register_blueprint(ota_bp)

接口一览（端侧实现见 system/ota/ota.c）:

    GET  /api/ota/check?version=&channel=&device=   查更新，返回 manifest
    GET  /api/ota/download?version=                 下固件（支持 Range 续传）
    GET  /api/ota/versions                          列出仓库里全部固件
    POST /api/ota/report                            端侧回报升级结果
    POST /api/ota/publish                           发布新固件（需 token）
"""
from __future__ import annotations

import os

from flask import Blueprint, jsonify, request, send_file

import store

ota_bp = Blueprint("ota", __name__)

MAX_UPLOAD_BYTES = 16 * 1024 * 1024


def _manifest(fw: store.Firmware, channel: str) -> dict:
    """端侧解析的 manifest。字段名与 ota.c 的 parse_manifest() 一一对应。"""
    return {
        "version": fw.version,
        "url": f"{request.host_url.rstrip('/')}/api/ota/download?version={fw.version}",
        "size": fw.size,
        "sha256": fw.sha256,
        "date": fw.date,
        "changelog": fw.changelog,
        "mandatory": fw.mandatory,
        "min_battery": fw.min_battery,
        "channel": channel,
    }


@ota_bp.get("/api/ota/check")
def api_ota_check():
    """查更新。

    端侧带上自己的版本号（?version=0.1.0）时，额外返回 update_available，
    方便服务端侧统计；端侧自己也会再比一次版本，不依赖这个字段。
    """
    channel = request.args.get("channel", store.DEFAULT_CHANNEL).strip() or store.DEFAULT_CHANNEL
    current = request.args.get("version", "").strip()
    device = request.args.get("device", "") or request.headers.get("X-Device-Id", "")

    fw = store.latest(channel)
    if fw is None:
        return jsonify({"error": "no firmware published"}), 404

    body = _manifest(fw, channel)
    if current:
        body["update_available"] = store.is_newer(fw.version, current)
    body["device"] = device or None
    return jsonify(body)


@ota_bp.get("/api/ota/download")
def api_ota_download():
    """下发固件二进制。conditional=True 让 Flask 处理 Range —— esp_https_ota
    断点续传时会发 `Range: bytes=<已写字节>-`，少了它续传会拿到整包。"""
    version = request.args.get("version", "").strip()
    channel = request.args.get("channel", store.DEFAULT_CHANNEL).strip() or store.DEFAULT_CHANNEL

    fw = store.find(version) if version else store.latest(channel)
    if fw is None or not fw.path.exists():
        return jsonify({"error": "firmware not found"}), 404

    return send_file(
        str(fw.path),
        mimetype="application/octet-stream",
        as_attachment=True,
        download_name=fw.path.name,
        conditional=True,
    )


@ota_bp.get("/api/ota/versions")
def api_ota_versions():
    """仓库里有哪些固件 —— 发版核对 / 回滚时用。"""
    builds = store.scan()
    return jsonify({
        "count": len(builds),
        "latest": builds[0].version if builds else None,
        "items": [{
            "version": fw.version,
            "size": fw.size,
            "sha256": fw.sha256,
            "date": fw.date,
            "file": fw.path.name,
            "mandatory": fw.mandatory,
            "min_battery": fw.min_battery,
        } for fw in builds],
    })


@ota_bp.post("/api/ota/report")
def api_ota_report():
    """端侧升级结果回报。刷完重启后端侧发一条，失败时也发。"""
    data = request.get_json(silent=True) or {}
    status = str(data.get("status", "")).strip()
    if status not in ("success", "failed", "rollback"):
        return jsonify({"error": "status 必须是 success / failed / rollback"}), 400

    store.record_report({
        "device": str(data.get("device", ""))[:64],
        "from": str(data.get("from", ""))[:32],
        "to": str(data.get("to", ""))[:32],
        "status": status,
        "error": str(data.get("error", ""))[:256],
        "ip": request.remote_addr,
    })
    return jsonify({"ok": True})


@ota_bp.post("/api/ota/publish")
def api_ota_publish():
    """发布新固件。

    鉴权：设了环境变量 OTA_PUBLISH_TOKEN 就校验 X-OTA-Token；没设则只接受
    本机回环地址的请求（本地开发默认可用，公网部署必须设 token）。
    """
    token = os.environ.get("OTA_PUBLISH_TOKEN", "")
    if token:
        if request.headers.get("X-OTA-Token", "") != token:
            return jsonify({"error": "token 不匹配"}), 403
    elif request.remote_addr not in ("127.0.0.1", "::1"):
        return jsonify({"error": "未设置 OTA_PUBLISH_TOKEN，仅允许本机发布"}), 403

    upload = request.files.get("file")
    if upload is None:
        return jsonify({"error": "缺少 file 字段（固件 .bin）"}), 400

    data = upload.read(MAX_UPLOAD_BYTES + 1)
    if len(data) > MAX_UPLOAD_BYTES:
        return jsonify({"error": f"固件超过 {MAX_UPLOAD_BYTES} 字节"}), 413

    version = request.form.get("version", "").strip()
    channel = request.form.get("channel", store.DEFAULT_CHANNEL).strip() or store.DEFAULT_CHANNEL
    changelog = request.form.get("changelog", "")

    try:
        fw = store.publish(data, version, changelog, channel)
    except ValueError as e:
        return jsonify({"error": str(e)}), 400

    return jsonify({"ok": True, "channel": channel, **_manifest(fw, channel)})
