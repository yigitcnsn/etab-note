#ifndef NOTEOS_STORAGE_H
#define NOTEOS_STORAGE_H

#include "types.h"

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
bool page_add_image(Page *pg, const char *filename, float x, float y, float w, float h);
void page_remove_image(Page *pg, size_t idx);
bool page_ensure_image_loaded(PageImage *img);
bool page_add_text(Page *pg, float x, float y, float w, int scale, const char *text);
void page_remove_text(Page *pg, size_t idx);
void page_text_recalc_height(PageText *pt);

void doc_init_new(NoteDoc *doc, const char *folder, const char *id, const char *title,
                  PaperKind paper, ThemeKind theme);
void doc_free(NoteDoc *doc);

int  storage_init(const char *root_dir);
int  storage_list_folders(const char *root_dir, const char *parent_folder,
                          FolderMeta *out, int max_folders);
bool storage_create_folder(const char *root_dir, const char *parent_folder,
                           const char *display_name, char *name_out, size_t name_sz);
bool storage_rename_folder(const char *root_dir, const char *old_rel_path,
                           const char *new_display, char *new_rel_out, size_t new_rel_sz);
bool storage_delete_folder(const char *root_dir, const char *folder);

int  storage_list(const char *root_dir, const char *folder, NoteMeta *out, int max_notes);
int  storage_list_md(const char *root_dir, const char *folder, MdMeta *out, int max_md);
bool storage_delete_md(const char *root_dir, const char *folder, const char *filename);
bool storage_save(const char *root_dir, const NoteDoc *doc);
bool storage_load(const char *root_dir, const char *folder, const char *id, NoteDoc *doc);
bool storage_move_note(const char *root_dir, const char *old_folder, const char *id,
                       const char *new_folder, char *new_id_out, size_t new_id_sz);
bool storage_delete(const char *root_dir, const char *folder, const char *id);
void storage_next_id(const char *root_dir, const char *folder,
                     char *id_out, size_t id_sz, char *title_out, size_t title_sz);

#endif
