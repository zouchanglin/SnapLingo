param(
    [string]$QtRoot = 'D:\Program Files\QT\6.8.3\msvc2022_64',
    [string]$BuildTools = 'D:\BuildTools',
    [string]$OutputName = 'SnapLingo-v0.3.0-win-x64-portable'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$dest = Join-Path $repo "publish\$OutputName"
if (Test-Path $dest) { throw "Output already exists: $dest. Choose a new OutputName to preserve prior packages." }
New-Item -ItemType Directory -Path $dest | Out-Null
Copy-Item "$repo\build-native\SnapLingo.exe" $dest
$env:PATH = "$QtRoot\bin;" + $env:PATH
& "$QtRoot\bin\windeployqt.exe" --release --no-plugins --no-translations --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler --no-compiler-runtime "$dest\SnapLingo.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
foreach ($plugin in @('platforms/qwindows.dll','imageformats/qico.dll','tls/qschannelbackend.dll','networkinformation/qnetworklistmanager.dll')) {
    $target = Join-Path $dest $plugin
    New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
    Copy-Item (Join-Path "$QtRoot\plugins" $plugin) $target
}
# Use only the officially redistributable CRT directory, never compiler/developer DLLs.
$crt = Get-ChildItem "$BuildTools\VC\Redist\MSVC" -Directory | Where-Object { $_.Name -match '^\d' } | Sort-Object Name -Descending | Select-Object -First 1
Copy-Item "$($crt.FullName)\x64\Microsoft.VC143.CRT\*.dll" $dest
New-Item -ItemType Directory -Path "$dest\licenses" | Out-Null
Copy-Item "$repo\LICENSE" "$dest\licenses\SnapLingo-MIT.txt"
Copy-Item "$PSScriptRoot\licenses\*" "$dest\licenses" -Recurse
Copy-Item "$PSScriptRoot\PORTABLE.txt" "$dest\README.txt"
Set-Content "$dest\qt.conf" "[Paths]`nPlugins=." -Encoding ascii
Compress-Archive -Path "$dest\*" -DestinationPath "$dest.zip" -CompressionLevel Optimal
$total = (Get-ChildItem $dest -Recurse -File | Measure-Object Length -Sum).Sum
[pscustomobject]@{ Folder=$dest; ExtractedMiB=[math]::Round($total/1MB,1); ZipMiB=[math]::Round((Get-Item "$dest.zip").Length/1MB,1) }
Get-FileHash "$dest.zip" -Algorithm SHA256
