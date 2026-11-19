# E-Tab 5 Bare Note OS (`etab5-noteos`)

A standalone, bare-Linux note-taking OS written in C for the **General Mobile E-Tab 5** (MediaTek MT8392 / ARMv7, 10.1" 1920×1200 IPS display, Wacom I2C EMR stylus, 2 GB RAM, 32 GB eMMC).

Android (`zygote`, `surfaceflinger`, `system_server`, Dalvik/ART) is completely disabled at boot. The device boots directly from the stock Linux 3.4.67 kernel into a ~140 KB static ARMv7 binary rendering to `/dev/graphics/fb0`, using ~60 MB total system RAM (~1.95 GB free).

## Features
- **Direct Framebuffer Rendering:** Double-buffered dirty-rect renderer on 1920×1200 ARGB8888 (`/dev/graphics/fb0`).
- **Wacom EMR Stylus:** Sub-pixel coordinates (`21696×13560`), 1024-level pressure sensitivity, hover cursor, flip eraser, and hardware palm rejection (capacitive touch drives UI buttons only).
- **Stylus Radial Menu:** Side button opens an 8-wedge circular menu (`PEN`, `BRUSH`, `PENCIL`, `HI`, `ERASE`, `SIZE`, `UNDO`, `REDO`) + 5-color swatch bar (supports hold-glide-release and click-tap).
- **Notebooks, Folders & Vector Storage:** Atomic `.enote` vector files under `/data/noteos/notes/` with custom-named folders, note renaming, moving notes between folders, multi-page notebooks, object eraser, and unlimited per-page undo/redo.
- **Persistent Preferences:** Remembers theme (`LIGHT` / `PAPER` / `DARK`), paper template (`LINED` / `GRID` / `BLANK`), active tool/color/size, brightness, and last opened note across reboots.
- **Standalone OS Settings (`SET`):** Native Wi-Fi scanner/connector (`wpa_cli`) with fast partial-redraw on-screen QWERTY keyboard, brightness control, live battery/storage diagnostics, ADB-over-Wi-Fi control (`:5555`), and sleep/reboot/TWRP/power-off controls.
- **Standalone DHCPv4 Daemon:** Custom 28 KB static RFC 2131 DHCP client (`noteos-dhcp`) bringing up Wi-Fi automatically on boot without Android.
- **Zero-Config `etab.local` & Wi-Fi Drop:** Built-in mDNS responder (`etab.local`) + HTTP Drop server (`http://etab.local:8080`) to paste images or text from iPhone, iPad, or Mac straight onto the current note page (movable, resizable, and annotatable with stylus).
- **Obsidian-Style Markdown Visualizer & Engram iCloud Sync:** Native `.md` reader with YAML frontmatter, headings, tappable `- [ ]` / `- [x]` checklists, code blocks, blockquotes, nested folder navigation, Turkish UTF-8 glyph support, and two-way sync with iCloud `Engram Notes` (`./scripts/sync_engram.py`).

## Project Structure
```text
include/
  types.h       Core enums, themes, stroke/page/doc structs, overlay states
  app.h         Top-level App state & lifecycle API
  fb.h          Framebuffer driver, backlight, drawing primitives & 5x7 font
  render.h      Paper templates, AA ink/brush/pencil/highlighter & radial menu renderer
  storage.h     Stroke/Page/NoteDoc operations & atomic .enote / folder CRUD
  prefs.h       Persistent user preferences (/data/noteos/prefs.conf)
  osk.h         On-screen QWERTY keyboard (partial redraw + press highlight)
  settings.h    OS Settings page & wpa_cli Wi-Fi management
  ui.h          Toolbar, folder/note sidebar, move-note picker & radial actions
  input.h       Wacom EMR, capacitive touch & hardware key event loop
src/
  main.c        Minimal entrypoint & event loop
  app.c         App lifecycle, auto-save, redraw orchestration & sleep/wake
  fb.c          Framebuffer mmap, dirty-rect presentation, font & UI primitives
  render.c      Paper & anti-aliased stroke rasterizer + radial wheel rendering
  storage.c     Atomic binary .enote storage & folder management
  prefs.c       Load/save /data/noteos/prefs.conf
  osk.c         On-screen keyboard renderer & tap handler
  settings.c    Settings screen, safe exec wpa_cli Wi-Fi & system power controls
  ui.c          Toolbar, sidebar browser, move picker & tap router
  input.c       Linux evdev handlers for stylus, touch & physical keys
daemons/
  dhcpc.c       Standalone static RFC 2131 DHCPv4 client daemon (noteos-dhcp)
scripts/
  build_bootimg.py  Pure-Python MediaTek bootimg repacker
  deploy.sh         Hot-deploy /data/noteos/noteos over ADB
PLAN.md         Phased roadmap
LOG.md          Chronological build & hardware command log
```

## Building & Deploying
Requires `zig` (static ARMv7 C cross-compiler) and `adb`:

```bash
# Build static ARMv7 binaries (build/noteos + build/noteos-dhcp)
make

# Hot-deploy updated noteos binary to the tablet over ADB (no reflash needed)
make deploy

# Repack standalone MediaTek bootimg (requires backups/bootimg.img)
make bootimg

# Drop current macOS clipboard (image or text) or an image file onto the tablet
./scripts/drop.sh
./scripts/drop.sh photo.png

# Sync with macOS iCloud Engram Notes (--fresh wipes tablet notes first)
./scripts/sync_engram.py --fresh
./scripts/sync_engram.py
```

## Roadmap
- [x] Hardware discovery, SHA-256 verified backups (`bootimg` & `recovery`), framebuffer + Wacom validation
- [x] Core Note OS (`noteos`): pens/brush/pencil/highlighter, radial menu, folders, move/rename, persistent prefs, OS settings & Wi-Fi
- [x] Standalone `bootimg` (0% Android, boots straight into `noteos` + Wi-Fi + `noteos-dhcp` + `adbd :5555`)
- [ ] Raspberry Pi & Engram iCloud sync bridge + lightweight Home Assistant panel
