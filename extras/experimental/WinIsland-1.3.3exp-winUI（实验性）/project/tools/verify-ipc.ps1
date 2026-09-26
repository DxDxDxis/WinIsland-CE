param([ValidateSet('write','restart','restore')][string]$Phase='write')
$ErrorActionPreference='Stop'
$branch=Split-Path $PSScriptRoot
$t=Get-Content -LiteralPath (Join-Path $branch 'verification/active-test.json') -Raw|ConvertFrom-Json
$data=Join-Path $t.root '安装数据 中文 spaces'
function ReadExact($stream,[int]$n){$b=[byte[]]::new($n);$at=0;while($at -lt $n){$job=$stream.ReadAsync($b,$at,$n-$at);if(!$job.Wait(10000)){throw 'read timeout'};$got=$job.Result;if(!$got){throw 'disconnected'};$at+=$got};return ,$b}
function Call($command,$params=@{}){
 $c=Get-Content -LiteralPath (Join-Path $data 'settings-connection.json') -Raw|ConvertFrom-Json
 $s=[IO.Pipes.NamedPipeClientStream]::new('.',$c.pipe.Substring(9),[IO.Pipes.PipeDirection]::InOut,[IO.Pipes.PipeOptions]::Asynchronous)
 try{$s.Connect(5000);$b=[Text.Encoding]::UTF8.GetBytes((@{protocol=1;token=$c.token;command=$command;params=$params}|ConvertTo-Json -Depth 16 -Compress));$s.Write([BitConverter]::GetBytes([int]$b.Length),0,4);$s.Write($b,0,$b.Length);$n=[BitConverter]::ToInt32((ReadExact $s 4),0);if($n -le 0 -or $n -gt 4194304){throw 'bad response'};$r=[Text.Encoding]::UTF8.GetString((ReadExact $s $n))|ConvertFrom-Json;$s.WriteByte(1);if(!$r.ok){throw ($r.code+': '+$r.error)};return $r}finally{$s.Dispose()}
}
$checks=[Collections.Generic.List[object]]::new()
function Check($name,$ok){$checks.Add(@{name=$name;passed=[bool]$ok});Write-Output ($name+': '+$ok);if(!$ok){throw ('FAILED '+$name)}}
try{
 $v=Get-Content -LiteralPath (Join-Path $branch 'product-version.json') -Raw|ConvertFrom-Json
 Check 'capabilities/version' ((Call 'capabilities.read').version -eq $v.display)
 $s=Call 'settings.read'
 $originalFile=Join-Path $t.root 'original-test-preferences.json'
 if($Phase -eq 'restore'){
  if(!(Test-Path -LiteralPath $originalFile)){throw 'No original preferences snapshot; do not guess user choices'}
  $original=Get-Content -LiteralPath $originalFile -Raw|ConvertFrom-Json
  Call 'settings.write' @{revision=$s.revision;patch=@{showMusic=$original.showMusic;showMessages=$original.showMessages}}|Out-Null
  Call 'clipboard.call' @{action='preferences';preferences=$original.clipboard}|Out-Null
  Call 'transfer.call' @{action='enable';enabled=$original.transferEnabled}|Out-Null
  Check 'original test preferences restored' $true
 } elseif($Phase -eq 'write' -and !(Test-Path -LiteralPath $originalFile)){
  @{showMusic=$s.settings.showMusic;showMessages=$s.settings.showMessages;clipboard=(Call 'clipboard.call' @{action='list'}).preferences;transferEnabled=(Call 'transfer.call' @{action='list'}).enabled}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $originalFile -Encoding utf8
 }
 if($Phase -eq 'write'){
  $r=Call 'settings.write' @{revision=$s.revision;patch=@{showMusic=$false;showMessages=$false}}
  Check 'settings persisted' (!$r.settings.showMusic -and !$r.settings.showMessages)
  Check 'plugins list' ((Call 'mods.list').mods.Count -eq 0)
  $r=Call 'clipboard.call' @{action='list'}
  Check 'clipboard preferences' ($r.preferences.maximum -eq 20)
  $r.preferences.show=$false
  Call 'clipboard.call' @{action='preferences';preferences=$r.preferences}|Out-Null
  Check 'clipboard preferences saved' (!(Call 'clipboard.call' @{action='list'}).preferences.show)
  $a=Join-Path $t.root 'reference 中文.txt';$b=Join-Path $t.root 'saved 中文.md'
  [IO.File]::WriteAllText($a,'reference fixture');[IO.File]::WriteAllText($b,'# saved fixture')
  $ids=(Call 'transfer.call' @{action='import';paths=@($a,$b)}).ids
  Check 'batch transfer pending' ($ids.Count -eq 2)
  Call 'transfer.call' @{action='choose';ids=@($ids[0]);mode='reference'}|Out-Null
  Call 'transfer.call' @{action='choose';ids=@($ids[1]);mode='saved'}|Out-Null
  $deadline=[DateTime]::UtcNow.AddSeconds(10)
  do{$list=Call 'transfer.call' @{action='list'};if(($list.entries|Where-Object {$_.state -eq 'ready'}).Count -eq 2){break};Start-Sleep -Milliseconds 150}while([DateTime]::UtcNow -lt $deadline)
  Check 'reference and saved copies ready' (($list.entries|Where-Object {$_.state -eq 'ready'}).Count -eq 2)
  $saved=$list.entries|Where-Object {$_.mode -eq 'saved'}
  Check 'saved copy hash' ((Get-FileHash -LiteralPath $b).Hash -eq (Get-FileHash -LiteralPath $saved.saved).Hash)
  Check 'source preserved' ((Test-Path -LiteralPath $a) -and (Test-Path -LiteralPath $b))
  Call 'transfer.call' @{action='enable';enabled=$true}|Out-Null
 }elseif($Phase -eq 'restart'){
  Check 'settings restored' (!$s.settings.showMusic -and !$s.settings.showMessages)
  Check 'clipboard preference restored' (!(Call 'clipboard.call' @{action='list'}).preferences.show)
  $list=Call 'transfer.call' @{action='list'}
  Check 'transfer index and enabled restored' ($list.enabled -and $list.entries.Count -eq 2)
 }
 [IO.File]::WriteAllText((Join-Path $data 'command.txt'),'settings')
}catch{$checks.Add(@{name='exception';passed=$false;error=$_.Exception.Message});Write-Output $_.Exception.Message}
$checks|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $t.root ('ipc-'+$Phase+'.json')) -Encoding utf8

# Restore user-visible choices after the restart assertions, including a failed run.
if($Phase -eq 'restart' -and (Test-Path -LiteralPath $originalFile)){ & $PSCommandPath -Phase restore }

if($checks|Where-Object {!$_.passed}){exit 1}
