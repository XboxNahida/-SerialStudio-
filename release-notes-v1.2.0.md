# Serial Studio v1.2.0 正式版

本版让未预装开发工具的 Windows 10/11 x64 电脑也能使用常用连接，并自动跟随系统代理。用户已完成本地候选验收。

## 工具链

- 安装包提供 Android Platform Tools 37.0.1（ADB/fastboot）、OpenSSH Client 10.4.0.0p1-Preview（ssh/sftp/scp）和 Python embeddable 3.13.7；保留上游许可。Python 不包含 pip，OpenSSH Preview 为上游版本名称。
- 设置页显示工具来源与路径，支持高级自选路径；自动查找顺序为自选、受管工具、系统 PATH，不修改系统 PATH。
- OpenOCD 0.12.0-7 按需下载；J-Link、STM32CubeProgrammer、dfu-util 提供官方入口和本地路径选择，不自动安装设备驱动。
- 下载后验证大小、SHA256 并安全解压；修复 WindowsApps 零字节 Python 启动别名导致 Android 工具下载完成却无法安装的问题。解压失败显示实际程序、退出码及错误。

## 网络与安装

- 更新、工具下载、HTTP、MQTT 和 TCP 客户端在新连接时查询 Windows 系统代理；未配置代理时使用默认网络，代理失败不自动改为直连。后台刷新状态避免阻塞界面。
- SSH/SFTP/SCP 和网络 ADB 遵循系统路由及各工具配置，不自动套用 Windows HTTP 代理；VPN/TUN 由系统路由处理。
- 升级完整替换旧安装清单内的应用文件，保留配置和用户文件。工具单独部署到每用户目录，同版本重装补齐缺失文件并保留已有修改。
- 卸载默认保留工具，可选择删除清单登记且摘要未改变的工具文件；未知文件、用户修改和外部工具保留。可选择桌面及开始菜单快捷方式。

## 下载与验证边界

下载 `SerialStudio-1.2.0-Windows-x64-Setup.exe` 和同名 `.sha256`。安装包未签名，请核对本仓库来源与摘要；支持从旧版本沿用原目录升级。

自动验证包括 25 套 CTest、Python/指令库检查，以及隔离安装、升级、快捷方式、工具部署与卸载回归。隔离升级使用旧版行为夹具，未覆盖用户当前真实安装。设备功能沿用此前验收；不将本机测试等同所有板端和代理环境均通过。完整 Windows PAC、SOCKS/TLS/认证代理及磁盘耗尽仍需按环境验收。
