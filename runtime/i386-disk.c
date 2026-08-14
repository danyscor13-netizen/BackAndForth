#include <stdint.h>
#include <stddef.h>

/* BackAndForth i386 block-device layer.
 *
 * The driver discovers PCI IDE/AHCI controllers, probes ATA disks, and
 * exposes a uniform writable 512-byte sector API.
 * SATA disks are driven through AHCI; there is no separate "SATA driver".
 */

#define BAF_SECTOR_SIZE 512u
#define BAF_MAX_DISKS 8u
#define BAF_MAX_AHCI_PORTS 8u

#define ATA_SR_BSY 0x80u
#define ATA_SR_DRQ 0x08u
#define ATA_SR_DF  0x20u
#define ATA_SR_ERR 0x01u
#define ATA_CMD_IDENTIFY 0xECu
#define ATA_CMD_READ_PIO 0x20u
#define ATA_CMD_READ_PIO_EXT 0x24u
#define ATA_CMD_WRITE_PIO 0x30u
#define ATA_CMD_WRITE_PIO_EXT 0x34u
#define ATA_CMD_READ_DMA_EXT 0x25u
#define ATA_CMD_WRITE_DMA_EXT 0x35u
#define ATA_CMD_FLUSH_CACHE 0xE7u
#define ATA_CMD_FLUSH_CACHE_EXT 0xEAu

#define SATA_SIG_ATA 0x00000101u
#define AHCI_GHC_AE (1u << 31)
#define AHCI_PXCMD_ST (1u << 0)
#define AHCI_PXCMD_FRE (1u << 4)
#define AHCI_PXCMD_FR (1u << 14)
#define AHCI_PXCMD_CR (1u << 15)
#define AHCI_PXIS_TFES (1u << 30)

extern void baf_core_console_write(const char *data, uint32_t length,
                                   uint32_t append_newline);

typedef enum {
    DISK_KIND_NONE = 0,
    DISK_KIND_IDE,
    DISK_KIND_AHCI
} DiskKind;

typedef struct {
    DiskKind kind;
    char model[41];
    uint32_t sectors;
    uint8_t supports_lba48;
    union {
        struct {
            uint16_t io;
            uint16_t control;
            uint8_t drive;
        } ide;
        struct {
            volatile void *port;
            uint8_t context;
        } ahci;
    } as;
} BafDisk;

static BafDisk disks[BAF_MAX_DISKS];
static uint32_t disk_count;
static uint8_t disk_scanned;
static uint32_t selected_disk = 0xFFFFFFFFu;

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static void memory_zero(void *destination, uint32_t count) {
    uint8_t *bytes = (uint8_t *)destination;
    for (uint32_t i = 0; i < count; i++) bytes[i] = 0u;
}

static void memory_copy(void *destination, const void *source, uint32_t count) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (uint32_t i = 0; i < count; i++) out[i] = in[i];
}

static int memory_equal(const void *left, const void *right, uint32_t count) {
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    for (uint32_t i = 0; i < count; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static void console_raw(const char *text, uint32_t length) {
    baf_core_console_write(text, length, 0u);
}

static uint32_t string_length(const char *text) {
    uint32_t length = 0u;
    while (text[length] != '\0') length++;
    return length;
}

static void console_text(const char *text) {
    console_raw(text, string_length(text));
}

static void console_line(const char *text) {
    baf_core_console_write(text, string_length(text), 1u);
}

static void console_character(char character) {
    console_raw(&character, 1u);
}

static void console_u32(uint32_t value) {
    char digits[10];
    uint32_t count = 0u;
    if (value == 0u) {
        console_character('0');
        return;
    }
    while (value != 0u && count < 10u) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count > 0u) console_character(digits[--count]);
}

static char hex_digit(uint8_t value) {
    return (char)(value < 10u ? ('0' + value) : ('A' + value - 10u));
}

static void console_hex8(uint8_t value) {
    console_character(hex_digit((uint8_t)(value >> 4)));
    console_character(hex_digit((uint8_t)(value & 0x0Fu)));
}

static void console_hex32(uint32_t value) {
    for (int shift = 28; shift >= 0; shift -= 4) {
        console_character(hex_digit((uint8_t)((value >> shift) & 0x0Fu)));
    }
}

static uint32_t pci_address(uint8_t bus, uint8_t slot, uint8_t function,
                            uint8_t offset) {
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
           ((uint32_t)function << 8) | ((uint32_t)offset & 0xFCu);
}

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset) {
    outl(0xCF8u, pci_address(bus, slot, function, offset));
    return inl(0xCFCu);
}

static void pci_write32(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint32_t value) {
    outl(0xCF8u, pci_address(bus, slot, function, offset));
    outl(0xCFCu, value);
}

static uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset) {
    uint32_t value = pci_read32(bus, slot, function, offset);
    return (uint16_t)(value >> ((offset & 2u) * 8u));
}

static uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t function,
                         uint8_t offset) {
    uint32_t value = pci_read32(bus, slot, function, offset);
    return (uint8_t)(value >> ((offset & 3u) * 8u));
}

static void ata_delay_400ns(uint16_t control) {
    (void)inb(control);
    (void)inb(control);
    (void)inb(control);
    (void)inb(control);
}

static int ata_wait_not_busy(uint16_t io, uint32_t timeout) {
    while (timeout-- != 0u) {
        if ((inb((uint16_t)(io + 7u)) & ATA_SR_BSY) == 0u) return 1;
    }
    return 0;
}

static int ata_wait_data(uint16_t io, uint32_t timeout) {
    while (timeout-- != 0u) {
        uint8_t status = inb((uint16_t)(io + 7u));
        if ((status & (ATA_SR_ERR | ATA_SR_DF)) != 0u) return 0;
        if ((status & ATA_SR_BSY) == 0u && (status & ATA_SR_DRQ) != 0u) {
            return 1;
        }
    }
    return 0;
}

static void ata_parse_model(char destination[41], const uint16_t words[256]) {
    uint32_t output = 0u;
    for (uint32_t i = 27u; i <= 46u; i++) {
        destination[output++] = (char)(words[i] >> 8);
        destination[output++] = (char)(words[i] & 0xFFu);
    }
    while (output > 0u && destination[output - 1u] == ' ') output--;
    destination[output] = '\0';
}

static uint32_t ata_sector_count(const uint16_t words[256]) {
    uint32_t lba28 = (uint32_t)words[60] | ((uint32_t)words[61] << 16);
    if ((words[83] & (1u << 10)) != 0u) {
        uint32_t low = (uint32_t)words[100] | ((uint32_t)words[101] << 16);
        uint32_t high = (uint32_t)words[102] | ((uint32_t)words[103] << 16);
        if (high != 0u) return 0xFFFFFFFFu;
        if (low != 0u) return low;
    }
    return lba28;
}

static void register_ide_disk(uint16_t io, uint16_t control, uint8_t drive,
                              const uint16_t identify[256]) {
    if (disk_count >= BAF_MAX_DISKS) return;
    BafDisk *disk = &disks[disk_count++];
    memory_zero(disk, (uint32_t)sizeof(*disk));
    disk->kind = DISK_KIND_IDE;
    disk->as.ide.io = io;
    disk->as.ide.control = control;
    disk->as.ide.drive = drive;
    disk->sectors = ata_sector_count(identify);
    disk->supports_lba48 = (identify[83] & (1u << 10)) != 0u;
    ata_parse_model(disk->model, identify);
}

static void ide_probe_drive(uint16_t io, uint16_t control, uint8_t drive) {
    uint16_t identify[256];
    outb((uint16_t)(io + 6u), (uint8_t)(0xA0u | (drive << 4)));
    ata_delay_400ns(control);
    outb((uint16_t)(io + 2u), 0u);
    outb((uint16_t)(io + 3u), 0u);
    outb((uint16_t)(io + 4u), 0u);
    outb((uint16_t)(io + 5u), 0u);
    outb((uint16_t)(io + 7u), ATA_CMD_IDENTIFY);

    uint8_t status = inb((uint16_t)(io + 7u));
    if (status == 0u || status == 0xFFu) return;
    if (!ata_wait_not_busy(io, 1000000u)) return;

    /* Non-zero LBA mid/high means ATAPI or another non-ATA device. */
    if (inb((uint16_t)(io + 4u)) != 0u || inb((uint16_t)(io + 5u)) != 0u) {
        return;
    }
    if (!ata_wait_data(io, 1000000u)) return;
    for (uint32_t i = 0u; i < 256u; i++) identify[i] = inw(io);
    register_ide_disk(io, control, drive, identify);
}

static void ide_probe_channel(uint16_t io, uint16_t control) {
    if (io == 0u || io == 1u || io == 0xFFFFu) return;
    ide_probe_drive(io, control, 0u);
    ide_probe_drive(io, control, 1u);
}

static int ide_read_sector(BafDisk *disk, uint32_t lba, uint8_t *buffer) {
    if (lba >= disk->sectors) return 0;
    uint16_t io = disk->as.ide.io;
    uint16_t control = disk->as.ide.control;
    uint8_t drive = disk->as.ide.drive;

    if (!ata_wait_not_busy(io, 1000000u)) return 0;
    if (lba <= 0x0FFFFFFFu) {
        outb((uint16_t)(io + 6u),
             (uint8_t)(0xE0u | (drive << 4) | ((lba >> 24) & 0x0Fu)));
        ata_delay_400ns(control);
        outb((uint16_t)(io + 1u), 0u);
        outb((uint16_t)(io + 2u), 1u);
        outb((uint16_t)(io + 3u), (uint8_t)lba);
        outb((uint16_t)(io + 4u), (uint8_t)(lba >> 8));
        outb((uint16_t)(io + 5u), (uint8_t)(lba >> 16));
        outb((uint16_t)(io + 7u), ATA_CMD_READ_PIO);
    } else {
        if (!disk->supports_lba48) return 0;
        outb((uint16_t)(io + 6u), (uint8_t)(0x40u | (drive << 4)));
        ata_delay_400ns(control);
        outb((uint16_t)(io + 1u), 0u);
        outb((uint16_t)(io + 2u), 0u);
        outb((uint16_t)(io + 3u), (uint8_t)(lba >> 24));
        outb((uint16_t)(io + 4u), 0u);
        outb((uint16_t)(io + 5u), 0u);
        outb((uint16_t)(io + 1u), 0u);
        outb((uint16_t)(io + 2u), 1u);
        outb((uint16_t)(io + 3u), (uint8_t)lba);
        outb((uint16_t)(io + 4u), (uint8_t)(lba >> 8));
        outb((uint16_t)(io + 5u), (uint8_t)(lba >> 16));
        outb((uint16_t)(io + 7u), ATA_CMD_READ_PIO_EXT);
    }
    if (!ata_wait_data(io, 1000000u)) return 0;

    uint16_t *words = (uint16_t *)buffer;
    for (uint32_t i = 0u; i < 256u; i++) words[i] = inw(io);
    ata_delay_400ns(control);
    return 1;
}


static int ide_write_sector(BafDisk *disk, uint32_t lba,
                            const uint8_t *buffer) {
    if (lba >= disk->sectors) return 0;
    uint16_t io = disk->as.ide.io;
    uint16_t control = disk->as.ide.control;
    uint8_t drive = disk->as.ide.drive;
    uint8_t flush_command = ATA_CMD_FLUSH_CACHE;

    if (!ata_wait_not_busy(io, 1000000u)) return 0;
    if (lba <= 0x0FFFFFFFu) {
        outb((uint16_t)(io + 6u),
             (uint8_t)(0xE0u | (drive << 4) | ((lba >> 24) & 0x0Fu)));
        ata_delay_400ns(control);
        outb((uint16_t)(io + 1u), 0u);
        outb((uint16_t)(io + 2u), 1u);
        outb((uint16_t)(io + 3u), (uint8_t)lba);
        outb((uint16_t)(io + 4u), (uint8_t)(lba >> 8));
        outb((uint16_t)(io + 5u), (uint8_t)(lba >> 16));
        outb((uint16_t)(io + 7u), ATA_CMD_WRITE_PIO);
    } else {
        if (!disk->supports_lba48) return 0;
        flush_command = ATA_CMD_FLUSH_CACHE_EXT;
        outb((uint16_t)(io + 6u), (uint8_t)(0x40u | (drive << 4)));
        ata_delay_400ns(control);
        outb((uint16_t)(io + 1u), 0u);
        outb((uint16_t)(io + 2u), 0u);
        outb((uint16_t)(io + 3u), (uint8_t)(lba >> 24));
        outb((uint16_t)(io + 4u), 0u);
        outb((uint16_t)(io + 5u), 0u);
        outb((uint16_t)(io + 1u), 0u);
        outb((uint16_t)(io + 2u), 1u);
        outb((uint16_t)(io + 3u), (uint8_t)lba);
        outb((uint16_t)(io + 4u), (uint8_t)(lba >> 8));
        outb((uint16_t)(io + 5u), (uint8_t)(lba >> 16));
        outb((uint16_t)(io + 7u), ATA_CMD_WRITE_PIO_EXT);
    }
    if (!ata_wait_data(io, 1000000u)) return 0;

    const uint16_t *words = (const uint16_t *)buffer;
    for (uint32_t i = 0u; i < 256u; i++) outw(io, words[i]);
    if (!ata_wait_not_busy(io, 2000000u)) return 0;
    outb((uint16_t)(io + 7u), flush_command);
    if (!ata_wait_not_busy(io, 2000000u)) return 0;
    return (inb((uint16_t)(io + 7u)) & (ATA_SR_ERR | ATA_SR_DF)) == 0u;
}

/* ---------- AHCI ---------- */

typedef struct {
    volatile uint32_t clb;
    volatile uint32_t clbu;
    volatile uint32_t fb;
    volatile uint32_t fbu;
    volatile uint32_t is;
    volatile uint32_t ie;
    volatile uint32_t cmd;
    volatile uint32_t reserved0;
    volatile uint32_t tfd;
    volatile uint32_t sig;
    volatile uint32_t ssts;
    volatile uint32_t sctl;
    volatile uint32_t serr;
    volatile uint32_t sact;
    volatile uint32_t ci;
    volatile uint32_t sntf;
    volatile uint32_t fbs;
    volatile uint32_t reserved1[11];
    volatile uint32_t vendor[4];
} HbaPort;

typedef struct {
    volatile uint32_t cap;
    volatile uint32_t ghc;
    volatile uint32_t is;
    volatile uint32_t pi;
    volatile uint32_t vs;
    volatile uint32_t ccc_ctl;
    volatile uint32_t ccc_pts;
    volatile uint32_t em_loc;
    volatile uint32_t em_ctl;
    volatile uint32_t cap2;
    volatile uint32_t bohc;
    volatile uint8_t reserved[0xA0u - 0x2Cu];
    volatile uint8_t vendor[0x100u - 0xA0u];
    HbaPort ports[32];
} HbaMemory;

_Static_assert(sizeof(HbaPort) == 0x80u, "AHCI port register size mismatch");
_Static_assert(offsetof(HbaMemory, ports) == 0x100u,
               "AHCI port register offset mismatch");

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t prdt_length;
    volatile uint32_t bytes_transferred;
    uint32_t table_base;
    uint32_t table_base_upper;
    uint32_t reserved[4];
} HbaCommandHeader;

typedef struct __attribute__((packed)) {
    uint32_t data_base;
    uint32_t data_base_upper;
    uint32_t reserved;
    uint32_t byte_count;
} HbaPrdtEntry;

typedef struct __attribute__((packed)) {
    uint8_t command_fis[64];
    uint8_t atapi_command[16];
    uint8_t reserved[48];
    HbaPrdtEntry prdt[1];
} HbaCommandTable;

typedef struct __attribute__((packed)) {
    uint8_t fis_type;
    uint8_t flags;
    uint8_t command;
    uint8_t feature_low;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t feature_high;
    uint8_t count_low;
    uint8_t count_high;
    uint8_t icc;
    uint8_t control;
    uint8_t reserved[4];
} FisHostToDevice;

static HbaCommandHeader ahci_command_lists[BAF_MAX_AHCI_PORTS][32]
    __attribute__((aligned(1024)));
static uint8_t ahci_received_fis[BAF_MAX_AHCI_PORTS][256]
    __attribute__((aligned(256)));
static uint8_t ahci_command_table_storage[BAF_MAX_AHCI_PORTS][256]
    __attribute__((aligned(128)));
static uint8_t ahci_identify_buffers[BAF_MAX_AHCI_PORTS][BAF_SECTOR_SIZE]
    __attribute__((aligned(2)));
static uint8_t ahci_context_count;

static int ahci_stop_port(HbaPort *port) {
    port->cmd &= ~AHCI_PXCMD_ST;
    port->cmd &= ~AHCI_PXCMD_FRE;
    uint32_t timeout = 1000000u;
    while ((port->cmd & (AHCI_PXCMD_FR | AHCI_PXCMD_CR)) != 0u) {
        if (timeout-- == 0u) return 0;
    }
    return 1;
}

static void ahci_start_port(HbaPort *port) {
    port->cmd |= AHCI_PXCMD_FRE;
    port->cmd |= AHCI_PXCMD_ST;
}

static int ahci_prepare_port(HbaPort *port, uint8_t context) {
    if (!ahci_stop_port(port)) return 0;
    memory_zero(ahci_command_lists[context],
                (uint32_t)sizeof(ahci_command_lists[context]));
    memory_zero(ahci_received_fis[context],
                (uint32_t)sizeof(ahci_received_fis[context]));
    memory_zero(&ahci_command_table_storage[context][0],
                (uint32_t)sizeof(ahci_command_table_storage[context]));

    port->clb = (uint32_t)(uintptr_t)&ahci_command_lists[context][0];
    port->clbu = 0u;
    port->fb = (uint32_t)(uintptr_t)&ahci_received_fis[context][0];
    port->fbu = 0u;
    port->serr = 0xFFFFFFFFu;
    port->is = 0xFFFFFFFFu;

    ahci_command_lists[context][0].table_base =
        (uint32_t)(uintptr_t)&ahci_command_table_storage[context][0];
    ahci_command_lists[context][0].table_base_upper = 0u;
    ahci_start_port(port);
    return 1;
}

static int ahci_wait_ready(HbaPort *port) {
    uint32_t timeout = 1000000u;
    while ((port->tfd & (ATA_SR_BSY | ATA_SR_DRQ)) != 0u) {
        if (timeout-- == 0u) return 0;
    }
    return 1;
}

static int ahci_submit(HbaPort *port, uint8_t context, uint8_t command,
                       uint32_t lba, uint8_t *buffer, uint32_t has_data,
                       uint32_t write_to_device) {
    if (!ahci_wait_ready(port)) return 0;
    if ((port->ci & 1u) != 0u || (port->sact & 1u) != 0u) return 0;

    HbaCommandHeader *header = &ahci_command_lists[context][0];
    HbaCommandTable *table =
        (HbaCommandTable *)&ahci_command_table_storage[context][0];
    memory_zero(table, (uint32_t)sizeof(*table));
    header->flags = (uint16_t)(5u | (write_to_device ? (1u << 6) : 0u));
    header->prdt_length = (uint16_t)(has_data ? 1u : 0u);
    header->bytes_transferred = 0u;

    if (has_data) {
        table->prdt[0].data_base = (uint32_t)(uintptr_t)buffer;
        table->prdt[0].data_base_upper = 0u;
        table->prdt[0].reserved = 0u;
        table->prdt[0].byte_count = (BAF_SECTOR_SIZE - 1u) | (1u << 31);
    }

    FisHostToDevice *fis = (FisHostToDevice *)&table->command_fis[0];
    fis->fis_type = 0x27u;
    fis->flags = 1u << 7;
    fis->command = command;
    if (command != ATA_CMD_IDENTIFY && command != ATA_CMD_FLUSH_CACHE &&
        command != ATA_CMD_FLUSH_CACHE_EXT) {
        fis->device = 1u << 6;
        fis->lba0 = (uint8_t)lba;
        fis->lba1 = (uint8_t)(lba >> 8);
        fis->lba2 = (uint8_t)(lba >> 16);
        fis->lba3 = (uint8_t)(lba >> 24);
        fis->count_low = 1u;
    }

    port->is = 0xFFFFFFFFu;
    port->ci = 1u;
    uint32_t timeout = 5000000u;
    while ((port->ci & 1u) != 0u) {
        if ((port->is & AHCI_PXIS_TFES) != 0u) return 0;
        if (timeout-- == 0u) return 0;
    }
    return (port->is & AHCI_PXIS_TFES) == 0u;
}

static int ahci_issue_read(HbaPort *port, uint8_t context, uint8_t command,
                           uint32_t lba, uint8_t *buffer) {
    return ahci_submit(port, context, command, lba, buffer, 1u, 0u);
}

static int ahci_issue_write(HbaPort *port, uint8_t context, uint8_t command,
                            uint32_t lba, const uint8_t *buffer) {
    return ahci_submit(port, context, command, lba, (uint8_t *)(uintptr_t)buffer,
                       1u, 1u);
}

static int ahci_issue_flush(HbaPort *port, uint8_t context) {
    return ahci_submit(port, context, ATA_CMD_FLUSH_CACHE_EXT, 0u, NULL, 0u, 0u);
}

static void register_ahci_disk(HbaPort *port, uint8_t context,
                               const uint16_t identify[256]) {
    if (disk_count >= BAF_MAX_DISKS) return;
    BafDisk *disk = &disks[disk_count++];
    memory_zero(disk, (uint32_t)sizeof(*disk));
    disk->kind = DISK_KIND_AHCI;
    disk->as.ahci.port = port;
    disk->as.ahci.context = context;
    disk->sectors = ata_sector_count(identify);
    disk->supports_lba48 = (identify[83] & (1u << 10)) != 0u;
    ata_parse_model(disk->model, identify);
}

static void ahci_probe_controller(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t bar5 = pci_read32(bus, slot, function, 0x24u);
    if ((bar5 & 1u) != 0u) return;
    if ((bar5 & 6u) == 4u) {
        uint32_t high = pci_read32(bus, slot, function, 0x28u);
        if (high != 0u) return;
    }
    uint32_t address = bar5 & 0xFFFFFFF0u;
    if (address == 0u || address == 0xFFFFFFF0u) return;

    uint32_t command = pci_read32(bus, slot, function, 0x04u) & 0xFFFFu;
    command |= (1u << 1) | (1u << 2); /* memory space + bus master */
    pci_write32(bus, slot, function, 0x04u, command);

    HbaMemory *hba = (HbaMemory *)(uintptr_t)address;
    hba->ghc |= AHCI_GHC_AE;
    uint32_t implemented = hba->pi;
    for (uint32_t index = 0u; index < 32u; index++) {
        if ((implemented & (1u << index)) == 0u) continue;
        HbaPort *port = &hba->ports[index];
        uint32_t status = port->ssts;
        uint32_t det = status & 0x0Fu;
        uint32_t ipm = (status >> 8) & 0x0Fu;
        if (det != 3u || ipm != 1u || port->sig != SATA_SIG_ATA) continue;
        if (ahci_context_count >= BAF_MAX_AHCI_PORTS) return;
        uint8_t context = ahci_context_count++;
        if (!ahci_prepare_port(port, context)) continue;
        uint8_t *identify = &ahci_identify_buffers[context][0];
        if (!ahci_issue_read(port, context, ATA_CMD_IDENTIFY, 0u, identify)) continue;
        register_ahci_disk(port, context, (const uint16_t *)identify);
    }
}

static int ahci_read_sector(BafDisk *disk, uint32_t lba, uint8_t *buffer) {
    if (lba >= disk->sectors) return 0;
    return ahci_issue_read((HbaPort *)disk->as.ahci.port,
                           disk->as.ahci.context, ATA_CMD_READ_DMA_EXT,
                           lba, buffer);
}

static int ahci_write_sector(BafDisk *disk, uint32_t lba,
                             const uint8_t *buffer) {
    if (lba >= disk->sectors) return 0;
    HbaPort *port = (HbaPort *)disk->as.ahci.port;
    uint8_t context = disk->as.ahci.context;
    if (!ahci_issue_write(port, context, ATA_CMD_WRITE_DMA_EXT, lba, buffer)) {
        return 0;
    }
    return ahci_issue_flush(port, context);
}

static void ide_probe_pci_controller(uint8_t bus, uint8_t slot,
                                     uint8_t function, uint8_t programming) {
    uint16_t primary_io = 0x1F0u;
    uint16_t primary_control = 0x3F6u;
    uint16_t secondary_io = 0x170u;
    uint16_t secondary_control = 0x376u;

    if ((programming & 1u) != 0u) {
        uint32_t bar0 = pci_read32(bus, slot, function, 0x10u);
        uint32_t bar1 = pci_read32(bus, slot, function, 0x14u);
        if ((bar0 & 1u) != 0u && (bar1 & 1u) != 0u) {
            primary_io = (uint16_t)(bar0 & 0xFFFCu);
            primary_control = (uint16_t)((bar1 & 0xFFFCu) + 2u);
        }
    }
    if ((programming & 4u) != 0u) {
        uint32_t bar2 = pci_read32(bus, slot, function, 0x18u);
        uint32_t bar3 = pci_read32(bus, slot, function, 0x1Cu);
        if ((bar2 & 1u) != 0u && (bar3 & 1u) != 0u) {
            secondary_io = (uint16_t)(bar2 & 0xFFFCu);
            secondary_control = (uint16_t)((bar3 & 0xFFFCu) + 2u);
        }
    }
    ide_probe_channel(primary_io, primary_control);
    ide_probe_channel(secondary_io, secondary_control);
}

static void scan_pci_storage(void) {
    uint8_t found_ide = 0u;
    for (uint32_t bus = 0u; bus < 256u; bus++) {
        for (uint32_t slot = 0u; slot < 32u; slot++) {
            uint16_t vendor0 = pci_read16((uint8_t)bus, (uint8_t)slot, 0u, 0u);
            if (vendor0 == 0xFFFFu) continue;
            uint8_t header = pci_read8((uint8_t)bus, (uint8_t)slot, 0u, 0x0Eu);
            uint8_t functions = (header & 0x80u) != 0u ? 8u : 1u;
            for (uint8_t function = 0u; function < functions; function++) {
                uint16_t vendor = pci_read16((uint8_t)bus, (uint8_t)slot,
                                             function, 0u);
                if (vendor == 0xFFFFu) continue;
                uint8_t class_code = pci_read8((uint8_t)bus, (uint8_t)slot,
                                               function, 0x0Bu);
                uint8_t subclass = pci_read8((uint8_t)bus, (uint8_t)slot,
                                             function, 0x0Au);
                uint8_t programming = pci_read8((uint8_t)bus, (uint8_t)slot,
                                                function, 0x09u);
                if (class_code != 0x01u) continue;
                if (subclass == 0x01u && !found_ide) {
                    found_ide = 1u;
                    ide_probe_pci_controller((uint8_t)bus, (uint8_t)slot,
                                             function, programming);
                } else if (subclass == 0x06u && programming == 0x01u) {
                    ahci_probe_controller((uint8_t)bus, (uint8_t)slot,
                                          function);
                }
            }
        }
    }
    if (!found_ide) {
        ide_probe_channel(0x1F0u, 0x3F6u);
        ide_probe_channel(0x170u, 0x376u);
    }
}

static int disk_read_sector(uint32_t disk_index, uint32_t lba,
                            uint8_t *buffer) {
    if (disk_index >= disk_count) return 0;
    BafDisk *disk = &disks[disk_index];
    if (disk->kind == DISK_KIND_IDE) return ide_read_sector(disk, lba, buffer);
    if (disk->kind == DISK_KIND_AHCI) return ahci_read_sector(disk, lba, buffer);
    return 0;
}

static int disk_write_sector(uint32_t disk_index, uint32_t lba,
                             const uint8_t *buffer) {
    if (disk_index >= disk_count) return 0;
    BafDisk *disk = &disks[disk_index];
    if (disk->kind == DISK_KIND_IDE) return ide_write_sector(disk, lba, buffer);
    if (disk->kind == DISK_KIND_AHCI) return ahci_write_sector(disk, lba, buffer);
    return 0;
}

void baf_core_disk_scan(void) {
    if (disk_scanned) return;
    disk_scanned = 1u;
    disk_count = 0u;
    ahci_context_count = 0u;
    memory_zero(disks, (uint32_t)sizeof(disks));
    scan_pci_storage();
    selected_disk = disk_count != 0u ? 0u : 0xFFFFFFFFu;
}

uint32_t baf_core_disk_count(void) {
    baf_core_disk_scan();
    return disk_count;
}

void baf_core_disk_list(void) {
    baf_core_disk_scan();
    if (disk_count == 0u) {
        console_line("No ATA disks detected.");
        return;
    }
    for (uint32_t i = 0u; i < disk_count; i++) {
        BafDisk *disk = &disks[i];
        console_text("disk");
        console_u32(i);
        console_text(": ");
        console_text(disk->kind == DISK_KIND_AHCI ? "AHCI/SATA " : "IDE/PATA ");
        console_text(disk->model[0] != '\0' ? disk->model : "unknown model");
        console_text(" | sectors=");
        if (disk->sectors == 0xFFFFFFFFu) {
            console_text(">=4294967295");
        } else {
            console_u32(disk->sectors);
        }
        console_character('\n');
    }
}

void baf_core_disk_hex(uint32_t disk_index, uint32_t lba) {
    static uint8_t sector[BAF_SECTOR_SIZE] __attribute__((aligned(2)));
    baf_core_disk_scan();
    if (disk_index >= disk_count) {
        console_line("Disk index does not exist.");
        return;
    }
    if (!disk_read_sector(disk_index, lba, sector)) {
        console_line("Sector read failed.");
        return;
    }

    console_text("disk");
    console_u32(disk_index);
    console_text(" lba ");
    console_u32(lba);
    console_line(":");
    for (uint32_t row = 0u; row < 32u; row++) {
        console_hex32(row * 16u);
        console_text("  ");
        for (uint32_t column = 0u; column < 16u; column++) {
            console_hex8(sector[row * 16u + column]);
            console_character(column == 7u ? '-' : ' ');
        }
        console_text(" |");
        for (uint32_t column = 0u; column < 16u; column++) {
            uint8_t value = sector[row * 16u + column];
            console_character(value >= 32u && value <= 126u ? (char)value : '.');
        }
        console_line("|");
    }
}

/* ---------- BAFS1: tiny persistent file store with directories ---------- */

#define BAF_FS_MAGIC "BAFS1\0\0\0"
#define BAF_FS_VERSION 1u
#define BAF_FS_SUPER_LBA 1u
#define BAF_FS_DIRECTORY_LBA 2u
#define BAF_FS_DIRECTORY_SECTORS 8u
#define BAF_FS_DATA_LBA (BAF_FS_DIRECTORY_LBA + BAF_FS_DIRECTORY_SECTORS)
#define BAF_FS_MAX_FILES 64u
#define BAF_FS_NAME_MAX 40u
#define BAF_FS_READ_CAPACITY 65536u
#define BAF_FS_PATH_CAPACITY 4096u
#define BAF_FS_ENTRY_FILE 1u
#define BAF_FS_ENTRY_DIRECTORY 2u
#define BAF_FS_ROOT_ID 0u

#pragma pack(push, 1)
typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t directory_lba;
    uint32_t directory_sectors;
    uint32_t data_lba;
    uint32_t total_sectors;
    uint32_t max_files;
    uint32_t checksum;
    uint8_t reserved[476];
} BafFsSuper;

typedef struct {
    uint8_t status;
    uint8_t name_length;
    uint16_t parent_id; /* 0 = root, otherwise directory entry index + 1 */
    uint32_t start_lba;
    uint32_t size_bytes;
    uint32_t sector_count;
    uint32_t checksum;
    uint32_t generation;
    char name[BAF_FS_NAME_MAX];
} BafFsEntry;
#pragma pack(pop)

_Static_assert(sizeof(BafFsSuper) == BAF_SECTOR_SIZE,
               "BAFS superblock must occupy one sector");
_Static_assert(sizeof(BafFsEntry) == 64u,
               "BAFS directory entry must be 64 bytes");

static uint8_t fs_sector[BAF_SECTOR_SIZE] __attribute__((aligned(4)));
static uint8_t fs_read_buffer[BAF_FS_READ_CAPACITY] __attribute__((aligned(4)));
static char fs_path_buffer[BAF_FS_PATH_CAPACITY];
static uint16_t fs_current_directory = BAF_FS_ROOT_ID;

static uint32_t fs_super_checksum(const BafFsSuper *super) {
    return 0xBAF50001u ^ super->version ^ super->directory_lba ^
           super->directory_sectors ^ super->data_lba ^
           super->total_sectors ^ super->max_files;
}

static uint32_t fs_data_checksum(const uint8_t *data, uint32_t length) {
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0u; i < length; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static int fs_selected_ready(void) {
    baf_core_disk_scan();
    if (selected_disk == 0xFFFFFFFFu || selected_disk >= disk_count) {
        console_line("No selected disk. Use Disk.Scan() and Disk.Select().");
        return 0;
    }
    return 1;
}

static int fs_load_super(BafFsSuper *super) {
    if (!fs_selected_ready()) return 0;
    if (!disk_read_sector(selected_disk, BAF_FS_SUPER_LBA,
                          (uint8_t *)super)) {
        console_line("Could not read the BAFS superblock.");
        return 0;
    }
    if (!memory_equal(super->magic, BAF_FS_MAGIC, 8u) ||
        super->version != BAF_FS_VERSION ||
        super->directory_lba != BAF_FS_DIRECTORY_LBA ||
        super->directory_sectors != BAF_FS_DIRECTORY_SECTORS ||
        super->data_lba != BAF_FS_DATA_LBA ||
        super->max_files != BAF_FS_MAX_FILES ||
        super->checksum != fs_super_checksum(super) ||
        super->total_sectors > disks[selected_disk].sectors ||
        super->total_sectors <= BAF_FS_DATA_LBA) {
        console_line("No valid BAFS filesystem. Run Disk.Format().");
        return 0;
    }
    return 1;
}

static int fs_read_entry(const BafFsSuper *super, uint32_t index,
                         BafFsEntry *entry) {
    if (index >= super->max_files) return 0;
    uint32_t entries_per_sector = BAF_SECTOR_SIZE / sizeof(BafFsEntry);
    uint32_t lba = super->directory_lba + index / entries_per_sector;
    uint32_t offset = (index % entries_per_sector) * sizeof(BafFsEntry);
    if (!disk_read_sector(selected_disk, lba, fs_sector)) return 0;
    memory_copy(entry, fs_sector + offset, (uint32_t)sizeof(*entry));
    return 1;
}

static int fs_write_entry(const BafFsSuper *super, uint32_t index,
                          const BafFsEntry *entry) {
    if (index >= super->max_files) return 0;
    uint32_t entries_per_sector = BAF_SECTOR_SIZE / sizeof(BafFsEntry);
    uint32_t lba = super->directory_lba + index / entries_per_sector;
    uint32_t offset = (index % entries_per_sector) * sizeof(BafFsEntry);
    if (!disk_read_sector(selected_disk, lba, fs_sector)) return 0;
    memory_copy(fs_sector + offset, entry, (uint32_t)sizeof(*entry));
    return disk_write_sector(selected_disk, lba, fs_sector);
}

static int fs_entry_live(const BafFsEntry *entry) {
    return entry->status == BAF_FS_ENTRY_FILE ||
           entry->status == BAF_FS_ENTRY_DIRECTORY;
}

static int fs_name_equal(const BafFsEntry *entry, uint16_t parent_id,
                         const char *name, uint32_t name_length,
                         uint8_t wanted_status) {
    return fs_entry_live(entry) && entry->parent_id == parent_id &&
           (wanted_status == 0u || entry->status == wanted_status) &&
           entry->name_length == name_length &&
           memory_equal(entry->name, name, name_length);
}

static int fs_find_child(const BafFsSuper *super, uint16_t parent_id,
                         const char *name, uint32_t name_length,
                         uint8_t wanted_status, BafFsEntry *entry_out,
                         uint32_t *index_out) {
    BafFsEntry entry;
    for (uint32_t i = 0u; i < super->max_files; i++) {
        if (!fs_read_entry(super, i, &entry)) return 0;
        if (fs_name_equal(&entry, parent_id, name, name_length,
                          wanted_status)) {
            if (entry_out) memory_copy(entry_out, &entry, (uint32_t)sizeof(entry));
            if (index_out) *index_out = i;
            return 1;
        }
    }
    return 0;
}

static int fs_read_directory_by_id(const BafFsSuper *super, uint16_t id,
                                   BafFsEntry *entry_out,
                                   uint32_t *index_out) {
    if (id == BAF_FS_ROOT_ID || id > super->max_files) return 0;
    uint32_t index = (uint32_t)id - 1u;
    BafFsEntry entry;
    if (!fs_read_entry(super, index, &entry) ||
        entry.status != BAF_FS_ENTRY_DIRECTORY) {
        return 0;
    }
    if (entry_out) memory_copy(entry_out, &entry, (uint32_t)sizeof(entry));
    if (index_out) *index_out = index;
    return 1;
}

static int fs_find_free_entry(const BafFsSuper *super, uint32_t *index_out) {
    BafFsEntry entry;
    for (uint32_t i = 0u; i < super->max_files; i++) {
        if (!fs_read_entry(super, i, &entry)) return 0;
        if (!fs_entry_live(&entry)) {
            *index_out = i;
            return 1;
        }
    }
    return 0;
}

static int fs_component_valid(const char *name, uint32_t name_length) {
    if (name_length == 0u || name_length > BAF_FS_NAME_MAX) return 0;
    if (name_length == 1u && name[0] == '.') return 0;
    if (name_length == 2u && name[0] == '.' && name[1] == '.') return 0;
    for (uint32_t i = 0u; i < name_length; i++) {
        if (name[i] == '/' || name[i] == '\\' || name[i] == '\0') return 0;
    }
    return 1;
}

static int fs_ranges_overlap(uint32_t left_start, uint32_t left_count,
                             uint32_t right_start, uint32_t right_count) {
    if (left_count == 0u || right_count == 0u) return 0;
    uint32_t left_end = left_start + left_count;
    uint32_t right_end = right_start + right_count;
    return left_start < right_end && right_start < left_end;
}

static int fs_allocate(const BafFsSuper *super, uint32_t sectors_needed,
                       uint32_t ignored_entry, uint32_t *start_out) {
    if (sectors_needed == 0u) {
        *start_out = super->data_lba;
        return 1;
    }
    uint32_t candidate = super->data_lba;
    while (candidate < super->total_sectors &&
           sectors_needed <= super->total_sectors - candidate) {
        uint32_t bumped = 0u;
        for (uint32_t i = 0u; i < super->max_files; i++) {
            if (i == ignored_entry) continue;
            BafFsEntry entry;
            if (!fs_read_entry(super, i, &entry)) return 0;
            if (entry.status != BAF_FS_ENTRY_FILE) continue;
            if (fs_ranges_overlap(candidate, sectors_needed, entry.start_lba,
                                  entry.sector_count)) {
                candidate = entry.start_lba + entry.sector_count;
                bumped = 1u;
                break;
            }
        }
        if (!bumped) {
            *start_out = candidate;
            return 1;
        }
    }
    return 0;
}

static int fs_build_current_path(const BafFsSuper *super,
                                 const char **data_out,
                                 uint32_t *length_out) {
    uint16_t chain[BAF_FS_MAX_FILES];
    uint32_t depth = 0u;
    uint16_t id = fs_current_directory;

    while (id != BAF_FS_ROOT_ID) {
        if (depth >= BAF_FS_MAX_FILES) return 0;
        BafFsEntry entry;
        if (!fs_read_directory_by_id(super, id, &entry, NULL)) return 0;
        chain[depth++] = id;
        id = entry.parent_id;
    }

    uint32_t length = 0u;
    fs_path_buffer[length++] = '/';
    for (uint32_t position = depth; position > 0u; position--) {
        BafFsEntry entry;
        if (!fs_read_directory_by_id(super, chain[position - 1u], &entry,
                                     NULL)) {
            return 0;
        }
        if (length != 1u) {
            if (length + 1u >= BAF_FS_PATH_CAPACITY) return 0;
            fs_path_buffer[length++] = '/';
        }
        if (length + entry.name_length >= BAF_FS_PATH_CAPACITY) return 0;
        memory_copy(fs_path_buffer + length, entry.name, entry.name_length);
        length += entry.name_length;
    }
    fs_path_buffer[length] = '\0';
    *data_out = fs_path_buffer;
    *length_out = length;
    return 1;
}

uint32_t baf_core_disk_select(uint32_t disk_index) {
    baf_core_disk_scan();
    if (disk_index >= disk_count) {
        console_line("Disk index does not exist.");
        return 0u;
    }
    selected_disk = disk_index;
    fs_current_directory = BAF_FS_ROOT_ID;
    console_text("Selected disk");
    console_u32(disk_index);
    console_character('\n');
    return 1u;
}

uint32_t baf_core_fs_format(void) {
    if (!fs_selected_ready()) return 0u;
    if (disks[selected_disk].sectors <= BAF_FS_DATA_LBA) {
        console_line("Disk is too small for BAFS.");
        return 0u;
    }

    BafFsSuper super;
    memory_zero(&super, (uint32_t)sizeof(super));
    memory_copy(super.magic, BAF_FS_MAGIC, 8u);
    super.version = BAF_FS_VERSION;
    super.directory_lba = BAF_FS_DIRECTORY_LBA;
    super.directory_sectors = BAF_FS_DIRECTORY_SECTORS;
    super.data_lba = BAF_FS_DATA_LBA;
    super.total_sectors = disks[selected_disk].sectors;
    super.max_files = BAF_FS_MAX_FILES;
    super.checksum = fs_super_checksum(&super);

    if (!disk_write_sector(selected_disk, BAF_FS_SUPER_LBA,
                           (const uint8_t *)&super)) {
        console_line("Failed to write the BAFS superblock.");
        return 0u;
    }
    memory_zero(fs_sector, BAF_SECTOR_SIZE);
    for (uint32_t i = 0u; i < BAF_FS_DIRECTORY_SECTORS; i++) {
        if (!disk_write_sector(selected_disk, BAF_FS_DIRECTORY_LBA + i,
                               fs_sector)) {
            console_line("Failed to initialize the BAFS directory.");
            return 0u;
        }
    }
    fs_current_directory = BAF_FS_ROOT_ID;
    console_line("BAFS format complete.");
    return 1u;
}

uint32_t baf_core_fs_create_dir(const char *name, uint32_t name_length) {
    BafFsSuper super;
    if (!fs_load_super(&super)) return 0u;
    if (!fs_component_valid(name, name_length)) {
        console_line("Directory names must be 1 to 40 bytes without slashes.");
        return 0u;
    }
    if (fs_find_child(&super, fs_current_directory, name, name_length, 0u,
                      NULL, NULL)) {
        console_line("A file or directory already has that name.");
        return 0u;
    }

    uint32_t index;
    if (!fs_find_free_entry(&super, &index)) {
        console_line("BAFS directory table is full.");
        return 0u;
    }

    BafFsEntry entry;
    memory_zero(&entry, (uint32_t)sizeof(entry));
    entry.status = BAF_FS_ENTRY_DIRECTORY;
    entry.name_length = (uint8_t)name_length;
    entry.parent_id = fs_current_directory;
    entry.generation = 1u;
    memory_copy(entry.name, name, name_length);
    if (!fs_write_entry(&super, index, &entry)) {
        console_line("Failed to create the directory.");
        return 0u;
    }
    return 1u;
}

uint32_t baf_core_fs_goto_dir(const char *name, uint32_t name_length) {
    BafFsSuper super;
    if (!fs_load_super(&super)) return 0u;

    if (name_length == 1u && name[0] == '/') {
        fs_current_directory = BAF_FS_ROOT_ID;
        return 1u;
    }
    if (name_length == 1u && name[0] == '.') return 1u;
    if (name_length == 2u && name[0] == '.' && name[1] == '.') {
        if (fs_current_directory == BAF_FS_ROOT_ID) return 1u;
        BafFsEntry current;
        if (!fs_read_directory_by_id(&super, fs_current_directory, &current,
                                     NULL)) {
            console_line("Current directory metadata is invalid.");
            fs_current_directory = BAF_FS_ROOT_ID;
            return 0u;
        }
        fs_current_directory = current.parent_id;
        return 1u;
    }
    if (!fs_component_valid(name, name_length)) {
        console_line("GotoDir currently accepts one directory name, /, . or ...");
        return 0u;
    }

    uint32_t index;
    if (!fs_find_child(&super, fs_current_directory, name, name_length,
                       BAF_FS_ENTRY_DIRECTORY, NULL, &index)) {
        console_line("Directory not found.");
        return 0u;
    }
    fs_current_directory = (uint16_t)(index + 1u);
    return 1u;
}

uint32_t baf_core_fs_get_dir(const char **data_out, uint32_t *length_out) {
    *data_out = fs_path_buffer;
    *length_out = 0u;
    BafFsSuper super;
    if (!fs_load_super(&super)) return 0u;
    if (!fs_build_current_path(&super, data_out, length_out)) {
        console_line("Could not build the current directory path.");
        fs_current_directory = BAF_FS_ROOT_ID;
        return 0u;
    }
    return 1u;
}

uint32_t baf_core_fs_write(const char *name, uint32_t name_length,
                           const char *content, uint32_t content_length) {
    BafFsSuper super;
    if (!fs_load_super(&super)) return 0u;
    if (!fs_component_valid(name, name_length)) {
        console_line("File names must be 1 to 40 bytes without slashes.");
        return 0u;
    }

    BafFsEntry old_entry;
    uint32_t entry_index = 0u;
    uint32_t old_index = 0xFFFFFFFFu;
    uint32_t generation = 1u;
    if (fs_find_child(&super, fs_current_directory, name, name_length,
                      BAF_FS_ENTRY_FILE, &old_entry, &entry_index)) {
        old_index = entry_index;
        generation = old_entry.generation + 1u;
    } else if (fs_find_child(&super, fs_current_directory, name, name_length,
                             BAF_FS_ENTRY_DIRECTORY, NULL, NULL)) {
        console_line("A directory already has that name.");
        return 0u;
    } else if (!fs_find_free_entry(&super, &entry_index)) {
        console_line("BAFS directory table is full.");
        return 0u;
    }

    uint32_t sector_count =
        (content_length + BAF_SECTOR_SIZE - 1u) / BAF_SECTOR_SIZE;
    uint32_t start_lba;
    if (!fs_allocate(&super, sector_count, old_index, &start_lba)) {
        console_line("Not enough contiguous disk space.");
        return 0u;
    }

    uint32_t copied = 0u;
    for (uint32_t i = 0u; i < sector_count; i++) {
        memory_zero(fs_sector, BAF_SECTOR_SIZE);
        uint32_t remaining = content_length - copied;
        uint32_t chunk = remaining < BAF_SECTOR_SIZE ? remaining : BAF_SECTOR_SIZE;
        memory_copy(fs_sector, content + copied, chunk);
        if (!disk_write_sector(selected_disk, start_lba + i, fs_sector)) {
            console_line("Disk write failed.");
            return 0u;
        }
        copied += chunk;
    }

    BafFsEntry new_entry;
    memory_zero(&new_entry, (uint32_t)sizeof(new_entry));
    new_entry.status = BAF_FS_ENTRY_FILE;
    new_entry.name_length = (uint8_t)name_length;
    new_entry.parent_id = fs_current_directory;
    new_entry.start_lba = start_lba;
    new_entry.size_bytes = content_length;
    new_entry.sector_count = sector_count;
    new_entry.checksum = fs_data_checksum((const uint8_t *)content,
                                          content_length);
    new_entry.generation = generation;
    memory_copy(new_entry.name, name, name_length);
    if (!fs_write_entry(&super, entry_index, &new_entry)) {
        console_line("Failed to commit the directory entry.");
        return 0u;
    }
    return 1u;
}

uint32_t baf_core_fs_read(const char *name, uint32_t name_length,
                          const char **data_out, uint32_t *length_out) {
    *data_out = (const char *)fs_read_buffer;
    *length_out = 0u;
    BafFsSuper super;
    if (!fs_load_super(&super)) return 0u;
    BafFsEntry entry;
    if (!fs_find_child(&super, fs_current_directory, name, name_length,
                       BAF_FS_ENTRY_FILE, &entry, NULL)) {
        console_line("File not found in the current directory.");
        return 0u;
    }
    if (entry.size_bytes >= BAF_FS_READ_CAPACITY) {
        console_line("File is too large for the current BAF read buffer.");
        return 0u;
    }

    uint32_t copied = 0u;
    for (uint32_t i = 0u; i < entry.sector_count; i++) {
        if (!disk_read_sector(selected_disk, entry.start_lba + i, fs_sector)) {
            console_line("Disk read failed.");
            return 0u;
        }
        uint32_t remaining = entry.size_bytes - copied;
        uint32_t chunk = remaining < BAF_SECTOR_SIZE ? remaining : BAF_SECTOR_SIZE;
        memory_copy(fs_read_buffer + copied, fs_sector, chunk);
        copied += chunk;
    }
    fs_read_buffer[entry.size_bytes] = 0u;
    if (fs_data_checksum(fs_read_buffer, entry.size_bytes) != entry.checksum) {
        console_line("File checksum mismatch.");
        return 0u;
    }
    *length_out = entry.size_bytes;
    return 1u;
}

uint32_t baf_core_fs_remove(const char *name, uint32_t name_length) {
    BafFsSuper super;
    if (!fs_load_super(&super)) return 0u;
    uint32_t index;
    BafFsEntry entry;
    if (!fs_find_child(&super, fs_current_directory, name, name_length,
                       BAF_FS_ENTRY_FILE, &entry, &index)) {
        console_line("File not found in the current directory.");
        return 0u;
    }
    memory_zero(&entry, (uint32_t)sizeof(entry));
    if (!fs_write_entry(&super, index, &entry)) {
        console_line("Failed to remove the file entry.");
        return 0u;
    }
    return 1u;
}

uint32_t baf_core_fs_exists(const char *name, uint32_t name_length) {
    BafFsSuper super;
    if (!fs_load_super(&super)) return 0u;
    return fs_find_child(&super, fs_current_directory, name, name_length,
                         BAF_FS_ENTRY_FILE, NULL, NULL) ? 1u : 0u;
}

uint32_t baf_core_fs_size(const char *name, uint32_t name_length) {
    BafFsSuper super;
    if (!fs_load_super(&super)) return 0u;
    BafFsEntry entry;
    if (!fs_find_child(&super, fs_current_directory, name, name_length,
                       BAF_FS_ENTRY_FILE, &entry, NULL)) {
        return 0u;
    }
    return entry.size_bytes;
}

void baf_core_fs_list(void) {
    BafFsSuper super;
    if (!fs_load_super(&super)) return;
    uint32_t entries = 0u;
    for (uint32_t i = 0u; i < super.max_files; i++) {
        BafFsEntry entry;
        if (!fs_read_entry(&super, i, &entry)) {
            console_line("Directory read failed.");
            return;
        }
        if (!fs_entry_live(&entry) ||
            entry.parent_id != fs_current_directory) {
            continue;
        }
        entries++;
        if (entry.status == BAF_FS_ENTRY_DIRECTORY) {
            console_text("[dir] ");
            console_raw(entry.name, entry.name_length);
            console_character('\n');
        } else {
            console_raw(entry.name, entry.name_length);
            console_text("  ");
            console_u32(entry.size_bytes);
            console_line(" bytes");
        }
    }
    if (entries == 0u) console_line("Directory is empty.");
}

void baf_core_fs_info(void) {
    if (!fs_selected_ready()) return;
    console_text("disk=");
    console_u32(selected_disk);
    console_text(" driver=");
    console_text(disks[selected_disk].kind == DISK_KIND_AHCI ? "AHCI/SATA" :
                                                               "IDE/PATA");
    console_text(" sectors=");
    console_u32(disks[selected_disk].sectors);
    console_character('\n');

    BafFsSuper super;
    if (!fs_load_super(&super)) return;
    uint32_t files = 0u;
    uint32_t directories = 0u;
    uint32_t used_sectors = 0u;
    for (uint32_t i = 0u; i < super.max_files; i++) {
        BafFsEntry entry;
        if (!fs_read_entry(&super, i, &entry)) return;
        if (entry.status == BAF_FS_ENTRY_FILE) {
            files++;
            used_sectors += entry.sector_count;
        } else if (entry.status == BAF_FS_ENTRY_DIRECTORY) {
            directories++;
        }
    }
    console_text("filesystem=BAFS1 files=");
    console_u32(files);
    console_text(" dirs=");
    console_u32(directories);
    console_text(" used_sectors=");
    console_u32(used_sectors);
    console_text(" free_sectors=");
    console_u32(super.total_sectors - super.data_lba - used_sectors);
    console_character('\n');

    const char *path;
    uint32_t path_length;
    if (fs_build_current_path(&super, &path, &path_length)) {
        console_text("cwd=");
        console_raw(path, path_length);
        console_character('\n');
    }
}
