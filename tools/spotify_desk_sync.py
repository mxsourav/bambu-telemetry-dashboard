"""
=============================================================================
  STΔN studio. Spotify & Media Desk Station Sync
=============================================================================
Works 100% natively on your Windows PC!
Runs in the background. Whenever you play a song on Spotify (or any media player)
on your PC, it:
1. Automatically opens the FULLSCREEN Spotify Player on the Desk Station!
2. Sends the ACTUAL music album artwork cover directly to the display!
3. Updates the live progress bar and track timeline in real time!
=============================================================================
"""
import time
import requests
import subprocess
import os
import sys

DEVICE_IP = "192.168.0.154"
SPOTIFY_NOW_URL = f"http://{DEVICE_IP}/api/spotify/now"
SPOTIFY_COVER_URL = f"http://{DEVICE_IP}/api/spotify/cover"

try:
    from PIL import Image
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

PS_SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "get_media.ps1")
THUMB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "current_thumb.jpg")

def send_cover_art():
    if not HAS_PIL or not os.path.exists(THUMB_PATH):
        return
    try:
        im = Image.open(THUMB_PATH).convert("RGB")
        im = im.resize((56, 56), Image.Resampling.LANCZOS)
        # Convert to 56x56 RGB565 little-endian bytes (matches ESP32 memory)
        pixels = list(im.getdata())
        raw = bytearray(56 * 56 * 2)
        idx = 0
        for r, g, b in pixels:
            r5 = (r >> 3) & 0x1F
            g6 = (g >> 2) & 0x3F
            b5 = (b >> 3) & 0x1F
            c = (r5 << 11) | (g6 << 5) | b5
            raw[idx]   = c & 0xFF
            raw[idx+1] = (c >> 8) & 0xFF
            idx += 2
        
        files = {'file': ('cover.bin', bytes(raw), 'application/octet-stream')}
        requests.post(SPOTIFY_COVER_URL, files=files, timeout=2)
        print("  [ART] Sent actual album cover artwork to Desk Monitor (56x56)!")
    except Exception as e:
        print(f"  [ART ERROR] {e}")

def push_spotify_now(title, artist, pos_ms=0, dur_ms=0, is_playing=True):
    try:
        payload = {
            "title": title,
            "artist": artist,
            "progress_ms": pos_ms,
            "duration_ms": dur_ms if dur_ms > 0 else 180000,
            "is_playing": is_playing
        }
        resp = requests.post(SPOTIFY_NOW_URL, json=payload, timeout=2)
        if resp.status_code == 200:
            return True
    except Exception:
        pass
    return False

def get_media_info():
    if not os.path.exists(PS_SCRIPT):
        return None
    try:
        res = subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", PS_SCRIPT],
            capture_output=True, text=True, timeout=2
        )
        out = res.stdout.strip()
        if not out or out == "IDLE":
            return None
        
        # Format: THUMB::Artist::::Title::::posMs::::durMs
        # Format: NOTHUMB::Artist::::Title::::posMs::::durMs
        parts = out.split("::::")
        if len(parts) >= 2:
            tag_artist = parts[0]
            has_thumb = "THUMB::" in tag_artist
            artist = tag_artist.replace("THUMB::", "").replace("NOTHUMB::", "").strip()
            title = parts[1].strip()
            pos_ms = int(parts[2]) if len(parts) >= 3 and parts[2].isdigit() else 0
            dur_ms = int(parts[3]) if len(parts) >= 4 and parts[3].isdigit() else 0
            return {
                "artist": artist if artist else "Spotify",
                "title": title if title else "Playing",
                "pos_ms": pos_ms,
                "dur_ms": dur_ms,
                "has_thumb": has_thumb
            }
    except Exception:
        pass
    return None

def main():
    print("=" * 65)
    print(" STΔN studio. Spotify & Media Desk Station Sync (Fullscreen Player)")
    print(f" Target Device: http://{DEVICE_IP}/")
    print(" Listening for music playing on your PC...")
    print(" Press Ctrl+C at any time to stop.")
    print("=" * 65)

    last_track_id = ""
    idle_count = 0

    while True:
        try:
            info = get_media_info()
            if info:
                idle_count = 0
                track_id = f"{info['artist']} - {info['title']}"
                if track_id != last_track_id:
                    last_track_id = track_id
                    print(f"\n[NOW PLAYING] {info['artist']} - {info['title']}")
                    if info["has_thumb"]:
                        send_cover_art()
                    push_spotify_now(info["title"], info["artist"], info["pos_ms"], info["dur_ms"], True)
                else:
                    # Update live timeline progress bar
                    push_spotify_now(info["title"], info["artist"], info["pos_ms"], info["dur_ms"], True)
            else:
                if last_track_id:
                    idle_count += 1
                    if idle_count >= 3:
                        print("\n[IDLE] Playback stopped. Returning to 3D Printer dashboard.")
                        push_spotify_now("", "", 0, 0, False)
                        last_track_id = ""
            time.sleep(1.5)
        except KeyboardInterrupt:
            print("\nExiting Desk Sync.")
            sys.exit(0)
        except Exception:
            time.sleep(2)

if __name__ == "__main__":
    main()
