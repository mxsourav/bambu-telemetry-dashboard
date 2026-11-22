#!/usr/bin/env python3
import os

with open('web/manifest.json', 'r', encoding='utf-8') as f:
    manifest = f.read()

with open('web/sw.js', 'r', encoding='utf-8') as f:
    sw = f.read()

with open('web/icon.svg', 'r', encoding='utf-8') as f:
    icon_svg = f.read()

with open('web/icon-192.png', 'rb') as f:
    icon192 = f.read()

with open('web/icon-512.png', 'rb') as f:
    icon512 = f.read()

with open('src/pwa_assets.h', 'w', encoding='utf-8') as f:
    f.write('/* Auto-generated PWA assets for STΔN studio. */\n#pragma once\n#include <Arduino.h>\n\n')
    f.write('static const char PWA_MANIFEST[] PROGMEM = R"rawliteral(' + manifest.strip() + ')rawliteral";\n\n')
    f.write('static const char PWA_SW[] PROGMEM = R"rawliteral(' + sw.strip() + ')rawliteral";\n\n')
    f.write('static const char PWA_ICON_SVG[] PROGMEM = R"rawliteral(' + icon_svg.strip() + ')rawliteral";\n\n')
    
    f.write(f'#define PWA_ICON_192_LEN {len(icon192)}\n')
    f.write('static const uint8_t PWA_ICON_192[] PROGMEM = {\n  ')
    for i, b in enumerate(icon192):
        f.write(f'0x{b:02x},')
        if (i + 1) % 16 == 0:
            f.write('\n  ')
    f.write('\n};\n\n')

    f.write(f'#define PWA_ICON_512_LEN {len(icon512)}\n')
    f.write('static const uint8_t PWA_ICON_512[] PROGMEM = {\n  ')
    for i, b in enumerate(icon512):
        f.write(f'0x{b:02x},')
        if (i + 1) % 16 == 0:
            f.write('\n  ')
    f.write('\n};\n')

print(f"Generated src/pwa_assets.h: icon192={len(icon192)}B, icon512={len(icon512)}B")
