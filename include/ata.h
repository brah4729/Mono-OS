#ifndef ATA_H
#define ATA_H

#include "types.h"

/* ATA I/O ports (primary bus) */
#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_ERROR        0x1F1
#define ATA_PRIMARY_SECTOR_COUNT 0x1F2
#define ATA_PRIMARY_LBA_LO       0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HI       0x1F5
#define ATA_PRIMARY_DRIVE_HEAD   0x1F6
#define ATA_PRIMARY_STATUS       0x1F7
#define ATA_PRIMARY_COMMAND      0x1F7

/* ATA status register bits */
#define ATA_STATUS_ERR   0x01
#define ATA_STATUS_DRQ   0x08
#define ATA_STATUS_SRV   0x10
#define ATA_STATUS_DF    0x20
#define ATA_STATUS_RDY   0x40
#define ATA_STATUS_BSY   0x80

/* ATA commands */
#define ATA_CMD_READ_SECTORS   0x20
#define ATA_CMD_WRITE_SECTORS  0x30
#define ATA_CMD_IDENTIFY       0xEC
#define ATA_CMD_FLUSH_CACHE    0xE7

/* Sector size */
#define ATA_SECTOR_SIZE 512

/* Drive selection */
#define ATA_DRIVE_MASTER 0xE0
#define ATA_DRIVE_SLAVE  0xF0

/* Identify response offsets (in words) */
#define ATA_IDENT_MODEL        27
#define ATA_IDENT_MAX_LBA      60
#define ATA_IDENT_CAPABILITIES 49

typedef struct {
    bool     present;
    bool     is_master;
    uint32_t size_sectors;       /* Total addressable sectors */
    uint32_t size_mb;            /* Size in megabytes */
    char     model[41];          /* Model string (null-terminated) */
} ata_drive_t;

void        ata_init(void);
bool        ata_is_present(void);
ata_drive_t ata_get_drive_info(void);
int         ata_read_sectors(uint32_t lba, uint8_t count, void* buffer);
int         ata_write_sectors(uint32_t lba, uint8_t count, const void* buffer);
void        ata_flush(void);

#endif
