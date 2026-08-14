#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

./scripts/baf-kernel32 examples/kernel32_hello.baf >/dev/null
IR=build/i386/kernel32_hello.ll
KERNEL=build/i386/kernel32_hello.elf
IMAGE=build/i386/kernel32_hello.bin

grep -q 'target triple = "i386-unknown-none"' "$IR"
grep -q '%baf.str = type { ptr, i32 }' "$IR"
grep -q 'define void @baf.begin()' "$IR"

readelf -h "$KERNEL" | grep -q 'Class:.*ELF32'
readelf -h "$KERNEL" | grep -q 'Machine:.*Intel 80386'
readelf -s --wide "$KERNEL" | grep -q ' _start$'
readelf -s --wide "$KERNEL" | grep -q ' baf.begin$'
readelf -s --wide "$KERNEL" | grep -q ' baf.putsc$'

test -s "$IMAGE"
./scripts/baf-check-i386 examples/kernel32_hello.baf >/dev/null
printf 'PASS kernel32_hello\n'

./scripts/baf-kernel32 examples/kernel32_io.baf >/dev/null
IO_IR=build/i386/kernel32_io.ll
IO_KERNEL=build/i386/kernel32_io.elf

grep -q 'call %baf.str @baf.input.read' "$IO_IR"
grep -q 'call i1 @baf.str.eq' "$IO_IR"
grep -q 'while.cond' "$IO_IR"
grep -q 'switch.case' "$IO_IR"
readelf -s --wide "$IO_KERNEL" | grep -q ' baf.input.read$'
readelf -s --wide "$IO_KERNEL" | grep -q ' baf.put.int$'
readelf -s --wide "$IO_KERNEL" | grep -q ' baf.put.bool$'
readelf -s --wide "$IO_KERNEL" | grep -q ' baf.put.newline$'
readelf -s --wide "$IO_KERNEL" | grep -q ' baf.str.eq$'
readelf -s --wide "$IO_KERNEL" | grep -q ' baf.power.shutdown$'
readelf -s --wide "$IO_KERNEL" | grep -q ' baf_core_input_read$'
./scripts/baf-check-i386 examples/kernel32_io.baf >/dev/null
printf 'PASS kernel32_io\n'


./scripts/baf-kernel32 examples/kernel32_disk.baf >/dev/null
DISK_IR=build/i386/kernel32_disk.ll
DISK_KERNEL=build/i386/kernel32_disk.elf

grep -q 'call void @baf.disk.scan' "$DISK_IR"
grep -q 'call void @baf.disk.list' "$DISK_IR"
grep -q 'call void @baf.disk.hex' "$DISK_IR"
grep -q 'call i1 @baf.disk.write' "$DISK_IR"
grep -q 'call %baf.str @baf.disk.read' "$DISK_IR"
grep -q 'call i1 @baf.disk.rem' "$DISK_IR"
grep -q 'call void @baf.disk.files' "$DISK_IR"
grep -q 'call i1 @baf.disk.create_dir' "$DISK_IR"
grep -q 'call i1 @baf.disk.goto_dir' "$DISK_IR"
grep -q 'call %baf.str @baf.disk.get_dir' "$DISK_IR"
readelf -s --wide "$DISK_KERNEL" | grep -q ' baf.disk.scan$'
readelf -s --wide "$DISK_KERNEL" | grep -q ' baf_core_disk_scan$'
readelf -s --wide "$DISK_KERNEL" | grep -q ' baf_core_disk_hex$'
readelf -s --wide "$DISK_KERNEL" | grep -q ' baf_core_fs_write$'
readelf -s --wide "$DISK_KERNEL" | grep -q ' baf_core_fs_read$'
readelf -s --wide "$DISK_KERNEL" | grep -q ' baf_core_fs_remove$'
readelf -s --wide "$DISK_KERNEL" | grep -q ' baf_core_fs_create_dir$'
readelf -s --wide "$DISK_KERNEL" | grep -q ' baf_core_fs_goto_dir$'
readelf -s --wide "$DISK_KERNEL" | grep -q ' baf_core_fs_get_dir$'
readelf -s --wide "$DISK_KERNEL" | grep -q ' ahci_write_sector$'
./scripts/baf-check-i386 examples/kernel32_disk.baf >/dev/null
printf 'PASS kernel32_disk
'

./scripts/baf-kernel32 examples/kernel32_colors.baf >/dev/null
COLOR_IR=build/i386/kernel32_colors.ll
COLOR_KERNEL=build/i386/kernel32_colors.elf

grep -q 'call void @baf.console.set_text_color' "$COLOR_IR"
grep -q 'call void @baf.console.set_background_color' "$COLOR_IR"
readelf -s --wide "$COLOR_KERNEL" | grep -q ' baf.console.set_text_color$'
readelf -s --wide "$COLOR_KERNEL" | grep -q ' baf.console.set_background_color$'
readelf -s --wide "$COLOR_KERNEL" | grep -q ' baf_core_console_set_text_color$'
readelf -s --wide "$COLOR_KERNEL" | grep -q ' baf_core_console_set_background_color$'
./scripts/baf-check-i386 examples/kernel32_colors.baf >/dev/null
printf 'PASS kernel32_colors\n'

./scripts/baf-kernel32 examples/kernel32_variadic.baf >/dev/null
VAR_IR=build/i386/kernel32_variadic.ll
VAR_KERNEL=build/i386/kernel32_variadic.elf

grep -q 'call void @baf.put.int' "$VAR_IR"
grep -q 'call void @baf.put.bool' "$VAR_IR"
grep -q 'call void @baf.put.newline' "$VAR_IR"
grep -q '@.baf.input.0' "$VAR_IR"
grep -q '@.baf.input.1' "$VAR_IR"
readelf -s --wide "$VAR_KERNEL" | grep -q ' baf.put.int$'
readelf -s --wide "$VAR_KERNEL" | grep -q ' baf.put.bool$'
readelf -s --wide "$VAR_KERNEL" | grep -q ' baf.input.read$'
./scripts/baf-check-i386 examples/kernel32_variadic.baf >/dev/null
printf 'PASS kernel32_variadic\n'
