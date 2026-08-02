# Removes XR_APILAYER_CPVR_probe from the implicit OpenXR API layers, in both the machine and the
# user scope. Every other registered layer is left alone, and the recorded logs are kept.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$keys = @{
    Machine = 'HKLM:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit'
    User    = 'HKCU:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit'
}

$removed = 0
foreach ($scope in 'Machine', 'User') {
    $key = $keys[$scope]
    if (-not (Test-Path $key)) { continue }
    foreach ($p in (Get-ItemProperty $key).PSObject.Properties) {
        if ($p.Name -like 'PS*') { continue }
        if ($p.Name -notlike '*XR_APILAYER_CPVR_probe.json') { continue }
        try {
            Remove-ItemProperty -Path $key -Name $p.Name -Force
            Write-Host "Removed ($scope): $($p.Name)" -ForegroundColor Yellow
            $removed++
        } catch {
            Write-Warning "Could not remove from $scope (elevation needed?): $($p.Name)"
        }
    }
}

if ($removed -eq 0) {
    Write-Host "The probe layer was not registered in either scope."
} else {
    Write-Host "$removed entry/entries removed. Logs in $env:LOCALAPPDATA\xrprobe are kept."
}
