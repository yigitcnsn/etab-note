#ifndef NOTEOS_MD_H
#define NOTEOS_MD_H

#include "types.h"

#define MAX_MD_CHECKBOXES 128

typedef struct {
    int x0, y0, x1, y1;
    int line_idx;
} MdCheckHit;

typedef struct {
    bool active;
    char folder[256];
    char filename[128]; /* e.g. "Tasks.md" */
    char title[128];
    char tags[128];
    char status[64];

    char **lines;
    int line_count;
    int line_cap;
    int body_start_line; /* first line after YAML frontmatter */

    int scroll_y;
    int content_h;

    MdCheckHit checks[MAX_MD_CHECKBOXES];
    int check_count;
} MdDoc;

void md_free(MdDoc *md);
bool md_load(MdDoc *md, const char *root_dir, const char *folder, const char *filename);
bool md_save(const MdDoc *md, const char *root_dir);
bool md_create(MdDoc *md, const char *root_dir, const char *folder, const char *title);
bool md_append_line(MdDoc *md, const char *root_dir, const char *line);
bool md_toggle_checkbox_at(MdDoc *md, const char *root_dir, int x, int y);
void md_scroll(MdDoc *md, int delta_y, int view_h);
void md_render(FB *fb, ThemeKind theme, MdDoc *md, int clip_x0);

#endif
