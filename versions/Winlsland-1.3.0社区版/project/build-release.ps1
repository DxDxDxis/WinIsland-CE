param([switch]$SkipNative,[string]$BuildTools)
$ErrorActionPreference='Stop'
$env:npm_config_cache=Join-Path $PSScriptRoot 'dependency-cache/npm'
$env:electron_config_cache=Join-Path $PSScriptRoot 'dependency-cache/electron'
New-Item -ItemType Directory -Force "$PSScriptRoot/verification","$PSScriptRoot/release" | Out-Null
$env:ELECTRON_SKIP_BINARY_DOWNLOAD='1'
Push-Location "$PSScriptRoot/settings-electron"
try{
  & npm.cmd ci *> "$PSScriptRoot/verification/npm-ci.log"; if($LASTEXITCODE){throw 'npm ci failed'}
  & npm.cmd run build *> "$PSScriptRoot/verification/build-settings.log"; if($LASTEXITCODE){throw 'TypeScript build failed'}
  & node package.cjs *> "$PSScriptRoot/verification/package-settings.log"; if($LASTEXITCODE){throw 'Electron packaging failed'}
}finally{Pop-Location}

New-Item -ItemType Directory -Force "$PSScriptRoot/release/mods" | Out-Null
New-Item -ItemType Directory -Force "$PSScriptRoot/release/licenses" | Out-Null
Copy-Item "$PSScriptRoot/source/third_party/quickjs-ng/LICENSE" "$PSScriptRoot/release/licenses/QuickJS-NG.txt" -Force
Copy-Item "$PSScriptRoot/source/third_party/miniz/LICENSE" "$PSScriptRoot/release/licenses/miniz.txt" -Force
Copy-Item "$PSScriptRoot/source/third_party/gsap/package/README.md" "$PSScriptRoot/release/licenses/GSAP-README.md" -Force
if(!$SkipNative){& "$PSScriptRoot/source/build.ps1" -BuildTools $BuildTools -OutputDirectory "$PSScriptRoot/build-1.3.0" *> "$PSScriptRoot/verification/build-native.log"}
Copy-Item -LiteralPath "$PSScriptRoot/build-1.3.0/WinIsland-1.3.0.exe" -Destination "$PSScriptRoot/release/WinIsland-1.3.0.exe" -Force
Get-FileHash -Algorithm SHA256 "$PSScriptRoot/release/WinIsland-1.3.0.exe","$PSScriptRoot/release/settings/WinIslandSettings.exe" | ConvertTo-Json | Set-Content "$PSScriptRoot/verification/release-hashes.json"

