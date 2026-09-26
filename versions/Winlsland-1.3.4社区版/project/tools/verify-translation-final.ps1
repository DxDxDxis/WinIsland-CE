$ErrorActionPreference='Stop'
$branch=Split-Path $PSScriptRoot
$run=Join-Path $branch ('verification/translation-final-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path (Join-Path $run 'clipboard') -Force | Out-Null
# Synthetic history only; do not read or overwrite the user's system clipboard.
$entry=@{id='translation-ui-fixture';text='Please close the window before leaving the room.';original='';translation='';target='';source='Synthetic translation test';created=1789900000000;version=1;modified=$false}
$prefs=@{listening=$false;show=$false;save=$true;clearOnExit=$false;maximum=20;excluded='';target='zh-Hans';translationEndpoint='https://154-219-99-106.sslip.io/translation/translate';translationOffline=$false}
$json=@{format=1;preferences=$prefs;entries=@($entry)} | ConvertTo-Json -Depth 6 -Compress
$encrypted=[Security.Cryptography.ProtectedData]::Protect([Text.Encoding]::UTF8.GetBytes($json),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
[IO.File]::WriteAllBytes((Join-Path $run 'clipboard/history.dat'),$encrypted)
$version=Get-Content (Join-Path $branch 'product-version.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$exe=Join-Path $branch ('release/'+$version.executable)
$test=Start-Process -FilePath $exe -ArgumentList ('--clipboard-test "'+$run+'\unit"') -PassThru -WindowStyle Hidden -Wait
Write-Output ('unitExit='+$test.ExitCode)
Get-Content (Join-Path $run 'unit/clipboard-tests.txt')
if($test.ExitCode){throw 'Clipboard regression failed'}
$hostProcess=Start-Process -FilePath $exe -ArgumentList ('--verify "'+$run+'"') -PassThru -WindowStyle Hidden
@{root=$run;pid=$hostProcess.Id;started=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content (Join-Path $branch 'verification/translation-final-active.json') -Encoding utf8
Start-Sleep -Seconds 2
[IO.File]::WriteAllText((Join-Path $run 'command.txt'),'settings')
Write-Output ('testRoot='+$run+' hostId='+$hostProcess.Id)
