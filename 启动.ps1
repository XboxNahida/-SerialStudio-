param([switch]$Demo)
$ErrorActionPreference = 'Stop'
$executable = Join-Path $PSScriptRoot 'release\SerialStudio.exe'
if (!(Test-Path -LiteralPath $executable)) { throw '请先运行构建.ps1，生成 release 目录。' }
if ($Demo) { Start-Process -FilePath $executable -ArgumentList '--demo' -WorkingDirectory $PSScriptRoot }
else { Start-Process -FilePath $executable -WorkingDirectory $PSScriptRoot }
