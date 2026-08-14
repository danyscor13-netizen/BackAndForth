# Installing BackAndForth on Windows

The short version, and the one that works without any extra build tools:

```powershell
powershell -ExecutionPolicy Bypass -File installer\install.ps1
```

That builds the compiler, copies everything into your profile, and puts `baf`
and `bafc` on your `PATH`. Open a new terminal and you are done. The rest of
this page explains that step by step, and covers the installer build only for
people who actually want to produce a `Setup.exe`.

---

## Step 1 — Install LLVM

The compiler is written in portable C, but it emits LLVM IR, so clang does the
final build in both directions: it compiles `bafc.exe` itself, and it links
your programs into executables.

Download the Windows installer from <https://releases.llvm.org> (the
`LLVM-*-win64.exe` asset) and **tick "Add LLVM to the system PATH"** during
setup. Then, in a **new** terminal:

```powershell
clang --version
```

If that prints a version, you are set. If it says the command is not
recognised, LLVM is installed but not on `PATH`; the easiest fix is to run the
LLVM installer again and select the PATH option.

*Alternative:* if you already have MSYS2 or Visual Studio Build Tools, `gcc` or
`cl` can build `bafc.exe`. But you still need clang to turn a `.baf` program
into an `.exe`, so installing LLVM is the path of least resistance.

## Step 2 — Unpack BackAndForth

Extract the zip anywhere, for example `C:\Users\<you>\Downloads\BackAndForth-0.7.0`.
Open PowerShell in that folder — in Explorer, Shift+Right-click on the folder
and choose "Open PowerShell window here".

## Step 3 — Run the installer script

```powershell
powershell -ExecutionPolicy Bypass -File installer\install.ps1
```

What it does:

1. builds `build\bafc.exe` with whichever C compiler it finds;
2. copies the compiler, the driver scripts, the runtimes, the editor, the
   examples and the docs into
   `%LOCALAPPDATA%\Programs\BackAndForth`;
3. appends `...\BackAndForth\bin` to your **user** `PATH`.

No administrator rights are needed, and nothing outside your own profile is
touched.

`-ExecutionPolicy Bypass` applies to that single command only — it does not
change your machine's script policy. It is needed because PowerShell blocks
downloaded scripts by default.

### Options

| Switch | Effect |
| --- | --- |
| `-Prefix D:\tools\BackAndForth` | install somewhere else |
| `-System` | install for all users under `Program Files` (needs an elevated PowerShell) |
| `-NoPath` | copy the files but leave `PATH` alone |
| `-SkipCompilerBuild` | reuse an existing `build\bafc.exe` |

## Step 4 — Open a new terminal and check

Windows only hands the updated `PATH` to processes started **after** the
change, so the terminal you ran the installer in will not see it. Open a fresh
one:

```powershell
baf --version
bafc --version
```

Both should print `BackAndForth 0.7.0`.

## Step 5 — Build something

```powershell
cd $env:LOCALAPPDATA\Programs\BackAndForth\examples
baf hello.baf --exe --run
```

For the editor, open
`%LOCALAPPDATA%\Programs\BackAndForth\editor\index.html` in your browser —
double-clicking it works, there is nothing to install.

## Uninstalling

```powershell
powershell -ExecutionPolicy Bypass -File installer\uninstall.ps1
```

It removes the directory and takes its entry back out of `PATH`, leaving every
other entry alone. Add `-System` if you installed with `-System`, `-Prefix` if
you installed somewhere custom, and `-KeepFiles` if you only want the `PATH`
entry gone.

---

## Manual install, if you prefer to see every step

```powershell
# 1. build the compiler
.\windows\make.bat

# 2. choose a home and copy the tree
$dest = "$env:LOCALAPPDATA\Programs\BackAndForth"
New-Item -ItemType Directory -Force $dest\bin, $dest\runtime, $dest\editor | Out-Null
Copy-Item build\bafc.exe $dest\bin\
Copy-Item windows\baf.cmd, windows\bafb.cmd, windows\baf.ps1, windows\bafb.ps1 $dest\bin\
Copy-Item runtime\* $dest\runtime\ -Recurse
Copy-Item tools\editor\index.html $dest\editor\

# 3. add it to PATH for your user
$old = [Environment]::GetEnvironmentVariable("Path", "User")
[Environment]::SetEnvironmentVariable("Path", "$old;$dest\bin", "User")
```

Then open a new terminal.

---

## Building a `Setup.exe` (optional)

Only needed if you want to hand someone a double-clickable installer.

1. Install [Inno Setup 6](https://jrsoftware.org/isdl.php).
2. Build the compiler first — the script that packages it expects
   `build\bafc.exe` to exist:

   ```powershell
   .\windows\make.bat
   powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1
   ```

The result lands in `installer\Output`.

**If ISCC reports an error**, it is almost always one of these:

| Message | Cause | Fix |
| --- | --- | --- |
| `ISCC.exe was not found` | Inno Setup is not on `PATH` | pass `-Iscc "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"` |
| `Could not read file ...\build\bafc.exe` | the compiler was never built | run `.\windows\make.bat` first |
| `Could not read file ...\Languages\Italian.isl` | the Italian language file was not selected during Inno Setup's own install | delete the `Name: "italian"` line from the `[Languages]` section of `installer\BackAndForth.iss`, or rerun Inno Setup's installer and include all languages |
| Errors about `HKA` or `IsAdminInstallMode` | Inno Setup 5, not 6 | install version 6; `HKA` and the per-user install mode do not exist in 5 |

If it still refuses, you do not need it. `install.ps1` produces exactly the same
installed tree, including the `PATH` entry — the `.iss` file only exists to wrap
that in a wizard.

---

## What you get, and what you still need

The install gives you the compiler, the driver scripts, the runtimes and the
editor. It does **not** ship a linker or an emulator:

| To do this | You need |
| --- | --- |
| `bafc program.baf` (LLVM IR) | nothing more |
| `baf program.baf --exe` (native `.exe`) | clang |
| `baf program.baf --osDev` (bootable image) | clang, `ld.lld`, `llvm-objcopy` — all in the LLVM install |
| `baf system.o --run` (boot it) | QEMU |

See `docs/WINDOWS.md` for toolchain notes and `docs/I386_BOOT.md` for the
freestanding target.

## Troubleshooting

**`baf : The term 'baf' is not recognized`.** Open a new terminal first. If it
persists, check the entry is really there:

```powershell
[Environment]::GetEnvironmentVariable("Path","User") -split ";" | Select-String BackAndForth
```

**`running scripts is disabled on this system`.** You dropped the
`-ExecutionPolicy Bypass` part. Use the full command as written above.

**`no C compiler found`.** `windows\make.bat` looked for clang, gcc, clang-cl
and cl and found none. Install LLVM (step 1) and open a new terminal.

**`clang is required for --exe`.** The compiler installed fine but clang is not
on `PATH`. Same fix.

**WSL and Windows at once.** They are separate installs. Inside WSL use `make`
and `make path`; the Windows install does not carry over, and a Windows
`bafc.exe` cannot be run by the WSL driver script.
