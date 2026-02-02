# Voice Satellite

## Problem
Commercial voice assistants (Alexa, Google Home) are limited to predefined capabilities and raise privacy concerns by sending audio to cloud servers. There's a need for a privacy-focused, AI-powered voice assistant that can handle open-ended conversations and complex queries while keeping processing local (or using trusted AI services).

## Solution
A DIY voice assistant system combining ESP32-S3 hardware with Raspberry Pi processing. The ESP32 acts as a voice satellite (microphone + speaker + button), while the Raspberry Pi handles the heavy lifting: speech-to-text (faster-whisper), AI responses (Claude Code CLI), and text-to-speech (Piper TTS). This separates the simple, low-power satellite from the processing hub, allowing multiple satellites in the future.

## Architecture

### Hardware Components
- **ESP32-S3 Development Board** (Voice Satellite)
  - INMP441 I2S MEMS microphone (audio input)
  - PCM5102A I2S DAC module (audio output)
  - Speaker (3-8W, 4-8 ohm)
  - Push-to-talk button
  - Status LED (optional, many boards have built-in)

- **Processing Hub** (Debian server — upgraded from Raspberry Pi 4)
  - Any x86/ARM machine running Debian/Ubuntu on the local network
  - More CPU/RAM headroom for AI processing
  - Runs FastAPI server + cloud STT pipeline

### Data Flow Pipeline
```
User presses button
  ↓
ESP32-S3: Record audio via INMP441 microphone
  ↓
WiFi: Send audio (WAV) to Raspberry Pi via HTTP POST
  ↓
Server: Speech-to-Text (OpenAI Whisper API — cloud)
  ↓
RPi: AI Processing (Claude Code CLI: `claude -p "transcribed text"`)
  ↓
RPi: Text-to-Speech (Piper TTS)
  ↓
WiFi: Send audio response back to ESP32
  ↓
ESP32: Play audio through PCM5102A DAC → Speaker
```

### Technology Stack

**ESP32-S3 Firmware:**
- Language: C++ (Arduino framework)
- IDE: PlatformIO
- Libraries: WiFi, I2S, HTTPClient
- Communication: HTTP POST (v1), WebSocket (future v2)

**Raspberry Pi Server:**
- Language: Python 3.9+
- Web Framework: FastAPI + uvicorn
- STT: OpenAI Whisper API (cloud) via httpx
- AI: Claude Code CLI (subprocess calls) — planned
- TTS: Piper TTS (local neural TTS) — planned
- Audio Processing: numpy, wave

## ESP32-S3 Pin Configuration

### INMP441 Microphone (I2S Input)
| INMP441 Pin | ESP32-S3 Pin | Description |
|-------------|--------------|-------------|
| SCK         | GPIO 4       | Serial Clock |
| WS          | GPIO 5       | Word Select (L/R Clock) |
| SD          | GPIO 6       | Serial Data |
| VDD         | 3.3V         | Power |
| GND         | GND          | Ground |

### PCM5102A DAC (I2S Output)
| PCM5102A Pin | ESP32-S3 Pin | Description |
|--------------|--------------|-------------|
| BCK          | GPIO 15      | Bit Clock |
| LCK (LRCK)   | GPIO 16      | Word Select Clock |
| DIN          | GPIO 17      | Data Input |
| VIN          | 5V or 3.3V   | Power |
| GND          | GND          | Ground |

### Additional Components
| Component    | ESP32-S3 Pin | Description |
|--------------|--------------|-------------|
| PTT Button   | GPIO 0       | Push-to-Talk (with internal pull-up) |
| Status LED   | GPIO 2       | Recording/Processing indicator |

## Audio Specifications

- **Sample Rate:** 16000 Hz (16 kHz, optimal for speech and Whisper)
- **Bit Depth:** 16-bit signed PCM
- **Channels:** Mono
- **Transfer Format:** WAV (PCM container)
- **Encoding:** Linear PCM (no compression)

## Raspberry Pi Server API

### Endpoints (v1)

**POST /api/voice**
- Receives: Raw WAV binary body (Content-Type: audio/wav)
- Processing: Phase 2 = echo; Phase 6 = STT → AI → TTS
- Returns: WAV audio file (Content-Type: audio/wav)
- Response headers: X-Processing-Time, X-Pipeline-Mode
- Saves recording to `received_audio/recording_{timestamp}.wav`

**GET /api/health**
- Returns: JSON with server status, version, and service availability
- Example: `{"status":"ok","phase":"echo-test","services":{"stt":"not_installed",...}}`

### Future Endpoints (v2)
- WebSocket `/ws/voice` for streaming audio
- GET `/api/conversation/history` for context

## Project Structure

```
voice-satellite/
├── PROJECT.md                    # This file
├── esp32/                        # ESP32-S3 firmware
│   ├── platformio.ini           # PlatformIO config (ESP32-S3, Arduino) ✅
│   └── src/
│       └── main.cpp             # All-in-one firmware: I2S, WiFi, HTTP, PTT ✅
├── raspberry-pi/                 # Raspberry Pi server
│   ├── requirements.txt         # Python deps (fastapi, uvicorn, numpy) ✅
│   ├── main.py                  # FastAPI server — echo mode for Phase 2 ✅
│   ├── received_audio/          # Saved recordings for debugging (auto-created)
│   └── services/
│       ├── __init__.py          # Package init ✅
│       ├── stt_service.py       # OpenAI Whisper API integration ✅
│       ├── ai_service.py        # Claude CLI integration (planned)
│       └── tts_service.py       # Piper TTS integration (planned)
└── docs/                         # Documentation (planned)
```

## Roadmap

### Phase 1: Basic Infrastructure
**Goal:** Verify all hardware components work independently

**Tasks:**
- [x] Set up PlatformIO project for ESP32-S3
- [x] Set up Python project structure for RPi (FastAPI server created)
- [x] Test INMP441 microphone recording on ESP32 (I2S input)
- [x] Test PCM5102A DAC playback on ESP32 (I2S output)
- [x] Test I2S full-duplex mode (mic + DAC simultaneously)
- [x] Loopback test: mic → speaker working (voice quality confirmed)
- [ ] Test push-to-talk button with LED feedback
- [ ] Verify WiFi connectivity between ESP32 and RPi (ping test)
- [ ] Investigate noise reduction for INMP441 in noisy environments

### Phase 2: Network Audio Transfer
**Goal:** Establish reliable audio communication between ESP32 and RPi

**Tasks:**
- [x] ESP32: Implement "record while button pressed" functionality (main.cpp)
- [x] ESP32: Construct WAV header in memory after recording
- [x] ESP32: Send recorded WAV file via HTTP POST (HTTPClient)
- [x] RPi: Create FastAPI server with /api/voice endpoint (main.py)
- [x] RPi: Receive, parse, and save audio files with WAV header validation
- [x] RPi: Echo audio back to ESP32 for round-trip testing
- [x] ESP32: Receive response audio and play through PCM5102A DAC
- [x] ESP32: LED status feedback (on=recording, blink=processing)
- [x] ESP32: PSRAM allocation for large audio buffer (15s max)
- [x] ESP32: Minimum duration check (>0.3s) to discard accidental presses
- [ ] **TEST: Flash firmware and run echo round-trip**
- [ ] **TEST: Verify WAV files saved on RPi are valid (play with aplay/audacity)**

### Phase 3: Cloud STT Pipeline
**Goal:** Convert speech to text reliably via cloud API

**Tasks:**
- [x] Create STT service wrapper (services/stt_service.py) using OpenAI Whisper API
- [x] Integrate STT into /api/voice endpoint (auto-detects API key, falls back to echo)
- [x] Update ESP32 to handle JSON responses (prints transcription to serial)
- [x] Added httpx dependency for async HTTP calls to Whisper API
- [ ] **TEST: Set OPENAI_API_KEY and test transcription with real audio**
- [ ] **TEST: Verify transcription accuracy with ESP32 mic recordings**

### Phase 4: AI Integration
**Goal:** Generate intelligent responses using Claude

**Tasks:**
- [ ] Verify Claude Code CLI is installed and working on RPi
- [ ] Create AI service wrapper (ai_service.py) using subprocess
- [ ] Implement basic prompt: `claude -p "User said: {transcription}. Respond briefly."`
- [ ] Parse and extract response text from CLI output
- [ ] Test with various queries (questions, commands, conversations)
- [ ] Add error handling for API failures/timeouts
- [ ] Consider conversation context (store last N exchanges in memory)

### Phase 5: TTS Pipeline on RPi
**Goal:** Convert AI text responses to natural-sounding speech

**Tasks:**
- [ ] Install Piper TTS on Raspberry Pi
- [ ] Download voice model (choose quality vs speed)
- [ ] Create TTS service wrapper (tts_service.py)
- [ ] Generate audio from test text
- [ ] Convert Piper output to 16-bit PCM, 16kHz WAV format
- [ ] Test audio quality on ESP32 speaker
- [ ] Optimize audio buffer size for smooth playback

### Phase 6: Full Pipeline Integration
**Goal:** Complete end-to-end voice assistant functionality

**Tasks:**
- [ ] Connect all services: audio → STT → AI → TTS → audio
- [ ] Implement full /api/voice endpoint handler
- [ ] Add status feedback on ESP32 (LED states: idle, recording, processing, playing)
- [ ] Measure and log end-to-end latency
- [ ] Optimize bottlenecks (model loading, audio conversion, network transfer)
- [ ] Add comprehensive error handling and user feedback
- [ ] Test extensively with real-world queries
- [ ] Create usage documentation

### Phase 7 (Future Enhancements): Advanced Features
**Goal:** Improve user experience and capabilities

**Tasks:**
- [ ] Add wake word detection (ESP-SR or microWakeWord on ESP32)
- [ ] Implement Voice Activity Detection (VAD) for auto-stop recording
- [ ] Add conversation context management (save/load history)
- [ ] Implement WebSocket streaming for lower latency
- [ ] Support multiple ESP32 satellites with single RPi hub
- [ ] Add web dashboard for monitoring and configuration
- [ ] Implement voice profiles (recognize different users)
- [ ] Add local fallback responses when network/AI unavailable

## Current Status
**Phase 3 — Cloud STT integrated, ready for testing. (2026-02-02)**
- Phase 1: ✅ Hardware verified (I2S duplex, loopback, mic + DAC working)
- Phase 2: ✅ ESP32 firmware + server code written (echo mode works)
- Phase 3: 🔧 Cloud STT code written (OpenAI Whisper API). Awaiting API key test.
- Platform: Switched from Raspberry Pi 4 to stronger Debian machine
- Phases 4-7: Pending

## Todo
- [ ] Flash ESP32 firmware and test PTT recording over serial
- [ ] Set up Python venv on server, install deps, set OPENAI_API_KEY
- [ ] Run full test: speak → ESP32 → server → Whisper API → transcription in logs
- [ ] Move to Phase 4: AI integration (Claude Code CLI)

## Decisions Log

### 2026-01-29 - Hardware Platform Selection
**Choice:** ESP32-S3 + Raspberry Pi 4 architecture
**Why:**
- ESP32-S3 has dual I2S interfaces (simultaneous mic input + speaker output)
- Low power consumption for always-on satellite
- Raspberry Pi 4 has sufficient CPU/RAM for AI models (faster-whisper, Piper)
- Separating satellite from hub allows future scalability (multiple satellites)
- Cost-effective compared to running everything on more powerful embedded boards

**Alternatives considered:**
- Raspberry Pi Zero 2W only: Too slow for real-time STT/TTS
- ESP32 + cloud services: Privacy concerns, requires internet, ongoing costs
- Desktop PC as hub: Works but overkill for dedicated appliance

### 2026-01-29 - STT Model Selection
**Choice:** faster-whisper (small or base model)
**Why:**
- Runs locally on RPi4 (privacy + works offline)
- Faster than original Whisper (optimized with CTranslate2)
- High accuracy for English speech
- Small model fits in RPi4 memory, base offers better accuracy if performance allows

**Alternatives considered:**
- Vosk: Faster but lower accuracy
- Cloud STT (Google, Azure): Privacy concerns, requires internet, costs
- Whisper.cpp: Good option but faster-whisper has better Python integration

### 2026-01-29 - AI Response Generation
**Choice:** Claude Code CLI via subprocess
**Why:**
- Already installed and configured on RPi
- Provides high-quality, contextual AI responses
- Simple integration via command line
- Can leverage Claude's latest capabilities

**Alternatives considered:**
- Local LLM (Llama, Mistral): Requires significant RAM/processing, slower on RPi4
- GPT API: Requires internet, costs per request, privacy concerns
- Rule-based responses: Too limited for open-ended conversations

### 2026-01-29 - TTS Engine Selection
**Choice:** Piper TTS
**Why:**
- Runs locally (fast, private, offline)
- Neural TTS with natural-sounding voices
- Low latency compared to cloud TTS
- Multiple voice options
- Works well on Raspberry Pi 4

**Alternatives considered:**
- espeak: Very fast but robotic voice quality
- Festival: Dated voice quality
- Cloud TTS (Google, Amazon): Requires internet, latency, costs
- Coqui TTS: Good quality but slower than Piper

### 2026-01-29 - Communication Protocol (v1)
**Choice:** HTTP POST (request-response)
**Why:**
- Simple to implement and debug
- Reliable (built-in error handling)
- Works with standard web tools (curl, Postman)
- Good enough for PTT use case (user waits for response)
- Easy to add authentication later

**Alternatives considered:**
- WebSocket streaming: Better latency but more complex (planned for v2)
- MQTT: Adds broker dependency, unnecessary for 1:1 communication
- Raw TCP: Lower-level complexity without significant benefit

### 2026-02-02 - Platform Switch: RPi 4 → Debian Server
**Choice:** Replace Raspberry Pi 4 with a more powerful Debian machine
**Why:**
- More CPU/RAM for running the processing pipeline
- Still on local network, same architecture (ESP32 satellite → server hub)
- All code is platform-agnostic Python + FastAPI, no changes needed for the switch

### 2026-02-02 - STT Switch: Local faster-whisper → OpenAI Whisper API (Cloud)
**Choice:** Use OpenAI's Whisper API instead of running faster-whisper locally
**Why:**
- Simpler setup (no model download, no GPU/CPU optimization)
- Higher accuracy (cloud model is larger than what runs locally)
- Faster transcription (offloads compute to OpenAI's servers)
- Requires OPENAI_API_KEY environment variable and internet access

**Alternatives considered:**
- faster-whisper (local): Good for privacy/offline, but more setup and slower on CPU
- Groq Whisper API: Faster and cheaper, same API pattern — easy to swap later
- Deepgram: Good streaming support, could revisit for Phase 7 WebSocket upgrade

### 2026-01-29 - Protocol Selection: HTTP First, WebSocket Later
**Choice:** HTTP POST for v1 (record-then-send), upgrade to WebSocket in v2
**Why:**
- HTTP is simplest to implement and debug — standard libraries on both sides
- For PTT with 3-8 second commands, the ~0.3s upload overhead is negligible next to AI processing time (~2-3s)
- ESP32-S3 has enough PSRAM for 15 seconds of buffered audio (480KB)
- WebSocket's main advantage (no RAM limit, streaming STT overlap) only matters when we add wake word + VAD
- Discussed all three options (HTTP, WebSocket, UDP) — UDP rejected due to unnecessary complexity on LAN

**Key insight from discussion:**
- WebSocket streaming only saves ~0.3s upload time unless paired with streaming STT (faster-whisper doesn't support streaming)
- True latency savings require streaming STT (Vosk or whisper-streaming) — planned for Phase 7

### 2026-01-29 - ESP32 Firmware Architecture
**Choice:** Single-file main.cpp (monolithic) for Phase 2
**Why:**
- Faster to develop and iterate during prototyping
- All I2S, WiFi, HTTP, and button logic in one place for easy overview
- Will refactor into separate modules (audio_recorder.h, http_client.h, etc.) once the full pipeline is stable
- PSRAM-first buffer allocation for maximum recording length

## Notes

### Useful Resources
- [faster-whisper GitHub](https://github.com/guillaumekln/faster-whisper)
- [Piper TTS GitHub](https://github.com/rhasspy/piper)
- [ESP32-S3 I2S Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/i2s.html)
- [INMP441 Datasheet](https://www.invensense.com/products/digital/inmp441/)
- [PCM5102A Datasheet](https://www.ti.com/product/PCM5102A)

### Future Ideas
- Add LCD display for visual feedback (transcription, response text)
- Implement smart home control (turn on lights, adjust thermostat)
- Add calendar/reminder functionality
- Voice-controlled music playback
- Multi-language support
- Privacy mode (disable listening with physical switch)
- Battery-powered portable version
- 3D printed enclosure design

### Performance Targets
- **Recording Start Latency:** <100ms (button press to recording)
- **STT Processing:** <2s for 5s audio clip
- **AI Response:** <3s (depends on Claude API)
- **TTS Processing:** <1s for typical response
- **Playback Start:** <200ms (audio received to playback start)
- **Total Round-Trip:** <8s for typical interaction

### Power Consumption Estimates
- ESP32-S3 idle (WiFi on): ~40mA @ 3.3V
- ESP32-S3 recording: ~80mA @ 3.3V
- ESP32-S3 playing audio: ~100-150mA (depends on speaker volume)
- Raspberry Pi 4 idle: ~600mA @ 5V
- Raspberry Pi 4 processing: ~1200mA @ 5V

### Security Considerations
- WiFi: Use WPA2/WPA3 encryption
- API: Add authentication token for /api/voice endpoint
- Network: Consider running on isolated VLAN
- Audio: No cloud storage of recordings (unless explicitly desired)
- Updates: Keep ESP32 firmware and RPi software updated

### Testing Checklist (Pre-Deployment)
- [ ] Audio quality (clear recording, no clipping)
- [ ] STT accuracy (95%+ for clear speech)
- [ ] AI response relevance and coherence
- [ ] TTS naturalness and intelligibility
- [ ] Network reliability (WiFi dropouts, reconnection)
- [ ] Error handling (server down, button held too long, etc.)
- [ ] Long-duration testing (memory leaks, stability)
- [ ] Edge cases (loud background noise, multiple speakers, accents)
