"""
talk_to_pet.py - Interactive CLI interface to converse with the Desk Companion AI
Queries the local Pet AI Cortex or direct NVIDIA NIM inference microservice.
"""

import argparse
import json
import sys
import urllib.error
import urllib.request

DEFAULT_API_URL = "http://127.0.0.1:8765/api/ai/ask"
DEFAULT_TIMEOUT = 12.0


def build_parser():
    parser = argparse.ArgumentParser(
        description="Interactive CLI interface to converse with the STΔN Desk Companion AI."
    )
    parser.add_argument(
        "prompt",
        nargs="*",
        help="Optional one-shot prompt; omit to launch interactive console.",
    )
    parser.add_argument(
        "--url",
        default=DEFAULT_API_URL,
        help=f"Pet AI Cortex endpoint URL (default: {DEFAULT_API_URL})",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=DEFAULT_TIMEOUT,
        help=f"HTTP request timeout in seconds (default: {DEFAULT_TIMEOUT})",
    )
    return parser


def _load_cortex_class():
    try:
        from pet_ai_cortex import PetAICortex
    except ImportError:
        from tools.pet_ai_cortex import PetAICortex
    return PetAICortex


def query_cortex(prompt, api_url=DEFAULT_API_URL, timeout=DEFAULT_TIMEOUT):
    payload = json.dumps({"prompt": prompt}).encode("utf-8")
    req = urllib.request.Request(
        api_url,
        data=payload,
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except (urllib.error.URLError, json.JSONDecodeError):
        # If background daemon isn't running or returns invalid JSON, run direct inference
        PetAICortex = _load_cortex_class()
        cortex = PetAICortex()
        res = cortex.generate_response(prompt)
        cortex.actuate(res)
        return res


def main(argv=None):
    args = build_parser().parse_args(argv)
    if args.prompt:
        query = " ".join(args.prompt)
        print(f"\n[YOU]: {query}")
        res = query_cortex(query, api_url=args.url, timeout=args.timeout)
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
            res = query_cortex(user_input, api_url=args.url, timeout=args.timeout)
            print(f"[STAN]: {res.get('reply', '')}")
            print(f"[SCREEN BANNER]: {res.get('display_msg', '')} (Mood: {res.get('mood', 'neutral')})")
        except (KeyboardInterrupt, EOFError):
            print("\nGoodbye!")
            break


if __name__ == "__main__":
    main()
