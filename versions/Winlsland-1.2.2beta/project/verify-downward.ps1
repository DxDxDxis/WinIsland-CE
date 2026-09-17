param([string]$Executable, [string]$OutputDirectory)
$ErrorActionPreference='Stop'; New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
function State { try { $h=@{}; foreach($l in [IO.File]::ReadAllLines((Join-Path $OutputDirectory 'state.txt'))){$p=$l -split '=',2;if($p.Count -eq 2){$h[$p[0]]=$p[1]}};return $h } catch { return @{} } }
function Wait([scriptblock]$p,[int]$ms=7000){$w=[Diagnostics.Stopwatch]::StartNew();do{$s=State;if(& $p $s){return $s};Start-Sleep -Milliseconds 60}while($w.ElapsedMilliseconds -lt $ms);throw "timeout $($s|ConvertTo-Json -Compress)"}
function Send([string]$c){$p=Join-Path $OutputDirectory 'command.txt';[IO.File]::WriteAllText(($p+'.tmp'),$c);Move-Item -LiteralPath ($p+'.tmp') -Destination $p -Force;while(Test-Path $p){Start-Sleep -Milliseconds 30}}
$checks=[Collections.Generic.List[string]]::new();function Ok([bool]$v,[string]$m){if(!$v){throw "FAIL $m"};$checks.Add("PASS $m")}
$app=Start-Process -FilePath $Executable -ArgumentList '--verify',$OutputDirectory -WindowStyle Hidden -PassThru
try {
 Wait {param($s)$s.HelperId -gt 0}|Out-Null; Send 'music-stop'; $s=Wait {param($s)$s.HasMusic -eq 'False' -and $s.Idle -eq 'True'}; Ok ([math]::Abs([double]$s.Width-180.4)-lt .02 -and [math]::Abs([double]$s.Height-29.92)-lt .02) 'idle dimensions enlarged 10 percent'
 Send 'music=play'; $s=Wait {param($s)$s.HasMusic -eq 'True' -and $s.Idle -eq 'True'}; Ok ([math]::Abs([double]$s.MusicWidth-460.46)-lt .02 -and [math]::Abs([double]$s.MusicHeight-48.384)-lt .02) 'compact music dimensions enlarged'
 Send 'notice-long'; $s=Wait {param($s)$s.Holding -eq 'True'}; Ok ([double]$s.MusicTop -eq 0 -and [double]$s.NoticeTop -eq [double]$s.MusicHeight -and [double]$s.Height -gt [double]$s.MusicHeight) 'message is below music in one shell'; $base=[double]$s.MusicWidth
 Send 'music-expand'; $s=Wait {param($s)$s.MusicExpanded -eq 'True' -and $s.Holding -eq 'True' -and $s.Animating -eq 'False'}; Ok ([double]$s.NoticeTop -eq [double]$s.MusicHeight -and [double]$s.MusicHeight -ge 121.7) 'expanded panel keeps message attached below'
 Send 'seconds=0.1'; Send 'burst'; $s=Wait {param($s)[int]$s.SyntheticDelivered -eq 12 -and $s.Pending -eq 0 -and $s.Holding -eq 'True'} 16000; Ok ($s.MusicExpanded -eq 'True' -and [double]$s.NoticeTop -eq [double]$s.MusicHeight) 'consecutive messages drain without collapsing music'
 Send 'seconds=0.1'; Start-Sleep -Milliseconds 400; Ok ((State).Holding -eq 'False' -or (State).Pending -ge 0) 'message dismissal leaves a valid shared layout'
 Send 'music-stop'; $s=Wait {param($s)$s.HasMusic -eq 'False' -and $s.Idle -eq 'True'}; Ok ([math]::Abs([double]$s.Width-180.4)-lt .02 -and [math]::Abs([double]$s.Height-29.92)-lt .02) 'music exit returns to idle pill'
} finally { [IO.File]::WriteAllText((Join-Path $OutputDirectory 'exit.request'),'exit');$null=$app.WaitForExit(5000); [IO.File]::WriteAllLines((Join-Path $OutputDirectory 'verification.txt'),$checks,[Text.Encoding]::UTF8) }
$checks
