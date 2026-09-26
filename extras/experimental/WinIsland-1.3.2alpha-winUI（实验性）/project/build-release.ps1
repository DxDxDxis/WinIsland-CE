param([switch]$SkipNative,[string]$BuildTools)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force "$PSScriptRoot/verification","$PSScriptRoot/release" | Out-Null

# The distributable is a single native EXE.  The WinUI settings client and its
# app-local Windows App SDK files are embedded into the native resource bundle
# by source/build.ps1; they are extracted into the selected data root on first
# run.  Electron remains in settings-electron as a rollback source only and is
# deliberately not built or copied into the release package.
$settingsBuild = Join-Path $PSScriptRoot 'settings-winui/build.ps1'
$settingsOutput = Join-Path $PSScriptRoot 'settings-winui/build/Release'
$releaseSettings = Join-Path $PSScriptRoot 'release/settings'
if(Test-Path -LiteralPath $releaseSettings){Remove-Item -LiteralPath $releaseSettings -Recurse -Force}
New-Item -ItemType Directory -Force $releaseSettings | Out-Null
$settingsParams=@{Configuration='Release'}
if($BuildTools){$settingsParams.BuildTools=$BuildTools}
& $settingsBuild @settingsParams *> "$PSScriptRoot/verification/build-settings-winui.log"
if($LASTEXITCODE){throw 'WinUI 3 settings build failed'}
$settingsExe=Join-Path $settingsOutput 'WinIslandSettings.exe'
if(!(Test-Path -LiteralPath $settingsExe)){throw 'WinUI settings executable was not produced'}
Copy-Item -LiteralPath $settingsExe -Destination (Join-Path $releaseSettings 'WinIslandSettings.exe') -Force

# Windows DLL search resolves app-local Windows App SDK dependencies beside the
# launched executable.  Keep the self-contained WinUI runtime files in the same
# directory as WinIslandSettings.exe; placing them in a child directory makes
# Microsoft.WindowsAppRuntime.dll invisible to the loader after extraction.
$settingsRoot = [IO.Path]::GetFullPath($settingsOutput)
foreach($file in (Get-ChildItem -LiteralPath $settingsRoot -File -Recurse)){
  $relative=[IO.Path]::GetRelativePath($settingsRoot,$file.FullName)
  if($relative -like 'obj\*' -or $relative -like 'MSIX\*' -or $relative -eq 'WinIslandSettings.exe'){continue}
  $target=Join-Path $releaseSettings $relative
  New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null
  Copy-Item -LiteralPath $file.FullName -Destination $target -Force
}

New-Item -ItemType Directory -Force "$PSScriptRoot/release/mods" | Out-Null
New-Item -ItemType Directory -Force "$PSScriptRoot/release/licenses" | Out-Null
Copy-Item "$PSScriptRoot/source/third_party/quickjs-ng/LICENSE" "$PSScriptRoot/release/licenses/QuickJS-NG.txt" -Force
Copy-Item "$PSScriptRoot/source/third_party/miniz/LICENSE" "$PSScriptRoot/release/licenses/miniz.txt" -Force
Copy-Item "$PSScriptRoot/source/third_party/gsap/package/README.md" "$PSScriptRoot/release/licenses/GSAP-README.md" -Force
if(!$SkipNative){
  $nativeParams=@{OutputDirectory="$PSScriptRoot/build-1.3.2alpha"}
  if($BuildTools){$nativeParams.BuildTools=$BuildTools}
  & "$PSScriptRoot/source/build.ps1" @nativeParams *> "$PSScriptRoot/verification/build-native.log"
  if($LASTEXITCODE){throw 'Native build failed'}
}
Copy-Item -LiteralPath "$PSScriptRoot/build-1.3.2alpha/WinIsland-1.3.2alpha.exe" -Destination "$PSScriptRoot/release/WinIsland-1.3.2alpha.exe" -Force
# Keep the native build directory directly runnable as well as the packaged release.
# The host resolves WinIslandSettings.exe relative to its own directory when no
# installed data root has been selected yet, so the WinUI client and its runtime
# must sit beside the developer build too.
$buildSettings = Join-Path $PSScriptRoot 'build-1.3.2alpha/settings'
if(!(Test-Path -LiteralPath "$PSScriptRoot/release/settings/WinIslandSettings.exe")){
  throw 'Native WinUI settings client is missing after the settings build.'
}
if(Test-Path -LiteralPath $buildSettings){Remove-Item -LiteralPath $buildSettings -Recurse -Force}
New-Item -ItemType Directory -Force $buildSettings | Out-Null
Get-ChildItem -LiteralPath "$PSScriptRoot/release/settings" -Force | ForEach-Object {
  Copy-Item -LiteralPath $_.FullName -Destination $buildSettings -Recurse -Force
}
Get-FileHash -Algorithm SHA256 "$PSScriptRoot/release/WinIsland-1.3.2alpha.exe","$PSScriptRoot/release/settings/WinIslandSettings.exe" | ConvertTo-Json | Set-Content "$PSScriptRoot/verification/release-hashes.json"

