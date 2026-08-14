# Builds bafc.exe and then compiles installer\BackAndForth.iss with Inno Setup.
#
#     powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1
#
# Requirements:
#   * clang (LLVM for Windows) on PATH, to build the compiler itself
#   * Inno Setup 6, for ISCC.exe
#
# The finished installer lands in installer\Output.

[CmdletBinding()]
param(
    [string]$Iscc = "",
    [switch]$SkipCompilerBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Find-Iscc {
    if ($Iscc -and (Test-Path $Iscc)) { return $Iscc }
    $onPath = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $candidates = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles}\Inno Setup 6\ISCC.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

if (-not $SkipCompilerBuild) {
    Write-Host "==> Building bafc.exe" -ForegroundColor Cyan
    & "$root\windows\make.bat"
    if ($LASTEXITCODE -ne 0) { throw "the compiler build failed" }
}

$binary = Join-Path $root "build\bafc.exe"
if (-not (Test-Path $binary)) {
    throw "build\bafc.exe is missing. Run windows\make.bat first, or drop the -SkipCompilerBuild switch."
}

$compiler = Find-Iscc
if (-not $compiler) {
    throw "ISCC.exe was not found. Install Inno Setup 6, or pass -Iscc <path to ISCC.exe>."
}

Write-Host "==> Compiling the installer with $compiler" -ForegroundColor Cyan
& $compiler "$root\installer\BackAndForth.iss"
if ($LASTEXITCODE -ne 0) { throw "ISCC reported an error" }

$output = Get-ChildItem (Join-Path $root "installer\Output") -Filter "*.exe" |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
Write-Host ""
Write-Host "Installer written to $($output.FullName)" -ForegroundColor Green
