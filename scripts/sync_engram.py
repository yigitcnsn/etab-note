#!/usr/bin/env python3
"""
Two-way sync between a local Markdown vault (Obsidian / iCloud Engram Notes)
and E-Tab 5 (/data/noteos/notes).

Usage:
  ./scripts/sync_engram.py --fresh                 # wipe tablet notes & push clean copy
  ./scripts/sync_engram.py                         # two-way incremental sync (.md & folders)
  NOTES_DIR=~/Notes ./scripts/sync_engram.py       # override local Markdown vault path
  ETAB_HOST=192.168.1.50:5555 ./scripts/sync_engram.py
"""

import os
import subprocess
import sys

DEFAULT_NOTES_DIR = os.path.expanduser(
    "~/Library/Mobile Documents/com~apple~CloudDocs/Engram/Engram Notes"
)
ENGRAM_DIR = os.path.expanduser(os.environ.get("NOTES_DIR", DEFAULT_NOTES_DIR))
REMOTE_DIR = "/data/noteos/notes"
HOST = os.environ.get("ETAB_HOST", "etab.local:5555")


def adb(*args, check=True):
    cmd = ["adb", *args]
    return subprocess.run(cmd, capture_output=True, text=True, check=check)


def ensure_connected():
    adb("connect", HOST, check=False)


def fresh_push():
    if not os.path.isdir(ENGRAM_DIR):
        sys.exit(f"Engram Notes folder not found: {ENGRAM_DIR}")

    ensure_connected()
    print("Wiping old test notes on tablet for fresh sync...")
    adb("shell", f"rm -rf '{REMOTE_DIR}' && mkdir -p '{REMOTE_DIR}'")

    pushed_files = 0
    pushed_dirs = 0
    for root, dirs, files in os.walk(ENGRAM_DIR):
        # Skip hidden dirs and .drawing packages for direct md tree
        dirs[:] = [d for d in dirs if not d.startswith(".") and not d.endswith(".drawing")]
        rel_dir = os.path.relpath(root, ENGRAM_DIR)
        rdir = REMOTE_DIR if rel_dir == "." else f"{REMOTE_DIR}/{rel_dir}"
        if rel_dir != ".":
            adb("shell", f"mkdir -p \"{rdir}\"")
            pushed_dirs += 1

        for fn in files:
            if fn.startswith(".") or not fn.endswith(".md"):
                continue
            local_path = os.path.join(root, fn)
            remote_path = f"{rdir}/{fn}"
            adb("push", local_path, remote_path)
            pushed_files += 1

    # Restart noteos so it opens fresh vault immediately
    adb("shell", "rm -f /data/noteos/prefs.conf && killall noteos", check=False)
    print(f"Synced {pushed_files} Markdown notes across {pushed_dirs} folders to E-Tab 5.")


def two_way_sync():
    if not os.path.isdir(ENGRAM_DIR):
        sys.exit(f"Engram Notes folder not found: {ENGRAM_DIR}")

    ensure_connected()
    # 1. Pull any modified/new .md files from tablet to local temp dir and merge newest
    tmp_pull = "/tmp/etab_engram_pull"
    subprocess.run(["rm", "-rf", tmp_pull], check=False)
    os.makedirs(tmp_pull, exist_ok=True)
    adb("pull", REMOTE_DIR, tmp_pull, check=False)
    pulled_root = os.path.join(tmp_pull, "notes")

    pulled_updates = 0
    if os.path.isdir(pulled_root):
        for root, dirs, files in os.walk(pulled_root):
            rel_dir = os.path.relpath(root, pulled_root)
            ldir = ENGRAM_DIR if rel_dir == "." else os.path.join(ENGRAM_DIR, rel_dir)
            for fn in files:
                if not fn.endswith(".md"):
                    continue
                rfile = os.path.join(root, fn)
                lfile = os.path.join(ldir, fn)
                os.makedirs(ldir, exist_ok=True)
                if not os.path.exists(lfile):
                    subprocess.run(["cp", "-p", rfile, lfile], check=True)
                    pulled_updates += 1
                else:
                    # Compare content; if different and tablet copy was modified more recently
                    if open(rfile, "rb").read() != open(lfile, "rb").read():
                        if os.path.getmtime(rfile) > os.path.getmtime(lfile):
                            subprocess.run(["cp", "-p", rfile, lfile], check=True)
                            pulled_updates += 1

    # 2. Push any newer/new local .md files to tablet
    pushed_updates = 0
    for root, dirs, files in os.walk(ENGRAM_DIR):
        dirs[:] = [d for d in dirs if not d.startswith(".") and not d.endswith(".drawing")]
        rel_dir = os.path.relpath(root, ENGRAM_DIR)
        rdir = REMOTE_DIR if rel_dir == "." else f"{REMOTE_DIR}/{rel_dir}"
        if rel_dir != ".":
            adb("shell", f"mkdir -p \"{rdir}\"")
        for fn in files:
            if not fn.endswith(".md"):
                continue
            lfile = os.path.join(root, fn)
            pfile = os.path.join(pulled_root, "" if rel_dir == "." else rel_dir, fn)
            if not os.path.exists(pfile) or open(lfile, "rb").read() != open(pfile, "rb").read():
                adb("push", lfile, f"{rdir}/{fn}")
                pushed_updates += 1

    subprocess.run(["rm", "-rf", tmp_pull], check=False)
    print(f"Sync complete: pulled {pulled_updates} updates from E-Tab, pushed {pushed_updates} updates to E-Tab.")


if __name__ == "__main__":
    if "--fresh" in sys.argv:
        fresh_push()
    else:
        two_way_sync()
