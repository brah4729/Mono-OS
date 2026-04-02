#ifndef ELF_H
#define ELF_H

#include "types.h"

/* ELF32 magic */
#define ELF_MAGIC      0x464C457F   /* "\x7FELF" */

/* ELF class */
#define ELFCLASS32     1

/* ELF data encoding */
#define ELFDATA2LSB    1            /* Little-endian */

/* ELF type */
#define ET_EXEC        2            /* Executable */

/* ELF machine */
#define EM_386         3            /* x86 */

/* Program header types */
#define PT_NULL        0
#define PT_LOAD        1
#define PT_DYNAMIC     2
#define PT_INTERP      3

/* Program header flags */
#define PF_X           0x1          /* Execute */
#define PF_W           0x2          /* Write */
#define PF_R           0x4          /* Read */

/* ELF32 file header */
typedef struct {
    uint8_t  e_ident[16];           /* Magic number and other info */
    uint16_t e_type;                /* Object file type */
    uint16_t e_machine;             /* Architecture */
    uint32_t e_version;             /* Object file version */
    uint32_t e_entry;               /* Entry point virtual address */
    uint32_t e_phoff;               /* Program header table file offset */
    uint32_t e_shoff;               /* Section header table file offset */
    uint32_t e_flags;               /* Processor-specific flags */
    uint16_t e_ehsize;              /* ELF header size in bytes */
    uint16_t e_phentsize;           /* Program header table entry size */
    uint16_t e_phnum;               /* Program header table entry count */
    uint16_t e_shentsize;           /* Section header table entry size */
    uint16_t e_shnum;               /* Section header table entry count */
    uint16_t e_shstrndx;            /* Section header string table index */
} __attribute__((packed)) elf32_ehdr_t;

/* ELF32 program header */
typedef struct {
    uint32_t p_type;                /* Segment type */
    uint32_t p_offset;              /* Segment file offset */
    uint32_t p_vaddr;               /* Segment virtual address */
    uint32_t p_paddr;               /* Segment physical address */
    uint32_t p_filesz;              /* Segment size in file */
    uint32_t p_memsz;               /* Segment size in memory */
    uint32_t p_flags;               /* Segment flags */
    uint32_t p_align;               /* Segment alignment */
} __attribute__((packed)) elf32_phdr_t;

/* ELF loader API */
int     elf_validate(const void* data, uint32_t size);
int     elf_load(const void* data, uint32_t size, uint32_t* entry_point);

#endif
