// UTF-8 指令字典维护工具。只写项目数据，不运行模板；保留原有六字段及稳定 ID。
const fs=require('fs'),path=require('path');
const file=path.join(__dirname,'../data/terminal_commands.json');
const entries=JSON.parse(fs.readFileSync(file,'utf8'));
const refs={};
function ref(tool,options,positions,advanced=[]) {
    refs[tool.toLowerCase().replace(/\.exe$/,'')]={options:options.split('|').filter(Boolean).map(x=>{const i=x.indexOf('=');return {value:x.slice(0,i),description:x.slice(i+1)}}),positions:positions.split('|'),advanced};
}
function adv(title,command,description){return {title,command,description}}
ref('ls','-l=详细列出权限、所有者、大小和时间|-a=包含以点开头的隐藏项|-h=配合 -l 显示易读大小|-R=递归列出子目录','目录路径；省略时查看当前目录',[adv('按修改时间倒序','ls -lht /tmp','最近修改的项目排在前面。')]);
ref('pwd','-P=显示解析符号链接后的物理目录|-L=显示逻辑目录','');
ref('cp','-r=递归复制目录|-i=覆盖前询问|-p=保留模式和时间|-a=归档复制，尽量保留元数据','源文件或目录|目标文件或目录',[adv('复制含空格的目录',"cp -ri '/tmp/my app' '/tmp/app backup'",'目录用引号括住；覆盖前逐项确认。')]);
ref('mv','-i=覆盖前询问|-n=不覆盖已存在的目标|-v=显示移动结果','源文件或目录|目标路径；同目录可用于重命名');
ref('rm','-i=每次删除前询问|-r=递归删除目录|-f=忽略不存在项且不询问，慎用','要删除的路径；先用 ls 检查再执行');
ref('mkdir','-p=父目录不存在时一并创建|-m=指定权限模式，如 755','要创建的目录路径');
ref('cd','','要进入的目录；.. 为上级，~ 为用户主目录');
ref('cat','-n=显示行号','要查看的文本文件；不要直接输出二进制文件');
ref('grep','-n=显示匹配行号|-i=忽略大小写|-r=递归搜索目录|-E=扩展正则表达式|-v=只输出不匹配的行|-C=同时显示前后指定行数','搜索词或正则表达式；含空格请加引号|目标文件或目录',[adv('从日志筛选错误',"dmesg | grep -Ein 'error|fail|timeout'",'管道将内核日志交给 grep；-E 启用多个条件，-i 忽略大小写。')]);
ref('find','-name=按文件名模式匹配，模式用引号保护|-type=类型 f 文件 / d 目录|-mtime=修改时间，+7 表示超过 7 天|-size=大小，+10M 表示超过 10 MiB','起始目录|查找条件和对应值',[adv('定位最近一天日志',"find /var/log -type f -mtime -1 -name '*.log'",'所有条件同时满足；只查找，不删除。')]);
ref('head','-n=显示前 N 行|-c=显示前 N 字节','输入文件');
ref('tail','-n=最后 N 行|-f=持续跟踪新增内容|-F=跟踪文件名，适合日志轮转','输入文件',[adv('持续查看应用日志','tail -n 100 -F /tmp/app.log','先显示末尾 100 行，再持续跟踪；Ctrl+C 停止。')]);
ref('sed','-n=不默认输出全部行|-E=使用扩展正则|-i=原地修改文件，先备份','编辑表达式，如 s/旧值/新值/g|输入文件',[adv('预览替换效果',"sed 's/old/new/g' /tmp/config.txt",'只打印替换结果；没有 -i，不改原文件。')]);
ref('awk','-F=输入字段分隔符|-v=传入变量 name=value','单引号内的匹配与动作程序|输入文件',[adv('筛选并取字段',"awk -F, '$2 > 10 {print $1, $2}' /tmp/data.csv",'逗号分列；$2 是第二列，{} 中定义输出动作。不处理带嵌套引号的完整 CSV。')]);
ref('chmod','-R=递归修改权限，谨慎使用','模式：644 文件 / 755 可执行 / u+x 当前用户增加执行权|文件或目录');
ref('chown','-R=递归修改所有者','用户:组；需要相应权限|文件或目录');
ref('ping','-c=Linux/Android 发送次数|-W=Linux 等待响应秒数','目标 IP 或域名');
ref('ip','-br=简洁摘要|-s=附加统计','对象：addr 地址 / link 网卡 / route 路由|动作：show 查看 / add 新增 / del 删除 / set 修改',[adv('查看指定网卡地址','ip -br addr show wlan0','网卡名称须先用 ip link 确认。'),adv('设置临时网卡地址','ip addr add 192.168.8.2/24 dev eth0','root 操作；先核对网段和网卡；重启或网络服务可能恢复配置。')]);
ref('ss','-l=仅监听中的套接字|-t=TCP|-u=UDP|-n=数字显示地址端口|-p=进程信息，需要权限','');
ref('curl','-I=只获取 HTTP 响应头|-L=跟随重定向|-o=保存到指定文件|-O=使用远端文件名|-C=断点续传，值 - 自动续传|-X=指定 HTTP 方法|-H=自定义请求头|-d=请求正文|-f=HTTP 错误时返回失败码|--connect-timeout=连接超时秒数','请求网址；包含 & 时加引号',[adv('POST JSON',"curl -X POST -H 'Content-Type: application/json' -d '{\"enabled\":true}' http://192.168.3.2:8080/api",'替换真实 API；-d 发送正文，不把账号令牌写入共享模板。'),adv('断点续传程序','curl -fL -C - -o app.bin https://example.com/app.bin','example.com 是示意地址，必须换成可信下载地址；下载后校验摘要。')]);
ref('wget','-O=保存文件名|-c=继续未完成下载|-T=网络超时秒数','可信下载 URL');
ref('ssh','-p=服务端口，默认 22|-i=私钥文件|-L=本地端口:目标主机:目标端口|-v=详细诊断|-N=只转发不执行远端命令','用户名@主机|可选远端命令',[adv('本地端口转发','ssh -N -L 8080:127.0.0.1:80 user@192.168.3.2','把本机 8080 转发至远端的 80；需服务端允许，Ctrl+C 停止。')]);
ref('scp','-P=SSH 端口；大写 P|-i=私钥文件|-r=递归目录|-p=尽量保留时间模式','源路径；远端写 user@host:/path|目标路径',[adv('上传程序到开发板','scp ./app user@192.168.3.2:/tmp/app','先核对主机指纹及目标路径；上传后在板端 chmod u+x /tmp/app。')]);
ref('rsync','-a=归档模式|-v=详细输出|-n=只预览，不写入|--progress=显示进度|--delete=删除目标多余文件；先用 -n 预览|-e=指定远程传输工具，如 ssh','源目录；末尾 / 表示仅内容|目标目录或 user@host:/path');
ref('tar','-c=创建归档|-x=解包|-t=查看归档目录|-z=gzip 压缩|-f=紧随归档路径|-C=切换解包/打包工作目录','归档及文件路径',[adv('解包前列目录','tar -tzf /tmp/app.tar.gz','先检查路径和内容，再选择独立目录解包。')]);
ref('systemctl','--user=操作当前用户服务|--no-pager=直接输出，不打开分页器','动作 status/start/stop/restart/enable/disable|服务名，如 sshd.service',[adv('服务开机启动并立即启动','systemctl enable --now sshd.service','需 systemd 与管理员权限；部分发行版服务名为 ssh.service。')]);
ref('journalctl','-u=指定服务|-b=本次启动|-f=持续跟踪|-n=末尾 N 行|--since=起始时间|--no-pager=直接输出','查询条件',[adv('按服务与时间查看',"journalctl -u app.service --since '1 hour ago' --no-pager",'systemd 系统；服务名换成实际值，需要日志读取权限。')]);
ref('dmesg','-T=人类可读时间；部分实现不支持|-w=持续监视|-l=按级别筛选','');
ref('ps','-ef=常见 Linux 完整进程列表|-A=全部进程','');
ref('kill','-TERM=请求进程正常退出|-KILL=强制结束，无法清理资源|-0=只检查进程存在及权限','进程 PID；先通过 ps 确认');
ref('top','-p=Linux 按 PID 观察|-H=显示线程','');
ref('gdb','--args=后续参数传给待调试程序|-p=附加已有进程 PID|-ex=启动后执行一条 GDB 命令|-batch=批处理执行后退出','带调试符号的可执行文件|可选 core 文件',[adv('启动并打印调用栈',"gdb -ex run -ex bt --args ./app --config /tmp/app.conf",'安装 gdb；程序崩溃后 bt 输出栈。在 GDB 内可输入 break main、next、step、print 变量、continue、quit。')]);
ref('strace','-f=跟踪派生子进程|-p=附加 PID|-o=保存跟踪日志|-e=过滤调用，如 trace=network|-tt=显示详细时间','程序及参数或附加 PID',[adv('追踪网络系统调用','strace -f -e trace=network -o /tmp/network.trace ./app','需 strace、ptrace 权限；会运行目标程序并产生额外开销。')]);
ref('perf','-p=指定进程 PID|-a=全系统，需要额外权限','子命令 stat/record/report|目标程序或分析参数');
ref('valgrind','--leak-check=内存泄漏检查 full/summary/no|--track-origins=yes 可追踪未初始化值来源','待运行的可执行文件及参数');
ref('readelf','-h=ELF 文件头|-l=程序段|-S=节表|-d=动态依赖|-s=符号表','ELF 可执行文件或库路径');
ref('objdump','-d=反汇编|-S=结合源代码；需要调试信息|-f=文件架构头','ELF 路径；交叉工具链需使用匹配架构版本');
ref('ldd','','可信可执行文件或库；不要对不可信程序使用');
ref('cmake','-S=源码目录|-B=独立构建目录|-D=配置变量，格式 NAME=VALUE|-G=构建器，如 Ninja|--build=构建指定目录|--parallel=并行任务数|--install=安装指定构建目录','动作或项目路径',[adv('配置并编译 Release','cmake -S . -B build -DCMAKE_BUILD_TYPE=Release','先配置，再执行 cmake --build build --parallel 4；Qt 项目需指定正确 Qt 路径。')]);
ref('gcc','-g=生成调试信息|-O0=关闭优化便于调试|-Wall=常用警告|-o=输出文件名|-I=头文件目录|-L=库目录|-l=链接指定库','C 源文件路径');
ref('make','-j=并行任务数|-C=先切换目录|-n=仅打印将执行的命令','构建目标；省略为默认目标');
ref('ninja','-C=构建目录|-j=并行任务数|-n=预览构建命令','构建目标');
ref('git','-C=在指定仓库目录操作','子命令 status/log/diff/clone/pull|对应子命令参数',[adv('只查看未提交变化','git diff --stat','不会提交或上传；继续用 git diff 查看详细差异。')]);
ref('python3','-m=以模块运行，如 pip / http.server|-u=不缓冲 stdout/stderr','脚本路径及脚本参数');
ref('pip','--user=仅为当前用户安装|--version=查询版本','子命令 install/list/show|包名；推荐使用虚拟环境');
ref('apt','--simulate=预览安装计划，不修改系统','动作 update/install/show|包名；仅 Debian/Ubuntu，安装需 root');
ref('nmcli','','对象 device/connection/radio|动作及参数；NetworkManager 系统',[adv('查看 Wi-Fi 接入点','nmcli device wifi list','先扫描可用接入点；连接时用 nmcli --ask device wifi connect SSID，避免把密码写入历史。')]);
ref('i2cdetect','-l=只列总线|-y=跳过确认，扫描前核对器件','I²C 总线编号；探测可能影响敏感器件');
ref('i2cget','-y=跳过交互确认','总线编号|7 位设备地址，如 0x50|寄存器地址|b 字节 / w 字');
ref('i2cset','-y=跳过交互确认','总线编号|设备地址|寄存器地址|写入值；会修改硬件状态');
ref('spidev_test','-D=设备节点|-s=时钟频率 Hz|-b=每字位数|-p=发送模式字符串|-v=显示详细收发','');
ref('adb','-s=指定设备序列号，多个设备时必填|-d=只选 USB 设备|-e=只选网络/模拟器设备','子命令 devices/connect/pair/tcpip/usb/shell/push/pull/install/logcat|对应的端点、路径或包名',[adv('先指定 USB 设备开启网络 ADB','adb -s SERIAL tcpip 5555','电脑执行；SERIAL 换成 adb devices -l 中的 USB 序列号；设备与电脑在可信网络，随后 adb connect IP:5555。重启后可能失效。'),adv('有空格路径的文件传输','adb -s SERIAL push "C:/my app/app.apk" /data/local/tmp/app.apk','在电脑上运行；设备目录必须可写。push 是复制文件，安装另用 adb install。')]);
ref('fastboot','-s=指定设备序列号|-w=清除用户数据，危险，不默认使用','子命令 devices/getvar/flash/reboot|分区名及匹配镜像路径',[adv('写入 boot 镜像','fastboot flash boot boot.img','仅电脑端 fastboot 模式；需要厂商允许和解锁，镜像/分区必须严格匹配。可能导致无法启动，请先备份并确认恢复途径。')]);
ref('pm','-3=第三方应用|-s=系统应用|-r=安装时替换现有包','动作 list/path/install/uninstall/clear/enable/disable-user|包名或 APK 路径',[adv('安装设备内 APK','pm install -r /data/local/tmp/app.apk','APK 已在设备中；需 shell 安装权限及兼容签名；不会绕过安装限制。')]);
ref('am','-n=显式组件 包名/Activity|-a=Intent Action|-d=Intent 数据 URI|--es=字符串额外参数：键 值|--ez=布尔额外参数：键 true/false|--ei=整数额外参数：键 值','动作 start/broadcast/force-stop|组件名、包名或 Intent 参数',[adv('向应用广播参数','am broadcast -a com.example.app.TEST --es message hello --ei count 1','应用必须注册并允许 shell 发送的接收器；Action、键和值由应用定义，不能任意控制其它应用。')]);
ref('input','','事件类型 keyevent/tap/swipe/text|键码、坐标或文本；坐标单位是屏幕像素',[adv('模拟滑动','input swipe 500 1400 500 400 300','从起点滑向终点，最后是持续毫秒；按设备分辨率调整。'),adv('输入文字','input text hello%sworld','%s 通常表示空格；适合 ASCII，中文和特殊字符依系统输入实现而异。')]);
ref('settings','--user=指定 Android 用户，如 0','动作 list/get/put/delete|命名空间 system/secure/global|键|值；修改需相应权限',[adv('修改亮度','settings put system screen_brightness 128','先查询原值并记录；数值范围依设备实现，自动亮度可能覆盖手动设置。'),adv('恢复原设置','settings put system screen_brightness 100','100 仅示意，必须换成修改前保存的值；不要用删除键代替恢复。')]);
ref('svc','','子命令 wifi/data/power/usb|enable/disable 或对应操作；可能断开连接');
ref('getprop','','属性名；留空列全部，不保证属性可写');
ref('setprop','','属性名|值；修改受属性服务和 SELinux 控制');
ref('logcat','-d=输出后退出|-c=清空日志缓冲区|-b=缓冲区 main/system/crash 等|-v=格式 threadtime/time/brief|-s=按 TAG:级别 筛选|--pid=按进程 PID 筛选','TAG:级别；V/D/I/W/E/F/S',[adv('只看应用错误','logcat -v threadtime -s AndroidRuntime:E ActivityManager:W','TAG 和级别之间用冒号；持续输出，Ctrl+C 停止。')]);
ref('dumpsys','-l=列出可查询服务','服务名 activity/window/package/battery/meminfo|服务相关选项',[adv('查看应用内存','dumpsys meminfo com.example.app','包名替换为已安装包，PSS 为按比例分摊的实际内存指标。')]);
ref('cmd','','Android 服务命令入口，如 package/power/wifi|先用 cmd -l 查服务、cmd 服务 help 查可用子命令');
ref('getevent','-l=显示符号化事件名|-p=列输入设备能力|-t=附加时间','/dev/input/eventN；读设备可能需要 root');
ref('wm','','对象 size/density|查看或设置值；reset 恢复默认',[adv('临时设置逻辑分辨率','wm size 1280x720','会改变布局，可能影响操作；记录原值，用 wm size reset 恢复。')]);
ref('screencap','-p=输出 PNG 格式','设备端输出路径；电脑 pull 取回');
ref('screenrecord','--time-limit=录制秒数，存在版本上限|--size=宽x高|--bit-rate=码率 bit/s','设备端输出 MP4 路径；Ctrl+C 结束');
ref('monkey','-p=限定包名|-c=限定启动类别|--throttle=事件之间间隔毫秒','事件次数；随机事件可能改变应用状态');
ref('Start-Process','-FilePath=可执行文件或关联文件路径|-ArgumentList=启动参数数组|-Wait=等待退出|-Verb=操作如 RunAs 会触发 UAC','应用路径或系统 URI',[adv('带参数启动程序',"Start-Process -FilePath 'C:/Tools/app.exe' -ArgumentList '--help'",'PowerShell 执行；替换程序路径，参数中空格需要正确引用。')]);
ref('Get-Help','-Examples=只看示例|-Detailed=包含参数说明|-Full=完整说明','PowerShell 指令名');
ref('Get-Command','-Name=命令名，支持通配符|-Syntax=查看参数语法','');
ref('Get-Process','-Name=进程名|-Id=进程 PID','');
ref('Stop-Process','-Id=进程 PID|-Name=进程名|-Confirm=结束前询问','');
ref('Get-Service','-Name=服务名或通配符','');
ref('Start-Service','-Name=服务名|-WhatIf=预览操作','');
ref('Stop-Service','-Name=服务名|-WhatIf=预览操作','');
ref('Set-Service','-Name=服务名|-StartupType=Automatic/Manual/Disabled|-WhatIf=预览操作','');
ref('Get-ChildItem','-Path=目录路径|-Force=含隐藏项|-Recurse=递归|-Filter=文件名过滤','');
ref('Copy-Item','-Path=源路径|-Destination=目标路径|-Recurse=递归目录|-WhatIf=只预览','');
ref('Move-Item','-Path=源路径|-Destination=目标路径|-WhatIf=只预览','');
ref('Remove-Item','-Path=删除路径|-Recurse=含子目录|-WhatIf=只预览|-Confirm=逐项确认','');
ref('Get-Content','-Path=文件路径|-Tail=末尾 N 行|-Wait=跟踪更新|-Encoding=UTF8 指定 UTF-8 编码','');
ref('Set-Content','-Path=目标路径|-Value=文本内容|-Encoding=utf8 指定编码；会覆盖文件','');
ref('Invoke-WebRequest','-Uri=请求 URL|-OutFile=保存文件|-Method=Get/Post/Put 等|-Headers=请求头哈希表|-Body=请求内容','');
ref('Invoke-RestMethod','-Uri=接口 URL|-Method=Get/Post/Put 等|-ContentType=正文类型|-Body=请求内容','');
ref('Test-NetConnection','-ComputerName=目标主机|-Port=TCP 端口|-InformationLevel=Detailed 显示诊断','');
ref('Get-FileHash','-Path=文件路径|-Algorithm=SHA256/SHA512 等','');
ref('winget','--id=精确应用标识|-e=精确匹配|--source=指定软件源','子命令 search/show/install/upgrade/list|软件标识；安装先核对发布者');
ref('start','/wait=等待应用退出|/d=启动目录','窗口标题；启动带引号路径时先写空标题 ""|应用路径或 URI');
ref('net','/user=指定用户；不要把密码写入模板','子命令 use/start/stop|共享路径或服务名');
ref('sc.exe','','动作 query/start/stop/qc/config|真实服务名；修改需要管理员');
ref('netsh','','上下文 interface/wlan/advfirewall|子命令及参数；修改网络可能断开当前会话');
ref('schtasks','/query=查询任务|/run=运行现有任务|/tn=任务名称|/fo=输出格式 LIST/TABLE/CSV','');
ref('tasklist','/fi=筛选条件，如 PID eq 1234|/svc=列进程相关服务','');
ref('taskkill','/pid=进程 PID|/im=映像名|/t=含子进程|/f=强制终止，有丢失数据风险','');
ref('df','-h=以易读单位显示容量|-T=显示文件系统类型','可选挂载路径');
ref('du','-h=易读大小|-s=仅汇总|-a=包含文件','待统计目录');
ref('free','-h=易读单位|-m=以 MiB 显示','');
ref('uname','-a=完整系统信息|-r=内核版本|-m=处理器架构','');
ref('lsusb','-t=USB 拓扑树|-v=详细描述符，可能需管理员|-d=按 vendor:product 过滤','');
ref('ln','-s=创建符号链接|-i=替换前询问','现有目标路径|新建的链接路径');
ref('file','-L=跟随符号链接|-i=显示 MIME 类型','需要识别的文件');
ref('stat','-c=输出格式，如 %s 为大小','文件或目录');
ref('diff','-u=统一格式差异|-r=递归目录|-q=仅报告是否不同','旧文件|新文件');
ref('sha256sum','-c=按校验清单检查文件','文件路径；与发布者提供的摘要逐字比较');
ref('unzip','-l=只列归档目录|-d=指定解包目录|-n=不覆盖现有文件','ZIP 文件路径');
ref('wc','-l=行数|-w=单词数|-c=字节数|-m=字符数','文本文件');
ref('sort','-n=按数值排序|-r=倒序|-u=去重|-k=排序列','输入文件或来自管道的文本');
ref('nano','-l=显示行号|-B=保存时创建备份','文本文件路径',[adv('保存与退出 nano','nano -B /tmp/app.conf','Ctrl+O 写入，Enter 确认文件名，Ctrl+X 退出；先核对路径和权限。')]);
ref('vi','-R=以只读模式打开','文件路径',[adv('查找并替换预览','vi -R /tmp/app.conf','按 / 输入搜索词后 Enter；n 下一个；Esc 回普通模式；:q 退出。编辑用 i，保存 :w，退出 :q，放弃 :q!。')]);
refs.vim=refs.vi;
ref('service','','服务名|动作 status/start/stop/restart；服务框架由发行版决定');
ref('sftp','-P=SSH 端口|-i=私钥路径','用户@主机',[adv('SFTP 交互操作','sftp user@192.168.3.2','连接后 ls/lls 查看远端/本地；get 下载；put 上传；cd/lcd 切目录；bye 退出。')]);
ref('sudo','-l=查看被授权的操作|-u=以指定用户运行','需要提权的指令；仅对已理解的操作提权');
ref('mosquitto_pub','-h=Broker 主机|-p=端口，常见1883/8883|-t=主题|-m=消息正文|-q=QoS 0/1/2|-r=保留消息，可能影响后来订阅者|--cafile=TLS CA 文件','');
ref('mosquitto_sub','-h=Broker 主机|-p=端口|-t=主题过滤器|-q=QoS 0/1/2|-v=同时打印主题|--cafile=TLS CA 文件','');
ref('v4l2-ctl','-d=摄像头设备节点|--list-devices=列设备|--list-formats-ext=列支持格式|--all=查看全部状态','');
ref('aplay','-l=列硬件声卡|-L=列 PCM 设备|-D=指定 PCM，如 plughw:0,0','兼容格式的音频文件路径');
ref('New-Item','-Path=新建路径|-ItemType=File 文件或 Directory 目录|-Value=初始内容','');
ref('Set-Location','-Path=切换到的目录|-LiteralPath=按字面路径，不展开通配符','');
ref('Select-String','-Path=搜索文件|-Pattern=搜索词或正则|-SimpleMatch=按普通字符串|-Context=附带前后行数','');
ref('Get-WinEvent','-LogName=日志名称|-MaxEvents=最大条数|-FilterHashtable=按日志和时间条件筛选','');
ref('Get-CimInstance','-ClassName=CIM 类名，如 Win32_OperatingSystem|-Filter=WQL 条件','');
ref('Get-NetTCPConnection','-State=状态 Listen/Established|-LocalPort=本地端口|-RemotePort=远端端口','');
ref('Resolve-DnsName','-Name=域名|-Type=记录类型 A/AAAA/MX 等','');
ref('Test-Connection','-ComputerName=目标主机|-Count=请求次数','');
ref('dir','/a=包含隐藏项及系统项|/b=仅显示名称|/s=递归|/o=排序条件 n 名称 / -d 时间倒序','目录或文件名模式');
ref('copy','/y=覆盖时不询问，慎用|/-y=覆盖前询问','源文件|目标文件；目录用 xcopy/robocopy');
ref('move','/y=覆盖前不询问|/-y=覆盖前询问','源文件|目标路径');
ref('findstr','/i=忽略大小写|/n=输出行号|/s=递归子目录|/c:=完整搜索字符串','搜索词|目标文本文件');
ref('ipconfig','/all=显示完整网卡信息|/flushdns=清理 DNS 缓存|/release=释放 DHCP 地址，会断网|/renew=更新 DHCP 租约','');
ref('netstat','-ano=连接和监听端口及 PID|-r=路由表|-s=协议统计','');
ref('route','print=查看路由|-p=修改时持久保存，需要管理员','动作 print/add/delete|目标网段和网关');
ref('certutil','-hashfile=计算文件摘要','文件路径|算法，如 SHA256');
ref('wevtutil','qe=查询日志|/c:=最大事件数|/f:=输出格式 text/xml','日志名或查询参数');
ref('openocd','--version=查询版本|-f=加载接口或目标配置|-c=执行一段 OpenOCD 配置命令','接口/目标脚本必须与调试器及芯片一致',[adv('通过调试器烧录并验证','openocd -f interface/cmsis-dap.cfg -f target/stm32f4x.cfg -c "program app.elf verify reset exit"','仅适合匹配的 CMSIS-DAP 与 STM32F4 示例；烧录/复位会改变设备，必须核对目标脚本、镜像、Flash 布局并备份，不能直接套到其它芯片。')]);
ref('pyocd','--help=查看当前版本帮助|-t=目标类型|-u=探针唯一标识','子命令 list/flash/gdbserver|镜像或服务选项',[adv('查看烧录命令参数','pyocd flash --help','先用 pyocd list 查探针，用 pyocd list --targets 查支持目标；实际 flash 写入会修改设备，不自动选择目标与镜像。')]);
ref('esptool','--port=串口设备路径|--baud=下载波特率|--chip=ESP 芯片型号','子命令 version/read-mac/flash-id/write-flash；名称依安装版本',[adv('查看当前烧录语法','esptool write-flash --help','esptool 5 使用连字符子命令，旧版可能用 write_flash。实际烧录必须核对芯片、下载模式、镜像偏移与板卡方案，先备份。')]);
ref('dfu-util','-l=仅列 DFU 设备|-a=备用接口编号或名称|-d=VID:PID|-D=向设备下载镜像|-U=从设备上传到文件','DFU/DFUse 镜像；不要把未经确认的 bin 直接写入',[adv('查看 DFU 参数帮助','dfu-util --help','实际 -D 会写入设备；必须先核对 VID/PID、接口、存储区域及固件格式，STM32MP 引导阶段和最终 Linux 是不同设备状态。')]);
ref('STM32_Programmer_CLI','-l=列调试接口|-c=连接参数，例如 port=SWD|-w=写入镜像，危险|-v=写后验证|-rst=复位目标','参数以当前 STM32CubeProgrammer 版本为准',[adv('查看当前工具完整参数','STM32_Programmer_CLI --help','ST-Link/SWD、UART、USB DFU 等模式取决于目标芯片及接线；写入前核对供电、Flash 地址和镜像布局。')]);
ref('rkdeveloptool','','命令 ld 列设备 / rfi 查询Flash / db 下载Loader到RAM / wl 写存储',[adv('查看瑞芯微下载工具帮助','rkdeveloptool -h','只在 Rockusb Loader/Maskrom 模式使用；db/wl 不是 Android ADB 指令。Loader、镜像和扇区地址必须来自板卡官方方案，写存储前备份。')]);
ref('JLinkExe','-CommanderScript=命令脚本|-Device=目标型号|-If=SWD 或 JTAG|-Speed=调试时钟 kHz','J-Link Commander；Windows 通常为 JLink.exe',[adv('查看 J-Link 调用格式','JLinkExe -?','实际加载镜像用 Commander 的 loadfile 等指令；确认芯片、接口、时钟、电压及镜像后操作，不能自动猜测。')]);
ref('avrdude','-?=帮助，列调用语法|-c=编程器类型|-p=MCU型号|-P=端口|-U=存储操作；w 写入、r 读取','适用于受支持的 AVR；不是所有51芯片的通用工具',[adv('查看支持的编程器类型','avrdude -c ?','此项仅查询；写Flash、EEPROM、熔丝前必须核对器件、编程器及官方参数，熔丝错误可能使常规连接失效。')]);

const valueOptions={ls:'',grep:'-C',find:'-name -type -mtime -size',head:'-n -c',tail:'-n',sed:'',awk:'-F -v',ping:'-c -W',
 curl:'-o -C -X -H -d --connect-timeout',wget:'-O -T',ssh:'-p -i -L',scp:'-P -i',rsync:'-e',tar:'-f -C',journalctl:'-u -n --since',
 dmesg:'-l',kill:'',top:'-p',gdb:'-p -ex',strace:'-p -o -e',perf:'-p',cmake:'-S -B -D -G --build --parallel --install',
 gcc:'-o -I -L -l',make:'-j -C',ninja:'-j -C',git:'-C',python3:'-m',i2cdetect:'',spidev_test:'-D -s -b -p',adb:'-s',fastboot:'-s',
 pm:'',am:'-n -a -d --es --ez --ei',settings:'--user',logcat:'-b -v -s --pid',screenrecord:'--time-limit --size --bit-rate',monkey:'-p -c --throttle',
 start:'/d',net:'/user',schtasks:'/tn /fo',tasklist:'/fi',taskkill:'/pid /im',stat:'-c',unzip:'-d',sort:'-k',sftp:'-P -i',sudo:'-u',
 mosquitto_pub:'-h -p -t -m -q --cafile',mosquitto_sub:'-h -p -t -q --cafile','v4l2-ctl':'-d',aplay:'-D'};
for(const [tool,r] of Object.entries(refs))for(const o of r.options) {
 o.takesValue=(valueOptions[tool]||'').split(' ').includes(o.value) || (tool.includes('-') && o.value.startsWith('-') && !['-Wait','-Recurse','-Force','-WhatIf','-Confirm','-Full','-Detailed','-Examples','-Syntax','-SimpleMatch'].includes(o.value));
}

// 新模板：列明目标与前提，所有修改均由用户核对后发送。
function add(system,id,category,title,syntax,example,description,requires,tags=[]) {
    if(entries.some(e=>e.id===system+'-'+id || (e.system===system && e.example===example)))return;
    entries.push({system,id:system+'-'+id,category,title,syntax,example,description,requires,interaction:'shell',tags});
}
const linux=[
 ['debug-gdb','调试诊断','调试程序与调用栈','gdb [选项] <程序>','gdb --args ./app --help','启动调试器；在 GDB 内使用 break main、run、next、step、print、bt、continue、quit。','需安装 gdb；程序建议以 -g -O0 编译。'],
 ['debug-attach','调试诊断','附加进程调试','gdb -p <PID>','gdb -p 1234','附加会暂停被调试进程；PID 换成真实值，退出用 detach 再 quit。','需 ptrace 权限；不能影响关键服务。'],
 ['debug-strace','调试诊断','追踪文件与系统调用','strace [选项] <程序>','strace -f -o /tmp/app.trace ./app','把系统调用记录到文件，定位打不开文件、网络失败及权限错误。','需 strace；会运行程序并有性能开销。'],
 ['debug-perf','调试诊断','统计程序性能','perf stat -- <程序>','perf stat -- ./app','统计 CPU、上下文切换等事件；并非所有内核开放计数器。','需 perf 及内核事件权限。'],
 ['debug-valgrind','调试诊断','检查内存泄漏','valgrind [选项] <程序>','valgrind --leak-check=full ./app','检查泄漏和非法访存，运行会明显变慢。','需 valgrind、匹配架构和可信程序。'],
 ['debug-readelf','调试诊断','检查 ELF 架构与依赖','readelf [-hld] <文件>','readelf -h -l -d ./app','检查 CPU 架构、解释器及动态库依赖，定位 Exec format error。','需 binutils；只读取文件。'],
 ['debug-objdump','调试诊断','反汇编查看指令','objdump -d <ELF>','objdump -d ./app','读取程序机器指令，结合符号定位问题。','需匹配目标架构的 binutils。'],
 ['debug-ldd','调试诊断','检查动态库缺失','ldd <可信程序>','ldd ./app','not found 表示库未找到；不要随意复制不同架构的库。','仅对可信程序使用；部分实现可能执行加载器。'],
 ['download-resume','下载与部署','断点续传程序文件','curl -fL -C - -o <文件> <URL>','curl -fL -C - -o app.bin https://example.com/app.bin','替换可信下载地址；下载后核对来源和 SHA256。','需 curl、可写目录和可达网络；示例网址不是实际程序。'],
 ['deploy-scp','下载与部署','上传程序到开发板','scp <本机程序> <用户@主机:路径>','scp ./app user@192.168.3.2:/tmp/app','在有源文件的一端执行；目标应可写，之后在板端设置执行权限。','需 scp、SSH 服务和授权账号。'],
 ['deploy-rsync','下载与部署','预览同步程序目录','rsync -avn <源/> <目标>','rsync -avn ./dist/ user@192.168.3.2:/opt/app/','-n 只预览；去掉后才写入，源目录末尾 / 表示同步内容。','两端需 rsync；远端通过 SSH。'],
 ['build-cmake','构建开发','配置 CMake 项目','cmake -S <源码> -B <构建> [-D变量=值]','cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug','配置源码与独立构建目录，不直接把生成文件混进源码。','需 cmake、编译器及项目依赖。'],
 ['build-cmake-run','构建开发','编译项目','cmake --build <构建目录> --parallel <任务数>','cmake --build build --parallel 4','先执行 CMake 配置；并行数按 CPU 与内存调整。','已有 build 配置及相应构建工具。'],
 ['build-gcc','构建开发','编译可调试 C 程序','gcc -g -O0 -Wall <源文件> -o <输出>','gcc -g -O0 -Wall main.c -o app','生成带调试符号的程序；目标架构应与运行板一致。','需 gcc；交叉编译要用目标工具链。'],
 ['build-python-venv','构建开发','创建 Python 虚拟环境','python3 -m venv <目录>','python3 -m venv .venv','创建隔离依赖环境，后用 . .venv/bin/activate 激活。','需 Python3 与 venv 模块。'],
 ['build-python-deps','下载与部署','安装项目 Python 依赖','python3 -m pip install -r <依赖文件>','python3 -m pip install -r requirements.txt','先激活虚拟环境；依赖文件和软件源应可信。','需 pip 和 requirements.txt。'],
 ['app-launch','应用与操作','启动程序并传参','<程序路径> [参数]','./app --config /tmp/app.conf','当前目录的程序用 ./；参数由程序文档定义，需有执行权限。','实际存在的匹配架构程序及配置。'],
 ['app-background','应用与操作','后台运行并保存日志','nohup <程序> > <日志> 2>&1 &','nohup ./app > /tmp/app.log 2>&1 &','标准输出写日志，2>&1 合并错误，& 后台；用 ps 确认状态。','需 nohup；会启动程序，停止前先确认 PID。'],
 ['app-open-file','应用与操作','使用桌面默认应用打开文件','xdg-open <路径或URL>','xdg-open /tmp/readme.txt','交给桌面默认应用；纯 SSH 无图形会话时可能无法使用。','需图形会话、xdg-utils 和相应应用。'],
 ['setting-service','设置与服务','设置服务开机启动','systemctl enable --now <服务>','systemctl enable --now app.service','立即启动并加入开机启动；取消用 disable，停止用 stop。','需 systemd、管理员权限和真实服务单元。'],
 ['setting-hostname','设置与服务','修改主机名','hostnamectl set-hostname <名称>','hostnamectl set-hostname dev-board','修改持久主机名；先记录旧名以便恢复。','systemd 系统；需管理员权限。'],
 ['setting-timezone','设置与服务','修改时区','timedatectl set-timezone <时区>','timedatectl set-timezone Asia/Shanghai','先用 timedatectl list-timezones 查询支持的名称。','systemd 系统；需管理员权限。'],
 ['setting-wifi','设置与服务','交互连接 Wi-Fi','nmcli --ask device wifi connect <SSID>','nmcli --ask device wifi connect MyWiFi','会提示认证信息，不把密码存入指令模板；可能断开当前网络。','需 NetworkManager、Wi-Fi 硬件和授权。'],
 ['setting-nm-conn','设置与服务','启用已保存网络配置','nmcli connection up <连接名>','nmcli connection up Wired-connection-1','先用 nmcli connection show 查真实名称；修改网络可能使 SSH 断开。','NetworkManager 管理的网络及权限。'],
 ['action-http','应用与操作','调用 HTTP 接口触发操作','curl -X POST -H <头> -d <正文> <URL>',"curl -X POST -H 'Content-Type: application/json' -d '{\"action\":\"status\"}' http://192.168.3.2:8080/api",'Action 和字段必须与设备 API 一致；不包含通用控制所有设备的协议。','设备已开放并授权的 HTTP API；需 curl。'],
 ['action-mqtt','应用与操作','发布 MQTT 消息','mosquitto_pub -h <主机> -t <主题> -m <内容>','mosquitto_pub -h 192.168.3.2 -t board/command -m status','主题及内容由业务协议约定；生产环境应使用认证和 TLS。','需 mosquitto-clients、可达且有权限的 Broker。'],
 ['debug-mqtt','调试诊断','订阅 MQTT 调试消息','mosquitto_sub -h <主机> -t <主题> -v','mosquitto_sub -h 192.168.3.2 -t board/status -v','持续显示主题与消息；Ctrl+C 停止。','需 mosquitto-clients 与 Broker 订阅权限。'],
 ['debug-video','硬件调试','列出摄像头支持格式','v4l2-ctl -d <设备> --list-formats-ext','v4l2-ctl -d /dev/video0 --list-formats-ext','只查询摄像头格式和分辨率，不启动采集。','需 v4l-utils 和设备读取权限。'],
 ['debug-audio','硬件调试','列出 ALSA 播放设备','aplay -l','aplay -l','查看声卡编号；播放另用 aplay -D plughw:卡,设备 文件.wav。','需 alsa-utils、声卡驱动与权限。'],
 ['debug-gpio','硬件调试','列出 GPIO 控制器','gpiodetect','gpiodetect','先确认控制器和引脚定义，再操作；libgpiod 版本语法有差别。','需 libgpiod 工具；不自动改变引脚电平。'],
 ['debug-usb','硬件调试','查看 USB 拓扑','lsusb -t','lsusb -t','查看 USB 速度、层级和驱动，区分物理连接与功能驱动。','需 usbutils。'],
 ['debug-storage','调试诊断','查看块设备文件系统','lsblk -f','lsblk -f','查看 UUID、类型与挂载点，不格式化、不写盘。','需 util-linux。'],
 ['syntax-pipeline','高级语法','管道过滤并保存结果','<命令> | <过滤器> | tee <文件>',"dmesg | grep -Ei 'usb|gadget' | tee /tmp/usb.log",'| 把前一条标准输出交给下一条；tee 同时显示并写文件，默认覆盖。','Linux Shell、grep、tee 及日志权限。'],
 ['syntax-condition','高级语法','成功后才执行下一步','<命令1> && <命令2>','test -f /tmp/app && ls -l /tmp/app','&& 仅前一步返回0才执行；; 无条件继续；|| 失败时继续。','POSIX Shell；test 和 ls 可用。'],
 ['syntax-loop','高级语法','循环查看目录下文件','for <变量> in <集合>; do <命令>; done','for f in /sys/class/udc/*/state; do if [ -r "$f" ]; then echo "$f"; cat "$f"; fi; done','循环变量使用 $f，路径引用必须保留双引号；分号分隔同一行命令。','POSIX Shell；只读，路径不存在时无输出。'],
 ['syntax-capture','高级语法','将输出作为参数','<命令> "$(<子命令>)"','printf "%s\\n" "$(uname -r)"','$(...) 取子命令标准输出，双引号保留空格；不要对不可信输入使用 eval。','POSIX Shell。']
];
for(const row of linux)add('linux',...row);

for(const system of ['powershell','cmd']) {
 const common='在电脑的 '+(system==='cmd'?'CMD':'PowerShell')+' 执行，需安装工具并加入 PATH；不能在设备 adb shell 内运行。';
 const rows=[
 ['adb-devices','ADB连接','查看 USB / 网络 ADB 设备','adb devices [-l]','adb devices -l','device 可用，unauthorized 需在设备授权，offline 应检查线缆与服务。'],
 ['adb-tcpip','ADB连接','通过 USB 开启网络 ADB','adb [-s 序列号] tcpip <端口>','adb tcpip 5555','先通过 USB 授权；多设备用 -s。重启 adbd 会短暂断连；仅在可信网络开放。'],
 ['adb-connect','ADB连接','连接网络 ADB','adb connect <IP:端口>','adb connect 192.168.3.109:5555','目标必须先开放该端口；传统 tcpip 与 Android 无线调试端口并不相同。'],
 ['adb-pair','ADB连接','配对 Android 无线调试','adb pair <IP:配对端口>','adb pair 192.168.3.109:37123','端口取自设备无线调试配对窗口，运行后自行输入配对码；随后使用连接端口 connect。'],
 ['adb-disconnect','ADB连接','断开指定网络 ADB','adb disconnect <IP:端口>','adb disconnect 192.168.3.109:5555','只断开电脑连接，不等于关闭板端监听。'],
 ['adb-usb','ADB连接','切回 USB 并关闭传统网络 ADB','adb [-s 序列号] usb','adb usb','重启为 USB 模式，网络终端会断开；设备策略可能影响结果。'],
 ['adb-shell','ADB连接','打开指定设备 Shell','adb [-s 序列号] shell','adb shell','进入设备后指令改用 Android Shell 语法；exit 返回电脑。'],
 ['adb-push','下载与部署','把程序文件复制到设备','adb push <电脑文件> <设备路径>','adb push "C:/Downloads/app.apk" /data/local/tmp/app.apk','只是复制，不是安装；设备路径须可写。'],
 ['adb-pull','下载与部署','从设备下载日志或文件','adb pull <设备路径> <电脑路径>','adb pull /sdcard/Download/log.txt "C:/Downloads/log.txt"','只复制权限可读的文件；电脑目录必须存在且可写。'],
 ['adb-install','下载与部署','安装或更新 Android 应用','adb install [-r] <APK>','adb install -r "C:/Downloads/app.apk"','-r 保留数据更新，APK 签名和版本必须兼容；不要随意卸载清数据。'],
 ['adb-logcat','调试诊断','采集 Android 错误日志','adb logcat [参数]','adb logcat -v threadtime -s AndroidRuntime:E','持续跟踪崩溃日志；Ctrl+C 停止，更多筛选在 Android logcat 条目。'],
 ['adb-bugreport','调试诊断','导出 Android 诊断报告','adb bugreport <输出.zip>','adb bugreport "C:/Downloads/bugreport.zip"','报告可能含设备及应用信息，分享前检查；生成可能较慢。'],
 ['adb-reboot','应用与操作','重启 Android 设备','adb reboot [bootloader|recovery]','adb reboot','会中断终端、文件传输及未保存工作；可选目标模式需设备支持。'],
 ['fastboot-devices','固件与恢复','列出 Fastboot 模式设备','fastboot devices','fastboot devices','ADB 模式与 Fastboot 模式不同，驱动也可能不同。'],
 ['fastboot-product','固件与恢复','查询 Fastboot 产品型号','fastboot getvar product','fastboot getvar product','只读取型号；镜像与设备型号必须匹配，不能仅凭能识别就刷写。'],
 ['fastboot-reboot','固件与恢复','从 Fastboot 返回系统','fastboot reboot','fastboot reboot','退出引导加载器并启动系统，会断开当前连接。'],
 ['download-curl','下载与部署','下载程序并支持续传','curl.exe -fL -C - -o <文件> <URL>','curl.exe -fL -C - -o app.zip https://example.com/app.zip','替换可信真实地址；下载后验证摘要，不自动运行或解压覆盖。'],
 ['deploy-scp','下载与部署','经 SSH 上传程序','scp <文件> <用户@主机:目标>','scp "C:/Downloads/app" user@192.168.3.2:/tmp/app','目标系统需开启 SSH/SFTP；首次连接核对主机指纹。'],
 ['git-clone','构建开发','下载 Git 项目源码','git clone <仓库URL> [新目录]','git clone https://github.com/example/project.git project-src','示例仓库需替换；下载源码不等于编译或安装。'],
 ['build-cmake','构建开发','配置 CMake 编译目录','cmake -S <源码> -B <构建>','cmake -S . -B build','需对应编译器和依赖，不同工具链使用独立 build 目录。'],
 ['build-project','构建开发','编译 CMake 项目','cmake --build <目录> --parallel <任务数>','cmake --build build --parallel 4','在已配置的构建目录编译，多配置生成器可加 --config Release。'],
 ['python-packages','下载与部署','通过 Python 安装项目依赖','py -m pip install -r <文件>','py -m pip install -r requirements.txt','建议先激活项目虚拟环境；软件包可能运行构建脚本，确认来源。'],
 ['winget-search','下载与部署','搜索软件包与标识','winget search <名称>','winget search 7zip','先查询准确 ID、发布者和来源再安装。'],
 ['winget-install','下载与部署','安装指定软件包','winget install --id <ID> -e','winget install --id 7zip.7zip -e','会启动安装程序，可能提示 UAC；先用 winget show 查来源。'],
 ['open-settings','设置与应用','打开 Windows 网络设置','start 或 Start-Process <设置URI>',system==='cmd'?'start "" ms-settings:network-status':"Start-Process 'ms-settings:network-status'",'只打开系统设置页面；用户在界面确认网络更改。'],
 ['run-scheduled','应用与操作','触发现有计划任务','schtasks /run /tn <任务名>','schtasks /run /tn "MyTask"','只运行已经创建且有权限的任务；按任务定义执行，不能假设任务安全。'],
 ['service-query','设置与服务','查询 Windows 服务','sc.exe query <服务名>','sc.exe query sshd','查看 OpenSSH 服务是否运行；系统未安装时会报告服务不存在。'],
 ['service-start','设置与服务','启动 Windows SSH 服务','sc.exe start <服务名>','sc.exe start sshd','会启用入站 SSH 服务，需管理员及安装 OpenSSH Server；检查防火墙与账号。'],
 ['service-stop','设置与服务','停止 Windows SSH 服务','sc.exe stop <服务名>','sc.exe stop sshd','会中断 SSH 连接，远程操作前确认其他接入途径。']
 ];
 for(const r of rows)add(system,...r,common);
}

const android=[
 ['adb-net-state','ADB与连接','查看网络 ADB 监听配置','getprop <属性>','getprop service.adb.tcp.port','-1 或空通常表示传统 TCP 端口未启用；不代表 USB ADB 不可用。','Android 设备 Shell；只读。'],
 ['adb-net-enable','ADB与连接','root 开启传统网络 ADB','setprop ...; setprop ...','setprop service.adb.tcp.port 5555; setprop ctl.restart adbd','需先在支持的设备执行 su；重启 adbd 会断开当前会话。电脑再 adb connect IP:5555；只用于可信网络。','Android 设备 Shell；必须已获 root，受 SELinux/厂商策略限制；不写持久属性。'],
 ['adb-net-disable','ADB与连接','root 关闭传统网络 ADB','setprop ...; setprop ...','setprop service.adb.tcp.port -1; setprop ctl.restart adbd','关闭临时传统 TCP 配置并重启 adbd，当前会话会断开；不控制独立的无线配对服务。','Android 设备 Shell；必须 root 并受设备策略允许。'],
 ['app-launch-component','应用与操作','指定 Activity 启动应用','am start -n <包名/Activity>','am start -n com.example.app/.MainActivity','先查真实包名及 Activity，应用必须允许从 Shell 启动。','Android Shell；应用已安装且组件允许访问。'],
 ['app-open-settings','设置与应用','打开系统设置主界面','am start -a <Action>','am start -a android.settings.SETTINGS','将系统设置应用切到前台，不直接修改设置。','Android Shell；需系统实现该 Intent。'],
 ['app-open-wifi','设置与应用','打开 Wi-Fi 设置界面','am start -a <Action>','am start -a android.settings.WIFI_SETTINGS','打开 Wi-Fi 设置供用户选择网络。','Android Shell；设置应用与 Activity 可用。'],
 ['app-open-details','设置与应用','打开应用详情与权限入口','am start -a <Action> -d <URI>','am start -a android.settings.APPLICATION_DETAILS_SETTINGS -d package:com.example.app','替换真实包名；在系统界面查看权限、存储和默认设置。','Android Shell；已安装对应应用。'],
 ['app-broadcast','应用与操作','发送带参数广播','am broadcast -a <Action> [--es 键 值]','am broadcast -a com.example.app.TEST --es message hello','需要应用声明并允许接收该广播；受保护广播不能任意发送。','Android Shell；已知应用协议和接收权限。'],
 ['app-stop','应用与操作','停止指定应用','am force-stop <包名>','am force-stop com.example.app','结束应用并标记强停，未保存数据可能丢失；用真实包名。','Android Shell；有权限停止目标应用。'],
 ['app-launch-monkey','应用与操作','按包名启动应用入口','monkey -p <包名> -c <类别> 1','monkey -p com.example.app -c android.intent.category.LAUNCHER 1','只发送一个入口启动事件；不要增加随机事件数用于日常启动。','Android Shell；包有桌面入口并允许启动。'],
 ['setting-bright-read','设置与服务','查询屏幕亮度原值','settings get system <键>','settings get system screen_brightness','先记录此值；支持范围和自动亮度行为依设备。','Android Shell；读取公开设置权限。'],
 ['setting-bright-mode','设置与服务','切换手动或自动亮度','settings put system <键> <0或1>','settings put system screen_brightness_mode 0','0 手动，1 自动；需权限，恢复时填修改前的值。','Android Shell；设备允许 WRITE_SETTINGS。'],
 ['setting-bright','设置与服务','设置屏幕亮度','settings put system <键> <亮度>','settings put system screen_brightness 128','先查询保存原值；范围由厂商决定，自动亮度可能覆盖。','Android Shell；需相应写设置权限。'],
 ['setting-timeout','设置与服务','设置屏幕休眠时间','settings put system screen_off_timeout <毫秒>','settings put system screen_off_timeout 120000','120000 毫秒为 2 分钟；先保存旧值，策略可能限制上限。','Android Shell；需写设置权限。'],
 ['setting-rotation','设置与服务','设置自动旋转开关','settings put system accelerometer_rotation <0或1>','settings put system accelerometer_rotation 1','1 自动旋转，0 禁用；设备传感器与策略影响结果。','Android Shell；需写设置权限。'],
 ['setting-dev-options','设置与应用','打开开发者选项','am start -a <Action>','am start -a android.settings.APPLICATION_DEVELOPMENT_SETTINGS','只打开入口，开发者选项是否可用由系统策略决定。','Android Shell；设置应用需提供该页面。'],
 ['action-home','交互操作','返回桌面','input keyevent <键码>','input keyevent KEYCODE_HOME','HOME 返回桌面，BACK 返回，ENTER 确认；不会关闭全部应用。','Android Shell；需输入注入权限。'],
 ['action-back','交互操作','触发返回操作','input keyevent <键码>','input keyevent KEYCODE_BACK','模拟系统返回；应用当前状态决定具体结果。','Android Shell；输入注入权限。'],
 ['action-tap','交互操作','点击屏幕坐标','input tap <X> <Y>','input tap 300 500','原点位于屏幕左上；先用 wm size 确认分辨率，勿点击敏感确认按钮。','Android Shell；输入注入权限。'],
 ['action-swipe','交互操作','滑动屏幕','input swipe <X1> <Y1> <X2> <Y2> [毫秒]','input swipe 500 1200 500 400 300','从起点滑动到终点，最后为持续毫秒，按设备尺寸修改。','Android Shell；输入注入权限。'],
 ['action-text','交互操作','向当前焦点输入文本','input text <ASCII文字>','input text hello%sworld','通常 %s 表示空格；中文及特殊字符支持有限，先确认焦点输入框。','Android Shell；输入注入权限。'],
 ['action-volume','交互操作','模拟音量增加键','input keyevent <键码>','input keyevent KEYCODE_VOLUME_UP','音量减为 KEYCODE_VOLUME_DOWN；具体音轨由前台状态决定。','Android Shell；输入注入权限。'],
 ['debug-app-memory','调试诊断','分析应用内存占用','dumpsys meminfo <包名>','dumpsys meminfo com.example.app','查看 PSS、Java/Native 堆等，包名用真实值。','Android Shell；服务访问权限因版本而异。'],
 ['debug-crash-buffer','调试诊断','只读取崩溃日志缓冲区','logcat -b crash -d','logcat -b crash -d','输出已有崩溃后退出，不清除原日志。','Android Shell；日志访问权限。'],
 ['debug-activity','调试诊断','查看前台 Activity 状态','dumpsys activity activities','dumpsys activity activities','查 resumed/top 状态定位当前前台组件；字段名随版本变化。','Android Shell；dumpsys 权限。'],
 ['debug-display','硬件调试','查看显示服务状态','dumpsys display','dumpsys display','查看显示设备、模式和逻辑显示信息。','Android Shell；dumpsys 服务权限。'],
 ['debug-input','硬件调试','查看输入设备能力','getevent -lp','getevent -lp','只列设备与按键能力；持续事件捕获用 getevent -lt，可能需 root。','Android Shell；读取 /dev/input 的权限。'],
 ['deploy-apk-local','下载与部署','安装设备目录中的 APK','pm install -r <APK路径>','pm install -r /data/local/tmp/app.apk','APK 先由电脑 adb push 或可信下载放到设备；需兼容签名。','Android Shell；包管理器安装权限。'],
 ['display-size-reset','设置与服务','恢复默认逻辑分辨率','wm size reset','wm size reset','撤销 wm size 覆盖；不是恢复出厂设置。','Android Shell；显示管理权限。'],
 ['display-density-reset','设置与服务','恢复默认显示密度','wm density reset','wm density reset','撤销 density 覆盖；布局会重新计算。','Android Shell；显示管理权限。'],
 ['package-path','应用与操作','查询应用 APK 安装路径','pm path <包名>','pm path com.example.app','只读查询安装位置，不代表有读取或修改 APK 权限。','Android Shell；包已安装。'],
 ['debug-cpu','调试诊断','查询 Android CPU 负载','dumpsys cpuinfo','dumpsys cpuinfo','查看近期各进程 CPU 统计，不代表实时精确采样。','Android Shell；dumpsys 权限。'],
 ['debug-services','调试诊断','列出 Android 服务命令入口','cmd -l','cmd -l','找到服务名后用 cmd 服务名 help 查询系统实际支持的参数。','Android Shell；Android 版本需提供 cmd 工具。'],
 ['syntax-android-filter','高级语法','组合筛选设备内日志',"logcat -d | grep -i <词>","logcat -d | grep -i 'fatal exception'",'管道连接两条设备命令，单引号保护含空格文本；仅输出，不清缓冲区。','Android Shell；logcat、grep 及日志权限。']
];
for(const row of android)add('android',...row);

for(const system of ['linux','powershell','cmd']) {
 const specs=[['probe-openocd','OpenOCD版本与调试器配置','openocd --version'],['probe-pyocd','列出 CMSIS-DAP / pyOCD 探针','pyocd list'],['probe-esp','查询 ESP 下载工具版本','esptool version'],['probe-dfu','枚举 USB DFU 下载设备','dfu-util -l'],['probe-stm32','枚举 STM32 下载接口','STM32_Programmer_CLI -l'],['probe-rockusb','枚举瑞芯微 Loader / Maskrom 设备','rkdeveloptool ld'],['probe-jlink','查询 J-Link Commander 参数',system==='linux'?'JLinkExe -?':'JLink.exe -?'],['probe-avr','查询 AVR 编程工具参数','avrdude -?']];
 for(const [id,title,example] of specs)add(system,id,'固件与调试器',title,example,example,'仅查询工具或设备；具体固件写入查看选项与高级说明。连接模式、工具驱动与镜像必须符合目标板方案。','在有USB/调试器访问权限的主机执行；需另装对应厂商/开源工具，不附带驱动，不自动进入下载或刷写。');
}

for(const r of [
 ['download-web','下载与部署','通过 PowerShell 下载程序',"Invoke-WebRequest -Uri <URL> -OutFile <路径>","Invoke-WebRequest -Uri 'https://example.com/app.zip' -OutFile './app.zip'",'替换可信下载地址；下载后校验摘要，文件存在会覆盖。','电脑 PowerShell；网络可达与目录可写。'],
 ['action-api','应用与操作','触发 HTTP JSON 接口',"Invoke-RestMethod -Method Post -Uri <URL> -Body <JSON>","Invoke-RestMethod -Method Post -Uri 'http://192.168.3.2:8080/api' -ContentType 'application/json' -Body '{\"action\":\"status\"}'",'URL、Action 及字段以目标 API 为准；调用可能改变设备状态。','PowerShell；已授权的 API。'],
 ['app-run-args','设置与应用','带参数启动 Windows 应用',"Start-Process -FilePath <程序> -ArgumentList <参数>","Start-Process -FilePath 'C:/Tools/app.exe' -ArgumentList '--help'",'指定程序路径；--help 仅示例参数，先查该程序文档。','Windows PowerShell；真实可执行文件。'],
 ['setting-service-startup','设置与服务','修改服务启动方式','Set-Service -Name <服务> -StartupType <类型>',"Set-Service -Name 'sshd' -StartupType Manual",'Manual 手动、Automatic 自动、Disabled 禁用；先记录旧值。','Windows PowerShell；管理员与真实服务。'],
 ['debug-module-help','高级语法','查询指令完整参数与示例','Get-Help <指令> -Full','Get-Help Invoke-WebRequest -Full','查看当前系统安装版本的参数集；离线帮助可能需要管理员更新。','PowerShell 内置帮助。'],
 ['syntax-ps-pipeline','高级语法','管道筛选并排序进程','Get-Process | Where-Object {...} | Sort-Object ...',"Get-Process | Where-Object { $_.CPU -gt 10 } | Sort-Object CPU -Descending | Select-Object -First 10 Name,Id,CPU",'PowerShell 管道传对象；$_ 为当前对象，-gt 为大于；CPU 为累计秒数。','PowerShell；查询权限。'],
 ['syntax-ps-loop','高级语法','循环处理文件对象','Get-ChildItem ... | ForEach-Object {...}',"Get-ChildItem -Path './logs' -Filter '*.log' | ForEach-Object { Get-FileHash -Path $_.FullName -Algorithm SHA256 }",'ForEach-Object 对每个对象执行脚本；此例只计算摘要，不删除文件。','PowerShell；logs 目录可读。'],
 ['debug-net-port','调试诊断','测试板端 TCP 端口','Test-NetConnection -ComputerName <IP> -Port <端口>',"Test-NetConnection -ComputerName 192.168.3.109 -Port 5555",'TcpTestSucceeded=False 不能直接推断设备离线，可能未监听或防火墙阻止。','Windows PowerShell；目标及端口由用户提供。']
])add('powershell',...r);
for(const r of [
 ['app-run-args','设置与应用','带参数启动 Windows 应用','start "" <程序> [参数]','start "" "C:\\Tools\\app.exe" --help','第一个带引号参数是窗口标题，空标题不能省略；--help 由程序定义。','电脑 CMD；真实可执行文件。'],
 ['syntax-cmd-loop','高级语法','逐个计算文件摘要','for %F in (...) do <命令>','for %F in (*.bin) do certutil -hashfile "%F" SHA256','交互 CMD 用 %F，.bat 文件中改成 %%F；此例只读文件。','CMD 与 certutil。'],
 ['syntax-cmd-condition','高级语法','成功后再执行下一条','<命令> && <成功命令> || <失败命令>','where adb && adb devices -l','where 找到工具后才列设备；|| 仅前一命令失败时继续。','电脑 CMD；ADB 需安装。'],
 ['syntax-cmd-redirect','高级语法','重定向输出和错误','<命令> > <文件> 2>&1','adb logcat -d > android-log.txt 2>&1','> 覆盖文件；>> 追加；2>&1 将错误合并到输出，命令在电脑执行。','CMD；ADB 设备已授权。']
])add('cmd',...r);

// 将实际示例拆为可编辑参数；复杂语法保留完整表达式，避免改坏引号和控制符。
function tokens(s){return s.match(/(?:"(?:[^"\\]|\\.)*"|'[^']*'|[^\s"'])+/g)||[]}
for(const e of entries) {
 if(e.interaction!=='shell')continue;
 const ts=tokens(e.example),toolIndex=ts[0]==='toybox'?1:0,tool=(ts[toolIndex]||'').replace(/\.exe$/i,'').toLowerCase();
 let r=refs[tool==='jlink'?'jlinkexe':tool];
 if(e.system==='cmd' && ['find','sort','mkdir','ping'].includes(tool)) {
  const special={find:[['/v','输出不包含搜索词的行'],['/c','只计算匹配行数'],['/i','忽略大小写']],sort:[['/r','倒序排序'],['/unique','去重；依 Windows 版本']],mkdir:[],ping:[['-n','发送次数'],['-w','等待响应毫秒数'],['-t','持续发送，Ctrl+C停止']]};
  r={options:special[tool].map(([value,description])=>({value,description,takesValue:['-n','-w'].includes(value)})),positions:tool==='ping'?['目标 IP 或域名']:tool==='find'?['双引号包住的搜索字符串','输入文件']:['目录或文件路径'],advanced:[]};
 }
 if(e.system==='powershell' && ['ping','sort','mkdir','find'].includes(tool))r=undefined;
 if(e.system!=='linux' && tool==='ssh')r={...r,advanced:[]};
 e.location=e.system==='android'?'开发板 · Android 设备 Shell':e.system==='linux'?'Linux 目标终端（本机或已登录开发板）':'电脑 / Windows 目标 · '+(e.system==='cmd'?'CMD':'PowerShell');
 e.usage=e.description;
 e.options=r?r.options.map(o=>({...o})):[];
 e.advanced=r?r.advanced:[];
 if(tool==='curl' && e.system==='cmd')e.advanced=[adv('检查下载服务器响应头','curl.exe -IL https://example.com/app.zip','CMD 使用双引号引用含空格的参数；-I 只读响应头，-L 跟随重定向。')];
 let positionIndex=0;
 e.parameters=/[|;&<>\n]/.test(e.example)?[{index:0,label:'完整表达式',hint:'整句编辑：| 管道，> 覆盖输出，>> 追加，2>&1 合并错误；&& 成功后继续。保留引号、分号、括号及变量引用。'+e.description,choices:[]}]:ts.map((v,i)=>{
   let opt=e.options.find(o=>o.value===v),prev=e.options.find(o=>o.value===ts[i-1] && o.takesValue);
   if(!opt && /^-[a-zA-Z]{2,}$/.test(v)) {
    const group=v.slice(1).split('').map(c=>e.options.find(o=>o.value==='-'+c));
    if(group.every(Boolean))opt={description:group.map(o=>o.value+' '+o.description).join('；')};
   }
   const position=r && r.positions[positionIndex];
   if(i>toolIndex && !opt && !prev && !/^[-]/.test(v))positionIndex++;
   return {index:i,label:i<=toolIndex?'工具 / 指令':opt?'选项 '+v:prev?'值：'+prev.value:position||'参数 '+i,
     hint:i<=toolIndex?'在'+e.location+'执行。'+e.requires:opt?opt.description:prev?prev.description+'；替换示例值。':position||'按格式中对应位置填写实际值。'+e.description+' 含空格请保留引号。',choices:[]};
 });
 if(['android','linux'].includes(e.system) && tool==='input' && e.parameters[2])e.parameters[2].choices=['KEYCODE_HOME','KEYCODE_BACK','KEYCODE_ENTER','KEYCODE_VOLUME_UP','KEYCODE_VOLUME_DOWN','KEYCODE_POWER'].map(value=>({value,description:'Android 按键；POWER 可能熄屏'}));
 if(tool==='adb' && e.parameters[1])e.parameters[1].choices=['devices','connect','pair','tcpip','usb','shell','push','pull','install','logcat','reboot'].map(value=>({value,description:'电脑 ADB 子命令；选定后查看对应模板参数'}));
 if(tool==='settings') {
  if(e.parameters[1])e.parameters[1].choices=['get','put','list','delete'].map(value=>({value,description:'get 查询 / put 修改 / list 列表 / delete 删除键，修改需权限'}));
  if(e.parameters[2])e.parameters[2].choices=['system','secure','global'].map(value=>({value,description:'Android 设置命名空间；写权限不同'}));
 }
 if(tool==='systemctl' && e.parameters[1])e.parameters[1].choices=['status','start','stop','restart','enable','disable'].map(value=>({value,description:'服务动作；修改前确认服务名与权限'}));
 if(tool==='ip' && e.parameters[1])e.parameters[1].choices=['addr','link','route'].map(value=>({value,description:'地址 / 网卡 / 路由'}));
 e.tags=[...new Set([...(e.tags||[]),e.category,e.title,tool,...(r?r.positions:[])])].filter(Boolean);
}
// 真实上下文相关值，候选不会覆盖用户已经输入的其它参数。
for(const e of entries) {
 const ts=tokens(e.example);
 if(ts[0]==='settings' && ts[1]==='put' && /_mode|_rotation/.test(ts[3]||'') && e.parameters[4])e.parameters[4].choices=[{value:'0',description:'关闭 / 手动；依键含义确定'},{value:'1',description:'开启 / 自动；依键含义确定'}];
 if(ts[0]==='adb' && ts[1]==='tcpip' && e.parameters[2])e.parameters[2].choices=[{value:'5555',description:'常用传统 TCP ADB 端口；需可信网络'}];
 if(ts[0]==='am' && ts[1]==='start')e.options.push({value:'android.settings.SETTINGS',description:'配合 -a 打开设置主页'},{value:'android.settings.WIFI_SETTINGS',description:'配合 -a 打开 Wi-Fi 设置'});
}
fs.writeFileSync(file,JSON.stringify(entries,null,2)+'\n','utf8');
console.log('已生成指令字典：'+entries.length+' 条；只更新数据，未执行指令。');
