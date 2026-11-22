"""
STΔN Desk Station Cloud Backend Service (Render Deployment)
Provides 24/7 cloud AI intelligence, genre classification, and Spotify token exchange.
"""
import os
import re
import json
import requests
from fastapi import FastAPI, HTTPException, Request
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from typing import Optional

app = FastAPI(title="STAN Desk Station Cloud API", version="1.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

NVIDIA_API_KEY = os.environ.get("NVIDIA_API_KEY", "")
NVIDIA_BASE_URL = os.environ.get("NVIDIA_BASE_URL", "https://integrate.api.nvidia.com/v1")
PRIMARY_MODEL = os.environ.get("NVIDIA_MODEL", "nvidia/nemotron-3.5-bolt-e4b")

SPOTIFY_CLIENT_ID = os.environ.get("SPOTIFY_CLIENT_ID", "")
SPOTIFY_CLIENT_SECRET = os.environ.get("SPOTIFY_CLIENT_SECRET", "")

class AskRequest(BaseModel):
    prompt: str
    system_prompt: Optional[str] = None
    temperature: Optional[float] = 0.2

class ClassifyRequest(BaseModel):
    title: str
    artist: Optional[str] = ""

@app.get("/")
def root():
    return {
        "status": "online",
        "service": "STAN Desk Station Cloud API",
        "endpoints": ["/api/status", "/api/ai/ask", "/api/music/classify", "/api/weather"]
    }

@app.get("/api/status")
def get_status():
    return {
        "status": "healthy",
        "cloud_provider": "Render",
        "ai_ready": bool(NVIDIA_API_KEY),
        "primary_model": PRIMARY_MODEL
    }

@app.post("/api/music/classify")
def classify_music_genre(req: ClassifyRequest):
    """Classifies music genre using fast sub-second NVIDIA NIM inference."""
    title = req.title.strip()
    artist = (req.artist or "").strip()
    combined = f"{artist} {title}".lower()

    # Rule-based fast pass (0ms)
    breakup_keywords = [
        "breakup", "heartbreak", "sad", "crying", "tears", "alone", "lonely", "whiskey",
        "ballad", "die with a smile", "someone like you", "channa mereya", "drivers license",
        "regret", "how to hate", "fuck love", "fuckk love", "bitch", "numb",
        "all girls are the same", "lucid dreams", "downers at dusk", "down at dusk", "gumaan"
    ]
    if any(k in combined for k in breakup_keywords):
        return {"genre": "Ballad", "vibe_style": 5, "source": "rules"}

    if any(k in combined for k in ["party", "dance", "club", "rave", "remix", "edm", "house", "techno"]):
        return {"genre": "EDM", "vibe_style": 2, "source": "rules"}

    if any(k in combined for k in ["rock", "metal", "punk", "grunge", "linkin", "nirvana", "fossils"]):
        return {"genre": "Rock", "vibe_style": 3, "source": "rules"}

    if any(k in combined for k in ["lofi", "lo-fi", "chillhop", "ambient", "study beats"]):
        return {"genre": "Lofi", "vibe_style": 4, "source": "rules"}

    if any(k in combined for k in ["hip-hop", "hip hop", "rap", "drill", "trap", "808"]):
        return {"genre": "Hip-Hop/Rap", "vibe_style": 1, "source": "rules"}

    # Cloud LLM reflex fallback
    if not NVIDIA_API_KEY:
        return {"genre": "Pop", "vibe_style": 0, "source": "default"}

    prompt = (
        f"Classify music genre of: \"{title}\" by \"{artist}\".\n"
        "Return ONLY JSON: {\"genre\": \"Hip-Hop/Rap\"|\"Rock\"|\"EDM\"|\"Lofi\"|\"Ballad\"|\"Pop\"}\n"
        "Rules:\n"
        "- Sad, breakup, heartbreak, crying, emo rap -> \"Ballad\"\n"
        "- Rap, drill, trap -> \"Hip-Hop/Rap\"\n"
        "- House, dance, edm -> \"EDM\"\n"
        "- Rock, metal -> \"Rock\"\n"
        "- Lofi, chillhop, ambient -> \"Lofi\"\n"
        "- Commercial upbeat -> \"Pop\""
    )

    try:
        url = f"{NVIDIA_BASE_URL}/chat/completions"
        headers = {"Authorization": f"Bearer {NVIDIA_API_KEY}", "Content-Type": "application/json"}
        payload = {
            "model": PRIMARY_MODEL,
            "messages": [{"role": "user", "content": prompt}],
            "max_tokens": 30,
            "temperature": 0.1
        }
        res = requests.post(url, headers=headers, json=payload, timeout=3.5)
        if res.status_code == 200:
            data = res.json()
            content = data["choices"][0]["message"].get("content", "")
            match = re.search(r'\{.*\}', content, re.DOTALL)
            if match:
                parsed = json.loads(match.group(0))
                g = parsed.get("genre", "Pop")
                style_map = {"Hip-Hop/Rap": 1, "EDM": 2, "Rock": 3, "Lofi": 4, "Ballad": 5, "Pop": 0}
                return {"genre": g, "vibe_style": style_map.get(g, 0), "source": "nim_llm"}
    except Exception:
        pass

    return {"genre": "Pop", "vibe_style": 0, "source": "fallback"}

@app.post("/api/ai/ask")
def ask_ai(req: AskRequest):
    """Direct cloud AI response for desk station queries."""
    if not NVIDIA_API_KEY:
        raise HTTPException(status_code=500, detail="NVIDIA_API_KEY not configured on Render")

    sys_p = req.system_prompt or "You are STAN, an intelligent, concise desk companion. Keep answers under 30 words."
    messages = [
        {"role": "system", "content": sys_p},
        {"role": "user", "content": req.prompt}
    ]
    try:
        url = f"{NVIDIA_BASE_URL}/chat/completions"
        headers = {"Authorization": f"Bearer {NVIDIA_API_KEY}", "Content-Type": "application/json"}
        payload = {
            "model": PRIMARY_MODEL,
            "messages": messages,
            "max_tokens": 100,
            "temperature": req.temperature or 0.2
        }
        res = requests.post(url, headers=headers, json=payload, timeout=6.0)
        if res.status_code == 200:
            data = res.json()
            return {"reply": data["choices"][0]["message"]["content"]}
        raise HTTPException(status_code=res.status_code, detail=res.text)
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))
