param([string]$OutputDirectory=(Join-Path $PSScriptRoot 'release'),[string]$BuildTools,[switch]$IncludeTests)
$ErrorActionPreference='Stop'
if(!$BuildTools){$where=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe';if(Test-Path $where){$BuildTools=& $where -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath}}
if(!$BuildTools -or !(Test-Path (Join-Path $BuildTools 'VC\Auxiliary\Build\vcvars64.bat'))){throw 'Install Visual Studio C++ Build Tools (MSVC x64 and Windows 11 SDK 22621+) or pass -BuildTools.'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory);New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
$src=Join-Path $PSScriptRoot 'src';$obj=Join-Path $PSScriptRoot 'tools\obj';New-Item -ItemType Directory -Path $obj -Force|Out-Null
$testBuild=''
if($IncludeTests){$testBin=Join-Path $PSScriptRoot 'tests\bin';New-Item -ItemType Directory -Path $testBin -Force|Out-Null;$testBuild=@"
if errorlevel 1 exit /b 1
cl /nologo /MP /std:c++20 /EHsc /MT /O2 /Gy /Gw /utf-8 /DUNICODE /D_UNICODE /Fo"$obj\\" /Fe"$testBin\WinIsland-MediaFixture.exe" ..\tests\media-fixture.cpp "$obj\core.obj" "$obj\monitor.obj" /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib shlwapi.lib ole32.lib windowsapp.lib dwrite.lib bcrypt.lib xmllite.lib psapi.lib dxgi.lib advapi32.lib user32.lib
"@}
$cmd=@"
@echo off
chcp 65001 >nul
call "$BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "$src"
rc /nologo /fo "$obj\app.res" app.rc
if errorlevel 1 exit /b 1
cl /nologo /MP /std:c++20 /EHsc /MT /O2 /Gy /Gw /utf-8 /permissive- /Zc:__cplusplus /W3 /DUNICODE /D_UNICODE /Fo"$obj\\" /Fe"$OutputDirectory\WinIsland-1.2.3beta.exe" core.cpp monitor.cpp media.cpp audio.cpp notifications.cpp render.cpp accessibility.cpp app.cpp tests.cpp "$obj\app.res" /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib runtimeobject.lib windowsapp.lib d3d11.lib d2d1.lib dwrite.lib dxgi.lib dcomp.lib windowscodecs.lib dwmapi.lib shcore.lib wtsapi32.lib bcrypt.lib advapi32.lib xmllite.lib psapi.lib mmdevapi.lib comctl32.lib oleacc.lib gdiplus.lib
$testBuild
exit /b %errorlevel%
"@
$script=Join-Path $obj 'build.cmd';[IO.File]::WriteAllText($script,$cmd.Replace("`r`n","`n").Replace("`n","`r`n"),[Text.UTF8Encoding]::new($false));& cmd.exe /d /c $script
if($LASTEXITCODE -ne 0){throw 'Native compilation failed'}
Get-Item -LiteralPath (Join-Path $OutputDirectory 'WinIsland-1.2.3beta.exe')|Select-Object FullName,Length
