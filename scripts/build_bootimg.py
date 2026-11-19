#!/usr/bin/env python3
"""
Builds a standalone E-Tab 5 Bare Note OS bootimg (build/bootimg_noteos.img)
from backups/bootimg.img, preserving:
  - Exact Android boot image header fields & page alignment (2048 bytes)
  - Exact stock Linux 3.4.67 kernel + 512-byte MTK KERNEL header
  - Exact 512-byte MTK ROOTFS header (with updated ramdisk payload size)
  - All stock Linux CPIO uid/gid/mode/symlinks untouched (pure Python newc editor)

Disables 100% of Android (surfaceflinger, bootanim, zygote, main, late_start)
and boots straight into Note OS + Wi-Fi (wpa_supplicant + noteos-dhcp) + adbd (:5555 & USB).
"""

import gzip
import hashlib
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BACKUP_BOOT = os.path.join(ROOT, "backups", "bootimg.img")
NOTEOS_BIN  = os.path.join(ROOT, "build", "noteos")
DHCP_BIN    = os.path.join(ROOT, "build", "noteos-dhcp")
OUT_BOOT    = os.path.join(ROOT, "build", "bootimg_noteos.img")

NOTEOS_RUN_SH = b"""#!/system/bin/sh
mkdir -p /data/noteos/notes
if [ -x /data/noteos/noteos ]; then
    exec /data/noteos/noteos
else
    exec /sbin/noteos
fi
"""

NOTEOS_WIFI_SH = b"""#!/system/bin/sh
# Initialize MediaTek MT6592/MT8392 WMT Wi-Fi without Android framework
/system/bin/wmt_loader

i=0
while [ ! -e /dev/wmtWifi ] && [ $i -lt 40 ]; do
    sleep 0.2
    i=$((i + 1))
done
sleep 1
echo 1 > /dev/wmtWifi

i=0
while [ ! -d /sys/class/net/wlan0 ] && [ $i -lt 50 ]; do
    sleep 0.2
    i=$((i + 1))
done

/system/bin/ifconfig wlan0 up
mkdir -p /data/misc/wifi/sockets
chmod 0770 /data/misc/wifi/sockets

/system/bin/wpa_supplicant -iwlan0 -Dnl80211 \
    -c/data/misc/wifi/wpa_supplicant.conf \
    -I/system/etc/wifi/wpa_supplicant_overlay.conf \
    -O/data/misc/wifi/sockets \
    -e/data/misc/wifi/entropy.bin -B

i=0
while [ ! -e /data/misc/wifi/sockets/wlan0 ] && [ $i -lt 40 ]; do
    sleep 0.2
    i=$((i + 1))
done

/system/bin/wpa_cli -p /data/misc/wifi/sockets -i wlan0 enable_network all
/system/bin/wpa_cli -p /data/misc/wifi/sockets -i wlan0 reconnect

exec /sbin/noteos-dhcp wlan0
"""


def align(n, a):
    return ((n + a - 1) // a) * a


def parse_cpio_newc(data):
    """Parse SVR4 newc (070701) cpio archive into list of (fields_dict, name_bytes, body_bytes)."""
    entries = []
    pos = 0
    total = len(data)
    while pos + 110 <= total:
        magic = data[pos:pos + 6]
        if magic != b"070701":
            raise ValueError(f"Unexpected cpio magic {magic!r} at offset {pos}")
        vals = [int(data[pos + 6 + i * 8:pos + 14 + i * 8], 16) for i in range(13)]
        fields = {
            "ino": vals[0], "mode": vals[1], "uid": vals[2], "gid": vals[3],
            "nlink": vals[4], "mtime": vals[5], "filesize": vals[6],
            "devmajor": vals[7], "devminor": vals[8], "rdevmajor": vals[9],
            "rdevminor": vals[10], "namesize": vals[11], "check": vals[12],
        }
        pos += 110
        name = data[pos:pos + fields["namesize"] - 1]
        pos = align(pos + fields["namesize"], 4)
        body = data[pos:pos + fields["filesize"]]
        pos = align(pos + fields["filesize"], 4)
        if name == b"TRAILER!!!":
            break
        entries.append([fields, name, body])
    return entries


def build_cpio_newc(entries):
    """Serialize entries back to SVR4 newc (070701) cpio bytes."""
    out = bytearray()
    for fields, name, body in entries:
        fields["namesize"] = len(name) + 1
        fields["filesize"] = len(body)
        hdr = (
            f"070701"
            f"{fields['ino']:08x}{fields['mode']:08x}{fields['uid']:08x}{fields['gid']:08x}"
            f"{fields['nlink']:08x}{fields['mtime']:08x}{fields['filesize']:08x}"
            f"{fields['devmajor']:08x}{fields['devminor']:08x}{fields['rdevmajor']:08x}"
            f"{fields['rdevminor']:08x}{fields['namesize']:08x}{fields['check']:08x}"
        ).encode("ascii")
        out += hdr + name + b"\x00"
        while len(out) % 4 != 0:
            out += b"\x00"
        out += body
        while len(out) % 4 != 0:
            out += b"\x00"

    trailer_name = b"TRAILER!!!"
    thdr = (
        f"070701" + "00000000" * 4 + "00000001" + "00000000" * 6
        + f"{len(trailer_name) + 1:08x}" + "00000000"
    ).encode("ascii")
    out += thdr + trailer_name + b"\x00"
    while len(out) % 4 != 0:
        out += b"\x00"
    # Pad to 512-byte block boundary like standard cpio
    while len(out) % 512 != 0:
        out += b"\x00"
    return bytes(out)


def make_entry(ino, name, body, mode=0o100755):
    return [
        {
            "ino": ino, "mode": mode, "uid": 0, "gid": 0, "nlink": 1,
            "mtime": 0, "filesize": len(body),
            "devmajor": 0, "devminor": 0, "rdevmajor": 0, "rdevminor": 0,
            "namesize": len(name) + 1, "check": 0,
        },
        name,
        body,
    ]


def patch_default_prop(text):
    lines = text.splitlines()
    out = []
    seen = set()
    overrides = {
        "ro.adb.secure": "0",
        "ro.secure": "0",
        "ro.debuggable": "1",
        "persist.sys.usb.config": "adb",
        "service.adb.tcp.port": "5555",
    }
    for ln in lines:
        key = ln.split("=", 1)[0].strip() if "=" in ln else ""
        if key in overrides:
            out.append(f"{key}={overrides[key]}")
            seen.add(key)
        else:
            out.append(ln)
    for k, v in overrides.items():
        if k not in seen:
            out.append(f"{k}={v}")
    return "\n".join(out) + "\n"


def patch_init_rc(text):
    # 1. Don't import zygote
    text = text.replace("import /init.${ro.zygote}.rc", "# disabled: zygote")

    # 2. Replace `class_start core` in `on boot` with ONLY our minimal bare-Linux services
    if "    class_start core\n" not in text:
        raise RuntimeError("Could not find 'class_start core' in init.rc")
    bare_boot = (
        "    # Bare Note OS boot (replaces class_start core — no Android)\n"
        "    start healthd\n"
        "    start conn_launcher\n"
        "    setprop service.adb.tcp.port 5555\n"
        "    setprop sys.usb.config adb\n"
        "    start adbd\n"
        "    start noteos_wifi\n"
        "    start noteos\n"
    )
    text = text.replace("    class_start core\n", bare_boot, 1)

    # 3. Disable all Android class_start main / late_start triggers
    text = text.replace("    class_start main\n", "    # disabled: class_start main\n")
    text = text.replace("    class_start late_start\n", "    # disabled: class_start late_start\n")

    # 4. Disable surfaceflinger explicitly
    sf_block = "service surfaceflinger /system/bin/surfaceflinger\n"
    if sf_block in text:
        text = text.replace(sf_block, sf_block + "    disabled\n", 1)

    # 5. Append Note OS services
    noteos_services = """
# ---- E-Tab 5 Bare Note OS Services ----
service noteos_wifi /sbin/noteos_wifi.sh
    class core
    user root
    group root wifi inet net_raw net_admin
    disabled
    oneshot
    seclabel u:r:su:s0

service noteos /sbin/noteos_run.sh
    class core
    user root
    group root graphics input
    disabled
    seclabel u:r:su:s0
"""
    return text + noteos_services


def main():
    for path in (BACKUP_BOOT, NOTEOS_BIN, DHCP_BIN):
        if not os.path.exists(path):
            sys.exit(f"Missing required file: {path}")

    raw = open(BACKUP_BOOT, "rb").read()
    hdr_fmt = "<8sIIIIIIII8s16s512s"
    magic, ksize, kaddr, rsize, raddr, ssize, saddr, taddr, psize, unused, name, cmdline = (
        struct.unpack(hdr_fmt, raw[:576])
    )
    assert magic == b"ANDROID!", f"Bad boot magic: {magic}"
    assert ssize == 0, f"Unexpected second stage size: {ssize}"

    koff = psize
    roff = koff + align(ksize, psize)
    kernel_blob = raw[koff:koff + ksize]
    mtk_rootfs_hdr = bytearray(raw[roff:roff + 512])
    assert mtk_rootfs_hdr[:4] == b"\x88\x16\x88\x58", "Missing MTK ROOTFS header"

    orig_ramdisk_gz = raw[roff + 512:roff + rsize]
    orig_cpio = gzip.decompress(orig_ramdisk_gz)
    entries = parse_cpio_newc(orig_cpio)
    max_ino = max(e[0]["ino"] for e in entries)

    patched_default = False
    patched_init = False
    for e in entries:
        if e[1] == b"default.prop":
            e[2] = patch_default_prop(e[2].decode("utf-8")).encode("utf-8")
            patched_default = True
        elif e[1] == b"init.rc":
            e[2] = patch_init_rc(e[2].decode("utf-8")).encode("utf-8")
            patched_init = True

    assert patched_default and patched_init, "Failed to locate default.prop or init.rc in ramdisk"

    # Remove any prior copies of our custom files if re-running
    custom_names = {b"sbin/noteos", b"sbin/noteos-dhcp", b"sbin/noteos_run.sh", b"sbin/noteos_wifi.sh"}
    entries = [e for e in entries if e[1] not in custom_names]

    noteos_bytes = open(NOTEOS_BIN, "rb").read()
    dhcp_bytes = open(DHCP_BIN, "rb").read()
    entries.append(make_entry(max_ino + 1, b"sbin/noteos", noteos_bytes, 0o100755))
    entries.append(make_entry(max_ino + 2, b"sbin/noteos-dhcp", dhcp_bytes, 0o100755))
    entries.append(make_entry(max_ino + 3, b"sbin/noteos_run.sh", NOTEOS_RUN_SH, 0o100755))
    entries.append(make_entry(max_ino + 4, b"sbin/noteos_wifi.sh", NOTEOS_WIFI_SH, 0o100755))

    new_cpio = build_cpio_newc(entries)
    new_ramdisk_gz = gzip.compress(new_cpio, compresslevel=9, mtime=0)

    # Update 512-byte MTK ROOTFS header with exact new gzipped payload size
    struct.pack_into("<I", mtk_rootfs_hdr, 4, len(new_ramdisk_gz))
    new_ramdisk_blob = bytes(mtk_rootfs_hdr) + new_ramdisk_gz
    new_rsize = len(new_ramdisk_blob)

    # Compute standard Android boot.img SHA-1 ID
    sha = hashlib.sha1()
    sha.update(kernel_blob)
    sha.update(struct.pack("<I", ksize))
    sha.update(new_ramdisk_blob)
    sha.update(struct.pack("<I", new_rsize))
    sha.update(b"")
    sha.update(struct.pack("<I", 0))
    boot_id = sha.digest() + b"\x00" * 12

    new_hdr = bytearray(psize)
    struct.pack_into(
        hdr_fmt, new_hdr, 0,
        magic, ksize, kaddr, new_rsize, raddr, ssize, saddr, taddr, psize, unused, name, cmdline
    )
    new_hdr[576:608] = boot_id

    out = bytearray()
    out += new_hdr
    out += kernel_blob
    out += b"\x00" * (align(len(kernel_blob), psize) - len(kernel_blob))
    out += new_ramdisk_blob
    out += b"\x00" * (align(len(new_ramdisk_blob), psize) - len(new_ramdisk_blob))

    max_part_size = 10 * 1024 * 1024  # 10 MB bootimg partition
    assert len(out) <= max_part_size, f"bootimg too large: {len(out)} > {max_part_size}"

    os.makedirs(os.path.dirname(OUT_BOOT), exist_ok=True)
    open(OUT_BOOT, "wb").write(out)

    print(f"Built {OUT_BOOT}")
    print(f"  kernel:  {ksize} bytes (untouched)")
    print(f"  ramdisk: {new_rsize} bytes (orig {rsize}, +{new_rsize - rsize} bytes)")
    print(f"  total:   {len(out)} bytes / {max_part_size} bytes ({len(out)*100/max_part_size:.1f}%)")
    print(f"  sha256:  {hashlib.sha256(out).hexdigest()}")


if __name__ == "__main__":
    main()
