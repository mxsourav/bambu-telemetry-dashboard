#!/usr/bin/env python3
import gzip
import os

with open('web/index.html', 'rb') as f:
    raw_html = f.read()

# Gzip compress with max compression and fixed mtime
gz_data = gzip.compress(raw_html, compresslevel=9, mtime=0)

out_path = 'src/web_ui.h'
with open(out_path, 'w', encoding='utf-8') as f:
    f.write('/* Auto-generated gzipped Web UI header for STΔN studio */\n')
    f.write('#pragma once\n')
    f.write('#include <Arduino.h>\n\n')
    f.write(f'const uint32_t WEB_UI_GZ_LEN = {len(gz_data)};\n')
    f.write(f'const uint32_t WEB_UI_HTML_GZ_LEN = {len(gz_data)};\n')
    f.write('const uint8_t WEB_UI_GZ[] PROGMEM = {\n  ')
    for i, b in enumerate(gz_data):
        f.write(f'0x{b:02X},')
        if (i + 1) % 16 == 0:
            f.write('\n  ')
        elif (i + 1) % 8 == 0:
            f.write(' ')
    f.write('\n};\n')

print(f"Generated {out_path}: raw={len(raw_html)} bytes -> gzipped={len(gz_data)} bytes ({len(gz_data)*100//len(raw_html)}%)")
