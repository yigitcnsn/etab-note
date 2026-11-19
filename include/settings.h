#ifndef NOTEOS_SETTINGS_H
#define NOTEOS_SETTINGS_H

#include "types.h"

void           settings_refresh(SettingsState *st);
void           settings_scan_wifi(SettingsState *st);
void           settings_connect_wifi(SettingsState *st, const char *ssid, const char *psk);
void           settings_render(FB *fb, ThemeKind theme, const SettingsState *st);
SettingsAction settings_handle_tap(FB *fb, SettingsState *st, int x, int y,
                                   OskPurpose *out_purpose, char *out_text, size_t out_sz);

#endif
