# Builds bafc.exe on Windows without needing make.
#
# Works with clang-cl, clang, or MSVC cl. The compiler is pure C17 with no
# platform dependencies, so the only Windows-specific part is the toolchain.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Build = Join-Path $Root 'build'
$Sources = Get-ChildItem (Join-Path $Root 'src\*.c') | ForEach-Object { $_.FullName }
$Include = Join-Path $Root 'include'
$Output = Join-Path $Build 'bafc.exe'

New-Item -ItemType Directory -Path $Build -Force | Out-Null

$Compiler = $null
foreach ($c in @('clang', 'gcc', 'clang-cl', 'cl')) {
    if (Get-Command $c -ErrorAction SilentlyContinue) { $Compiler = $c; break }
}
if (-not $Compiler) {
    [Console]::Error.WriteLine('build: no C compiler found.')
    [Console]::Error.WriteLine('build: install LLVM (https://releases.llvm.org), MSYS2 mingw-w64 gcc, or Visual Studio Build Tools.')
    exit 1
}

Write-Output "building bafc.exe with $Compiler"

if ($Compiler -eq 'cl') {
    # MSVC has no -std=c17 spelling that matches clang's; /std:c17 is correct.
    & cl /nologo /std:c17 /W3 /I $Include $Sources /Fe:$Output /Fo:"$Build\" 
} else {
    & $Compiler -std=c17 -Wall -Wextra -Wpedantic -g "-I$Include" $Sources -o $Output
}

if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output "wrote $Output"
