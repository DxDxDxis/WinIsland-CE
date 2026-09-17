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
}
"@
function State { $d=@{};if(Test-Path "$test/state.txt"){foreach($line in [IO.File]::ReadAllLines("$test/state.txt")){if($line -match '^([^=]+)=(.*)$'){$d[$matches[1]]=$matches[2]}}};return $d }
function Command([string]$command){$old=(State).CommandsProcessed;[IO.File]::WriteAllText("$test/command.txt",$command);$end=[DateTime]::Now.AddSeconds(8);do{Start-Sleep -Milliseconds 40;$s=State}while(($s.CommandsProcessed -eq $old -or !$s.CommandsProcessed) -and [DateTime]::Now -lt $end);if($s.CommandsProcessed -eq $old){throw "Command timeout: $command"};Start-Sleep -Milliseconds 300}
$results=[Collections.Generic.List[string]]::new()
function Check([bool]$yes,[string]$label){$results.Add(('PASS','FAIL')[[int](!$yes)]+' '+$label);if(!$yes){Write-Warning $label}}
$p=Start-Process -FilePath "$Release/WinIsland-1.2.6alpha-c1.exe" -ArgumentList @('--verify',('"'+$test+'"')) -WindowStyle Hidden -PassThru
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
Command 'settings-page=4'
Command 'settings-scroll=9999'
$s=State;$button=[ModNative]::GetDlgItem([IntPtr][long]$s.SettingsContent,140);Check ([ModNative]::IsWindow($button)) 'Existing settings has real Plugin Management entry'
[ModNative]::SendMessage($button,0xF5,[IntPtr]0,[IntPtr]0)|Out-Null
Start-Sleep -Milliseconds 300
foreach($dpi in @('1','1.25','1.5','2')) {Command ('dpi='+$dpi);Command 'capture';Copy-Item "$test/capture.png" "$test/island-$dpi.png";$s=State;Check (@($s.Keys|Where-Object {$_ -match '^Button5\d{4}$'}).Count -gt 0) ('Plugin hit targets maintained at diagnostic DPI '+$dpi)}
Command 'music-collapse';Command 'music-stop'
} finally {
[IO.File]::WriteAllText("$test/exit.request",'exit');$done=$p.WaitForExit(8000);Check $done 'App exits after safe plugin shutdown';$results|Set-Content "$test/results.txt" -Encoding utf8
}
$results
Write-Output "Artifacts: $test"
if(@($results|Where-Object {$_ -like 'FAIL*'}).Count){exit 1}
