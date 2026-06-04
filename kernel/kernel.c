/* kernel/kernel.c — Dori kernel entry point */

#include "../include/kernel.h"
#include "../include/types.h"
#include "../include/gdt.h"
#include "../include/idt.h"
#include "../include/pic.h"
#include "../include/pit.h"
#include "../include/pmm.h"
#include "../include/vmm.h"
#include "../include/heap.h"
#include "../include/vga.h"
#include "../include/keyboard.h"
#include "../include/serial.h"
#include "../include/kshell.h"
#include "../include/io.h"
#include "../include/string.h"
#include "../include/ata.h"
#include "../include/vfs.h"
#include "../include/dorifs.h"
#include "../include/syscall.h"
#include "../include/process.h"
#include "../include/elf.h"
#include "../include/framebuffer.h"
#include "../include/mouse.h"
#include "../include/oki.h"

#define MULTIBOOT2_MAGIC_CHECK 0x36D76289

/* ─── Multiboot2 tag parsing ──────────────────────────── */

#define MB2_TAG_END        0
#define MB2_TAG_BOOT_CMD   1
#define MB2_TAG_BASIC_MEM  4
#define MB2_TAG_MMAP       6
#define MB2_TAG_FRAMEBUFFER 8

typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) mb2_tag_header_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
} __attribute__((packed)) mb2_tag_basic_mem_t;

/* Parsed multiboot2 info */
static uint32_t mb2_mem_upper = 0;
static uint32_t mb2_info_addr_global = 0;

static void parse_multiboot2(uint32_t mb_info_addr) {
    mb2_info_addr_global = mb_info_addr;
    uint8_t* tag_ptr = (uint8_t*)(mb_info_addr + 8);

    while (1) {
        mb2_tag_header_t* tag = (mb2_tag_header_t*)tag_ptr;
        if (tag->type == MB2_TAG_END) break;

        if (tag->type == MB2_TAG_BASIC_MEM) {
            mb2_tag_basic_mem_t* mem = (mb2_tag_basic_mem_t*)tag;
            mb2_mem_upper = mem->mem_upper;
        }

        /* Advance to next tag (8-byte aligned) */
        uint32_t tag_size = (tag->size + 7) & ~7;
        tag_ptr += tag_size;
    }
}

/* ─── Banner ──────────────────────────────────────────── */

static void print_banner(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n");
    vga_puts("  __  __                            ___    ____  \n");
    vga_puts(" |  \\/  |   ___    _ __     ___    / _ \\  / ___| \n");
    vga_puts(" | |\\/| |  / _ \\  | '_ \\   / _ \\  | | | | \\___ \\ \n");
    vga_puts(" | |  | | | (_) | | | | | | (_) | | |_| |  ___) |\n");
    vga_puts(" |_|  |_|  \\___/  |_| |_|  \\___/   \\___/  |____/ \n");
    vga_puts("\n");

    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  MonoOS v" MONOOS_VERSION " - ");
    vga_puts(KERNEL_NAME);
    vga_puts(" Kernel\n");

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_puts("  -------------------------------------\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* ─── Memory init ────────────────────────────────────── */

static void init_memory(uint32_t mem_upper) {
    /* mem_upper is in KiB, starting at 1 MiB */
    uint32_t mem_size = (mem_upper + 1024) * 1024;
    
    /* Place bitmap right after kernel */
    extern uint32_t _kernel_end;
    uint32_t* bitmap = (uint32_t*)((uint32_t)&_kernel_end);

    /* Initialize PMM */
    pmm_init(mem_size, bitmap);

    /* Mark available memory regions */
    uint32_t bitmap_size = pmm_get_block_count() / 8;
    uint32_t kernel_end_aligned = (uint32_t)&_kernel_end + bitmap_size;
    if (kernel_end_aligned & 0xFFF) {
        kernel_end_aligned = (kernel_end_aligned & 0xFFFFF000) + PAGE_SIZE;
    }

    /* Mark memory above kernel as available */
    if (mem_size > kernel_end_aligned) {
        pmm_init_region(kernel_end_aligned, mem_size - kernel_end_aligned);
    }

    /* Report memory */
    vga_puts("  Memory: ");
    vga_put_dec(pmm_get_free_block_count() * 4);
    vga_puts(" KiB free / ");
    vga_put_dec(pmm_get_block_count() * 4);
    vga_puts(" KiB total\n");
}

/* ─── Mouse IRQ12 handler ────────────────────────────── */

static void mouse_irq12(registers_t* regs) {
    (void)regs;
    mouse_irq_handler();
}

/* ─── Kernel panic ───────────────────────────────────── */

void kernel_panic(const char* msg) {
    cli();
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_puts("\n*** DORI KERNEL PANIC ***\n");
    vga_puts(msg);
    vga_puts("\nSystem halted.\n");
    serial_puts("[PANIC] ");
    serial_puts(msg);
    serial_puts("\n");
    for (;;) hlt();
}

/* ─── Entry point ────────────────────────────────────── */

void kernel_main(uint32_t magic, uint32_t mboot_info_addr) {
    /* Initialize serial first for debug logging */
    serial_init();
    serial_puts("\n[DORI] MonoOS v" MONOOS_VERSION " booting...\n");

    /* Initialize VGA (text mode — used during early boot) */
    vga_init();
    print_banner();

    /* Verify multiboot2 magic */
    if (magic != MULTIBOOT2_MAGIC_CHECK) {
        kernel_panic("Not booted by a Multiboot2-compliant bootloader!");
        return;
    }
    serial_puts("[DORI] Multiboot2 verified\n");

    /* Parse multiboot2 tags */
    parse_multiboot2(mboot_info_addr);

    /* Initialize GDT */
    gdt_init();
    vga_puts("  [OK] GDT initialized\n");
    serial_puts("[DORI] GDT initialized\n");

    /* Initialize IDT */
    pic_init();
    idt_init();
    vga_puts("  [OK] IDT + PIC initialized\n");
    serial_puts("[DORI] IDT + PIC initialized\n");

    /* Initialize PIT at 1000 Hz */
    pit_init(1000);
    vga_puts("  [OK] PIT timer (1000 Hz)\n");

    /* Initialize memory */
    init_memory(mb2_mem_upper);
    vga_puts("  [OK] Physical memory manager\n");

    /* Initialize VMM */
    vmm_init();

    /* Initialize heap */
    heap_init();
    vga_puts("  [OK] Kernel heap\n");

    /* Initialize keyboard */
    keyboard_init();
    vga_puts("  [OK] PS/2 keyboard\n");

    /* Initialize mouse + register IRQ12 */
    mouse_init();
    irq_register_handler(12, mouse_irq12);
    vga_puts("  [OK] PS/2 mouse\n");
    serial_puts("[DORI] PS/2 mouse initialized\n");

    /* Initialize ATA disk driver */
    ata_init();
    if (ata_is_present()) {
        ata_drive_t drive = ata_get_drive_info();
        vga_puts("  [OK] ATA disk: ");
        vga_puts(drive.model);
        vga_puts(" (");
        vga_put_dec(drive.size_mb);
        vga_puts(" MB)\n");
    } else {
        vga_puts("  [--] No ATA disk detected\n");
    }

    /* Initialize VFS */
    vfs_init();
    vga_puts("  [OK] Virtual filesystem\n");

    /* Mount DoriFS if disk is present */
    if (ata_is_present()) {
        vfs_node_t* root = dorifs_mount(0);
        if (!root) {
            vga_puts("  [..] Formatting disk as DoriFS...\n");
            ata_drive_t drive = ata_get_drive_info();
            dorifs_format(0, drive.size_sectors);
            root = dorifs_mount(0);
        }
        if (root) {
            vfs_mount("/", root);
            vga_puts("  [OK] DoriFS mounted at /\n");

            vfs_mkdir("/nix");
            vfs_mkdir("/nix/store");
            vfs_mkdir("/nix/var");
            vfs_mkdir("/nix/var/nix");
            vfs_mkdir("/nix/var/nix/profiles");
            vga_puts("  [OK] /nix/store ready\n");
        }
    }

    /* Initialize system calls */
    syscall_init();
    vga_puts("  [OK] System calls (int 0x80)\n");

    /* Initialize process manager */
    process_init();
    vga_puts("  [OK] Process manager\n");

    /* Enable interrupts */
    sti();
    vga_puts("  [OK] Interrupts enabled\n");

    serial_puts("[DORI] All systems initialized\n");

    /* ─── Initialize framebuffer and start Oki DE ─── */
    fb_init_from_multiboot(mboot_info_addr);

    if (fb_is_available()) {
        vga_puts("  [OK] Framebuffer ready\n");
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_puts("\n  Starting Oki Desktop Environment...\n");

        /* Small delay so user can see boot messages */
        for (volatile int i = 0; i < 5000000; i++);

        /* Set mouse callback for Oki */
        mouse_set_callback(oki_handle_mouse);

        /* Initialize and run Oki DE */
        oki_init();
        oki_run();

        /* If oki_run returns, fall back to shell */
    }

    /* Fallback: text-mode shell */
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_puts("  -------------------------------------\n");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("  System ready. Type 'help' for commands.\n\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    kshell_init();
    kshell_run();
}
