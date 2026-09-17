$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -TypeDefinition @'
using System;using System.Text;using System.Runtime.InteropServices;
public class PlayerWindows {
 public delegate bool Visitor(IntPtr h,IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(Visitor p,IntPtr l);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out RECT r);
 public struct RECT {public int L,T,R,B;}
}
'@
$targets=@{}
Get-Process QQ,cloudmusic,SodaMusic -ErrorAction SilentlyContinue | ForEach-Object {$targets[$_.Id]=$_.ProcessName}
$rows=[Collections.Generic.List[object]]::new()
[void][PlayerWindows]::EnumWindows({param($h,$l)
 $processNumber=[uint32]0;[void][PlayerWindows]::GetWindowThreadProcessId($h,[ref]$processNumber)
 if($targets.ContainsKey([int]$processNumber)) {
  $c=[Text.StringBuilder]::new(256);[void][PlayerWindows]::GetClassName($h,$c,256)
  $rect=New-Object PlayerWindows+RECT;[void][PlayerWindows]::GetWindowRect($h,[ref]$rect)
  $rows.Add([pscustomobject]@{Hwnd=$h.ToInt64();Pid=$processNumber;App=$targets[[int]$processNumber];Class=$c.ToString();Visible=[PlayerWindows]::IsWindowVisible($h);Bounds="$($rect.L),$($rect.T),$($rect.R),$($rect.B)"})
 }
 return $true
},[IntPtr]::Zero)
$rows | ConvertTo-Json
# Only inspect music accessibility, never QQ chat text.
foreach($w in $rows) {
 if($w.App -ne 'cloudmusic' -or -not $w.Visible -or $w.Class -notmatch 'Chrome_WidgetWin|Orpheus') {continue}
 try {
  $root=[Windows.Automation.AutomationElement]::FromHandle([IntPtr]$w.Hwnd)
  $nodes=$root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)
  "Music UIA hwnd=$($w.Hwnd) nodes=$($nodes.Count)"
  for($i=0;$i -lt [Math]::Min($nodes.Count,500);$i++) {
   $n=$nodes[$i];$cur=$n.Current
   if($cur.ControlType -eq [Windows.Automation.ControlType]::Slider -or $cur.ControlType -eq [Windows.Automation.ControlType]::ProgressBar -or $cur.Name -match '\d{1,2}:\d{2}') {
    $range=$null;$value=$null
    if($n.TryGetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern,[ref]$range)) {$value="$($range.Current.Value)/$($range.Current.Maximum)"}
    [pscustomobject]@{Type=$cur.ControlType.ProgrammaticName;Name=$cur.Name;Id=$cur.AutomationId;Value=$value}|ConvertTo-Json -Compress
   }
  }
 }catch {"UIA error: $($_.Exception.GetType().Name)"}
}
