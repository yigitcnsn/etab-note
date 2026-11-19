# E-Tab 5 Bare Note OS (`etab5-noteos`)

Turn a General Mobile E-Tab 5 (MediaTek MT8392 / ARMv7, 10.1" 1920×1200 IPS, Wacom I2C EMR stylus, 2 GB RAM, 32 GB eMMC) into a dedicated, fast, bare-Linux note-taking & smart-home control tablet.

## Hardware Summary
- **Kernel:** Linux 3.4.67 (`armv7l`, SMP PREEMPT)
- **Display:** `/dev/graphics/fb0` (1920×1200 @ 32bpp, triple-buffered `1920x3600`)
- **Stylus:** `Wacom I2C Digitizer` (`/dev/input/event1`, Vendor `056a` — X, Y, Pressure, Pen/Rubber/Side buttons)
- **Touch:** `mtk-tpd` (`/dev/input/event4`, separate device for hardware palm rejection)
- **Keys:** `mtk-kpd` (`/dev/input/event0`)
- **Storage:** `usrdata` (`/dev/block/mmcblk0p7`, ~27 GB), `bootimg` (10 MB), `recovery` (10 MB, TWRP kept intact)

## Architecture
- **Keep:** Stock Linux 3.4.67 kernel + TWRP in `recovery` (safety net).
- **Replace:** Strip Android userspace completely; boot a minimal static Linux `init` directly into a single full-screen C app rendered to `/dev/graphics/fb0`.
- **Features:**
  - Low-latency pressure-sensitive note-taking with hardware palm rejection, eraser, undo/redo, notebooks & pages, auto-save on `usrdata`.
  - Wi-Fi sync of notes to local Raspberry Pi.
  - Lightweight native Home Assistant control panel (REST/WebSocket API; no browser/video).
  - Power-button sleep/wake + battery gauge.

## Phases
- [x] **Phase 0 — Discovery & Setup:** Connect via ADB, verify root/hardware/battery, create repo & command log.
- [x] **Phase 1 — Backup & Hardware Probe:** Dump `bootimg` & `recovery`, read exact Wacom axis/pressure ranges, framebuffer layout (`FBIOGET_VSCREENINFO`), and backlight/sleep sysfs nodes. Install `zig` for static `arm-linux-musleabihf` cross-compilation.
- [x] **Phase 2 — Live Prototype (No Flashing):** Pause Android (`stop`), run static ARMv7 prototype on `/dev/graphics/fb0` + `/dev/input/event1` to tune ink, pressure, eraser, palm rejection, and UI.
- [x] **Phase 3 — v1 Note App & OS Settings:** Notebooks/folders/pages, stylus radial menu, brush/pencil/highlighter, Wi-Fi/OS settings & on-screen keyboard, power/sleep management.
- [x] **Phase 4 — Standalone OS Flash:** Repack stock kernel + minimal static Linux initramfs into a new `bootimg` and flash `bootimg` only (leaving `recovery`/TWRP untouched).
- [ ] **Phase 5 — Pi Sync, Engram Bridge & Home Assistant:** Always-on Pi note sync, macOS iCloud/PencilKit bridge, and lightweight native Home Assistant controls.
