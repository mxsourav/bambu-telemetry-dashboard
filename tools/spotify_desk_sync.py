"""
=============================================================================
  STΔN studio. Spotify & Media Desk Station Sync (Native Windows)
=============================================================================
Runs continuously on your Windows laptop.
Detects ANY music playing on Spotify (or browser / media player) on your PC and:
1. Updates the live Track Title and Artist on the Desk Monitor in real-time.
2. Updates the live Progress Bar and Equalizer animation smoothly.
3. Automatically fetches the official HD album artwork and sends it to the device!
=============================================================================
"""
import time
import requests
import subprocess
import os
import sys
import json
import urllib.request
import urllib.parse

DEVICE_IP = "192.168.0.154"
SPOTIFY_NOW_URL = f"http://{DEVICE_IP}/api/spotify/now"
SPOTIFY_ART_TRIGGER = f"http://{DEVICE_IP}/api/spotify/testart?url=/art.jpg"
FS_UPLOAD_URL = f"http://{DEVICE_IP}/api/fs/upload?path=/art.jpg"

PS_SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "get_media.ps1")
THUMB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "current_thumb.jpg")

try:
    from PIL import Image
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

def fetch_itunes_artwork(artist, title):
    """Fetches high quality 300x300 album artwork for artist and title."""
    try:
        query = urllib.parse.quote(f"{artist} {title}")
        url = f"https://itunes.apple.com/search?term={query}&entity=song&limit=1"
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=3) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            if data.get("resultCount", 0) > 0:
                art = data["results"][0].get("artworkUrl100", "")
                if art:
                    # Upgrade to 300x300
                    return art.replace("100x100bb.jpg", "300x300bb.jpg")
    except Exception:
        pass
    return None

def upload_album_art(jpeg_path_or_url):
    """Uploads a JPEG to the SD card as /art.jpg and triggers dirty render."""
    try:
        temp_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), "temp_art.jpg")
        if jpeg_path_or_url.startswith("http://") or jpeg_path_or_url.startswith("https://"):
            req = urllib.request.Request(jpeg_path_or_url, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=4) as response, open(temp_file, "wb") as out_file:
                out_file.write(response.read())
            upload_source = temp_file
        else:
            upload_source = jpeg_path_or_url

        if os.path.exists(upload_source) and os.path.getsize(upload_source) > 500:
            with open(upload_source, "rb") as f:
                files = {"file": ("/art.jpg", f, "image/jpeg")}
                r = requests.post(FS_UPLOAD_URL, files=files, timeout=4)
                if r.status_code == 200:
                    requests.get(SPOTIFY_ART_TRIGGER, timeout=3)
                    print("  [ART] High-Res Album Art uploaded to Desk Monitor!")
                    return True
    except Exception as e:
        print(f"  [ART ERROR] {e}")
    return False

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
        return resp.status_code == 200
    except Exception:
        return False

def get_media_info():
    if not os.path.exists(PS_SCRIPT):
        return None
    try:
        res = subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", PS_SCRIPT],
            capture_output=True, text=True, timeout=3
        )
        out = res.stdout.strip()
        if not out or out == "IDLE":
            return None
        
        # Format: THUMB::Artist::::Title::::posMs::::durMs or NOTHUMB::Artist::::Title::::posMs::::durMs
        parts = out.split("::::")
        if len(parts) >= 2:
            tag_artist = parts[0]
            has_thumb = tag_artist.startswith("THUMB::")
            if tag_artist.startswith("THUMB::"):
                artist = tag_artist[7:].strip()
            elif tag_artist.startswith("NOTHUMB::"):
                artist = tag_artist[9:].strip()
            else:
                artist = tag_artist.strip()
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
    print(" STΔN studio. Spotify & Media Desk Station Sync")
    print(f" Target Device: http://{DEVICE_IP}/")
    print(" Listening for music playing on your PC...")
    print(" Press Ctrl+C at any time to stop.")
    print("=" * 65)

    last_track_id = ""
    last_art_track = ""

    while True:
        try:
            info = get_media_info()
            if info:
                track_id = f"{info['artist']} - {info['title']}"
                if track_id != last_track_id:
                    last_track_id = track_id
                    print(f"\n[NOW PLAYING] {info['artist']} - {info['title']}")
                    push_spotify_now(info["title"], info["artist"], info["pos_ms"], info["dur_ms"], True)
                    
                    if track_id != last_art_track:
                        last_art_track = track_id
                        # Try local thumbnail first
                        art_sent = False
                        if info["has_thumb"] and os.path.exists(THUMB_PATH):
                            art_sent = upload_album_art(THUMB_PATH)
                        # Fallback to iTunes official album artwork
                        if not art_sent:
                            art_url = fetch_itunes_artwork(info["artist"], info["title"])
                            if art_url:
                                upload_album_art(art_url)
                else:
                    # Update live timeline progress bar
                    push_spotify_now(info["title"], info["artist"], info["pos_ms"], info["dur_ms"], True)
            else:
                if last_track_id:
                    # Media paused or closed
                    push_spotify_now("", "", 0, 0, False)
                    last_track_id = ""
            time.sleep(1.0)
        except KeyboardInterrupt:
            print("\nExiting Desk Sync.")
            sys.exit(0)
        except Exception:
            time.sleep(2)

if __name__ == "__main__":
    main()
