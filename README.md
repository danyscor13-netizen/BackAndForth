# BackAndForth 0.7.1 — control flow, real expressions, an editor, and a Windows installer

BackAndForth (BAF) is a small LLVM-backed systems language that compiles hosted
programs and freestanding 32-bit Multiboot operating systems from the same
source.

0.6.1 added a Windows host, native linking, and working hosted colours.
0.7.0 fills in what the language was still missing: branching, the rest of the
operators, functions that return values, and enough of a string library to
write programs that manipulate text.

- Language reference: `docs/LANGUAGE.md`
- Grammar: `docs/GRAMMAR.md`
- Editor: `docs/EDITOR.md`
- Windows install: `docs/INSTALL_WINDOWS.md`

## New: `if` / `elsif` / `else`

```baf
func -> describe(n: int) : str {
    if (n < 0) {
        return "negative"
    } elsif (n == 0) {
        return "zero"
    } elsif (n % 2 == 0) {
        return "even"
    } else {
        return "odd"
    }
}
```

Any number of `elsif` branches; the trailing `else` is optional. `elif` and
`else if` mean the same thing. Conditions must be `bool` — there is no
truthiness — and braces are always required, so there is no dangling-else
ambiguity to reason about.

## New: the rest of the expression language

0.6 had `+` on integers and nothing else. 0.7 has:

```text
+ - * / %          arithmetic
== != < <= > >=    comparison
&& || !            logic, with short circuiting
- (unary)          negation
```

Precedence runs from `||` (loosest) to unary operators and calls (tightest);
all binary operators associate to the left. `&&` and `||` evaluate the right
side only when the answer still depends on it.

`x / 0` and `x % 0` are **defined to produce `0`**. They used to compile to a
raw `sdiv`, which is undefined behaviour in LLVM and took a freestanding kernel
down with it.

## New: functions that return values

```baf
func -> factorial(n: int) : int {
    if (n <= 1) { return 1 }
    return n * factorial(n - 1)
}

// The return type can be left off; it is inferred from the returns.
func -> isPositive(n: int) {
    return n > 0
}

begin {
    putsc("6! = " + factorial(6))
}
```

The return type goes after `:` (or `->`) and may be `int`, `str`, `bool` or
`void`. A function with no annotation and no value-returning `return` is
`void`, exactly as before. The analyser proves that every path through a
value-returning function ends in a `return`:

```text
program.baf:4:9: error: function 'area' returns int, but some paths reach the
                        end of its body without a return
```

## New: `for`, `break`, `continue`

```baf
int total = 0
for (int i = 1; i <= 10; i += 1) {
    if (i % 3 == 0) { continue }
    if (i > 8) { break }
    total += i
}
```

All three parts of the header are optional, so `for (;;) { }` loops forever.
The initialiser may declare a variable scoped to the loop. `continue` jumps to
the step, so a `for` loop always advances.

One deliberate difference from C: a `switch` is not a break target. Cases never
fall through, so `break` inside a `switch` leaves the enclosing *loop*.

## New: string concatenation and a string library

```baf
begin {
    str name = "BackAndForth"
    int version = 7

    putsc("version " + version + ", ready = " + true)
    putsc(Str.Sub(name, 4, 3) + " has length " + Str.Length(name))

    if (name == "BackAndForth") {
        putsc("== compares contents, not addresses")
    }
}
```

When either side of `+` is a `str`, the operator concatenates and converts an
`int` or `bool` operand automatically. `==` and `!=` on strings compare
contents.

```text
Str.Length(text) -> int          Str.FromInt(value) -> str
Str.Concat(a, b) -> str          Str.FromBool(value) -> str
Str.Sub(text, start, count)      Str.ToInt(text) -> int
Str.Equals(a, b) -> bool         Math.Abs / Math.Min / Math.Max
```

These are hand-written LLVM IR in all three runtimes, backed by a fixed 64 KiB
bump arena rather than an allocator, so the same code runs hosted and inside a
kernel. The arena wraps when it fills: keep concatenation near where the result
is used rather than accumulating thousands of built strings.

## New: compound assignment and block comments

```baf
count += 1      // also -= *= /= %=

/* block comments
   span lines now */
```

## New: BackAndForth Studio

`tools/editor/index.html` is a single-file editor built on Monaco, the editor
from VS Code. Open it in a browser — there is no build step and no server.

Syntax highlighting for every 0.7 construct, completion and snippets for the
keywords and all the built-ins, hover help, bracket matching, fourteen themes,
open/save through the File System Access API with a download fallback, and a
Problems panel driven by a checker that runs as you type (unbalanced brackets,
unterminated strings and comments, a missing or duplicated `begin`, `=` where
`==` was meant). See `docs/EDITOR.md`.

## New: Windows installer

`installer/BackAndForth.iss` builds an Inno Setup package that installs the
compiler, runtimes, editor, examples and docs, and **adds the install directory
to `PATH`** — per-user by default, machine-wide when run elevated — removing it
again on uninstall.

```bat
powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1
```

Without Inno Setup, `installer\install.ps1` does the same job from source:

```bat
powershell -ExecutionPolicy Bypass -File installer\install.ps1
powershell -ExecutionPolicy Bypass -File installer\uninstall.ps1
```

See `docs/INSTALL_WINDOWS.md`.

## Fixed: `alloca` inside loops leaked stack

Variable slots were emitted wherever the declaration appeared, so a variable
declared inside a loop body pushed a fresh stack slot on every iteration and a
long-running loop eventually ran out of stack. All slots now live in the
function's entry block.

## Variadic console I/O

`putsc`, `putl`, and `inpt` accept any number of `str`, `int`, and `bool`
arguments.

```baf
begin {
    int version = 7
    bool ready = true

    putsc("BackAndForth ", version, " ready=", ready)

    str name = inpt("File ", version, ": ")
    putsc("Opening ", name)
}
```

```text
putsc(...) -> prints every value, then one newline
putl(...)  -> prints every value without a newline
inpt(...)  -> prints every value, then reads a string
```

Zero arguments are valid. Named arguments are rejected for these calls because
output order is significant.

## Includes

```baf
[include "lib/console.baf"]   // one file
[include "commands"]          // every direct .baf file in a directory
```

Paths are relative to the file containing the directive. Nested includes work,
duplicates are ignored, and cycles are errors. See `docs/INCLUDES.md`.

## Install on Unix

```sh
make
make path
hash -r
```

`make path` installs `baf`, `bafb`, `bafc`, the hosted and i386 runtimes, and
the boot assets under `/usr/local`.

## Build and test

```sh
make clean
make
make test
make test-kernel32
```

`make test` runs the driver and script tests, `make test-hosted` (which
JIT-compiles both hosted runtimes and unit tests them against a mocked
kernel32), and `make test-language` (which compiles a suite of programs,
JIT-runs them against `runtime/posix.ll`, checks their output, checks that
invalid programs are rejected, and verifies the i386 IR for every case). The
last two need `pip install llvmlite` and skip themselves cleanly without it.

## Compile

```sh
baf program.baf -o program.ll     # hosted LLVM IR
baf program.baf --exe -o program  # native executable
baf program.baf --exe --run       # link it and run it
bafc program.baf --check          # analyse only

baf system.baf --osDev -o system.o   # freestanding i386 image
baf system.o --run                   # boot it in QEMU
bafb system.o -o system.iso          # bootable GRUB ISO
```

With a disk attached:

```sh
truncate -s 16M disk.img
baf system.o --run --disk disk.img          # IDE
baf system.o --run --disk disk.img --ahci   # AHCI/SATA
```

## OS APIs

Unchanged from 0.6:

- VGA text console, scrolling, cursor, and 16-colour foreground/background;
- PS/2 keyboard input;
- reboot and shutdown;
- IDE/PATA LBA28/LBA48 reads and writes;
- AHCI/SATA DMA reads and writes;
- BAFS1 files and directories;
- `Disk.Scan`, `Disk.Select`, `Disk.Write`, `Disk.Read`, `Disk.Rem`;
- `Disk.CreateDir`, `Disk.GotoDir`, and `Disk.GetDir`;
- BIOS/GRUB Multiboot images.

UEFI, NVMe, interrupts, owned general-purpose strings, and multitasking are not
implemented yet.

## License

MIT. See `LICENSE`.
