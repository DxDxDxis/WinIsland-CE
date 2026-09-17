$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$qq=Get-Process QQ | Where-Object {$_.MainWindowHandle -ne 0} | Select-Object -First 1
if(!$qq){'QQ main window not available';exit}
$root=[Windows.Automation.AutomationElement]::FromHandle($qq.MainWindowHandle)
$all=$root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)
"QQ accessible descendants=$($all.Count); all message text redacted"
for($i=0;$i -lt [Math]::Min($all.Count,350);$i++) {
 $c=$all[$i].Current
 $nameKind=if($c.Name -match '^(QQ|消息|新消息|消息通知|回复|发送|设置|聊天|聊天记录|联系人|消息列表|屏幕阅读器|文件传输助手)$'){$c.Name}elseif($c.Name -match '^\d{1,2}:\d{2}$'){'clock'}else{"redacted_length=$($c.Name.Length)"}
 [pscustomobject]@{Index=$i;Type=$c.ControlType.ProgrammaticName;Class=$c.ClassName;Id=$c.AutomationId;NameKind=$nameKind;Bounds=$c.BoundingRectangle.ToString();Offscreen=$c.IsOffscreen}|ConvertTo-Json -Compress
}
