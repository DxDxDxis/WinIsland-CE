param([string]$Executable, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$checks = [Collections.Generic.List[string]]::new()
function Check([bool]$ok, [string]$name) {
    if (-not $ok) { throw "FAIL: $name" }
    $checks.Add("PASS: $name")
}
function Read-State([string]$name = 'state.txt') {
    try {
        $s = @{}
        foreach ($line in [IO.File]::ReadAllLines((Join-Path $OutputDirectory $name))) {
            $p = $line -split '=', 2
            if ($p.Count -eq 2) { $s[$p[0]] = $p[1] }
        }
        return $s
    } catch [IO.IOException] { return @{} }
}
function Wait-State([scriptblock]$test, [string]$name = 'state.txt') {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    do {
        $s = Read-State $name
        if (& $test $s) { return $s }
        Start-Sleep -Milliseconds 50
    } while ($clock.Elapsed.TotalSeconds -lt 9)
    throw "Timeout: $name $($s | ConvertTo-Json -Compress)"
}
function Send([string]$text, [string]$name = 'command.txt') {
    $path = Join-Path $OutputDirectory $name
    [IO.File]::WriteAllText(($path + '.tmp'), $text)
    Move-Item -LiteralPath ($path + '.tmp') -Destination $path -Force
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while (Test-Path -LiteralPath $path) {
        if ($clock.Elapsed.TotalSeconds -gt 4) { throw 'Command not consumed' }
        Start-Sleep -Milliseconds 20
    }
}
function Invoke-Music([string]$id) {
    $s = Read-State
    $root = [Windows.Automation.AutomationElement]::FromHandle([IntPtr][long]$s['MusicWindow'])
    $condition = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty, $id)
    $button = $root.FindFirst([Windows.Automation.TreeScope]::Descendants, $condition)
    if (-not $button -or $button.Current.IsOffscreen) { throw "Button missing: $id" }
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while (-not $button.Current.IsEnabled) {
        if ($clock.Elapsed.TotalSeconds -gt 5) { throw "Button busy: $id" }
        Start-Sleep -Milliseconds 100
    }
    $bounds = $button.Current.BoundingRectangle; $win = $root.Current.BoundingRectangle; python (Join-Path $PSScriptRoot 'artifacts\diagnostics\click-window.py') ([long]$s['MusicWindow']) ([int]($bounds.X - $win.X + $bounds.Width / 2)) ([int]($bounds.Y - $win.Y + $bounds.Height / 2)); if ($LASTEXITCODE -ne 0) { throw 'Pointer click failed' }
}
function Capture([string]$name) {
    $capture = Join-Path $OutputDirectory 'capture.png'
    if (Test-Path -LiteralPath $capture) { Remove-Item -LiteralPath $capture }
    Send 'capture'
    $clock = [Diagnostics.Stopwatch]::StartNew()
    do {
        try {
            # Command consumption precedes PNG encoding; wait for the newly created file to close.
            $bytes = [IO.File]::ReadAllBytes($capture)
            [IO.File]::WriteAllBytes((Join-Path $OutputDirectory ($name + '.png')), $bytes)
            return
        } catch [IO.IOException] { Start-Sleep -Milliseconds 20 }
    } while ($clock.Elapsed.TotalSeconds -lt 3)
    throw 'Screenshot encoding did not finish'
}
$app = $fixture = $null
try {
    $app=Start-Process -FilePath $Executable -ArgumentList @('--verify',('"'+$OutputDirectory+'"')) -WindowStyle Hidden -PassThru
    $null=Wait-State {param($s) $s.MusicWindow}
    Send 'music-stop'
    $null=Wait-State {param($s) $s.Idle -eq 'True' -and $s.HasMusic -eq 'False'}
    Capture 'idle'
    Send 'media-fixture-only'
    $fixture=Start-Process -FilePath $Executable -ArgumentList @('--media-fixture',('"'+$OutputDirectory+'"')) -WindowStyle Hidden -PassThru
    $s=Wait-State {param($s) $s.HasMusic -eq 'True' -and $s.FixtureTitle -match '1$' -and $s.Idle -eq 'True' -and [int]$s.MusicControls -eq 3}
    Check ([Math]::Abs([double]$s.MusicHeight-48.384) -lt .01 -and [Math]::Abs([double]$s.MusicWidth-460.46) -lt .01) 'Compact music is 105% height and 110% width of the starting dimensions'
    Capture 'compact'
    $s=Wait-State {param($s) $s.AudioAvailable -eq 'True'}
    Check ([double]$s.AudioPeak -lt .000001) 'Silent fixture has no activity even while another real player is playing'
    Send 'audio-on' 'media-command.txt'
    $null=Wait-State {param($s) [double]$s.AudioPeak -gt .000001}
    $levels=@(); for($i=0;$i -lt 25;$i++){ $levels += [double](Read-State).AudioPeak; Start-Sleep -Milliseconds 160 }
    $range=$levels|Measure-Object -Minimum -Maximum
    Check (($range.Maximum-$range.Minimum) -gt .00001) 'Six-bar input follows changing real PCM amplitude, not playback state'
    Send 'mute' 'media-command.txt'
    $s=Wait-State {param($s) [double]$s.AudioPeak -lt .000001 -and [double]$s.Bars -lt .09}
    Check ($s.MusicPlaying -eq 'True') 'Muted audio falls to zero even while metadata still says Playing'
    Send 'unmute' 'media-command.txt'
    $null=Wait-State {param($s) [double]$s.AudioPeak -gt .000001}
    Invoke-Music 'MusicHeader'
    $s=Wait-State {param($s) $s.Idle -eq 'True' -and [Math]::Abs([double]$s.MusicHeight-121.8) -lt .01}
    Check $true 'Physical header click expands into 121.8px panel'
    Capture 'expanded-pointer'
    Invoke-Music 'MusicToggle'
    $null=Wait-State {param($s) $s.Playing -eq 'False'} 'media-state.txt'
    $null=Wait-State {param($s) $s.MusicPlaying -eq 'False'}
    Check $true 'Physical pause button changes actual media state'
    Invoke-Music 'MusicToggle'
    $null=Wait-State {param($s) $s.Playing -eq 'True'} 'media-state.txt'
    $null=Wait-State {param($s) $s.MusicPlaying -eq 'True'}
    Check $true 'Physical resume button changes actual media state'
    Invoke-Music 'MusicNext'
    $null=Wait-State {param($s) $s.Track -eq '2'} 'media-state.txt'
    $null=Wait-State {param($s) $s.FixtureTitle -match '2$'}
    Check $true 'Physical next button changes current producer track and UI'
    Invoke-Music 'MusicPrevious'
    $null=Wait-State {param($s) $s.Track -eq '1'} 'media-state.txt'
    $null=Wait-State {param($s) $s.FixtureTitle -match '1$'}
    Check $true 'Physical previous button changes current producer track and UI'
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'fixture.lrc'), "[00:00.00]测试歌词：第一行`n[00:20.00]测试歌词：这一行用于验证较长歌词随实际播放进度切换，宽度按整首歌词预留且不抖动",[Text.Encoding]::UTF8)
    Send 'lyrics-fixture'
    Send 'seek=0' 'media-command.txt'
    $s=Wait-State {param($s) $s.FixtureLyric -eq '测试歌词：第一行' -and $s.Idle -eq 'True'}
    $width=[double]$s.MusicWidth
    Check ($width -ge 460.46 -and $width -le 616) 'LRC import reserves bounded width based on the real lyric text'
    Send 'seek=20' 'media-command.txt'
    $s=Wait-State {param($s) $s.FixtureLyric -match '较长歌词' -and $s.Idle -eq 'True'}
    Check ([double]$s.MusicWidth -eq $width) 'Lyric switches using actual SMTC seek while width remains stable'
    Capture 'expanded-lyrics'
    Invoke-Music 'MusicHeader'
    $s=Wait-State {param($s) [Math]::Abs([double]$s.MusicHeight-48.384) -lt .01 -and $s.Idle -eq 'True'}
    Check ($s.HasLyric -eq 'True' -and [double]$s.MusicWidth -le 616) 'Compact view also contains the synchronized current lyric'
    Capture 'compact-lyrics'
    Send 'notice-long'
    $s=Wait-State {param($s) $s.Holding -eq 'True'}
    Check ([double]$s.MusicTop -eq 0 -and [double]$s.NoticeTop -eq [double]$s.MusicHeight -and $s.HasMusic -eq 'True') 'Wide lyric view stays above the message inside one shell'
    Capture 'message-lyrics'
    Send 'seconds=0.1'
    Send 'close-settings'
    $null=Wait-State {param($s) $s.Idle -eq 'True'}
    Invoke-Music 'MusicHeader'
    $null=Wait-State {param($s) [double]$s.MusicHeight -eq 121.8 -and $s.Idle -eq 'True'}
    Invoke-Music 'MusicNext'
    $s=Wait-State {param($s) $s.FixtureTitle -match '2$' -and $s.HasLyric -eq 'False' -and $s.Idle -eq 'True'}
    Check ([Math]::Abs([double]$s.MusicWidth-460.46) -lt .01) 'Track change clears old lyrics and restores base width'
    Send 'mode-on' 'media-command.txt'
    Start-Sleep -Milliseconds 800
    if((Read-State).ModeVisible -eq 'True') {
        Invoke-Music 'MusicMode'
        $null=Wait-State {param($s) $s.Repeat -eq 'List'} 'media-state.txt'
        $null=Wait-State {param($s) $s.Mode -eq '列表循环'}
        Invoke-Music 'MusicMode'
        $null=Wait-State {param($s) $s.Repeat -eq 'Track'} 'media-state.txt'
        $null=Wait-State {param($s) $s.Mode -eq '单曲循环'}
        Invoke-Music 'MusicMode'
        $null=Wait-State {param($s) $s.Shuffle -eq 'True'} 'media-state.txt'
        $null=Wait-State {param($s) $s.Mode -eq '随机播放'}
        Invoke-Music 'MusicMode'
        $null=Wait-State {param($s) $s.Repeat -eq 'None' -and $s.Shuffle -eq 'False'} 'media-state.txt'
        $null=Wait-State {param($s) $s.Mode -eq '顺序播放'}
        Check $true 'Physical mode button cycles four real producer modes with readback'
    } else { $checks.Add('LIMIT: This Windows fixture does not advertise mode writes; entry remains hidden') }
    Send 'ignore-next' 'media-command.txt'
    Invoke-Music 'MusicNext'
    $null=Wait-State {param($s) [int]$s.MusicControls -eq 2}
    Check $true 'Unacknowledged command hides its button'
    Send 'pause' 'media-command.txt'
    $null=Wait-State {param($s) $s.MusicPlaying -eq 'False'}
    $watch=[Diagnostics.Stopwatch]::StartNew()
    Start-Sleep -Seconds 8
    Check ((Read-State).HasMusic -eq 'True') 'Paused music remains visible before ten-second timeout'
    Send 'seconds=2'
    Send 'close-settings'
    Send 'notice-long'
    $null=Wait-State {param($s) $s.Holding -eq 'True'}
    $s=Wait-State {param($s) $s.HasMusic -eq 'False' -and $s.MusicVisible -eq 'True'}
    Check ($watch.Elapsed.TotalSeconds -ge 9.3 -and $s.Holding -eq 'True') 'Ten-second timeout starts a smooth exit without dismissing the message'
    $null=Wait-State {param($s) $s.Idle -eq 'True' -and $s.MusicVisible -eq 'False'}
    Check ([Math]::Abs([double](Read-State).Height-29.92) -lt .01) 'Timeout completes as the enlarged ordinary pill'
    Send 'lyrics-fixture'
    Check ((Read-State).HasMusic -eq 'False') 'Importing lyrics after inactivity timeout does not resurrect the music panel'
    Send 'play' 'media-command.txt'
    $null=Wait-State {param($s) $s.HasMusic -eq 'True' -and $s.Idle -eq 'True'}
    Check $true 'Playback after timeout expands the same pill again'
    Send 'music-stop'; Send 'music=play'
    $s=Wait-State {param($s) $s.HasMusic -eq 'True' -and $s.Idle -eq 'True'}
    Check ($s.MusicVisible -eq 'True') 'Interrupted exit has no late callback that hides restarted music'
    Send 'exit' 'media-command.txt'
} finally {
 if($fixture -and -not $fixture.HasExited){[IO.File]::WriteAllText((Join-Path $OutputDirectory 'media-command.txt'),'exit');$null=$fixture.WaitForExit(4000)}
 if($app -and -not $app.HasExited){[IO.File]::WriteAllText((Join-Path $OutputDirectory 'exit.request'),'exit');$null=$app.WaitForExit(5000)}
 [IO.File]::WriteAllLines((Join-Path $OutputDirectory 'verification.txt'),$checks,[Text.Encoding]::UTF8)
}
$checks
