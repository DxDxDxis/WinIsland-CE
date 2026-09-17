param([string]$OutputDirectory=(Join-Path $PSScriptRoot 'release'),[string]$BuildTools,[switch]$IncludeTests)
$ErrorActionPreference='Stop'
if(!$BuildTools){$where=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe';if(Test-Path $where){$BuildTools=& $where -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath}}
if(!$BuildTools -or !(Test-Path (Join-Path $BuildTools 'VC\Auxiliary\Build\vcvars64.bat'))){throw 'Install Visual Studio C++ Build Tools (MSVC x64 and Windows 11 SDK 22621+) or pass -BuildTools.'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory);New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
$src=Join-Path $PSScriptRoot 'src';$obj=Join-Path $OutputDirectory 'obj';New-Item -ItemType Directory -Path $obj -Force|Out-Null
$testBuild=''
if($IncludeTests){$testBin=Join-Path $PSScriptRoot 'tests\bin';New-Item -ItemType Directory -Path $testBin -Force|Out-Null;$testBuild=@"
if errorlevel 1 exit /b 1
cl /nologo /MP /std:c++20 /EHsc /MT /O2 /Gy /Gw /utf-8 /DUNICODE /D_UNICODE /Fo"$obj\\" /Fe"$testBin\WinIsland-MediaFixture.exe" ..\tests\media-fixture.cpp "$obj\core.obj" "$obj\monitor.obj" "$obj\telemetry.obj" "$obj\music_sources.obj" /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib shlwapi.lib ole32.lib windowsapp.lib dwrite.lib bcrypt.lib xmllite.lib psapi.lib dxgi.lib advapi32.lib user32.lib iphlpapi.lib ws2_32.lib winhttp.lib /OPT:REF /OPT:ICF
"@}
# Build a deterministic binary bundle consumed through the Win32 RCDATA resource.
$bundlePath=Join-Path $obj 'embedded_components.bundle'
$componentRoot=(Resolve-Path (Join-Path $PSScriptRoot '..\lyric-provider')).Path
$componentFiles=Get-ChildItem -LiteralPath $componentRoot -Recurse -File | Sort-Object FullName
$bundleStream=[IO.File]::Open($bundlePath,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::Read)
$bundleWriter=[IO.BinaryWriter]::new($bundleStream,[Text.Encoding]::UTF8)
$bundleWriter.Write([Text.Encoding]::ASCII.GetBytes('WICOMP1'))
$bundleWriter.Write([UInt32]$componentFiles.Count)
foreach($componentFile in $componentFiles){
  $relative=$componentFile.FullName.Substring($componentRoot.Length+1).Replace('\','/')
  $nameBytes=[Text.Encoding]::UTF8.GetBytes($relative)
  $data=[IO.File]::ReadAllBytes($componentFile.FullName)
  $bundleWriter.Write([UInt32]$nameBytes.Length);$bundleWriter.Write($nameBytes);$bundleWriter.Write([UInt64]$data.LongLength);$bundleWriter.Write($data)
}
$bundleWriter.Dispose();$bundleStream.Dispose()
$resourceFile=Join-Path $obj 'embedded_components.generated.rc'
[IO.File]::WriteAllText($resourceFile,('100 RCDATA "'+$bundlePath.Replace('\','\\')+'"'),[Text.UTF8Encoding]::new($false))
$cmd=@"
@echo off
chcp 65001 >nul
call "$BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "$src"
cd /d "$src\..\third_party\quickjs-ng"
cl /nologo /TC /std:c11 /experimental:c11atomics /MT /O2 /DNDEBUG /D_GNU_SOURCE /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /c cutils.c /Fo"$obj\qjs_cutils.obj"
cl /nologo /TC /std:c11 /experimental:c11atomics /MT /O2 /DNDEBUG /D_GNU_SOURCE /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /c libregexp.c /Fo"$obj\qjs_libregexp.obj"
cl /nologo /TC /std:c11 /experimental:c11atomics /MT /O2 /DNDEBUG /D_GNU_SOURCE /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /c libunicode.c /Fo"$obj\qjs_libunicode.obj"
cl /nologo /TC /std:c11 /experimental:c11atomics /MT /O2 /DNDEBUG /D_GNU_SOURCE /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /c quickjs.c /Fo"$obj\qjs_quickjs.obj"
cl /nologo /TC /std:c11 /experimental:c11atomics /MT /O2 /DNDEBUG /D_GNU_SOURCE /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /c xsum.c /Fo"$obj\qjs_xsum.obj"
lib /nologo /OUT:"$obj\qjs.lib" "$obj\qjs_cutils.obj" "$obj\qjs_libregexp.obj" "$obj\qjs_libunicode.obj" "$obj\qjs_quickjs.obj" "$obj\qjs_xsum.obj"
cd /d "$src"
cl /nologo /TC /MT /O2 /DMINIZ_NO_STDIO /DMINIZ_NO_TIME /DMINIZ_NO_DEFLATE_APIS /DMINIZ_NO_ZLIB_COMPATIBLE_NAMES /Fo"$obj\\" /c ..\third_party\miniz\miniz.c ..\third_party\miniz\miniz_tinfl.c ..\third_party\miniz\miniz_zip.c
if errorlevel 1 exit /b 1
rc /nologo /c 65001 /i "$obj" /fo "$obj\app.res" app.rc
if errorlevel 1 exit /b 1
cl /nologo /MP /std:c++20 /EHsc /MT /O2 /Gy /Gw /utf-8 /permissive- /Zc:__cplusplus /W3 /DUNICODE /D_UNICODE /Fo"$obj\\" /Fe"$OutputDirectory\WinIsland-1.2.7alpha.exe" core.cpp monitor.cpp media.cpp music_sources.cpp telemetry.cpp audio.cpp notifications.cpp qq_notifications.cpp render.cpp accessibility.cpp animation_runtime.cpp embedded_components.cpp mod_system.cpp scene_store.cpp mod_package.cpp mod_dependency.cpp mod_ui.cpp mod_tests.cpp settings_bridge.cpp app.cpp tests.cpp "$obj\miniz.obj" "$obj\miniz_tinfl.obj" "$obj\miniz_zip.obj" "$obj\app.res" "$obj\qjs.lib" /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib runtimeobject.lib windowsapp.lib d3d11.lib d2d1.lib dwrite.lib dxgi.lib dcomp.lib windowscodecs.lib dwmapi.lib shcore.lib wtsapi32.lib bcrypt.lib advapi32.lib xmllite.lib psapi.lib mmdevapi.lib crypt32.lib comctl32.lib uxtheme.lib oleacc.lib uiautomationcore.lib gdiplus.lib winhttp.lib iphlpapi.lib ws2_32.lib
$testBuild
exit /b %errorlevel%
"@
$script=Join-Path $obj 'build.cmd';[IO.File]::WriteAllText($script,$cmd.Replace("
","`n").Replace("`n","
"),[Text.UTF8Encoding]::new($false));& cmd.exe /d /c $script
if($LASTEXITCODE -ne 0){throw 'Native compilation failed'}
Get-Item -LiteralPath (Join-Path $OutputDirectory 'WinIsland-1.2.7alpha.exe')|Select-Object FullName,Length










