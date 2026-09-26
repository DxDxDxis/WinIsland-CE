param([Parameter(Mandatory=$true)][string]$Folder,[Parameter(Mandatory=$true)][string]$OutputFile)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$source=[IO.Path]::GetFullPath($Folder)
$output=[IO.Path]::GetFullPath($OutputFile)
if([IO.Path]::GetExtension($output) -ne '.wimod'){throw 'Output must use .wimod'}
if(!(Test-Path -LiteralPath (Join-Path $source 'mod.json'))){throw 'Missing root mod.json'}
if(Test-Path -LiteralPath $output){throw 'Output exists; select a new package filename'}
New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($output))|Out-Null
[IO.Compression.ZipFile]::CreateFromDirectory($source,$output,[IO.Compression.CompressionLevel]::Optimal,$false)
Get-Item -LiteralPath $output | Select-Object FullName,Length

