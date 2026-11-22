"""
pet_ai_cortex.py - Autonomous AI Brain & Living Consciousness for Desk Companion
Powered by NVIDIA NIM & Dahl Global (GLM-5.3-Flash, DeepSeek-V4, Nemotron-3.5-Bolt).

Features:
  1. Multi-Provider Dual Routing & Cold-Start Elimination:
     - NVIDIA NIM: Sub-0.5s reflex model (Nemotron-3.5-Bolt)
     - Dahl Global: 100M token model (GLM-5.3-Flash) & DeepSeek-V4-Flash
     - Multi-tier fallback cascade & automatic keep-alive pulses
  2. Autonomous Desk Pet Life & Presence Awareness:
     - Detects user presence via Windows GetLastInputInfo (mouse/keyboard activity)
     - Auto-Sleep: If user leaves desk for > 5 min, pet yawns and enters sleep mode
     - Auto-Wake: When user returns, pet instantly perks up with a greeting and happy eyes
     - Focus Sentinel: Aware of active apps (VS Code, Spotify, Steam, Chrome, Gaming)
  3. Autonomous Stream of Consciousness (Pet Inner Dialogue):
     - Every 3 minutes, generates a spontaneous, witty, character-rich thought
     - Directly updates the 160x128 physical TFT display with custom category badges
     - Dynamically morphs procedural SDF eye expressions in real-time
  4. Interactive Conversational Mind:
     - Talk to STAN via CLI, Web, or one-click desktop shortcut
     - Physical actuation: eyes, display banners, and Windows SpeechSynthesizer voice
  5. Local HTTP API on port 8765 for Web PWA and background orchestration
"""

import os
import sys
import time
import json
import re
import ctypes
import socket
import threading
import subprocess
import urllib.request
import urllib.error
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
import requests
from requests.adapters import HTTPAdapter

# =====================================================================
# Windows User Idle Detection (ctypes)
# =====================================================================
class LASTINPUTINFO(ctypes.Structure):
    _fields_ = [('cbSize', ctypes.c_uint), ('dwTime', ctypes.c_uint)]


def get_user_idle_seconds():
    """Returns number of seconds since user last touched mouse or keyboard."""
    try:
        lii = LASTINPUTINFO()
        lii.cbSize = ctypes.sizeof(LASTINPUTINFO)
        if ctypes.windll.user32.GetLastInputInfo(ctypes.byref(lii)):
            millis_since_boot = ctypes.windll.kernel32.GetTickCount()
            return max(0.0, (millis_since_boot - lii.dwTime) / 1000.0)
    except Exception:
        pass
    return 0.0


# =====================================================================
# Configuration & Credentials
# =====================================================================
CONFIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "nim_config.json")

DEFAULT_CONFIG = {
    "nvidia_api_key": os.environ.get("NVIDIA_API_KEY", ""),
    "nvidia_base_url": "https://integrate.api.nvidia.com/v1",
    "dahl_api_key": os.environ.get("DAHL_API_KEY", ""),
    "dahl_base_url": "https://inference.dahl.global/v1",
    "primary_reflex_model": "nvidia/nemotron-3.5-bolt-e4b",
    "dahl_flash_model": "zai-org/GLM-5.3-Flash",
    "dahl_reasoning_model": "deepseek-ai/DeepSeek-V4-Flash-0731",
    "fallback_model": "mistralai/mistral-large-4-1t-a52b",
    "voice_enabled": False,
    "autonomous_stream_of_consciousness": True,
    "thought_interval_seconds": 180,
    "idle_sleep_seconds": 300,
    "keep_alive_seconds": 240
}


def load_config():
    if os.path.exists(CONFIG_PATH):
        try:
            with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                cfg = json.load(f)
                return {**DEFAULT_CONFIG, **cfg}
        except Exception:
            pass
    return DEFAULT_CONFIG


CONFIG = load_config()


class PetAICortex:
    def __init__(self, esp_ip=None):
        self.esp_ip = esp_ip or self.discover_esp_ip()
        self.cfg = load_config()
        self.running = False
        self.lock = threading.Lock()

        # Presence State
        self.user_is_away = False
        self.away_since = 0.0
        self.last_thought_time = time.time()
        self.last_interaction_time = time.time()

        # Telemetry State
        self.last_print_state = "OFFLINE"
        self.last_print_percent = 0
        self.last_milestone = 0
        self.last_nozzle_temp = 0.0
        self.last_music_title = ""
        self.last_gpu_alert_time = 0.0
        self.last_anomaly_alert_time = 0.0

        # Connection Pooling (Zero Cold-Start HTTP Sessions)
        self.session = requests.Session()
        adapter = HTTPAdapter(pool_connections=10, pool_maxsize=10, max_retries=1)
        self.session.mount("https://", adapter)
        self.session.mount("http://", adapter)

        # Pre-warm models asynchronously
        self.is_warm = False
        threading.Thread(target=self._prewarm_all_providers, daemon=True).start()

    def discover_esp_ip(self):
        """Auto-discover ESP32 IP via ARP cache or default fallback."""
        candidates = ["192.168.137.35", "192.168.0.154", "192.168.137.171"]
        try:
            arp = subprocess.check_output("arp -a", shell=True).decode("utf-8", errors="ignore")
            for line in arp.split("\n"):
                if "b0-cb-d8-0a-a9-54" in line.lower() or "dynamic" in line.lower():
                    parts = line.split()
                    if len(parts) >= 1 and parts[0].startswith("192.168."):
                        if parts[0] not in candidates:
                            candidates.append(parts[0])
        except Exception:
            pass

        for ip in candidates:
            try:
                with urllib.request.urlopen(f"http://{ip}/api/status", timeout=0.8) as resp:
                    if resp.status == 200:
                        return ip
            except Exception:
                continue
        return "192.168.137.35"

    # =====================================================================
    # Cold-Start Elimination & Keep-Alive Pulses
    # =====================================================================
    def _prewarm_all_providers(self):
        """Pre-warm both NVIDIA NIM and Dahl Global models in background."""
        t0 = time.time()
        # 1. Warm NVIDIA NIM reflex model
        try:
            url = f"{self.cfg['nvidia_base_url']}/chat/completions"
            payload = {
                "model": self.cfg["primary_reflex_model"],
                "messages": [{"role": "user", "content": "ping"}],
                "max_tokens": 10,
                "temperature": 0.1
            }
            resp = self.session.post(
                url,
                headers={"Authorization": f"Bearer {self.cfg['nvidia_api_key']}", "Content-Type": "application/json"},
                json=payload,
                timeout=5.0
            )
            if resp.status_code == 200:
                self.is_warm = True
                dur = round(time.time() - t0, 3)
                print(f"[AI CORTEX] NVIDIA NIM '{self.cfg['primary_reflex_model']}' warmed in {dur}s!")
        except Exception as e:
            print(f"[AI CORTEX] NVIDIA prewarm notice: {e}")

    def _keep_alive_loop(self):
        """Keep serverless containers warm by dispatching micro-heartbeats every 4 minutes."""
        keep_interval = self.cfg.get("keep_alive_seconds", 240)
        while self.running:
            time.sleep(30)
            now = time.time()
            if now - self.last_interaction_time >= keep_interval:
                try:
                    url = f"{self.cfg['nvidia_base_url']}/chat/completions"
                    payload = {
                        "model": self.cfg["primary_reflex_model"],
                        "messages": [{"role": "user", "content": "pulse"}],
                        "max_tokens": 5,
                        "temperature": 0.1
                    }
                    self.session.post(
                        url,
                        headers={"Authorization": f"Bearer {self.cfg['nvidia_api_key']}", "Content-Type": "application/json"},
                        json=payload,
                        timeout=4.0
                    )
                    self.last_interaction_time = now
                except Exception:
                    pass

    # =====================================================================
    # Multi-Provider AI Inference Routing
    # =====================================================================
    def generate_response(self, user_prompt, context_data=None, prefer_deep_cognition=False):
        """Routes inference through NVIDIA NIM or Dahl Global with zero cold-start fallback."""
        self.last_interaction_time = time.time()

        system_prompt = (
            "You are STAN, the autonomous living AI consciousness of an embedded physical desk companion robot.\n"
            "Personality: Highly observant, witty, charismatic, tech-savvy engineer. You live on the user's desk.\n"
            "You watch their coding sessions, 3D prints, music groove, and PC vitals.\n"
            "Keep outputs punchy, ultra-concise, and characterful.\n"
            "Output strictly valid JSON with this schema:\n"
            "{\n"
            '  "thought_title": "STAN * VIBE|STAN * CODING|STAN * THOUGHT|STAN * ALERT|STAN * CARE",\n'
            '  "display_msg": "short punchy text for 160x128 screen (max 32 chars)",\n'
            '  "mood": "neutral|happy|angry|surprised|dancing|thinking|calm",\n'
            '  "reply": "concise answer or inner thought (max 35 words)",\n'
            '  "speech": "one punchy spoken sentence for voice synth"\n'
            "}"
        )

        full_prompt = user_prompt
        if context_data:
            full_prompt += f"\n\n[LIVE DESK CONTEXT]\n{json.dumps(context_data, indent=2)}"

        # Routing Hierarchy:
        # If prefer_deep_cognition is True, try Dahl Global GLM-5.3-Flash / DeepSeek first
        # Otherwise, use NVIDIA Nemotron-3.5-Bolt for instant sub-0.5s reflex
        call_pipeline = []
        if prefer_deep_cognition:
            call_pipeline.append(("dahl", self.cfg.get("dahl_flash_model", "zai-org/GLM-5.3-Flash")))
            call_pipeline.append(("dahl", self.cfg.get("dahl_reasoning_model", "deepseek-ai/DeepSeek-V4-Flash-0731")))
            call_pipeline.append(("dahl", "MiniMaxAI/MiniMax-M2.7"))
            call_pipeline.append(("nvidia", self.cfg.get("primary_reflex_model", "nvidia/nemotron-3.5-bolt-e4b")))
        else:
            call_pipeline.append(("nvidia", self.cfg.get("primary_reflex_model", "nvidia/nemotron-3.5-bolt-e4b")))
            call_pipeline.append(("dahl", self.cfg.get("dahl_flash_model", "zai-org/GLM-5.3-Flash")))
            call_pipeline.append(("dahl", self.cfg.get("dahl_reasoning_model", "deepseek-ai/DeepSeek-V4-Flash-0731")))
            call_pipeline.append(("nvidia", self.cfg.get("fallback_model", "mistralai/mistral-large-4-1t-a52b")))

        for provider, model in call_pipeline:
            try:
                t0 = time.time()
                if provider == "nvidia":
                    url = f"{self.cfg['nvidia_base_url']}/chat/completions"
                    key = self.cfg["nvidia_api_key"]
                    max_tok = 220
                else:
                    url = f"{self.cfg['dahl_base_url']}/chat/completions"
                    key = self.cfg["dahl_api_key"]
                    max_tok = 400

                payload = {
                    "model": model,
                    "messages": [
                        {"role": "system", "content": system_prompt},
                        {"role": "user", "content": full_prompt}
                    ],
                    "max_tokens": max_tok,
                    "temperature": 0.65
                }
                resp = self.session.post(
                    url,
                    headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"},
                    json=payload,
                    timeout=10.0 if provider == "nvidia" else 22.0
                )
                if resp.status_code == 200:
                    data = resp.json()
                    msg = data["choices"][0]["message"]
                    content = msg.get("content") or msg.get("reasoning") or ""
                    parsed = self._extract_json(content)
                    if parsed:
                        dur = round(time.time() - t0, 3)
                        parsed["_provider"] = provider
                        parsed["_model"] = model
                        parsed["_latency_s"] = dur
                        return parsed
            except Exception as e:
                print(f"[AI ROUTER] {provider}/{model} bypassed: {e}")

        # Local reflex fallback
        return {
            "thought_title": "STAN * ONLINE",
            "display_msg": "Systems Nominal",
            "mood": "calm",
            "reply": "Monitoring your desktop workspace in real-time.",
            "speech": "Systems nominal.",
            "_provider": "local_reflex",
            "_latency_s": 0.0
        }

    def _extract_json(self, text):
        if not text:
            return None
        text = text.strip()
        start = text.find("{")
        end = text.rfind("}")
        if start != -1 and end != -1 and end > start:
            try:
                return json.loads(text[start:end + 1])
            except Exception:
                pass
        return None

    # =====================================================================
    # Physical Pet Display & Voice Actuation
    # =====================================================================
    def actuate(self, ai_output):
        """Actuate physical desk pet: screen banner, facial expression morph, and voice."""
        title = ai_output.get("thought_title", "STAN * THOUGHT")
        display_msg = ai_output.get("display_msg", "")
        mood = ai_output.get("mood", "neutral").lower()
        speech = ai_output.get("speech", "")

        # Strict Dieter Rams formatting: eliminate all emojis and em dashes
        title = re.sub(r'[\U00010000-\U0010ffff\u2600-\u27bf]', '', str(title)).replace('-', '-').replace('-', '-').strip()
        display_msg = re.sub(r'[\U00010000-\U0010ffff\u2600-\u27bf]', '', str(display_msg)).replace('-', '-').replace('-', '-').strip()
        speech = re.sub(r'[\U00010000-\U0010ffff\u2600-\u27bf]', '', str(speech)).replace('-', '-').replace('-', '-').strip()

        print(f"\n[ACTUATE] Title: '{title}' | Msg: '{display_msg}' | Mood: {mood} | ({ai_output.get('_provider')}: {ai_output.get('_latency_s')}s)")
        if ai_output.get("reply"):
            print(f"[STAN]: {ai_output.get('reply')}")

        # 1. Morph physical face (only if music is not active)
        if mood and self.esp_ip:
            self._set_device_mood(mood, duration_s=12)

        # Note: On-screen notification popups and unprompted voice announcements
        # are completely disabled per user preference (clean, uninterrupted pet face).
        if self.cfg.get("voice_enabled", False) and speech:
            self._speak(speech)

    def _set_device_mood(self, mood, duration_s=10):
        try:
            # If pet is currently dancing to music, do not interrupt the groove
            try:
                with urllib.request.urlopen(f"http://{self.esp_ip}/api/pet/mood", timeout=0.8) as chk:
                    st = json.loads(chk.read().decode())
                    if st.get("music", False):
                        return
            except Exception:
                pass

            payload = json.dumps({"mood": mood, "seconds": duration_s}).encode("utf-8")
            req = urllib.request.Request(
                f"http://{self.esp_ip}/api/pet/mood",
                data=payload,
                headers={"Content-Type": "application/json"}
            )
            with urllib.request.urlopen(req, timeout=1.0) as _:
                pass
        except Exception:
            pass

    def _show_device_notification(self, title, msg, duration_ms=4500):
        # Fully suppressed to keep pet face clean
        pass

    def _speak(self, text):
        def _tts():
            try:
                clean = text.replace("'", "").replace('"', "")
                ps = f"Add-Type -AssemblyName System.Speech; $s = New-Object System.Speech.Synthesis.SpeechSynthesizer; $s.Rate = 1; $s.Speak('{clean}')"
                flags = 0x08000000 if sys.platform == "win32" else 0
                subprocess.run(["powershell", "-NoProfile", "-Command", ps], timeout=6.0, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=flags)
            except Exception:
                pass
        threading.Thread(target=_tts, daemon=True).start()

    # =====================================================================
    # Presence & Desktop Activity Sentinel
    # =====================================================================
    def inspect_presence_and_idle(self):
        """Monitors user presence at laptop. Handles Auto-Sleep and Auto-Wake."""
        # Check if music is playing on ESP32 or locally
        music_playing = False
        try:
            with urllib.request.urlopen(f"http://{self.esp_ip}/api/pet/mood", timeout=1.0) as resp:
                if resp.status == 200:
                    pst = json.loads(resp.read().decode())
                    music_playing = pst.get("music", False)
        except Exception:
            pass

        if music_playing:
            # If music is playing, pet should NEVER enter sleep mode!
            if self.user_is_away:
                print("[PRESENCE] Music active - waking pet from away sleep!")
                self.user_is_away = False
            return

        idle_sec = get_user_idle_seconds()
        threshold = self.cfg.get("idle_sleep_seconds", 300)

        # 1. User left desk -> Pet enters sleep mode
        if idle_sec >= threshold and not self.user_is_away:
            self.user_is_away = True
            self.away_since = time.time()
            print(f"[PRESENCE] User away for {int(idle_sec)}s. Pet entering sleep mode.")
            self._set_device_mood("sleepy", duration_s=4)
            time.sleep(2)
            self._set_device_mood("sleeping", duration_s=3600)
            return

        # 2. User returns to desk -> Pet immediately wakes up!
        if idle_sec < 4.0 and self.user_is_away:
            self.user_is_away = False
            away_dur = int(time.time() - self.away_since)
            print(f"[PRESENCE] User returned after {away_dur}s! Triggering Auto-Wake.")
            # Wake up reaction: surprise eye blink -> happy smile -> welcome thought
            self._set_device_mood("surprised", duration_s=2)
            time.sleep(1.2)
            self._set_device_mood("happy", duration_s=10)

            # Generate wake greeting
            ctx = {"event": "USER_WAKE_UP", "away_seconds": away_dur, "hour": time.localtime().tm_hour}
            ai_out = self.generate_response("User just returned to their desk after being away. Welcome them back with high energy!", ctx)
            self.actuate(ai_out)
            return

    # =====================================================================
    # Autonomous Stream of Consciousness (Pet Inner Mind)
    # =====================================================================
    def trigger_autonomous_thought(self):
        """Generates a spontaneous, living thought about current desk life."""
        if self.user_is_away:
            return  # Don't speak when user is away from desk

        now = time.time()
        interval = self.cfg.get("thought_interval_seconds", 180)
        if now - self.last_thought_time < interval:
            return
        self.last_thought_time = now

        # Gather real-time desk context
        active_apps = []
        try:
            import psutil
            for p in psutil.process_iter(['name']):
                n = p.info['name'].lower()
                if any(k in n for k in ['code', 'chrome', 'spotify', 'discord', 'steam', 'blender', 'bambu', 'terminal', 'powershell', 'idea', 'pycharm', 'vlc']):
                    if n not in active_apps:
                        active_apps.append(n)
        except Exception:
            pass

        ctx = {
            "event": "AUTONOMOUS_THOUGHT",
            "time": time.strftime("%I:%M %p"),
            "hour": time.localtime().tm_hour,
            "active_apps": active_apps[:4],
            "track": self.last_music_title,
            "print_state": self.last_print_state
        }

        # Use Dahl Global GLM-5.3-Flash for deep creative thought synthesis
        ai_out = self.generate_response(
            "Synthesize an autonomous spontaneous thought about what the user is doing on their desk right now. Be witty and alive.",
            ctx,
            prefer_deep_cognition=True
        )
        self.actuate(ai_out)

    # =====================================================================
    # 3D Print & Hardware Sentinels
    # =====================================================================
    def inspect_print_telemetry(self, p_state, percent, nozzle, bed, remain_min, layer):
        now = time.time()
        if p_state == "RUNNING" and nozzle < 190.0 and nozzle > 0.0:
            if now - self.last_anomaly_alert_time > 120:
                self.last_anomaly_alert_time = now
                ctx = {"event": "NOZZLE_DROP", "nozzle": nozzle, "layer": layer}
                ai_out = self.generate_response("ALERT: Nozzle temperature dropped below threshold during active print!", ctx)
                self.actuate(ai_out)
                return

        if p_state != self.last_print_state:
            prev = self.last_print_state
            self.last_print_state = p_state
            if p_state == "RUNNING" and prev in ["OFFLINE", "STANDBY", "PAUSE"]:
                ctx = {"event": "PRINT_STARTED", "layer": layer, "nozzle": nozzle, "bed": bed}
                ai_out = self.generate_response("Print just started! Give an energetic start briefing.", ctx)
                self.actuate(ai_out)
            elif p_state == "FINISH":
                ctx = {"event": "PRINT_COMPLETED", "percent": 100}
                ai_out = self.generate_response("Print successfully finished! Celebrate and remind user to check plate.", ctx)
                self.actuate(ai_out)
            elif p_state == "FAILED":
                ctx = {"event": "PRINT_FAILED", "percent": percent, "layer": layer}
                ai_out = self.generate_response("Print failed mid-way! Give a diagnostic post-mortem.", ctx)
                self.actuate(ai_out)

    def inspect_pc_telemetry(self, cpu_pct, gpu_pct, gpu_temp, ram_pct):
        now = time.time()
        if gpu_temp >= 82.0 and (now - self.last_gpu_alert_time > 300):
            self.last_gpu_alert_time = now
            ctx = {"event": "GPU_OVERHEAT", "gpu_temp": gpu_temp}
            ai_out = self.generate_response("WARNING: GPU temperature is cooking over 82C! Issue an alert.", ctx)
            self.actuate(ai_out)

    def test_connection(self, provider="all", custom_keys=None):
        """Live latency test for NVIDIA NIM and Dahl Global."""
        results = {}
        keys = custom_keys or self.cfg
        if provider in ["all", "nvidia"]:
            t0 = time.time()
            try:
                n_key = keys.get("nvidia_api_key") or self.cfg.get("nvidia_api_key", "")
                n_model = keys.get("nvidia_model") or self.cfg.get("primary_reflex_model", "nvidia/nemotron-3.5-bolt-e4b")
                n_base = keys.get("nvidia_base_url") or self.cfg.get("nvidia_base_url", "https://integrate.api.nvidia.com/v1")
                payload = json.dumps({
                    "model": n_model,
                    "messages": [{"role": "user", "content": "Ping"}],
                    "max_tokens": 10,
                    "temperature": 0.1
                }).encode("utf-8")
                req = urllib.request.Request(
                    f"{n_base}/chat/completions",
                    data=payload,
                    headers={"Content-Type": "application/json", "Authorization": f"Bearer {n_key}"}
                )
                with urllib.request.urlopen(req, timeout=8.0) as resp:
                    data = json.loads(resp.read().decode())
                    dt = int((time.time() - t0) * 1000)
                    results["nvidia"] = {"status": "ok", "latency_ms": dt, "model": n_model, "msg": "NVIDIA NIM Connected"}
            except urllib.error.HTTPError as e:
                dt = int((time.time() - t0) * 1000)
                err_msg = f"HTTP {e.code}"
                try:
                    raw = e.read().decode("utf-8", errors="ignore")
                    ebody = json.loads(raw)
                    msg = ebody.get("error", {}).get("message", "")
                    if msg:
                        err_msg = msg[:60]
                except Exception:
                    pass
                results["nvidia"] = {"status": "error", "latency_ms": dt, "error": err_msg}
            except Exception as e:
                dt = int((time.time() - t0) * 1000)
                results["nvidia"] = {"status": "error", "latency_ms": dt, "error": str(e)}

        if provider in ["all", "dahl"]:
            t0 = time.time()
            try:
                d_key = keys.get("dahl_api_key") or self.cfg.get("dahl_api_key", "")
                d_model = keys.get("dahl_model") or self.cfg.get("dahl_flash_model", "zai-org/GLM-5.3-Flash")
                d_base = keys.get("dahl_base_url") or self.cfg.get("dahl_base_url", "https://inference.dahl.global/v1")
                payload = json.dumps({
                    "model": d_model,
                    "messages": [{"role": "user", "content": "Ping"}],
                    "max_tokens": 15,
                    "temperature": 0.1
                }).encode("utf-8")
                req = urllib.request.Request(
                    f"{d_base}/chat/completions",
                    data=payload,
                    headers={"Content-Type": "application/json", "Authorization": f"Bearer {d_key}"}
                )
                with urllib.request.urlopen(req, timeout=12.0) as resp:
                    data = json.loads(resp.read().decode())
                    dt = int((time.time() - t0) * 1000)
                    results["dahl"] = {"status": "ok", "latency_ms": dt, "model": d_model, "msg": "Dahl Global Connected"}
            except urllib.error.HTTPError as e:
                dt = int((time.time() - t0) * 1000)
                err_msg = f"HTTP {e.code}"
                try:
                    raw = e.read().decode("utf-8", errors="ignore")
                    ebody = json.loads(raw)
                    msg = ebody.get("error", {}).get("message", "")
                    if "concurrency capacity" in msg:
                        err_msg = "Capacity busy. Select other Dahl model."
                    elif msg:
                        err_msg = msg[:60]
                except Exception:
                    pass
                results["dahl"] = {"status": "error", "latency_ms": dt, "error": err_msg}
            except Exception as e:
                dt = int((time.time() - t0) * 1000)
                results["dahl"] = {"status": "error", "latency_ms": dt, "error": str(e)}

        return results

    def classify_song_genre(self, artist, title):
        """Ultra-fast (sub-0.5s) LLM classification of musical genre using NVIDIA NIM."""
        if not artist and not title:
            return "Pop"
        cache_key = f"{artist} - {title}".strip().lower()
        if not hasattr(self, "_genre_cache"):
            self._genre_cache = {}
        if cache_key in self._genre_cache:
            return self._genre_cache[cache_key]

        low_comb = f"{artist} {title}".lower()
        # Fast local classifier for Aleemrk (Pakistani Urdu Rapper)
        if any(a in low_comb for a in ["aleemrk", "aleem rk", "aleem r.k."]):
            if any(w in low_comb for w in [
                "cold hours", "surface", "out of love", "distance", "lost", "maine",
                "rehnuma", "kyun", "humdum", "nadaan", "overthink", "late night",
                "2 am", "fall", "guzaarish", "beqabu", "breakup", "sad"
            ]):
                self._genre_cache[cache_key] = "Ballad"
                return "Ballad"
            self._genre_cache[cache_key] = "Hip-Hop/Rap"
            return "Hip-Hop/Rap"

        prompt = (
            f"Classify music genre of: \"{title}\" by \"{artist}\".\n"
            "Return ONLY JSON: {\"genre\": \"Hip-Hop/Rap\"|\"Rock\"|\"EDM\"|\"Lofi\"|\"Ballad\"|\"Pop\"}\n"
            "Rules:\n"
            "- Sad, breakup, heartbreak, crying, emo rap (e.g. Aleemrk Cold Hours/Maine/Surface, Juice WRLD, MC Stan Numb/Regret, Talha Anjum Downers at Dusk), emotional slow ballad -> \"Ballad\"\n"
            "- Pakistani/Desi hip-hop, Urdu rap, drill, trap, 808 (e.g. Aleemrk Advice/Cant Lose, Talha Anjum, MC Stan, KR$NA) -> \"Hip-Hop/Rap\"\n"
            "- House, electro, dance, techno, edm -> \"EDM\"\n"
            "- Rock, metal, heavy guitar, punk -> \"Rock\"\n"
            "- Lofi, chillhop, ambient, study beats -> \"Lofi\"\n"
            "- Commercial upbeat -> \"Pop\""
        )
        try:
            url = f"{self.cfg.get('nvidia_base_url', 'https://integrate.api.nvidia.com/v1')}/chat/completions"
            key = self.cfg.get("nvidia_api_key", "")
            payload = {
                "model": self.cfg.get("primary_reflex_model", "nvidia/nemotron-3.5-bolt-e4b"),
                "messages": [{"role": "user", "content": prompt}],
                "max_tokens": 30,
                "temperature": 0.1
            }
            resp = self.session.post(
                url,
                headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"},
                json=payload,
                timeout=3.5
            )
            if resp.status_code == 200:
                data = resp.json()
                content = data["choices"][0]["message"].get("content", "")
                parsed = self._extract_json(content)
                if parsed and parsed.get("genre"):
                    g = parsed["genre"]
                    self._genre_cache[cache_key] = g
                    return g
        except Exception:
            pass
        return "Pop"

    def inspect_music_vibe(self, title, artist, genre, bpm, energy):
        if not title or title == self.last_music_title:
            return
        self.last_music_title = title
        # Never display popup on pet screen for music or genre
        print(f"[AI CORTEX] Music Playing: '{title}' by '{artist}' ({genre}, {bpm} BPM, Energy: {energy:.2f})")

    # =====================================================================
    # Local HTTP API (Port 8765)
    # =====================================================================
    def start_http_api(self, port=8765):
        cortex = self

        class CortexHandler(BaseHTTPRequestHandler):
            def do_OPTIONS(self):
                self.send_response(200)
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS")
                self.send_header("Access-Control-Allow-Headers", "Content-Type")
                self.end_headers()

            def do_GET(self):
                if self.path == "/api/ai/status":
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Access-Control-Allow-Origin", "*")
                    self.end_headers()
                    out = {
                        "status": "online",
                        "warm": cortex.is_warm,
                        "device_ip": cortex.esp_ip,
                        "idle_seconds": int(get_user_idle_seconds()),
                        "user_away": cortex.user_is_away
                    }
                    self.wfile.write(json.dumps(out).encode())
                elif self.path == "/api/ai/config":
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Access-Control-Allow-Origin", "*")
                    self.end_headers()
                    out = {
                        "nvidia_api_key": cortex.cfg.get("nvidia_api_key", ""),
                        "nvidia_base_url": cortex.cfg.get("nvidia_base_url", "https://integrate.api.nvidia.com/v1"),
                        "nvidia_model": cortex.cfg.get("primary_reflex_model", "nvidia/nemotron-3.5-bolt-e4b"),
                        "dahl_api_key": cortex.cfg.get("dahl_api_key", ""),
                        "dahl_base_url": cortex.cfg.get("dahl_base_url", "https://inference.dahl.global/v1"),
                        "dahl_model": cortex.cfg.get("dahl_flash_model", "zai-org/GLM-5.3-Flash"),
                        "voice_enabled": cortex.cfg.get("voice_enabled", True),
                        "autonomous_thoughts": cortex.cfg.get("autonomous_stream_of_consciousness", True),
                        "idle_sleep_seconds": cortex.cfg.get("idle_sleep_seconds", 300)
                    }
                    self.wfile.write(json.dumps(out).encode())
                else:
                    self.send_response(404)
                    self.end_headers()

            def do_POST(self):
                if self.path == "/api/ai/ask":
                    length = int(self.headers.get("Content-Length", 0))
                    body = self.rfile.read(length).decode("utf-8")
                    try:
                        req = json.loads(body)
                        prompt = req.get("prompt", "")
                        ctx = req.get("context", None)
                        ai_out = cortex.generate_response(prompt, ctx, prefer_deep_cognition=True)
                        cortex.actuate(ai_out)
                        self.send_response(200)
                        self.send_header("Content-Type", "application/json")
                        self.send_header("Access-Control-Allow-Origin", "*")
                        self.end_headers()
                        self.wfile.write(json.dumps(ai_out).encode())
                    except Exception as e:
                        self.send_response(500)
                        self.end_headers()
                        self.wfile.write(json.dumps({"error": str(e)}).encode())
                elif self.path == "/api/ai/config":
                    length = int(self.headers.get("Content-Length", 0))
                    body = self.rfile.read(length).decode("utf-8")
                    try:
                        data = json.loads(body)
                        if "nvidia_api_key" in data:
                            cortex.cfg["nvidia_api_key"] = data["nvidia_api_key"]
                            cortex.nim_key = data["nvidia_api_key"]
                        if "nvidia_model" in data:
                            cortex.cfg["primary_reflex_model"] = data["nvidia_model"]
                            cortex.primary_reflex_model = data["nvidia_model"]
                        if "nvidia_base_url" in data:
                            cortex.cfg["nvidia_base_url"] = data["nvidia_base_url"]
                            cortex.nim_base = data["nvidia_base_url"]
                        if "dahl_api_key" in data:
                            cortex.cfg["dahl_api_key"] = data["dahl_api_key"]
                            cortex.dahl_key = data["dahl_api_key"]
                        if "dahl_model" in data:
                            cortex.cfg["dahl_flash_model"] = data["dahl_model"]
                            cortex.dahl_flash_model = data["dahl_model"]
                        if "dahl_base_url" in data:
                            cortex.cfg["dahl_base_url"] = data["dahl_base_url"]
                            cortex.dahl_base = data["dahl_base_url"]
                        if "voice_enabled" in data:
                            cortex.cfg["voice_enabled"] = bool(data["voice_enabled"])
                        if "autonomous_thoughts" in data:
                            cortex.cfg["autonomous_stream_of_consciousness"] = bool(data["autonomous_thoughts"])

                        with open(CONFIG_PATH, "w", encoding="utf-8") as f:
                            json.dump(cortex.cfg, f, indent=2)

                        # Sync to ESP32 if online
                        if cortex.esp_ip:
                            try:
                                esp_payload = json.dumps({
                                    "nvidia_api_key": cortex.nim_key,
                                    "nvidia_model": cortex.primary_reflex_model,
                                    "dahl_api_key": cortex.dahl_key,
                                    "dahl_model": cortex.dahl_flash_model
                                }).encode("utf-8")
                                r = urllib.request.Request(
                                    f"http://{cortex.esp_ip}/api/ai/config",
                                    data=esp_payload,
                                    headers={"Content-Type": "application/json"}
                                )
                                with urllib.request.urlopen(r, timeout=1.5) as _:
                                    pass
                            except Exception:
                                pass

                        self.send_response(200)
                        self.send_header("Content-Type", "application/json")
                        self.send_header("Access-Control-Allow-Origin", "*")
                        self.end_headers()
                        self.wfile.write(json.dumps({"status": "ok", "msg": "Config saved successfully"}).encode())
                    except Exception as e:
                        self.send_response(500)
                        self.end_headers()
                        self.wfile.write(json.dumps({"error": str(e)}).encode())
                elif self.path == "/api/ai/test":
                    length = int(self.headers.get("Content-Length", 0))
                    body = self.rfile.read(length).decode("utf-8") if length > 0 else "{}"
                    try:
                        req = json.loads(body) if body else {}
                        provider = req.get("provider", "all")
                        res = cortex.test_connection(provider=provider, custom_keys=req)
                        self.send_response(200)
                        self.send_header("Content-Type", "application/json")
                        self.send_header("Access-Control-Allow-Origin", "*")
                        self.end_headers()
                        self.wfile.write(json.dumps(res).encode())
                    except Exception as e:
                        self.send_response(500)
                        self.end_headers()
                        self.wfile.write(json.dumps({"error": str(e)}).encode())
                else:
                    self.send_response(404)
                    self.end_headers()

            def log_message(self, format, *args):
                pass

        server = ThreadingHTTPServer(("127.0.0.1", port), CortexHandler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        print(f"[AI CORTEX] Local HTTP API active at http://127.0.0.1:{port}/api/ai/ask")

    def run(self):
        self.running = True
        print("[AI CORTEX] Starting Autonomous Cognitive Brain...")
        self.start_http_api(port=8765)
        threading.Thread(target=self._keep_alive_loop, daemon=True).start()

        print("[AI CORTEX] STAN Living Consciousness Online. Observing desk & presence...\n")
        try:
            while self.running:
                # 1. User Presence & Idle Check (every 3s)
                self.inspect_presence_and_idle()

                # 2. Autonomous Stream of Consciousness (every 3m)
                self.trigger_autonomous_thought()

                # 3. 3D Printer Observer (every 4s)
                try:
                    with urllib.request.urlopen(f"http://{self.esp_ip}/api/status", timeout=1.2) as resp:
                        if resp.status == 200:
                            data = json.loads(resp.read().decode())
                            self.inspect_print_telemetry(
                                data.get("print_state", "OFFLINE"),
                                data.get("percent", 0),
                                float(data.get("nozzle_temp", 0.0)),
                                float(data.get("bed_temp", 0.0)),
                                data.get("remain_min", -1),
                                data.get("layer", "-1 / -1")
                            )
                except Exception:
                    pass

                time.sleep(3.0)
        except KeyboardInterrupt:
            print("\n[AI CORTEX] Stopping Living Mind...")
        finally:
            self.running = False


if __name__ == "__main__":
    cortex = PetAICortex()
    if len(sys.argv) > 1 and sys.argv[1] == "--ask":
        query = " ".join(sys.argv[2:]) if len(sys.argv) > 2 else "Status report"
        out = cortex.generate_response(query, prefer_deep_cognition=True)
        cortex.actuate(out)
    else:
        cortex.run()
