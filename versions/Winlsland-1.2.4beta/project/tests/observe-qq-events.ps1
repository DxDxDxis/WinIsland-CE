param([string]$OutputPath,[int]$Seconds=180)
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;using System.IO;using System.Text;using System.Diagnostics;using System.Runtime.InteropServices;using System.Collections.Generic;
public class QqEvents {
 delegate void EventProc(IntPtr hook,uint ev,IntPtr h,int obj,int child,uint thread,uint time);
 [DllImport("user32.dll")] static extern IntPtr SetWinEventHook(uint a,uint b,IntPtr mod,EventProc cb,uint pid,uint thread,uint flags);
 [DllImport("user32.dll")] static extern bool UnhookWinEvent(IntPtr h);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")] static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr h,uint f);
 [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")] static extern bool PeekMessage(out MSG m,IntPtr h,uint a,uint b,uint f);
 [DllImport("user32.dll")] static extern bool TranslateMessage(ref MSG m);
 [DllImport("user32.dll")] static extern IntPtr DispatchMessage(ref MSG m);
 struct MSG{public IntPtr h;public uint message;public UIntPtr w;public IntPtr l;public uint time;public int x,y;public uint p;}
 struct RECT{public int L,T,R,B;}
 public static void Run(string path,int seconds) {
  var seen=new Dictionary<IntPtr,string>();var start=DateTime.UtcNow;
  EventProc proc=(hook,ev,h,obj,child,thread,time)=>{try {
   if(h==IntPtr.Zero||obj!=0||child!=0||GetAncestor(h,2)!=h)return;
   uint pid;GetWindowThreadProcessId(h,out pid);if(Process.GetProcessById((int)pid).ProcessName!="QQ")return;
   var cls=new StringBuilder(256);GetClassName(h,cls,256);RECT r;GetWindowRect(h,out r);
   var title=new StringBuilder(256);GetWindowText(h,title,256);
   bool notice=title.ToString().Contains("消息")||title.ToString().Contains("通知");
   string facts="hwnd="+h+" pid="+pid+" class="+cls+" visible="+IsWindowVisible(h)+" bounds="+r.L+","+r.T+","+r.R+","+r.B+" notice_title="+notice;
   string old;if(seen.TryGetValue(h,out old)&&old==facts&&ev!=0x8002&&ev!=0x8003)return;seen[h]=facts;
   File.AppendAllText(path,DateTime.Now.ToString("O")+" event="+ev+" "+facts+Environment.NewLine,new UTF8Encoding(false));
  }catch{}};
  var hookHandle=SetWinEventHook(0x8000,0x800C,IntPtr.Zero,proc,0,0,2);
  File.AppendAllText(path,"Passive real QQ window events; NO CHAT TEXT; hook="+hookHandle+Environment.NewLine);
  while((DateTime.UtcNow-start).TotalSeconds<seconds){MSG msg;while(PeekMessage(out msg,IntPtr.Zero,0,0,1)){TranslateMessage(ref msg);DispatchMessage(ref msg);}System.Threading.Thread.Sleep(25);}
  UnhookWinEvent(hookHandle);GC.KeepAlive(proc);
 }
}
'@
[QqEvents]::Run([IO.Path]::GetFullPath($OutputPath),$Seconds)
