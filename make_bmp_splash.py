#!/usr/bin/env python3
"""
make_bmp_splash.py — auto-resolution BMP splash generator for ElseaOS

Reads the target display resolution from grub/grub.cfg so the generated
file always matches what the kernel framebuffer will actually be.

Output file : initrd/bsplash_WxH.bin   (loaded by bsplash.c at runtime)

Usage:
  python3 make_bmp_splash.py                           # auto-detect resolution
  python3 make_bmp_splash.py video.mp4                 # custom video, auto-detect res
  python3 make_bmp_splash.py video.mp4 15              # explicit fps
  python3 make_bmp_splash.py video.mp4 15 1920x1080    # explicit resolution
  python3 make_bmp_splash.py video.mp4 15 all          # generate all supported sizes

bsplash_WxH.bin format:
  [0-3]   "BSPL" magic
  [4-5]   width       uint16 LE
  [6-7]   height      uint16 LE
  [8-9]   fps         uint16 LE
  [10-13] frame_count uint32 LE
  [14-17] audio_bytes uint32 LE  (8-bit unsigned mono 22050 Hz PCM)
  [18-31] reserved zeros
  [32..]  raw audio PCM
  [32+audio..] frame_count × width × height × 3  raw RGB24 pixels
"""

import re, struct, sys, os, subprocess

FFMPEG  = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "ffmpeg_dir", "ffmpeg")
GRUB_CFG = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "grub", "grub.cfg")
INITRD   = "initrd"

SUPPORTED_RESOLUTIONS = [
    (800,  600),
    (1024, 768),
    (1280, 720),
    (1366, 768),
    (1600, 900),
    (1920, 1080),
    (2560, 1440),
    (3840, 2160),
]


# ─── resolution detection ──────────────────────────────────────────────────────

def read_grub_resolution():
    """Return (width, height) from the BIOS gfxpayload line in grub.cfg."""
    try:
        with open(GRUB_CFG) as f:
            for line in f:
                m = re.search(r'gfxpayload\s*=\s*(\d+)[xX](\d+)', line)
                if m:
                    return int(m.group(1)), int(m.group(2))
    except (IOError, OSError):
        pass
    print(f"[bsplash] grub.cfg not found or no gfxpayload — defaulting to 1920×1080")
    return 1920, 1080


# ─── core generator ───────────────────────────────────────────────────────────

def make_bmp_splash(inp, width, height, fps=15):
    """
    Generate initrd/bsplash_WxH.bin from `inp` at `width`×`height` @ `fps`.
    Returns True on success, False on failure.
    """
    if not os.path.exists(FFMPEG):
        print(f"ERROR: ffmpeg not found at {FFMPEG}", file=sys.stderr)
        return False
    if not os.path.exists(inp):
        print(f"ERROR: input video not found: {inp}", file=sys.stderr)
        return False

    tag     = f"{width}x{height}"
    out_bin = os.path.join(INITRD, f"bsplash_{tag}.bin")
    frame_sz = width * height * 3

    est_mb = frame_sz * fps * 10 / 1024 / 1024
    if est_mb > 150:
        print(f"\n[bsplash {tag}]  WARNING: ~{est_mb:.0f} MB for a 10-s clip. "
              f"Use a shorter video or lower fps to stay under 150 MB.")

    print(f"\n{'─'*60}")
    print(f"  Resolution : {width} × {height}  @  {fps} fps")
    print(f"  Output     : {out_bin}")
    print(f"  Frame size : {frame_sz // 1024} KB each  ({width}×{height}×3 bytes)")
    print(f"{'─'*60}")

    # ── Step 1: extract video frames as raw RGB24 ─────────────────────────────
    print(f"  [1/2] Extracting frames via FFmpeg...")
    vp = subprocess.run(
        [FFMPEG, "-y", "-i", inp,
         "-vf", f"scale={width}:{height}:force_original_aspect_ratio=decrease,"
                f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:black",
         "-r", str(fps),
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    if vp.returncode != 0:
        print(f"  FFmpeg error:\n" + vp.stderr.decode(errors="replace")[-2000:])
        print(f"  SKIPPING {tag}")
        return False

    raw = vp.stdout
    frame_count = len(raw) // frame_sz
    if frame_count == 0:
        print(f"  ERROR: no frames extracted — SKIPPING {tag}")
        return False
    raw = raw[:frame_count * frame_sz]   # trim any partial trailing frame

    dur     = frame_count / fps
    raw_mb  = len(raw) / 1024 / 1024
    print(f"  {frame_count} frames  ({dur:.1f}s)  {raw_mb:.1f} MB raw "
          f"({frame_sz} bytes × {frame_count})")

    # ── Step 2: extract audio (8-bit unsigned mono PCM @ 22050 Hz) ───────────
    print(f"  [2/2] Extracting audio track...")
    ap = subprocess.run(
        [FFMPEG, "-y", "-i", inp,
         "-vn", "-f", "u8", "-ac", "1", "-ar", "22050", "-"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    audio = ap.stdout if (ap.returncode == 0 and ap.stdout) else b""
    if audio:
        print(f"  Audio: {len(audio)//1024} KB  ({len(audio)/22050:.1f}s)"
              f"  8-bit mono 22050 Hz PCM")
    else:
        print(f"  Audio: none")

    # ── Write bsplash_WxH.bin ─────────────────────────────────────────────────
    total = 32 + len(audio) + len(raw)
    print(f"  Writing {total/1024/1024:.1f} MB → {out_bin} ...")
    os.makedirs(INITRD, exist_ok=True)
    with open(out_bin, "wb") as f:
        f.write(b"BSPL")
        f.write(struct.pack("<H", width))
        f.write(struct.pack("<H", height))
        f.write(struct.pack("<H", fps))
        f.write(struct.pack("<I", frame_count))
        f.write(struct.pack("<I", len(audio)))
        f.write(b"\x00" * 14)          # reserved — 32-byte header total
        f.write(audio)
        f.write(raw)

    actual = os.path.getsize(out_bin)
    print(f"  Done: {actual/1024/1024:.1f} MB  ({frame_count} frames × {frame_sz} B)")
    return True


# ─── main ─────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    inp = sys.argv[1] if len(sys.argv) > 1 else "in_this_video_remove_apple_ima.mp4"
    fps = int(sys.argv[2]) if len(sys.argv) > 2 else 15
    res = sys.argv[3].lower() if len(sys.argv) > 3 else "auto"

    if res == "all":
        # Generate every supported resolution
        print(f"Generating {len(SUPPORTED_RESOLUTIONS)} splash files @ {fps} fps")
        ok = sum(make_bmp_splash(inp, w, h, fps) for w, h in SUPPORTED_RESOLUTIONS)
        print(f"\nDone: {ok}/{len(SUPPORTED_RESOLUTIONS)} succeeded.")

    elif res == "auto" or res == "":
        # Auto-detect from grub.cfg
        w, h = read_grub_resolution()
        print(f"Auto-detected resolution: {w}×{h}  (from grub/grub.cfg)")
        make_bmp_splash(inp, w, h, fps)

    else:
        # Explicit "WxH"
        try:
            w, h = map(int, res.split('x'))
            make_bmp_splash(inp, w, h, fps)
        except ValueError:
            print(f"ERROR: resolution must be WxH (e.g. 1920x1080), 'auto', or 'all'",
                  file=sys.stderr)
            sys.exit(1)
