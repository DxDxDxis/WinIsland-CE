param([string]$BuildTools='E:\WinIslandBuildTools')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
New-Item -ItemType Directory -Force "$root/obj","$root/packages" | Out-Null
$variants=@(@{id='open-stream';source='stream.cpp';extra=''},@{id='open-stream-view';source='stream.cpp';extra='/DSTREAM_VIEW'},@{id='open-browser';source='browser.cpp';extra=''},@{id='open-hooks';source='hooks.cpp';extra=''},@{id='open-process';source='process.cpp';extra=''},@{id='open-lifecycle';source='lifecycle.cpp';extra=''})
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach($v in $variants){
 $id=$v.id;$stage="$root/staging/$id";New-Item -ItemType Directory -Force "$stage/bin" | Out-Null
 $libs=if($id -eq 'open-browser'){'"'+$root+'/third_party/webview2/build/native/x64/WebView2LoaderStatic.lib" ole32.lib oleaut32.lib version.lib shlwapi.lib user32.lib advapi32.lib'}else{''}
 $cmd=@"
@echo off
chcp 65001 >nul
call "$BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "$root"
cl /nologo /std:c++20 /EHsc /MT /O2 /utf-8 /LD $($v.extra) "$($v.source)" /Fo"$root/obj/$id.obj" /Fe"$stage/bin/$id.dll" /link /IMPLIB:"$root/obj/$id.lib" $libs
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /O2 /utf-8 helper.cpp /Fo"$root/obj/helper.obj" /Fe"$root/obj/helper.exe"
exit /b %errorlevel%
"@
 [IO.File]::WriteAllText("$root/obj/build.cmd",$cmd.Replace("`r`n","`n").Replace("`n","`r`n"),[Text.UTF8Encoding]::new($false)); & cmd.exe /d /c "$root/obj/build.cmd";if($LASTEXITCODE){throw "build $id failed"}
 if($id -eq 'open-process'){Copy-Item "$root/obj/helper.exe" "$stage/bin/helper.exe" -Force}
 $m=[ordered]@{id=$id;name=$id;author='WinIsland open SDK examples';version='1.0.0';apiVersion=1;entry="bin/$id.dll";gameVersion='>=1.3.2alpha <2.0.0'}
 if($id -eq 'open-stream'){$m.providesServices=@(@{id='org.winisland.example.wiav';version=1})}
 if($id -eq 'open-stream-view'){$m.serviceDependencies=@(@{id='org.winisland.example.wiav';minVersion=1;maxVersion=1;provider='open-stream'});New-Item -ItemType Directory -Force "$stage/assets" | Out-Null;[IO.File]::WriteAllBytes("$stage/assets/clip.wiav",[Text.Encoding]::ASCII.GetBytes('WIAV fixture version 1: 2 sec video + PCM'))}
 if($id -eq 'open-browser'){New-Item -ItemType Directory -Force "$stage/licenses" | Out-Null;Copy-Item "$root/third_party/webview2/LICENSE.txt","$root/third_party/webview2/NOTICE.txt" "$stage/licenses" -Force}
 $m | ConvertTo-Json -Depth 5 | Set-Content "$stage/mod.json" -Encoding utf8
 $package="$root/packages/$id.wimod";if(Test-Path -LiteralPath $package){[IO.File]::Delete($package)};[IO.Compression.ZipFile]::CreateFromDirectory($stage,$package,[IO.Compression.CompressionLevel]::Optimal,$false)
}

