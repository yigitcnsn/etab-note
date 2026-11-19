#ifndef NOTEOS_OSK_H
#define NOTEOS_OSK_H

#include "types.h"

void           osk_open(OskState *osk, OskPurpose purpose, const char *prompt,
                        const char *initial, const char *target_ssid);
void           osk_render_full(FB *fb, ThemeKind theme, const OskState *osk);
void           osk_render_fast(FB *fb, ThemeKind theme, OskState *osk);
void           osk_release(FB *fb, ThemeKind theme, OskState *osk);
SettingsAction osk_handle_tap(FB *fb, OskState *osk, int x, int y,
                              OskPurpose *out_purpose, char *out_text, size_t out_sz,
                              char *out_ssid, size_t ssid_sz);

#endif
