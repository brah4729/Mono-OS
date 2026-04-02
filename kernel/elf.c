/* kernel/elf.c — ELF32 binary loader */

#include "../include/elf.h"
#include "../include/pmm.h"
#include "../include/vmm.h"
#include "../include/heap.h"
#include "../include/string.h"
#include "../include/serial.h"

int elf_validate(const void* data, uint32_t size) {
    if (size < sizeof(elf32_ehdr_t)) {
        serial_puts("[ELF] File too small for ELF header\n");
        return -1;
    }

    const elf32_ehdr_t* ehdr = (const elf32_ehdr_t*)data;

    /* Check magic */
    if (*(uint32_t*)ehdr->e_ident != ELF_MAGIC) {
        serial_puts("[ELF] Invalid magic number\n");
        return -1;
    }

    /* Check class (32-bit) */
    if (ehdr->e_ident[4] != ELFCLASS32) {
        serial_puts("[ELF] Not a 32-bit ELF\n");
        return -1;
    }

    /* Check endianness (little-endian) */
    if (ehdr->e_ident[5] != ELFDATA2LSB) {
        serial_puts("[ELF] Not little-endian\n");
        return -1;
    }

    /* Check type (executable) */
    if (ehdr->e_type != ET_EXEC) {
        serial_puts("[ELF] Not an executable\n");
        return -1;
    }

    /* Check machine (x86) */
    if (ehdr->e_machine != EM_386) {
        serial_puts("[ELF] Not compiled for i386\n");
        return -1;
    }

    /* Check program headers exist */
    if (ehdr->e_phnum == 0 || ehdr->e_phoff == 0) {
        serial_puts("[ELF] No program headers\n");
        return -1;
    }

    /* Validate program headers are within file */
    uint32_t ph_end = ehdr->e_phoff + (ehdr->e_phnum * ehdr->e_phentsize);
    if (ph_end > size) {
        serial_puts("[ELF] Program headers extend past end of file\n");
        return -1;
    }

    serial_puts("[ELF] Valid ELF32 executable\n");
    return 0;
}

int elf_load(const void* data, uint32_t size, uint32_t* entry_point) {
    if (elf_validate(data, size) != 0) {
        return -1;
    }

    const elf32_ehdr_t* ehdr = (const elf32_ehdr_t*)data;
    const uint8_t* file_data = (const uint8_t*)data;

    serial_puts("[ELF] Loading ");
    char buf[16];
    utoa(ehdr->e_phnum, buf, 10);
    serial_puts(buf);
    serial_puts(" program segments\n");

    /* Process each program header */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const elf32_phdr_t* phdr = (const elf32_phdr_t*)
            (file_data + ehdr->e_phoff + (i * ehdr->e_phentsize));

        /* Only load PT_LOAD segments */
        if (phdr->p_type != PT_LOAD) continue;

        if (phdr->p_memsz == 0) continue;

        serial_puts("[ELF]   Segment: vaddr=0x");
        utoa(phdr->p_vaddr, buf, 16);
        serial_puts(buf);
        serial_puts(" filesz=");
        utoa(phdr->p_filesz, buf, 10);
        serial_puts(buf);
        serial_puts(" memsz=");
        utoa(phdr->p_memsz, buf, 10);
        serial_puts(buf);
        serial_puts("\n");

        /* Validate file bounds */
        if (phdr->p_offset + phdr->p_filesz > size) {
            serial_puts("[ELF] Segment data extends past end of file\n");
            return -1;
        }

        /* Allocate physical pages and map them */
        uint32_t vaddr_start = phdr->p_vaddr & 0xFFFFF000;
        uint32_t vaddr_end = (phdr->p_vaddr + phdr->p_memsz + PAGE_SIZE - 1) & 0xFFFFF000;

        uint32_t flags = PAGE_PRESENT | PAGE_WRITABLE;
        if (phdr->p_flags & PF_R) flags |= PAGE_USER;

        for (uint32_t vaddr = vaddr_start; vaddr < vaddr_end; vaddr += PAGE_SIZE) {
            uint32_t paddr = pmm_alloc_frame();
            if (paddr == 0) {
                serial_puts("[ELF] Out of physical memory\n");
                return -1;
            }
            vmm_map_page(paddr, vaddr, flags);
        }

        /* Copy file data to mapped memory */
        if (phdr->p_filesz > 0) {
            memcpy((void*)phdr->p_vaddr,
                   file_data + phdr->p_offset,
                   phdr->p_filesz);
        }

        /* Zero out BSS (memsz > filesz) */
        if (phdr->p_memsz > phdr->p_filesz) {
            memset((void*)(phdr->p_vaddr + phdr->p_filesz),
                   0,
                   phdr->p_memsz - phdr->p_filesz);
        }
    }

    *entry_point = ehdr->e_entry;

    serial_puts("[ELF] Entry point: 0x");
    utoa(ehdr->e_entry, buf, 16);
    serial_puts(buf);
    serial_puts("\n");

    return 0;
}
