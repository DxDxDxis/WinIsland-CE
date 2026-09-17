$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe='E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.6alpha-c2-no-motion\source\build-no-motion\WinIsland-1.2.6alpha-c2.exe'
$run=Join-Path $root ('verification/exe-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
$results=[Collections.Generic.List[string]]::new()
function Check($ok,$label){$results.Add("$(if($ok){'PASS'}else{'FAIL'}) $label");if(!$ok){throw $label}}
function WaitFor([scriptblock]$test){$end=[DateTime]::UtcNow.AddSeconds(12);while([DateTime]::UtcNow -lt $end){if(&$test){return};Start-Sleep -Milliseconds 100};throw 'Timeout'}
function Command($value){[IO.File]::WriteAllText("$script:dir/command.txt",$value,[Text.UTF8Encoding]::new($false));WaitFor {!(Test-Path "$script:dir/command.txt")};Start-Sleep -Milliseconds 600}
function State(){Get-Content "$script:dir/state.txt" -Raw | ConvertFrom-StringData}
$sizes=@{}
try {
 foreach($mode in @('baseline','plugin')){
  $script:dir="$run/$mode";New-Item -ItemType Directory "$dir/mods","$dir/local" | Out-Null
  if($mode -eq 'plugin'){Copy-Item "$root/time-display.wimod" "$dir/mods/time-display.wimod"}
  $info=[Diagnostics.ProcessStartInfo]::new($exe);$info.UseShellExecute=$false;$info.WorkingDirectory=$dir
  $info.ArgumentList.Add('--verify');$info.ArgumentList.Add($dir);$info.Environment['LOCALAPPDATA']="$dir/local"
  $p=[Diagnostics.Process]::Start($info)
  try {
   WaitFor {Test-Path "$dir/state.txt"}
   Command 'music-stop';WaitFor {(State).Animating -eq 'False'}
   $s=State;$sizes[$mode]="$($s.Width)x$($s.Height)";Copy-Item "$dir/state.txt" "$dir/idle.txt"
   Check ($s.Build -eq '1.2.6alpha-c2-no-motion-20260913') "$mode actual EXE build id"
   if($mode -eq 'plugin'){
    $log="$dir/mods/time-display/mod.log"
    WaitFor {(Get-Content $log -Raw) -match '时间显示插件已启用'}
    Check ($true) 'actual EXE scans, loads and enables package'
    Command 'capture';Copy-Item "$dir/capture.png" "$dir/idle-hidden.png"
    Command 'music=play';WaitFor {(State).HasMusic -eq 'True'}
    WaitFor {(Get-Content $log -Raw) -match '收到音乐信息事件'}
    Check ($true) 'actual music event reaches DLL; no visual resource added'
    Command 'notice-short';WaitFor {(Get-Content $log -Raw) -match '收到通知事件'}
    Check ($true) 'actual notice event reaches DLL; no visual resource added'
    Command 'capture';Copy-Item "$dir/capture.png" "$dir/music-notice-hidden.png"
    Command 'music-stop';Command 'settings-page=4';WaitFor {(State).SettingsPage -eq '4'}
    Check ($true) 'settings remains usable with plugin'
    Command 'close-settings';WaitFor {(State).SettingsWindow -eq '0'}
   }
  } finally {
   [IO.File]::WriteAllText("$dir/exit.request",'exit')
   Check ($p.WaitForExit(12000)) "$mode normal host exit"
   Check ($p.ExitCode -eq 0) "$mode exit code zero"
  }
 }
 Check ($sizes.baseline -eq $sizes.plugin) "actual idle dimensions identical: $($sizes.plugin)"
 $log=Get-Content "$run/plugin/mods/time-display/mod.log" -Raw
 Check ($log.Contains('时间显示插件已禁用') -and $log.Contains('时间显示插件已卸载') -and $log.Contains('DLL 卸载')) 'actual shutdown disables, unloads, FreeLibrary'
 Check ($log -notmatch '注册资源 kind=[234] target=') 'actual EXE never registers visual/replacement resources'
} finally {
 $results | Set-Content "$run/results.txt" -Encoding utf8
 $results
 Write-Output $run
}
