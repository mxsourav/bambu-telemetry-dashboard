#!/usr/bin/env python3
"""
tools/build_web_ui_header.py
Packs and compresses web/index.html into a PROGMEM C-array (src/web_ui.h)
for flashing directly to ESP32 SPIFFS or flash memory.
"""

import gzip
import os
import sys
import argparse

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_INPUT = os.path.join(REPO_ROOT, "web", "index.html")
DEFAULT_OUTPUT = os.path.join(REPO_ROOT, "src", "web_ui.h")


def build_web_ui_header(input_path=DEFAULT_INPUT, output_path=DEFAULT_OUTPUT, check_only=False):
    if not os.path.isfile(input_path):
        raise FileNotFoundError(f"Input HTML file not found: {input_path}")

    with open(input_path, "rb") as f:
        raw_html = f.read()

    # Gzip compress with max compression and fixed mtime
    gz_data = gzip.compress(raw_html, compresslevel=9, mtime=0)

    header_content = (
        "/* Auto-generated gzipped Web UI header for STΔN studio */\n"
        "#pragma once\n"
        "#include <Arduino.h>\n\n"
        f"const uint32_t WEB_UI_GZ_LEN = {len(gz_data)};\n"
        f"const uint32_t WEB_UI_HTML_GZ_LEN = {len(gz_data)};\n"
        "const uint8_t WEB_UI_GZ[] PROGMEM = {\n  "
    )

    lines = []
    for i, b in enumerate(gz_data):
        part = f"0x{b:02X},"
        if (i + 1) % 16 == 0:
            part += "\n  "
        elif (i + 1) % 8 == 0:
            part += " "
        lines.append(part)

    header_content += "".join(lines)
    header_content += "\n};\n"

    if check_only:
        if os.path.exists(output_path):
            with open(output_path, "r", encoding="utf-8") as f:
                existing = f.read()
            if existing == header_content:
                print(f"[CHECK] {output_path} is up to date with {input_path}.")
                return True
            else:
                print(f"[CHECK] {output_path} differs from current {input_path}.")
                return False
        else:
            print(f"[CHECK] {output_path} does not exist.")
            return False

    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(header_content)

    ratio = len(gz_data) * 100 // len(raw_html) if len(raw_html) else 0
    print(f"Generated {output_path}: raw={len(raw_html)} bytes -> gzipped={len(gz_data)} bytes ({ratio}%)")
    return True


def main():
    parser = argparse.ArgumentParser(description="Compile web/index.html to compressed C-header for ESP32")
    parser.add_argument("--input", "-i", default=DEFAULT_INPUT, help="Path to input HTML file")
    parser.add_argument("--output", "-o", default=DEFAULT_OUTPUT, help="Path to output .h file")
    parser.add_argument("--check", action="store_true", help="Check whether output matches current input without writing")
    args = parser.parse_args()

    success = build_web_ui_header(args.input, args.output, check_only=args.check)
    if args.check and not success:
        sys.exit(1)


if __name__ == "__main__":
    main()
