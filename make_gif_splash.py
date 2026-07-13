#!/usr/bin/env python3
"""
make_gif_splash.py — Convert MP4 to animated GIF for ElseaOS boot splash

Creates initrd/splash.gif with optimized 256-colour palette.

Usage:
  python3 make_gif_splash.py [input.mp4] [width] [height] [fps]
  Defaults: in_this_video_remove_apple_ima.mp4  320 180 12

Lower fps → smaller file, smoother playback on slow hardware.
Higher fps → larger file but smoother motion (max ~25fps for QEMU).
"""

import subprocess, struct, sys, os

FFMPEG = os.path.join(os.path.dirname(__file__), "ffmpeg_dir", "ffmpeg")
FFPROBE = os.path.join(os.path.dirname(__file__), "ffmpeg_dir", "ffprobe")
OUT = "initrd/splash.gif"


def make_gif(inp, width=1280, height=720, fps=12):
    if not os.path.exists(FFMPEG):
        print(f"ERROR: ffmpeg not found at {FFMPEG}", file=sys.stderr)
        sys.exit(1)
    if not os.path.exists(inp):
        print(f"ERROR: {inp} not found", file=sys.stderr)
        sys.exit(1)

    os.makedirs("initrd", exist_ok=True)

    print(f"[gif] Input  : {inp}")
    print(f"[gif] Output : {OUT}")
    print(f"[gif] Format : {width}x{height} @ {fps}fps  256-colour GIF")

    # Step 1: Generate optimal palette from video
    palette = "/tmp/palette_splash.png"
    print("[gif] Step 1/2 — generating colour palette...")
    r = subprocess.run(
        [FFMPEG, "-y", "-i", inp,
         "-vf", f"scale={width}:{height}:flags=lanczos,fps={fps},palettegen=max_colors=256:stats_mode=full",
         palette],
        capture_output=True
    )
    if r.returncode != 0:
        print("[gif] palettegen failed:\n" + r.stderr.decode(errors="replace")[-2000:])
        sys.exit(1)

    # Step 2: Encode GIF using palette
    print("[gif] Step 2/2 — encoding animated GIF...")
    r2 = subprocess.run(
        [FFMPEG, "-y", "-i", inp, "-i", palette,
         "-lavfi", f"scale={width}:{height}:flags=lanczos,fps={fps}[v];[v][1:v]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle",
         "-loop", "0",   # loop forever
         OUT],
        capture_output=True
    )
    if r2.returncode != 0:
        print("[gif] GIF encode failed:\n" + r2.stderr.decode(errors="replace")[-2000:])
        sys.exit(1)

    size = os.path.getsize(OUT)
    print(f"[gif] Done   : {OUT}  ({size/1024/1024:.1f} MB)")
    print(f"[gif] Tip    : larger fps → bigger file but smoother animation")
    print(f"[gif] Rebuild: make && make run")


if __name__ == "__main__":
    inp = sys.argv[1] if len(sys.argv) > 1 else "in_this_video_remove_apple_ima.mp4"
    w   = int(sys.argv[2]) if len(sys.argv) > 2 else 1280
    h   = int(sys.argv[3]) if len(sys.argv) > 3 else 720
    fps = int(sys.argv[4]) if len(sys.argv) > 4 else 12
    make_gif(inp, w, h, fps)
