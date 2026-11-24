#!/usr/bin/env python3
"""
tools/admin_server.py
Standalone Localhost Admin Web Server & Reverse Proxy for STΔN studio Desk Station.

1. Automatically discovers active ESP32 Desk Monitor IP on the local network (or accepts CLI override).
2. Serves the web dashboard from web/ at http://localhost:3000/.
3. Provides /esp/* reverse proxy with full CORS and Private Network Access headers.
4. Auto-opens the default browser directly to the dashboard (can be disabled via --no-browser).
"""

import sys
import os
import json
import time
import socket
import argparse
import urllib.request
import urllib.parse
import webbrowser
from http.server import HTTPServer, SimpleHTTPRequestHandler
from socketserver import ThreadingMixIn

DEFAULT_CANDIDATE_IPS = [
    "192.168.137.140",
    "192.168.137.35",
    "192.168.0.154",
    "192.168.1.35",
    "192.168.0.35",
    "192.168.1.154"
]
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB_DIR = os.environ.get("STAN_WEB_DIR", os.path.join(REPO_ROOT, "web"))

g_target_ip = "192.168.137.140"
g_printer_ip = "192.168.137.171"


def find_devices_from_arp():
    """Parses local ARP cache to find ESP32 and Bambu Lab printer MAC addresses."""
    esp_ip = None
    printer_ip = None
    try:
        import subprocess
        out = subprocess.check_output("arp -a", shell=True).decode("latin1", errors="ignore")
        for line in out.splitlines():
            parts = line.strip().split()
            if len(parts) >= 2:
                ip = parts[0]
                mac = parts[1].lower().replace("-", ":")
                if mac.startswith("b0:cb:d8") or mac == "b0:cb:d8:0a:a9:54":
                    esp_ip = ip
                elif mac.startswith("80:45:6b") or mac == "80:45:6b:33:57:14":
                    printer_ip = ip
    except Exception:
        pass
    return esp_ip, printer_ip


def sync_printer_ip_to_esp(esp_ip, printer_ip):
    """Pushes discovered printer IP to ESP32 /api/printer/ip."""
    if not esp_ip or not printer_ip:
        return False
    try:
        url = f"http://{esp_ip}/api/printer/ip"
        payload = json.dumps({"ip": printer_ip}).encode("utf-8")
        req = urllib.request.Request(url, data=payload, headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=1.5) as resp:
            return resp.status == 200
    except Exception:
        return False


def probe_esp_ip(candidates=None):
    """Probes candidate IPs and ARP table to discover an active ESP32 desk station."""
    global g_target_ip, g_printer_ip
    arp_esp, arp_printer = find_devices_from_arp()
    if arp_printer:
        g_printer_ip = arp_printer
        print(f" Discovered Bambu Printer via ARP: {g_printer_ip}")

    probe_list = []
    if arp_esp:
        probe_list.append(arp_esp)
    if candidates:
        probe_list.extend(candidates)
    else:
        for ip in DEFAULT_CANDIDATE_IPS:
            if ip not in probe_list:
                probe_list.append(ip)

    for ip in probe_list:
        try:
            req = urllib.request.Request(f"http://{ip}/api/status", headers={"User-Agent": "AdminProbe/1.0"})
            with urllib.request.urlopen(req, timeout=1.2) as resp:
                if resp.status == 200:
                    g_target_ip = ip
                    if g_printer_ip:
                        sync_printer_ip_to_esp(g_target_ip, g_printer_ip)
                    return ip
        except Exception:
            pass
    return g_target_ip


class ThreadedHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class AdminRequestHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WEB_DIR, **kwargs)

    def log_message(self, format, *args):
        # Keep console output minimal and clean
        if len(args) > 0 and ("/api/control/screen_rle" in args[0] or "/api/status" in args[0]):
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
            self.wfile.write(json.dumps({
                "device_ip": g_target_ip,
                "printer_ip": g_printer_ip,
                "status": "online"
            }).encode())
            return

        if clean_path in ["/api/discover", "/api/rescan"]:
            probe_esp_ip()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({
                "device_ip": g_target_ip,
                "printer_ip": g_printer_ip,
                "status": "discovered"
            }).encode())
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


def build_parser():
    """Builds the argument parser for the admin server."""
    parser = argparse.ArgumentParser(
        description="STΔN studio Local Admin Web Server & Reverse Proxy",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    parser.add_argument("--port", "-p", type=int, default=3000, help="Initial port to bind to (will scan up to +10)")
    parser.add_argument("--host", "-H", default="0.0.0.0", help="Host interface to bind to")
    parser.add_argument("--ip", type=str, default=None, help="Explicit ESP32 device IP address (bypasses auto-probe)")
    parser.add_argument("--candidates", type=str, default=None, help="Comma-separated candidate IPs to probe")
    parser.add_argument("--no-probe", action="store_true", help="Skip LAN probing and use default/specified IP")
    parser.add_argument("--no-browser", action="store_true", help="Do not automatically launch the web browser")
    parser.add_argument("--no-pna", action="store_true", help="Skip applying Windows Private Network Access policies")
    return parser


def run_server(args=None):
    global g_target_ip

    if args is None:
        parser = build_parser()
        args = parser.parse_args()

    print("=" * 65)
    print(" STΔN studio. Local Admin Web Server & Reverse Proxy")
    print("=" * 65)

    if args.ip:
        g_target_ip = args.ip
        print(f" Target Device (explicit): http://{g_target_ip}/")
    elif args.no_probe:
        print(f" Probing skipped. Using default target: http://{g_target_ip}/")
    else:
        candidates = [c.strip() for c in args.candidates.split(",")] if args.candidates else DEFAULT_CANDIDATE_IPS
        print(" Probing for ESP32 Desk Monitor...")
        g_target_ip = probe_esp_ip(candidates)
        print(f" Target Device: http://{g_target_ip}/")

    if not args.no_pna:
        try:
            from setup_vercel_pna import apply_browser_policies
            apply_browser_policies()
        except Exception:
            pass

    server = None
    bound_port = args.port
    for p in range(args.port, args.port + 10):
        try:
            server = ThreadedHTTPServer((args.host, p), AdminRequestHandler)
            bound_port = p
            break
        except OSError:
            continue

    if not server:
        print(f" Error: Unable to bind to any port between {args.port} and {args.port + 10}.")
        sys.exit(1)

    url = f"http://localhost:{bound_port}/?ip={g_target_ip}"
    print(f" Admin Panel live at: {url}")
    print(f" Direct Device URL:   http://{g_target_ip}/")
    if not args.no_browser:
        print(" Opening dashboard in default browser...")
        try:
            webbrowser.open(url)
        except Exception:
            pass
    print(" Press Ctrl+C to stop.")
    print("=" * 65)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n Shutting down admin server.")
        server.server_close()


def main():
    parser = build_parser()
    args = parser.parse_args()
    run_server(args)


if __name__ == "__main__":
    main()
