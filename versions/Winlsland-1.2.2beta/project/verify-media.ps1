param([string]$Executable, [string]$OutputDirectory)
# Compatibility entry: the current test uses real PCM and physical pointer input.
& (Join-Path $PSScriptRoot 'verify-real-music.ps1') -Executable $Executable -OutputDirectory $OutputDirectory