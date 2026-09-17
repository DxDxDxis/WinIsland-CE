param([string]$Executable, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$checks = [Collections.Generic.List[string]]::new()
function Check([bool]$ok, [string]$label) { if (!$ok) { throw "FAIL: $label" }; $checks.Add("PASS: $label") }
function State {
    try { $s=@{}; foreach($line in [IO.File]::ReadAllLines((Join-Path $OutputDirectory 'state.txt'))) { $p=$line -split '=',2; if($p.Count -eq 2){$s[$p[0]]=$p[1]} }; return $s } catch [IO.IOException] { return @{} }
}
function Wait-State([scriptblock]$predicate) {
    $watch=[Diagnostics.Stopwatch]::StartNew()
    do { $s=State; if (& $predicate $s) { return $s }; Start-Sleep -Milliseconds 50 } while($watch.Elapsed.TotalSeconds -lt 8)
    throw "State timeout: $($s|ConvertTo-Json -Compress)"
}
function Send([string]$command) {
    $path=Join-Path $OutputDirectory 'command.txt'
    [IO.File]::WriteAllText(($path+'.tmp'),$command); Move-Item -LiteralPath ($path+'.tmp') -Destination $path -Force
    $watch=[Diagnostics.Stopwatch]::StartNew()
    while(Test-Path -LiteralPath $path) { if($watch.Elapsed.TotalSeconds -gt 5){throw 'Command timeout'}; Start-Sleep -Milliseconds 30 }
}
function Find-Settings {
    $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty,$app.Id)
    $windows=[Windows.Automation.AutomationElement]::RootElement.FindAll([Windows.Automation.TreeScope]::Children,$condition)
    foreach($window in $windows){ if($window.Current.Name -eq 'WinIsland 设置'){ return $window } }; throw 'Settings not found'
}
function Field($window,[string]$id) { return $window.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$id)) }
function Stop-App {
    if($app -and !$app.HasExited){[IO.File]::WriteAllText((Join-Path $OutputDirectory 'exit.request'),'exit');if(!$app.WaitForExit(5000)){throw 'Exit timed out'}}
}
function Start-App {
    foreach($name in @('exit.request','state.txt')) { $path=Join-Path $OutputDirectory $name; if(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path} }
    $script:app=Start-Process -FilePath $Executable -ArgumentList @('--verify',('"'+$OutputDirectory+'"')) -WindowStyle Hidden -PassThru
    $null=Wait-State {param($s)$s.MusicWindow}
}
$originalLight=$env:WINISLAND_LIGHTWEIGHT; $app=$null
try {
    $env:WINISLAND_LIGHTWEIGHT='0'; Start-App
    $s=State; Check ($s.ConfiguredFrameRate -eq '0' -and $s.EffectiveFrameRate -eq '0') 'Default uses system compositor cadence with no app FPS cap on this machine'
    Send 'settings'; $settings=Find-Settings; $field=Field $settings 'FrameRate'
    Check ($field -and !$field.Current.IsOffscreen) 'Frame-rate input is visible in the tray settings window'
    $version=$settings.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'1.2.2beta'))
    Check ($null -ne $version) 'Settings shows version 1.2.2beta'
    $field.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('144')
    $s=Wait-State {param($s)$s.ConfiguredFrameRate -eq '144' -and $s.EffectiveFrameRate -eq '144'}
    Check ([math]::Abs([double]$s.FrameIntervalMs-(1000.0/144)) -lt .0001) 'Editing the visible field immediately selects exact 144 FPS timing'
    $field.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('999')
    Start-Sleep -Milliseconds 150
    Check ((State).ConfiguredFrameRate -eq '144') 'Invalid input preserves the active FPS'
    $field.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('120')
    $null=Wait-State {param($s)$s.ConfiguredFrameRate -eq '120'}
    Send 'capture-settings'
    [xml]$saved=Get-Content -LiteralPath (Join-Path $OutputDirectory 'settings.xml') -Raw
    Check ($saved.WinIsland.FrameRate -eq '120') 'Manual FPS is persisted to the existing configuration format'
    Stop-App; Start-App; $s=State
    Check ($s.ConfiguredFrameRate -eq '120' -and $s.EffectiveFrameRate -eq '120') 'Restart restores manual FPS'
    Send 'frame=0'; $null=Wait-State {param($s)$s.EffectiveFrameRate -eq '0'}
    Stop-App; $env:WINISLAND_LIGHTWEIGHT='1'; Start-App; $s=State
    Check ($s.LightweightRendering -eq 'True' -and $s.ConfiguredFrameRate -eq '0' -and $s.EffectiveFrameRate -eq '60') 'Forced low-performance profile defaults to 60 FPS, not 30'
    Send 'frame=144'; $null=Wait-State {param($s)$s.EffectiveFrameRate -eq '144'}
    Check $true 'A manual frame rate overrides the low-performance default'
    Send 'frame=60'; Send 'close-settings'; Send 'music=play'
    $null=Wait-State {param($s)$s.Idle -eq 'True' -and $s.HasMusic -eq 'True'}
    $start=State; $watch=[Diagnostics.Stopwatch]::StartNew(); Start-Sleep -Seconds 3; $end=State; $watch.Stop()
    $measured=([long]$end.MusicRenderFrames-[long]$start.MusicRenderFrames)/$watch.Elapsed.TotalSeconds
    Check ($measured -gt 5 -and $measured -lt 63) ('Active music updates at a bounded rate; measured callbacks/s='+[math]::Round($measured,1))
    Send 'notice-long'; $s=Wait-State {param($s)$s.Holding -eq 'True'}
    Check ($s.MusicTop -eq '0' -and [double]$s.NoticeTop -eq [double]$s.MusicHeight) 'Message animation completes below music at the selected FPS'
    Send 'frame=30'; Send 'music-expand'; $null=Wait-State {param($s)$s.MusicExpanded -eq 'True' -and $s.Animating -eq 'False'}
    Send 'frame=0'; Send 'music-collapse'; $null=Wait-State {param($s)$s.MusicExpanded -eq 'False' -and $s.Animating -eq 'False'}
    Check $true 'Changing FPS while music and messages are active does not stall the transition'
} finally {
    Stop-App; $env:WINISLAND_LIGHTWEIGHT=$originalLight
    [IO.File]::WriteAllLines((Join-Path $OutputDirectory 'verification.txt'),$checks,[Text.Encoding]::UTF8)
}
$checks
