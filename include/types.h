#ifndef NOTEOS_TYPES_H
#define NOTEOS_TYPES_H

#include <linux/fb.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef NOTEOS_VERSION
#define NOTEOS_VERSION "1.0.0"
#endif

#define STORAGE_DIR    "/data/noteos/notes"
#define IMAGES_DIR     "/data/noteos/images"
#define PREFS_PATH     "/data/noteos/prefs.conf"

#define TOOLBAR_H      58
#define SIDEBAR_W      320
#define MAX_NOTES      128
#define MAX_FOLDERS    64
#define MAX_PAGES      64
#define MAX_PAGE_IMGS  16
#define MAX_PAGE_TEXTS 32

#define RADIAL_INNER_R 44
#define RADIAL_OUTER_R 156
#define RADIAL_WEDGES  8

#define MAX_WIFI_NETS  16

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
    uint32_t bg;
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
    uint8_t tool;
    uint8_t color_idx;
    uint8_t size_idx;
    uint8_t _pad;
    uint32_t count;
    uint32_t cap;
    StrokePoint *pts;
    float min_x, min_y, max_x, max_y;
} Stroke;

typedef struct {
    char filename[64];   /* relative to IMAGES_DIR */
    float x, y, w, h;
    uint8_t *rgba;       /* cached RGBA8888 decoded pixels */
    int img_w, img_h;
} PageImage;

typedef struct {
    float x, y, w, h;
    int scale;
    char text[256];
} PageText;

typedef struct {
    Stroke *strokes;
    size_t count;
    size_t cap;
    Stroke *redo_strokes;
    size_t redo_count;
    size_t redo_cap;
    PageImage *images;
    size_t img_count;
    size_t img_cap;
    PageText *texts;
    size_t text_count;
    size_t text_cap;
} Page;

typedef struct {
    char folder[256];
    char id[64];
    char title[64];
    uint8_t paper;
    uint8_t theme;
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
    char filename[128];
    char title[128];
} MdMeta;

typedef struct {
    char name[128];
    char display[128];
    int item_count;
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
    int last_btn_x, last_btn_y, last_btn_w, last_btn_h;
    char last_btn_label[32];
} FB;

typedef struct {
    bool open;
    int cx, cy;
    int hover_wedge;
    int hover_color;
} RadialMenu;

typedef struct {
    char ssid[64];
    int signal;
    bool secured;
} WifiNet;

typedef enum {
    OSK_NONE          = 0,
    OSK_WIFI_PASS     = 1,
    OSK_RENAME_NOTE   = 2,
    OSK_NEW_NOTE      = 3,
    OSK_NEW_FOLDER    = 4,
    OSK_RENAME_FOLDER = 5,
    OSK_NEW_TEXTBOX   = 6,
    OSK_EDIT_TEXTBOX  = 7,
    OSK_NEW_MD        = 8,
    OSK_MD_ADD_TASK   = 9,
    OSK_MD_ADD_LINE   = 10
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
    int hi_x, hi_y, hi_w, hi_h;
    char hi_label[12];
    bool hi_active;
    bool need_full_keys;
} OskState;

typedef struct {
    bool open;
    char ssid[64];
    char ip[32];
    char wpa_state[32];
    char status_msg[96];
    WifiNet nets[MAX_WIFI_NETS];
    int net_count;
    int confirm_action; /* 0=none, 1=reboot, 2=recovery, 3=poweroff */
    OskState osk;
} SettingsState;

/* Action codes returned by settings & OSK tap handlers */
typedef enum {
    SET_ACT_NONE        = 0,
    SET_ACT_REDRAW      = 1,
    SET_ACT_CLOSE       = 2,
    SET_ACT_SLEEP       = 3,
    SET_ACT_OSK_DONE    = 4,
    SET_ACT_OSK_FAST    = 5,
    SET_ACT_CYCLE_THEME = 6,
    SET_ACT_CYCLE_PAPER = 7
} SettingsAction;

extern const uint32_t SWATCH_RGB[5];
extern const ThemePalette THEMES[THEME_COUNT];
extern const float PEN_SIZES[3];

#endif
