# 串口工作台 · Serial Studio

Windows 上的 Qt Quick 通信调试工具。当前版本 **0.4.2-beta.1（测试版）**，窗口默认 1200×800，最小 1120×760，支持深浅主题。

[下载安装包](https://github.com/XboxNahida/-SerialStudio-/releases) · [使用说明](使用说明.md) · [安装与卸载](安装与卸载说明.md) · [更新机制](版本更新说明.md)

## 功能

- 串口 UTF-8 / HEX 收发，常用及自定义波特率、定时发送、DTR/RTS、日志导出。高波特率是否可用取决于转接芯片、驱动和开发板。
- TCP 客户端/服务端、UDP；文本/JSON、Modbus RTU、自定义帧头尾解析。
- 自定义二进制字段、JSON/CSV 数据解释，文字和图表输出；完整图片、音频、视频文件预览。
- 开发板 I²C、SPI、USB，可选择串口、网络桥接或 Linux SSH 终端。提供 [板端 Python 参考](board/board_agent.py) 和 [桥接说明](桥接协议说明.md)。
- Linux SSH、本机 PowerShell/CMD、Android ADB Shell；独立指令辅助窗口，默认只填入不执行。ADB 自动扫描设备并支持手动选择。
- USB 可见磁盘文件复制、SFTP 上传/下载。
- 后台更新检查、手动下载并校验 SHA256、退出并安装、打开卸载向导。安装向导可选择快捷方式。

## 安装

从本仓库 Release 下载 `SerialStudio-0.4.2-beta.1-Windows-x64-Setup.exe` 和 `.sha256`。支持 Windows 10/11 x64，包含运行依赖。安装包未签名，核对来源与 SHA256 后按中文向导安装。升级选择原安装目录，旧安装清单中的程序文件会先移除，再写入完整新版。

## 构建与验证

需要 Windows、Qt 6.11.1 MinGW（Quick、Quick Controls、Network、Multimedia、Test、QuickTest）、MinGW 13.1、CMake、Ninja、PowerShell。默认路径为 `C:\Qt\6.11.1\mingw_64` 和 `C:\Qt\Tools`，可通过脚本参数指定。全局 Qt 缺少 SerialPort 时脚本在工程内构建固定版本并校验下载哈希，不修改全局 Qt。

```powershell
& '.\构建.ps1'
& '.\制作安装包.ps1' -SkipBuild
```

前者运行 CTest 并部署 `release`，后者校验 NSIS 3.11 工具与产物后生成安装包。源码不包含 `deps`、`build`、`release` 和安装包。脱机使用须提前准备工具及下载依赖。

```powershell
python -m unittest discover -s tests -p "test_*.py"
& '.\tests\验证安装包.ps1'
```

安装测试拒绝覆盖用户已有安装记录；仅在工程内测试目录安装和卸载。`-TestPackage` 可验证隔离快捷方式。更新测试使用模拟网络，发布后的真实下载检查使用 `build\update_tests.exe --live-download`，只下载与校验，不执行安装。

## 验收范围与依赖

自动化检查涵盖核心、网络、QML、媒体和安装流程；真实 UART 高波特率长时通信、SSH/SFTP 认证、Android Shell 及板载外设仍需按设备验收。USB/ADB 枚举已在开发环境验证，不能保证所有厂商驱动均适配。

Qt、FFmpeg、MinGW、NSIS 等依赖说明见 [第三方组件说明](第三方组件说明.md)。界面视觉参考 EvolveUI，源码独立实现。本仓库与其他同名 Serial Studio 项目没有隶属关系。
