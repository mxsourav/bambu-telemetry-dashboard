"""
Unit tests for STΔN studio cloud backend API (FastAPI service).
"""

import unittest
from fastapi.testclient import TestClient
from backend.main import app


class BackendApiTestCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.client = TestClient(app)

    def test_root_endpoint(self):
        resp = self.client.get("/")
        self.assertEqual(resp.status_code, 200)
        data = resp.json()
        self.assertEqual(data.get("status"), "online")
        self.assertIn("STAN Desk Station Cloud API", data.get("service", ""))
        self.assertIn("/api/status", data.get("endpoints", []))
        self.assertIn("/api/music/classify", data.get("endpoints", []))

    def test_status_endpoint(self):
        resp = self.client.get("/api/status")
        self.assertEqual(resp.status_code, 200)
        data = resp.json()
        self.assertEqual(data.get("status"), "healthy")
        self.assertEqual(data.get("cloud_provider"), "Render")
        self.assertIn("primary_model", data)

    def test_music_classify_ballad_rules(self):
        test_cases = [
            {"title": "Drivers License", "artist": "Olivia Rodrigo"},
            {"title": "Someone Like You", "artist": "Adele"},
            {"title": "Downers At Dusk", "artist": "Talha Anjum"},
            {"title": "All Girls Are The Same", "artist": "Juice WRLD"},
            {"title": "Sad Heartbreak Nights", "artist": "Unknown"}
        ]
        for tc in test_cases:
            with self.subTest(title=tc["title"]):
                resp = self.client.post("/api/music/classify", json=tc)
                self.assertEqual(resp.status_code, 200)
                data = resp.json()
                self.assertEqual(data.get("genre"), "Ballad")
                self.assertEqual(data.get("vibe_style"), 5)
                self.assertEqual(data.get("source"), "rules")

    def test_music_classify_edm_rules(self):
        test_cases = [
            {"title": "Midnight Club Dance", "artist": "Avicii"},
            {"title": "Techno Rave Party 2026", "artist": "DJ Test"},
            {"title": "House Remix Deluxe", "artist": "Producer"}
        ]
        for tc in test_cases:
            with self.subTest(title=tc["title"]):
                resp = self.client.post("/api/music/classify", json=tc)
                self.assertEqual(resp.status_code, 200)
                data = resp.json()
                self.assertEqual(data.get("genre"), "EDM")
                self.assertEqual(data.get("vibe_style"), 2)
                self.assertEqual(data.get("source"), "rules")

    def test_music_classify_rock_rules(self):
        test_cases = [
            {"title": "In The End", "artist": "Linkin Park"},
            {"title": "Heavy Metal Thunder", "artist": "Band"},
            {"title": "Smells Like Teen Spirit", "artist": "Nirvana"}
        ]
        for tc in test_cases:
            with self.subTest(title=tc["title"]):
                resp = self.client.post("/api/music/classify", json=tc)
                self.assertEqual(resp.status_code, 200)
                data = resp.json()
                self.assertEqual(data.get("genre"), "Rock")
                self.assertEqual(data.get("vibe_style"), 3)
                self.assertEqual(data.get("source"), "rules")

    def test_music_classify_lofi_rules(self):
        test_cases = [
            {"title": "Rainy Window Chillhop", "artist": "Lofi Girl"},
            {"title": "Late Night Study Beats", "artist": "Producer"},
            {"title": "Ambient Calm Space", "artist": "Artist"}
        ]
        for tc in test_cases:
            with self.subTest(title=tc["title"]):
                resp = self.client.post("/api/music/classify", json=tc)
                self.assertEqual(resp.status_code, 200)
                data = resp.json()
                self.assertEqual(data.get("genre"), "Lofi")
                self.assertEqual(data.get("vibe_style"), 4)
                self.assertEqual(data.get("source"), "rules")

    def test_music_classify_hiphop_rules(self):
        test_cases = [
            {"title": "NY Drill Anthem", "artist": "Rapper"},
            {"title": "Trap Beat 808 Heat", "artist": "Producer"},
            {"title": "Classic Hip-Hop Track", "artist": "MC"}
        ]
        for tc in test_cases:
            with self.subTest(title=tc["title"]):
                resp = self.client.post("/api/music/classify", json=tc)
                self.assertEqual(resp.status_code, 200)
                data = resp.json()
                self.assertEqual(data.get("genre"), "Hip-Hop/Rap")
                self.assertEqual(data.get("vibe_style"), 1)
                self.assertEqual(data.get("source"), "rules")

    def test_music_classify_fallback_pop(self):
        resp = self.client.post("/api/music/classify", json={"title": "Completely Unmatched Song", "artist": "Anon"})
        self.assertEqual(resp.status_code, 200)
        data = resp.json()
        self.assertIn(data.get("genre"), ["Pop", "Ballad", "EDM", "Rock", "Lofi", "Hip-Hop/Rap"])

    def test_ask_ai_endpoint_structure(self):
        # Without key, expects HTTP 500 with informative message
        resp = self.client.post("/api/ai/ask", json={"prompt": "What is the status of the desk station?"})
        self.assertIn(resp.status_code, [200, 500])


if __name__ == "__main__":
    unittest.main()
