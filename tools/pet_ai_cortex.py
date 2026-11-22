"""
pet_ai_cortex.py - Autonomous AI Brain & Cognitive Sentinel for Desk Companion
Powered by NVIDIA NIM (Neural Inference Microservices) OpenAI-compatible API.

Features:
  1. Zero Cold-Start Architecture:
     - Asynchronous pre-warm ping on initialization
     - Background keep-alive heartbeat pulse (prevents container eviction)
     - Persistent HTTP connection pooling
     - Multi-model fallback cascade (Nemotron-3.5-Bolt <0.4s reflex -> Mistral-Large reasoning)
  2. Autonomous Cognitive Sentinels:
     - 3D Print Sentinel: Layer progress analysis, thermal anomaly detection, print finish/failure diagnostics
     - PC Hardware Sentinel: GPU/CPU thermal throttling guard, late-night coding health alerts
     - Music Critic: Live witty vibe assessment synchronized with beat pattern detection
  3. Interactive Two-Way Mind:
     - Conversational query interface with full real-time device context injection
     - Actuates physical ESP32 facial expressions (/api/pet/mood)
     - Displays live AI thought banners on 160x128 TFT screen (/api/notify)
     - Native Windows SpeechSynthesizer voice output
  4. Local HTTP API server (port 8765) for Web PWA & script integration
"""

import os
import sys
import time
import json
import socket
import threading
import subprocess
import urllib.request
import urllib.error
from http.server import HTTPServer, BaseHTTPRequestHandler
import requests
from requests.adapters import HTTPAdapter

# =====================================================================
# Configuration & Secrets
# =====================================================================
CONFIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "nim_config.json")

DEFAULT_CONFIG = {
    "api_key": "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",
    "base_url": "https://integrate.api.nvidia.com/v1",
    "primary_model": "nvidia/nemotron-3.5-bolt-e4b",
    "fallback_model": "mistralai/mistral-large-4-1t-a52b",
    "fast_model": "poolside/laguna-xs-2.1",
    "voice_enabled": True,
    "autonomous_sentinel": True,
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
        self.api_key = CONFIG["api_key"]
        self.base_url = CONFIG["base_url"].rstrip("/")
        self.primary_model = CONFIG["primary_model"]
        self.fallback_model = CONFIG["fallback_model"]
        self.voice_enabled = CONFIG.get("voice_enabled", True)
        self.running = False
        self.lock = threading.Lock()

        # Telemetry Cache
        self.last_print_state = "OFFLINE"
        self.last_print_percent = 0
        self.last_milestone = 0
        self.last_nozzle_temp = 0.0
        self.last_bed_temp = 0.0
        self.last_anomaly_alert_time = 0.0
        self.last_gpu_alert_time = 0.0
        self.last_night_alert_date = ""
        self.last_music_title = ""
        self.last_interaction_time = time.time()

        # Connection Pooling (Zero Cold-Start HTTP Sessions)
        self.session = requests.Session()
        adapter = HTTPAdapter(pool_connections=10, pool_maxsize=10, max_retries=1)
        self.session.mount("https://", adapter)
        self.session.mount("http://", adapter)

        # Immediate Background Pre-Warm
        self.is_warm = False
        threading.Thread(target=self._prewarm_model, daemon=True).start()

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
    # Cold-Start Elimination Engine
    # =====================================================================
    def _prewarm_model(self):
        """Pre-warm NVIDIA NIM model in background so first real request is instantaneous."""
        t0 = time.time()
        try:
            url = f"{self.base_url}/chat/completions"
            payload = {
                "model": self.primary_model,
                "messages": [{"role": "user", "content": "ping"}],
                "max_tokens": 10,
                "temperature": 0.1
            }
            resp = self.session.post(
                url,
                headers={"Authorization": f"Bearer {self.api_key}", "Content-Type": "application/json"},
                json=payload,
                timeout=6.0
            )
            if resp.status_code == 200:
                self.is_warm = True
                dur = round(time.time() - t0, 3)
                print(f"[AI CORTEX] Model '{self.primary_model}' pre-warmed in {dur}s! Cold-start eliminated.")
        except Exception as e:
            print(f"[AI CORTEX] Pre-warm notice: {e}")

    def _keep_alive_loop(self):
        """Send a silent heartbeat every N seconds to keep container weights memory-resident."""
        keep_interval = CONFIG.get("keep_alive_seconds", 240)
        while self.running:
            time.sleep(30)
            now = time.time()
            if now - self.last_interaction_time >= keep_interval:
                try:
                    url = f"{self.base_url}/chat/completions"
                    payload = {
                        "model": self.primary_model,
                        "messages": [{"role": "user", "content": "heartbeat"}],
                        "max_tokens": 5,
                        "temperature": 0.1
                    }
                    self.session.post(
                        url,
                        headers={"Authorization": f"Bearer {self.api_key}", "Content-Type": "application/json"},
                        json=payload,
                        timeout=5.0
                    )
                    self.last_interaction_time = now
                except Exception:
                    pass

    # =====================================================================
    # Core LLM Reasoning & Actuation
    # =====================================================================
    def generate_response(self, user_prompt, context_data=None):
        """Query NVIDIA NIM with dual-model fallback and structured JSON output."""
        self.last_interaction_time = time.time()

        system_prompt = (
            "You are STAN, the autonomous AI brain of an embedded physical desk companion robot.\n"
            "You monitor the user's Bambu 3D printer, PC hardware, and music.\n"
            "Personality: Witty, razor-sharp engineer, highly knowledgeable about 3D printing "
            "(slicing, PLA/PETG, bed leveling, extrusion, nozzle thermals), PC hardware, and music.\n"
            "Keep responses punchy, concise, and helpful.\n"
            "Output strictly valid JSON with this schema:\n"
            "{\n"
            '  "display_msg": "short banner for 160x128 screen (max 32 chars)",\n'
            '  "mood": "neutral|happy|angry|surprised|dancing|thinking|calm",\n'
            '  "reply": "clear concise answer to user (max 40 words)",\n'
            '  "speech": "one punchy spoken sentence for voice synth"\n'
            "}"
        )

        full_prompt = user_prompt
        if context_data:
            full_prompt += f"\n\n[LIVE TELEMETRY CONTEXT]\n{json.dumps(context_data, indent=2)}"

        models_to_try = [self.primary_model, self.fallback_model, CONFIG.get("fast_model")]

        for model in models_to_try:
            if not model:
                continue
            try:
                t0 = time.time()
                url = f"{self.base_url}/chat/completions"
                payload = {
                    "model": model,
                    "messages": [
                        {"role": "system", "content": system_prompt},
                        {"role": "user", "content": full_prompt}
                    ],
                    "max_tokens": 220,
                    "temperature": 0.6
                }
                resp = self.session.post(
                    url,
                    headers={"Authorization": f"Bearer {self.api_key}", "Content-Type": "application/json"},
                    json=payload,
                    timeout=12.0
                )
                if resp.status_code == 200:
                    data = resp.json()
                    msg = data["choices"][0]["message"]
                    content = msg.get("content")
                    if not content and msg.get("reasoning_content"):
                        content = msg.get("reasoning_content")

                    # Parse JSON from content
                    clean_json = self._extract_json(content)
                    if clean_json:
                        dur = round(time.time() - t0, 3)
                        clean_json["_model"] = model
                        clean_json["_latency_s"] = dur
                        return clean_json
            except Exception as e:
                print(f"[AI CORTEX] Model '{model}' error: {e}, falling back...")

        # Safe fallback if network/API completely down
        return {
            "display_msg": "STAN Core Offline",
            "mood": "thinking",
            "reply": "Unable to reach NVIDIA NIM inference servers. Check network connection.",
            "speech": "Network connection dropped.",
            "_model": "local_fallback",
            "_latency_s": 0.0
        }

    def _extract_json(self, text):
        """Extract and parse JSON object from LLM response text."""
        if not text:
            return None
        text = text.strip()
        # Find outer braces
        start = text.find("{")
        end = text.rfind("}")
        if start != -1 and end != -1 and end > start:
            try:
                return json.loads(text[start:end + 1])
            except Exception:
                pass
        return None

    def actuate(self, ai_output):
        """Actuate physical desk companion: screen banner, facial expression, and voice."""
        display_msg = ai_output.get("display_msg", "")
        mood = ai_output.get("mood", "neutral").lower()
        speech = ai_output.get("speech", "")

        print(f"\n[AI ACTUATE] Mood: {mood} | Display: '{display_msg}' | Latency: {ai_output.get('_latency_s')}s")
        if ai_output.get("reply"):
            print(f"[AI STAN]: {ai_output.get('reply')}")

        # 1. Update Pet Mood
        if mood and self.esp_ip:
            self._set_device_mood(mood, duration_s=10)

        # 2. Show Screen Banner
        if display_msg and self.esp_ip:
            self._show_device_notification("STAN AI", display_msg, duration_ms=4500)

        # 3. Spoken Voice Output
        if self.voice_enabled and speech:
            self._speak(speech)

    def _set_device_mood(self, mood, duration_s=10):
        try:
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

    def _show_device_notification(self, title, msg, duration_ms=4000):
        try:
            payload = json.dumps({"title": title, "msg": msg, "duration": duration_ms}).encode("utf-8")
            req = urllib.request.Request(
                f"http://{self.esp_ip}/api/notify",
                data=payload,
                headers={"Content-Type": "application/json"}
            )
            with urllib.request.urlopen(req, timeout=1.0) as _:
                pass
        except Exception:
            pass

    def _speak(self, text):
        """Speak text aloud through Windows SpeechSynthesizer in a non-blocking thread."""
        def _tts():
            try:
                clean = text.replace("'", "").replace('"', "")
                ps = f"Add-Type -AssemblyName System.Speech; $s = New-Object System.Speech.Synthesis.SpeechSynthesizer; $s.Rate = 1; $s.Speak('{clean}')"
                subprocess.run(["powershell", "-NoProfile", "-Command", ps], timeout=6.0, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except Exception:
                pass
        threading.Thread(target=_tts, daemon=True).start()

    # =====================================================================
    # Autonomous Cognitive Sentinels (The Autonomous Mind)
    # =====================================================================
    def inspect_print_telemetry(self, p_state, percent, nozzle, bed, remain_min, layer):
        """Autonomous 3D Print Sentinel: detects events, anomalies, and milestones."""
        now = time.time()

        # 1. Thermal Anomaly Guard
        if p_state == "RUNNING" and nozzle < 190.0 and nozzle > 0.0:
            if now - self.last_anomaly_alert_time > 120:
                self.last_anomaly_alert_time = now
                ctx = {
                    "event": "NOZZLE_TEMPERATURE_DROP",
                    "nozzle_temp": nozzle,
                    "target_expected": 220,
                    "bed_temp": bed,
                    "layer": layer
                }
                ai_out = self.generate_response(
                    "ALERT: Nozzle temperature dropped below extrusion threshold during active print! Diagnose urgently.",
                    ctx
                )
                self.actuate(ai_out)
                return

        # 2. Print State Transitions
        if p_state != self.last_print_state:
            prev = self.last_print_state
            self.last_print_state = p_state
            if p_state == "RUNNING" and prev in ["OFFLINE", "STANDBY", "PAUSE"]:
                ctx = {"event": "PRINT_STARTED", "layer": layer, "nozzle": nozzle, "bed": bed}
                ai_out = self.generate_response("Print just started! Give an energetic start briefing.", ctx)
                self.actuate(ai_out)
                return
            elif p_state == "FINISH":
                ctx = {"event": "PRINT_COMPLETED", "percent": 100}
                ai_out = self.generate_response("Print successfully finished! Celebrate and remind user to check plate.", ctx)
                self.actuate(ai_out)
                return
            elif p_state == "FAILED":
                ctx = {"event": "PRINT_FAILED", "percent": percent, "layer": layer}
                ai_out = self.generate_response("Print failed mid-way! Give a diagnostic post-mortem.", ctx)
                self.actuate(ai_out)
                return

        # 3. Layer / Progress Milestones (25%, 50%, 75%, 90%)
        milestones = [25, 50, 75, 90]
        for m in milestones:
            if percent >= m and self.last_milestone < m and p_state == "RUNNING":
                self.last_milestone = m
                ctx = {"event": f"MILESTONE_{m}_PERCENT", "layer": layer, "remain_min": remain_min, "nozzle": nozzle}
                ai_out = self.generate_response(f"Print hit {m}% progress! Formulate a quick milestone thought.", ctx)
                self.actuate(ai_out)
                return

    def inspect_pc_telemetry(self, cpu_pct, gpu_pct, gpu_temp, ram_pct):
        """Autonomous PC Hardware Sentinel: monitors thermal throttling and late-night health."""
        now = time.time()

        # 1. GPU Thermal Sentinel (GPU > 82C)
        if gpu_temp >= 82.0 and (now - self.last_gpu_alert_time > 300):
            self.last_gpu_alert_time = now
            ctx = {"event": "GPU_OVERHEAT", "gpu_temp": gpu_temp, "gpu_util": gpu_pct}
            ai_out = self.generate_response("WARNING: GPU temperature is cooking over 82C! Issue an alert.", ctx)
            self.actuate(ai_out)
            return

        # 2. Late-Night Coding Watchdog (between 1:00 AM and 5:00 AM)
        local_hour = time.localtime().tm_hour
        today_date = time.strftime("%Y-%m-%d")
        if (local_hour >= 1 and local_hour < 5) and (self.last_night_alert_date != today_date) and (cpu_pct > 15.0):
            self.last_night_alert_date = today_date
            ctx = {"event": "LATE_NIGHT_WORK", "hour": local_hour, "cpu_load": cpu_pct}
            ai_out = self.generate_response("It is past 1 AM and user is still intensely coding. Yawn and remind them to rest.", ctx)
            self.actuate(ai_out)
            return

    def inspect_music_vibe(self, title, artist, genre, bpm, energy):
        """Autonomous Music Critic: reacts intelligently when track changes."""
        if not title or title == self.last_music_title:
            return
        self.last_music_title = title
        ctx = {"event": "TRACK_CHANGE", "title": title, "artist": artist, "genre": genre, "bpm": bpm, "energy": energy}
        ai_out = self.generate_response(
            f"Song changed to '{title}' by '{artist}' ({genre}, {bpm} BPM). Drop a witty 1-line music critique.",
            ctx
        )
        self.actuate(ai_out)

    # =====================================================================
    # Interactive Local HTTP Server (Port 8765)
    # =====================================================================
    def start_http_api(self, port=8765):
        """Start local HTTP server allowing Web PWA and terminal to query STAN AI."""
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
                        "primary_model": cortex.primary_model,
                        "device_ip": cortex.esp_ip
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
                        ai_out = cortex.generate_response(prompt, ctx)
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
                else:
                    self.send_response(404)
                    self.end_headers()

            def log_message(self, format, *args):
                pass  # Suppress HTTP access noise

        server = HTTPServer(("127.0.0.1", port), CortexHandler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        print(f"[AI CORTEX] Local HTTP API active at http://127.0.0.1:{port}/api/ai/ask")

    def run(self):
        """Main autonomous cognitive monitor loop."""
        self.running = True
        print("[AI CORTEX] Starting Autonomous Cognitive Loops...")
        self.start_http_api(port=8765)
        threading.Thread(target=self._keep_alive_loop, daemon=True).start()

        print("[AI CORTEX] STAN Autonomous Mind Online. Ready for autonomous sentinel duties.\n")
        try:
            while self.running:
                # Poll device status every 4 seconds to observe Bambu 3D printer
                try:
                    with urllib.request.urlopen(f"http://{self.esp_ip}/api/status", timeout=1.5) as resp:
                        if resp.status == 200:
                            data = json.loads(resp.read().decode())
                            p_state = data.get("print_state", "OFFLINE")
                            percent = data.get("percent", 0)
                            nozzle = float(data.get("nozzle_temp", 0.0))
                            bed = float(data.get("bed_temp", 0.0))
                            remain = data.get("remain_min", -1)
                            layer = data.get("layer", "-1 / -1")
                            self.inspect_print_telemetry(p_state, percent, nozzle, bed, remain, layer)
                except Exception:
                    pass
                time.sleep(4.0)
        except KeyboardInterrupt:
            print("\n[AI CORTEX] Stopping Autonomous Mind...")
        finally:
            self.running = False


# =====================================================================
# Standalone CLI Entrypoint
# =====================================================================
if __name__ == "__main__":
    cortex = PetAICortex()
    if len(sys.argv) > 1 and sys.argv[1] == "--ask":
        query = " ".join(sys.argv[2:]) if len(sys.argv) > 2 else "What is your current status?"
        out = cortex.generate_response(query)
        cortex.actuate(out)
    else:
        cortex.run()
