/* Mirror characters to QEMU's debug port 0xE9 for terminal-visible output. */

.section .text
.code32
.global baf.debug_putc
.type baf.debug_putc, @function

baf.debug_putc:
    mov 4(%esp), %al
    mov $0xE9, %dx
    out %al, (%dx)
    ret

.size baf.debug_putc, . - baf.debug_putc
.section .note.GNU-stack,"",@progbits
