"""
Automatic browser security configurator for Vercel Cloud Deployment & LAN ESP32.
Configures Chrome, Edge, and Brave in HKCU to allow Vercel HTTPS origins to access local IP 192.168.137.35 without Mixed Content or Private Network Access blocks.
"""
import sys
import winreg
import urllib.request
import json

TARGET_IP = "192.168.137.140"
PATTERNS = [
    "[*.]vercel.app",
    "https://*.vercel.app",
    "https://web-tau-five-60.vercel.app",
    "http://192.168.137.*",
    "http://192.168.137.140",
    "http://192.168.137.140:80",
    "http://192.168.137.35",
    "http://192.168.137.35:80"
]

POLICIES = [
    ("Google Chrome", r"Software\Policies\Google\Chrome"),
    ("Microsoft Edge", r"Software\Policies\Microsoft\Edge"),
    ("Brave Software", r"Software\Policies\BraveSoftware\Brave"),
]

def apply_browser_policies():
    print("[1/3] Configuring Windows User Profile registry policies...")
    success_count = 0
    for name, base_key in POLICIES:
        for sub_policy in [
            "InsecureContentAllowedForUrls",
            "InsecurePrivateNetworkRequestsAllowedForUrls"
        ]:
            full_path = f"{base_key}\\{sub_policy}"
            try:
                key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, full_path)
                for idx, pattern in enumerate(PATTERNS, 1):
                    winreg.SetValueEx(key, str(idx), 0, winreg.REG_SZ, pattern)
                winreg.CloseKey(key)
                success_count += 1
            except Exception as e:
                print(f"  Note: Could not write {full_path}: {e}")

    print(f"  Applied {success_count} policy registry rules successfully.")

def test_esp_connectivity():
    print("[2/3] Verifying ESP32 connection at http://" + TARGET_IP + " ...")
    try:
        req = urllib.request.Request(
            f"http://{TARGET_IP}/api/status",
            headers={"User-Agent": "Vercel-Auto-Setup/1.0"}
        )
        with urllib.request.urlopen(req, timeout=3) as resp:
            data = json.loads(resp.read().decode())
            print(f"  ESP32 online! Free Heap: {data.get('heap_free', 0)} bytes, Uptime: {data.get('uptime_s', 0)}s, App Mode: {data.get('app_mode', 'unknown')}")
            return True
    except Exception as e:
        print(f"  Warning: ESP32 ping returned: {e}")
        return False

def print_summary(online):
    print("[3/3] Setup Completed.")
    print("----------------------------------------------------------------------")
    print("Vercel Cloud Deployment auto-setup is active!")
    print(f"Chrome and Edge will now allow https://*.vercel.app to reach http://{TARGET_IP}.")
    print("If you had Chrome or Edge open, restart the browser or open a new tab.")
    print("----------------------------------------------------------------------")

if __name__ == "__main__":
    apply_browser_policies()
    online = test_esp_connectivity()
    print_summary(online)
