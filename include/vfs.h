#ifndef VFS_H
#define VFS_H

#include "types.h"

#define VFS_MAX_PATH      256
#define VFS_MAX_NAME      255
#define VFS_MAX_OPEN_FILES 64
#define VFS_MAX_MOUNTS     8

/* File types */
#define VFS_FILE        0x01
#define VFS_DIRECTORY   0x02
#define VFS_SYMLINK     0x04
#define VFS_MOUNTPOINT  0x08

/* Open flags */
#define VFS_O_RDONLY    0x0000
#define VFS_O_WRONLY    0x0001
#define VFS_O_RDWR      0x0002
#define VFS_O_CREAT     0x0040
#define VFS_O_TRUNC     0x0200
#define VFS_O_APPEND    0x0400

/* Seek whence */
#define VFS_SEEK_SET    0
#define VFS_SEEK_CUR    1
#define VFS_SEEK_END    2

/* Forward declarations */
struct vfs_node;
struct vfs_dirent;

/* Stat structure */
typedef struct {
    uint32_t inode;
    uint32_t type;          /* VFS_FILE, VFS_DIRECTORY, etc. */
    uint32_t size;
    uint32_t block_count;
    uint32_t create_time;
    uint32_t modify_time;
} vfs_stat_t;

/* Directory entry */
typedef struct vfs_dirent {
    char     name[VFS_MAX_NAME + 1];
    uint32_t inode;
    uint8_t  type;
} vfs_dirent_t;

/* File operation function pointers */
typedef struct {
    int      (*open)(struct vfs_node* node, uint32_t flags);
    int      (*close)(struct vfs_node* node);
    ssize_t  (*read)(struct vfs_node* node, uint32_t offset, uint32_t size, uint8_t* buffer);
    ssize_t  (*write)(struct vfs_node* node, uint32_t offset, uint32_t size, const uint8_t* buffer);
    int      (*readdir)(struct vfs_node* node, uint32_t index, vfs_dirent_t* out);
    struct vfs_node* (*finddir)(struct vfs_node* node, const char* name);
    int      (*create)(struct vfs_node* parent, const char* name, uint32_t type);
    int      (*unlink)(struct vfs_node* parent, const char* name);
    int      (*stat)(struct vfs_node* node, vfs_stat_t* st);
    int      (*truncate)(struct vfs_node* node, uint32_t size);
    int      (*symlink)(struct vfs_node* parent, const char* name, const char* target);
    ssize_t  (*readlink)(struct vfs_node* node, char* buf, size_t bufsiz);
} vfs_ops_t;

/* VFS node — represents any file/directory in the tree */
typedef struct vfs_node {
    char        name[VFS_MAX_NAME + 1];
    uint32_t    inode;
    uint32_t    type;           /* VFS_FILE, VFS_DIRECTORY, etc. */
    uint32_t    size;
    uint32_t    flags;
    vfs_ops_t*  ops;
    void*       fs_private;     /* Filesystem-specific data */
    struct vfs_node* mount;     /* If this is a mount point, points to mounted root */
    struct vfs_node* parent;
} vfs_node_t;

/* File descriptor */
typedef struct {
    vfs_node_t* node;
    uint32_t    offset;
    uint32_t    flags;
    bool        in_use;
} file_descriptor_t;

/* Mount entry */
typedef struct {
    char        path[VFS_MAX_PATH];
    vfs_node_t* root;
    bool        in_use;
} mount_entry_t;

/* VFS API */
void        vfs_init(void);
vfs_node_t* vfs_get_root(void);

/* File descriptor operations */
int         vfs_open(const char* path, uint32_t flags);
int         vfs_close(int fd);
ssize_t     vfs_read(int fd, void* buffer, uint32_t size);
ssize_t     vfs_write(int fd, const void* buffer, uint32_t size);
int         vfs_seek(int fd, int32_t offset, int whence);
int         vfs_stat_fd(int fd, vfs_stat_t* st);

/* Path operations */
int         vfs_stat(const char* path, vfs_stat_t* st);
int         vfs_mkdir(const char* path);
int         vfs_create(const char* path, uint32_t type);
int         vfs_unlink(const char* path);
int         vfs_readdir(const char* path, uint32_t index, vfs_dirent_t* out);
int         vfs_symlink(const char* target, const char* linkpath);
ssize_t     vfs_readlink(const char* path, char* buf, size_t bufsiz);

/* Mount operations */
int         vfs_mount(const char* path, vfs_node_t* fs_root);
int         vfs_umount(const char* path);

/* Path resolution */
vfs_node_t* vfs_resolve_path(const char* path);

#endif
