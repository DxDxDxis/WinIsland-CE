param([string]$OutputDirectory=(Join-Path $PSScriptRoot '../../built'))
$ErrorActionPreference='Stop'
$where=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $where -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force "$out/media-fixture/bin" | Out-Null
$batch=@"
@echo off
chcp 65001 >nul
call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++20 /EHsc /MT /O2 /utf-8 /LD "$PSScriptRoot\media-fixture.cpp" /Fo"$out\media-fixture.obj" /Fe"$out\media-fixture\bin\media-fixture.dll" /link /IMPLIB:"$out\media-fixture.lib"
exit /b %errorlevel%
"@
[IO.File]::WriteAllText("$out/build.cmd",$batch.Replace("`n","`r`n"),[Text.UTF8Encoding]::new($false))
& cmd.exe /d /c "$out/build.cmd"
if($LASTEXITCODE){throw 'compile failed'}
@{id='media-fixture';name='媒体场景 API 验证';author='WinIsland';version='1.0.0';apiVersion=1;entry='bin/media-fixture.dll'} | ConvertTo-Json | Set-Content "$out/media-fixture/mod.json" -Encoding utf8
# 2x2 24-bit BMP
$bytes=[Collections.Generic.List[byte]]::new(); function B([int]$n){$bytes.Add([byte]$n)}
(0..1)|%{B 0}; B 0x36; (0..3)|%{B 0}; B 0x28; (0..3)|%{B 0}; B 2;B 0;B 0;B 0; B 2;B 0;B 0;B 0; B 1;B 0; B 24;B 0; (0..4)|%{B 0}; B 0x10;B 0;B 0;B 0; (0..7)|%{B 0}
[byte[]](0,0,255,0,255,0,0,0, 0,255,0,255,255,255,255,0) | %{$bytes.Add($_)}
[IO.File]::WriteAllBytes("$out/media-fixture/skin.bmp",$bytes.ToArray())
Add-Type -AssemblyName System.IO.Compression.FileSystem
if(Test-Path "$out/media-fixture.wimod"){Remove-Item "$out/media-fixture.wimod"}
[IO.Compression.ZipFile]::CreateFromDirectory("$out/media-fixture","$out/media-fixture.wimod")

