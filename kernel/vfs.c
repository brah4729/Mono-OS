/* kernel/vfs.c — Virtual Filesystem layer */

#include "../include/vfs.h"
#include "../include/string.h"
#include "../include/serial.h"
#include "../include/heap.h"

static vfs_node_t*       vfs_root = NULL;
static file_descriptor_t fd_table[VFS_MAX_OPEN_FILES];
static mount_entry_t     mount_table[VFS_MAX_MOUNTS];

void vfs_init(void) {
    memset(fd_table, 0, sizeof(fd_table));
    memset(mount_table, 0, sizeof(mount_table));
    vfs_root = NULL;
    serial_puts("[VFS] Virtual filesystem initialized\n");
}

vfs_node_t* vfs_get_root(void) {
    return vfs_root;
}

/* ─── Internal helpers ──────────────────────────────────── */

/* Allocate the lowest available file descriptor */
static int fd_alloc(void) {
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!fd_table[i].in_use) return i;
    }
    return -1;
}

/* Split a path component: fills `component` and returns pointer past '/' */
static const char* path_next_component(const char* path, char* component, size_t maxlen) {
    while (*path == '/') path++;   /* skip leading slashes */
    size_t i = 0;
    while (*path && *path != '/' && i < maxlen - 1) {
        component[i++] = *path++;
    }
    component[i] = '\0';
    return path;
}

/* Check if a mount is bound at this node and return the mount root */
static vfs_node_t* vfs_check_mount(vfs_node_t* node) {
    if (node && (node->type & VFS_MOUNTPOINT) && node->mount) {
        return node->mount;
    }
    return node;
}

/* ─── Path resolution ───────────────────────────────────── */

vfs_node_t* vfs_resolve_path(const char* path) {
    if (!path || !vfs_root) return NULL;

    vfs_node_t* current = vfs_root;
    current = vfs_check_mount(current);

    /* Handle absolute path */
    if (*path == '/') path++;

    /* Empty path or "/" means root */
    if (*path == '\0') return current;

    char component[VFS_MAX_NAME + 1];
    while (*path) {
        path = path_next_component(path, component, sizeof(component));
        if (component[0] == '\0') break;

        /* Current node must be a directory */
        if (!(current->type & VFS_DIRECTORY)) return NULL;

        /* Look up component in current directory */
        if (!current->ops || !current->ops->finddir) return NULL;
        vfs_node_t* child = current->ops->finddir(current, component);
        if (!child) return NULL;

        /* Handle mount points */
        child = vfs_check_mount(child);

        /* Handle symlinks (single level, no recursive resolution for now) */
        if (child->type & VFS_SYMLINK) {
            if (!child->ops || !child->ops->readlink) return NULL;
            char link_target[VFS_MAX_PATH];
            ssize_t len = child->ops->readlink(child, link_target, sizeof(link_target));
            if (len <= 0) return NULL;
            link_target[len] = '\0';
            /* Resolve the symlink target */
            child = vfs_resolve_path(link_target);
            if (!child) return NULL;
        }

        current = child;
    }

    return current;
}

/* ─── File descriptor API ───────────────────────────────── */

int vfs_open(const char* path, uint32_t flags) {
    vfs_node_t* node = vfs_resolve_path(path);

    /* If node doesn't exist and O_CREAT is set, create it */
    if (!node && (flags & VFS_O_CREAT)) {
        if (vfs_create(path, VFS_FILE) != 0) return -1;
        node = vfs_resolve_path(path);
        if (!node) return -1;
    }

    if (!node) return -1;

    int fd = fd_alloc();
    if (fd < 0) return -1;

    /* Call filesystem-specific open if available */
    if (node->ops && node->ops->open) {
        if (node->ops->open(node, flags) != 0) return -1;
    }

    /* Truncate if requested */
    if ((flags & VFS_O_TRUNC) && node->ops && node->ops->truncate) {
        node->ops->truncate(node, 0);
    }

    fd_table[fd].node = node;
    fd_table[fd].offset = (flags & VFS_O_APPEND) ? node->size : 0;
    fd_table[fd].flags = flags;
    fd_table[fd].in_use = true;

    return fd;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !fd_table[fd].in_use)
        return -1;

    vfs_node_t* node = fd_table[fd].node;
    if (node && node->ops && node->ops->close) {
        node->ops->close(node);
    }

    fd_table[fd].in_use = false;
    fd_table[fd].node = NULL;
    fd_table[fd].offset = 0;
    fd_table[fd].flags = 0;

    return 0;
}

ssize_t vfs_read(int fd, void* buffer, uint32_t size) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !fd_table[fd].in_use)
        return -1;

    vfs_node_t* node = fd_table[fd].node;
    if (!node || !node->ops || !node->ops->read)
        return -1;

    ssize_t bytes = node->ops->read(node, fd_table[fd].offset, size, (uint8_t*)buffer);
    if (bytes > 0) {
        fd_table[fd].offset += bytes;
    }
    return bytes;
}

ssize_t vfs_write(int fd, const void* buffer, uint32_t size) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !fd_table[fd].in_use)
        return -1;

    vfs_node_t* node = fd_table[fd].node;
    if (!node || !node->ops || !node->ops->write)
        return -1;

    /* Append mode: always write at end */
    if (fd_table[fd].flags & VFS_O_APPEND) {
        fd_table[fd].offset = node->size;
    }

    ssize_t bytes = node->ops->write(node, fd_table[fd].offset, size, (const uint8_t*)buffer);
    if (bytes > 0) {
        fd_table[fd].offset += bytes;
        /* Update node size if we wrote past the end */
        if (fd_table[fd].offset > node->size) {
            node->size = fd_table[fd].offset;
        }
    }
    return bytes;
}

int vfs_seek(int fd, int32_t offset, int whence) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !fd_table[fd].in_use)
        return -1;

    vfs_node_t* node = fd_table[fd].node;
    uint32_t new_offset;

    switch (whence) {
        case VFS_SEEK_SET:
            new_offset = (uint32_t)offset;
            break;
        case VFS_SEEK_CUR:
            new_offset = fd_table[fd].offset + offset;
            break;
        case VFS_SEEK_END:
            new_offset = node->size + offset;
            break;
        default:
            return -1;
    }

    fd_table[fd].offset = new_offset;
    return 0;
}

int vfs_stat_fd(int fd, vfs_stat_t* st) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !fd_table[fd].in_use)
        return -1;

    vfs_node_t* node = fd_table[fd].node;
    if (!node || !node->ops || !node->ops->stat)
        return -1;

    return node->ops->stat(node, st);
}

/* ─── Path-based API ────────────────────────────────────── */

int vfs_stat(const char* path, vfs_stat_t* st) {
    vfs_node_t* node = vfs_resolve_path(path);
    if (!node || !node->ops || !node->ops->stat)
        return -1;
    return node->ops->stat(node, st);
}

int vfs_create(const char* path, uint32_t type) {
    /* Find parent directory and target name */
    char parent_path[VFS_MAX_PATH];
    char name[VFS_MAX_NAME + 1];

    /* Split path into parent + basename */
    strncpy(parent_path, path, VFS_MAX_PATH - 1);
    parent_path[VFS_MAX_PATH - 1] = '\0';

    /* Find last '/' */
    int last_slash = -1;
    for (int i = 0; parent_path[i]; i++) {
        if (parent_path[i] == '/') last_slash = i;
    }

    if (last_slash < 0) {
        /* No slash — create in root */
        strcpy(name, parent_path);
        strcpy(parent_path, "/");
    } else if (last_slash == 0) {
        /* Slash at start — parent is root */
        strcpy(name, &parent_path[1]);
        strcpy(parent_path, "/");
    } else {
        strcpy(name, &parent_path[last_slash + 1]);
        parent_path[last_slash] = '\0';
    }

    vfs_node_t* parent = vfs_resolve_path(parent_path);
    if (!parent || !(parent->type & VFS_DIRECTORY))
        return -1;

    if (!parent->ops || !parent->ops->create)
        return -1;

    return parent->ops->create(parent, name, type);
}

int vfs_mkdir(const char* path) {
    return vfs_create(path, VFS_DIRECTORY);
}

int vfs_unlink(const char* path) {
    char parent_path[VFS_MAX_PATH];
    char name[VFS_MAX_NAME + 1];

    strncpy(parent_path, path, VFS_MAX_PATH - 1);
    parent_path[VFS_MAX_PATH - 1] = '\0';

    int last_slash = -1;
    for (int i = 0; parent_path[i]; i++) {
        if (parent_path[i] == '/') last_slash = i;
    }

    if (last_slash < 0) {
        strcpy(name, parent_path);
        strcpy(parent_path, "/");
    } else if (last_slash == 0) {
        strcpy(name, &parent_path[1]);
        strcpy(parent_path, "/");
    } else {
        strcpy(name, &parent_path[last_slash + 1]);
        parent_path[last_slash] = '\0';
    }

    vfs_node_t* parent = vfs_resolve_path(parent_path);
    if (!parent || !(parent->type & VFS_DIRECTORY))
        return -1;
    if (!parent->ops || !parent->ops->unlink)
        return -1;

    return parent->ops->unlink(parent, name);
}

int vfs_readdir(const char* path, uint32_t index, vfs_dirent_t* out) {
    vfs_node_t* node = vfs_resolve_path(path);
    if (!node || !(node->type & VFS_DIRECTORY))
        return -1;
    if (!node->ops || !node->ops->readdir)
        return -1;

    return node->ops->readdir(node, index, out);
}

int vfs_symlink(const char* target, const char* linkpath) {
    char parent_path[VFS_MAX_PATH];
    char name[VFS_MAX_NAME + 1];

    strncpy(parent_path, linkpath, VFS_MAX_PATH - 1);
    parent_path[VFS_MAX_PATH - 1] = '\0';

    int last_slash = -1;
    for (int i = 0; parent_path[i]; i++) {
        if (parent_path[i] == '/') last_slash = i;
    }

    if (last_slash < 0) {
        strcpy(name, parent_path);
        strcpy(parent_path, "/");
    } else if (last_slash == 0) {
        strcpy(name, &parent_path[1]);
        strcpy(parent_path, "/");
    } else {
        strcpy(name, &parent_path[last_slash + 1]);
        parent_path[last_slash] = '\0';
    }

    vfs_node_t* parent = vfs_resolve_path(parent_path);
    if (!parent || !(parent->type & VFS_DIRECTORY))
        return -1;
    if (!parent->ops || !parent->ops->symlink)
        return -1;

    return parent->ops->symlink(parent, name, target);
}

ssize_t vfs_readlink(const char* path, char* buf, size_t bufsiz) {
    /* Don't follow symlinks — find the node's parent and look up directly */
    vfs_node_t* node = vfs_resolve_path(path);
    if (!node) return -1;
    if (!(node->type & VFS_SYMLINK)) return -1;
    if (!node->ops || !node->ops->readlink) return -1;

    return node->ops->readlink(node, buf, bufsiz);
}

/* ─── Mount API ─────────────────────────────────────────── */

int vfs_mount(const char* path, vfs_node_t* fs_root) {
    if (!fs_root) return -1;

    /* Mounting at root "/" */
    if (strcmp(path, "/") == 0) {
        vfs_root = fs_root;
        serial_puts("[VFS] Mounted filesystem at /\n");
        return 0;
    }

    /* Find a free mount slot */
    int slot = -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mount_table[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    /* Resolve the mount point */
    vfs_node_t* mount_point = vfs_resolve_path(path);
    if (!mount_point || !(mount_point->type & VFS_DIRECTORY))
        return -1;

    /* Mark as mount point */
    mount_point->type |= VFS_MOUNTPOINT;
    mount_point->mount = fs_root;
    fs_root->parent = mount_point->parent;

    strncpy(mount_table[slot].path, path, VFS_MAX_PATH - 1);
    mount_table[slot].root = fs_root;
    mount_table[slot].in_use = true;

    serial_puts("[VFS] Mounted filesystem at ");
    serial_puts(path);
    serial_puts("\n");

    return 0;
}

int vfs_umount(const char* path) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mount_table[i].in_use && strcmp(mount_table[i].path, path) == 0) {
            vfs_node_t* mount_point = vfs_resolve_path(path);
            if (mount_point) {
                mount_point->type &= ~VFS_MOUNTPOINT;
                mount_point->mount = NULL;
            }
            mount_table[i].in_use = false;
            serial_puts("[VFS] Unmounted ");
            serial_puts(path);
            serial_puts("\n");
            return 0;
        }
    }
    return -1;
}
