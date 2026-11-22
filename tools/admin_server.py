#!/usr/bin/env python3
"""
tools/admin_server.py
Standalone Localhost Admin Web Server & Reverse Proxy for STΔN studio Desk Station.

1. Automatically discovers active ESP32 Desk Monitor IP on the local network.
2. Serves the web dashboard from web/ at http://localhost:3000/.
3. Provides /esp/* reverse proxy with full CORS and Private Network Access headers.
4. Auto-opens the default browser directly to the dashboard.
"""

import sys
import os
import json
import time
import socket
import urllib.request
import urllib.parse
import webbrowser
from http.server import HTTPServer, SimpleHTTPRequestHandler
from socketserver import ThreadingMixIn

CANDIDATE_IPS = ["192.168.137.35", "192.168.0.154", "192.168.1.35", "192.168.0.35", "192.168.1.154"]
WEB_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "web")

g_target_ip = "192.168.137.35"

def probe_esp_ip():
    global g_target_ip
    for ip in CANDIDATE_IPS:
        try:
            req = urllib.request.Request(f"http://{ip}/api/status", headers={"User-Agent": "AdminProbe/1.0"})
            with urllib.request.urlopen(req, timeout=1.2) as resp:
                if resp.status == 200:
                    g_target_ip = ip
                    return ip
        except Exception:
            pass
    return g_target_ip

class ThreadedHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True

class AdminRequestHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WEB_DIR, **kwargs)

    def log_message(self, format, *args):
        # Keep console output minimal and clean
        if "/api/control/screen_rle" in args[0] or "/api/status" in args[0]:
            return
        super().log_message(format, *args)

    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS, PUT, DELETE")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.send_header("Access-Control-Allow-Private-Network", "true")
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        clean_path = self.path.split("?")[0]

        if clean_path in ["/api/device/ip", "/api/ip"]:
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"device_ip": g_target_ip, "status": "online"}).encode())
            return

        # Reverse proxy: /esp/... -> http://{g_target_ip}/...
        if self.path.startswith("/esp/"):
            subpath = self.path[5:]
            target_url = f"http://{g_target_ip}/{subpath}"
            try:
                req = urllib.request.Request(target_url, headers={"User-Agent": "AdminProxy/1.0"})
                with urllib.request.urlopen(req, timeout=3.5) as resp:
                    data = resp.read()
                    self.send_response(resp.status)
                    for k, v in resp.headers.items():
                        if k.lower() not in ["content-length", "transfer-encoding", "connection"]:
                            self.send_header(k, v)
                    self.send_header("Content-Length", str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)
                    return
            except Exception as e:
                self.send_response(502)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"error": f"ESP32 unreachable at {g_target_ip}: {e}"}).encode())
                return

        return super().do_GET()

    def do_POST(self):
        # Reverse proxy for POST actions
        if self.path.startswith("/esp/"):
            subpath = self.path[5:]
            target_url = f"http://{g_target_ip}/{subpath}"
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length) if length > 0 else b""
            try:
                req = urllib.request.Request(target_url, data=body, method="POST")
                for k, v in self.headers.items():
                    if k.lower() in ["content-type", "accept", "user-agent"]:
                        req.add_header(k, v)
                with urllib.request.urlopen(req, timeout=4.5) as resp:
                    resp_data = resp.read()
                    self.send_response(resp.status)
                    for k, v in resp.headers.items():
                        if k.lower() not in ["content-length", "transfer-encoding", "connection"]:
                            self.send_header(k, v)
                    self.send_header("Content-Length", str(len(resp_data)))
                    self.end_headers()
                    self.wfile.write(resp_data)
                    return
            except Exception as e:
                self.send_response(502)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"error": f"ESP32 POST failed at {g_target_ip}: {e}"}).encode())
                return

        self.send_response(404)
        self.end_headers()

def main():
    print("=" * 65)
    print(" STΔN studio. Local Admin Web Server & Reverse Proxy")
    print("=" * 65)

    print(" Probing for ESP32 Desk Monitor...")
    esp_ip = probe_esp_ip()
    print(f" Target Device: http://{esp_ip}/")

    port = 3000
    server = None
    for p in range(3000, 3010):
        try:
            server = ThreadedHTTPServer(("0.0.0.0", p), AdminRequestHandler)
            port = p
            break
        except OSError:
            continue

    if not server:
        print(" Error: Unable to bind to any port between 3000 and 3010.")
        sys.exit(1)

    url = f"http://localhost:{port}/?ip={esp_ip}"
    print(f" Admin Panel live at: {url}")
    print(f" Direct Device URL:   http://{esp_ip}/")
    print(" Opening dashboard in default browser...")
    print(" Press Ctrl+C to stop.")
    print("=" * 65)

    try:
        webbrowser.open(url)
    except Exception:
        pass

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n Shutting down admin server.")
        server.server_close()

if __name__ == "__main__":
    main()
