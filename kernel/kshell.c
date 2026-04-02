/* kernel/kshell.c — Dori Shell (dsh) — kernel-mode command-line shell */

#include "../include/kshell.h"
#include "../include/vga.h"
#include "../include/keyboard.h"
#include "../include/string.h"
#include "../include/pmm.h"
#include "../include/pit.h"
#include "../include/serial.h"
#include "../include/io.h"
#include "../include/kernel.h"
#include "../include/vfs.h"
#include "../include/dorifs.h"
#include "../include/ata.h"
#include "../include/process.h"

#define CMD_BUFFER_SIZE 256

static char cmd_buffer[CMD_BUFFER_SIZE];
static int  cmd_pos = 0;

static void print_prompt(void) {
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("dori");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("> ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void cmd_help(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("Dori Shell (dsh) - Available Commands:\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("  help      - Show this help message\n");
    vga_puts("  clear     - Clear the screen\n");
    vga_puts("  meminfo   - Display memory information\n");
    vga_puts("  ver       - Show kernel version\n");
    vga_puts("  echo      - Echo text to screen\n");
    vga_puts("  uptime    - Show system uptime\n");
    vga_puts("  reboot    - Restart the system\n");
    vga_puts("  halt      - Shut down the system\n");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("Filesystem:\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("  ls [path] - List directory contents\n");
    vga_puts("  cat file  - Display file contents\n");
    vga_puts("  touch f   - Create an empty file\n");
    vga_puts("  write f d - Write data to a file\n");
    vga_puts("  mkdir dir - Create a directory\n");
    vga_puts("  rm file   - Remove a file\n");
    vga_puts("  diskinfo  - Show disk information\n");
    vga_puts("  fsinfo    - Show filesystem information\n");
}

static void cmd_meminfo(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("Memory Information:\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    vga_puts("  Total blocks:  ");
    vga_put_dec(pmm_get_block_count());
    vga_puts(" (");
    vga_put_dec(pmm_get_block_count() * 4);
    vga_puts(" KiB)\n");

    vga_puts("  Used blocks:   ");
    vga_put_dec(pmm_get_used_block_count());
    vga_puts(" (");
    vga_put_dec(pmm_get_used_block_count() * 4);
    vga_puts(" KiB)\n");

    vga_puts("  Free blocks:   ");
    vga_put_dec(pmm_get_free_block_count());
    vga_puts(" (");
    vga_put_dec(pmm_get_free_block_count() * 4);
    vga_puts(" KiB)\n");
}

static void cmd_ver(void) {
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("MonoOS v" MONOOS_VERSION " - ");
    vga_puts(KERNEL_NAME);
    vga_puts(" Kernel\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("  Built with love and caffeine.\n");
}

static void cmd_echo(const char* args) {
    if (args && *args) {
        vga_puts(args);
    }
    vga_puts("\n");
}

static void cmd_uptime(void) {
    uint32_t ticks = pit_get_ticks();
    uint32_t seconds = ticks / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;

    vga_puts("  Uptime: ");
    vga_put_dec(hours);
    vga_puts("h ");
    vga_put_dec(minutes % 60);
    vga_puts("m ");
    vga_put_dec(seconds % 60);
    vga_puts("s (");
    vga_put_dec(ticks);
    vga_puts(" ticks)\n");
}

static void cmd_reboot(void) {
    vga_puts("Rebooting...\n");
    /* Triple fault to reboot: load empty IDT and trigger interrupt */
    uint8_t good = 0x02;
    while (good & 0x02) {
        good = inb(0x64);
    }
    outb(0x64, 0xFE);
    cli();
    for (;;) hlt();
}

static void cmd_halt(void) {
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("System halted. You may turn off your computer.\n");
    cli();
    for (;;) hlt();
}

/* ─── Filesystem commands ───────────────────────────────── */

static void cmd_ls(const char* path) {
    if (!path || !*path) path = "/";

    vfs_dirent_t entry;
    uint32_t index = 0;

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("  Contents of ");
    vga_puts(path);
    vga_puts(":\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    while (vfs_readdir(path, index, &entry) == 0) {
        vga_puts("  ");
        if (entry.type == 2) {  /* directory */
            vga_set_color(VGA_LIGHT_BLUE, VGA_BLACK);
            vga_puts("[DIR] ");
        } else if (entry.type == 3) {  /* symlink */
            vga_set_color(VGA_LIGHT_MAGENTA, VGA_BLACK);
            vga_puts("[LNK] ");
        } else {
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            vga_puts("      ");
        }
        vga_puts(entry.name);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        vga_puts("\n");
        index++;
    }

    if (index == 0) {
        vga_puts("  (empty)\n");
    }
}

static void cmd_cat(const char* path) {
    if (!path || !*path) {
        vga_puts("Usage: cat <file>\n");
        return;
    }

    int fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("Cannot open: ");
        vga_puts(path);
        vga_puts("\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    uint8_t buf[256];
    ssize_t n;
    while ((n = vfs_read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        vga_puts((const char*)buf);
    }
    vga_puts("\n");
    vfs_close(fd);
}

static void cmd_touch(const char* path) {
    if (!path || !*path) {
        vga_puts("Usage: touch <file>\n");
        return;
    }

    int fd = vfs_open(path, VFS_O_CREAT | VFS_O_WRONLY);
    if (fd < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("Cannot create: ");
        vga_puts(path);
        vga_puts("\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    vfs_close(fd);
    vga_puts("Created: ");
    vga_puts(path);
    vga_puts("\n");
}

static void cmd_write_file(const char* args) {
    if (!args || !*args) {
        vga_puts("Usage: write <file> <data>\n");
        return;
    }

    /* Parse: first word is filename, rest is data */
    char filepath[VFS_MAX_PATH];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < VFS_MAX_PATH - 1) {
        filepath[i] = args[i];
        i++;
    }
    filepath[i] = '\0';

    const char* data = &args[i];
    while (*data == ' ') data++;

    if (!*data) {
        vga_puts("Usage: write <file> <data>\n");
        return;
    }

    int fd = vfs_open(filepath, VFS_O_CREAT | VFS_O_WRONLY | VFS_O_TRUNC);
    if (fd < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("Cannot open for writing: ");
        vga_puts(filepath);
        vga_puts("\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    ssize_t written = vfs_write(fd, data, strlen(data));
    vfs_close(fd);

    vga_puts("Wrote ");
    vga_put_dec((uint32_t)written);
    vga_puts(" bytes to ");
    vga_puts(filepath);
    vga_puts("\n");
}

static void cmd_mkdir_path(const char* path) {
    if (!path || !*path) {
        vga_puts("Usage: mkdir <directory>\n");
        return;
    }

    if (vfs_mkdir(path) == 0) {
        vga_puts("Created directory: ");
        vga_puts(path);
        vga_puts("\n");
    } else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("Failed to create: ");
        vga_puts(path);
        vga_puts("\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

static void cmd_rm(const char* path) {
    if (!path || !*path) {
        vga_puts("Usage: rm <file>\n");
        return;
    }

    if (vfs_unlink(path) == 0) {
        vga_puts("Removed: ");
        vga_puts(path);
        vga_puts("\n");
    } else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("Failed to remove: ");
        vga_puts(path);
        vga_puts("\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

static void cmd_diskinfo(void) {
    if (!ata_is_present()) {
        vga_puts("  No disk detected\n");
        return;
    }
    ata_drive_t drive = ata_get_drive_info();
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("Disk Information:\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("  Model:    ");
    vga_puts(drive.model);
    vga_puts("\n");
    vga_puts("  Sectors:  ");
    vga_put_dec(drive.size_sectors);
    vga_puts("\n");
    vga_puts("  Size:     ");
    vga_put_dec(drive.size_mb);
    vga_puts(" MB\n");
}

static void cmd_fsinfo(void) {
    dorifs_print_info();
}

/* ─── Command dispatcher ───────────────────────────────── */

static void execute_command(const char* cmd) {
    /* Skip leading whitespace */
    while (*cmd == ' ') cmd++;

    if (*cmd == '\0') return;

    serial_puts("[DSH] > ");
    serial_puts(cmd);
    serial_puts("\n");

    if (strcmp(cmd, "help") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "clear") == 0) {
        vga_clear();
    } else if (strcmp(cmd, "meminfo") == 0) {
        cmd_meminfo();
    } else if (strcmp(cmd, "ver") == 0) {
        cmd_ver();
    } else if (strncmp(cmd, "echo ", 5) == 0) {
        cmd_echo(cmd + 5);
    } else if (strcmp(cmd, "echo") == 0) {
        cmd_echo("");
    } else if (strcmp(cmd, "uptime") == 0) {
        cmd_uptime();
    } else if (strcmp(cmd, "reboot") == 0) {
        cmd_reboot();
    } else if (strcmp(cmd, "halt") == 0) {
        cmd_halt();
    /* Filesystem commands */
    } else if (strcmp(cmd, "ls") == 0) {
        cmd_ls("/");
    } else if (strncmp(cmd, "ls ", 3) == 0) {
        cmd_ls(cmd + 3);
    } else if (strncmp(cmd, "cat ", 4) == 0) {
        cmd_cat(cmd + 4);
    } else if (strncmp(cmd, "touch ", 6) == 0) {
        cmd_touch(cmd + 6);
    } else if (strncmp(cmd, "write ", 6) == 0) {
        cmd_write_file(cmd + 6);
    } else if (strncmp(cmd, "mkdir ", 6) == 0) {
        cmd_mkdir_path(cmd + 6);
    } else if (strncmp(cmd, "rm ", 3) == 0) {
        cmd_rm(cmd + 3);
    } else if (strcmp(cmd, "diskinfo") == 0) {
        cmd_diskinfo();
    } else if (strcmp(cmd, "fsinfo") == 0) {
        cmd_fsinfo();
    } else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("Unknown command: ");
        vga_puts(cmd);
        vga_puts("\nType 'help' for available commands.\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

void kshell_init(void) {
    serial_puts("[DSH] Dori shell initialized\n");
}

void kshell_run(void) {
    print_prompt();
    cmd_pos = 0;
    memset(cmd_buffer, 0, CMD_BUFFER_SIZE);

    while (1) {
        char c = keyboard_getchar();

        if (c == '\n') {
            vga_putchar('\n');
            cmd_buffer[cmd_pos] = '\0';
            execute_command(cmd_buffer);
            cmd_pos = 0;
            memset(cmd_buffer, 0, CMD_BUFFER_SIZE);
            print_prompt();
        } else if (c == '\b') {
            if (cmd_pos > 0) {
                cmd_pos--;
                cmd_buffer[cmd_pos] = '\0';
                vga_putchar('\b');
            }
        } else if (c == 3) {
            /* Ctrl+C: cancel current line */
            vga_puts("^C\n");
            cmd_pos = 0;
            memset(cmd_buffer, 0, CMD_BUFFER_SIZE);
            print_prompt();
        } else if (cmd_pos < CMD_BUFFER_SIZE - 1) {
            cmd_buffer[cmd_pos++] = c;
            vga_putchar(c);
        }
    }
}
