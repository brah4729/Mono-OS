/* kernel/oki.c — Oki Desktop Environment compositor and window manager */

#include "../include/oki.h"
#include "../include/framebuffer.h"
#include "../include/mouse.h"
#include "../include/heap.h"
#include "../include/string.h"
#include "../include/serial.h"
#include "../include/pit.h"
#include "../include/keyboard.h"

static oki_desktop_t desktop;

/* ─── Wallpaper rendering ───────────────────────────────── */

static color_t lerp_color(color_t a, color_t b, int t, int max) {
    uint8_t ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    uint8_t br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;

    uint8_t r = ar + ((br - ar) * t / max);
    uint8_t g = ag + ((bg - ag) * t / max);
    uint8_t bl = ab + ((bb - ab) * t / max);

    return 0xFF000000 | (r << 16) | (g << 8) | bl;
}

static void draw_wallpaper(void) {
    for (int y = 0; y < desktop.screen_h; y++) {
        color_t row_color = lerp_color(desktop.wallpaper_top, desktop.wallpaper_bottom,
                                        y, desktop.screen_h);
        fb_fill_rect(0, y, desktop.screen_w, 1, row_color);
    }
}

/* ─── Panel rendering (Waybar-inspired) ─────────────────── */

static void draw_panel(void) {
    if (!desktop.panel_visible) return;

    int pw = desktop.screen_w;
    int ph = OKI_PANEL_HEIGHT;
    int py = 0; /* Top panel */

    /* Semi-transparent dark panel background */
    fb_fill_rect_alpha(0, py, pw, ph, COLOR_PANEL);

    /* Left: workspace indicators */
    for (int i = 0; i < OKI_MAX_WORKSPACES; i++) {
        int wx = 8 + i * 28;
        int wy = py + 6;
        int ws_size = 20;

        if (i == desktop.current_workspace) {
            fb_fill_rounded_rect(wx, wy, ws_size, ws_size, 4, COLOR_MAUVE);
            char num[2] = { '1' + i, '\0' };
            fb_puts_transparent(wx + 6, wy + 2, num, COLOR_CRUST);
        } else {
            /* Check if workspace has windows */
            bool has_windows = false;
            for (int j = 0; j < desktop.window_count; j++) {
                if (desktop.windows[j] && desktop.windows[j]->workspace == i &&
                    (desktop.windows[j]->flags & OKI_WIN_VISIBLE)) {
                    has_windows = true;
                    break;
                }
            }
            if (has_windows) {
                fb_fill_rounded_rect(wx, wy, ws_size, ws_size, 4, COLOR_SURFACE1);
            } else {
                fb_draw_rounded_rect(wx, wy, ws_size, ws_size, 4, COLOR_SURFACE1);
            }
            char num[2] = { '1' + i, '\0' };
            fb_puts_transparent(wx + 6, wy + 2, num, COLOR_OVERLAY1);
        }
    }

    /* Center: focused window title */
    if (desktop.focused_window >= 0 && desktop.focused_window < desktop.window_count) {
        oki_window_t* win = desktop.windows[desktop.focused_window];
        if (win) {
            int title_w = fb_text_width(win->title);
            int title_x = (pw - title_w) / 2;
            fb_puts_transparent(title_x, py + 8, win->title, COLOR_TEXT);
        }
    }

    /* Right: clock + status */
    uint32_t uptime = pit_get_ticks() / 1000; /* seconds */
    uint32_t hours = uptime / 3600;
    uint32_t minutes = (uptime % 3600) / 60;

    char clock[16];
    clock[0] = '0' + (hours / 10);
    clock[1] = '0' + (hours % 10);
    clock[2] = ':';
    clock[3] = '0' + (minutes / 10);
    clock[4] = '0' + (minutes % 10);
    clock[5] = '\0';

    int clock_w = fb_text_width(clock);
    fb_puts_transparent(pw - clock_w - 12, py + 8, clock, COLOR_SUBTEXT);

    /* Separator dots */
    fb_fill_circle(pw - clock_w - 28, py + ph/2, 3, COLOR_GREEN);
}

/* ─── Window rendering ──────────────────────────────────── */

static void draw_window(oki_window_t* win) {
    if (!win || !(win->flags & OKI_WIN_VISIBLE)) return;
    if (win->workspace != desktop.current_workspace) return;

    bool focused = (desktop.focused_window >= 0 &&
                    desktop.windows[desktop.focused_window] == win);

    color_t border = focused ? COLOR_BORDER_ACTIVE : COLOR_BORDER_INACTIVE;

    int total_x = win->x;
    int total_y = win->y;
    int total_w = win->w;
    int total_h = win->h;

    if (win->flags & OKI_WIN_DECORATED) {
        total_h += OKI_TITLEBAR_HEIGHT;
    }

    /* Shadow (subtle) */
    fb_fill_rect_alpha(total_x + 4, total_y + 4, total_w, total_h, 0x40000000);

    /* Window border + background */
    fb_fill_rounded_rect(total_x - OKI_BORDER_WIDTH,
                         total_y - OKI_BORDER_WIDTH,
                         total_w + 2 * OKI_BORDER_WIDTH,
                         total_h + 2 * OKI_BORDER_WIDTH,
                         OKI_CORNER_RADIUS + 2, border);

    fb_fill_rounded_rect(total_x, total_y, total_w, total_h,
                         OKI_CORNER_RADIUS, COLOR_BG);

    /* Titlebar */
    if (win->flags & OKI_WIN_DECORATED) {
        fb_fill_rect(total_x, total_y, total_w, OKI_TITLEBAR_HEIGHT, COLOR_MANTLE);

        /* Title text */
        fb_puts_transparent(total_x + 12, total_y + 6, win->title,
                           focused ? COLOR_TEXT : COLOR_OVERLAY0);

        /* Close button [x] */
        int btn_x = total_x + total_w - 24;
        int btn_y = total_y + 6;
        fb_fill_rounded_rect(btn_x, btn_y, 16, 16, 4, COLOR_RED);
        fb_puts_transparent(btn_x + 4, btn_y, "x", COLOR_CRUST);

        /* Maximize button */
        btn_x -= 22;
        fb_fill_rounded_rect(btn_x, btn_y, 16, 16, 4, COLOR_YELLOW);

        /* Minimize button */
        btn_x -= 22;
        fb_fill_rounded_rect(btn_x, btn_y, 16, 16, 4, COLOR_GREEN);

        /* Separator line */
        fb_fill_rect(total_x + 1, total_y + OKI_TITLEBAR_HEIGHT - 1,
                     total_w - 2, 1, COLOR_SURFACE0);
    }

    /* Client area — blit the surface */
    if (win->surface) {
        int client_y = total_y + ((win->flags & OKI_WIN_DECORATED) ? OKI_TITLEBAR_HEIGHT : 0);
        uint32_t* target = backbuffer ? backbuffer : fb_get_info().address;
        /* Use backbuffer if available */
        uint32_t pitch4 = fb_get_info().pitch / 4;

        for (int sy = 0; sy < win->surface_h && sy < win->h; sy++) {
            int dy = client_y + sy;
            if (dy < 0 || (uint32_t)dy >= fb_get_info().height) continue;
            for (int sx = 0; sx < win->surface_w && sx < win->w; sx++) {
                int dx = total_x + sx;
                if (dx < 0 || (uint32_t)dx >= fb_get_info().width) continue;
                target[dy * pitch4 + dx] = win->surface[sy * win->surface_w + sx];
            }
        }
    }
}

/* ─── Compositor ────────────────────────────────────────── */

void oki_compose(void) {
    /* 1. Draw wallpaper */
    draw_wallpaper();

    /* 2. Draw windows (back to front) */
    for (int i = 0; i < desktop.window_count; i++) {
        if (i != desktop.focused_window) {
            draw_window(desktop.windows[i]);
        }
    }
    /* Draw focused window last (on top) */
    if (desktop.focused_window >= 0) {
        draw_window(desktop.windows[desktop.focused_window]);
    }

    /* 3. Draw panel */
    draw_panel();

    /* 4. Draw mouse cursor */
    mouse_state_t ms = mouse_get_state();
    fb_draw_cursor(ms.x, ms.y);

    /* 5. Swap buffers */
    fb_swap();
}

/* ─── Window management ─────────────────────────────────── */

oki_window_t* oki_create_window(const char* title, int x, int y, int w, int h, uint32_t flags) {
    if (desktop.window_count >= OKI_MAX_WINDOWS) return NULL;

    oki_window_t* win = (oki_window_t*)kmalloc(sizeof(oki_window_t));
    if (!win) return NULL;

    memset(win, 0, sizeof(oki_window_t));
    win->id = desktop.window_count;
    strncpy(win->title, title, 63);
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->flags = flags | OKI_WIN_VISIBLE;
    win->workspace = desktop.current_workspace;
    win->border_color = COLOR_BORDER_ACTIVE;
    win->titlebar_color = COLOR_TITLEBAR;
    win->title_text_color = COLOR_TEXT;
    win->dirty = true;

    /* Allocate client surface */
    win->surface_w = w;
    win->surface_h = h;
    win->surface = (uint32_t*)kmalloc(w * h * 4);
    if (win->surface) {
        /* Fill with dark background */
        for (int i = 0; i < w * h; i++) {
            win->surface[i] = COLOR_BG;
        }
    }

    desktop.windows[desktop.window_count] = win;
    desktop.focused_window = desktop.window_count;
    desktop.window_count++;
    desktop.needs_redraw = true;

    serial_puts("[OKI] Created window: ");
    serial_puts(title);
    serial_puts("\n");

    return win;
}

void oki_destroy_window(oki_window_t* win) {
    if (!win) return;

    /* Find and remove from windows array */
    for (int i = 0; i < desktop.window_count; i++) {
        if (desktop.windows[i] == win) {
            if (win->surface) kfree(win->surface);

            /* Shift remaining windows */
            for (int j = i; j < desktop.window_count - 1; j++) {
                desktop.windows[j] = desktop.windows[j+1];
            }
            desktop.window_count--;
            desktop.windows[desktop.window_count] = NULL;

            /* Update focus */
            if (desktop.focused_window >= desktop.window_count) {
                desktop.focused_window = desktop.window_count - 1;
            }

            kfree(win);
            desktop.needs_redraw = true;
            return;
        }
    }
}

void oki_focus_window(oki_window_t* win) {
    for (int i = 0; i < desktop.window_count; i++) {
        if (desktop.windows[i] == win) {
            desktop.focused_window = i;
            desktop.needs_redraw = true;
            return;
        }
    }
}

void oki_move_window(oki_window_t* win, int x, int y) {
    if (!win) return;
    win->x = x;
    win->y = y;
    desktop.needs_redraw = true;
}

void oki_resize_window(oki_window_t* win, int w, int h) {
    if (!win) return;

    /* Reallocate surface */
    if (win->surface) kfree(win->surface);
    win->w = w;
    win->h = h;
    win->surface_w = w;
    win->surface_h = h;
    win->surface = (uint32_t*)kmalloc(w * h * 4);
    if (win->surface) {
        for (int i = 0; i < w * h; i++) win->surface[i] = COLOR_BG;
    }
    desktop.needs_redraw = true;
}

/* ─── Window surface drawing ────────────────────────────── */

void oki_window_clear(oki_window_t* win, color_t color) {
    if (!win || !win->surface) return;
    for (int i = 0; i < win->surface_w * win->surface_h; i++) {
        win->surface[i] = color;
    }
    win->dirty = true;
}

void oki_window_putchar(oki_window_t* win, int x, int y, char c, color_t fg) {
    if (!win || !win->surface) return;
    if (c < 32 || c > 126) c = '?';

    const uint8_t* glyph = font_8x16[c - 32];

    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && py >= 0 && px < win->surface_w && py < win->surface_h) {
                    win->surface[py * win->surface_w + px] = fg;
                }
            }
        }
    }
    win->dirty = true;
}

void oki_window_puts(oki_window_t* win, int x, int y, const char* str, color_t fg) {
    int ox = x;
    while (*str) {
        if (*str == '\n') {
            y += 16;
            x = ox;
        } else {
            oki_window_putchar(win, x, y, *str, fg);
            x += 8;
        }
        str++;
    }
}

void oki_window_fill_rect(oki_window_t* win, int x, int y, int w, int h, color_t color) {
    if (!win || !win->surface) return;

    for (int py = y; py < y + h; py++) {
        if (py < 0 || py >= win->surface_h) continue;
        for (int px = x; px < x + w; px++) {
            if (px < 0 || px >= win->surface_w) continue;
            win->surface[py * win->surface_w + px] = color;
        }
    }
    win->dirty = true;
}

/* ─── Workspace ─────────────────────────────────────────── */

void oki_switch_workspace(int ws) {
    if (ws < 0 || ws >= OKI_MAX_WORKSPACES) return;
    desktop.current_workspace = ws;
    desktop.focused_window = -1;

    /* Find first visible window on this workspace */
    for (int i = desktop.window_count - 1; i >= 0; i--) {
        if (desktop.windows[i] && desktop.windows[i]->workspace == ws &&
            (desktop.windows[i]->flags & OKI_WIN_VISIBLE)) {
            desktop.focused_window = i;
            break;
        }
    }

    desktop.needs_redraw = true;
    serial_puts("[OKI] Switched to workspace ");
    char buf[2] = { '1' + ws, '\0' };
    serial_puts(buf);
    serial_puts("\n");
}

/* ─── Mouse handling ────────────────────────────────────── */

void oki_handle_mouse(mouse_state_t* state) {
    desktop.needs_redraw = true; /* Always redraw for cursor movement */

    if (!state->left_button) {
        /* Release drag */
        for (int i = 0; i < desktop.window_count; i++) {
            if (desktop.windows[i]) desktop.windows[i]->dragging = false;
        }
        return;
    }

    /* Check if we're dragging a window */
    for (int i = 0; i < desktop.window_count; i++) {
        oki_window_t* win = desktop.windows[i];
        if (win && win->dragging) {
            win->x = state->x - win->drag_offset_x;
            win->y = state->y - win->drag_offset_y;
            return;
        }
    }

    /* Check if clicking on a window titlebar (back to front, focused first) */
    for (int i = desktop.window_count - 1; i >= 0; i--) {
        oki_window_t* win = desktop.windows[i];
        if (!win || !(win->flags & OKI_WIN_VISIBLE)) continue;
        if (win->workspace != desktop.current_workspace) continue;

        int tx = win->x;
        int ty = win->y;
        int tw = win->w;
        int th = OKI_TITLEBAR_HEIGHT;

        if (win->flags & OKI_WIN_DECORATED) {
            /* Check titlebar click — start drag */
            if (state->x >= tx && state->x < tx + tw &&
                state->y >= ty && state->y < ty + th) {

                /* Check close button */
                int close_x = tx + tw - 24;
                if (state->x >= close_x && state->x < close_x + 16 &&
                    state->y >= ty + 6 && state->y < ty + 22) {
                    oki_destroy_window(win);
                    return;
                }

                win->dragging = true;
                win->drag_offset_x = state->x - win->x;
                win->drag_offset_y = state->y - win->y;
                oki_focus_window(win);
                return;
            }
        }

        /* Check client area click — focus */
        int top_offset = (win->flags & OKI_WIN_DECORATED) ? OKI_TITLEBAR_HEIGHT : 0;
        if (state->x >= win->x && state->x < win->x + win->w &&
            state->y >= win->y + top_offset && state->y < win->y + top_offset + win->h) {
            oki_focus_window(win);
            return;
        }
    }
}

/* ─── Keyboard handling ─────────────────────────────────── */

void oki_handle_key(uint8_t scancode) {
    /* Super+1..4: Switch workspace */
    /* For now, use F1-F4 (scancodes 0x3B-0x3E) */
    if (scancode >= 0x3B && scancode <= 0x3E) {
        oki_switch_workspace(scancode - 0x3B);
        return;
    }

    /* TODO: pass key events to focused window */
    (void)scancode;
}

/* ─── Initialization ────────────────────────────────────── */

void oki_init(void) {
    serial_puts("[OKI] Initializing Oki Desktop Environment...\n");

    memset(&desktop, 0, sizeof(desktop));

    framebuffer_info_t fb = fb_get_info();
    desktop.screen_w = fb.width;
    desktop.screen_h = fb.height;
    desktop.focused_window = -1;
    desktop.current_workspace = 0;
    desktop.panel_visible = true;
    desktop.running = true;
    desktop.needs_redraw = true;

    /* Hyprland-inspired gradient wallpaper */
    desktop.wallpaper_top    = 0xFF1A1B26;  /* Deep dark blue */
    desktop.wallpaper_bottom = 0xFF24283B;  /* Slightly lighter */

    /* Set mouse bounds */
    mouse_set_bounds(desktop.screen_w, desktop.screen_h);

    /* Create a welcome terminal window */
    oki_window_t* term = oki_create_window("Terminal ~ MonoOS",
        OKI_GAP, OKI_PANEL_HEIGHT + OKI_GAP,
        500, 350,
        OKI_WIN_DECORATED | OKI_WIN_MOVABLE | OKI_WIN_RESIZABLE);

    if (term) {
        oki_window_clear(term, COLOR_BG);
        oki_window_puts(term, 8, 8,
            "Welcome to MonoOS!", COLOR_MAUVE);
        oki_window_puts(term, 8, 28,
            "Oki Desktop Environment v0.1", COLOR_SUBTEXT);
        oki_window_puts(term, 8, 48,
            "-----------------------------------", COLOR_SURFACE1);
        oki_window_puts(term, 8, 72,
            "$ dori>_", COLOR_GREEN);
    }

    /* Create a system info window */
    oki_window_t* sysinfo = oki_create_window("System Info",
        520 + OKI_GAP, OKI_PANEL_HEIGHT + OKI_GAP,
        380, 250,
        OKI_WIN_DECORATED | OKI_WIN_MOVABLE);

    if (sysinfo) {
        oki_window_clear(sysinfo, COLOR_BG);
        oki_window_puts(sysinfo, 8, 8,   "MonoOS 0.2.0 (Dori)", COLOR_MAUVE);
        oki_window_puts(sysinfo, 8, 32,  "Kernel:  Dori v0.2", COLOR_TEXT);
        oki_window_puts(sysinfo, 8, 48,  "Desktop: Oki v0.1", COLOR_TEXT);
        oki_window_puts(sysinfo, 8, 64,  "Shell:   dsh 1.0", COLOR_TEXT);
        oki_window_puts(sysinfo, 8, 88,  "Arch:    i686 (x86)", COLOR_SUBTEXT);
        oki_window_puts(sysinfo, 8, 104, "Video:   VESA FB", COLOR_SUBTEXT);

        char buf[64];
        buf[0] = '\0';
        strncpy(buf, "Screen:  ", 10);
        char num[16];
        utoa(fb.width, num, 10);
        strcat(buf, num);
        strcat(buf, "x");
        utoa(fb.height, num, 10);
        strcat(buf, num);
        strcat(buf, "x");
        utoa(fb.bpp, num, 10);
        strcat(buf, num);
        oki_window_puts(sysinfo, 8, 120, buf, COLOR_SUBTEXT);

        oki_window_puts(sysinfo, 8, 152, "Workspaces:", COLOR_LAVENDER);
        oki_window_puts(sysinfo, 8, 168, "  F1-F4 to switch", COLOR_OVERLAY0);
        oki_window_puts(sysinfo, 8, 188, "Drag titlebar to move windows", COLOR_OVERLAY0);
        oki_window_puts(sysinfo, 8, 204, "Click [x] to close", COLOR_OVERLAY0);
    }

    serial_puts("[OKI] Desktop initialized with 2 windows\n");
}

/* ─── Main loop ─────────────────────────────────────────── */

void oki_run(void) {
    serial_puts("[OKI] Entering desktop event loop\n");

    /* Initial compose */
    oki_compose();
    uint32_t last_frame_tick = pit_get_ticks();

    while (desktop.running) {
        /* ── Keyboard input (non-blocking) ── */
        uint8_t sc = keyboard_get_scancode();
        if (sc) {
            oki_handle_key(sc);
        }

        /* ── Frame rate cap: ~30 fps = redraw every 33ms ── */
        uint32_t now = pit_get_ticks();
        if (desktop.needs_redraw && (now - last_frame_tick) >= 33) {
            oki_compose();
            desktop.needs_redraw = false;
            last_frame_tick = now;
        }

        /* ── Sleep until next interrupt instead of busy-spinning ── */
        __asm__ volatile ("hlt");
    }
}