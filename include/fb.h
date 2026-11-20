#ifndef NOTEOS_FB_H
#define NOTEOS_FB_H

#include "types.h"

int      fb_open(FB *fb);
void     fb_close(FB *fb);
void     fb_set_backlight(int level);
int      fb_get_backlight(void);
void     fb_blank(FB *fb, bool blank);

uint32_t fb_pack_hex(const FB *fb, uint32_t hex);
uint32_t fb_blend_pixel(const FB *fb, uint32_t dst_px,
                        uint8_t sr, uint8_t sg, uint8_t sb, uint8_t alpha);

void     rect_reset(Rect *r);
void     rect_expand(Rect *r, int x0, int y0, int x1, int y1, int w, int h);
void     fb_mark_dirty(FB *fb, int x0, int y0, int x1, int y1);
void     fb_mark_all(FB *fb);

void     fb_fill_rect(FB *fb, int x, int y, int w, int h, uint32_t hex);
void     fb_stroke_rect(FB *fb, int x, int y, int w, int h, int thick, uint32_t hex);
void     fb_fill_circle(FB *fb, int cx, int cy, int r, uint32_t hex);
void     fb_draw_button(FB *fb, const ThemePalette *tp, int x, int y, int w, int h,
                        const char *label, bool active, bool danger);
void     fb_flash_last_button(FB *fb, const ThemePalette *tp);
void     fb_draw_text(FB *fb, int x, int y, const char *s, uint32_t hex, int scale);
int      fb_text_width(const char *s, int scale);

void     fb_present(FB *fb, bool hover, int hx, int hy, bool erase_cursor, int clip_x0);

#endif
