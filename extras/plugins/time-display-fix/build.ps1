$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC x64 build tools required'}
$root=$PSScriptRoot
New-Item -ItemType Directory -Force "$root/build","$root/package/bin" | Out-Null
$cmd=@"
@echo off
call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "$root\build"
cl /nologo /std:c++20 /EHsc /MT /O2 /W4 /utf-8 /LD "$root\src\time-display.cpp" /Fo"time-display.obj" /Fe"$root\package\bin\time-display.dll" /link /IMPLIB:"time-display.lib"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /O2 /W4 /utf-8 "$root\tests\unit.cpp" /Fo"unit.obj" /Fe"unit.exe"
if errorlevel 1 exit /b 1
unit.exe > unit-results.txt
if errorlevel 1 exit /b 1
dumpbin /headers "$root\package\bin\time-display.dll" > dll-headers.txt
dumpbin /exports "$root\package\bin\time-display.dll" > dll-exports.txt
dumpbin /dependents "$root\package\bin\time-display.dll" > dll-dependents.txt
"@
[IO.File]::WriteAllText("$root/build/compile.cmd",$cmd.Replace("`r`n","`n").Replace("`n","`r`n"),[Text.UTF8Encoding]::new($false))
& cmd.exe /d /c "$root/build/compile.cmd"
if($LASTEXITCODE -ne 0){throw 'Build or unit tests failed'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$temp="$root/build/time-display.zip"
if(Test-Path -LiteralPath $temp){Remove-Item -LiteralPath $temp}
[IO.Compression.ZipFile]::CreateFromDirectory("$root/package",$temp,[IO.Compression.CompressionLevel]::Optimal,$false)
Copy-Item -LiteralPath $temp -Destination "$root/time-display.wimod" -Force
Get-FileHash "$root/time-display.wimod"
