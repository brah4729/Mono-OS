/* kernel/dorifs.c — DoriFS filesystem implementation */

#include "../include/dorifs.h"
#include "../include/ata.h"
#include "../include/heap.h"
#include "../include/string.h"
#include "../include/serial.h"
#include "../include/vga.h"

static dorifs_state_t fs_state;

/* ─── Block I/O helpers ─────────────────────────────────── */

/* Read a logical block (4096 bytes = 8 sectors) */
static int dorifs_read_block(uint32_t block, void* buffer) {
    uint32_t lba = fs_state.partition_lba + (block * DORIFS_SECTORS_PER_BLOCK);
    return ata_read_sectors(lba, DORIFS_SECTORS_PER_BLOCK, buffer);
}

/* Write a logical block */
static int dorifs_write_block(uint32_t block, const void* buffer) {
    uint32_t lba = fs_state.partition_lba + (block * DORIFS_SECTORS_PER_BLOCK);
    return ata_write_sectors(lba, DORIFS_SECTORS_PER_BLOCK, buffer);
}

/* ─── Inode operations ──────────────────────────────────── */

static int dorifs_read_inode(uint32_t ino, dorifs_inode_t* inode) {
    if (ino >= fs_state.sb.total_inodes) return -1;

    uint32_t inodes_per_block = DORIFS_BLOCK_SIZE / sizeof(dorifs_inode_t);
    uint32_t block = fs_state.sb.inode_table_start + (ino / inodes_per_block);
    uint32_t offset = (ino % inodes_per_block) * sizeof(dorifs_inode_t);

    uint8_t buf[DORIFS_BLOCK_SIZE];
    if (dorifs_read_block(block, buf) != 0) return -1;

    memcpy(inode, &buf[offset], sizeof(dorifs_inode_t));
    return 0;
}

static int dorifs_write_inode(uint32_t ino, const dorifs_inode_t* inode) {
    if (ino >= fs_state.sb.total_inodes) return -1;

    uint32_t inodes_per_block = DORIFS_BLOCK_SIZE / sizeof(dorifs_inode_t);
    uint32_t block = fs_state.sb.inode_table_start + (ino / inodes_per_block);
    uint32_t offset = (ino % inodes_per_block) * sizeof(dorifs_inode_t);

    uint8_t buf[DORIFS_BLOCK_SIZE];
    if (dorifs_read_block(block, buf) != 0) return -1;

    memcpy(&buf[offset], inode, sizeof(dorifs_inode_t));
    return dorifs_write_block(block, buf);
}

/* Allocate a free inode, returns inode number or 0 on failure */
static uint32_t dorifs_alloc_inode(void) {
    uint8_t buf[DORIFS_BLOCK_SIZE];
    uint32_t inodes_per_block = DORIFS_BLOCK_SIZE / sizeof(dorifs_inode_t);

    for (uint32_t i = 1; i < fs_state.sb.total_inodes; i++) {
        uint32_t block = fs_state.sb.inode_table_start + (i / inodes_per_block);
        uint32_t offset = (i % inodes_per_block) * sizeof(dorifs_inode_t);

        if (dorifs_read_block(block, buf) != 0) continue;

        dorifs_inode_t* inode = (dorifs_inode_t*)&buf[offset];
        if (inode->type == DORIFS_TYPE_FREE) {
            memset(inode, 0, sizeof(dorifs_inode_t));
            inode->link_count = 1;
            dorifs_write_block(block, buf);
            fs_state.sb.free_inodes--;
            return i;
        }
    }
    return 0;
}

/* Free an inode */
static void dorifs_free_inode(uint32_t ino) {
    dorifs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.type = DORIFS_TYPE_FREE;
    dorifs_write_inode(ino, &inode);
    fs_state.sb.free_inodes++;
}

/* ─── Data block allocation (bitmap) ───────────────────── */

static uint32_t dorifs_alloc_block(void) {
    uint32_t total_data_blocks = fs_state.sb.total_blocks - fs_state.sb.data_start;

    for (uint32_t i = 0; i < total_data_blocks; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx  = i % 8;

        if (!(fs_state.data_bitmap[byte_idx] & (1 << bit_idx))) {
            fs_state.data_bitmap[byte_idx] |= (1 << bit_idx);
            fs_state.sb.free_blocks--;

            /* Write bitmap back to disk */
            uint32_t bm_block = fs_state.sb.data_bitmap_start + (byte_idx / DORIFS_BLOCK_SIZE);
            uint32_t bm_offset = byte_idx % DORIFS_BLOCK_SIZE;
            uint8_t buf[DORIFS_BLOCK_SIZE];
            dorifs_read_block(bm_block, buf);
            buf[bm_offset] = fs_state.data_bitmap[byte_idx];
            dorifs_write_block(bm_block, buf);

            return fs_state.sb.data_start + i;
        }
    }

    serial_puts("[DoriFS] ERROR: No free blocks\n");
    return 0;
}

static void dorifs_free_block(uint32_t block) {
    if (block < fs_state.sb.data_start) return;
    uint32_t index = block - fs_state.sb.data_start;
    uint32_t byte_idx = index / 8;
    uint32_t bit_idx  = index % 8;

    fs_state.data_bitmap[byte_idx] &= ~(1 << bit_idx);
    fs_state.sb.free_blocks++;

    /* Write bitmap back */
    uint32_t bm_block = fs_state.sb.data_bitmap_start + (byte_idx / DORIFS_BLOCK_SIZE);
    uint32_t bm_offset = byte_idx % DORIFS_BLOCK_SIZE;
    uint8_t buf[DORIFS_BLOCK_SIZE];
    dorifs_read_block(bm_block, buf);
    buf[bm_offset] = fs_state.data_bitmap[byte_idx];
    dorifs_write_block(bm_block, buf);
}

/* Get the disk block number for a given file block index in an inode */
static uint32_t dorifs_get_block(dorifs_inode_t* inode, uint32_t file_block) {
    if (file_block < DORIFS_DIRECT_BLOCKS) {
        return inode->direct[file_block];
    }

    uint32_t indirect_idx = file_block - DORIFS_DIRECT_BLOCKS;
    if (indirect_idx >= DORIFS_INDIRECT_PTRS || inode->indirect == 0) {
        return 0;
    }

    /* Read indirect block */
    uint32_t indirect_table[DORIFS_INDIRECT_PTRS];
    dorifs_read_block(inode->indirect, indirect_table);
    return indirect_table[indirect_idx];
}

/* Set a block pointer in an inode (allocating indirect block if needed) */
static int dorifs_set_block(dorifs_inode_t* inode, uint32_t file_block, uint32_t disk_block) {
    if (file_block < DORIFS_DIRECT_BLOCKS) {
        inode->direct[file_block] = disk_block;
        return 0;
    }

    uint32_t indirect_idx = file_block - DORIFS_DIRECT_BLOCKS;
    if (indirect_idx >= DORIFS_INDIRECT_PTRS) return -1;

    /* Allocate indirect block if not present */
    if (inode->indirect == 0) {
        inode->indirect = dorifs_alloc_block();
        if (inode->indirect == 0) return -1;
        /* Zero it out */
        uint8_t zero[DORIFS_BLOCK_SIZE];
        memset(zero, 0, DORIFS_BLOCK_SIZE);
        dorifs_write_block(inode->indirect, zero);
    }

    uint32_t indirect_table[DORIFS_INDIRECT_PTRS];
    dorifs_read_block(inode->indirect, indirect_table);
    indirect_table[indirect_idx] = disk_block;
    dorifs_write_block(inode->indirect, indirect_table);

    return 0;
}

/* ─── VFS operations for DoriFS ─────────────────────────── */

/* Forward declarations */
static vfs_node_t* dorifs_make_vfs_node(uint32_t ino);

static int dorifs_vfs_open(vfs_node_t* node, uint32_t flags) {
    (void)node; (void)flags;
    return 0;
}

static int dorifs_vfs_close(vfs_node_t* node) {
    (void)node;
    return 0;
}

static ssize_t dorifs_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    dorifs_inode_t inode;
    if (dorifs_read_inode(node->inode, &inode) != 0) return -1;

    if (offset >= inode.size) return 0;
    if (offset + size > inode.size) size = inode.size - offset;

    uint32_t bytes_read = 0;
    uint8_t block_buf[DORIFS_BLOCK_SIZE];

    while (bytes_read < size) {
        uint32_t file_block = (offset + bytes_read) / DORIFS_BLOCK_SIZE;
        uint32_t block_offset = (offset + bytes_read) % DORIFS_BLOCK_SIZE;
        uint32_t disk_block = dorifs_get_block(&inode, file_block);

        if (disk_block == 0) break;

        if (dorifs_read_block(disk_block, block_buf) != 0) break;

        uint32_t to_copy = DORIFS_BLOCK_SIZE - block_offset;
        if (to_copy > size - bytes_read) to_copy = size - bytes_read;

        memcpy(&buffer[bytes_read], &block_buf[block_offset], to_copy);
        bytes_read += to_copy;
    }

    return (ssize_t)bytes_read;
}

static ssize_t dorifs_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    dorifs_inode_t inode;
    if (dorifs_read_inode(node->inode, &inode) != 0) return -1;

    uint32_t bytes_written = 0;
    uint8_t block_buf[DORIFS_BLOCK_SIZE];

    while (bytes_written < size) {
        uint32_t file_block = (offset + bytes_written) / DORIFS_BLOCK_SIZE;
        uint32_t block_offset = (offset + bytes_written) % DORIFS_BLOCK_SIZE;
        uint32_t disk_block = dorifs_get_block(&inode, file_block);

        /* Allocate new block if needed */
        if (disk_block == 0) {
            disk_block = dorifs_alloc_block();
            if (disk_block == 0) break;
            dorifs_set_block(&inode, file_block, disk_block);
            inode.block_count++;
            memset(block_buf, 0, DORIFS_BLOCK_SIZE);
        } else {
            dorifs_read_block(disk_block, block_buf);
        }

        uint32_t to_copy = DORIFS_BLOCK_SIZE - block_offset;
        if (to_copy > size - bytes_written) to_copy = size - bytes_written;

        memcpy(&block_buf[block_offset], &buffer[bytes_written], to_copy);
        dorifs_write_block(disk_block, block_buf);
        bytes_written += to_copy;
    }

    /* Update inode size */
    if (offset + bytes_written > inode.size) {
        inode.size = offset + bytes_written;
    }
    node->size = inode.size;
    dorifs_write_inode(node->inode, &inode);

    return (ssize_t)bytes_written;
}

static int dorifs_vfs_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* out) {
    dorifs_inode_t inode;
    if (dorifs_read_inode(node->inode, &inode) != 0) return -1;
    if (inode.type != DORIFS_TYPE_DIR) return -1;

    /* Read directory data (list of dorifs_dirent_t entries) */
    uint32_t entry_idx = 0;
    uint32_t offset = 0;
    uint8_t block_buf[DORIFS_BLOCK_SIZE];

    while (offset < inode.size) {
        uint32_t file_block = offset / DORIFS_BLOCK_SIZE;
        uint32_t block_offset = offset % DORIFS_BLOCK_SIZE;
        uint32_t disk_block = dorifs_get_block(&inode, file_block);

        if (disk_block == 0) return -1;
        if (dorifs_read_block(disk_block, block_buf) != 0) return -1;

        dorifs_dirent_t* de = (dorifs_dirent_t*)&block_buf[block_offset];

        if (de->inode != 0) {
            if (entry_idx == index) {
                out->inode = de->inode;
                out->type = de->type;
                memcpy(out->name, de->name, de->name_len);
                out->name[de->name_len] = '\0';
                return 0;
            }
            entry_idx++;
        }

        offset += sizeof(dorifs_dirent_t);
    }

    return -1; /* No more entries */
}

static vfs_node_t* dorifs_vfs_finddir(vfs_node_t* node, const char* name) {
    dorifs_inode_t inode;
    if (dorifs_read_inode(node->inode, &inode) != 0) return NULL;
    if (inode.type != DORIFS_TYPE_DIR) return NULL;

    uint32_t offset = 0;
    uint8_t block_buf[DORIFS_BLOCK_SIZE];

    while (offset < inode.size) {
        uint32_t file_block = offset / DORIFS_BLOCK_SIZE;
        uint32_t block_offset = offset % DORIFS_BLOCK_SIZE;
        uint32_t disk_block = dorifs_get_block(&inode, file_block);

        if (disk_block == 0) return NULL;
        if (dorifs_read_block(disk_block, block_buf) != 0) return NULL;

        dorifs_dirent_t* de = (dorifs_dirent_t*)&block_buf[block_offset];

        if (de->inode != 0 && strncmp(de->name, name, de->name_len) == 0
            && name[de->name_len] == '\0') {
            return dorifs_make_vfs_node(de->inode);
        }

        offset += sizeof(dorifs_dirent_t);
    }

    return NULL;
}

static int dorifs_vfs_create(vfs_node_t* parent, const char* name, uint32_t type) {
    uint32_t ino = dorifs_alloc_inode();
    if (ino == 0) return -1;

    /* Initialize the new inode */
    dorifs_inode_t new_inode;
    memset(&new_inode, 0, sizeof(new_inode));

    if (type & VFS_DIRECTORY) {
        new_inode.type = DORIFS_TYPE_DIR;
    } else if (type & VFS_SYMLINK) {
        new_inode.type = DORIFS_TYPE_SYMLINK;
    } else {
        new_inode.type = DORIFS_TYPE_FILE;
    }
    new_inode.link_count = 1;
    dorifs_write_inode(ino, &new_inode);

    /* Add directory entry to parent */
    dorifs_inode_t parent_inode;
    if (dorifs_read_inode(parent->inode, &parent_inode) != 0) {
        dorifs_free_inode(ino);
        return -1;
    }

    dorifs_dirent_t de;
    memset(&de, 0, sizeof(de));
    de.inode = ino;
    de.type = (uint8_t)new_inode.type;
    de.name_len = (uint8_t)strlen(name);
    strncpy(de.name, name, DORIFS_MAX_NAME);

    /* Append entry to parent directory data */
    uint32_t write_offset = parent_inode.size;
    uint32_t file_block = write_offset / DORIFS_BLOCK_SIZE;
    uint32_t block_offset = write_offset % DORIFS_BLOCK_SIZE;

    uint8_t block_buf[DORIFS_BLOCK_SIZE];
    uint32_t disk_block = dorifs_get_block(&parent_inode, file_block);

    if (disk_block == 0) {
        disk_block = dorifs_alloc_block();
        if (disk_block == 0) {
            dorifs_free_inode(ino);
            return -1;
        }
        dorifs_set_block(&parent_inode, file_block, disk_block);
        parent_inode.block_count++;
        memset(block_buf, 0, DORIFS_BLOCK_SIZE);
    } else {
        dorifs_read_block(disk_block, block_buf);
    }

    memcpy(&block_buf[block_offset], &de, sizeof(de));
    dorifs_write_block(disk_block, block_buf);

    parent_inode.size += sizeof(dorifs_dirent_t);
    dorifs_write_inode(parent->inode, &parent_inode);
    parent->size = parent_inode.size;

    return 0;
}

static int dorifs_vfs_unlink(vfs_node_t* parent, const char* name) {
    dorifs_inode_t parent_inode;
    if (dorifs_read_inode(parent->inode, &parent_inode) != 0) return -1;

    uint32_t offset = 0;
    uint8_t block_buf[DORIFS_BLOCK_SIZE];

    while (offset < parent_inode.size) {
        uint32_t file_block = offset / DORIFS_BLOCK_SIZE;
        uint32_t block_offset = offset % DORIFS_BLOCK_SIZE;
        uint32_t disk_block = dorifs_get_block(&parent_inode, file_block);

        if (disk_block == 0) return -1;
        if (dorifs_read_block(disk_block, block_buf) != 0) return -1;

        dorifs_dirent_t* de = (dorifs_dirent_t*)&block_buf[block_offset];

        if (de->inode != 0 && strncmp(de->name, name, de->name_len) == 0
            && name[de->name_len] == '\0') {
            /* Free the inode's data blocks */
            dorifs_inode_t target;
            dorifs_read_inode(de->inode, &target);

            for (uint32_t b = 0; b < DORIFS_DIRECT_BLOCKS; b++) {
                if (target.direct[b]) dorifs_free_block(target.direct[b]);
            }
            if (target.indirect) {
                uint32_t indirect_table[DORIFS_INDIRECT_PTRS];
                dorifs_read_block(target.indirect, indirect_table);
                for (uint32_t b = 0; b < DORIFS_INDIRECT_PTRS; b++) {
                    if (indirect_table[b]) dorifs_free_block(indirect_table[b]);
                }
                dorifs_free_block(target.indirect);
            }

            dorifs_free_inode(de->inode);

            /* Mark directory entry as deleted */
            de->inode = 0;
            dorifs_write_block(disk_block, block_buf);

            return 0;
        }

        offset += sizeof(dorifs_dirent_t);
    }

    return -1;
}

static int dorifs_vfs_stat(vfs_node_t* node, vfs_stat_t* st) {
    dorifs_inode_t inode;
    if (dorifs_read_inode(node->inode, &inode) != 0) return -1;

    st->inode = node->inode;
    st->size = inode.size;
    st->block_count = inode.block_count;
    st->create_time = inode.create_time;
    st->modify_time = inode.modify_time;

    switch (inode.type) {
        case DORIFS_TYPE_FILE:    st->type = VFS_FILE;      break;
        case DORIFS_TYPE_DIR:     st->type = VFS_DIRECTORY;  break;
        case DORIFS_TYPE_SYMLINK: st->type = VFS_SYMLINK;    break;
        default:                 st->type = 0;              break;
    }

    return 0;
}

static int dorifs_vfs_truncate(vfs_node_t* node, uint32_t size) {
    dorifs_inode_t inode;
    if (dorifs_read_inode(node->inode, &inode) != 0) return -1;

    if (size < inode.size) {
        /* Free blocks beyond new size */
        uint32_t first_free_block = (size + DORIFS_BLOCK_SIZE - 1) / DORIFS_BLOCK_SIZE;
        uint32_t last_block = (inode.size + DORIFS_BLOCK_SIZE - 1) / DORIFS_BLOCK_SIZE;

        for (uint32_t b = first_free_block; b < last_block; b++) {
            uint32_t disk_block = dorifs_get_block(&inode, b);
            if (disk_block) {
                dorifs_free_block(disk_block);
                dorifs_set_block(&inode, b, 0);
                inode.block_count--;
            }
        }
    }

    inode.size = size;
    node->size = size;
    dorifs_write_inode(node->inode, &inode);

    return 0;
}

static int dorifs_vfs_symlink(vfs_node_t* parent, const char* name, const char* target) {
    uint32_t ino = dorifs_alloc_inode();
    if (ino == 0) return -1;

    dorifs_inode_t new_inode;
    memset(&new_inode, 0, sizeof(new_inode));
    new_inode.type = DORIFS_TYPE_SYMLINK;
    new_inode.link_count = 1;
    new_inode.size = strlen(target);
    strncpy((char*)new_inode.symlink_target, target, VFS_MAX_PATH - 1);
    dorifs_write_inode(ino, &new_inode);

    /* Add directory entry */
    dorifs_inode_t parent_inode;
    if (dorifs_read_inode(parent->inode, &parent_inode) != 0) {
        dorifs_free_inode(ino);
        return -1;
    }

    dorifs_dirent_t de;
    memset(&de, 0, sizeof(de));
    de.inode = ino;
    de.type = DORIFS_TYPE_SYMLINK;
    de.name_len = (uint8_t)strlen(name);
    strncpy(de.name, name, DORIFS_MAX_NAME);

    uint32_t write_offset = parent_inode.size;
    uint32_t file_block = write_offset / DORIFS_BLOCK_SIZE;
    uint32_t block_offset = write_offset % DORIFS_BLOCK_SIZE;

    uint8_t block_buf[DORIFS_BLOCK_SIZE];
    uint32_t disk_block = dorifs_get_block(&parent_inode, file_block);

    if (disk_block == 0) {
        disk_block = dorifs_alloc_block();
        if (disk_block == 0) {
            dorifs_free_inode(ino);
            return -1;
        }
        dorifs_set_block(&parent_inode, file_block, disk_block);
        parent_inode.block_count++;
        memset(block_buf, 0, DORIFS_BLOCK_SIZE);
    } else {
        dorifs_read_block(disk_block, block_buf);
    }

    memcpy(&block_buf[block_offset], &de, sizeof(de));
    dorifs_write_block(disk_block, block_buf);

    parent_inode.size += sizeof(dorifs_dirent_t);
    dorifs_write_inode(parent->inode, &parent_inode);
    parent->size = parent_inode.size;

    return 0;
}

static ssize_t dorifs_vfs_readlink(vfs_node_t* node, char* buf, size_t bufsiz) {
    dorifs_inode_t inode;
    if (dorifs_read_inode(node->inode, &inode) != 0) return -1;
    if (inode.type != DORIFS_TYPE_SYMLINK) return -1;

    size_t len = strlen((char*)inode.symlink_target);
    if (len > bufsiz) len = bufsiz;
    memcpy(buf, inode.symlink_target, len);
    return (ssize_t)len;
}

/* Operations table */
static vfs_ops_t dorifs_ops = {
    .open     = dorifs_vfs_open,
    .close    = dorifs_vfs_close,
    .read     = dorifs_vfs_read,
    .write    = dorifs_vfs_write,
    .readdir  = dorifs_vfs_readdir,
    .finddir  = dorifs_vfs_finddir,
    .create   = dorifs_vfs_create,
    .unlink   = dorifs_vfs_unlink,
    .stat     = dorifs_vfs_stat,
    .truncate = dorifs_vfs_truncate,
    .symlink  = dorifs_vfs_symlink,
    .readlink = dorifs_vfs_readlink,
};

/* ─── VFS node creation ────────────────────────────────── */

static vfs_node_t* dorifs_make_vfs_node(uint32_t ino) {
    dorifs_inode_t inode;
    if (dorifs_read_inode(ino, &inode) != 0) return NULL;

    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;

    memset(node, 0, sizeof(vfs_node_t));
    node->inode = ino;
    node->size = inode.size;
    node->ops = &dorifs_ops;

    switch (inode.type) {
        case DORIFS_TYPE_FILE:    node->type = VFS_FILE;      break;
        case DORIFS_TYPE_DIR:     node->type = VFS_DIRECTORY;  break;
        case DORIFS_TYPE_SYMLINK: node->type = VFS_SYMLINK;    break;
    }

    return node;
}

/* ─── Public API ────────────────────────────────────────── */

int dorifs_format(uint32_t partition_lba, uint32_t total_sectors) {
    serial_puts("[DoriFS] Formatting...\n");

    uint32_t total_blocks = total_sectors / DORIFS_SECTORS_PER_BLOCK;

    /* Calculate layout */
    uint32_t inode_table_blocks = (DORIFS_MAX_INODES * sizeof(dorifs_inode_t)
                                   + DORIFS_BLOCK_SIZE - 1) / DORIFS_BLOCK_SIZE;
    uint32_t data_blocks = total_blocks - 1 - inode_table_blocks;
    uint32_t bitmap_blocks = (data_blocks / 8 + DORIFS_BLOCK_SIZE - 1) / DORIFS_BLOCK_SIZE;
    data_blocks -= bitmap_blocks;

    /* Write superblock */
    dorifs_superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic = DORIFS_MAGIC;
    sb.version = DORIFS_VERSION;
    sb.total_blocks = total_blocks;
    sb.total_inodes = DORIFS_MAX_INODES;
    sb.free_blocks = data_blocks;
    sb.free_inodes = DORIFS_MAX_INODES - 1; /* inode 0 reserved */
    sb.inode_table_start = 1;
    sb.inode_table_blocks = inode_table_blocks;
    sb.data_bitmap_start = 1 + inode_table_blocks;
    sb.data_bitmap_blocks = bitmap_blocks;
    sb.data_start = 1 + inode_table_blocks + bitmap_blocks;
    sb.block_size = DORIFS_BLOCK_SIZE;

    uint8_t buf[DORIFS_BLOCK_SIZE];
    memset(buf, 0, DORIFS_BLOCK_SIZE);
    memcpy(buf, &sb, sizeof(sb));

    uint32_t lba = partition_lba;
    ata_write_sectors(lba, DORIFS_SECTORS_PER_BLOCK, buf);

    /* Zero out inode table */
    memset(buf, 0, DORIFS_BLOCK_SIZE);
    for (uint32_t i = 0; i < inode_table_blocks; i++) {
        lba = partition_lba + ((1 + i) * DORIFS_SECTORS_PER_BLOCK);
        ata_write_sectors(lba, DORIFS_SECTORS_PER_BLOCK, buf);
    }

    /* Zero out data bitmap */
    for (uint32_t i = 0; i < bitmap_blocks; i++) {
        lba = partition_lba + ((sb.data_bitmap_start + i) * DORIFS_SECTORS_PER_BLOCK);
        ata_write_sectors(lba, DORIFS_SECTORS_PER_BLOCK, buf);
    }

    /* Create root directory (inode 1) */
    dorifs_inode_t root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.type = DORIFS_TYPE_DIR;
    root_inode.link_count = 1;

    /* Write root inode at position 1 in inode table */
    memset(buf, 0, DORIFS_BLOCK_SIZE);
    lba = partition_lba + (1 * DORIFS_SECTORS_PER_BLOCK); /* first inode table block */
    ata_read_sectors(lba, DORIFS_SECTORS_PER_BLOCK, buf);
    memcpy(&buf[sizeof(dorifs_inode_t)], &root_inode, sizeof(root_inode)); /* index 1 */
    ata_write_sectors(lba, DORIFS_SECTORS_PER_BLOCK, buf);

    sb.free_inodes--;

    /* Rewrite superblock with updated free count */
    memset(buf, 0, DORIFS_BLOCK_SIZE);
    memcpy(buf, &sb, sizeof(sb));
    ata_write_sectors(partition_lba, DORIFS_SECTORS_PER_BLOCK, buf);

    ata_flush();

    serial_puts("[DoriFS] Format complete: ");
    char num[16];
    utoa(data_blocks, num, 10);
    serial_puts(num);
    serial_puts(" data blocks, ");
    utoa(DORIFS_MAX_INODES, num, 10);
    serial_puts(num);
    serial_puts(" inodes\n");

    return 0;
}

vfs_node_t* dorifs_mount(uint32_t partition_lba) {
    serial_puts("[DoriFS] Mounting from LBA ");
    char buf_str[16];
    utoa(partition_lba, buf_str, 10);
    serial_puts(buf_str);
    serial_puts("...\n");

    fs_state.partition_lba = partition_lba;

    /* Read superblock */
    uint8_t buf[DORIFS_BLOCK_SIZE];
    if (ata_read_sectors(partition_lba, DORIFS_SECTORS_PER_BLOCK, buf) != 0) {
        serial_puts("[DoriFS] Failed to read superblock\n");
        return NULL;
    }

    memcpy(&fs_state.sb, buf, sizeof(dorifs_superblock_t));

    if (fs_state.sb.magic != DORIFS_MAGIC) {
        serial_puts("[DoriFS] Invalid magic number\n");
        return NULL;
    }

    serial_puts("[DoriFS] DoriFS v");
    utoa(fs_state.sb.version, buf_str, 10);
    serial_puts(buf_str);
    serial_puts(" detected\n");

    /* Load data bitmap into memory */
    fs_state.data_bitmap_size = fs_state.sb.data_bitmap_blocks * DORIFS_BLOCK_SIZE;
    fs_state.data_bitmap = (uint8_t*)kmalloc(fs_state.data_bitmap_size);
    if (!fs_state.data_bitmap) {
        serial_puts("[DoriFS] Failed to allocate bitmap memory\n");
        return NULL;
    }

    for (uint32_t i = 0; i < fs_state.sb.data_bitmap_blocks; i++) {
        dorifs_read_block(fs_state.sb.data_bitmap_start + i,
                          &fs_state.data_bitmap[i * DORIFS_BLOCK_SIZE]);
    }

    /* Create root VFS node */
    vfs_node_t* root = dorifs_make_vfs_node(DORIFS_ROOT_INODE);
    if (!root) {
        serial_puts("[DoriFS] Failed to create root node\n");
        kfree(fs_state.data_bitmap);
        return NULL;
    }

    strcpy(root->name, "/");

    serial_puts("[DoriFS] Mounted successfully (");
    utoa(fs_state.sb.free_blocks, buf_str, 10);
    serial_puts(buf_str);
    serial_puts(" free blocks)\n");

    return root;
}

void dorifs_unmount(void) {
    /* Write superblock back to disk */
    uint8_t buf[DORIFS_BLOCK_SIZE];
    memset(buf, 0, DORIFS_BLOCK_SIZE);
    memcpy(buf, &fs_state.sb, sizeof(dorifs_superblock_t));
    dorifs_write_block(0, buf);
    ata_flush();

    if (fs_state.data_bitmap) {
        kfree(fs_state.data_bitmap);
        fs_state.data_bitmap = NULL;
    }

    serial_puts("[DoriFS] Unmounted\n");
}

void dorifs_print_info(void) {
    char buf[16];
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("DoriFS Information:\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    vga_puts("  Version:      ");
    vga_put_dec(fs_state.sb.version);
    vga_puts("\n");

    vga_puts("  Block size:   ");
    vga_put_dec(fs_state.sb.block_size);
    vga_puts(" bytes\n");

    vga_puts("  Total blocks: ");
    vga_put_dec(fs_state.sb.total_blocks);
    vga_puts("\n");

    vga_puts("  Free blocks:  ");
    vga_put_dec(fs_state.sb.free_blocks);
    vga_puts(" (");
    vga_put_dec((fs_state.sb.free_blocks * DORIFS_BLOCK_SIZE) / 1024);
    vga_puts(" KiB)\n");

    vga_puts("  Total inodes: ");
    vga_put_dec(fs_state.sb.total_inodes);
    vga_puts("\n");

    vga_puts("  Free inodes:  ");
    vga_put_dec(fs_state.sb.free_inodes);
    vga_puts("\n");

    (void)buf;
}
