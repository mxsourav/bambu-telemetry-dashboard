#!/usr/bin/env python3
"""
tools/create_delta_icons.py
Generates official STΔN studio Delta icons in SVG and PNG resolutions (192px and 512px).
"""

import os
import argparse
from PIL import Image, ImageDraw

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_WEB_DIR = os.path.join(REPO_ROOT, "web")


def generate_icons(output_dir=DEFAULT_WEB_DIR):
    os.makedirs(output_dir, exist_ok=True)

    # 512x512 master
    img512 = Image.new("RGB", (512, 512), (8, 8, 10))
    draw = ImageDraw.Draw(img512)

    top = (256, 108)
    bl = (108, 368)
    br = (404, 368)

    stroke_w = 46
    draw.line([top, br, bl, top], fill=(255, 255, 255), width=stroke_w, joint="curve")
    r = stroke_w // 2
    for pt in [top, bl, br]:
        draw.ellipse([pt[0] - r, pt[1] - r, pt[0] + r, pt[1] + r], fill=(255, 255, 255))

    # Save 512
    p512_delta = os.path.join(output_dir, "icon-delta-512.png")
    p512_std = os.path.join(output_dir, "icon-512.png")
    img512.save(p512_delta, optimize=True)
    img512.save(p512_std, optimize=True)

    # Save 192
    img192 = img512.resize((192, 192), Image.Resampling.LANCZOS)
    p192_delta = os.path.join(output_dir, "icon-delta-192.png")
    p192_std = os.path.join(output_dir, "icon-192.png")
    img192.save(p192_delta, optimize=True)
    img192.save(p192_std, optimize=True)

    svg = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512">
  <rect width="512" height="512" rx="112" fill="#08080a"/>
  <path d="M256 108 L404 368 L108 368 Z" fill="none" stroke="#FFFFFF" stroke-width="46" stroke-linejoin="round" stroke-linecap="round"/>
</svg>
"""
    svg_delta = os.path.join(output_dir, "icon-delta.svg")
    svg_std = os.path.join(output_dir, "icon.svg")
    with open(svg_delta, "w", encoding="utf-8") as f:
        f.write(svg)
    with open(svg_std, "w", encoding="utf-8") as f:
        f.write(svg)

    print(f"Icons generated successfully in {output_dir}!")
    return [p512_delta, p512_std, p192_delta, p192_std, svg_delta, svg_std]


def main():
    parser = argparse.ArgumentParser(description="Generate STAN Delta icons in SVG and PNG formats")
    parser.add_argument("--output-dir", "-o", default=DEFAULT_WEB_DIR, help="Destination directory for icons")
    args = parser.parse_args()

    generate_icons(args.output_dir)


if __name__ == "__main__":
    main()
