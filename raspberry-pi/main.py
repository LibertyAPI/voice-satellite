"""
Voice Satellite - Processing Hub Server

FastAPI server that receives audio from ESP32 voice satellites,
processes it through the pipeline, and returns results.

Phase 2: Echo mode (returns same audio)
Phase 3 (current): Cloud STT via OpenAI Whisper API
Phase 4+: Will add AI → TTS pipeline

Usage:
    export OPENAI_API_KEY='sk-...'
    python main.py
"""

import struct
import time
import logging
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse, Response

from services import stt_service

# ============================================================
# CONFIGURATION
# ============================================================

HOST = "0.0.0.0"       # Listen on all interfaces
PORT = 8000
AUDIO_DIR = Path("received_audio")

# ============================================================
# LOGGING
# ============================================================

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S"
)
log = logging.getLogger("voice-hub")

# ============================================================
# APP
# ============================================================

app = FastAPI(title="Voice Satellite Hub", version="0.3.0")

AUDIO_DIR.mkdir(exist_ok=True)


@app.get("/api/health")
async def health():
    """Health check endpoint."""
    return {
        "status": "ok",
        "version": "0.3.0",
        "phase": "cloud-stt",
        "services": {
            "stt": "openai-whisper" if stt_service.is_available() else "no_api_key",
            "ai": "not_installed",
            "tts": "not_installed",
        }
    }


@app.post("/api/voice")
async def process_voice(request: Request):
    """
    Receive audio from ESP32, process through pipeline, return result.

    Current mode:
    - If OPENAI_API_KEY is set: transcribes audio via Whisper API
    - If no API key: falls back to echo mode

    Returns JSON with transcription (for now).
    Future: will return audio once TTS is added.
    """
    start_time = time.time()

    body = await request.body()
    content_type = request.headers.get("content-type", "unknown")

    log.info(f"Received audio: {len(body)} bytes, content-type: {content_type}")

    if len(body) < 44:
        log.warning("Audio too small (< WAV header size)")
        return JSONResponse(
            content={"error": "Audio too small"},
            status_code=400
        )

    # Parse and log WAV info
    wav_info = None
    try:
        wav_info = parse_wav_header(body)
        log.info(
            f"WAV: {wav_info['sample_rate']}Hz, "
            f"{wav_info['bits_per_sample']}bit, "
            f"{wav_info['channels']}ch, "
            f"{wav_info['duration']:.1f}s"
        )
    except Exception as e:
        log.warning(f"Could not parse WAV header: {e}")

    # Save to disk for debugging
    timestamp = int(time.time())
    save_path = AUDIO_DIR / f"recording_{timestamp}.wav"
    save_path.write_bytes(body)
    log.info(f"Saved to {save_path}")

    # ──────────────────────────────────────────────
    # PIPELINE
    # ──────────────────────────────────────────────

    transcript = None
    pipeline_mode = "echo"

    # Phase 3: Cloud STT
    if stt_service.is_available():
        pipeline_mode = "cloud-stt"
        try:
            transcript = await stt_service.transcribe(body)
            log.info(f">>> TRANSCRIPT: \"{transcript}\"")
        except Exception as e:
            log.error(f"STT failed: {e}")
            transcript = f"[STT Error: {e}]"
    else:
        log.warning("No OPENAI_API_KEY set — running in echo mode")

    # Phase 4 (future): AI response
    # ai_response = await ai_service.ask(transcript)

    # Phase 5 (future): TTS
    # response_audio = await tts_service.speak(ai_response)

    elapsed = time.time() - start_time
    log.info(f"Processing complete in {elapsed:.2f}s")

    # Return transcription as JSON
    # (Once TTS is added, this will return audio/wav instead)
    return JSONResponse(
        content={
            "transcript": transcript,
            "duration": wav_info["duration"] if wav_info else None,
            "pipeline": pipeline_mode,
            "processing_time": round(elapsed, 3),
        },
        headers={
            "X-Processing-Time": f"{elapsed:.3f}",
            "X-Pipeline-Mode": pipeline_mode,
        }
    )


def parse_wav_header(data: bytes) -> dict:
    """Parse a WAV file header and return audio properties."""
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise ValueError("Not a valid WAV file")

    audio_format = struct.unpack_from('<H', data, 20)[0]
    channels = struct.unpack_from('<H', data, 22)[0]
    sample_rate = struct.unpack_from('<I', data, 24)[0]
    bits_per_sample = struct.unpack_from('<H', data, 34)[0]
    data_size = struct.unpack_from('<I', data, 40)[0]

    duration = data_size / (sample_rate * channels * (bits_per_sample // 8))

    return {
        "audio_format": audio_format,
        "channels": channels,
        "sample_rate": sample_rate,
        "bits_per_sample": bits_per_sample,
        "data_size": data_size,
        "duration": duration,
    }


# ============================================================
# ENTRY POINT
# ============================================================

if __name__ == "__main__":
    import uvicorn

    stt_status = "READY" if stt_service.is_available() else "NO API KEY (echo mode)"

    log.info("=" * 50)
    log.info("  Voice Satellite Hub")
    log.info(f"  STT: {stt_status}")
    log.info(f"  Listening on http://{HOST}:{PORT}")
    log.info("=" * 50)

    uvicorn.run(app, host=HOST, port=PORT, log_level="info")
