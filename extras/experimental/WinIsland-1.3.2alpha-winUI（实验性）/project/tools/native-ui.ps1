param([string]$Action='tree',[string]$Id,[string]$Value,[string]$Name)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$root=Split-Path $PSScriptRoot
$active=Get-Content -LiteralPath (Join-Path $root 'verification/native-live/active.json') -Raw | ConvertFrom-Json
$condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty,[int]$active.settingsPid)
$window=[Windows.Automation.AutomationElement]::RootElement.FindFirst([Windows.Automation.TreeScope]::Children,$condition)
if(!$window){throw 'Native settings window not found'}
$element=$window
if($Id){$element=$window.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$Id));if(!$element){throw "Missing UI Automation element: $Id"}}
if($Name){$element=$window.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$Name));if(!$element){throw "Missing UI Automation element named: $Name"}}
switch($Action){
 'tree' {$element.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)|ForEach-Object { [pscustomobject]@{id=$_.Current.AutomationId;name=$_.Current.Name;type=$_.Current.ControlType.ProgrammaticName;enabled=$_.Current.IsEnabled;offscreen=$_.Current.IsOffscreen} }|ConvertTo-Json -Depth 3}
 'select' {([Windows.Automation.SelectionItemPattern]$element.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern)).Select()}
 'toggle' {([Windows.Automation.TogglePattern]$element.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern)).Toggle()}
 'invoke' {([Windows.Automation.InvokePattern]$element.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern)).Invoke()}
 'expand' {([Windows.Automation.ExpandCollapsePattern]$element.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern)).Expand()}
 'value' {([Windows.Automation.ValuePattern]$element.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern)).SetValue($Value)}
 'close' {([Windows.Automation.WindowPattern]$window.GetCurrentPattern([Windows.Automation.WindowPattern]::Pattern)).Close()}
 'screenshot' {
  Add-Type -AssemblyName System.Drawing
  $bounds=$window.Current.BoundingRectangle
  $bmp=[Drawing.Bitmap]::new([int]$bounds.Width,[int]$bounds.Height);$g=[Drawing.Graphics]::FromImage($bmp)
  try{$g.CopyFromScreen([int]$bounds.X,[int]$bounds.Y,0,0,$bmp.Size);$bmp.Save((Join-Path $root ('verification/native-live/'+$Value+'.png')),[Drawing.Imaging.ImageFormat]::Png)}finally{$g.Dispose();$bmp.Dispose()}
 }
}
