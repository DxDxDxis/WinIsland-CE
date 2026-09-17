$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$bat="@echo off`r`ncall `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul`r`ncl /nologo /std:c++20 /EHsc /MT /utf-8 `"$PSScriptRoot\verify.cpp`" /Fo`"$root\build\verify.obj`" /Fe`"$root\build\verify.exe`" /link dwrite.lib`r`n"
[IO.File]::WriteAllText("$root/build/verify.cmd",$bat)
& cmd /d /c "$root/build/verify.cmd"
if($LASTEXITCODE){throw 'test compile failed'}
& "$root/build/verify.exe" "$root/package/bin/time-display.dll"
if($LASTEXITCODE){throw 'verification failed'}
