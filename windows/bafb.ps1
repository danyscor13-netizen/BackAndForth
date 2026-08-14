# BackAndForth bootable-ISO builder for Windows.
#
# grub-mkrescue is not native to Windows. This script finds it in MSYS2 or
# falls back to WSL, and says exactly what to install when it finds neither,
# rather than failing with a bare "command not found".

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Show-Usage {
    @'
usage: bafb <file.o> -o <bootable-output.iso>

  bafb bafOS.o -o bafOS.iso
'@
}

function Fail([string]$Message, [int]$Code = 1) {
    [Console]::Error.WriteLine("bafb: $Message")
    exit $Code
}

$InputPath = $null
$Output = $null

for ($i = 0; $i -lt $args.Count; $i++) {
    switch ($args[$i]) {
        { $_ -in '-h', '--help' } { Show-Usage; exit 0 }
        '--version' { Write-Output 'BackAndForthBootable 0.7.1'; exit 0 }
        '-o' {
            if ($i + 1 -ge $args.Count) { Fail "'-o' requires a path" 2 }
            $Output = $args[++$i]
        }
        default {
            if ($args[$i] -like '-*') { Fail "unknown option '$($args[$i])'" 2 }
            if ($InputPath) { Fail 'only one input file is supported' 2 }
            $InputPath = $args[$i]
        }
    }
}

if (-not $InputPath -or -not $Output) {
    [Console]::Error.WriteLine((Show-Usage | Out-String)); exit 2
}
if (-not (Test-Path -LiteralPath $InputPath -PathType Leaf)) {
    Fail "input file not found: $InputPath"
}
if ($Output -notlike '*.iso') {
    Fail 'output must use the .iso extension' 2
}

$GrubNative = if ($env:GRUB_MKRESCUE) { $env:GRUB_MKRESCUE } else { 'grub-mkrescue' }
$HaveNative = [bool](Get-Command $GrubNative -ErrorAction SilentlyContinue)
$HaveWsl = $false
if (-not $HaveNative -and (Get-Command wsl -ErrorAction SilentlyContinue)) {
    & wsl -e sh -c 'command -v grub-mkrescue' *> $null
    $HaveWsl = ($LASTEXITCODE -eq 0)
}

if (-not $HaveNative -and -not $HaveWsl) {
    [Console]::Error.WriteLine('bafb: grub-mkrescue is required')
    [Console]::Error.WriteLine('bafb: install MSYS2 and run: pacman -S grub xorriso')
    [Console]::Error.WriteLine('bafb: or install WSL and run: sudo apt install grub-pc-bin grub-common xorriso')
    exit 1
}

$Temp = Join-Path ([System.IO.Path]::GetTempPath()) ("bafb-" + [guid]::NewGuid())
$IsoRoot = Join-Path $Temp 'iso'
New-Item -ItemType Directory -Path (Join-Path $IsoRoot 'boot\grub') -Force | Out-Null

try {
    Copy-Item -LiteralPath $InputPath -Destination (Join-Path $IsoRoot 'boot\bafOS.o')

    # GRUB reads this from an ISO9660 image, so it must use LF endings.
    $cfg = "set timeout=0`nset default=0`n`nmenuentry `"bafOS`" {`n    multiboot /boot/bafOS.o`n    boot`n}`n"
    [System.IO.File]::WriteAllText((Join-Path $IsoRoot 'boot\grub\grub.cfg'), $cfg)

    $OutputDir = Split-Path -Parent $Output
    if ($OutputDir) { New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null }

    if ($HaveNative) {
        & $GrubNative -o $Output $IsoRoot
    } else {
        function To-WslPath([string]$p) {
            $full = [System.IO.Path]::GetFullPath($p)
            (& wsl wslpath -a -u $full.Replace('\', '/')).Trim()
        }
        $outFull = [System.IO.Path]::GetFullPath($Output)
        & wsl -e grub-mkrescue -o (To-WslPath $outFull) (To-WslPath $IsoRoot)
    }
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Remove-Item -Recurse -Force $Temp -ErrorAction SilentlyContinue
}

Write-Output "wrote $Output"
