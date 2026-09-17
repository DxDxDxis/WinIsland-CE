param(
    [string]$BuildTools='E:\WinIslandBuildTools',
    [string]$QtRoot=(Join-Path $PSScriptRoot 'tools\qt'),
    [string]$CMake=(Join-Path $PSScriptRoot 'tools\python-build\cmake\data\bin\cmake.exe'),
    [string]$Stage=(Join-Path $env:TEMP 'WinIsland-QCloudBuild-1242'),
    [string]$OutputDirectory=(Join-Path $PSScriptRoot 'release\1.2.4beta_2')
)
$ErrorActionPreference='Stop'
$Stage=[IO.Path]::GetFullPath($Stage)
if($Stage -match '[^\x00-\x7F]' -or $Stage.Contains('"')){throw 'Choose an ASCII staging path for MSVC/NMake.'}
foreach($required in @($CMake,(Join-Path $QtRoot 'bin\Qt6Core.dll'),(Join-Path $BuildTools 'VC\Auxiliary\Build\vcvars64.bat'))){
    if(!(Test-Path -LiteralPath $required)){throw "Missing build dependency: $required"}
}
New-Item -ItemType Directory -Path $Stage -Force | Out-Null
# Physical ASCII staging avoids NMake's Unicode-path/PDB failure. No junctions or deletion.
foreach($pair in @(@((Join-Path $PSScriptRoot 'lyric-helper'),'lyric-helper'),@((Join-Path $PSScriptRoot 'third_party'),'third_party'),@($QtRoot,'tools\qt'))){
    & robocopy $pair[0] (Join-Path $Stage $pair[1]) /E /NFL /NDL /NJH /NJS /NC /NS | Out-Null
    if($LASTEXITCODE -ge 8){throw 'Staging copy failed'}
}
$build=@"
@echo off
call "$BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set VSLANG=1033
"$CMake" -S "$Stage\lyric-helper" -B "$Stage\build" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$Stage\tools\qt" -DCRYPTOPP_USE_OPENMP=OFF
if errorlevel 1 exit /b 1
"$CMake" --build "$Stage\build" --config Release
exit /b %errorlevel%
"@
$script=Join-Path $Stage 'build.cmd'
[IO.File]::WriteAllText($script,$build.Replace("`r`n","`n").Replace("`n","`r`n"),[Text.UTF8Encoding]::new($false))
& cmd.exe /d /c $script
if($LASTEXITCODE -ne 0){throw 'QCloudMusicApi build failed'}
$dest=Join-Path $OutputDirectory 'lyric-provider'
New-Item -ItemType Directory -Path (Join-Path $dest 'tls') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $Stage 'build\WinIsland-LyricHelper.exe') -Destination $dest
foreach($name in @('Qt6Core.dll','Qt6Network.dll')){Copy-Item -LiteralPath (Join-Path $QtRoot ('bin\'+$name)) -Destination $dest}
Copy-Item -LiteralPath (Join-Path $QtRoot 'plugins\tls\qschannelbackend.dll') -Destination (Join-Path $dest 'tls')
$redist=Get-ChildItem -LiteralPath (Join-Path $BuildTools 'VC\Redist\MSVC') -Directory | Where-Object {$_.Name -match '^\d'} | Sort-Object Name -Descending | Select-Object -First 1
foreach($name in @('msvcp140.dll','msvcp140_1.dll','vcruntime140.dll','vcruntime140_1.dll')){
    Copy-Item -LiteralPath (Join-Path $redist.FullName ('x64\Microsoft.VC143.CRT\'+$name)) -Destination $dest
}
Set-Content -LiteralPath (Join-Path $dest 'qt.conf') -Value "[Paths]`nPlugins=." -Encoding Ascii
Write-Output "Built and deployed: $dest"
