#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"
#include "isr.h"

/* System call numbers */
#define SYS_EXIT       0
#define SYS_READ       1
#define SYS_WRITE      2
#define SYS_OPEN       3
#define SYS_CLOSE      4
#define SYS_STAT       5
#define SYS_SEEK       6
#define SYS_READDIR    7
#define SYS_EXEC       8
#define SYS_FORK       9
#define SYS_WAITPID    10
#define SYS_BRK        11
#define SYS_GETPID     12
#define SYS_MKDIR      13
#define SYS_UNLINK     14
#define SYS_SYMLINK    15
#define SYS_READLINK   16
#define SYS_GETCWD     17
#define SYS_CHDIR      18

#define SYSCALL_COUNT  19
#define SYSCALL_INT    0x80

/* Initialize system call handler */
void syscall_init(void);

/* System call handler (called from ISR) */
void syscall_handler(registers_t* regs);

#endif
