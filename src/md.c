#define _GNU_SOURCE
#include "md.h"
#include "fb.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void md_free(MdDoc *md) {
    for (int i = 0; i < md->line_count; i++) free(md->lines[i]);
    free(md->lines);
    memset(md, 0, sizeof(*md));
}

static bool md_push_line(MdDoc *md, const char *s) {
    if (md->line_count >= md->line_cap) {
        int ncap = md->line_cap ? md->line_cap * 2 : 64;
        char **nl = realloc(md->lines, (size_t)ncap * sizeof(char *));
        if (!nl) return false;
        md->lines = nl;
        md->line_cap = ncap;
    }
    md->lines[md->line_count] = strdup(s);
    if (!md->lines[md->line_count]) return false;
    md->line_count++;
    return true;
}

static void build_md_path(char *out, size_t sz, const char *root_dir,
                          const char *folder, const char *filename) {
    if (folder && folder[0])
        snprintf(out, sz, "%s/%s/%s", root_dir, folder, filename);
    else
        snprintf(out, sz, "%s/%s", root_dir, filename);
}

static void strip_quotes(const char *in, char *out, size_t sz) {
    while (*in == ' ' || *in == '\t') in++;
    if (*in == '"' || *in == '\'') in++;
    size_t len = strlen(in);
    while (len > 0 && (in[len - 1] == ' ' || in[len - 1] == '\t' ||
                       in[len - 1] == '"' || in[len - 1] == '\'')) {
        len--;
    }
    if (len >= sz) len = sz - 1;
    memcpy(out, in, len);
    out[len] = '\0';
}

static void parse_frontmatter(MdDoc *md) {
    md->title[0] = '\0';
    md->tags[0] = '\0';
    md->status[0] = '\0';
    md->body_start_line = 0;

    if (md->line_count >= 2 && strcmp(md->lines[0], "---") == 0) {
        for (int i = 1; i < md->line_count && i < 40; i++) {
            if (strcmp(md->lines[i], "---") == 0) {
                md->body_start_line = i + 1;
                break;
            }
            if (strncmp(md->lines[i], "title:", 6) == 0) {
                strip_quotes(md->lines[i] + 6, md->title, sizeof(md->title));
            } else if (strncmp(md->lines[i], "tags:", 5) == 0) {
                strip_quotes(md->lines[i] + 5, md->tags, sizeof(md->tags));
            } else if (strncmp(md->lines[i], "status:", 7) == 0) {
                strip_quotes(md->lines[i] + 7, md->status, sizeof(md->status));
            }
        }
    }

    if (md->title[0] == '\0') {
        size_t len = strlen(md->filename);
        if (len > 3 && strcmp(md->filename + len - 3, ".md") == 0) len -= 3;
        if (len >= sizeof(md->title)) len = sizeof(md->title) - 1;
        memcpy(md->title, md->filename, len);
        md->title[len] = '\0';
    }
}

bool md_load(MdDoc *md, const char *root_dir, const char *folder, const char *filename) {
    char path[512];
    build_md_path(path, sizeof(path), root_dir, folder, filename);
    FILE *f = fopen(path, "r");
    if (!f) return false;

    md_free(md);
    md->active = true;
    if (folder && folder[0]) snprintf(md->folder, sizeof(md->folder), "%s", folder);
    snprintf(md->filename, sizeof(md->filename), "%s", filename);

    char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
        md_push_line(md, buf);
    }
    fclose(f);
    parse_frontmatter(md);
    return true;
}

bool md_save(const MdDoc *md, const char *root_dir) {
    char final_path[512], tmp_path[512];
    build_md_path(final_path, sizeof(final_path), root_dir, md->folder, md->filename);
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", final_path);

    FILE *f = fopen(tmp_path, "w");
    if (!f) return false;
    for (int i = 0; i < md->line_count; i++) {
        fprintf(f, "%s\n", md->lines[i]);
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    return rename(tmp_path, final_path) == 0;
}

bool md_create(MdDoc *md, const char *root_dir, const char *folder, const char *title) {
    char clean[96];
    size_t j = 0;
    for (size_t i = 0; title && title[i] && j + 4 < sizeof(clean); i++) {
        unsigned char c = (unsigned char)title[i];
        if (isalnum(c) || c == ' ' || c == '-' || c == '_') clean[j++] = (char)c;
    }
    while (j > 0 && clean[j - 1] == ' ') j--;
    clean[j] = '\0';
    if (clean[0] == '\0') snprintf(clean, sizeof(clean), "Untitled");

    char fname[128];
    snprintf(fname, sizeof(fname), "%s.md", clean);

    md_free(md);
    md->active = true;
    if (folder && folder[0]) snprintf(md->folder, sizeof(md->folder), "%s", folder);
    snprintf(md->filename, sizeof(md->filename), "%s", fname);

    char hdr[160];
    md_push_line(md, "---");
    snprintf(hdr, sizeof(hdr), "title: \"%s\"", title && title[0] ? title : clean);
    md_push_line(md, hdr);
    md_push_line(md, "tags: []");
    md_push_line(md, "status: \"\"");
    md_push_line(md, "---");
    md_push_line(md, "");
    parse_frontmatter(md);
    return md_save(md, root_dir);
}

bool md_append_line(MdDoc *md, const char *root_dir, const char *line) {
    if (!md->active || !line) return false;
    if (!md_push_line(md, line)) return false;
    return md_save(md, root_dir);
}

/* Strip lightweight inline Markdown markers (**bold**, __em__, `code`, [[link]]) for clean display */
static void clean_inline_md(const char *src, char *dst, size_t dst_sz) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < dst_sz; ) {
        if ((src[i] == '*' && src[i + 1] == '*') ||
            (src[i] == '_' && src[i + 1] == '_') ||
            (src[i] == '[' && src[i + 1] == '[') ||
            (src[i] == ']' && src[i + 1] == ']')) {
            i += 2;
            continue;
        }
        if (src[i] == '`') { i++; continue; }
        dst[j++] = src[i++];
    }
    dst[j] = '\0';
}

static int draw_wrapped(FB *fb, int x, int y, int max_w, const char *text,
                        uint32_t color, int scale, bool strikethrough, int clip_x0) {
    int char_w = 6 * scale;
    int max_cols = max_w / char_w;
    if (max_cols < 8) max_cols = 8;
    int line_h = 7 * scale + 10;

    char clean[512];
    clean_inline_md(text, clean, sizeof(clean));

    int cur_y = y;
    const char *p = clean;
    while (*p) {
        char row[256];
        int len = 0;
        int last_space = -1;
        while (p[len] && len < max_cols && len < (int)sizeof(row) - 1) {
            if (p[len] == ' ') last_space = len;
            len++;
        }
        int take = len;
        if (p[len] != '\0' && last_space > 0) take = last_space;

        memcpy(row, p, (size_t)take);
        row[take] = '\0';

        if (cur_y + line_h > TOOLBAR_H && cur_y < fb->h) {
            int dx = x < clip_x0 ? clip_x0 : x;
            fb_draw_text(fb, dx, cur_y, row, color, scale);
            if (strikethrough) {
                int rw = fb_text_width(row, scale);
                fb_fill_rect(fb, dx, cur_y + (7 * scale) / 2, rw, 2, color);
            }
        }
        cur_y += line_h;
        p += take;
        while (*p == ' ') p++;
    }
    if (cur_y == y) cur_y += line_h;
    return cur_y;
}

void md_scroll(MdDoc *md, int delta_y, int view_h) {
    md->scroll_y += delta_y;
    int max_scroll = md->content_h - view_h + 120;
    if (max_scroll < 0) max_scroll = 0;
    if (md->scroll_y < 0) md->scroll_y = 0;
    if (md->scroll_y > max_scroll) md->scroll_y = max_scroll;
}

bool md_toggle_checkbox_at(MdDoc *md, const char *root_dir, int x, int y) {
    if (!md->active) return false;
    for (int i = 0; i < md->check_count; i++) {
        MdCheckHit *h = &md->checks[i];
        if (x >= h->x0 && x <= h->x1 && y >= h->y0 && y <= h->y1) {
            int idx = h->line_idx;
            if (idx < 0 || idx >= md->line_count) return false;
            char *ln = md->lines[idx];
            char *p = strstr(ln, "[ ]");
            if (p) {
                p[1] = 'x';
                md_save(md, root_dir);
                return true;
            }
            p = strstr(ln, "[x]");
            if (!p) p = strstr(ln, "[X]");
            if (p) {
                p[1] = ' ';
                md_save(md, root_dir);
                return true;
            }
        }
    }
    return false;
}

void md_render(FB *fb, ThemeKind theme, MdDoc *md, int clip_x0) {
    const ThemePalette *tp = &THEMES[theme];
    fb_fill_rect(fb, clip_x0, TOOLBAR_H, fb->w - clip_x0, fb->h - TOOLBAR_H, tp->bg);
    md->check_count = 0;

    int avail_w = fb->w - clip_x0;
    int col_w = avail_w > 1360 ? 1240 : (avail_w - 88);
    if (col_w < 300) col_w = avail_w - 32;
    int col_x = clip_x0 + (avail_w - col_w) / 2;

    int y = TOOLBAR_H + 34 - md->scroll_y;
    int start_y = y;

    /* Document title header */
    y = draw_wrapped(fb, col_x, y, col_w, md->title, tp->accent, 4, false, clip_x0);
    if (md->tags[0] && strcmp(md->tags, "[]") != 0) {
        char tbuf[160];
        snprintf(tbuf, sizeof(tbuf), "TAGS: %s", md->tags);
        y = draw_wrapped(fb, col_x, y + 4, col_w, tbuf, tp->bar_muted, 2, false, clip_x0);
    }
    if (y + 10 > TOOLBAR_H && y + 10 < fb->h) {
        fb_fill_rect(fb, col_x, y + 8, col_w, 2, tp->bar_border);
    }
    y += 28;

    bool in_code = false;
    for (int i = md->body_start_line; i < md->line_count; i++) {
        const char *ln = md->lines[i];

        /* Fenced code block toggle */
        if (strncmp(ln, "```", 3) == 0) {
            in_code = !in_code;
            y += 8;
            continue;
        }
        if (in_code) {
            if (y + 26 > TOOLBAR_H && y < fb->h) {
                fb_fill_rect(fb, col_x, y - 4, col_w, 26, tp->sidebar_bg);
            }
            y = draw_wrapped(fb, col_x + 16, y, col_w - 32, ln[0] ? ln : " ",
                             tp->bar_muted, 2, false, clip_x0);
            continue;
        }

        /* Empty line */
        if (ln[0] == '\0') {
            y += 14;
            continue;
        }

        /* Horizontal rule */
        if (strcmp(ln, "---") == 0 || strcmp(ln, "***") == 0) {
            if (y + 8 > TOOLBAR_H && y + 8 < fb->h) {
                fb_fill_rect(fb, col_x, y + 6, col_w, 2, tp->bar_border);
            }
            y += 20;
            continue;
        }

        /* Headings */
        if (strncmp(ln, "# ", 2) == 0) {
            y += 12;
            y = draw_wrapped(fb, col_x, y, col_w, ln + 2, tp->accent, 3, false, clip_x0);
            if (y > TOOLBAR_H && y < fb->h) fb_fill_rect(fb, col_x, y, col_w, 1, tp->bar_border);
            y += 10;
            continue;
        }
        if (strncmp(ln, "## ", 3) == 0) {
            y += 10;
            y = draw_wrapped(fb, col_x, y, col_w, ln + 3, tp->accent, 3, false, clip_x0);
            y += 4;
            continue;
        }
        if (strncmp(ln, "### ", 4) == 0) {
            y += 6;
            y = draw_wrapped(fb, col_x, y, col_w, ln + 4, tp->primary_ink, 2, false, clip_x0);
            y += 4;
            continue;
        }

        /* Blockquote */
        if (strncmp(ln, "> ", 2) == 0) {
            int by0 = y;
            int by1 = draw_wrapped(fb, col_x + 24, y + 6, col_w - 32, ln + 2, tp->bar_muted, 2, false, clip_x0);
            if (by1 > TOOLBAR_H && by0 < fb->h) {
                int cy0 = by0 < TOOLBAR_H ? TOOLBAR_H : by0;
                int cy1 = by1 >= fb->h ? fb->h - 1 : by1;
                if (cy1 > cy0) fb_fill_rect(fb, col_x + 4, cy0, 5, cy1 - cy0, tp->accent);
            }
            y = by1 + 4;
            continue;
        }

        /* Indentation calculation (tabs or 2/4 spaces) */
        int indent = 0;
        const char *p = ln;
        while (*p == ' ' || *p == '\t') {
            indent += (*p == '\t') ? 36 : 12;
            p++;
        }
        if (indent > 200) indent = 200;

        /* Task checkbox: - [ ] or - [x] */
        if ((strncmp(p, "- [ ] ", 6) == 0 || strncmp(p, "- [x] ", 6) == 0 ||
             strncmp(p, "- [X] ", 6) == 0 || strncmp(p, "* [ ] ", 6) == 0 ||
             strncmp(p, "* [x] ", 6) == 0 || strncmp(p, "* [X] ", 6) == 0)) {
            bool done = (p[3] == 'x' || p[3] == 'X');
            int bx = col_x + indent;
            int by = y - 2;
            if (by + 24 > TOOLBAR_H && by < fb->h) {
                if (done) {
                    fb_fill_rect(fb, bx, by, 24, 24, tp->accent);
                    fb_draw_text(fb, bx + 6, by + 5, "v", 0xFFFFFF, 2);
                } else {
                    fb_fill_rect(fb, bx, by, 24, 24, tp->sidebar_bg);
                    fb_stroke_rect(fb, bx, by, 24, 24, 2, tp->accent);
                }
                if (md->check_count < MAX_MD_CHECKBOXES) {
                    MdCheckHit *h = &md->checks[md->check_count++];
                    h->x0 = bx - 12; h->y0 = by - 8;
                    h->x1 = col_x + col_w; h->y1 = by + 32;
                    h->line_idx = i;
                }
            }
            y = draw_wrapped(fb, bx + 38, y + 2, col_w - indent - 40, p + 6,
                             done ? tp->bar_muted : tp->primary_ink, 2, done, clip_x0);
            y += 4;
            continue;
        }

        /* Bullet list: - item or * item */
        if ((p[0] == '-' || p[0] == '*') && p[1] == ' ') {
            int bx = col_x + indent;
            if (y + 10 > TOOLBAR_H && y < fb->h) {
                fb_fill_circle(fb, bx + 8, y + 7, 4, tp->accent);
            }
            y = draw_wrapped(fb, bx + 26, y, col_w - indent - 28, p + 2,
                             tp->primary_ink, 2, false, clip_x0);
            y += 2;
            continue;
        }

        /* Normal paragraph */
        y = draw_wrapped(fb, col_x + indent, y, col_w - indent, p,
                         tp->primary_ink, 2, false, clip_x0);
        y += 4;
    }

    md->content_h = (y - start_y) + 80;

    /* Scrollbar indicator on far right */
    int view_h = fb->h - TOOLBAR_H;
    if (md->content_h > view_h) {
        int bar_h = (view_h * view_h) / md->content_h;
        if (bar_h < 40) bar_h = 40;
        int max_scroll = md->content_h - view_h + 120;
        int bar_y = TOOLBAR_H + (md->scroll_y * (view_h - bar_h)) / (max_scroll > 0 ? max_scroll : 1);
        fb_fill_rect(fb, fb->w - 10, bar_y, 6, bar_h, tp->bar_border);
    }
}
