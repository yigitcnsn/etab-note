#ifndef NOTEOS_APP_H
#define NOTEOS_APP_H

#include "drop.h"
#include "mdns.h"
#include "types.h"
#include <time.h>

typedef enum {
    OBJ_DRAG_NONE   = 0,
    OBJ_DRAG_MOVE   = 1,
    OBJ_DRAG_RESIZE = 2
} ObjDragMode;

typedef struct {
    FB fb;
    NoteDoc doc;

    /* Folder browser state */
    char cur_folder[64];
    FolderMeta folders[MAX_FOLDERS];
    int folder_count;
    NoteMeta notes[MAX_NOTES];
    int note_count;

    /* UI flags */
    bool sidebar_open;
    bool move_mode;
    bool obj_edit_mode;
    int selected_img;       /* index in current page images, or -1 */
    int selected_text;      /* index in current page texts, or -1 */
    ObjDragMode obj_drag;
    float drag_off_x, drag_off_y;
    float drag_aspect;
    char toast_msg[64];
    time_t toast_until;
    bool confirm_delete;
    bool confirm_clear;
    bool sleeping;
    int saved_brightness;
    bool quit;

    /* Network services (mDNS etab.local + HTTP Drop :8080) */
    MdnsState mdns;
    DropServer drop;

    /* Drawing tool state */
    ToolKind tool;
    uint8_t color_idx;
    uint8_t size_idx;

    /* Persisted preferences */
    PaperKind pref_paper;
    ThemeKind pref_theme;
    char pref_folder[64];
    char pref_note[64];

    /* Overlays */
    RadialMenu radial;
    bool radial_opened_by_hold;
    SettingsState settings;

    /* Active stroke */
    bool drawing;
    Stroke active_stroke;
    float smooth_p;

    /* Wacom EMR stylus */
    int fd_pen;
    int max_x, max_y, max_p;
    int raw_x, raw_y, raw_p;
    bool tool_pen, tool_rubber, btn_stylus, btn_stylus_prev, pen_touch;
    bool pen_was_down;

    /* Capacitive touch (UI only) */
    int fd_touch;
    int touch_x, touch_y;
    bool touch_down, touch_was_down;

    /* Hardware keys */
    int fd_key;

    time_t last_status_tick;
} App;

int  app_canvas_clip_x0(const App *app);
void app_pretty_folder_name(const char *name, char *out, size_t sz);
void app_refresh_browser(App *app);
void app_save_current(App *app);
void app_open_or_create_first_note(App *app);
void app_finish_active_stroke(App *app);
void app_redraw_all(App *app);
void app_toggle_sleep(App *app);

#endif
