# MonoOS — Dori Kernel Build System
# Cross-compiler toolchain: i686-elf-gcc

# Toolchain
CROSS_PREFIX = $(HOME)/opt/cross/bin/i686-elf-
CC      = $(CROSS_PREFIX)gcc
AS      = nasm
LD      = $(CROSS_PREFIX)ld

# Flags
CFLAGS  = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Werror \
           -I include -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
           -nostartfiles -nodefaultlibs
ASFLAGS = -f elf32
LDFLAGS = -T linker.ld -nostdlib

# Source files
ASM_SOURCES = boot/multiboot.asm boot/gdt.asm boot/idt.asm \
              boot/context_switch.asm
C_SOURCES   = kernel/kernel.c kernel/gdt.c kernel/idt.c kernel/pic.c \
              kernel/pit.c kernel/pmm.c kernel/vmm.c kernel/heap.c \
              kernel/kshell.c kernel/vfs.c kernel/dorifs.c \
              kernel/syscall.c kernel/process.c kernel/elf.c \
              drivers/vga.c drivers/keyboard.c drivers/serial.c \
              drivers/ata.c \
              lib/string.c

# Object files
ASM_OBJECTS = $(ASM_SOURCES:.asm=.o)
C_OBJECTS   = $(C_SOURCES:.c=.o)
OBJECTS     = $(ASM_OBJECTS) $(C_OBJECTS)

# Output
KERNEL  = dori.kernel
ISO     = monoos.iso
DISKIMG = monoos-disk.img

# ─── Targets ───────────────────────────────────────────────

.PHONY: all clean run iso disk

all: $(ISO)

$(ISO): $(KERNEL)
	@mkdir -p iso/boot/grub
	cp $(KERNEL) iso/boot/$(KERNEL)
	grub-mkrescue -o $(ISO) iso/ 2>/dev/null
	@echo ""
	@echo "═══════════════════════════════════════"
	@echo "  MonoOS ISO built: $(ISO)"
	@echo "  Run with: make run"
	@echo "═══════════════════════════════════════"

$(KERNEL): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^

# Assembly compilation
%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

# C compilation
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Create a 64 MB disk image for QEMU
disk: $(DISKIMG)

$(DISKIMG):
	dd if=/dev/zero of=$(DISKIMG) bs=1M count=64 2>/dev/null
	@echo "Created $(DISKIMG) (64 MB)"

# Run with disk attached
run: $(ISO) $(DISKIMG)
	qemu-system-i386 -cdrom $(ISO) -serial stdio -m 128M \
		-drive file=$(DISKIMG),format=raw,if=ide

# Run without disk (CD-ROM only)
run-nodisk: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -serial stdio -m 128M

debug: $(ISO) $(DISKIMG)
	qemu-system-i386 -cdrom $(ISO) -serial stdio -m 128M \
		-drive file=$(DISKIMG),format=raw,if=ide -s -S

clean:
	rm -f $(OBJECTS) $(KERNEL) $(ISO)
	rm -f iso/boot/$(KERNEL)
	@echo "Cleaned."

cleanall: clean
	rm -f $(DISKIMG)
	@echo "Cleaned everything (including disk image)."

