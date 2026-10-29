param(
    [string]$QtRoot = 'D:\Program Files\QT\6.8.3\msvc2022_64',
    [string]$BuildTools = 'D:\BuildTools'
)
$ErrorActionPreference = 'Stop'
$env:VSLANG = '1033'
$repo = Split-Path $PSScriptRoot -Parent
$vsdev = Join-Path $BuildTools 'Common7\Tools\VsDevCmd.bat'
if (!(Test-Path $vsdev)) { throw "C++ Build Tools not found: $vsdev" }
if (!(Test-Path "$QtRoot\bin\Qt6Core.dll")) { throw "Qt not found: $QtRoot" }
# Import the compiler environment into this process; do not change the user's PATH.
& $env:ComSpec /d /s /c "`"$vsdev`" -no_logo -arch=x64 >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$cmake = Join-Path $BuildTools 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $BuildTools 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$env:PATH = "$QtRoot\bin;" + $env:PATH
$configureOptions = @()
if ((Test-Path "$env:VCToolsInstallDir\bin\Hostx64\x64\2052") -and !(Test-Path "$env:VCToolsInstallDir\bin\Hostx64\x64\1033")) {
    $configureOptions += '-DSNAPLINGO_MSVC_INCLUDE_PREFIX=注意: 包含文件: '
}
& $cmake -S $repo -B "$repo\build-native" -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_PREFIX_PATH=$QtRoot" -DCMAKE_BUILD_TYPE=Release @configureOptions
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& $cmake --build "$repo\build-native" --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'C++ build failed' }
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir "$repo\build-native" --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Regression checks failed' }
