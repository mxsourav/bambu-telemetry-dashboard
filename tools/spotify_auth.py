"""
=============================================================================
  STΔN studio. Spotify Permanent Pairing Tool
=============================================================================
Authorizes your Spotify account ONCE and saves your permanent Refresh Token
directly to the ESP32 Desk Monitor's SD card (/settings.json).

After this 1-minute setup:
* The desk monitor stays auto-connected to Spotify FOREVER on boot.
* Works seamlessly whether you play music on your PHONE, PC, or iPad.
* You NEVER need to run any scripts on your PC again!
=============================================================================
"""
import urllib.parse
import urllib.request
import webbrowser
import http.server
import socketserver
import json
import base64
import sys

CLIENT_ID     = "bc80d4154155425a939097ea6079c225"
CLIENT_SECRET = "eb42da8944b74b64be17997b22b7ed42"
DEVICE_IP     = "192.168.0.154"

# Spotify allows 127.0.0.1 loopback or Vercel redirect
REDIRECT_URI  = "https://web-tau-five-60.vercel.app/"
SCOPES        = "user-read-currently-playing user-read-playback-state user-modify-playback-state"

def exchange_code(code):
    print("\n[SPOTIFY] Exchanging authorization code for permanent tokens...")
    token_url = "https://accounts.spotify.com/api/token"
    auth_header = base64.b64encode(f"{CLIENT_ID}:{CLIENT_SECRET}".encode()).decode()
    
    data = urllib.parse.urlencode({
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": REDIRECT_URI
    }).encode()
    
    req = urllib.request.Request(token_url, data=data, headers={
        "Authorization": f"Basic {auth_header}",
        "Content-Type": "application/x-www-form-urlencoded"
    })
    
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            tokens = json.loads(resp.read().decode())
            refresh_token = tokens.get("refresh_token")
            access_token = tokens.get("access_token")
            if refresh_token:
                print("[SUCCESS] Got permanent Refresh Token from Spotify!")
                save_to_esp32(refresh_token)
                return True
            else:
                print("[ERROR] No refresh token returned:", tokens)
    except Exception as e:
        print("[ERROR] Token exchange failed:", e)
    return False

def save_to_esp32(refresh_token):
    print(f"[DEVICE] Sending permanent token to Desk Monitor at http://{DEVICE_IP}/...")
    target_url = f"http://{DEVICE_IP}/api/spotify/token"
    data = json.dumps({"refresh_token": refresh_token}).encode()
    
    req = urllib.request.Request(target_url, data=data, headers={
        "Content-Type": "application/json"
    })
    
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            res = json.loads(resp.read().decode())
            print("=" * 60)
            print(" [SUCCESS] PAIRING COMPLETE!")
            print(" Your Desk Monitor is now permanently connected to your Spotify!")
            print(" It will auto-connect every time it powers on.")
            print(" Works with your phone, laptop, or any Spotify app!")
            print("=" * 60)
            return True
    except Exception as e:
        print(f"[NOTE] Could not reach Desk Monitor directly on {DEVICE_IP}.")
        print("Make sure your PC Wi-Fi is connected to Wann Router 4G.")
        print(f"Your Refresh Token is:\n{refresh_token}\n")
    return False

def main():
    print("=" * 60)
    print(" STΔN studio. Spotify 1-Click Auto-Connect Setup")
    print("=" * 60)
    
    auth_url = (
        f"https://accounts.spotify.com/authorize?"
        f"client_id={CLIENT_ID}&"
        f"response_type=code&"
        f"redirect_uri={urllib.parse.quote(REDIRECT_URI)}&"
        f"scope={urllib.parse.quote(SCOPES)}"
    )
    
    print("\nOpening Spotify login in your browser...")
    print("If it doesn't open automatically, click this link:\n" + auth_url + "\n")
    webbrowser.open(auth_url)
    
    print("-" * 60)
    print("After logging in, your browser will redirect to:")
    print("https://web-tau-five-60.vercel.app/?code=AQ...")
    print("-" * 60)
    
    redirected_url = input("\nPaste the full URL from your browser address bar here:\n> ").strip()
    
    if "code=" in redirected_url:
        parsed = urllib.parse.urlparse(redirected_url)
        params = urllib.parse.parse_qs(parsed.query)
        code = params.get("code", [None])[0]
        if code:
            exchange_code(code)
            return
            
    print("[ERROR] Could not find 'code=' in the pasted URL. Please try again.")

if __name__ == "__main__":
    main()
