param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[string]$BuildTools='E:\WinIslandBuildTools')
$ErrorActionPreference='Stop'
$branch=Split-Path $PSScriptRoot
$sdk=Join-Path $branch 'dependency-cache\winui\appsdk'
$framework=Join-Path $branch 'dependency-cache\winui\framework'
$output=Join-Path $PSScriptRoot "build\$Configuration"
$obj=Join-Path $output 'obj'
New-Item -ItemType Directory -Path $obj -Force | Out-Null
if(!(Test-Path (Join-Path $PSScriptRoot 'generated\complete.txt'))){ & python (Join-Path $PSScriptRoot 'prepare.py'); if($LASTEXITCODE){throw 'Projection preparation failed'} }
# Follow the official self-contained framework payload deployment; exclude package registration metadata.
Get-ChildItem -LiteralPath $framework -File | Where-Object { $_.Name -ne 'resources.pri' -and $_.Extension -in '.dll','.exe','.winmd','.png','.xbf','.xaml','.mui','.html','.pri' } | Copy-Item -Destination $output -Force
Get-ChildItem -LiteralPath $framework -Directory | Where-Object { $_.Name -notin 'AppxMetadata','Assets' } | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $output -Recurse -Force }
& python (Join-Path $PSScriptRoot 'activation.py')
if($LASTEXITCODE){throw 'Activation manifest failed'}
$flags=if($Configuration -eq 'Debug'){'/Od /Zi /D_DEBUG /MDd'}else{'/O2 /DNDEBUG /MD'}
$commands=@"
@echo off
chcp 65001 >nul
call "$BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "$output"
cl /nologo /std:c++20 /EHsc /utf-8 /bigobj /DUNICODE /D_UNICODE $flags /I"$PSScriptRoot\generated" /I"$sdk\include" /Fo"$obj\\" /Fd"$obj\settings.pdb" /Fe"$output\WinIslandSettings.exe" "$PSScriptRoot\main.cpp" /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /LIBPATH:"$sdk\lib\win10-x64" Microsoft.WindowsAppRuntime.lib windowsapp.lib user32.lib ole32.lib
if errorlevel 1 exit /b 1
mt.exe -nologo -manifest "$PSScriptRoot\generated\runtime.manifest" "$PSScriptRoot\app.manifest" -out:"$obj\merged.manifest"
if errorlevel 1 exit /b 1
mt.exe -nologo -manifest "$obj\merged.manifest" -outputresource:"$output\WinIslandSettings.exe";1
exit /b %errorlevel%
"@
$script=Join-Path $obj 'build.cmd'
[IO.File]::WriteAllText($script,$commands.Replace("`r`n","`n").Replace("`n","`r`n"),[Text.UTF8Encoding]::new($false))
& cmd.exe /d /c $script
if($LASTEXITCODE){throw 'Native WinUI build failed'}
Get-Item -LiteralPath (Join-Path $output 'WinIslandSettings.exe') | Select-Object FullName,Length





