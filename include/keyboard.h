#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

#define KB_DATA_PORT 0x60
#define KB_STATUS_PORT 0x64
#define KB_BUFFER_SIZE 256

void keyboard_init(void);
char keyboard_get_char(void);
bool keyboard_has_input(void);
uint8_t keyboard_get_scancode(void);

#endif
