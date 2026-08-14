# BackAndForth on Windows

BackAndForth builds and runs natively on Windows. The compiler itself is
portable C17, so the only Windows-specific pieces are the hosted runtime and
the driver scripts.

| Piece | POSIX | Windows |
| --- | --- | --- |
| compiler | `build/bafc` | `build\bafc.exe` |
| hosted runtime | `runtime/posix.ll` | `runtime\windows.ll` |
| driver | `baf` | `windows\baf.cmd` |
| ISO builder | `bafb` | `windows\bafb.cmd` |
| build | `make` | `windows\make.bat` |

The freestanding i386 target is identical on both platforms. A kernel does not
care which operating system cross-compiled it.

## Requirements

Only a C compiler is needed to build `bafc.exe`. `windows\make.bat` accepts
any of clang, gcc (MSYS2 mingw-w64), clang-cl, or MSVC `cl`, in that order.

Beyond that, each feature pulls in what it needs:

| Feature | Needs |
| --- | --- |
| `baf program.baf` (LLVM IR) | nothing extra |
| `baf program.baf --exe` | clang |
| `baf program.baf --osDev` | clang, `ld.lld`, `llvm-objcopy` |
| `baf os.o --run` | `qemu-system-i386` |
| `bafb os.o -o os.iso` | `grub-mkrescue` via MSYS2 or WSL |

The simplest way to get clang, `ld.lld`, and `llvm-objcopy` together is the
official LLVM Windows installer from <https://releases.llvm.org>. Tick the
option that adds LLVM to `PATH`.

## Build

```bat
git clone <repo>
cd BackAndForth-0.7.0
windows\make.bat
```

That writes `build\bafc.exe`. To confirm:

```bat
build\bafc.exe --version
windows\baf.cmd --version
```

If you are staging from a Linux checkout instead, `make install-windows`
produces a self-contained tree you can copy across:

```sh
make install-windows WINDIST=/tmp/baf-windows
```

Then run `windows\make.bat` inside that copied tree.

## Compile and run

Hosted LLVM IR:

```bat
windows\baf.cmd program.baf -o program.ll
```

Native executable:

```bat
windows\baf.cmd program.baf --exe -o program.exe
windows\baf.cmd program.baf --exe --run
```

Freestanding i386 image, then boot it:

```bat
windows\baf.cmd system.baf --osDev -o system.o
windows\baf.cmd system.o --run
```

With a disk attached:

```bat
fsutil file createnew disk.img 16777216
windows\baf.cmd system.o --run --disk disk.img
windows\baf.cmd system.o --run --disk disk.img --ahci
```

Bootable GRUB ISO:

```bat
windows\bafb.cmd system.o -o system.iso
```

## Putting `baf` on PATH

There is no `make path` equivalent. Either add the `windows` directory to your
`PATH`, or create a shim somewhere already on it:

```bat
setx PATH "%PATH%;C:\path\to\BackAndForth-0.7.0\windows"
```

`BAF_HOME` works the same as on POSIX and takes priority over the directory the
script lives in, so a stale copy on `PATH` cannot silently select an older
compiler:

```bat
set BAF_HOME=C:\path\to\BackAndForth-0.7.0
```

## What the Windows runtime does differently

`runtime\windows.ll` exposes exactly the same 27 `@baf.*` entry points as
`runtime\posix.ll`, so any program compiles unchanged. Three behaviours are
deliberately better than the POSIX runtime rather than merely equivalent:

**Console colours are real.** `Console.SetTextColor` and
`Console.SetTextBackgroundColor` call `SetConsoleTextAttribute`. VGA colour
numbers 0-15 use the same nibble encoding as Windows console attributes, so a
program produces the same colours hosted on Windows as it does on bafOS.
Setting the foreground preserves the background and vice versa.

**`Console.Clear` clears the real screen buffer** with
`FillConsoleOutputCharacter`, instead of writing an ANSI escape that a plain
conhost window would print literally. When output is redirected to a file or
pipe there is no screen to clear, so the call does nothing.

**`inpt()` strips CR.** A CRLF line ending does not leave a stray `\r` on the
end of every string the program reads, which matters because Windows tools
produce CRLF by default. Input is still limited to 255 bytes per call, and each
lexical `inpt()` call site still gets its own buffer.

The console attribute is also restored before the process exits, so a program
that ends mid-colour does not leave your shell recoloured.

The BAFS1 disk APIs are freestanding-only and remain stubs in hosted builds,
exactly as they are on POSIX. `Disk.*` calls print a notice and return false.

## Target support

`runtime\windows.ll` targets **x86_64** Windows, which is what clang produces by
default. The kernel32 imports use the default C calling convention, which is
correct on x64. A 32-bit Windows build would need `x86_stdcallcc` on every
kernel32 declaration in that file.

## Testing the runtime

`tests/hosted/runtime_test.py` JIT-compiles both hosted runtimes and calls the
entry points directly, with kernel32 replaced by `tests/hosted/mock-kernel32.c`.
Because of that mock, the Windows runtime can be tested from any platform:

```sh
pip install llvmlite
make test-hosted
```

The suite skips itself cleanly when llvmlite is not installed.

## Known gaps

- `windows\bafb.cmd` depends on MSYS2 or WSL for `grub-mkrescue`; there is no
  native Windows build of it. The script detects both and names the exact
  package to install when it finds neither.
- QEMU's `gtk` display backend is used for `--run`. If your QEMU build lacks
  it, set `QEMU` to a wrapper or use `--headless`.
- 32-bit Windows hosts are not supported. See "Target support" above.
