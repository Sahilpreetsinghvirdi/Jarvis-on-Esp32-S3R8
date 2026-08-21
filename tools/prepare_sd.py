"""
JARVIS SD Card Movie Preprocessor
Converts a video file to raw RGB565 frames on an SD card.

Usage:
  python prepare_sd.py <video_file> <sd_card_path> [fps]

Requirements:
  pip install opencv-python numpy

SD card structure created:
  /MOVIE/meta.txt          <- width height total_frames fps
  /MOVIE/frames/000000.rgb <- raw RGB565 frame data
  /MOVIE/frames/000001.rgb
  ...
"""

import cv2
import numpy as np
import os
import sys
import struct

# Config
DISPLAY_W = 320
DISPLAY_H = 172


def rgb888_to_rgb565_le(frame):
    """Convert BGR numpy frame to RGB565 little-endian (for ESP32 direct read)."""
    r = frame[:, :, 2].astype(np.uint16) >> 3
    g = frame[:, :, 1].astype(np.uint16) >> 2
    b = frame[:, :, 0].astype(np.uint16) >> 3
    rgb565 = ((r << 11) | (g << 5) | b).astype(np.uint16)
    return rgb565  # little-endian, ESP32 native byte order


def main():
    if len(sys.argv) < 3:
        print("Usage: python prepare_sd.py <video_file> <sd_card_path> [fps]")
        print("  video_file:  path to video (mp4, avi, etc.)")
        print("  sd_card_path: root of SD card (e.g., D:\\ or /media/user/SD)")
        print("  fps:         target framerate (default: video's native fps)")
        return

    video_path = sys.argv[1]
    sd_path = sys.argv[2]
    target_fps = float(sys.argv[3]) if len(sys.argv) > 3 else 0

    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        print(f"Error: Cannot open {video_path}")
        return

    native_fps = cap.get(cv2.CAP_PROP_FPS)
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

    if target_fps <= 0:
        target_fps = native_fps

    # Calculate which frames to extract (skip frames if downsampling fps)
    skip = 1
    if target_fps < native_fps:
        skip = int(native_fps / target_fps)
        out_frames = total_frames // skip
    else:
        out_frames = total_frames
        target_fps = native_fps

    frame_size = DISPLAY_W * DISPLAY_H * 2
    est_size_mb = (out_frames * frame_size) / (1024 * 1024)

    print(f"Video: {width}x{height} @ {native_fps:.1f}fps, {total_frames} frames")
    print(f"Output: {DISPLAY_W}x{DISPLAY_H} @ {target_fps:.1f}fps, {out_frames} frames")
    print(f"Frame size: {frame_size} bytes | Total: {est_size_mb:.1f} MB")
    print()

    if est_size_mb > 512:
        print(f"WARNING: {est_size_mb:.0f}MB is large. Ensure SD card has enough space.")
        resp = input("Continue? (y/n): ")
        if resp.lower() != "y":
            return

    # Create directories
    movie_dir = os.path.join(sd_path, "MOVIE")
    frames_dir = os.path.join(movie_dir, "frames")
    os.makedirs(frames_dir, exist_ok=True)

    # Write meta.txt
    meta_path = os.path.join(movie_dir, "meta.txt")
    with open(meta_path, "w") as f:
        f.write(f"{DISPLAY_W} {DISPLAY_H} {out_frames} {target_fps:.2f}\n")
    print(f"Wrote {meta_path}")

    # Extract and convert frames
    print("Converting frames...")
    frame_idx = 0
    written = 0
    last_pct = -1

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        frame_idx += 1
        if (frame_idx - 1) % skip != 0:
            continue

        frame = cv2.resize(frame, (DISPLAY_W, DISPLAY_H))
        frame565 = rgb888_to_rgb565_le(frame)

        out_path = os.path.join(frames_dir, f"{written:06d}.rgb")
        with open(out_path, "wb") as f:
            f.write(frame565.tobytes())

        written += 1
        pct = (written * 100) // out_frames
        if pct != last_pct and pct % 5 == 0:
            print(f"  {pct}% ({written}/{out_frames} frames)")
            last_pct = pct

    cap.release()
    print(f"\nDone! Wrote {written} frames to {frames_dir}")
    print(f"Insert SD card into ESP32 SD module and run: !movie sd")


if __name__ == "__main__":
    main()
