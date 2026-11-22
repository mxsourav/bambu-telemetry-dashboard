#!/usr/bin/env python3
"""
tools/render_doc_images.py
Generates pixel-perfect, clean, high-contrast Dieter Rams minimalist documentation visuals
for README.md (320x256 resolution for every card):
1. bambu_printer_telemetry.png
2. desk_pet_crying.png (Breakup phase: anime watering tears, downcast eyes, booze bottle)
3. desk_pet_dancing.png (Attitude swagger lean, half-lidded confidence, rhythm beat wave)
4. desk_pet_neutral.png
5. desk_pet_angry.png
6. desk_pet_calm.png
7. pc_hardware_hud.png
8. spotify_media_sync.png
"""
import os
import math
from PIL import Image, ImageDraw, ImageFont

OUT_DIR = "docs/images"
os.makedirs(OUT_DIR, exist_ok=True)

# Fonts
font_title = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 16)
font_large = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 26)
font_bold  = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 13)
font_mono  = ImageFont.truetype("C:/Windows/Fonts/consola.ttf", 12)
font_small = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 11)
font_tiny  = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 9)

# Colors
COL_DARK_BG = (12, 12, 14)
COL_CARD_BG = (22, 22, 26)
COL_BORDER  = (38, 38, 44)
COL_CYAN    = (0, 242, 254)
COL_GREEN   = (16, 185, 129)
COL_AMBER   = (245, 158, 11)
COL_RED     = (248, 113, 113)
COL_WHITE   = (255, 255, 255)
COL_MUTED   = (156, 163, 175)


def render_bambu_telemetry():
    """Generates clean, authentic Bambu Lab P1P telemetry dashboard."""
    im = Image.new("RGB", (320, 256), COL_DARK_BG)
    d = ImageDraw.Draw(im)

    # Top Status Bar
    d.rectangle([0, 0, 320, 32], fill=(18, 18, 22))
    d.line([(0, 32), (320, 32)], fill=COL_BORDER, width=1)
    
    # Bambu Lab Logo / Title
    d.text((12, 8), "BAMBU LAB P1P", fill=COL_WHITE, font=font_bold)
    
    # Status Pill: PRINTING
    d.rounded_rectangle([220, 6, 308, 26], radius=10, fill=(6, 78, 59), outline=COL_GREEN, width=1)
    d.ellipse([228, 13, 234, 19], fill=COL_GREEN)
    d.text((240, 7), "PRINTING", fill=COL_GREEN, font=font_small)

    # Gauges Section (Nozzle & Bed)
    # Left Gauge: Nozzle
    d.rounded_rectangle([12, 42, 154, 118], radius=8, fill=COL_CARD_BG, outline=COL_BORDER, width=1)
    d.text((22, 48), "NOZZLE TEMP", fill=COL_MUTED, font=font_tiny)
    d.arc([22, 64, 74, 110], start=135, end=405, fill=(45, 45, 52), width=5)
    d.arc([22, 64, 74, 110], start=135, end=380, fill=COL_AMBER, width=5)
    d.text((82, 68), "220°C", fill=COL_WHITE, font=font_large)
    d.text((84, 96), "TARGET 220°C", fill=COL_AMBER, font=font_tiny)

    # Right Gauge: Bed
    d.rounded_rectangle([166, 42, 308, 118], radius=8, fill=COL_CARD_BG, outline=COL_BORDER, width=1)
    d.text((176, 48), "HEATBED TEMP", fill=COL_MUTED, font=font_tiny)
    d.arc([176, 64, 228, 110], start=135, end=405, fill=(45, 45, 52), width=5)
    d.arc([176, 64, 228, 110], start=135, end=340, fill=COL_GREEN, width=5)
    d.text((236, 68), "65°C", fill=COL_WHITE, font=font_large)
    d.text((238, 96), "TARGET 65°C", fill=COL_GREEN, font=font_tiny)

    # Print Progress Card
    d.rounded_rectangle([12, 126, 308, 244], radius=8, fill=COL_CARD_BG, outline=COL_BORDER, width=1)
    d.text((22, 134), "ACTIVE JOB: stan_mount_plate_v2.gcode", fill=COL_WHITE, font=font_small)

    # Large Progress & Progress Bar
    d.text((22, 154), "74%", fill=COL_CYAN, font=font_large)
    d.text((95, 162), "1h 14m remaining (ETA: 15:42)", fill=COL_MUTED, font=font_small)

    # Progress bar
    bar_x, bar_y, bar_w, bar_h = 22, 192, 276, 12
    d.rounded_rectangle([bar_x, bar_y, bar_x + bar_w, bar_y + bar_h], radius=6, fill=(35, 35, 42))
    d.rounded_rectangle([bar_x, bar_y, bar_x + int(bar_w * 0.74), bar_y + bar_h], radius=6, fill=COL_GREEN)

    # Stats Bottom Row
    d.text((22, 216), "Layer: 184 / 248", fill=COL_WHITE, font=font_mono)
    d.text((140, 216), "Speed: 100% (Standard)", fill=COL_MUTED, font=font_mono)
    d.text((260, 216), "Fan: 100%", fill=COL_CYAN, font=font_mono)

    im.save(os.path.join(OUT_DIR, "bambu_printer_telemetry.png"))
    print("Saved bambu_printer_telemetry.png")


def render_desk_pet_crying():
    """Generates authentic Breakup Phase / Crying Pet face with anime tears and booze bottle."""
    # Crystal anime blue background
    im = Image.new("RGB", (320, 256), (184, 213, 250))
    d = ImageDraw.Draw(im)

    # Downcast heartbroken eyes
    # Left eye: center (106, 122), size (76, 96), rounded squircle (r=26)
    lx0, ly0, lx1, ly1 = 68, 74, 144, 170
    rx0, ry0, rx1, ry1 = 176, 74, 252, 170
    
    # Eye base (pure black)
    d.rounded_rectangle([lx0, ly0, lx1, ly1], radius=26, fill=(0, 0, 0))
    d.rounded_rectangle([rx0, ry0, rx1, ry1], radius=26, fill=(0, 0, 0))

    # Downcast sad eyelids (slanting down from center)
    d.polygon([(60, 70), (150, 70), (146, 100), (60, 114)], fill=(184, 213, 250))
    d.polygon([(170, 70), (260, 70), (260, 114), (174, 100)], fill=(184, 213, 250))

    # Watery pooled specular reflections at bottom of eyes
    d.rounded_rectangle([78, 148, 134, 164], radius=8, fill=(0, 160, 255))
    d.rounded_rectangle([84, 154, 126, 162], radius=4, fill=(65, 210, 255))
    d.ellipse([88, 118, 98, 128], fill=(255, 255, 255))

    d.rounded_rectangle([186, 148, 242, 164], radius=8, fill=(0, 160, 255))
    d.rounded_rectangle([192, 154, 234, 162], radius=4, fill=(65, 210, 255))
    d.ellipse([196, 118, 206, 128], fill=(255, 255, 255))

    # Streaming teardrop tracks down both cheeks
    # Left cheek tear trickle
    d.line([(88, 170), (84, 230)], fill=(0, 160, 255), width=4)
    d.ellipse([80, 222, 92, 238], fill=(65, 210, 255))
    d.ellipse([83, 225, 89, 233], fill=(255, 255, 255))

    # Right cheek tear trickle
    d.line([(232, 170), (236, 226)], fill=(0, 160, 255), width=4)
    d.ellipse([230, 218, 242, 234], fill=(65, 210, 255))
    d.ellipse([233, 221, 239, 229], fill=(255, 255, 255))

    # Sorrowful downturned quivering mouth
    d.arc([145, 184, 175, 204], start=210, end=330, fill=(0, 0, 0), width=4)

    # Minimalist Dieter Rams Alcohol / Booze Bottle on right side
    bx, by = 265, 145
    # Bottle body (22w x 44h)
    d.rounded_rectangle([bx - 10, by, bx + 12, by + 44], radius=4, fill=(33, 16, 4))
    # Amber whiskey / liquor fill inside
    d.rectangle([bx - 8, by + 10, bx + 10, by + 40], fill=(251, 190, 0))
    d.rectangle([bx - 8, by + 30, bx + 10, by + 40], fill=(179, 110, 0))
    # Modern white minimalist label with center stripe
    d.rectangle([bx - 8, by + 16, bx + 10, by + 28], fill=(255, 255, 255))
    d.line([(bx - 6, by + 22), (bx + 8, by + 22)], fill=(0, 0, 0), width=2)
    # Bottle neck & cork
    d.rectangle([bx - 4, by - 14, bx + 6, by], fill=(33, 16, 4))
    d.rectangle([bx - 4, by - 18, bx + 6, by - 12], fill=(154, 96, 0))
    # Amber droplet glint
    d.ellipse([bx - 6, by - 14, bx - 2, by - 10], fill=(251, 190, 0))

    im.save(os.path.join(OUT_DIR, "desk_pet_crying.png"))
    print("Saved desk_pet_crying.png")


def render_desk_pet_dancing():
    """Generates authentic Dancing Pet face with attitude swagger tilt and rhythm groove."""
    im = Image.new("RGB", (320, 256), (255, 255, 255))
    d = ImageDraw.Draw(im)

    # Swagger tilted eye centers (left higher at y=110, right lower at y=130)
    lx0, ly0, lx1, ly1 = 70, 68, 146, 162
    rx0, ry0, rx1, ry1 = 174, 88, 250, 182

    # Squashed & stretched body shape
    d.rounded_rectangle([lx0, ly0, lx1, ly1], radius=24, fill=(0, 0, 0))
    d.rounded_rectangle([rx0, ry0, rx1, ry1], radius=24, fill=(0, 0, 0))

    # Upper lids half-closed in confident swagger attitude
    d.polygon([(60, 60), (156, 60), (156, 102), (60, 88)], fill=(255, 255, 255))
    d.polygon([(164, 80), (260, 80), (260, 112), (164, 126)], fill=(255, 255, 255))

    # Crisp specular highlights
    d.ellipse([84, 104, 100, 120], fill=(255, 255, 255))
    d.ellipse([188, 124, 204, 140], fill=(255, 255, 255))

    # Dynamic groove smirk mouth
    d.arc([144, 172, 178, 196], start=20, end=160, fill=(0, 0, 0), width=4)

    # Minimalist Audio Rhythm Bounce Wave Bars at bottom
    wave_x = 90
    bar_heights = [6, 14, 24, 38, 52, 34, 18, 42, 60, 48, 28, 16, 8]
    for i, h in enumerate(bar_heights):
        bx = wave_x + i * 11
        by = 244 - h // 2
        d.rounded_rectangle([bx, by, bx + 6, 244], radius=3, fill=(168, 85, 247))

    im.save(os.path.join(OUT_DIR, "desk_pet_dancing.png"))
    print("Saved desk_pet_dancing.png")


def render_desk_pet_neutral():
    """Generates authentic Neutral living squircle eyes."""
    im = Image.new("RGB", (320, 256), (255, 255, 255))
    d = ImageDraw.Draw(im)

    lx0, ly0, lx1, ly1 = 70, 76, 146, 172
    rx0, ry0, rx1, ry1 = 174, 76, 250, 172

    d.rounded_rectangle([lx0, ly0, lx1, ly1], radius=26, fill=(0, 0, 0))
    d.rounded_rectangle([rx0, ry0, rx1, ry1], radius=26, fill=(0, 0, 0))

    # Primary catchlight
    d.ellipse([84, 90, 104, 110], fill=(255, 255, 255))
    d.ellipse([188, 90, 208, 110], fill=(255, 255, 255))
    # Secondary sparkle
    d.ellipse([118, 140, 126, 148], fill=(255, 255, 255))
    d.ellipse([222, 140, 230, 148], fill=(255, 255, 255))

    # Resting mouth
    d.arc([148, 180, 172, 194], start=30, end=150, fill=(0, 0, 0), width=3)

    # Bottom PC Stats Pill
    d.rounded_rectangle([60, 218, 260, 244], radius=12, fill=(244, 244, 246), outline=(218, 220, 224), width=1)
    d.text((74, 224), "CPU 20%   GPU 48°C   RAM 52%", fill=(75, 85, 99), font=font_mono)

    im.save(os.path.join(OUT_DIR, "desk_pet_neutral.png"))
    print("Saved desk_pet_neutral.png")


def render_desk_pet_angry():
    """Generates authentic Warning / Rage crimson state."""
    im = Image.new("RGB", (320, 256), (248, 113, 113))
    d = ImageDraw.Draw(im)

    lx0, ly0, lx1, ly1 = 70, 80, 146, 174
    rx0, ry0, rx1, ry1 = 174, 80, 250, 174

    d.rounded_rectangle([lx0, ly0, lx1, ly1], radius=24, fill=(0, 0, 0))
    d.rounded_rectangle([rx0, ry0, rx1, ry1], radius=24, fill=(0, 0, 0))

    # Aggressive diagonal furrowed brows
    d.polygon([(60, 70), (150, 70), (150, 110), (60, 80)], fill=(248, 113, 113))
    d.polygon([(170, 70), (260, 70), (260, 80), (170, 110)], fill=(248, 113, 113))

    d.ellipse([88, 120, 98, 130], fill=(255, 255, 255))
    d.ellipse([222, 120, 232, 130], fill=(255, 255, 255))

    # Angry downturned mouth
    d.arc([145, 186, 175, 206], start=210, end=330, fill=(0, 0, 0), width=4)

    # Forehead Anger Cross Vein
    vx, vy = 250, 48
    d.line([(vx - 14, vy - 14), (vx + 14, vy + 14)], fill=(220, 38, 38), width=5)
    d.line([(vx + 14, vy - 14), (vx - 14, vy + 14)], fill=(220, 38, 38), width=5)
    d.line([(vx - 7, vy - 18), (vx - 7, vy - 10)], fill=(220, 38, 38), width=4)
    d.line([(vx + 7, vy + 10), (vx + 7, vy + 18)], fill=(220, 38, 38), width=4)

    im.save(os.path.join(OUT_DIR, "desk_pet_angry.png"))
    print("Saved desk_pet_angry.png")


def render_desk_pet_calm():
    """Generates authentic Calm / Sleep state with sleeping curved crescents."""
    im = Image.new("RGB", (320, 256), (255, 255, 255))
    d = ImageDraw.Draw(im)

    # Sleeping eye crescents (︶ ︶)
    d.arc([70, 96, 146, 148], start=20, end=160, fill=(0, 0, 0), width=6)
    d.arc([174, 96, 250, 148], start=20, end=160, fill=(0, 0, 0), width=6)

    # Peaceful resting smile
    d.arc([148, 178, 172, 194], start=20, end=160, fill=(0, 0, 0), width=4)

    # Floating diagonal Z Z z
    d.text((230, 40), "Z", fill=(180, 185, 195), font=font_large)
    d.text((254, 70), "Z", fill=(195, 200, 210), font=font_title)
    d.text((272, 98), "z", fill=(210, 215, 225), font=font_bold)

    im.save(os.path.join(OUT_DIR, "desk_pet_calm.png"))
    print("Saved desk_pet_calm.png")


def render_pc_hud():
    """Generates authentic PC Hardware HUD."""
    im = Image.new("RGB", (320, 256), COL_DARK_BG)
    d = ImageDraw.Draw(im)

    # Top Status Bar
    d.rectangle([0, 0, 320, 32], fill=(18, 18, 22))
    d.line([(0, 32), (320, 32)], fill=COL_BORDER, width=1)
    d.text((12, 8), "PC HARDWARE VITALS", fill=COL_WHITE, font=font_bold)
    d.rounded_rectangle([216, 6, 308, 26], radius=10, fill=(15, 23, 42), outline=COL_CYAN, width=1)
    d.text((226, 7), "24°C CLEAR", fill=COL_CYAN, font=font_small)

    # Metrics
    # CPU
    d.rounded_rectangle([12, 42, 308, 88], radius=6, fill=COL_CARD_BG, outline=COL_BORDER, width=1)
    d.text((22, 48), "CPU LOAD", fill=COL_MUTED, font=font_tiny)
    d.text((22, 60), "24%", fill=COL_WHITE, font=font_title)
    d.text((70, 64), "3.8 GHz  -  48°C", fill=COL_MUTED, font=font_small)
    d.rounded_rectangle([180, 58, 296, 72], radius=4, fill=(35, 35, 42))
    d.rounded_rectangle([180, 58, 180 + int(116 * 0.24), 72], radius=4, fill=COL_GREEN)

    # GPU
    d.rounded_rectangle([12, 96, 308, 142], radius=6, fill=COL_CARD_BG, outline=COL_BORDER, width=1)
    d.text((22, 102), "GPU (RTX 4070)", fill=COL_MUTED, font=font_tiny)
    d.text((22, 114), "42%", fill=COL_WHITE, font=font_title)
    d.text((70, 118), "2450 MHz  -  54°C", fill=COL_MUTED, font=font_small)
    d.rounded_rectangle([180, 112, 296, 126], radius=4, fill=(35, 35, 42))
    d.rounded_rectangle([180, 112, 180 + int(116 * 0.42), 126], radius=4, fill=COL_CYAN)

    # RAM
    d.rounded_rectangle([12, 150, 308, 196], radius=6, fill=COL_CARD_BG, outline=COL_BORDER, width=1)
    d.text((22, 156), "SYSTEM MEMORY", fill=COL_MUTED, font=font_tiny)
    d.text((22, 168), "58%", fill=COL_WHITE, font=font_title)
    d.text((70, 172), "9.2 GB / 16.0 GB", fill=COL_MUTED, font=font_small)
    d.rounded_rectangle([180, 166, 296, 180], radius=4, fill=(35, 35, 42))
    d.rounded_rectangle([180, 166, 180 + int(116 * 0.58), 180], radius=4, fill=COL_AMBER)

    # Network
    d.rounded_rectangle([12, 204, 308, 246], radius=6, fill=COL_CARD_BG, outline=COL_BORDER, width=1)
    d.text((22, 210), "NETWORK ADAPTER", fill=COL_MUTED, font=font_tiny)
    d.text((22, 224), "DOWN: 14.2 MB/s", fill=COL_GREEN, font=font_mono)
    d.text((170, 224), "UP: 1.8 MB/s", fill=COL_CYAN, font=font_mono)

    im.save(os.path.join(OUT_DIR, "pc_hardware_hud.png"))
    print("Saved pc_hardware_hud.png")


def render_spotify_media():
    """Generates authentic Spotify Media Visualizer."""
    im = Image.new("RGB", (320, 256), (10, 10, 12))
    d = ImageDraw.Draw(im)

    # Top Status Bar
    d.rectangle([0, 0, 320, 32], fill=(18, 18, 22))
    d.line([(0, 32), (320, 32)], fill=COL_BORDER, width=1)
    d.text((12, 8), "SPOTIFY MEDIA SYNC", fill=COL_WHITE, font=font_bold)
    d.rounded_rectangle([216, 6, 308, 26], radius=10, fill=(30, 20, 40), outline=(236, 72, 153), width=1)
    d.text((230, 7), "HIP-HOP", fill=(236, 72, 153), font=font_small)

    # Vinyl Record Deck
    vx, vy, vr = 72, 118, 48
    d.ellipse([vx - vr, vy - vr, vx + vr, vy + vr], fill=(20, 20, 24), outline=(40, 40, 48), width=2)
    for r in [40, 32, 24]:
        d.ellipse([vx - r, vy - r, vx + r, vy + r], outline=(30, 30, 36), width=1)
    d.ellipse([vx - 14, vy - 14, vx + 14, vy + 14], fill=(236, 72, 153))
    d.ellipse([vx - 4, vy - 4, vx + 4, vy + 4], fill=(10, 10, 12))

    # Track Metadata
    d.text((140, 64), "NOW PLAYING", fill=COL_MUTED, font=font_tiny)
    d.text((140, 80), "Starboy", fill=COL_WHITE, font=font_large)
    d.text((140, 114), "The Weeknd, Daft Punk", fill=(236, 72, 153), font=font_small)
    d.text((140, 134), "BPM: 186.0  -  Energy: 0.84", fill=COL_MUTED, font=font_tiny)

    # Progress Bar
    bar_x, bar_y, bar_w, bar_h = 24, 184, 272, 8
    d.rounded_rectangle([bar_x, bar_y, bar_x + bar_w, bar_y + bar_h], radius=4, fill=(35, 35, 42))
    d.rounded_rectangle([bar_x, bar_y, bar_x + int(bar_w * 0.62), bar_y + bar_h], radius=4, fill=(236, 72, 153))
    d.text((24, 198), "2:14", fill=COL_MUTED, font=font_mono)
    d.text((264, 198), "3:50", fill=COL_MUTED, font=font_mono)

    # Audio Spectrum Analyzer Bars
    spec_x = 24
    heights = [8, 16, 26, 18, 30, 36, 22, 14, 28, 34, 20, 12, 24, 32, 16, 10]
    for i, h in enumerate(heights):
        bx = spec_x + i * 17
        d.rounded_rectangle([bx, 246 - h, bx + 10, 246], radius=2, fill=COL_CYAN)

    im.save(os.path.join(OUT_DIR, "spotify_media_sync.png"))
    print("Saved spotify_media_sync.png")


if __name__ == "__main__":
    render_bambu_telemetry()
    render_desk_pet_crying()
    render_desk_pet_dancing()
    render_desk_pet_neutral()
    render_desk_pet_angry()
    render_desk_pet_calm()
    render_pc_hud()
    render_spotify_media()
    print("All 8 documentation visuals generated successfully at 320x256!")
