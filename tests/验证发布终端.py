"""验证发布 EXE 的真实终端输入；只创建本机临时 CMD/PowerShell。"""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def 等待(条件, 秒=8):
    截止 = time.monotonic() + 秒
    while time.monotonic() < 截止:
        if 条件():
            return
        time.sleep(0.05)
    raise AssertionError("等待条件超时")


def 检查(程序, 系统):
    系统根 = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    环境 = dict(os.environ)
    环境["PATH"] = os.pathsep.join(map(str, [程序.parent, 系统根 / "System32", 系统根]))
    if 系统 == "CMD":
        启动参数 = [str(系统根 / "System32/cmd.exe"), "/D", "/K"]
    else:
        启动参数 = [str(系统根 / "System32/WindowsPowerShell/v1.0/powershell.exe"), "-NoLogo", "-NoExit"]
    内核 = ctypes.WinDLL("kernel32", use_last_error=True)
    内核.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    内核.OpenProcess.restype = wintypes.HANDLE
    内核.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    内核.CloseHandle.argtypes = [wintypes.HANDLE]
    with tempfile.TemporaryDirectory(prefix="终端发布验证-") as 目录:
        结果 = Path(目录) / "中文结果.txt"
        进程 = subprocess.Popen(启动参数, env=环境, creationflags=subprocess.CREATE_NEW_CONSOLE)
        句柄 = 内核.OpenProcess(0x1000, False, 进程.pid)
        try:
            assert 句柄, "无法打开本次测试的进程句柄"
            时间 = [wintypes.FILETIME() for _ in range(4)]
            assert 内核.GetProcessTimes(句柄, *(ctypes.byref(值) for 值 in 时间))
            创建时间 = (时间[0].dwHighDateTime << 32) | 时间[0].dwLowDateTime

            def 投递(类型, 值):
                请求 = json.dumps({"pid": 进程.pid, "started": str(创建时间), "kind": 类型, "value": 值}, ensure_ascii=False).encode("utf-8")
                返回 = subprocess.run([str(程序), "--terminal-input"], input=请求, capture_output=True, env=环境, timeout=6)
                assert 返回.returncode == 0, (返回.returncode, 返回.stderr.decode("utf-8", "replace"))

            time.sleep(0.6)
            命令 = f'echo marker>"{结果}"' if 系统 == "CMD" else f"[IO.File]::WriteAllText('{结果}','marker')"
            投递("text", 命令)
            time.sleep(0.4)
            assert not 结果.exists(), "填入操作意外执行了命令"
            投递("key", "enter")
            等待(结果.exists)
            assert b"marker" in 结果.read_bytes()
            投递("text", "exit")
            投递("key", "enter")
            进程.wait(timeout=6)
            print(f"通过：{系统} 发布辅助进程、中文路径、只填入不执行、显式回车与退出")
        finally:
            if 句柄:
                内核.CloseHandle(句柄)
            if 进程.poll() is None:
                进程.terminate()  # 仅清理本函数自行创建的测试进程。
                进程.wait(timeout=5)


if __name__ == "__main__":
    if sys.platform != "win32":
        raise SystemExit("此验证仅适用于 Windows")
    发布程序 = (Path(__file__).resolve().parent.parent / "release/SerialStudio.exe").resolve()
    if len(sys.argv) > 1:
        发布程序 = Path(sys.argv[1]).resolve()
    assert 发布程序.is_file(), 发布程序
    for 模式 in ("CMD", "PowerShell"):
        检查(发布程序, 模式)
