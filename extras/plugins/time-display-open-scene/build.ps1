$ErrorActionPreference='Stop'; $root=$PSScriptRoot
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'; $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC x64 build tools required'}
New-Item -ItemType Directory -Force "$root/build","$root/package/bin" | Out-Null
$bat="@echo off`r`ncall `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul`r`ncl /nologo /std:c++20 /EHsc /MT /O2 /W3 /utf-8 /LD `"$root\src\time-display.cpp`" /Fo`"$root\build\time-display.obj`" /Fe`"$root\package\bin\time-display.dll`" /link /NOLOGO dwrite.lib`r`n"
[IO.File]::WriteAllText("$root/build/build.cmd",$bat); & cmd /d /c "$root/build/build.cmd"; if($LASTEXITCODE){throw 'build failed'}
Remove-Item "$root/package/bin/time-display.lib","$root/package/bin/time-display.exp" -ErrorAction SilentlyContinue
$out="$root/time-display.wimod"; if(Test-Path $out){Remove-Item $out}
$py=@' 
import zipfile, os, sys
root=sys.argv[1]; out=sys.argv[2]
with zipfile.ZipFile(out,"w",zipfile.ZIP_DEFLATED) as z:
    z.write(os.path.join(root,"mod.json"),"mod.json")
    z.write(os.path.join(root,"bin","time-display.dll"),"bin/time-display.dll")
    z.write(os.path.join(os.path.dirname(root),"README.md"),"README.md")
    z.write(os.path.join(os.path.dirname(root),"LICENSE"),"LICENSE")
'@
$pyFile="$root/build/package.py"; [IO.File]::WriteAllText($pyFile,$py); python $pyFile "$root/package" $out; if($LASTEXITCODE){throw 'package failed'}; Get-FileHash $out
