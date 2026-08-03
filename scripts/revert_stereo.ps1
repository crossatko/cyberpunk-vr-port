# Undo deploy_stereo.ps1: remove the plugin + CET mod and put back any dxgi.dll that was moved
# out of the load path.
#
# Usage:
#   pwsh scripts\revert_stereo.ps1 -GameRoot "C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077"

param(
    [Parameter(Mandatory=$true)]
    [string]$GameRoot
)

$ErrorActionPreference = "Stop"

$BinX64   = Join-Path $GameRoot "bin\x64"
$Dxgi     = Join-Path $BinX64 "dxgi.dll"
$Stash    = Join-Path $BinX64 "dxgi.dll.disabled_by_stereo"
$ModName  = "CyberpunkVR_Stereo"
$PlugDir  = Join-Path $GameRoot "red4ext\plugins\$ModName"
$CETName  = "CyberpunkVRPort_Stereo"
$CETMods  = Join-Path $BinX64 "plugins\cyber_engine_tweaks\mods\$CETName"

$proc = Get-Process Cyberpunk2077 -ErrorAction SilentlyContinue
if ($proc) { throw "Cyberpunk2077.exe is running -- close it first." }

if (Test-Path $PlugDir) {
    Remove-Item $PlugDir -Recurse -Force
    Write-Host "[+] Removed $PlugDir"
}
if (Test-Path $CETMods) {
    Remove-Item $CETMods -Recurse -Force
    Write-Host "[+] Removed $CETMods"
}
if (Test-Path $Stash) {
    if (Test-Path $Dxgi) { Remove-Item $Dxgi -Force }
    Move-Item $Stash $Dxgi -Force
    Write-Host "[+] Restored bin\x64\dxgi.dll from the stash"
}
Write-Host "[ok] reverted."
