param([string]$Candidate='build\toolchains-candidate', [string]$Seed='build\toolchain-payload-8298d739f11c44f7a5eaa6b1f2a579a1', [string]$Version='1.2.0')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$workspace=[IO.Path]::GetFullPath((Join-Path $project 'build'))
$root=Join-Path $workspace ('tool-installer-'+[guid]::NewGuid().ToString('N'))
$candidatePath=[IO.Path]::GetFullPath((Join-Path $project $Candidate))
$seedPath=[IO.Path]::GetFullPath((Join-Path $project $Seed))
foreach($path in @($root,$candidatePath,$seedPath)){
    if(!$path.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)){throw '回归输入和输出必须在工程 build 内'}
}
$key='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\SerialStudio_PackageTest'
if(Test-Path -LiteralPath $key){throw '已有隔离测试安装记录，请先核对，拒绝覆盖'}
$utf8=[Text.UTF8Encoding]::new($false)
New-Item -ItemType Directory -Path $root|Out-Null
$install=Join-Path $root 'App'
$managed=Join-Path $root 'Tools'
$originalPath=$env:PATH
$userPath=[Environment]::GetEnvironmentVariable('PATH','User')
$machinePath=[Environment]::GetEnvironmentVariable('PATH','Machine')
$catalog=Get-Content -Raw -Encoding UTF8 (Join-Path $project 'tools\toolchains.json')|ConvertFrom-Json
$toolsInclude=Join-Path $root 'tools.nsh'
$lines=@()
foreach($component in $catalog.components){
    $relative=$component.id+'\'+$component.version
    $lines+='SetOutPath "$PLUGINSDIR\ToolSeed\'+$relative+'"'
    $lines+='File /r "'+(Join-Path $seedPath $relative)+'\*"'
}
[IO.File]::WriteAllLines($toolsInclude,$lines,$utf8)
function Build-Fixture([string]$Name,[bool]$Old){
    $stage=Join-Path $root $Name
    New-Item -ItemType Directory -Path $stage|Out-Null
    Copy-Item -Path (Join-Path $candidatePath '*') -Destination $stage -Recurse
    if($Old){[IO.File]::WriteAllText((Join-Path $stage 'obsolete-tool-test.dll'),'旧版专有文件',$utf8)}
    $files=@(Get-ChildItem -LiteralPath $stage -File -Recurse)
    $manifest=@($files|ForEach-Object{[ordered]@{path=$_.FullName.Substring($stage.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
    [IO.File]::WriteAllText((Join-Path $stage '安装文件清单.json'),(ConvertTo-Json -Depth 4 -InputObject $manifest),$utf8)
    $files=@(Get-ChildItem -LiteralPath $stage -File -Recurse)
    $remove=Join-Path $root ($Name+'-files.nsh')
    $dirs=Join-Path $root ($Name+'-dirs.nsh')
    $checks=Join-Path $root ($Name+'-checks.nsh')
    $deletes=@($files|ForEach-Object{'Delete "$INSTDIR\'+$_.FullName.Substring($stage.Length+1)+'"'})
    [IO.File]::WriteAllLines($remove,$deletes,$utf8)
    $dirLines=@(Get-ChildItem -LiteralPath $stage -Directory -Recurse|Sort-Object {$_.FullName.Length} -Descending|ForEach-Object{'RMDir "$INSTDIR\'+$_.FullName.Substring($stage.Length+1)+'"'})
    [IO.File]::WriteAllLines($dirs,$dirLines,$utf8)
    $checkLines=@($files|ForEach-Object{
        $relative=$_.FullName.Substring($stage.Length+1)
        '${If} ${FileExists} "$INSTDIR\'+$relative+'"'
        'System::Call ''kernel32::CreateFileW(w "$INSTDIR\'+$relative+'", i 0x10000, i 7, p 0, i 3, i 0, p 0) p.r0'''
        '${If} $0 == -1'
        'SetErrorLevel 1'
        'Abort'
        '${EndIf}'
        'System::Call ''kernel32::CloseHandle(p r0)'''
        '${EndIf}'
    })
    [IO.File]::WriteAllLines($checks,$checkLines,$utf8)
    $setup=Join-Path $root ($Name+'.exe')
    $fixtureVersion=if($Old){'1.1.0'}else{$Version}
    $args=@('/V3','/INPUTCHARSET','UTF8',"/DAPP_VERSION=$fixtureVersion","/DAPP_NUMERIC_VERSION=$fixtureVersion","/DPAYLOAD=$stage","/DOUTPUT_FILE=$setup","/DREMOVE_FILES=$remove","/DREMOVE_DIRS=$dirs","/DCHECK_FILES=$checks",'/DINSTALLED_KIB=300000',"/DTEST_ROOT=$root")
    if(!$Old){$args+="/DTOOL_INSTALL=$toolsInclude"}
    $args+=Join-Path $project 'installer\安装包.nsi'
    & (Join-Path $project 'deps\installer-tools\nsis-3.11\makensis.exe') @args *> (Join-Path $root ($Name+'-compile.log'))
    if($LASTEXITCODE -ne 0){throw "隔离安装器编译失败：$Name"}
    Write-Host "通过：$Name 编译"
    return $setup
}
function Install-Fixture([string]$Setup){
    $process=Start-Process -FilePath $Setup -ArgumentList '/S /SHORTCUTS=0' -WindowStyle Hidden -Wait -PassThru
    if($process.ExitCode -ne 0){throw "隔离安装失败：$($process.ExitCode)"}
    if(!(Test-Path -LiteralPath $key)){throw '隔离卸载注册缺失'}
    foreach($entry in (Get-Content -Raw -Encoding UTF8 (Join-Path $install '安装文件清单.json')|ConvertFrom-Json)){
        if((Get-FileHash -LiteralPath (Join-Path $install $entry.path)).Hash -ne $entry.sha256){throw "程序文件不一致：$($entry.path)"}
    }
}
function Uninstall-Fixture([bool]$RemoveTools){
    $flags='/S';if($RemoveTools){$flags+=' /REMOVETOOLS=1'}
    $process=Start-Process -FilePath (Join-Path $install '卸载.exe') -ArgumentList $flags -WindowStyle Hidden -Wait -PassThru
    if($process.ExitCode -ne 0){throw '隔离卸载失败'}
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    while((Test-Path -LiteralPath (Join-Path $install '卸载.exe')) -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 100}
    if((Test-Path -LiteralPath $key) -or (Test-Path -LiteralPath (Join-Path $install 'SerialStudio.exe'))){throw '程序或隔离注册残留'}
}
$old=Build-Fixture 'old' $true
$new=Build-Fixture 'new' $false
Install-Fixture $old
[IO.File]::WriteAllText((Join-Path $install '用户文件.txt'),'保留用户文件',$utf8)
Install-Fixture $new
if(Test-Path -LiteralPath (Join-Path $install 'obsolete-tool-test.dll')){throw '升级未移除旧清单专有文件'}
foreach($component in $catalog.components){
    $version=Join-Path $managed ($component.id+'\'+$component.version)
    foreach($entry in (Get-Content -Raw -Encoding UTF8 (Join-Path $version '.serialstudio-files.json')|ConvertFrom-Json)){
        if((Get-FileHash -LiteralPath (Join-Path $version $entry.path)).Hash -ne $entry.sha256){throw "工具部署摘要错误：$($entry.path)"}
    }
}
Write-Host '通过：旧版升级、完整程序文件与必备工具摘要'
$python=Join-Path $managed 'python\3.13.7\python.exe'
$modified=Join-Path $managed 'python\3.13.7\LICENSE.txt'
[IO.File]::WriteAllText($modified,'用户修改内容',$utf8)
$unknown=Join-Path $managed 'python\3.13.7\用户资料.txt'
$external=Join-Path $root '自选外部工具.exe'
$config=Join-Path $root '用户配置.json'
foreach($path in @($unknown,$external,$config)){[IO.File]::WriteAllText($path,'用户数据',$utf8)}
$held=[IO.File]::Open((Join-Path $install 'Qt6Core.dll'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try{
    $blocked=Start-Process -FilePath $new -ArgumentList '/S /SHORTCUTS=0' -WindowStyle Hidden -Wait -PassThru
    if($blocked.ExitCode -eq 0){throw '文件占用时未拒绝升级'}
}finally{$held.Dispose()}
Uninstall-Fixture $false
if(!(Test-Path -LiteralPath $python)){throw '默认卸载没有保留工具'}
Write-Host '通过：文件占用时停止升级，默认卸载保留工具'
Install-Fixture $new
if((Get-Content -Raw -Encoding UTF8 $modified) -ne '用户修改内容'){throw '重装覆盖了已有工具修改'}
Uninstall-Fixture $true
if(Test-Path -LiteralPath $python){throw '勾选移除后仍保留未修改的受管文件'}
foreach($path in @($modified,$unknown,$external,$config,(Join-Path $install '用户文件.txt'))){if(!(Test-Path -LiteralPath $path)){throw "用户文件丢失：$path"}}
if((Get-Content -Raw -Encoding UTF8 $modified) -ne '用户修改内容'){throw '修改内容未保留'}
if($env:PATH -ne $originalPath -or [Environment]::GetEnvironmentVariable('PATH','User') -ne $userPath -or [Environment]::GetEnvironmentVariable('PATH','Machine') -ne $machinePath){throw 'PATH 被修改'}
Write-Host '通过：选择移除工具、保留已改/未知/外部文件与配置、PATH 不变'
Install-Fixture $new
if(!(Test-Path -LiteralPath $python)){throw '移除工具后重装没有补齐 Python'}
if((Get-Content -Raw -Encoding UTF8 $modified) -ne '用户修改内容' -or !(Test-Path -LiteralPath $unknown)){throw '修复缺失工具时覆盖了已有用户文件'}
Uninstall-Fixture $true
if(Test-Path -LiteralPath $python){throw '重装补齐的工具没有重新登记卸载清单'}
Write-Host '通过：同版本重装补齐缺失工具并保留已有修改，新文件可正常卸载'
$conflict=Join-Path $managed 'android-platform-tools\37.0.1'
New-Item -ItemType Directory -Path $conflict -Force|Out-Null
$conflictFile=Join-Path $conflict '用户资料.txt'
[IO.File]::WriteAllText($conflictFile,'无清单的既有目录',$utf8)
$failed=Start-Process -FilePath $new -ArgumentList '/S /SHORTCUTS=0' -WindowStyle Hidden -Wait -PassThru
if($failed.ExitCode -eq 0){throw '受管工具目录冲突时安装未报错'}
if(!(Test-Path -LiteralPath $key) -or !(Test-Path -LiteralPath (Join-Path $install '卸载.exe'))){throw '工具部署失败后没有卸载入口'}
Uninstall-Fixture $false
if((Get-Content -Raw -Encoding UTF8 $conflictFile) -ne '无清单的既有目录'){throw '失败安装修改了冲突目录'}
Write-Host '通过：工具部署失败仍可卸载应用，冲突目录保持原样'
Write-Host "隔离证据目录：$root"
