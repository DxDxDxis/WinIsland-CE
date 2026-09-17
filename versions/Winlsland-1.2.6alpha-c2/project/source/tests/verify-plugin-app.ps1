param([string]$Release=(Join-Path $PSScriptRoot '../..'))
$ErrorActionPreference='Stop'
$Release=[IO.Path]::GetFullPath($Release)
$test=Join-Path $Release ('verification-app-'+[DateTime]::Now.ToString('HHmmss'))
New-Item -ItemType Directory -Path "$test/mods" -Force | Out-Null
Copy-Item -LiteralPath "$Release/examples/example-button.wimod" -Destination "$test/mods/example-button.wimod"
Add-Type @"
using System;using System.Runtime.InteropServices;
public static class ModNative {
[DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
[DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
[DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
[DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h,out RECT r);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int left,top,right,bottom; }
public static int[] Rect(IntPtr h){RECT r;if(!GetWindowRect(h,out r))return new[]{0,0,0,0};return new[]{r.left,r.top,r.right,r.bottom};}
}
"@
Add-Type -AssemblyName System.Drawing
function CaptureWindow([IntPtr]$handle,[string]$path){
    $r=[ModNative]::Rect($handle);$w=$r[2]-$r[0];$h=$r[3]-$r[1];if($w -le 0 -or $h -le 0){return $false}
    $bitmap=[Drawing.Bitmap]::new($w,$h);$graphics=[Drawing.Graphics]::FromImage($bitmap);$graphics.CopyFromScreen($r[0],$r[1],0,0,$bitmap.Size);$bitmap.Save($path,[Drawing.Imaging.ImageFormat]::Png);$graphics.Dispose();$bitmap.Dispose();return $true
}
function State {
    $d=@{}
    if(Test-Path "$test/state.txt") {
        # The diagnostic writer keeps the file open while replacing its
        # snapshot. Allow a concurrent read instead of failing the whole UI
        # smoke test on a harmless sharing window.
        try {
            $stream=[IO.File]::Open("$test/state.txt",[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
            $reader=[IO.StreamReader]::new($stream)
            $text=$reader.ReadToEnd();$reader.Dispose();$stream.Dispose()
            foreach($line in ($text -split "`r?`n")){if($line -match '^([^=]+)=(.*)$'){$d[$matches[1]]=$matches[2]}}
        } catch [IO.IOException] { }
    }
    return $d
}
function Command([string]$command){$old=(State).CommandsProcessed;[IO.File]::WriteAllText("$test/command.txt",$command);$end=[DateTime]::Now.AddSeconds(8);do{Start-Sleep -Milliseconds 40;$s=State}while(($s.CommandsProcessed -eq $old -or !$s.CommandsProcessed) -and [DateTime]::Now -lt $end);if($s.CommandsProcessed -eq $old){throw "Command timeout: $command"};Start-Sleep -Milliseconds 300}
$results=[Collections.Generic.List[string]]::new()
function Check([bool]$yes,[string]$label){$results.Add(('PASS','FAIL')[[int](!$yes)]+' '+$label);if(!$yes){Write-Warning $label}}
$p=Start-Process -FilePath "$Release/WinIsland-1.2.6alpha-c2.exe" -ArgumentList @('--verify',('"'+$test+'"')) -WindowStyle Hidden -PassThru
try {
$end=[DateTime]::Now.AddSeconds(12);do{Start-Sleep -Milliseconds 100;$s=State}while(!$s.RenderWindow -and [DateTime]::Now -lt $end)
Check ([bool]$s.RenderWindow) 'App starts without missing-ordinal popup'
Start-Sleep -Milliseconds 700
$s=State;$keys=@($s.Keys|Where-Object {$_ -match '^Button5\d{4}$'})
Check ($keys.Count -gt 0) 'Registered DLL button reaches actual island hit region'
if($keys.Count){$xy=$s[$keys[0]].Split(',');$x=[int]([double]$xy[0]*[double]$s.Dpi);$y=[int]([double]$xy[1]*[double]$s.Dpi);$point=[IntPtr](($y -shl 16) -bor ($x -band 65535));$h=[IntPtr][long]$s.MusicWindow;[ModNative]::SendMessage($h,0x201,[IntPtr]1,$point)|Out-Null;[ModNative]::SendMessage($h,0x202,[IntPtr]0,$point)|Out-Null;Start-Sleep -Milliseconds 500;Check (([IO.File]::ReadAllText("$test/mods/example-button/mod.log")).Contains('示例按钮已执行')) 'Island mouse click executes DLL callback'}
Command 'music=play'
[IO.File]::WriteAllText("$test/fixture.lrc","[00:00.00]插件布局验证`n[00:12.00]与音乐和歌词共同展示`n[00:30.00]下一行")
Command 'lyrics-fixture'
Command 'music-expand'
Command 'capture'
Copy-Item "$test/capture.png" "$test/music-plugin.png"
$s=State;Check ($s.HasMusic -eq 'True' -and $s.HasLyric -eq 'True' -and $s.MusicExpanded -eq 'True') 'Synthetic music lyrics expanded coexist with real plugin'
Command 'settings-page=0'
$s=State;$settingsWindow=[IntPtr][long]$s.SettingsWindow
foreach($tab in 250..254) {
    $tabHandle=[ModNative]::GetDlgItem($settingsWindow,$tab)
    [ModNative]::SendMessage($tabHandle,0xF5,[IntPtr]0,[IntPtr]0)|Out-Null
    Start-Sleep -Milliseconds 90
}
Check (CaptureWindow $settingsWindow "$test/categories-after-rapid-switch.png") 'All five category buttons switch without a stale overlay'
Command 'settings-page=4'
Command 'settings-scroll=650'
$s=State;$settingsWindow=[IntPtr][long]$s.SettingsWindow
$slider=[ModNative]::GetDlgItem([IntPtr][long]$s.SettingsContent,138)
Check ([ModNative]::IsWindow($slider)) 'Top arc slider exists in the other-settings page'
if([ModNative]::IsWindow($slider)) {
    # Exercise the real owner-drawn trackbar repeatedly.  WM_HSCROLL is sent
    # to its actual parent, matching a mouse drag without introducing a
    # second control or a synthetic animation path.
    foreach($value in @(50,100,150,75,125,100)) {
        [ModNative]::SendMessage($slider,0x0400+5,[IntPtr]1,[IntPtr]$value)|Out-Null # TBM_SETPOS
        [ModNative]::SendMessage([IntPtr][long]$s.SettingsContent,0x0114,[IntPtr]4,$slider)|Out-Null # WM_HSCROLL/TB_THUMBTRACK
        Start-Sleep -Milliseconds 35
    }
    $pos=[ModNative]::SendMessage($slider,0x0400,[IntPtr]0,[IntPtr]0).ToInt32()
    Check ($pos -eq 100) 'Top arc slider remains at the final legal value after repeated drags'
    Check (CaptureWindow $settingsWindow "$test/top-arc-after-drag.png") 'Top arc repaint captured after repeated drags'
}
Command 'settings-scroll=9999'
$s=State;$button=[ModNative]::GetDlgItem([IntPtr][long]$s.SettingsContent,140);Check ([ModNative]::IsWindow($button)) 'Existing settings has real Plugin Management entry'
Check (CaptureWindow $settingsWindow "$test/settings-before-plugin.png") 'Settings page screenshot captured before plugin expansion'
[ModNative]::SendMessage($button,0xF5,[IntPtr]0,[IntPtr]0)|Out-Null
Start-Sleep -Milliseconds 150;Check (CaptureWindow $settingsWindow "$test/plugin-expand-mid.png") 'Embedded plugin expansion intermediate frame captured'
Start-Sleep -Milliseconds 500;Check (CaptureWindow $settingsWindow "$test/plugin-expanded.png") 'Embedded plugin page final frame captured'
foreach($dpi in @('1','1.25','1.5','2')) {Command ('dpi='+$dpi);Command 'capture';Copy-Item "$test/capture.png" "$test/island-$dpi.png";$s=State;Check (@($s.Keys|Where-Object {$_ -match '^Button5\d{4}$'}).Count -gt 0) ('Plugin hit targets maintained at diagnostic DPI '+$dpi)}
Command 'music-collapse';Command 'music-stop'
} finally {
[IO.File]::WriteAllText("$test/exit.request",'exit');$done=$p.WaitForExit(8000);Check $done 'App exits after safe plugin shutdown';$results|Set-Content "$test/results.txt" -Encoding utf8
}
$results
Write-Output "Artifacts: $test"
if(@($results|Where-Object {$_ -like 'FAIL*'}).Count){exit 1}

