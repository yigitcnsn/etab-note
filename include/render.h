#ifndef NOTEOS_RENDER_H
#define NOTEOS_RENDER_H

#include "types.h"

void render_paper_region(FB *fb, ThemeKind theme, PaperKind paper,
                         int x0, int y0, int x1, int y1);
void render_stroke_segment(FB *fb, ThemeKind theme, uint8_t tool,
                           uint8_t color_idx, uint8_t size_idx,
                           StrokePoint a, StrokePoint b, int clip_x0);
void render_stroke(FB *fb, ThemeKind theme, const Stroke *st, int clip_x0);
void render_page(FB *fb, const NoteDoc *doc, int clip_x0);

void render_radial_menu(FB *fb, ThemeKind theme, const RadialMenu *rm,
                        ToolKind cur_tool, uint8_t cur_size, uint8_t cur_color);
int  radial_hit_wedge(const RadialMenu *rm, int x, int y);
int  radial_hit_color(const RadialMenu *rm, int x, int y);

#endif
