# BackAndForth roadmap

## Completed

### 0.1 — hosted compiler

- lexer, parser, AST, semantic analysis
- functions and named arguments
- integers and strings
- LLVM IR output

### 0.2 — first 32-bit kernel

- freestanding i386 target
- Multiboot flat image
- VGA Hello World

### 0.3 — I/O and shell

- `str`, `bool`, `while`, and `switch`
- expression-level `inpt()`
- VGA scrolling and cursor
- polled PS/2 keyboard
- clear, reboot, and shutdown

### 0.4 — disk drivers

- PCI storage-controller discovery
- IDE PIO and AHCI/SATA reads
- block-device registry and sector dump

### 0.5 — writable BAFS1

- IDE and AHCI writes
- BAFS1 files and directories
- VGA foreground/background colors

### 0.6 — source composition and practical I/O

- variadic `putsc`, `putl`, and `inpt`
- automatic output for `str`, `int`, and `bool`
- recursive file and directory includes
- duplicate and cycle detection
- independent input buffers per call site
- `BAF_HOME` precedence fix
- version reporting
- complete `make path` installation

## Next

### 0.7 — interrupt foundation

- GDT owned by bafOS
- IDT
- PIC remapping
- timer interrupt
- keyboard IRQ and input queue
- panic screen

### 0.8 — language growth

- return types and `return`
- comparisons and `if`/`else`
- arrays and indexing
- explicit pointer types
- real modules and namespaces

### 0.9 — memory

- Multiboot memory map
- physical page allocator
- paging
- kernel heap
- owned dynamic strings

### Later

- UEFI
- NVMe
- processes and userspace
- stronger filesystem recovery
