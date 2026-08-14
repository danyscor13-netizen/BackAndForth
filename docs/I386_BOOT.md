# BAF i386 boot target

BAF 0.2.1 produces both a debuggable ELF and a flat Multiboot boot image.

## Pipeline

```text
kernel32_hello.baf
    |
    v
bafc --target i386-freestanding
    |
    v
32-bit LLVM IR
    |
    v
Clang -> i386 object file
    |
    +-- arch/i386/boot.S       Multiboot header, stack, `_start`
    +-- runtime/i386-abi.ll    string and console shims
    +-- runtime/i386-core.c    VGA console, PS/2 keyboard, power
    +-- runtime/i386-disk.c    PCI discovery, IDE/AHCI, BAFS1
    |
    v
LLD -> kernel32_hello.elf
    |
    v
objcopy -> kernel32_hello.bin
```

The flat image uses Multiboot flag bit 16 and embeds `header_addr`,
`load_addr`, `load_end_addr`, `bss_end_addr`, and `entry_addr`. QEMU can
therefore load it without interpreting the ELF program-header layout.

## BAF source

```baf
begin {
    putsc("Hello, World!\n")
}
```

## Build and validate

```sh
make
make test-kernel32
./scripts/baf-check-i386
```

Outputs:

```text
build/i386/kernel32_hello.elf  debugging and symbols
build/i386/kernel32_hello.bin  direct QEMU boot image
```

## Run headlessly

```sh
./scripts/baf-run-i386
```

`Hello, World!` is mirrored to QEMU's debug port and appears in the terminal.
QEMU remains open in the kernel halt loop; stop it with `Ctrl+C`.

## Run with a VGA window

```sh
qemu-system-i386 \
  -machine pc \
  -m 32M \
  -kernel build/i386/kernel32_hello.bin \
  -monitor none \
  -serial none \
  -no-reboot \
  -no-shutdown
```

## Target ABI

- `int` lowers to LLVM `i32`;
- a string is `{ ptr, i32 }`;
- `begin` lowers to `void @baf.begin()`;
- `putsc` has the ABI `void @baf.putsc(%baf.str)`;
- no libc, startup files, dynamic loader, or host system calls are linked.

## Unused assets

`runtime/i386-vga.ll` and `arch/i386/debugcon.S` are left over from 0.2 and
0.3. Nothing in `baf`, `scripts/baf-kernel32`, or the Makefile compiles them
today: the VGA console moved into `runtime/i386-core.c`, and the QEMU debug
port is driven from there too. They are kept for reference and should either be
wired back in or deleted before 0.7.
