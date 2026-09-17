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
$app=$fixture=$null
try {
 $app=Start-Process -FilePath $Executable -ArgumentList @('--verify',('"'+$OutputDirectory+'"')) -WindowStyle Hidden -PassThru
 $null=Wait-State {param($s) $s.MusicWindow}
 Send 'media-fixture-only'
 $fixture=Start-Process -FilePath $Executable -ArgumentList @('--media-fixture',('"'+$OutputDirectory+'"')) -WindowStyle Hidden -PassThru
 $null=Wait-State {param($s) $s.FixtureTitle -match '1$' -and $s.Idle -eq 'True'}
 Invoke-Music 'MusicHeader'
 $null=Wait-State {param($s) [double]$s.MusicHeight -eq 121.8 -and $s.Idle -eq 'True'}
 Capture 'mouse-focus'
 $s=Read-State
 python -c "import ctypes as c,sys; u=c.windll.user32; assert u.GetForegroundWindow()==int(sys.argv[1]); u.keybd_event(9,0,0,0); u.keybd_event(9,0,2,0)" $s.MusicWindow
 if($LASTEXITCODE -ne 0){throw 'Keyboard foreground changed'}
 Start-Sleep -Milliseconds 200
 $root=[Windows.Automation.AutomationElement]::FromHandle([IntPtr][long]$s.MusicWindow)
 $c=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,'MusicPrevious')
 $b=$root.FindFirst([Windows.Automation.TreeScope]::Descendants,$c)
 Check ($b.Current.HasKeyboardFocus) 'Tab moves keyboard focus into the real control group'
 Capture 'keyboard-focus'
 Send 'stop' 'media-command.txt'
 $null=Wait-State {param($s) $s.MusicPlaying -eq 'False'}
 Start-Sleep -Seconds 8
 Check ((Read-State).HasMusic -eq 'True') 'Stopped session remains for ten seconds like paused music'
 $null=Wait-State {param($s) $s.HasMusic -eq 'False' -and $s.Idle -eq 'True'}
 Check $true 'Stopped session retracts after the timeout'
} finally {
 if($fixture -and -not $fixture.HasExited){[IO.File]::WriteAllText((Join-Path $OutputDirectory 'media-command.txt'),'exit');$null=$fixture.WaitForExit(4000)}
 if($app -and -not $app.HasExited){[IO.File]::WriteAllText((Join-Path $OutputDirectory 'exit.request'),'exit');$null=$app.WaitForExit(4000)}
 [IO.File]::WriteAllLines((Join-Path $OutputDirectory 'verification.txt'),$checks,[Text.Encoding]::UTF8)
}
$checks
