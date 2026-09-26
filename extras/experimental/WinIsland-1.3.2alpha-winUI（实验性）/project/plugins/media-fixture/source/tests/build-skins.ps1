param([string]$OutputDirectory=(Join-Path $PSScriptRoot '../../built'))
$ErrorActionPreference='Stop'
$where=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $where -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force "$out/media-fixture/bin" | Out-Null
$sdk=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../source/src'))
foreach($header in 'mod_api.h','scene_api.h','media_api.h'){Copy-Item -LiteralPath (Join-Path $sdk $header) -Destination (Join-Path $PSScriptRoot "../src/$header") -Force}
$lines=@('@echo off','chcp 65001 >nul',('call "'+$vs+'\VC\Auxiliary\Build\vcvars64.bat" >nul'),('cl /nologo /std:c++20 /EHsc /MT /O2 /utf-8 /LD "'+$PSScriptRoot+'\skin-example.cpp" /Fo"'+$out+'\skin.obj" /Fe"'+$out+'\media-fixture\bin\media-fixture.dll" /link /IMPLIB:"'+$out+'\skin.lib"'),'exit /b %errorlevel%')
[IO.File]::WriteAllText("$out/build-skin.cmd",($lines -join [Environment]::NewLine),[Text.UTF8Encoding]::new($false))
& cmd.exe /d /c "$out/build-skin.cmd"
if($LASTEXITCODE){throw 'compile failed'}
@{id='media-fixture';name='多媒体与交互皮肤';author='WinIsland SDK example';version='1.0.0';apiVersion=1;entry='bin/media-fixture.dll';gameVersion='>=1.2.7alpha-r1 <1.3.0'} | ConvertTo-Json | Set-Content "$out/media-fixture/mod.json" -Encoding utf8
foreach($asset in 'skin.png','motion.gif','motion.mp4'){if(!(Test-Path "$out/media-fixture/assets/$asset")){throw "Generate test media first: $asset"}}
Add-Type -AssemblyName System.IO.Compression.FileSystem
if(Test-Path "$out/media-fixture.wimod"){Remove-Item -LiteralPath "$out/media-fixture.wimod"}
$zip=[IO.Compression.ZipFile]::Open("$out/media-fixture.wimod",[IO.Compression.ZipArchiveMode]::Create)
try{
  foreach($name in 'mod.json','bin/media-fixture.dll','assets/skin.png','assets/motion.gif','assets/motion.mp4'){
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,"$out/media-fixture/$name",$name,[IO.Compression.CompressionLevel]::Optimal)|Out-Null
  }
  foreach($name in 'README.md','LICENSE'){
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,(Join-Path $PSScriptRoot "../../$name"),$name,[IO.Compression.CompressionLevel]::Optimal)|Out-Null
  }
}finally{$zip.Dispose()}
Get-FileHash "$out/media-fixture.wimod" -Algorithm SHA256
