# Step-by-Step Build & Command Log

Every action and command executed on the Mac and tablet is logged here chronologically.

---

## 2026-11-19 — Phase 0: Hardware Discovery, ADB Setup & Battery Diagnostics

### Step 1: Detect tablet on macOS USB bus
- **Commands:**
  ```bash
  ioreg -p IOUSB -w0
  ioreg -n E-tab5 -r -l -w0
  ```
- **Result:** Found `E-tab5@20231000` (`General Mobile`, `idVendor=0x0bb4`, `idProduct=0x0c02`, serial `FXICBUF821BC8NA`) connected via Dell dock. Initially exposed `MTP@0`; after USB debugging verified, also exposed ADB interface (`IOUSBHostInterface@1`, class `0xff/0x42/0x01`).

### Step 2: Install Android Platform Tools (`adb`, `fastboot`)
- **Commands:**
  ```bash
  brew install --cask android-platform-tools
  ```
- **Result:** Installed `android-platform-tools` `37.0.1` (`/opt/homebrew/bin/adb`).

### Step 3: Start ADB server & authorize device
- **Commands:**
  ```bash
  adb kill-server
  adb devices -l
  ```
- **Result:** Detected `FXICBUF821BC8NA` over USB; user tapped **Allow** on the tablet's USB debugging prompt.

### Step 4: Inspect kernel, input devices, display, root, and partitions
- **Commands:**
  ```bash
  adb shell "uname -a; getprop ro.build.version.release; getprop ro.product.model; cat /proc/bus/input/devices; ls -l /dev/graphics/ /dev/fb*; cat /proc/meminfo | head -n 3"
  adb shell "su -c 'id; cat /sys/class/graphics/fb0/virtual_size; cat /sys/class/graphics/fb0/bits_per_pixel; ls -l /dev/block/platform/mtk-msdc.0/by-name/ 2>/dev/null || cat /proc/dumchar_info'"
  ```
- **Result:**
  - Kernel: `Linux 3.4.67 #1 SMP PREEMPT armv7l`
  - ROM: Android `7.1.2` (`GeneralMobile hulkbuser`), root confirmed (`uid=0(root)`)
  - RAM: `2011816 kB` (~2 GB)
  - Framebuffer: `/dev/graphics/fb0`, `1920x3600` virtual (1920×1200 triple-buffered), `32 bpp`
  - Input devices:
    - `event0`: `mtk-kpd` (hardware keys)
    - `event1`: `Wacom I2C Digitizer` (`Vendor=056a`, `EV=b`, `ABS=1000003` -> X/Y/Pressure, `KEY=1c03` -> Pen/Rubber/Buttons)
    - `event4`: `mtk-tpd` (capacitive multi-touch)
  - Partitions (`/proc/dumchar_info`): `bootimg` (10 MB @ `0x01da0000`), `recovery` (10 MB @ `0x027a0000`), `android` (1 GB @ `mmcblk0p5`), `cache` (512 MB @ `mmcblk0p6`), `usrdata` (~27.5 GB @ `mmcblk0p7`).

### Step 5: Diagnose battery & move to 2.0A powered hub + Wi-Fi ADB
- **Context:** Tablet shut off on 500 mA Dell dock port with battery stuck at 0–1%. Moved to externally powered 2.0A hub port (charge-only) and connected over Wi-Fi ADB.
- **Commands:**
  ```bash
  adb connect 192.168.1.185:5555
  adb shell "cat /sys/class/power_supply/battery/uevent; echo ---; cat /sys/class/power_supply/ac/uevent 2>/dev/null; echo ---; cat /sys/class/power_supply/usb/uevent 2>/dev/null"
  ```
- **Result:** Battery healthy (`HEALTH=Good`, `TEMP=30.0°C`, `BATT_VOL=3428 mV`), actively charging as `ac` (`ONLINE=1`) at `~1003 mA`.

### Step 6: Check local cross-compilation toolchains & create project repo
- **Commands:**
  ```bash
  which clang gcc rustc cargo zig docker podman brew
  ```
- **Result:** Xcode `clang`/`gcc` and `brew` available; `zig` not yet installed. Created `/Volumes/X/developer/etab5-noteos` with `PLAN.md`, `LOG.md`, and `.gitignore`.

---

## 2026-11-19 — Phase 1: Cross-Compiler, Hardware Probe & Partition Backups

### Step 7: Install Zig (`zig cc`) for static ARMv7 Linux cross-compilation
- **Commands:**
  ```bash
  brew install zig
  ```
- **Result:** Installed `zig 0.17.1` (along with `llvm@22`, `lld@22`) at `/opt/homebrew/bin/zig`.

### Step 8: Reconnect Wi-Fi ADB & probe battery, Wacom digitizer, touch, keys, backlight, power, and Wi-Fi
- **Commands:**
  ```bash
  adb connect 192.168.1.185:5555
  adb shell "su -c 'cat /sys/class/power_supply/battery/capacity /sys/class/power_supply/battery/batt_vol /sys/class/power_supply/battery/BatteryAverageCurrent; echo ===INPUT===; getevent -p /dev/input/event1; echo ===TOUCH===; getevent -p /dev/input/event4; echo ===KEYS===; getevent -p /dev/input/event0; echo ===LEDS===; ls /sys/class/leds/; cat /sys/class/leds/lcd-backlight/max_brightness 2>/dev/null; echo ===POWER===; cat /sys/power/state; echo ===DEV===; ls -l /dev/bootimg /dev/recovery /dev/block/mmcblk0; echo ===NET===; ip addr show wlan0 2>/dev/null; lsmod'"
  ```
- **Result:**
  - **Battery:** `18%`, `3903 mV`, charging (`+314 mA` with screen on).
  - **Wacom I2C Digitizer (`/dev/input/event1`):**
    - `ABS_X (0x00)`: `0..21696` (11.3 counts/px horizontally)
    - `ABS_Y (0x01)`: `0..13560` (11.3 counts/px vertically)
    - `ABS_PRESSURE (0x18)`: `0..1023`
    - `KEY`: `0x140` (`BTN_TOOL_PEN`), `0x141` (`BTN_TOOL_RUBBER`), `0x14a` (`BTN_TOUCH`), `0x14b` (`BTN_STYLUS`), `0x14c` (`BTN_STYLUS2`)
  - **Capacitive Touch (`/dev/input/event4`):** `mtk-tpd`, `0..1920` × `0..1200`, 10-finger multitouch
  - **Hardware Keys (`/dev/input/event0`):** `mtk-kpd` (`0x72` Vol-, `0x73` Vol+, `0x74` Power)
  - **Backlight:** `/sys/class/leds/lcd-backlight/brightness` (`0..255`)
  - **Power states:** `mem`, `disk`
  - **Wi-Fi:** `wlan0` built into kernel (`lsmod` empty)

### Step 9: Back up `bootimg` and `recovery` partitions to Mac (SHA-256 verified)
- **Commands:**
  ```bash
  mkdir -p /Volumes/X/developer/etab5-noteos/backups
  adb shell "su -c 'dd if=/dev/bootimg of=/data/local/tmp/bootimg.img bs=1048576 count=10 && dd if=/dev/recovery of=/data/local/tmp/recovery.img bs=1048576 count=10 && chmod 644 /data/local/tmp/bootimg.img /data/local/tmp/recovery.img && sha256sum /data/local/tmp/bootimg.img /data/local/tmp/recovery.img'"
  adb pull /data/local/tmp/bootimg.img /Volumes/X/developer/etab5-noteos/backups/bootimg.img
  adb pull /data/local/tmp/recovery.img /Volumes/X/developer/etab5-noteos/backups/recovery.img
  shasum -a 256 /Volumes/X/developer/etab5-noteos/backups/bootimg.img /Volumes/X/developer/etab5-noteos/backups/recovery.img
  adb shell "su -c 'rm -f /data/local/tmp/bootimg.img /data/local/tmp/recovery.img'"
  ```
- **Result:**
  - `backups/bootimg.img`: `10485760` bytes, SHA-256 `87145651d3ed50ab98a0605893b9db70baa45e9468551e2dc787778cf74c1a9f`
  - `backups/recovery.img`: `10485760` bytes, SHA-256 `7c61b415f439e420786a9c9157af36318e2e5106ac00f69a158f49fe8d84e9d8`

---

## 2026-11-19 — Phase 2: Bare Framebuffer + Wacom Stylus Prototype

### Step 10: Reconnect ADB & build static ARMv7 prototype (`src/proto.c`)
- **Commands:**
  ```bash
  adb disconnect && adb connect 192.168.1.186:5555
  adb shell "su -c 'cat /sys/class/power_supply/battery/capacity /sys/class/power_supply/battery/batt_vol'"
  make -C /Volumes/X/developer/etab5-noteos
  file /Volumes/X/developer/etab5-noteos/build/proto
  ```
- **Result:**
  - Battery was at `31%` (`3663 mV`).
  - Created `src/proto.c`, `Makefile`, and `scripts/run_proto.sh`.
  - Cross-compiled cleanly with `zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 -Wall -Wextra` (0 warnings, `1.2M` static ARMv7 ELF binary).
  - `make push` hit `device offline` after idle gap — waiting to reconnect Wi-Fi ADB.

### Step 11: Push & launch live framebuffer prototype on tablet
- **Commands:**
  ```bash
  adb disconnect && adb connect 192.168.1.186:5555
  make -C /Volumes/X/developer/etab5-noteos push
  adb shell "su -c 'stop; sleep 1; /data/local/tmp/proto; start'"
  ```
- **Result:**
  - Launched live on `/dev/graphics/fb0` with Android paused (`stop`).
  - Verified display layout: `1920x1200 (virt 1920x3600) bpp=32 line_len=7680 smem=27648000 R=16/8 G=8/8 B=0/8 A=24/8 double=1`
  - Verified Wacom digitizer: `X=0..21696 Y=0..13560 P=0..1023`

---

## 2026-11-19 — Phase 3: Lightweight Engram-Style Note OS v1

### Step 12: Inspect Engram repo & build `noteos` v1
- **Context:** Reviewed `/Volumes/X/developer/Engram` (`Theme.swift`, `Note.swift`, `DrawingDocument.swift`, `DrawingLayout.swift`, `DocumentManager.swift`, `CanvasView.swift`) to align aesthetics and keep vector strokes compatible for future Pi/Mac iCloud bridge.
- **Files created/updated:**
  - `src/noteos.h` — core structs (`Stroke`, `Page`, `NoteDoc`, `ThemePalette`, `FB`)
  - `src/fb.c` — double-buffered dirty-rect framebuffer renderer, anti-aliased pressure-sensitive ink, translucent highlighter, lined/grid/blank paper, 3 Engram themes (`LIGHT`, `PAPER`, `DARK`)
  - `src/storage.c` — atomic binary `.enote` vector storage in `/data/noteos/notes`, multi-page notes, stroke-based undo/redo & object eraser
  - `src/main.c` — Wacom stylus + capacitive touch UI buttons (100% hardware palm rejection on canvas) + Power sleep/wake + Vol+/Vol- brightness
  - `Makefile`, `scripts/run_noteos.sh`
- **Commands:**
  ```bash
  chmod +x /Volumes/X/developer/etab5-noteos/scripts/run_noteos.sh
  make -C /Volumes/X/developer/etab5-noteos
  ```
- **Result:** Cross-compiled cleanly (`0` warnings) to static ARMv7 ELF (`build/noteos`, `1.4M`).

### Step 13: Push & launch Note OS v1 live on tablet (`192.168.1.188:5555`)
- **Commands:**
  ```bash
  adb disconnect && adb connect 192.168.1.188:5555
  make -C /Volumes/X/developer/etab5-noteos push
  adb shell "su -c 'stop; sleep 1; /data/local/tmp/noteos; start'"
  ```
- **Result:** Pushed `build/noteos` (`1.4 MB` in `0.14s`) and launched live on `/dev/graphics/fb0` (notes auto-save to `/data/noteos/notes/*.enote`).

### Step 14: Add stylus-button circular radial menu, Brush & Pencil tools, and Folders (`v1.1`)
- **Changes:**
  - `src/noteos.h`, `src/fb.c`, `src/main.c`: Added circular 8-wedge **Radial Menu** triggered by stylus side button (`BTN_STYLUS` — supports both hold-glide-release and click-then-tap) with `PEN`, `BRUSH` (calligraphy), `PENCIL` (textured graphite), `HI`, `ERASE`, `SZ:S/M/L`, `UNDO`, `REDO`, plus a 5-color swatch pill below the wheel.
  - `src/storage.c`, `src/main.c`: Added real filesystem **Folders** in `/data/noteos/notes/<folder>/` with `+ FOLDER`, `< BACK`, `+ NEW NOTE HERE`, and `DEL DIR` in the `NOTES` sidebar.
- **Commands:**
  ```bash
  make -C /Volumes/X/developer/etab5-noteos
  adb push /Volumes/X/developer/etab5-noteos/build/noteos /data/local/tmp/noteos.new
  adb shell "su -c 'killall noteos 2>/dev/null || true; sleep 0.3; mv /data/local/tmp/noteos.new /data/local/tmp/noteos; chmod +x /data/local/tmp/noteos'"
  adb shell "su -c 'stop; /data/local/tmp/noteos; start'"
  ```
- **Result:** Compiled cleanly (`0` warnings, `1.5M` static ARMv7) and launched live on `/dev/graphics/fb0`.

### Step 15: Add OS Settings page (Wi-Fi, brightness, battery, ADB, power) & On-Screen Keyboard (`v1.2`)
- **Hardware probe:**
  ```bash
  adb shell "su -c 'which wpa_cli wpa_supplicant dhcpcd ifconfig ip reboot; ls -la /data/misc/wifi/ /system/bin/wpa* /system/bin/wmt* /system/bin/6620* 2>/dev/null; wpa_cli -p /data/misc/wifi/sockets -i wlan0 status'"
  ```
  Verified `wpa_cli -p /data/misc/wifi/sockets -i wlan0` works directly even with Android stopped.
- **Changes:**
  - `src/settings.c`: Full-screen **OS Settings** (`SET` button) with Wi-Fi scan/connect/disconnect (`wpa_cli`), display brightness bar, live battery/voltage/current/temp + `/data` storage stats, `RESTART ADBD :5555`, and 2-tap confirmed `SLEEP` / `REBOOT` / `BOOT RECOVERY (TWRP)` / `POWER OFF`.
  - Built-in **On-Screen QWERTY Keyboard** (alpha + symbols + shift) used for Wi-Fi passwords and `RENAME NOTE` in the sidebar.
- **Commands:**
  ```bash
  make -C /Volumes/X/developer/etab5-noteos
  adb push /Volumes/X/developer/etab5-noteos/build/noteos /data/local/tmp/noteos.new
  adb shell "su -c 'killall noteos 2>/dev/null || true; sleep 0.3; mv /data/local/tmp/noteos.new /data/local/tmp/noteos; chmod +x /data/local/tmp/noteos'"
  adb shell "su -c 'stop; /data/local/tmp/noteos; start'"
  ```
- **Result:** Compiled cleanly (`0` warnings, `1.6M` static ARMv7) and launched live on `/dev/graphics/fb0`.

### Step 16: Prepare repository for GitHub (`README.md`, `.gitignore`, initial commit)
- **Files added/updated:** `README.md`, `.gitignore` (ignoring `backups/`, `build/`, `*.img`, `*.bin`).
- **Commands:**
  ```bash
  git -C /Volumes/X/developer/etab5-noteos add .
  git -C /Volumes/X/developer/etab5-noteos commit -m "Initial commit: E-Tab 5 bare Linux Note OS v1.2"
  ```

---

## 2026-11-19 — Phase 4: Standalone Bare Note OS (`bootimg` Flash)

### Step 17: Build static DHCPv4 client (`src/dhcpc.c`) & standalone `bootimg_noteos.img`
- **Discovery:**
  - Inspected `backups/bootimg.img`: Android v0 boot image (`page_size=2048`) with 512-byte MediaTek `KERNEL` and `ROOTFS` headers (`88 16 88 58`).
  - Verified `/system/etc/init/` defines `surfaceflinger` & `bootanim` in `class core`, and Android 7.1.2 has no standalone `dhcpcd` binary.
- **Changes:**
  - `src/dhcpc.c`: Wrote 28 KB static RFC 2131 DHCPv4 client daemon (`build/noteos-dhcp`), live-tested on `wlan0` (`leased 192.168.1.188 gw 192.168.1.1`).
  - `Makefile`: Added `-s` stripping (`build/noteos` = `131 KB`, `build/noteos-dhcp` = `28 KB`) and `bootimg` target.
  - `scripts/build_bootimg.py`: Pure-Python `newc` CPIO editor that preserves stock kernel & all Linux permissions, sets `ro.adb.secure=0` + `service.adb.tcp.port=5555` + `persist.sys.usb.config=adb`, disables 100% of Android (`zygote`, `surfaceflinger`, `bootanim`, `main`, `late_start`), and adds `/sbin/noteos`, `/sbin/noteos-dhcp`, `/sbin/noteos_run.sh`, `/sbin/noteos_wifi.sh`.
- **Commands:**
  ```bash
  make -C /Volumes/X/developer/etab5-noteos bootimg
  adb push backups/bootimg.img /data/local/tmp/stock_bootimg.img
  adb push build/bootimg_noteos.img /data/local/tmp/bootimg_noteos.img
  adb shell "su -c 'mv /data/local/tmp/stock_bootimg.img /data/noteos/stock_bootimg.img'"
  adb shell "su -c 'dd if=/data/local/tmp/bootimg_noteos.img of=/dev/bootimg bs=4096 && sync && dd if=/dev/bootimg bs=2048 count=3005 2>/dev/null | sha256sum && rm -f /data/local/tmp/bootimg_noteos.img'"
  adb shell "su -c reboot"
  ```
- **Result:**
  - `build/bootimg_noteos.img`: `6154240` bytes (`58.7%` of 10 MB partition), SHA-256 `afb930bb30a4433db8c25263cbc4eb68991472caca3b724364c33e6a2142c7d4` — verified byte-for-byte after flashing to `/dev/bootimg`.
  - TWRP in `/dev/recovery` left untouched; stock `bootimg.img` backed up both on Mac and `/data/noteos/stock_bootimg.img`.

### Step 18: Add naming & renaming for folders and drawings (`v1.3`)
- **Changes:**
  - `src/noteos.h`, `src/storage.c`, `src/settings.c`, `src/main.c`:
    - `+ FOLDER` and `+ NOTE` now open the on-screen QWERTY keyboard (with a new `CLR` key) so you can name folders and drawings at creation time.
    - Added `REN DIR` inside folders alongside `REN NOTE` / `RENAME NOTE`.
    - Sanitized custom folder names safely for filesystem storage.
- **Commands:**
  ```bash
  make -C /Volumes/X/developer/etab5-noteos
  ```
- **Result:** Compiled cleanly (`0` warnings, `133 KB` static ARMv7 binary) — ready to push straight to `/data/noteos/noteos` (instant live upgrade, no reflashing needed).

### Step 19: Persist visual & tool preferences across reboots (`v1.4`)
- **Changes:**
  - `src/main.c`: Added atomic `prefs_load` / `prefs_save` (`/data/noteos/prefs.conf`) remembering `theme`, `paper` (`LINED`/`GRID`/`BLANK`), `tool`, `color`, `size`, `brightness`, and last opened `folder`/`note`.
- **Commands:**
  ```bash
  make -C /Volumes/X/developer/etab5-noteos
  ```
- **Result:** Compiled cleanly (`0` warnings, `135 KB` static ARMv7).

### Step 20: Add Move Note between folders (`v1.5`)
- **Changes:**
  - `src/noteos.h`, `src/storage.c`: Added `storage_move_note` (atomic `rename()` between folders / root with collision-safe ID generation).
  - `src/main.c`: Added `MOVE` button & inline folder picker (`MOVE NOTE TO:`) in the `NOTES` sidebar, plus prominent `RENAME FOLDER` button when inside a folder.
- **Commands:**
  ```bash
  make -C /Volumes/X/developer/etab5-noteos
  ```
- **Result:** Compiled cleanly (`0` warnings, `137 KB` static ARMv7). Waiting for tablet IP or USB data connection to push `/data/noteos/noteos`.

### Step 21: Verify standalone OS boot over Wi-Fi ADB (`192.168.1.189:5555`) & hot-update to `v1.5`
- **Commands:**
  ```bash
  adb disconnect && adb connect 192.168.1.189:5555
  adb shell "ps; cat /proc/meminfo | head -n 4"
  adb push /Volumes/X/developer/etab5-noteos/build/noteos /data/noteos/noteos.new
  adb shell "mv /data/noteos/noteos.new /data/noteos/noteos && chmod +x /data/noteos/noteos && killall noteos"
  ```
- **Result:**
  - Connected automatically over Wi-Fi ADB (`192.168.1.189:5555`) as root with **0% Android running**.
  - **Memory:** `1951 MB free / 2011 MB total` (~60 MB used total; `noteos` = `9.2 MB`, `noteos-dhcp` = `4 KB`).
  - Hot-updated `/data/noteos/noteos` to `v1.5` (`140 KB` in `0.002s`); `init` auto-respawned `v1.5` immediately.

### Step 22: Fast partial-redraw on-screen keyboard + key press highlight (`v1.5.1`)
- **Fix:** Previously each keypress called `redraw_all()` (re-rendering the entire 1920×1200 canvas + all strokes).
- **Changes:**
  - `src/noteos.h`, `src/settings.c`, `src/main.c`: Added `SET_ACT_OSK_FAST`, `settings_render_osk_fast()` (redraws only the text box + pressed key), and `settings_osk_release()` (blue press-down highlight that releases cleanly on touch/stylus up).
- **Commands:**
  ```bash
  make -C /Volumes/X/developer/etab5-noteos
  adb push /Volumes/X/developer/etab5-noteos/build/noteos /data/noteos/noteos.new
  adb shell "mv /data/noteos/noteos.new /data/noteos/noteos && chmod +x /data/noteos/noteos && killall noteos"
  ```
- **Result:** Hot-updated live on `192.168.1.189:5555` (`142 KB`).

### Step 23: Modular codebase refactor & Conventional Commits
- **Changes:**
  - Split monolithic `src/main.c` (`1216` → `60` lines) and `src/noteos.h` into clean, single-responsibility modules under `include/` (`types.h`, `app.h`, `fb.h`, `render.h`, `storage.h`, `prefs.h`, `osk.h`, `settings.h`, `ui.h`, `input.h`) and `src/` (`main.c`, `app.c`, `fb.c`, `render.c`, `storage.c`, `prefs.c`, `osk.c`, `settings.c`, `ui.c`, `input.c`).
  - Moved standalone DHCP daemon to `daemons/dhcpc.c`, removed obsolete prototype files (`src/proto.c`, `scripts/run_proto.sh`, `scripts/run_noteos.sh`), added `scripts/deploy.sh`.
  - Upgraded `Makefile` to incremental `.o` + `.d` builds with `-Wall -Wextra -Werror` and `make deploy`.
  - Updated `README.md` to reflect the new architecture.
- **Commands:**
  ```bash
  make -C /Volumes/X/developer/etab5-noteos clean && make -C /Volumes/X/developer/etab5-noteos
  make -C /Volumes/X/developer/etab5-noteos deploy
  adb shell "ps | grep noteos"
  ```
- **Result:** Built cleanly (`0` warnings under `-Werror`, `135 KB` `noteos`, `28 KB` `noteos-dhcp`) and verified running live on tablet.

### Step 24: Add `etab.local` mDNS responder & Wi-Fi Drop (images + text on notes)
- **Changes:**
  - `include/mdns.h`, `src/mdns.c`: Added lightweight multicast mDNS responder (`224.0.0.251:5353`) resolving `etab.local` dynamically to current `wlan0` IPv4 (stays active even when screen sleeps).
  - `include/drop.h`, `src/drop.c`, `third_party/stb_image.h`, `src/stb_impl.c`: Added non-blocking HTTP Drop server on `:8080` (`GET /` serves mobile web paste/upload UI; `POST /drop` accepts raw PNG/JPEG/BMP, `multipart/form-data`, or `text/plain`).
  - `src/storage.c`, `src/render.c`, `src/ui.c`, `src/input.c`: Upgraded `.enote` to v2 (backward-compatible with v1) with movable/resizable images (`PIC` toolbar mode + drag move + aspect-locked resize handle + red `[X]` delete badge) and wrapped text blocks rendered underneath vector strokes.
  - `scripts/drop.sh`: macOS clipboard/file sender to `http://etab.local:8080/drop`.
- **Commands:**
  ```bash
  make -C /Volumes/X/developer/etab5-noteos clean && make -C /Volumes/X/developer/etab5-noteos
  make -C /Volumes/X/developer/etab5-noteos deploy
  ping -c 2 etab.local
  curl -fsS http://etab.local:8080/ | head -c 140
  ```
- **Result:** Built cleanly (`0` warnings under `-Werror`, `214 KB` static ARMv7 including PNG/JPEG decoder), deployed live, `etab.local` resolved in `4.0 ms`, and `http://etab.local:8080/` responded immediately.

### Step 25: Live drag coalescing, tap-to-reselect placed objects & movable/editable Text Boxes
- **Fixes & Features:**
  - `src/input.c`: Coalesced drag events outside the evdev `read()` loop so moving/resizing images and text boxes updates live in real time instead of waiting until lift. Enabled finger-tap on any placed image or text box (when pen is not hovering) to re-select and move/resize/delete it anytime.
  - `include/types.h`, `src/storage.c`, `src/render.c`, `src/ui.c`: Upgraded `PageText` to full movable/resizable/editable **Text Boxes** (`.enote` v3, backward-compatible with v1/v2) with `[EDIT]`, `[X]`, and `<>` handles, plus `+T` (new text box) and `EDIT` toolbar buttons.
- **Commands:**
  ```bash
  make -C /Volumes/X/developer/etab5-noteos
  adb connect etab.local:5555
  make -C /Volumes/X/developer/etab5-noteos deploy
  ```
- **Result:** Built cleanly (`0` warnings under `-Werror`, `217 KB`) and hot-deployed over `etab.local:5555`.
