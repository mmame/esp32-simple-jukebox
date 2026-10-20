#!/usr/bin/env python3
"""Uploads every sub folder of the given folders as one album (folder name = title, cover.jpg + mp3 files).

  python tools/upload_folders.py "C:\\Music\\Cajun-Zydeco" "C:\\Music\\NewOrleans-Jazz" ...

Albums that already exist on the player are replaced, so the script can simply be started again after an
interruption; --skip-existing leaves finished albums alone instead. Progress goes to stdout, one line per file.
"""
import argparse
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("folders", nargs="+")
    p.add_argument("--host", default="192.168.4.1")
    p.add_argument("--skip-existing", action="store_true")
    p.add_argument("--done", action="store_true", help="leave upload mode after the last album")
    a = p.parse_args()

    albums = []
    for root in a.folders:
        for n in sorted(os.listdir(root), key=str.lower):
            if os.path.isdir(os.path.join(root, n)):
                albums.append((n, os.path.join(root, n)))

    base = [sys.executable, "-u", os.path.join(HERE, "musicplayer.py"), "--host", a.host]
    existing = set()
    if a.skip_existing:
        r = subprocess.run(base + ["list"], capture_output=True, text=True, encoding="utf-8", errors="replace",
                           env={**os.environ, "PYTHONUTF8": "1"})
        existing = {x["name"].lower() for x in json.loads(r.stdout.strip().splitlines()[-1])["albums"]}

    t0, failed = time.time(), []
    for i, (name, path) in enumerate(albums, 1):
        title = name[:60].rstrip(" .")
        print(f"=== album {i}/{len(albums)}: {title}  ({(time.time() - t0) / 60:.1f} min elapsed)", flush=True)
        if title.lower() in existing:
            print("    already on the player, skipped", flush=True)
            continue
        for attempt in (1, 2, 3):
            r = subprocess.run(base + ["upload", "--album", name, "--dir", path, "--replace"],
                               stdout=subprocess.PIPE, stderr=sys.stdout, text=True, encoding="utf-8",
                               errors="replace", env={**os.environ, "PYTHONUTF8": "1"})
            if r.returncode == 0:
                break
            print(f"    attempt {attempt} failed: {r.stdout.strip()[-200:]}", flush=True)
            time.sleep(5)
        else:
            failed.append(name)
    if a.done and not failed:
        subprocess.run(base + ["done"])
    print(f"=== finished in {(time.time() - t0) / 60:.1f} min, {len(albums) - len(failed)}/{len(albums)} albums ok", flush=True)
    for n in failed:
        print("FAILED:", n, flush=True)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
