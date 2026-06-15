/* drivers/keyboard.c — PS/2 keyboard driver (IRQ1, scancode set 1) */

#include "../include/keyboard.h"
#include "../include/isr.h"
#include "../include/io.h"
#include "../include/serial.h"

/* ── ASCII buffer (for kshell blocking reads) ── */
static char kb_buffer[KB_BUFFER_SIZE];
static volatile uint32_t kb_buffer_head = 0;
static volatile uint32_t kb_buffer_tail = 0;

/* ── Raw scancode buffer (for Oki DE non-blocking reads) ── */  // ← ADD THIS BLOCK
static uint8_t  sc_buffer[KB_BUFFER_SIZE];
static volatile uint32_t sc_head = 0;
static volatile uint32_t sc_tail = 0;

static void sc_buffer_push(uint8_t sc) {
    uint32_t next = (sc_head + 1) % KB_BUFFER_SIZE;
    if (next != sc_tail) {
        sc_buffer[sc_head] = sc;
        sc_head = next;
    }
}

/* Modifier key states */
static bool shift_pressed = false;
static bool caps_lock     = false;
static bool ctrl_pressed  = false;

/* US QWERTY scancode-to-ASCII table (scancode set 1) */
static const char scancode_ascii[] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ', 0, 0,0,0,0,0,0,0,0,0,0, 0, 0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.'
};

static const char scancode_ascii_shift[] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' ', 0, 0,0,0,0,0,0,0,0,0,0, 0, 0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.'
};

static void kb_buffer_push(char c) {
    uint32_t next = (kb_buffer_head + 1) % KB_BUFFER_SIZE;
    if (next != kb_buffer_tail) {
        kb_buffer[kb_buffer_head] = c;
        kb_buffer_head = next;
    }
}

static void keyboard_callback(registers_t* regs) {
    (void)regs;
    uint8_t scancode = inb(KB_DATA_PORT);

    /* Key release (bit 7 set) */
    if (scancode & 0x80) {
        uint8_t released = scancode & 0x7F;
        if (released == 0x2A || released == 0x36) shift_pressed = false;
        if (released == 0x1D) ctrl_pressed = false;
        return;
    }

    /* Push raw scancode for DE hotkeys (key press only) */  // ← ADD THIS
    sc_buffer_push(scancode);

    /* Special keys */
    switch (scancode) {
        case 0x2A: case 0x36: shift_pressed = true; return;
        case 0x1D: ctrl_pressed = true; return;
        case 0x3A: caps_lock = !caps_lock; return;
    }

    if (scancode >= sizeof(scancode_ascii)) return;

    char c;
    bool use_shift = shift_pressed ^ caps_lock;

    if (use_shift) {
        c = scancode_ascii_shift[scancode];
    } else {
        c = scancode_ascii[scancode];
    }

    if (caps_lock && !shift_pressed) {
        if (c >= 'a' && c <= 'z') c -= 32;
    } else if (caps_lock && shift_pressed) {
        if (c >= 'A' && c <= 'Z') c += 32;
    }

    if (c != 0) {
        if (ctrl_pressed) {
            if (c == 'c' || c == 'C') {
                kb_buffer_push(3);
                return;
            }
        }
        kb_buffer_push(c);
    }
}

void keyboard_init(void) {
    kb_buffer_head = 0;
    kb_buffer_tail = 0;
    sc_head        = 0;   // ← ADD
    sc_tail        = 0;   // ← ADD
    irq_register_handler(1, keyboard_callback);
    serial_puts("[KB] PS/2 keyboard initialized\n");
}

char keyboard_getchar(void) {
    while (kb_buffer_tail == kb_buffer_head) {
        __asm__ volatile ("hlt");
    }
    char c = kb_buffer[kb_buffer_tail];
    kb_buffer_tail = (kb_buffer_tail + 1) % KB_BUFFER_SIZE;
    return c;
}

bool keyboard_has_input(void) {
    return kb_buffer_tail != kb_buffer_head;
}

/* Non-blocking — returns 0 if no scancode waiting */   // ← ADD THIS FUNCTION
uint8_t keyboard_get_scancode(void) {
    if (sc_tail == sc_head) return 0;
    uint8_t sc = sc_buffer[sc_tail];
    sc_tail = (sc_tail + 1) % KB_BUFFER_SIZE;
    return sc;
}