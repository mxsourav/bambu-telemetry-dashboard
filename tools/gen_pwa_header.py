#!/usr/bin/env python3
"""
tools/gen_pwa_header.py
Packs web PWA manifest, service worker, and icon assets into src/pwa_assets.h.
"""

import os
import sys
import argparse

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_WEB_DIR = os.path.join(REPO_ROOT, "web")
DEFAULT_OUTPUT = os.path.join(REPO_ROOT, "src", "pwa_assets.h")


def generate_pwa_header(web_dir=DEFAULT_WEB_DIR, output_path=DEFAULT_OUTPUT, dry_run=False):
    manifest_path = os.path.join(web_dir, "manifest.json")
    sw_path = os.path.join(web_dir, "sw.js")
    icon_svg_path = os.path.join(web_dir, "icon.svg")
    icon192_path = os.path.join(web_dir, "icon-192.png")
    icon512_path = os.path.join(web_dir, "icon-512.png")

    for p in [manifest_path, sw_path, icon_svg_path, icon192_path, icon512_path]:
        if not os.path.isfile(p):
            raise FileNotFoundError(f"Required PWA asset not found: {p}")

    with open(manifest_path, "r", encoding="utf-8") as f:
        manifest = f.read()

    with open(sw_path, "r", encoding="utf-8") as f:
        sw = f.read()

    with open(icon_svg_path, "r", encoding="utf-8") as f:
        icon_svg = f.read()

    with open(icon192_path, "rb") as f:
        icon192 = f.read()

    with open(icon512_path, "rb") as f:
        icon512 = f.read()

    header_parts = [
        "/* Auto-generated PWA assets for STΔN studio. */\n",
        "#pragma once\n",
        "#include <Arduino.h>\n\n",
        'static const char PWA_MANIFEST[] PROGMEM = R"rawliteral(' + manifest.strip() + ')rawliteral";\n\n',
        'static const char PWA_SW[] PROGMEM = R"rawliteral(' + sw.strip() + ')rawliteral";\n\n',
        'static const char PWA_ICON_SVG[] PROGMEM = R"rawliteral(' + icon_svg.strip() + ')rawliteral";\n\n',
        f"#define PWA_ICON_192_LEN {len(icon192)}\n",
        "static const uint8_t PWA_ICON_192[] PROGMEM = {\n  "
    ]

    for i, b in enumerate(icon192):
        header_parts.append(f"0x{b:02x},")
        if (i + 1) % 16 == 0:
            header_parts.append("\n  ")
    header_parts.append("\n};\n\n")

    header_parts.append(f"#define PWA_ICON_512_LEN {len(icon512)}\n")
    header_parts.append("static const uint8_t PWA_ICON_512[] PROGMEM = {\n  ")
    for i, b in enumerate(icon512):
        header_parts.append(f"0x{b:02x},")
        if (i + 1) % 16 == 0:
            header_parts.append("\n  ")
    header_parts.append("\n};\n")

    full_header = "".join(header_parts)

    if not dry_run:
        os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
        with open(output_path, "w", encoding="utf-8") as f:
            f.write(full_header)

    print(f"Generated {output_path}: icon192={len(icon192)}B, icon512={len(icon512)}B")
    return True


def main():
    parser = argparse.ArgumentParser(description="Pack web/ PWA assets into Arduino C-header")
    parser.add_argument("--web-dir", default=DEFAULT_WEB_DIR, help="Path to web directory containing manifest and icons")
    parser.add_argument("--output", "-o", default=DEFAULT_OUTPUT, help="Path to output header file")
    parser.add_argument("--dry-run", action="store_true", help="Validate assets without writing header")
    args = parser.parse_args()

    generate_pwa_header(web_dir=args.web_dir, output_path=args.output, dry_run=args.dry_run)


if __name__ == "__main__":
    main()
