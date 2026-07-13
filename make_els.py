#!/usr/bin/env python3
"""
make_els.py — Convert any video to Elsea Video (.els) format for ElseaOS.

Usage:
  python3 make_els.py <input.mp4> <output.els> [width] [height] [fps]

Defaults: 256x144 @ 25fps
FPS controls playback speed: higher = faster, lower = slower.

.els binary layout:
  [0-3]   "ELS1" magic
  [4-5]   src_width  (uint16 LE)
  [6-7]   src_height (uint16 LE)
  [8-9]   fps        (uint16 LE)
  [10-13] frame_count (uint32 LE)
  [14-17] audio_samples (uint32 LE) -- 8-bit unsigned mono 22050Hz
  [18-31] reserved (zeros)
  [32 ..]           audio PCM data
  [32+audio ..]     frame_count * (src_w * src_h * 3) bytes raw RGB24
"""

import subprocess, struct, sys, os

FFMPEG = os.path.join(os.path.dirname(__file__), "ffmpeg_dir", "ffmpeg")

def make_els(input_file, output_file, width=256, height=144, fps=25):
    if not os.path.exists(FFMPEG):
        print(f"ERROR: ffmpeg not found at {FFMPEG}", file=sys.stderr)
        sys.exit(1)
    if not os.path.exists(input_file):
        print(f"ERROR: input file not found: {input_file}", file=sys.stderr)
        sys.exit(1)

    frame_size = width * height * 3

    print(f"[els] Input  : {input_file}")
    print(f"[els] Output : {output_file}")
    print(f"[els] Res    : {width}x{height} @ {fps}fps")
    print(f"[els] Note   : Use els_set_fps() in kernel.c to change playback speed")

    print("[els] Extracting video frames...")
    vproc = subprocess.run(
        [FFMPEG, "-y", "-i", input_file,
         "-vf", f"scale={width}:{height}",
         "-r", str(fps),
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    if vproc.returncode != 0:
        print("[els] ffmpeg stderr:", vproc.stderr.decode(errors="replace")[-2000:])
        sys.exit(1)

    raw_video = vproc.stdout
    frame_count = len(raw_video) // frame_size
    if frame_count == 0:
        print("ERROR: no frames extracted", file=sys.stderr)
        sys.exit(1)
    raw_video = raw_video[:frame_count * frame_size]
    print(f"[els] Frames : {frame_count}  ({frame_count/fps:.1f}s at {fps}fps)")

    print("[els] Extracting audio...")
    aproc = subprocess.run(
        [FFMPEG, "-y", "-i", input_file,
         "-f", "u8", "-ac", "1", "-ar", "22050", "-"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    audio_data = aproc.stdout if aproc.returncode == 0 else b""
    audio_samples = len(audio_data)
    print(f"[els] Audio  : {audio_samples//1024} KB  ({audio_samples/22050:.1f}s)")

    total = 32 + audio_samples + len(raw_video)
    print(f"[els] Writing {total/1024/1024:.1f} MB → {output_file} ...")

    with open(output_file, "wb") as f:
        f.write(b"ELS1")
        f.write(struct.pack("<H", width))
        f.write(struct.pack("<H", height))
        f.write(struct.pack("<H", fps))
        f.write(struct.pack("<I", frame_count))
        f.write(struct.pack("<I", audio_samples))
        f.write(b"\x00" * 14)
        f.write(audio_data)
        f.write(raw_video)

    size_mb = os.path.getsize(output_file) / 1024 / 1024
    print(f"[els] Done   : {output_file}  ({size_mb:.1f} MB)")
    print(f"[els] Speed tips:")
    print(f"[els]   Faster → els_set_fps({fps*2}) in kernel.c  (2x speed)")
    print(f"[els]   Slower → els_set_fps({fps//2}) in kernel.c  (0.5x speed)")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 make_els.py <input.mp4> <output.els> [width] [height] [fps]")
        sys.exit(1)
    inp = sys.argv[1]
    out = sys.argv[2]
    w   = int(sys.argv[3]) if len(sys.argv) > 3 else 256
    h   = int(sys.argv[4]) if len(sys.argv) > 4 else 144
    fps = int(sys.argv[5]) if len(sys.argv) > 5 else 25
    make_els(inp, out, w, h, fps)
