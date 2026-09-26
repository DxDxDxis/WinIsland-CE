$ErrorActionPreference='Continue'
$root=(Get-Location).Path
$tr=Join-Path $root 'verification\inline-test'
if(Test-Path $tr){Remove-Item $tr -Recurse -Force}
New-Item $tr -ItemType Directory|Out-Null
$exe=(Get-ChildItem (Join-Path $root 'release') -Filter '*.exe'|Select-Object -First 1).FullName
$p=Start-Process -FilePath $exe -ArgumentList @('--verify-installed',$tr) -PassThru -WindowStyle Hidden
Start-Sleep 3
Get-Process -Id $p.Id | Select-Object Id,MainWindowHandle,MainWindowTitle,Responding
Get-Process | Where-Object {$_.ProcessName -match 'WinIsland|Settings'} | Select-Object Id,ProcessName,MainWindowHandle,MainWindowTitle,Path
Get-ChildItem -Recurse $tr -ErrorAction SilentlyContinue | Select-Object FullName,Length
if(!$p.HasExited){Stop-Process -Id $p.Id -Force}
