/* drivers/mouse.c — PS/2 Mouse driver for MonoOS */

#include "../include/mouse.h"
#include "../include/io.h"
#include "../include/serial.h"
#include "../include/pic.h"
#include "../include/string.h"

/* PS/2 controller ports */
#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_CMD     0x64

/* Mouse commands */
#define MOUSE_CMD_ENABLE     0xF4
#define MOUSE_CMD_DISABLE    0xF5
#define MOUSE_CMD_RESET      0xFF
#define MOUSE_CMD_SET_DEFAULTS 0xF6

static mouse_state_t mouse;
static mouse_callback_t mouse_cb = NULL;
static int screen_w = 1024;
static int screen_h = 768;

/* Packet state machine */
static uint8_t packet[3];
static int     packet_idx = 0;

/* Wait for PS/2 controller */
static void mouse_wait_write(void) {
    int timeout = 100000;
    while (timeout--) {
        if (!(inb(PS2_STATUS) & 0x02)) return;
    }
}

static void mouse_wait_read(void) {
    int timeout = 100000;
    while (timeout--) {
        if (inb(PS2_STATUS) & 0x01) return;
    }
}

/* Send command to mouse (via PS/2 controller) */
static void mouse_write(uint8_t data) {
    mouse_wait_write();
    outb(PS2_CMD, 0xD4);  /* Tell controller: next byte goes to mouse */
    mouse_wait_write();
    outb(PS2_DATA, data);
}

/* Read response from mouse */
static uint8_t mouse_read(void) {
    mouse_wait_read();
    return inb(PS2_DATA);
}

void mouse_init(void) {
    serial_puts("[MOUSE] Initializing PS/2 mouse...\n");

    memset(&mouse, 0, sizeof(mouse));
    mouse.x = screen_w / 2;
    mouse.y = screen_h / 2;

    /* Enable auxiliary device (mouse) */
    mouse_wait_write();
    outb(PS2_CMD, 0xA8);

    /* Enable interrupts */
    mouse_wait_write();
    outb(PS2_CMD, 0x20);  /* Read controller config */
    mouse_wait_read();
    uint8_t status = inb(PS2_DATA);
    status |= 0x02;       /* Enable IRQ12 */
    status &= ~0x20;      /* Clear mouse clock disable */
    mouse_wait_write();
    outb(PS2_CMD, 0x60);  /* Write controller config */
    mouse_wait_write();
    outb(PS2_DATA, status);

    /* Set defaults */
    mouse_write(MOUSE_CMD_SET_DEFAULTS);
    mouse_read(); /* ACK */

    /* Enable mouse data reporting */
    mouse_write(MOUSE_CMD_ENABLE);
    mouse_read(); /* ACK */

    serial_puts("[MOUSE] PS/2 mouse initialized\n");
}

void mouse_set_bounds(int width, int height) {
    screen_w = width;
    screen_h = height;
}

mouse_state_t mouse_get_state(void) {
    return mouse;
}

void mouse_set_callback(mouse_callback_t cb) {
    mouse_cb = cb;
}

/* Called from IRQ12 interrupt handler */
void mouse_irq_handler(void) {
    uint8_t data = inb(PS2_DATA);

    packet[packet_idx++] = data;

    if (packet_idx < 3) return;

    /* Full 3-byte packet received */
    packet_idx = 0;

    /* Validate: bit 3 of byte 0 should always be set */
    if (!(packet[0] & 0x08)) return;

    /* Decode buttons */
    mouse.left_button   = (packet[0] & 0x01) != 0;
    mouse.right_button  = (packet[0] & 0x02) != 0;
    mouse.middle_button = (packet[0] & 0x04) != 0;

    /* Decode movement (sign-extend using bits in byte 0) */
    int dx = packet[1];
    int dy = packet[2];

    if (packet[0] & 0x10) dx |= 0xFFFFFF00; /* sign extend X */
    if (packet[0] & 0x20) dy |= 0xFFFFFF00; /* sign extend Y */

    /* Check overflow bits — discard if set */
    if (packet[0] & 0x40) return; /* X overflow */
    if (packet[0] & 0x80) return; /* Y overflow */

    /* Debug for now  */
    serial_puts("[MOUSE] dx=");
    serial_put_hex(dx);
    serial_puts(" dy=");
    serial_put_hex(dy);
    serial_puts("\n");


    mouse.dx = -dx;
    mouse.dy = -dy; /* PS/2 Y is inverted */

    /* Update position with clamping */
    mouse.x -= dx;
    mouse.y -= dy; /* PS/2 Y is inverted */

    if (mouse.x < 0) mouse.x = 0;
    if (mouse.y < 0) mouse.y = 0;
    if (mouse.x >= screen_w) mouse.x = screen_w - 1;
    if (mouse.y >= screen_h) mouse.y = screen_h - 1;

    /* Notify callback */
    if (mouse_cb) mouse_cb(&mouse);
}
