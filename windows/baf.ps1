# BackAndForth driver for Windows.
#
# Feature-for-feature port of the POSIX `baf` shell script. The hosted path
# links against runtime\windows.ll instead of runtime\posix.ll; the --osDev
# path is byte-for-byte the same cross-compile, because the i386 kernel does
# not care which OS built it.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Version = '0.7.1'

function Show-Usage {
    @'
usage: baf <file.baf> [--exe] [--osDev] [-o output] [--run] [--headless] [--disk image] [--ahci]
       baf <kernel-image> --run [--headless] [--disk image] [--ahci]

  baf program.baf                  write hosted LLVM IR to a.ll
  baf program.baf -o out.ll        write hosted LLVM IR to out.ll
  baf program.baf --exe            link a native .exe to a.exe
  baf program.baf --exe -o prog.exe   link a native .exe to prog.exe
  baf program.baf --exe --run      build a.exe and run it
  baf program.baf --osDev          write bootable i386 image to a.o
  baf program.baf --osDev -o os.o  write bootable i386 image to os.o
  baf os.o --run                   boot an existing image in QEMU
  baf os.o --run --disk disk.img   attach a raw IDE disk
  baf os.o --run --disk disk.img --ahci  attach it through AHCI
'@
}

function Fail([string]$Message, [int]$Code = 1) {
    [Console]::Error.WriteLine("baf: $Message")
    exit $Code
}

# ------------------------------------------------------------------ arguments

$Source = $null
$Output = $null
$OsDev = $false
$Exe = $false
$Run = $false
$Headless = $false
$DiskImage = $null
$DiskMode = 'ide'

for ($i = 0; $i -lt $args.Count; $i++) {
    switch ($args[$i]) {
        { $_ -in '-h', '--help' } { Show-Usage; exit 0 }
        '--version' { Write-Output "BackAndForth $Version"; exit 0 }
        '--exe'      { $Exe = $true }
        '--osDev'    { $OsDev = $true }
        '--run'      { $Run = $true }
        '--headless' { $Headless = $true }
        '--ahci'     { $DiskMode = 'ahci' }
        '--disk' {
            if ($i + 1 -ge $args.Count) { Fail "'--disk' requires a raw disk image" 2 }
            $DiskImage = $args[++$i]
        }
        '-o' {
            if ($i + 1 -ge $args.Count) { Fail "'-o' requires a path" 2 }
            $Output = $args[++$i]
        }
        default {
            if ($args[$i] -like '-*') {
                [Console]::Error.WriteLine("baf: unknown option '$($args[$i])'")
                [Console]::Error.WriteLine((Show-Usage | Out-String))
                exit 2
            }
            if ($Source) { Fail 'only one input file is supported' 2 }
            $Source = $args[$i]
        }
    }
}

if (-not $Source) {
    [Console]::Error.WriteLine((Show-Usage | Out-String))
    exit 2
}
if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
    Fail "input file not found: $Source"
}
if ($OsDev -and $Exe) {
    Fail "'--exe' and '--osDev' are mutually exclusive" 2
}

# --------------------------------------------------------------- installation

# BAF_HOME wins over the directory this script happens to live in, so a stale
# copy on PATH cannot silently select an older compiler.
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir

$AssetRoot = $null
$Bafc = $null

if ($env:BAF_HOME) {
    if (-not (Test-Path (Join-Path $env:BAF_HOME 'src'))) {
        Fail "BAF_HOME does not point to a BackAndForth source tree: $env:BAF_HOME"
    }
    $AssetRoot = $env:BAF_HOME
} elseif (Test-Path (Join-Path $ProjectRoot 'src')) {
    $AssetRoot = $ProjectRoot
} elseif (Test-Path (Join-Path $ScriptDir 'runtime')) {
    # Installed layout: bafc and the assets sit beside this script.
    $AssetRoot = $ScriptDir
}

if ($AssetRoot) {
    foreach ($candidate in @(
        (Join-Path $AssetRoot 'build\bafc.exe'),
        (Join-Path $AssetRoot 'build\bafc'),
        (Join-Path $AssetRoot 'bafc.exe'))) {
        if (Test-Path -LiteralPath $candidate) { $Bafc = $candidate; break }
    }
}

if (-not $Bafc) {
    Fail 'cannot find bafc.exe; run windows\make.bat first, or set BAF_HOME'
}

function Require-Tool([string]$Name, [string]$Why) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        Fail "$Name is required for $Why"
    }
}

function Invoke-Checked {
    param([string]$Exe, [string[]]$Arguments)
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# --------------------------------------------------------------------- QEMU

function Start-Image([string]$Image) {
    $Qemu = if ($env:QEMU) { $env:QEMU } else { 'qemu-system-i386' }

    if ($DiskImage -and -not (Test-Path -LiteralPath $DiskImage)) {
        Fail "disk image not found: $DiskImage"
    }
    Require-Tool $Qemu '--run'

    $Display = if ($Headless) { 'none' } else { 'gtk' }
    $Common = @(
        '-m', '32M', '-kernel', $Image,
        '-display', $Display, '-monitor', 'none', '-serial', 'none',
        '-debugcon', 'stdio', '-global', 'isa-debugcon.iobase=0xe9',
        '-no-reboot', '-no-shutdown')

    if (-not $DiskImage) {
        & $Qemu @('-machine', 'pc') @Common
    } elseif ($DiskMode -eq 'ahci') {
        & $Qemu @('-machine', 'q35') @Common @(
            '-device', 'ich9-ahci,id=bafahci',
            '-drive', "file=$DiskImage,format=raw,if=none,id=bafdisk",
            '-device', 'ide-hd,drive=bafdisk,bus=bafahci.0')
    } else {
        & $Qemu @('-machine', 'pc') @Common @(
            '-drive', "file=$DiskImage,format=raw,if=ide,index=0,media=disk")
    }
    exit $LASTEXITCODE
}

if ($Source -notlike '*.baf') {
    if (-not $Run -or $OsDev -or $Exe -or $Output) {
        Fail 'non-.baf inputs are only accepted with --run' 2
    }
    Start-Image $Source
}

# A bare --run means "build an OS image and boot it"; --run --exe means
# "build a native program and execute it".
if ($Run -and -not $OsDev -and -not $Exe) { $OsDev = $true }

# ---------------------------------------------------------------- hosted IR

if (-not $OsDev -and -not $Exe) {
    if (-not $Output) { $Output = 'a.ll' }
    Invoke-Checked $Bafc @($Source, '-o', $Output)
    exit 0
}

# ------------------------------------------------------------ native .exe

if ($Exe) {
    if (-not $Output) { $Output = 'a.exe' }
    $Clang = if ($env:CLANG) { $env:CLANG } else { 'clang' }
    Require-Tool $Clang '--exe'

    $HostRuntime = Join-Path $AssetRoot 'runtime\windows.ll'
    if (-not (Test-Path -LiteralPath $HostRuntime)) {
        Fail "missing hosted runtime: $HostRuntime"
    }

    $Temp = Join-Path ([System.IO.Path]::GetTempPath()) ("baf-exe-" + [guid]::NewGuid())
    New-Item -ItemType Directory -Path $Temp | Out-Null
    try {
        $Ir = Join-Path $Temp 'program.ll'
        Invoke-Checked $Bafc @($Source, '-o', $Ir)
        Invoke-Checked $Clang @('-Wno-override-module', $Ir, $HostRuntime, '-o', $Output)
    } finally {
        Remove-Item -Recurse -Force $Temp -ErrorAction SilentlyContinue
    }

    Write-Output "wrote $Output"
    if ($Run) {
        & (Resolve-Path -LiteralPath $Output)
        exit $LASTEXITCODE
    }
    exit 0
}

# -------------------------------------------------------------- i386 kernel

if (-not $Output) { $Output = 'a.o' }
$Clang = if ($env:CLANG) { $env:CLANG } else { 'clang' }
$LdLld = if ($env:LD_LLD) { $env:LD_LLD } else { 'ld.lld' }

foreach ($required in @(
    'runtime\i386-abi.ll', 'runtime\i386-core.c', 'runtime\i386-disk.c',
    'arch\i386\boot.S', 'arch\i386\linker.ld')) {
    $path = Join-Path $AssetRoot $required
    if (-not (Test-Path -LiteralPath $path)) {
        Fail "missing OS development asset: $path"
    }
}

Require-Tool $Clang '--osDev'
Require-Tool $LdLld '--osDev'

$Objcopy = $null
foreach ($candidate in @($env:OBJCOPY, 'llvm-objcopy', 'objcopy')) {
    if ($candidate -and (Get-Command $candidate -ErrorAction SilentlyContinue)) {
        $Objcopy = $candidate; break
    }
}
if (-not $Objcopy) { Fail 'llvm-objcopy or objcopy is required for --osDev' }

$Temp = Join-Path ([System.IO.Path]::GetTempPath()) ("baf-osdev-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $Temp | Out-Null
try {
    $CommonFlags = @(
        '--target=i386-unknown-none', '-m32', '-ffreestanding',
        '-fno-pic', '-fno-pie', '-fno-stack-protector', '-fno-builtin')

    $Ir      = Join-Path $Temp 'program.ll'
    $ProgObj = Join-Path $Temp 'program.o'
    $AbiObj  = Join-Path $Temp 'runtime-abi.o'
    $CoreObj = Join-Path $Temp 'runtime-core.o'
    $DiskObj = Join-Path $Temp 'runtime-disk.o'
    $BootObj = Join-Path $Temp 'boot.o'
    $Elf     = Join-Path $Temp 'kernel.elf'

    Invoke-Checked $Bafc @($Source, '--target', 'i386-freestanding', '-o', $Ir)
    Invoke-Checked $Clang ($CommonFlags + @('-Wno-override-module', '-c', $Ir, '-o', $ProgObj))
    Invoke-Checked $Clang ($CommonFlags + @('-Wno-override-module', '-c',
        (Join-Path $AssetRoot 'runtime\i386-abi.ll'), '-o', $AbiObj))
    Invoke-Checked $Clang ($CommonFlags + @('-std=c17', '-Wall', '-Wextra', '-Werror', '-c',
        (Join-Path $AssetRoot 'runtime\i386-core.c'), '-o', $CoreObj))
    Invoke-Checked $Clang ($CommonFlags + @('-std=c17', '-Wall', '-Wextra', '-Werror', '-c',
        (Join-Path $AssetRoot 'runtime\i386-disk.c'), '-o', $DiskObj))
    Invoke-Checked $Clang ($CommonFlags + @('-c',
        (Join-Path $AssetRoot 'arch\i386\boot.S'), '-o', $BootObj))

    Invoke-Checked $LdLld @('-m', 'elf_i386', '-nostdlib',
        '-T', (Join-Path $AssetRoot 'arch\i386\linker.ld'),
        $BootObj, $ProgObj, $AbiObj, $CoreObj, $DiskObj, '-o', $Elf)
    Invoke-Checked $Objcopy @('-O', 'binary', $Elf, $Output)
} finally {
    Remove-Item -Recurse -Force $Temp -ErrorAction SilentlyContinue
}

Write-Output "wrote $Output"
if ($Run) { Start-Image $Output }
