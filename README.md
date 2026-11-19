# E-Tab 5 Bare Note OS (`etab5-noteos`)

A lightweight, bare-Linux note-taking OS built in C for the **General Mobile E-Tab 5** (MediaTek MT8392 / ARMv7, 10.1" 1920×1200 IPS display, Wacom I2C EMR stylus, 2 GB RAM, 32 GB eMMC).

Instead of running bloated Android, `noteos` renders directly to the Linux framebuffer (`/dev/graphics/fb0`) and reads raw input events from the Wacom EMR digitizer (`/dev/input/event1`), capacitive touch (`/dev/input/event4`), and hardware keys (`/dev/input/event0`).

## Features
- **Direct Framebuffer Rendering:** Double-buffered dirty-rect renderer on 1920×1200 ARGB8888 (`/dev/graphics/fb0`).
- **Wacom EMR Stylus Support:** Sub-pixel coordinates (`21696×13560`), 1024-level pressure sensitivity, hover cursor, flip eraser, and **100% hardware palm rejection** (capacitive touch drives UI buttons only and never draws on canvas).
- **Stylus Button Radial Menu:** Press the stylus side button for an 8-wedge circular quick menu (`PEN`, `BRUSH`, `PENCIL`, `HI`, `ERASE`, `SIZE`, `UNDO`, `REDO`) + 5-color swatch bar (supports both hold-glide-release and click-tap).
- **Notebooks, Folders & Multi-Page Storage:** Vector strokes saved atomically in `.enote` files under `/data/noteos/notes/`, with folders, note renaming, unlimited per-page undo/redo, and object eraser.
- **Themes & Paper Templates:** Light, Parchment, and Obsidian dark themes; Lined, Grid, and Blank paper.
- **Standalone OS Settings (`SET`):** Native Wi-Fi scanner/connector (`wpa_cli`) with built-in on-screen QWERTY keyboard, brightness control, live battery/storage diagnostics, ADB-over-Wi-Fi control (`:5555`), and sleep/reboot/TWRP/power-off controls.

## Project Structure
```text
src/
  noteos.h      Core data structures & shared interfaces
  main.c        Input loop (Wacom/touch/keys), UI layout, radial menu, folders
  fb.c          Framebuffer driver, AA ink/brush/pencil renderer, radial wheel
  storage.c     Atomic binary .enote vector storage & folder management
  settings.c    Wi-Fi (wpa_cli), OS settings page & on-screen QWERTY keyboard
  proto.c       Early single-file hardware validation prototype
scripts/
  run_noteos.sh Push & run live on device with Android paused
  run_proto.sh  Push & run prototype
PLAN.md         Phased roadmap
LOG.md          Chronological build & hardware discovery log
```

## Building & Running (macOS / Linux)
Requires `zig` (used as a zero-setup static ARMv7 C cross-compiler) and `adb`:

```bash
# Build static ARMv7 binary (build/noteos)
make

# Push to tablet over ADB
make push

# Test live on /dev/graphics/fb0 (pauses Android, restores on EXIT)
./scripts/run_noteos.sh
```

## Roadmap
- [x] Hardware discovery, partition backups (`bootimg` & `recovery`), framebuffer + Wacom prototype
- [x] Core Note OS (`noteos`): pens/brush/pencil/highlighter, radial menu, folders, multi-page vector storage, themes, OS settings & Wi-Fi
- [ ] Standalone `bootimg` (strip Android completely, boot straight into `noteos` + Wi-Fi + `adbd :5555`)
- [ ] Sync bridge with Raspberry Pi & Engram notes + lightweight Home Assistant panel
