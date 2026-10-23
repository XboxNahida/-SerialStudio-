# 串口工作台 · Serial Studio

Windows 上的 Qt Quick 通信调试工具。当前版本 **1.0.0（正式版）**，窗口默认 1200×800，最小 1120×760，支持深浅主题和简约/详细显示。

[下载安装包](https://github.com/XboxNahida/-SerialStudio-/releases) · [使用说明](使用说明.md) · [安装与卸载](安装与卸载说明.md) · [更新机制](版本更新说明.md)

## 功能

- 串口 UTF-8 / HEX 收发，常用及自定义波特率、定时发送、DTR/RTS、日志导出。高波特率是否可用取决于转接芯片、驱动和开发板。
- TCP 客户端/服务端、UDP、HTTP(S)、MQTT(S)；文本/JSON、Modbus RTU、自定义帧头尾解析。
- 自定义二进制字段、JSON/CSV 数据解释，文字和图表输出；完整图片、音频、视频文件预览。
- 开发板 I²C、SPI、USB，可选择串口、网络桥接或 Linux SSH 终端。提供 [板端 Python 参考](board/board_agent.py) 和 [桥接说明](桥接协议说明.md)。
- Qt 内置完整交互终端或电脑终端可选，支持 Linux SSH、本机 PowerShell/CMD、Android ADB Shell；独立非置顶指令助手，401条分类模板和用户自定义指令，默认只填入不执行。
- 在端口与目标选择处自动探索串口、ADB、USB/盘符、网卡，以及广播和历史设备；主动网段扫描由用户触发。
- 本地/共享目录、Android ADB、USB MTP、SFTP 统一文件管理；单文件复制/剪切/粘贴、回读 SHA256 校验、文件及目录永久删除确认。
- 消息多选复制、各主要发送入口的快捷列表、低频参数收纳到高级设置，配置在本地自动保存和加载。
- 后台更新检查、手动下载并校验 SHA256、退出并安装、打开卸载向导。安装向导可选择快捷方式。

## 安装

从本仓库 Release 下载 `SerialStudio-1.0.0-Windows-x64-Setup.exe` 和同名 `.sha256`。支持 Windows 10/11 x64，包含运行依赖。安装包未签名，核对来源与 SHA256 后按中文向导安装。升级选择原安装目录，旧安装清单中的程序文件会先移除，再写入完整新版。

正式版变更、校验值及已知限制见 [v1.0.0 发布说明](release-notes-v1.0.0.md)。

## 构建与验证

需要 Windows、Qt 6.11.1 MinGW（Quick、Quick Controls、Network、Multimedia、Test、QuickTest）、MinGW 13.1、CMake、Ninja、PowerShell。默认路径为 `C:\Qt\6.11.1\mingw_64` 和 `C:\Qt\Tools`，可通过脚本参数指定。全局 Qt 缺少 SerialPort 时脚本在工程内构建固定版本并校验下载哈希，不修改全局 Qt。

```powershell
& '.\构建.ps1'
& '.\制作安装包.ps1' -SkipBuild
```

前者在工程内获取并校验 SerialPort、libvterm 依赖，运行 CTest 并部署 `release`；后者校验 NSIS 3.11 工具与产物后生成安装包。源码不包含 `deps`、`build`、`release` 和安装包。脱机使用须提前准备工具及下载依赖。

```powershell
python -m unittest discover -s tests -p "test_*.py"
& '.\tests\验证安装包.ps1'
```

安装测试拒绝覆盖用户已有安装记录；仅在工程内测试目录安装和卸载。`-TestPackage` 可验证隔离快捷方式。更新测试使用模拟网络，发布后的真实下载检查使用 `build\update_tests.exe --live-download`，只下载与校验，不执行安装。

## 验收范围与依赖

正式版构建及22套 CTest 全部通过，另有板端 Python 和指令库检查，以及安装、重装、升级、快捷方式和卸载验证。ADB/MTP 目录已在实际开发板只读验证；真实 UART 高波特率长时通信、SSH/SFTP 认证、Android Shell、板载外设及设备写入仍需按设备验收。

文件复制/剪切仅支持单个普通文件；目录支持永久删除，操作需确认且不进入回收站。USB 文件访问依赖设备开放 ADB 或 MTP 并授予权限，网络文件访问依赖 SFTP 或 Windows 共享。SFTP 粘贴需服务端硬链接扩展支持，不支持会报错并保留源文件；符号链接不生成可操作路径。详见 [文件管理说明](数据解析与文件传输说明.md)。

Qt、FFmpeg、MinGW、NSIS 等依赖说明见 [第三方组件说明](第三方组件说明.md)。界面视觉参考 EvolveUI，源码独立实现。本仓库与其他同名 Serial Studio 项目没有隶属关系。
