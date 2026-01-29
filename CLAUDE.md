# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

DIY voice assistant ("Voice Satellite") using an ESP32-S3 as a mic/speaker satellite and a Raspberry Pi 4 as the processing hub. The pipeline: ESP32 records audio via push-to-talk → sends WAV over HTTP to RPi → RPi runs STT (faster-whisper) → AI (Claude Code CLI) → TTS (Piper) → returns audio to ESP32 for playback.

See PROJECT.md for full architecture, pin mappings, roadmap, and decisions log.

## Keeping PROJECT.md Up To Date

**This is critical.** When the user reports any of the following, immediately update PROJECT.md:
- **Task completed or tested** → Check off the relevant `[ ]` → `[x]` in the Roadmap
- **Breakthrough or milestone** → Update the `Current Status` section with what was achieved and the date
- **New decision made** → Add an entry to the `Decisions Log` with choice, reasoning, and alternatives
- **New issue discovered** → Add it as a task under the relevant Phase or to the `Todo` section
- **Phase transition** → Update `Current Status` to reflect the new active phase

Do not wait until the end of a session — update the log as things happen.

## Two Codebases

This repo contains two independent projects that communicate over HTTP:

### ESP32 Firmware (`esp32/`)
- **Language:** C++ (Arduino framework)
- **Build system:** PlatformIO
- **Build/upload:** `cd esp32 && pio run -t upload`
- **Serial monitor:** `cd esp32 && pio device monitor -b 115200`
- **Target board:** esp32-s3-devkitc-1 with PSRAM enabled
- **Key hardware:** INMP441 mic (I2S_NUM_0), PCM5102A DAC (I2S_NUM_1), PTT button (GPIO 0, active LOW)
- **All firmware is in a single file** (`src/main.cpp`) — monolithic by design during prototyping

### Raspberry Pi Server (`raspberry-pi/`)
- **Language:** Python 3.9+
- **Framework:** FastAPI + uvicorn
- **Install deps:** `cd raspberry-pi && pip install -r requirements.txt`
- **Run server:** `cd raspberry-pi && python main.py` (listens on port 8000)
- **Test health:** `curl http://localhost:8000/api/health`
- **Test voice endpoint:** `curl -X POST http://localhost:8000/api/voice -H "Content-Type: audio/wav" --data-binary @test.wav --output response.wav`

## Audio Format Contract

Both sides must agree on this format. Changing it requires updating both codebases:
- **Sample rate:** 16000 Hz (required by Whisper STT)
- **Bit depth:** 16-bit signed PCM
- **Channels:** Mono
- **Container:** WAV with 44-byte header
- **Transfer:** Raw WAV binary as HTTP body (Content-Type: audio/wav), NOT multipart form data

## Architecture Decisions

- **HTTP POST (not WebSocket):** Record full audio, send on button release. Simpler, and the ~0.3s upload overhead is negligible vs AI processing time. WebSocket planned for v2 when wake word replaces PTT.
- **Single audioBuffer (ESP32):** One PSRAM-allocated buffer is reused for both recording and receiving response audio. The WAV header is written retroactively after recording stops (first 44 bytes reserved).
- **Echo mode (RPi):** The server currently echoes received audio back. The pipeline placeholder in `process_voice()` shows where STT → AI → TTS services will be inserted.
- **Claude Code CLI for AI:** Will use `claude -p "prompt"` via subprocess — each call is stateless (no conversation memory unless we pass context).

## Configuration That Must Be Updated Per-Deployment

ESP32 `main.cpp` top section:
- `WIFI_SSID` / `WIFI_PASSWORD` — WiFi credentials
- `SERVER_URL` — RPi IP address and port
- GPIO pin numbers if wiring differs from defaults

## Planned Services (not yet implemented)

`raspberry-pi/services/` will contain:
- `stt_service.py` — faster-whisper wrapper
- `ai_service.py` — Claude CLI subprocess wrapper
- `tts_service.py` — Piper TTS wrapper
