#ifndef NOTEOS_H
#define NOTEOS_H

#include <linux/fb.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TOOLBAR_H    58
#define SIDEBAR_W    320
#define MAX_NOTES    128
#define MAX_FOLDERS  64
#define MAX_PAGES    64

typedef enum {
    TOOL_PEN         = 0,
    TOOL_HIGHLIGHTER = 1,
    TOOL_ERASER      = 2,
    TOOL_BRUSH       = 3,
    TOOL_PENCIL      = 4
} ToolKind;

typedef enum {
    PAPER_LINED = 0,
    PAPER_GRID  = 1,
    PAPER_BLANK = 2,
    PAPER_COUNT = 3
} PaperKind;

typedef enum {
    THEME_LIGHT     = 0,
    THEME_PARCHMENT = 1,
    THEME_OBSIDIAN  = 2,
    THEME_COUNT     = 3
} ThemeKind;

typedef struct {
    const char *name;
    uint32_t bg;         /* 0xRRGGBB */
    uint32_t rule;
    uint32_t margin;
    uint32_t bar_bg;
    uint32_t bar_border;
    uint32_t bar_text;
    uint32_t bar_muted;
    uint32_t accent;
    uint32_t sidebar_bg;
    uint32_t sidebar_sel;
    uint32_t primary_ink;
} ThemePalette;

typedef struct {
    float x;
    float y;
    float p; /* normalized 0.0 .. 1.0 */
} StrokePoint;

typedef struct {
    uint8_t tool;       /* ToolKind */
    uint8_t color_idx;  /* 0..4 */
    uint8_t size_idx;   /* 0=S, 1=M, 2=L */
    uint8_t _pad;
    uint32_t count;
    uint32_t cap;
    StrokePoint *pts;
    float min_x, min_y, max_x, max_y;
} Stroke;

typedef struct {
    Stroke *strokes;
    size_t count;
    size_t cap;
    Stroke *redo_strokes;
    size_t redo_count;
    size_t redo_cap;
} Page;

typedef struct {
    char folder[64];    /* "" for root, or folder name e.g. "folder_01" */
    char id[64];        /* filename stem, e.g. "note_0001" */
    char title[64];
    uint8_t paper;      /* PaperKind */
    uint8_t theme;      /* ThemeKind */
    int page_count;
    int cur_page;
    Page pages[MAX_PAGES];
    bool dirty;
} NoteDoc;

typedef struct {
    char id[64];
    char title[64];
    int page_count;
} NoteMeta;

typedef struct {
    char name[64];
    char display[64];
    int note_count;
} FolderMeta;

typedef struct {
    int x0, y0, x1, y1;
    bool valid;
} Rect;

typedef struct {
    int fd;
    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo vinfo;
    uint8_t *fb_mem;
    size_t fb_size;
    uint32_t *canvas;
    int w, h;
    bool double_buf;
    int back_page;
    Rect dirty[2];
    Rect prev_cursor[2];
} FB;

/* Radial menu state */
#define RADIAL_INNER_R 44
#define RADIAL_OUTER_R 156
#define RADIAL_WEDGES  8

typedef struct {
    bool open;
    int cx, cy;          /* center of wheel on screen */
    int hover_wedge;     /* 0..7, or -1 */
    int hover_color;     /* 0..4, or -1 */
} RadialMenu;

#define MAX_WIFI_NETS 16

typedef struct {
    char ssid[64];
    int signal;          /* dBm */
    bool secured;
} WifiNet;

typedef enum {
    OSK_NONE = 0,
    OSK_WIFI_PASS = 1,
    OSK_RENAME_NOTE = 2
} OskPurpose;

typedef struct {
    bool open;
    OskPurpose purpose;
    char prompt[96];
    char target_ssid[64];
    char buf[128];
    int len;
    bool shift;
    bool symbols;
} OskState;

typedef struct {
    bool open;
    char ssid[64];
    char ip[32];
    char wpa_state[32];
    char status_msg[96];
    WifiNet nets[MAX_WIFI_NETS];
    int net_count;
    int confirm_action;  /* 0=none, 1=reboot, 2=recovery, 3=poweroff */
    OskState osk;
} SettingsState;

/* Return flags from settings_handle_tap */
#define SET_ACT_NONE      0
#define SET_ACT_REDRAW    1
#define SET_ACT_CLOSE     2
#define SET_ACT_SLEEP     3
#define SET_ACT_RENAMED   4

extern const uint32_t SWATCH_RGB[5];
extern const ThemePalette THEMES[THEME_COUNT];
extern const float PEN_SIZES[3];

/* fb.c */
int  fb_open(FB *fb);
void fb_close(FB *fb);
void fb_set_backlight(int level);
int  fb_get_backlight(void);
void fb_blank(FB *fb, bool blank);

uint32_t fb_pack_hex(const FB *fb, uint32_t hex);
void rect_reset(Rect *r);
void rect_expand(Rect *r, int x0, int y0, int x1, int y1, int w, int h);
void fb_mark_dirty(FB *fb, int x0, int y0, int x1, int y1);
void fb_mark_all(FB *fb);

void fb_fill_rect(FB *fb, int x, int y, int w, int h, uint32_t hex);
void fb_stroke_rect(FB *fb, int x, int y, int w, int h, int thick, uint32_t hex);
void fb_fill_circle(FB *fb, int cx, int cy, int r, uint32_t hex);
void fb_draw_text(FB *fb, int x, int y, const char *s, uint32_t hex, int scale);
int  fb_text_width(const char *s, int scale);

void render_paper_region(FB *fb, ThemeKind theme, PaperKind paper, int x0, int y0, int x1, int y1);
void render_stroke_segment(FB *fb, ThemeKind theme, uint8_t tool, uint8_t color_idx, uint8_t size_idx,
                           StrokePoint a, StrokePoint b, int clip_x0);
void render_stroke(FB *fb, ThemeKind theme, const Stroke *st, int clip_x0);
void render_page(FB *fb, const NoteDoc *doc, int clip_x0);
void render_radial_menu(FB *fb, ThemeKind theme, const RadialMenu *rm,
                        ToolKind cur_tool, uint8_t cur_size, uint8_t cur_color);
int  radial_hit_wedge(const RadialMenu *rm, int x, int y);
int  radial_hit_color(const RadialMenu *rm, int x, int y);

void fb_present(FB *fb, bool hover, int hx, int hy, bool erase_cursor, int clip_x0);

/* storage.c */
void stroke_free(Stroke *st);
bool stroke_push_pt(Stroke *st, float x, float y, float p);
void stroke_update_bbox(Stroke *st);

void page_init(Page *pg);
void page_free(Page *pg);
bool page_push_stroke(Page *pg, Stroke st);
bool page_undo(Page *pg);
bool page_redo(Page *pg);
void page_clear(Page *pg);
bool page_erase_at(Page *pg, float x, float y, float radius);

void doc_init_new(NoteDoc *doc, const char *folder, const char *id, const char *title,
                  PaperKind paper, ThemeKind theme);
void doc_free(NoteDoc *doc);

int  storage_init(const char *root_dir);
int  storage_list_folders(const char *root_dir, FolderMeta *out, int max_folders);
bool storage_create_folder(const char *root_dir, char *name_out, size_t name_sz);
bool storage_delete_folder(const char *root_dir, const char *folder);
int  storage_list(const char *root_dir, const char *folder, NoteMeta *out, int max_notes);
bool storage_save(const char *root_dir, const NoteDoc *doc);
bool storage_load(const char *root_dir, const char *folder, const char *id, NoteDoc *doc);
bool storage_delete(const char *root_dir, const char *folder, const char *id);
void storage_next_id(const char *root_dir, const char *folder,
                     char *id_out, size_t id_sz, char *title_out, size_t title_sz);

/* settings.c */
void settings_refresh(SettingsState *st);
void settings_scan_wifi(SettingsState *st);
void settings_open_osk(SettingsState *st, OskPurpose purpose, const char *prompt,
                       const char *initial, const char *target_ssid);
void settings_render(FB *fb, ThemeKind theme, const SettingsState *st);
int  settings_handle_tap(FB *fb, SettingsState *st, int x, int y, char *out_text, size_t out_sz);

#endif
