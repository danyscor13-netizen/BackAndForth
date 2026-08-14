# BackAndForth i386 disks and BAFS1

BackAndForth 0.5 provides a writable 512-byte block layer for IDE/PATA and AHCI/SATA disks, plus the first BAF-native persistent file store.

## Drive API

```baf
Disk.Scan()
int count = Disk.Count()
Disk.List()
Disk.Hex(disk: 0, lba: 0)
Disk.Select(0)
Disk.Info()
```

`Disk.Scan()` is idempotent. Disk 0 becomes selected automatically when at least one disk is found.

## File API

Uppercase and lowercase namespaces are aliases:

```baf
disk.format()
disk.write("hello.txt", "Hello")
str text = disk.read("hello.txt")
disk.files()
disk.exists("hello.txt")
disk.size("hello.txt")
disk.rem("hello.txt")
```

## IDE / ATA PIO

- PCI IDE discovery plus compatibility channels
- master/slave `IDENTIFY DEVICE`
- LBA28 and LBA48 sector reads
- LBA28 and LBA48 sector writes
- cache flush following writes

## AHCI / SATA

- PCI AHCI discovery
- memory-space and bus-master enablement
- SATA ATA-port detection
- command-list, received-FIS, command-table, and PRDT setup
- `IDENTIFY DEVICE`
- `READ DMA EXT`
- `WRITE DMA EXT`
- cache flush following writes

SATA is the transport. AHCI is the controller interface used by this driver.

## BAFS1 layout

```text
LBA 0      reserved / untouched
LBA 1      BAFS1 superblock
LBA 2–9    64 fixed-size directory entries
LBA 10+    contiguous file extents
```

Each directory entry stores the name, starting LBA, byte length, sector count, generation, and content checksum. Allocation uses a simple first-fit scan. There are no directories, permissions, timestamps, or journaling yet.

## Test

```sh
truncate -s 16M disk.img
./baf examples/kernel32_disk.baf --osDev -o bafOS.o
./baf bafOS.o --run --disk disk.img
```

AHCI:

```sh
./baf bafOS.o --run --disk disk.img --ahci
```

Inside bafOS, run `format` once, then use `write`, `read`, `files`, `rem`, and `info`.

## Safety

`Disk.Format()` destroys existing BAFS metadata and directory entries on the selected disk. The drivers are experimental; use raw images or expendable hardware only.


## BAFS1 directories (0.5.1)

Directory entries reuse the existing 64-byte BAFS1 metadata records. The
entry status distinguishes files from directories, and the former reserved
16-bit field stores a parent directory ID. This remains compatible with 0.5.0
images because old files have a zero parent ID and therefore live in `/`.

```baf
Disk.CreateDir("docs")
Disk.GotoDir("docs")
putsc(Disk.GetDir())
Disk.GotoDir("..")
Disk.GotoDir("/")
```

`Disk.Files()` lists only children of the current directory. File operations
also resolve names relative to the current directory. Version 0.5.1 accepts a
single component per `GotoDir` call; slash-separated paths and directory
removal are future work.
