# Removes an installation made by installer\install.ps1, including its PATH
# entry.
#
#     powershell -ExecutionPolicy Bypass -File installer\uninstall.ps1
#     powershell -ExecutionPolicy Bypass -File installer\uninstall.ps1 -System   # needs admin

[CmdletBinding()]
param(
    [string]$Prefix = "$env:LOCALAPPDATA\Programs\BackAndForth",
    [switch]$System,
    [switch]$KeepFiles
)

$ErrorActionPreference = "Stop"

if ($System -and -not $PSBoundParameters.ContainsKey("Prefix")) {
    $Prefix = "$env:ProgramFiles\BackAndForth"
}

$scope = if ($System) { "Machine" } else { "User" }
$binDir = Join-Path $Prefix "bin"

$current = [Environment]::GetEnvironmentVariable("Path", $scope)
if ($current) {
    $parts = $current -split ";" | Where-Object { $_ -ne "" -and $_ -ne $binDir }
    [Environment]::SetEnvironmentVariable("Path", ($parts -join ";"), $scope)
    Write-Host "==> Removed $binDir from the $scope PATH" -ForegroundColor Green
}

if (-not $KeepFiles) {
    if (Test-Path $Prefix) {
        Remove-Item $Prefix -Recurse -Force
        Write-Host "==> Deleted $Prefix" -ForegroundColor Green
    } else {
        Write-Host "==> $Prefix does not exist; nothing to delete"
    }
}

Write-Host "Done. Open a new terminal so the PATH change is picked up."
