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
import re
import msvcrt
from PIL import Image, ImageOps, ImageFilter

DEVICE_IP = "192.168.0.154"
SPOTIFY_NOW_URL = f"http://{DEVICE_IP}/api/spotify/now"
SPOTIFY_ART_TRIGGER = f"http://{DEVICE_IP}/api/spotify/testart?url=/art.jpg"
FS_UPLOAD_URL = f"http://{DEVICE_IP}/api/fs/upload?path=/art.jpg"

PS_SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "get_media.ps1")
THUMB_RAW_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "current_thumb_raw.dat")
THUMB_JPEG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "current_thumb.jpg")
LOCK_FILE_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".sync.lock")
LOG_FILE_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "desk_sync.log")

# Ensure single instance
try:
    _lock_file = open(LOCK_FILE_PATH, "w")
    msvcrt.locking(_lock_file.fileno(), msvcrt.LK_NBLCK, 1)
except (IOError, OSError):
    sys.exit(0)

# Redirect output to log file if running windowless (e.g. via pythonw.exe)
if sys.stdout is None or not sys.stdout.isatty():
    log_fp = open(LOG_FILE_PATH, "a", encoding="utf-8", buffering=1)
    sys.stdout = log_fp
    sys.stderr = log_fp

def fetch_itunes_artwork(artist, title):
    """Fallback: Fetches high quality album artwork for artist and title."""
    try:
        clean_title = re.sub(r'[\(\[\{].*?[\)\]\}]', '', title).replace('|', ' ').strip()
        clean_artist = artist.split(',')[0].split('&')[0].replace('Music', '').strip()
        query = urllib.parse.quote(f"{clean_artist} {clean_title}".strip())
        url = f"https://itunes.apple.com/search?term={query}&entity=song&limit=1"
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=3) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            if data.get("resultCount", 0) > 0:
                art = data["results"][0].get("artworkUrl100", "")
                if art:
                    return art.replace("100x100bb.jpg", "600x600bb.jpg")
    except Exception:
        pass
    return None

def fetch_youtube_hd_thumbnail(query):
    """Fetches high-res (480x360 or 1280x720) YouTube thumbnail for music videos."""
    try:
        clean = re.sub(r'[\(\[\{].*?[\)\]\}]', '', query).replace('|', ' ').strip()
        url = 'https://www.youtube.com/results?search_query=' + urllib.parse.quote(clean)
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'})
        with urllib.request.urlopen(req, timeout=3) as resp:
            html = resp.read().decode('utf-8', errors='ignore')
        m = re.search(r'watch\?v=([a-zA-Z0-9_-]{11})', html)
        if m:
            vid = m.group(1)
            return f"https://img.youtube.com/vi/{vid}/hqdefault.jpg"
    except Exception:
        pass
    return None

def upload_album_art(source_path_or_url):
    """Uploads a JPEG to the SD card as /art.jpg and triggers dirty render."""
    try:
        jpeg_to_upload = THUMB_JPEG_PATH
        if source_path_or_url.startswith("http://") or source_path_or_url.startswith("https://"):
            req = urllib.request.Request(source_path_or_url, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=4) as response:
                img_data = response.read()
            from io import BytesIO
            im = Image.open(BytesIO(img_data)).convert("RGB")
            # If YouTube 480x360 hqdefault, crop the 45px black letterbox bars
            if im.size == (480, 360):
                im = im.crop((0, 45, 480, 315))
            im = ImageOps.fit(im, (160, 128), method=Image.Resampling.LANCZOS)
            im = im.filter(ImageFilter.UnsharpMask(radius=1.2, percent=120, threshold=2))
            im.save(jpeg_to_upload, "JPEG", quality=95)
        elif source_path_or_url == THUMB_RAW_PATH or os.path.exists(source_path_or_url):
            im = Image.open(source_path_or_url).convert("RGB")
            im = ImageOps.fit(im, (160, 128), method=Image.Resampling.LANCZOS)
            im = im.filter(ImageFilter.UnsharpMask(radius=1.2, percent=120, threshold=2))
            im.save(jpeg_to_upload, "JPEG", quality=95)
        else:
            return False

        if os.path.exists(jpeg_to_upload) and os.path.getsize(jpeg_to_upload) > 500:
            with open(jpeg_to_upload, "rb") as f:
                files = {"file": ("/art.jpg", f, "image/jpeg")}
                r = requests.post(FS_UPLOAD_URL, files=files, timeout=4)
                if r.status_code == 200:
                    requests.get(SPOTIFY_ART_TRIGGER, timeout=3)
                    print("  [ART] Ultra-HD Album Art uploaded to Desk Monitor!")
                    return True
    except Exception as e:
        print(f"  [ART ERROR] {e}")
    return False

def push_spotify_now(title, artist, pos_ms=0, dur_ms=0, is_playing=True):
    try:
        payload = {
            "title": title,
            "artist": artist,
            "progress_ms": int(pos_ms),
            "duration_ms": int(dur_ms) if dur_ms > 0 else 180000,
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
        
        # Format: THUMB::PLAY::Artist::::Title::::posMs::::durMs
        parts = out.split("::::")
        if len(parts) >= 2:
            prefix_parts = parts[0].split("::")
            has_thumb = prefix_parts[0] == "THUMB"
            is_playing = prefix_parts[1] == "PLAY" if len(prefix_parts) >= 2 else True
            artist = prefix_parts[2].strip() if len(prefix_parts) >= 3 else prefix_parts[-1].strip()
            title = parts[1].strip()
            
            try:
                pos_ms = int(float(parts[2])) if len(parts) >= 3 else 0
            except Exception:
                pos_ms = 0
                
            try:
                dur_ms = int(float(parts[3])) if len(parts) >= 4 else 0
            except Exception:
                dur_ms = 0

            return {
                "artist": artist if artist else "Spotify",
                "title": title if title else "Playing",
                "pos_ms": pos_ms,
                "dur_ms": dur_ms,
                "is_playing": is_playing,
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
                    push_spotify_now(info["title"], info["artist"], info["pos_ms"], info["dur_ms"], info["is_playing"])
                    
                    if track_id != last_art_track:
                        last_art_track = track_id
                        art_sent = False
                        
                        # Check if local thumbnail is low-res (e.g. YouTube in browser gives only 150x83)
                        is_low_res = False
                        if info["has_thumb"] and os.path.exists(THUMB_RAW_PATH):
                            try:
                                with Image.open(THUMB_RAW_PATH) as test_im:
                                    if test_im.size[0] < 250:
                                        is_low_res = True
                            except Exception:
                                is_low_res = True

                        # If we have a local high-res thumb (Spotify desktop gives 300x300), use it
                        if not is_low_res and info["has_thumb"] and os.path.exists(THUMB_RAW_PATH):
                            art_sent = upload_album_art(THUMB_RAW_PATH)

                        # If low-res or no thumb, fetch HD artwork from iTunes or YouTube
                        if not art_sent:
                            art_url = fetch_itunes_artwork(info["artist"], info["title"])
                            if not art_url:
                                art_url = fetch_youtube_hd_thumbnail(f"{info['title']} {info['artist']}")
                            if art_url:
                                art_sent = upload_album_art(art_url)

                        # Fallback to local thumbnail if online fetch failed
                        if not art_sent and os.path.exists(THUMB_RAW_PATH):
                            upload_album_art(THUMB_RAW_PATH)
                else:
                    # Update live timeline progress bar
                    push_spotify_now(info["title"], info["artist"], info["pos_ms"], info["dur_ms"], info["is_playing"])
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
