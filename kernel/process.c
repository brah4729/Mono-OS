/* kernel/process.c — Process management and round-robin scheduler */

#include "../include/process.h"
#include "../include/heap.h"
#include "../include/pmm.h"
#include "../include/vmm.h"
#include "../include/string.h"
#include "../include/serial.h"
#include "../include/io.h"
#include "../include/pit.h"
#include "../include/isr.h"
#include "../include/gdt.h"

static process_t process_table[MAX_PROCESSES];
static uint32_t  current_pid = 0;
static uint32_t  next_pid = 1;
static bool      scheduler_enabled = false;

/* ─── Internal helpers ──────────────────────────────────── */

static process_t* find_free_slot(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_STATE_FREE) {
            return &process_table[i];
        }
    }
    return NULL;
}

static process_t* find_process(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == pid && process_table[i].state != PROCESS_STATE_FREE) {
            return &process_table[i];
        }
    }
    return NULL;
}

/* Find the next ready process (round-robin) */
static process_t* find_next_ready(void) {
    uint32_t start = current_pid;
    uint32_t idx = start;

    /* Search process table for next READY process */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        idx = (start + 1 + i) % MAX_PROCESSES;
        if (process_table[idx].state == PROCESS_STATE_READY) {
            return &process_table[idx];
        }
    }

    /* No other ready process — stay with current if it's running */
    process_t* curr = find_process(current_pid);
    if (curr && curr->state == PROCESS_STATE_RUNNING) {
        return curr;
    }

    return NULL;
}

/* ─── Public API ────────────────────────────────────────── */

void process_init(void) {
    memset(process_table, 0, sizeof(process_table));

    /* Create PID 0 — the kernel idle process (current context) */
    process_table[0].pid = 0;
    process_table[0].state = PROCESS_STATE_RUNNING;
    process_table[0].parent_pid = 0;
    strcpy(process_table[0].name, "kernel");

    current_pid = 0;
    next_pid = 1;
    scheduler_enabled = false;

    serial_puts("[PROCESS] Process manager initialized (PID 0: kernel)\n");
}

int process_create(const char* name, void (*entry)(void), bool is_user) {
    process_t* proc = find_free_slot();
    if (!proc) {
        serial_puts("[PROCESS] No free process slots\n");
        return -1;
    }

    memset(proc, 0, sizeof(process_t));
    proc->pid = next_pid++;
    proc->state = PROCESS_STATE_READY;
    proc->parent_pid = current_pid;
    strncpy(proc->name, name, 63);

    /* Allocate kernel stack */
    proc->kernel_stack = (uint32_t)kmalloc(KERNEL_STACK_SIZE);
    if (!proc->kernel_stack) {
        proc->state = PROCESS_STATE_FREE;
        return -1;
    }

    uint32_t stack_top = proc->kernel_stack + KERNEL_STACK_SIZE;

    if (is_user) {
        /* Set up a stack frame that iret will pop into user mode */
        /* For now, all processes run in kernel mode (ring 0) */
        /* User mode (ring 3) requires TSS setup — implemented later */
        (void)is_user;
    }

    /*
     * Set up initial stack frame for context_switch:
     *   - Push entry point (eip for ret)
     *   - Push initial register values (ebp, ebx, esi, edi)
     */
    uint32_t* sp = (uint32_t*)stack_top;
    *(--sp) = (uint32_t)entry;   /* Return address (eip) */
    *(--sp) = 0;                  /* ebp */
    *(--sp) = 0;                  /* ebx */
    *(--sp) = 0;                  /* esi */
    *(--sp) = 0;                  /* edi */

    proc->esp = (uint32_t)sp;
    proc->ebp = 0;
    proc->eip = (uint32_t)entry;

    serial_puts("[PROCESS] Created PID ");
    char buf[16];
    utoa(proc->pid, buf, 10);
    serial_puts(buf);
    serial_puts(": ");
    serial_puts(name);
    serial_puts("\n");

    if (!scheduler_enabled) {
        scheduler_enabled = true;
    }

    return (int)proc->pid;
}

void process_exit(int32_t code) {
    process_t* current = find_process(current_pid);
    if (!current || current->pid == 0) {
        serial_puts("[PROCESS] Cannot exit kernel process\n");
        return;
    }

    current->state = PROCESS_STATE_ZOMBIE;
    current->exit_code = code;

    serial_puts("[PROCESS] PID ");
    char buf[16];
    utoa(current->pid, buf, 10);
    serial_puts(buf);
    serial_puts(" exited with code ");
    itoa(code, buf, 10);
    serial_puts(buf);
    serial_puts("\n");

    /* Free kernel stack */
    if (current->kernel_stack) {
        kfree((void*)current->kernel_stack);
        current->kernel_stack = 0;
    }

    /* Unblock parent if it's waiting */
    process_t* parent = find_process(current->parent_pid);
    if (parent && parent->state == PROCESS_STATE_BLOCKED) {
        parent->state = PROCESS_STATE_READY;
    }

    /* Yield to next process */
    process_yield();
}

void process_yield(void) {
    if (!scheduler_enabled) return;

    process_t* current = find_process(current_pid);
    process_t* next = find_next_ready();

    if (!next || next == current) return;

    /* Mark current as ready (if still running) */
    if (current && current->state == PROCESS_STATE_RUNNING) {
        current->state = PROCESS_STATE_READY;
    }

    /* Switch to next */
    next->state = PROCESS_STATE_RUNNING;
    uint32_t old_pid = current_pid;
    current_pid = next->pid;

    if (current) {
        context_switch(&current->esp, next->esp);
    }

    (void)old_pid;
}

void process_schedule(registers_t* regs) {
    /* Called from PIT timer interrupt */
    if (!scheduler_enabled) return;

    (void)regs;
    process_yield();
}

uint32_t process_get_pid(void) {
    return current_pid;
}

process_t* process_get_current(void) {
    return find_process(current_pid);
}

void process_block(uint32_t pid) {
    process_t* proc = find_process(pid);
    if (proc) {
        proc->state = PROCESS_STATE_BLOCKED;
    }
}

void process_unblock(uint32_t pid) {
    process_t* proc = find_process(pid);
    if (proc && proc->state == PROCESS_STATE_BLOCKED) {
        proc->state = PROCESS_STATE_READY;
    }
}

int process_wait(uint32_t pid) {
    process_t* child = find_process(pid);
    if (!child) return -1;

    /* Block current process until child exits */
    while (child->state != PROCESS_STATE_ZOMBIE) {
        process_t* current = find_process(current_pid);
        if (current) {
            current->state = PROCESS_STATE_BLOCKED;
        }
        process_yield();
    }

    int32_t code = child->exit_code;

    /* Clean up zombie */
    child->state = PROCESS_STATE_FREE;

    return code;
}
