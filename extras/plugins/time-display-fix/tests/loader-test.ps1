$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$source='E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.6alpha-c2-no-motion\source'
$where=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $where -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
# Link the current build's actual objects; no copied/reimplemented loader.
$objects=(Get-ChildItem "$source/tools/obj/*.obj" | Where-Object Name -ne 'app.obj' | ForEach-Object {'"'+$_.FullName+'"'}) -join ' '
$run=Join-Path $root ('verification/loader-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
New-Item -ItemType Directory $run | Out-Null
$cmd=@"
@echo off
call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "$root\build"
cl /nologo /std:c++20 /EHsc /MT /O2 /utf-8 /DUNICODE /D_UNICODE /I"$source\src" "$PSScriptRoot\loader.cpp" /Fo"loader-test.obj" /Fe"loader-test.exe" $objects /link /OPT:REF user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib runtimeobject.lib windowsapp.lib d3d11.lib d2d1.lib dwrite.lib dxgi.lib dcomp.lib windowscodecs.lib dwmapi.lib shcore.lib wtsapi32.lib bcrypt.lib advapi32.lib xmllite.lib psapi.lib mmdevapi.lib crypt32.lib comctl32.lib uxtheme.lib oleacc.lib uiautomationcore.lib gdiplus.lib winhttp.lib iphlpapi.lib ws2_32.lib
if errorlevel 1 exit /b 1
loader-test.exe "$root\time-display.wimod" "$run" > "$run\results.txt" 2>&1
exit /b %errorlevel%
"@
[IO.File]::WriteAllText("$root/build/loader-test.cmd",$cmd.Replace("`r`n","`n").Replace("`n","`r`n"),[Text.UTF8Encoding]::new($false))
& cmd /d /c "$root/build/loader-test.cmd"
$code=$LASTEXITCODE
Get-Content "$run/results.txt" -ErrorAction SilentlyContinue
if($code -ne 0){throw "Loader test failed: $code"}
