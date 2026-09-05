"""
DeepStoa 开发服务端 —— 总入口。

    python server/run.py

一条命令把 news / ota / logs 三套接口起在同一个端口上，并检查板子能不能
真的连进来（热点是否开着、防火墙是否放行），把要在固件里填的地址直接打出来。

典型链路：

    PC 开移动热点  →  板子连上热点，拿到 192.168.137.x
    板子请求 http://192.168.137.1:8000/api/news?page=1
    本进程返回数据

Windows 移动热点的网关固定是 192.168.137.1（ICS 的默认网段），所以固件里
写死这个地址就行，不用每次开机去看本机 IP。

常用参数：
    --port 8000          换端口（换了记得同步改固件里的常量）
    --reload             改完 server/ 下的代码自动重启
    --setup-firewall     尝试添加入站放行规则（需要管理员权限）
"""

from __future__ import annotations

import argparse
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

import hotspot

HERE = Path(__file__).resolve().parent

DEFAULT_PORT = 8000

# Windows「移动热点」走的是 ICS，网关恒为 192.168.137.1。
# 板子连上热点后拿到 192.168.137.x，访问这个地址就是本机。
HOTSPOT_IP = "192.168.137.1"

FIREWALL_RULE = "DeepStoa Dev Server"

# 固件里需要和本服务对齐的常量。改了 --port 或换了热点方案，这几处要跟着改。
FIRMWARE_REFS = [
    ("apps/news/service/news_svc_http.c", "NEWS_API_BASE", "{base}"),
    ("system/ota/ota.h", "OTA_DEFAULT_MANIFEST_URL", "{base}/api/ota/check"),
    ("system/logs/log_uploader.c", "PODCAST_SERVER", "{base}"),
]


# ── 环境探测 ────────────────────────────────────────────────────────────

def local_ipv4() -> list[str]:
    """列出本机的 IPv4 地址。

    不引第三方库：先问主机名对应的全部地址（Windows 上会把各网卡都列出来，
    包括热点那块虚拟网卡），再用一个不发包的 UDP connect 补一个默认出口地址。
    """
    ips: set[str] = set()
    try:
        for res in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.add(res[4][0])
    except OSError:
        pass

    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))          # 只查路由，不会真的发包
        ips.add(s.getsockname()[0])
        s.close()
    except OSError:
        pass

    return sorted(ips)


def firewall_rule_exists() -> bool | None:
    """规则在不在。非 Windows 或 netsh 不可用时返回 None（未知）。"""
    if os.name != "nt":
        return None
    try:
        r = subprocess.run(
            ["netsh", "advfirewall", "firewall", "show", "rule", f"name={FIREWALL_RULE}"],
            capture_output=True, timeout=10,
        )
        # netsh 的输出是本地化的，不能匹配文本，只看返回码
        return r.returncode == 0
    except (OSError, subprocess.SubprocessError):
        return None


def firewall_add_command(port: int) -> str:
    return (
        f'netsh advfirewall firewall add rule name="{FIREWALL_RULE}" '
        f"dir=in action=allow protocol=TCP localport={port}"
    )


def setup_firewall(port: int) -> bool:
    if os.name != "nt":
        print("  [跳过] 非 Windows，自行放行入站 TCP", port)
        return False
    try:
        r = subprocess.run(
            ["netsh", "advfirewall", "firewall", "add", "rule",
             f"name={FIREWALL_RULE}", "dir=in", "action=allow",
             "protocol=TCP", f"localport={port}"],
            capture_output=True, text=True, timeout=15,
        )
    except (OSError, subprocess.SubprocessError) as e:
        print(f"  [失败] {e}")
        return False

    if r.returncode == 0:
        print(f'  [成功] 已添加规则 "{FIREWALL_RULE}"（TCP {port} 入站放行）')
        return True

    print("  [失败] 添加规则失败，多半是没有管理员权限。")
    print("         用管理员身份的 PowerShell 执行：")
    print("           " + firewall_add_command(port))
    return False


# ── 热点 ────────────────────────────────────────────────────────────────

def ensure_hotspot() -> dict[str, str] | None:
    """开发机上顺手把移动热点打开，板子才有得连。

    这一步是纯开发期便利：量产时服务端在云上，板子走公网，压根没有热点这回事。
    所以失败一律只警告不中断 —— 服务本身照常起，你完全可以用路由器组网。
    """
    if not hotspot.IS_WINDOWS:
        print("[热点] 非 Windows，跳过自动开启（自行保证板子和本机同网段）")
        return None

    info = hotspot.query()
    if info.get("available") == "0" or "error" in info:
        reason = info.get("error", "unknown")
        if reason == "no_internet_profile":
            print("[热点] 本机没有可共享的网络连接，Windows 起不了热点。")
            print("       先让 PC 自己联网（这是 Windows 的限制，不是脚本的问题）。")
        else:
            print(f"[热点] 状态读取失败：{reason}")
            print("       手动开：start ms-settings:network-mobilehotspot")
        return info

    if hotspot.is_on(info):
        print(f"[热点] 已开启（SSID {info.get('ssid', '?')}）")
        return info

    print("[热点] 未开启，正在打开 ...", end="", flush=True)
    info = hotspot.start()
    status = info.get("start_status", "")

    if status in ("Success", "AlreadyOn"):
        # StartTetheringAsync 返回后 ICS 网关不一定立刻就位，等 192.168.137.1
        # 真的出现在网卡上再往下走，否则横幅里会显示成"热点未检测到"。
        for _ in range(20):
            if HOTSPOT_IP in local_ipv4():
                break
            time.sleep(0.5)
        print(f" 完成（SSID {info.get('ssid', '?')}）")
    else:
        detail = info.get("start_error") or info.get("error") or status or "unknown"
        print(f" 失败：{detail}")
        print("       手动开：start ms-settings:network-mobilehotspot")
    return info


# ── 启动横幅 ────────────────────────────────────────────────────────────

def print_banner(host: str, port: int, hs: dict[str, str] | None) -> None:
    ips = local_ipv4()
    hotspot_up = HOTSPOT_IP in ips
    base = f"http://{HOTSPOT_IP}:{port}"

    print()
    print("=" * 68)
    print("  DeepStoa 开发服务端")
    print("=" * 68)
    print(f"  监听        {host}:{port}")

    print("  本机地址    ", end="")
    if ips:
        for i, ip in enumerate(ips):
            pad = "" if i == 0 else " " * 14
            tag = "  <- 移动热点网关，板子连这个" if ip == HOTSPOT_IP else ""
            print(f"{pad}{ip}{tag}")
    else:
        print("(没探测到)")

    if hotspot_up:
        print("  热点        已开启")
        if hs:
            ssid = hs.get("ssid", "")
            pw = hs.get("passphrase", "")
            if ssid:
                print(f"              SSID     {ssid}")
            if pw:
                print(f"              密码     {pw}   <- 在设备 Settings 里填这两项")
            if hs.get("clients"):
                print(f"              已连设备 {hs['clients']}")
            if hotspot.band_ok(hs) is False:
                print(f"              [警告] 频段是 {hs.get('band')}，"
                      "ESP32-S3 只有 2.4GHz，连不上")
                print("                     去移动热点设置里把「网络频段」改成 2.4GHz")
    else:
        print("  热点        未检测到 192.168.137.1")
        print("              打开「设置 → 网络和 Internet → 移动热点」，或执行：")
        print("                start ms-settings:network-mobilehotspot")
        print("              热点开起来之前，板子连不到本机。")

    fw = firewall_rule_exists()
    if fw is True:
        print(f'  防火墙      规则 "{FIREWALL_RULE}" 已存在')
    elif fw is False:
        print(f'  防火墙      没有 "{FIREWALL_RULE}" 规则 —— 板子的请求很可能被静默丢弃')
        print("              用 python run.py --setup-firewall（管理员）添加，或手动执行：")
        print("                " + firewall_add_command(port))
    else:
        print(f"  防火墙      未知（非 Windows），确保入站 TCP {port} 放行")

    print("-" * 68)
    print(f"  固件里应该指向  {base}")
    for path, macro, tmpl in FIRMWARE_REFS:
        print(f"    {macro:<26} {tmpl.format(base=base)}")
        print(f"    {'':<26} {path}")
    print("-" * 68)
    print("  路由")
    for line in [
        "GET  /                                服务清单",
        "GET  /docs                            交互式接口文档",
        "GET  /api/health                      存活探测",
        "GET  /api/news?page=&page_size=       新闻列表（不含正文）",
        "GET  /api/news/{id}                   新闻详情",
        "GET  /api/ota/check?version=          查更新",
        "GET  /api/ota/download?version=       下载固件（Range 续传）",
        "GET  /api/ota/versions                固件列表",
        "POST /api/ota/report                  升级结果回报",
        "POST /api/ota/publish                 发布新固件",
        "POST /api/logs/upload                 设备日志上传",
        "GET  /api/logs/sessions               已收到的日志会话",
    ]:
        print("    " + line)
    print("=" * 68)
    print()
    # 重定向到文件 / 管道时 stdout 是块缓冲的，横幅会卡在缓冲区里，
    # 看上去像"服务没起来"。uvicorn 接管前先冲一次。
    sys.stdout.flush()


# ── main ────────────────────────────────────────────────────────────────

def main() -> int:
    # 横幅里有中文。控制台代码页表示不了某个字符时，默认的 strict 会直接抛
    # UnicodeEncodeError 把入口script 打挂 —— 换成 replace，宁可显示成问号。
    # 不改 encoding：控制台自己的编码才是对的，强设 utf-8 反而会在 cp936 下乱码。
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="replace")
        except (AttributeError, OSError, ValueError):
            pass

    parser = argparse.ArgumentParser(
        description="DeepStoa 开发服务端总入口（news + ota + logs）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--host", default="0.0.0.0",
                        help="监听地址，默认 0.0.0.0（板子要连，不能用 127.0.0.1）")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--reload", action="store_true", help="改代码自动重启")
    parser.add_argument("--setup-firewall", action="store_true",
                        help="添加入站放行规则后退出（需要管理员权限）")
    parser.add_argument("--no-hotspot", action="store_true",
                        help="不自动开移动热点（已经用路由器组网时加这个）")
    parser.add_argument("--log-level", default="info",
                        choices=["critical", "error", "warning", "info", "debug", "trace"])
    args = parser.parse_args()

    if args.setup_firewall:
        print(f"添加防火墙规则（TCP {args.port} 入站）...")
        return 0 if setup_firewall(args.port) else 1

    if args.host == "127.0.0.1" or args.host == "localhost":
        print("[警告] 绑定在回环地址上，板子连不进来。去掉 --host 用默认的 0.0.0.0。")

    try:
        import uvicorn
    except ImportError:
        print("缺依赖。先执行：pip install -r server/requirements.txt")
        return 1

    # gateway.py 用相对路径找 news/ota/logs_data，必须以 server/ 为工作目录；
    # 同时 "gateway:app" 这个导入串也要能在 sys.path 里找到。
    os.chdir(HERE)
    if str(HERE) not in sys.path:
        sys.path.insert(0, str(HERE))

    hs = None if args.no_hotspot else ensure_hotspot()
    print_banner(args.host, args.port, hs)

    uvicorn.run(
        "gateway:app",
        host=args.host,
        port=args.port,
        reload=args.reload,
        reload_dirs=[str(HERE)] if args.reload else None,
        log_level=args.log_level,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
