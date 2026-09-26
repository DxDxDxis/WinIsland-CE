$ErrorActionPreference='Stop'
$base=(Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$testRoot=Join-Path $base 'verification\quick-settings-test'
if(Test-Path $testRoot){Remove-Item $testRoot -Recurse -Force}
New-Item -ItemType Directory $testRoot|Out-Null
$exe=Join-Path $base 'release\WinIsland-1.3.3exp-winUI（实验性）.exe'
$p=Write-Output "EXE=$exe TEST=$testRoot"; Start-Process -FilePath $exe -ArgumentList @('--verify-installed',$testRoot) -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
Write-Output "host=$($p.Id) exited=$($p.HasExited)"
Get-Process | Where-Object {$_.ProcessName -match 'WinIsland|Settings'} | Select-Object Id,ProcessName,MainWindowHandle,MainWindowTitle,Path
Get-ChildItem -Recurse -LiteralPath $testRoot -ErrorAction SilentlyContinue | Select-Object FullName,Length
if($p.HasExited){throw "host exited $($p.ExitCode)"}
$children=Get-CimInstance Win32_Process | Where-Object {$_.ParentProcessId -eq $p.Id};$children|Select-Object Name,ProcessId,CommandLine
Start-Sleep -Seconds 1
$p|Stop-Process -Force



