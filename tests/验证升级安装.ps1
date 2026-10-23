param([string]$OldSetup)
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$workspace=[IO.Path]::GetFullPath((Join-Path $project 'build\installer'))
$root=Join-Path $workspace ('升级验证-'+[Guid]::NewGuid().ToString('N'))
$install=[IO.Path]::GetFullPath((Join-Path $root 'App'))
if(!$install.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)){throw '验证目录越界'}
$key='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\SerialStudio'
if(Test-Path -LiteralPath $key){throw '已有用户安装记录，拒绝覆盖'}
$desktop=Join-Path ([Environment]::GetFolderPath('Desktop')) '串口工作台.lnk'
$menu=Join-Path ([Environment]::GetFolderPath('Programs')) 'Serial Studio'
if((Test-Path -LiteralPath $desktop)-or(Test-Path -LiteralPath $menu)){throw '已有用户快捷方式，拒绝覆盖'}
$versionText=Get-Content -Raw -Encoding UTF8 (Join-Path $project 'CMakeLists.txt')
if($versionText -notmatch 'set\(SERIALSTUDIO_VERSION "([0-9A-Za-z.-]+)"\)'){throw '版本缺失'}
$version=$Matches[1]
$setup=Join-Path $project "安装包\SerialStudio-$version-Windows-x64-Setup.exe"
if(!$OldSetup){$OldSetup=Join-Path $project '安装包\SerialStudio-0.4.1-Windows-x64-Setup.exe'}
New-Item -ItemType Directory -Path $root | Out-Null
function Run-Install([string]$Package,[string[]]$Extra=@(),[int]$Expected=0){
    $process=Start-Process -FilePath $Package -ArgumentList (@('/S','/SHORTCUTS=0')+$Extra+"/D=$install") -Wait -PassThru
    if($process.ExitCode -ne $Expected){throw "安装退出码 $($process.ExitCode)，预期 $Expected"}
}
function Check-New {
    if((Get-ItemProperty -LiteralPath $key).DisplayVersion -ne $version){throw '升级版本不正确'}
    $manifest=Get-Content -Raw -Encoding UTF8 (Join-Path $install '安装文件清单.json') | ConvertFrom-Json
    foreach($entry in $manifest){if((Get-FileHash -LiteralPath (Join-Path $install $entry.path)).Hash -ne $entry.sha256){throw "哈希不符 $($entry.path)"}}
    if(!(Test-Path -LiteralPath (Join-Path $install '用户自存.txt'))){throw '用户文件丢失'}
}
function Remove-TestInstall {
    $process=Start-Process -FilePath (Join-Path $install '卸载.exe') -ArgumentList '/S' -Wait -PassThru
    if($process.ExitCode -ne 0){throw '验证卸载失败'}
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    while((Test-Path -LiteralPath (Join-Path $install '卸载.exe'))-and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 100}
    if(Test-Path -LiteralPath $key){throw '卸载注册残留'}
    $left=@(Get-ChildItem -LiteralPath $install -Recurse -File)
    if($left.Count -ne 1 -or $left[0].Name -ne '用户自存.txt'){throw '卸载未仅保留用户文件'}
}
# 先验证实际交付的上一版安装包到新测试版。
Run-Install $OldSetup
[IO.File]::WriteAllText((Join-Path $install '用户自存.txt'),'保留',[Text.UTF8Encoding]::new($false))
Run-Install $setup
Check-New
Write-Output '通过：真实 0.4.1 安装包升级到测试版，全部安装文件 SHA256 正确，用户文件保留'
# 占用文件时必须在删除前停止，不能先删掉其他程序文件。
$held=[IO.File]::Open((Join-Path $install 'Qt6Core.dll'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try {Run-Install $setup @() 1} finally {$held.Dispose()}
Check-New
Write-Output '通过：文件占用时拒绝替换，安装清单全部文件仍完整'
# 更新进程退出等待，在测试辅助进程结束后才能安装。
$helper=Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile','-Command','Start-Sleep -Seconds 5') -WindowStyle Hidden -PassThru
Run-Install $setup @("/UPDATEPID=$($helper.Id)")
$helper.Refresh();if(!$helper.HasExited){throw '安装器未等待发起进程退出'}
Check-New
Write-Output '通过：UPDATEPID 等待进程退出后完成安装'
Remove-TestInstall
# 极小的旧版清单夹具用于验证新包中已不存在的文件也会被移除。
$fixture=Join-Path $root '旧版夹具'
$payload=Join-Path $fixture 'payload'
New-Item -ItemType Directory -Path $payload | Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $payload 'obsolete-upgrade-probe.dll'),'旧版清单文件',$utf8)
$files=Join-Path $fixture 'files.nsh';$dirs=Join-Path $fixture 'dirs.nsh';$checks=Join-Path $fixture 'check.nsh'
[IO.File]::WriteAllText($files,'Delete "$INSTDIR\obsolete-upgrade-probe.dll"',$utf8)
[IO.File]::WriteAllText($dirs,'; 无子目录',$utf8)
[IO.File]::WriteAllText($checks,'; 旧版测试夹具',$utf8)
$fixtureSetup=Join-Path $fixture '旧版清单夹具.exe'
$nsis=Join-Path $project 'deps\installer-tools\nsis-3.11\makensis.exe'
& $nsis /V2 /INPUTCHARSET UTF8 /DAPP_VERSION=0.4.1 /DAPP_NUMERIC_VERSION=0.4.1 "/DPAYLOAD=$payload" "/DOUTPUT_FILE=$fixtureSetup" "/DREMOVE_FILES=$files" "/DREMOVE_DIRS=$dirs" "/DCHECK_FILES=$checks" /DINSTALLED_KIB=1 (Join-Path $project 'installer\安装包.nsi')
if($LASTEXITCODE -ne 0){throw '旧版清单夹具编译失败'}
Run-Install $fixtureSetup
if(!(Test-Path -LiteralPath (Join-Path $install 'obsolete-upgrade-probe.dll'))){throw '旧版夹具未安装'}
Run-Install $setup
Check-New
if(Test-Path -LiteralPath (Join-Path $install 'obsolete-upgrade-probe.dll')){throw '旧清单独有文件未移除'}
Write-Output '通过：旧清单独有 DLL 已移除，新版完整写入，清单外用户文件保留'
Remove-TestInstall
Write-Output "升级验证目录：$root"
