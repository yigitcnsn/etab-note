#define _GNU_SOURCE
#include "render.h"
#include "fb.h"

#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void render_paper_region(FB *fb, ThemeKind theme, PaperKind paper,
                         int x0, int y0, int x1, int y1) {
    const ThemePalette *tp = &THEMES[theme];
    if (x0 < 0) x0 = 0;
    if (y0 < TOOLBAR_H) y0 = TOOLBAR_H;
    if (x1 >= fb->w) x1 = fb->w - 1;
    if (y1 >= fb->h) y1 = fb->h - 1;
    if (x0 > x1 || y0 > y1) return;

    uint32_t bg_px     = fb_pack_hex(fb, tp->bg);
    uint32_t rule_px   = fb_pack_hex(fb, tp->rule);
    uint32_t margin_px = fb_pack_hex(fb, tp->margin);

    for (int y = y0; y <= y1; y++) {
        uint32_t *row = fb->canvas + (size_t)y * fb->w;
        int ry = y - TOOLBAR_H;
        bool h_line = false;
        if (paper == PAPER_LINED) h_line = (ry > 0 && (ry % 48 == 47));
        else if (paper == PAPER_GRID) h_line = (ry > 0 && (ry % 40 == 0));

        if (h_line) {
            for (int x = x0; x <= x1; x++) row[x] = rule_px;
        } else {
            for (int x = x0; x <= x1; x++) row[x] = bg_px;
            if (paper == PAPER_GRID) {
                int gx = ((x0 + 39) / 40) * 40;
                for (int x = gx; x <= x1; x += 40) row[x] = rule_px;
            }
        }
        if (paper == PAPER_LINED && x0 <= 116 && x1 >= 116) {
            row[116] = margin_px;
        }
    }
    fb_mark_dirty(fb, x0, y0, x1, y1);
}

static uint32_t stroke_color_hex(ThemeKind theme, uint8_t tool, uint8_t color_idx) {
    if (tool == TOOL_HIGHLIGHTER) {
        if (color_idx == 0) return 0xF5D94E;
        return SWATCH_RGB[color_idx < 5 ? color_idx : 4];
    }
    if (color_idx == 0) return THEMES[theme].primary_ink;
    return SWATCH_RGB[color_idx < 5 ? color_idx : 0];
}

static float stroke_radius_at(uint8_t tool, uint8_t size_idx, float p) {
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    float base = PEN_SIZES[size_idx < 3 ? size_idx : 1];
    switch (tool) {
        case TOOL_HIGHLIGHTER: return base * 3.8f + 4.0f;
        case TOOL_BRUSH:       return base * (0.18f + 2.15f * powf(p, 1.15f));
        case TOOL_PENCIL:      return base * (0.65f + 0.85f * p);
        default:               return base * (0.35f + 1.15f * powf(p, 0.78f));
    }
}

static inline uint32_t grain_hash(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (h ^ (h >> 16)) & 0xFFu;
}

static void stamp_aa_disc(FB *fb, float cx, float cy, float r,
                          uint8_t sr, uint8_t sg, uint8_t sb, uint8_t max_alpha,
                          bool pencil_grain, int clip_x0) {
    int ir = (int)ceilf(r) + 1;
    int x0 = (int)floorf(cx) - ir;
    int y0 = (int)floorf(cy) - ir;
    int x1 = (int)ceilf(cx) + ir;
    int y1 = (int)ceilf(cy) + ir;
    if (x0 < clip_x0) x0 = clip_x0;
    if (y0 < TOOLBAR_H) y0 = TOOLBAR_H;
    if (x1 >= fb->w) x1 = fb->w - 1;
    if (y1 >= fb->h) y1 = fb->h - 1;
    if (x0 > x1 || y0 > y1) return;

    float inner = r - 0.65f;
    float outer = r + 0.65f;
    float inv_span = 1.0f / (outer - inner);

    for (int y = y0; y <= y1; y++) {
        float dy = (float)y - cy;
        float dy2 = dy * dy;
        uint32_t *row = fb->canvas + (size_t)y * fb->w;
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x - cx;
            float d2 = dx * dx + dy2;
            if (d2 >= outer * outer) continue;
            float cov;
            if (d2 <= inner * inner && inner > 0.0f) {
                cov = 1.0f;
            } else {
                float d = sqrtf(d2);
                cov = (outer - d) * inv_span;
                if (cov <= 0.0f) continue;
                if (cov > 1.0f) cov = 1.0f;
            }
            uint32_t a = (uint32_t)(cov * (float)max_alpha);
            if (pencil_grain) {
                uint32_t g = grain_hash(x, y);
                a = (a * (90u + g)) / 345u;
            }
            if (a > 0) row[x] = fb_blend_pixel(fb, row[x], sr, sg, sb, (uint8_t)a);
        }
    }
    fb_mark_dirty(fb, x0, y0, x1, y1);
}

void render_stroke_segment(FB *fb, ThemeKind theme, uint8_t tool,
                           uint8_t color_idx, uint8_t size_idx,
                           StrokePoint a, StrokePoint b, int clip_x0) {
    uint32_t hex = stroke_color_hex(theme, tool, color_idx);
    uint8_t sr = (uint8_t)((hex >> 16) & 0xFF);
    uint8_t sg = (uint8_t)((hex >> 8) & 0xFF);
    uint8_t sb = (uint8_t)(hex & 0xFF);
    bool pencil = (tool == TOOL_PENCIL);

    float r0 = stroke_radius_at(tool, size_idx, a.p);
    float r1 = stroke_radius_at(tool, size_idx, b.p);
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float dist = sqrtf(dx * dx + dy * dy);
    float step = (tool == TOOL_HIGHLIGHTER)
        ? fmaxf(3.0f, fminf(r0, r1) * 0.45f)
        : (pencil ? fmaxf(1.2f, fminf(r0, r1) * 0.45f)
                  : fmaxf(0.55f, fminf(r0, r1) * 0.30f));
    int steps = (int)ceilf(dist / step);
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float x = a.x + dx * t;
        float y = a.y + dy * t;
        float r = r0 + (r1 - r0) * t;
        float p = a.p + (b.p - a.p) * t;
        uint8_t alpha = 255;
        if (tool == TOOL_HIGHLIGHTER) alpha = 52;
        else if (pencil) alpha = (uint8_t)(75.0f + 135.0f * p);
        stamp_aa_disc(fb, x, y, r, sr, sg, sb, alpha, pencil, clip_x0);
    }
}

void render_stroke(FB *fb, ThemeKind theme, const Stroke *st, int clip_x0) {
    if (st->count == 0) return;
    if (st->count == 1) {
        render_stroke_segment(fb, theme, st->tool, st->color_idx, st->size_idx,
                              st->pts[0], st->pts[0], clip_x0);
        return;
    }
    for (uint32_t i = 1; i < st->count; i++) {
        render_stroke_segment(fb, theme, st->tool, st->color_idx, st->size_idx,
                              st->pts[i - 1], st->pts[i], clip_x0);
    }
}

void render_page(FB *fb, const NoteDoc *doc, int clip_x0) {
    render_paper_region(fb, (ThemeKind)doc->theme, (PaperKind)doc->paper,
                        clip_x0, TOOLBAR_H, fb->w - 1, fb->h - 1);
    const Page *pg = &doc->pages[doc->cur_page];
    for (size_t i = 0; i < pg->count; i++) {
        render_stroke(fb, (ThemeKind)doc->theme, &pg->strokes[i], clip_x0);
    }
}

int radial_hit_wedge(const RadialMenu *rm, int x, int y) {
    if (!rm->open) return -1;
    float dx = (float)(x - rm->cx);
    float dy = (float)(y - rm->cy);
    float d2 = dx * dx + dy * dy;
    if (d2 < (float)(RADIAL_INNER_R * RADIAL_INNER_R) ||
        d2 > (float)(RADIAL_OUTER_R * RADIAL_OUTER_R)) {
        return -1;
    }
    float ang = atan2f(dy, dx) + (float)(M_PI * 0.5) + (float)(M_PI / 8.0);
    while (ang < 0.0f) ang += (float)(2.0 * M_PI);
    while (ang >= (float)(2.0 * M_PI)) ang -= (float)(2.0 * M_PI);
    int idx = (int)(ang / (float)(M_PI / 4.0));
    if (idx < 0) idx = 0;
    if (idx >= RADIAL_WEDGES) idx = RADIAL_WEDGES - 1;
    return idx;
}

int radial_hit_color(const RadialMenu *rm, int x, int y) {
    if (!rm->open) return -1;
    int bar_y = rm->cy + RADIAL_OUTER_R + 34;
    for (int i = 0; i < 5; i++) {
        int sx = rm->cx + (i - 2) * 52;
        int dx = x - sx, dy = y - bar_y;
        if (dx * dx + dy * dy <= 24 * 24) return i;
    }
    return -1;
}

void render_radial_menu(FB *fb, ThemeKind theme, const RadialMenu *rm,
                        ToolKind cur_tool, uint8_t cur_size, uint8_t cur_color) {
    if (!rm->open) return;
    const ThemePalette *tp = &THEMES[theme];
    int cx = rm->cx, cy = rm->cy;
    int outer = RADIAL_OUTER_R, inner = RADIAL_INNER_R;

    int x0 = cx - outer - 4, y0 = cy - outer - 4;
    int x1 = cx + outer + 4, y1 = cy + outer + 4;
    if (x0 < 0) x0 = 0;
    if (y0 < TOOLBAR_H) y0 = TOOLBAR_H;
    if (x1 >= fb->w) x1 = fb->w - 1;
    if (y1 >= fb->h) y1 = fb->h - 1;

    uint8_t bg_r = (tp->bar_bg >> 16) & 0xFF, bg_g = (tp->bar_bg >> 8) & 0xFF, bg_b = tp->bar_bg & 0xFF;
    uint8_t bd_r = (tp->bar_border >> 16) & 0xFF, bd_g = (tp->bar_border >> 8) & 0xFF, bd_b = tp->bar_border & 0xFF;
    uint8_t ac_r = (tp->accent >> 16) & 0xFF, ac_g = (tp->accent >> 8) & 0xFF, ac_b = tp->accent & 0xFF;
    uint8_t sl_r = (tp->sidebar_sel >> 16) & 0xFF, sl_g = (tp->sidebar_sel >> 8) & 0xFF, sl_b = tp->sidebar_sel & 0xFF;

    int active_wedge = -1;
    if (cur_tool == TOOL_PEN) active_wedge = 0;
    else if (cur_tool == TOOL_BRUSH) active_wedge = 1;
    else if (cur_tool == TOOL_PENCIL) active_wedge = 2;
    else if (cur_tool == TOOL_HIGHLIGHTER) active_wedge = 3;
    else if (cur_tool == TOOL_ERASER) active_wedge = 4;

    for (int y = y0; y <= y1; y++) {
        float dy = (float)(y - cy);
        uint32_t *row = fb->canvas + (size_t)y * fb->w;
        for (int x = x0; x <= x1; x++) {
            float dx = (float)(x - cx);
            float d = sqrtf(dx * dx + dy * dy);
            if (d > (float)outer + 2.0f) continue;

            if (d > (float)outer - 2.0f || (d >= (float)inner - 2.0f && d < (float)inner + 2.0f)) {
                row[x] = fb_blend_pixel(fb, row[x], bd_r, bd_g, bd_b, 240);
            } else if (d < (float)inner - 2.0f) {
                row[x] = fb_blend_pixel(fb, row[x], bg_r, bg_g, bg_b, 235);
            } else {
                float ang = atan2f(dy, dx) + (float)(M_PI * 0.5) + (float)(M_PI / 8.0);
                while (ang < 0.0f) ang += (float)(2.0 * M_PI);
                while (ang >= (float)(2.0 * M_PI)) ang -= (float)(2.0 * M_PI);
                float wedge_f = ang / (float)(M_PI / 4.0);
                int idx = (int)wedge_f;
                if (idx >= RADIAL_WEDGES) idx = RADIAL_WEDGES - 1;
                float frac = wedge_f - (float)idx;
                float edge_dist = fminf(frac, 1.0f - frac) * d * (float)(M_PI / 4.0);

                if (edge_dist < 1.3f) {
                    row[x] = fb_blend_pixel(fb, row[x], bd_r, bd_g, bd_b, 220);
                } else if (idx == rm->hover_wedge) {
                    row[x] = fb_blend_pixel(fb, row[x], ac_r, ac_g, ac_b, 235);
                } else if (idx == active_wedge) {
                    row[x] = fb_blend_pixel(fb, row[x], sl_r, sl_g, sl_b, 235);
                } else {
                    row[x] = fb_blend_pixel(fb, row[x], bg_r, bg_g, bg_b, 230);
                }
            }
        }
    }
    fb_mark_dirty(fb, x0, y0, x1, y1);

    const char *size_lbl[3] = { "SZ:S", "SZ:M", "SZ:L" };
    const char *labels[RADIAL_WEDGES] = {
        "PEN", "BRUSH", "PENCIL", "HI", "ERASE", size_lbl[cur_size % 3], "UNDO", "REDO"
    };
    float mid_r = (float)(inner + outer) * 0.52f;
    for (int i = 0; i < RADIAL_WEDGES; i++) {
        float theta = (float)(-M_PI * 0.5) + (float)i * (float)(M_PI / 4.0);
        int lx = cx + (int)(cosf(theta) * mid_r);
        int ly = cy + (int)(sinf(theta) * mid_r);
        int tw = fb_text_width(labels[i], 2);
        uint32_t fg = (i == rm->hover_wedge) ? 0xFFFFFF : tp->bar_text;
        fb_draw_text(fb, lx - tw / 2, ly - 7, labels[i], fg, 2);
    }
    fb_draw_text(fb, cx - 5, cy - 7, "X", tp->bar_muted, 2);

    int bar_w = 276, bar_h = 52;
    int bar_x = cx - bar_w / 2;
    int bar_y = cy + outer + 8;
    fb_fill_rect(fb, bar_x, bar_y, bar_w, bar_h, tp->bar_bg);
    fb_stroke_rect(fb, bar_x, bar_y, bar_w, bar_h, 2, tp->bar_border);
    int swatch_cy = bar_y + bar_h / 2;
    for (int i = 0; i < 5; i++) {
        int sx = cx + (i - 2) * 52;
        uint32_t col = (i == 0) ? tp->primary_ink : SWATCH_RGB[i];
        if (cur_color == (uint8_t)i || rm->hover_color == i) {
            fb_fill_circle(fb, sx, swatch_cy, 20, tp->accent);
            fb_fill_circle(fb, sx, swatch_cy, 17, tp->bar_bg);
        }
        fb_fill_circle(fb, sx, swatch_cy, 14, col);
    }
}
