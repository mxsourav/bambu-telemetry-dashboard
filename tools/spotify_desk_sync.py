"""
=============================================================================
  STΔN studio. Spotify & Media Desk Station Sync (Ultra-Low Latency Native)
=============================================================================
Runs continuously in background on Windows.
Features:
1. Persistent 0-latency PowerShell media stream (no repeated process spawning).
2. HTTP Keep-Alive connection pooling for instant 3ms packet delivery.
3. Non-blocking asynchronous HD artwork fetching (zero UI lag or frozen timer).
4. Auto-reconnect and crash resilience.
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
import threading
import psutil
from PIL import Image, ImageOps, ImageFilter

DEVICE_IP = "192.168.0.154"
SPOTIFY_NOW_URL = f"http://{DEVICE_IP}/api/spotify/now"
SPOTIFY_ART_TRIGGER = f"http://{DEVICE_IP}/api/spotify/testart?url=/art.jpg"
SPOTIFY_COVER_URL = f"http://{DEVICE_IP}/api/spotify/cover"
FS_UPLOAD_URL = f"http://{DEVICE_IP}/api/fs/upload?path=/art.jpg"
LED_SET_URL = f"http://{DEVICE_IP}/api/led/set"
PC_STATS_URL = f"http://{DEVICE_IP}/api/pc/stats"

# Music "vibe" energy (0..1) shared with the PC telemetry thread: the desk
# pet bounces harder to energetic tracks. Derived deterministically from the
# track name (no extra API calls); 0 = nothing playing.
g_music_energy = 0.5

def track_energy(artist, title):
    h = sum(ord(c) for c in (artist or "") + (title or ""))
    return round(0.3 + (h % 7) / 10.0, 2)  # 0.3 .. 0.9

def lookup_genre(artist, title):
    """Genre via the same iTunes search used for artwork (no auth needed).
    Returns e.g. 'Drill', 'Hip-Hop/Rap', 'Metal' or ''."""
    try:
        clean_title = re.sub(r'[\(\[\{].*?[\)\]\}]', '', title or '').replace('|', ' ').strip()
        clean_artist = (artist or '').split(',')[0].split('&')[0].replace('Music', '').strip()
        query = urllib.parse.quote(f"{clean_artist} {clean_title}".strip())
        url = f"https://itunes.apple.com/search?term={query}&entity=song&limit=1"
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=3) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            if data.get("resultCount", 0) > 0:
                return data["results"][0].get("primaryGenreName", "") or ""
    except Exception:
        pass
    return ""

g_music_genre = ""

PS_SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "get_media.ps1")
THUMB_RAW_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "current_thumb_raw.dat")
THUMB_JPEG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "current_thumb.jpg")
LOG_FILE_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "desk_sync.log")

# HTTP session with connection pooling
http_session = requests.Session()
adapter = requests.adapters.HTTPAdapter(pool_connections=5, pool_maxsize=10, max_retries=1)
http_session.mount("http://", adapter)

# Redirect output if running without console
if sys.stdout is None or not sys.stdout.isatty():
    try:
        log_fp = open(LOG_FILE_PATH, "a", encoding="utf-8", buffering=1)
        sys.stdout = log_fp
        sys.stderr = log_fp
    except Exception:
        pass

def fetch_itunes_artwork(artist, title):
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
    try:
        clean = re.sub(r'[\(\[\{].*?[\)\]\}]', '', query).replace('|', ' ')
        clean = re.sub(r'[^\w\s-]', ' ', clean)
        clean = ' '.join(clean.split())
        url = 'https://www.youtube.com/results?search_query=' + urllib.parse.quote(clean)
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'})
        with urllib.request.urlopen(req, timeout=3) as resp:
            html = resp.read().decode('utf-8', errors='ignore')
        m = re.search(r'"videoId":"([a-zA-Z0-9_-]{11})"', html)
        if not m:
            m = re.search(r'watch\?v=([a-zA-Z0-9_-]{11})', html)
        if m:
            vid = m.group(1)
            return f"https://img.youtube.com/vi/{vid}/hqdefault.jpg"
    except Exception:
        pass
    return None

def process_smart_art(img):
    """Smart aspect-ratio fitting for 160x128 screen.
    Prevents cropping off heads/text on 16:9 YouTube video thumbnails or square album covers,
    blending uncropped art with an atmospheric blurred background.
    """
    w, h = img.size
    aspect = w / float(h)
    target_w, target_h = 160, 128

    # Atmospheric blurred background filling entire 160x128
    bg = ImageOps.fit(img, (target_w, target_h), method=Image.Resampling.LANCZOS)
    bg = bg.filter(ImageFilter.GaussianBlur(14))

    if aspect > 1.35:
        # 16:9 or wide video thumbnail:
        # Scale width to 160, height to ~90. Place at top (y=0) so 100% of video art
        # is uncropped above the bottom player card (card is at y=90..124)!
        fg_w = target_w
        fg_h = int(round(target_w / aspect))
        if fg_h > target_h: fg_h = target_h
        fg = img.resize((fg_w, fg_h), Image.Resampling.LANCZOS)
        bg.paste(fg, (0, 0))
    elif aspect < 0.9:
        # Portrait
        fg_h = target_h
        fg_w = int(round(target_h * aspect))
        fg = img.resize((fg_w, fg_h), Image.Resampling.LANCZOS)
        x_off = (target_w - fg_w) // 2
        bg.paste(fg, (x_off, 0))
    else:
        # Square / near-square album cover (Spotify / iTunes ~ 1:1):
        # Scale height to 128, width to ~128. Centered horizontally (x=16..144).
        # Zero top/bottom cropping!
        fg_h = target_h
        fg_w = int(round(target_h * aspect))
        if fg_w > target_w: fg_w = target_w
        fg = img.resize((fg_w, fg_h), Image.Resampling.LANCZOS)
        x_off = (target_w - fg_w) // 2
        bg.paste(fg, (x_off, 0))

    return bg.filter(ImageFilter.UnsharpMask(radius=1.0, percent=100, threshold=2))

def extract_vibrant_rgb(im):
    try:
        small = im.copy().resize((48, 48), Image.Resampling.BOX).convert("RGB")
        quantized = small.quantize(colors=16)
        palette = quantized.getpalette()[:48]
        best_color = (200, 200, 200)
        best_score = -1.0
        for i in range(16):
            r, g, b = palette[i*3], palette[i*3+1], palette[i*3+2]
            max_c = max(r, g, b)
            min_c = min(r, g, b)
            if max_c < 35 or max_c > 250:
                continue
            sat = (max_c - min_c) / float(max_c) if max_c > 0 else 0
            val = max_c / 255.0
            score = sat * 1.6 + val * 0.4
            if score > best_score:
                best_score = score
                best_color = (r, g, b)
        return best_color
    except Exception:
        return (180, 180, 180)

def push_ambient_led(r, g, b):
    try:
        http_session.post(LED_SET_URL, json={"r": int(r), "g": int(g), "b": int(b)}, timeout=1.5)
    except Exception:
        pass

def upload_album_art(source_path_or_url):
    try:
        jpeg_to_upload = THUMB_JPEG_PATH
        raw_art = None
        if source_path_or_url.startswith("http://") or source_path_or_url.startswith("https://"):
            req = urllib.request.Request(source_path_or_url, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=4) as response:
                img_data = response.read()
            from io import BytesIO
            raw_art = Image.open(BytesIO(img_data)).convert("RGB")
            if raw_art.size == (480, 360):
                raw_art = raw_art.crop((0, 45, 480, 315))
            im = process_smart_art(raw_art)
            im.save(jpeg_to_upload, "JPEG", quality=95)
        elif source_path_or_url == THUMB_RAW_PATH or os.path.exists(source_path_or_url):
            raw_art = Image.open(source_path_or_url).convert("RGB")
            im = process_smart_art(raw_art)
            im.save(jpeg_to_upload, "JPEG", quality=95)
        else:
            return False

        # Ambient RGB desk lighting: extract dominant vibrant color and push to LED strip
        if raw_art:
            ambient_rgb = extract_vibrant_rgb(raw_art)
            threading.Thread(target=push_ambient_led, args=ambient_rgb, daemon=True).start()

        if os.path.exists(jpeg_to_upload) and os.path.getsize(jpeg_to_upload) > 500:
            with open(jpeg_to_upload, "rb") as f:
                jpeg_bytes = f.read()

            try:
                files = {"file": ("cover.jpg", jpeg_bytes, "image/jpeg")}
                r = http_session.post(SPOTIFY_COVER_URL, files=files, timeout=2)
                if r.status_code == 200:
                    print("  [ART] Cover art sent to Desk Monitor!")
                    return True
            except Exception:
                pass

            try:
                files = {"file": ("/art.jpg", jpeg_bytes, "image/jpeg")}
                r = http_session.post(FS_UPLOAD_URL, files=files, timeout=2)
                if r.status_code == 200:
                    http_session.get(SPOTIFY_ART_TRIGGER, timeout=2)
                    print("  [ART] Cover art sent via FS fallback!")
                    return True
            except Exception:
                pass
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
        resp = http_session.post(SPOTIFY_NOW_URL, json=payload, timeout=1.5)
        return resp.status_code == 200
    except Exception:
        return False

def async_fetch_hd_art(artist, title, current_track_id, track_lock):
    """Background worker: searches iTunes / YouTube for Ultra-HD art without blocking the timeline loop."""
    try:
        art_url = fetch_itunes_artwork(artist, title)
        if not art_url:
            art_url = fetch_youtube_hd_thumbnail(f"{title} {artist}")
        if art_url:
            # Check if track is still the current one before uploading
            if track_lock["id"] == current_track_id:
                ok = upload_album_art(art_url)
                if not ok:
                    # Retry up to 3 times if device was temporarily offline or rebooting
                    for _ in range(3):
                        time.sleep(1.2)
                        if track_lock["id"] != current_track_id:
                            break
                        if upload_album_art(art_url):
                            ok = True
                            break
                if ok:
                    print(f"  [HD ART] Upgraded to Ultra-HD art for: {title}")
    except Exception:
        pass

def get_weather():
    try:
        req = urllib.request.Request("http://ip-api.com/json", headers={"User-Agent": "DeskSync/1.0"})
        with urllib.request.urlopen(req, timeout=3) as r:
            loc = json.loads(r.read().decode())
            lat = loc.get("lat", 28.6139)
            lon = loc.get("lon", 77.2090)
            city = loc.get("city", "Delhi")

        weather_url = f"https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}&current_weather=true"
        req_w = urllib.request.Request(weather_url, headers={"User-Agent": "DeskSync/1.0"})
        with urllib.request.urlopen(req_w, timeout=3) as r:
            data = json.loads(r.read().decode())
            cw = data.get("current_weather", {})
            temp = float(cw.get("temperature", 0.0))
            wcode = int(cw.get("weathercode", 0))

            desc = "CLEAR"
            if wcode in (1, 2, 3):
                desc = "CLOUDY"
            elif wcode in (45, 48):
                desc = "FOG"
            elif wcode in (51, 53, 55, 61, 63, 65, 80, 81, 82):
                desc = "RAIN"
            elif wcode in (71, 73, 75, 77, 85, 86):
                desc = "SNOW"
            elif wcode >= 95:
                desc = "STORM"

            return {"city": city, "temp": temp, "weather": desc}
    except Exception:
        return {"city": "PC", "temp": 0.0, "weather": "ONLINE"}

def get_gpu_stats():
    try:
        creationflags = 0
        if sys.platform == "win32":
            creationflags = subprocess.CREATE_NO_WINDOW
        res = subprocess.run(
            ["nvidia-smi", "--query-gpu=utilization.gpu,temperature.gpu", "--format=csv,noheader,nounits"],
            capture_output=True, text=True, timeout=1, creationflags=creationflags
        )
        if res.returncode == 0:
            parts = res.stdout.strip().split(",")
            return float(parts[0].strip()), float(parts[1].strip())
    except Exception:
        pass
    return 0.0, 0.0

def pc_telemetry_loop():
    """Background hardware vitals & weather stream to Desk Station (every 2s)."""
    weather_info = {"city": "PC", "temp": 0.0, "weather": "ONLINE"}
    last_weather_fetch = 0
    try:
        net_init = psutil.net_io_counters()
        last_net_bytes = (net_init.bytes_recv, net_init.bytes_sent)
    except Exception:
        last_net_bytes = (0, 0)
    last_net_time = time.time()

    while True:
        try:
            now = time.time()
            if now - last_weather_fetch > 900 or last_weather_fetch == 0:
                weather_info = get_weather()
                last_weather_fetch = now

            cpu = psutil.cpu_percent(interval=None)
            mem = psutil.virtual_memory()
            gpu_pct, gpu_temp = get_gpu_stats()

            net = psutil.net_io_counters()
            dt = max(now - last_net_time, 0.1)
            down_kbs = round((net.bytes_recv - last_net_bytes[0]) / (1024 * dt), 1)
            up_kbs = round((net.bytes_sent - last_net_bytes[1]) / (1024 * dt), 1)
            last_net_bytes = (net.bytes_recv, net.bytes_sent)
            last_net_time = now

            payload = {
                "cpu": cpu,
                "ram": mem.percent,
                "ram_used": round(mem.used / (1024**3), 1),
                "ram_total": round(mem.total / (1024**3), 1),
                "gpu": gpu_pct,
                "gpu_temp": gpu_temp,
                "net_down": down_kbs,
                "net_up": up_kbs,
                "temp": weather_info["temp"],
                "weather": weather_info["weather"],
                "city": weather_info["city"],
                "vibe": g_music_energy,
                "genre": g_music_genre
            }
            http_session.post(PC_STATS_URL, json=payload, timeout=1.5)
        except Exception:
            pass
        time.sleep(2.0)

def spawn_ps_stream():
    creationflags = 0
    if sys.platform == "win32":
        creationflags = subprocess.CREATE_NO_WINDOW
    return subprocess.Popen(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", PS_SCRIPT, "-Stream"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        stdin=subprocess.DEVNULL,
        text=True,
        bufsize=1,
        creationflags=creationflags
    )

def async_lookup_genre(artist, title):
    """Background genre lookup so the timeline loop never blocks."""
    global g_music_genre
    genre = lookup_genre(artist, title)
    if genre:
        g_music_genre = genre

def main():
    global g_music_energy, g_music_genre
    print("=" * 65)
    print(" STΔN studio. Spotify & Media Desk Station Sync (Low-Latency Native)")
    print(f" Target Device: http://{DEVICE_IP}/")
    print(" PC Hardware Telemetry & Weather Sync: Active (2s interval)")
    print(" Listening for music playing on your PC...")
    print("=" * 65)

    # Launch background PC Telemetry & Weather thread
    threading.Thread(target=pc_telemetry_loop, daemon=True).start()

    last_track_id = ""
    last_art_track = ""
    track_lock = {"id": ""}
    ps_proc = None

    while True:
        try:
            if ps_proc is None or ps_proc.poll() is not None:
                ps_proc = spawn_ps_stream()

            line = ps_proc.stdout.readline()
            if not line:
                time.sleep(0.1)
                continue

            line = line.strip()
            if not line:
                continue

            if line == "IDLE":
                if last_track_id:
                    push_spotify_now("", "", 0, 0, False)
                    last_track_id = ""
                    track_lock["id"] = ""
                    g_music_energy = 0.0
                    g_music_genre = ""
                continue

            parts = line.split("::::")
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

                track_id = f"{artist} - {title}"
                track_lock["id"] = track_id
                g_music_energy = track_energy(artist, title) if is_playing else 0.0
                threading.Thread(
                    target=async_lookup_genre,
                    args=(artist, title),
                    daemon=True
                ).start()

                if track_id != last_track_id:
                    last_track_id = track_id
                    print(f"\n[NOW PLAYING] {artist} - {title} (Playing: {is_playing})")
                    push_spotify_now(title, artist, pos_ms, dur_ms, is_playing)

                    if track_id != last_art_track:
                        last_art_track = track_id
                        # 1. ONLY upload local thumbnail if this song actually HAS a fresh thumbnail!
                        if has_thumb and os.path.exists(THUMB_RAW_PATH):
                            upload_album_art(THUMB_RAW_PATH)
                            # Background HD upgrade in case local thumbnail is low-res
                            t = threading.Thread(
                                target=async_fetch_hd_art,
                                args=(artist, title, track_id, track_lock),
                                daemon=True
                            )
                            t.start()
                        else:
                            # 2. No local thumbnail (e.g. YouTube in Brave): fetch online HD art directly!
                            # NEVER upload stale leftover thumb from previous track!
                            t = threading.Thread(
                                target=async_fetch_hd_art,
                                args=(artist, title, track_id, track_lock),
                                daemon=True
                            )
                            t.start()
                else:
                    # Update live progress bar and play/pause state
                    push_spotify_now(title, artist, pos_ms, dur_ms, is_playing)

        except KeyboardInterrupt:
            print("\nExiting Desk Sync.")
            if ps_proc:
                ps_proc.terminate()
            sys.exit(0)
        except Exception as e:
            print(f"[LOOP ERROR] {e}")
            time.sleep(1.0)

if __name__ == "__main__":
    main()
