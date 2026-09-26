param([Parameter(Mandatory=$true)][string]$Output,[string]$RuntimeDirectory)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression
$base=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$roots=@{'lyric-provider'="$base/lyric-provider";'settings'="$base/release/settings";'licenses'="$base/release/licenses"}
if($RuntimeDirectory){$roots['settings']=Join-Path $RuntimeDirectory 'settings';$roots['lyric-provider']=Join-Path $RuntimeDirectory 'lyric-provider';$roots['licenses']=Join-Path $RuntimeDirectory 'licenses'}
if(!(Test-Path -LiteralPath "$base/release/settings/resources/app.asar")){throw 'Package the current Electron settings first'}
$stream=[IO.File]::Open($Output,[IO.FileMode]::Create)
$zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create)
$manifest=@()
try {
 foreach($prefix in ($roots.Keys | Sort-Object)){
  $folder=[IO.Path]::GetFullPath($roots[$prefix])
  if(!(Test-Path -LiteralPath $folder)){continue}
  foreach($file in (Get-ChildItem -LiteralPath $folder -Recurse -File | Sort-Object FullName)){
   if($file.Name -eq 'debug.log'){continue}
   $relative=$prefix+'/'+$file.FullName.Substring($folder.Length+1).Replace('\','/')
   $entry=$zip.CreateEntry($relative,[IO.Compression.CompressionLevel]::Optimal)
   $entry.LastWriteTime=[DateTimeOffset]::new(2026,1,1,0,0,0,[TimeSpan]::Zero)
   $input=[IO.File]::OpenRead($file.FullName);$outputStream=$entry.Open()
   try{$input.CopyTo($outputStream)}finally{$input.Dispose();$outputStream.Dispose()}
   $manifest += [ordered]@{path=$relative;size=$file.Length;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
  }
 }
 $entry=$zip.CreateEntry('bundle-manifest.json');$entry.LastWriteTime=[DateTimeOffset]::new(2026,1,1,0,0,0,[TimeSpan]::Zero)
 $writer=[IO.StreamWriter]::new($entry.Open(),[Text.UTF8Encoding]::new($false))
 try{$writer.Write(([ordered]@{format=2;files=$manifest}|ConvertTo-Json -Depth 5 -Compress))}finally{$writer.Dispose()}
}finally{$zip.Dispose();$stream.Dispose()}
Write-Output "Embedded runtime: $($manifest.Count) files"
