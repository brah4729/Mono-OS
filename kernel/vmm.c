/* kernel/vmm.c — Virtual Memory Manager (paging) */

#include "../include/vmm.h"
#include "../include/pmm.h"
#include "../include/string.h"
#include "../include/isr.h"
#include "../include/vga.h"
#include "../include/serial.h"
#include "../include/io.h"

static page_directory_t* current_directory = 0;

/* Page fault handler */
static void page_fault_handler(registers_t* regs) {
    uint32_t faulting_address;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(faulting_address));

    int present  = !(regs->err_code & 0x1);
    int rw       = regs->err_code & 0x2;
    int us       = regs->err_code & 0x4;

    vga_set_color(VGA_WHITE, VGA_RED);
    vga_puts("\n*** PAGE FAULT ***\n");
    if (present) vga_puts("  Page not present\n");
    if (rw)      vga_puts("  Write operation\n");
    if (us)      vga_puts("  User mode\n");
    vga_puts("  Address: 0x");
    vga_put_hex(faulting_address);
    vga_puts("\n");

    serial_puts("[PAGE FAULT] addr=0x");
    serial_put_hex(faulting_address);
    serial_puts("\n");

    cli();
    for (;;) hlt();
}

void vmm_map_page(uint32_t phys, uint32_t virt, uint32_t flags) {
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x3FF;

    /* Get or create page table */
    if (!(current_directory->entries[pd_index] & PAGE_PRESENT)) {
        uint32_t pt_phys = pmm_alloc_frame();
        current_directory->entries[pd_index] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE | flags;
        page_table_t* pt = (page_table_t*)(pt_phys);
        memset(pt, 0, sizeof(page_table_t));
    }

    page_table_t* table = (page_table_t*)(current_directory->entries[pd_index] & 0xFFFFF000);
    table->entries[pt_index] = (phys & 0xFFFFF000) | PAGE_PRESENT | flags;
}

void vmm_unmap_page(uint32_t virt) {
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x3FF;

    if (!(current_directory->entries[pd_index] & PAGE_PRESENT)) {
        return;
    }

    page_table_t* table = (page_table_t*)(current_directory->entries[pd_index] & 0xFFFFF000);
    table->entries[pt_index] = 0;

    /* Invalidate TLB entry */
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

uint32_t vmm_get_physical(uint32_t virt) {
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x3FF;

    if (!(current_directory->entries[pd_index] & PAGE_PRESENT)) {
        return 0;
    }

    page_table_t* table = (page_table_t*)(current_directory->entries[pd_index] & 0xFFFFF000);
    return table->entries[pt_index] & 0xFFFFF000;
}

void vmm_switch_directory(page_directory_t* dir) {
    current_directory = dir;
    __asm__ volatile ("mov %0, %%cr3" : : "r"((uint32_t)dir));
}

void vmm_init(void) {
    uint32_t pd_phys = pmm_alloc_frame();
    current_directory = (page_directory_t*)pd_phys;
    memset(current_directory, 0, sizeof(page_directory_t));
    for (uint32_t i = 0; i < 0x1000000; i += PAGE_SIZE) {
        vmm_map_page(i, i, PAGE_WRITABLE);
    }
    for (uint32_t i= 0; i < 0x400000; i += PAGE_SIZE){
        vmm_map_page(0xfd000000 + i, 0xfd000000 + i, PAGE_WRITABLE);
    }
    isr_register_handler(14, page_fault_handler);
    vmm_switch_directory(current_directory);
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));
    serial_puts("[VMM] Paging enabled, first 16 MiB identity-mapped + FB mapped\n");
}