param([string]$OutputDirectory=(Join-Path $PSScriptRoot '../../examples'))
$ErrorActionPreference='Stop'
$where=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $where -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$out=[IO.Path]::GetFullPath($OutputDirectory)
foreach($name in @('example-button','example-overlay','fault-fixture')) { New-Item -ItemType Directory -Force -Path (Join-Path $out $name)|Out-Null }
New-Item -ItemType Directory -Force -Path "$out/example-button/animations" | Out-Null
Set-Content -LiteralPath "$out/example-button/animations/island.js" -Value 'return { opacity: 1, scale: 1, radius: 24 };' -Encoding utf8
Set-Content -LiteralPath "$out/example-button/animations/manifest.json" -Value '{"version":1,"engine":"winisland-host"}' -Encoding utf8
$cmd=@"
@echo off
call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "$PSScriptRoot"
cl /nologo /std:c++20 /EHsc /MT /O2 /utf-8 /LD example.cpp /Fo"$out\example-button\example.obj" /Fe"$out\example-button\example.dll" /link /IMPLIB:"$out\example-button\example.lib"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /O2 /utf-8 /DSECOND_EXAMPLE /LD example.cpp /Fo"$out\example-overlay\example.obj" /Fe"$out\example-overlay\example.dll" /link /IMPLIB:"$out\example-overlay\example.lib"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MT /O2 /utf-8 /LD fault.cpp /Fo"$out\fault-fixture\fault.obj" /Fe"$out\fault-fixture\example.dll" /link /IMPLIB:"$out\fault-fixture\example.lib"
exit /b %errorlevel%
"@
$batch=Join-Path $out 'build-examples.cmd'
[IO.File]::WriteAllText($batch,$cmd.Replace("`r`n","`n").Replace("`n","`r`n"),[Text.UTF8Encoding]::new($false))
& cmd.exe /d /c $batch
if($LASTEXITCODE -ne 0){throw 'Example build failed'}
@{id='example-button';name='按钮与问候示例';description='增加问候文字设置、实际按钮和灵动岛图层；调整动画倍率，可关闭恢复。';author='WinIsland 示例';version='1.0.0';apiVersion=1;entry='example.dll';dependencies=@()} | ConvertTo-Json | Set-Content -LiteralPath "$out/example-button/mod.json" -Encoding utf8
@{id='example-overlay';name='外观替换示例';description='演示同一目标的替换链，依赖按钮与问候示例。';author='WinIsland 示例';version='1.0.0';apiVersion=1;entry='example.dll';dependencies=@('example-button')} | ConvertTo-Json | Set-Content -LiteralPath "$out/example-overlay/mod.json" -Encoding utf8
@{id='fault-fixture';name='异常测试专用（不要安装）';description='仅供自测，onEnable 主动抛出异常。';version='1.0.0';apiVersion=1;entry='example.dll';dependencies=@()} | ConvertTo-Json | Set-Content -LiteralPath "$out/fault-fixture/mod.json" -Encoding utf8

# Users install one package; staging folders are build artifacts only.
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach($name in @('example-button','example-overlay','fault-fixture')) {
    $stage=Join-Path $out ('staging-'+$name)
    New-Item -ItemType Directory -Force -Path "$stage/bin"|Out-Null
    Copy-Item -LiteralPath "$out/$name/example.dll" -Destination "$stage/bin/example.dll" -Force
    if($name -eq 'example-button' -and (Test-Path "$out/example-button/animations")){Copy-Item -LiteralPath "$out/example-button/animations" -Destination "$stage/animations" -Recurse -Force}
    $manifest=Get-Content -LiteralPath "$out/$name/mod.json" -Raw | ConvertFrom-Json
    $manifest.entry='bin/example.dll'
    if($name -eq 'example-overlay'){$manifest.dependencies=@(@{id='example-button';version='>=1.0.0'})}
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$stage/mod.json" -Encoding utf8
    $package=Join-Path $out ($name+'.wimod')
    # Replace only this build output, never an installed package.
    if(Test-Path -LiteralPath $package){[IO.File]::Delete($package)}
    [IO.Compression.ZipFile]::CreateFromDirectory($stage,$package,[IO.Compression.CompressionLevel]::Optimal,$false)
}
