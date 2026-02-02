/*
 * Voice Satellite - ESP32-S3 Firmware
 *
 * Push-to-talk voice assistant satellite.
 * Records audio from INMP441 mic while button is held,
 * sends WAV to Raspberry Pi server via HTTP POST,
 * receives processed audio response and plays through PCM5102A DAC.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>

// ============================================================
// CONFIGURATION - Update these for your setup
// ============================================================

// WiFi credentials
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Processing hub server address (Debian server / RPi / any machine on LAN)
const char* SERVER_URL = "http://192.168.1.100:8000/api/voice";

// ============================================================
// PIN DEFINITIONS
// ============================================================

// INMP441 Microphone (I2S Input)
#define I2S_MIC_SCK     4   // Serial Clock
#define I2S_MIC_WS      5   // Word Select (Left/Right Clock)
#define I2S_MIC_SD      6   // Serial Data

// PCM5102A DAC (I2S Output)
#define I2S_DAC_BCK     15  // Bit Clock
#define I2S_DAC_LCK     16  // Word Select / LRCLK
#define I2S_DAC_DIN     17  // Data In

// Controls
#define BUTTON_PIN      0   // Push-to-Talk button (active LOW with internal pull-up)
#define LED_PIN         2   // Status LED (built-in on most boards)

// ============================================================
// AUDIO CONFIGURATION
// ============================================================

#define SAMPLE_RATE         16000   // 16kHz - optimal for speech / Whisper STT
#define BITS_PER_SAMPLE     16
#define CHANNELS            1       // Mono
#define BYTES_PER_SAMPLE    (BITS_PER_SAMPLE / 8)

// I2S read buffer (per DMA read call)
#define I2S_READ_BUF_SIZE   1024

// Maximum recording: 15 seconds (uses PSRAM if available)
// 16000 samples/s * 2 bytes/sample * 15s = 480,000 bytes (~469 KB)
#define MAX_RECORDING_SECS  15
#define MAX_AUDIO_BYTES     (SAMPLE_RATE * BYTES_PER_SAMPLE * MAX_RECORDING_SECS)

// WAV header is 44 bytes
#define WAV_HEADER_SIZE     44

// ============================================================
// GLOBALS
// ============================================================

uint8_t* audioBuffer      = nullptr;  // Main recording buffer
size_t   audioBufferPos   = 0;        // Current write position in buffer
bool     isRecording      = false;
bool     lastButtonState  = HIGH;     // Pull-up = HIGH when not pressed

// I2S port assignments
#define I2S_MIC_PORT    I2S_NUM_0
#define I2S_DAC_PORT    I2S_NUM_1

// ============================================================
// I2S SETUP
// ============================================================

void setupI2SMic() {
    i2s_config_t i2s_config = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,  // INMP441 L/R pin to GND = left channel
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 256,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num   = I2S_MIC_SCK,
        .ws_io_num    = I2S_MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,  // No output on mic port
        .data_in_num  = I2S_MIC_SD
    };

    i2s_driver_install(I2S_MIC_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_MIC_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_MIC_PORT);

    Serial.println("[I2S] Microphone initialized on I2S_NUM_0");
}

void setupI2SDAC() {
    i2s_config_t i2s_config = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 256,
        .use_apll             = false,
        .tx_desc_auto_clear   = true,   // Auto-clear TX buffer on underflow
        .fixed_mclk           = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num   = I2S_DAC_BCK,
        .ws_io_num    = I2S_DAC_LCK,
        .data_out_num = I2S_DAC_DIN,
        .data_in_num  = I2S_PIN_NO_CHANGE   // No input on DAC port
    };

    i2s_driver_install(I2S_DAC_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_DAC_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_DAC_PORT);

    Serial.println("[I2S] DAC initialized on I2S_NUM_1");
}

// ============================================================
// WAV HEADER
// ============================================================

void writeWavHeader(uint8_t* buf, uint32_t dataSize) {
    uint32_t fileSize    = dataSize + WAV_HEADER_SIZE - 8;
    uint32_t byteRate    = SAMPLE_RATE * CHANNELS * BYTES_PER_SAMPLE;
    uint16_t blockAlign  = CHANNELS * BYTES_PER_SAMPLE;

    // RIFF chunk
    buf[0]  = 'R'; buf[1]  = 'I'; buf[2]  = 'F'; buf[3]  = 'F';
    buf[4]  = (uint8_t)(fileSize);
    buf[5]  = (uint8_t)(fileSize >> 8);
    buf[6]  = (uint8_t)(fileSize >> 16);
    buf[7]  = (uint8_t)(fileSize >> 24);
    buf[8]  = 'W'; buf[9]  = 'A'; buf[10] = 'V'; buf[11] = 'E';

    // fmt sub-chunk
    buf[12] = 'f'; buf[13] = 'm'; buf[14] = 't'; buf[15] = ' ';
    buf[16] = 16;  buf[17] = 0;   buf[18] = 0;   buf[19] = 0;   // Sub-chunk size (16 for PCM)
    buf[20] = 1;   buf[21] = 0;                                   // Audio format (1 = PCM)
    buf[22] = (uint8_t)CHANNELS;  buf[23] = 0;                    // Num channels
    buf[24] = (uint8_t)(SAMPLE_RATE);
    buf[25] = (uint8_t)(SAMPLE_RATE >> 8);
    buf[26] = (uint8_t)(SAMPLE_RATE >> 16);
    buf[27] = (uint8_t)(SAMPLE_RATE >> 24);
    buf[28] = (uint8_t)(byteRate);
    buf[29] = (uint8_t)(byteRate >> 8);
    buf[30] = (uint8_t)(byteRate >> 16);
    buf[31] = (uint8_t)(byteRate >> 24);
    buf[32] = (uint8_t)(blockAlign);  buf[33] = 0;                // Block align
    buf[34] = BITS_PER_SAMPLE;        buf[35] = 0;                // Bits per sample

    // data sub-chunk
    buf[36] = 'd'; buf[37] = 'a'; buf[38] = 't'; buf[39] = 'a';
    buf[40] = (uint8_t)(dataSize);
    buf[41] = (uint8_t)(dataSize >> 8);
    buf[42] = (uint8_t)(dataSize >> 16);
    buf[43] = (uint8_t)(dataSize >> 24);
}

// ============================================================
// WiFi
// ============================================================

void connectWiFi() {
    Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] FAILED to connect. Check credentials.");
    }
}

// ============================================================
// RECORDING
// ============================================================

void startRecording() {
    audioBufferPos = WAV_HEADER_SIZE;  // Leave room for WAV header
    isRecording = true;
    digitalWrite(LED_PIN, HIGH);       // LED on while recording
    Serial.println("[REC] Recording started...");
}

void captureAudioChunk() {
    if (!isRecording) return;

    int16_t readBuf[I2S_READ_BUF_SIZE / 2];  // 16-bit samples
    size_t bytesRead = 0;

    esp_err_t result = i2s_read(
        I2S_MIC_PORT,
        readBuf,
        I2S_READ_BUF_SIZE,
        &bytesRead,
        portMAX_DELAY
    );

    if (result == ESP_OK && bytesRead > 0) {
        // Check if we have room in the buffer
        if (audioBufferPos + bytesRead <= MAX_AUDIO_BYTES + WAV_HEADER_SIZE) {
            memcpy(audioBuffer + audioBufferPos, readBuf, bytesRead);
            audioBufferPos += bytesRead;
        } else {
            // Buffer full - stop recording
            Serial.println("[REC] Buffer full, stopping.");
            isRecording = false;
            digitalWrite(LED_PIN, LOW);
        }
    }
}

void stopRecording() {
    isRecording = false;
    digitalWrite(LED_PIN, LOW);

    size_t audioDataSize = audioBufferPos - WAV_HEADER_SIZE;
    float durationSecs = (float)audioDataSize / (SAMPLE_RATE * BYTES_PER_SAMPLE);

    Serial.printf("[REC] Stopped. Recorded %.1f seconds (%d bytes)\n",
                  durationSecs, audioDataSize);

    // Write WAV header at the beginning of the buffer
    writeWavHeader(audioBuffer, audioDataSize);
}

// ============================================================
// HTTP SEND & RECEIVE
// ============================================================

void sendAudioToServer() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[HTTP] WiFi not connected, skipping send.");
        return;
    }

    size_t totalSize = audioBufferPos;  // WAV header + audio data
    Serial.printf("[HTTP] Sending %d bytes to %s\n", totalSize, SERVER_URL);

    // Blink LED rapidly to indicate "processing"
    digitalWrite(LED_PIN, HIGH);

    HTTPClient http;
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "audio/wav");
    http.setTimeout(30000);  // 30 second timeout (AI processing takes time)

    int httpCode = http.POST(audioBuffer, totalSize);

    if (httpCode == 200) {
        Serial.printf("[HTTP] Response received: %d\n", httpCode);

        String contentType = http.header("Content-Type");
        int responseLen = http.getSize();

        if (contentType.startsWith("audio/wav") && responseLen > WAV_HEADER_SIZE) {
            // Response is audio — play it through the speaker
            Serial.printf("[HTTP] Audio response: %d bytes\n", responseLen);

            if (responseLen <= MAX_AUDIO_BYTES + WAV_HEADER_SIZE) {
                WiFiClient* stream = http.getStreamPtr();
                size_t bytesRead = 0;

                while (bytesRead < responseLen) {
                    size_t available = stream->available();
                    if (available) {
                        size_t toRead = min(available, (size_t)(responseLen - bytesRead));
                        size_t got = stream->readBytes(audioBuffer + bytesRead, toRead);
                        bytesRead += got;
                    } else {
                        delay(1);
                    }
                }
                playAudio(bytesRead);
            } else {
                Serial.println("[HTTP] Audio response too large for buffer.");
            }
        } else {
            // Response is JSON or text — print it to serial (e.g. transcription result)
            String body = http.getString();
            Serial.println("[HTTP] Server response:");
            Serial.println(body);
        }
    } else {
        Serial.printf("[HTTP] Error: %d - %s\n", httpCode, http.errorToString(httpCode).c_str());
    }

    http.end();
    digitalWrite(LED_PIN, LOW);
}

// ============================================================
// AUDIO PLAYBACK
// ============================================================

void playAudio(size_t totalBytes) {
    Serial.printf("[PLAY] Playing %d bytes of audio...\n", totalBytes);

    // Skip the WAV header (44 bytes) - we just need the raw PCM data
    size_t offset = WAV_HEADER_SIZE;
    size_t remaining = totalBytes - WAV_HEADER_SIZE;

    // Write audio data to DAC in chunks
    size_t chunkSize = 1024;
    size_t bytesWritten = 0;

    while (offset < totalBytes) {
        size_t toWrite = min(chunkSize, totalBytes - offset);
        size_t written = 0;

        i2s_write(I2S_DAC_PORT, audioBuffer + offset, toWrite, &written, portMAX_DELAY);

        offset += written;
        bytesWritten += written;
    }

    // Flush any remaining data in DMA buffers
    i2s_zero_dma_buffer(I2S_DAC_PORT);

    float durationSecs = (float)(totalBytes - WAV_HEADER_SIZE) / (SAMPLE_RATE * BYTES_PER_SAMPLE);
    Serial.printf("[PLAY] Done. Played %.1f seconds\n", durationSecs);
}

// ============================================================
// SETUP & LOOP
// ============================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=============================");
    Serial.println("  Voice Satellite - ESP32-S3");
    Serial.println("=============================\n");

    // GPIO setup
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Allocate audio buffer (prefer PSRAM for larger buffer)
    audioBuffer = (uint8_t*)ps_malloc(MAX_AUDIO_BYTES + WAV_HEADER_SIZE);
    if (audioBuffer) {
        Serial.printf("[MEM] Allocated %d bytes from PSRAM\n", MAX_AUDIO_BYTES + WAV_HEADER_SIZE);
    } else {
        // Fallback to regular RAM (smaller buffer)
        audioBuffer = (uint8_t*)malloc(MAX_AUDIO_BYTES + WAV_HEADER_SIZE);
        if (audioBuffer) {
            Serial.printf("[MEM] Allocated %d bytes from heap (no PSRAM)\n", MAX_AUDIO_BYTES + WAV_HEADER_SIZE);
        } else {
            Serial.println("[MEM] FATAL: Could not allocate audio buffer!");
            while (true) { delay(1000); }
        }
    }

    // Initialize I2S
    setupI2SMic();
    setupI2SDAC();

    // Connect to WiFi
    connectWiFi();

    Serial.println("\n[READY] Press and hold the button to record.");
    Serial.println("[READY] Release to send audio to server.\n");
}

void loop() {
    bool buttonState = digitalRead(BUTTON_PIN);

    // Button just pressed (HIGH → LOW transition, because INPUT_PULLUP)
    if (lastButtonState == HIGH && buttonState == LOW) {
        startRecording();
    }

    // Button is being held - capture audio
    if (buttonState == LOW && isRecording) {
        captureAudioChunk();
    }

    // Button just released (LOW → HIGH transition)
    if (lastButtonState == LOW && buttonState == HIGH && isRecording) {
        stopRecording();

        // Only send if we captured meaningful audio (> 0.3 seconds)
        size_t audioDataSize = audioBufferPos - WAV_HEADER_SIZE;
        float durationSecs = (float)audioDataSize / (SAMPLE_RATE * BYTES_PER_SAMPLE);

        if (durationSecs > 0.3) {
            sendAudioToServer();
        } else {
            Serial.println("[REC] Too short, discarding.");
        }
    }

    lastButtonState = buttonState;

    // Small delay to debounce button
    delay(10);
}
