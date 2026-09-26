param([ValidateSet('Setup','Baseline','Inspect','InspectHere','Plugins','Transfer','Clipboard','Cleanup','Restart','Themes')][string]$Phase='Inspect')
$ErrorActionPreference='Stop'
$branch=Split-Path $PSScriptRoot
$run=Join-Path $branch 'verification\ui-layout-20260925'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Windows.Forms,System.Drawing,System.Security
Add-Type @'
using System;using System.Runtime.InteropServices;
public class LayoutProbe {
 public delegate bool Callback(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(Callback cb,IntPtr p);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int height,uint flags);
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint msg,IntPtr w,IntPtr l);
 public static IntPtr Find(int pid){IntPtr result=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id==pid&&IsWindowVisible(h)){result=h;return false;}return true;},IntPtr.Zero);return result;}
}
'@
function WaitFor([scriptblock]$fn,[int]$seconds=15){$until=[datetime]::UtcNow.AddSeconds($seconds);do{$v=& $fn;if($v){return $v};Start-Sleep -Milliseconds 150}while([datetime]::UtcNow -lt $until);throw 'Timed out waiting for UI state'}
function ReadExact($s,$n){$b=[byte[]]::new($n);$at=0;while($at -lt $n){$task=$s.ReadAsync($b,$at,$n-$at);if(!$task.Wait(10000)){throw 'Pipe timeout'};$got=$task.Result;if(!$got){throw 'Disconnected'};$at+=$got};return ,$b}
function Call($command,$params=@{}){
 $c=Get-Content -LiteralPath (Join-Path $data 'settings-connection.json') -Raw -Encoding UTF8|ConvertFrom-Json
 $s=[IO.Pipes.NamedPipeClientStream]::new('.',$c.pipe.Substring(9),[IO.Pipes.PipeDirection]::InOut,[IO.Pipes.PipeOptions]::Asynchronous)
 try{$s.Connect(5000);$b=[Text.Encoding]::UTF8.GetBytes((@{protocol=1;token=$c.token;command=$command;params=$params}|ConvertTo-Json -Depth 16 -Compress));$s.Write([BitConverter]::GetBytes([int]$b.Length),0,4);$s.Write($b,0,$b.Length);$n=[BitConverter]::ToInt32((ReadExact $s 4),0);$r=[Text.Encoding]::UTF8.GetString((ReadExact $s $n))|ConvertFrom-Json;$s.WriteByte(1);if(!$r.ok){throw ($r.code+': '+$r.error)};return $r}finally{$s.Dispose()}
}
function FindId($id){$ui.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$id))}
function FindName($name){$ui.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$name))}
function Invoke($e){if(!$e){throw 'UI element missing'};$e.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke();Start-Sleep -Milliseconds 250}
function SelectPage($id){(FindId $id).GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();Start-Sleep -Milliseconds 1000}
function ScrollTop {
 $all=$ui.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::IsScrollPatternAvailableProperty,$true));foreach($e in $all){if($e.Current.AutomationId -notmatch 'MenuItems|FooterItems'){$pattern=$e.GetCurrentPattern([Windows.Automation.ScrollPattern]::Pattern);if($pattern.Current.VerticallyScrollable){$pattern.SetScrollPercent(-1,0);break}}};Start-Sleep -Milliseconds 500
}
function Value($e,$value){$e.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($value);Start-Sleep -Milliseconds 200}
function Expand($e){$e.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand();Start-Sleep -Milliseconds 450}
function Snap($name){[void][LayoutProbe]::SetForegroundWindow($handle);Start-Sleep -Milliseconds 350;$r=$ui.Current.BoundingRectangle;$b=[Drawing.Bitmap]::new([int]$r.Width,[int]$r.Height);$g=[Drawing.Graphics]::FromImage($b);try{$g.CopyFromScreen([int]$r.X,[int]$r.Y,0,0,$b.Size);$b.Save((Join-Path $run ($name+'.png')),[Drawing.Imaging.ImageFormat]::Png)}finally{$g.Dispose();$b.Dispose()}}
$checks=[Collections.Generic.List[object]]::new()
function Check($name,$passed){$checks.Add(@{name=$name;passed=[bool]$passed});Write-Output ($name+': '+$passed);if(!$passed){throw ('Failed '+$name)}}
if($Phase -eq 'Setup'){
 $data=Join-Path $run ('data-'+(Get-Date -Format 'HHmmss'));New-Item -ItemType Directory -Path (Join-Path $data 'clipboard'),(Join-Path $data 'mods') -Force|Out-Null
 [IO.File]::WriteAllText((Join-Path $run 'translation-mode.txt'),'success')
 $portFile=Join-Path $run 'translation-port.txt';if(Test-Path -LiteralPath $portFile){Remove-Item -LiteralPath $portFile}
 $python=(Get-Command python).Source;$server=Start-Process -FilePath $python -ArgumentList ('"'+(Join-Path $branch 'tools/ui-layout-local-server.py')+'" "'+$run+'"') -PassThru -WindowStyle Hidden
 WaitFor {Test-Path (Join-Path $run 'translation-port.txt')}|Out-Null;$port=Get-Content (Join-Path $run 'translation-port.txt')
 $entry=@{id='ui-layout-fixture';text='Synthetic text for WinUI translation tests.';original='';translation='';target='';source='Synthetic UI test';created=1789900000000;version=1;modified=$false}
 $prefs=@{listening=$false;show=$false;save=$true;clearOnExit=$false;maximum=20;excluded='';target='zh-Hans';translationEndpoint="http://127.0.0.1:$port/translate";translationOffline=$false}
 $json=@{format=1;preferences=$prefs;entries=@($entry)}|ConvertTo-Json -Depth 8 -Compress
 $encrypted=[Security.Cryptography.ProtectedData]::Protect([Text.Encoding]::UTF8.GetBytes($json),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser);[IO.File]::WriteAllBytes((Join-Path $data 'clipboard/history.dat'),$encrypted)
 Copy-Item -LiteralPath (Join-Path $branch 'examples/example-button.wimod') -Destination (Join-Path $data 'mods/example-button.wimod')
 Copy-Item -LiteralPath (Join-Path $branch 'test-fixtures/scene-fixture.wimod') -Destination (Join-Path $data 'mods/scene-fixture.wimod')
 $product=Get-Content (Join-Path $branch 'product-version.json') -Raw -Encoding UTF8|ConvertFrom-Json
 $hostProcess=Start-Process -FilePath (Join-Path $branch ('release/'+$product.executable)) -ArgumentList ('--verify "'+$data+'"') -PassThru -WindowStyle Hidden
 WaitFor {Test-Path (Join-Path $data 'settings-connection.json')}|Out-Null
 $state=@{data=$data;hostPid=$hostProcess.Id;serverPid=$server.Id};$state|ConvertTo-Json|Set-Content (Join-Path $run 'state.json') -Encoding UTF8
 Write-Output $data;Call 'mods.action' @{action='enable';id='example-button'}|Out-Null
 exit
}
$state=Get-Content (Join-Path $run 'state.json') -Raw -Encoding UTF8|ConvertFrom-Json;$data=$state.data
if($Phase -eq 'Restart'){
 $exitFile=Join-Path $data 'exit.request';if(Test-Path -LiteralPath $exitFile){Remove-Item -LiteralPath $exitFile}
 $readyFile=Join-Path $data 'settings-connection.json';if(Test-Path -LiteralPath $readyFile){Remove-Item -LiteralPath $readyFile}
 $product=Get-Content (Join-Path $branch 'product-version.json') -Raw -Encoding UTF8|ConvertFrom-Json
 $hostProcess=Start-Process -FilePath (Join-Path $branch ('release/'+$product.executable)) -ArgumentList ('--verify "'+$data+'"') -PassThru -WindowStyle Hidden
 $state.hostPid=$hostProcess.Id;$state.uiPid=0;$state|ConvertTo-Json|Set-Content (Join-Path $run 'state.json') -Encoding UTF8
 WaitFor {Test-Path -LiteralPath $readyFile}|Out-Null;exit
}
if($Phase -eq 'Cleanup'){
 if($state.uiPid){$w=[LayoutProbe]::Find($state.uiPid);if($w -ne [IntPtr]::Zero){[void][LayoutProbe]::PostMessage($w,0x10,[IntPtr]::Zero,[IntPtr]::Zero)}}
 [IO.File]::WriteAllText((Join-Path $data 'exit.request'),'exit');if($state.serverPid){Stop-Process -Id $state.serverPid -ErrorAction SilentlyContinue};exit
}
if($Phase -eq 'Baseline' -or !$state.uiPid -or !(Get-Process -Id $state.uiPid -ErrorAction SilentlyContinue)){
 $c=Get-Content (Join-Path $data 'settings-connection.json') -Raw -Encoding UTF8|ConvertFrom-Json
 $path=if($Phase -eq 'Baseline'){'release/settings/WinIslandSettings.exe'}else{'settings-winui/build/Release/WinIslandSettings.exe'}
 $p=Start-Process -FilePath (Join-Path $branch $path) -ArgumentList ('--pipe="'+$c.pipe+'" --token="'+$c.token+'"') -PassThru -WindowStyle Hidden
 $state|Add-Member -NotePropertyName uiPid -NotePropertyValue $p.Id -Force;$state|ConvertTo-Json|Set-Content (Join-Path $run 'state.json') -Encoding UTF8
}
$handle=WaitFor {$w=[LayoutProbe]::Find($state.uiPid);if($w -ne [IntPtr]::Zero){$w}};$ui=[Windows.Automation.AutomationElement]::FromHandle($handle)
try{
 if($Phase -eq 'Baseline'){
  SelectPage 'nav-5';$items=@('导入模组','扫描','打开插件目录')|ForEach-Object {$e=FindName $_;[pscustomobject]@{name=$_;rect=$e.Current.BoundingRectangle}}
  $items|ConvertTo-Json -Depth 5|Set-Content (Join-Path $run 'baseline-buttons.json') -Encoding UTF8;Snap 'baseline-plugins';Check 'Original buttons reproduced as vertical stack' ($items[0].rect.Y -lt $items[1].rect.Y -and $items[1].rect.Y -lt $items[2].rect.Y)
  Expand (FindId 'mod-example-button');Check 'Original dedicated settings button missing' ($null -eq (FindId 'mod-settings-example-button'));Snap 'baseline-mod-details';[void][LayoutProbe]::PostMessage($handle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
 }elseif($Phase -eq 'Plugins'){
  [void][LayoutProbe]::SetWindowPos($handle,[IntPtr]::Zero,100,80,1000,820,4);SelectPage 'nav-5'
  $buttons=@('plugins-import','plugins-scan','plugins-folder','plugins-back')|ForEach-Object {FindId $_}
  Check 'Wide plugin actions share one row' ((@($buttons|ForEach-Object {$_.Current.BoundingRectangle.Y}|Select-Object -Unique)).Count -eq 1)
  Check 'Module settings available in collapsed card' ((FindId 'mod-settings-example-button').Current.IsEnabled)
  Check 'Unavailable module settings keeps disabled button' (!(FindId 'mod-settings-scene-fixture').Current.IsEnabled)
  Value (FindId 'plugin-search') 'example';$before=$ui.Current.BoundingRectangle
  $focus=FindId 'mod-settings-example-button';$focus.SetFocus();Invoke $focus;WaitFor {FindId 'mod-settings-back'}|Out-Null
  Check 'Module overlay covers navigation without resizing window' (!(FindId 'nav-5').Current.IsEnabled -and $ui.Current.BoundingRectangle.Width -eq $before.Width)
  Check 'Module resource editor is present' ($null -ne (FindName '问候文字'));Snap 'module-settings'
  Value (FindId 'mod-input-greeting-text') 'UI layout test greeting';Invoke (FindName '保存模组设置')
  WaitFor {((Call 'mods.list').resources|Where-Object {$_.key -eq 'greeting-text'}).value -eq 'UI layout test greeting'}|Out-Null
  Check 'Module settings save reaches host resource' $true
  Invoke (FindId 'mod-settings-back');Start-Sleep -Milliseconds 500
  Check 'Back restores search and keyboard focus' ((FindId 'plugin-search').GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value -eq 'example' -and (FindId 'mod-settings-example-button').Current.HasKeyboardFocus)
  for($i=0;$i -lt 4;$i++){Invoke (FindId 'mod-settings-example-button');Invoke (FindId 'mod-settings-back');Start-Sleep -Milliseconds 250}
  Check 'Repeated open/back retains a single functional page' ((FindId 'nav-5').Current.IsEnabled -and $null -eq (FindId 'mod-settings-back'))
  Value (FindId 'plugin-search') '';[void][LayoutProbe]::SetWindowPos($handle,[IntPtr]::Zero,100,80,620,820,4);Start-Sleep -Milliseconds 800
  $buttons=@('plugins-import','plugins-scan','plugins-folder','plugins-back')|ForEach-Object {FindId $_};$ys=@($buttons|ForEach-Object {$_.Current.BoundingRectangle.Y}|Select-Object -Unique)
  Check 'Narrow toolbar uses two rows' ($ys.Count -eq 2)
  Check 'Narrow back button remains in window' ($buttons[3].Current.BoundingRectangle.Right -le $ui.Current.BoundingRectangle.Right)
  Snap 'plugins-narrow';[void][LayoutProbe]::SetWindowPos($handle,[IntPtr]::Zero,100,80,1000,820,4)
 }elseif($Phase -eq 'Transfer'){
  $fixture=Join-Path $data 'synthetic-transfer.txt';[IO.File]::WriteAllText($fixture,'Synthetic transfer fixture')
  $entry=(Call 'transfer.call' @{action='import';paths=@($fixture)}).ids[0]
  Call 'transfer.call' @{action='choose';ids=@($entry);mode='saved'}|Out-Null
  WaitFor {(Call 'transfer.call' @{action='list'}).entries|Where-Object {$_.id -eq $entry -and $_.state -eq 'ready'}}|Out-Null
  SelectPage 'nav-6';Check 'Transfer toolbar is horizontal' ((FindId 'add-files').Current.BoundingRectangle.Y -eq (FindId 'transfer-refresh').Current.BoundingRectangle.Y)
  Expand (WaitFor {FindId ('file-'+$entry)});$buttons=@('打开原位置','打开保存位置','取消复制','从中转站移除（保留文件）')|ForEach-Object {FindName $_}
  Check 'Four detail actions use equal width two columns' ($buttons[0].Current.BoundingRectangle.Y -eq $buttons[1].Current.BoundingRectangle.Y -and $buttons[2].Current.BoundingRectangle.Y -eq $buttons[3].Current.BoundingRectangle.Y -and $buttons[0].Current.BoundingRectangle.Y -lt $buttons[2].Current.BoundingRectangle.Y -and [Math]::Abs($buttons[0].Current.BoundingRectangle.Width-$buttons[1].Current.BoundingRectangle.Width) -lt 2)
  Snap 'transfer-details';[void][LayoutProbe]::SetWindowPos($handle,[IntPtr]::Zero,100,80,620,820,4);Start-Sleep -Milliseconds 700;Snap 'transfer-narrow';Check 'Narrow transfer actions stay within window' ((FindName '打开保存位置').Current.BoundingRectangle.Right -le $ui.Current.BoundingRectangle.Right)
  [void][LayoutProbe]::SetWindowPos($handle,[IntPtr]::Zero,100,80,1000,820,4)
 }elseif($Phase -eq 'Clipboard'){
  SelectPage 'nav-clipboard';ScrollTop;$switches=@('clipboard-listening','clipboard-show','clipboard-save','clipboard-clearOnExit')|ForEach-Object {FindId $_}
  Snap 'clipboard-debug';

  Check 'Clipboard switches arranged as two by two' ($switches[0].Current.BoundingRectangle.Y -eq $switches[1].Current.BoundingRectangle.Y -and $switches[2].Current.BoundingRectangle.Y -eq $switches[3].Current.BoundingRectangle.Y -and $switches[0].Current.BoundingRectangle.Y -lt $switches[2].Current.BoundingRectangle.Y)
  Check 'Large permanent endpoint input removed' ($null -eq (FindId 'clipboard-translation-endpoint'))
  Snap 'clipboard-overview';$original=(Call 'clipboard.call' @{action='list'}).preferences.translationEndpoint
  Invoke (FindId 'clipboard-endpoint-open');Check 'Server editor opens in current window' ($null -ne (FindId 'clipboard-endpoint-input'));Value (FindId 'clipboard-endpoint-input') 'invalid address';Invoke (FindId 'PrimaryButton')
  Check 'Invalid server address keeps editor open' ($null -ne (FindId 'clipboard-endpoint-input'));Snap 'endpoint-validation'
  Invoke (FindId 'SecondaryButton');Check 'Returning discards server draft' ((Call 'clipboard.call' @{action='list'}).preferences.translationEndpoint -eq $original)
  Invoke (FindId 'clipboard-endpoint-open');Value (FindId 'clipboard-endpoint-input') $original;Invoke (FindId 'PrimaryButton');WaitFor {$null -eq (FindId 'clipboard-endpoint-input')}|Out-Null
  Check 'Confirmed address saved through IPC' ((Call 'clipboard.call' @{action='list'}).preferences.translationEndpoint -eq $original)
  $id='ui-layout-fixture';Expand (FindId ('clipboard-card-'+$id));WaitFor {(FindId ('clipboard-editor-'+$id)).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value -match 'Synthetic'}|Out-Null
  [IO.File]::WriteAllText((Join-Path $run 'translation-mode.txt'),'success');$count=(Call 'clipboard.call' @{action='list'}).entries.Count
  Invoke (FindId ('clipboard-translate-'+$id));Check 'Immediate translating dialog appears' ($null -ne (FindId 'clipboard-translation-loading'));Snap 'translation-loading'
  WaitFor {FindName '翻译完成'} 20|Out-Null;Check 'Translation adds a separate history record' ((Call 'clipboard.call' @{action='list'}).entries.Count -eq $count+1);Check 'Original text preserved' ((Call 'clipboard.call' @{action='get';id=$id}).entry.text -eq 'Synthetic text for WinUI translation tests.')
  Snap 'translation-completed';Invoke (FindId 'CloseButton')
  [IO.File]::WriteAllText((Join-Path $run 'translation-mode.txt'),'error');Invoke (FindId ('clipboard-translate-'+$id));WaitFor {FindName '翻译未完成'} 20|Out-Null
  Check 'Failed translation offers retry and cleans loading state' ($null -ne (FindName '重试') -and $null -eq (FindId 'clipboard-translation-loading'));Snap 'translation-failed';Invoke (FindId 'CloseButton')
  [IO.File]::WriteAllText((Join-Path $run 'translation-mode.txt'),'slow');$count=(Call 'clipboard.call' @{action='list'}).entries.Count;Invoke (FindId ('clipboard-translate-'+$id));Invoke (FindId 'CloseButton');Start-Sleep -Milliseconds 1200
  Check 'Cancel closes spinner and does not create result' ($null -eq (FindId 'clipboard-translation-loading') -and (Call 'clipboard.call' @{action='list'}).entries.Count -eq $count)
  SelectPage 'nav-about';Check 'About entry remains keyboard accessible' ($null -ne (FindId 'nav-about'));Snap 'about-current'
 }
 elseif($Phase -eq 'Themes'){
  SelectPage 'nav-clipboard';ScrollTop;(FindId 'appearance-dark').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle();Start-Sleep -Milliseconds 500;Snap 'clipboard-light'
  Invoke (FindId 'clipboard-endpoint-open');Snap 'endpoint-light';Invoke (FindId 'SecondaryButton')
  SelectPage 'nav-4';ScrollTop;$toggle=FindId 'reduceMotion';$toggle.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
  WaitFor {(Call 'settings.read').settings.reduceMotion}|Out-Null
  SelectPage 'nav-5';Invoke (FindId 'mod-settings-example-button');Invoke (FindId 'mod-settings-back');Check 'Reduced-motion module return restores usable navigation' ((FindId 'nav-5').Current.IsEnabled)
  SelectPage 'nav-clipboard';ScrollTop;Invoke (FindId 'clipboard-endpoint-open');Check 'Reduced-motion dialog keeps input and return controls' ($null -ne (FindId 'clipboard-endpoint-input'));Snap 'endpoint-reduced-motion';Invoke (FindId 'SecondaryButton')
  SelectPage 'nav-4';ScrollTop;(FindId 'reduceMotion').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle();Start-Sleep -Milliseconds 400
  (FindId 'appearance-dark').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
 }
 elseif($Phase -eq 'Inspect' -or $Phase -eq 'InspectHere'){
  if($Phase -eq 'Inspect'){SelectPage 'nav-5'};Snap 'inspect-current';$elements=$ui.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition);$elements|ForEach-Object {[pscustomobject]@{id=$_.Current.AutomationId;name=$_.Current.Name;type=$_.Current.ControlType.ProgrammaticName;rect=$_.Current.BoundingRectangle;enabled=$_.Current.IsEnabled}}|ConvertTo-Json -Depth 4|Set-Content (Join-Path $run 'ui-tree.json') -Encoding UTF8
  Write-Output ('DPI='+[LayoutProbe]::GetDpiForWindow($handle));Call 'mods.list'|ConvertTo-Json -Depth 10
 }
}catch{$checks.Add(@{name='exception';passed=$false;error=$_.Exception.Message;line=$_.InvocationInfo.ScriptLineNumber});Write-Output $_.ScriptStackTrace;Write-Output $_.Exception.Message}
$checks|ConvertTo-Json -Depth 6|Set-Content (Join-Path $run ($Phase+'-results.json')) -Encoding UTF8
if($checks|Where-Object {!$_.passed}){exit 1}
