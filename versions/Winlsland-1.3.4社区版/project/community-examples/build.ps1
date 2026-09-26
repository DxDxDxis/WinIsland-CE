param([string]$BuildTools='E:\WinIslandBuildTools')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$out=Join-Path $root 'packages'
New-Item -ItemType Directory -Force $out,"$root/obj","$root/staging" | Out-Null
$variants=@(
 @{id='community-base';role=1;name='A 基础服务';version=1},
 @{id='community-base-v2';role=1;name='A 第二版本服务';version=2},
 @{id='community-consumer';role=2;name='A 服务消费者';version=1},
 @{id='community-optional';role=5;name='A 可选服务消费者';version=1},
 @{id='community-task';role=3;name='C 可取消后台任务';version=1},
 @{id='community-qoi-view';role=4;name='B 原有媒体 API 显示 QOI';version=1},
 @{id='community-qoi-decoder';role=0;name='B QOI 解码基础服务';version=1},
 @{id='community-restart';role=1;name='重启生效策略示例';version=1}
)
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach($v in $variants){
 $id=$v.id;$stage="$root/staging/$id";New-Item -ItemType Directory -Force "$stage/bin" | Out-Null
 $source=if($v.role -eq 0){'qoi_decoder.cpp'}else{'example.cpp'}
 $cmd=@"
@echo off
call "$BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "$root"
cl /nologo /std:c++20 /EHsc /MT /O2 /utf-8 /LD /DROLE=$($v.role) /DSERVICE_VERSION=$($v.version) "$source" /Fo"$root/obj/$id.obj" /Fe"$stage/bin/$id.dll" /link /IMPLIB:"$root/obj/$id.lib"
exit /b %errorlevel%
"@
 [IO.File]::WriteAllText("$root/obj/build.cmd",$cmd.Replace("`r`n","`n").Replace("`n","`r`n"),[Text.UTF8Encoding]::new($false));& cmd.exe /d /c "$root/obj/build.cmd";if($LASTEXITCODE){throw "build $id failed"}
 $manifest=[ordered]@{id=$id;name=$v.name;author='WinIsland community SDK examples';version='1.0.0';apiVersion=1;entry="bin/$id.dll";gameVersion='>=1.3.2alpha <2.0.0'}
 if($v.role -eq 1){$manifest.providesServices=@(@{id='org.winisland.example.math';version=$v.version})}
 if($v.role -eq 2 -or $v.role -eq 5){$manifest.serviceDependencies=@(@{id='org.winisland.example.math';minVersion=1;maxVersion=1;optional=($v.role -eq 5)})}
 if($v.role -eq 0){$manifest.providesServices=@(@{id='org.winisland.example.qoi';version=1});New-Item -ItemType Directory -Force "$stage/licenses" | Out-Null;Copy-Item "$root/third_party/qoi.h","$root/third_party/origin.txt","$root/third_party/LICENSE.txt" "$stage/licenses" -Force}
 if($id -eq 'community-restart'){$manifest.lifecycle='restart-required'}
 if($v.role -eq 4){New-Item -ItemType Directory -Force "$stage/assets" | Out-Null;[byte[]]$bytes=@(113,111,105,102,0,0,0,8,0,0,0,8,4,0,255,32,192,96,255,253,192,0,0,0,0,0,0,0,1);[IO.File]::WriteAllBytes("$stage/assets/example.qoi",$bytes)}
 $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$stage/mod.json" -Encoding utf8
 $package="$out/$id.wimod";if(Test-Path -LiteralPath $package){[IO.File]::Delete($package)}
 [IO.Compression.ZipFile]::CreateFromDirectory($stage,$package,[IO.Compression.CompressionLevel]::Optimal,$false)
}
Get-FileHash "$out/*.wimod" | ConvertTo-Json | Set-Content "$root/package-hashes.json" -Encoding utf8

& python (Join-Path $PSScriptRoot "prepare-test-packages.py")
if($LASTEXITCODE){throw "test fixture packaging failed"}
