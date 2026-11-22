"""
talk_to_pet.py - Interactive CLI interface to converse with the Desk Companion AI
Queries the local Pet AI Cortex or direct NVIDIA NIM inference microservice.
"""

import sys
import json
import urllib.request
import urllib.error

API_URL = "http://127.0.0.1:8765/api/ai/ask"


def query_cortex(prompt):
    payload = json.dumps({"prompt": prompt}).encode("utf-8")
    req = urllib.request.Request(API_URL, data=payload, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=12.0) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.URLError:
        # If background daemon isn't running, run direct inference
        from pet_ai_cortex import PetAICortex
        cortex = PetAICortex()
        res = cortex.generate_response(prompt)
        cortex.actuate(res)
        return res


def main():
    if len(sys.argv) > 1:
        query = " ".join(sys.argv[1:])
        print(f"\n[YOU]: {query}")
        res = query_cortex(query)
        print(f"[STAN]: {res.get('reply', '')}")
        print(f"[SCREEN BANNER]: {res.get('display_msg', '')} (Mood: {res.get('mood', 'neutral')})")
        return

    print("=" * 60)
    print("  STΔN studio. Autonomous Desk Pet AI Console")
    print("  Type your question/command or 'exit' to quit.")
    print("=" * 60)

    while True:
        try:
            user_input = input("\n[YOU] > ").strip()
            if not user_input or user_input.lower() in ["exit", "quit", "q"]:
                break
            res = query_cortex(user_input)
            print(f"[STAN]: {res.get('reply', '')}")
            print(f"[SCREEN BANNER]: {res.get('display_msg', '')} (Mood: {res.get('mood', 'neutral')})")
        except (KeyboardInterrupt, EOFError):
            print("\nGoodbye!")
            break


if __name__ == "__main__":
    main()
