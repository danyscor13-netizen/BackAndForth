#include <stdint.h>

#define VGA_WIDTH 80u
#define VGA_HEIGHT 25u
#define VGA_MEMORY ((volatile uint16_t *)0xB8000u)
static uint8_t console_foreground = 0x0Fu;
static uint8_t console_background = 0x00u;

static uint32_t cursor_row;
static uint32_t cursor_column;
static uint8_t shift_down;

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void debug_putc(char character) {
    outb(0xE9u, (uint8_t)character);
}

static void update_hardware_cursor(void) {
    uint16_t position = (uint16_t)(cursor_row * VGA_WIDTH + cursor_column);
    outb(0x3D4u, 0x0Fu);
    outb(0x3D5u, (uint8_t)(position & 0xFFu));
    outb(0x3D4u, 0x0Eu);
    outb(0x3D5u, (uint8_t)((position >> 8) & 0xFFu));
}

static uint8_t console_attribute(void) {
    return (uint8_t)(((console_background & 0x0Fu) << 4) |
                     (console_foreground & 0x0Fu));
}

static void clear_row(uint32_t row) {
    uint16_t blank = (uint16_t)(' ' | ((uint16_t)console_attribute() << 8));
    for (uint32_t column = 0; column < VGA_WIDTH; column++) {
        VGA_MEMORY[row * VGA_WIDTH + column] = blank;
    }
}

static void scroll_if_needed(void) {
    if (cursor_row < VGA_HEIGHT) return;

    for (uint32_t row = 1; row < VGA_HEIGHT; row++) {
        for (uint32_t column = 0; column < VGA_WIDTH; column++) {
            VGA_MEMORY[(row - 1u) * VGA_WIDTH + column] =
                VGA_MEMORY[row * VGA_WIDTH + column];
        }
    }
    clear_row(VGA_HEIGHT - 1u);
    cursor_row = VGA_HEIGHT - 1u;
}

static void console_backspace(void) {
    if (cursor_column == 0u) {
        if (cursor_row == 0u) return;
        cursor_row--;
        cursor_column = VGA_WIDTH - 1u;
    } else {
        cursor_column--;
    }
    VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_column] =
        (uint16_t)(' ' | ((uint16_t)console_attribute() << 8));
    debug_putc('\b');
    debug_putc(' ');
    debug_putc('\b');
    update_hardware_cursor();
}

static void console_putc(char character) {
    if (character == '\n') {
        debug_putc(character);
        cursor_column = 0u;
        cursor_row++;
        scroll_if_needed();
        update_hardware_cursor();
        return;
    }
    if (character == '\r') {
        debug_putc(character);
        cursor_column = 0u;
        update_hardware_cursor();
        return;
    }
    if (character == '\b') {
        console_backspace();
        return;
    }
    if (character == '\t') {
        uint32_t spaces = 4u - (cursor_column & 3u);
        while (spaces--) console_putc(' ');
        return;
    }

    debug_putc(character);
    VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_column] =
        (uint16_t)((uint8_t)character | ((uint16_t)console_attribute() << 8));
    cursor_column++;
    if (cursor_column >= VGA_WIDTH) {
        cursor_column = 0u;
        cursor_row++;
        scroll_if_needed();
    }
    update_hardware_cursor();
}

void baf_core_console_write(const char *data, uint32_t length,
                            uint32_t append_newline) {
    for (uint32_t i = 0; i < length; i++) console_putc(data[i]);
    if (append_newline) console_putc('\n');
}

void baf_core_console_newline(void) {
    console_putc('\n');
}

void baf_core_console_write_bool(uint32_t value) {
    static const char true_text[] = "true";
    static const char false_text[] = "false";
    if (value) {
        baf_core_console_write(true_text, 4u, 0u);
    } else {
        baf_core_console_write(false_text, 5u, 0u);
    }
}

void baf_core_console_write_i32(int32_t value) {
    char digits[11];
    uint32_t count = 0u;
    uint32_t magnitude;

    if (value < 0) {
        console_putc('-');
        magnitude = (uint32_t)(-(value + 1)) + 1u;
    } else {
        magnitude = (uint32_t)value;
    }

    do {
        digits[count++] = (char)('0' + (magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude != 0u);

    while (count > 0u) console_putc(digits[--count]);
}

void baf_core_console_set_text_color(uint32_t color) {
    console_foreground = (uint8_t)(color & 0x0Fu);
}

void baf_core_console_set_background_color(uint32_t color) {
    console_background = (uint8_t)(color & 0x0Fu);
}

void baf_core_console_clear(void) {
    for (uint32_t row = 0; row < VGA_HEIGHT; row++) clear_row(row);
    cursor_row = 0u;
    cursor_column = 0u;
    update_hardware_cursor();
}

static char translate_scancode(uint8_t scancode) {
    static const char normal[128] = {
        [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
        [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
        [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
        [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
        [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
        [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
        [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
        [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
        [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
        [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c',
        [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
        [0x33] = ',', [0x34] = '.', [0x35] = '/', [0x39] = ' '
    };
    static const char shifted[128] = {
        [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
        [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
        [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
        [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
        [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
        [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
        [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F',
        [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
        [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
        [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C',
        [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M',
        [0x33] = '<', [0x34] = '>', [0x35] = '?', [0x39] = ' '
    };
    return shift_down ? shifted[scancode] : normal[scancode];
}

static uint8_t keyboard_read_scancode(void) {
    while ((inb(0x64u) & 1u) == 0u) {
        __asm__ volatile("pause");
    }
    return inb(0x60u);
}

uint32_t baf_core_input_read(char *buffer, uint32_t capacity) {
    if (capacity == 0u) return 0u;
    uint32_t length = 0u;

    for (;;) {
        uint8_t scancode = keyboard_read_scancode();

        if (scancode == 0x2Au || scancode == 0x36u) {
            shift_down = 1u;
            continue;
        }
        if (scancode == 0xAAu || scancode == 0xB6u) {
            shift_down = 0u;
            continue;
        }
        if ((scancode & 0x80u) != 0u) continue;

        if (scancode == 0x1Cu) {
            buffer[length] = '\0';
            console_putc('\n');
            return length;
        }
        if (scancode == 0x0Eu) {
            if (length > 0u) {
                length--;
                console_backspace();
            }
            continue;
        }

        char character = translate_scancode(scancode);
        if (character != '\0' && length + 1u < capacity) {
            buffer[length++] = character;
            console_putc(character);
        }
    }
}

uint32_t baf_core_string_equal(const char *left, uint32_t left_length,
                               const char *right, uint32_t right_length) {
    if (left_length != right_length) return 0u;
    for (uint32_t i = 0; i < left_length; i++) {
        if (left[i] != right[i]) return 0u;
    }
    return 1u;
}

__attribute__((noreturn)) void baf_core_power_shutdown(void) {
    outw(0x604u, 0x2000u);  /* QEMU ACPI */
    outw(0xB004u, 0x2000u); /* older Bochs/QEMU */
    outw(0x4004u, 0x3400u); /* VirtualBox */
    for (;;) __asm__ volatile("cli; hlt");
}

__attribute__((noreturn)) void baf_core_power_reboot(void) {
    while ((inb(0x64u) & 0x02u) != 0u) {
        __asm__ volatile("pause");
    }
    outb(0x64u, 0xFEu);
    for (;;) __asm__ volatile("cli; hlt");
}

/* ------------------------------------------------------------------------- */
/* Freestanding memory primitives.                                            */
/*                                                                            */
/* There is no libc here, but the compiler may still emit calls to these four */
/* names when it lowers a structure copy or an initialised array. Providing    */
/* them keeps the link self-contained. The volatile pointers stop the         */
/* optimiser from recognising each loop as the very function it is compiling   */
/* and rewriting it into a call to itself.                                     */
/* ------------------------------------------------------------------------- */

void *memcpy(void *destination, const void *source, unsigned long count) {
    volatile unsigned char *out = (volatile unsigned char *)destination;
    const volatile unsigned char *in = (const volatile unsigned char *)source;
    for (unsigned long i = 0; i < count; i++) {
        out[i] = in[i];
    }
    return destination;
}

void *memmove(void *destination, const void *source, unsigned long count) {
    volatile unsigned char *out = (volatile unsigned char *)destination;
    const volatile unsigned char *in = (const volatile unsigned char *)source;
    if (out == in || count == 0) {
        return destination;
    }
    if (out < in) {
        for (unsigned long i = 0; i < count; i++) {
            out[i] = in[i];
        }
    } else {
        for (unsigned long i = count; i > 0; i--) {
            out[i - 1] = in[i - 1];
        }
    }
    return destination;
}

void *memset(void *destination, int value, unsigned long count) {
    volatile unsigned char *out = (volatile unsigned char *)destination;
    unsigned char byte = (unsigned char)value;
    for (unsigned long i = 0; i < count; i++) {
        out[i] = byte;
    }
    return destination;
}

int memcmp(const void *left, const void *right, unsigned long count) {
    const volatile unsigned char *a = (const volatile unsigned char *)left;
    const volatile unsigned char *b = (const volatile unsigned char *)right;
    for (unsigned long i = 0; i < count; i++) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}
