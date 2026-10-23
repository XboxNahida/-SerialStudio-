param([switch]$TestPackage)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$workspace = [IO.Path]::GetFullPath((Join-Path $project 'build\installer'))
$versionText = Get-Content -Raw -Encoding UTF8 (Join-Path $project 'CMakeLists.txt')
if ($versionText -notmatch 'project\(SerialStudio VERSION (\d+\.\d+\.\d+)') { throw '缺少版本' }
if ($versionText -notmatch 'set\(SERIALSTUDIO_VERSION "([0-9A-Za-z.-]+)"\)') { throw '缺少完整版本' }
$version = $Matches[1]
$suffix = if ($TestPackage) { '-隔离验证' } else { '' }
$setup = Join-Path $project "安装包\SerialStudio-$version-Windows-x64-Setup$suffix.exe"
$testRoot = if ($TestPackage) { Join-Path $workspace "验证环境-$version" } else { Join-Path $workspace ('正式验证-'+[Guid]::NewGuid().ToString('N')) }
$install = [IO.Path]::GetFullPath((Join-Path $testRoot 'App'))
if (!$install.StartsWith($workspace+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw '安装验证目录越界' }
$keyName = if ($TestPackage) { 'SerialStudio_PackageTest' } else { 'SerialStudio' }
$key = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$keyName"
if (Test-Path -LiteralPath $key) { throw '已有对应安装记录，拒绝覆盖；请保留用户已有安装' }
if (Test-Path -LiteralPath $install) { throw '验证安装目录已存在，拒绝覆盖' }
$desktop = if ($TestPackage) { Join-Path $testRoot 'Desktop' } else { [Environment]::GetFolderPath('Desktop') }
$menu = if ($TestPackage) { Join-Path $testRoot 'StartMenu\Serial Studio' } else { Join-Path ([Environment]::GetFolderPath('Programs')) 'Serial Studio' }
$desktopLink = Join-Path $desktop '串口工作台.lnk'
$menuLink = Join-Path $menu '串口工作台.lnk'
if ((Test-Path -LiteralPath $desktopLink) -or (Test-Path -LiteralPath $menu)) { throw '已有同名快捷方式，拒绝覆盖或移除' }
function Invoke-Setup {
    param([string]$Shortcuts)
    $flags = @('/S')
    if ($Shortcuts -ne '') { $flags += "/SHORTCUTS=$Shortcuts" }
    if (!$TestPackage) { $flags += "/D=$install" } # NSIS 的 /D 必须为最后一个参数，不加引号。
    $process = Start-Process -FilePath $setup -ArgumentList $flags -WindowStyle Hidden -PassThru -Wait
    if ($process.ExitCode -ne 0) { throw "安装退出码：$($process.ExitCode)" }
}
function Check-Installed {
    $registration = Get-ItemProperty -LiteralPath $key
    if ($registration.DisplayVersion -ne $version -or $registration.InstallLocation -ne $install) { throw '卸载注册版本或安装路径错误' }
    if (!(Test-Path -LiteralPath (Join-Path $install '卸载.exe'))) { throw '没有生成卸载程序' }
    $manifest = Get-Content -Raw -Encoding UTF8 (Join-Path $install '安装文件清单.json') | ConvertFrom-Json
    foreach ($entry in $manifest) {
        $file = Join-Path $install $entry.path
        if (!(Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file).Hash -ne $entry.sha256) { throw "安装文件哈希错误：$($entry.path)" }
    }
    $origin = (Get-FileHash -LiteralPath (Join-Path $project 'release\SerialStudio.exe')).Hash
    if ((Get-FileHash -LiteralPath (Join-Path $install 'SerialStudio.exe')).Hash -ne $origin) { throw '安装 EXE 与 release 不一致' }
    Write-Output "通过：$($manifest.Count) 个安装文件的 SHA256、版本与卸载注册"
}
function Check-Shortcut {
    param([string]$Path,[string]$Target)
    if (!(Test-Path -LiteralPath $Path)) { throw "未创建快捷方式：$Path" }
    $shell = New-Object -ComObject WScript.Shell
    $link = $shell.CreateShortcut($Path)
    try { if ($link.TargetPath -ne $Target) { throw "快捷方式目标错误：$Path" } }
    finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($link); [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell) }
}

if ($TestPackage) {
    Invoke-Setup '' # 验证默认勾选。
    Check-Shortcut $desktopLink (Join-Path $install 'SerialStudio.exe')
    Check-Shortcut $menuLink (Join-Path $install 'SerialStudio.exe')
    Check-Shortcut (Join-Path $menu '卸载串口工作台.lnk') (Join-Path $install '卸载.exe')
    Write-Output '通过：默认桌面和开始菜单快捷方式、启动和卸载目标'
} else {
    Invoke-Setup '0'
    if ((Test-Path -LiteralPath $desktopLink) -or (Test-Path -LiteralPath $menuLink)) { throw '取消快捷方式后仍创建了入口' }
    Write-Output '通过：正式安装包取消创建快捷方式'
}
Check-Installed
$sentinel = Join-Path $install '用户新增文件-不得删除.txt'
[IO.File]::WriteAllText($sentinel,'用户文件应保留',[Text.UTF8Encoding]::new($false))
Invoke-Setup '0' # 覆盖升级并取消原有快捷方式。
Check-Installed
if ((Test-Path -LiteralPath $desktopLink) -or (Test-Path -LiteralPath $menuLink)) { throw '重新安装取消快捷方式后仍有入口' }
if (!(Test-Path -LiteralPath $sentinel)) { throw '重新安装影响用户额外文件' }
Write-Output '通过：同目录覆盖安装、取消已有快捷方式、保留用户额外文件'
$originalPath = $env:PATH
try {
    $env:PATH="$install;C:\Windows\System32;C:\Windows"
    $process = Start-Process -FilePath (Join-Path $install 'SerialStudio.exe') -ArgumentList '--smoke-test' -WindowStyle Hidden -Wait -PassThru -RedirectStandardError (Join-Path $testRoot '启动.err')
    if ($process.ExitCode -ne 0 -or (Get-Item -LiteralPath (Join-Path $testRoot '启动.err')).Length -ne 0) { throw '安装后程序启动失败或 stderr 非空' }
} finally { $env:PATH=$originalPath }
Write-Output '通过：仅安装目录与 Windows 系统 PATH 的原生程序启动'
$process = Start-Process -FilePath (Join-Path $install '卸载.exe') -ArgumentList '/S' -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "卸载失败：$($process.ExitCode)" }
$deadline = [DateTime]::UtcNow.AddSeconds(10)
while ((Test-Path -LiteralPath (Join-Path $install '卸载.exe')) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
if ((Test-Path -LiteralPath $key) -or (Test-Path -LiteralPath $desktopLink) -or (Test-Path -LiteralPath $menuLink)) { throw '卸载注册或快捷方式有残留' }
$remaining = @(Get-ChildItem -LiteralPath $install -Recurse -File)
if ($remaining.Count -ne 1 -or $remaining[0].FullName -ne $sentinel -or (Get-Content -Raw -Encoding UTF8 -LiteralPath $sentinel) -ne '用户文件应保留') { throw '卸载未仅保留用户额外文件' }
Write-Output '通过：卸载清理程序/依赖/注册/快捷方式，并保留用户额外文件'
Write-Output "验证目录：$testRoot"
