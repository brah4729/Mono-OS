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

#define MULTIBOOT2_MAGIC_CHECK 0x36D76289

static void print_banner(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n");
    vga_puts("  __   __                           ___     ____      \n");
    vga_puts(" |  \\/  |   ___    _ __    ___   / _  \\  / ___|    \n");
    vga_puts(" | |\\/| | / _ \\ | '_ \\ / _ \\ | |  \\ | \\ __    \n");
    vga_puts(" | |   | || (_) | | | | | | (_)  | |__ |_|  ____\\  \n");
    vga_puts(" |_|   |_| \\___/ |_| |_| \\___/ \\___ //  |____//    \n");
    vga_puts("\n");

    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  MonoOS v" MONOOS_VERSION " — ");
    vga_puts(KERNEL_NAME);
    vga_puts(" Kernel\n");

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_puts("  ─────────────────────────────────────\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void init_memory(uint32_t mem_upper) {
    /* mem_upper is in KiB, starting at 1 MiB */
    uint32_t mem_size = (mem_upper + 1024) * 1024;

    /* Place bitmap right after kernel */
    extern uint32_t _kernel_end;
    uint32_t* bitmap = (uint32_t*)((uint32_t)&_kernel_end);

    /* Initialize PMM */
    pmm_init(mem_size, bitmap);

    /* Mark available memory regions
     * Simple approach: mark everything above 1 MiB as available,
     * then deinit kernel region */
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

/* Entry point — called from boot/multiboot.asm */
void kernel_main(uint32_t magic, uint32_t mboot_info_addr) {
    /* Initialize serial first for debug logging */
    serial_init();
    serial_puts("\n[DORI] MonoOS v" MONOOS_VERSION " booting...\n");

    /* Initialize VGA */
    vga_init();
    print_banner();

    /* Verify multiboot2 magic */
    if (magic != MULTIBOOT2_MAGIC_CHECK) {
        kernel_panic("Not booted by a Multiboot2-compliant bootloader!");
        return;
    }
    serial_puts("[DORI] Multiboot2 verified\n");

    /* Initialize GDT */
    vga_puts("  [OK] GDT initialized\n");
    gdt_init();
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
    multiboot_info_t* mboot = (multiboot_info_t*)mboot_info_addr;
    init_memory(mboot->mem_upper);
    vga_puts("  [OK] Physical memory manager\n");

    /* Initialize heap */
    heap_init();
    vga_puts("  [OK] Kernel heap\n");

    /* Initialize keyboard */
    keyboard_init();
    vga_puts("  [OK] PS/2 keyboard\n");

    /* Enable interrupts */
    sti();
    vga_puts("  [OK] Interrupts enabled\n");

    serial_puts("[DORI] All systems initialized\n");

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_puts("  ─────────────────────────────────────\n");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("  System ready. Type 'help' for commands.\n\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    /* Start Dori shell */
    kshell_init();
    kshell_run();
}
