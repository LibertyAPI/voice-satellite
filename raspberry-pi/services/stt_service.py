"""
Cloud STT Service - OpenAI Whisper API

Sends audio to OpenAI's Whisper API for speech-to-text transcription.
Requires OPENAI_API_KEY environment variable.
"""

import os
import logging
import httpx

log = logging.getLogger("voice-hub.stt")

OPENAI_API_KEY = os.environ.get("OPENAI_API_KEY", "")
WHISPER_URL = "https://api.openai.com/v1/audio/transcriptions"
WHISPER_MODEL = "whisper-1"


def is_available() -> bool:
    """Check if the STT service is configured."""
    return bool(OPENAI_API_KEY)


async def transcribe(audio_bytes: bytes, language: str = "en") -> str:
    """
    Send WAV audio to OpenAI Whisper API and return transcription text.

    Args:
        audio_bytes: Raw WAV file bytes (with header)
        language: Language hint for Whisper (ISO 639-1 code)

    Returns:
        Transcribed text string

    Raises:
        RuntimeError: If API key is missing or API call fails
    """
    if not OPENAI_API_KEY:
        raise RuntimeError(
            "OPENAI_API_KEY not set. Export it before starting the server:\n"
            "  export OPENAI_API_KEY='sk-...'"
        )

    log.info(f"Sending {len(audio_bytes)} bytes to Whisper API (model={WHISPER_MODEL})")

    async with httpx.AsyncClient(timeout=30.0) as client:
        response = await client.post(
            WHISPER_URL,
            headers={"Authorization": f"Bearer {OPENAI_API_KEY}"},
            files={"file": ("recording.wav", audio_bytes, "audio/wav")},
            data={
                "model": WHISPER_MODEL,
                "language": language,
                "response_format": "text",
            },
        )

    if response.status_code != 200:
        error_detail = response.text[:500]
        log.error(f"Whisper API error {response.status_code}: {error_detail}")
        raise RuntimeError(f"Whisper API returned {response.status_code}: {error_detail}")

    transcript = response.text.strip()
    log.info(f"Transcription: \"{transcript}\"")
    return transcript
