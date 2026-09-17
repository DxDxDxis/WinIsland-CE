param([string]$OutputDirectory=(Join-Path $PSScriptRoot '../../test-fixtures'))
$ErrorActionPreference='Stop'
$where=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $where -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force "$out/scene-fixture/bin" | Out-Null
$batch=@"
@echo off
chcp 65001 >nul
call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++20 /EHsc /MT /O2 /utf-8 /LD "$PSScriptRoot\scene-fixture.cpp" /Fo"$out\scene-fixture.obj" /Fe"$out\scene-fixture\bin\example.dll" /link /IMPLIB:"$out\scene-fixture.lib"
exit /b %errorlevel%
"@
[IO.File]::WriteAllText("$out/build.cmd",$batch.Replace("`r`n","`n").Replace("`n","`r`n"),[Text.UTF8Encoding]::new($false))
& cmd.exe /d /c "$out/build.cmd"
if($LASTEXITCODE -ne 0){throw 'Fixture compilation failed'}
@{id='scene-fixture';name='宿主场景 API 验证';author='Host tests';version='1.0.0';apiVersion=1;entry='bin/example.dll'} | ConvertTo-Json | Set-Content "$out/scene-fixture/mod.json" -Encoding utf8
Add-Type -AssemblyName System.IO.Compression.FileSystem
if(Test-Path "$out/scene-fixture.wimod"){[IO.File]::Delete("$out/scene-fixture.wimod")}
[IO.Compression.ZipFile]::CreateFromDirectory("$out/scene-fixture","$out/scene-fixture.wimod")

