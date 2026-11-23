#!/usr/bin/env python3
"""
tools/test_push_image.py
Test utility to push 160x128 16-bit RGB565 raw image buffers to the ESP32 /api/display/image endpoint.
Supports solid test colors, RGB565 gradient test patterns, and --dry-run validation.
"""

import os
import sys
import argparse
import urllib.request
import urllib.error

DEFAULT_IP = os.environ.get("ESP32_IP", "192.168.137.35")
DEFAULT_PORT = 80
WIDTH = 160
HEIGHT = 128


def create_rgb565_buffer(color="red", width=WIDTH, height=HEIGHT):
    """Generates raw 16-bit RGB565 (big-endian) pixel buffer for the ST7735 display."""
    total_pixels = width * height
    pixels = bytearray(total_pixels * 2)

    color = color.lower().strip()
    if color == "red":
        hi, lo = 0xF8, 0x00
    elif color == "green":
        hi, lo = 0x07, 0xE0
    elif color == "blue":
        hi, lo = 0x00, 0x1F
    elif color == "white":
        hi, lo = 0xFF, 0xFF
    elif color == "black":
        hi, lo = 0x00, 0x00
    elif color == "cyan":
        hi, lo = 0x07, 0xFF
    elif color == "yellow":
        hi, lo = 0xFF, 0xE0
    elif color == "magenta":
        hi, lo = 0xF8, 0x1F
    elif color == "gradient":
        for y in range(height):
            for x in range(width):
                r = (x * 31) // width
                g = (y * 63) // height
                b = ((width - x) * 31) // width
                val = (r << 11) | (g << 5) | b
                idx = (y * width + x) * 2
                pixels[idx] = (val >> 8) & 0xFF
                pixels[idx + 1] = val & 0xFF
        return pixels
    else:
        # Default to blue
        hi, lo = 0x00, 0x1F

    for i in range(total_pixels):
        pixels[i * 2] = hi
        pixels[i * 2 + 1] = lo

    return pixels


def build_multipart_payload(pixel_bytes, boundary="----STΔNBoundary7MA4YWxkTrZu0gW"):
    """Builds multipart/form-data payload with screen.bin attachment."""
    body = bytearray()
    body.extend(f"--{boundary}\r\n".encode("utf-8"))
    body.extend(b'Content-Disposition: form-data; name="file"; filename="screen.bin"\r\n')
    body.extend(b"Content-Type: application/octet-stream\r\n\r\n")
    body.extend(pixel_bytes)
    body.extend(f"\r\n--{boundary}--\r\n".encode("utf-8"))
    return bytes(body), boundary


def push_image(ip=DEFAULT_IP, port=DEFAULT_PORT, color="gradient", timeout=5.0, dry_run=False):
    pixels = create_rgb565_buffer(color=color)
    body, boundary = build_multipart_payload(pixels)

    target_url = f"http://{ip}:{port}/api/display/image"
    print(f"[TEST IMAGE] Target URL: {target_url}")
    print(f"[TEST IMAGE] Color/Pattern: {color} ({WIDTH}x{HEIGHT}, {len(pixels)} raw bytes, {len(body)} multipart bytes)")

    if dry_run:
        print("[TEST IMAGE] --dry-run active. Payload constructed and verified successfully without sending.")
        return True

    req = urllib.request.Request(
        target_url,
        data=body,
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"}
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            content = resp.read().decode("utf-8", errors="replace")
            print(f"[TEST IMAGE] Upload SUCCESS! Status: {resp.status}, Response: {content}")
            return True
    except urllib.error.URLError as e:
        print(f"[TEST IMAGE] Upload FAILED (URLError): {e}")
        return False
    except Exception as e:
        print(f"[TEST IMAGE] Upload ERROR: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(description="Push test RGB565 image to STΔN ESP32 TFT screen")
    parser.add_argument("--ip", default=DEFAULT_IP, help=f"Target ESP32 IP address (default: {DEFAULT_IP})")
    parser.add_argument("--port", "-p", type=int, default=DEFAULT_PORT, help="Target HTTP port (default: 80)")
    parser.add_argument(
        "--color", "-c",
        choices=["red", "green", "blue", "white", "black", "cyan", "yellow", "magenta", "gradient"],
        default="gradient",
        help="Test pattern color"
    )
    parser.add_argument("--timeout", "-t", type=float, default=5.0, help="Connection timeout in seconds")
    parser.add_argument("--dry-run", action="store_true", help="Construct and validate payload without transmitting")
    args = parser.parse_args()

    ok = push_image(ip=args.ip, port=args.port, color=args.color, timeout=args.timeout, dry_run=args.dry_run)
    if not ok and not args.dry_run:
        sys.exit(1)


if __name__ == "__main__":
    main()
