param([string]$Executable, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$results = [Collections.Generic.List[string]]::new()
$runPath = 'Software\Microsoft\Windows\CurrentVersion\Run'
$runKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($runPath)
try { $startupBefore = if ($runKey) { $runKey.GetValue('WinIsland') } else { $null } } finally { if ($runKey) { $runKey.Dispose() } }
function Assert-Check([bool]$condition, [string]$label) {
    if (-not $condition) { throw "FAIL: $label" }
    $results.Add("PASS: $label")
}
function Read-State {
    try {
        $result = @{}
        foreach ($line in [IO.File]::ReadAllLines((Join-Path $OutputDirectory 'state.txt'))) {
            $pair = $line -split '=', 2
            if ($pair.Count -eq 2) { $result[$pair[0]] = $pair[1] }
        }
        return $result
    } catch [IO.IOException] { return @{} }
}
function Wait-State([scriptblock]$predicate, [int]$timeout = 6000) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    do {
        $state = Read-State
        if (& $predicate $state) { return $state }
        Start-Sleep -Milliseconds 60
    } while ($watch.ElapsedMilliseconds -lt $timeout)
    throw ('Timed out waiting for state: ' + ($state | ConvertTo-Json -Compress))
}
function Send-Command([string]$command) {
    $commandPath = Join-Path $OutputDirectory 'command.txt'
    [IO.File]::WriteAllText(($commandPath + '.tmp'), $command)
    Move-Item -LiteralPath ($commandPath + '.tmp') -Destination $commandPath -Force
    for ($i = 0; $i -lt 50 -and (Test-Path -LiteralPath $commandPath); $i++) { Start-Sleep -Milliseconds 60 }
    if (Test-Path -LiteralPath $commandPath) { throw 'Command not consumed' }
}
$nativeCode = @'
using System;using System.Runtime.InteropServices;
public static class WinIslandMaskVerification {
[DllImport("gdi32.dll")]static extern IntPtr CreateRectRgn(int l,int t,int r,int b);
[DllImport("gdi32.dll")]static extern bool DeleteObject(IntPtr h);
[DllImport("user32.dll")]static extern int GetWindowRgn(IntPtr h,IntPtr r);
[DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
[DllImport("dwmapi.dll")]static extern int DwmGetWindowAttribute(IntPtr h,int a,out int v,int s);
public static bool Masked(long hwnd){IntPtr h=new IntPtr(hwnd);int cloaked;DwmGetWindowAttribute(h,14,out cloaked,4);IntPtr r=CreateRectRgn(0,0,0,0);int k=GetWindowRgn(h,r);DeleteObject(r);return IsWindow(h)&&((cloaked&1)!=0||k==1);}
}
'@
Add-Type -TypeDefinition $nativeCode
$app = $null
$fixture = $null
$fixturePath = Join-Path $OutputDirectory 'fixture.txt'
try {
    $app = Start-Process -FilePath $Executable -ArgumentList @('--verify', ('"' + $OutputDirectory + '"')) -WindowStyle Hidden -PassThru
    $state = Wait-State { param($s) $s['Idle'] -eq 'True' -and [int]$s['HelperId'] -gt 0 }
    Send-Command 'music-stop'
    $null = Wait-State { param($s) $s['HasMusic'] -eq 'False' -and $s['Idle'] -eq 'True' }
    Assert-Check ($state['Resident'] -eq 'True' -and $state['HideNative'] -eq 'True' -and $state['Seconds'] -eq '4') 'Default settings and native helper startup'
    $fixture = Start-Process -FilePath $Executable -ArgumentList @('--native-fixture', ('"' + $fixturePath + '"')) -WindowStyle Hidden -PassThru
    $null = Wait-State { param($s) [int]$s['Masks'] -gt 0 }
    $fixtureHandle = [long][IO.File]::ReadAllText($fixturePath)
    Assert-Check ([WinIslandMaskVerification]::Masked($fixtureHandle)) 'Cross-process native window masking'
    Send-Command 'native=off'
    $null = Wait-State { param($s) $s['HideNative'] -eq 'False' -and $s['HelperId'] -eq '0' }
    Assert-Check (-not [WinIslandMaskVerification]::Masked($fixtureHandle)) 'Turning native suppression off immediately restores the window'
    Send-Command 'native=on'
    $null = Wait-State { param($s) [int]$s['Masks'] -gt 0 }
    Assert-Check ([WinIslandMaskVerification]::Masked($fixtureHandle)) 'Turning native suppression on masks the existing window again'
    Send-Command 'resident=off'
    $null = Wait-State { param($s) $s['Offscreen'] -eq 'True' }
    Assert-Check $true 'Resident off moves the pill above the display'
    Send-Command 'seconds=0.7'
    $null = Wait-State { param($s) $s['Seconds'] -eq '0.7' }
    Send-Command 'notice-short'
    $state = Wait-State { param($s) $s['Holding'] -eq 'True' }
    Assert-Check ([double]$state['Width'] -lt 250 -and $state['Offscreen'] -eq 'False') 'Short content uses compact dimensions and slides into view'
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $null = Wait-State { param($s) $s['Offscreen'] -eq 'True' }
    Assert-Check ($watch.Elapsed.TotalSeconds -lt 2) 'Configured sub-second hold finishes and slides offscreen'
    Send-Command 'seconds=-5'
    $state = Read-State
    Assert-Check ($state['Seconds'] -eq '0.7') 'Invalid input preserves the previous setting'
    Send-Command 'seconds=4'
    Send-Command 'resident=on'
    $null = Wait-State { param($s) $s['Idle'] -eq 'True' -and $s['Offscreen'] -eq 'False' }
    Assert-Check $true 'Resident on restores the pill immediately'
    Send-Command 'music=play'
    Send-Command 'music-expand'
    $state = Wait-State { param($s) $s['HasMusic'] -eq 'True' -and $s['Idle'] -eq 'True' -and [double]$s['MusicTop'] -lt 1 }
    Assert-Check ($state['MusicPlaying'] -eq 'True' -and $state['HasLyric'] -eq 'True') 'Music displays independently with synchronized synthetic lyrics'
    Send-Command 'notice-long'
    $state = Wait-State { param($s) $s['Holding'] -eq 'True' }
    Assert-Check ($state['HasMusic'] -eq 'True' -and [double]$state['MusicTop'] -eq 0 -and [double]$state['NoticeTop'] -eq [double]$state['MusicHeight'] -and [double]$state['Height'] -gt [double]$state['MusicHeight']) 'Message extends below music inside the shared container'
    Send-Command 'music=next'
    $state = Read-State
    Assert-Check ($state['Holding'] -eq 'True' -and $state['HasLyric'] -eq 'False') 'Track change clears missing lyrics without replacing the message'
    Send-Command 'music=pause'
    $state = Read-State
    Assert-Check ($state['HasMusic'] -eq 'True' -and $state['MusicPlaying'] -eq 'False' -and $state['Holding'] -eq 'True') 'Pause is represented explicitly while the message remains visible'
    Send-Command 'seconds=0.1'
    Send-Command 'music=play'
    $null = Wait-State { param($s) $s['Idle'] -eq 'True' }
    Send-Command 'burst'
    $state = Wait-State { param($s) $s['SyntheticDelivered'] -eq '12' -and $s['Idle'] -eq 'True' } 14000
    Assert-Check ($state['Pending'] -eq '0' -and $state['HasMusic'] -eq 'True') 'All 12 animation-overlapping messages display once and restore music after draining'
    Send-Command 'music-stop'
    $state = Wait-State { param($s) $s['HasMusic'] -eq 'False' -and $s['Idle'] -eq 'True' }
    Assert-Check ([Math]::Abs([double]$state['Height'] - 29.92) -lt .01) 'Music exit restores the enlarged idle pill'
    Send-Command 'seconds=4'
    $received = Join-Path $OutputDirectory 'notification-received.txt'
    # Earlier UI fixtures use the same diagnostic title; discard their marker before the real-toast check.
    if (Test-Path -LiteralPath $received) { Remove-Item -LiteralPath $received }
    $script = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'test-toast.ps1'), [Text.Encoding]::UTF8).Replace("'WinIslandTest'", "'WI11Test'")
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($script))
    & powershell -NoProfile -EncodedCommand $encoded | Out-Null
    $received = Join-Path $OutputDirectory 'notification-received.txt'
    for ($i=0; $i -lt 60 -and -not (Test-Path -LiteralPath $received); $i++) { Start-Sleep -Milliseconds 100 }
    Assert-Check ((Test-Path -LiteralPath $received) -and [IO.File]::ReadAllText($received).Contains('body=True')) 'Real Windows notification received while native suppression is enabled'
    $state = Read-State
    $helper = Get-Process -Id ([int]$state['HelperId'])
    Assert-Check ($state['SettingsOpen'] -eq 'True') 'Settings window remains open during tray exit test'
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'exit.request'), 'Exit')
    Assert-Check ($app.WaitForExit(5000) -and $helper.WaitForExit(3000)) 'Tray exit terminates main and helper processes'
    Assert-Check ([WinIslandMaskVerification]::IsWindow([IntPtr]$fixtureHandle) -and -not [WinIslandMaskVerification]::Masked($fixtureHandle)) 'Tray exit restores the still-live native test window'
    $run = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\Microsoft\Windows\CurrentVersion\Run')
    try { Assert-Check ($startupBefore -eq $run.GetValue('WinIsland')) 'Diagnostic run and tray exit preserve the user autostart setting' } finally { $run.Dispose() }
} finally {
    if ($app -and -not $app.HasExited) {
        [IO.File]::WriteAllText((Join-Path $OutputDirectory 'exit.request'), 'Exit')
        if (-not $app.WaitForExit(5000)) { Stop-Process -Id $app.Id -Force }
    }
    if ($fixture -and -not $fixture.HasExited) {
        [IO.File]::WriteAllText(($fixturePath + '.close'), 'Close')
        if (-not $fixture.WaitForExit(3000)) { Stop-Process -Id $fixture.Id -Force }
    }
    [IO.File]::WriteAllLines((Join-Path $OutputDirectory 'verification.txt'), $results, [Text.Encoding]::UTF8)
}
$results
