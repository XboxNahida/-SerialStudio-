param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$output=[IO.Path]::GetFullPath($(if([IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory}else{Join-Path $projectRoot $OutputDirectory}))
if(!$output.StartsWith($projectRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw '工具暂存目录必须在工程内'}
if(Test-Path -LiteralPath $output){throw '请使用新的工具暂存目录，避免混入旧文件'}
$catalog=Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot 'toolchains.json')|ConvertFrom-Json
$cache=Join-Path $projectRoot 'deps\toolchains'
New-Item -ItemType Directory -Force -Path $cache|Out-Null
foreach($component in $catalog.components){
    $archive=Join-Path $cache $component.archive
    if(!(Test-Path -LiteralPath $archive)){
        $temporary=$archive+'.'+[guid]::NewGuid().ToString('N')+'.part'
        # PowerShell 使用 Windows 系统代理/默认网络，不传入强制直连参数。
        Invoke-WebRequest -Uri $component.url -OutFile $temporary -UseBasicParsing
        if((Get-FileHash -LiteralPath $temporary).Hash -ne $component.sha256){throw "工具摘要不匹配：$($component.id)"}
        Move-Item -LiteralPath $temporary -Destination $archive
    }
    if((Get-FileHash -LiteralPath $archive).Hash -ne $component.sha256){throw "缓存工具摘要不匹配：$($component.id)"}
}
$bootstrap=Join-Path $projectRoot ('build\tool-bootstrap-'+[guid]::NewGuid().ToString('N'))
$pythonComponent=$catalog.components|Where-Object id -eq 'python'
Expand-Archive -LiteralPath (Join-Path $cache $pythonComponent.archive) -DestinationPath $bootstrap
$python=Join-Path $bootstrap 'python.exe'
New-Item -ItemType Directory -Path $output|Out-Null
foreach($component in $catalog.components){
    $entry=Join-Path $bootstrap ($component.id+'.json')
    [IO.File]::WriteAllText($entry,($component|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    $destination=Join-Path $output ($component.id+'\'+$component.version)
    & $python -I (Join-Path $PSScriptRoot 'safe_archive.py') (Join-Path $cache $component.archive) $destination $entry
    if($LASTEXITCODE -ne 0){throw "安全解压失败：$($component.id)"}
}
Write-Host "工具已准备：$output（未修改系统 PATH，未安装服务/驱动）"
