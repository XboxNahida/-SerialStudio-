param([switch]$SkipBuild, [switch]$TestPackage)
$ErrorActionPreference = 'Stop'
$project = $PSScriptRoot
$buildRoot = Join-Path $project 'build\installer'
$toolRoot = Join-Path $project 'deps\installer-tools'
$nsis = Join-Path $toolRoot 'nsis-3.11\makensis.exe'
$archive = Join-Path $toolRoot 'nsis-3.11.zip'
$toolHash = 'C7D27F780DDB6CFFB4730138CD1591E841F4B7EDB155856901CDF5F214394FA1'
$utf8 = [Text.UTF8Encoding]::new($false)
New-Item -ItemType Directory -Force -Path $buildRoot,$toolRoot | Out-Null
if (!(Test-Path -LiteralPath $archive)) {
    & curl.exe -fL --max-time 120 'https://downloads.sourceforge.net/project/nsis/NSIS%203/3.11/nsis-3.11.zip' -o $archive
    if ($LASTEXITCODE -ne 0) { throw 'NSIS 下载失败' }
}
if ((Get-FileHash -LiteralPath $archive).Hash -ne $toolHash) { throw 'NSIS 压缩包 SHA256 不匹配' }
if (!(Test-Path -LiteralPath $nsis)) { Expand-Archive -LiteralPath $archive -DestinationPath $toolRoot }
if (!$SkipBuild) { & (Join-Path $project '构建.ps1'); if (!$?) { throw '程序构建失败' } }
$release = Join-Path $project 'release'
foreach ($name in @('SerialStudio.exe','Qt6Core.dll','Qt6Quick.dll','Qt6SerialPort.dll','Qt6Multimedia.dll','platforms\qwindows.dll','qt.conf','终端指令辅助说明.md','catalog\terminal_commands.json','指令库更新说明.md')) {
    if (!(Test-Path -LiteralPath (Join-Path $release $name))) { throw "缺少发布文件：$name" }
}
if ((Get-FileHash (Join-Path $release 'SerialStudio.exe')).Hash -ne (Get-FileHash (Join-Path $project 'build\SerialStudio.exe')).Hash) { throw 'release 与最新构建 EXE 不一致' }
if (Get-ChildItem -LiteralPath $release -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) { throw '发布源中含重解析点，已停止' }
$cmake = Get-Content -Raw -Encoding UTF8 (Join-Path $project 'CMakeLists.txt')
if ($cmake -notmatch 'project\(SerialStudio VERSION (\d+\.\d+\.\d+)') { throw '无法读取程序版本' }
$numericVersion = $Matches[1]
if ($cmake -notmatch 'set\(SERIALSTUDIO_VERSION "([0-9A-Za-z.-]+)"\)') { throw '无法读取完整发布版本' }
$version = $Matches[1]
# 每次使用新目录，避免递归清理或把旧文件带入安装包。
$stage = Join-Path $buildRoot ('payload-' + [Guid]::NewGuid().ToString('N'))
$toolStage = Join-Path $buildRoot ('tools-' + [Guid]::NewGuid().ToString('N'))
& (Join-Path $project 'tools\准备工具链.ps1') -OutputDirectory $toolStage
if(!$?){throw '必备工具准备失败'}
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -Path (Join-Path $release '*') -Destination $stage -Recurse
Copy-Item -LiteralPath (Join-Path $project 'installer\图标.ico') -Destination (Join-Path $stage 'SerialStudio.ico')
Copy-Item -LiteralPath (Join-Path $project '安装与卸载说明.md') -Destination $stage
Copy-Item -LiteralPath (Join-Path $toolRoot 'nsis-3.11\COPYING') -Destination (Join-Path $stage 'licenses\NSIS-COPYING.txt')
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName)
$manifest = @($files | ForEach-Object { [ordered]@{path=$_.FullName.Substring($stage.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash;bytes=$_.Length} })
[IO.File]::WriteAllText((Join-Path $stage '安装文件清单.json'), (ConvertTo-Json -Depth 4 -InputObject $manifest), $utf8)
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName)
$remove = @($files | ForEach-Object {
    $relative = $_.FullName.Substring($stage.Length+1)
    if ($relative.Contains('$') -or $relative.Contains('"')) { throw "NSIS 文件名含特殊符号：$relative" }
    'IfFileExists "$INSTDIR\' + $relative + '" 0 +2'
    'Delete "$INSTDIR\' + $relative + '"'
})
$directories = @(Get-ChildItem -LiteralPath $stage -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending | ForEach-Object { 'RMDir "$INSTDIR\' + $_.FullName.Substring($stage.Length+1) + '"' })
# 清单放在 payload 外，避免把构建用 include 交给最终用户。
$removeFiles = Join-Path $buildRoot ([IO.Path]::GetFileName($stage)+'-files.nsh')
$removeDirs = Join-Path $buildRoot ([IO.Path]::GetFileName($stage)+'-dirs.nsh')
$checkFiles = Join-Path $buildRoot ([IO.Path]::GetFileName($stage)+'-check.nsh')
$toolInstall = Join-Path $buildRoot ([IO.Path]::GetFileName($stage)+'-tools.nsh')
$toolLines = @()
$toolCatalog=Get-Content -Raw -Encoding UTF8 (Join-Path $project 'tools\toolchains.json')|ConvertFrom-Json
foreach($component in $toolCatalog.components){
    $relative=$component.id+'\'+$component.version
    $source=Join-Path $toolStage $relative
    $toolLines += 'SetOutPath "$PLUGINSDIR\ToolSeed\'+$relative+'"'
    $toolLines += 'File /r "'+$source+'\*"'
}
[IO.File]::WriteAllLines($toolInstall,$toolLines,$utf8)
$check = @($files | ForEach-Object {
    $relative = $_.FullName.Substring($stage.Length+1)
    '${If} ${FileExists} "$INSTDIR\' + $relative + '"'
    'System::Call ''kernel32::CreateFileW(w "$INSTDIR\' + $relative + '", i 0x10000, i 7, p 0, i 3, i 0, p 0) p.r0'''
    '${If} $0 == -1'
    'MessageBox MB_ICONEXCLAMATION "程序文件正在使用或没有删除权限，请关闭串口工作台后重试。" /SD IDOK'
    'SetErrorLevel 1'
    'Abort'
    '${EndIf}'
    'System::Call ''kernel32::CloseHandle(p r0)'''
    '${EndIf}'
})
[IO.File]::WriteAllLines($checkFiles, $check, $utf8)
[IO.File]::WriteAllLines($removeFiles, $remove, $utf8)
[IO.File]::WriteAllLines($removeDirs, $directories, $utf8)
$output = Join-Path $project '安装包'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$suffix = if ($TestPackage) { '-隔离验证' } else { '' }
$setup = Join-Path $output "SerialStudio-$version-Windows-x64-Setup$suffix.exe"
$toolBytes=(Get-ChildItem -LiteralPath $toolStage -File -Recurse|Measure-Object Length -Sum).Sum
$arguments = @('/V3','/INPUTCHARSET','UTF8','/OUTPUTCHARSET','UTF8',"/DAPP_VERSION=$version","/DAPP_NUMERIC_VERSION=$numericVersion","/DPAYLOAD=$stage","/DOUTPUT_FILE=$setup","/DREMOVE_FILES=$removeFiles","/DREMOVE_DIRS=$removeDirs","/DCHECK_FILES=$checkFiles","/DTOOL_INSTALL=$toolInstall",('/DINSTALLED_KIB='+[Math]::Ceiling((($files | Measure-Object Length -Sum).Sum+$toolBytes)/1KB)))
if ($TestPackage) { $arguments += '/DTEST_ROOT='+(Join-Path $buildRoot "验证环境-$version") }
$arguments += Join-Path $project 'installer\安装包.nsi'
& $nsis @arguments
if ($LASTEXITCODE -ne 0) { throw "安装包编译失败：$LASTEXITCODE" }
$hash = (Get-FileHash -LiteralPath $setup).Hash
[IO.File]::WriteAllText($setup+'.sha256',"$hash  $([IO.Path]::GetFileName($setup))`r`n",$utf8)
[IO.File]::WriteAllText((Join-Path $buildRoot '最近打包清单路径.txt'),$stage,$utf8)
Write-Host "安装包：$setup"
Write-Host "SHA256：$hash"
