#ifndef DORIFS_H
#define DORIFS_H

#include "types.h"
#include "vfs.h"

/*
 * DoriFS — Simple on-disk filesystem for MonoOS
 *
 * Disk layout:
 *   Sector 0:        Superblock
 *   Sector 1..N:     Inode table
 *   Sector N+1..M:   Data bitmap
 *   Sector M+1..END: Data blocks
 *
 * Each inode has direct block pointers (12) and one indirect block.
 * Max file size with 512-byte blocks: 12*512 + 128*512 = 71,680 bytes
 * For larger files, we use 4096-byte logical blocks (8 sectors each).
 */

#define DORIFS_MAGIC         0x444F5249  /* "DORI" */
#define DORIFS_VERSION       1
#define DORIFS_BLOCK_SIZE    4096        /* Logical block = 8 sectors */
#define DORIFS_SECTORS_PER_BLOCK (DORIFS_BLOCK_SIZE / 512)
#define DORIFS_MAX_INODES    1024
#define DORIFS_MAX_NAME      255
#define DORIFS_DIRECT_BLOCKS 12
#define DORIFS_INDIRECT_PTRS (DORIFS_BLOCK_SIZE / sizeof(uint32_t))

/* Inode types */
#define DORIFS_TYPE_FREE     0
#define DORIFS_TYPE_FILE     1
#define DORIFS_TYPE_DIR      2
#define DORIFS_TYPE_SYMLINK  3

#define DORIFS_ROOT_INODE    1          /* Inode 0 is unused/reserved */

/* Superblock — stored in first block */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t total_blocks;
    uint32_t total_inodes;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t inode_table_start;       /* Block number */
    uint32_t inode_table_blocks;
    uint32_t data_bitmap_start;       /* Block number */
    uint32_t data_bitmap_blocks;
    uint32_t data_start;              /* Block number */
    uint32_t block_size;
    uint8_t  padding[DORIFS_BLOCK_SIZE - 48];
} __attribute__((packed)) dorifs_superblock_t;

/* On-disk inode */
typedef struct {
    uint32_t type;                     /* DORIFS_TYPE_* */
    uint32_t size;                     /* File size in bytes */
    uint32_t block_count;              /* Number of allocated blocks */
    uint32_t create_time;
    uint32_t modify_time;
    uint32_t direct[DORIFS_DIRECT_BLOCKS]; /* Direct block pointers */
    uint32_t indirect;                 /* Single indirect block pointer */
    uint32_t link_count;
    uint8_t  symlink_target[VFS_MAX_PATH]; /* For symlinks */
    uint8_t  padding[12];
} __attribute__((packed)) dorifs_inode_t;

/* Directory entry on disk */
typedef struct {
    uint32_t inode;
    uint8_t  type;
    uint8_t  name_len;
    char     name[DORIFS_MAX_NAME + 1];
} __attribute__((packed)) dorifs_dirent_t;

/* In-memory filesystem state */
typedef struct {
    dorifs_superblock_t sb;
    uint32_t            partition_lba;  /* Starting LBA on disk */
    uint8_t*            data_bitmap;    /* Cached in memory */
    uint32_t            data_bitmap_size;
} dorifs_state_t;

/* Public API */
int          dorifs_format(uint32_t partition_lba, uint32_t total_sectors);
vfs_node_t*  dorifs_mount(uint32_t partition_lba);
void         dorifs_unmount(void);

/* Debug */
void         dorifs_print_info(void);

#endif
