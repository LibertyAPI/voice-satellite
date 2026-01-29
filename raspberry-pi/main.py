"""
Voice Satellite - Raspberry Pi Processing Hub

FastAPI server that receives audio from ESP32 voice satellites,
processes it through the STT → AI → TTS pipeline, and returns
audio responses.

Phase 2 (current): Echo mode - receives audio and sends it back
Phase 3+: Will add STT → AI → TTS pipeline
"""

import io
import wave
import struct
import time
import logging
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.responses import Response

# ============================================================
# CONFIGURATION
# ============================================================

HOST = "0.0.0.0"       # Listen on all interfaces
PORT = 8000
AUDIO_DIR = Path("received_audio")  # Directory to save received audio for debugging

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

app = FastAPI(title="Voice Satellite Hub", version="0.2.0")

# Create audio directory on startup
AUDIO_DIR.mkdir(exist_ok=True)


@app.get("/api/health")
async def health():
    """Health check endpoint."""
    return {
        "status": "ok",
        "version": "0.2.0",
        "phase": "echo-test",
        "services": {
            "stt": "not_installed",
            "ai": "not_installed",
            "tts": "not_installed",
        }
    }


@app.post("/api/voice")
async def process_voice(request: Request):
    """
    Receive audio from ESP32, process it, return audio response.

    Current mode: ECHO - returns the same audio back for round-trip testing.
    Future: STT → AI → TTS pipeline.
    """
    start_time = time.time()

    # Read raw body (ESP32 sends WAV with Content-Type: audio/wav)
    body = await request.body()
    content_type = request.headers.get("content-type", "unknown")

    log.info(f"Received audio: {len(body)} bytes, content-type: {content_type}")

    if len(body) < 44:
        log.warning("Audio too small (< WAV header size)")
        return Response(content=b"", status_code=400)

    # Parse WAV header for logging
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

    # Save to disk for debugging (optional - can disable later)
    timestamp = int(time.time())
    save_path = AUDIO_DIR / f"recording_{timestamp}.wav"
    save_path.write_bytes(body)
    log.info(f"Saved to {save_path}")

    # ──────────────────────────────────────────────
    # PIPELINE: Currently echo mode
    # Future phases will replace this section:
    #   Phase 3: transcript = stt_service.transcribe(body)
    #   Phase 4: response   = ai_service.ask(transcript)
    #   Phase 5: audio_out  = tts_service.speak(response)
    # ──────────────────────────────────────────────

    response_audio = body  # Echo mode: return same audio

    elapsed = time.time() - start_time
    log.info(f"Processing complete in {elapsed:.2f}s, returning {len(response_audio)} bytes")

    return Response(
        content=response_audio,
        media_type="audio/wav",
        headers={
            "X-Processing-Time": f"{elapsed:.3f}",
            "X-Pipeline-Mode": "echo",
        }
    )


def parse_wav_header(data: bytes) -> dict:
    """Parse a WAV file header and return audio properties."""
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise ValueError("Not a valid WAV file")

    # fmt chunk starts at byte 12
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

    log.info("=" * 50)
    log.info("  Voice Satellite Hub - Raspberry Pi")
    log.info("  Mode: ECHO TEST (Phase 2)")
    log.info(f"  Listening on http://{HOST}:{PORT}")
    log.info("=" * 50)

    uvicorn.run(app, host=HOST, port=PORT, log_level="info")
