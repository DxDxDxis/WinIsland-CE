param([string]$Version)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$preserved=Join-Path $root 'settings'
if((Get-Content -LiteralPath "$preserved/version" -Raw).Trim() -ne $Version){throw 'Preserved Electron runtime version mismatch'}
$runtime=Join-Path $root 'build-electron/runtime'
New-Item -ItemType Directory -Force $runtime | Out-Null
Get-ChildItem -LiteralPath $preserved | Where-Object {$_.Name -notin @('resources','debug.log','WinIslandSettings.exe')} | Copy-Item -Destination $runtime -Recurse -Force
Copy-Item -LiteralPath "$preserved/WinIslandSettings.exe" -Destination "$runtime/electron.exe" -Force
$zip=Join-Path $root "dependency-cache/electron/electron-v$Version-win32-x64.zip"
New-Item -ItemType Directory -Force (Split-Path $zip) | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
if(!(Test-Path -LiteralPath $zip)){[IO.Compression.ZipFile]::CreateFromDirectory($runtime,$zip,[IO.Compression.CompressionLevel]::Fastest,$false)}
$stream=[IO.File]::OpenRead($zip);$sha=[Security.Cryptography.SHA256]::Create()
try{$digest=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}
@{Path=$zip;SHA256=$digest;Source='Preserved and hash-inventoried r1 Electron runtime; application ASAR excluded'} | ConvertTo-Json | Set-Content -LiteralPath "$root/verification/electron-runtime-archive.json"
