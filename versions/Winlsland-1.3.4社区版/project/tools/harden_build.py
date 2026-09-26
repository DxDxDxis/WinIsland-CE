from pathlib import Path
p=Path('build-release.ps1');s=p.read_text('utf-8-sig')
s=s.replace("$ErrorActionPreference='Stop'",'''$ErrorActionPreference='Stop'
function Assert-BranchPath([string]$Path){
 $root=[IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\\')+'\\'
 $resolved=[IO.Path]::GetFullPath($Path)
 if(!$resolved.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)){throw "Output outside the 1.3.4 community release branch: $resolved"}
}''',1)
s=s.replace('if(Test-Path -LiteralPath $releaseSettings){Remove-Item', 'Assert-BranchPath $releaseSettings\nif(Test-Path -LiteralPath $releaseSettings){Remove-Item').replace('if(Test-Path -LiteralPath $buildSettings){Remove-Item','Assert-BranchPath $buildSettings\nif(Test-Path -LiteralPath $buildSettings){Remove-Item')
p.write_text(s,encoding='utf-8-sig')
p=Path('settings-winui/build.ps1');s=p.read_text('utf-8-sig');s += '''
# Deploy the release CRT app-locally, without requiring a preinstalled VC runtime.
if($Configuration -eq 'Release'){
 $redist=Get-ChildItem -LiteralPath (Join-Path $BuildTools 'VC/Redist/MSVC') -Directory | Where-Object {$_.Name -match '^14\\.'} | Sort-Object Name -Descending | Select-Object -First 1
 if(!$redist){throw 'VC release redistributable not found'}
 $crt=Join-Path $redist.FullName 'x64/Microsoft.VC143.CRT'
 if(!(Test-Path -LiteralPath $crt)){throw 'VC x64 release CRT not found'}
 Get-ChildItem -LiteralPath $crt -File -Filter '*.dll' | Copy-Item -Destination $output -Force
}
''';p.write_text(s,encoding='utf-8-sig')
