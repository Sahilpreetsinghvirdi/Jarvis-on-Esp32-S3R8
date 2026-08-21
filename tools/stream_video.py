"""
JARVIS WiFi UDP Video Streamer
Streams video frames from laptop to ESP32 over WiFi UDP.

Usage:
  python stream_video.py <video_file> [esp32_ip] [fps]

Requirements:
  pip install opencv-python numpy
"""

import cv2
import numpy as np
import socket
import struct
import time
import sys

# Config
UDP_PORT = 9999
CHUNK_SIZE = 1400
DISPLAY_W = 160
DISPLAY_H = 86


def rgb888_to_rgb565(frame):
    """Convert BGR numpy frame to RGB565 little-endian for ESP32."""
    r = frame[:, :, 2].astype(np.uint16) >> 3
    g = frame[:, :, 1].astype(np.uint16) >> 2
    b = frame[:, :, 0].astype(np.uint16) >> 3
    return ((r << 11) | (g << 5) | b).astype(np.uint16)


def send_frame(sock, esp32_ip, frame_id, frame_data):
    """Send a frame as chunked UDP packets."""
    total = len(frame_data)
    chunks = (total + CHUNK_SIZE - 1) // CHUNK_SIZE

    for i in range(chunks):
        start = i * CHUNK_SIZE
        end = min(start + CHUNK_SIZE, total)
        chunk = frame_data[start:end]

        # Header: frame_id (4) + chunk_idx (2) + total_chunks (2) = 8 bytes
        header = struct.pack("<IHH", frame_id, i, chunks)
        sock.sendto(header + chunk, (esp32_ip, UDP_PORT))
        time.sleep(0.00005)  # 50us inter-packet delay


def main():
    if len(sys.argv) < 2:
        print("Usage: python stream_video.py <video_file> [esp32_ip] [fps]")
        print("  video_file: path to video (mp4, avi, etc.)")
        print("  esp32_ip:   ESP32's IP address (default: 192.168.1.100)")
        print("  fps:        target framerate (default: video's native fps)")
        return

    video_path = sys.argv[1]
    esp32_ip = sys.argv[2] if len(sys.argv) > 2 else "192.168.1.100"
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

    frame_delay = 1.0 / target_fps

    print(f"Video: {width}x{height} @ {native_fps:.1f}fps, {total_frames} frames")
    print(f"Streaming to {esp32_ip}:{UDP_PORT} @ {target_fps:.1f}fps")
    print(f"Frame size: {DISPLAY_W}x{DISPLAY_H} x 2 = {DISPLAY_W * DISPLAY_H * 2} bytes")
    print(f"Packets per frame: {(DISPLAY_W * DISPLAY_H * 2 + CHUNK_SIZE - 1) // CHUNK_SIZE}")
    print("Press 'q' in preview window to stop\n")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    frame_id = 0

    try:
        while True:
            ret, frame = cap.read()
            if not ret:
                print(f"\nLooping video...")
                cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                continue

            frame = cv2.resize(frame, (DISPLAY_W, DISPLAY_H))
            frame565 = rgb888_to_rgb565(frame)
            frame_bytes = frame565.tobytes()

            t0 = time.time()
            send_frame(sock, esp32_ip, frame_id, frame_bytes)
            elapsed = time.time() - t0

            frame_id += 1
            if frame_id % 30 == 0:
                print(f"  Sent {frame_id}/{total_frames} frames ({elapsed * 1000:.0f}ms send time)")

            # Preview
            preview = cv2.resize(frame, (480, 258))
            cv2.putText(preview, f"Frame {frame_id} | {target_fps:.0f}fps -> {esp32_ip}", (10, 25),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 1)
            cv2.imshow("Streaming to ESP32", preview)

            wait_ms = max(1, int(frame_delay * 1000))
            if cv2.waitKey(wait_ms) & 0xFF == ord("q"):
                break

            # Maintain target fps
            sleep_time = frame_delay - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)

    except KeyboardInterrupt:
        print("\nStopped by user")
    finally:
        sock.close()
        cap.release()
        cv2.destroyAllWindows()
        print(f"Sent {frame_id} frames total")


if __name__ == "__main__":
    main()
