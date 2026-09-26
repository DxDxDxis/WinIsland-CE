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
