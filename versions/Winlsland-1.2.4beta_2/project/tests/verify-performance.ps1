param([string]$Executable,[string]$OutputDirectory,[switch]$AssertBudget,[int]$SoakSeconds=0,[switch]$Monitor)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
function Send([string]$c) { $file=Join-Path $OutputDirectory 'command.txt';[IO.File]::WriteAllText(($file+'.tmp'),$c);Move-Item -LiteralPath ($file+'.tmp') -Destination $file -Force;$w=[Diagnostics.Stopwatch]::StartNew();while(Test-Path -LiteralPath $file){if($w.Elapsed.TotalSeconds -gt 15){throw 'UI command blocked for 15 seconds'};Start-Sleep -Milliseconds 20} }
function State {try {$s=@{};foreach($l in [IO.File]::ReadAllLines((Join-Path $OutputDirectory 'state.txt'))){$p=$l -split '=',2;if($p.Length -eq 2){$s[$p[0]]=$p[1]}};return $s}catch{return @{}}}
function Wait-Idle { $w=[Diagnostics.Stopwatch]::StartNew(); do{ $s=State; if($s.Idle -eq 'True'){return};Start-Sleep -Milliseconds 50}while($w.Elapsed.TotalSeconds -lt 10);throw 'Animation did not settle' }
$old=$env:WINISLAND_PERF_DIR;$env:WINISLAND_PERF_DIR=Join-Path $OutputDirectory 'metrics';$app=$null;$startup=[Diagnostics.Stopwatch]::StartNew()
try {
 $app=Start-Process -FilePath $Executable -ArgumentList @('--verify',('"'+$OutputDirectory+'"')) -WindowStyle Hidden -PassThru
 while(!(Test-Path (Join-Path $OutputDirectory 'ready.txt'))){if($startup.Elapsed.TotalSeconds -gt 15){throw 'Startup timeout'};Start-Sleep -Milliseconds 20}
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'startup-ms.txt'),[string]$startup.ElapsedMilliseconds)
 if($Monitor){Send 'monitor-start'}
 Send 'music-stop';Wait-Idle;Send 'phase=idle';Start-Sleep -Seconds 3
 Send 'music=play';Send 'phase=music';Wait-Idle;Start-Sleep -Seconds 3
 Send 'seconds=0.2';Send 'close-settings';Send 'phase=lyrics';Send 'perf-lyrics';Start-Sleep -Seconds 1
 Send 'phase=transitions'
 for($i=0;$i -lt 24;$i++){Send $(if($i%2 -eq 0){'music-expand'}else{'music-collapse'});Send 'perf-poll';Start-Sleep -Milliseconds 120}
 Wait-Idle;Send 'phase=messages';Send 'burst'
 for($i=0;$i -lt 28;$i++){Send 'perf-poll';Start-Sleep -Milliseconds 350}
 Wait-Idle;Send 'phase=resume';Send 'music=pause';Start-Sleep -Milliseconds 200;Send 'music=play';Wait-Idle
 if($SoakSeconds -gt 0){Send 'phase=soak';for($i=0;$i -lt $SoakSeconds;$i++){if($i%10 -eq 0){Send $(if($i%20 -eq 0){'music-expand'}else{'music-collapse'})};Start-Sleep -Seconds 1}}
 Send 'phase=finished';Send 'perf-flush';Start-Sleep -Milliseconds 500
 if($Monitor){Send 'monitor-stop';Start-Sleep -Milliseconds 250;Send 'monitor-export';Start-Sleep -Milliseconds 500}
} finally { if($app -and !$app.HasExited){[IO.File]::WriteAllText((Join-Path $OutputDirectory 'exit.request'),'exit');$null=$app.WaitForExit(5000)};$env:WINISLAND_PERF_DIR=$old }
$lines=Get-Content -LiteralPath (Join-Path $OutputDirectory 'metrics\latest.csv')
$stats=$lines[2..($lines.Length-1)]|ConvertFrom-Csv
$stats | Format-Table
if($AssertBudget){
 $lyric=$stats|Where-Object part -eq 'LyricMeasure'
 if(!$lyric -or [long]$lyric.count -ne 1){throw 'Expected exactly one background lyric measurement'}
 foreach($part in @('UiWork','LayoutWork')){
  $metric=$stats|Where-Object part -eq $part
  if(!$metric -or [long]$metric.count -eq 0){throw ('Missing native UI metric: '+$part)}
  if([double]$metric.max_ms -gt 50){throw ('UI blocking budget exceeded: '+$part+' '+$metric.max_ms+'ms')}
 }
 Write-Output 'PASS: native UI layout budget and single background lyric measurement'
}
