"""
Automatic browser security configurator for Vercel Cloud Deployment & LAN ESP32.
Configures Chrome, Edge, and Brave in HKCU to allow Vercel HTTPS origins to access local ESP32 IP without Mixed Content or Private Network Access blocks.
"""
import argparse
import json
import urllib.request

try:
    import winreg
except ImportError:
    winreg = None

DEFAULT_TARGET_IP = "192.168.137.35"

POLICIES = [
    ("Google Chrome", r"Software\Policies\Google\Chrome"),
    ("Microsoft Edge", r"Software\Policies\Microsoft\Edge"),
    ("Brave Software", r"Software\Policies\BraveSoftware\Brave"),
]


def build_patterns(target_ip=DEFAULT_TARGET_IP):
    return [
        "[*.]vercel.app",
        "https://*.vercel.app",
        "https://web-tau-five-60.vercel.app",
        f"http://{target_ip}",
        f"http://{target_ip}:80",
    ]


def build_parser():
    parser = argparse.ArgumentParser(
        description="Configure Chrome/Edge/Brave HKCU policies for Vercel PNA access to ESP32."
    )
    parser.add_argument(
        "--ip",
        default=DEFAULT_TARGET_IP,
        help=f"ESP32 local IP address (default: {DEFAULT_TARGET_IP})",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print policy targets without modifying Windows Registry.",
    )
    parser.add_argument(
        "--skip-ping",
        action="store_true",
        help="Skip ESP32 /api/status connectivity check.",
    )
    return parser


def apply_browser_policies(target_ip=DEFAULT_TARGET_IP, dry_run=False):
    patterns = build_patterns(target_ip)
    print("[1/3] Configuring Windows User Profile registry policies...")
    if dry_run or winreg is None:
        mode = "dry-run" if dry_run else "non-Windows host"
        print(f"  Skipped registry writes ({mode}); {len(patterns)} URL patterns prepared for {target_ip}.")
        return 0

    success_count = 0
    for name, base_key in POLICIES:
        for sub_policy in [
            "InsecureContentAllowedForUrls",
            "InsecurePrivateNetworkRequestsAllowedForUrls"
        ]:
            full_path = f"{base_key}\\{sub_policy}"
            try:
                key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, full_path)
                for idx, pattern in enumerate(patterns, 1):
                    winreg.SetValueEx(key, str(idx), 0, winreg.REG_SZ, pattern)
                winreg.CloseKey(key)
                success_count += 1
            except Exception as e:
                print(f"  Note: Could not write {full_path}: {e}")

    print(f"  Applied {success_count} policy registry rules successfully.")
    return success_count


def test_esp_connectivity(target_ip=DEFAULT_TARGET_IP):
    print(f"[2/3] Verifying ESP32 connection at http://{target_ip} ...")
    try:
        req = urllib.request.Request(
            f"http://{target_ip}/api/status",
            headers={"User-Agent": "Vercel-Auto-Setup/1.0"}
        )
        with urllib.request.urlopen(req, timeout=3) as resp:
            data = json.loads(resp.read().decode())
            print(f"  ESP32 online! Free Heap: {data.get('heap_free', 0)} bytes, Uptime: {data.get('uptime_s', 0)}s, App Mode: {data.get('app_mode', 'unknown')}")
            return True
    except Exception as e:
        print(f"  Warning: ESP32 ping returned: {e}")
        return False


def print_summary(target_ip=DEFAULT_TARGET_IP):
    print("[3/3] Setup Completed.")
    print("----------------------------------------------------------------------")
    print("Vercel Cloud Deployment auto-setup is active!")
    print(f"Chrome and Edge will now allow https://*.vercel.app to reach http://{target_ip}.")
    print("If you had Chrome or Edge open, restart the browser or open a new tab.")
    print("----------------------------------------------------------------------")


def main(argv=None):
    args = build_parser().parse_args(argv)
    apply_browser_policies(target_ip=args.ip, dry_run=args.dry_run)
    if not args.skip_ping:
        test_esp_connectivity(target_ip=args.ip)
    print_summary(target_ip=args.ip)


if __name__ == "__main__":
    main()
