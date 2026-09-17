param([switch]$SkipNative)
$ErrorActionPreference='Stop'
$env:npm_config_cache=Join-Path $PSScriptRoot '../dependency-cache/npm'
$env:electron_config_cache=Join-Path $PSScriptRoot '../dependency-cache/electron'
New-Item -ItemType Directory -Force "$PSScriptRoot/verification","$PSScriptRoot/release" | Out-Null
if(!$SkipNative){& "$PSScriptRoot/source/build.ps1" -OutputDirectory "$PSScriptRoot/build-electron" *> "$PSScriptRoot/verification/build-native.log"}
Push-Location "$PSScriptRoot/settings-electron"
try{
  & npm.cmd ci; if($LASTEXITCODE){throw 'npm ci failed'}
  & npm.cmd run build; if($LASTEXITCODE){throw 'TypeScript build failed'}
  & node package.cjs; if($LASTEXITCODE){throw 'Electron packaging failed'}
}finally{Pop-Location}
Copy-Item -LiteralPath "$PSScriptRoot/build-electron/WinIsland-1.2.7alpha.exe" -Destination "$PSScriptRoot/release/WinIsland-1.2.7alpha.exe" -Force
New-Item -ItemType Directory -Force "$PSScriptRoot/release/mods" | Out-Null
New-Item -ItemType Directory -Force "$PSScriptRoot/release/licenses" | Out-Null
Copy-Item "$PSScriptRoot/source/third_party/quickjs-ng/LICENSE" "$PSScriptRoot/release/licenses/QuickJS-NG.txt" -Force
Copy-Item "$PSScriptRoot/source/third_party/miniz/LICENSE" "$PSScriptRoot/release/licenses/miniz.txt" -Force
Copy-Item "$PSScriptRoot/source/third_party/gsap/package/README.md" "$PSScriptRoot/release/licenses/GSAP-README.md" -Force
Get-FileHash -Algorithm SHA256 "$PSScriptRoot/release/WinIsland-1.2.7alpha.exe","$PSScriptRoot/release/settings/WinIslandSettings.exe" | ConvertTo-Json | Set-Content "$PSScriptRoot/verification/release-hashes.json"

