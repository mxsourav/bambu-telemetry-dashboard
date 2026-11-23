# STΔN Desk Station Cloud Backend on Render

This backend provides a 24/7 cloud API gateway for the Desk Station, enabling cloud AI intelligence, genre classification, and Spotify token operations when the host PC is turned off.

## Deploying to Render in 1 Click

1. Push this repository to GitHub.
2. Log in to [Render Dashboard](https://dashboard.render.com/).
3. Click **New +** -> **Blueprint**.
4. Select your `bambu-telemetry-dashboard` repository.
5. Render will automatically detect `backend/render.yaml` and configure the web service:
   - **Service Name**: `stan-desk-station-backend`
   - **Environment**: Python 3.11
   - **Build Command**: `pip install -r backend/requirements.txt`
   - **Start Command**: `uvicorn backend.main:app --host 0.0.0.0 --port $PORT`
6. Under **Environment Variables**, set:
   - `NVIDIA_API_KEY`: Your NVIDIA NIM API key
   - `SPOTIFY_CLIENT_ID`: Your Spotify App Client ID
   - `SPOTIFY_CLIENT_SECRET`: Your Spotify App Client Secret
7. Click **Apply**. Render will build and deploy your service at `https://stan-desk-station-backend.onrender.com`.

## Endpoints

- `GET /api/status`: Health check and system readiness
- `POST /api/music/classify`: Fast music genre classifier
- `POST /api/ai/ask`: Direct LLM query endpoint

## Local Development & Testing

Run all automated unit and integration tests across the backend and developer tools:

```bash
# Run full automated test suite
python run_tests.py

# Or run tests using standard unittest
python -m unittest discover tests -v
```
