// Test "hardware-lab": microphone and speaker.
// The CoreS3 has one I2S bus for both: while the mic is open the speaker can't play.
// Like demo 02's audio.cpp, one task owns M5.Mic and M5.Speaker and switches between
// them; calling M5.Speaker.tone() from elsewhere with the mic open panics the robot.
#include "lab.h"
#include "board.h"
#include <M5Unified.h>

static constexpr size_t BLOCK = 320;                       // 20 ms
static constexpr size_t REC_SAMPLES = AUDIO_RATE * REC_SECONDS;

static int16_t* s_rec;                                     // PSRAM
static volatile size_t s_recLen;
static volatile bool s_live, s_recordReq, s_playReq, s_volumeChanged;
static volatile int s_toneHz, s_toneMs, s_volume = 120;    // louder + LEDs + Wi-Fi can brown out the robot
static volatile int s_level, s_peak;
static const char* volatile s_status = "idle";
static volatile bool s_micOn;

// Switching reconfigures the codec and the amplifier over I2C: under the lock.
static void micOn(bool on) {
    if (on == s_micOn) return;
    I2CGuard lock;
    if (on) {
        M5.Speaker.stop();
        M5.Speaker.end();
        M5.Mic.begin();
    } else {
        M5.Mic.end();
        M5.Speaker.begin();
        M5.Speaker.setVolume(s_volume);
    }
    s_micOn = on;
}

// Level in dBFS mapped to 0-100 (-60 dB = 0, 0 dB = 100) and the peak in %.
static void measure(const int16_t* p, size_t n) {
    int64_t sum = 0;
    int peak = 0;
    for (size_t i = 0; i < n; i++) {
        sum += (int32_t)p[i] * p[i];
        peak = max(peak, abs((int)p[i]));
    }
    const float rms = sqrtf((float)sum / n);
    const float db = 20 * log10f(max(rms, 1.0f) / 32768);
    s_level = constrain((int)((db + 60) * 100 / 60), 0, 100);
    s_peak = peak * 100 / 32768;
}

static void audioTask(void*) {
    static int16_t blocks[3][BLOCK];
    int idx = 0, queued = 0;
    for (;;) {
        if (s_recordReq) {
            s_recordReq = false;
            micOn(true);
            s_status = "recording";
            s_recLen = 0;
            M5.Mic.record(s_rec, REC_SAMPLES, AUDIO_RATE);
            while (M5.Mic.isRecording()) vTaskDelay(pdMS_TO_TICKS(20));
            measure(s_rec, REC_SAMPLES);
            s_recLen = REC_SAMPLES;
            s_status = "recorded";
            queued = 0;
            continue;
        }
        micOn(s_live && !s_toneHz && !s_playReq);
        if (s_micOn) {
            // record() queues the buffer and returns; with two in flight, the one
            // queued two calls ago is complete. It paces this loop at 20 ms.
            M5.Mic.record(blocks[idx], BLOCK, AUDIO_RATE);
            const int ready = (idx + 1) % 3;
            idx = ready;
            if (++queued >= 3) measure(blocks[ready], BLOCK);
            continue;
        }
        queued = 0;
        if (s_volumeChanged) {
            s_volumeChanged = false;
            I2CGuard lock;
            M5.Speaker.setVolume(s_volume);
        }
        if (s_toneHz) {
            M5.Speaker.tone(s_toneHz, s_toneMs);
            s_toneHz = 0;
        }
        if (s_playReq) {
            s_playReq = false;
            if (s_recLen) {
                s_status = "playing";
                M5.Speaker.playRaw(s_rec, s_recLen, AUDIO_RATE, false, 1, 0);
                while (M5.Speaker.isPlaying()) vTaskDelay(pdMS_TO_TICKS(20));
                s_status = "recorded";
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void audioStart() {
    s_rec = (int16_t*)heap_caps_malloc(REC_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    {
        I2CGuard lock;
        M5.Speaker.begin();
        M5.Speaker.setVolume(s_volume);
    }
    xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 3, nullptr, 1);   // core 1: audio
}

void audioTone(int hz, int ms) {
    s_toneMs = ms;
    s_toneHz = hz;
}

void audioVolume(int volume) {
    s_volume = volume;
    s_volumeChanged = true;
}

void audioMicLive(bool on) {
    s_live = on;
    if (!on) s_level = s_peak = 0;
}

void audioRecord() { if (s_rec) s_recordReq = true; }
void audioPlay() { s_playReq = true; }

AudioState audioState() { return { s_micOn, s_level, s_peak, s_status, s_volume }; }

size_t audioRecording(const int16_t** samples) {
    *samples = s_rec;
    return s_recLen;
}
