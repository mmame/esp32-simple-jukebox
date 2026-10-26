#!/usr/bin/env python3
"""Command line client for the music player's upload API (for scripts and AI agents).

The player must be in upload mode: tap the WiFi button on the device, join the WiFi
(SSID MyMusicPlayer, password music1234) and use the default host 192.168.4.1.

  musicplayer.py list
  musicplayer.py upload --album "Beatles - Abbey Road" --cover cover.png 01.mp3 02.flac ...
  musicplayer.py upload --album "Beatles - Abbey Road" --dir ./abbey_road     (audio files + cover/folder/front image)
  musicplayer.py rename --album "Old name" --to "New name"
  musicplayer.py delete --album "Beatles - Abbey Road" [--file 03_x.mp3]
  musicplayer.py ota release/esp32-cyd-musicplayer-ota.bin
  musicplayer.py screenshot [file.png]      (the player's screen as PNG, default screenshot.png)
  musicplayer.py done                       (leave upload mode: WiFi off, library reloads)

Standard library only. Optional: Pillow (cover crop/convert), ffmpeg on PATH (converts non-MP3 audio to MP3).
Every command prints one JSON object as its last stdout line and exits non-zero on failure.
"""
import argparse
import http.client
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.parse

COVER_PX = 360
AUDIO_EXT = {".mp3", ".flac", ".wav", ".m4a", ".ogg", ".aac", ".opus", ".wma"}
IMAGE_EXT = {".jpg", ".jpeg", ".png", ".webp", ".bmp", ".gif"}
COVER_NAMES = ("cover", "folder", "front", "album")
BAD_CHARS = re.compile(r'[\\/:*?"<>|\x00-\x1f]')


def fail(msg):
    print(json.dumps({"ok": False, "error": msg}))
    return 1


class Api:
    CHUNK = 16 * 1024

    def __init__(self, host, timeout):
        self.host = host
        self.timeout = timeout

    def call(self, method, path, params=None, body=None, content_type=None, progress=None):
        """progress(sent_bytes, total_bytes) is called while the body is sent."""
        if params:
            path += "?" + urllib.parse.urlencode(params, quote_via=urllib.parse.quote)
        try:
            conn = http.client.HTTPConnection(self.host, timeout=self.timeout)
            conn.putrequest(method, path)
            if body is not None:
                conn.putheader("Content-Length", str(len(body)))
                if content_type:
                    conn.putheader("Content-Type", content_type)
            conn.endheaders()
            if body is not None:
                for i in range(0, len(body), self.CHUNK):
                    conn.send(body[i:i + self.CHUNK])
                    if progress:
                        progress(min(i + self.CHUNK, len(body)), len(body))
            resp = conn.getresponse()
            data = resp.read()
            conn.close()
        except (OSError, http.client.HTTPException) as e:
            raise SystemExit(fail(f"cannot reach the player at {self.host} ({e}). Is upload mode on?"))
        if resp.status >= 300:
            raise SystemExit(fail(f"{method} {path}: HTTP {resp.status} {data.decode(errors='replace')[:200]}"))
        return data


class Progress:
    """Percent display on stderr (stdout stays one JSON line). Weights are the source file sizes."""

    def __init__(self, enabled, weights):
        self.enabled, self.weights, self.total = enabled, weights, max(1, sum(weights))
        self.done = 0.0
        self.label = ""
        self.tty = sys.stderr.isatty()

    def start(self, index, label):
        self.index, self.label, self.base = index, label, sum(self.weights[:index])

    def update(self, sent, total):
        if not self.enabled:
            return
        frac = sent / total if total else 1
        pct = int(100 * (self.base + frac * self.weights[self.index]) / self.total)
        line = f"[{pct:3d}%] {self.label} ({self.index + 1}/{len(self.weights)}) {int(100 * frac)}%"
        if self.tty:
            print(chr(13) + line.ljust(78), end="", file=sys.stderr, flush=True)
        elif frac >= 1:
            print(line, file=sys.stderr, flush=True)

    def finish(self):
        if self.enabled and self.tty:
            print(file=sys.stderr)


def clean_name(s, limit=60):
    s = BAD_CHARS.sub("", s).strip().strip(".")
    return s[:limit].rstrip(" .")


def make_cover(path):
    """360x360 baseline JPEG bytes (centre crop). Without Pillow only a JPEG can be sent as it is."""
    try:
        from PIL import Image
    except ImportError:
        if os.path.splitext(path)[1].lower() in (".jpg", ".jpeg"):
            with open(path, "rb") as f:
                return f.read()
        raise SystemExit(fail("converting the cover needs Pillow (pip install pillow), or pass a 360x360 JPEG"))
    im = Image.open(path).convert("RGB")
    w, h = im.size
    s = min(w, h)
    left, top = (w - s) // 2, (h - s) // 2
    im = im.crop((left, top, left + s, top + s)).resize((COVER_PX, COVER_PX), Image.LANCZOS)
    out = io.BytesIO()
    im.save(out, "JPEG", quality=88, progressive=False)
    return out.getvalue()


def to_mp3(path, tmpdir):
    """Returns (bytes, name without extension). MP3 files are sent unchanged (the player skips ID3v2 tags)."""
    base, ext = os.path.splitext(os.path.basename(path))
    if ext.lower() == ".mp3":
        with open(path, "rb") as f:
            return f.read(), base
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise SystemExit(fail(f"{path}: not an MP3 and ffmpeg is not installed"))
    out = os.path.join(tmpdir, "conv.mp3")
    r = subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", path, "-vn", "-map_metadata", "-1",
                        "-codec:a", "libmp3lame", "-b:a", "160k", "-ar", "44100", out], capture_output=True)
    if r.returncode != 0:
        raise SystemExit(fail(f"{path}: ffmpeg failed: {r.stderr.decode(errors='replace')[:200]}"))
    with open(out, "rb") as f:
        return f.read(), base


def track_file_name(original_base, number):
    base = re.sub(r"^\s*\d{1,3}\s*[-._)]*\s*", "", original_base)
    return f"{number:02d}_{clean_name(base, 60) or 'track'}.mp3"


def natural_key(s):
    return [int(t) if t.isdigit() else t.lower() for t in re.split(r"(\d+)", s)]


def cmd_list(api, a):
    info = json.loads(api.call("GET", "/api/albums"))
    print(json.dumps({"ok": True, **info}, ensure_ascii=False))


def cmd_upload(api, a):
    album = clean_name(a.album)
    if not album:
        return fail("--album is empty or has only forbidden characters")
    tracks, cover = list(a.files), a.cover
    if a.dir:
        for n in sorted(os.listdir(a.dir), key=natural_key):
            stem, ext = os.path.splitext(n)
            full = os.path.join(a.dir, n)
            if ext.lower() in AUDIO_EXT:
                tracks.append(full)
            elif ext.lower() in IMAGE_EXT and not cover and stem.lower() in COVER_NAMES:
                cover = full
    if not tracks and not cover:
        return fail("nothing to upload: give audio files and/or --cover")

    info = json.loads(api.call("GET", "/api/albums"))
    existing = next((x for x in info["albums"] if x["name"].lower() == album.lower()), None)
    if existing:
        album = existing["name"]                    # same folder, whatever the letter case
        if a.replace:
            api.call("DELETE", "/api/album", {"album": album})
            existing = None
    nxt = 1
    if existing:
        for t in existing["tracks"]:
            m = re.match(r"(\d+)_", t["name"])
            if m:
                nxt = max(nxt, int(m.group(1)) + 1)

    sent = []
    sources = ([cover] if cover else []) + tracks
    prog = Progress(not a.quiet, [max(1, os.path.getsize(f)) for f in sources])
    i = 0
    if cover:
        prog.start(i, "cover.jpg")
        api.call("PUT", "/api/file", {"album": album, "file": "cover.jpg"}, make_cover(cover), "image/jpeg", prog.update)
        sent.append("cover.jpg")
        i += 1
    with tempfile.TemporaryDirectory() as tmp:
        for path in tracks:
            data, base = to_mp3(path, tmp)
            name = track_file_name(base, nxt)
            nxt += 1
            prog.start(i, name)
            api.call("PUT", "/api/file", {"album": album, "file": name}, data, "audio/mpeg", prog.update)
            sent.append(name)
            i += 1
    prog.finish()
    if a.done:
        api.call("POST", "/api/done")
    print(json.dumps({"ok": True, "album": album, "files": sent, "done": a.done}, ensure_ascii=False))


def cmd_rename(api, a):
    api.call("POST", "/api/rename", {"album": a.album, "to": clean_name(a.to)})
    print(json.dumps({"ok": True, "album": clean_name(a.to)}, ensure_ascii=False))


def cmd_delete(api, a):
    if a.file:
        api.call("DELETE", "/api/file", {"album": a.album, "file": a.file})
    else:
        api.call("DELETE", "/api/album", {"album": a.album})
    print(json.dumps({"ok": True}))


def cmd_ota(api, a):
    with open(a.image, "rb") as f:
        data = f.read()
    prog = Progress(not a.quiet, [len(data)])
    prog.start(0, os.path.basename(a.image))
    api.call("POST", "/api/ota", body=data, content_type="application/octet-stream", progress=prog.update)
    prog.finish()
    print(json.dumps({"ok": True, "note": "the player reboots into the new firmware"}))


def cmd_screenshot(api, a):
    data = api.call("GET", "/screenshot.png")
    with open(a.file, "wb") as f:
        f.write(data)
    print(json.dumps({"ok": True, "file": a.file, "bytes": len(data)}))


def cmd_done(api, a):
    api.call("POST", "/api/done")
    print(json.dumps({"ok": True}))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default="192.168.4.1")
    p.add_argument("--timeout", type=float, default=120, help="seconds per request (default 120)")
    p.add_argument("--quiet", action="store_true", help="no progress display on stderr")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("list").set_defaults(fn=cmd_list)

    u = sub.add_parser("upload")
    u.add_argument("--album", required=True, help="album title = folder name shown on the player")
    u.add_argument("--cover", help="picture (centre-cropped to a 360x360 JPEG)")
    u.add_argument("--dir", help="folder with audio files (natural order) and an optional cover/folder/front image")
    u.add_argument("--replace", action="store_true", help="delete the album first if it exists (default: append tracks)")
    u.add_argument("--done", action="store_true", help="leave upload mode afterwards")
    u.add_argument("files", nargs="*", help="audio files in play order")
    u.set_defaults(fn=cmd_upload)

    r = sub.add_parser("rename")
    r.add_argument("--album", required=True)
    r.add_argument("--to", required=True)
    r.set_defaults(fn=cmd_rename)

    d = sub.add_parser("delete")
    d.add_argument("--album", required=True)
    d.add_argument("--file", help="only this file (e.g. 03_song.mp3); default: the whole album")
    d.set_defaults(fn=cmd_delete)

    o = sub.add_parser("ota")
    o.add_argument("image")
    o.set_defaults(fn=cmd_ota)

    s = sub.add_parser("screenshot")
    s.add_argument("file", nargs="?", default="screenshot.png")
    s.set_defaults(fn=cmd_screenshot)

    sub.add_parser("done").set_defaults(fn=cmd_done)

    a = p.parse_args()
    try:
        rc = a.fn(Api(a.host, a.timeout), a)
    except SystemExit:
        raise
    except Exception as e:                      # keep the "one JSON line" contract for scripts
        rc = fail(f"{type(e).__name__}: {e}")
    sys.exit(rc or 0)


if __name__ == "__main__":
    main()
