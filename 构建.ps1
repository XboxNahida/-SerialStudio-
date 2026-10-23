param(
    [string]$QtRoot = 'C:\Qt\6.11.1\mingw_64',
    [string]$QtTools = 'C:\Qt\Tools',
    [switch]$SkipTests,
    [switch]$SkipDeploy,
    [string]$OutputDirectory = 'release'
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$cmake = Join-Path $QtTools 'CMake_64\bin\cmake.exe'
$ctest = Join-Path $QtTools 'CMake_64\bin\ctest.exe'
$mingw = Join-Path $QtTools 'mingw1310_64\bin'
$ninja = Join-Path $QtTools 'Ninja'
$depRoot = Join-Path $projectRoot 'deps'
$localQt = Join-Path $depRoot 'qt'
$source = Join-Path $depRoot 'qtserialport-6.11.1'
$build = Join-Path $projectRoot 'build'
$release = [IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
if (!$release.StartsWith($projectRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw '部署目录必须在本工程内' }
$originalPath = $env:PATH
foreach ($required in @($cmake, $ctest, (Join-Path $mingw 'g++.exe'), (Join-Path $QtRoot 'bin\qmake.exe'))) {
    if (!(Test-Path -LiteralPath $required)) { throw "缺少工具：$required" }
}
function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "命令失败（$LASTEXITCODE）：$Program" }
}
try {
    # 主 Qt DLL 先于本地附加模块，保证开发期插件从完整的 Qt 安装加载。
    $env:PATH = "$QtRoot\bin;$localQt\bin;$mingw;$ninja;$originalPath"
    if (!(Test-Path -LiteralPath (Join-Path $localQt 'lib\cmake\Qt6SerialPort\Qt6SerialPortConfig.cmake'))) {
        New-Item -ItemType Directory -Force -Path $depRoot | Out-Null
        $archive = Join-Path $depRoot 'qtserialport-6.11.1.zip'
        if (!(Test-Path -LiteralPath $archive)) {
            Invoke-Checked 'curl.exe' @('-fL','--max-time','120','https://codeload.github.com/qt/qtserialport/zip/refs/tags/v6.11.1','-o',$archive)
        }
        $expectedHash = '3C8931C226FDCA18315EBC636F90B50DBC4CB99AF3BB3D6D413211590A014835'
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) { throw '串口源码压缩包 SHA256 不匹配，已停止构建' }
        if (!(Test-Path -LiteralPath $source)) { Expand-Archive -LiteralPath $archive -DestinationPath $depRoot }
        $depBuild = Join-Path $depRoot 'serialport-build'
        Invoke-Checked $cmake @('-S',$source,'-B',$depBuild,'-G','Ninja',"-DCMAKE_PREFIX_PATH=$QtRoot", "-DCMAKE_INSTALL_PREFIX=$localQt",'-DCMAKE_BUILD_TYPE=Release','-DQT_BUILD_TESTS=OFF','-DQT_BUILD_EXAMPLES=OFF')
        Invoke-Checked $cmake @('--build',$depBuild,'--parallel','6')
        Invoke-Checked $cmake @('--install',$depBuild)
    }
    $vtermArchive = Join-Path $depRoot 'libvterm-0.3.3.tar.gz'
    $vtermSource = Join-Path $depRoot 'libvterm-0.3.3'
    if (!(Test-Path -LiteralPath $vtermArchive)) {
        Invoke-Checked 'curl.exe' @('-fL','--max-time','120','https://www.leonerd.org.uk/code/libvterm/libvterm-0.3.3.tar.gz','-o',$vtermArchive)
    }
    if ((Get-FileHash -LiteralPath $vtermArchive -Algorithm SHA256).Hash -ne '09156F43DD2128BD347CBEEBE50D9A571D32C64E0CF18D211197946AFF7226E0') { throw 'libvterm SHA256 不匹配' }
    if (!(Test-Path -LiteralPath (Join-Path $vtermSource 'src\vterm.c'))) { Invoke-Checked 'tar.exe' @('-xzf',$vtermArchive,'-C',$depRoot) }
    Invoke-Checked $cmake @('-S',$projectRoot,'-B',$build,'-G','Ninja',"-DCMAKE_PREFIX_PATH=$QtRoot", "-DQt6SerialPort_DIR=$localQt/lib/cmake/Qt6SerialPort",'-DCMAKE_BUILD_TYPE=Release')
    Invoke-Checked $cmake @('--build',$build,'--parallel','6')
    if (!$SkipTests) { Invoke-Checked $ctest @('--test-dir',$build,'--output-on-failure') }
    if (!$SkipDeploy) {
        New-Item -ItemType Directory -Force -Path $release | Out-Null
        Copy-Item -LiteralPath (Join-Path $build 'SerialStudio.exe') -Destination $release -Force
        Copy-Item -LiteralPath (Join-Path $localQt 'bin\Qt6SerialPort.dll') -Destination $release -Force
        # 使用项目内的部署工具视图，将 Qt 主模块与本地 SerialPort 放在同一查询路径。
        # 只复制文件，不修改全局 Qt；部署工具仍正常检查所有依赖。
        $deployTools = Join-Path $depRoot 'deploy-tools'
        New-Item -ItemType Directory -Force -Path $deployTools | Out-Null
        Copy-Item -LiteralPath (Join-Path $QtRoot 'bin\qtpaths.exe') -Destination $deployTools -Force
        Copy-Item -Path (Join-Path $QtRoot 'bin\Qt6*.dll') -Destination (Join-Path $localQt 'bin') -Force
        # Qt Multimedia 的 FFmpeg 后端依赖同套 Qt 的编解码 DLL；部署工具查询本地附加模块目录。
        foreach ($codec in @('avcodec-*.dll','avformat-*.dll','avutil-*.dll','swresample-*.dll','swscale-*.dll')) {
            $codecFiles = @(Get-ChildItem -Path (Join-Path $QtRoot "bin\$codec") -File)
            if ($codecFiles.Count -eq 0) { throw "缺少 Qt 多媒体运行依赖：$codec" }
            $codecFiles | Copy-Item -Destination (Join-Path $localQt 'bin') -Force
        }
        Copy-Item -LiteralPath (Join-Path $QtRoot 'bin\Qt6Core.dll') -Destination $deployTools -Force
        $deployConfig = "[Paths]`r`nPrefix=$($QtRoot.Replace('\','/'))`r`nBinaries=$($localQt.Replace('\','/'))/bin`r`n"
        [IO.File]::WriteAllText((Join-Path $deployTools 'qt.conf'),$deployConfig,[Text.UTF8Encoding]::new($false))
        Invoke-Checked (Join-Path $QtRoot 'bin\windeployqt.exe') @('--qtpaths',(Join-Path $deployTools 'qtpaths.exe'),'--release','--qmldir',(Join-Path $projectRoot 'qml'),'--no-translations','--compiler-runtime','--dir',$release,(Join-Path $release 'SerialStudio.exe'))
        $runtimeConfig = "[Paths]`r`nPlugins=.`r`nQmlImports=qml`r`n"
        [IO.File]::WriteAllText((Join-Path $release 'qt.conf'),$runtimeConfig,[Text.UTF8Encoding]::new($false))
        Copy-Item -LiteralPath (Join-Path $projectRoot '使用说明.md') -Destination $release -Force
        Copy-Item -LiteralPath (Join-Path $projectRoot '版本更新说明.md') -Destination $release -Force
        Copy-Item -LiteralPath (Join-Path $projectRoot '桥接协议说明.md') -Destination $release -Force
        Copy-Item -LiteralPath (Join-Path $projectRoot '数据解析与文件传输说明.md') -Destination $release -Force
        Copy-Item -LiteralPath (Join-Path $projectRoot '终端指令辅助说明.md') -Destination $release -Force
        foreach ($document in @('正式版验收清单.md','指令任务覆盖矩阵.md')) {
            if (Test-Path -LiteralPath (Join-Path $projectRoot $document)) { Copy-Item -LiteralPath (Join-Path $projectRoot $document) -Destination $release -Force }
        }
        $boardOutput = Join-Path $release 'board'
        New-Item -ItemType Directory -Force -Path $boardOutput | Out-Null
        Copy-Item -LiteralPath (Join-Path $projectRoot 'board\board_agent.py') -Destination $boardOutput -Force
        Copy-Item -LiteralPath (Join-Path $projectRoot '第三方组件说明.md') -Destination $release -Force
        $licenseOutput = Join-Path $release 'licenses'
        New-Item -ItemType Directory -Force -Path $licenseOutput | Out-Null
        Copy-Item -Path (Join-Path $source 'LICENSES\*.txt') -Destination $licenseOutput -Force
        Copy-Item -LiteralPath (Join-Path $vtermSource 'LICENSE') -Destination (Join-Path $licenseOutput 'libvterm-MIT.txt') -Force
        Copy-Item -Path (Join-Path $QtRoot '..\..\Licenses\*') -Destination $licenseOutput -Force
        Write-Host "独立运行程序：$release\SerialStudio.exe"
    }
    Write-Host '构建完成。真实串口与开发板通讯需要另外验证。'
} finally { $env:PATH = $originalPath }
