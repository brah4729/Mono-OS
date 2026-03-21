# 🖥️ MonoOS

> **A monolithic kernel OS, built from scratch in C and Assembly — it boots, has a shell, and manages memory.**

[![Status](https://img.shields.io/badge/status-early%20development-yellow?style=flat-square)](https://github.com/brah4729/Mono-Os)
[![Boot](https://img.shields.io/badge/boots%20in%20QEMU-%E2%9C%93-brightgreen?style=flat-square)]()
[![Shell](https://img.shields.io/badge/shell-%E2%9C%93-brightgreen?style=flat-square)]()
[![Architecture](https://img.shields.io/badge/arch-x86__64-blue?style=flat-square)]()
[![Language](https://img.shields.io/badge/language-C%20%2F%20Assembly-lightgrey?style=flat-square)]()
[![License](https://img.shields.io/badge/license-open--source-green?style=flat-square)]()

---

## ⚠️ Safety Warnings — Please Read First

MonoOS is **experimental pre-release software**. It is not stable. It is not a daily driver. The following risks apply to anyone building or running it:

| Risk | Level | Details |
|------|-------|---------|
| **Data Loss** | 🔴 CRITICAL | No filesystem protection exists. MonoOS may write to or corrupt disk partitions if run on real hardware. |
| **No Recovery Mode** | 🔴 CRITICAL | There is no safe mode, rescue shell, or recovery mechanism if something goes wrong. |
| **Untested on Real Hardware** | 🟠 HIGH | Only tested inside QEMU. Behaviour on physical machines is completely unknown. |
| **No UEFI Support** | 🟡 MEDIUM | Legacy BIOS boot only (GRUB Multiboot2). Do not attempt EFI boot. |
| **No Memory Deallocation** | 🟡 MEDIUM | The memory manager allocates but cannot yet free memory. Sustained heavy allocation will exhaust RAM over time. |
| **No Process Isolation** | 🟡 MEDIUM | All code runs in kernel mode (Ring 0). A bug anywhere can affect everything else. |

> ✅ **The only safe way to run MonoOS:** inside a virtual machine using QEMU. Do not boot on bare metal with data you care about.

---

## 📖 What is MonoOS?

MonoOS is a hobby operating system written from the ground up — no borrowed kernel code, no OS underneath it. The goal is to understand how a real operating system works at the lowest level: hardware initialisation, CPU privilege rings, interrupt routing, memory management, and device drivers, all implemented entirely by hand.

It uses a **monolithic kernel architecture**, meaning the kernel, drivers, and core services all share the same address space and privilege level (Ring 0). This is the same model used by Linux and most traditional Unix kernels.

The project targets **64-bit x86 (x86-64)** processors and boots via GRUB using the Multiboot2 specification.

---

## ✅ What Works Right Now

As of the current build, MonoOS successfully:

| Feature | Status | Notes |
|---------|--------|-------|
| **GRUB Multiboot2 boot** | ✅ Working | Stable handoff from GRUB → Assembly stub → `kernel_main()` |
| **GDT initialisation** | ✅ Working | Kernel-mode segment descriptors configured |
| **IDT & exception handling** | ✅ Working | All 256 interrupt vectors registered; CPU exceptions handled |
| **PIC & hardware IRQs** | ✅ Working | PIC remapped; IRQ0 (timer) and IRQ1 (keyboard) active |
| **VGA text output** | ✅ Working | Prints to the 80×25 character buffer in text mode |
| **PS/2 keyboard input** | ✅ Working | Reads scan codes, translates to ASCII, echoes to screen |
| **Basic memory manager** | ✅ Working | Physical memory allocation is functional |
| **Shell** | ✅ Working | Interactive command input via keyboard is available |

---

## ❌ What Does NOT Work Yet

| Feature | Notes |
|---------|-------|
| Filesystem | No disk reads or writes from userspace |
| Userspace / Ring 3 | All code still runs in kernel mode |
| Process scheduler | No multitasking; single flow of execution only |
| Virtual memory / paging | Running on physical (identity-mapped) memory |
| Memory deallocation (`kfree`) | Allocations cannot be freed yet |
| VGA scrolling | Screen wraps when the 80×25 buffer fills |
| Networking | Not started |
| UEFI / ACPI | Not supported |

---

## 📁 Project Structure

```
Mono-Os/
│
├── boot/                   # Bootloader stub — the very first code to execute
│   └── boot.asm            # x86-64 Assembly entry, Multiboot2 header, initial stack setup
│
├── kernel/                 # Core kernel source
│   ├── kernel.c            # kernel_main() — C entry point, subsystem init sequence
│   └── ...                 # GDT, IDT, ISR/IRQ handlers, PIC configuration
│
├── drivers/                # Hardware driver implementations
│   ├── vga.c               # VGA text mode driver (writes to 0xB8000)
│   ├── keyboard.c          # PS/2 keyboard driver (reads from I/O port 0x60)
│   └── ...
│
├── include/                # Shared C header files
│   └── *.h                 # Structs, typedefs, and function prototypes
│
├── lib/                    # Minimal hand-written C library (no glibc)
│   └── *.c                 # memset, memcpy, strlen, itoa, and so on
│
├── iso/
│   └── boot/
│       └── grub/
│           └── grub.cfg    # GRUB bootloader configuration
│
├── linker.ld               # Kernel linker script — defines physical memory layout
├── Makefile                # Build system
└── implementation_plan.md.resolved   # Internal development notes
```

### Component Deep Dive

#### `boot/boot.asm`
The absolute first code that runs. It declares the Multiboot2 header so GRUB can recognise the binary, sets up the initial kernel stack, and calls into `kernel_main()`. If `kernel_main` ever returns (it should not), the boot stub halts the CPU in a permanent `hlt` loop.

#### `kernel/`
The core of MonoOS. Initialises all fundamental CPU structures in order:

- **GDT (Global Descriptor Table)** — defines kernel-mode memory segments. Must be loaded before the IDT can function.
- **IDT (Interrupt Descriptor Table)** — registers handlers for all 256 interrupt vectors, covering CPU exceptions (divide-by-zero, general protection fault, page fault, etc.) and hardware IRQs.
- **PIC (8259 Programmable Interrupt Controller)** — remapped so hardware IRQ vectors don't collide with CPU exception numbers. IRQ0 (system timer) and IRQ1 (PS/2 keyboard) are unmasked and active.
- **`kernel_main()`** — calls each subsystem's init function in the correct order, then drops into the shell or kernel idle loop.

#### `drivers/vga.c`
Writes characters and colour attributes directly to the VGA text buffer at physical address `0xB8000`. Each cell in the 80×25 grid is 2 bytes: one for the ASCII character and one for the colour attribute byte (foreground and background packed into nibbles). The driver maintains an X/Y cursor position and handles newline characters. **Scrolling is not yet implemented** — when the buffer fills, output continues from the top of the screen.

#### `drivers/keyboard.c`
An IRQ1-driven PS/2 keyboard driver. On each keypress the CPU fires IRQ1, the handler reads the raw scan code from I/O port `0x60`, and a lookup table translates it to an ASCII character. Basic shift-state tracking is implemented. Some special keys (function keys, arrow keys, numpad) are not yet fully mapped.

#### Shell
An interactive command interface built on top of the keyboard and VGA drivers. Accepts typed input, processes it on Enter, and dispatches to registered command handlers. This is the primary way to interact with a running MonoOS instance. Available commands and their behaviour depend on the current build.

#### Memory Manager
Manages physical memory allocation for the kernel. Tracks which regions of RAM are available (as reported by the Multiboot2 memory map from GRUB) and which are already occupied by the kernel image, BIOS regions, or hardware. Provides a `kmalloc`-style interface for allocating fixed-size blocks. **Deallocation (`kfree`) is not yet implemented.**

#### `lib/`
A minimal hand-written substitute for the C standard library. Because the kernel has no operating system beneath it, standard functions like `memset`, `memcpy`, `strlen`, and number-to-string conversion must be reimplemented from scratch. **No external library is linked.**

#### `linker.ld`
Instructs the GNU linker exactly where to place each ELF section in physical memory. The kernel loads at `0x100000` (1 MiB) — above BIOS-reserved lower memory — as required by the Multiboot specification. It also defines `__kernel_start` and `__kernel_end` symbols so the memory manager knows which RAM is already occupied by the kernel binary.

---

## 🛠️ Prerequisites

**Linux is strongly recommended** as the build host. All required tools:

| Tool | Purpose | How to Install |
|------|---------|----------------|
| `x86_64-elf-gcc` | Cross-compiler for bare-metal C | Build from source — [OSDev Wiki: GCC Cross-Compiler](https://wiki.osdev.org/GCC_Cross-Compiler) |
| `x86_64-elf-ld` | Bare-metal linker | Comes with the cross-compiler binutils build |
| `nasm` | Assemble `.asm` boot files | `sudo apt install nasm` |
| `grub-mkrescue` | Build the bootable ISO image | `sudo apt install grub-pc-bin grub-common xorriso` |
| `xorriso` | ISO creation backend (used by grub-mkrescue) | `sudo apt install xorriso` |
| `qemu-system-x86_64` | Run the OS in a virtual machine | `sudo apt install qemu-system-x86` |
| `make` | Run the build system | `sudo apt install make` |

> ⚠️ **You must use the `x86_64-elf` cross-compiler — not your system's native GCC.** A native GCC build will link against the host OS and produce a binary that will not boot. This is the most common cause of build failure for new contributors. Building the cross-compiler takes about 30 minutes but only needs to be done once.

---

## 🔧 Building MonoOS

### 1. Clone the repository

```bash
git clone https://github.com/brah4729/Mono-Os.git
cd Mono-Os
```

### 2. Build the kernel and create the bootable ISO

```bash
make
```

A successful build produces `mono.iso` — a bootable ISO image containing the GRUB bootloader and the MonoOS kernel binary.

### 3. Clean all build artifacts

```bash
make clean
```

### Common Build Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `x86_64-elf-gcc: command not found` | Cross-compiler not installed | Build it following the [OSDev cross-compiler guide](https://wiki.osdev.org/GCC_Cross-Compiler) |
| `grub-mkrescue: command not found` | GRUB tools not installed | `sudo apt install grub-pc-bin xorriso` |
| `undefined reference to __stack_chk_fail` | Wrong compiler (system GCC used) | Switch to `x86_64-elf-gcc` |
| `ld: cannot find -lgcc` | Cross-compiler built without libgcc | Rebuild the cross-compiler including libgcc |
| `error: multiboot header not found` | Multiboot2 magic missing or misaligned | Check `boot.asm` — the header must appear in the first 32 KB of the binary |

---

## 🚀 Running MonoOS

### Quick start

```bash
make run
```

### Manual QEMU command

```bash
qemu-system-x86_64 -cdrom mono.iso
```

### Recommended debug setup

```bash
qemu-system-x86_64 \
  -cdrom mono.iso \
  -m 128M \
  -serial stdio \
  -no-reboot \
  -no-shutdown \
  -d int,cpu_reset
```

| Flag | What it Does |
|------|-------------|
| `-m 128M` | Allocate 128 MB RAM to the VM |
| `-serial stdio` | Pipe serial port output to your terminal |
| `-no-reboot` | Halt instead of rebooting on a crash — stops infinite reset loops |
| `-no-shutdown` | Keep QEMU open after shutdown so you can read final output |
| `-d int,cpu_reset` | Log every CPU interrupt and reset to stderr — essential for diagnosing triple faults |

### What a successful boot looks like

GRUB loads and briefly shows its menu, then boots MonoOS. You should see text on screen confirming that each subsystem (GDT, IDT, drivers, memory manager) has initialised. The shell prompt will appear, and the keyboard will be live and ready for input.

If QEMU resets immediately after GRUB, see the Troubleshooting section below.

---

## 🐛 Known Bugs & Active Issues

### 🟠 High

- **VGA driver does not scroll.** When all 80×25 cells are filled, the next character written starts from the top of the screen, overwriting existing output. Scrolling is not yet implemented — the shell will become unreadable after enough output.
- **Keyboard scan code map is incomplete.** Arrow keys, function keys (F1–F12), and numpad keys produce scan codes that are not mapped. They may produce unexpected output or be silently ignored. Stick to standard alphanumeric input for now.
- **No bounds checking in `lib/` string functions.** Functions such as `strcpy` and `strcat` do not validate buffer sizes. Passing a string longer than its destination buffer will silently corrupt adjacent memory with no error or crash — until something else breaks later.

### 🟡 Medium

- **Memory cannot be freed.** `kmalloc` works, but there is no corresponding `kfree`. All allocations are permanent for the lifetime of the session. The kernel will not crash from this under normal interactive use, but any code that allocates in a loop will eventually exhaust available RAM.
- **No memory isolation between subsystems.** A bug in one driver or shell command can corrupt data owned by another subsystem. There are no guard pages or domain separation.
- **Hardware cursor is unmanaged.** The blinking text cursor stays wherever the BIOS placed it rather than following VGA output.
- **GRUB timeout may need adjustment.** Depending on `grub.cfg`, the boot menu might wait for user input before booting. Edit the `timeout` value in `iso/boot/grub/grub.cfg` to auto-boot after N seconds.

### 🟢 Low / Cosmetic

- All VGA output is light grey on black — no colour scheme or syntax highlighting.
- No version string or welcome banner on startup.
- No `make install` target.

---

## 🗺️ Roadmap

### ✅ Completed
- [x] GRUB Multiboot2 boot sequence
- [x] Assembly → C kernel handoff
- [x] GDT initialisation
- [x] IDT and CPU exception (ISR) handlers
- [x] PIC remapping and hardware IRQ handling
- [x] VGA text mode driver
- [x] PS/2 keyboard driver
- [x] Basic physical memory manager
- [x] Interactive shell with command input

### 🔄 Next Priorities
- [ ] VGA scrolling — essential for usable shell output
- [ ] Complete keyboard scan code mapping (arrows, F-keys, shift combos)
- [ ] Memory deallocation (`kfree`)
- [ ] Serial port output for debug logging
- [ ] Shell command expansion (more built-in commands)

### 🔮 Longer Term
- [ ] Virtual memory and 4-level paging (x86-64)
- [ ] Kernel heap improvements
- [ ] System timer (PIT / APIC)
- [ ] Process scheduler (round-robin to start)
- [ ] Userspace / Ring 3 privilege separation
- [ ] System calls (`syscall` or `int 0x80`)
- [ ] VFS (Virtual Filesystem) abstraction
- [ ] Simple filesystem (FAT32 or custom)
- [ ] ELF binary loading and execution
- [ ] Userspace shell

---

## 🔍 Troubleshooting

### QEMU resets immediately after GRUB
This is almost always a **triple fault** — the CPU hit an unhandled exception during boot and reset itself. Run with `-d int,cpu_reset -no-reboot` to log which interrupt fired before the reset. The most common culprits are a malformed GDT descriptor or an IDT that was loaded before all handlers were installed.

### Black screen — no text output at all
The kernel may be reaching `kernel_main()` but the VGA driver is not writing correctly. Quick test: write a single character byte directly to address `0xB8000` in the Assembly boot stub, before calling into C. If that character appears, the VGA buffer is accessible and the issue is in driver initialisation. If not, check that the GDT's data segment descriptor covers the `0xB8000` physical region.

### Shell prompt appears but keyboard does nothing
The most common causes are: (1) `sti` was not called after the IDT was loaded (interrupts are disabled), or (2) IRQ1 is still masked in the PIC's Interrupt Mask Register. Check that the PIC init writes the correct mask to I/O ports `0x21` and `0xA1`.

### Screen fills up and becomes unreadable
VGA scrolling is not yet implemented. This is a known limitation — the screen wraps. Reboot the VM to clear it. Scrolling is the next priority on the roadmap.

### `x86_64-elf-gcc: command not found`
The cross-compiler needs to be built manually. Follow the full guide at https://wiki.osdev.org/GCC_Cross-Compiler — the process takes about 30 minutes but only needs to be done once per machine.

### `grub-mkrescue` fails or produces an unbootable ISO
Install `grub-pc-bin`, `grub-common`, and `xorriso`. On some distributions `grub-pc-bin` is packaged as `grub2-pc`. Also verify that `grub.cfg` uses the `multiboot2` command (not the older `multiboot`) to match the header in `boot.asm`.

---

## 🤝 Contributing

Bug reports, fixes, and new features are all welcome.

1. Fork the repository
2. Create a branch: `git checkout -b feature/your-feature-name`
3. Write clear, descriptive commit messages explaining what changed and why
4. Open a pull request with a short summary

For significant changes — new subsystems, memory layout changes, build system overhauls — please open an issue first to discuss the approach before writing code. It saves everyone time.

---

## 📚 Learning Resources

These references are directly relevant to the MonoOS codebase:

- [OSDev Wiki](https://wiki.osdev.org) — the definitive resource for everything in this project
- [Intel 64 and IA-32 Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) — authoritative CPU reference
- [GRUB Multiboot2 Specification](https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html)
- [James Molloy's Kernel Development Tutorial](http://www.jamesmolloy.co.uk/tutorial_html/)
- [Writing a Simple OS from Scratch — Nick Blundell (PDF)](https://www.cs.bham.ac.uk/~exr/lectures/opsys/10_11/lectures/os-dev.pdf)
- [GNU Linker Scripts Reference](https://sourceware.org/binutils/docs/ld/Scripts.html)
- [x86-64 System V ABI Specification](https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf)

---

## 📜 License

MonoOS is free and open-source software. See the repository for license details.

---

<div align="center">
<sub>Built from scratch with curiosity ☕ — MonoOS Team</sub>
</div>