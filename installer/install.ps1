# Installs BackAndForth for the current user without needing Inno Setup, and
# puts it on PATH.
#
#     powershell -ExecutionPolicy Bypass -File installer\install.ps1
#     powershell -ExecutionPolicy Bypass -File installer\install.ps1 -Prefix D:\tools\BackAndForth
#     powershell -ExecutionPolicy Bypass -File installer\install.ps1 -System   # all users, needs admin
#
# Run installer\uninstall.ps1 to undo it, including the PATH entry.

[CmdletBinding()]
param(
    [string]$Prefix = "$env:LOCALAPPDATA\Programs\BackAndForth",
    [switch]$System,
    [switch]$NoPath,
    [switch]$SkipCompilerBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if ($System) {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "-System installs to Program Files and needs an elevated PowerShell."
    }
    if (-not $PSBoundParameters.ContainsKey("Prefix")) {
        $Prefix = "$env:ProgramFiles\BackAndForth"
    }
}

if (-not $SkipCompilerBuild -and -not (Test-Path "$root\build\bafc.exe")) {
    Write-Host "==> Building bafc.exe" -ForegroundColor Cyan
    & "$root\windows\make.bat"
    if ($LASTEXITCODE -ne 0) { throw "the compiler build failed" }
}
if (-not (Test-Path "$root\build\bafc.exe")) {
    throw "build\bafc.exe is missing; build it with windows\make.bat first."
}

Write-Host "==> Installing into $Prefix" -ForegroundColor Cyan
foreach ($directory in @("bin", "runtime", "arch\i386", "editor", "examples", "docs")) {
    New-Item -ItemType Directory -Force -Path (Join-Path $Prefix $directory) | Out-Null
}

Copy-Item "$root\build\bafc.exe" "$Prefix\bin\" -Force
Copy-Item "$root\windows\baf.cmd", "$root\windows\bafb.cmd", `
          "$root\windows\baf.ps1", "$root\windows\bafb.ps1" "$Prefix\bin\" -Force
Copy-Item "$root\runtime\*.ll", "$root\runtime\*.c" "$Prefix\runtime\" -Force
Copy-Item "$root\arch\i386\*" "$Prefix\arch\i386\" -Force
Copy-Item "$root\tools\editor\index.html" "$Prefix\editor\" -Force
Copy-Item "$root\examples\*" "$Prefix\examples\" -Recurse -Force
Copy-Item "$root\docs\*" "$Prefix\docs\" -Recurse -Force
Copy-Item "$root\README.md", "$root\CHANGELOG.md", "$root\LICENSE" "$Prefix\" -Force

if (-not $NoPath) {
    $scope = if ($System) { "Machine" } else { "User" }
    $binDir = Join-Path $Prefix "bin"
    $current = [Environment]::GetEnvironmentVariable("Path", $scope)
    $parts = @()
    if ($current) { $parts = $current -split ";" | Where-Object { $_ -ne "" } }
    if ($parts -notcontains $binDir) {
        [Environment]::SetEnvironmentVariable("Path", (($parts + $binDir) -join ";"), $scope)
        Write-Host "==> Added $binDir to the $scope PATH" -ForegroundColor Green
        Write-Host "    Open a new terminal for it to take effect."
    } else {
        Write-Host "==> $binDir was already on the $scope PATH"
    }
    # Make it usable in this session too.
    $env:Path = "$binDir;$env:Path"
}

Write-Host ""
Write-Host "BackAndForth 0.7.1 installed." -ForegroundColor Green
Write-Host "  baf $Prefix\examples\hello.baf --exe --run"
Write-Host "  editor: $Prefix\editor\index.html"
