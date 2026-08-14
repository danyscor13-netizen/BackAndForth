# Changelog

## 0.7.1

### Fixed

- `--osDev` failed to link with `undefined symbol: memcpy`. The string helpers
  added in 0.7.0 used `llvm.memcpy`, which lowers to a call into libc — which a
  freestanding kernel does not have. `runtime/i386-abi.ll` now copies bytes
  itself, and `runtime/i386-core.c` defines `memcpy`, `memmove`, `memset` and
  `memcmp` so that anything else the compiler lowers to them also resolves.
- `docs/INSTALL_WINDOWS.md` now leads with the PowerShell install, which needs
  no build tools beyond LLVM, and treats building a `Setup.exe` with Inno Setup
  as the optional path it is. Added a table of the errors ISCC actually reports
  and what each one means.

## 0.7.0

The control-flow and expression release. 0.6 could loop and switch, but it
could not branch, could not compare two numbers, and could not return a value
from a function. It can now.

### Added

- `if` / `elsif` / `else`, with any number of `elsif` branches. `elif` and
  `else if` are accepted as spellings of `elsif`. Conditions must be `bool`;
  braces are always required.
- The rest of the operators: `-`, `*`, `/`, `%`, comparisons `== != < <= > >=`,
  short-circuiting `&&` and `||`, and unary `!` and `-`. Precedence follows the
  table in `docs/GRAMMAR.md`; every binary operator is left associative.
- Functions can return values: `func -> f(n: int) : int { return n * 2 }`.
  The return type may also be written with `->`. Omitting it infers the type
  from the `return` statements, or `void` when there are none. The analyser
  proves that every path through a value-returning function reaches a `return`.
- `for (init; condition; step)`, with all three parts optional. The initialiser
  may declare a loop-scoped variable. `continue` runs the step, so a `for` loop
  always advances.
- `break` and `continue`, valid only inside a `while` or `for`. Note that a
  `switch` is not a break target: cases never fall through, so `break` inside a
  `switch` leaves the enclosing *loop*.
- Compound assignment: `+=`, `-=`, `*=`, `/=`, `%=`.
- String concatenation with `+`. When either side is a `str`, an `int` or
  `bool` operand is converted automatically, so `"v" + 7 + " ok=" + true`
  works.
- `==` and `!=` on strings compare contents rather than addresses.
- Block comments, `/* ... */`.
- A string library — `Str.Length`, `Str.Concat`, `Str.Sub`, `Str.FromInt`,
  `Str.FromBool`, `Str.ToInt`, `Str.Equals` — and `Math.Abs`, `Math.Min`,
  `Math.Max`. Lowercase `str.*` and `math.*` aliases are accepted. The string
  entry points are hand-written LLVM IR added to all three runtimes and back
  onto a fixed 64 KiB bump arena, so they need no allocator and work
  identically hosted and freestanding.
- `tools/editor/index.html`: BackAndForth Studio, a single-file Monaco editor
  with highlighting, completion, hover help, a live checker, fourteen themes,
  and file open/save. Documented in `docs/EDITOR.md`.
- A Windows installer. `installer/BackAndForth.iss` builds an Inno Setup
  package that registers the install directory on `PATH` (per-user, or
  system-wide when elevated) and removes it again on uninstall.
  `installer/install.ps1` and `installer/uninstall.ps1` do the same job without
  Inno Setup. See `docs/INSTALL_WINDOWS.md`.
- `docs/LANGUAGE.md`, a complete language reference.
- `make test-language`, which compiles a suite of programs and JIT-runs them
  against `runtime/posix.ll`, checking their output, verifying that invalid
  programs are rejected, and verifying the i386 IR for every case. It needs
  llvmlite rather than clang, and is part of `make test`.
- Examples: `control_flow.baf`, `functions.baf`, `strings.baf`, `fizzbuzz.baf`.

### Changed

- `x / 0` and `x % 0` are defined to produce `0` instead of being undefined
  behaviour. Division by zero used to compile to an LLVM `sdiv` that would
  fault, which killed a freestanding kernel outright.
- Calls to user-defined functions now carry the callee's return type, so a
  function call is a normal expression.

### Fixed

- Variable and parameter slots were allocated with `alloca` wherever the
  declaration appeared, so declaring a variable inside a loop body pushed a new
  slot on every iteration and a long-running loop exhausted the stack. All
  slots are now emitted in the function's entry block.

## 0.6.1

### Added

- Windows host support: `runtime/windows.ll`, `windows/baf.cmd`,
  `windows/bafb.cmd`, and `windows/make.bat`.
- `baf --exe` links a native executable against the platform's hosted runtime,
  and `--exe --run` links and runs it. `--exe` and `--osDev` are rejected
  together.
- Real console colours in hosted builds: ANSI SGR on POSIX,
  `SetConsoleTextAttribute` on Windows. Colours are reset on exit.
- `make test-hosted`, which JIT-compiles both hosted runtimes and unit tests
  them against a mocked kernel32, so the Windows runtime is covered from any
  platform. Included in `make test`; skips cleanly without llvmlite.
- `make install-windows`, which stages a copyable Windows tree.
- `docs/WINDOWS.md` and `LICENSE` (MIT).

### Fixed

- `.gitignore` no longer excludes `runtime/*.ll`. The `*.ll` rule matched the
  hand-written runtime sources, so a fresh clone could not link anything.
- `make install` now installs `runtime/posix.ll`, `runtime/windows.ll`, and
  `runtime/i386-vga.ll`. Previously no hosted runtime was installed at all,
  so `make path` produced a tree that could not link a hosted program.
- `inpt()` strips CR on Windows, so CRLF input no longer leaves a trailing
  `\r` on every string.
- `docs/I386_BOOT.md` described the 0.3 asset list. It now matches what the
  build actually compiles, and records that `runtime/i386-vga.ll` and
  `arch/i386/debugcon.S` are unused leftovers.

## 0.6.0

- Added variadic `putsc`, `putl`, and `inpt`.
- Added automatic console formatting for `str`, `int`, and `bool`.
- Added `[include "path"]` for files and directories.
- Added relative nested includes, duplicate suppression, and cycle detection.
- Fixed the shared `inpt()` buffer corruption bug with one buffer per lexical call site.
- Made `BAF_HOME` override stale local wrappers.
- Added `baf --version`, `bafb --version`, and `bafc --version`.
- Added a complete `make path` installation target.
- Added hosted and i386 tests for variadic output, input-buffer independence, includes, and cycle errors.

## 0.5.2

- Added VGA foreground and background color APIs.
