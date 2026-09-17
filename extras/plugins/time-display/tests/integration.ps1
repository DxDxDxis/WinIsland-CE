$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe='E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.6alpha-c2-no-motion\source\build-no-motion\WinIsland-1.2.6alpha-c2.exe'
$run=Join-Path $root ('verification/run-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
New-Item -ItemType Directory "$run/mods","$run/local" | Out-Null
Copy-Item "$root/time-display.wimod" "$run/mods/time-display.wimod"
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class WIProbe {
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr h,IntPtr a,string c,string t);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,StringBuilder l,uint f,uint t,out IntPtr r);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out IntPtr r);
 public static bool Command(IntPtr h,int id,IntPtr control){IntPtr r;return SendMessageTimeout(h,273,(IntPtr)id,control,2,4000,out r)!=IntPtr.Zero;}
 public delegate bool Visitor(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p,Visitor v,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,StringBuilder b,int n);
 public static string Children(IntPtr p){var s=new StringBuilder();EnumChildWindows(p,(h,l)=>{var b=new StringBuilder(256);GetClassName(h,b,256);s.AppendLine(h+" "+b+" "+Text(h));return true;},IntPtr.Zero);return s.ToString();}
 public static string Text(IntPtr h){var b=new StringBuilder(8192);IntPtr r;SendMessageTimeout(h,13,(IntPtr)8192,b,2,2000,out r);return b.ToString();}
}
'@
$lines=[Collections.Generic.List[string]]::new()
function Check($ok,$label){$lines.Add("$(if($ok){'PASS'}else{'FAIL'}) $label");if(!$ok){throw $label}}
function WaitFor([scriptblock]$test){$end=[DateTime]::UtcNow.AddSeconds(12);while([DateTime]::UtcNow -lt $end){if(&$test){return};Start-Sleep -Milliseconds 100};throw 'Timeout waiting for host'}
function Command($value){[IO.File]::WriteAllText("$run/command.txt",$value,[Text.UTF8Encoding]::new($false));WaitFor {!(Test-Path "$run/command.txt")};Start-Sleep -Milliseconds 250}
function State(){Get-Content "$run/state.txt" -Raw | ConvertFrom-StringData}
function Click($parent,$id){$h=[WIProbe]::GetDlgItem($parent,$id);Check ($h -ne 0) "control $id exists";Check ([WIProbe]::Command($parent,$id,$h)) "command $id delivered";Start-Sleep -Milliseconds 500}
function Details(){[WIProbe]::Text([WIProbe]::GetDlgItem($script:manager,609))}
$start=[Diagnostics.ProcessStartInfo]::new($exe)
$start.UseShellExecute=$false;$start.WorkingDirectory=$run
$start.ArgumentList.Add('--verify');$start.ArgumentList.Add($run)
$start.Environment['LOCALAPPDATA']="$run/local"
$proc=[Diagnostics.Process]::Start($start)
try {
 WaitFor {Test-Path "$run/state.txt"}
 WaitFor {(Test-Path "$run/mods/time-display/mod.log") -and ((Get-Content "$run/mods/time-display/mod.log" -Raw) -match '时间显示插件已启用')}
 Command 'music-stop'; Start-Sleep -Seconds 1
 $enabled=State
 Copy-Item "$run/state.txt" "$run/idle-enabled.txt"
 Command 'capture';Copy-Item "$run/capture.png" "$run/idle-hidden.png"
 Command 'settings-page=4'
 WaitFor {((State).SettingsWindow -ne '0') -and ((State).SettingsPage -eq '4')}
 $settings=[IntPtr][long](State).SettingsWindow;$content=[WIProbe]::GetDlgItem($settings,300)
 Command 'settings-scroll=1000'
 Check ([WIProbe]::Command($settings,140,[WIProbe]::GetDlgItem($content,140))) 'settings plugin-entry command delivered'
 [WIProbe]::Children($settings) | Set-Content "$run/window-tree.txt"
 WaitFor {[WIProbe]::FindWindowEx($settings,0,'WinIsland.ModManager',$null) -ne 0}
 $script:manager=[WIProbe]::FindWindowEx($settings,0,'WinIsland.ModManager',$null)
 Check ($manager -ne 0) 'embedded real manager opened'
 Click $manager 601
 WaitFor {(Details) -match '已启用'}
 $details=Details;$details | Set-Content "$run/details-enabled.txt" -Encoding utf8
 foreach($text in @('时间显示','daxian','1.0.0','一个能让 WinIsland 在待机状态显示实时时间的插件。')) {Check ($details.Contains($text)) "actual manager metadata: $text"}
 Click $manager 612;WaitFor {(Details) -match '已禁用'}
 $disabled=State
 Check ($disabled.Width -eq $enabled.Width -and $disabled.Height -eq $enabled.Height) 'idle size equal enabled vs disabled'
 for($i=0;$i -lt 2;$i++){
   Click $manager 611;WaitFor {(Details) -match '已启用'}
   Click $manager 614;WaitFor {(Details) -match '已启用'}
   Click $manager 612;WaitFor {(Details) -match '已禁用'}
 }
 Click $manager 611;WaitFor {(Details) -match '已启用'}
 Command 'close-settings'
 Command 'music=play';Start-Sleep -Seconds 1
 Copy-Item "$run/state.txt" "$run/music.txt"
 Check ((State).HasMusic -eq 'True') 'real host synthetic music shown'
 Check ((Get-Content "$run/mods/time-display/mod.log" -Raw).Contains('收到音乐信息事件')) 'actual music event received, clock stays hidden'
 Command 'notice-short';Check ((Get-Content "$run/mods/time-display/mod.log" -Raw).Contains('收到通知事件')) 'actual notification event received, clock stays hidden'
 Command 'capture';Copy-Item "$run/capture.png" "$run/music-notice-hidden.png"
 [IO.File]::WriteAllText("$run/exit.request",'exit')
 Check ($proc.WaitForExit(12000)) 'host normal shutdown'
 Check ($proc.ExitCode -eq 0) 'host exit code 0'
 $log=Get-Content "$run/mods/time-display/mod.log" -Raw
 Check ($log.Contains('时间显示插件已卸载') -and $log.Contains('DLL 卸载')) 'actual onUnload and FreeLibrary'
 Check ($log -notmatch '注册资源 kind=[234] target=') 'no visual/layout replacement resources'
} finally {
 if(!$proc.HasExited){[IO.File]::WriteAllText("$run/exit.request",'exit');[void]$proc.WaitForExit(12000)}
 $lines | Set-Content "$run/integration-results.txt" -Encoding utf8
 Write-Output $run
 $lines
}


