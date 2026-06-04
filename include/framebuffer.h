#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "types.h"

/* Framebuffer info — populated from Multiboot2 tags */
typedef struct {
    uint32_t* address;      /* Linear framebuffer base address */
    uint32_t  width;        /* Pixels wide */
    uint32_t  height;       /* Pixels high */
    uint32_t  pitch;        /* Bytes per scanline */
    uint8_t   bpp;          /* Bits per pixel (usually 32) */
    bool      available;    /* True if framebuffer was set up */
} framebuffer_info_t;

/* Color type: 0xAARRGGBB */
typedef uint32_t color_t;

/* Color constants — Hyprland-inspired dark palette */
#define COLOR_BG           0xFF1E1E2E  /* Dark base (Catppuccin Mocha) */
#define COLOR_SURFACE0     0xFF313244
#define COLOR_SURFACE1     0xFF45475A
#define COLOR_SURFACE2     0xFF585B70
#define COLOR_OVERLAY0     0xFF6C7086
#define COLOR_OVERLAY1     0xFF7F849C
#define COLOR_TEXT         0xFFCDD6F4
#define COLOR_SUBTEXT      0xFFA6ADC8
#define COLOR_LAVENDER     0xFFB4BEFE
#define COLOR_BLUE         0xFF89B4FA
#define COLOR_SAPPHIRE     0xFF74C7EC
#define COLOR_SKY          0xFF89DCFE
#define COLOR_TEAL         0xFF94E2D5
#define COLOR_GREEN        0xFFA6E3A1
#define COLOR_YELLOW       0xFFF9E2AF
#define COLOR_PEACH        0xFFFAB387
#define COLOR_RED          0xFFF38BA8
#define COLOR_MAUVE        0xFFCBA6F7
#define COLOR_PINK         0xFFF5C2E7
#define COLOR_ROSEWATER    0xFFF5E0DC
#define COLOR_FLAMINGO     0xFFF2CDCD
#define COLOR_CRUST        0xFF11111B
#define COLOR_MANTLE       0xFF181825
#define COLOR_TRANSPARENT  0x00000000
#define COLOR_WHITE        0xFFFFFFFF
#define COLOR_BLACK        0xFF000000

/* Window border colors */
#define COLOR_BORDER_ACTIVE   COLOR_MAUVE
#define COLOR_BORDER_INACTIVE COLOR_SURFACE1
#define COLOR_TITLEBAR        0xFF1E1E2E
#define COLOR_PANEL           0xE0181825  /* Semi-transparent panel */

/* Externally accessible globals (for compositor use) */
extern uint32_t* backbuffer;
extern const uint8_t font_8x16[95][16];

/* Framebuffer API */
void        fb_init_from_multiboot(uint32_t mb_info_addr);
bool        fb_is_available(void);
framebuffer_info_t fb_get_info(void);

/* Drawing primitives */
void        fb_putpixel(int x, int y, color_t color);
void        fb_fill_rect(int x, int y, int w, int h, color_t color);
void        fb_draw_rect(int x, int y, int w, int h, color_t color, int thickness);
void        fb_draw_rounded_rect(int x, int y, int w, int h, int radius, color_t color);
void        fb_fill_rounded_rect(int x, int y, int w, int h, int radius, color_t color);
void        fb_draw_line(int x0, int y0, int x1, int y1, color_t color);
void        fb_draw_circle(int cx, int cy, int r, color_t color);
void        fb_fill_circle(int cx, int cy, int r, color_t color);
void        fb_clear(color_t color);
void        fb_scroll_up(int pixels, color_t fill_color);

/* Alpha blending */
color_t     fb_blend(color_t bg, color_t fg);
void        fb_fill_rect_alpha(int x, int y, int w, int h, color_t color);

/* Text rendering (8x16 bitmap font) */
void        fb_putchar(int x, int y, char c, color_t fg, color_t bg);
void        fb_puts(int x, int y, const char* str, color_t fg, color_t bg);
void        fb_puts_transparent(int x, int y, const char* str, color_t fg);
int         fb_text_width(const char* str);

/* Double-buffering */
void        fb_swap(void);
void        fb_copy_region(int x, int y, int w, int h);

/* Cursor */
void        fb_draw_cursor(int x, int y);

#define FONT_WIDTH   8
#define FONT_HEIGHT  16

#endif
