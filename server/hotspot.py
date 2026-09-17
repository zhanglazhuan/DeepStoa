"""
Windows「移动热点」控制 —— 只服务于开发机，量产不用。

量产时服务端跑在云主机上，板子走公网接入，没有热点这回事。所以这个模块只被
run.py（开发入口）引用，gateway.py（真正的 ASGI 应用）完全不 import 它 ——
把 gateway.py 丢到云上跑不会碰到任何 Windows 相关代码。

── 为什么要走 PowerShell ──────────────────────────────────────────────
Windows 10/11 的「移动热点」是 WinRT 的 NetworkOperatorTetheringManager，
没有命令行接口（`netsh wlan start hostednetwork` 是被废弃的旧 Hosted Network
API，现在多数网卡驱动直接不支持）。Python 侧要调只能装 winsdk 之类的绑定，
为一个开发期便利加运行时依赖不划算，所以 shell 出去让 PowerShell 调。

**必须用 powershell.exe（Windows PowerShell 5.1），不能用 pwsh 7。**
PowerShell Core 6+ 移除了 WinRT 类型投影，`[...,ContentType=WindowsRuntime]`
在 7.x 里会直接报错。powershell.exe 在所有 Windows 10/11 上都在。

── 已知限制 ───────────────────────────────────────────────────────────
* 依赖一个「可共享的网络连接」。PC 自己断网时 GetInternetConnectionProfile()
  返回 null，热点根本创建不出来 —— 这是 Windows 本身的限制。
* 热点频段若被设成 5GHz，ESP32-S3（仅 2.4GHz）连不上，这里会读出来提示。
"""

from __future__ import annotations

import base64
import os
import subprocess
from pathlib import Path

IS_WINDOWS = os.name == "nt"


def _powershell() -> str | None:
    """定位 Windows PowerShell 5.1。刻意不用 PATH 上的 `powershell`，
    某些环境里它被 alias 到 pwsh 7，而 7 调不了 WinRT。"""
    if not IS_WINDOWS:
        return None
    root = os.environ.get("SystemRoot", r"C:\Windows")
    exe = Path(root) / "System32" / "WindowsPowerShell" / "v1.0" / "powershell.exe"
    return str(exe) if exe.exists() else None


# WinRT 的异步方法返回 IAsyncOperation，PowerShell 不会自动 await，
# 要借 System.Runtime.WindowsRuntime 的 AsTask 扩展方法转成 .NET Task。
_PREAMBLE = r"""
$ErrorActionPreference = 'Stop'
try {
    Add-Type -AssemblyName System.Runtime.WindowsRuntime | Out-Null
    $asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
        $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
        $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]

    function Await($op, $type) {
        $task = $asTaskGeneric.MakeGenericMethod($type).Invoke($null, @($op))
        $task.Wait(-1) | Out-Null
        $task.Result
    }

    $connProfile = [Windows.Networking.Connectivity.NetworkInformation,Windows.Networking.Connectivity,ContentType=WindowsRuntime]::GetInternetConnectionProfile()
    if ($null -eq $connProfile) {
        Write-Output 'error=no_internet_profile'
        exit 0
    }
    Write-Output "profile=$($connProfile.ProfileName)"

    $mgr = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager,Windows.Networking.NetworkOperators,ContentType=WindowsRuntime]::CreateFromConnectionProfile($connProfile)
"""

_EPILOGUE = r"""
    Write-Output "state=$($mgr.TetheringOperationalState)"
    Write-Output "clients=$($mgr.ClientCount)"
    $cfg = $mgr.GetCurrentAccessPointConfiguration()
    Write-Output "ssid=$($cfg.Ssid)"
    Write-Output "passphrase=$($cfg.Passphrase)"
    try { Write-Output "band=$($cfg.Band)" } catch { }
    Write-Output 'ok=1'
} catch {
    Write-Output "error=$($_.Exception.Message -replace '\r?\n', ' ')"
}
"""

_START_ACTION = r"""
    if ("$($mgr.TetheringOperationalState)" -ne 'On') {
        $res = Await ($mgr.StartTetheringAsync()) ([Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult])
        Write-Output "start_status=$($res.Status)"
        if ($res.AdditionalErrorMessage) {
            Write-Output "start_error=$($res.AdditionalErrorMessage)"
        }
    } else {
        Write-Output 'start_status=AlreadyOn'
    }
"""


def _run(action: str, timeout: int) -> dict[str, str]:
    exe = _powershell()
    if exe is None:
        return {"available": "0",
                "error": "not_windows" if not IS_WINDOWS else "powershell_5_not_found"}

    script = _PREAMBLE + action + _EPILOGUE

    # 用 -EncodedCommand 而不是 -Command：
    #   * `-Command -`（从 stdin 读）是按行逐条执行的，多行 try/catch 会被拆散
    #   * `-Command "<一大段>"` 要过 cmd + PowerShell 两层引号解析，引号会被啃掉
    # -EncodedCommand 把整段当一个单元传，两个坑都绕开。编码固定 UTF-16LE。
    encoded = base64.b64encode(script.encode("utf-16-le")).decode("ascii")

    try:
        proc = subprocess.run(
            [exe, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
             "-EncodedCommand", encoded],
            capture_output=True, text=True, timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return {"available": "1", "error": "timeout"}
    except OSError as e:
        return {"available": "0", "error": f"spawn_failed: {e}"}

    out: dict[str, str] = {"available": "1"}
    for line in proc.stdout.splitlines():
        line = line.strip()
        if "=" in line:
            k, _, v = line.partition("=")
            out[k.strip()] = v.strip()

    if "ok" not in out and "error" not in out:
        err = (proc.stderr or "").strip().replace("\n", " ")
        out["error"] = err[:200] or f"powershell exit {proc.returncode}"
    return out


def query(timeout: int = 20) -> dict[str, str]:
    """读当前热点状态，不改动任何东西。"""
    return _run("", timeout)


def start(timeout: int = 60) -> dict[str, str]:
    """打开移动热点。已经开着就什么都不做（start_status=AlreadyOn）。

    超时给到 60s：冷启动时 Windows 要拉起虚拟网卡并配好 ICS，实测可能要十几秒。
    """
    return _run(_START_ACTION, timeout)


def is_on(info: dict[str, str]) -> bool:
    return info.get("state", "").lower() == "on"


def band_ok(info: dict[str, str]) -> bool | None:
    """频段能不能被 ESP32-S3 用（它只有 2.4GHz）。读不到返回 None。"""
    band = info.get("band", "")
    if not band:
        return None
    return "Five" not in band       # TwoPointFourGigahertz / Auto 都行
