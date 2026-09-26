$ErrorActionPreference='Stop'
$branch=Split-Path $PSScriptRoot
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Windows.Forms,System.Drawing
Add-Type @'
using System;using System.Runtime.InteropServices;
public class NativeProbe {
 public delegate bool Callback(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(Callback cb,IntPtr p);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,System.Text.StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 public static IntPtr Find(int pid,string cls){IntPtr answer=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var s=new System.Text.StringBuilder(256);GetClassName(h,s,256);if(id==pid&&(cls==null||s.ToString()==cls)&&IsWindowVisible(h)){answer=h;return false;}return true;},IntPtr.Zero);return answer;}
}
'@
$results=[Collections.Generic.List[object]]::new()
function Check($name,$condition){$results.Add([pscustomobject]@{name=$name;passed=[bool]$condition});Write-Output "$name : $condition";if(!$condition){throw "FAILED: $name"}}
function Wait-For([scriptblock]$fn,[int]$seconds=20){$end=[datetime]::UtcNow.AddSeconds($seconds);do{$v=& $fn;if($v){return $v};Start-Sleep -Milliseconds 150}while([datetime]::UtcNow -lt $end);throw 'Timed out waiting for expected state'}
function Read-Exact($stream,[int]$length){$b=[byte[]]::new($length);$offset=0;while($offset -lt $length){$task=$stream.ReadAsync($b,$offset,$length-$offset);if(!$task.Wait(12000)){throw 'Pipe read timeout'};$n=$task.Result;if(!$n){throw 'Pipe disconnected'};$offset+=$n};return ,$b}
function Call($command,$params=@{}){
 $c=Get-Content -LiteralPath (Join-Path $data 'settings-connection.json') -Raw|ConvertFrom-Json
 $pipe=[IO.Pipes.NamedPipeClientStream]::new('.',$c.pipe.Substring(9),[IO.Pipes.PipeDirection]::InOut,[IO.Pipes.PipeOptions]::Asynchronous)
 try{$pipe.Connect(5000);$payload=[Text.Encoding]::UTF8.GetBytes((@{protocol=1;token=$c.token;command=$command;params=$params}|ConvertTo-Json -Depth 15 -Compress));$prefix=[BitConverter]::GetBytes([int]$payload.Length);$pipe.Write($prefix,0,4);$pipe.Write($payload,0,$payload.Length);$length=[BitConverter]::ToInt32((Read-Exact $pipe 4),0);if($length -lt 1 -or $length -gt 4194304){throw 'Invalid length'};$reply=[Text.Encoding]::UTF8.GetString((Read-Exact $pipe $length))|ConvertFrom-Json;$pipe.WriteByte(1);if(!$reply.ok){throw ($reply.code+': '+$reply.error)};return $reply}finally{$pipe.Dispose()}
}
function Find-UI($root,[string]$id){$root.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$id))}
function Click-UI($element){$element.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()}
function Select-UI($element){$element.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()}
function Snap($name){$screen=[Windows.Forms.SystemInformation]::VirtualScreen;$b=[Drawing.Bitmap]::new($screen.Width,$screen.Height);$g=[Drawing.Graphics]::FromImage($b);try{$g.CopyFromScreen($screen.Left,$screen.Top,0,0,$b.Size);$b.Save((Join-Path $run $name),[Drawing.Imaging.ImageFormat]::Png)}finally{$g.Dispose();$b.Dispose()}}
$run=Join-Path $branch ('verification\live-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $run|Out-Null
$version=Get-Content (Join-Path $branch 'product-version.json') -Raw -Encoding UTF8|ConvertFrom-Json
$exe=Join-Path $branch ('release\'+$version.executable)
$testHome=Join-Path $run 'first-run 中文 spaces';$data=Join-Path $testHome '安装数据 中文 spaces'
$hostProcess=$null;$uiProcess=$null;$backup=[Windows.Forms.Clipboard]::GetDataObject()
try{
 $hostProcess=Start-Process -FilePath $exe -ArgumentList ('--verify-installed "'+$testHome+'"') -PassThru -WindowStyle Hidden
 $installer=Wait-For {[NativeProbe]::Find($hostProcess.Id,'WinIsland.Installation')}
 Check 'Isolated first-run installer shown' ($installer -ne [IntPtr]::Zero);Snap 'installer.png'
 [void][NativeProbe]::PostMessage($installer,0x111,[IntPtr]1003,[IntPtr]::Zero)
 Wait-For {Test-Path -LiteralPath (Join-Path $data 'ready.txt')} 40|Out-Null
 Check 'Embedded components installed under selected root' (Test-Path -LiteralPath (Join-Path $testHome 'bootstrap\install-location.json'))
 $caps=Call 'capabilities.read';Check 'Experimental version and clipboard API' ($caps.version -eq $version.display -and $caps.clipboardInterface -eq 1)
 $settings=Call 'settings.read';$saved=Call 'settings.write' @{revision=$settings.revision;patch=@{showMusic=$false;showMessages=$false}}
 Check 'Settings IPC updates music/message flags' (!$saved.settings.showMusic -and !$saved.settings.showMessages)
 $state=Call 'clipboard.call' @{action='list'};$state.preferences.show=$false
 Call 'clipboard.call' @{action='preferences';preferences=$state.preferences}|Out-Null
 [Windows.Forms.Clipboard]::SetText("WinUI 移植验证`n第二行 alpha")
 $history=Wait-For {$r=Call 'clipboard.call' @{action='list'};if($r.entries.Count -gt 0){$r}}
 $id=$history.entries[0].id;Check 'OS clipboard capture' ($history.entries[0].text -match 'WinUI 移植验证')
 $h=[NativeProbe]::Find($hostProcess.Id,'WinIsland.Native');[void][NativeProbe]::PostMessage($h,0x802d,[IntPtr]::Zero,[IntPtr]::Zero)
 $uiProcess=Wait-For {Get-CimInstance Win32_Process -Filter "name='WinIslandSettings.exe'"|Where-Object {$_.ParentProcessId -eq $hostProcess.Id}|Select-Object -First 1}
 Check 'Host launches extracted WinUI' ($uiProcess.ExecutablePath.StartsWith($data,[StringComparison]::OrdinalIgnoreCase))
 $uiWindow=Wait-For {[NativeProbe]::Find($uiProcess.ProcessId,$null)}
 [void][NativeProbe]::SetForegroundWindow($uiWindow);$ui=[Windows.Automation.AutomationElement]::FromHandle($uiWindow)
 Select-UI (Wait-For {Find-UI $ui 'nav-clipboard'})
 $card=Wait-For {Find-UI $ui ('clipboard-card-'+$id)}
 $card.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
 $editor=Wait-For {Find-UI $ui ('clipboard-editor-'+$id)};$vp=$editor.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern)
 Wait-For {$vp.Current.Value -match 'WinUI 移植验证'}|Out-Null
 Check 'WinUI reads complete text through IPC' ($vp.Current.Value -match '第二行')
 $vp.SetValue("WinUI 保存验证`n中文多行保留");Click-UI (Find-UI $ui ('clipboard-save-'+$id))
 Wait-For {(Call 'clipboard.call' @{action='get';id=$id}).entry.text -match '保存验证'}|Out-Null
 Check 'WinUI save writes host history' $true;Snap 'clipboard-winui.png'
 $search=Find-UI $ui 'clipboard-search';$search.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('no-match-unique');Start-Sleep -Milliseconds 1200
 Check 'WinUI search filters nonmatching cards' (!(Find-UI $ui ('clipboard-card-'+$id)))
 $search.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('');Wait-For {Find-UI $ui ('clipboard-card-'+$id)}|Out-Null
 Select-UI (Find-UI $ui 'nav-about');Start-Sleep -Milliseconds 400
 $about=$ui.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,('版本：'+$version.display)))
 Check 'About reports experimental version' ($null -ne $about);Snap 'about-winui.png'
 Select-UI (Find-UI $ui 'nav-5');Wait-For {Find-UI $ui 'plugin-list'}|Out-Null;Check 'Plugin IPC responds' ((Call 'mods.list').ok)
 Select-UI (Find-UI $ui 'nav-6');Wait-For {Find-UI $ui 'transfer-list'}|Out-Null;Check 'Transfer IPC responds' ((Call 'transfer.call' @{action='list'}).ok)
 [void][NativeProbe]::PostMessage($uiWindow,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
 [IO.File]::WriteAllText((Join-Path $data 'exit.request'),'exit');if(!$hostProcess.WaitForExit(12000)){throw 'Host exit timeout'}
 Remove-Item -LiteralPath (Join-Path $data 'exit.request');Remove-Item -LiteralPath (Join-Path $data 'ready.txt')
 $locatorHash=(Get-FileHash -LiteralPath (Join-Path $testHome 'bootstrap\install-location.json')).Hash
 $hostProcess=Start-Process -FilePath $exe -ArgumentList ('--verify-installed "'+$testHome+'"') -PassThru -WindowStyle Hidden
 Wait-For {Test-Path -LiteralPath (Join-Path $data 'ready.txt')}|Out-Null
 Check 'Second launch skips installer and reuses locator' (([NativeProbe]::Find($hostProcess.Id,'WinIsland.Installation') -eq [IntPtr]::Zero) -and ((Get-FileHash -LiteralPath (Join-Path $testHome 'bootstrap\install-location.json')).Hash -eq $locatorHash))
 $restored=Call 'settings.read';Check 'Settings restored after restart' (!$restored.settings.showMusic -and !$restored.settings.showMessages)
 Check 'Edited history restored after restart' ((Call 'clipboard.call' @{action='get';id=$id}).entry.text -match '中文多行保留')
 Check 'Clipboard preferences restored' (!(Call 'clipboard.call' @{action='list'}).preferences.show)
 Check 'EXE version matches' ((Get-Item -LiteralPath $exe).VersionInfo.ProductVersion -eq $version.display)
}catch{$results.Add([pscustomobject]@{name='Exception';passed=$false;error=$_.Exception.Message;line=$_.InvocationInfo.ScriptLineNumber});Write-Output $_.ScriptStackTrace}
finally{
 if($hostProcess -and !$hostProcess.HasExited){if(Test-Path -LiteralPath $data){[IO.File]::WriteAllText((Join-Path $data 'exit.request'),'exit')};if(!$hostProcess.WaitForExit(8000)){Stop-Process -Id $hostProcess.Id -Force}}
 if($uiProcess){$p=Get-Process -Id $uiProcess.ProcessId -ErrorAction SilentlyContinue;if($p){[void]$p.CloseMainWindow()}}
 if($backup){[Windows.Forms.Clipboard]::SetDataObject($backup,$true)}
 $results|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $run 'results.json') -Encoding utf8
 Write-Output ('Evidence: '+$run)
}
if($results|Where-Object {!$_.passed}){exit 1}
