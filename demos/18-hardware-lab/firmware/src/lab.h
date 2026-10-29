#pragma once
// Demo 18 - Hardware lab: what its files share.
#include <Arduino.h>

// main.cpp. Any task: text commands ("leds 255 0 0", "head 30 10"...) run by loop(), and
// the last state as JSON, rebuilt by loop() every 150 ms.
void labCommand(const char* line);
size_t labStateJson(char* out, size_t size);

// audio.cpp. The mic and the speaker share one I2S bus (half-duplex): only its task
// touches M5.Mic and M5.Speaker. These only leave it orders.
static constexpr int REC_SECONDS = 3;
void audioStart();
void audioTone(int hz, int ms);
void audioVolume(int volume);       // 0-255
void audioMicLive(bool on);         // the level meter
void audioRecord();                 // REC_SECONDS from the mic into PSRAM
void audioPlay();                   // the recording on the speaker
struct AudioState {
    bool micOn;
    int level, peak;                // 0-100: last 20 ms (or the whole recording)
    const char* status;             // "idle", "recording", "recorded", "playing"
    int volume;
};
AudioState audioState();
// The recording, 16 kHz mono PCM16. Returns the number of samples (0 = none, or recording).
size_t audioRecording(const int16_t** samples);
static constexpr uint32_t AUDIO_RATE = 16000;

// web.cpp: port 80 (page, /state, /cmd, /photo.jpg, /rec.wav) and MJPEG on port 81.
void webStart();
