$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName Accessibility
Add-Type -ReferencedAssemblies Accessibility -TypeDefinition @'
using System;using System.Runtime.InteropServices;
public class AccessibleWindow {
 [DllImport("oleacc.dll")] static extern int AccessibleObjectFromWindow(IntPtr h,uint id,ref Guid iid,out Accessibility.IAccessible obj);
 static int visited;
 static string Tree(Accessibility.IAccessible a,int depth){if(depth>8||++visited>100)return "";string result=" depth="+depth+" role="+a.get_accRole(0)+" children="+a.accChildCount+" name_len="+(a.get_accName(0)??"").Length+";";for(int i=1;i<=Math.Min(a.accChildCount,20);i++){try{var child=a.get_accChild(i) as Accessibility.IAccessible;if(child!=null){result+=Tree(child,depth+1);Marshal.ReleaseComObject(child);}}catch{}}return result;}
 public static string Probe(IntPtr h){var guid=new Guid("618736E0-3C3D-11CF-810C-00AA00389B71");Accessibility.IAccessible a;int hr=AccessibleObjectFromWindow(h,0xFFFFFFFC,ref guid,out a);if(hr<0||a==null)return "hr="+hr;try{visited=0;return Tree(a,0);}finally{Marshal.ReleaseComObject(a);}}
 [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr h,int command);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h,uint message,UIntPtr w,IntPtr l,uint flags,uint timeout,out UIntPtr result);
}
'@
foreach($name in @('QQ','cloudmusic')) {
 $p=Get-Process $name -ErrorAction SilentlyContinue | Where-Object {$_.MainWindowHandle -ne 0} | Select-Object -First 1
 if(!$p){continue}
 $window=$p.MainWindowHandle;$minimized=[AccessibleWindow]::IsIconic($window)
 try {
  if($minimized){[void][AccessibleWindow]::ShowWindowAsync($window,4);Start-Sleep -Milliseconds 400}
  $result=[UIntPtr]::Zero
  [void][AccessibleWindow]::SendMessageTimeout($window,0x3D,[UIntPtr]::Zero,[IntPtr](-4),2,300,[ref]$result)
  $root=[Windows.Automation.AutomationElement]::FromHandle($window)
  for($attempt=0;$attempt -lt 2;$attempt++) {
   $all=$root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)
   "$name attempt=$attempt descendants=$($all.Count)"
   for($i=0;$i -lt [Math]::Min($all.Count,20);$i++) {
    $c=$all[$i].Current
    "type=$($c.ControlType.ProgrammaticName) class=$($c.ClassName) name_length=$($c.Name.Length)"
    if($c.ClassName -eq 'Chrome_RenderWidgetHostHWND') {"MSAA $([AccessibleWindow]::Probe([IntPtr]$c.NativeWindowHandle))"}
   }
   Start-Sleep -Milliseconds 250
  }
 }finally{if($minimized){[void][AccessibleWindow]::ShowWindowAsync($window,6)}}
}
