"""
JARVIS SD Card Photo Converter
Converts JPG/PNG photos to RGB565 format for ESP32 display.
Auto-detects portrait vs landscape orientation.

Usage:
  python prepare_photos.py <photos_dir> <sd_card_path>

Requirements:
  pip install Pillow

SD card structure created:
  /PHOTOS/meta.txt          <- count, then "filename.rgb orientation" per line
  /PHOTOS/000001.rgb        <- raw RGB565 photo data
  /PHOTOS/000002.rgb
  ...
"""

import os
import sys
from PIL import Image

SCREEN_LAND_W = 320
SCREEN_LAND_H = 172
SCREEN_PORT_W = 172
SCREEN_PORT_H = 320


def image_to_rgb565_le(img):
    """Convert PIL Image (RGB) to RGB565 little-endian bytes."""
    pixels = img.load()
    w, h = img.size
    data = bytearray(w * h * 2)
    idx = 0
    for y in range(h):
        for x in range(w):
            r, g, b = pixels[x, y]
            rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            data[idx] = rgb565 & 0xFF
            data[idx + 1] = (rgb565 >> 8) & 0xFF
            idx += 2
    return data


def main():
    if len(sys.argv) < 3:
        print("Usage: python prepare_photos.py <photos_dir> <sd_card_path>")
        print("  photos_dir:  folder containing JPG/PNG images")
        print("  sd_card_path: root of SD card (e.g., D:\\ or /media/user/SD)")
        return

    photos_dir = sys.argv[1]
    sd_path = sys.argv[2]

    if not os.path.isdir(photos_dir):
        print(f"Error: {photos_dir} is not a directory")
        return

    # Collect image files
    extensions = ('.jpg', '.jpeg', '.png', '.bmp')
    files = sorted([f for f in os.listdir(photos_dir)
                    if f.lower().endswith(extensions)])

    if not files:
        print(f"No image files found in {photos_dir}")
        return

    print(f"Found {len(files)} images in {photos_dir}")
    print()

    # Create output directory
    photos_out = os.path.join(sd_path, "PHOTOS")
    os.makedirs(photos_out, exist_ok=True)

    meta_lines = []
    total_size = 0

    for i, filename in enumerate(files):
        filepath = os.path.join(photos_dir, filename)
        img = Image.open(filepath).convert('RGB')
        w, h = img.size

        # Determine orientation
        if w >= h:
            orientation = 'L'
            target_w, target_h = SCREEN_LAND_W, SCREEN_LAND_H
        else:
            orientation = 'P'
            target_w, target_h = SCREEN_PORT_W, SCREEN_PORT_H

        # Resize to fit screen while maintaining aspect ratio
        img_ratio = w / h
        target_ratio = target_w / target_h

        if img_ratio > target_ratio:
            new_w = target_w
            new_h = int(target_w / img_ratio)
        else:
            new_h = target_h
            new_w = int(target_h * img_ratio)

        img = img.resize((new_w, new_h), Image.LANCZOS)

        # Create black canvas and center the image
        canvas = Image.new('RGB', (target_w, target_h), (0, 0, 0))
        offset_x = (target_w - new_w) // 2
        offset_y = (target_h - new_h) // 2
        canvas.paste(img, (offset_x, offset_y))

        # Convert to RGB565
        rgb565_data = image_to_rgb565_le(canvas)

        # Save .rgb file
        out_name = f"{i + 1:06d}.rgb"
        out_path = os.path.join(photos_out, out_name)
        with open(out_path, 'wb') as f:
            f.write(rgb565_data)

        file_size = len(rgb565_data)
        total_size += file_size

        # Display name: use original filename without extension, truncated
        display_name = os.path.splitext(filename)[0]
        if len(display_name) > 30:
            display_name = display_name[:30]

        meta_lines.append(f"{out_name} {orientation} {display_name}")
        print(f"  [{i + 1:3d}/{len(files)}] {filename}")
        print(f"          -> {out_name} | {orientation} | {w}x{h} -> {target_w}x{target_h} | {file_size} bytes")

    # Write meta.txt
    meta_path = os.path.join(photos_out, "meta.txt")
    with open(meta_path, 'w') as f:
        f.write(f"{len(meta_lines)}\n")
        for line in meta_lines:
            f.write(line + "\n")

    print()
    print(f"Done! {len(meta_lines)} photos converted")
    print(f"Total size: {total_size / 1024 / 1024:.1f} MB")
    print(f"Output: {photos_out}")
    print(f"meta.txt: {meta_path}")
    print()
    print("SD card structure:")
    print("  /PHOTOS/meta.txt")
    print("  /PHOTOS/000001.rgb")
    print("  /PHOTOS/000002.rgb")
    print("  ...")


if __name__ == "__main__":
    main()
