/* drivers/ata.c — PIO-mode ATA/IDE disk driver (LBA28) */

#include "../include/ata.h"
#include "../include/io.h"
#include "../include/string.h"
#include "../include/serial.h"
#include "../include/vga.h"

static ata_drive_t primary_drive;

/* Wait for BSY to clear */
static void ata_wait_bsy(void) {
    while (inb(ATA_PRIMARY_STATUS) & ATA_STATUS_BSY);
}

/* Wait for DRQ to set */
static void ata_wait_drq(void) {
    while (!(inb(ATA_PRIMARY_STATUS) & ATA_STATUS_DRQ));
}

/* 400ns delay by reading status port 4 times */
static void ata_delay(void) {
    inb(ATA_PRIMARY_STATUS);
    inb(ATA_PRIMARY_STATUS);
    inb(ATA_PRIMARY_STATUS);
    inb(ATA_PRIMARY_STATUS);
}

/* Check for errors after a command */
static bool ata_check_error(void) {
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status & ATA_STATUS_ERR) return true;
    if (status & ATA_STATUS_DF)  return true;
    return false;
}

/* Swap bytes in ATA identify string (ATA stores strings big-endian word-order) */
static void ata_fix_string(char* str, int len) {
    for (int i = 0; i < len; i += 2) {
        char tmp = str[i];
        str[i] = str[i + 1];
        str[i + 1] = tmp;
    }
    /* Trim trailing spaces */
    for (int i = len - 1; i >= 0 && str[i] == ' '; i--) {
        str[i] = '\0';
    }
}

void ata_init(void) {
    memset(&primary_drive, 0, sizeof(ata_drive_t));

    serial_puts("[ATA] Detecting primary master drive...\n");

    /* Select master drive */
    outb(ATA_PRIMARY_DRIVE_HEAD, ATA_DRIVE_MASTER);
    ata_delay();

    /* Zero out sector count and LBA registers */
    outb(ATA_PRIMARY_SECTOR_COUNT, 0);
    outb(ATA_PRIMARY_LBA_LO, 0);
    outb(ATA_PRIMARY_LBA_MID, 0);
    outb(ATA_PRIMARY_LBA_HI, 0);

    /* Send IDENTIFY command */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay();

    /* Check if drive exists */
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0) {
        serial_puts("[ATA] No drive detected\n");
        primary_drive.present = false;
        return;
    }

    /* Wait for BSY to clear */
    ata_wait_bsy();

    /* Check LBA_MID and LBA_HI — if non-zero, it's not ATA */
    if (inb(ATA_PRIMARY_LBA_MID) != 0 || inb(ATA_PRIMARY_LBA_HI) != 0) {
        serial_puts("[ATA] Not an ATA drive (ATAPI or SATA?)\n");
        primary_drive.present = false;
        return;
    }

    /* Wait for DRQ or ERR */
    while (1) {
        status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_STATUS_ERR) {
            serial_puts("[ATA] IDENTIFY failed with error\n");
            primary_drive.present = false;
            return;
        }
        if (status & ATA_STATUS_DRQ) break;
    }

    /* Read 256 words of identification data */
    uint16_t ident[256];
    for (int i = 0; i < 256; i++) {
        ident[i] = inw(ATA_PRIMARY_DATA);
    }

    primary_drive.present = true;
    primary_drive.is_master = true;

    /* Extract total sectors (LBA28) */
    primary_drive.size_sectors = (uint32_t)ident[ATA_IDENT_MAX_LBA]
                               | ((uint32_t)ident[ATA_IDENT_MAX_LBA + 1] << 16);
    primary_drive.size_mb = primary_drive.size_sectors / 2048;

    /* Extract model string */
    memcpy(primary_drive.model, &ident[ATA_IDENT_MODEL], 40);
    primary_drive.model[40] = '\0';
    ata_fix_string(primary_drive.model, 40);

    serial_puts("[ATA] Drive detected: ");
    serial_puts(primary_drive.model);
    serial_puts(" (");
    char buf[16];
    utoa(primary_drive.size_mb, buf, 10);
    serial_puts(buf);
    serial_puts(" MB)\n");
}

bool ata_is_present(void) {
    return primary_drive.present;
}

ata_drive_t ata_get_drive_info(void) {
    return primary_drive;
}

int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer) {
    if (!primary_drive.present) return -1;
    if (count == 0) return -1;

    ata_wait_bsy();

    /* Select drive + LBA mode + top 4 bits of LBA */
    outb(ATA_PRIMARY_DRIVE_HEAD, ATA_DRIVE_MASTER | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SECTOR_COUNT, count);
    outb(ATA_PRIMARY_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));

    /* Send READ command */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_SECTORS);

    uint16_t* buf = (uint16_t*)buffer;

    for (uint8_t s = 0; s < count; s++) {
        ata_wait_bsy();
        ata_wait_drq();

        if (ata_check_error()) {
            serial_puts("[ATA] Read error at sector ");
            char b[16];
            utoa(lba + s, b, 10);
            serial_puts(b);
            serial_puts("\n");
            return -1;
        }

        /* Read 256 words (512 bytes) */
        for (int i = 0; i < 256; i++) {
            buf[i] = inw(ATA_PRIMARY_DATA);
        }
        buf += 256;
    }

    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void* buffer) {
    if (!primary_drive.present) return -1;
    if (count == 0) return -1;

    ata_wait_bsy();

    /* Select drive + LBA mode + top 4 bits of LBA */
    outb(ATA_PRIMARY_DRIVE_HEAD, ATA_DRIVE_MASTER | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SECTOR_COUNT, count);
    outb(ATA_PRIMARY_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));

    /* Send WRITE command */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_SECTORS);

    const uint16_t* buf = (const uint16_t*)buffer;

    for (uint8_t s = 0; s < count; s++) {
        ata_wait_bsy();
        ata_wait_drq();

        /* Write 256 words (512 bytes) */
        for (int i = 0; i < 256; i++) {
            outw(ATA_PRIMARY_DATA, buf[i]);
        }
        buf += 256;
    }

    /* Flush cache */
    ata_flush();

    return 0;
}

void ata_flush(void) {
    outb(ATA_PRIMARY_DRIVE_HEAD, ATA_DRIVE_MASTER);
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_FLUSH_CACHE);
    ata_wait_bsy();
}
