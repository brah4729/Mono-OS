# 🖥️ MonoOS

> **A from-scratch monolithic kernel (codename: Dori) — boots, has a shell, a real filesystem, system calls, and an ELF loader.**

[![Status](https://img.shields.io/badge/status-active%20development-yellow?style=flat-square)](https://github.com/brah4729/Mono-Os)
[![Boot](https://img.shields.io/badge/boots%20in%20QEMU-%E2%9C%93-brightgreen?style=flat-square)]()
[![Shell](https://img.shields.io/badge/shell-%E2%9C%93-brightgreen?style=flat-square)]()
[![VFS](https://img.shields.io/badge/VFS%20%2B%20DoriFS-%E2%9C%93-brightgreen?style=flat-square)]()
[![Architecture](https://img.shields.io/badge/arch-i686%20%2832--bit%20x86%29-blue?style=flat-square)]()
[![Language](https://img.shields.io/badge/language-C%20%2F%20NASM%20Assembly-lightgrey?style=flat-square)]()

---

## ⚠️ Safety Warnings — Read Before Proceeding

MonoOS is **experimental pre-release software**. It is under active development and is not stable. The following risks apply:

| Risk | Level | Details |
|------|-------|---------|
| **Data Loss** | 🔴 CRITICAL | The ATA driver can read/write real disks. Running on bare metal with real drives attached risks data loss or partition corruption. |
| **No Recovery Mode** | 🔴 CRITICAL | There is no safe mode, fsck, or recovery mechanism of any kind. |
| **Untested on Real Hardware** | 🟠 HIGH | Only tested inside QEMU with a virtual disk image. Behaviour on physical machines is unknown. |
| **Kernel Heap Corruption** | 🟠 HIGH | The heap allocator is functional but early-stage. Bugs in higher-level code can produce silent heap corruption. |
| **No Process Isolation** | 🟠 HIGH | Userspace ring separation (Ring 3) is being built but is not fully enforced yet. |
| **No UEFI Support** | 🟡 MEDIUM | Legacy BIOS boot only via GRUB Multiboot. Do not attempt EFI boot. |

> ✅ **Safe way to try MonoOS:** Use QEMU with the provided virtual disk image (`make run`). Never run on a physical machine with disks you care about.

---

## 📖 What is MonoOS?

MonoOS is a hobby operating system built entirely from scratch in C and NASM Assembly, with no borrowed kernel code and no OS underneath it. It uses a **monolithic kernel architecture** — the same model as Linux — where the kernel, drivers, and core services share a single address space in Ring 0.

Internally the kernel is called **Dori** (`dori.kernel`). The project targets **32-bit x86 (i686)** processors and boots via GRUB using the Multiboot specification.

The project now includes a real on-disk filesystem (**DoriFS**), a working VFS layer, a system call interface with 19 calls, a process manager, an ELF binary loader, and is currently integrating **nixpkgs** as a reproducible build environment and package manager.

---

## ✅ What Is Working Right Now

| Subsystem | Status | Notes |
|-----------|--------|-------|
| GRUB Multiboot boot | ✅ Working | Stable entry from GRUB → `boot/multiboot.asm` → `kernel_main()` |
| GDT | ✅ Working | 6 descriptors: null, kernel code/data (Ring 0), user code/data (Ring 3), TSS |
| IDT + CPU exceptions | ✅ Working | All 256 vectors registered; exceptions handled and reported |
| PIC (8259) | ✅ Working | Remapped; IRQ0 (timer) and IRQ1 (keyboard) active |
| PIT (system timer) | ✅ Working | Programmable Interval Timer for time-keeping and future scheduling |
| VGA text output | ✅ Working | Full 16-colour support; `vga_set_color()` for foreground and background |
| Serial port | ✅ Working | `drivers/serial.c` — serial output available for debug logging |
| PS/2 Keyboard | ✅ Working | IRQ1-driven; scan codes translated to ASCII |
| Physical memory manager | ✅ Working | `kernel/pmm.c` — tracks free/used physical frames |
| Virtual memory manager | ✅ Working | `kernel/vmm.c` — page table management |
| Kernel heap | ✅ Working | `kernel/heap.c` — `kmalloc` and `kfree` both implemented |
| ATA/IDE disk driver | ✅ Working | `drivers/ata.c` — reads/writes to IDE disk via PIO mode |
| VFS layer | ✅ Working | Full virtual filesystem abstraction with mount points, file descriptors, symlinks |
| DoriFS | ✅ Working | Custom on-disk filesystem; format, mount, read/write/seek, directories, symlinks |
| System calls | ✅ Working | 19 syscalls via `int 0x80`; see full list below |
| Process manager | ✅ Working | `kernel/process.c` + `boot/context_switch.asm` — process structures and context switch |
| ELF loader | ✅ Working | `kernel/elf.c` — loads ELF32 binaries for execution |
| Kernel shell | ✅ Working | `kernel/kshell.c` — interactive command interface |

---

## ❌ What Is Not Yet Working

| Feature | Notes |
|---------|-------|
| Full userspace (Ring 3 enforcement) | GDT has user segments and TSS is set up; process execution in Ring 3 is in progress |
| Process scheduler | Process structures exist; round-robin or priority scheduling not yet running |
| nixpkgs integration | In progress — will provide reproducible build env and package management |
| VGA scrolling | Screen wraps when the 80×25 buffer fills |
| Networking | Not started |
| UEFI / ACPI | Not supported |
| Multi-process `fork()` | Syscall defined (SYS_FORK = 9); implementation pending |

---

## 📁 Project Structure

```
MonoOs/
│
├── boot/
│   ├── multiboot.asm       # Multiboot header + kernel entry point (_start)
│   ├── gdt.asm             # gdt_flush — loads GDT register (lgdt) and reloads segments
│   ├── idt.asm             # idt_flush — loads IDT register (lidt)
│   └── context_switch.asm  # context_switch(old_esp*, new_esp) — saves/restores registers
│
├── kernel/
│   ├── kernel.c            # kernel_main() — init sequence for all subsystems
│   ├── gdt.c               # GDT setup: 6 descriptors including TSS for Ring 3 transition
│   ├── idt.c               # IDT setup: all 256 vectors, ISRs for CPU exceptions
│   ├── pic.c               # 8259 PIC remapping (IRQs to 0x20–0x2F)
│   ├── pit.c               # PIT (timer) driver — IRQ0
│   ├── pmm.c               # Physical memory manager
│   ├── vmm.c               # Virtual memory manager — page tables
│   ├── heap.c              # Kernel heap — kmalloc / kfree
│   ├── kshell.c            # Interactive kernel shell
│   ├── vfs.c               # Virtual Filesystem layer implementation
│   ├── dorifs.c            # DoriFS on-disk filesystem driver
│   ├── syscall.c           # System call dispatcher (int 0x80)
│   ├── process.c           # Process manager — PCBs, process creation
│   └── elf.c               # ELF32 binary loader
│
├── drivers/
│   ├── vga.c               # VGA text mode — 80×25, 16 colours, cursor control
│   ├── keyboard.c          # PS/2 keyboard — IRQ1, scan code translation
│   ├── serial.c            # Serial port (COM1) — debug output
│   └── ata.c               # ATA/IDE disk driver — PIO mode read/write
│
├── include/
│   ├── types.h             # Fundamental types (uint8_t, uint32_t, bool, size_t, etc.)
│   ├── vga.h               # VGA API + colour enum (VGA_BLACK … VGA_WHITE)
│   ├── vfs.h               # VFS structs and full API
│   ├── dorifs.h            # DoriFS on-disk structures and API
│   ├── syscall.h           # System call numbers and handler prototypes
│   ├── gdt.h               # GDT structs and prototypes
│   ├── isr.h               # ISR register frame (registers_t)
│   ├── string.h            # String/memory function prototypes
│   └── ...                 # Additional headers for each subsystem
│
├── lib/
│   └── string.c            # memset, memcpy, memmove, strlen, strcmp, strncmp,
│                           # strcpy, strncpy, itoa, utoa
│
├── iso/
│   └── boot/
│       └── grub/
│           └── grub.cfg    # GRUB bootloader config
│
├── linker.ld               # Kernel linker script — memory layout at 1 MiB
└── Makefile                # Build system — see Build section
```

---

## 🔬 Subsystem Documentation

### Boot Sequence

```
GRUB (Multiboot)
    │
    └─► boot/multiboot.asm (_start)
            • Validates Multiboot magic
            • Sets up initial stack
            • Calls kernel_main(multiboot_info*)
                │
                └─► kernel/kernel.c (kernel_main)
                        • gdt_init()
                        • idt_init()
                        • pic_init()
                        • pit_init()
                        • pmm_init()
                        • vmm_init()
                        • heap_init()
                        • vga_init()
                        • keyboard_init()
                        • serial_init()
                        • ata_init()
                        • vfs_init()
                        • dorifs_mount()
                        • syscall_init()
                        • process_init()
                        • kshell_run()   ← waits for input here
```

### GDT — Global Descriptor Table (`kernel/gdt.c`)

The GDT has **6 entries**:

| # | Descriptor | Ring | Base | Limit | Purpose |
|---|------------|------|------|-------|---------|
| 0 | Null | — | 0 | 0 | Required null descriptor |
| 1 | Kernel Code | 0 | 0 | 4 GB | Kernel `.text` execution |
| 2 | Kernel Data | 0 | 0 | 4 GB | Kernel `.data`/`.bss`/stack |
| 3 | User Code | 3 | 0 | 4 GB | Userspace program execution |
| 4 | User Data | 3 | 0 | 4 GB | Userspace stack and data |
| 5 | TSS | — | `&tss` | sizeof(tss) | Task State Segment for Ring 0↔3 transitions |

The TSS stores the kernel stack pointer (`esp0`) which the CPU loads automatically when transitioning from Ring 3 → Ring 0 on a syscall or interrupt. `gdt_set_kernel_stack(uint32_t stack)` is called by the process manager to update this before switching to a user process.

### VFS — Virtual Filesystem (`kernel/vfs.c` + `include/vfs.h`)

The VFS is the unified interface between kernel code (and syscalls) and the underlying filesystem driver. It abstracts away which filesystem is actually handling the data — you call `vfs_read()` and the VFS dispatches through the appropriate driver's function pointer table.

**Limits:**
- Maximum open file descriptors: **64** (`VFS_MAX_OPEN_FILES`)
- Maximum mount points: **8** (`VFS_MAX_MOUNTS`)
- Maximum path length: **256** bytes
- Maximum filename: **255** bytes

**Node types:**

| Constant | Value | Meaning |
|----------|-------|---------|
| `VFS_FILE` | 0x01 | Regular file |
| `VFS_DIRECTORY` | 0x02 | Directory |
| `VFS_SYMLINK` | 0x04 | Symbolic link |
| `VFS_MOUNTPOINT` | 0x08 | Mount point — VFS transparent redirect |

**Open flags:**

| Flag | Value | Meaning |
|------|-------|---------|
| `VFS_O_RDONLY` | 0x0000 | Read only |
| `VFS_O_WRONLY` | 0x0001 | Write only |
| `VFS_O_RDWR` | 0x0002 | Read + write |
| `VFS_O_CREAT` | 0x0040 | Create if not exists |
| `VFS_O_TRUNC` | 0x0200 | Truncate to zero on open |
| `VFS_O_APPEND` | 0x0400 | Writes go to end of file |

**VFS public API:**

```c
/* Initialise the VFS */
void        vfs_init(void);

/* File descriptor operations */
int         vfs_open(const char* path, uint32_t flags);
int         vfs_close(int fd);
ssize_t     vfs_read(int fd, void* buffer, uint32_t size);
ssize_t     vfs_write(int fd, const void* buffer, uint32_t size);
int         vfs_seek(int fd, int32_t offset, int whence);  /* SET, CUR, END */
int         vfs_stat_fd(int fd, vfs_stat_t* st);

/* Path operations */
int         vfs_stat(const char* path, vfs_stat_t* st);
int         vfs_mkdir(const char* path);
int         vfs_create(const char* path, uint32_t type);
int         vfs_unlink(const char* path);
int         vfs_readdir(const char* path, uint32_t index, vfs_dirent_t* out);
int         vfs_symlink(const char* target, const char* linkpath);
ssize_t     vfs_readlink(const char* path, char* buf, size_t bufsiz);

/* Mount / unmount */
int         vfs_mount(const char* path, vfs_node_t* fs_root);
int         vfs_umount(const char* path);

/* Path resolution (follows symlinks, traverses mount points) */
vfs_node_t* vfs_resolve_path(const char* path);
```

**How filesystems plug in:**  
Every VFS node carries a pointer to a `vfs_ops_t` struct — a table of function pointers (`open`, `close`, `read`, `write`, `readdir`, `finddir`, `create`, `unlink`, `stat`, `truncate`, `symlink`, `readlink`). When DoriFS mounts, it fills in this table with its own implementations. The VFS always calls through these pointers and never touches disk directly.

### DoriFS — On-Disk Filesystem (`kernel/dorifs.c` + `include/dorifs.h`)

DoriFS is MonoOS's own custom filesystem, designed to sit on top of the ATA driver. It is modelled after a simplified Unix filesystem (similar to ext2).

**Disk Layout:**

```
┌─────────────────────────────────────────────────────┐
│  Sector 0         │  Superblock (magic=0x444F5249)  │
├─────────────────────────────────────────────────────┤
│  Sectors 1..N     │  Inode Table (up to 1024 inodes)│
├─────────────────────────────────────────────────────┤
│  Sectors N+1..M   │  Data Bitmap                    │
├─────────────────────────────────────────────────────┤
│  Sectors M+1..END │  Data Blocks (4096 bytes each)  │
└─────────────────────────────────────────────────────┘
```

**Superblock fields (magic `0x444F5249` = "DORI"):**
- Total blocks and inodes
- Free block and free inode counts
- Starting sector for inode table, data bitmap, and data blocks
- Block size (4096 bytes)

**Inode structure:**
- Type: `FREE`, `FILE`, `DIR`, or `SYMLINK`
- Size in bytes, block count, timestamps (create + modify)
- **12 direct block pointers** + **1 single-indirect block pointer**
- Max file size: `12 × 4096 + 1024 × 4096 = ~4.2 MB` per file
- Symlink target stored directly in inode (for short paths)
- Link count for hard links

**Disk interaction:**  
DoriFS calls the ATA driver for all reads and writes. Sectors are 512 bytes; DoriFS works in logical 4096-byte blocks (8 sectors each). The data bitmap is cached in kernel RAM to speed up free-block searches.

**Public API:**

```c
/* Format a partition starting at partition_lba with total_sectors sectors */
int         dorifs_format(uint32_t partition_lba, uint32_t total_sectors);

/* Mount a DoriFS partition — returns a VFS root node to pass to vfs_mount() */
vfs_node_t* dorifs_mount(uint32_t partition_lba);

/* Unmount and release resources */
void        dorifs_unmount(void);

/* Print filesystem statistics (inode/block usage) */
void        dorifs_print_info(void);
```

**Formatting a new disk in the shell:**
```
> format
> mount /
```
*(exact command names depend on kshell implementation)*

### System Calls (`kernel/syscall.c` + `include/syscall.h`)

System calls are invoked via `int 0x80` with the call number in `eax` and arguments in the general-purpose registers. The handler reads `registers_t* regs` (the full CPU state saved by the ISR stub) and dispatches to the appropriate kernel function.

**All 19 system calls:**

| # | Name | Description |
|---|------|-------------|
| 0 | `SYS_EXIT` | Terminate the current process |
| 1 | `SYS_READ` | Read bytes from a file descriptor |
| 2 | `SYS_WRITE` | Write bytes to a file descriptor |
| 3 | `SYS_OPEN` | Open a file by path, return fd |
| 4 | `SYS_CLOSE` | Close a file descriptor |
| 5 | `SYS_STAT` | Get file metadata (size, type, timestamps) |
| 6 | `SYS_SEEK` | Reposition file offset (SET / CUR / END) |
| 7 | `SYS_READDIR` | Read a directory entry by index |
| 8 | `SYS_EXEC` | Load and execute an ELF binary |
| 9 | `SYS_FORK` | Fork the current process *(defined, implementation pending)* |
| 10 | `SYS_WAITPID` | Wait for a child process to exit |
| 11 | `SYS_BRK` | Adjust program break (heap expansion) |
| 12 | `SYS_GETPID` | Return the current process ID |
| 13 | `SYS_MKDIR` | Create a directory |
| 14 | `SYS_UNLINK` | Delete a file or directory |
| 15 | `SYS_SYMLINK` | Create a symbolic link |
| 16 | `SYS_READLINK` | Read the target of a symbolic link |
| 17 | `SYS_GETCWD` | Get the current working directory path |
| 18 | `SYS_CHDIR` | Change the current working directory |

### Process Manager + Context Switch (`kernel/process.c`, `boot/context_switch.asm`)

The process manager maintains Process Control Blocks (PCBs) for each running process. Each PCB tracks the process ID, saved stack pointer, and state.

**Context switch (`context_switch`):**  
Written in Assembly for precision. It saves the callee-saved registers (`edi`, `esi`, `ebx`, `ebp`) onto the current stack, writes the current `esp` to `*old_esp`, loads the new stack pointer, and restores the registers from the new stack — the `ret` instruction then continues execution in the new process.

```asm
; Signature: void context_switch(uint32_t* old_esp, uint32_t new_esp)
context_switch:
    push ebp / push ebx / push esi / push edi
    mov [eax], esp      ; save old stack
    mov esp, edx        ; load new stack
    pop edi / pop esi / pop ebx / pop ebp
    ret                 ; jumps to new process's EIP
```

**Ring 3 transition:**  
The GDT has Ring 3 code/data segments (entries 3 and 4) and a TSS (entry 5). `gdt_set_kernel_stack()` keeps the TSS updated with the current kernel stack so that interrupts from Ring 3 correctly save state and switch to Ring 0.

### ELF Loader (`kernel/elf.c`)

Loads ELF32 executables from the filesystem. The loader:
1. Reads the ELF header and validates the magic bytes (`0x7F 'E' 'L' 'F'`) and machine type (i386)
2. Iterates the program header table
3. Loads `PT_LOAD` segments into memory at their specified virtual addresses
4. Handles BSS (zero-fill) segments
5. Returns the ELF entry point for the process manager to jump to

This is the foundation for running userspace programs. Combined with `SYS_EXEC`, it allows the shell to launch ELF binaries from disk.

### ATA / IDE Driver (`drivers/ata.c`)

Uses **PIO (Programmed I/O) mode** to read and write 512-byte sectors to an IDE disk via the standard x86 I/O ports (`0x1F0`–`0x1F7` for primary channel). PIO mode is slower than DMA but simpler to implement correctly.

In QEMU this maps to the `-drive file=monoos-disk.img,format=raw,if=ide` virtual disk. The disk image is created by `make disk` (64 MB, zeroed).

> ⚠️ **Warning:** On real hardware the ATA driver will address whatever IDE disk is connected. Never run on a machine with important disks attached.

### VGA Driver (`drivers/vga.c` + `include/vga.h`)

Writes to the VGA text buffer at physical address `0xB8000`. Each cell is 2 bytes: ASCII character + colour byte.

**Full 16-colour palette:**

```
VGA_BLACK=0   VGA_BLUE=1      VGA_GREEN=2      VGA_CYAN=3
VGA_RED=4     VGA_MAGENTA=5   VGA_BROWN=6      VGA_LIGHT_GREY=7
VGA_DARK_GREY=8  VGA_LIGHT_BLUE=9  VGA_LIGHT_GREEN=10  VGA_LIGHT_CYAN=11
VGA_LIGHT_RED=12  VGA_LIGHT_MAGENTA=13  VGA_YELLOW=14  VGA_WHITE=15
```

**API:**
```c
void vga_init(void);
void vga_clear(void);
void vga_set_color(uint8_t fg, uint8_t bg);  /* e.g. VGA_WHITE, VGA_BLUE */
void vga_putchar(char c);
void vga_puts(const char* str);
void vga_put_hex(uint32_t value);            /* prints 0xXXXXXXXX */
void vga_put_dec(uint32_t value);
void vga_set_cursor(int x, int y);
```

### Serial Driver (`drivers/serial.c`)

Outputs to COM1. Useful for piping kernel debug messages to the host terminal when running under QEMU with `-serial stdio`. Any `serial_write()` call from kernel code will appear in the terminal that launched QEMU — no VGA needed.

### Kernel Heap (`kernel/heap.c`)

Provides `kmalloc(size_t size)` and `kfree(void* ptr)`. Both are implemented. The heap grows upward from a fixed base address above the kernel image. Allocations that would exceed the reserved heap region will fail and return `NULL` — callers must check return values.

### `lib/string.c`

A complete hand-written C string library — no glibc:

```c
void*  memset  (void* dest, int val, size_t count);
void*  memcpy  (void* dest, const void* src, size_t count);
void*  memmove (void* dest, const void* src, size_t count);  /* handles overlap */
size_t strlen  (const char* str);
int    strcmp  (const char* s1, const char* s2);
int    strncmp (const char* s1, const char* s2, size_t n);
char*  strcpy  (char* dest, const char* src);
char*  strncpy (char* dest, const char* src, size_t n);
void   itoa    (int value, char* buf, int base);             /* signed, any base */
void   utoa    (uint32_t value, char* buf, int base);        /* unsigned, any base */
```

> ⚠️ **`strcpy` and `strncpy` have no overflow detection.** Pass a destination buffer large enough for the source string. Using `strncpy` with an explicit `n` is safer.

---

## 🛠️ Prerequisites

**Linux is strongly recommended** as the build host.

| Tool | Purpose | How to Install |
|------|---------|----------------|
| `i686-elf-gcc` | 32-bit bare-metal C cross-compiler | Build from source — [OSDev Wiki: GCC Cross-Compiler](https://wiki.osdev.org/GCC_Cross-Compiler) |
| `i686-elf-ld` | Bare-metal linker | Comes with the cross-compiler binutils build |
| `nasm` | Assemble `.asm` boot and context switch files | `sudo apt install nasm` |
| `grub-mkrescue` | Build bootable ISO | `sudo apt install grub-pc-bin grub-common xorriso` |
| `xorriso` | ISO creation backend | `sudo apt install xorriso` |
| `qemu-system-i386` | Run the OS in a 32-bit VM | `sudo apt install qemu-system-x86` |
| `make` | Run the build | `sudo apt install make` |
| `dd` | Create disk image | Pre-installed on all Linux systems |

> ⚠️ **The cross-compiler must be `i686-elf-gcc`, not `x86_64-elf-gcc` and not your system GCC.** MonoOS compiles to 32-bit ELF. Using the wrong compiler will produce a binary that does not boot. Building the cross-compiler takes about 30 minutes but only needs to be done once.

### nixpkgs (Coming Soon)

nixpkgs integration is currently in development. When complete, it will let you enter a fully reproducible build shell with all tools pre-configured — no manual cross-compiler build required:

```bash
# Future workflow (not yet available):
nix develop
make
```

Watch the repository for a `flake.nix` / `shell.nix` commit.

---

## 🔧 Building MonoOS

### 1. Clone the repository

```bash
git clone https://github.com/brah4729/Mono-Os.git
cd Mono-Os
```

### 2. Build the kernel and ISO

```bash
make
```

Produces `monoos.iso` — a bootable ISO with GRUB and `dori.kernel` embedded.

### 3. Create the disk image (required for ATA / DoriFS)

```bash
make disk
```

Creates `monoos-disk.img` — a 64 MB zeroed disk image. This is the virtual hard drive the ATA driver and DoriFS use. You need to run this once before `make run`.

### 4. Clean build artifacts

```bash
make clean       # Removes objects, kernel binary, ISO
make cleanall    # Also removes monoos-disk.img
```

### Common Build Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `i686-elf-gcc: command not found` | Cross-compiler not installed | Build it: [OSDev Wiki: GCC Cross-Compiler](https://wiki.osdev.org/GCC_Cross-Compiler) |
| `grub-mkrescue: command not found` | GRUB tools missing | `sudo apt install grub-pc-bin xorriso` |
| `undefined reference to __stack_chk_fail` | Using system GCC instead of cross-compiler | Use `i686-elf-gcc` |
| `ld: cannot find -lgcc` | Cross-compiler built without libgcc | Rebuild cross-compiler with libgcc support |
| `error: multiboot header not found` | Multiboot magic missing | Verify `boot/multiboot.asm` Multiboot header is in first 32 KB of binary |

---

## 🚀 Running MonoOS

### Normal run (with disk image)

```bash
make run
```

Equivalent to:
```bash
qemu-system-i386 -cdrom monoos.iso -serial stdio -m 128M \
    -drive file=monoos-disk.img,format=raw,if=ide
```

The disk image must exist (`make disk`) before running this.

### Run without disk (CD-ROM only, no filesystem)

```bash
make run-nodisk
```

VFS and DoriFS will not have a disk to mount. Useful for testing boot and kernel init only.

### Debug mode (GDB attach)

```bash
make debug
```

Starts QEMU with `-s -S` — the CPU halts on boot and waits for a GDB connection on port 1234.

In a second terminal:
```bash
gdb dori.kernel
(gdb) target remote :1234
(gdb) break kernel_main
(gdb) continue
```

### Recommended QEMU flags for deeper debugging

```bash
qemu-system-i386 \
  -cdrom monoos.iso \
  -drive file=monoos-disk.img,format=raw,if=ide \
  -m 128M \
  -serial stdio \
  -no-reboot \
  -no-shutdown \
  -d int,cpu_reset
```

| Flag | Effect |
|------|--------|
| `-serial stdio` | Serial output (`drivers/serial.c`) appears in your terminal |
| `-no-reboot` | Halts on crash instead of looping — prevents hiding triple faults |
| `-no-shutdown` | Keeps QEMU open after `hlt` so you can read final output |
| `-d int,cpu_reset` | Logs every interrupt and CPU reset to stderr |

### What a successful boot looks like

GRUB briefly shows its menu, then hands off. You should see:
- Kernel banner / version string in VGA text output
- Each subsystem init printed to screen (GDT, IDT, PMM, heap, ATA, VFS, DoriFS...)
- The `kshell` prompt waiting for input

---

## 🐛 Known Bugs & Active Issues

### 🔴 Critical

- **ATA driver uses PIO mode.** PIO is synchronous and blocks the CPU for every disk read/write. Large reads will visibly stall the kernel. DMA support is on the roadmap.

### 🟠 High

- **No process scheduler running yet.** The process manager and context switch are implemented, but nothing currently calls the scheduler in a timer interrupt. `SYS_EXEC` can load and jump to a binary but multi-process preemption is not active.
- **`SYS_FORK` is not implemented.** The syscall number is defined and the handler is registered, but the implementation is pending.
- **VGA driver does not scroll.** When the 80×25 buffer fills, output wraps from the top. The shell becomes unreadable after enough output. This is the most immediately painful known issue for interactive use.

### 🟡 Medium

- **No bounds checking in `strcpy` / `strcat` in `lib/string.c`.** Passing a string longer than the destination buffer silently corrupts adjacent memory. Always use `strncpy` with an explicit length when the source size is not guaranteed.
- **Heap allocator has no guard pages.** Overflowing a heap allocation into the next block produces silent corruption with no detection until something breaks later.
- **DoriFS data bitmap is cached in RAM but not write-through.** If the kernel crashes between a bitmap update and the next full flush, the on-disk bitmap may be inconsistent. Run `dorifs_format()` to recover a corrupted disk image.
- **Hardware cursor unmanaged.** The blinking cursor does not follow VGA output — it stays at the BIOS default position.

### 🟢 Low / Cosmetic

- No version string or ASCII banner on startup.
- GRUB `timeout` may need adjustment in `iso/boot/grub/grub.cfg` to auto-boot.

---

## 🗺️ Roadmap

### ✅ Completed
- [x] GRUB Multiboot boot
- [x] GDT with Ring 0/3 segments and TSS
- [x] IDT + CPU exception handlers
- [x] PIC remapping + IRQ handling
- [x] PIT (system timer)
- [x] VGA text driver with 16-colour support
- [x] Serial port debug output
- [x] PS/2 keyboard driver
- [x] Physical memory manager
- [x] Virtual memory manager
- [x] Kernel heap (`kmalloc` + `kfree`)
- [x] ATA/IDE disk driver (PIO)
- [x] VFS layer with mount points, symlinks, file descriptors
- [x] DoriFS on-disk filesystem
- [x] System call interface (19 calls via `int 0x80`)
- [x] Process manager + context switch
- [x] ELF32 binary loader
- [x] Interactive kernel shell

### 🔄 In Progress
- [ ] nixpkgs integration — reproducible build environment + package manager
- [ ] Process scheduler (timer-driven preemption via PIT IRQ0)
- [ ] Ring 3 userspace — full enforcement of privilege separation
- [ ] `SYS_FORK` implementation

### 🔮 Upcoming
- [ ] VGA scrolling
- [ ] ATA DMA mode (replaces PIO — much faster disk I/O)
- [ ] `SYS_WAITPID` full implementation
- [ ] Larger filesystem support (multi-indirect blocks)
- [ ] Userspace C library (libc stub)
- [ ] Networking (NIC driver + TCP/IP stack)
- [ ] ACPI power management (shutdown/reboot from software)

---

## 🔍 Troubleshooting

### QEMU resets immediately after GRUB
Triple fault. Run with `-d int,cpu_reset -no-reboot` and check stderr for which interrupt vector fired. Most likely: GDT descriptor access byte is wrong, or IDT was loaded before all ISR stubs were installed.

### Black screen — nothing on VGA
The kernel may be executing but the VGA driver is not writing. Add a single early write to `0xB8000` in `boot/multiboot.asm` (before the `call kernel_main`) as a sanity check. If that character appears, the issue is in `vga_init()`.

### Keyboard does not respond
Check: (1) `sti` is called after `idt_init()`, (2) IRQ1 is unmasked in the PIC's IMR (port `0x21`), (3) the keyboard ISR stub in `idt.c` correctly calls `keyboard_handler`.

### DoriFS panics or returns errors on boot
The disk image may be unformatted or corrupted. Run `make cleanall && make disk` to recreate a fresh zeroed image, then boot and format from the shell.

### Serial output not appearing in terminal
Make sure you're using `make run` (or manually passing `-serial stdio`). Serial output goes to **stdout** of the QEMU process. If you launched QEMU from a script that redirects output, it may be going somewhere unexpected.

### GDB `target remote :1234` fails
You need to use `make debug` — not `make run`. The `-s -S` flags are only added in the debug target.

---

## 🤝 Contributing

Bug reports, fixes, and features are welcome.

1. Fork the repository
2. Create a branch: `git checkout -b feature/your-feature`
3. Commit with clear messages explaining what and why
4. Open a pull request

For significant changes (new subsystem, memory layout, syscall additions) please open an issue first to discuss.

---

## 📚 Learning Resources

These are directly relevant to MonoOS's implementation:

- [OSDev Wiki](https://wiki.osdev.org) — everything in this codebase is covered here
- [Intel IA-32 Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) — GDT/IDT/TSS/paging specs
- [GRUB Multiboot Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html)
- [ELF-32 Specification](https://refspecs.linuxbase.org/elf/elf.pdf) — for `kernel/elf.c`
- [James Molloy's Kernel Tutorial](http://www.jamesmolloy.co.uk/tutorial_html/) — covers GDT, IDT, paging, heap
- [Writing a Simple OS from Scratch — Nick Blundell (PDF)](https://www.cs.bham.ac.uk/~exr/lectures/opsys/10_11/lectures/os-dev.pdf)
- [GNU Linker Scripts Reference](https://sourceware.org/binutils/docs/ld/Scripts.html)
- [ATA PIO Mode — OSDev Wiki](https://wiki.osdev.org/ATA_PIO_Mode)

---

## 📜 License

MonoOS is free and open-source software. See the repository for license details.

---

<div align="center">
<sub>MonoOS / Dori Kernel — built from scratch ☕</sub>
</div>
