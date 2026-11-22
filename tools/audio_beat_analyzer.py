"""
audio_beat_analyzer.py - Real-Time Audio Beat Pattern & Music Genre Analyzer
Captures Windows desktop loopback audio (WASAPI) and performs real-time:
  1. RMS energy and silence detection
  2. Multi-band spectral decomposition (Sub-bass, Bass, Low-mid, High-mid, Treble)
  3. Spectral flux onset detection for kick drums and transients
  4. Autocorrelation-based Tempo (BPM) estimation (50 - 190 BPM)
  5. Beat pattern classification:
       - EDM / Party / Dance (Style 2, 4-on-the-floor, 120-135 BPM)
       - Hip-Hop / Drill / Trap (Style 1, heavy 808 sub-bass, 65-88 BPM half-time)
       - Rock / Metal (Style 3, guitar distortion, driving rhythm, 110-180 BPM)
       - Lo-Fi / Chill (Style 4, warm low-mids, soft transients, 70-95 BPM)
       - Ballad / Emotional (Style 5, slow, sparse acoustic onsets, <68 BPM)
       - Pop / Upbeat Groove (Style 0, balanced rhythm, 95-125 BPM)
  6. Live streaming to ESP32 Desk Pet (/api/spotify/now and /api/pc/stats)
"""

import os
import sys
import time
import math
import json
import socket
import threading
import subprocess
import warnings
import urllib.request
import urllib.error

# Suppress audio driver discontinuity warnings
warnings.filterwarnings("ignore")

import numpy as np
from scipy import signal

try:
    import soundcard as sc
except ImportError:
    print("[ERROR] 'soundcard' package not installed. Run: pip install soundcard")
    sys.exit(1)


# =====================================================================
# Configuration Constants
# =====================================================================
SAMPLE_RATE = 44100
BLOCK_SIZE = 2048       # ~46.4 ms per analysis frame
HOP_SIZE = 1024         # ~23.2 ms hop step
HISTORY_SECONDS = 4.0   # 4-second rolling window for autocorrelation
SILENCE_RMS_THRESH = 0.0035  # Below this is considered silence / idle
SILENCE_HOLD_SEC = 1.8       # Require silence for 1.8s before going idle


class BeatPatternAnalyzer:
    def __init__(self, esp_ip=None):
        self.esp_ip = esp_ip
        self.running = False
        self.lock = threading.Lock()

        # Audio stream state
        self.speaker = None
        self.mic = None

        # Rolling history buffers
        self.history_len = int(HISTORY_SECONDS * SAMPLE_RATE / HOP_SIZE)
        self.rms_history = np.zeros(self.history_len, dtype=np.float32)
        self.bass_flux_history = np.zeros(self.history_len, dtype=np.float32)
        self.high_flux_history = np.zeros(self.history_len, dtype=np.float32)
        self.timestamps = np.zeros(self.history_len, dtype=np.float64)

        # Spectral state
        self.last_band_energies = np.zeros(5, dtype=np.float32)

        # Analysis outputs (thread-safe reads)
        self.current_rms = 0.0
        self.current_energy = 0.0     # 0.0 .. 1.0 normalized
        self.is_playing = False
        self.is_beat = False
        self.detected_bpm = 0.0
        self.detected_genre = "Pop"
        self.vibe_style = 0           # 0=pop, 1=hiphop, 2=edm, 3=rock, 4=lofi, 5=ballad
        self.vibe_name = "Pop / Groove"
        self.sub_bass_ratio = 0.0
        self.distortion_factor = 0.0
        self.last_active_time = time.time()

        # Genre stability voting (last N classifications)
        self.genre_votes = []
        self.max_votes = 10

        # Frequency bin indices for 2048 FFT at 44100 Hz (~21.53 Hz per bin)
        freq_per_bin = SAMPLE_RATE / BLOCK_SIZE
        self.bin_sub_bass = (int(30 / freq_per_bin), int(80 / freq_per_bin))      # 30-80 Hz
        self.bin_bass = (int(80 / freq_per_bin), int(200 / freq_per_bin))          # 80-200 Hz
        self.bin_low_mid = (int(200 / freq_per_bin), int(800 / freq_per_bin))      # 200-800 Hz
        self.bin_high_mid = (int(800 / freq_per_bin), int(3500 / freq_per_bin))    # 800-3500 Hz
        self.bin_highs = (int(3500 / freq_per_bin), int(12000 / freq_per_bin))     # 3500-12000 Hz

        # Auto-discover ESP32 if not provided
        if not self.esp_ip:
            self.esp_ip = self.discover_esp_ip()

    def discover_esp_ip(self):
        """Find the ESP32 IP via ARP cache and quick probing."""
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
                req = urllib.request.Request(f"http://{ip}/api/status", headers={"User-Agent": "BeatAnalyzer"})
                with urllib.request.urlopen(req, timeout=1.2) as resp:
                    if resp.status == 200:
                        return ip
            except Exception:
                continue
        return "192.168.137.35"  # default fallback

    def init_audio(self):
        """Initialize WASAPI loopback capture on the default Windows playback device."""
        try:
            self.speaker = sc.default_speaker()
            self.mic = sc.get_microphone(id=self.speaker.id, include_loopback=True)
            print(f"[AUDIO] Initialized loopback recording from: {self.speaker.name}")
            return True
        except Exception as e:
            print(f"[AUDIO ERROR] Failed to initialize loopback audio: {e}")
            return False

    def process_frame(self, mono_samples):
        """Process a block of audio samples and update feature buffers."""
        now = time.time()
        n = len(mono_samples)
        if n == 0:
            return

        # 1. Compute RMS energy
        rms = float(np.sqrt(np.mean(mono_samples**2)))

        # 2. Compute Real FFT with Hanning window
        windowed = mono_samples * np.hanning(n)
        fft_data = np.abs(np.fft.rfft(windowed))

        # 3. Extract 5 frequency band energies
        e_sub_bass = float(np.sum(fft_data[self.bin_sub_bass[0]:self.bin_sub_bass[1]]))
        e_bass = float(np.sum(fft_data[self.bin_bass[0]:self.bin_bass[1]]))
        e_low_mid = float(np.sum(fft_data[self.bin_low_mid[0]:self.bin_low_mid[1]]))
        e_high_mid = float(np.sum(fft_data[self.bin_high_mid[0]:self.bin_high_mid[1]]))
        e_highs = float(np.sum(fft_data[self.bin_highs[0]:self.bin_highs[1]]))
        current_bands = np.array([e_sub_bass, e_bass, e_low_mid, e_high_mid, e_highs], dtype=np.float32)

        # 4. Compute Spectral Flux (onset strength)
        bass_band_total = e_sub_bass + e_bass
        last_bass_total = self.last_band_energies[0] + self.last_band_energies[1]
        bass_flux = max(0.0, float(bass_band_total - last_bass_total))

        high_flux = max(0.0, float(e_highs - self.last_band_energies[4]))
        self.last_band_energies = current_bands

        # 5. Shift rolling history
        self.rms_history[:-1] = self.rms_history[1:]
        self.rms_history[-1] = rms

        self.bass_flux_history[:-1] = self.bass_flux_history[1:]
        self.bass_flux_history[-1] = bass_flux

        self.high_flux_history[:-1] = self.high_flux_history[1:]
        self.high_flux_history[-1] = high_flux

        self.timestamps[:-1] = self.timestamps[1:]
        self.timestamps[-1] = now

        # 6. Silence & Activity tracking
        if rms >= SILENCE_RMS_THRESH:
            self.last_active_time = now
            is_active = True
        else:
            is_active = (now - self.last_active_time < SILENCE_HOLD_SEC)

        # 7. Beat Onset Detection
        # Adaptive threshold based on moving mean + 1.2 * std of bass flux
        mean_flux = np.mean(self.bass_flux_history)
        std_flux = np.std(self.bass_flux_history)
        onset_thresh = mean_flux + 1.25 * std_flux
        is_beat = (bass_flux > onset_thresh) and (bass_flux > 0.05) and is_active

        # 8. Normalized energy metric (0.0 .. 1.0)
        # Logarithmic scaling for human ear perception
        norm_energy = float(np.clip((math.log10(max(1e-5, rms)) + 3.0) / 2.2, 0.0, 1.0)) if is_active else 0.0

        # Sub-bass ratio: ratio of sub-bass (<80Hz) to low-mids (200-800Hz)
        sub_ratio = float(e_sub_bass / (e_low_mid + 1e-4))

        # Distortion / sustained high-mid presence factor
        dist_factor = float(e_high_mid / (e_sub_bass + e_bass + 1e-4))

        with self.lock:
            self.current_rms = rms
            self.current_energy = norm_energy
            self.is_playing = is_active
            self.is_beat = is_beat
            self.sub_bass_ratio = sub_ratio
            self.distortion_factor = dist_factor

    def estimate_tempo_and_genre(self):
        """Analyze the 4-second onset buffer using autocorrelation to find BPM and classify beat pattern."""
        if not self.is_playing:
            with self.lock:
                self.detected_bpm = 0.0
                self.detected_genre = "Pop"
                self.vibe_style = 0
                self.vibe_name = "Idle (Silent)"
            return

        # Prepare onset signal (normalize zero-mean)
        signal_data = self.bass_flux_history - np.mean(self.bass_flux_history)
        std_val = np.std(signal_data)
        if std_val < 1e-5:
            return
        signal_data /= std_val

        # Autocorrelation via FFT
        n = len(signal_data)
        autocorr = signal.correlate(signal_data, signal_data, mode="full")[n - 1:]

        # Frame rate of the hop buffer: ~44100 / 1024 ~= 43.066 fps
        fps = SAMPLE_RATE / HOP_SIZE

        # Musical BPM search range: 55 BPM to 185 BPM
        # Corresponding to lag range: (60 / 185) * fps to (60 / 55) * fps
        min_lag = int((60.0 / 185.0) * fps)  # ~14 frames
        max_lag = int((60.0 / 55.0) * fps)   # ~47 frames

        if max_lag >= len(autocorr):
            max_lag = len(autocorr) - 1

        search_slice = autocorr[min_lag:max_lag]
        if len(search_slice) == 0:
            return

        # Find prominent peaks in autocorrelation
        peak_idx = int(np.argmax(search_slice)) + min_lag
        peak_corr = autocorr[peak_idx]

        lag_seconds = peak_idx / fps
        primary_bpm = 60.0 / lag_seconds if lag_seconds > 0 else 120.0

        # Detect half-time / double-time equivalence
        half_time_bpm = primary_bpm / 2.0 if primary_bpm > 130 else primary_bpm

        # Regularity / rhythm consistency (peak correlation strength)
        regularity = float(peak_corr / (autocorr[0] + 1e-6))

        # Classify Beat Pattern and Music Genre based on DSP features:
        # 1. EDM / Party / Dance:
        #    - Strict 4-on-the-floor kick pattern
        #    - 120 - 138 BPM (or 145-160 BPM)
        #    - High rhythm regularity (> 0.28)
        #    - Solid energy (> 0.40)
        is_edm = (118 <= primary_bpm <= 138 or 145 <= primary_bpm <= 165) and (regularity > 0.22) and (self.current_energy > 0.35)

        # 2. Hip-Hop / Drill / Trap:
        #    - Heavy 808 sub-bass drops (sub_bass_ratio > 1.45)
        #    - Half-time feel (65 - 88 BPM or 130 - 176 BPM)
        #    - Syncopated kick spacing
        is_hiphop = (self.sub_bass_ratio > 1.35) and ((64 <= primary_bpm <= 92) or (128 <= primary_bpm <= 180))

        # 3. Rock / Metal / Punk:
        #    - Wall-of-sound guitar distortion / high mids (distortion_factor > 1.8)
        #    - Driving eighth-note tempo (110 - 180 BPM)
        #    - High sustained energy
        is_rock = (self.distortion_factor > 1.65) and (self.current_energy > 0.45) and (105 <= primary_bpm <= 180)

        # 4. Lo-Fi / Chill:
        #    - Mellow energy (0.05 < energy < 0.45)
        #    - Muted high frequencies, gentle tempo (68 - 95 BPM)
        is_lofi = (self.current_energy < 0.45) and (66 <= primary_bpm <= 98) and (self.distortion_factor < 1.1)

        # 5. Emotional / Ballad:
        #    - Slow tempo (< 68 BPM)
        #    - Low regularity or sparse kicks
        is_ballad = (primary_bpm < 68) or (regularity < 0.12 and self.current_energy < 0.35)

        # Decision hierarchy:
        if is_edm:
            vote = ("EDM", 2, "Party / EDM (Hype Bounce)", primary_bpm)
        elif is_hiphop:
            # Display the half-time groove BPM if in 130-170 range
            bpm_display = half_time_bpm if primary_bpm > 115 else primary_bpm
            vote = ("Hip-Hop/Rap", 1, "Hip-Hop / Drill (Swagger 808 Nod)", bpm_display)
        elif is_rock:
            vote = ("Rock", 3, "Rock / Metal (Hard Headbang)", primary_bpm)
        elif is_lofi:
            vote = ("Lofi", 4, "Lo-Fi / Chill (Gentle Sway)", primary_bpm)
        elif is_ballad:
            vote = ("Ballad", 5, "Emotional Ballad (Gentle Sway)", primary_bpm)
        else:
            vote = ("Pop", 0, "Upbeat Pop (Bouncy Groove)", primary_bpm)

        # Vote smoothing (last N votes)
        self.genre_votes.append(vote)
        if len(self.genre_votes) > self.max_votes:
            self.genre_votes.pop(0)

        # Majority vote on genre
        from collections import Counter
        genres = [v[0] for v in self.genre_votes]
        top_genre = Counter(genres).most_common(1)[0][0]

        # Find matching details for the winning genre
        for v in reversed(self.genre_votes):
            if v[0] == top_genre:
                with self.lock:
                    self.detected_genre = v[0]
                    self.vibe_style = v[1]
                    self.vibe_name = v[2]
                    self.detected_bpm = round(v[3], 1)
                break

    def send_to_esp32(self):
        """Push real-time beat pattern analysis directly to ESP32."""
        if not self.esp_ip:
            return

        with self.lock:
            genre = self.detected_genre
            vibe_style = self.vibe_style
            energy = self.current_energy
            bpm = self.detected_bpm
            is_playing = self.is_playing

        # 1. Update /api/pc/stats with live music energy, bpm, and genre
        try:
            stats_payload = json.dumps({
                "music_genre": genre,
                "music_vibe": round(energy, 2),
                "music_bpm": int(bpm)
            }).encode("utf-8")
            req = urllib.request.Request(
                f"http://{self.esp_ip}/api/pc/stats",
                data=stats_payload,
                headers={"Content-Type": "application/json", "User-Agent": "BeatAnalyzer"}
            )
            with urllib.request.urlopen(req, timeout=1.0) as resp:
                pass
        except Exception:
            pass

        # 2. Update /api/spotify/now to ensure mood engine immediately locks onto the genre
        try:
            now_payload = json.dumps({
                "genre": genre,
                "is_playing": is_playing,
                "energy": round(energy, 2)
            }).encode("utf-8")
            req2 = urllib.request.Request(
                f"http://{self.esp_ip}/api/spotify/now",
                data=now_payload,
                headers={"Content-Type": "application/json", "User-Agent": "BeatAnalyzer"}
            )
            with urllib.request.urlopen(req2, timeout=1.0) as resp2:
                pass
        except Exception:
            pass

    def run(self, print_dashboard=True):
        """Main real-time audio analysis loop."""
        if not self.init_audio():
            return

        self.running = True
        print(f"[ANALYZER] Running real-time beat analysis (Target ESP32: {self.esp_ip})...")
        print("[ANALYZER] Press Ctrl+C to stop.\n")

        last_tempo_calc = time.time()
        last_esp_sync = time.time()

        try:
            with self.mic.recorder(samplerate=SAMPLE_RATE, blocksize=BLOCK_SIZE) as recorder:
                while self.running:
                    # Capture audio block
                    data = recorder.record(numframes=BLOCK_SIZE)
                    if len(data) == 0:
                        continue

                    # Stereo to mono
                    mono = np.mean(data, axis=1)

                    # Process audio block
                    self.process_frame(mono)

                    now = time.time()

                    # Periodic tempo & genre classification (every 250ms)
                    if now - last_tempo_calc >= 0.25:
                        self.estimate_tempo_and_genre()
                        last_tempo_calc = now

                    # Periodic ESP32 sync (every 500ms)
                    if now - last_esp_sync >= 0.5:
                        self.send_to_esp32()
                        last_esp_sync = now

                    # Visual Terminal Dashboard
                    if print_dashboard:
                        self.render_dashboard()

        except KeyboardInterrupt:
            print("\n[ANALYZER] Stopping beat analysis engine...")
        finally:
            self.running = False

    def render_dashboard(self):
        """Render a live ASCII dashboard in terminal."""
        with self.lock:
            rms = self.current_rms
            energy = self.current_energy
            is_playing = self.is_playing
            is_beat = self.is_beat
            genre = self.detected_genre
            vibe_style = self.vibe_style
            vibe_name = self.vibe_name
            bpm = self.detected_bpm
            sub_ratio = self.sub_bass_ratio

        # RMS meter bar (20 chars)
        bar_len = int(energy * 20)
        bar_str = "[" + "=" * bar_len + " " * (20 - bar_len) + "]"

        # Beat indicator
        beat_str = "[ * BASS KICK * ]" if is_beat else "[               ]"

        status_str = f"\r{bar_str} Energy: {energy:.2f} | {beat_str} | BPM: {bpm:5.1f} | Genre: {genre:<11} | Vibe: {vibe_name:<28}"
        sys.stdout.write(status_str)
        sys.stdout.flush()


# =====================================================================
# Standalone CLI Entrypoint
# =====================================================================
if __name__ == "__main__":
    target_ip = sys.argv[1] if len(sys.argv) > 1 else None
    analyzer = BeatPatternAnalyzer(esp_ip=target_ip)
    analyzer.run(print_dashboard=True)
