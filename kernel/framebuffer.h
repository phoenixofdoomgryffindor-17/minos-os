#ifndef MINOS_KERNEL_FRAMEBUFFER_H
#define MINOS_KERNEL_FRAMEBUFFER_H

#include <stdint.h>
#include "../boot/bootinfo.h"

#define COLOR_BG          0xFF12161F  /* Deep Slate Navy */
#define COLOR_CARD        0xFF1E2430  /* Elevated Container */
#define COLOR_CARD_BORDER 0xFF2E384D  /* Subtle Glass Border */
#define COLOR_ACCENT      0xFF38BDF8  /* MinOS Sky Blue Accent */
#define COLOR_ACCENT_PURP 0xFF818CF8  /* Modern Indigo Accent */
#define COLOR_TEXT_WHITE  0xFFF8FAFC  /* High contrast text */
#define COLOR_TEXT_MUTED  0xFF94A3B8  /* Secondary info text */
#define COLOR_SUCCESS     0xFF34D399  /* Status green */

void fb_init(MinOS_BootInfo *boot_info);
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t argb);
void fb_clear(uint32_t argb);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t argb);
void fb_draw_rect_outline(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void fb_draw_string(uint32_t x, uint32_t y, const char *str, uint32_t fg, uint32_t bg);
void fb_draw_badge(uint32_t x, uint32_t y, const char *text, uint32_t bg_color, uint32_t fg_color);

uint32_t fb_get_width(void);
uint32_t fb_get_height(void);

#endif /* MINOS_KERNEL_FRAMEBUFFER_H */
