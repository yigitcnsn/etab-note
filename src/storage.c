#define _GNU_SOURCE
#include "noteos.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ENOTE_MAGIC   0x454E4F54u /* "ENOT" */
#define ENOTE_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    char title[64];
    uint8_t paper;
    uint8_t theme;
    uint16_t page_count;
    uint16_t cur_page;
    uint16_t _pad;
} EnoteHeader;

typedef struct {
    uint8_t tool;
    uint8_t color_idx;
    uint8_t size_idx;
    uint8_t _pad;
    uint32_t point_count;
} EnoteStrokeHeader;

void stroke_free(Stroke *st) {
    free(st->pts);
    memset(st, 0, sizeof(*st));
}

bool stroke_push_pt(Stroke *st, float x, float y, float p) {
    if (st->count >= st->cap) {
        uint32_t ncap = st->cap ? st->cap * 2 : 64;
        StrokePoint *npts = realloc(st->pts, (size_t)ncap * sizeof(StrokePoint));
        if (!npts) return false;
        st->pts = npts;
        st->cap = ncap;
    }
    st->pts[st->count].x = x;
    st->pts[st->count].y = y;
    st->pts[st->count].p = p;
    if (st->count == 0) {
        st->min_x = st->max_x = x;
        st->min_y = st->max_y = y;
    } else {
        if (x < st->min_x) st->min_x = x;
        if (x > st->max_x) st->max_x = x;
        if (y < st->min_y) st->min_y = y;
        if (y > st->max_y) st->max_y = y;
    }
    st->count++;
    return true;
}

void stroke_update_bbox(Stroke *st) {
    if (st->count == 0) {
        st->min_x = st->min_y = st->max_x = st->max_y = 0;
        return;
    }
    st->min_x = st->max_x = st->pts[0].x;
    st->min_y = st->max_y = st->pts[0].y;
    for (uint32_t i = 1; i < st->count; i++) {
        float x = st->pts[i].x, y = st->pts[i].y;
        if (x < st->min_x) st->min_x = x;
        if (x > st->max_x) st->max_x = x;
        if (y < st->min_y) st->min_y = y;
        if (y > st->max_y) st->max_y = y;
    }
}

void page_init(Page *pg) {
    memset(pg, 0, sizeof(*pg));
}

static void clear_redo(Page *pg) {
    for (size_t i = 0; i < pg->redo_count; i++) {
        stroke_free(&pg->redo_strokes[i]);
    }
    pg->redo_count = 0;
}

void page_free(Page *pg) {
    for (size_t i = 0; i < pg->count; i++) {
        stroke_free(&pg->strokes[i]);
    }
    free(pg->strokes);
    clear_redo(pg);
    free(pg->redo_strokes);
    memset(pg, 0, sizeof(*pg));
}

bool page_push_stroke(Page *pg, Stroke st) {
    clear_redo(pg);
    if (pg->count >= pg->cap) {
        size_t ncap = pg->cap ? pg->cap * 2 : 64;
        Stroke *ns = realloc(pg->strokes, ncap * sizeof(Stroke));
        if (!ns) {
            stroke_free(&st);
            return false;
        }
        pg->strokes = ns;
        pg->cap = ncap;
    }
    pg->strokes[pg->count++] = st;
    return true;
}

static bool push_redo(Page *pg, Stroke st) {
    if (pg->redo_count >= pg->redo_cap) {
        size_t ncap = pg->redo_cap ? pg->redo_cap * 2 : 32;
        Stroke *ns = realloc(pg->redo_strokes, ncap * sizeof(Stroke));
        if (!ns) {
            stroke_free(&st);
            return false;
        }
        pg->redo_strokes = ns;
        pg->redo_cap = ncap;
    }
    pg->redo_strokes[pg->redo_count++] = st;
    return true;
}

bool page_undo(Page *pg) {
    if (pg->count == 0) return false;
    Stroke st = pg->strokes[--pg->count];
    push_redo(pg, st);
    return true;
}

bool page_redo(Page *pg) {
    if (pg->redo_count == 0) return false;
    Stroke st = pg->redo_strokes[--pg->redo_count];
    if (pg->count >= pg->cap) {
        size_t ncap = pg->cap ? pg->cap * 2 : 64;
        Stroke *ns = realloc(pg->strokes, ncap * sizeof(Stroke));
        if (!ns) {
            stroke_free(&st);
            return false;
        }
        pg->strokes = ns;
        pg->cap = ncap;
    }
    pg->strokes[pg->count++] = st;
    return true;
}

void page_clear(Page *pg) {
    for (size_t i = 0; i < pg->count; i++) {
        stroke_free(&pg->strokes[i]);
    }
    pg->count = 0;
    clear_redo(pg);
}

static float point_seg_dist2(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    float l2 = dx * dx + dy * dy;
    if (l2 < 1e-4f) {
        float ex = px - ax, ey = py - ay;
        return ex * ex + ey * ey;
    }
    float t = ((px - ax) * dx + (py - ay) * dy) / l2;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float cx = ax + t * dx, cy = ay + t * dy;
    float ex = px - cx, ey = py - cy;
    return ex * ex + ey * ey;
}

bool page_erase_at(Page *pg, float x, float y, float radius) {
    bool erased = false;
    float r2 = radius * radius;
    size_t i = 0;
    while (i < pg->count) {
        Stroke *st = &pg->strokes[i];
        float pad = radius + 8.0f;
        if (x < st->min_x - pad || x > st->max_x + pad ||
            y < st->min_y - pad || y > st->max_y + pad) {
            i++;
            continue;
        }
        bool hit = false;
        if (st->count == 1) {
            float dx = x - st->pts[0].x, dy = y - st->pts[0].y;
            if (dx * dx + dy * dy <= r2) hit = true;
        } else {
            for (uint32_t j = 1; j < st->count; j++) {
                if (point_seg_dist2(x, y, st->pts[j-1].x, st->pts[j-1].y,
                                    st->pts[j].x, st->pts[j].y) <= r2) {
                    hit = true;
                    break;
                }
            }
        }
        if (hit) {
            Stroke removed = *st;
            memmove(&pg->strokes[i], &pg->strokes[i + 1], (pg->count - i - 1) * sizeof(Stroke));
            pg->count--;
            push_redo(pg, removed);
            erased = true;
        } else {
            i++;
        }
    }
    return erased;
}

void doc_init_new(NoteDoc *doc, const char *folder, const char *id, const char *title,
                  PaperKind paper, ThemeKind theme) {
    memset(doc, 0, sizeof(*doc));
    if (folder && folder[0]) snprintf(doc->folder, sizeof(doc->folder), "%s", folder);
    snprintf(doc->id, sizeof(doc->id), "%s", id);
    snprintf(doc->title, sizeof(doc->title), "%s", title);
    doc->paper = (uint8_t)paper;
    doc->theme = (uint8_t)theme;
    doc->page_count = 1;
    doc->cur_page = 0;
    page_init(&doc->pages[0]);
    doc->dirty = true;
}

void doc_free(NoteDoc *doc) {
    for (int i = 0; i < doc->page_count; i++) {
        page_free(&doc->pages[i]);
    }
    doc->page_count = 0;
    doc->cur_page = 0;
}

static int mkdir_p(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len == 0) return -1;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) < 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) < 0 && errno != EEXIST) return -1;
    return 0;
}

static void build_dir_path(char *out, size_t sz, const char *root_dir, const char *folder) {
    if (folder && folder[0]) {
        snprintf(out, sz, "%s/%s", root_dir, folder);
    } else {
        snprintf(out, sz, "%s", root_dir);
    }
}

int storage_init(const char *root_dir) {
    return mkdir_p(root_dir);
}

static int count_notes_in(const char *dir_path) {
    DIR *d = opendir(dir_path);
    if (!d) return 0;
    int c = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len > 6 && strcmp(ent->d_name + len - 6, ".enote") == 0) c++;
    }
    closedir(d);
    return c;
}

static int cmp_folder(const void *a, const void *b) {
    const FolderMeta *fa = (const FolderMeta *)a;
    const FolderMeta *fb = (const FolderMeta *)b;
    return strcmp(fa->name, fb->name);
}

int storage_list_folders(const char *root_dir, FolderMeta *out, int max_folders) {
    DIR *d = opendir(root_dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && n < max_folders) {
        if (ent->d_name[0] == '.') continue;
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", root_dir, ent->d_name);
        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(out[n].name, sizeof(out[n].name), "%s", ent->d_name);
            /* Pretty display name: replace '_' with ' ' and uppercase */
            size_t j = 0;
            for (; ent->d_name[j] && j + 1 < sizeof(out[n].display); j++) {
                char c = ent->d_name[j];
                out[n].display[j] = (c == '_') ? ' ' : (char)toupper((unsigned char)c);
            }
            out[n].display[j] = '\0';
            out[n].note_count = count_notes_in(full);
            n++;
        }
    }
    closedir(d);
    if (n > 1) qsort(out, (size_t)n, sizeof(FolderMeta), cmp_folder);
    return n;
}

static void sanitize_folder_name(const char *in, char *out, size_t sz) {
    size_t j = 0;
    for (size_t i = 0; in && in[i] && j + 1 < sz; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c) || c == '-' || c == '_') {
            out[j++] = (char)c;
        } else if (c == ' ') {
            out[j++] = '_';
        }
    }
    while (j > 0 && out[j - 1] == '_') j--;
    out[j] = '\0';
}

bool storage_create_folder(const char *root_dir, const char *display_name, char *name_out, size_t name_sz) {
    char name[64] = {0};
    sanitize_folder_name(display_name, name, sizeof(name));
    if (name[0] == '\0') {
        FolderMeta folders[MAX_FOLDERS];
        int n = storage_list_folders(root_dir, folders, MAX_FOLDERS);
        int max_num = 0;
        for (int i = 0; i < n; i++) {
            int num = 0;
            if (sscanf(folders[i].name, "folder_%d", &num) == 1 && num > max_num) {
                max_num = num;
            }
        }
        snprintf(name, sizeof(name), "folder_%02d", max_num + 1);
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", root_dir, name);
    if (mkdir(path, 0755) != 0 && errno != EEXIST) return false;
    if (name_out && name_sz > 0) snprintf(name_out, name_sz, "%s", name);
    return true;
}

bool storage_rename_folder(const char *root_dir, const char *old_name, const char *new_display,
                           char *new_name_out, size_t new_name_sz) {
    if (!old_name || !old_name[0]) return false;
    char clean[64] = {0};
    sanitize_folder_name(new_display, clean, sizeof(clean));
    if (clean[0] == '\0' || strcmp(clean, old_name) == 0) return false;

    char old_path[512], new_path[512];
    snprintf(old_path, sizeof(old_path), "%s/%s", root_dir, old_name);
    snprintf(new_path, sizeof(new_path), "%s/%s", root_dir, clean);
    struct stat st;
    if (stat(new_path, &st) == 0) return false; /* already exists */
    if (rename(old_path, new_path) != 0) return false;
    if (new_name_out && new_name_sz > 0) snprintf(new_name_out, new_name_sz, "%s", clean);
    return true;
}

bool storage_delete_folder(const char *root_dir, const char *folder) {
    if (!folder || !folder[0]) return false;
    char dir_path[512];
    build_dir_path(dir_path, sizeof(dir_path), root_dir, folder);
    DIR *d = opendir(dir_path);
    if (!d) return false;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char fp[512];
        snprintf(fp, sizeof(fp), "%s/%s", dir_path, ent->d_name);
        unlink(fp);
    }
    closedir(d);
    return rmdir(dir_path) == 0;
}

static int cmp_meta(const void *a, const void *b) {
    const NoteMeta *ma = (const NoteMeta *)a;
    const NoteMeta *mb = (const NoteMeta *)b;
    return strcmp(mb->id, ma->id); /* newest first */
}

int storage_list(const char *root_dir, const char *folder, NoteMeta *out, int max_notes) {
    char dir_path[512];
    build_dir_path(dir_path, sizeof(dir_path), root_dir, folder);
    DIR *d = opendir(dir_path);
    if (!d) return 0;
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && n < max_notes) {
        size_t len = strlen(ent->d_name);
        if (len <= 6 || strcmp(ent->d_name + len - 6, ".enote") != 0) continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir_path, ent->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        EnoteHeader hdr;
        if (fread(&hdr, sizeof(hdr), 1, f) == 1 && hdr.magic == ENOTE_MAGIC) {
            size_t stem_len = len - 6;
            if (stem_len >= sizeof(out[n].id)) stem_len = sizeof(out[n].id) - 1;
            memcpy(out[n].id, ent->d_name, stem_len);
            out[n].id[stem_len] = '\0';
            snprintf(out[n].title, sizeof(out[n].title), "%s", hdr.title);
            out[n].page_count = hdr.page_count > 0 ? hdr.page_count : 1;
            n++;
        }
        fclose(f);
    }
    closedir(d);
    if (n > 1) qsort(out, (size_t)n, sizeof(NoteMeta), cmp_meta);
    return n;
}

bool storage_save(const char *root_dir, const NoteDoc *doc) {
    char dir_path[512];
    build_dir_path(dir_path, sizeof(dir_path), root_dir, doc->folder);
    mkdir_p(dir_path);

    char tmp_path[512], final_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s/%s.enote.tmp", dir_path, doc->id);
    snprintf(final_path, sizeof(final_path), "%s/%s.enote", dir_path, doc->id);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) return false;

    EnoteHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = ENOTE_MAGIC;
    hdr.version = ENOTE_VERSION;
    snprintf(hdr.title, sizeof(hdr.title), "%s", doc->title);
    hdr.paper = doc->paper;
    hdr.theme = doc->theme;
    hdr.page_count = (uint16_t)doc->page_count;
    hdr.cur_page = (uint16_t)doc->cur_page;

    if (fwrite(&hdr, sizeof(hdr), 1, f) != 1) goto fail;

    for (int p = 0; p < doc->page_count; p++) {
        uint32_t sc = (uint32_t)doc->pages[p].count;
        if (fwrite(&sc, sizeof(sc), 1, f) != 1) goto fail;
        for (uint32_t s = 0; s < sc; s++) {
            const Stroke *st = &doc->pages[p].strokes[s];
            EnoteStrokeHeader sh;
            sh.tool = st->tool;
            sh.color_idx = st->color_idx;
            sh.size_idx = st->size_idx;
            sh._pad = 0;
            sh.point_count = st->count;
            if (fwrite(&sh, sizeof(sh), 1, f) != 1) goto fail;
            if (st->count > 0) {
                if (fwrite(st->pts, sizeof(StrokePoint), st->count, f) != st->count) goto fail;
            }
        }
    }

    fflush(f);
    fsync(fileno(f));
    fclose(f);
    if (rename(tmp_path, final_path) != 0) {
        unlink(tmp_path);
        return false;
    }
    return true;

fail:
    fclose(f);
    unlink(tmp_path);
    return false;
}

bool storage_load(const char *root_dir, const char *folder, const char *id, NoteDoc *doc) {
    char dir_path[512], path[512];
    build_dir_path(dir_path, sizeof(dir_path), root_dir, folder);
    snprintf(path, sizeof(path), "%s/%s.enote", dir_path, id);
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    EnoteHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 || hdr.magic != ENOTE_MAGIC || hdr.version != ENOTE_VERSION) {
        fclose(f);
        return false;
    }

    doc_free(doc);
    memset(doc, 0, sizeof(*doc));
    if (folder && folder[0]) snprintf(doc->folder, sizeof(doc->folder), "%s", folder);
    snprintf(doc->id, sizeof(doc->id), "%s", id);
    snprintf(doc->title, sizeof(doc->title), "%s", hdr.title);
    doc->paper = hdr.paper < PAPER_COUNT ? hdr.paper : PAPER_LINED;
    doc->theme = hdr.theme < THEME_COUNT ? hdr.theme : THEME_PARCHMENT;
    doc->page_count = hdr.page_count;
    if (doc->page_count < 1) doc->page_count = 1;
    if (doc->page_count > MAX_PAGES) doc->page_count = MAX_PAGES;
    doc->cur_page = hdr.cur_page < doc->page_count ? hdr.cur_page : 0;

    for (int p = 0; p < doc->page_count; p++) {
        page_init(&doc->pages[p]);
        uint32_t sc = 0;
        if (fread(&sc, sizeof(sc), 1, f) != 1) break;
        for (uint32_t s = 0; s < sc; s++) {
            EnoteStrokeHeader sh;
            if (fread(&sh, sizeof(sh), 1, f) != 1) break;
            Stroke st;
            memset(&st, 0, sizeof(st));
            st.tool = sh.tool;
            st.color_idx = sh.color_idx < 5 ? sh.color_idx : 0;
            st.size_idx = sh.size_idx < 3 ? sh.size_idx : 1;
            if (sh.point_count > 0 && sh.point_count < 1000000u) {
                st.pts = malloc((size_t)sh.point_count * sizeof(StrokePoint));
                if (!st.pts) break;
                if (fread(st.pts, sizeof(StrokePoint), sh.point_count, f) != sh.point_count) {
                    free(st.pts);
                    break;
                }
                st.count = sh.point_count;
                st.cap = sh.point_count;
                stroke_update_bbox(&st);
                page_push_stroke(&doc->pages[p], st);
            }
        }
    }

    fclose(f);
    doc->dirty = false;
    return true;
}

bool storage_delete(const char *root_dir, const char *folder, const char *id) {
    char dir_path[512], path[512];
    build_dir_path(dir_path, sizeof(dir_path), root_dir, folder);
    snprintf(path, sizeof(path), "%s/%s.enote", dir_path, id);
    return unlink(path) == 0;
}

void storage_next_id(const char *root_dir, const char *folder,
                     char *id_out, size_t id_sz, char *title_out, size_t title_sz) {
    NoteMeta metas[MAX_NOTES];
    int n = storage_list(root_dir, folder, metas, MAX_NOTES);
    int max_num = 0;
    for (int i = 0; i < n; i++) {
        int num = 0;
        if (sscanf(metas[i].id, "note_%d", &num) == 1 && num > max_num) {
            max_num = num;
        }
    }
    int next = max_num + 1;
    snprintf(id_out, id_sz, "note_%04d", next);
    snprintf(title_out, title_sz, "NOTE %d", next);
}
