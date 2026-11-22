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

# Ensure tools directory is on sys.path
tools_dir = os.path.dirname(os.path.abspath(__file__))
if tools_dir not in sys.path:
    sys.path.insert(0, tools_dir)

# Ensure stdout and stderr exist even under pythonw.exe
LOG_FILE_PATH = os.path.join(tools_dir, "desk_sync.log")
if sys.stdout is None:
    try:
        log_fp = open(LOG_FILE_PATH, "a", encoding="utf-8", buffering=1)
        sys.stdout = log_fp
        sys.stderr = log_fp
    except Exception:
        pass

import json
import urllib.request
import urllib.parse
import re
import threading
import psutil
from PIL import Image, ImageOps, ImageFilter

def find_device_ip():
    global DEVICE_IP
    if 'DEVICE_IP' in globals() and DEVICE_IP:
        try:
            with urllib.request.urlopen(f"http://{DEVICE_IP}/api/status", timeout=0.4) as r:
                if r.status == 200:
                    return DEVICE_IP
        except Exception:
            pass

    # Check ARP table for ESP32 MAC address
    try:
        out = subprocess.check_output("arp -a", shell=True).decode()
        for line in out.splitlines():
            if "b0-cb-d8-0a-a9-54" in line.lower():
                parts = line.split()
                if parts:
                    cand = parts[0]
                    try:
                        with urllib.request.urlopen(f"http://{cand}/api/status", timeout=0.5) as r:
                            if r.status == 200:
                                DEVICE_IP = cand
                                return cand
                    except Exception:
                        DEVICE_IP = cand
                        return cand
    except Exception:
        pass

    for ip in ["192.168.137.35", "192.168.137.171", "192.168.0.154"]:
        try:
            with urllib.request.urlopen(f"http://{ip}/api/status", timeout=0.5) as r:
                if r.status == 200:
                    DEVICE_IP = ip
                    return ip
        except Exception:
            pass

    if 'DEVICE_IP' not in globals() or not DEVICE_IP:
        DEVICE_IP = "192.168.137.35"
    return DEVICE_IP

DEVICE_IP = os.environ.get("DEVICE_IP", "") or find_device_ip()

def get_url(path):
    global DEVICE_IP
    return f"http://{DEVICE_IP}{path}"

# Music "vibe" energy (0..1) shared with the PC telemetry thread: the desk
# pet bounces harder to energetic tracks. Derived deterministically from the
# track name (no extra API calls); 0 = nothing playing.
g_music_energy = 0.5

def classify_track_genre(artist, title, raw_itunes_genre=""):
    """
    Bulletproof multi-tiered music genre & vibe classifier.
    Priority: Party / EDM / Dance -> Hip-Hop / Drill -> Rock -> Lo-Fi -> Sad Ballads -> Pop.
    """
    combined = f"{artist or ''} {title or ''} {raw_itunes_genre or ''}".lower()

    # 1. EDM / Dance / Rave / Party / Club / DJ (Priority 1: Party tracks never get classified as sad!)
    edm_keywords = [
        "party", "dance", "club", "rave", "remix", "edm", "house", "techno", "electro",
        "trance", "hardstyle", "dj", "bhangra", "dhol", "garmi", "kala chashma", "ghungroo",
        "kar gayi chull", "aankh marey", "chogada", "saturday saturday", "nashe si chadh gayi",
        "kamariya", "jhoome jo pathaan", "jhoome jo", "besharam rang", "lungi dance",
        "badtameez dil", "subha hone", "high heels", "balam pichkari", "baby doll",
        "chammak challo", "swag se swagat", "dhoom", "mauja hi mauja", "tum hi ho bandhu",
        "gallan goodiyaan", "london thumakda", "morni banke", "coca cola tu", "dheeme dheeme",
        "dilliwaali girlfriend", "dilliwali", "sooraj dooba", "first class", "hookah bar",
        "lutt putt gaya", "breakup song", "nachde ne saare", "sauda khara", "dil chori",
        "chote chote peg", "bom diggy", "makhna", "nacho nacho", "naatu naatu",
        "garrix", "avicii", "marshmello", "skrillex", "dj snake", "tiesto", "alesso",
        "guetta", "calvin harris", "alan walker", "chainsmokers", "zedd", "kygo", "hardwell",
        "nucleya", "ritviz", "lost stories", "pitbull", "dua lipa", "levitating"
    ]
    if any(k in combined for k in edm_keywords):
        return "EDM"

    # 2. Explicit Emotional / Sad Ballads / Emo Rap / Heartbreak (Vibe 5 - Tears, heartbreak, booze)
    sad_keywords = [
        "breakup", "break up", "broken", "heartbreak", "crying", "tears", "goodbye", "sorrow",
        "alone", "lonely", "depressed", "pain", "hurt", "miss you", "farewell", "alcohol",
        "drunk", "whiskey", "beer", "wine", "sober", "sad version", "sad song", "ballad",
        "emo rap", "emo", "sad rap", "heartbreak rap", "melancholy rap", "storytelling", "story telling",
        # Specific MC Stan heartbreak & sad tracks:
        "regret", "how to hate", "fuck love", "fuckk love", "bitch", "dil pe mat le",
        # Specific Juice WRLD heartbreak & emo rap tracks:
        "all girls are the same", "lucid dreams", "robbery", "wishing well", "already dead",
        "cigarettes", "lean wit me", "flaws and sins", "scared of love", "hear me calling",
        # Specific Talha Anjum melancholy & storytelling tracks:
        "downers at dusk", "down at dusk", "gumaan", "afsanay", "open letter", "glass half full",
        # Specific Aleemrk heartbreak & emo rap tracks:
        "cold hours", "surface", "out of love", "distance", "lost", "maine",
        "rehnuma", "kyun", "humdum", "nadaan", "overthink", "late night",
        "2 am", "fall", "guzaarish", "beqabu",
        # XXXTentacion & Lil Peep emo classics:
        "jocelyn flores", "star shopping", "save that shit", "everybody dies in their nightmares",
        # Iconic Ballads & Heartbreak Classics:
        "die with a smile", "someone like you", "easy on me", "set fire to the rain",
        "hello", "rolling in the deep", "someone you loved", "before you go", "drivers license",
        "traitor", "deja vu", "vampire", "happier than ever", "when the party's over",
        "glimpse of us", "slow dancing in the dark", "sanctuary", "lose you to love me",
        "all too well", "cardigan", "exile", "champagne problems", "stay with me",
        "i'm not the only one", "too good at goodbyes", "adele", "lewis capaldi",
        "joji", "olivia rodrigo", "conan gray",
        "kal ho naa ho", "bela bose", "2441139", "agar tum saath", "channa mereya",
        "tujhe bhula diya", "tum hi ho", "sunn raha hai", "hamari adhuri", "bekhayali",
        "pachtaoge", "duaa", "alvida", "tadap tadap", "bhula dena", "phir bhi tumko",
        "ranjish hi sahi", "faasle", "woh lamhe", "kyun main jaagoon", "chithi na koi",
        "kahani suno", "kaifi khalil", "iraaday", "kinna sona", "mann bharryaa",
        "shei tumi", "boba tunnel"
    ]
    if ("mc stan" in combined and "numb" in combined) or any(k in combined for k in sad_keywords):
        return "Ballad"

    # Dedicated Aleemrk classification (Pakistani Urdu Rapper):
    if any(a in combined for a in ["aleemrk", "aleem rk", "aleem r.k."]):
        return "Hip-Hop/Rap"

    # 3. Hip-Hop / Drill / Trap / Rap / Desi Hip-Hop (Vibe 1 - Swagger 11px nod)
    hiphop_keywords = [
        "hip-hop", "hip hop", "rap", "drill", "trap", "808", "grime", "urban", "r&b",
        "boom bap", "freestyle", "cypher", "sambata", "palti fire", "lal ranga",
        "shob choritro", "bhooter raja", "juta abishkar", "yeda yung", "yung dsa",
        "shaggy dushing", "blood tears", "rehmani keeda", "emiway", "bantai", "divine",
        "mc stan", "krsna", "kr$na", "raftaar", "seedhe maut", "calm", "encore abj",
        "aleemrk", "aleem rk", "aleem r.k.", "can't lose", "cant lose", "lollipop", "advice", "genie", "khauf",
        "karan aujla", "aujla", "sidhu moose", "moosewala", "shubh", "ap dhillon",
        "wazir patar", "talha anjum", "talhah yunus", "young stunners", "faris shafi",
        "badshah", "honey singh", "yo yo honey", "king", "mc square", "paradox",
        "gravity", "fotty seven", "bali", "bella", "anirudh", "hukum", "badass",
        "jailer", "leo", "vikram", "rolex", "master", "vathi", "naa ready", "payal",
        "tauba tauba", "devara", "fear song", "drake", "travis scott", "travis",
        "21 savage", "metro boomin", "playboi carti", "carti", "lil uzi", "uzi",
        "future", "kendrick", "eminem", "kanye", "yeat", "central cee", "pop smoke",
        "fivio", "stormzy", "50 cent", "snoop", "j. cole", "weeknd", "post malone",
        "juice wrld", "xxxtentacion", "lil baby", "gunna", "don toliver", "asap rocky"
    ]
    if any(k in combined for k in hiphop_keywords):
        return "Hip-Hop/Rap"

    # 3. Rock / Metal / Punk / Bengali Rock (Vibe 3 - 12px Hard Headbang)
    rock_keywords = [
        "rock", "metal", "punk", "grunge", "hard rock", "heavy metal", "metalcore",
        "fossils", "rupam islam", "rupam", "cactus", "krosswindz", "moheener ghoraguli",
        "lakkhichhara", "aaro ekbar", "hasnuhana", "the local train", "indian ocean",
        "parikrama", "agnee", "euphoria", "linkin park", "linkin", "nirvana",
        "metallica", "queen", "ac/dc", "slipknot", "system of a down", "soad",
        "green day", "foo fighters", "rammstein", "arctic monkeys", "strokes",
        "radiohead", "evanescence", "avenged sevenfold", "bring me the horizon",
        "guns n' roses", "bon jovi", "led zeppelin", "pink floyd",
        "red hot chili peppers", "rhcp", "muse", "iron maiden", "black sabbath",
        "disturbed", "korn", "limp bizkit", "papa roach", "three days grace",
        "breaking benjamin", "skillet", "my chemical romance", "paramore"
    ]
    if any(k in combined for k in rock_keywords):
        return "Rock"

    # 4. Lo-Fi / Chill / Acoustic (Vibe 4 - Gentle Mellow Sway)
    lofi_keywords = [
        "lofi", "lo-fi", "chillhop", "sleep beats", "rain beats", "slowed + reverb",
        "slowed and reverb", "chilledcow", "lofi girl", "study beats", "ambient",
        "meditation", "piano solo", "chill beats"
    ]
    if any(k in combined for k in lofi_keywords):
        return "Lofi"

    # 5. Fallback based on raw iTunes primaryGenreName
    if raw_itunes_genre:
        low_it = raw_itunes_genre.lower()
        if "hip-hop" in low_it or "rap" in low_it or "r&b" in low_it:
            return "Hip-Hop/Rap"
        if "rock" in low_it or "metal" in low_it or "alternative" in low_it:
            return "Rock"
        if "dance" in low_it or "electronic" in low_it or "house" in low_it:
            return "EDM"
        if "singer" in low_it or "acoustic" in low_it or "blues" in low_it:
            return "Ballad"

    # 7. Instant LLM Query (sub-0.5s) if AI cortex is active
    if 'g_ai_cortex' in globals() and g_ai_cortex:
        try:
            ai_genre = g_ai_cortex.classify_song_genre(artist, title)
            if ai_genre in ["EDM", "Hip-Hop/Rap", "Rock", "Lofi", "Ballad"]:
                return ai_genre
        except Exception:
            pass

    return "Pop"

def clean_music_meta(artist, title):
    if not title:
        return artist or "", "", ""

    # Filter out dummy stream noise
    if title in ["Spotify - Unknown", "Unknown", "Waiting for music..."]:
        return "", "", ""

    # 1. Detect genre using the comprehensive classifier
    detected_genre = classify_track_genre(artist, title, "")

    # 2. Extract best title segment from '|' separated YouTube titles
    segments = [s.strip() for s in title.split("|") if s.strip()]
    chosen_title = title
    if len(segments) > 1:
        def seg_score(s):
            ascii_chars = sum(1 for c in s if 32 <= ord(c) <= 126)
            total = max(1, len(s))
            ratio = ascii_chars / total
            low = s.lower()
            if any(t in low for t in ["official video", "lyric video", "full song", "bangla new song", "hindi song", "video 202", "audio 202", "@"]):
                return -10.0
            return ratio * 10.0 + min(len(s), 25)

        best = max(segments, key=seg_score)
        if seg_score(best) > 0:
            chosen_title = best

    # If title has "Artist - Song", separate them if artist is generic
    if " - " in chosen_title and (not artist or artist.lower() in ["youtube", "unknown", "spotify"]):
        parts = chosen_title.split(" - ", 1)
        artist = parts[0].strip()
        chosen_title = parts[1].strip()

    # Remove [Official Video], (Audio), (Full Song), etc.
    chosen_title = re.sub(r'[\(\[\{].*?(?:video|official|audio|remake|song|remix|hd|4k|8k|prod\.).*?[\)\]\}]', '', chosen_title, flags=re.IGNORECASE).strip()
    
    # Remove non-ASCII characters if mostly ASCII
    ascii_title = "".join(c for c in chosen_title if 32 <= ord(c) <= 126).strip(" -|:")
    if len(ascii_title) >= 3:
        chosen_title = ascii_title

    # Clean artist
    clean_artist = "".join(c for c in (artist or "") if 32 <= ord(c) <= 126).strip(" -|:")
    if not clean_artist:
        clean_artist = "Music"

    return clean_artist, chosen_title, detected_genre

g_audio_analyzer = None

def track_energy(artist, title):
    global g_audio_analyzer
    if g_audio_analyzer and g_audio_analyzer.is_playing:
        return round(float(g_audio_analyzer.current_energy), 2)
    h = sum(ord(c) for c in (artist or "") + (title or ""))
    return round(0.3 + (h % 7) / 10.0, 2)  # 0.3 .. 0.9

def lookup_genre(artist, title):
    """Genre via the same iTunes search used for artwork (no auth needed).
    Returns e.g. 'Hip-Hop/Rap', 'Rock', 'Bollywood' or ''."""
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

def get_genre_led_color(genre):
    g = (genre or "").lower()
    if "ballad" in g or "breakup" in g or "sad" in g:
        return (20, 70, 255)    # Deep sorrow oceanic blue (Breakup, Emo Rap, Crying)
    if "rock" in g or "metal" in g:
        return (255, 20, 20)    # Fiery crimson rage (Rock, Metal)
    if "edm" in g or "dance" in g:
        return (240, 30, 200)   # Neon electric magenta / cyber violet (EDM, Rave)
    if "hip-hop" in g or "rap" in g or "drill" in g:
        return (140, 30, 255)   # Midnight 808 purple (Hip-Hop, Drill)
    if "lofi" in g or "chill" in g:
        return (255, 120, 35)   # Warm sunset amber / candle peach (Lo-Fi)
    return (255, 180, 35)       # Sunny golden glow (Pop, Happy)

def push_ambient_led(r, g, b):
    try:
        http_session.post(get_url("/api/led/set"), json={"r": int(r), "g": int(g), "b": int(b)}, timeout=1.5)
    except Exception:
        pass

def sync_ambient_lighting(genre, title, raw_art=None):
    """
    Sets the monitor ambient back-light color:
    1. Visualizer tracks or unclassified ambient -> extracted dominant thumbnail theme color.
    2. Specific genre tracks (Sad/Breakup, Rock/Rage, EDM, Hip-Hop, Lofi, Pop) -> genre signature mood color.
    """
    low_title = (title or "").lower()
    is_visualizer = any(w in low_title for w in ["visualizer", "visualiser", "audio video", "bass boosted", "loop", "bgm", "instrumental"])

    target_rgb = None
    if raw_art and (is_visualizer or genre in ["Pop", "Auto", ""]):
        target_rgb = extract_vibrant_rgb(raw_art)
    elif genre and genre != "Pop":
        target_rgb = get_genre_led_color(genre)
    elif raw_art:
        target_rgb = extract_vibrant_rgb(raw_art)
    else:
        target_rgb = get_genre_led_color(genre)

    if target_rgb:
        threading.Thread(target=push_ambient_led, args=target_rgb, daemon=True).start()

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

        # Ambient RGB monitor backlight: sync genre mood color or thumbnail theme
        if raw_art:
            sync_ambient_lighting(g_music_genre, "", raw_art)

        if os.path.exists(jpeg_to_upload) and os.path.getsize(jpeg_to_upload) > 500:
            with open(jpeg_to_upload, "rb") as f:
                jpeg_bytes = f.read()

            try:
                files = {"file": ("cover.jpg", jpeg_bytes, "image/jpeg")}
                r = http_session.post(get_url("/api/spotify/cover"), files=files, timeout=2)
                if r.status_code == 200:
                    print("  [ART] Cover art sent to Desk Monitor!")
                    return True
            except Exception:
                pass

            try:
                files = {"file": ("/art.jpg", jpeg_bytes, "image/jpeg")}
                r = http_session.post(get_url("/api/fs/upload?path=/art.jpg"), files=files, timeout=2)
                if r.status_code == 200:
                    http_session.get(get_url("/api/spotify/testart?url=/art.jpg"), timeout=2)
                    print("  [ART] Cover art sent via FS fallback!")
                    return True
            except Exception:
                pass
    except Exception as e:
        print(f"  [ART ERROR] {e}")
    return False

def push_spotify_now(title, artist, pos_ms=0, dur_ms=0, is_playing=True, genre=""):
    global http_session, g_music_genre, g_music_energy, g_audio_analyzer
    # If live audio beat pattern analyzer is active, update energy dynamics
    if g_audio_analyzer and g_audio_analyzer.is_playing:
        with g_audio_analyzer.lock:
            g_music_energy = g_audio_analyzer.current_energy
            # Only fall back to audio analyzer genre if metadata genre is completely unknown
            if not g_music_genre and g_audio_analyzer.detected_genre:
                g_music_genre = g_audio_analyzer.detected_genre

    resolved_genre = genre or g_music_genre or "Pop"
    payload = {
        "title": title,
        "artist": artist,
        "progress_ms": int(pos_ms),
        "duration_ms": int(dur_ms) if dur_ms > 0 else 180000,
        "is_playing": is_playing,
        "genre": resolved_genre
    }
    for attempt in range(2):
        try:
            resp = http_session.post(get_url("/api/spotify/now"), json=payload, timeout=1.8)
            if resp.status_code == 200:
                return True
        except Exception:
            find_device_ip()
            try:
                http_session = requests.Session()
                http_session.mount("http://", requests.adapters.HTTPAdapter(pool_connections=5, pool_maxsize=10, max_retries=1))
            except Exception:
                pass
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

            # Sync live audio beat pattern telemetry if active (energy only; never overwrite locked track genre)
            if g_audio_analyzer and g_audio_analyzer.is_playing:
                with g_audio_analyzer.lock:
                    g_music_energy = g_audio_analyzer.current_energy

            # Autonomous AI Hardware Sentinel Check
            if 'g_ai_cortex' in globals() and g_ai_cortex:
                try:
                    g_ai_cortex.inspect_pc_telemetry(cpu, gpu_pct, gpu_temp, mem.percent)
                except Exception:
                    pass

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
            http_session.post(get_url("/api/pc/stats"), json=payload, timeout=1.5)
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

def async_lookup_genre(artist, title, current_track_id, track_lock, pos_ms=0, dur_ms=0, is_playing=True):
    """Background genre lookup so the timeline loop never blocks."""
    global g_music_genre
    # If genre is already identified with confidence, do not disrupt the playing track
    if g_music_genre and g_music_genre != "Pop":
        return
    try:
        raw_genre = lookup_genre(artist, title)
        refined = classify_track_genre(artist, title, raw_genre)
        if track_lock["id"] == current_track_id and refined and refined != g_music_genre and refined != "Pop":
            g_music_genre = refined
            print(f"  [GENRE REFINED] -> {refined} (via catalog: {raw_genre})")
            push_spotify_now(title, artist, pos_ms, dur_ms, is_playing, g_music_genre)
    except Exception:
        pass

def main():
    global g_music_energy, g_music_genre
    print("=" * 65)
    print(" STΔN studio. Spotify & Media Desk Station Sync (Low-Latency Native)")
    print(f" Target Device: http://{DEVICE_IP}/")
    print(" PC Hardware Telemetry & Weather Sync: Active (2s interval)")
    print(" Listening for music playing on your PC...")
    print("=" * 65)

    # Launch background real-time Audio Beat Pattern & Genre Analyzer
    global g_audio_analyzer, g_ai_cortex
    try:
        from audio_beat_analyzer import BeatPatternAnalyzer
        g_audio_analyzer = BeatPatternAnalyzer(esp_ip=DEVICE_IP)
        threading.Thread(target=g_audio_analyzer.run, kwargs={"print_dashboard": False}, daemon=True).start()
        print(" Real-Time Audio Beat Pattern Analyzer: Active (WASAPI Loopback)")
    except Exception as e:
        print(f" Real-Time Audio Beat Pattern Analyzer: Inactive ({e})")

    # Launch background Autonomous AI Cognitive Sentinel (NVIDIA NIM)
    try:
        from pet_ai_cortex import PetAICortex
        g_ai_cortex = PetAICortex(esp_ip=DEVICE_IP)
        threading.Thread(target=g_ai_cortex.run, daemon=True).start()
        print(" Autonomous AI Cognitive Sentinel: Active (NVIDIA NIM Zero Cold-Start)")
    except Exception as e:
        print(f" Autonomous AI Cognitive Sentinel: Inactive ({e})")

    # Launch background PC Telemetry & Weather thread
    threading.Thread(target=pc_telemetry_loop, daemon=True).start()

    last_track_id = ""
    last_art_track = ""
    last_is_playing = None
    track_lock = {"id": ""}
    idle_count = 0
    last_push_time = 0.0
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
                idle_count += 1
                if idle_count >= 5 and last_track_id:
                    push_spotify_now("", "", 0, 0, False, "")
                    last_track_id = ""
                    last_is_playing = False
                    track_lock["id"] = ""
                    g_music_energy = 0.0
                    g_music_genre = ""
                continue
            idle_count = 0

            parts = line.split("::::")
            if len(parts) >= 2:
                prefix_parts = parts[0].split("::")
                has_thumb = prefix_parts[0] == "THUMB"
                is_playing = prefix_parts[1] == "PLAY" if len(prefix_parts) >= 2 else True
                raw_artist = prefix_parts[2].strip() if len(prefix_parts) >= 3 else prefix_parts[-1].strip()
                raw_title = parts[1].strip()

                try:
                    pos_ms = int(float(parts[2])) if len(parts) >= 3 else 0
                except Exception:
                    pos_ms = 0

                try:
                    dur_ms = int(float(parts[3])) if len(parts) >= 4 else 0
                except Exception:
                    dur_ms = 0

                artist, title, genre_hint = clean_music_meta(raw_artist, raw_title)
                if not title:
                    continue

                track_id = f"{artist} - {title}"
                track_lock["id"] = track_id
                now_ts = time.time()
                play_state_changed = (is_playing != last_is_playing)

                # Instant push on track transition OR Play/Pause state change!
                if track_id != last_track_id or play_state_changed:
                    last_track_id = track_id
                    last_is_playing = is_playing
                    last_push_time = now_ts
                    # Reset and immediately classify genre for the new track
                    g_music_genre = genre_hint or "Pop"
                    g_music_energy = track_energy(artist, title) if is_playing else 0.0
                    print(f"\n[MEDIA EVENT] {artist} - {title} (Playing: {is_playing}, Genre: {g_music_genre})")
                    push_spotify_now(title, artist, pos_ms, dur_ms, is_playing, g_music_genre)
                    if is_playing:
                        sync_ambient_lighting(g_music_genre, title)

                    # Trigger Autonomous AI Music Critic
                    if 'g_ai_cortex' in globals() and g_ai_cortex and is_playing:
                        try:
                            bpm_val = getattr(g_audio_analyzer, "detected_bpm", 120.0) if g_audio_analyzer else 120.0
                            threading.Thread(
                                target=g_ai_cortex.inspect_music_vibe,
                                args=(title, artist, g_music_genre, bpm_val, g_music_energy),
                                daemon=True
                            ).start()
                        except Exception:
                            pass

                    # Background lookup for online catalog verification
                    threading.Thread(
                        target=async_lookup_genre,
                        args=(artist, title, track_id, track_lock, pos_ms, dur_ms, is_playing),
                        daemon=True
                    ).start()

                    if track_id != last_art_track and is_playing:
                        last_art_track = track_id
                        # Non-blocking background art upload so the timeline stream never hangs
                        if has_thumb and os.path.exists(THUMB_RAW_PATH):
                            threading.Thread(target=upload_album_art, args=(THUMB_RAW_PATH,), daemon=True).start()
                            t = threading.Thread(
                                target=async_fetch_hd_art,
                                args=(artist, title, track_id, track_lock),
                                daemon=True
                            )
                            t.start()
                        else:
                            t = threading.Thread(
                                target=async_fetch_hd_art,
                                args=(artist, title, track_id, track_lock),
                                daemon=True
                            )
                            t.start()
                else:
                    # Update live progress bar and play/pause state regularly so ESP32 keepalive stays active
                    if is_playing and (now_ts - last_push_time >= 1.8):
                        last_push_time = now_ts
                        push_spotify_now(title, artist, pos_ms, dur_ms, is_playing, g_music_genre)

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
