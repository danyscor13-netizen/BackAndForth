/* Multiboot1 header and 32-bit entry point for BackAndForth OS. */

.set MULTIBOOT_MAGIC,    0x1BADB002
/*
 * Bits 0 and 1 request page-aligned modules and a memory map.
 * Bit 16 enables explicit physical load addresses. This lets QEMU load the
 * flat kernel image without interpreting the LLD-produced ELF layout.
 */
.set MULTIBOOT_FLAGS,    0x00010003
.set MULTIBOOT_CHECKSUM, -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

.section .multiboot, "a"
.align 4
.global multiboot_header
multiboot_header:
.long MULTIBOOT_MAGIC
.long MULTIBOOT_FLAGS
.long MULTIBOOT_CHECKSUM
.long multiboot_header
.long __kernel_load_addr
.long __kernel_load_end
.long __kernel_bss_end
.long _start

.section .text
.code32
.global _start
.type _start, @function
.extern baf.begin

_start:
    cli
    mov $stack_top, %esp
    xor %ebp, %ebp
    call baf.begin

.Lhalt:
    hlt
    jmp .Lhalt

.size _start, . - _start

.section .stack, "aw", @nobits
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .note.GNU-stack,"",@progbits
